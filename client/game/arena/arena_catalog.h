#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace duel::game::arena {

inline constexpr std::string_view kDefaultArenaId = "neon_rooftop";
inline constexpr std::string_view kEmberFoundryArenaId = "ember_foundry";

struct RgbColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    bool operator==(const RgbColor&) const = default;
};

struct VisualBounds {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    bool operator==(const VisualBounds&) const = default;
};

struct ArenaPalette {
    RgbColor backgroundTop;
    RgbColor backgroundBottom;
    RgbColor floor;
    RgbColor border;
    RgbColor accent;
};

enum class DecorationKind {
    Skyline,
    LightColumn,
    EmberVent,
};

struct ArenaDecoration {
    DecorationKind kind = DecorationKind::Skyline;
    float x = 0.0F;
    float y = 0.0F;
    float size = 0.0F;
};

// ArenaScene contains presentation metadata only. Authoritative bounds,
// collision and spawn rules remain server-owned and are not represented here.
struct ArenaScene {
    std::string id;
    std::string displayName;
    std::string backgroundAsset;
    std::string musicCue;
    ArenaPalette palette;
    VisualBounds visualBounds;
    std::vector<ArenaDecoration> decorations;
};

struct ArenaSelection {
    const ArenaScene& scene;
    bool usedFallback = false;
};

class ArenaCatalog {
public:
    ArenaCatalog();

    [[nodiscard]] ArenaSelection Resolve(std::string_view arenaId) const noexcept;
    [[nodiscard]] const ArenaScene& DefaultScene() const noexcept;
    [[nodiscard]] const std::vector<ArenaScene>& Scenes() const noexcept;

private:
    std::vector<ArenaScene> scenes_;
};

} // namespace duel::game::arena
