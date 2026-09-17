#include "game/match_lifecycle.h"

namespace duel::game {

void MatchmakingCleanup::MatchAccepted() { queueResetRequired_ = true; }

void MatchmakingCleanup::ConnectionFailed() {
    // Preserve the matched queue entry cleanup requirement for the next join.
}

void MatchmakingCleanup::MatchEnded() { queueResetRequired_ = true; }

void MatchmakingCleanup::QueueResetSucceeded() { queueResetRequired_ = false; }

bool MatchmakingCleanup::QueueResetRequired() const { return queueResetRequired_; }

std::uint32_t TicksToDisplaySeconds(std::uint32_t ticks, std::uint32_t tickRate) {
    if (tickRate == 0 || ticks == 0) {
        return 0;
    }
    return (ticks + tickRate - 1) / tickRate;
}

std::string MatchResultLabel(const ::game::v1::MatchEnd& matchEnd, const std::string& playerId) {
    if (matchEnd.winner_player_id().empty()) {
        return "DRAW";
    }
    return matchEnd.winner_player_id() == playerId ? "YOU WIN" : "YOU LOSE";
}

std::string MatchFinishReasonLabel(::game::v1::MatchFinishReason reason) {
    switch (reason) {
    case ::game::v1::MATCH_FINISH_REASON_KO:
        return "Knockout";
    case ::game::v1::MATCH_FINISH_REASON_TIME_LIMIT:
        return "Time limit";
    default:
        return "Match finished";
    }
}

} // namespace duel::game
