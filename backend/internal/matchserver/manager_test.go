package matchserver

import (
	"context"
	"errors"
	"fmt"
	"runtime"
	"testing"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/room"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/quicserver"
)

func TestMatchStatusWireValuesRemainCompatible(t *testing.T) {
	t.Parallel()

	if gamev1.MatchStatus_MATCH_STATUS_ACTIVE != 0 || gamev1.MatchStatus_MATCH_STATUS_FINISHED != 1 {
		t.Fatalf("wire values changed: ACTIVE=%d FINISHED=%d", gamev1.MatchStatus_MATCH_STATUS_ACTIVE, gamev1.MatchStatus_MATCH_STATUS_FINISHED)
	}
}

func TestMatchManager_RejectsInvalidToken(t *testing.T) {
	t.Parallel()

	manager := NewMatchManager(newTicketManager(t))
	_, err := manager.Authenticate(t.Context(), &gamev1.AuthenticateRequest{
		MatchToken: "invalid",
		PlayerId:   "alice",
	})
	if !errors.Is(err, ErrAuthentication) {
		t.Errorf("Authenticate() error = %v, want ErrAuthentication", err)
	}
}

func TestMatchManager_PairsPlayersWithReliableStartAndSharedCountdown(t *testing.T) {
	t.Parallel()

	tickets := newTicketManager(t)
	aliceTicket, bobTicket := issueTickets(t, tickets, "match-1", "alice", "bob")
	manager := NewMatchManager(tickets)
	aliceResult := authenticateAsync(manager, t.Context(), "alice", aliceTicket)
	bobResult := authenticateAsync(manager, t.Context(), "bob", bobTicket)

	alice := receiveSession(t, aliceResult)
	bob := receiveSession(t, bobResult)
	t.Cleanup(alice.Close)
	t.Cleanup(bob.Close)

	for _, session := range []quicserver.Session{alice, bob} {
		start := session.MatchStart()
		if start.GetTickRate() != 30 || start.GetCountdownTicks() != 90 || start.GetMatchDurationTicks() != 2700 {
			t.Errorf("start timing = rate %d countdown %d duration %d", start.GetTickRate(), start.GetCountdownTicks(), start.GetMatchDurationTicks())
		}
		initial := start.GetInitialSnapshot()
		if start.GetArenaId() == "" || start.GetArenaId() != initial.GetArenaId() {
			t.Errorf("arena metadata mismatch: start=%q snapshot=%q", start.GetArenaId(), initial.GetArenaId())
		}
		if initial.GetStatus() != gamev1.MatchStatus_MATCH_STATUS_COUNTDOWN {
			t.Errorf("initial status = %v, want countdown", initial.GetStatus())
		}
		if playerPositionX(t, initial, "alice") != -playerPositionX(t, initial, "bob") {
			t.Errorf("initial spawns are not symmetric")
		}
	}

	assertNoSnapshot(t, alice)
	assertNoSnapshot(t, bob)
	alice.AcknowledgeMatchStart()
	alice.AcknowledgeMatchStart() // Per-session acknowledgement is idempotent.
	select {
	case <-alice.(*playerSession).match.startBarrier:
		t.Fatal("start barrier opened after only one session acknowledged MatchStart")
	default:
	}
	assertNoSnapshot(t, alice)
	assertNoSnapshot(t, bob)
	bob.AcknowledgeMatchStart()
	select {
	case <-alice.(*playerSession).match.startBarrier:
	default:
		t.Fatal("start barrier remained closed after both sessions acknowledged MatchStart")
	}

	aliceSnapshot := receiveSnapshot(t, alice)
	bobSnapshot := receiveSnapshot(t, bob)
	if aliceSnapshot.GetStatus() != gamev1.MatchStatus_MATCH_STATUS_COUNTDOWN || bobSnapshot.GetStatus() != gamev1.MatchStatus_MATCH_STATUS_COUNTDOWN {
		t.Errorf("paired sessions did not share countdown snapshots")
	}
	if aliceSnapshot.GetArenaId() == "" || aliceSnapshot.GetArenaId() != bobSnapshot.GetArenaId() ||
		aliceSnapshot.GetArenaId() != alice.MatchStart().GetArenaId() {
		t.Errorf("arena was not fixed for both sessions: alice=%q bob=%q start=%q",
			aliceSnapshot.GetArenaId(), bobSnapshot.GetArenaId(), alice.MatchStart().GetArenaId())
	}
}

