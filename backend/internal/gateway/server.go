package gateway

import (
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/auth"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchmaking"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/profile"
)

const maxRequestBodyBytes = 1 << 20

type claimsContextKey struct{}

type Server struct {
	addr           string
	handler        http.Handler
	auth           *auth.Service
	queue          *matchmaking.Service
	profiles       *profile.Service
	internalSecret []byte
}

type credentialsRequest struct {
	Login    string `json:"login"`
	Password string `json:"password"`
}

type refreshRequest struct {
	RefreshToken string `json:"refreshToken"`
}

type authResponse struct {
	User         userResponse `json:"user"`
	AccessToken  string       `json:"accessToken"`
	RefreshToken string       `json:"refreshToken"`
	TokenType    string       `json:"tokenType"`
	ExpiresAt    time.Time    `json:"expiresAt"`
}

type userResponse struct {
	ID        string    `json:"id"`
	Login     string    `json:"login"`
	CreatedAt time.Time `json:"createdAt"`
}

type queueResponse struct {
	Status          matchmaking.Status `json:"status"`
	MatchID         string             `json:"matchId,omitempty"`
	OpponentID      string             `json:"opponentId,omitempty"`
	ServerAddr      string             `json:"serverAddr,omitempty"`
	MatchTicket     string             `json:"matchTicket,omitempty"`
	TicketExpiresAt *time.Time         `json:"ticketExpiresAt,omitempty"`
}

type profileResponse struct {
	ID         string             `json:"id"`
	Login      string             `json:"login"`
	CreatedAt  time.Time          `json:"createdAt"`
	Statistics statisticsResponse `json:"statistics"`
}

type statisticsResponse struct {
	Played int64 `json:"played"`
	Wins   int64 `json:"wins"`
	Losses int64 `json:"losses"`
	Draws  int64 `json:"draws"`
}

type internalMatchResultRequest struct {
	MatchID   string    `json:"matchId"`
	PlayerAID string    `json:"playerAId"`
	PlayerBID string    `json:"playerBId"`
	WinnerID  string    `json:"winnerPlayerId"`
	Reason    string    `json:"reason"`
	EndedAt   time.Time `json:"endedAt"`
}

type internalMatchResultResponse struct {
	Recorded bool `json:"recorded"`
}

type healthResponse struct {
	Status string `json:"status"`
}

type errorResponse struct {
	Error apiError `json:"error"`
}

type apiError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

func New(
	addr string,
	authService *auth.Service,
	queue *matchmaking.Service,
	profiles *profile.Service,
	internalSecret []byte,
) *Server {
	server := &Server{
		addr: addr, auth: authService, queue: queue, profiles: profiles,
		internalSecret: append([]byte(nil), internalSecret...),
	}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", handleHealth)
	mux.HandleFunc("POST /api/v1/auth/register", server.handleRegister)
	mux.HandleFunc("POST /api/v1/auth/login", server.handleLogin)
	mux.HandleFunc("POST /api/v1/auth/refresh", server.handleRefresh)
	mux.HandleFunc("POST /api/v1/auth/logout", server.handleLogout)
	mux.Handle("GET /api/v1/auth/me", server.requireAccess(http.HandlerFunc(server.handleMe)))
	mux.Handle("GET /api/v1/profile", server.requireAccess(http.HandlerFunc(server.handleProfile)))
	mux.Handle("POST /internal/v1/matches/result", http.HandlerFunc(server.handleInternalMatchResult))
	mux.Handle("POST /api/v1/queue/join", server.requireAccess(http.HandlerFunc(server.handleQueueJoin)))
	mux.Handle("GET /api/v1/queue/status", server.requireAccess(http.HandlerFunc(server.handleQueueStatus)))
	mux.Handle("DELETE /api/v1/queue", server.requireAccess(http.HandlerFunc(server.handleQueueLeave)))
	server.handler = mux
	return server
}

func (s *Server) Handler() http.Handler {
	return s.handler
}

func (s *Server) ListenAndServe(ctx context.Context) error {
	httpServer := &http.Server{
		Addr:              s.addr,
		Handler:           s.handler,
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       60 * time.Second,
	}

	serverErrors := make(chan error, 1)
	go func() {
		serverErrors <- httpServer.ListenAndServe()
	}()

	select {
	case err := <-serverErrors:
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return fmt.Errorf("listen and serve: %w", err)
	case <-ctx.Done():
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		if err := httpServer.Shutdown(shutdownCtx); err != nil {
			return fmt.Errorf("shutdown server: %w", err)
		}
		return nil
	}
}

