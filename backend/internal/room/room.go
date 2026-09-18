// Package room contains the deterministic, authoritative duel simulation.
package room

import (
	"errors"
	"fmt"
	"sort"
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
	LightAttackCooldown    = 15
	LightAttackWindup      = 3
	LightAttackActive      = 1
	LightAttackRecovery    = 6
	DashCooldown           = 30
	DashDistance           = 120
	MaxPendingActions      = 8
	MaxActionSequenceAhead = 64
	MaxInputTickAdvance    = 120

	ArenaMinX = -500
	ArenaMaxX = 500
	ArenaMinY = -300
	ArenaMaxY = 300

	PlayerASpawnX = -250
	PlayerBSpawnX = 250

	ArenaIDNeonRooftop  = "neon_rooftop"
	ArenaIDEmberFoundry = "ember_foundry"
)

var (
	ErrInvalidPlayers = errors.New("room needs two distinct player IDs")
	ErrInvalidRuleset = errors.New("invalid room ruleset")
	ErrUnknownPlayer  = errors.New("unknown player")
)

type ArenaDefinition struct {
	ID         string
	MinX, MaxX int32
	MinY, MaxY int32
	SpawnAX    int32
	SpawnAY    int32
	SpawnBX    int32
	SpawnBY    int32
}

type ActionDefinition struct {
	CooldownTicks uint32
	WindupTicks   uint32
	ActiveTicks   uint32
	RecoveryTicks uint32
	Damage        int32
	DashDistance  int32
}

type Ruleset struct {
	TickRate           uint32
	CountdownTicks     uint32
	MatchDurationTicks uint32
	InitialHP          int32
	MovementPerTick    int32
	AttackDepth        int32
	AttackHalfWidth    int32
	PlayerHalfExtent   int32
	Arena              ArenaDefinition
	Dash               ActionDefinition
	LightAttack        ActionDefinition
}

func DefaultRuleset() Ruleset {
	return Ruleset{
		TickRate: TickRate, CountdownTicks: CountdownTicks, MatchDurationTicks: MatchDurationTicks,
		InitialHP: InitialHP, MovementPerTick: MovementPerTick,
		AttackDepth: AttackHitboxDepth, AttackHalfWidth: AttackHitboxHalfWidth,
		PlayerHalfExtent: PlayerHitboxHalfExtent,
		Arena: ArenaDefinition{ID: ArenaIDNeonRooftop,
			MinX: ArenaMinX, MaxX: ArenaMaxX, MinY: ArenaMinY, MaxY: ArenaMaxY,
			SpawnAX: PlayerASpawnX, SpawnBX: PlayerBSpawnX},
		Dash: ActionDefinition{CooldownTicks: DashCooldown, DashDistance: DashDistance},
		LightAttack: ActionDefinition{CooldownTicks: LightAttackCooldown, WindupTicks: LightAttackWindup,
			ActiveTicks: LightAttackActive, RecoveryTicks: LightAttackRecovery, Damage: AttackDamage},
	}
}

type MatchStatus uint8

