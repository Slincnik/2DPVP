package room

import (
	"context"
	"testing"
	"time"
)

func TestLoop_RunProcessesQueuedInputBeforeTick(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	inputs := make(chan QueuedInput, 1)
	snapshots := make(chan Snapshot, 1)
	ticks := make(chan time.Time, 1)
	loop := NewLoop(duel, inputs, snapshots)
	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()
	done := runLoop(loop, ctx, ticks)

	inputs <- QueuedInput{PlayerID: "alice", Input: Input{Tick: 1, MoveX: 1}}
	ticks <- time.Now()

	snapshot := receiveSnapshot(t, snapshots)
	if snapshot.Players[0].PositionX != MovementPerTick {
		t.Errorf("alice position x = %d, want %d", snapshot.Players[0].PositionX, MovementPerTick)
	}

	cancel()
	if err := <-done; err != nil {
		t.Errorf("Run() error = %v", err)
	}
}

func TestLoop_RunReturnsAfterFinishedMatch(t *testing.T) {
	t.Parallel()

	duel := newRoom(t)
	duel.players[0].HP = AttackDamage
	duel.players[1].HP = AttackDamage
	inputs := make(chan QueuedInput, 2)
	snapshots := make(chan Snapshot, 1)
	ticks := make(chan time.Time, 1)
	loop := NewLoop(duel, inputs, snapshots)
	done := runLoop(loop, t.Context(), ticks)

	inputs <- QueuedInput{PlayerID: "alice", Input: Input{Tick: 1, Attack: true}}
	inputs <- QueuedInput{PlayerID: "bob", Input: Input{Tick: 1, Attack: true}}
	ticks <- time.Now()

	snapshot := receiveSnapshot(t, snapshots)
	if snapshot.Status != MatchFinished {
		t.Errorf("match status = %d, want MatchFinished", snapshot.Status)
	}
	if err := <-done; err != nil {
		t.Errorf("Run() error = %v", err)
	}
}

func runLoop(loop *Loop, ctx context.Context, ticks <-chan time.Time) <-chan error {
	done := make(chan error, 1)
	go func() {
		done <- loop.Run(ctx, ticks)
	}()
	return done
}

func receiveSnapshot(t *testing.T, snapshots <-chan Snapshot) Snapshot {
	t.Helper()

	select {
	case snapshot := <-snapshots:
		return snapshot
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for snapshot")
		return Snapshot{}
	}
}
