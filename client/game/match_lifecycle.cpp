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

std::string MatchResultLabel(const protocol::MatchEnd& matchEnd, const std::string& playerId) {
    if (matchEnd.winnerPlayerId.empty()) {
        return "DRAW";
    }
    return matchEnd.winnerPlayerId == playerId ? "YOU WIN" : "YOU LOSE";
}

std::string MatchFinishReasonLabel(protocol::MatchFinishReason reason) {
    switch (reason) {
    case protocol::MatchFinishReason::KO:
        return "Knockout";
    case protocol::MatchFinishReason::TimeLimit:
        return "Time limit";
    case protocol::MatchFinishReason::Disconnect:
        return "Opponent disconnected";
    case protocol::MatchFinishReason::Unspecified:
    default:
        return "Match finished";
    }
}

} // namespace duel::game
