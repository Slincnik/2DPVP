#include "protocol/match_protocol_adapter.h"

#include "game/v1/duel.pb.h"

#include <algorithm>

namespace duel::protocol {
namespace {

::game::v1::ActionType ToProtobuf(ActionType type) noexcept {
    switch (type) {
    case ActionType::Dash: return ::game::v1::ACTION_TYPE_DASH;
    case ActionType::LightAttack: return ::game::v1::ACTION_TYPE_LIGHT_ATTACK;
    case ActionType::Unspecified: return ::game::v1::ACTION_TYPE_UNSPECIFIED;
    }
    return ::game::v1::ACTION_TYPE_UNSPECIFIED;
}

PlayerActionState FromProtobuf(::game::v1::PlayerActionState state) noexcept {
    switch (state) {
    case ::game::v1::PLAYER_ACTION_STATE_MOVE: return PlayerActionState::Move;
    case ::game::v1::PLAYER_ACTION_STATE_DASH: return PlayerActionState::Dash;
    case ::game::v1::PLAYER_ACTION_STATE_LIGHT_ATTACK_WINDUP:
        return PlayerActionState::LightAttackWindup;
    case ::game::v1::PLAYER_ACTION_STATE_LIGHT_ATTACK_ACTIVE:
        return PlayerActionState::LightAttackActive;
    case ::game::v1::PLAYER_ACTION_STATE_LIGHT_ATTACK_RECOVERY:
        return PlayerActionState::LightAttackRecovery;
    case ::game::v1::PLAYER_ACTION_STATE_HIT: return PlayerActionState::Hit;
    case ::game::v1::PLAYER_ACTION_STATE_KO: return PlayerActionState::KO;
    case ::game::v1::PLAYER_ACTION_STATE_IDLE:
    default:
        return PlayerActionState::Idle;
    }
}

MatchStatus FromProtobuf(::game::v1::MatchStatus status) noexcept {
    switch (status) {
    case ::game::v1::MATCH_STATUS_ACTIVE: return MatchStatus::Active;
    case ::game::v1::MATCH_STATUS_FINISHED: return MatchStatus::Finished;
    case ::game::v1::MATCH_STATUS_COUNTDOWN: return MatchStatus::Countdown;
    case ::game::v1::MATCH_STATUS_WAITING:
    default:
        return MatchStatus::Waiting;
    }
}

MatchFinishReason FromProtobuf(::game::v1::MatchFinishReason reason) noexcept {
    switch (reason) {
    case ::game::v1::MATCH_FINISH_REASON_KO: return MatchFinishReason::KO;
    case ::game::v1::MATCH_FINISH_REASON_TIME_LIMIT: return MatchFinishReason::TimeLimit;
    case ::game::v1::MATCH_FINISH_REASON_DISCONNECT: return MatchFinishReason::Disconnect;
    case ::game::v1::MATCH_FINISH_REASON_UNSPECIFIED:
    default:
        return MatchFinishReason::Unspecified;
    }
}

PlayerState FromProtobuf(const ::game::v1::PlayerState& player) {
    return PlayerState{
        .playerId = player.player_id(),
        .positionX = player.position_x(),
        .positionY = player.position_y(),
        .hp = player.hp(),
        .lastAckedInputTick = player.last_acked_input_tick(),
        .facingX = player.facing_x(),
        .facingY = player.facing_y(),
        .lastAckedActionSequence = player.last_acked_action_sequence(),
        .actionState = FromProtobuf(player.action_state()),
        .actionStartedServerTick = player.action_started_server_tick(),
        .actionTicksRemaining = player.action_ticks_remaining(),
    };
}

} // namespace

::game::v1::PlayerInput ToProtobuf(const PlayerInput& input) {
    ::game::v1::PlayerInput result;
    result.set_tick(input.tick);
    result.set_move_x(input.moveX);
    result.set_move_y(input.moveY);
    constexpr std::size_t maximumPendingActions = 8;
    const auto commandCount = std::min(input.pendingActions.size(), maximumPendingActions);
    for (std::size_t index = 0; index < commandCount; ++index) {
        const auto& command = input.pendingActions[index];
        auto* converted = result.add_pending_actions();
        converted->set_sequence(command.sequence);
        converted->set_type(ToProtobuf(command.type));
    }
    return result;
}

WorldSnapshot FromProtobuf(const ::game::v1::WorldSnapshot& snapshot) {
    WorldSnapshot result{
        .arenaId = snapshot.arena_id(),
        .serverTick = snapshot.server_tick(),
        .players = {},
        .status = FromProtobuf(snapshot.status()),
        .winnerPlayerId = snapshot.winner_player_id(),
        .countdownTicksRemaining = snapshot.countdown_ticks_remaining(),
        .matchTicksRemaining = snapshot.match_ticks_remaining(),
    };
    result.players.reserve(static_cast<std::size_t>(snapshot.players_size()));
    for (const auto& player : snapshot.players()) {
        result.players.push_back(FromProtobuf(player));
    }
    return result;
}

MatchStart FromProtobuf(const ::game::v1::MatchStart& start) {
    return MatchStart{
        .initialSnapshot = FromProtobuf(start.initial_snapshot()),
        .arenaId = start.arena_id(),
        .tickRate = start.tick_rate(),
        .countdownTicks = start.countdown_ticks(),
        .matchDurationTicks = start.match_duration_ticks(),
    };
}

MatchEnd FromProtobuf(const ::game::v1::MatchEnd& end) {
    return MatchEnd{
        .finalSnapshot = FromProtobuf(end.final_snapshot()),
        .winnerPlayerId = end.winner_player_id(),
        .reason = FromProtobuf(end.reason()),
    };
}

} // namespace duel::protocol
