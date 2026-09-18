#include "app/screens/client_screens.h"

#include "game/arena/arena_catalog.h"
#include "game/match_lifecycle.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace duel::app::screens {
namespace {

constexpr float kWorldScale = 0.7F;
constexpr float kAttackFlashDuration = 0.18F;
constexpr float kHitFlashDuration = 0.35F;
constexpr float kDamageTextDuration = 0.65F;
constexpr Vector2 kArenaCenter{640.0F, 360.0F};

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

void TextField(Rectangle bounds, const char* label, std::string& value, bool& active, bool secret) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        active = CheckCollisionPointRec(GetMousePosition(), bounds);
    }
    UpdateTextInput(value, active, secret ? 72 : 32);

    DrawText(label, static_cast<int>(bounds.x), static_cast<int>(bounds.y - 24), 18, LIGHTGRAY);
    DrawRectangleRec(bounds, Color{29, 36, 55, 255});
    DrawRectangleLinesEx(bounds, 2.0F, active ? SKYBLUE : GRAY);
    const std::string visible = secret ? std::string(value.size(), '*') : value;
    DrawText(visible.c_str(), static_cast<int>(bounds.x + 10), static_cast<int>(bounds.y + 11), 20, RAYWHITE);
}

Vector2 ScreenPosition(const protocol::PlayerState& player) {
    return Vector2{
        kArenaCenter.x + static_cast<float>(player.positionX) * kWorldScale,
        kArenaCenter.y + static_cast<float>(player.positionY) * kWorldScale,
    };
}

Color ToColor(game::arena::RgbColor color, unsigned char alpha = 255) {
    return Color{color.red, color.green, color.blue, alpha};
}

Color WithAlpha(Color color, float alpha) {
    color.a = static_cast<unsigned char>(std::clamp(alpha, 0.0F, 255.0F));
    return color;
}

void DrawArenaScene(const game::arena::ArenaSelection& selection) {
    const auto& scene = selection.scene;
    DrawRectangleGradientV(
        0,
        0,
        GetScreenWidth(),
        GetScreenHeight(),
        ToColor(scene.palette.backgroundTop),
        ToColor(scene.palette.backgroundBottom)
    );

    for (const auto& decoration : scene.decorations) {
        const auto accent = ToColor(scene.palette.accent, 145);
        switch (decoration.kind) {
        case game::arena::DecorationKind::Skyline:
            DrawRectangle(
                static_cast<int>(decoration.x - decoration.size / 2.0F),
                static_cast<int>(decoration.y - decoration.size),
                static_cast<int>(decoration.size),
                static_cast<int>(decoration.size),
                ToColor(scene.palette.floor, 190)
            );
            break;
        case game::arena::DecorationKind::LightColumn:
            DrawLineEx(
                Vector2{decoration.x, decoration.y},
                Vector2{decoration.x, decoration.y + decoration.size},
                5.0F,
                accent
            );
            break;
        case game::arena::DecorationKind::EmberVent:
            DrawCircleV(Vector2{decoration.x, decoration.y}, decoration.size, accent);
            DrawCircleLines(
                static_cast<int>(decoration.x),
                static_cast<int>(decoration.y),
                decoration.size + 9.0F,
                ToColor(scene.palette.border, 180)
            );
            break;
        }
    }

    const Rectangle bounds{
        scene.visualBounds.x,
        scene.visualBounds.y,
        scene.visualBounds.width,
        scene.visualBounds.height,
    };
    DrawRectangleRec(bounds, ToColor(scene.palette.floor, 225));
    DrawRectangleLinesEx(bounds, 2.0F, ToColor(scene.palette.border));
    DrawText(scene.displayName.c_str(), static_cast<int>(bounds.x), 116, 18,
        ToColor(scene.palette.accent));
    if (selection.usedFallback) {
        DrawText("Unknown arena: using safe visual fallback", 430, 688, 16, LIGHTGRAY);
    }
}

Vector2 FacingDirection(const protocol::PlayerState& player) {
    return Vector2{
        static_cast<float>(player.facingX),
        static_cast<float>(player.facingY),
    };
}

