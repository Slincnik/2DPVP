#include "app/application.h"
#include "platform/platform.h"

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

class RecordingScreen final : public duel::app::Screen {
public:
    RecordingScreen(
        duel::app::ScreenId id,
        std::string name,
        std::vector<std::string>& events
    ) : id_(id), name_(std::move(name)), events_(events) {}

    ~RecordingScreen() override {
        events_.push_back("destroy:" + name_);
    }

    [[nodiscard]] duel::app::ScreenId Id() const noexcept override {
        return id_;
    }

    void OnEnter() noexcept override {
        events_.push_back("enter:" + name_);
    }

    void OnExit() noexcept override {
        events_.push_back("exit:" + name_);
    }

private:
    duel::app::ScreenId id_;
    std::string name_;
    std::vector<std::string>& events_;
};

class FakeWindow final : public duel::platform::Window {
public:
    [[nodiscard]] bool ShouldClose() const noexcept override { return false; }
    [[nodiscard]] float FrameTimeSeconds() const noexcept override { return 1.0F / 60.0F; }
    void BeginFrame() noexcept override {}
    void EndFrame() noexcept override {}
};

void TestTransitionMatrix() {
    using duel::app::CanTransition;
    using duel::app::ScreenId;

    Require(CanTransition(ScreenId::SessionRestore, ScreenId::Login));
    Require(CanTransition(ScreenId::SessionRestore, ScreenId::MainMenu));
    Require(CanTransition(ScreenId::Login, ScreenId::MainMenu));
    Require(CanTransition(ScreenId::MainMenu, ScreenId::Profile));
    Require(CanTransition(ScreenId::MainMenu, ScreenId::Queue));
    Require(CanTransition(ScreenId::MainMenu, ScreenId::Settings));
    Require(CanTransition(ScreenId::MainMenu, ScreenId::Login));
    Require(CanTransition(ScreenId::Profile, ScreenId::MainMenu));
    Require(CanTransition(ScreenId::Queue, ScreenId::MainMenu));
    Require(CanTransition(ScreenId::Queue, ScreenId::Match));
    Require(CanTransition(ScreenId::Match, ScreenId::Result));
    Require(CanTransition(ScreenId::Match, ScreenId::MainMenu));
    Require(CanTransition(ScreenId::Result, ScreenId::MainMenu));
    Require(CanTransition(ScreenId::Result, ScreenId::Queue));
    Require(CanTransition(ScreenId::Settings, ScreenId::MainMenu));

    Require(!CanTransition(ScreenId::Login, ScreenId::Match));
    Require(!CanTransition(ScreenId::Settings, ScreenId::Result));
    Require(!CanTransition(ScreenId::MainMenu, ScreenId::MainMenu));
}

void TestApplicationLifecycle() {
    using duel::app::Application;
    using duel::app::ScreenId;

    std::vector<std::string> events;
    {
        Application application(std::make_unique<RecordingScreen>(ScreenId::Login, "login", events));
        Require(application.CurrentScreenId() == ScreenId::Login);
        Require(events == std::vector<std::string>{"enter:login"});

        const auto changed = application.SetScreen(
            std::make_unique<RecordingScreen>(ScreenId::MainMenu, "menu", events)
        );
        Require(changed);
        Require(application.CurrentScreenId() == ScreenId::MainMenu);
        Require(events == std::vector<std::string>{
            "enter:login", "exit:login", "destroy:login", "enter:menu"
        });
    }
    Require(events == std::vector<std::string>{
        "enter:login", "exit:login", "destroy:login", "enter:menu", "exit:menu", "destroy:menu"
    });
}

void TestRejectedTransitionKeepsCurrentScreen() {
    using duel::app::Application;
    using duel::app::ScreenId;

    std::vector<std::string> events;
    Application application(std::make_unique<RecordingScreen>(ScreenId::MainMenu, "menu", events));
    const auto changed = application.SetScreen(
        std::make_unique<RecordingScreen>(ScreenId::Match, "match", events)
    );

    Require(!changed);
    Require(application.CurrentScreenId() == ScreenId::MainMenu);
    Require(events == std::vector<std::string>{"enter:menu", "destroy:match"});
}

void TestInitialScreenIsRequired() {
    try {
        duel::app::Application application(std::unique_ptr<duel::app::Screen>{});
        Require(false);
    } catch (const std::invalid_argument&) {
    }
}

void TestGatewayUrlIsRequired() {
    try {
        duel::app::Application application(std::string{});
        Require(false);
    } catch (const std::invalid_argument&) {
    }

    duel::app::Application application("http://localhost:8080");
}

} // namespace

int main() {
    TestTransitionMatrix();
    TestApplicationLifecycle();
    TestRejectedTransitionKeepsCurrentScreen();
    TestInitialScreenIsRequired();
    TestGatewayUrlIsRequired();

    FakeWindow window;
    Require(!window.ShouldClose());
    Require(window.FrameTimeSeconds() > 0.0F);
    return 0;
}
