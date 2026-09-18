package gateway

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/auth"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchmaking"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/profile"
	"golang.org/x/crypto/bcrypt"
)

func TestServer_HealthzReturnsOK(t *testing.T) {
	t.Parallel()

	server := newTestServer(t)
	request := httptest.NewRequest(http.MethodGet, "/healthz", nil)
	response := httptest.NewRecorder()
	server.Handler().ServeHTTP(response, request)

	if response.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusOK)
	}
}

func TestServer_AuthFlow(t *testing.T) {
	server := newTestServer(t)

	register := performJSON(
		server,
		http.MethodPost,
		"/api/v1/auth/register",
		`{"login":"Alice","password":"correct horse battery"}`,
		"",
	)
	if register.Code != http.StatusCreated {
		t.Fatalf("register status = %d, want %d; body=%s", register.Code, http.StatusCreated, register.Body.String())
	}
	var registered authResponse
	decodeResponse(t, register, &registered)
	if registered.User.Login != "alice" || registered.AccessToken == "" || registered.RefreshToken == "" {
		t.Fatalf("invalid register response: %+v", registered)
	}

	me := performJSON(server, http.MethodGet, "/api/v1/auth/me", "", registered.AccessToken)
	if me.Code != http.StatusOK {
		t.Fatalf("me status = %d, want %d; body=%s", me.Code, http.StatusOK, me.Body.String())
	}
	var currentUser userResponse
	decodeResponse(t, me, &currentUser)
	if currentUser.ID != registered.User.ID {
		t.Errorf("me user ID = %q, want %q", currentUser.ID, registered.User.ID)
	}

	refresh := performJSON(
		server,
		http.MethodPost,
		"/api/v1/auth/refresh",
		`{"refreshToken":"`+registered.RefreshToken+`"}`,
		"",
	)
	if refresh.Code != http.StatusOK {
		t.Fatalf("refresh status = %d, want %d; body=%s", refresh.Code, http.StatusOK, refresh.Body.String())
	}
	var refreshed authResponse
	decodeResponse(t, refresh, &refreshed)
	if refreshed.RefreshToken == registered.RefreshToken {
		t.Error("refresh token was not rotated")
	}

	reuse := performJSON(
		server,
		http.MethodPost,
		"/api/v1/auth/refresh",
		`{"refreshToken":"`+registered.RefreshToken+`"}`,
		"",
	)
	if reuse.Code != http.StatusUnauthorized {
		t.Errorf("reused refresh status = %d, want %d", reuse.Code, http.StatusUnauthorized)
	}
}

func TestServer_LoginRejectsWrongPasswordWithoutLeakingUser(t *testing.T) {
	server := newTestServer(t)
	register := performJSON(
		server,
		http.MethodPost,
		"/api/v1/auth/register",
		`{"login":"alice","password":"correct horse battery"}`,
		"",
	)
	if register.Code != http.StatusCreated {
		t.Fatalf("register status = %d", register.Code)
	}

	for _, body := range []string{
		`{"login":"alice","password":"wrong password"}`,
		`{"login":"missing","password":"wrong password"}`,
	} {
		response := performJSON(server, http.MethodPost, "/api/v1/auth/login", body, "")
		if response.Code != http.StatusUnauthorized {
			t.Errorf("login status = %d, want %d", response.Code, http.StatusUnauthorized)
		}
		var failure errorResponse
		decodeResponse(t, response, &failure)
		if failure.Error.Code != "INVALID_CREDENTIALS" {
			t.Errorf("error code = %q, want INVALID_CREDENTIALS", failure.Error.Code)
		}
	}
}

