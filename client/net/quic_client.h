#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "game/v1/duel.pb.h"

namespace duel::net {

class QuicClient {
public:
    QuicClient();
    ~QuicClient();

    QuicClient(const QuicClient&) = delete;
    QuicClient& operator=(const QuicClient&) = delete;

    bool Connect(
        const std::string& host,
        std::uint16_t port,
        const std::string& matchToken,
        const std::string& playerId
    );
    bool SendInput(std::uint32_t tick, std::int32_t moveX, std::int32_t moveY, bool attack);
    std::optional<::game::v1::WorldSnapshot> PollSnapshot();
    std::string Error() const;
    bool IsConnected() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace duel::net
