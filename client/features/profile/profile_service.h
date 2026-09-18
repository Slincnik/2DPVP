#pragma once

#include "api/gateway_client.h"
#include "auth/session_manager.h"

namespace duel::features::profile {

// Profile feature boundary. Authentication and retry ownership stays with
// SessionManager; this service never receives credential values.
class ProfileService {
public:
    explicit ProfileService(auth::SessionManager& sessionManager) noexcept;

    [[nodiscard]] api::Result<api::Profile> Load();

private:
    auth::SessionManager& sessionManager_;
};

} // namespace duel::features::profile
