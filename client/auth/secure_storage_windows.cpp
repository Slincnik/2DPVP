#include "auth/secure_storage.h"

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>

#include <string>
#include <vector>

namespace duel::auth {
namespace {

std::wstring ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        wide.data(), length);
    return wide;
}

std::string ErrorMessage(DWORD error) {
    return "Windows Credential Manager error " + std::to_string(error);
}

std::wstring Target(const std::string& key) {
    return L"2D PvP Duel/refresh/" + ToWide(key);
}

class WindowsCredentialStorage final : public SecureStorage {
public:
    StorageResult Load(const std::string& key) override {
        PCREDENTIALW credential = nullptr;
        const auto target = Target(key);
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            if (GetLastError() == ERROR_NOT_FOUND) {
                return {};
            }
            return {.error = ErrorMessage(GetLastError())};
        }
        const std::string value(reinterpret_cast<const char*>(credential->CredentialBlob),
            credential->CredentialBlobSize);
        CredFree(credential);
        return {.value = value};
    }

    std::string Store(const std::string& key, const std::string& value) override {
        const auto target = Target(key);
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<wchar_t*>(target.c_str());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
        credential.CredentialBlobSize = static_cast<DWORD>(value.size());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (!CredWriteW(&credential, 0)) {
            return ErrorMessage(GetLastError());
        }
        return {};
    }

    std::string Erase(const std::string& key) override {
        const auto target = Target(key);
        if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
            return ErrorMessage(GetLastError());
        }
        return {};
    }
};

} // namespace

std::unique_ptr<SecureStorage> CreateWindowsCredentialStorage() {
    return std::make_unique<WindowsCredentialStorage>();
}

} // namespace duel::auth
#endif
