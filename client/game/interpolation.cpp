#include "game/interpolation.h"

#include <algorithm>

namespace duel::game {

InterpolationBuffer::InterpolationBuffer(std::uint32_t delayTicks, std::size_t capacity)
    : delayTicks_(delayTicks), capacity_(std::max<std::size_t>(capacity, 2)) {}

void InterpolationBuffer::Push(
    std::uint32_t serverTick,
    std::int32_t x,
    std::int32_t y
) {
    if (!points_.empty() && serverTick <= points_.back().tick) {
        return;
    }
    points_.push_back(Point{serverTick, static_cast<float>(x), static_cast<float>(y)});
    if (!initialized_) {
        renderTick_ = static_cast<float>(serverTick);
        initialized_ = true;
    }
    while (points_.size() > capacity_) {
        points_.pop_front();
    }
}

void InterpolationBuffer::Advance(float elapsedSeconds) {
    if (points_.empty()) {
        return;
    }
    const auto firstTick = static_cast<float>(points_.front().tick);
    const auto latestTick = points_.back().tick;
    const auto delayedTick = latestTick > delayTicks_ ? latestTick - delayTicks_ : 0;
    const auto targetTick = std::max(firstTick, static_cast<float>(delayedTick));
    renderTick_ = std::min(renderTick_ + std::max(elapsedSeconds, 0.0F) * 30.0F, targetTick);
}

InterpolatedPosition InterpolationBuffer::Sample() const {
    if (points_.empty()) {
        return {};
    }

    const auto targetTick = renderTick_;
    if (targetTick <= static_cast<float>(points_.front().tick)) {
        return {points_.front().x, points_.front().y};
    }

    for (std::size_t index = 1; index < points_.size(); ++index) {
        const auto& previous = points_[index - 1];
        const auto& next = points_[index];
        if (targetTick > static_cast<float>(next.tick)) {
            continue;
        }

        const auto span = static_cast<float>(next.tick - previous.tick);
        const auto offset = targetTick - static_cast<float>(previous.tick);
        const auto blend = span > 0.0F ? offset / span : 0.0F;
        return {
            previous.x + (next.x - previous.x) * blend,
            previous.y + (next.y - previous.y) * blend,
        };
    }

    return {points_.back().x, points_.back().y};
}

std::size_t InterpolationBuffer::Size() const { return points_.size(); }

} // namespace duel::game