const (
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

type ActionType uint8

const (
	ActionUnspecified ActionType = iota
	ActionDash
	ActionLightAttack
)

type ActionState uint8

const (
	ActionStateIdle ActionState = iota
	ActionStateMove
	ActionStateDash
	ActionStateLightAttackWindup
	ActionStateLightAttackActive
	ActionStateLightAttackRecovery
	ActionStateHit
	ActionStateKO
)

type ActionCommand struct {
	Sequence uint32
	Type     ActionType
}

type Input struct {
	Tick    uint32
	MoveX   int8
	MoveY   int8
	Actions []ActionCommand
}

type PlayerState struct {
	ID                      string
	PositionX               int32
	PositionY               int32
	HP                      int32
	LastAckedInputTick      uint32
	FacingX                 int8
	FacingY                 int8
	LastAckedActionSequence uint32
	ActionState             ActionState
	ActionStartedServerTick uint32
	ActionTicksRemaining    uint32
}

type Snapshot struct {
	ArenaID                 string
	ServerTick              uint32
	Players                 [2]PlayerState
	Status                  MatchStatus
	WinnerID                string
	FinishReason            FinishReason
	CountdownTicksRemaining uint32
	MatchTicksRemaining     uint32
}

type Room struct {
	rules          Ruleset
	players        [2]PlayerState
	inputs         [2]Input
	pendingActions [2][]ActionCommand
	nextDashTick   [2]uint32
	nextAttackTick [2]uint32
	lastInputTick  [2]uint32
	tick           uint32
	activeTicks    uint32
	status         MatchStatus
	winnerID       string
	finishReason   FinishReason
}

func New(playerA, playerB string) (*Room, error) {
	return NewWithRuleset(playerA, playerB, DefaultRuleset())
}

func NewWithRuleset(playerA, playerB string, rules Ruleset) (*Room, error) {
	if playerA == "" || playerB == "" || playerA == playerB {
		return nil, ErrInvalidPlayers
	}
	if rules.TickRate == 0 || rules.CountdownTicks == 0 || rules.MatchDurationTicks == 0 ||
		rules.InitialHP <= 0 || rules.MovementPerTick < 0 || rules.LightAttack.ActiveTicks == 0 ||
		rules.LightAttack.Damage < 0 || rules.Arena.ID == "" ||
		rules.Arena.MinX >= rules.Arena.MaxX || rules.Arena.MinY >= rules.Arena.MaxY {
		return nil, ErrInvalidRuleset
	}

	return &Room{
		rules: rules,
		players: [2]PlayerState{
			{ID: playerA, PositionX: rules.Arena.SpawnAX, PositionY: rules.Arena.SpawnAY, HP: rules.InitialHP, FacingX: 1},
			{ID: playerB, PositionX: rules.Arena.SpawnBX, PositionY: rules.Arena.SpawnBY, HP: rules.InitialHP, FacingX: -1},
		},
		status: MatchCountdown,
	}, nil
}

func (r *Room) TickRate() uint32 { return r.rules.TickRate }
func (r *Room) Ruleset() Ruleset { return r.rules }

// SubmitInput stores movement from the newest valid input and independently
// merges bounded action commands. The room is single-owner; callers serialize
// SubmitInput and Step on the room loop.
func (r *Room) SubmitInput(playerID string, input Input) error {
	playerIndex, err := r.playerIndex(playerID)
	if err != nil {
		return err
	}
	if input.Tick == 0 || input.Tick > r.lastInputTick[playerIndex]+MaxInputTickAdvance {
		return nil
	}
	commands := input.Actions
	if input.Tick > r.lastInputTick[playerIndex] {
		input.MoveX = clampAxis(input.MoveX)
		input.MoveY = clampAxis(input.MoveY)
		input.Actions = nil
		r.inputs[playerIndex] = input
		r.lastInputTick[playerIndex] = input.Tick
	}

	if len(commands) > MaxPendingActions {
		commands = commands[:MaxPendingActions]
	}
	for _, command := range commands {
		r.queueAction(playerIndex, command)
	}
	return nil
}

func (r *Room) queueAction(playerIndex int, command ActionCommand) {
	ack := r.players[playerIndex].LastAckedActionSequence
	if command.Sequence <= ack || command.Sequence-ack > MaxActionSequenceAhead {
		return
	}
	for _, pending := range r.pendingActions[playerIndex] {
		if pending.Sequence == command.Sequence {
			return
		}
	}
	if len(r.pendingActions[playerIndex]) >= MaxPendingActions {
		return
	}
	r.pendingActions[playerIndex] = append(r.pendingActions[playerIndex], command)
	sort.Slice(r.pendingActions[playerIndex], func(i, j int) bool {
		return r.pendingActions[playerIndex][i].Sequence < r.pendingActions[playerIndex][j].Sequence
	})
}

func (r *Room) Step() Snapshot {
	if r.status == MatchFinished {
		return r.Snapshot()
	}

	r.tick++
	if r.status == MatchCountdown {
		r.acknowledgeRejectedActions()
		if r.tick >= r.rules.CountdownTicks {
			r.status = MatchActive
			for index := range r.inputs {
				r.inputs[index] = Input{Tick: r.inputs[index].Tick}
				r.nextAttackTick[index] = r.tick + 1
				r.nextDashTick[index] = r.tick + 1
			}
		}
		return r.Snapshot()
	}

	r.activeTicks++
	r.advanceActionStates()
	r.applyMovement()
	r.processActions()
	r.applyActiveAttacks()
	r.finishFromKO()
	if r.status != MatchFinished && r.activeTicks >= r.rules.MatchDurationTicks {
		r.finishFromTimeLimit()
	}
	return r.Snapshot()
}

func (r *Room) Snapshot() Snapshot {
	snapshot := Snapshot{ArenaID: r.rules.Arena.ID, ServerTick: r.tick,
		Players: r.players, Status: r.status,
		WinnerID: r.winnerID, FinishReason: r.finishReason}
	if r.status == MatchCountdown && r.tick < r.rules.CountdownTicks {
		snapshot.CountdownTicksRemaining = r.rules.CountdownTicks - r.tick
	}
	if r.status == MatchActive && r.activeTicks < r.rules.MatchDurationTicks {
		snapshot.MatchTicksRemaining = r.rules.MatchDurationTicks - r.activeTicks
	}
	return snapshot
}

func (r *Room) acknowledgeRejectedActions() {
	for index := range r.pendingActions {
		for _, command := range r.pendingActions[index] {
			r.players[index].LastAckedActionSequence = command.Sequence
		}
		r.pendingActions[index] = nil
	}
}

func (r *Room) advanceActionStates() {
	for index := range r.players {
		player := &r.players[index]
		if player.ActionState == ActionStateKO || player.ActionState == ActionStateIdle || player.ActionState == ActionStateMove {
			continue
		}
		if player.ActionTicksRemaining > 0 {
			player.ActionTicksRemaining--
		}
		if player.ActionTicksRemaining > 0 {
			continue
		}
		switch player.ActionState {
		case ActionStateLightAttackWindup:
			player.ActionState = ActionStateLightAttackActive
			player.ActionTicksRemaining = r.rules.LightAttack.ActiveTicks
		case ActionStateLightAttackActive:
			if r.rules.LightAttack.RecoveryTicks > 0 {
				player.ActionState = ActionStateLightAttackRecovery
				player.ActionTicksRemaining = r.rules.LightAttack.RecoveryTicks
			} else {
				r.setLocomotionState(index)
			}
		case ActionStateLightAttackRecovery, ActionStateDash, ActionStateHit:
			r.setLocomotionState(index)
		}
	}
}

func (r *Room) applyMovement() {
	for index := range r.players {
		player := &r.players[index]
		input := r.inputs[index]
		player.LastAckedInputTick = input.Tick
		if player.HP <= 0 || (player.ActionState != ActionStateIdle && player.ActionState != ActionStateMove) {
			continue
		}
		player.updateFacing(input)
		player.PositionX = clampPosition(player.PositionX+int32(input.MoveX)*r.rules.MovementPerTick, r.rules.Arena.MinX, r.rules.Arena.MaxX)
		player.PositionY = clampPosition(player.PositionY+int32(input.MoveY)*r.rules.MovementPerTick, r.rules.Arena.MinY, r.rules.Arena.MaxY)
		r.setLocomotionState(index)
	}
}

func (r *Room) setLocomotionState(index int) {
	player := &r.players[index]
	input := r.inputs[index]
	if input.MoveX != 0 || input.MoveY != 0 {
		player.ActionState = ActionStateMove
	} else {
		player.ActionState = ActionStateIdle
	}
	player.ActionStartedServerTick = 0
	player.ActionTicksRemaining = 0
}

func (r *Room) processActions() {
	for index := range r.pendingActions {
		for _, command := range r.pendingActions[index] {
			r.players[index].LastAckedActionSequence = command.Sequence
			switch command.Type {
			case ActionDash:
				r.tryDash(index)
			case ActionLightAttack:
				r.tryLightAttack(index)
			}
		}
		r.pendingActions[index] = nil
	}
}

func (r *Room) canStartAction(index int) bool {
	state := r.players[index].ActionState
	return r.players[index].HP > 0 && (state == ActionStateIdle || state == ActionStateMove)
}

func (r *Room) tryDash(index int) {
	if !r.canStartAction(index) || r.tick < r.nextDashTick[index] {
		return
	}
	player := &r.players[index]
	player.PositionX = clampPosition(player.PositionX+int32(player.FacingX)*r.rules.Dash.DashDistance, r.rules.Arena.MinX, r.rules.Arena.MaxX)
	player.PositionY = clampPosition(player.PositionY+int32(player.FacingY)*r.rules.Dash.DashDistance, r.rules.Arena.MinY, r.rules.Arena.MaxY)
	player.ActionState = ActionStateDash
	player.ActionStartedServerTick = r.tick
	player.ActionTicksRemaining = 1
	r.nextDashTick[index] = r.tick + r.rules.Dash.CooldownTicks
}

func (r *Room) tryLightAttack(index int) {
	if !r.canStartAction(index) || r.tick < r.nextAttackTick[index] {
		return
	}
	player := &r.players[index]
	player.ActionState = ActionStateLightAttackWindup
	player.ActionStartedServerTick = r.tick
	player.ActionTicksRemaining = r.rules.LightAttack.WindupTicks
	r.nextAttackTick[index] = r.tick + r.rules.LightAttack.CooldownTicks
	if player.ActionTicksRemaining == 0 {
		player.ActionState = ActionStateLightAttackActive
		player.ActionTicksRemaining = r.rules.LightAttack.ActiveTicks
	}
}

func (r *Room) applyActiveAttacks() {
	var damage [2]int32
	for attacker := range r.players {
		target := 1 - attacker
		if r.players[attacker].ActionState == ActionStateLightAttackActive &&
			attackHitboxIntersects(r.players[attacker], r.players[target], r.rules) {
			damage[target] += r.rules.LightAttack.Damage
		}
	}
	for index := range r.players {
		if damage[index] == 0 {
			continue
		}
		player := &r.players[index]
		player.HP = max(player.HP-damage[index], 0)
		player.ActionStartedServerTick = r.tick
		if player.HP == 0 {
			player.ActionState = ActionStateKO
			player.ActionTicksRemaining = 0
		} else {
			player.ActionState = ActionStateHit
			player.ActionTicksRemaining = 1
		}
	}
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
		player.FacingX, player.FacingY = input.MoveX, 0
		return
	}
	player.FacingX, player.FacingY = 0, input.MoveY
}

