#include "auth/session_manager.h"

#include <utility>

namespace duel::auth {

SessionManager::SessionManager(
    api::GatewayClient& gateway,
    SecureStorage& storage,
    std::string storageKey
) : gateway_(gateway), storage_(storage), storageKey_(std::move(storageKey)) {}

api::Result<bool> SessionManager::Restore() {
    std::lock_guard lock(mutex_);
    const auto stored = storage_.Load(storageKey_);
    if (!stored) {
        return {.error = stored.error};
    }
    if (!stored.value || stored.value->empty()) {
        return {.value = false};
    }
    session_ = api::AuthSession{.refreshToken = std::move(*stored.value)};
    std::string error;
    if (!RefreshLocked(error)) {
        ClearSessionLocked();
        return {.error = error};
    }
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
    std::lock_guard lock(mutex_);
    SetSessionLocked(result.value);
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
    std::lock_guard lock(mutex_);
    SetSessionLocked(result.value);
    return result;
}

api::Result<bool> SessionManager::Logout() {
    std::lock_guard lock(mutex_);
    std::string error;
    if (session_ && !session_->refreshToken.empty()) {
        const auto result = gateway_.Logout(session_->refreshToken);
        if (!result) {
            error = result.error;
        }
    }
    // Local credentials must be removed even if the network call failed.
    const auto storageError = storage_.Erase(storageKey_);
    ClearSessionLocked();
    if (!error.empty()) {
        return {.error = error};
    }
    if (!storageError.empty()) {
        return {.error = storageError};
    }
    return {.value = true};
}

api::Result<api::QueueStatus> SessionManager::JoinQueue() {
    std::lock_guard lock(mutex_);
    if (!session_) {
        return {.error = "Not authenticated"};
    }
    auto result = gateway_.JoinQueue(session_->accessToken);
    if (result.status == 401) {
        std::string error;
        if (!RefreshLocked(error)) {
            return {.error = error};
        }
        result = gateway_.JoinQueue(session_->accessToken);
    }
    return result;
}

api::Result<api::QueueStatus> SessionManager::QueueStatus() {
    std::lock_guard lock(mutex_);
    if (!session_) {
        return {.error = "Not authenticated"};
    }
    auto result = gateway_.QueueStatusFor(session_->accessToken);
    if (result.status == 401) {
        std::string error;
        if (!RefreshLocked(error)) {
            return {.error = error};
        }
        result = gateway_.QueueStatusFor(session_->accessToken);
    }
    return result;
}

api::Result<bool> SessionManager::LeaveQueue() {
    std::lock_guard lock(mutex_);
    if (!session_) {
        return {.error = "Not authenticated"};
    }
    auto result = gateway_.LeaveQueue(session_->accessToken);
    if (result.status == 401) {
        std::string error;
        if (!RefreshLocked(error)) {
            return {.error = error};
        }
        result = gateway_.LeaveQueue(session_->accessToken);
    }
    return result;
}

std::optional<api::AuthSession> SessionManager::CurrentSession() const {
    std::lock_guard lock(mutex_);
    return session_;
}

std::string SessionManager::TakeWarning() {
    std::lock_guard lock(mutex_);
    return std::exchange(warning_, {});
}

bool SessionManager::RefreshLocked(std::string& error) {
    const auto result = gateway_.Refresh(*session_);
    if (!result) {
        error = result.error;
        const auto storageError = storage_.Erase(storageKey_);
        if (!storageError.empty()) {
            warning_ = "Could not remove invalid saved session: " + storageError;
        }
        ClearSessionLocked();
        return false;
    }
    SetSessionLocked(result.value);
    return true;
}

void SessionManager::SetSessionLocked(api::AuthSession session) {
    const auto storageError = storage_.Store(storageKey_, session.refreshToken);
    if (!storageError.empty()) {
        warning_ = "Session will not persist: " + storageError;
    }
    session_ = std::move(session);
}

void SessionManager::ClearSessionLocked() {
    session_.reset();
}

} // namespace duel::auth
