#include "auth/session_manager.h"

#include <utility>

namespace duel::auth {

SessionManager::SessionManager(
    api::GatewayClient& gateway,
    SecureStorage& storage,
    std::string storageKey
) : gateway_(gateway), storage_(storage), storageKey_(std::move(storageKey)) {}

api::Result<bool> SessionManager::Restore() {
    const auto stored = storage_.Load(storageKey_);
    if (!stored) {
        return {.error = stored.error};
    }
    if (!stored.value || stored.value->empty()) {
        return {.value = false};
    }

    const auto refreshed = gateway_.Refresh(api::AuthSession{.refreshToken = std::move(*stored.value)});
    if (!refreshed) {
        const auto storageError = storage_.Erase(storageKey_);
        if (!storageError.empty()) {
            AddWarning("Could not remove invalid saved session: " + storageError);
        }
        return {.error = refreshed.error};
    }
    SetSession(refreshed.value);
    return {.value = true};
}

api::Result<api::AuthSession> SessionManager::Login(
    const std::string& login,
    const std::string& password
) {
    const auto result = gateway_.Login(login, password);
    if (!result) {
        return result;
    }
    SetSession(result.value);
    return result;
}

api::Result<api::AuthSession> SessionManager::Register(
    const std::string& login,
    const std::string& password
) {
    const auto result = gateway_.Register(login, password);
    if (!result) {
        return result;
    }
    SetSession(result.value);
    return result;
}

api::Result<bool> SessionManager::Logout() {
    const auto session = SessionCopy();
    // Clear memory state first so other use-cases immediately observe logout.
    ClearSession();

    std::string error;
    if (session && !session->refreshToken.empty()) {
        const auto result = gateway_.Logout(session->refreshToken);
        if (!result) {
            error = result.error;
        }
    }
    // Local credentials must be removed even if the network call failed.
    const auto storageError = storage_.Erase(storageKey_);
    if (!error.empty()) {
        return {.error = error};
    }
    if (!storageError.empty()) {
        return {.error = storageError};
    }
    return {.value = true};
}

api::Result<api::QueueStatus> SessionManager::JoinQueue() {
    const auto session = SessionCopy();
    if (!session) {
        return {.error = "Not authenticated"};
    }
    auto result = gateway_.JoinQueue(session->accessToken);
    if (result.status != 401) {
        return result;
    }
    std::string error;
    if (!RefreshAfterUnauthorized(session->accessToken, error)) {
        return {.error = error};
    }
    const auto refreshed = SessionCopy();
    if (!refreshed) {
        return {.error = "Not authenticated"};
    }
    return gateway_.JoinQueue(refreshed->accessToken);
}

api::Result<api::QueueStatus> SessionManager::QueueStatus() {
    const auto session = SessionCopy();
    if (!session) {
        return {.error = "Not authenticated"};
    }
    auto result = gateway_.QueueStatusFor(session->accessToken);
    if (result.status != 401) {
        return result;
    }
    std::string error;
    if (!RefreshAfterUnauthorized(session->accessToken, error)) {
        return {.error = error};
    }
    const auto refreshed = SessionCopy();
    if (!refreshed) {
        return {.error = "Not authenticated"};
    }
    return gateway_.QueueStatusFor(refreshed->accessToken);
}

api::Result<bool> SessionManager::LeaveQueue() {
    const auto session = SessionCopy();
    if (!session) {
        return {.error = "Not authenticated"};
    }
    auto result = gateway_.LeaveQueue(session->accessToken);
    if (result.status != 401) {
        return result;
    }
    std::string error;
    if (!RefreshAfterUnauthorized(session->accessToken, error)) {
        return {.error = error};
    }
    const auto refreshed = SessionCopy();
    if (!refreshed) {
        return {.error = "Not authenticated"};
    }
    return gateway_.LeaveQueue(refreshed->accessToken);
}

api::Result<api::Profile> SessionManager::Profile() {
    const auto session = SessionCopy();
    if (!session) {
        return {.error = "Not authenticated"};
    }
    auto result = gateway_.ProfileFor(session->accessToken);
    if (result.status != 401) {
        return result;
    }
    std::string error;
    if (!RefreshAfterUnauthorized(session->accessToken, error)) {
        return {.error = error};
    }
    const auto refreshed = SessionCopy();
    if (!refreshed) {
        return {.error = "Not authenticated"};
    }
    return gateway_.ProfileFor(refreshed->accessToken);
}

std::optional<api::AuthSession> SessionManager::SessionCopy() const {
    std::lock_guard lock(mutex_);
    return session_;
}

std::optional<api::AuthSession> SessionManager::CurrentSession() const {
    return SessionCopy();
}

std::string SessionManager::TakeWarning() {
    std::lock_guard lock(mutex_);
    return std::exchange(warning_, {});
}

bool SessionManager::RefreshAfterUnauthorized(
    const std::string& rejectedAccessToken,
    std::string& error
) {
    std::lock_guard refreshLock(refreshMutex_);
    const auto before = SessionCopy();
    if (!before) {
        error = "Not authenticated";
        return false;
    }
    // A concurrent request already performed the one permitted refresh.
    if (before->accessToken != rejectedAccessToken) {
        return true;
    }

    const auto result = gateway_.Refresh(*before);
    if (!result) {
        const auto stillRejected = SessionCopy();
        if (stillRejected && stillRejected->accessToken == rejectedAccessToken) {
            ClearSession();
            const auto storageError = storage_.Erase(storageKey_);
            if (!storageError.empty()) {
                AddWarning("Could not remove invalid saved session: " + storageError);
            }
        }
        error = result.error;
        return false;
    }

    const auto stillRejected = SessionCopy();
    if (stillRejected && stillRejected->accessToken == rejectedAccessToken) {
        SetSession(result.value);
    }
    return CurrentSession().has_value();
}

void SessionManager::SetSession(api::AuthSession session) {
    const auto storageError = storage_.Store(storageKey_, session.refreshToken);
    if (!storageError.empty()) {
        AddWarning("Session will not persist: " + storageError);
    }
    std::lock_guard lock(mutex_);
    session_ = std::move(session);
}

void SessionManager::ClearSession() {
    std::lock_guard lock(mutex_);
    session_.reset();
}

void SessionManager::AddWarning(std::string warning) {
    std::lock_guard lock(mutex_);
    warning_ = std::move(warning);
}

} // namespace duel::auth
