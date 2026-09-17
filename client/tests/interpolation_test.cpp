#include "game/interpolation.h"

#include <cmath>
#include <iostream>

namespace {

bool Near(float left, float right) {
    return std::abs(left - right) < 0.001F;
}

} // namespace

int main() {
    duel::game::InterpolationBuffer buffer(1);
    buffer.Push(10, 0, 10);
    buffer.Push(12, 20, 30); // Simulates a lost tick 11 snapshot.
    buffer.Advance(1.0F / 30.0F);

    const auto sample = buffer.Sample();
    if (!Near(sample.x, 10.0F) || !Near(sample.y, 20.0F)) {
        std::cerr << "must interpolate across a missing snapshot\n";
        return 1;
    }

    buffer.Push(11, 999, 999); // Stale/out-of-order datagram.
    if (buffer.Size() != 2) {
        std::cerr << "must ignore stale snapshots\n";
        return 1;
    }
    return 0;
}