func attackHitboxIntersects(attacker, target PlayerState, rules Ruleset) bool {
	targetMinX, targetMaxX := target.PositionX-rules.PlayerHalfExtent, target.PositionX+rules.PlayerHalfExtent
	targetMinY, targetMaxY := target.PositionY-rules.PlayerHalfExtent, target.PositionY+rules.PlayerHalfExtent
	switch {
	case attacker.FacingX > 0:
		return rangesOverlap(attacker.PositionX, attacker.PositionX+rules.AttackDepth, targetMinX, targetMaxX) && rangesOverlap(attacker.PositionY-rules.AttackHalfWidth, attacker.PositionY+rules.AttackHalfWidth, targetMinY, targetMaxY)
	case attacker.FacingX < 0:
		return rangesOverlap(attacker.PositionX-rules.AttackDepth, attacker.PositionX, targetMinX, targetMaxX) && rangesOverlap(attacker.PositionY-rules.AttackHalfWidth, attacker.PositionY+rules.AttackHalfWidth, targetMinY, targetMaxY)
	case attacker.FacingY > 0:
		return rangesOverlap(attacker.PositionX-rules.AttackHalfWidth, attacker.PositionX+rules.AttackHalfWidth, targetMinX, targetMaxX) && rangesOverlap(attacker.PositionY, attacker.PositionY+rules.AttackDepth, targetMinY, targetMaxY)
	default:
		return rangesOverlap(attacker.PositionX-rules.AttackHalfWidth, attacker.PositionX+rules.AttackHalfWidth, targetMinX, targetMaxX) && rangesOverlap(attacker.PositionY-rules.AttackDepth, attacker.PositionY, targetMinY, targetMaxY)
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
