package matchmaking

import (
	"testing"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
)

func TestService_PairsPlayersAndIssuesBoundTickets(t *testing.T) {
	t.Parallel()

	service, tickets := newTestService(t)
	alice, err := service.Join("alice")
	if err != nil {
		t.Fatalf("Join(alice) error = %v", err)
	}
	if alice.Status != StatusWaiting {
		t.Fatalf("alice status = %q, want waiting", alice.Status)
	}

	bob, err := service.Join("bob")
	if err != nil {
		t.Fatalf("Join(bob) error = %v", err)
	}
	alice = service.PlayerStatus("alice")
	if bob.Status != StatusMatched || alice.Status != StatusMatched {
		t.Fatalf("statuses = (%q, %q), want matched", alice.Status, bob.Status)
	}
	if alice.MatchID != bob.MatchID || alice.OpponentID != "bob" || bob.OpponentID != "alice" {
		t.Errorf("pair mismatch: alice=%+v bob=%+v", alice, bob)
	}

	aliceClaims, err := tickets.Verify(alice.MatchTicket)
	if err != nil {
		t.Fatalf("Verify(alice) error = %v", err)
	}
	if aliceClaims.PlayerID() != "alice" || aliceClaims.OpponentID != "bob" {
		t.Errorf("alice claims = %+v", aliceClaims)
	}
}

func TestService_JoinIsIdempotentAndLeaveRemovesPlayer(t *testing.T) {
	t.Parallel()

	service, _ := newTestService(t)
	first, err := service.Join("alice")
	if err != nil {
		t.Fatalf("Join() error = %v", err)
	}
	second, err := service.Join("alice")
	if err != nil {
		t.Fatalf("second Join() error = %v", err)
	}
	if first.Status != second.Status {
		t.Errorf("idempotent statuses = (%q, %q)", first.Status, second.Status)
	}
	if err := service.Leave("alice"); err != nil {
		t.Fatalf("Leave() error = %v", err)
	}
	if got := service.PlayerStatus("alice").Status; got != StatusNotQueued {
		t.Errorf("status after leave = %q, want notQueued", got)
	}
}

func newTestService(t *testing.T) (*Service, *matchticket.Manager) {
	t.Helper()
	tickets, err := matchticket.NewManager(
		[]byte("matchmaking-test-secret-at-least-32-bytes"),
		"gateway",
		"matchserver",
		time.Minute,
	)
	if err != nil {
		t.Fatalf("NewManager() error = %v", err)
	}
	service, err := New(tickets, "localhost:4242", 5*time.Minute)
	if err != nil {
		t.Fatalf("New() error = %v", err)
	}
	return service, tickets
}
