package matchmaking

import (
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	"github.com/google/uuid"
)

var (
	ErrInvalidPlayer  = errors.New("matchmaking: invalid player")
	ErrAlreadyMatched = errors.New("matchmaking: match already created")
)

type Status string

const (
	StatusNotQueued Status = "notQueued"
	StatusWaiting   Status = "waiting"
	StatusMatched   Status = "matched"
)

type Result struct {
	Status       Status
	MatchID      string
	OpponentID   string
	ServerAddr   string
	MatchTicket  string
	TicketExpiry time.Time
}

type Service struct {
	mutex      sync.Mutex
	tickets    *matchticket.Manager
	serverAddr string
	waitingTTL time.Duration
	now        func() time.Time
	entries    map[string]entry
	waiting    []string
}

type entry struct {
	result   Result
	joinedAt time.Time
}

func New(
	tickets *matchticket.Manager,
	serverAddr string,
	waitingTTL time.Duration,
) (*Service, error) {
	if tickets == nil || serverAddr == "" || waitingTTL <= 0 {
		return nil, errors.New("matchmaking: invalid configuration")
	}
	return &Service{
		tickets:    tickets,
		serverAddr: serverAddr,
		waitingTTL: waitingTTL,
		now:        time.Now,
		entries:    make(map[string]entry),
	}, nil
}

func (s *Service) Join(playerID string) (Result, error) {
	if playerID == "" {
		return Result{}, ErrInvalidPlayer
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()
	now := s.now().UTC()
	s.purgeExpired(now)
	if existing, ok := s.entries[playerID]; ok {
		return existing.result, nil
	}

	opponentID := s.nextWaiting(playerID)
	if opponentID == "" {
		result := Result{Status: StatusWaiting}
		s.entries[playerID] = entry{result: result, joinedAt: now}
		s.waiting = append(s.waiting, playerID)
		return result, nil
	}

	matchID := uuid.NewString()
	playerTicket, expiry, err := s.tickets.Issue(matchID, playerID, opponentID)
	if err != nil {
		return Result{}, fmt.Errorf("issue player match ticket: %w", err)
	}
	opponentTicket, _, err := s.tickets.Issue(matchID, opponentID, playerID)
	if err != nil {
		return Result{}, fmt.Errorf("issue opponent match ticket: %w", err)
	}

	playerResult := Result{
		Status:       StatusMatched,
		MatchID:      matchID,
		OpponentID:   opponentID,
		ServerAddr:   s.serverAddr,
		MatchTicket:  playerTicket,
		TicketExpiry: expiry,
	}
	opponentResult := Result{
		Status:       StatusMatched,
		MatchID:      matchID,
		OpponentID:   playerID,
		ServerAddr:   s.serverAddr,
		MatchTicket:  opponentTicket,
		TicketExpiry: expiry,
	}
	s.entries[playerID] = entry{result: playerResult, joinedAt: now}
	s.entries[opponentID] = entry{result: opponentResult, joinedAt: now}
	return playerResult, nil
}

func (s *Service) PlayerStatus(playerID string) Result {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	s.purgeExpired(s.now().UTC())
	if existing, ok := s.entries[playerID]; ok {
		return existing.result
	}
	return Result{Status: StatusNotQueued}
}

func (s *Service) Leave(playerID string) error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	current, ok := s.entries[playerID]
	if ok && current.result.Status == StatusMatched {
		return ErrAlreadyMatched
	}
	delete(s.entries, playerID)
	return nil
}

func (s *Service) nextWaiting(playerID string) string {
	for len(s.waiting) > 0 {
		candidate := s.waiting[0]
		s.waiting = s.waiting[1:]
		if candidate == playerID {
			continue
		}
		current, ok := s.entries[candidate]
		if ok && current.result.Status == StatusWaiting {
			return candidate
		}
	}
	return ""
}

func (s *Service) purgeExpired(now time.Time) {
	for playerID, current := range s.entries {
		switch current.result.Status {
		case StatusWaiting:
			if now.Sub(current.joinedAt) >= s.waitingTTL {
				delete(s.entries, playerID)
			}
		case StatusMatched:
			if !now.Before(current.result.TicketExpiry) {
				delete(s.entries, playerID)
			}
		}
	}
}
