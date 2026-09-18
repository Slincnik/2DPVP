#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace duel::game::input {

enum class Action {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Dash,
    LightAttack,
};

enum class InputCode {
    KeyA,
    KeyB,
    KeyC,
    KeyD,
    KeyE,
    KeyF,
    KeyG,
    KeyH,
    KeyI,
    KeyJ,
    KeyK,
    KeyL,
    KeyM,
    KeyN,
    KeyO,
    KeyP,
    KeyQ,
    KeyR,
    KeyS,
    KeyT,
    KeyU,
    KeyV,
    KeyW,
    KeyX,
    KeyY,
    KeyZ,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Space,
    LeftShift,
};

struct Binding {
    Action action;
    InputCode input;

    bool operator==(const Binding&) const = default;
};

struct InputFrame {
    std::int8_t moveX = 0;
    std::int8_t moveY = 0;
    std::vector<Action> pressed;
    // Current held state for edge actions. Gameplay may use this to schedule
    // repeat discrete commands without coupling to platform keycodes.
    std::vector<Action> held;
};

struct ActionCommand {
    std::uint32_t sequence = 0;
    Action action = Action::Dash;
};

class PendingActionQueue {
public:
    static constexpr std::size_t kMaximumSize = 8;

    [[nodiscard]] bool Enqueue(Action action) noexcept;
    void Acknowledge(std::uint32_t sequence) noexcept;
    void Reset() noexcept;
    [[nodiscard]] const std::vector<ActionCommand>& Pending() const noexcept;

private:
    std::uint32_t nextSequence_ = 1;
    std::vector<ActionCommand> pending_;
};

class InputSource {
public:
    virtual ~InputSource() = default;

    [[nodiscard]] virtual bool IsDown(InputCode code) const noexcept = 0;
    [[nodiscard]] virtual bool IsPressed(InputCode code) const noexcept = 0;
};

class InputBindings {
public:
    static InputBindings Defaults();

    explicit InputBindings(std::vector<Binding> bindings);

    [[nodiscard]] const std::vector<Binding>& All() const noexcept;
    [[nodiscard]] std::optional<InputCode> InputFor(Action action) const noexcept;
    [[nodiscard]] bool Rebind(Action action, InputCode input, std::string_view* error = nullptr);
    [[nodiscard]] static bool Validate(
        const std::vector<Binding>& bindings,
        std::string_view* error = nullptr
    ) noexcept;

private:
    std::vector<Binding> bindings_;
};

class InputSampler {
public:
    explicit InputSampler(const InputBindings& bindings) noexcept;

    void SetBindings(const InputBindings& bindings) noexcept;
    void Observe(const InputSource& source);
    [[nodiscard]] InputFrame ConsumeFixedTick();

private:
    const InputBindings* bindings_;
    std::int8_t moveX_ = 0;
    std::int8_t moveY_ = 0;
    std::vector<Action> pendingPressed_;
    std::vector<Action> heldActions_;
};

[[nodiscard]] std::string_view ActionName(Action action) noexcept;
[[nodiscard]] std::optional<Action> ParseAction(std::string_view name) noexcept;
[[nodiscard]] std::string_view InputCodeName(InputCode code) noexcept;
[[nodiscard]] std::optional<InputCode> ParseInputCode(std::string_view name) noexcept;

} // namespace duel::game::input
