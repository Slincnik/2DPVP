#pragma once

namespace duel::platform {

// Platform window boundary used by the future application loop. Implementations
// may use raylib, while app and game code remain independent of raylib types.
class Window {
public:
    virtual ~Window() = default;

    [[nodiscard]] virtual bool ShouldClose() const noexcept = 0;
    [[nodiscard]] virtual float FrameTimeSeconds() const noexcept = 0;
    virtual void BeginFrame() noexcept = 0;
    virtual void EndFrame() noexcept = 0;
};

} // namespace duel::platform
