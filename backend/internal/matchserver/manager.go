// Package matchserver owns in-memory authoritative matches.
package matchserver

import (
	"context"
	"errors"
	"strings"
	"sync"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/room"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/quicserver"
)

var (
	ErrAuthentication = errors.New("invalid match credentials")
	ErrSamePlayer     = errors.New("a player cannot duel itself")
	ErrTicketUsed     = errors.New("match ticket already used")
)

type ticketVerifier interface {
	Verify(raw string) (matchticket.Claims, error)
}

type MatchManager struct {
	verifier ticketVerifier
	mutex    sync.Mutex
	waiting  map[string]*waitingPlayer
	used     map[string]time.Time
}

type waitingPlayer struct {
	claims     matchticket.Claims
	result     chan matchResult
	canceled   chan struct{}
	cancelOnce sync.Once
}

type matchResult struct {
	session quicserver.Session
	err     error
}

func NewMatchManager(verifier ticketVerifier) *MatchManager {
	return &MatchManager{
		verifier: verifier,
		waiting:  make(map[string]*waitingPlayer),
		used:     make(map[string]time.Time),
	}
}

func (m *MatchManager) Authenticate(
	ctx context.Context,
	request *gamev1.AuthenticateRequest,
) (quicserver.Session, error) {
	playerID := strings.TrimSpace(request.GetPlayerId())
	claims, err := m.verifier.Verify(request.GetMatchToken())
	if err != nil || playerID == "" || playerID != claims.PlayerID() {
		return nil, ErrAuthentication
	}

	m.mutex.Lock()
	m.purgeUsed(time.Now())
	if _, used := m.used[claims.MatchID]; used {
		m.mutex.Unlock()
		return nil, ErrTicketUsed
	}
	waiting := m.waiting[claims.MatchID]
	if waiting == nil {
		waiting = &waitingPlayer{
			claims:   claims,
			result:   make(chan matchResult),
			canceled: make(chan struct{}),
		}
		m.waiting[claims.MatchID] = waiting
		m.mutex.Unlock()

		select {
		case result := <-waiting.result:
			return result.session, result.err
		case <-ctx.Done():
			waiting.cancelOnce.Do(func() { close(waiting.canceled) })
			m.removeWaiting(claims.MatchID, waiting)
			return nil, ctx.Err()
		}
	}
	if waiting.claims.PlayerID() == playerID {
		m.mutex.Unlock()
		return nil, ErrSamePlayer
	}
	if waiting.claims.OpponentID != playerID || claims.OpponentID != waiting.claims.PlayerID() {
		m.mutex.Unlock()
		return nil, ErrAuthentication
	}
	delete(m.waiting, claims.MatchID)
	m.used[claims.MatchID] = claims.ExpiresAt.Time
	m.mutex.Unlock()

	match, err := newMatch(waiting.claims.PlayerID(), playerID)
	if err != nil {
		select {
		case waiting.result <- matchResult{err: err}:
		case <-waiting.canceled:
		}
		return nil, err
	}
	firstSession := match.session(waiting.claims.PlayerID())
	secondSession := match.session(playerID)
	select {
	case waiting.result <- matchResult{session: firstSession}:
		return secondSession, nil
	case <-waiting.canceled:
		firstSession.Close()
		secondSession.Close()
		return nil, context.Canceled
	}
}

func (m *MatchManager) purgeUsed(now time.Time) {
	for matchID, expiresAt := range m.used {
		if !now.Before(expiresAt) {
			delete(m.used, matchID)
		}
	}
}

func (m *MatchManager) removeWaiting(matchID string, waiting *waitingPlayer) {
	m.mutex.Lock()
	defer m.mutex.Unlock()
	if m.waiting[matchID] == waiting {
		delete(m.waiting, matchID)
	}
}

type match struct {
	ctx          context.Context
	cancel       context.CancelFunc
	inputs       chan room.QueuedInput
	players      map[string]*playerSession
	startBarrier chan struct{}
	startMutex   sync.Mutex
	startCount   int
	closeOnce    sync.Once
}

func newMatch(playerA, playerB string) (*match, error) {
	duel, err := room.New(playerA, playerB)
	if err != nil {
		return nil, err
	}

	ctx, cancel := context.WithCancel(context.Background())
	m := &match{
		ctx:          ctx,
		cancel:       cancel,
		inputs:       make(chan room.QueuedInput, room.TickRate*4),
		players:      make(map[string]*playerSession, 2),
		startBarrier: make(chan struct{}),
	}
	start := &gamev1.MatchStart{
		InitialSnapshot:    snapshotToProto(duel.Snapshot()),
		TickRate:           room.TickRate,
		CountdownTicks:     room.CountdownTicks,
		MatchDurationTicks: room.MatchDurationTicks,
	}
	m.players[playerA] = newPlayerSession(m, playerA, start)
	m.players[playerB] = newPlayerSession(m, playerB, start)

	roomSnapshots := make(chan room.Snapshot, 1)
	loop := room.NewLoop(duel, m.inputs, roomSnapshots)
	go func() {
		select {
		case <-m.startBarrier:
			_ = loop.RunRealtime(ctx)
		case <-ctx.Done():
		}
		close(roomSnapshots)
	}()
	go m.broadcast(roomSnapshots)
	return m, nil
}

