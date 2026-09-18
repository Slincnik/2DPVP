#pragma once

#include "api/gateway_client.h"
#include "auth/secure_storage.h"

#include <mutex>
#include <optional>
#include <string>

namespace duel::auth {

class SessionManager {
public:
    SessionManager(api::GatewayClient& gateway, SecureStorage& storage, std::string storageKey);

    // Returns true when a persisted session was restored, false when none exists.
    api::Result<bool> Restore();
    api::Result<api::AuthSession> Login(const std::string& login, const std::string& password);
    api::Result<api::AuthSession> Register(const std::string& login, const std::string& password);
    api::Result<bool> Logout();

    api::Result<api::QueueStatus> JoinQueue();
    api::Result<api::QueueStatus> QueueStatus();
    api::Result<bool> LeaveQueue();
    // Executes the authenticated profile request and retries it once after a
    // 401 refresh. Refresh failure clears the session without exposing tokens.
    api::Result<api::Profile> Profile();

    std::optional<api::AuthSession> CurrentSession() const;
    // A failed persistence attempt does not expose the token, but is surfaced
    // to the UI so users know their session will not survive a restart.
    std::string TakeWarning();

private:
    [[nodiscard]] std::optional<api::AuthSession> SessionCopy() const;
    bool RefreshAfterUnauthorized(const std::string& rejectedAccessToken, std::string& error);
    void SetSession(api::AuthSession session);
    void ClearSession();
    void AddWarning(std::string warning);

    api::GatewayClient& gateway_;
    SecureStorage& storage_;
    std::string storageKey_;
    mutable std::mutex mutex_;
    // Serializes refresh-token rotation without holding mutex_ over HTTP or
    // secure-storage I/O. Other callers reuse the refreshed access token.
    std::mutex refreshMutex_;
    std::optional<api::AuthSession> session_;
    std::string warning_;
};

} // namespace duel::auth
