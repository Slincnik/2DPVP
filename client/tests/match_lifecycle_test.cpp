#include "game/match_lifecycle.h"

#include <cassert>

int main() {
    duel::game::MatchmakingCleanup cleanup;
    assert(!cleanup.QueueResetRequired());
    cleanup.MatchAccepted();
    cleanup.ConnectionFailed();
    assert(cleanup.QueueResetRequired());
    cleanup.QueueResetSucceeded();
    assert(!cleanup.QueueResetRequired());
    cleanup.MatchAccepted();
    cleanup.MatchEnded();
    assert(cleanup.QueueResetRequired());

    assert(duel::game::TicksToDisplaySeconds(0, 30) == 0);
    assert(duel::game::TicksToDisplaySeconds(1, 30) == 1);
    assert(duel::game::TicksToDisplaySeconds(30, 30) == 1);
    assert(duel::game::TicksToDisplaySeconds(31, 30) == 2);

    duel::protocol::MatchEnd end;
    assert(duel::game::MatchResultLabel(end, "alice") == "DRAW");
    end.winnerPlayerId = "alice";
    assert(duel::game::MatchResultLabel(end, "alice") == "YOU WIN");
    assert(duel::game::MatchResultLabel(end, "bob") == "YOU LOSE");
    assert(duel::game::MatchFinishReasonLabel(duel::protocol::MatchFinishReason::KO) == "Knockout");
    assert(duel::game::MatchFinishReasonLabel(duel::protocol::MatchFinishReason::TimeLimit) == "Time limit");
    assert(duel::game::MatchFinishReasonLabel(duel::protocol::MatchFinishReason::Disconnect) == "Opponent disconnected");
    return 0;
}
