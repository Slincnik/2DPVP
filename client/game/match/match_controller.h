#pragma once

#include "game/input/input.h"
#include "game/match/match_model.h"
#include "game/match/match_transport.h"
#include "game/presentation/match_presentation.h"

namespace duel::game::match {

struct NetworkUpdate {
    bool started = false;
    bool ended = false;
    bool disconnected = false;
};

class MatchController {
public:
    MatchController(
        std::string localPlayerId,
        MatchTransport& transport,
        presentation::MatchPresentation& presentation
    );

    [[nodiscard]] NetworkUpdate PollNetwork();
    [[nodiscard]] bool FixedTick(const input::InputFrame& frame);
    void AdvanceFrame(float elapsedSeconds);

    [[nodiscard]] const MatchModel& Model() const noexcept;

private:
    void ApplySnapshot(const protocol::WorldSnapshot& snapshot);

    static constexpr std::uint32_t kLightAttackRepeatIntervalTicks = 15;

    MatchTransport& transport_;
    presentation::MatchPresentation& presentation_;
    MatchModel model_;
    std::uint32_t nextLightAttackTick_ = 0;
};

} // namespace duel::game::match
