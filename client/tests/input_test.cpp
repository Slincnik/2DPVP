#include "game/input/input.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string_view>

namespace {

using duel::game::input::Action;
using duel::game::input::InputBindings;
using duel::game::input::InputCode;
using duel::game::input::InputSampler;
using duel::game::input::InputSource;
using duel::game::input::PendingActionQueue;

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

class FakeInputSource final : public InputSource {
public:
    [[nodiscard]] bool IsDown(InputCode code) const noexcept override {
        return down.contains(code);
    }

    [[nodiscard]] bool IsPressed(InputCode code) const noexcept override {
        return pressed.contains(code);
    }

    std::set<InputCode> down;
    std::set<InputCode> pressed;
};

bool Contains(const std::vector<Action>& actions, Action action) {
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

void TestDefaultsAndValidation() {
    auto bindings = InputBindings::Defaults();
    Require(bindings.InputFor(Action::MoveUp) == InputCode::KeyW);
    Require(bindings.InputFor(Action::MoveDown) == InputCode::KeyS);
    Require(bindings.InputFor(Action::MoveLeft) == InputCode::KeyA);
    Require(bindings.InputFor(Action::MoveRight) == InputCode::KeyD);
    Require(bindings.InputFor(Action::LightAttack) == InputCode::Space);
    Require(bindings.InputFor(Action::Dash) == InputCode::LeftShift);

    std::string_view error;
    Require(!bindings.Rebind(Action::Dash, InputCode::Space, &error));
    Require(!error.empty());
    Require(bindings.InputFor(Action::Dash) == InputCode::LeftShift);

    auto duplicate = bindings.All();
    duplicate.back().input = InputCode::LeftShift;
    Require(!InputBindings::Validate(duplicate, &error));
}

void TestHeldMovementAndBufferedEdges() {
    auto bindings = InputBindings::Defaults();
    InputSampler sampler(bindings);
    FakeInputSource source;

    source.down = {InputCode::KeyW, InputCode::KeyD};
    source.pressed = {InputCode::Space, InputCode::LeftShift};
    sampler.Observe(source);
    source.pressed.clear();
    sampler.Observe(source);

    const auto first = sampler.ConsumeFixedTick();
    Require(first.moveX == 1);
    Require(first.moveY == -1);
    Require(first.pressed.size() == 2);
    Require(Contains(first.pressed, Action::LightAttack));
    Require(Contains(first.pressed, Action::Dash));

    const auto second = sampler.ConsumeFixedTick();
    Require(second.moveX == 1);
    Require(second.moveY == -1);
    Require(second.pressed.empty());
}

void TestPendingActionsRepeatUntilAcknowledgedAndStayBounded() {
    PendingActionQueue queue;
    Require(queue.Enqueue(Action::Dash));
    Require(queue.Enqueue(Action::LightAttack));
    Require(queue.Pending().size() == 2);
    Require(queue.Pending()[0].sequence == 1);
    Require(queue.Pending()[1].sequence == 2);

    // Reading the queue models retransmission in another datagram and does not consume it.
    Require(queue.Pending().size() == 2);
    queue.Acknowledge(1);
    Require(queue.Pending().size() == 1);
    Require(queue.Pending()[0].sequence == 2);
    queue.Acknowledge(2);
    Require(queue.Pending().empty());

    for (std::size_t index = 0; index < PendingActionQueue::kMaximumSize; ++index) {
        Require(queue.Enqueue(Action::Dash));
    }
    Require(!queue.Enqueue(Action::LightAttack));
    Require(queue.Pending().size() == PendingActionQueue::kMaximumSize);
    Require(!queue.Enqueue(Action::MoveUp));

    queue.Reset();
    Require(queue.Pending().empty());
    Require(queue.Enqueue(Action::LightAttack));
    Require(queue.Pending().front().sequence == 1);
}

void TestRebindAppliesOnNextObservedFrame() {
    auto bindings = InputBindings::Defaults();
    InputSampler sampler(bindings);
    FakeInputSource source;

    source.down = {InputCode::KeyW};
    sampler.Observe(source);
    Require(sampler.ConsumeFixedTick().moveY == -1);

    Require(bindings.Rebind(Action::MoveUp, InputCode::ArrowUp));
    source.down = {InputCode::KeyW};
    sampler.Observe(source);
    Require(sampler.ConsumeFixedTick().moveY == 0);

    source.down = {InputCode::ArrowUp};
    sampler.Observe(source);
    Require(sampler.ConsumeFixedTick().moveY == -1);
}

} // namespace

int main() {
    TestDefaultsAndValidation();
    TestHeldMovementAndBufferedEdges();
    TestPendingActionsRepeatUntilAcknowledgedAndStayBounded();
    TestRebindAppliesOnNextObservedFrame();
    return 0;
}
