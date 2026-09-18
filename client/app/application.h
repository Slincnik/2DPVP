#pragma once

#include "app/screen.h"

#include <memory>
#include <string>

namespace duel::app {

[[nodiscard]] bool CanTransition(ScreenId from, ScreenId to) noexcept;

class Application {
public:
    explicit Application(std::unique_ptr<Screen> initialScreen);
    explicit Application(std::string gatewayUrl);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    [[nodiscard]] ScreenId CurrentScreenId() const noexcept;
    [[nodiscard]] bool SetScreen(std::unique_ptr<Screen> nextScreen) noexcept;
    int Run();

private:
    std::unique_ptr<Screen> currentScreen_;
    std::string gatewayUrl_;
};

} // namespace duel::app
