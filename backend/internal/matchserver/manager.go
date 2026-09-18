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
	"google.golang.org/protobuf/proto"
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

	match, err := newMatch(claims.MatchID, waiting.claims.PlayerID(), playerID)
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
		match.abort()
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

	snapshotMutex sync.Mutex
	lastSnapshot  *gamev1.WorldSnapshot
	finishOnce    sync.Once
	cancelOnce    sync.Once
}

func newMatch(matchID, playerA, playerB string) (*match, error) {
	rules := room.DefaultRuleset()
	rules.Arena.ID = arenaIDForMatch(matchID)
	duel, err := room.NewWithRuleset(playerA, playerB, rules)
	if err != nil {
		return nil, err
	}

	ctx, cancel := context.WithCancel(context.Background())
	initialSnapshot := snapshotToProto(duel.Snapshot())
	m := &match{
		ctx:          ctx,
		cancel:       cancel,
		inputs:       make(chan room.QueuedInput, room.TickRate*4),
		players:      make(map[string]*playerSession, 2),
		startBarrier: make(chan struct{}),
		lastSnapshot: proto.Clone(initialSnapshot).(*gamev1.WorldSnapshot),
	}
	start := &gamev1.MatchStart{
		InitialSnapshot:    initialSnapshot,
		TickRate:           room.TickRate,
		CountdownTicks:     room.CountdownTicks,
		MatchDurationTicks: room.MatchDurationTicks,
		ArenaId:            initialSnapshot.GetArenaId(),
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

func arenaIDForMatch(matchID string) string {
	// FNV-1a is intentionally implemented inline so selection is stable across
	// processes and Go versions. Arena choice is visual only in this rollout.
	var hash uint32 = 2166136261
	for index := 0; index < len(matchID); index++ {
		hash ^= uint32(matchID[index])
		hash *= 16777619
	}
	if hash%2 == 0 {
		return room.ArenaIDNeonRooftop
	}
	return room.ArenaIDEmberFoundry
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
		m.updateLastSnapshot(converted)
		for _, player := range m.players {
			publishLatest(player.snapshots, converted)
		}
		if snapshot.Status == room.MatchFinished {
			m.publishMatchEnd(&gamev1.MatchEnd{
				FinalSnapshot:  converted,
				WinnerPlayerId: snapshot.WinnerID,
				Reason:         finishReasonToProto(snapshot.FinishReason),
			})
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

func (m *match) updateLastSnapshot(snapshot *gamev1.WorldSnapshot) {
	m.snapshotMutex.Lock()
	defer m.snapshotMutex.Unlock()
	m.lastSnapshot = proto.Clone(snapshot).(*gamev1.WorldSnapshot)
}

func (m *match) lastSnapshotClone() *gamev1.WorldSnapshot {
	m.snapshotMutex.Lock()
	defer m.snapshotMutex.Unlock()
	return proto.Clone(m.lastSnapshot).(*gamev1.WorldSnapshot)
}

func (m *match) publishMatchEnd(matchEnd *gamev1.MatchEnd) {
	m.finishOnce.Do(func() {
		for _, player := range m.players {
			player.matchEnds <- proto.Clone(matchEnd).(*gamev1.MatchEnd)
		}
	})
}

func (m *match) playerDisconnected(playerID string) {
	winnerID := m.opponentID(playerID)
	finalSnapshot := m.lastSnapshotClone()
	finalSnapshot.Status = gamev1.MatchStatus_MATCH_STATUS_FINISHED
	finalSnapshot.WinnerPlayerId = winnerID
	finalSnapshot.CountdownTicksRemaining = 0
	finalSnapshot.MatchTicksRemaining = 0
	m.publishMatchEnd(&gamev1.MatchEnd{
		FinalSnapshot:  finalSnapshot,
		WinnerPlayerId: winnerID,
		Reason:         gamev1.MatchFinishReason_MATCH_FINISH_REASON_DISCONNECT,
	})
	m.abort()
}

func (m *match) opponentID(playerID string) string {
	for candidate := range m.players {
		if candidate != playerID {
			return candidate
		}
	}
	return ""
}

func (m *match) abort() {
	m.cancelOnce.Do(m.cancel)
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
	actions := make([]room.ActionCommand, 0, min(len(input.GetPendingActions()), room.MaxPendingActions))
	for _, command := range input.GetPendingActions() {
		if len(actions) == room.MaxPendingActions {
			break
		}
		actions = append(actions, room.ActionCommand{
			Sequence: command.GetSequence(),
			Type:     actionTypeFromProto(command.GetType()),
		})
	}
	queued := room.QueuedInput{
		PlayerID: s.playerID,
		Input: room.Input{
			Tick:    input.GetTick(),
			MoveX:   clampProtoAxis(input.GetMoveX()),
			MoveY:   clampProtoAxis(input.GetMoveY()),
			Actions: actions,
		},
	}
	select {
	case s.match.inputs <- queued:
		return nil
	case <-s.match.ctx.Done():
		// A terminal event may already be waiting on the reliable stream. Keep the
		// connection handler alive so MatchEnd wins over a concurrent input reader.
		return nil
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
	s.closeOnce.Do(func() { s.match.playerDisconnected(s.playerID) })
}

func snapshotToProto(snapshot room.Snapshot) *gamev1.WorldSnapshot {
	players := make([]*gamev1.PlayerState, 0, len(snapshot.Players))
	for _, player := range snapshot.Players {
		players = append(players, &gamev1.PlayerState{
			PlayerId:                player.ID,
			PositionX:               player.PositionX,
			PositionY:               player.PositionY,
			Hp:                      player.HP,
			LastAckedInputTick:      player.LastAckedInputTick,
			FacingX:                 int32(player.FacingX),
			FacingY:                 int32(player.FacingY),
			LastAckedActionSequence: player.LastAckedActionSequence,
			ActionState:             actionStateToProto(player.ActionState),
			ActionStartedServerTick: player.ActionStartedServerTick,
			ActionTicksRemaining:    player.ActionTicksRemaining,
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
		ArenaId:                 snapshot.ArenaID,
		ServerTick:              snapshot.ServerTick,
		Players:                 players,
		Status:                  status,
		WinnerPlayerId:          snapshot.WinnerID,
		CountdownTicksRemaining: snapshot.CountdownTicksRemaining,
		MatchTicksRemaining:     snapshot.MatchTicksRemaining,
	}
}

func actionTypeFromProto(action gamev1.ActionType) room.ActionType {
	switch action {
	case gamev1.ActionType_ACTION_TYPE_DASH:
		return room.ActionDash
	case gamev1.ActionType_ACTION_TYPE_LIGHT_ATTACK:
		return room.ActionLightAttack
	default:
		return room.ActionUnspecified
	}
}

func actionStateToProto(state room.ActionState) gamev1.PlayerActionState {
	switch state {
	case room.ActionStateMove:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_MOVE
	case room.ActionStateDash:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_DASH
	case room.ActionStateLightAttackWindup:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_LIGHT_ATTACK_WINDUP
	case room.ActionStateLightAttackActive:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_LIGHT_ATTACK_ACTIVE
	case room.ActionStateLightAttackRecovery:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_LIGHT_ATTACK_RECOVERY
	case room.ActionStateHit:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_HIT
	case room.ActionStateKO:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_KO
	default:
		return gamev1.PlayerActionState_PLAYER_ACTION_STATE_IDLE
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
