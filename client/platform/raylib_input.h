#pragma once

#include "game/input/input.h"

#include <optional>

namespace duel::platform {

class RaylibInputAdapter final : public game::input::InputSource {
public:
    [[nodiscard]] bool IsDown(game::input::InputCode code) const noexcept override;
    [[nodiscard]] bool IsPressed(game::input::InputCode code) const noexcept override;
    [[nodiscard]] std::optional<game::input::InputCode> PressedInputCode() const noexcept;
};

} // namespace duel::platform
