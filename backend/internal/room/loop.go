package room

import (
	"context"
	"time"
)

type QueuedInput struct {
	PlayerID string
	Input    Input
}

// Loop owns a Room and serializes input handling with its simulation ticks.
// Snapshots are best-effort: an occupied output channel drops an old visual
// update rather than blocking the authoritative simulation.
type Loop struct {
	room      *Room
	inputs    <-chan QueuedInput
	snapshots chan Snapshot
}

func NewLoop(room *Room, inputs <-chan QueuedInput, snapshots chan Snapshot) *Loop {
	return &Loop{
		room:      room,
		inputs:    inputs,
		snapshots: snapshots,
	}
}

func (l *Loop) RunRealtime(ctx context.Context) error {
	ticker := time.NewTicker(time.Second / time.Duration(l.room.TickRate()))
	defer ticker.Stop()

	return l.Run(ctx, ticker.C)
}

// Run advances the room once for each value received from ticks. Supplying a
// tick channel makes timing deterministic in tests; production uses RunRealtime.
func (l *Loop) Run(ctx context.Context, ticks <-chan time.Time) error {
	for {
		select {
		case <-ctx.Done():
			return nil
		case input, open := <-l.inputs:
			if !open {
				l.inputs = nil
				continue
			}
			l.submit(input)
		case _, open := <-ticks:
			if !open {
				return nil
			}

			l.drainInputs()
			snapshot := l.room.Step()
			if !l.publish(ctx, snapshot) {
				return nil
			}
			if snapshot.Status == MatchFinished {
				return nil
			}
		}
	}
}

func (l *Loop) drainInputs() {
	for l.inputs != nil {
		select {
		case input, open := <-l.inputs:
			if !open {
				l.inputs = nil
				return
			}
			l.submit(input)
		default:
			return
		}
	}
}

func (l *Loop) submit(queued QueuedInput) {
	// MatchManager routes only participants of this room. Unknown input is
	// discarded here so a malformed datagram cannot terminate a live match.
	_ = l.room.SubmitInput(queued.PlayerID, queued.Input)
}

func (l *Loop) publish(ctx context.Context, snapshot Snapshot) bool {
	select {
	case l.snapshots <- snapshot:
		return true
	default:
	}

	// Keep only the newest visual update. A terminal snapshot replaces any
	// queued update and then blocks until accepted (or canceled), so completion
	// can never be lost to snapshot backpressure.
	select {
	case <-l.snapshots:
	default:
	}
	if snapshot.Status == MatchFinished {
		select {
		case l.snapshots <- snapshot:
			return true
		case <-ctx.Done():
			return false
		}
	}
	select {
	case l.snapshots <- snapshot:
	default:
	}
	return true
}