func TestMatchBroadcastPublishesDedicatedTerminalEvent(t *testing.T) {
	t.Parallel()

	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()
	match := &match{ctx: ctx, cancel: cancel, players: make(map[string]*playerSession)}
	start := &gamev1.MatchStart{}
	match.players["alice"] = newPlayerSession(match, "alice", start)
	match.players["bob"] = newPlayerSession(match, "bob", start)
	snapshots := make(chan room.Snapshot, 1)
	done := make(chan struct{})
	go func() {
		match.broadcast(snapshots)
		close(done)
	}()
	snapshots <- room.Snapshot{
		Status:       room.MatchFinished,
		WinnerID:     "alice",
		FinishReason: room.FinishReasonTimeLimit,
	}
	close(snapshots)

	for _, player := range match.players {
		matchEnd := <-player.MatchEnds()
		if matchEnd.GetWinnerPlayerId() != "alice" || matchEnd.GetReason() != gamev1.MatchFinishReason_MATCH_FINISH_REASON_TIME_LIMIT {
			t.Errorf("match end = winner %q reason %v", matchEnd.GetWinnerPlayerId(), matchEnd.GetReason())
		}
		if matchEnd.GetFinalSnapshot().GetStatus() != gamev1.MatchStatus_MATCH_STATUS_FINISHED {
			t.Errorf("final snapshot status = %v", matchEnd.GetFinalSnapshot().GetStatus())
		}
	}
	<-done
}

func TestMatchDisconnectPublishesTechnicalDefeatAndStopsRoom(t *testing.T) {
	t.Parallel()

	match, err := newMatch("disconnect-match", "alice", "bob")
	if err != nil {
		t.Fatalf("newMatch() error = %v", err)
	}
	alice := match.players["alice"]
	bob := match.players["bob"]

	bob.Close()

	select {
	case matchEnd := <-alice.MatchEnds():
		if matchEnd.GetWinnerPlayerId() != "alice" || matchEnd.GetReason() != gamev1.MatchFinishReason_MATCH_FINISH_REASON_DISCONNECT {
			t.Fatalf("match end = winner %q reason %v, want alice disconnect", matchEnd.GetWinnerPlayerId(), matchEnd.GetReason())
		}
		if matchEnd.GetFinalSnapshot().GetStatus() != gamev1.MatchStatus_MATCH_STATUS_FINISHED {
			t.Errorf("final status = %v, want finished", matchEnd.GetFinalSnapshot().GetStatus())
		}
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for disconnect MatchEnd")
	}

	select {
	case <-match.ctx.Done():
	case <-time.After(time.Second):
		t.Fatal("match room was not canceled after disconnect")
	}
}

func TestArenaSelectionIsDeterministicAndUsesBothVisualThemes(t *testing.T) {
	t.Parallel()

	seen := make(map[string]bool)
	for index := 0; index < 100; index++ {
		matchID := fmt.Sprintf("arena-match-%d", index)
		first := arenaIDForMatch(matchID)
		second := arenaIDForMatch(matchID)
		if first != second {
			t.Fatalf("arena selection changed for %q: %q then %q", matchID, first, second)
		}
		if first != room.ArenaIDNeonRooftop && first != room.ArenaIDEmberFoundry {
			t.Fatalf("unsupported arena selected: %q", first)
		}
		seen[first] = true
	}
	if len(seen) != 2 {
		t.Fatalf("selection did not reach both visual themes: %v", seen)
	}
}

func TestMatchManager_RejectsTicketReplay(t *testing.T) {
	t.Parallel()

	tickets := newTicketManager(t)
	aliceTicket, bobTicket := issueTickets(t, tickets, "match-1", "alice", "bob")
	manager := NewMatchManager(tickets)
	aliceResult := authenticateAsync(manager, t.Context(), "alice", aliceTicket)
	bobResult := authenticateAsync(manager, t.Context(), "bob", bobTicket)
	alice := receiveSession(t, aliceResult)
	bob := receiveSession(t, bobResult)
	t.Cleanup(alice.Close)
	t.Cleanup(bob.Close)

	_, err := manager.Authenticate(t.Context(), authRequest("alice", aliceTicket))
	if !errors.Is(err, ErrTicketUsed) {
		t.Errorf("replayed Authenticate() error = %v, want ErrTicketUsed", err)
	}
}

