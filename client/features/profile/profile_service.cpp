#include "features/profile/profile_service.h"

namespace duel::features::profile {

ProfileService::ProfileService(auth::SessionManager& sessionManager) noexcept
    : sessionManager_(sessionManager) {}

api::Result<api::Profile> ProfileService::Load() {
    return sessionManager_.Profile();
}

} // namespace duel::features::profile
