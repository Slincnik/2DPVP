#include "api/gateway_client.h"
#include "game/interpolation.h"
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
#include <utility>

namespace {

constexpr float kSimulationStep = 1.0F / 30.0F;
constexpr float kWorldScale = 0.7F;
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

void DrawPlayer(const ::game::v1::PlayerState& player, Color color) {
    const auto position = ScreenPosition(player);
    DrawCircleV(position, 22.0F, color);
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
        static_cast<int>(60.0F * static_cast<float>(player.hp()) / 100.0F),
        7,
        color
    );
}

} // namespace

int main() {
    InitWindow(1280, 720, "2D PvP Duel");
    SetTargetFPS(144);

    const char* configuredGateway = std::getenv("GATEWAY_URL");
    duel::api::GatewayClient gateway(
        configuredGateway != nullptr ? configuredGateway : "http://localhost:8080");
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
    bool receivedSnapshot = false;
    auto nextPoll = std::chrono::steady_clock::now();

    ::game::v1::WorldSnapshot world;
    duel::game::Prediction prediction;
    duel::game::InterpolationBuffer opponentInterpolation;
    std::uint32_t inputTick = 0;
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
                ? gateway.Register(loginCopy, passwordCopy)
                : gateway.Login(loginCopy, passwordCopy);
        });
        screen = Screen::AuthPending;
        message = registration ? "Creating account..." : "Signing in...";
    };

    auto beginQueueRequest = [&](bool join) {
        const auto accessToken = session.accessToken;
        queueFuture = std::async(std::launch::async, [&, join, accessToken] {
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
            handleQueueResult(queueFuture.get());
        }
        if (screen == Screen::Waiting && std::chrono::steady_clock::now() >= nextPoll) {
            beginQueueRequest(false);
        }
        if (screen == Screen::ConnectPending && FutureReady(connectFuture)) {
            if (connectFuture.get()) {
                world.Clear();
                prediction = duel::game::Prediction{};
                opponentInterpolation = duel::game::InterpolationBuffer{};
                inputTick = 0;
                accumulator = 0.0F;
                receivedSnapshot = false;
                message = "QUIC connected";
                screen = Screen::Playing;
            } else {
                message = "Match connection failed: " + network->Error();
                screen = Screen::Error;
            }
        }

        if (screen == Screen::Playing) {
            accumulator = std::min(accumulator + frameTime, 0.25F);
            while (accumulator >= kSimulationStep) {
                accumulator -= kSimulationStep;
                if (network && network->IsConnected()) {
                    const std::int32_t moveX = static_cast<std::int32_t>(IsKeyDown(KEY_D))
                        - static_cast<std::int32_t>(IsKeyDown(KEY_A));
                    const std::int32_t moveY = static_cast<std::int32_t>(IsKeyDown(KEY_S))
                        - static_cast<std::int32_t>(IsKeyDown(KEY_W));
                    ++inputTick;
                    prediction.ApplyInput(inputTick, moveX, moveY);
                    network->SendInput(inputTick, moveX, moveY, IsKeyDown(KEY_SPACE));
                }
            }

            if (network) {
                if (auto snapshot = network->PollSnapshot()) {
                    world = std::move(*snapshot);
                    for (const auto& player : world.players()) {
                        if (player.player_id() == session.userId) {
                            prediction.Reconcile(player);
                        } else {
                            opponentInterpolation.Push(
                                world.server_tick(), player.position_x(), player.position_y());
                        }
                    }
                    message = "Server tick " + std::to_string(world.server_tick());
                    receivedSnapshot = true;
                } else if (!network->IsConnected() && receivedSnapshot
                    && world.status() != ::game::v1::MATCH_STATUS_FINISHED) {
                    message = "Network error: " + network->Error();
                    screen = Screen::Error;
                }
            }
            prediction.AdvanceVisual(std::min(frameTime * 12.0F, 1.0F));
            opponentInterpolation.Advance(frameTime);
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
            DrawText(message.c_str(), 24, 50, 18, GREEN);
            for (int index = 0; index < world.players_size(); ++index) {
                auto player = world.players(index);
                if (player.player_id() == session.userId) {
                    const auto visual = prediction.VisualPosition();
                    player.set_position_x(visual.x);
                    player.set_position_y(visual.y);
                } else {
                    const auto interpolated = opponentInterpolation.Sample();
                    player.set_position_x(static_cast<std::int32_t>(std::lround(interpolated.x)));
                    player.set_position_y(static_cast<std::int32_t>(std::lround(interpolated.y)));
                }
                DrawPlayer(player, player.player_id() == session.userId ? SKYBLUE : RED);
            }
            if (world.status() == ::game::v1::MATCH_STATUS_FINISHED) {
                const std::string result = world.winner_player_id().empty()
                    ? "DRAW"
                    : world.winner_player_id() == session.userId ? "YOU WIN" : "YOU LOSE";
                DrawText(result.c_str(), 520, 90, 36, GOLD);
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

    CloseWindow();
    return 0;
}
