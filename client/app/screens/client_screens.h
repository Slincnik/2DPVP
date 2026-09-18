#pragma once

#include "game/interpolation.h"
#include "game/prediction.h"
#include "game/v1/duel.pb.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace duel::app::screens {

enum class LoginAction { None, Login, Register };
enum class MainMenuAction { None, FindMatch, Logout, Back };
enum class ResultAction { None, BackToMenu, FindAnother };

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
    bool error
);

void DrawMatch(
    const ::game::v1::WorldSnapshot& world,
    const std::string& localPlayerId,
    std::uint32_t serverTickRate,
    const duel::game::Prediction& prediction,
    const duel::game::InterpolationBuffer& opponentInterpolation,
    const std::unordered_map<std::string, PlayerVisualState>& playerVisuals
);

ResultAction DrawResult(
    const ::game::v1::MatchEnd& matchEnd,
    const ::game::v1::WorldSnapshot& world,
    const std::string& localPlayerId,
    const std::string& message
);

} // namespace duel::app::screens