void DrawAttackPulse(
    const protocol::PlayerState& player,
    const game::presentation::PlayerPresentation& visual
) {
    const bool authoritativeAttack =
        visual.animation == game::presentation::AnimationState::AttackWindup
        || visual.animation == game::presentation::AnimationState::AttackActive;
    if (visual.attackFlashSeconds <= 0.0F && !authoritativeAttack) {
        return;
    }
    const float remaining = visual.attackFlashSeconds > 0.0F
        ? std::clamp(visual.attackFlashSeconds / kAttackFlashDuration, 0.0F, 1.0F)
        : 1.0F;
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

void DrawHitFlash(
    const protocol::PlayerState& player,
    const game::presentation::PlayerPresentation& visual
) {
    const bool authoritativeHit = visual.animation == game::presentation::AnimationState::Hit;
    if (visual.hitFlashSeconds <= 0.0F && visual.damageTextSeconds <= 0.0F
        && !authoritativeHit) {
        return;
    }
    const auto position = ScreenPosition(player);
    if (visual.hitFlashSeconds > 0.0F || authoritativeHit) {
        const float remaining = visual.hitFlashSeconds > 0.0F
            ? std::clamp(visual.hitFlashSeconds / kHitFlashDuration, 0.0F, 1.0F)
            : 1.0F;
        DrawCircleV(position, 27.0F, WithAlpha(WHITE, 95.0F * remaining));
        DrawCircleLines(
            static_cast<int>(position.x),
            static_cast<int>(position.y),
            31.0F,
            WithAlpha(YELLOW, 220.0F * remaining)
        );
    }
    if (visual.damageTextSeconds > 0.0F && visual.lastDamage > 0) {
        const float remaining = std::clamp(
            visual.damageTextSeconds / kDamageTextDuration,
            0.0F,
            1.0F
        );
        const float rise = 28.0F * (1.0F - remaining);
        const std::string text = "-" + std::to_string(visual.lastDamage);
        DrawText(
            text.c_str(),
            static_cast<int>(position.x - 12),
            static_cast<int>(position.y - 70.0F - rise),
            20,
            WithAlpha(ORANGE, 255.0F * remaining)
        );
    }
}

const char* ActionLabel(game::input::Action action) {
    switch (action) {
    case game::input::Action::MoveUp: return "Move up";
    case game::input::Action::MoveDown: return "Move down";
    case game::input::Action::MoveLeft: return "Move left";
    case game::input::Action::MoveRight: return "Move right";
    case game::input::Action::Dash: return "Dash";
    case game::input::Action::LightAttack: return "Light attack";
    }
    return "Unknown";
}

void DrawPlayer(const protocol::PlayerState& player, Color color) {
    const auto position = ScreenPosition(player);
    const auto direction = FacingDirection(player);
    DrawCircleV(position, 22.0F, color);
    DrawLineEx(
        position,
        Vector2{position.x + direction.x * 28.0F, position.y + direction.y * 28.0F},
        3.0F,
        RAYWHITE
    );
    DrawText(player.playerId.c_str(), static_cast<int>(position.x - 30), static_cast<int>(position.y - 45), 12, RAYWHITE);
    DrawRectangle(static_cast<int>(position.x - 30), static_cast<int>(position.y + 30), 60, 7, DARKGRAY);
    DrawRectangle(
        static_cast<int>(position.x - 30),
        static_cast<int>(position.y + 30),
        static_cast<int>(60.0F * std::clamp(static_cast<float>(player.hp) / 100.0F, 0.0F, 1.0F)),
        7,
        color
    );
}

} // namespace

LoginAction DrawLogin(
    std::string& login,
    std::string& password,
    bool& loginActive,
    bool& passwordActive,
    const std::string& message,
    bool actionsEnabled
) {
    DrawText("2D PvP Duel", 500, 100, 36, RAYWHITE);
    TextField(Rectangle{440, 220, 400, 48}, "Login", login, loginActive, false);
    TextField(Rectangle{440, 310, 400, 48}, "Password", password, passwordActive, true);
    if (actionsEnabled) {
        if (Button(Rectangle{440, 390, 190, 48}, "Login")) {
            return LoginAction::Login;
        }
        if (Button(Rectangle{650, 390, 190, 48}, "Register")) {
            return LoginAction::Register;
        }
    }
    DrawText(message.c_str(), 440, 470, 18, LIGHTGRAY);
    return LoginAction::None;
}

MainMenuAction DrawMainMenu(
    const std::string& message,
    bool ready,
    bool waiting,
    bool error,
    bool settingsReady
) {
    DrawText("2D PvP Duel", 500, 120, 36, RAYWHITE);
    DrawText(message.c_str(), 420, 250, 22, LIGHTGRAY);
    if (ready && Button(Rectangle{520, 330, 240, 52}, "Find match")) {
        return MainMenuAction::FindMatch;
    }
    if (ready && Button(
            Rectangle{520, 400, 240, 52},
            settingsReady ? "Settings" : "Loading settings..."
        ) && settingsReady) {
        return MainMenuAction::Settings;
    }
    if (ready && Button(Rectangle{520, 470, 240, 52}, "Logout")) {
        return MainMenuAction::Logout;
    }
    if (waiting) {
        DrawCircleSector(Vector2{640, 360}, 30, 0, 280, 32, SKYBLUE);
    }
    if (error && Button(Rectangle{520, 330, 240, 52}, "Back")) {
        return MainMenuAction::Back;
    }
    return MainMenuAction::None;
}

