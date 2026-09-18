#include "game/match/match_controller.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using duel::game::input::Action;
using duel::game::input::InputFrame;
using duel::game::match::MatchController;
using duel::game::match::MatchTransport;
using duel::protocol::MatchStatus;
using duel::protocol::PlayerActionState;

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

class FakeTransport final : public MatchTransport {
public:
    bool SendInput(const duel::protocol::PlayerInput& input) override {
        sent.push_back(input);
        return true;
    }

    std::optional<duel::protocol::MatchStart> PollMatchStart() override {
        return Take(start);
    }

    std::optional<duel::protocol::WorldSnapshot> PollSnapshot() override {
        return Take(snapshot);
    }

    std::optional<duel::protocol::MatchEnd> PollMatchEnd() override {
        return Take(end);
    }

    bool IsConnected() const override { return connected; }
    std::string Error() const override { return {}; }

    template<typename T>
    static std::optional<T> Take(std::optional<T>& value) {
        auto result = std::move(value);
        value.reset();
        return result;
    }

    bool connected = true;
    std::optional<duel::protocol::MatchStart> start;
    std::optional<duel::protocol::WorldSnapshot> snapshot;
    std::optional<duel::protocol::MatchEnd> end;
    std::vector<duel::protocol::PlayerInput> sent;
};

duel::protocol::PlayerState Player(
    std::string id,
    std::int32_t x,
    std::uint32_t inputAck = 0,
    std::uint32_t actionAck = 0
) {
    return duel::protocol::PlayerState{
        .playerId = std::move(id),
        .positionX = x,
        .hp = 100,
        .lastAckedInputTick = inputAck,
        .facingX = 1,
        .lastAckedActionSequence = actionAck,
        .actionState = PlayerActionState::Idle,
    };
}

duel::protocol::WorldSnapshot ActiveSnapshot(
    std::uint32_t tick,
    duel::protocol::PlayerState local,
    std::string arenaId = "neon_rooftop"
) {
    return duel::protocol::WorldSnapshot{
        .arenaId = std::move(arenaId),
        .serverTick = tick,
        .players = {std::move(local), Player("bob", 250)},
        .status = MatchStatus::Active,
        .winnerPlayerId = {},
        .countdownTicksRemaining = 0,
        .matchTicksRemaining = 2000,
    };
}

void Start(MatchController& controller, FakeTransport& transport) {
    transport.start = duel::protocol::MatchStart{
        .initialSnapshot = ActiveSnapshot(100, Player("alice", 0)),
        .arenaId = "neon_rooftop",
        .tickRate = 30,
    };
    Require(controller.PollNetwork().started);
}

void TestCommandsRepeatUntilAck() {
    FakeTransport transport;
    duel::game::presentation::MatchPresentation presentation;
    MatchController controller("alice", transport, presentation);
    Start(controller, transport);

    Require(controller.FixedTick(InputFrame{.pressed = {Action::Dash}, .held = {}}));
    Require(transport.sent.back().pendingActions.size() == 1);
    Require(transport.sent.back().pendingActions.front().sequence == 1);

    Require(controller.FixedTick(InputFrame{}));
    Require(transport.sent.back().pendingActions.size() == 1);
    Require(transport.sent.back().pendingActions.front().sequence == 1);

    transport.snapshot = ActiveSnapshot(101, Player("alice", 120, 2, 1));
    static_cast<void>(controller.PollNetwork());
    Require(controller.FixedTick(InputFrame{}));
    Require(transport.sent.back().pendingActions.empty());
    Require(controller.Model().PendingActionCount() == 0);
}

