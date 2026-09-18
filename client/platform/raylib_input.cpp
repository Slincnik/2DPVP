#include "platform/raylib_input.h"

#include "raylib.h"

#include <array>

namespace duel::platform {
namespace {

using game::input::InputCode;

int RaylibKey(InputCode code) noexcept {
    switch (code) {
    case InputCode::KeyA: return KEY_A;
    case InputCode::KeyB: return KEY_B;
    case InputCode::KeyC: return KEY_C;
    case InputCode::KeyD: return KEY_D;
    case InputCode::KeyE: return KEY_E;
    case InputCode::KeyF: return KEY_F;
    case InputCode::KeyG: return KEY_G;
    case InputCode::KeyH: return KEY_H;
    case InputCode::KeyI: return KEY_I;
    case InputCode::KeyJ: return KEY_J;
    case InputCode::KeyK: return KEY_K;
    case InputCode::KeyL: return KEY_L;
    case InputCode::KeyM: return KEY_M;
    case InputCode::KeyN: return KEY_N;
    case InputCode::KeyO: return KEY_O;
    case InputCode::KeyP: return KEY_P;
    case InputCode::KeyQ: return KEY_Q;
    case InputCode::KeyR: return KEY_R;
    case InputCode::KeyS: return KEY_S;
    case InputCode::KeyT: return KEY_T;
    case InputCode::KeyU: return KEY_U;
    case InputCode::KeyV: return KEY_V;
    case InputCode::KeyW: return KEY_W;
    case InputCode::KeyX: return KEY_X;
    case InputCode::KeyY: return KEY_Y;
    case InputCode::KeyZ: return KEY_Z;
    case InputCode::ArrowUp: return KEY_UP;
    case InputCode::ArrowDown: return KEY_DOWN;
    case InputCode::ArrowLeft: return KEY_LEFT;
    case InputCode::ArrowRight: return KEY_RIGHT;
    case InputCode::Space: return KEY_SPACE;
    case InputCode::LeftShift: return KEY_LEFT_SHIFT;
    }
    return KEY_NULL;
}

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

} // namespace

bool RaylibInputAdapter::IsDown(InputCode code) const noexcept {
    return IsKeyDown(RaylibKey(code));
}

bool RaylibInputAdapter::IsPressed(InputCode code) const noexcept {
    return IsKeyPressed(RaylibKey(code));
}

std::optional<InputCode> RaylibInputAdapter::PressedInputCode() const noexcept {
    for (const auto code : kInputCodes) {
        if (IsPressed(code)) {
            return code;
        }
    }
    return std::nullopt;
}

} // namespace duel::platform
