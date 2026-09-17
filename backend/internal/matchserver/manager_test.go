package matchserver

import (
	"context"
	"errors"
	"runtime"
	"testing"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/quicserver"
)

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

func TestMatchManager_PairsTicketedPlayersInSharedFixedTickRoom(t *testing.T) {
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

	if err := alice.SubmitInput(&gamev1.PlayerInput{Tick: 1, MoveX: 1}); err != nil {
		t.Fatalf("alice SubmitInput() error = %v", err)
	}
	if err := bob.SubmitInput(&gamev1.PlayerInput{Tick: 1, MoveX: -1}); err != nil {
		t.Fatalf("bob SubmitInput() error = %v", err)
	}

	aliceSnapshot := receiveAcknowledgedSnapshot(t, alice, "alice", 1)
	bobSnapshot := receiveAcknowledgedSnapshot(t, bob, "bob", 1)
	assertPlayerDirection(t, aliceSnapshot, "alice", 1)
	assertPlayerDirection(t, aliceSnapshot, "bob", -1)
	assertPlayerDirection(t, bobSnapshot, "alice", 1)
	assertPlayerDirection(t, bobSnapshot, "bob", -1)

	firstPosition := playerPositionX(t, aliceSnapshot, "alice")
	second := receiveSnapshot(t, alice)
	if second.GetServerTick() <= aliceSnapshot.GetServerTick() {
		t.Errorf("server tick did not advance independently: first=%d second=%d", aliceSnapshot.GetServerTick(), second.GetServerTick())
	}
	if got := playerPositionX(t, second, "alice"); got <= firstPosition {
		t.Errorf("position did not advance without another input: first=%d second=%d", firstPosition, got)
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