func handleHealth(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, healthResponse{Status: "ok"})
}

func (s *Server) handleRegister(w http.ResponseWriter, r *http.Request) {
	request, ok := decodeJSON[credentialsRequest](w, r)
	if !ok {
		return
	}
	user, tokens, err := s.auth.Register(r.Context(), request.Login, request.Password)
	if err != nil {
		s.writeAuthError(w, err)
		return
	}
	writeJSON(w, http.StatusCreated, newAuthResponse(user, tokens))
}

func (s *Server) handleLogin(w http.ResponseWriter, r *http.Request) {
	request, ok := decodeJSON[credentialsRequest](w, r)
	if !ok {
		return
	}
	user, tokens, err := s.auth.Login(r.Context(), request.Login, request.Password)
	if err != nil {
		s.writeAuthError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, newAuthResponse(user, tokens))
}

func (s *Server) handleRefresh(w http.ResponseWriter, r *http.Request) {
	request, ok := decodeJSON[refreshRequest](w, r)
	if !ok {
		return
	}
	user, tokens, err := s.auth.Refresh(r.Context(), request.RefreshToken)
	if err != nil {
		s.writeAuthError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, newAuthResponse(user, tokens))
}

func (s *Server) handleLogout(w http.ResponseWriter, r *http.Request) {
	request, ok := decodeJSON[refreshRequest](w, r)
	if !ok {
		return
	}
	if err := s.auth.Logout(r.Context(), request.RefreshToken); err != nil {
		writeError(w, http.StatusInternalServerError, "INTERNAL_ERROR", "internal server error")
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (s *Server) handleMe(w http.ResponseWriter, r *http.Request) {
	claims, ok := r.Context().Value(claimsContextKey{}).(auth.AccessClaims)
	if !ok {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
		return
	}
	user, err := s.auth.UserByID(r.Context(), claims.Subject)
	if err != nil {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
		return
	}
	writeJSON(w, http.StatusOK, toUserResponse(user))
}

func (s *Server) handleProfile(w http.ResponseWriter, r *http.Request) {
	playerID, ok := playerIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
		return
	}
	result, err := s.profiles.Get(r.Context(), playerID)
	if errors.Is(err, auth.ErrUserNotFound) {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
		return
	}
	if err != nil {
		writeError(w, http.StatusInternalServerError, "INTERNAL_ERROR", "internal server error")
		return
	}
	writeJSON(w, http.StatusOK, profileResponse{
		ID: result.UserID, Login: result.Login, CreatedAt: result.CreatedAt,
		Statistics: statisticsResponse{
			Played: result.Statistics.Played, Wins: result.Statistics.Wins,
			Losses: result.Statistics.Losses, Draws: result.Statistics.Draws,
		},
	})
}

func (s *Server) handleInternalMatchResult(w http.ResponseWriter, r *http.Request) {
	if len(s.internalSecret) == 0 || subtle.ConstantTimeCompare(
		[]byte(r.Header.Get("X-Internal-Match-Secret")), s.internalSecret,
	) != 1 {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "internal authentication required")
		return
	}
	request, ok := decodeJSON[internalMatchResultRequest](w, r)
	if !ok {
		return
	}
	recorded, err := s.profiles.RecordMatchResult(r.Context(), profile.MatchResult{
		MatchID: request.MatchID, PlayerAID: request.PlayerAID, PlayerBID: request.PlayerBID,
		WinnerID: request.WinnerID, Reason: request.Reason, EndedAt: request.EndedAt,
	})
	if errors.Is(err, profile.ErrInvalidResult) {
		writeError(w, http.StatusBadRequest, "INVALID_RESULT", "invalid match result")
		return
	}
	if err != nil {
		writeError(w, http.StatusInternalServerError, "INTERNAL_ERROR", "internal server error")
		return
	}
	writeJSON(w, http.StatusOK, internalMatchResultResponse{Recorded: recorded})
}

func (s *Server) handleQueueJoin(w http.ResponseWriter, r *http.Request) {
	playerID, ok := playerIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
		return
	}
	result, err := s.queue.Join(playerID)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "INTERNAL_ERROR", "internal server error")
		return
	}
	writeJSON(w, http.StatusOK, newQueueResponse(result))
}

