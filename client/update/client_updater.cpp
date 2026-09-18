#include "update/client_updater.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace duel::update {
namespace {

#ifndef PVP_DUEL_VERSION
#define PVP_DUEL_VERSION "0.0.0-dev"
#endif

struct Url {
    std::string origin;
    std::string path;
};

std::string TrimVersion(std::string version) {
    if (!version.empty() && version.front() == 'v') {
        version.erase(version.begin());
    }
    return version;
}

std::vector<int> VersionParts(std::string version) {
    version = TrimVersion(std::move(version));
    std::vector<int> parts;
    std::size_t start = 0;
    while (start < version.size()) {
        const auto end = version.find('.', start);
        const auto component = version.substr(start, end == std::string::npos ? end : end - start);
        try {
            parts.push_back(std::stoi(component));
        } catch (...) {
            return {};
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

bool IsNewer(std::string_view candidate) {
    const auto currentParts = VersionParts(PVP_DUEL_VERSION);
    const auto candidateParts = VersionParts(std::string(candidate));
    if (currentParts.empty() || candidateParts.empty()) {
        return false;
    }
    const auto count = std::max(currentParts.size(), candidateParts.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto current = index < currentParts.size() ? currentParts[index] : 0;
        const auto next = index < candidateParts.size() ? candidateParts[index] : 0;
        if (next != current) {
            return next > current;
        }
    }
    return false;
}

bool ParseUrl(const std::string& value, Url& result) {
    constexpr std::string_view https = "https://";
    if (!value.starts_with(https)) {
        return false;
    }
    const auto separator = value.find('/', https.size());
    if (separator == std::string::npos) {
        result.origin = value;
        result.path = "/";
    } else {
        result.origin = value.substr(0, separator);
        result.path = value.substr(separator);
    }
    return true;
}

std::string CurrentAppImage() {
    if (const auto* appImage = std::getenv("APPIMAGE"); appImage != nullptr && *appImage != '\0') {
        return appImage;
    }
    return {};
}

std::string UpdaterPath() {
    if (const auto* configured = std::getenv("PVP_DUEL_UPDATER_PATH");
        configured != nullptr && *configured != '\0') {
        return configured;
    }
    return {};
}

bool StartUpdater(const std::string& url, const std::string& sha256) {
    const auto appImage = CurrentAppImage();
    const auto updater = UpdaterPath();
    if (appImage.empty() || updater.empty()) {
        return false;
    }

    const auto pid = fork();
    if (pid != 0) {
        return pid > 0;
    }
    execl(updater.c_str(), updater.c_str(), appImage.c_str(), url.c_str(), sha256.c_str(), nullptr);
    _exit(127);
}

} // namespace

void CheckAndStart(const std::string& manifestUrl) {
    if (manifestUrl.empty()) {
        return;
    }

    Url manifest;
    if (!ParseUrl(manifestUrl, manifest)) {
        std::cerr << "Skipping update check: manifest URL must use HTTPS\n";
        return;
    }

    httplib::SSLClient client(manifest.origin.substr(std::string("https://").size()));
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(3, 0);
    const auto response = client.Get(manifest.path);
    if (!response || response->status != 200) {
        return;
    }

    try {
        const auto body = nlohmann::json::parse(response->body);
        const auto version = body.at("version").get<std::string>();
        const auto url = body.at("url").get<std::string>();
        const auto sha256 = body.at("sha256").get<std::string>();
        if (!IsNewer(version) || sha256.size() != 64 || !StartUpdater(url, sha256)) {
            return;
        }
        std::cerr << "Updating client to " << version << "...\n";
        std::exit(0);
    } catch (const std::exception&) {
        // A malformed or unavailable manifest must never prevent the client
        // from starting.
    }
}

} // namespace duel::update
