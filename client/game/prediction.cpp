#include "game/prediction.h"

#include <algorithm>
#include <cmath>

namespace duel::game {
namespace {

constexpr std::int32_t kMovementPerTick = 10;
constexpr std::int32_t kArenaMinX = -500;
constexpr std::int32_t kArenaMaxX = 500;
constexpr std::int32_t kArenaMinY = -300;
constexpr std::int32_t kArenaMaxY = 300;

std::int8_t ClampAxis(std::int32_t axis) {
    return static_cast<std::int8_t>(std::clamp(axis, -1, 1));
}

} // namespace

void Prediction::ApplyInput(std::uint32_t tick, std::int32_t moveX, std::int32_t moveY) {
    const auto clampedX = ClampAxis(moveX);
    const auto clampedY = ClampAxis(moveY);
    pending_.push_back(PendingInput{tick, clampedX, clampedY});
    ApplyMovement(predicted_, clampedX, clampedY);

    if (!visualInitialized_) {
        visualX_ = static_cast<float>(predicted_.x);
        visualY_ = static_cast<float>(predicted_.y);
        visualInitialized_ = true;
    }
}

void Prediction::Reconcile(const protocol::PlayerState& authoritative) {
    while (!pending_.empty() && pending_.front().tick <= authoritative.lastAckedInputTick) {
        pending_.pop_front();
    }

    predicted_ = PredictedPosition{authoritative.positionX, authoritative.positionY};
    for (const auto& input : pending_) {
        ApplyMovement(predicted_, input.moveX, input.moveY);
    }

    if (!visualInitialized_) {
        visualX_ = static_cast<float>(predicted_.x);
        visualY_ = static_cast<float>(predicted_.y);
        visualInitialized_ = true;
    }
}

void Prediction::AdvanceVisual(float blend) {
    const auto clampedBlend = std::clamp(blend, 0.0F, 1.0F);
    visualX_ += (static_cast<float>(predicted_.x) - visualX_) * clampedBlend;
    visualY_ += (static_cast<float>(predicted_.y) - visualY_) * clampedBlend;

    if (std::abs(static_cast<float>(predicted_.x) - visualX_) < 0.01F) {
        visualX_ = static_cast<float>(predicted_.x);
    }
    if (std::abs(static_cast<float>(predicted_.y) - visualY_) < 0.01F) {
        visualY_ = static_cast<float>(predicted_.y);
    }
}

PredictedPosition Prediction::Position() const { return predicted_; }

PredictedPosition Prediction::VisualPosition() const {
    return PredictedPosition{
        static_cast<std::int32_t>(std::lround(visualX_)),
        static_cast<std::int32_t>(std::lround(visualY_)),
    };
}

std::size_t Prediction::PendingInputCount() const { return pending_.size(); }

void Prediction::ApplyMovement(
    PredictedPosition& position,
    std::int8_t moveX,
    std::int8_t moveY
) {
    position.x = std::clamp(
        position.x + static_cast<std::int32_t>(moveX) * kMovementPerTick,
        kArenaMinX,
        kArenaMaxX
    );
    position.y = std::clamp(
        position.y + static_cast<std::int32_t>(moveY) * kMovementPerTick,
        kArenaMinY,
        kArenaMaxY
    );
}

} // namespace duel::game
