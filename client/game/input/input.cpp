#include "game/input/input.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace duel::game::input {
namespace {

constexpr std::array<Action, 6> kActions{
    Action::MoveUp,
    Action::MoveDown,
    Action::MoveLeft,
    Action::MoveRight,
    Action::Dash,
    Action::LightAttack,
};

constexpr std::array<InputCode, 32> kInputCodes{
    InputCode::KeyA, InputCode::KeyB, InputCode::KeyC, InputCode::KeyD,
    InputCode::KeyE, InputCode::KeyF, InputCode::KeyG, InputCode::KeyH,
    InputCode::KeyI, InputCode::KeyJ, InputCode::KeyK, InputCode::KeyL,
    InputCode::KeyM, InputCode::KeyN, InputCode::KeyO, InputCode::KeyP,
    InputCode::KeyQ, InputCode::KeyR, InputCode::KeyS, InputCode::KeyT,
    InputCode::KeyU, InputCode::KeyV, InputCode::KeyW, InputCode::KeyX,
    InputCode::KeyY, InputCode::KeyZ, InputCode::ArrowUp, InputCode::ArrowDown,
    InputCode::ArrowLeft, InputCode::ArrowRight, InputCode::Space, InputCode::LeftShift,
};

bool IsEdgeAction(Action action) noexcept {
    return action == Action::Dash || action == Action::LightAttack;
}

void SetError(std::string_view* error, std::string_view message) noexcept {
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace

InputBindings InputBindings::Defaults() {
    return InputBindings({
        {Action::MoveUp, InputCode::KeyW},
        {Action::MoveDown, InputCode::KeyS},
        {Action::MoveLeft, InputCode::KeyA},
        {Action::MoveRight, InputCode::KeyD},
        {Action::Dash, InputCode::LeftShift},
        {Action::LightAttack, InputCode::Space},
    });
}

InputBindings::InputBindings(std::vector<Binding> bindings)
    : bindings_(std::move(bindings)) {
    std::string_view error;
    if (!Validate(bindings_, &error)) {
        throw std::invalid_argument(std::string(error));
    }
}

const std::vector<Binding>& InputBindings::All() const noexcept {
    return bindings_;
}

std::optional<InputCode> InputBindings::InputFor(Action action) const noexcept {
    const auto binding = std::find_if(bindings_.begin(), bindings_.end(), [action](const auto& value) {
        return value.action == action;
    });
    if (binding == bindings_.end()) {
        return std::nullopt;
    }
    return binding->input;
}

bool InputBindings::Rebind(Action action, InputCode input, std::string_view* error) {
    auto candidate = bindings_;
    const auto binding = std::find_if(candidate.begin(), candidate.end(), [action](const auto& value) {
        return value.action == action;
    });
    if (binding == candidate.end()) {
        SetError(error, "unknown action");
        return false;
    }
    binding->input = input;
    if (!Validate(candidate, error)) {
        return false;
    }
    bindings_ = std::move(candidate);
    return true;
}

bool InputBindings::Validate(
    const std::vector<Binding>& bindings,
    std::string_view* error
) noexcept {
    if (bindings.size() != kActions.size()) {
        SetError(error, "each action must have exactly one binding");
        return false;
    }
    for (const auto action : kActions) {
        const auto count = std::count_if(bindings.begin(), bindings.end(), [action](const auto& value) {
            return value.action == action;
        });
        if (count != 1) {
            SetError(error, "each action must have exactly one binding");
            return false;
        }
    }
    for (auto first = bindings.begin(); first != bindings.end(); ++first) {
        if (std::any_of(std::next(first), bindings.end(), [first](const auto& value) {
                return value.input == first->input;
            })) {
            SetError(error, "an input cannot be assigned to multiple actions");
            return false;
        }
    }
    SetError(error, {});
    return true;
}

InputSampler::InputSampler(const InputBindings& bindings) noexcept
    : bindings_(&bindings) {}

void InputSampler::SetBindings(const InputBindings& bindings) noexcept {
    bindings_ = &bindings;
}

void InputSampler::Observe(const InputSource& source) {
    const auto down = [&](Action action) {
        const auto code = bindings_->InputFor(action);
        return code && source.IsDown(*code);
    };
    moveX_ = static_cast<std::int8_t>(static_cast<int>(down(Action::MoveRight))
        - static_cast<int>(down(Action::MoveLeft)));
    moveY_ = static_cast<std::int8_t>(static_cast<int>(down(Action::MoveDown))
        - static_cast<int>(down(Action::MoveUp)));

    for (const auto action : kActions) {
        const auto code = bindings_->InputFor(action);
        if (!IsEdgeAction(action) || !code || !source.IsPressed(*code)) {
            continue;
        }
        if (std::find(pendingPressed_.begin(), pendingPressed_.end(), action) == pendingPressed_.end()) {
            pendingPressed_.push_back(action);
        }
    }
}

InputFrame InputSampler::ConsumeFixedTick() {
    InputFrame frame{
        .moveX = moveX_,
        .moveY = moveY_,
        .pressed = std::move(pendingPressed_),
    };
    pendingPressed_.clear();
    return frame;
}

std::string_view ActionName(Action action) noexcept {
    switch (action) {
    case Action::MoveUp: return "move_up";
    case Action::MoveDown: return "move_down";
    case Action::MoveLeft: return "move_left";
    case Action::MoveRight: return "move_right";
    case Action::Dash: return "dash";
    case Action::LightAttack: return "light_attack";
    }
    return {};
}

std::optional<Action> ParseAction(std::string_view name) noexcept {
    for (const auto action : kActions) {
        if (ActionName(action) == name) {
            return action;
        }
    }
    return std::nullopt;
}

std::string_view InputCodeName(InputCode code) noexcept {
    switch (code) {
    case InputCode::KeyA: return "key_a";
    case InputCode::KeyB: return "key_b";
    case InputCode::KeyC: return "key_c";
    case InputCode::KeyD: return "key_d";
    case InputCode::KeyE: return "key_e";
    case InputCode::KeyF: return "key_f";
    case InputCode::KeyG: return "key_g";
    case InputCode::KeyH: return "key_h";
    case InputCode::KeyI: return "key_i";
    case InputCode::KeyJ: return "key_j";
    case InputCode::KeyK: return "key_k";
    case InputCode::KeyL: return "key_l";
    case InputCode::KeyM: return "key_m";
    case InputCode::KeyN: return "key_n";
    case InputCode::KeyO: return "key_o";
    case InputCode::KeyP: return "key_p";
    case InputCode::KeyQ: return "key_q";
    case InputCode::KeyR: return "key_r";
    case InputCode::KeyS: return "key_s";
    case InputCode::KeyT: return "key_t";
    case InputCode::KeyU: return "key_u";
    case InputCode::KeyV: return "key_v";
    case InputCode::KeyW: return "key_w";
    case InputCode::KeyX: return "key_x";
    case InputCode::KeyY: return "key_y";
    case InputCode::KeyZ: return "key_z";
    case InputCode::ArrowUp: return "arrow_up";
    case InputCode::ArrowDown: return "arrow_down";
    case InputCode::ArrowLeft: return "arrow_left";
    case InputCode::ArrowRight: return "arrow_right";
    case InputCode::Space: return "space";
    case InputCode::LeftShift: return "left_shift";
    }
    return {};
}

std::optional<InputCode> ParseInputCode(std::string_view name) noexcept {
    for (const auto code : kInputCodes) {
        if (InputCodeName(code) == name) {
            return code;
        }
    }
    return std::nullopt;
}

} // namespace duel::game::input
