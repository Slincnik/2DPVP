#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace duel::game {

struct InterpolatedPosition {
    float x = 0.0F;
    float y = 0.0F;
};

class InterpolationBuffer {
public:
    explicit InterpolationBuffer(std::uint32_t delayTicks = 3, std::size_t capacity = 32);

    void Push(std::uint32_t serverTick, std::int32_t x, std::int32_t y);
    void Advance(float elapsedSeconds);
    InterpolatedPosition Sample() const;
    std::size_t Size() const;

private:
    struct Point {
        std::uint32_t tick;
        float x;
        float y;
    };

    std::uint32_t delayTicks_;
    std::size_t capacity_;
    float renderTick_ = 0.0F;
    bool initialized_ = false;
    std::deque<Point> points_;
};

} // namespace duel::game
