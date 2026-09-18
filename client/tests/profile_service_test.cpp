#include "api/gateway_client.h"
#include "auth/session_manager.h"
#include "features/profile/profile_service.h"

#include <httplib.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

namespace {

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

class MemoryStorage final : public duel::auth::SecureStorage {
public:
    duel::auth::StorageResult Load(const std::string&) override { return {.value = value, .error = {}}; }
    std::string Store(const std::string&, const std::string& stored) override {
        value = stored;
        return {};
    }
    std::string Erase(const std::string&) override {
        value.reset();
        return {};
    }

    std::optional<std::string> value;
};

void WriteAuth(httplib::Response& response) {
    response.set_content(
        R"({"user":{"id":"user-1","login":"alice"},"accessToken":"expired","refreshToken":"refresh"})",
        "application/json"
    );
}

} // namespace

int main() {
    httplib::Server server;
    server.Post("/api/v1/auth/login", [](const httplib::Request&, httplib::Response& response) {
        WriteAuth(response);
    });
    server.Get("/api/v1/profile", [](const httplib::Request&, httplib::Response& response) {
        response.status = 401;
    });
    server.Post("/api/v1/auth/refresh", [](const httplib::Request&, httplib::Response& response) {
        response.status = 401;
    });

    const int port = server.bind_to_any_port("127.0.0.1");
    Require(port > 0);
    std::thread serverThread([&] { server.listen_after_bind(); });
    server.wait_until_ready();

    duel::api::GatewayClient gateway("http://127.0.0.1:" + std::to_string(port));
    MemoryStorage storage;
    duel::auth::SessionManager session(gateway, storage, "profile-test");
    Require(static_cast<bool>(session.Login("alice", "correct horse battery")));
    duel::features::profile::ProfileService profile(session);
    const auto result = profile.Load();

    server.stop();
    serverThread.join();

    Require(!result);
    Require(!session.CurrentSession());
    Require(!storage.value);
    Require(result.error.find("refresh") == std::string::npos);
    Require(result.error.find("token") == std::string::npos);
    return 0;
}
