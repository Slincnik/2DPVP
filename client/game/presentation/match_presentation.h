#pragma once

#include "protocol/match_protocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace duel::game::presentation {

enum class AnimationState {
    Idle,
    Run,
    Dash,
    AttackWindup,
    AttackActive,
    AttackRecovery,
    Hit,
    KO,
};

enum class PresentationEventType {
    AttackStarted,
    DashStarted,
    PlayerHit,
};

struct PresentationEvent {
    PresentationEventType type = PresentationEventType::AttackStarted;
    std::string playerId;
    std::int32_t damage = 0;
};

struct PlayerPresentation {
    AnimationState animation = AnimationState::Idle;
    std::uint32_t actionElapsedTicks = 0;
    std::uint32_t actionTicksRemaining = 0;
    std::int32_t lastDamage = 0;
    float attackFlashSeconds = 0.0F;
    float hitFlashSeconds = 0.0F;
    float damageTextSeconds = 0.0F;
};

class MatchPresentation {
public:
    void Reset();
    void ApplySnapshot(const protocol::WorldSnapshot& snapshot);
    void Advance(float elapsedSeconds) noexcept;

    [[nodiscard]] const PlayerPresentation* ViewFor(const std::string& playerId) const noexcept;
    [[nodiscard]] std::vector<PresentationEvent> DrainEvents();

private:
    struct ObservedPlayer {
        std::int32_t hp = 0;
        protocol::PlayerActionState actionState = protocol::PlayerActionState::Idle;
        std::uint32_t actionStartedServerTick = 0;
    };

    std::unordered_map<std::string, PlayerPresentation> views_;
    std::unordered_map<std::string, ObservedPlayer> observed_;
    std::vector<PresentationEvent> events_;
};

[[nodiscard]] AnimationState AnimationFromAuthoritative(
    protocol::PlayerActionState state
) noexcept;

} // namespace duel::game::presentation
