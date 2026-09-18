#include "game/prediction.h"

#include <iostream>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    duel::game::Prediction prediction;
    prediction.ApplyInput(1, 1, 0);
    prediction.ApplyInput(2, 1, -1);

    bool passed = true;
    passed &= Expect(prediction.Position().x == 20, "prediction must apply local movement immediately");
    passed &= Expect(prediction.Position().y == -10, "prediction must apply both axes");
    passed &= Expect(prediction.PendingInputCount() == 2, "inputs must remain pending before acknowledgement");

    duel::protocol::PlayerState authoritative;
    authoritative.positionX = 8;
    authoritative.positionY = 0;
    authoritative.lastAckedInputTick = 1;
    prediction.Reconcile(authoritative);

    passed &= Expect(prediction.Position().x == 18, "reconciliation must replay unacknowledged input");
    passed &= Expect(prediction.Position().y == -10, "replayed input must preserve its y movement");
    passed &= Expect(prediction.PendingInputCount() == 1, "acknowledged input must be removed");

    for (int index = 0; index < 20; ++index) {
        prediction.AdvanceVisual(0.5F);
    }
    passed &= Expect(prediction.VisualPosition().x == 18, "visual position must converge to prediction");

    return passed ? 0 : 1;
}
