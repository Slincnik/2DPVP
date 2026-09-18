#pragma once

#include <string>

namespace duel::update {

// Checks the release manifest and starts the updater when a newer release is
// available. An empty manifest URL disables update checks (local builds).
void CheckAndStart(const std::string& manifestUrl);

} // namespace duel::update
