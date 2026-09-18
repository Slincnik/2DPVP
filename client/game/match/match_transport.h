#pragma once

#include "protocol/match_protocol.h"

#include <optional>
#include <string>

namespace duel::game::match {

class MatchTransport {
public:
    virtual ~MatchTransport() = default;

    [[nodiscard]] virtual bool SendInput(const protocol::PlayerInput& input) = 0;
    [[nodiscard]] virtual std::optional<protocol::MatchStart> PollMatchStart() = 0;
    [[nodiscard]] virtual std::optional<protocol::WorldSnapshot> PollSnapshot() = 0;
    [[nodiscard]] virtual std::optional<protocol::MatchEnd> PollMatchEnd() = 0;
    [[nodiscard]] virtual bool IsConnected() const = 0;
    [[nodiscard]] virtual std::string Error() const = 0;
};

} // namespace duel::game::match
