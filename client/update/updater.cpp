#include "update/https_fetch.h"

#include <openssl/evp.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string Sha256(const std::string& data) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digestSize = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1
        || EVP_DigestUpdate(context, data.data(), data.size()) != 1
        || EVP_DigestFinal_ex(context, digest.data(), &digestSize) != 1) {
        EVP_MD_CTX_free(context);
        return {};
    }
    EVP_MD_CTX_free(context);

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digestSize * 2);
    for (unsigned int index = 0; index < digestSize; ++index) {
        result += hex[digest[index] >> 4];
        result += hex[digest[index] & 0x0f];
    }
    return result;
}

bool Download(const std::string& url, std::string& content) {
    const auto response = duel::update::http::FetchHttps(url, 120);
    if (!response.ok) {
        std::cerr << "Update download failed: " << response.error << '\n';
        return false;
    }
    content = response.body;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        return 2;
    }

    const std::filesystem::path target = argv[1];
    const std::string url = argv[2];
    const std::string expectedHash = argv[3];
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string content;
    if (!Download(url, content) || Sha256(content) != expectedHash) {
        std::cerr << "Client update download or checksum verification failed\n";
        return 1;
    }

    const auto temporary = std::filesystem::temp_directory_path() /
        ("pvp-duel-update-" + std::to_string(static_cast<long long>(getpid())));
    const auto temporaryFile = temporary.string() + ".download";
    {
        std::ofstream output(temporaryFile, std::ios::binary | std::ios::trunc);
        if (!output || !output.write(content.data(), static_cast<std::streamsize>(content.size()))) {
            std::remove(temporaryFile.c_str());
            return 1;
        }
    }

    std::error_code error;
#ifdef __APPLE__
    std::filesystem::create_directories(temporary, error);
    if (error) {
        std::remove(temporaryFile.c_str());
        return 1;
    }
    const auto quote = [](const std::string& value) {
        return "'" + value + "'";
    };
    const auto command = "ditto -x -k --sequesterRsrc " + quote(temporaryFile) + " " + quote(temporary.string());
    if (std::system(command.c_str()) != 0) {
        std::filesystem::remove_all(temporary);
        std::remove(temporaryFile.c_str());
        return 1;
    }
    const auto extracted = temporary / "PvPDuel.app";
    const auto backup = target.string() + ".update.bak";
    std::filesystem::remove(backup, error);
    std::filesystem::rename(target, backup, error);
    if (error) {
        std::filesystem::remove_all(temporary);
        std::remove(temporaryFile.c_str());
        return 1;
    }
    std::filesystem::rename(extracted, target, error);
    if (error) {
        std::filesystem::rename(backup, target, error);
        std::filesystem::remove_all(temporary);
        std::remove(temporaryFile.c_str());
        return 1;
    }
    std::filesystem::remove(backup, error);
    std::filesystem::remove_all(temporary, error);
    std::remove(temporaryFile.c_str());
    const auto executable = target / "Contents/MacOS/PvPDuel";
    execl(executable.c_str(), executable.c_str(), nullptr);
#else
    const auto backup = target.string() + ".update.bak";
    if (chmod(temporaryFile.c_str(), 0755) != 0) {
        std::remove(temporaryFile.c_str());
        return 1;
    }
    std::filesystem::remove(backup, error);
    std::filesystem::rename(target, backup, error);
    if (error) {
        std::remove(temporaryFile.c_str());
        return 1;
    }
    std::filesystem::rename(temporaryFile, target, error);
    if (error) {
        std::filesystem::rename(backup, target, error);
        return 1;
    }
    std::filesystem::remove(backup, error);
    execl(target.c_str(), target.c_str(), nullptr);
#endif
    return errno == 0 ? 1 : errno;
}