SettingsScreenEvent DrawSettings(
    const game::input::InputBindings& bindings,
    std::optional<game::input::Action> captureAction,
    const std::string& message,
    bool savePending
) {
    DrawText("Settings", 540, 55, 36, RAYWHITE);
    DrawText("Select a binding, then press a key", 430, 105, 20, LIGHTGRAY);

    auto event = SettingsScreenEvent{};
    int row = 0;
    for (const auto& binding : bindings.All()) {
        const float y = 155.0F + static_cast<float>(row) * 55.0F;
        DrawText(ActionLabel(binding.action), 390, static_cast<int>(y + 12.0F), 20, RAYWHITE);
        const auto codeName = std::string(game::input::InputCodeName(binding.input));
        const bool capturing = captureAction == binding.action;
        if (Button(Rectangle{650, y, 240, 44}, capturing ? "Press a key..." : codeName.c_str())) {
            event.captureAction = binding.action;
        }
        ++row;
    }

    if (!savePending && Button(Rectangle{330, 525, 190, 48}, "Save")) {
        event.action = SettingsAction::Save;
    }
    if (!savePending && Button(Rectangle{545, 525, 190, 48}, "Reset defaults")) {
        event.action = SettingsAction::ResetDefaults;
    }
    if (Button(Rectangle{760, 525, 190, 48}, "Back")) {
        event.action = SettingsAction::Back;
    }
    DrawText(message.c_str(), 390, 610, 18, LIGHTGRAY);
    return event;
}

void DrawMatch(
    const game::match::MatchModel& model,
    const game::presentation::MatchPresentation& presentation,
    const game::input::InputBindings& bindings
) {
    const auto& world = model.World();
    static const game::arena::ArenaCatalog arenaCatalog;
    const auto arena = arenaCatalog.Resolve(model.ArenaId());
    DrawArenaScene(arena);
    const auto attack = bindings.InputFor(game::input::Action::LightAttack);
    const auto dash = bindings.InputFor(game::input::Action::Dash);
    const auto inputName = [](std::optional<game::input::InputCode> input) {
        return input ? game::input::InputCodeName(*input) : std::string_view("unbound");
    };
    const std::string controls = "Attack: " + std::string(inputName(attack))
        + " | Dash: " + std::string(inputName(dash));
    DrawText(controls.c_str(), 24, 20, 20, LIGHTGRAY);
    std::string clockText = "Waiting for match start";
    if (world.status == protocol::MatchStatus::Countdown) {
        clockText = "Starts in " + std::to_string(duel::game::TicksToDisplaySeconds(
            world.countdownTicksRemaining, model.TickRate()));
    } else if (world.status == protocol::MatchStatus::Active) {
        clockText = "Time: " + std::to_string(duel::game::TicksToDisplaySeconds(
            world.matchTicksRemaining, model.TickRate()));
    }
    DrawText(clockText.c_str(), 560, 90, 28, GOLD);
    for (auto player : world.players) {
        const bool isLocal = player.playerId == model.LocalPlayerId();
        const std::string hp = std::string(isLocal ? "Your HP: " : "Opponent HP: ")
            + std::to_string(player.hp);
        DrawText(hp.c_str(), isLocal ? 24 : 1060, 50, 18, isLocal ? SKYBLUE : RED);
        if (isLocal && world.status == protocol::MatchStatus::Active) {
            const auto visual = model.LocalPrediction().VisualPosition();
            player.positionX = visual.x;
            player.positionY = visual.y;
        } else if (!isLocal && world.status == protocol::MatchStatus::Active) {
            const auto interpolated = model.OpponentInterpolation().Sample();
            player.positionX = static_cast<std::int32_t>(std::lround(interpolated.x));
            player.positionY = static_cast<std::int32_t>(std::lround(interpolated.y));
        }
        const auto* visual = presentation.ViewFor(player.playerId);
        const auto emptyVisual = game::presentation::PlayerPresentation{};
        const auto& resolvedVisual = visual != nullptr ? *visual : emptyVisual;
        DrawAttackPulse(player, resolvedVisual);
        DrawPlayer(player, isLocal ? SKYBLUE : RED);
        DrawHitFlash(player, resolvedVisual);
    }
}

ResultAction DrawResult(
    const game::match::MatchModel& model,
    const std::string& message
) {
    if (!model.End()) {
        return ResultAction::None;
    }
    const auto& matchEnd = *model.End();
    const auto& world = model.World();
    const std::string result = duel::game::MatchResultLabel(matchEnd, model.LocalPlayerId());
    DrawText(result.c_str(), 540, 150, 40, GOLD);
    DrawText(message.c_str(), 560, 210, 24, LIGHTGRAY);
    for (std::size_t index = 0; index < world.players.size(); ++index) {
        const auto& player = world.players[index];
        const std::string hp = player.playerId + ": " + std::to_string(player.hp) + " HP";
        DrawText(hp.c_str(), 520, 260 + 32 * static_cast<int>(index), 20,
            player.playerId == model.LocalPlayerId() ? SKYBLUE : RED);
    }
    if (Button(Rectangle{390, 370, 240, 52}, "Back to menu")) {
        return ResultAction::BackToMenu;
    }
    if (Button(Rectangle{650, 370, 240, 52}, "Find another")) {
        return ResultAction::FindAnother;
    }
    return ResultAction::None;
}

} // namespace duel::app::screens
