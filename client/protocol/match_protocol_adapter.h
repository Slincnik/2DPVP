#pragma once

#include "protocol/match_protocol.h"

namespace game::v1 {
class PlayerInput;
class WorldSnapshot;
class MatchStart;
class MatchEnd;
} // namespace game::v1

namespace duel::protocol {

[[nodiscard]] ::game::v1::PlayerInput ToProtobuf(const PlayerInput& input);
[[nodiscard]] WorldSnapshot FromProtobuf(const ::game::v1::WorldSnapshot& snapshot);
[[nodiscard]] MatchStart FromProtobuf(const ::game::v1::MatchStart& start);
[[nodiscard]] MatchEnd FromProtobuf(const ::game::v1::MatchEnd& end);

} // namespace duel::protocol
