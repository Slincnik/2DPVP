#include "app/application.h"

#include <cstdlib>
#include <string>

int main() {
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
