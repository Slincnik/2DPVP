package room

import (
	"errors"
	"testing"
)

func TestNew_RejectsMissingOrDuplicatePlayerIDs(t *testing.T) {
	t.Parallel()

	for _, players := range [][2]string{{"", "bob"}, {"alice", ""}, {"alice", "alice"}} {
		_, err := New(players[0], players[1])
		if !errors.Is(err, ErrInvalidPlayers) {
			t.Errorf("New(%q, %q) error = %v, want ErrInvalidPlayers", players[0], players[1], err)
		}
	}
}

func TestRoom_StepAppliesLatestInputsAndAcknowledgesThem(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	if err := duel.SubmitInput("alice", Input{Tick: 1, MoveX: 1, MoveY: -1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}
	if err := duel.SubmitInput("bob", Input{Tick: 1, MoveX: -1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}

	snapshot := duel.Step()

	if snapshot.ServerTick != 1 {
		t.Errorf("server tick = %d, want 1", snapshot.ServerTick)
	}
	if snapshot.Players[0].PositionX != MovementPerTick || snapshot.Players[0].PositionY != -MovementPerTick {
		t.Errorf("alice position = (%d, %d), want (%d, %d)", snapshot.Players[0].PositionX, snapshot.Players[0].PositionY, MovementPerTick, -MovementPerTick)
	}
	if snapshot.Players[0].LastAckedInputTick != 1 {
		t.Errorf("alice ack = %d, want 1", snapshot.Players[0].LastAckedInputTick)
	}
	if snapshot.Players[1].PositionX != -MovementPerTick {
		t.Errorf("bob position x = %d, want %d", snapshot.Players[1].PositionX, -MovementPerTick)
	}
}

func TestRoom_StepClampsPositionToArena(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	if err := duel.SubmitInput("alice", Input{Tick: 1, MoveX: 127, MoveY: -127}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}

	for range 100 {
		duel.Step()
	}
	snapshot := duel.Snapshot()
	if snapshot.Players[0].PositionX != ArenaMaxX {
		t.Errorf("position x = %d, want %d", snapshot.Players[0].PositionX, ArenaMaxX)
	}
	if snapshot.Players[0].PositionY != ArenaMinY {
		t.Errorf("position y = %d, want %d", snapshot.Players[0].PositionY, ArenaMinY)
	}
}

func TestRoom_SubmitInputIgnoresStaleInput(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	if err := duel.SubmitInput("alice", Input{Tick: 2, MoveX: 1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}
	if err := duel.SubmitInput("alice", Input{Tick: 1, MoveX: -1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}

	snapshot := duel.Step()
	if snapshot.Players[0].PositionX != MovementPerTick {
		t.Errorf("position x = %d, want %d", snapshot.Players[0].PositionX, MovementPerTick)
	}
	if snapshot.Players[0].LastAckedInputTick != 2 {
		t.Errorf("ack = %d, want 2", snapshot.Players[0].LastAckedInputTick)
	}
}

func TestRoom_SubmitInputRejectsUnknownPlayer(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	err := duel.SubmitInput("mallory", Input{Tick: 1})
	if !errors.Is(err, ErrUnknownPlayer) {
		t.Errorf("SubmitInput() error = %v, want ErrUnknownPlayer", err)
	}
}

func TestRoom_HeldAttackDamagesOnCooldownAndEndsMatch(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	if err := duel.SubmitInput("alice", Input{Tick: 1, Attack: true}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}

	for range 1 + 4*AttackCooldown {
		duel.Step()
	}
	snapshot := duel.Snapshot()

	if snapshot.Players[1].HP != 0 {
		t.Errorf("bob HP = %d, want 0", snapshot.Players[1].HP)
	}
	if snapshot.Status != MatchFinished {
		t.Errorf("match status = %d, want MatchFinished", snapshot.Status)
	}
	if snapshot.WinnerID != "alice" {
		t.Errorf("winner = %q, want alice", snapshot.WinnerID)
	}

	finishedTick := snapshot.ServerTick
	afterFinish := duel.Step()
	if afterFinish.ServerTick != finishedTick {
		t.Errorf("finished room advanced from tick %d to %d", finishedTick, afterFinish.ServerTick)
	}
}

func TestRoom_SimultaneousLethalHitsEndInDraw(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	duel.players[0].HP = AttackDamage
	duel.players[1].HP = AttackDamage
	if err := duel.SubmitInput("alice", Input{Tick: 1, Attack: true}); err != nil {
		t.Fatalf("SubmitInput(alice) error = %v", err)
	}
	if err := duel.SubmitInput("bob", Input{Tick: 1, Attack: true}); err != nil {
		t.Fatalf("SubmitInput(bob) error = %v", err)
	}

	snapshot := duel.Step()
	if snapshot.Status != MatchFinished {
		t.Errorf("match status = %d, want MatchFinished", snapshot.Status)
	}
	if snapshot.WinnerID != "" {
		t.Errorf("winner = %q, want draw", snapshot.WinnerID)
	}
}

func newRoom(t *testing.T) *Room {
	t.Helper()

	duel, err := New("alice", "bob")
	if err != nil {
		t.Fatalf("New() error = %v", err)
	}
	return duel
}
