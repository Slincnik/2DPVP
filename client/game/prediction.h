#pragma once

#include <cstdint>
#include <deque>

#include "protocol/match_protocol.h"

namespace duel::game {

struct PredictedPosition {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

class Prediction {
public:
    void ApplyInput(std::uint32_t tick, std::int32_t moveX, std::int32_t moveY);
    void Reconcile(const protocol::PlayerState& authoritative);
    void AdvanceVisual(float blend);

    PredictedPosition Position() const;
    PredictedPosition VisualPosition() const;
    std::size_t PendingInputCount() const;

private:
    struct PendingInput {
        std::uint32_t tick;
        std::int8_t moveX;
        std::int8_t moveY;
    };

    static void ApplyMovement(PredictedPosition& position, std::int8_t moveX, std::int8_t moveY);

    PredictedPosition predicted_{};
    float visualX_ = 0.0F;
    float visualY_ = 0.0F;
    bool visualInitialized_ = false;
    std::deque<PendingInput> pending_;
};

} // namespace duel::game
