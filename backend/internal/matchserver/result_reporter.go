package matchserver

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"time"

	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
)

// ResultReporter delivers a terminal authoritative result to the Gateway. It
// never receives input from a game client.
type ResultReporter interface {
	Report(context.Context, TerminalResult) error
}

type TerminalResult struct {
	MatchID   string
	PlayerAID string
	PlayerBID string
	WinnerID  string
	Reason    gamev1.MatchFinishReason
	EndedAt   time.Time
}

type HTTPResultReporter struct {
	endpoint string
	secret   string
	client   *http.Client
}

func NewHTTPResultReporter(gatewayURL, secret string) (*HTTPResultReporter, error) {
	gatewayURL = strings.TrimRight(strings.TrimSpace(gatewayURL), "/")
	if gatewayURL == "" || len(secret) < 32 {
		return nil, fmt.Errorf("gateway result reporter requires URL and a secret of at least 32 bytes")
	}
	return &HTTPResultReporter{
		endpoint: gatewayURL + "/internal/v1/matches/result",
		secret:   secret,
		client:   &http.Client{Timeout: 5 * time.Second},
	}, nil
}

func (r *HTTPResultReporter) Report(ctx context.Context, result TerminalResult) error {
	payload := struct {
		MatchID   string    `json:"matchId"`
		PlayerAID string    `json:"playerAId"`
		PlayerBID string    `json:"playerBId"`
		WinnerID  string    `json:"winnerPlayerId"`
		Reason    string    `json:"reason"`
		EndedAt   time.Time `json:"endedAt"`
	}{
		MatchID: result.MatchID, PlayerAID: result.PlayerAID, PlayerBID: result.PlayerBID,
		WinnerID: result.WinnerID, Reason: reasonName(result.Reason), EndedAt: result.EndedAt,
	}
	body, err := json.Marshal(payload)
	if err != nil {
		return fmt.Errorf("marshal match result: %w", err)
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, r.endpoint, bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("create match result request: %w", err)
	}
	request.Header.Set("Content-Type", "application/json")
	request.Header.Set("X-Internal-Match-Secret", r.secret)
	response, err := r.client.Do(request)
	if err != nil {
		return fmt.Errorf("post match result: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("post match result returned HTTP %d", response.StatusCode)
	}
	return nil
}

func reasonName(reason gamev1.MatchFinishReason) string {
	switch reason {
	case gamev1.MatchFinishReason_MATCH_FINISH_REASON_KO:
		return "ko"
	case gamev1.MatchFinishReason_MATCH_FINISH_REASON_TIME_LIMIT:
		return "time_limit"
	default:
		return "disconnect"
	}
}

// RetryReport retries boundedly. The Gateway idempotency key makes repeated
// deliveries safe after a lost response.
func RetryReport(ctx context.Context, reporter ResultReporter, result TerminalResult) {
	for attempt := 0; attempt < 3; attempt++ {
		if reporter.Report(ctx, result) == nil || ctx.Err() != nil {
			return
		}
		timer := time.NewTimer(time.Duration(attempt+1) * 100 * time.Millisecond)
		select {
		case <-ctx.Done():
			timer.Stop()
			return
		case <-timer.C:
		}
	}
}
