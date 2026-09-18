package room

import (
	"errors"
	"testing"
)

func TestNewRejectsInvalidPlayersAndRuleset(t *testing.T) {
	t.Parallel()
	for _, players := range [][2]string{{"", "bob"}, {"alice", ""}, {"alice", "alice"}} {
		if _, err := New(players[0], players[1]); !errors.Is(err, ErrInvalidPlayers) {
			t.Fatalf("New(%q,%q) error = %v", players[0], players[1], err)
		}
	}
	rules := DefaultRuleset()
	rules.TickRate = 0
	if _, err := NewWithRuleset("alice", "bob", rules); !errors.Is(err, ErrInvalidRuleset) {
		t.Fatalf("NewWithRuleset error = %v, want ErrInvalidRuleset", err)
	}
	rules = DefaultRuleset()
	rules.Arena.ID = ""
	if _, err := NewWithRuleset("alice", "bob", rules); !errors.Is(err, ErrInvalidRuleset) {
		t.Fatalf("empty arena ID error = %v, want ErrInvalidRuleset", err)
	}
}

func TestRoomUsesImmutableRulesetCopy(t *testing.T) {
	t.Parallel()
	rules := DefaultRuleset()
	duel, err := NewWithRuleset("alice", "bob", rules)
	if err != nil {
		t.Fatal(err)
	}
	rules.MovementPerTick = 999
	rules.Arena.ID = ArenaIDEmberFoundry
	activeRoom(t, duel)
	mustSubmit(t, duel, "alice", Input{Tick: 1, MoveX: 1})
	snapshot := duel.Step()
	if got := snapshot.Players[0].PositionX; got != PlayerASpawnX+MovementPerTick {
		t.Fatalf("position = %d", got)
	}
	if snapshot.ArenaID != ArenaIDNeonRooftop {
		t.Fatalf("arena ID changed with caller ruleset: %q", snapshot.ArenaID)
	}
}

func TestRoomCountdownRejectsAndAcknowledgesActions(t *testing.T) {
	t.Parallel()
	duel := newRoom(t)
	mustSubmit(t, duel, "alice", Input{Tick: 1, MoveX: 1, Actions: []ActionCommand{{Sequence: 1, Type: ActionDash}}})
	snapshot := duel.Step()
	if snapshot.Players[0].PositionX != PlayerASpawnX || snapshot.Players[0].LastAckedActionSequence != 1 {
		t.Fatalf("countdown snapshot = %+v", snapshot.Players[0])
	}
	for range CountdownTicks - 1 {
		duel.Step()
	}
	if duel.Snapshot().Status != MatchActive {
		t.Fatal("room did not become active")
	}
	if got := duel.Step().Players[0].PositionX; got != PlayerASpawnX {
		t.Fatalf("countdown movement leaked: %d", got)
	}
}

func TestRoomMovementAndInputAck(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	mustSubmit(t, duel, "alice", Input{Tick: 1, MoveX: 1, MoveY: -1})
	snapshot := duel.Step()
	player := snapshot.Players[0]
	if player.PositionX != PlayerASpawnX+MovementPerTick || player.PositionY != -MovementPerTick || player.LastAckedInputTick != 1 {
		t.Fatalf("player = %+v", player)
	}
}

func TestLightAttackPhasesHitOnlyWhenActive(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	duel.players[0].PositionX, duel.players[1].PositionX = 0, 60
	mustSubmit(t, duel, "alice", actionInput(1, 1, ActionLightAttack))
	for remaining := uint32(LightAttackWindup); remaining > 0; remaining-- {
		snapshot := duel.Step()
		if snapshot.Players[1].HP != InitialHP {
			t.Fatalf("damage during windup: %d", snapshot.Players[1].HP)
		}
		if snapshot.Players[0].ActionState != ActionStateLightAttackWindup {
			t.Fatalf("state = %d", snapshot.Players[0].ActionState)
		}
	}
	snapshot := duel.Step()
	if snapshot.Players[0].ActionState != ActionStateLightAttackActive || snapshot.Players[1].HP != InitialHP-AttackDamage {
		t.Fatalf("active snapshot: attacker=%+v target=%+v", snapshot.Players[0], snapshot.Players[1])
	}
}

