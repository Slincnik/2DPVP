#include "api/gateway_client.h"

#include <httplib.h>

#include <iostream>
#include <string>
#include <thread>

int main() {
    httplib::Server server;
    server.Post("/api/v1/auth/login", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(
            R"({"user":{"id":"user-1","login":"alice"},"accessToken":"access","refreshToken":"refresh"})",
            "application/json"
        );
    });
    server.Post("/api/v1/queue/join", [](const httplib::Request& request, httplib::Response& response) {
        if (request.get_header_value("Authorization") != "Bearer access") {
            response.status = 401;
            return;
        }
        response.set_content(R"({"status":"waiting"})", "application/json");
    });
    server.Get("/api/v1/queue/status", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(
            R"({"status":"matched","matchId":"match-1","opponentId":"user-2","serverAddr":"localhost:4242","matchTicket":"ticket"})",
            "application/json"
        );
    });

    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        std::cerr << "could not bind test HTTP server\n";
        return 1;
    }
    std::thread serverThread([&] { server.listen_after_bind(); });
    server.wait_until_ready();

    duel::api::GatewayClient client("http://127.0.0.1:" + std::to_string(port));
    const auto auth = client.Login("alice", "correct horse battery");
    const auto waiting = client.JoinQueue(auth.value.accessToken);
    const auto matched = client.QueueStatusFor(auth.value.accessToken);

    server.stop();
    serverThread.join();

    if (!auth || auth.value.userId != "user-1" || auth.value.accessToken != "access") {
        std::cerr << "auth response was not parsed\n";
        return 1;
    }
    if (!waiting || waiting.value.state != duel::api::QueueState::Waiting) {
        std::cerr << "waiting response was not parsed\n";
        return 1;
    }
    if (!matched || matched.value.state != duel::api::QueueState::Matched
        || matched.value.matchTicket != "ticket" || matched.value.serverAddr != "localhost:4242") {
        std::cerr << "matched response was not parsed\n";
        return 1;
    }
    return 0;
}
