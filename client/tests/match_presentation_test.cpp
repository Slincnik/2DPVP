#include "game/presentation/match_presentation.h"

#include <cstdlib>

namespace {

using duel::game::presentation::AnimationState;
using duel::game::presentation::MatchPresentation;
using duel::game::presentation::PresentationEventType;
using duel::protocol::PlayerActionState;

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

duel::protocol::WorldSnapshot Snapshot(
    std::uint32_t serverTick,
    PlayerActionState state,
    std::uint32_t started,
    std::uint32_t remaining,
    std::int32_t hp = 100
) {
    return duel::protocol::WorldSnapshot{
        .serverTick = serverTick,
        .players = {{
            .playerId = "alice",
            .hp = hp,
            .actionState = state,
            .actionStartedServerTick = started,
            .actionTicksRemaining = remaining,
        }},
        .status = duel::protocol::MatchStatus::Active,
        .winnerPlayerId = {},
        .countdownTicksRemaining = 0,
        .matchTicksRemaining = 0,
    };
}

void TestAnimationUsesAuthoritativeTimeline() {
    MatchPresentation presentation;
    const auto snapshot = Snapshot(103, PlayerActionState::LightAttackActive, 100, 1);
    presentation.ApplySnapshot(snapshot);

    const auto* view = presentation.ViewFor("alice");
    Require(view != nullptr);
    Require(view->animation == AnimationState::AttackActive);
    Require(view->actionElapsedTicks == 3);
    Require(view->actionTicksRemaining == 1);
}

void TestLateSnapshotRestoresCurrentPhaseWithoutHistory() {
    MatchPresentation presentation;
    presentation.ApplySnapshot(Snapshot(
        208,
        PlayerActionState::LightAttackRecovery,
        200,
        2
    ));

    const auto* view = presentation.ViewFor("alice");
    Require(view != nullptr);
    Require(view->animation == AnimationState::AttackRecovery);
    Require(view->actionElapsedTicks == 8);
    Require(view->actionTicksRemaining == 2);
    Require(presentation.DrainEvents().empty());
}

void TestEventsArePresentationOnly() {
    MatchPresentation presentation;
    auto before = Snapshot(10, PlayerActionState::Idle, 0, 0, 100);
    presentation.ApplySnapshot(before);
    static_cast<void>(presentation.DrainEvents());

    auto after = Snapshot(14, PlayerActionState::LightAttackWindup, 14, 3, 80);
    const auto originalHp = after.players.front().hp;
    const auto originalState = after.players.front().actionState;
    presentation.ApplySnapshot(after);
    const auto events = presentation.DrainEvents();

    Require(events.size() == 2);
    Require(events[0].type == PresentationEventType::PlayerHit);
    Require(events[0].damage == 20);
    Require(events[1].type == PresentationEventType::AttackStarted);
    presentation.Advance(1.0F);
    Require(after.players.front().hp == originalHp);
    Require(after.players.front().actionState == originalState);
}

void TestAllAuthoritativeStatesMapToAnimations() {
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::Idle)
        == AnimationState::Idle);
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::Move)
        == AnimationState::Run);
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::Dash)
        == AnimationState::Dash);
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::LightAttackWindup)
        == AnimationState::AttackWindup);
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::LightAttackActive)
        == AnimationState::AttackActive);
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::LightAttackRecovery)
        == AnimationState::AttackRecovery);
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::Hit)
        == AnimationState::Hit);
    Require(duel::game::presentation::AnimationFromAuthoritative(PlayerActionState::KO)
        == AnimationState::KO);
}

} // namespace

int main() {
    TestAnimationUsesAuthoritativeTimeline();
    TestLateSnapshotRestoresCurrentPhaseWithoutHistory();
    TestEventsArePresentationOnly();
    TestAllAuthoritativeStatesMapToAnimations();
    return 0;
}
