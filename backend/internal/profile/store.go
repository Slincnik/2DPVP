package profile

import (
	"context"
	"sync"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/auth"
)

// MemoryStore is a concurrency-safe test and single-process development store.
// Postgres is used by the Gateway executable.
type MemoryStore struct {
	mutex   sync.RWMutex
	users   *auth.MemoryStore
	results map[string]MatchResult
}

func NewMemoryStore(users *auth.MemoryStore) *MemoryStore {
	return &MemoryStore{users: users, results: make(map[string]MatchResult)}
}

func (s *MemoryStore) RecordMatchResult(_ context.Context, result MatchResult) (bool, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	if _, exists := s.results[result.MatchID]; exists {
		return false, nil
	}
	s.results[result.MatchID] = result
	return true, nil
}

func (s *MemoryStore) ProfileByUserID(ctx context.Context, userID string) (Profile, error) {
	user, err := s.users.UserByID(ctx, userID)
	if err != nil {
		return Profile{}, err
	}
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	statistics := Statistics{}
	for _, result := range s.results {
		if result.PlayerAID != userID && result.PlayerBID != userID {
			continue
		}
		statistics.Played++
		switch {
		case result.WinnerID == "":
			statistics.Draws++
		case result.WinnerID == userID:
			statistics.Wins++
		default:
			statistics.Losses++
		}
	}
	return Profile{UserID: user.ID, Login: user.Login, CreatedAt: user.CreatedAt, Statistics: statistics}, nil
}

var _ Store = (*MemoryStore)(nil)
