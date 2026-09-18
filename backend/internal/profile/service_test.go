package profile

import (
	"context"
	"testing"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/auth"
)

func TestRecordMatchResultIsIdempotentAndBuildsStatistics(t *testing.T) {
	t.Parallel()
	users := auth.NewMemoryStore()
	now := time.Now().UTC()
	for _, user := range []auth.User{
		{ID: "alice", Login: "alice", CreatedAt: now},
		{ID: "bob", Login: "bob", CreatedAt: now},
	} {
		if err := users.CreateUser(t.Context(), user); err != nil {
			t.Fatal(err)
		}
	}
	service, err := NewService(NewMemoryStore(users))
	if err != nil {
		t.Fatal(err)
	}
	result := MatchResult{
		MatchID: "match-1", PlayerAID: "alice", PlayerBID: "bob",
		WinnerID: "alice", Reason: "ko", EndedAt: now,
	}
	inserted, err := service.RecordMatchResult(context.Background(), result)
	if err != nil || !inserted {
		t.Fatalf("first record = inserted %t err %v", inserted, err)
	}
	inserted, err = service.RecordMatchResult(context.Background(), result)
	if err != nil || inserted {
		t.Fatalf("duplicate record = inserted %t err %v", inserted, err)
	}
	alice, err := service.Get(t.Context(), "alice")
	if err != nil {
		t.Fatal(err)
	}
	if alice.Statistics != (Statistics{Played: 1, Wins: 1}) {
		t.Fatalf("alice stats = %+v", alice.Statistics)
	}
	bob, err := service.Get(t.Context(), "bob")
	if err != nil {
		t.Fatal(err)
	}
	if bob.Statistics != (Statistics{Played: 1, Losses: 1}) {
		t.Fatalf("bob stats = %+v", bob.Statistics)
	}
}

func TestRecordMatchResultRejectsInvalidWinner(t *testing.T) {
	t.Parallel()
	service, err := NewService(NewMemoryStore(auth.NewMemoryStore()))
	if err != nil {
		t.Fatal(err)
	}
	_, err = service.RecordMatchResult(t.Context(), MatchResult{
		MatchID: "match-1", PlayerAID: "alice", PlayerBID: "bob", WinnerID: "mallory", Reason: "ko",
	})
	if err != ErrInvalidResult {
		t.Fatalf("RecordMatchResult() error = %v, want ErrInvalidResult", err)
	}
}
