#include "app/application.h"

#include <stdexcept>
#include <utility>

namespace duel::app {

bool CanTransition(ScreenId from, ScreenId to) noexcept {
    switch (from) {
    case ScreenId::SessionRestore:
        return to == ScreenId::Login || to == ScreenId::MainMenu;
    case ScreenId::Login:
        return to == ScreenId::MainMenu;
    case ScreenId::MainMenu:
        return to == ScreenId::Profile || to == ScreenId::Queue
            || to == ScreenId::Settings || to == ScreenId::Login;
    case ScreenId::Profile:
        return to == ScreenId::MainMenu;
    case ScreenId::Queue:
        return to == ScreenId::MainMenu || to == ScreenId::Match;
    case ScreenId::Match:
        return to == ScreenId::Result || to == ScreenId::MainMenu;
    case ScreenId::Result:
        return to == ScreenId::MainMenu || to == ScreenId::Queue;
    case ScreenId::Settings:
        return to == ScreenId::MainMenu;
    }
    return false;
}

Application::Application(std::unique_ptr<Screen> initialScreen)
    : currentScreen_(std::move(initialScreen)) {
    if (!currentScreen_) {
        throw std::invalid_argument("Application requires an initial screen");
    }
    currentScreen_->OnEnter();
}

Application::~Application() {
    currentScreen_->OnExit();
}

ScreenId Application::CurrentScreenId() const noexcept {
    return currentScreen_->Id();
}

bool Application::SetScreen(std::unique_ptr<Screen> nextScreen) noexcept {
    if (!nextScreen || !CanTransition(currentScreen_->Id(), nextScreen->Id())) {
        return false;
    }

    currentScreen_->OnExit();
    currentScreen_ = std::move(nextScreen);
    currentScreen_->OnEnter();
    return true;
}

} // namespace duel::app
