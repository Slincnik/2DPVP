#include "game/arena/arena_catalog.h"

#include <algorithm>

namespace duel::game::arena {
namespace {

constexpr VisualBounds kSharedVisualBounds{
    .x = 290.0F,
    .y = 150.0F,
    .width = 700.0F,
    .height = 420.0F,
};

} // namespace

ArenaCatalog::ArenaCatalog()
    : scenes_({
        ArenaScene{
            .id = std::string(kDefaultArenaId),
            .displayName = "Neon Rooftop",
            .backgroundAsset = "arena/neon_rooftop",
            .musicCue = "music/neon_rooftop",
            .palette = {
                .backgroundTop = {13, 18, 42},
                .backgroundBottom = {39, 20, 60},
                .floor = {27, 35, 58},
                .border = {63, 205, 255},
                .accent = {232, 84, 255},
            },
            .visualBounds = kSharedVisualBounds,
            .decorations = {
                {DecorationKind::Skyline, 340.0F, 122.0F, 64.0F},
                {DecorationKind::Skyline, 865.0F, 112.0F, 82.0F},
                {DecorationKind::LightColumn, 322.0F, 178.0F, 110.0F},
                {DecorationKind::LightColumn, 958.0F, 178.0F, 110.0F},
            },
        },
        ArenaScene{
            .id = std::string(kEmberFoundryArenaId),
            .displayName = "Ember Foundry",
            .backgroundAsset = "arena/ember_foundry",
            .musicCue = "music/ember_foundry",
            .palette = {
                .backgroundTop = {35, 20, 19},
                .backgroundBottom = {73, 31, 20},
                .floor = {54, 43, 39},
                .border = {242, 133, 58},
                .accent = {255, 207, 91},
            },
            .visualBounds = kSharedVisualBounds,
            .decorations = {
                {DecorationKind::EmberVent, 360.0F, 545.0F, 42.0F},
                {DecorationKind::EmberVent, 920.0F, 545.0F, 42.0F},
                {DecorationKind::LightColumn, 322.0F, 190.0F, 96.0F},
                {DecorationKind::LightColumn, 958.0F, 190.0F, 96.0F},
            },
        },
    }) {}

ArenaSelection ArenaCatalog::Resolve(std::string_view arenaId) const noexcept {
    const auto found = std::find_if(scenes_.begin(), scenes_.end(), [arenaId](const auto& scene) {
        return scene.id == arenaId;
    });
    if (found != scenes_.end()) {
        return {.scene = *found, .usedFallback = false};
    }
    return {.scene = scenes_.front(), .usedFallback = true};
}

const ArenaScene& ArenaCatalog::DefaultScene() const noexcept {
    return scenes_.front();
}

const std::vector<ArenaScene>& ArenaCatalog::Scenes() const noexcept {
    return scenes_;
}

} // namespace duel::game::arena
