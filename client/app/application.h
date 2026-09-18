#pragma once

#include "app/screen.h"

#include <memory>

namespace duel::app {

[[nodiscard]] bool CanTransition(ScreenId from, ScreenId to) noexcept;

class Application {
public:
    explicit Application(std::unique_ptr<Screen> initialScreen);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    [[nodiscard]] ScreenId CurrentScreenId() const noexcept;
    [[nodiscard]] bool SetScreen(std::unique_ptr<Screen> nextScreen) noexcept;

private:
    std::unique_ptr<Screen> currentScreen_;
};

} // namespace duel::app
