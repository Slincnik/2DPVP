#include "app/application.h"
#include "update/client_updater.h"

#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
#ifdef PVP_DUEL_UPDATE_MANIFEST_URL
    const std::string executablePath = argc > 0 && argv[0] != nullptr ? argv[0] : std::string{};
    duel::update::CheckAndStart(PVP_DUEL_UPDATE_MANIFEST_URL, executablePath);
#endif
#ifdef PVP_DUEL_DEFAULT_GATEWAY_URL
    constexpr const char* defaultGateway = PVP_DUEL_DEFAULT_GATEWAY_URL;
#else
    constexpr const char* defaultGateway = "http://localhost:8080";
#endif
    const char* configuredGateway = std::getenv("GATEWAY_URL");
    const std::string gatewayUrl = configuredGateway != nullptr ? configuredGateway : defaultGateway;

    duel::app::Application application(gatewayUrl);
    return application.Run();
}
