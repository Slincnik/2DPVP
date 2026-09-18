// Package profile owns persisted terminal match results and player statistics.
package profile

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"time"
)

var ErrInvalidResult = errors.New("profile: invalid match result")

type MatchResult struct {
	MatchID   string
	PlayerAID string
	PlayerBID string
	WinnerID  string
	Reason    string
	EndedAt   time.Time
}

type Statistics struct {
	Played int64
	Wins   int64
	Losses int64
	Draws  int64
}

type Profile struct {
	UserID     string
	Login      string
	CreatedAt  time.Time
	Statistics Statistics
}

type Store interface {
	RecordMatchResult(context.Context, MatchResult) (inserted bool, err error)
	ProfileByUserID(context.Context, string) (Profile, error)
}

type Service struct {
	store Store
	now   func() time.Time
}

func NewService(store Store) (*Service, error) {
	if store == nil {
		return nil, errors.New("profile: store is required")
	}
	return &Service{store: store, now: time.Now}, nil
}

func (s *Service) RecordMatchResult(ctx context.Context, result MatchResult) (bool, error) {
	result.MatchID = strings.TrimSpace(result.MatchID)
	result.PlayerAID = strings.TrimSpace(result.PlayerAID)
	result.PlayerBID = strings.TrimSpace(result.PlayerBID)
	result.WinnerID = strings.TrimSpace(result.WinnerID)
	result.Reason = strings.TrimSpace(result.Reason)
	if result.MatchID == "" || result.PlayerAID == "" || result.PlayerBID == "" ||
		result.PlayerAID == result.PlayerBID || !validReason(result.Reason) ||
		(result.WinnerID != "" && result.WinnerID != result.PlayerAID && result.WinnerID != result.PlayerBID) {
		return false, ErrInvalidResult
	}
	if result.EndedAt.IsZero() {
		result.EndedAt = s.now().UTC()
	}
	inserted, err := s.store.RecordMatchResult(ctx, result)
	if err != nil {
		return false, fmt.Errorf("record match result: %w", err)
	}
	return inserted, nil
}

func (s *Service) Get(ctx context.Context, userID string) (Profile, error) {
	result, err := s.store.ProfileByUserID(ctx, userID)
	if err != nil {
		return Profile{}, fmt.Errorf("get profile: %w", err)
	}
	return result, nil
}

func validReason(reason string) bool {
	return reason == "ko" || reason == "time_limit" || reason == "disconnect"
}
