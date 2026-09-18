#include "game/presentation/match_presentation.h"

#include <algorithm>
#include <utility>

namespace duel::game::presentation {
namespace {

constexpr float kAttackFlashDuration = 0.18F;
constexpr float kHitFlashDuration = 0.35F;
constexpr float kDamageTextDuration = 0.65F;

bool IsAttack(protocol::PlayerActionState state) noexcept {
    return state == protocol::PlayerActionState::LightAttackWindup
        || state == protocol::PlayerActionState::LightAttackActive
        || state == protocol::PlayerActionState::LightAttackRecovery;
}

} // namespace

AnimationState AnimationFromAuthoritative(protocol::PlayerActionState state) noexcept {
    switch (state) {
    case protocol::PlayerActionState::Move: return AnimationState::Run;
    case protocol::PlayerActionState::Dash: return AnimationState::Dash;
    case protocol::PlayerActionState::LightAttackWindup: return AnimationState::AttackWindup;
    case protocol::PlayerActionState::LightAttackActive: return AnimationState::AttackActive;
    case protocol::PlayerActionState::LightAttackRecovery: return AnimationState::AttackRecovery;
    case protocol::PlayerActionState::Hit: return AnimationState::Hit;
    case protocol::PlayerActionState::KO: return AnimationState::KO;
    case protocol::PlayerActionState::Idle: return AnimationState::Idle;
    }
    return AnimationState::Idle;
}

void MatchPresentation::Reset() {
    views_.clear();
    observed_.clear();
    events_.clear();
}

void MatchPresentation::ApplySnapshot(const protocol::WorldSnapshot& snapshot) {
    for (const auto& player : snapshot.players) {
        auto& view = views_[player.playerId];
        view.animation = AnimationFromAuthoritative(player.actionState);
        view.actionTicksRemaining = player.actionTicksRemaining;
        view.actionElapsedTicks = player.actionStartedServerTick != 0
                && snapshot.serverTick >= player.actionStartedServerTick
            ? snapshot.serverTick - player.actionStartedServerTick
            : 0;

        const auto previous = observed_.find(player.playerId);
        if (previous != observed_.end()) {
            if (player.hp < previous->second.hp) {
                view.lastDamage = previous->second.hp - player.hp;
                view.hitFlashSeconds = kHitFlashDuration;
                view.damageTextSeconds = kDamageTextDuration;
                events_.push_back({PresentationEventType::PlayerHit, player.playerId, view.lastDamage});
            }
            const bool newTimeline = player.actionStartedServerTick
                != previous->second.actionStartedServerTick;
            if (newTimeline && IsAttack(player.actionState)) {
                view.attackFlashSeconds = kAttackFlashDuration;
                events_.push_back({PresentationEventType::AttackStarted, player.playerId, 0});
            } else if (newTimeline && player.actionState == protocol::PlayerActionState::Dash) {
                events_.push_back({PresentationEventType::DashStarted, player.playerId, 0});
            }
        }
        observed_[player.playerId] = ObservedPlayer{
            .hp = player.hp,
            .actionState = player.actionState,
            .actionStartedServerTick = player.actionStartedServerTick,
        };
    }
}

void MatchPresentation::Advance(float elapsedSeconds) noexcept {
    const auto elapsed = std::max(0.0F, elapsedSeconds);
    for (auto& [unused, view] : views_) {
        static_cast<void>(unused);
        view.attackFlashSeconds = std::max(0.0F, view.attackFlashSeconds - elapsed);
        view.hitFlashSeconds = std::max(0.0F, view.hitFlashSeconds - elapsed);
        view.damageTextSeconds = std::max(0.0F, view.damageTextSeconds - elapsed);
    }
}

const PlayerPresentation* MatchPresentation::ViewFor(const std::string& playerId) const noexcept {
    const auto found = views_.find(playerId);
    return found == views_.end() ? nullptr : &found->second;
}

std::vector<PresentationEvent> MatchPresentation::DrainEvents() {
    auto result = std::move(events_);
    events_.clear();
    return result;
}

} // namespace duel::game::presentation
