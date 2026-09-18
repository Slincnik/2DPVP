#include "app/application.h"
#include "app/screens/client_screens.h"

#include "api/gateway_client.h"
#include "auth/secure_storage.h"
#include "auth/session_manager.h"
#include "game/input/input.h"
#include "game/interpolation.h"
#include "game/match_lifecycle.h"
#include "game/prediction.h"
#include "net/quic_client.h"
#include "platform/raylib_input.h"
#include "platform/settings_store.h"

#include "raylib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr float kSimulationStep = 1.0F / 30.0F;
constexpr float kAttackFlashDuration = 0.18F;
constexpr float kHitFlashDuration = 0.35F;
constexpr float kDamageTextDuration = 0.65F;
constexpr float kAttackFeedbackCooldown = 0.5F;
constexpr float kTwoPi = 6.28318530718F;

using AuthResult = duel::api::Result<duel::api::AuthSession>;
using QueueResult = duel::api::Result<duel::api::QueueStatus>;

enum class RuntimeState {
    Login,
    Restoring,
    AuthPending,
    Ready,
    QueuePending,
    Waiting,
    ConnectPending,
    Playing,
    Result,
    Settings,
    Error,
};

struct Endpoint {
    std::string host;
    std::uint16_t port = 0;
};

using duel::app::screens::PlayerVisualState;

std::optional<Endpoint> ParseEndpoint(const std::string& address) {
    const auto separator = address.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= address.size()) {
        return std::nullopt;
    }
    try {
        const auto port = std::stoi(address.substr(separator + 1));
        if (port <= 0 || port > 65535) {
            return std::nullopt;
        }
        return Endpoint{address.substr(0, separator), static_cast<std::uint16_t>(port)};
    } catch (...) {
        return std::nullopt;
    }
}

bool FutureReady(const auto& future) {
    return future.valid()
        && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool SoundReady(const Sound& sound) {
    return sound.stream.buffer != nullptr;
}

Sound MakeTone(float frequency, float durationSeconds, float volume) {
    constexpr unsigned int sampleRate = 44100;
    const auto frameCount = static_cast<unsigned int>(durationSeconds * static_cast<float>(sampleRate));
    std::vector<std::int16_t> samples(frameCount);
    for (unsigned int index = 0; index < frameCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(sampleRate);
        const float fade = 1.0F - static_cast<float>(index) / static_cast<float>(frameCount);
        samples[index] = static_cast<std::int16_t>(
            32767.0F * volume * fade * std::sin(kTwoPi * frequency * t)
        );
    }

    Wave wave{};
    wave.frameCount = frameCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = samples.data();
    return LoadSoundFromWave(wave);
}


} // namespace

