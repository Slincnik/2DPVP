#pragma once

#include <string>

namespace duel::api {

struct AuthSession {
    std::string userId;
    std::string login;
    std::string accessToken;
    std::string refreshToken;
};

enum class QueueState {
    NotQueued,
    Waiting,
    Matched,
};

struct QueueStatus {
    QueueState state = QueueState::NotQueued;
    std::string matchId;
    std::string opponentId;
    std::string serverAddr;
    std::string matchTicket;
};

template <typename T>
struct Result {
    T value{};
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

class GatewayClient {
public:
    explicit GatewayClient(std::string baseUrl);

    Result<AuthSession> Register(const std::string& login, const std::string& password) const;
    Result<AuthSession> Login(const std::string& login, const std::string& password) const;
    Result<AuthSession> Refresh(const AuthSession& session) const;
    Result<QueueStatus> JoinQueue(const std::string& accessToken) const;
    Result<QueueStatus> QueueStatusFor(const std::string& accessToken) const;
    Result<bool> LeaveQueue(const std::string& accessToken) const;

private:
    Result<AuthSession> Authenticate(
        const std::string& path,
        const std::string& login,
        const std::string& password
    ) const;

    std::string baseUrl_;
};

} // namespace duel::api