func TestLightAttackMovementKeepsLockedHitboxFacing(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	duel.players[0].PositionX, duel.players[0].PositionY = 0, 0
	// This target would be hit only if movement during windup redirected the
	// attack downward. A right-facing locked hitbox must miss it.
	duel.players[1].PositionX, duel.players[1].PositionY = 0, 120

	mustSubmit(t, duel, "alice", actionInput(1, 1, ActionLightAttack))
	if got := duel.Step().Players[0]; got.ActionState != ActionStateLightAttackWindup {
		t.Fatalf("attack did not start: %+v", got)
	}
	mustSubmit(t, duel, "alice", Input{Tick: 2, MoveY: 1})
	for range LightAttackWindup {
		duel.Step()
	}

	snapshot := duel.Snapshot()
	attacker := snapshot.Players[0]
	if attacker.PositionY != int32(LightAttackWindup)*MovementPerTick {
		t.Fatalf("movement was blocked during attack: %+v", attacker)
	}
	if attacker.FacingX != 1 || attacker.FacingY != 0 {
		t.Fatalf("attack facing changed during windup: %+v", attacker)
	}
	if snapshot.Players[1].HP != InitialHP {
		t.Fatalf("movement redirected attack hitbox: target=%+v", snapshot.Players[1])
	}
}

func TestLightAttackCooldownAndForbiddenPhaseAreAcknowledged(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	mustSubmit(t, duel, "alice", actionInput(1, 1, ActionLightAttack))
	first := duel.Step()
	if first.Players[0].ActionState != ActionStateLightAttackWindup {
		t.Fatal("attack did not start")
	}
	mustSubmit(t, duel, "alice", actionInput(2, 2, ActionDash))
	second := duel.Step()
	if second.Players[0].LastAckedActionSequence != 2 || second.Players[0].ActionState != ActionStateLightAttackWindup {
		t.Fatalf("forbidden dash was not rejected and acked: %+v", second.Players[0])
	}
	for duel.Snapshot().Players[0].ActionState != ActionStateIdle {
		duel.Step()
	}
	if duel.tick >= duel.nextAttackTick[0] {
		t.Fatal("test no longer exercises cooldown")
	}
	mustSubmit(t, duel, "alice", actionInput(3, 3, ActionLightAttack))
	cooldown := duel.Step()
	if cooldown.Players[0].ActionState != ActionStateIdle || cooldown.Players[0].LastAckedActionSequence != 3 {
		t.Fatalf("cooldown command result = %+v", cooldown.Players[0])
	}
	for duel.tick+1 < duel.nextAttackTick[0] {
		duel.Step()
	}
	mustSubmit(t, duel, "alice", actionInput(4, 4, ActionLightAttack))
	if got := duel.Step().Players[0].ActionState; got != ActionStateLightAttackWindup {
		t.Fatalf("state = %d", got)
	}
}

func TestDashDirectionClampCooldownAndDuplicate(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	duel.players[0].PositionX = ArenaMaxX - 5
	mustSubmit(t, duel, "alice", Input{Tick: 1, MoveX: 1, Actions: []ActionCommand{{Sequence: 1, Type: ActionDash}, {Sequence: 1, Type: ActionDash}}})
	first := duel.Step()
	if first.Players[0].PositionX != ArenaMaxX || first.Players[0].ActionState != ActionStateDash || first.Players[0].LastAckedActionSequence != 1 {
		t.Fatalf("first dash = %+v", first.Players[0])
	}
	mustSubmit(t, duel, "alice", actionInput(2, 1, ActionDash))
	duplicate := duel.Step()
	if duplicate.Players[0].PositionX != ArenaMaxX {
		t.Fatal("duplicate dash moved player")
	}
	mustSubmit(t, duel, "alice", actionInput(3, 2, ActionDash))
	rejected := duel.Step()
	if rejected.Players[0].LastAckedActionSequence != 2 || rejected.Players[0].ActionState == ActionStateDash {
		t.Fatalf("cooldown dash = %+v", rejected.Players[0])
	}

	other := newActiveRoom(t)
	mustSubmit(t, other, "alice", Input{Tick: 1, MoveX: -1, Actions: []ActionCommand{{Sequence: 1, Type: ActionDash}}})
	moved := other.Step().Players[0]
	if moved.FacingX != -1 || moved.PositionX != PlayerASpawnX-MovementPerTick-DashDistance {
		t.Fatalf("directional dash = %+v", moved)
	}
}

func TestLostFirstDatagramAndReorderedCommands(t *testing.T) {
	t.Parallel()
	lost := newActiveRoom(t)
	// Tick 1 was lost. The next datagram repeats the same pending command.
	mustSubmit(t, lost, "alice", actionInput(2, 1, ActionDash))
	if got := lost.Step().Players[0]; got.LastAckedActionSequence != 1 || got.PositionX != PlayerASpawnX+DashDistance {
		t.Fatalf("repeated command = %+v", got)
	}

	reordered := newActiveRoom(t)
	mustSubmit(t, reordered, "alice", actionInput(2, 2, ActionDash))
	mustSubmit(t, reordered, "alice", actionInput(1, 1, ActionLightAttack))
	got := reordered.Step().Players[0]
	if got.LastAckedActionSequence != 2 || got.ActionState != ActionStateLightAttackWindup || got.PositionX != PlayerASpawnX {
		t.Fatalf("reordered commands = %+v", got)
	}
}

