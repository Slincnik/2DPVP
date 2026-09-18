#pragma once

#include "game/input/input.h"
#include "game/match/match_model.h"
#include "game/presentation/match_presentation.h"

#include <optional>
#include <string>

namespace duel::app::screens {

enum class LoginAction { None, Login, Register };
enum class MainMenuAction { None, FindMatch, Settings, Logout, Back };
enum class ResultAction { None, BackToMenu, FindAnother };
enum class SettingsAction { None, Save, ResetDefaults, Back };

struct SettingsScreenEvent {
    SettingsAction action = SettingsAction::None;
    std::optional<game::input::Action> captureAction;
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
    const game::match::MatchModel& model,
    const game::presentation::MatchPresentation& presentation,
    const game::input::InputBindings& bindings
);

ResultAction DrawResult(
    const game::match::MatchModel& model,
    const std::string& message
);

} // namespace duel::app::screens
