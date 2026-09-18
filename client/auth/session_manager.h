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

    std::optional<api::AuthSession> CurrentSession() const;
    // A failed persistence attempt does not expose the token, but is surfaced
    // to the UI so users know their session will not survive a restart.
    std::string TakeWarning();

private:
    bool RefreshLocked(std::string& error);
    void SetSessionLocked(api::AuthSession session);
    void ClearSessionLocked();

    api::GatewayClient& gateway_;
    SecureStorage& storage_;
    std::string storageKey_;
    mutable std::mutex mutex_;
    std::optional<api::AuthSession> session_;
    std::string warning_;
};

} // namespace duel::auth