func (m *match) session(playerID string) quicserver.Session {
	return m.players[playerID]
}

func (m *match) broadcast(roomSnapshots <-chan room.Snapshot) {
	defer func() {
		for _, player := range m.players {
			close(player.snapshots)
			close(player.matchEnds)
		}
	}()

	for snapshot := range roomSnapshots {
		converted := snapshotToProto(snapshot)
		for _, player := range m.players {
			publishLatest(player.snapshots, converted)
		}
		if snapshot.Status == room.MatchFinished {
			matchEnd := &gamev1.MatchEnd{
				FinalSnapshot:  converted,
				WinnerPlayerId: snapshot.WinnerID,
				Reason:         finishReasonToProto(snapshot.FinishReason),
			}
			// Each terminal channel is dedicated and buffered, unlike lossy
			// snapshots, so MatchEnd cannot be displaced or dropped.
			for _, player := range m.players {
				player.matchEnds <- matchEnd
			}
		}
	}
}

func publishLatest(channel chan *gamev1.WorldSnapshot, snapshot *gamev1.WorldSnapshot) {
	select {
	case channel <- snapshot:
		return
	default:
	}
	select {
	case <-channel:
	default:
	}
	select {
	case channel <- snapshot:
	default:
	}
}

func (m *match) acknowledgeStart() {
	m.startMutex.Lock()
	defer m.startMutex.Unlock()
	m.startCount++
	if m.startCount == len(m.players) {
		close(m.startBarrier)
	}
}

func (m *match) close() {
	m.closeOnce.Do(m.cancel)
}

type playerSession struct {
	match        *match
	playerID     string
	matchStart   *gamev1.MatchStart
	snapshots    chan *gamev1.WorldSnapshot
	matchEnds    chan *gamev1.MatchEnd
	startAckOnce sync.Once
	closeOnce    sync.Once
}

func newPlayerSession(match *match, playerID string, start *gamev1.MatchStart) *playerSession {
	return &playerSession{
		match:      match,
		playerID:   playerID,
		matchStart: start,
		snapshots:  make(chan *gamev1.WorldSnapshot, 1),
		matchEnds:  make(chan *gamev1.MatchEnd, 1),
	}
}

func (s *playerSession) SubmitInput(input *gamev1.PlayerInput) error {
	queued := room.QueuedInput{
		PlayerID: s.playerID,
		Input: room.Input{
			Tick:   input.GetTick(),
			MoveX:  clampProtoAxis(input.GetMoveX()),
			MoveY:  clampProtoAxis(input.GetMoveY()),
			Attack: input.GetAttack(),
		},
	}
	select {
	case s.match.inputs <- queued:
		return nil
	case <-s.match.ctx.Done():
		return context.Canceled
	default:
		return nil
	}
}

func (s *playerSession) MatchStart() *gamev1.MatchStart {
	return s.matchStart
}

func (s *playerSession) AcknowledgeMatchStart() {
	s.startAckOnce.Do(s.match.acknowledgeStart)
}

func (s *playerSession) Snapshots() <-chan *gamev1.WorldSnapshot {
	return s.snapshots
}

func (s *playerSession) MatchEnds() <-chan *gamev1.MatchEnd {
	return s.matchEnds
}

func (s *playerSession) Close() {
	s.closeOnce.Do(s.match.close)
}

func snapshotToProto(snapshot room.Snapshot) *gamev1.WorldSnapshot {
	players := make([]*gamev1.PlayerState, 0, len(snapshot.Players))
	for _, player := range snapshot.Players {
		players = append(players, &gamev1.PlayerState{
			PlayerId:           player.ID,
			PositionX:          player.PositionX,
			PositionY:          player.PositionY,
			Hp:                 player.HP,
			LastAckedInputTick: player.LastAckedInputTick,
		})
	}

	status := gamev1.MatchStatus_MATCH_STATUS_ACTIVE
	switch snapshot.Status {
	case room.MatchFinished:
		status = gamev1.MatchStatus_MATCH_STATUS_FINISHED
	case room.MatchWaiting:
		status = gamev1.MatchStatus_MATCH_STATUS_WAITING
	case room.MatchCountdown:
		status = gamev1.MatchStatus_MATCH_STATUS_COUNTDOWN
	}
	return &gamev1.WorldSnapshot{
		ServerTick:              snapshot.ServerTick,
		Players:                 players,
		Status:                  status,
		WinnerPlayerId:          snapshot.WinnerID,
		CountdownTicksRemaining: snapshot.CountdownTicksRemaining,
		MatchTicksRemaining:     snapshot.MatchTicksRemaining,
	}
}

func finishReasonToProto(reason room.FinishReason) gamev1.MatchFinishReason {
	switch reason {
	case room.FinishReasonKO:
		return gamev1.MatchFinishReason_MATCH_FINISH_REASON_KO
	case room.FinishReasonTimeLimit:
		return gamev1.MatchFinishReason_MATCH_FINISH_REASON_TIME_LIMIT
	default:
		return gamev1.MatchFinishReason_MATCH_FINISH_REASON_UNSPECIFIED
	}
}

func clampProtoAxis(axis int32) int8 {
	if axis < -1 {
		return -1
	}
	if axis > 1 {
		return 1
	}
	return int8(axis)
}
