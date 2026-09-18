#pragma once

#include <cstdint>
#include <string>

#include "protocol/match_protocol.h"

namespace duel::game {

class MatchmakingCleanup {
public:
    void MatchAccepted();
    void ConnectionFailed();
    void MatchEnded();
    void QueueResetSucceeded();
    [[nodiscard]] bool QueueResetRequired() const;

private:
    bool queueResetRequired_ = false;
};

std::uint32_t TicksToDisplaySeconds(std::uint32_t ticks, std::uint32_t tickRate);
std::string MatchResultLabel(const protocol::MatchEnd& matchEnd, const std::string& playerId);
std::string MatchFinishReasonLabel(protocol::MatchFinishReason reason);

} // namespace duel::game
