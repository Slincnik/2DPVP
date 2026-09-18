#pragma once

#include <httplib.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace duel::update::http {

struct Url {
    std::string origin;
    std::string path;
};

struct FetchResult {
    bool ok = false;
    std::string body;
    std::string error;
};

// Parses only absolute HTTPS URLs. Redirects are parsed with this same helper
// so cpp-httplib cannot silently downgrade a request to HTTP.
[[nodiscard]] inline bool ParseHttpsUrl(std::string_view value, Url& result) {
    constexpr std::string_view prefix = "https://";
    if (!value.starts_with(prefix)) {
        return false;
    }
    const auto authorityEnd = value.find('/', prefix.size());
    const auto authority = value.substr(
        prefix.size(),
        authorityEnd == std::string_view::npos ? std::string_view::npos : authorityEnd - prefix.size()
    );
    if (authority.empty() || authority.find_first_of("?#") != std::string_view::npos) {
        return false;
    }

    result.origin = std::string(prefix) + std::string(authority);
    result.path = authorityEnd == std::string_view::npos
        ? "/"
        : std::string(value.substr(authorityEnd));
    return true;
}

[[nodiscard]] inline FetchResult FetchHttps(std::string_view url, int timeoutSeconds) {
    Url current;
    if (!ParseHttpsUrl(url, current)) {
        return {.error = "URL is not HTTPS"};
    }

    constexpr std::size_t kMaximumRedirects = 5;
    for (std::size_t redirect = 0; redirect <= kMaximumRedirects; ++redirect) {
        httplib::SSLClient client(current.origin.substr(std::string_view("https://").size()));
        client.set_connection_timeout(timeoutSeconds, 0);
        client.set_read_timeout(timeoutSeconds, 0);
        // Do not enable set_follow_location(): cpp-httplib follows an HTTP
        // Location by constructing a plain Client, which would violate the
        // updater's HTTPS-only policy.
        const auto response = client.Get(current.path);
        if (!response) {
            return {.error = "HTTPS request failed"};
        }
        if (response->status == 200) {
            return {.ok = true, .body = response->body};
        }

        const bool redirectResponse = response->status == 301 || response->status == 302
            || response->status == 303 || response->status == 307 || response->status == 308;
        if (!redirectResponse) {
            return {
                .error = "HTTPS request returned status " + std::to_string(response->status),
            };
        }
        if (redirect == kMaximumRedirects) {
            return {.error = "HTTPS redirect limit exceeded"};
        }

        Url next;
        const auto location = response->get_header_value("location");
        if (!ParseHttpsUrl(location, next)) {
            return {.error = "HTTPS redirect was not HTTPS"};
        }
        current = std::move(next);
    }
    return {.error = "HTTPS redirect limit exceeded"};
}

} // namespace duel::update::http
