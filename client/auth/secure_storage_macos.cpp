#include "auth/secure_storage.h"

#ifdef __APPLE__
#include <Security/Security.h>

#include <string>

namespace duel::auth {
namespace {

constexpr char kService[] = "com.github.dprishchepa.pvp-duel.refresh-token";

std::string ErrorMessage(OSStatus status) {
    CFStringRef message = SecCopyErrorMessageString(status, nullptr);
    if (message == nullptr) {
        return "macOS Keychain error " + std::to_string(status);
    }
    char buffer[256]{};
    const bool converted = CFStringGetCString(message, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(message);
    return converted ? std::string(buffer) : "macOS Keychain error " + std::to_string(status);
}

class KeychainStorage final : public SecureStorage {
public:
    StorageResult Load(const std::string& key) override {
        UInt32 length = 0;
        void* data = nullptr;
        const OSStatus status = SecKeychainFindGenericPassword(
            nullptr,
            sizeof(kService) - 1,
            kService,
            static_cast<UInt32>(key.size()),
            key.data(),
            &length,
            &data,
            nullptr
        );
        if (status == errSecItemNotFound) {
            return {};
        }
        if (status != errSecSuccess) {
            return {.error = ErrorMessage(status)};
        }
        std::string value(static_cast<const char*>(data), length);
        SecKeychainItemFreeContent(nullptr, data);
        return {.value = std::move(value)};
    }

    std::string Store(const std::string& key, const std::string& value) override {
        SecKeychainItemRef item = nullptr;
        const OSStatus found = SecKeychainFindGenericPassword(
            nullptr, sizeof(kService) - 1, kService, static_cast<UInt32>(key.size()), key.data(),
            nullptr, nullptr, &item);
        if (found == errSecSuccess) {
            const OSStatus updated = SecKeychainItemModifyAttributesAndData(
                item, nullptr, static_cast<UInt32>(value.size()), value.data());
            CFRelease(item);
            return updated == errSecSuccess ? std::string{} : ErrorMessage(updated);
        }
        if (found != errSecItemNotFound) {
            return ErrorMessage(found);
        }
        const OSStatus added = SecKeychainAddGenericPassword(
            nullptr, sizeof(kService) - 1, kService, static_cast<UInt32>(key.size()), key.data(),
            static_cast<UInt32>(value.size()), value.data(), nullptr);
        return added == errSecSuccess ? std::string{} : ErrorMessage(added);
    }

    std::string Erase(const std::string& key) override {
        SecKeychainItemRef item = nullptr;
        const OSStatus found = SecKeychainFindGenericPassword(
            nullptr, sizeof(kService) - 1, kService, static_cast<UInt32>(key.size()), key.data(),
            nullptr, nullptr, &item);
        if (found == errSecItemNotFound) {
            return {};
        }
        if (found != errSecSuccess) {
            return ErrorMessage(found);
        }
        const OSStatus deleted = SecKeychainItemDelete(item);
        CFRelease(item);
        return deleted == errSecSuccess ? std::string{} : ErrorMessage(deleted);
    }
};

} // namespace

std::unique_ptr<SecureStorage> CreateMacOSKeychainStorage() {
    return std::make_unique<KeychainStorage>();
}

} // namespace duel::auth
#endif
