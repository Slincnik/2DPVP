// Package room contains the deterministic, authoritative duel simulation.
package room

import (
	"errors"
	"fmt"
)

const (
	TickRate               = 30
	CountdownTicks         = 3 * TickRate
	MatchDurationTicks     = 90 * TickRate
	InitialHP              = 100
	MovementPerTick        = 10
	AttackDamage           = 20
	AttackHitboxDepth      = 80
	AttackHitboxHalfWidth  = 45
	PlayerHitboxHalfExtent = 20
	AttackCooldown         = 15

	ArenaMinX = -500
	ArenaMaxX = 500
	ArenaMinY = -300
	ArenaMaxY = 300

	PlayerASpawnX = -250
	PlayerBSpawnX = 250
)

var (
	ErrInvalidPlayers = errors.New("room needs two distinct player IDs")
	ErrUnknownPlayer  = errors.New("unknown player")
)

type MatchStatus uint8

const (
	// Keep Active and Finished first to mirror their established protobuf values.
	MatchActive MatchStatus = iota
	MatchFinished
	MatchWaiting
	MatchCountdown
)

type FinishReason uint8

const (
	FinishReasonNone FinishReason = iota
	FinishReasonKO
	FinishReasonTimeLimit
)

type Input struct {
	Tick   uint32
	MoveX  int8
	MoveY  int8
	Attack bool
}

type PlayerState struct {
	ID                 string
	PositionX          int32
	PositionY          int32
	HP                 int32
	LastAckedInputTick uint32
	FacingX            int8
	FacingY            int8
}

type Snapshot struct {
	ServerTick              uint32
	Players                 [2]PlayerState
	Status                  MatchStatus
	WinnerID                string
	FinishReason            FinishReason
	CountdownTicksRemaining uint32
	MatchTicksRemaining     uint32
}

type Room struct {
	players        [2]PlayerState
	inputs         [2]Input
	nextAttackTick [2]uint32
	tick           uint32
	activeTicks    uint32
	status         MatchStatus
	winnerID       string
	finishReason   FinishReason
}

func New(playerA, playerB string) (*Room, error) {
	if playerA == "" || playerB == "" || playerA == playerB {
		return nil, ErrInvalidPlayers
	}

	return &Room{
		players: [2]PlayerState{
			{ID: playerA, PositionX: PlayerASpawnX, HP: InitialHP, FacingX: 1},
			{ID: playerB, PositionX: PlayerBSpawnX, HP: InitialHP, FacingX: -1},
		},
		status: MatchCountdown,
	}, nil
}

// SubmitInput accepts only a newer input for the supplied player. The room is
// deliberately single-owner: call SubmitInput and Step from its tick goroutine.
func (r *Room) SubmitInput(playerID string, input Input) error {
	playerIndex, err := r.playerIndex(playerID)
	if err != nil {
		return err
	}
	if input.Tick <= r.inputs[playerIndex].Tick {
		return nil
	}

	input.MoveX = clampAxis(input.MoveX)
	input.MoveY = clampAxis(input.MoveY)
	r.inputs[playerIndex] = input
	return nil
}

// Step advances the lifecycle and simulation by exactly one fixed tick.
func (r *Room) Step() Snapshot {
	if r.status == MatchFinished {
		return r.Snapshot()
	}

	r.tick++
	if r.status == MatchCountdown {
		if r.tick >= CountdownTicks {
			r.status = MatchActive
			// Inputs sent during countdown must never take effect after it ends.
			for index := range r.inputs {
				r.inputs[index] = Input{Tick: r.inputs[index].Tick}
				r.nextAttackTick[index] = r.tick + 1
			}
		}
		return r.Snapshot()
	}

	r.activeTicks++
	r.applyMovement()
	r.applyAttacks()
	r.finishFromKO()
	if r.status != MatchFinished && r.activeTicks >= MatchDurationTicks {
		r.finishFromTimeLimit()
	}
	return r.Snapshot()
}

func (r *Room) Snapshot() Snapshot {
	snapshot := Snapshot{
		ServerTick:   r.tick,
		Players:      r.players,
		Status:       r.status,
		WinnerID:     r.winnerID,
		FinishReason: r.finishReason,
	}
	if r.status == MatchCountdown && r.tick < CountdownTicks {
		snapshot.CountdownTicksRemaining = CountdownTicks - r.tick
	}
	if r.status == MatchActive && r.activeTicks < MatchDurationTicks {
		snapshot.MatchTicksRemaining = MatchDurationTicks - r.activeTicks
	}
	return snapshot
}

func (r *Room) applyMovement() {
	for index := range r.players {
		if r.players[index].HP <= 0 {
			continue
		}

		input := r.inputs[index]
		r.players[index].updateFacing(input)
		r.players[index].PositionX = clampPosition(
			r.players[index].PositionX+int32(input.MoveX)*MovementPerTick,
			ArenaMinX,
			ArenaMaxX,
		)
		r.players[index].PositionY = clampPosition(
			r.players[index].PositionY+int32(input.MoveY)*MovementPerTick,
			ArenaMinY,
			ArenaMaxY,
		)
		r.players[index].LastAckedInputTick = input.Tick
	}
}

