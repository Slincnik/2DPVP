#pragma once

#include <memory>
#include <optional>
#include <string>

namespace duel::auth {

struct StorageResult {
    std::optional<std::string> value;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

class SecureStorage {
public:
    virtual ~SecureStorage() = default;

    virtual StorageResult Load(const std::string& key) = 0;
    virtual std::string Store(const std::string& key, const std::string& value) = 0;
    virtual std::string Erase(const std::string& key) = 0;
};

// Creates the native credential-store implementation for the current platform.
// When the required OS integration is not present, it deliberately provides no
// persistence rather than writing the refresh token to disk.
std::unique_ptr<SecureStorage> CreateSecureStorage();

} // namespace duel::auth