func TestServer_MatchmakingRequiresJWTAndReturnsPlayerBoundTickets(t *testing.T) {
	server := newTestServer(t)
	alice := registerPlayer(t, server, "alice")
	bob := registerPlayer(t, server, "bob")

	unauthorized := performJSON(server, http.MethodPost, "/api/v1/queue/join", "", "")
	if unauthorized.Code != http.StatusUnauthorized {
		t.Errorf("unauthorized join status = %d, want %d", unauthorized.Code, http.StatusUnauthorized)
	}

	aliceJoin := performJSON(server, http.MethodPost, "/api/v1/queue/join", "", alice.AccessToken)
	var aliceWaiting queueResponse
	decodeResponse(t, aliceJoin, &aliceWaiting)
	if aliceWaiting.Status != matchmaking.StatusWaiting || aliceWaiting.MatchTicket != "" {
		t.Errorf("alice waiting response = %+v", aliceWaiting)
	}

	bobJoin := performJSON(server, http.MethodPost, "/api/v1/queue/join", "", bob.AccessToken)
	var bobMatched queueResponse
	decodeResponse(t, bobJoin, &bobMatched)
	if bobMatched.Status != matchmaking.StatusMatched || bobMatched.MatchTicket == "" {
		t.Fatalf("bob match response = %+v", bobMatched)
	}

	aliceStatus := performJSON(server, http.MethodGet, "/api/v1/queue/status", "", alice.AccessToken)
	var aliceMatched queueResponse
	decodeResponse(t, aliceStatus, &aliceMatched)
	if aliceMatched.Status != matchmaking.StatusMatched || aliceMatched.MatchID != bobMatched.MatchID {
		t.Errorf("alice match response = %+v, bob = %+v", aliceMatched, bobMatched)
	}
}

func TestServer_RejectsInvalidJSONAndUnknownFields(t *testing.T) {
	t.Parallel()

	server := newTestServer(t)
	response := performJSON(
		server,
		http.MethodPost,
		"/api/v1/auth/register",
		`{"login":"alice","password":"correct horse battery","admin":true}`,
		"",
	)
	if response.Code != http.StatusBadRequest {
		t.Errorf("status = %d, want %d", response.Code, http.StatusBadRequest)
	}
}

func newTestServer(t *testing.T) *Server {
	t.Helper()
	manager, err := auth.NewTokenManager(
		[]byte("gateway-test-secret-at-least-32-bytes"),
		"test-issuer",
		"test-audience",
		15*time.Minute,
		24*time.Hour,
	)
	if err != nil {
		t.Fatalf("NewTokenManager() error = %v", err)
	}
	store := auth.NewMemoryStore()
	service, err := auth.NewService(store, manager, bcrypt.MinCost)
	if err != nil {
		t.Fatalf("NewService() error = %v", err)
	}
	tickets, err := matchticket.NewManager(
		[]byte("gateway-match-ticket-secret-at-least-32-bytes"),
		"pvp-duel-gateway",
		"pvp-duel-matchserver",
		time.Minute,
	)
	if err != nil {
		t.Fatalf("matchticket.NewManager() error = %v", err)
	}
	queue, err := matchmaking.New(tickets, "localhost:4242", 5*time.Minute)
	if err != nil {
		t.Fatalf("matchmaking.New() error = %v", err)
	}
	profiles, err := profile.NewService(profile.NewMemoryStore(store))
	if err != nil {
		t.Fatalf("profile.NewService() error = %v", err)
	}
	return New("", service, queue, profiles, []byte("gateway-internal-result-secret-32-bytes"))
}

func registerPlayer(t *testing.T, server *Server, login string) authResponse {
	t.Helper()
	response := performJSON(
		server,
		http.MethodPost,
		"/api/v1/auth/register",
		`{"login":"`+login+`","password":"correct horse battery"}`,
		"",
	)
	if response.Code != http.StatusCreated {
		t.Fatalf("register %s status = %d; body=%s", login, response.Code, response.Body.String())
	}
	var registered authResponse
	decodeResponse(t, response, &registered)
	return registered
}

func performJSON(
	server *Server,
	method string,
	path string,
	body string,
	accessToken string,
) *httptest.ResponseRecorder {
	request := httptest.NewRequest(method, path, strings.NewReader(body))
	if body != "" {
		request.Header.Set("Content-Type", "application/json")
	}
	if accessToken != "" {
		request.Header.Set("Authorization", "Bearer "+accessToken)
	}
	response := httptest.NewRecorder()
	server.Handler().ServeHTTP(response, request)
	return response
}

func decodeResponse(t *testing.T, response *httptest.ResponseRecorder, target any) {
	t.Helper()
	if err := json.NewDecoder(response.Body).Decode(target); err != nil {
		t.Fatalf("decode response: %v", err)
	}
}
