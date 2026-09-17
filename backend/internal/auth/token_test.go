package auth

import (
	"errors"
	"testing"
	"time"
)

func TestTokenManager_RejectsExpiredAndTamperedAccessTokens(t *testing.T) {
	t.Parallel()

	manager, err := NewTokenManager(
		[]byte("test-secret-that-is-at-least-32-bytes-long"),
		"issuer",
		"audience",
		time.Minute,
		time.Hour,
	)
	if err != nil {
		t.Fatalf("NewTokenManager() error = %v", err)
	}
	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	manager.now = func() time.Time { return now }
	token, _, err := manager.IssueAccess(User{ID: "user-1", Login: "alice"})
	if err != nil {
		t.Fatalf("IssueAccess() error = %v", err)
	}

	if _, err := manager.ParseAccess(token + "tampered"); !errors.Is(err, ErrAccessTokenInvalid) {
		t.Errorf("tampered token error = %v, want ErrAccessTokenInvalid", err)
	}
	now = now.Add(2 * time.Minute)
	if _, err := manager.ParseAccess(token); !errors.Is(err, ErrAccessTokenInvalid) {
		t.Errorf("expired token error = %v, want ErrAccessTokenInvalid", err)
	}
}

func TestTokenManager_RequiresStrongSecret(t *testing.T) {
	t.Parallel()

	_, err := NewTokenManager([]byte("short"), "issuer", "audience", time.Minute, time.Hour)
	if err == nil {
		t.Fatal("NewTokenManager() error = nil, want weak-secret error")
	}
}
