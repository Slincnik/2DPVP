#include "api/gateway_client.h"
#include "auth/session_manager.h"

#include <httplib.h>

#include <iostream>
#include <string>
#include <thread>

namespace {

class MemoryStorage final : public duel::auth::SecureStorage {
public:
    duel::auth::StorageResult Load(const std::string&) override { return {.value = value}; }
    std::string Store(const std::string&, const std::string& stored) override {
        value = stored;
        ++storeCalls;
        return {};
    }
    std::string Erase(const std::string&) override {
        value.reset();
        return {};
    }

    std::optional<std::string> value;
    int storeCalls = 0;
};

void WriteAuth(httplib::Response& response, const char* access, const char* refresh) {
    response.set_content(
        std::string(R"({"user":{"id":"user-1","login":"alice"},"accessToken":")") + access
            + R"(","refreshToken":")" + refresh + R"("})",
        "application/json"
    );
}

} // namespace

int main() {
    httplib::Server server;
    int joins = 0;
    int profiles = 0;
    int refreshes = 0;
    server.Post("/api/v1/auth/login", [](const httplib::Request&, httplib::Response& response) {
        WriteAuth(response, "expired-access", "initial-refresh");
    });
    server.Post("/api/v1/auth/refresh", [&](const httplib::Request& request, httplib::Response& response) {
        if (request.body != R"({"refreshToken":"initial-refresh"})") {
            response.status = 401;
            return;
        }
        ++refreshes;
        WriteAuth(response, "fresh-access", "rotated-refresh");
    });
    server.Post("/api/v1/queue/join", [&](const httplib::Request& request, httplib::Response& response) {
        ++joins;
        if (request.get_header_value("Authorization") != "Bearer fresh-access") {
            response.status = 401;
            return;
        }
        response.set_content(R"({"status":"waiting"})", "application/json");
    });
    server.Get("/api/v1/profile", [&](const httplib::Request& request, httplib::Response& response) {
        ++profiles;
        if (request.get_header_value("Authorization") != "Bearer fresh-access") {
            response.status = 401;
            return;
        }
        response.set_content(
            R"({"id":"user-1","login":"alice","createdAt":"2026-09-18T00:00:00Z","statistics":{"played":1,"wins":1,"losses":0,"draws":0}})",
            "application/json"
        );
    });
    server.Post("/api/v1/auth/logout", [](const httplib::Request& request, httplib::Response& response) {
        if (request.body == R"({"refreshToken":"rotated-refresh"})") {
            response.status = 204;
        } else {
            response.status = 400;
        }
    });

    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        std::cerr << "could not bind test HTTP server\n";
        return 1;
    }
    std::thread serverThread([&] { server.listen_after_bind(); });
    server.wait_until_ready();

    duel::api::GatewayClient client("http://127.0.0.1:" + std::to_string(port));
    MemoryStorage storage;
    duel::auth::SessionManager session(client, storage, "test-gateway");
    const auto login = session.Login("alice", "correct horse battery");
    const auto profile = session.Profile();
    const auto join = session.JoinQueue();
    const auto logout = session.Logout();

    server.stop();
    serverThread.join();

    if (!login || !join || !profile || profile.value.statistics.wins != 1 || !logout
        || joins != 1 || profiles != 2 || refreshes != 1 || storage.value || storage.storeCalls != 2
        || session.CurrentSession()) {
        std::cerr << "session refresh/retry/logout behaviour was incorrect\n";
        return 1;
    }
    return 0;
}
