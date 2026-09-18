#include "game/match/match_controller.h"

#include <algorithm>
#include <utility>

namespace duel::game::match {
namespace {

protocol::ActionType ProtocolAction(input::Action action) noexcept {
    switch (action) {
    case input::Action::Dash: return protocol::ActionType::Dash;
    case input::Action::LightAttack: return protocol::ActionType::LightAttack;
    default: return protocol::ActionType::Unspecified;
    }
}

} // namespace

MatchController::MatchController(
    std::string localPlayerId,
    MatchTransport& transport,
    presentation::MatchPresentation& presentation
) : transport_(transport), presentation_(presentation), model_(std::move(localPlayerId)) {
    presentation_.Reset();
}

NetworkUpdate MatchController::PollNetwork() {
    NetworkUpdate update;
    if (auto start = transport_.PollMatchStart()) {
        model_.tickRate_ = start->tickRate == 0 ? 30 : start->tickRate;
        model_.started_ = true;
        ApplySnapshot(start->initialSnapshot);
        update.started = true;
    }
    if (auto snapshot = transport_.PollSnapshot()) {
        ApplySnapshot(*snapshot);
    }
    if (auto end = transport_.PollMatchEnd()) {
        ApplySnapshot(end->finalSnapshot);
        model_.end_ = std::move(*end);
        update.ended = true;
    }
    update.disconnected = !update.ended && !transport_.IsConnected();
    return update;
}

bool MatchController::FixedTick(const input::InputFrame& frame) {
    if (!model_.started_ || model_.end_
        || model_.world_.status != protocol::MatchStatus::Active) {
        return false;
    }

    for (const auto action : frame.pressed) {
        static_cast<void>(model_.pendingActions_.Enqueue(action));
    }
    ++model_.inputTick_;
    model_.localPrediction_.ApplyInput(model_.inputTick_, frame.moveX, frame.moveY);

    protocol::PlayerInput outgoing{
        .tick = model_.inputTick_,
        .moveX = frame.moveX,
        .moveY = frame.moveY,
        .pendingActions = {},
    };
    outgoing.pendingActions.reserve(model_.pendingActions_.Pending().size());
    for (const auto& command : model_.pendingActions_.Pending()) {
        outgoing.pendingActions.push_back({
            .sequence = command.sequence,
            .type = ProtocolAction(command.action),
        });
    }
    return transport_.SendInput(outgoing);
}

void MatchController::AdvanceFrame(float elapsedSeconds) {
    model_.localPrediction_.AdvanceVisual(std::min(elapsedSeconds * 12.0F, 1.0F));
    model_.opponentInterpolation_.Advance(elapsedSeconds);
    presentation_.Advance(elapsedSeconds);
}

const MatchModel& MatchController::Model() const noexcept { return model_; }

void MatchController::ApplySnapshot(const protocol::WorldSnapshot& snapshot) {
    if (model_.started_ && snapshot.serverTick < model_.world_.serverTick) {
        return;
    }

    for (const auto& player : snapshot.players) {
        if (player.playerId == model_.localPlayerId_) {
            model_.pendingActions_.Acknowledge(player.lastAckedActionSequence);
            model_.localPrediction_.Reconcile(player);
        } else {
            model_.opponentInterpolation_.Push(
                snapshot.serverTick,
                player.positionX,
                player.positionY
            );
        }
    }
    presentation_.ApplySnapshot(snapshot);
    model_.world_ = snapshot;
}

} // namespace duel::game::match
