#include "protocol/match_protocol_adapter.h"

#include "game/v1/duel.pb.h"

#include <cstdlib>

namespace {

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

} // namespace

int main() {
    ::game::v1::WorldSnapshot snapshot;
    snapshot.set_server_tick(42);
    snapshot.set_arena_id("ember_foundry");
    const auto convertedSnapshot = duel::protocol::FromProtobuf(snapshot);
    Require(convertedSnapshot.serverTick == 42);
    Require(convertedSnapshot.arenaId == "ember_foundry");

    ::game::v1::MatchStart start;
    start.set_arena_id("ember_foundry");
    *start.mutable_initial_snapshot() = snapshot;
    const auto convertedStart = duel::protocol::FromProtobuf(start);
    Require(convertedStart.arenaId == "ember_foundry");
    Require(convertedStart.initialSnapshot.arenaId == convertedStart.arenaId);

    ::game::v1::MatchStart legacyStart;
    const auto convertedLegacy = duel::protocol::FromProtobuf(legacyStart);
    Require(convertedLegacy.arenaId.empty());
    Require(convertedLegacy.initialSnapshot.arenaId.empty());
    return 0;
}
