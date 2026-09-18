#include "app/application.h"
#include "app/screens/client_screens.h"

#include "api/gateway_client.h"
#include "auth/secure_storage.h"
#include "auth/session_manager.h"
#include "game/input/input.h"
#include "game/match/match_controller.h"
#include "game/match_lifecycle.h"
#include "game/presentation/match_presentation.h"
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
    duel::game::presentation::MatchPresentation matchPresentation;
    std::unique_ptr<duel::game::match::MatchController> matchController;
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
    bool queueRequestReset = false;
    duel::game::MatchmakingCleanup matchmakingCleanup;
    auto nextPoll = std::chrono::steady_clock::now();
    float accumulator = 0.0F;

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
            // Controller references the transport, so release it before replacing
            // callbacks and transport state from the previous attempt.
            matchController.reset();
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
                matchController = std::make_unique<duel::game::match::MatchController>(
                    session.userId,
                    *network,
                    matchPresentation
                );
                accumulator = 0.0F;
                message = "Waiting for match start...";
                screen = RuntimeState::Playing;
            } else {
                matchmakingCleanup.ConnectionFailed();
                message = "Match connection failed: " + network->Error();
                screen = RuntimeState::Error;
            }
        }

        if (screen == RuntimeState::Playing && matchController) {
            inputSampler.Observe(inputAdapter);
            const auto update = matchController->PollNetwork();
            if (update.started) {
                message = "Match starting";
            }
            for (const auto& event : matchPresentation.DrainEvents()) {
                if (event.type == duel::game::presentation::PresentationEventType::AttackStarted
                    && audioReady && SoundReady(attackSound)) {
                    PlaySound(attackSound);
                } else if (event.type
                        == duel::game::presentation::PresentationEventType::PlayerHit
                    && audioReady && SoundReady(hitSound)) {
                    PlaySound(hitSound);
                }
            }
            if (update.ended) {
                matchmakingCleanup.MatchEnded();
                screen = RuntimeState::Result;
                message = duel::game::MatchFinishReasonLabel(
                    matchController->Model().End()->reason
                );
            } else if (update.disconnected) {
                matchmakingCleanup.ConnectionFailed();
                message = "Network error: " + network->Error();
                screen = RuntimeState::Error;
            }

            accumulator = std::min(accumulator + frameTime, 0.25F);
            while (screen == RuntimeState::Playing && accumulator >= kSimulationStep) {
                accumulator -= kSimulationStep;
                const auto inputFrame = inputSampler.ConsumeFixedTick();
                static_cast<void>(matchController->FixedTick(inputFrame));
            }
            matchController->AdvanceFrame(frameTime);
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
        } else if (screen == RuntimeState::Playing && matchController) {
            screens::DrawMatch(matchController->Model(), matchPresentation, inputBindings);
        } else if (screen == RuntimeState::Result && matchController) {
            const auto action = screens::DrawResult(matchController->Model(), message);
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