void TestHeldLightAttackRepeatsAtCooldownCadenceUntilAcknowledged() {
    FakeTransport transport;
    duel::game::presentation::MatchPresentation presentation;
    MatchController controller("alice", transport, presentation);
    Start(controller, transport);

    const auto heldAttack = InputFrame{.pressed = {}, .held = {Action::LightAttack}};
    Require(controller.FixedTick(heldAttack));
    Require(transport.sent.back().pendingActions.size() == 1);
    Require(transport.sent.back().pendingActions.front().sequence == 1);
    Require(transport.sent.back().pendingActions.front().type == duel::protocol::ActionType::LightAttack);

    for (int tick = 0; tick < 14; ++tick) {
        Require(controller.FixedTick(heldAttack));
        Require(transport.sent.back().pendingActions.size() == 1);
        Require(transport.sent.back().pendingActions.front().sequence == 1);
    }
    Require(controller.FixedTick(heldAttack));
    Require(transport.sent.back().pendingActions.size() == 2);
    Require(transport.sent.back().pendingActions[0].sequence == 1);
    Require(transport.sent.back().pendingActions[1].sequence == 2);

    transport.snapshot = ActiveSnapshot(116, Player("alice", 0, 16, 1));
    static_cast<void>(controller.PollNetwork());
    Require(controller.FixedTick(InputFrame{}));
    Require(transport.sent.back().pendingActions.size() == 1);
    Require(transport.sent.back().pendingActions.front().sequence == 2);
}

void TestMovementReconciliationReplaysOnlyUnackedInput() {
    FakeTransport transport;
    duel::game::presentation::MatchPresentation presentation;
    MatchController controller("alice", transport, presentation);
    Start(controller, transport);

    Require(controller.FixedTick(InputFrame{.moveX = 1, .pressed = {}, .held = {}}));
    Require(controller.Model().LocalPrediction().Position().x == 10);
    transport.snapshot = ActiveSnapshot(101, Player("alice", 8, 1));
    static_cast<void>(controller.PollNetwork());
    Require(controller.Model().LocalPrediction().Position().x == 8);

    Require(controller.FixedTick(InputFrame{.moveX = 1, .pressed = {}, .held = {}}));
    transport.snapshot = ActiveSnapshot(102, Player("alice", 8, 1));
    static_cast<void>(controller.PollNetwork());
    Require(controller.Model().LocalPrediction().Position().x == 18);
}

void TestArenaIdIsFixedFromMatchStart() {
    FakeTransport transport;
    duel::game::presentation::MatchPresentation presentation;
    MatchController controller("alice", transport, presentation);
    Start(controller, transport);
    Require(controller.Model().ArenaId() == "neon_rooftop");
    Require(controller.Model().World().arenaId == "neon_rooftop");

    transport.snapshot = ActiveSnapshot(101, Player("alice", 0), "future_arena");
    static_cast<void>(controller.PollNetwork());
    Require(controller.Model().ArenaId() == "neon_rooftop");
    Require(controller.Model().World().arenaId == "neon_rooftop");
}

void TestDashWaitsForAuthoritativePosition() {
    FakeTransport transport;
    duel::game::presentation::MatchPresentation presentation;
    MatchController controller("alice", transport, presentation);
    Start(controller, transport);

    Require(controller.FixedTick(InputFrame{.pressed = {Action::Dash}, .held = {}}));
    Require(controller.Model().LocalPrediction().Position().x == 0);

    auto dashed = Player("alice", 120, 1, 1);
    dashed.actionState = PlayerActionState::Dash;
    dashed.actionStartedServerTick = 101;
    dashed.actionTicksRemaining = 1;
    transport.snapshot = ActiveSnapshot(101, std::move(dashed));
    static_cast<void>(controller.PollNetwork());
    Require(controller.Model().LocalPrediction().Position().x == 120);
}

} // namespace

int main() {
    TestCommandsRepeatUntilAck();
    TestHeldLightAttackRepeatsAtCooldownCadenceUntilAcknowledged();
    TestMovementReconciliationReplaysOnlyUnackedInput();
    TestArenaIdIsFixedFromMatchStart();
    TestDashWaitsForAuthoritativePosition();
    return 0;
}
