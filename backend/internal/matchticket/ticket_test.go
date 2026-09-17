package matchticket

import (
	"errors"
	"testing"
	"time"
)

func TestManager_IssueVerifyAndExpire(t *testing.T) {
	t.Parallel()

	manager, err := NewManager(
		[]byte("match-ticket-test-secret-at-least-32-bytes"),
		"gateway",
		"matchserver",
		time.Minute,
	)
	if err != nil {
		t.Fatalf("NewManager() error = %v", err)
	}
	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	manager.now = func() time.Time { return now }
	raw, _, err := manager.Issue("match-1", "alice", "bob")
	if err != nil {
		t.Fatalf("Issue() error = %v", err)
	}
	claims, err := manager.Verify(raw)
	if err != nil {
		t.Fatalf("Verify() error = %v", err)
	}
	if claims.MatchID != "match-1" || claims.PlayerID() != "alice" || claims.OpponentID != "bob" {
		t.Errorf("claims = %+v", claims)
	}

	now = now.Add(2 * time.Minute)
	if _, err := manager.Verify(raw); !errors.Is(err, ErrInvalid) {
		t.Errorf("expired Verify() error = %v, want ErrInvalid", err)
	}
}

func TestManager_RejectsTamperedTicket(t *testing.T) {
	t.Parallel()

	manager, err := NewManager(
		[]byte("match-ticket-test-secret-at-least-32-bytes"),
		"gateway",
		"matchserver",
		time.Minute,
	)
	if err != nil {
		t.Fatalf("NewManager() error = %v", err)
	}
	raw, _, err := manager.Issue("match-1", "alice", "bob")
	if err != nil {
		t.Fatalf("Issue() error = %v", err)
	}
	if _, err := manager.Verify(raw + "tampered"); !errors.Is(err, ErrInvalid) {
		t.Errorf("Verify() error = %v, want ErrInvalid", err)
	}
}
