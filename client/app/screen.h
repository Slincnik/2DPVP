#pragma once

namespace duel::app {

enum class ScreenId {
    SessionRestore,
    Login,
    MainMenu,
    Profile,
    Queue,
    Match,
    Result,
    Settings,
};

class Screen {
public:
    virtual ~Screen() = default;

    [[nodiscard]] virtual ScreenId Id() const noexcept = 0;
    virtual void OnEnter() noexcept {}
    virtual void OnExit() noexcept {}
};

} // namespace duel::app
