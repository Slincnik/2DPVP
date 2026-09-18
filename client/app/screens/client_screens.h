#pragma once

#include "game/input/input.h"
#include "game/interpolation.h"
#include "game/prediction.h"
#include "game/v1/duel.pb.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace duel::app::screens {

enum class LoginAction { None, Login, Register };
enum class MainMenuAction { None, FindMatch, Settings, Logout, Back };
enum class ResultAction { None, BackToMenu, FindAnother };
enum class SettingsAction { None, Save, ResetDefaults, Back };

struct SettingsScreenEvent {
    SettingsAction action = SettingsAction::None;
    std::optional<game::input::Action> captureAction;
};

struct PlayerVisualState {
    int hp = -1;
    int damage = 0;
    float attackFlash = 0.0F;
    float hitFlash = 0.0F;
    float damageText = 0.0F;
};

LoginAction DrawLogin(
    std::string& login,
    std::string& password,
    bool& loginActive,
    bool& passwordActive,
    const std::string& message,
    bool actionsEnabled
);

MainMenuAction DrawMainMenu(
    const std::string& message,
    bool ready,
    bool waiting,
    bool error,
    bool settingsReady
);

SettingsScreenEvent DrawSettings(
    const game::input::InputBindings& bindings,
    std::optional<game::input::Action> captureAction,
    const std::string& message,
    bool savePending
);

void DrawMatch(
    const ::game::v1::WorldSnapshot& world,
    const std::string& localPlayerId,
    std::uint32_t serverTickRate,
    const duel::game::Prediction& prediction,
    const duel::game::InterpolationBuffer& opponentInterpolation,
    const std::unordered_map<std::string, PlayerVisualState>& playerVisuals,
    const game::input::InputBindings& bindings
);

ResultAction DrawResult(
    const ::game::v1::MatchEnd& matchEnd,
    const ::game::v1::WorldSnapshot& world,
    const std::string& localPlayerId,
    const std::string& message
);

} // namespace duel::app::screens