func TestMatchManager_RejectsSamePlayerAsOpponent(t *testing.T) {
	t.Parallel()

	tickets := newTicketManager(t)
	aliceTicket, _ := issueTickets(t, tickets, "match-1", "alice", "bob")
	manager := NewMatchManager(tickets)
	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()
	first := authenticateAsync(manager, ctx, "alice", aliceTicket)
	waitForWaitingPlayer(t, manager)

	_, err := manager.Authenticate(t.Context(), authRequest("alice", aliceTicket))
	if !errors.Is(err, ErrSamePlayer) {
		t.Errorf("Authenticate() error = %v, want ErrSamePlayer", err)
	}
	cancel()
	<-first
}

func newTicketManager(t *testing.T) *matchticket.Manager {
	t.Helper()
	manager, err := matchticket.NewManager(
		[]byte("match-server-test-secret-at-least-32-bytes"),
		"gateway",
		"matchserver",
		time.Minute,
	)
	if err != nil {
		t.Fatalf("matchticket.NewManager() error = %v", err)
	}
	return manager
}

func issueTickets(
	t *testing.T,
	manager *matchticket.Manager,
	matchID string,
	playerA string,
	playerB string,
) (string, string) {
	t.Helper()
	first, _, err := manager.Issue(matchID, playerA, playerB)
	if err != nil {
		t.Fatalf("Issue(first) error = %v", err)
	}
	second, _, err := manager.Issue(matchID, playerB, playerA)
	if err != nil {
		t.Fatalf("Issue(second) error = %v", err)
	}
	return first, second
}

func waitForWaitingPlayer(t *testing.T, manager *MatchManager) {
	t.Helper()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		manager.mutex.Lock()
		count := len(manager.waiting)
		manager.mutex.Unlock()
		if count == 1 {
			return
		}
		runtime.Gosched()
	}
	t.Fatal("timed out waiting for first queued player")
}

func authenticateAsync(
	manager *MatchManager,
	ctx context.Context,
	playerID string,
	ticket string,
) <-chan matchResult {
	result := make(chan matchResult, 1)
	go func() {
		session, err := manager.Authenticate(ctx, authRequest(playerID, ticket))
		result <- matchResult{session: session, err: err}
	}()
	return result
}

func authRequest(playerID, ticket string) *gamev1.AuthenticateRequest {
	return &gamev1.AuthenticateRequest{MatchToken: ticket, PlayerId: playerID}
}

func receiveSession(t *testing.T, result <-chan matchResult) quicserver.Session {
	t.Helper()
	select {
	case received := <-result:
		if received.err != nil {
			t.Fatalf("Authenticate() error = %v", received.err)
		}
		return received.session
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for matched session")
		return nil
	}
}

func receiveAcknowledgedSnapshot(
	t *testing.T,
	session quicserver.Session,
	playerID string,
	tick uint32,
) *gamev1.WorldSnapshot {
	t.Helper()
	deadline := time.After(time.Second)
	for {
		select {
		case snapshot := <-session.Snapshots():
			for _, player := range snapshot.GetPlayers() {
				if player.GetPlayerId() == playerID && player.GetLastAckedInputTick() >= tick {
					return snapshot
				}
			}
		case <-deadline:
			t.Fatal("timed out waiting for acknowledged snapshot")
			return nil
		}
	}
}

func assertNoSnapshot(t *testing.T, session quicserver.Session) {
	t.Helper()
	select {
	case snapshot := <-session.Snapshots():
		t.Fatalf("received snapshot before both MatchStart acknowledgements: %+v", snapshot)
	case <-time.After(50 * time.Millisecond):
	}
}

func receiveSnapshot(t *testing.T, session quicserver.Session) *gamev1.WorldSnapshot {
	t.Helper()
	select {
	case snapshot := <-session.Snapshots():
		return snapshot
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for snapshot")
		return nil
	}
}

func assertPlayerDirection(
	t *testing.T,
	snapshot *gamev1.WorldSnapshot,
	playerID string,
	direction int32,
) {
	t.Helper()
	position := playerPositionX(t, snapshot, playerID)
	if position*direction <= 0 {
		t.Errorf("%s position x = %d, want direction %d", playerID, position, direction)
	}
}

func playerPositionX(
	t *testing.T,
	snapshot *gamev1.WorldSnapshot,
	playerID string,
) int32 {
	t.Helper()
	for _, player := range snapshot.GetPlayers() {
		if player.GetPlayerId() == playerID {
			return player.GetPositionX()
		}
	}
	t.Fatalf("player %s not found", playerID)
	return 0
}
