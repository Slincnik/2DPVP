#include "api/gateway_client.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <utility>

namespace duel::api {
namespace {

using Json = nlohmann::json;

httplib::Client NewClient(const std::string& baseUrl) {
    httplib::Client client(baseUrl);
    client.set_connection_timeout(std::chrono::seconds(5));
    client.set_read_timeout(std::chrono::seconds(5));
    client.set_write_timeout(std::chrono::seconds(5));
    return client;
}

std::string ResponseError(const httplib::Result& response) {
    if (!response) {
        return "Gateway connection failed: " + httplib::to_string(response.error());
    }
    try {
        const auto body = Json::parse(response->body);
        if (body.contains("error") && body["error"].contains("message")) {
            return body["error"]["message"].get<std::string>();
        }
    } catch (const std::exception&) {
    }
    return "Gateway returned HTTP " + std::to_string(response->status);
}

Result<AuthSession> ParseAuth(const httplib::Result& response) {
    if (!response || response->status < 200 || response->status >= 300) {
        return {.error = ResponseError(response), .status = response ? response->status : 0};
    }
    try {
        const auto body = Json::parse(response->body);
        return {
            .value = AuthSession{
                .userId = body.at("user").at("id").get<std::string>(),
                .login = body.at("user").at("login").get<std::string>(),
                .accessToken = body.at("accessToken").get<std::string>(),
                .refreshToken = body.at("refreshToken").get<std::string>(),
            },
            .error = {},
        };
    } catch (const std::exception& error) {
        return {.error = "Invalid auth response: " + std::string(error.what()), .status = response->status};
    }
}

Result<QueueStatus> ParseQueue(const httplib::Result& response) {
    if (!response || response->status < 200 || response->status >= 300) {
        return {.error = ResponseError(response), .status = response ? response->status : 0};
    }
    try {
        const auto body = Json::parse(response->body);
        const auto status = body.at("status").get<std::string>();
        QueueState state = QueueState::NotQueued;
        if (status == "waiting") {
            state = QueueState::Waiting;
        } else if (status == "matched") {
            state = QueueState::Matched;
        }
        return {
            .value = QueueStatus{
                .state = state,
                .matchId = body.value("matchId", ""),
                .opponentId = body.value("opponentId", ""),
                .serverAddr = body.value("serverAddr", ""),
                .matchTicket = body.value("matchTicket", ""),
            },
            .error = {},
        };
    } catch (const std::exception& error) {
        return {.error = "Invalid queue response: " + std::string(error.what()), .status = response->status};
    }
}

httplib::Headers Bearer(const std::string& accessToken) {
    return {{"Authorization", "Bearer " + accessToken}};
}

} // namespace

GatewayClient::GatewayClient(std::string baseUrl) : baseUrl_(std::move(baseUrl)) {}

Result<AuthSession> GatewayClient::Register(
    const std::string& login,
    const std::string& password
) const {
    return Authenticate("/api/v1/auth/register", login, password);
}

Result<AuthSession> GatewayClient::Login(
    const std::string& login,
    const std::string& password
) const {
    return Authenticate("/api/v1/auth/login", login, password);
}

Result<AuthSession> GatewayClient::Refresh(const AuthSession& session) const {
    auto client = NewClient(baseUrl_);
    const auto payload = Json{{"refreshToken", session.refreshToken}}.dump();
    return ParseAuth(client.Post("/api/v1/auth/refresh", payload, "application/json"));
}

Result<bool> GatewayClient::Logout(const std::string& refreshToken) const {
    auto client = NewClient(baseUrl_);
    const auto payload = Json{{"refreshToken", refreshToken}}.dump();
    const auto response = client.Post("/api/v1/auth/logout", payload, "application/json");
    if (!response || response->status != 204) {
        return {.error = ResponseError(response), .status = response ? response->status : 0};
    }
    return {.value = true, .status = response->status};
}

Result<QueueStatus> GatewayClient::JoinQueue(const std::string& accessToken) const {
    auto client = NewClient(baseUrl_);
    return ParseQueue(client.Post("/api/v1/queue/join", Bearer(accessToken), "", "application/json"));
}

Result<QueueStatus> GatewayClient::QueueStatusFor(const std::string& accessToken) const {
    auto client = NewClient(baseUrl_);
    return ParseQueue(client.Get("/api/v1/queue/status", Bearer(accessToken)));
}

Result<bool> GatewayClient::LeaveQueue(const std::string& accessToken) const {
    auto client = NewClient(baseUrl_);
    const auto response = client.Delete("/api/v1/queue", Bearer(accessToken));
    if (!response || response->status != 204) {
        return {.error = ResponseError(response), .status = response ? response->status : 0};
    }
    return {.value = true, .status = response->status};
}

Result<AuthSession> GatewayClient::Authenticate(
    const std::string& path,
    const std::string& login,
    const std::string& password
) const {
    auto client = NewClient(baseUrl_);
    const auto payload = Json{{"login", login}, {"password", password}}.dump();
    return ParseAuth(client.Post(path, payload, "application/json"));
}

} // namespace duel::api