func (s *Server) handleQueueStatus(w http.ResponseWriter, r *http.Request) {
	playerID, ok := playerIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
		return
	}
	writeJSON(w, http.StatusOK, newQueueResponse(s.queue.PlayerStatus(playerID)))
}

func (s *Server) handleQueueLeave(w http.ResponseWriter, r *http.Request) {
	playerID, ok := playerIDFromContext(r.Context())
	if !ok {
		writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
		return
	}
	if err := s.queue.Leave(playerID); err != nil {
		writeError(w, http.StatusInternalServerError, "INTERNAL_ERROR", "internal server error")
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (s *Server) requireAccess(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		header := r.Header.Get("Authorization")
		scheme, token, found := strings.Cut(header, " ")
		if !found || !strings.EqualFold(scheme, "Bearer") || token == "" {
			writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "authentication required")
			return
		}
		claims, err := s.auth.AuthenticateAccess(token)
		if err != nil {
			writeError(w, http.StatusUnauthorized, "UNAUTHORIZED", "invalid or expired access token")
			return
		}
		ctx := context.WithValue(r.Context(), claimsContextKey{}, claims)
		next.ServeHTTP(w, r.WithContext(ctx))
	})
}

func (s *Server) writeAuthError(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, auth.ErrInvalidLogin):
		writeError(w, http.StatusBadRequest, "INVALID_LOGIN", "login must match [a-z0-9_]{3,32}")
	case errors.Is(err, auth.ErrInvalidPassword):
		writeError(w, http.StatusBadRequest, "INVALID_PASSWORD", "password must contain 10-72 UTF-8 bytes")
	case errors.Is(err, auth.ErrUserExists):
		writeError(w, http.StatusConflict, "LOGIN_TAKEN", "login is already registered")
	case errors.Is(err, auth.ErrInvalidCredentials):
		writeError(w, http.StatusUnauthorized, "INVALID_CREDENTIALS", "invalid login or password")
	case errors.Is(err, auth.ErrRefreshTokenInvalid):
		writeError(w, http.StatusUnauthorized, "INVALID_REFRESH_TOKEN", "invalid or expired refresh token")
	default:
		writeError(w, http.StatusInternalServerError, "INTERNAL_ERROR", "internal server error")
	}
}

func decodeJSON[T any](w http.ResponseWriter, r *http.Request) (T, bool) {
	var request T
	r.Body = http.MaxBytesReader(w, r.Body, maxRequestBodyBytes)
	defer r.Body.Close()
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&request); err != nil {
		writeError(w, http.StatusBadRequest, "INVALID_REQUEST", "invalid JSON request")
		return request, false
	}
	if err := decoder.Decode(&struct{}{}); !errors.Is(err, io.EOF) {
		writeError(w, http.StatusBadRequest, "INVALID_REQUEST", "request must contain one JSON object")
		return request, false
	}
	return request, true
}

func playerIDFromContext(ctx context.Context) (string, bool) {
	claims, ok := ctx.Value(claimsContextKey{}).(auth.AccessClaims)
	return claims.Subject, ok && claims.Subject != ""
}

func newQueueResponse(result matchmaking.Result) queueResponse {
	response := queueResponse{
		Status:      result.Status,
		MatchID:     result.MatchID,
		OpponentID:  result.OpponentID,
		ServerAddr:  result.ServerAddr,
		MatchTicket: result.MatchTicket,
	}
	if !result.TicketExpiry.IsZero() {
		expiry := result.TicketExpiry
		response.TicketExpiresAt = &expiry
	}
	return response
}

func newAuthResponse(user auth.User, tokens auth.Tokens) authResponse {
	return authResponse{
		User:         toUserResponse(user),
		AccessToken:  tokens.AccessToken,
		RefreshToken: tokens.RefreshToken,
		TokenType:    "Bearer",
		ExpiresAt:    tokens.AccessExpiresAt,
	}
}

func toUserResponse(user auth.User) userResponse {
	return userResponse{ID: user.ID, Login: user.Login, CreatedAt: user.CreatedAt}
}

func writeError(w http.ResponseWriter, status int, code, message string) {
	writeJSON(w, status, errorResponse{
		Error: apiError{Code: code, Message: message},
	})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
