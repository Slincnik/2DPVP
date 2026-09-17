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

func TestRoom_StartsPlayersAtDistinctSymmetricSpawns(t *testing.T) {
	t.Parallel()

	snapshot := newRoom(t).Snapshot()
	if snapshot.Players[0].PositionX != PlayerASpawnX || snapshot.Players[1].PositionX != PlayerBSpawnX {
		t.Fatalf("spawn positions = (%d, %d), want (%d, %d)", snapshot.Players[0].PositionX, snapshot.Players[1].PositionX, PlayerASpawnX, PlayerBSpawnX)
	}
	if snapshot.Players[0].PositionX != -snapshot.Players[1].PositionX {
		t.Errorf("spawn positions are not symmetric: %d and %d", snapshot.Players[0].PositionX, snapshot.Players[1].PositionX)
	}
}

func TestRoom_CountdownDisablesInputAndDeterministicallyActivates(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	if err := duel.SubmitInput("alice", Input{Tick: 1, MoveX: 1, Attack: true}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}
	for range CountdownTicks - 1 {
		duel.Step()
	}
	beforeStart := duel.Snapshot()
	if beforeStart.Status != MatchCountdown || beforeStart.CountdownTicksRemaining != 1 {
		t.Fatalf("before start = status %d remaining %d", beforeStart.Status, beforeStart.CountdownTicksRemaining)
	}
	if beforeStart.Players[0].PositionX != PlayerASpawnX || beforeStart.Players[1].HP != InitialHP {
		t.Fatalf("countdown applied input: position=%d hp=%d", beforeStart.Players[0].PositionX, beforeStart.Players[1].HP)
	}

	started := duel.Step()
	if started.Status != MatchActive || started.ServerTick != CountdownTicks {
		t.Fatalf("start snapshot = status %d tick %d", started.Status, started.ServerTick)
	}
	if started.MatchTicksRemaining != MatchDurationTicks {
		t.Errorf("match ticks remaining = %d, want %d", started.MatchTicksRemaining, MatchDurationTicks)
	}
	if after := duel.Step(); after.Players[0].PositionX != PlayerASpawnX || after.Players[1].HP != InitialHP {
		t.Errorf("countdown input leaked into active match: position=%d hp=%d", after.Players[0].PositionX, after.Players[1].HP)
	}
}

func TestRoom_StepAppliesLatestInputsAndAcknowledgesThem(t *testing.T) {
	t.Parallel()

	duel := newActiveRoom(t)
	if err := duel.SubmitInput("alice", Input{Tick: 1, MoveX: 1, MoveY: -1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}
	if err := duel.SubmitInput("bob", Input{Tick: 1, MoveX: -1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}

	snapshot := duel.Step()

	if snapshot.ServerTick != CountdownTicks+1 {
		t.Errorf("server tick = %d, want %d", snapshot.ServerTick, CountdownTicks+1)
	}
	if snapshot.Players[0].PositionX != PlayerASpawnX+MovementPerTick || snapshot.Players[0].PositionY != -MovementPerTick {
		t.Errorf("alice position = (%d, %d), want (%d, %d)", snapshot.Players[0].PositionX, snapshot.Players[0].PositionY, PlayerASpawnX+MovementPerTick, -MovementPerTick)
	}
	if snapshot.Players[0].LastAckedInputTick != 1 {
		t.Errorf("alice ack = %d, want 1", snapshot.Players[0].LastAckedInputTick)
	}
	if snapshot.Players[1].PositionX != PlayerBSpawnX-MovementPerTick {
		t.Errorf("bob position x = %d, want %d", snapshot.Players[1].PositionX, PlayerBSpawnX-MovementPerTick)
	}
}

func TestRoom_StepClampsPositionToArena(t *testing.T) {
	t.Parallel()

	duel := newActiveRoom(t)
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

	duel := newActiveRoom(t)
	if err := duel.SubmitInput("alice", Input{Tick: 2, MoveX: 1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}
	if err := duel.SubmitInput("alice", Input{Tick: 1, MoveX: -1}); err != nil {
		t.Fatalf("SubmitInput() error = %v", err)
	}

	snapshot := duel.Step()
	if snapshot.Players[0].PositionX != PlayerASpawnX+MovementPerTick {
		t.Errorf("position x = %d, want %d", snapshot.Players[0].PositionX, PlayerASpawnX+MovementPerTick)
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

	duel := newActiveRoom(t)
	duel.players[0].PositionX = 0
	duel.players[1].PositionX = 0
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

	duel := newActiveRoom(t)
	duel.players[0].PositionX = 0
	duel.players[1].PositionX = 0
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
	if snapshot.FinishReason != FinishReasonKO {
		t.Errorf("finish reason = %d, want KO", snapshot.FinishReason)
	}
}

func TestRoom_TimeLimitUsesHPAndSupportsDraw(t *testing.T) {
	t.Parallel()

	for _, test := range []struct {
		name   string
		hp     [2]int32
		winner string
	}{
		{name: "higher HP wins", hp: [2]int32{80, 60}, winner: "alice"},
		{name: "equal HP draws", hp: [2]int32{80, 80}},
	} {
		t.Run(test.name, func(t *testing.T) {
			duel := newActiveRoom(t)
			duel.players[0].HP = test.hp[0]
			duel.players[1].HP = test.hp[1]
			for range MatchDurationTicks {
				duel.Step()
			}
			snapshot := duel.Snapshot()
			if snapshot.Status != MatchFinished || snapshot.FinishReason != FinishReasonTimeLimit {
				t.Fatalf("finish = status %d reason %d, want time limit", snapshot.Status, snapshot.FinishReason)
			}
			if snapshot.WinnerID != test.winner {
				t.Errorf("winner = %q, want %q", snapshot.WinnerID, test.winner)
			}
		})
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

func newActiveRoom(t *testing.T) *Room {
	t.Helper()
	duel := newRoom(t)
	for range CountdownTicks {
		duel.Step()
	}
	return duel
}