func (r *Room) applyAttacks() {
	var damage [2]int32
	for attacker := range r.players {
		target := 1 - attacker
		if !r.canAttack(attacker) || !attackHitboxIntersects(r.players[attacker], r.players[target]) {
			continue
		}

		damage[target] += AttackDamage
		r.nextAttackTick[attacker] = r.tick + AttackCooldown
	}

	for index := range r.players {
		r.players[index].HP = max(r.players[index].HP-damage[index], 0)
	}
}

func (r *Room) canAttack(playerIndex int) bool {
	return r.players[playerIndex].HP > 0 && r.inputs[playerIndex].Attack && r.tick >= r.nextAttackTick[playerIndex]
}

func (r *Room) finishFromKO() {
	playerAAlive := r.players[0].HP > 0
	playerBAlive := r.players[1].HP > 0
	if playerAAlive && playerBAlive {
		return
	}

	r.status = MatchFinished
	r.finishReason = FinishReasonKO
	switch {
	case playerAAlive:
		r.winnerID = r.players[0].ID
	case playerBAlive:
		r.winnerID = r.players[1].ID
	}
}

func (r *Room) finishFromTimeLimit() {
	r.status = MatchFinished
	r.finishReason = FinishReasonTimeLimit
	switch {
	case r.players[0].HP > r.players[1].HP:
		r.winnerID = r.players[0].ID
	case r.players[1].HP > r.players[0].HP:
		r.winnerID = r.players[1].ID
	}
}

func (r *Room) playerIndex(playerID string) (int, error) {
	for index, player := range r.players {
		if player.ID == playerID {
			return index, nil
		}
	}
	return 0, fmt.Errorf("%w: %s", ErrUnknownPlayer, playerID)
}

func clampAxis(axis int8) int8 {
	if axis < -1 {
		return -1
	}
	if axis > 1 {
		return 1
	}
	return axis
}

func clampPosition(position, minimum, maximum int32) int32 {
	return min(max(position, minimum), maximum)
}

func (player *PlayerState) updateFacing(input Input) {
	if input.MoveX == 0 && input.MoveY == 0 {
		return
	}
	if absolute(int(input.MoveX)) >= absolute(int(input.MoveY)) {
		player.FacingX = input.MoveX
		player.FacingY = 0
		return
	}
	player.FacingX = 0
	player.FacingY = input.MoveY
}

// attackHitboxIntersects tests an axis-aligned forward attack rectangle against
// the target's body hitbox. Facing is cardinal, so all calculations stay
// deterministic integer arithmetic.
func attackHitboxIntersects(attacker, target PlayerState) bool {
	targetMinX := target.PositionX - PlayerHitboxHalfExtent
	targetMaxX := target.PositionX + PlayerHitboxHalfExtent
	targetMinY := target.PositionY - PlayerHitboxHalfExtent
	targetMaxY := target.PositionY + PlayerHitboxHalfExtent

	switch {
	case attacker.FacingX > 0:
		return rangesOverlap(attacker.PositionX, attacker.PositionX+AttackHitboxDepth, targetMinX, targetMaxX) &&
			rangesOverlap(attacker.PositionY-AttackHitboxHalfWidth, attacker.PositionY+AttackHitboxHalfWidth, targetMinY, targetMaxY)
	case attacker.FacingX < 0:
		return rangesOverlap(attacker.PositionX-AttackHitboxDepth, attacker.PositionX, targetMinX, targetMaxX) &&
			rangesOverlap(attacker.PositionY-AttackHitboxHalfWidth, attacker.PositionY+AttackHitboxHalfWidth, targetMinY, targetMaxY)
	case attacker.FacingY > 0:
		return rangesOverlap(attacker.PositionX-AttackHitboxHalfWidth, attacker.PositionX+AttackHitboxHalfWidth, targetMinX, targetMaxX) &&
			rangesOverlap(attacker.PositionY, attacker.PositionY+AttackHitboxDepth, targetMinY, targetMaxY)
	default: // FacingY < 0
		return rangesOverlap(attacker.PositionX-AttackHitboxHalfWidth, attacker.PositionX+AttackHitboxHalfWidth, targetMinX, targetMaxX) &&
			rangesOverlap(attacker.PositionY-AttackHitboxDepth, attacker.PositionY, targetMinY, targetMaxY)
	}
}

func rangesOverlap(firstMin, firstMax, secondMin, secondMax int32) bool {
	return firstMin <= secondMax && secondMin <= firstMax
}

func absolute(value int) int {
	if value < 0 {
		return -value
	}
	return value
}
