package matchserver

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"

	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
)

func TestHTTPResultReporterAuthenticatesInternalDelivery(t *testing.T) {
	t.Parallel()
	var requests atomic.Int32
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests.Add(1)
		if r.URL.Path != "/internal/v1/matches/result" ||
			r.Header.Get("X-Internal-Match-Secret") != "internal-match-result-test-secret-123" {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		body, err := io.ReadAll(r.Body)
		if err != nil || len(body) == 0 {
			w.WriteHeader(http.StatusBadRequest)
			return
		}
		w.WriteHeader(http.StatusOK)
	}))
	defer server.Close()

	reporter, err := NewHTTPResultReporter(server.URL, "internal-match-result-test-secret-123")
	if err != nil {
		t.Fatal(err)
	}
	err = reporter.Report(t.Context(), TerminalResult{
		MatchID: "match-1", PlayerAID: "alice", PlayerBID: "bob", WinnerID: "alice",
		Reason: gamev1.MatchFinishReason_MATCH_FINISH_REASON_KO, EndedAt: time.Now().UTC(),
	})
	if err != nil || requests.Load() != 1 {
		t.Fatalf("Report() err = %v requests=%d", err, requests.Load())
	}
}

func TestRetryReportRetriesDuplicateSafeTerminalResult(t *testing.T) {
	t.Parallel()
	reporter := &retryReporter{failures: 1}
	RetryReport(context.Background(), reporter, TerminalResult{MatchID: "match-1"})
	if reporter.calls.Load() != 2 {
		t.Fatalf("calls = %d, want 2", reporter.calls.Load())
	}
}

type retryReporter struct {
	failures int32
	calls    atomic.Int32
}

func (r *retryReporter) Report(context.Context, TerminalResult) error {
	call := r.calls.Add(1)
	if call <= r.failures {
		return context.DeadlineExceeded
	}
	return nil
}