func TestInputBoundsAndFutureTickValidation(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	mustSubmit(t, duel, "alice", actionInput(MaxInputTickAdvance+1, 1, ActionDash))
	if got := duel.Step().Players[0]; got.LastAckedActionSequence != 0 || got.PositionX != PlayerASpawnX {
		t.Fatalf("future input applied: %+v", got)
	}
	commands := make([]ActionCommand, MaxPendingActions+3)
	for index := range commands {
		commands[index] = ActionCommand{Sequence: uint32(index + 1), Type: ActionUnspecified}
	}
	mustSubmit(t, duel, "alice", Input{Tick: 1, Actions: commands})
	if ack := duel.Step().Players[0].LastAckedActionSequence; ack != MaxPendingActions {
		t.Fatalf("ack = %d", ack)
	}
	mustSubmit(t, duel, "alice", actionInput(2, MaxActionSequenceAhead+MaxPendingActions+1, ActionDash))
	if ack := duel.Step().Players[0].LastAckedActionSequence; ack != MaxPendingActions {
		t.Fatalf("far sequence ack = %d", ack)
	}
}

func TestSimultaneousLethalActiveHitsDraw(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	duel.players[0].PositionX, duel.players[1].PositionX = 0, 0
	duel.players[0].HP, duel.players[1].HP = AttackDamage, AttackDamage
	mustSubmit(t, duel, "alice", actionInput(1, 1, ActionLightAttack))
	mustSubmit(t, duel, "bob", actionInput(1, 1, ActionLightAttack))
	for range LightAttackWindup + 1 {
		duel.Step()
	}
	snapshot := duel.Snapshot()
	if snapshot.Status != MatchFinished || snapshot.WinnerID != "" || snapshot.Players[0].HP != 0 || snapshot.Players[1].HP != 0 {
		t.Fatalf("terminal snapshot = %+v", snapshot)
	}
}

func TestDirectionalAttackOnlyHitsForward(t *testing.T) {
	t.Parallel()
	for _, test := range []struct {
		name string
		x, y int32
		hit  bool
	}{
		{"forward", 60, 0, true}, {"behind", -30, 0, false}, {"wide", 0, 66, false},
	} {
		t.Run(test.name, func(t *testing.T) {
			duel := newActiveRoom(t)
			duel.players[0].PositionX, duel.players[0].PositionY = 0, 0
			duel.players[1].PositionX, duel.players[1].PositionY = test.x, test.y
			mustSubmit(t, duel, "alice", actionInput(1, 1, ActionLightAttack))
			for range LightAttackWindup + 1 {
				duel.Step()
			}
			if hit := duel.Snapshot().Players[1].HP < InitialHP; hit != test.hit {
				t.Fatalf("hit = %t", hit)
			}
		})
	}
}

func TestRoomTimeLimitUsesHP(t *testing.T) {
	t.Parallel()
	duel := newActiveRoom(t)
	duel.players[0].HP, duel.players[1].HP = 80, 60
	for range MatchDurationTicks {
		duel.Step()
	}
	if got := duel.Snapshot(); got.Status != MatchFinished || got.WinnerID != "alice" || got.FinishReason != FinishReasonTimeLimit {
		t.Fatalf("finish = %+v", got)
	}
}

func TestUnknownPlayer(t *testing.T) {
	t.Parallel()
	if err := newRoom(t).SubmitInput("mallory", Input{Tick: 1}); !errors.Is(err, ErrUnknownPlayer) {
		t.Fatal(err)
	}
}

func actionInput(tick, sequence uint32, action ActionType) Input {
	return Input{Tick: tick, Actions: []ActionCommand{{Sequence: sequence, Type: action}}}
}

func mustSubmit(t *testing.T, duel *Room, player string, input Input) {
	t.Helper()
	if err := duel.SubmitInput(player, input); err != nil {
		t.Fatal(err)
	}
}

func newRoom(t *testing.T) *Room {
	t.Helper()
	duel, err := New("alice", "bob")
	if err != nil {
		t.Fatal(err)
	}
	return duel
}

func activeRoom(t *testing.T, duel *Room) {
	t.Helper()
	for range duel.rules.CountdownTicks {
		duel.Step()
	}
}

func newActiveRoom(t *testing.T) *Room {
	t.Helper()
	duel := newRoom(t)
	activeRoom(t, duel)
	return duel
}
