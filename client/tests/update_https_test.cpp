#include "update/https_fetch.h"

#include <cstdlib>
#include <string_view>

namespace {

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

void TestRequestHeaders() {
    const auto headers = duel::update::http::RequestHeaders();
    const auto userAgent = headers.find("User-Agent");
    const auto accept = headers.find("Accept");
    Require(userAgent != headers.end());
    Require(accept != headers.end());
    Require(userAgent->second == duel::update::http::kUserAgent);
    Require(accept->second == duel::update::http::kAcceptHeader);
    Require(userAgent->second.find("token") == std::string::npos);
}

void TestHttpsUrlPolicy() {
    duel::update::http::Url url;
    Require(duel::update::http::ParseHttpsUrl(
        "https://github.com/example/project/releases/latest/download/latest.json", url
    ));
    Require(url.origin == "https://github.com");
    Require(url.path == "/example/project/releases/latest/download/latest.json");

    Require(duel::update::http::ParseHttpsUrl(
        "https://release-assets.githubusercontent.com/file?download=1", url
    ));
    Require(url.path == "/file?download=1");
    Require(!duel::update::http::ParseHttpsUrl("http://github.com/latest.json", url));
    Require(!duel::update::http::ParseHttpsUrl("/latest.json", url));
    Require(!duel::update::http::ParseHttpsUrl("https://", url));
}

} // namespace

int main() {
    TestRequestHeaders();
    TestHttpsUrlPolicy();
    return 0;
}
