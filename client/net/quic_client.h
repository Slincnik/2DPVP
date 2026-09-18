#pragma once

#include "game/match/match_transport.h"

#include <cstdint>
#include <memory>
#include <string>

namespace duel::net {

class QuicClient final : public game::match::MatchTransport {
public:
    QuicClient();
    ~QuicClient() override;

    QuicClient(const QuicClient&) = delete;
    QuicClient& operator=(const QuicClient&) = delete;

    bool Connect(
        const std::string& host,
        std::uint16_t port,
        const std::string& matchToken,
        const std::string& playerId
    );

    [[nodiscard]] bool SendInput(const protocol::PlayerInput& input) override;
    [[nodiscard]] std::optional<protocol::MatchStart> PollMatchStart() override;
    [[nodiscard]] std::optional<protocol::WorldSnapshot> PollSnapshot() override;
    [[nodiscard]] std::optional<protocol::MatchEnd> PollMatchEnd() override;
    [[nodiscard]] std::string Error() const override;
    [[nodiscard]] bool IsConnected() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace duel::net
