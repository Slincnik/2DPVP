#include "api/gateway_client.h"
#include "game/interpolation.h"
#include "game/match_lifecycle.h"
#include "game/prediction.h"
#include "net/quic_client.h"

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
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr float kSimulationStep = 1.0F / 30.0F;
constexpr float kWorldScale = 0.7F;
constexpr float kAttackFlashDuration = 0.18F;
constexpr float kHitFlashDuration = 0.35F;
constexpr float kDamageTextDuration = 0.65F;
constexpr float kAttackFeedbackCooldown = 0.5F;
constexpr float kTwoPi = 6.28318530718F;
constexpr Vector2 kArenaCenter{640.0F, 360.0F};

using AuthResult = duel::api::Result<duel::api::AuthSession>;
using QueueResult = duel::api::Result<duel::api::QueueStatus>;

enum class Screen {
    Login,
    AuthPending,
    Ready,
    QueuePending,
    Waiting,
    ConnectPending,
    Playing,
    Result,
    Error,
};

struct Endpoint {
    std::string host;
    std::uint16_t port = 0;
};

struct PlayerVisualState {
    int hp = -1;
    int damage = 0;
    float attackFlash = 0.0F;
    float hitFlash = 0.0F;
    float damageText = 0.0F;
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

bool Button(Rectangle bounds, const char* label) {
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawRectangleRec(bounds, hovered ? Color{65, 89, 145, 255} : Color{45, 62, 105, 255});
    DrawRectangleLinesEx(bounds, 1.0F, hovered ? SKYBLUE : GRAY);
    const int width = MeasureText(label, 20);
    DrawText(
        label,
        static_cast<int>(bounds.x + (bounds.width - static_cast<float>(width)) / 2.0F),
        static_cast<int>(bounds.y + 11),
        20,
        RAYWHITE
    );
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void UpdateTextInput(std::string& value, bool active, std::size_t maxLength) {
    if (!active) {
        return;
    }
    for (int character = GetCharPressed(); character > 0; character = GetCharPressed()) {
        if (character >= 32 && character <= 126 && value.size() < maxLength) {
            value.push_back(static_cast<char>(character));
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !value.empty()) {
        value.pop_back();
    }
}

bool TextField(
    Rectangle bounds,
    const char* label,
    std::string& value,
    bool& active,
    bool secret
) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        active = CheckCollisionPointRec(GetMousePosition(), bounds);
    }
    UpdateTextInput(value, active, secret ? 72 : 32);

    DrawText(label, static_cast<int>(bounds.x), static_cast<int>(bounds.y - 24), 18, LIGHTGRAY);
    DrawRectangleRec(bounds, Color{29, 36, 55, 255});
    DrawRectangleLinesEx(bounds, 2.0F, active ? SKYBLUE : GRAY);
    const std::string visible = secret ? std::string(value.size(), '*') : value;
    DrawText(visible.c_str(), static_cast<int>(bounds.x + 10), static_cast<int>(bounds.y + 11), 20, RAYWHITE);
    return active;
}

Vector2 ScreenPosition(const ::game::v1::PlayerState& player) {
    return Vector2{
        kArenaCenter.x + static_cast<float>(player.position_x()) * kWorldScale,
        kArenaCenter.y + static_cast<float>(player.position_y()) * kWorldScale,
    };
}

Color WithAlpha(Color color, float alpha) {
    color.a = static_cast<unsigned char>(std::clamp(alpha, 0.0F, 255.0F));
    return color;
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

Vector2 FacingDirection(const ::game::v1::PlayerState& player) {
    return Vector2{
        static_cast<float>(player.facing_x()),
        static_cast<float>(player.facing_y()),
    };
}

void DrawAttackPulse(const ::game::v1::PlayerState& player, const PlayerVisualState& visual) {
    if (visual.attackFlash <= 0.0F) {
        return;
    }
    const float remaining = std::clamp(visual.attackFlash / kAttackFlashDuration, 0.0F, 1.0F);
    const float progress = 1.0F - remaining;
    const auto position = ScreenPosition(player);
    const auto direction = FacingDirection(player);
    const Vector2 hitboxCenter{
        position.x + direction.x * 40.0F * kWorldScale,
        position.y + direction.y * 40.0F * kWorldScale,
    };
    const bool horizontal = direction.x != 0.0F;
    const Rectangle hitbox{
        hitboxCenter.x - (horizontal ? 40.0F : 45.0F) * kWorldScale,
        hitboxCenter.y - (horizontal ? 45.0F : 40.0F) * kWorldScale,
        (horizontal ? 80.0F : 90.0F) * kWorldScale,
        (horizontal ? 90.0F : 80.0F) * kWorldScale,
    };
    DrawRectangleLinesEx(hitbox, 2.0F, WithAlpha(ORANGE, 210.0F * remaining));
    DrawCircleLines(
        static_cast<int>(position.x),
        static_cast<int>(position.y),
        28.0F + 28.0F * progress,
        WithAlpha(GOLD, 120.0F * remaining)
    );
}

void DrawHitFlash(const ::game::v1::PlayerState& player, const PlayerVisualState& visual) {
    if (visual.hitFlash <= 0.0F && visual.damageText <= 0.0F) {
        return;
    }
    const auto position = ScreenPosition(player);
    if (visual.hitFlash > 0.0F) {
        const float remaining = std::clamp(visual.hitFlash / kHitFlashDuration, 0.0F, 1.0F);
        DrawCircleV(position, 27.0F, WithAlpha(WHITE, 95.0F * remaining));
        DrawCircleLines(
            static_cast<int>(position.x),
            static_cast<int>(position.y),
            31.0F,
            WithAlpha(YELLOW, 220.0F * remaining)
        );
    }
    if (visual.damageText > 0.0F && visual.damage > 0) {
        const float remaining = std::clamp(visual.damageText / kDamageTextDuration, 0.0F, 1.0F);
        const float rise = 28.0F * (1.0F - remaining);
        const std::string text = "-" + std::to_string(visual.damage);
        DrawText(
            text.c_str(),
            static_cast<int>(position.x - 12),
            static_cast<int>(position.y - 70.0F - rise),
            20,
            WithAlpha(ORANGE, 255.0F * remaining)
        );
    }
}

void DrawPlayer(const ::game::v1::PlayerState& player, Color color) {
    const auto position = ScreenPosition(player);
    const auto direction = FacingDirection(player);
    DrawCircleV(position, 22.0F, color);
    DrawLineEx(
        position,
        Vector2{position.x + direction.x * 28.0F, position.y + direction.y * 28.0F},
        3.0F,
        RAYWHITE
    );
    DrawText(
        player.player_id().c_str(),
        static_cast<int>(position.x - 30),
        static_cast<int>(position.y - 45),
        12,
        RAYWHITE
    );
    DrawRectangle(static_cast<int>(position.x - 30), static_cast<int>(position.y + 30), 60, 7, DARKGRAY);
    DrawRectangle(
        static_cast<int>(position.x - 30),
        static_cast<int>(position.y + 30),
        static_cast<int>(60.0F * std::clamp(static_cast<float>(player.hp()) / 100.0F, 0.0F, 1.0F)),
        7,
        color
    );
}

} // namespace

int main() {
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

#ifdef PVP_DUEL_DEFAULT_GATEWAY_URL
    constexpr const char* defaultGateway = PVP_DUEL_DEFAULT_GATEWAY_URL;
#else
    constexpr const char* defaultGateway = "http://localhost:8080";
#endif
    const char* configuredGateway = std::getenv("GATEWAY_URL");
    duel::api::GatewayClient gateway(
        configuredGateway != nullptr ? configuredGateway : defaultGateway);
    std::unique_ptr<duel::net::QuicClient> network;
    duel::api::AuthSession session;
    std::future<AuthResult> authFuture;
    std::future<QueueResult> queueFuture;
    std::future<bool> connectFuture;

    Screen screen = Screen::Login;
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
                ? gateway.Register(loginCopy, passwordCopy)
                : gateway.Login(loginCopy, passwordCopy);
        });
        screen = Screen::AuthPending;
        message = registration ? "Creating account..." : "Signing in...";
    };

    auto beginQueueRequest = [&](bool join) {
        const auto accessToken = session.accessToken;
        const bool resetCompletedMatch = join && matchmakingCleanup.QueueResetRequired();
        queueRequestReset = resetCompletedMatch;
        if (join) {
            // Dispose callbacks and transport state from the previous attempt
            // before its queue entry is removed and a new ticket is requested.
            network.reset();
        }
        queueFuture = std::async(std::launch::async, [&, join, resetCompletedMatch, accessToken] {
            if (resetCompletedMatch) {
                const auto left = gateway.LeaveQueue(accessToken);
                if (!left) {
                    return QueueResult{.error = left.error};
                }
            }
            return join ? gateway.JoinQueue(accessToken) : gateway.QueueStatusFor(accessToken);
        });
        screen = Screen::QueuePending;
        message = join ? "Joining queue..." : "Checking queue...";
    };

    auto handleQueueResult = [&](QueueResult result) {
        if (!result) {
            message = result.error;
            screen = Screen::Error;
            return;
        }
        if (result.value.state == duel::api::QueueState::Waiting) {
            message = "Waiting for opponent...";
            screen = Screen::Waiting;
            nextPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            return;
        }
        if (result.value.state != duel::api::QueueState::Matched) {
            message = "Ready to find a match";
            screen = Screen::Ready;
            return;
        }

        // From this point every exit path must clear the matched Gateway entry
        // before another join, including malformed endpoints and QUIC failures.
        matchmakingCleanup.MatchAccepted();
        const auto endpoint = ParseEndpoint(result.value.serverAddr);
        if (!endpoint) {
            message = "Gateway returned an invalid match server address";
            screen = Screen::Error;
            return;
        }
        network = std::make_unique<duel::net::QuicClient>();
        const auto playerID = session.userId;
        const auto ticket = result.value.matchTicket;
        connectFuture = std::async(std::launch::async, [&, endpoint = *endpoint, playerID, ticket] {
            return network->Connect(endpoint.host, endpoint.port, ticket, playerID);
        });
        screen = Screen::ConnectPending;
        message = "Connecting to match server...";
    };

    while (!WindowShouldClose()) {
        const float frameTime = GetFrameTime();

        if (screen == Screen::AuthPending && FutureReady(authFuture)) {
            auto result = authFuture.get();
            if (!result) {
                message = result.error;
                screen = Screen::Login;
            } else {
                session = std::move(result.value);
                password.clear();
                message = "Welcome, " + session.login;
                screen = Screen::Ready;
            }
        }
        if (screen == Screen::QueuePending && FutureReady(queueFuture)) {
            auto result = queueFuture.get();
            if (queueRequestReset && result) {
                matchmakingCleanup.QueueResetSucceeded();
            }
            queueRequestReset = false;
            handleQueueResult(std::move(result));
        }
        if (screen == Screen::Waiting && std::chrono::steady_clock::now() >= nextPoll) {
            beginQueueRequest(false);
        }
        if (screen == Screen::ConnectPending && FutureReady(connectFuture)) {
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
                screen = Screen::Playing;
            } else {
                matchmakingCleanup.ConnectionFailed();
                message = "Match connection failed: " + network->Error();
                screen = Screen::Error;
            }
        }

        if (screen == Screen::Playing) {
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
                    screen = Screen::Result;
                    message = duel::game::MatchFinishReasonLabel(matchEnd.reason());
                } else if (!network->IsConnected()) {
                    matchmakingCleanup.ConnectionFailed();
                    message = "Network error: " + network->Error();
                    screen = Screen::Error;
                }
            }

            accumulator = std::min(accumulator + frameTime, 0.25F);
            while (screen == Screen::Playing && accumulator >= kSimulationStep) {
                accumulator -= kSimulationStep;
                if (network && network->IsConnected() && matchStarted
                    && world.status() == ::game::v1::MATCH_STATUS_ACTIVE) {
                    const std::int32_t moveX = static_cast<std::int32_t>(IsKeyDown(KEY_D))
                        - static_cast<std::int32_t>(IsKeyDown(KEY_A));
                    const std::int32_t moveY = static_cast<std::int32_t>(IsKeyDown(KEY_S))
                        - static_cast<std::int32_t>(IsKeyDown(KEY_W));
                    const bool attack = IsKeyDown(KEY_SPACE);
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

        if (screen == Screen::Login || screen == Screen::AuthPending) {
            DrawText("2D PvP Duel", 500, 100, 36, RAYWHITE);
            TextField(Rectangle{440, 220, 400, 48}, "Login", login, loginActive, false);
            TextField(Rectangle{440, 310, 400, 48}, "Password", password, passwordActive, true);
            if (screen == Screen::Login) {
                if (Button(Rectangle{440, 390, 190, 48}, "Login")) {
                    beginAuth(false);
                }
                if (Button(Rectangle{650, 390, 190, 48}, "Register")) {
                    beginAuth(true);
                }
            }
            DrawText(message.c_str(), 440, 470, 18, LIGHTGRAY);
        } else if (screen == Screen::Playing) {
            DrawRectangleLines(290, 150, 700, 420, GRAY);
            DrawText("WASD: move | SPACE: attack", 24, 20, 20, LIGHTGRAY);
            std::string clockText = "Waiting for match start";
            if (world.status() == ::game::v1::MATCH_STATUS_COUNTDOWN) {
                clockText = "Starts in " + std::to_string(duel::game::TicksToDisplaySeconds(
                    world.countdown_ticks_remaining(), serverTickRate));
            } else if (world.status() == ::game::v1::MATCH_STATUS_ACTIVE) {
                clockText = "Time: " + std::to_string(duel::game::TicksToDisplaySeconds(
                    world.match_ticks_remaining(), serverTickRate));
            }
            DrawText(clockText.c_str(), 560, 90, 28, GOLD);
            for (int index = 0; index < world.players_size(); ++index) {
                auto player = world.players(index);
                const bool isLocal = player.player_id() == session.userId;
                const std::string hp = std::string(isLocal ? "Your HP: " : "Opponent HP: ")
                    + std::to_string(player.hp());
                DrawText(hp.c_str(), isLocal ? 24 : 1060, 50, 18, isLocal ? SKYBLUE : RED);
                if (isLocal && world.status() == ::game::v1::MATCH_STATUS_ACTIVE) {
                    const auto visual = prediction.VisualPosition();
                    player.set_position_x(visual.x);
                    player.set_position_y(visual.y);
                } else if (!isLocal && world.status() == ::game::v1::MATCH_STATUS_ACTIVE) {
                    const auto interpolated = opponentInterpolation.Sample();
                    player.set_position_x(static_cast<std::int32_t>(std::lround(interpolated.x)));
                    player.set_position_y(static_cast<std::int32_t>(std::lround(interpolated.y)));
                }
                const auto visualIt = playerVisuals.find(player.player_id());
                const PlayerVisualState visual = visualIt != playerVisuals.end()
                    ? visualIt->second
                    : PlayerVisualState{};
                DrawAttackPulse(player, visual);
                DrawPlayer(player, isLocal ? SKYBLUE : RED);
                DrawHitFlash(player, visual);
            }
        } else if (screen == Screen::Result) {
            const std::string result = duel::game::MatchResultLabel(matchEnd, session.userId);
            DrawText(result.c_str(), 540, 150, 40, GOLD);
            DrawText(message.c_str(), 560, 210, 24, LIGHTGRAY);
            for (int index = 0; index < world.players_size(); ++index) {
                const auto& player = world.players(index);
                const std::string hp = player.player_id() + ": " + std::to_string(player.hp()) + " HP";
                DrawText(hp.c_str(), 520, 260 + 32 * index, 20,
                    player.player_id() == session.userId ? SKYBLUE : RED);
            }
            if (Button(Rectangle{390, 370, 240, 52}, "Back to menu")) {
                screen = Screen::Ready;
                message = "Ready to find a match";
            }
            if (Button(Rectangle{650, 370, 240, 52}, "Find another")) {
                beginQueueRequest(true);
            }
        } else {
            DrawText("2D PvP Duel", 500, 120, 36, RAYWHITE);
            DrawText(message.c_str(), 420, 250, 22, LIGHTGRAY);
            if (screen == Screen::Ready && Button(Rectangle{520, 330, 240, 52}, "Find match")) {
                beginQueueRequest(true);
            }
            if (screen == Screen::Waiting) {
                DrawCircleSector(Vector2{640, 360}, 30, 0, 280, 32, SKYBLUE);
            }
            if (screen == Screen::Error && Button(Rectangle{520, 330, 240, 52}, "Back")) {
                screen = session.accessToken.empty() ? Screen::Login : Screen::Ready;
                message = session.accessToken.empty() ? "Login or create an account" : "Ready to find a match";
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
