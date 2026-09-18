#include "game/match/match_model.h"

#include <utility>

namespace duel::game::match {

MatchModel::MatchModel(std::string localPlayerId)
    : localPlayerId_(std::move(localPlayerId)) {}

const std::string& MatchModel::LocalPlayerId() const noexcept { return localPlayerId_; }
const protocol::WorldSnapshot& MatchModel::World() const noexcept { return world_; }
const std::optional<protocol::MatchEnd>& MatchModel::End() const noexcept { return end_; }
const std::string& MatchModel::ArenaId() const noexcept { return arenaId_; }
std::uint32_t MatchModel::TickRate() const noexcept { return tickRate_; }
bool MatchModel::Started() const noexcept { return started_; }
const Prediction& MatchModel::LocalPrediction() const noexcept { return localPrediction_; }
const InterpolationBuffer& MatchModel::OpponentInterpolation() const noexcept {
    return opponentInterpolation_;
}
std::size_t MatchModel::PendingActionCount() const noexcept {
    return pendingActions_.Pending().size();
}

} // namespace duel::game::match
