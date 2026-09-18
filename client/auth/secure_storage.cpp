#include "auth/secure_storage.h"

#include <utility>

#ifdef PVP_DUEL_HAS_LIBSECRET
#include <libsecret/secret.h>
#endif

namespace duel::auth {

#ifdef _WIN32
std::unique_ptr<SecureStorage> CreateWindowsCredentialStorage();
#elif defined(__APPLE__)
std::unique_ptr<SecureStorage> CreateMacOSKeychainStorage();
#endif

namespace {

class UnavailableStorage final : public SecureStorage {
public:
    StorageResult Load(const std::string&) override { return {}; }

    std::string Store(const std::string&, const std::string&) override {
        return "Secure credential storage is unavailable; refresh token was not persisted";
    }

    std::string Erase(const std::string&) override { return {}; }
};

#ifdef PVP_DUEL_HAS_LIBSECRET

const SecretSchema kSchema = {
    "com.github.dprishchepa.pvp-duel",
    SECRET_SCHEMA_NONE,
    {
        {"gateway", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, static_cast<SecretSchemaAttributeType>(0)},
    },
};

std::string ErrorMessage(GError* error) {
    if (error == nullptr) {
        return {};
    }
    const std::string message = error->message;
    g_error_free(error);
    return message;
}

class SecretServiceStorage final : public SecureStorage {
public:
    StorageResult Load(const std::string& key) override {
        GError* error = nullptr;
        gchar* secret = secret_password_lookup_sync(&kSchema, nullptr, &error, "gateway", key.c_str(), nullptr);
        if (error != nullptr) {
            return {.error = ErrorMessage(error)};
        }
        if (secret == nullptr) {
            return {};
        }
        std::string value(secret);
        secret_password_free(secret);
        return {.value = std::move(value)};
    }

    std::string Store(const std::string& key, const std::string& value) override {
        GError* error = nullptr;
        const gboolean stored = secret_password_store_sync(
            &kSchema,
            SECRET_COLLECTION_DEFAULT,
            "2D PvP Duel refresh token",
            value.c_str(),
            nullptr,
            &error,
            "gateway",
            key.c_str(),
            nullptr
        );
        if (!stored) {
            return ErrorMessage(error);
        }
        return {};
    }

    std::string Erase(const std::string& key) override {
        GError* error = nullptr;
        const gboolean cleared = secret_password_clear_sync(
            &kSchema, nullptr, &error, "gateway", key.c_str(), nullptr);
        if (!cleared && error != nullptr) {
            return ErrorMessage(error);
        }
        return {};
    }
};

#endif

} // namespace

std::unique_ptr<SecureStorage> CreateSecureStorage() {
#ifdef _WIN32
    return CreateWindowsCredentialStorage();
#elif defined(__APPLE__)
    return CreateMacOSKeychainStorage();
#elif defined(PVP_DUEL_HAS_LIBSECRET)
    return std::make_unique<SecretServiceStorage>();
#else
    return std::make_unique<UnavailableStorage>();
#endif
}

} // namespace duel::auth
