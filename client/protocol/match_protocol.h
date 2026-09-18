#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace duel::protocol {

enum class ActionType {
    Unspecified,
    Dash,
    LightAttack,
};

enum class PlayerActionState {
    Idle,
    Move,
    Dash,
    LightAttackWindup,
    LightAttackActive,
    LightAttackRecovery,
    Hit,
    KO,
};

enum class MatchStatus {
    Active,
    Finished,
    Waiting,
    Countdown,
};

enum class MatchFinishReason {
    Unspecified,
    KO,
    TimeLimit,
    Disconnect,
};

struct ActionCommand {
    std::uint32_t sequence = 0;
    ActionType type = ActionType::Unspecified;
};

struct PlayerInput {
    std::uint32_t tick = 0;
    std::int32_t moveX = 0;
    std::int32_t moveY = 0;
    std::vector<ActionCommand> pendingActions;
};

struct PlayerState {
    std::string playerId;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
    std::int32_t hp = 0;
    std::uint32_t lastAckedInputTick = 0;
    std::int32_t facingX = 0;
    std::int32_t facingY = 0;
    std::uint32_t lastAckedActionSequence = 0;
    PlayerActionState actionState = PlayerActionState::Idle;
    std::uint32_t actionStartedServerTick = 0;
    std::uint32_t actionTicksRemaining = 0;
};

struct WorldSnapshot {
    std::string arenaId;
    std::uint32_t serverTick = 0;
    std::vector<PlayerState> players;
    MatchStatus status = MatchStatus::Waiting;
    std::string winnerPlayerId;
    std::uint32_t countdownTicksRemaining = 0;
    std::uint32_t matchTicksRemaining = 0;
};

struct MatchStart {
    WorldSnapshot initialSnapshot;
    std::string arenaId;
    std::uint32_t tickRate = 0;
    std::uint32_t countdownTicks = 0;
    std::uint32_t matchDurationTicks = 0;
};

struct MatchEnd {
    WorldSnapshot finalSnapshot;
    std::string winnerPlayerId;
    MatchFinishReason reason = MatchFinishReason::Unspecified;
};

} // namespace duel::protocol
