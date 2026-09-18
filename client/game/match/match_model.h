#pragma once

#include "game/input/input.h"
#include "game/interpolation.h"
#include "game/prediction.h"
#include "protocol/match_protocol.h"

#include <cstdint>
#include <optional>
#include <string>

namespace duel::game::match {

class MatchController;

class MatchModel {
public:
    explicit MatchModel(std::string localPlayerId);

    [[nodiscard]] const std::string& LocalPlayerId() const noexcept;
    [[nodiscard]] const protocol::WorldSnapshot& World() const noexcept;
    [[nodiscard]] const std::optional<protocol::MatchEnd>& End() const noexcept;
    [[nodiscard]] std::uint32_t TickRate() const noexcept;
    [[nodiscard]] bool Started() const noexcept;
    [[nodiscard]] const Prediction& LocalPrediction() const noexcept;
    [[nodiscard]] const InterpolationBuffer& OpponentInterpolation() const noexcept;
    [[nodiscard]] std::size_t PendingActionCount() const noexcept;

private:
    friend class MatchController;

    std::string localPlayerId_;
    protocol::WorldSnapshot world_;
    std::optional<protocol::MatchEnd> end_;
    std::uint32_t tickRate_ = 30;
    bool started_ = false;
    std::uint32_t inputTick_ = 0;
    input::PendingActionQueue pendingActions_;
    Prediction localPrediction_;
    InterpolationBuffer opponentInterpolation_;
};

} // namespace duel::game::match
