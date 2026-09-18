package gateway

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestServer_ProfileRequiresAccessAndReturnsStatistics(t *testing.T) {
	server := newTestServer(t)
	player := registerPlayer(t, server, "alice")
	unauthorized := performJSON(server, http.MethodGet, "/api/v1/profile", "", "")
	if unauthorized.Code != http.StatusUnauthorized {
		t.Fatalf("unauthorized status = %d", unauthorized.Code)
	}

	response := performJSON(server, http.MethodGet, "/api/v1/profile", "", player.AccessToken)
	if response.Code != http.StatusOK {
		t.Fatalf("profile status = %d body=%s", response.Code, response.Body.String())
	}
	var profile profileResponse
	decodeResponse(t, response, &profile)
	if profile.ID != player.User.ID || profile.Statistics.Played != 0 || profile.Statistics.Wins != 0 {
		t.Fatalf("profile = %+v", profile)
	}
}

func TestServer_InternalResultIsAuthenticatedAndIdempotent(t *testing.T) {
	server := newTestServer(t)
	alice := registerPlayer(t, server, "alice")
	bob := registerPlayer(t, server, "bob")
	body := `{"matchId":"00000000-0000-0000-0000-000000000001","playerAId":"` + alice.User.ID +
		`","playerBId":"` + bob.User.ID + `","winnerPlayerId":"` + alice.User.ID +
		`","reason":"ko","endedAt":"` + time.Now().UTC().Format(time.RFC3339Nano) + `"}`

	denied := performJSON(server, http.MethodPost, "/internal/v1/matches/result", body, "")
	if denied.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated internal result status = %d", denied.Code)
	}
	first := performInternalJSON(server, body)
	if first.Code != http.StatusOK {
		t.Fatalf("first result status = %d body=%s", first.Code, first.Body.String())
	}
	var recorded internalMatchResultResponse
	decodeResponse(t, first, &recorded)
	if !recorded.Recorded {
		t.Fatal("first result was not recorded")
	}
	second := performInternalJSON(server, body)
	if second.Code != http.StatusOK {
		t.Fatalf("duplicate result status = %d", second.Code)
	}
	decodeResponse(t, second, &recorded)
	if recorded.Recorded {
		t.Fatal("duplicate result was recorded twice")
	}
	profile := performJSON(server, http.MethodGet, "/api/v1/profile", "", alice.AccessToken)
	var aliceProfile profileResponse
	decodeResponse(t, profile, &aliceProfile)
	if aliceProfile.Statistics != (statisticsResponse{Played: 1, Wins: 1}) {
		t.Fatalf("alice statistics = %+v", aliceProfile.Statistics)
	}
}

func performInternalJSON(server *Server, body string) *httptest.ResponseRecorder {
	request := httptest.NewRequest(http.MethodPost, "/internal/v1/matches/result", strings.NewReader(body))
	request.Header.Set("Content-Type", "application/json")
	request.Header.Set("X-Internal-Match-Secret", "gateway-internal-result-secret-32-bytes")
	response := httptest.NewRecorder()
	server.Handler().ServeHTTP(response, request)
	return response
}