int duel::app::Application::Run() {
    InitWindow(1280, 720, "2D PvP Duel");
    SetTargetFPS(144);
    InitAudioDevice();
    const bool audioReady = IsAudioDeviceReady();
    Sound attackSound{};
    Sound hitSound{};
    if (audioReady) {
        attackSound = MakeTone(880.0F, 0.08F, 0.18F);
        hitSound = MakeTone(180.0F, 0.12F, 0.35F);
        if (SoundReady(attackSound)) {
            SetSoundVolume(attackSound, 0.45F);
        }
        if (SoundReady(hitSound)) {
            SetSoundVolume(hitSound, 0.6F);
        }
    }

    duel::api::GatewayClient gateway(gatewayUrl_);
    auto secureStorage = duel::auth::CreateSecureStorage();
    duel::auth::SessionManager sessionManager(gateway, *secureStorage, gatewayUrl_);
    std::unique_ptr<duel::net::QuicClient> network;
    duel::api::AuthSession session;
    std::future<AuthResult> authFuture;
    std::future<duel::api::Result<bool>> restoreFuture;
    std::future<QueueResult> queueFuture;
    std::future<bool> connectFuture;
    std::future<duel::platform::SettingsLoadResult> settingsLoadFuture;
    std::future<duel::platform::SettingsSaveResult> settingsSaveFuture;

    duel::platform::SettingsStore settingsStore(duel::platform::DefaultSettingsPath());
    auto inputBindings = duel::game::input::InputBindings::Defaults();
    auto settingsDraft = inputBindings;
    std::optional<duel::game::input::InputBindings> pendingBindings;
    duel::platform::RaylibInputAdapter inputAdapter;
    duel::game::input::InputSampler inputSampler(inputBindings);
    std::optional<duel::game::input::Action> captureAction;
    std::string settingsMessage = "Loading settings...";
    bool settingsLoaded = false;

    RuntimeState screen = RuntimeState::Restoring;
    restoreFuture = std::async(std::launch::async, [&] { return sessionManager.Restore(); });
    settingsLoadFuture = std::async(std::launch::async, [settingsStore] {
        return settingsStore.Load();
    });
    std::string login;
    std::string password;
    std::string message = "Login or create an account";
    bool loginActive = true;
    bool passwordActive = false;
    bool matchStarted = false;
    bool queueRequestReset = false;
    duel::game::MatchmakingCleanup matchmakingCleanup;
    std::uint32_t serverTickRate = 30;
    auto nextPoll = std::chrono::steady_clock::now();

    ::game::v1::WorldSnapshot world;
    ::game::v1::MatchEnd matchEnd;
    duel::game::Prediction prediction;
    duel::game::InterpolationBuffer opponentInterpolation;
    std::unordered_map<std::string, PlayerVisualState> playerVisuals;
    std::uint32_t inputTick = 0;
    float accumulator = 0.0F;
    float localAttackSoundCooldown = 0.0F;

    auto resetPlayerVisuals = [&] {
        playerVisuals.clear();
    };

    auto seedPlayerVisuals = [&](const ::game::v1::WorldSnapshot& snapshot) {
        for (const auto& player : snapshot.players()) {
            playerVisuals[player.player_id()].hp = player.hp();
        }
    };

    auto updatePlayerVisuals = [&](const ::game::v1::WorldSnapshot& snapshot) {
        // Create all entries before retaining references into unordered_map: an
        // insertion during hit processing could otherwise rehash and invalidate
        // the current player's reference.
        for (const auto& player : snapshot.players()) {
            playerVisuals.try_emplace(player.player_id());
        }
        for (const auto& player : snapshot.players()) {
            auto& visual = playerVisuals.at(player.player_id());
            if (visual.hp >= 0 && player.hp() < visual.hp) {
                visual.damage = visual.hp - player.hp();
                visual.hitFlash = kHitFlashDuration;
                visual.damageText = kDamageTextDuration;
                if (audioReady && SoundReady(hitSound)) {
                    PlaySound(hitSound);
                }
                for (const auto& attacker : snapshot.players()) {
                    if (attacker.player_id() != player.player_id()) {
                        auto& attackerVisual = playerVisuals.at(attacker.player_id());
                        attackerVisual.attackFlash = std::max(
                            attackerVisual.attackFlash,
                            kAttackFlashDuration
                        );
                    }
                }
            }
            visual.hp = player.hp();
        }
    };

    auto tickPlayerVisuals = [&](float deltaSeconds) {
        for (auto& entry : playerVisuals) {
            auto& visual = entry.second;
            visual.attackFlash = std::max(0.0F, visual.attackFlash - deltaSeconds);
            visual.hitFlash = std::max(0.0F, visual.hitFlash - deltaSeconds);
            visual.damageText = std::max(0.0F, visual.damageText - deltaSeconds);
        }
    };

    auto beginAuth = [&](bool registration) {
        if (login.empty() || password.empty()) {
            message = "Enter login and password";
            return;
        }
        const auto loginCopy = login;
        const auto passwordCopy = password;
        authFuture = std::async(std::launch::async, [&, registration, loginCopy, passwordCopy] {
            return registration
                ? sessionManager.Register(loginCopy, passwordCopy)
                : sessionManager.Login(loginCopy, passwordCopy);
        });
        screen = RuntimeState::AuthPending;
        message = registration ? "Creating account..." : "Signing in...";
    };

    auto beginQueueRequest = [&](bool join) {
        const bool resetCompletedMatch = join && matchmakingCleanup.QueueResetRequired();
        queueRequestReset = resetCompletedMatch;
        if (join) {
            // Dispose callbacks and transport state from the previous attempt
            // before its queue entry is removed and a new ticket is requested.
            network.reset();
        }
        queueFuture = std::async(std::launch::async, [&, join, resetCompletedMatch] {
            if (resetCompletedMatch) {
                const auto left = sessionManager.LeaveQueue();
                if (!left) {
                    return QueueResult{.error = left.error};
                }
            }
            return join ? sessionManager.JoinQueue() : sessionManager.QueueStatus();
        });
        screen = RuntimeState::QueuePending;
        message = join ? "Joining queue..." : "Checking queue...";
    };

    auto beginSettingsSave = [&] {
        pendingBindings = settingsDraft;
        const duel::platform::Settings settings{
            .schemaVersion = duel::platform::kSettingsSchemaVersion,
            .bindings = pendingBindings->All(),
        };
        settingsSaveFuture = std::async(std::launch::async, [settingsStore, settings] {
            return settingsStore.Save(settings);
        });
        settingsMessage = "Saving settings...";
    };

    auto handleQueueResult = [&](QueueResult result) {
        if (!result) {
            message = result.error;
            screen = RuntimeState::Error;
            return;
        }
        if (result.value.state == duel::api::QueueState::Waiting) {
            message = "Waiting for opponent...";
            screen = RuntimeState::Waiting;
            nextPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            return;
        }
        if (result.value.state != duel::api::QueueState::Matched) {
            message = "Ready to find a match";
            screen = RuntimeState::Ready;
            return;
        }

        // From this point every exit path must clear the matched Gateway entry
        // before another join, including malformed endpoints and QUIC failures.
        matchmakingCleanup.MatchAccepted();
        const auto endpoint = ParseEndpoint(result.value.serverAddr);
        if (!endpoint) {
            message = "Gateway returned an invalid match server address";
            screen = RuntimeState::Error;
            return;
        }
        network = std::make_unique<duel::net::QuicClient>();
        const auto playerID = session.userId;
        const auto ticket = result.value.matchTicket;
        connectFuture = std::async(std::launch::async, [&, endpoint = *endpoint, playerID, ticket] {
            return network->Connect(endpoint.host, endpoint.port, ticket, playerID);
        });
        screen = RuntimeState::ConnectPending;
        message = "Connecting to match server...";
    };

    while (!WindowShouldClose()) {
        const float frameTime = GetFrameTime();

        if (FutureReady(settingsLoadFuture)) {
            auto result = settingsLoadFuture.get();
            inputBindings = duel::game::input::InputBindings(std::move(result.settings.bindings));
            settingsDraft = inputBindings;
            inputSampler.SetBindings(inputBindings);
            settingsMessage = result.warning.empty()
                ? "Select a control to rebind"
                : std::move(result.warning);
            settingsLoaded = true;
        }
        if (FutureReady(settingsSaveFuture)) {
            const auto result = settingsSaveFuture.get();
            if (result && pendingBindings) {
                inputBindings = *pendingBindings;
                inputSampler.SetBindings(inputBindings);
                settingsMessage = "Settings saved";
            } else {
                settingsMessage = "Settings save failed: " + result.error;
            }
            pendingBindings.reset();
        }
        if (screen == RuntimeState::Restoring && FutureReady(restoreFuture)) {
            const auto result = restoreFuture.get();
            if (!result) {
                message = "Saved session could not be restored: " + result.error;
                screen = RuntimeState::Login;
            } else if (result.value) {
                const auto restored = sessionManager.CurrentSession();
                if (restored) {
                    session = *restored;
                    message = "Welcome back, " + session.login;
                    screen = RuntimeState::Ready;
                } else {
                    message = "Login or create an account";
                    screen = RuntimeState::Login;
                }
            } else {
                message = "Login or create an account";
                screen = RuntimeState::Login;
            }
        }
        if (screen == RuntimeState::AuthPending && FutureReady(authFuture)) {
            auto result = authFuture.get();
            if (!result) {
                message = result.error;
                screen = RuntimeState::Login;
            } else {
                session = std::move(result.value);
                password.clear();
                message = "Welcome, " + session.login;
                const auto warning = sessionManager.TakeWarning();
                if (!warning.empty()) {
                    message += "\n" + warning;
                }
                screen = RuntimeState::Ready;
            }
        }
        if (screen == RuntimeState::QueuePending && FutureReady(queueFuture)) {
            auto result = queueFuture.get();
            if (queueRequestReset && result) {
                matchmakingCleanup.QueueResetSucceeded();
            }
            queueRequestReset = false;
            handleQueueResult(std::move(result));
        }
        if (screen == RuntimeState::Waiting && std::chrono::steady_clock::now() >= nextPoll) {
            beginQueueRequest(false);
        }
        if (screen == RuntimeState::ConnectPending && FutureReady(connectFuture)) {
            if (connectFuture.get()) {
                world.Clear();
                prediction = duel::game::Prediction{};
                opponentInterpolation = duel::game::InterpolationBuffer{};
                resetPlayerVisuals();
                inputTick = 0;
                accumulator = 0.0F;
                localAttackSoundCooldown = 0.0F;
                matchStarted = false;
                matchEnd.Clear();
                serverTickRate = 30;
                message = "Waiting for match start...";
                screen = RuntimeState::Playing;
            } else {
                matchmakingCleanup.ConnectionFailed();
                message = "Match connection failed: " + network->Error();
                screen = RuntimeState::Error;
            }
        }

        if (screen == RuntimeState::Playing) {
            inputSampler.Observe(inputAdapter);
            if (network) {
                if (auto start = network->PollMatchStart()) {
                    matchStarted = true;
                    serverTickRate = start->tick_rate();
                    world = start->initial_snapshot();
                    seedPlayerVisuals(world);
                    message = "Match starting";
                }
                if (auto snapshot = network->PollSnapshot()) {
                    updatePlayerVisuals(*snapshot);
                    world = std::move(*snapshot);
                    for (const auto& player : world.players()) {
                        if (player.player_id() == session.userId) {
                            prediction.Reconcile(player);
                        } else {
                            opponentInterpolation.Push(
                                world.server_tick(), player.position_x(), player.position_y());
                        }
                    }
                }
                if (auto end = network->PollMatchEnd()) {
                    matchEnd = std::move(*end);
                    updatePlayerVisuals(matchEnd.final_snapshot());
                    world = matchEnd.final_snapshot();
                    matchmakingCleanup.MatchEnded();
                    screen = RuntimeState::Result;
                    message = duel::game::MatchFinishReasonLabel(matchEnd.reason());
                } else if (!network->IsConnected()) {
                    matchmakingCleanup.ConnectionFailed();
                    message = "Network error: " + network->Error();
                    screen = RuntimeState::Error;
                }
            }

            accumulator = std::min(accumulator + frameTime, 0.25F);
            while (screen == RuntimeState::Playing && accumulator >= kSimulationStep) {
                accumulator -= kSimulationStep;
                const auto inputFrame = inputSampler.ConsumeFixedTick();
                if (network && network->IsConnected() && matchStarted
                    && world.status() == ::game::v1::MATCH_STATUS_ACTIVE) {
                    const auto moveX = static_cast<std::int32_t>(inputFrame.moveX);
                    const auto moveY = static_cast<std::int32_t>(inputFrame.moveY);
                    const auto attackCode = inputBindings.InputFor(
                        duel::game::input::Action::LightAttack
                    );
                    // Compatibility with the v1 protocol: LightAttack remains a held
                    // boolean until action commands land in migration stage 4. Dash
                    // edges are buffered by InputSampler but intentionally not sent.
                    const bool attack = attackCode && inputAdapter.IsDown(*attackCode);
                    ++inputTick;
                    prediction.ApplyInput(inputTick, moveX, moveY);
                    if (attack) {
                        if (localAttackSoundCooldown <= 0.0F) {
                            playerVisuals[session.userId].attackFlash = kAttackFlashDuration;
                            if (audioReady && SoundReady(attackSound)) {
                                PlaySound(attackSound);
                            }
                            // The server applies held attacks every 15 ticks (0.5 s).
                            localAttackSoundCooldown = kAttackFeedbackCooldown;
                        }
                    }
                    network->SendInput(inputTick, moveX, moveY, attack);
                }
            }
            prediction.AdvanceVisual(std::min(frameTime * 12.0F, 1.0F));
            opponentInterpolation.Advance(frameTime);
            tickPlayerVisuals(frameTime);
            localAttackSoundCooldown = std::max(0.0F, localAttackSoundCooldown - frameTime);
        }

        BeginDrawing();
        ClearBackground(Color{20, 24, 35, 255});

        if (screen == RuntimeState::Login || screen == RuntimeState::AuthPending) {
            const auto action = screens::DrawLogin(
                login,
                password,
                loginActive,
                passwordActive,
                message,
                screen == RuntimeState::Login
            );
            if (action == screens::LoginAction::Login) {
                beginAuth(false);
            } else if (action == screens::LoginAction::Register) {
                beginAuth(true);
            }
        } else if (screen == RuntimeState::Playing) {
            screens::DrawMatch(
                world,
                session.userId,
                serverTickRate,
                prediction,
                opponentInterpolation,
                playerVisuals,
                inputBindings
            );
        } else if (screen == RuntimeState::Result) {
            const auto action = screens::DrawResult(matchEnd, world, session.userId, message);
            if (action == screens::ResultAction::BackToMenu) {
                screen = RuntimeState::Ready;
                message = "Ready to find a match";
            } else if (action == screens::ResultAction::FindAnother) {
                beginQueueRequest(true);
            }
        } else if (screen == RuntimeState::Settings) {
            const auto settingsEvent = screens::DrawSettings(
                settingsDraft,
                captureAction,
                settingsMessage,
                settingsSaveFuture.valid()
            );
            if (settingsEvent.captureAction) {
                captureAction = settingsEvent.captureAction;
                settingsMessage = "Press a key for the selected action";
            }
            if (captureAction) {
                if (const auto inputCode = inputAdapter.PressedInputCode()) {
                    std::string_view validationError;
                    if (settingsDraft.Rebind(*captureAction, *inputCode, &validationError)) {
                        captureAction.reset();
                        settingsMessage = "Binding changed; save to apply";
                    } else {
                        settingsMessage = std::string(validationError);
                    }
                }
            }
            if (settingsEvent.action == screens::SettingsAction::Save
                && !settingsSaveFuture.valid()) {
                beginSettingsSave();
            } else if (settingsEvent.action == screens::SettingsAction::ResetDefaults
                && !settingsSaveFuture.valid()) {
                settingsDraft = duel::game::input::InputBindings::Defaults();
                captureAction.reset();
                beginSettingsSave();
            } else if (settingsEvent.action == screens::SettingsAction::Back) {
                captureAction.reset();
                screen = RuntimeState::Ready;
                message = "Ready to find a match";
            }
        } else {
            const auto action = screens::DrawMainMenu(
                message,
                screen == RuntimeState::Ready,
                screen == RuntimeState::Waiting,
                screen == RuntimeState::Error,
                settingsLoaded
            );
            if (action == screens::MainMenuAction::FindMatch) {
                beginQueueRequest(true);
            } else if (action == screens::MainMenuAction::Settings) {
                settingsDraft = inputBindings;
                captureAction.reset();
                settingsMessage = "Select a control to rebind";
                screen = RuntimeState::Settings;
            } else if (action == screens::MainMenuAction::Logout) {
                const auto logout = sessionManager.Logout();
                session = {};
                screen = RuntimeState::Login;
                message = logout ? "Logged out" : "Logged out locally: " + logout.error;
            } else if (action == screens::MainMenuAction::Back) {
                screen = session.accessToken.empty() ? RuntimeState::Login : RuntimeState::Ready;
                message = session.accessToken.empty()
                    ? "Login or create an account"
                    : "Ready to find a match";
            }
        }

        EndDrawing();
    }

    if (audioReady) {
        if (SoundReady(attackSound)) {
            UnloadSound(attackSound);
        }
        if (SoundReady(hitSound)) {
            UnloadSound(hitSound);
        }
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
