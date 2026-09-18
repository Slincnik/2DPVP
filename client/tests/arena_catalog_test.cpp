#include "game/arena/arena_catalog.h"

#include <cstdlib>
#include <string>
#include <string_view>

namespace {

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

} // namespace

int main() {
    duel::game::arena::ArenaCatalog catalog;
    Require(catalog.Scenes().size() >= 2);

    const auto neon = catalog.Resolve(duel::game::arena::kDefaultArenaId);
    const auto foundry = catalog.Resolve(duel::game::arena::kEmberFoundryArenaId);
    Require(!neon.usedFallback);
    Require(!foundry.usedFallback);
    Require(neon.scene.id != foundry.scene.id);
    Require(neon.scene.palette.backgroundTop != foundry.scene.palette.backgroundTop);
    Require(neon.scene.visualBounds == foundry.scene.visualBounds);
    Require(!neon.scene.backgroundAsset.empty());
    Require(!foundry.scene.musicCue.empty());

    const auto unknown = catalog.Resolve("future_arena");
    const auto missing = catalog.Resolve(std::string_view{});
    Require(unknown.usedFallback);
    Require(missing.usedFallback);
    Require(unknown.scene.id == catalog.DefaultScene().id);
    Require(missing.scene.id == std::string(duel::game::arena::kDefaultArenaId));
    Require(unknown.scene.visualBounds == neon.scene.visualBounds);
    return 0;
}
