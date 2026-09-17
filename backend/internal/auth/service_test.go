package auth

import (
	"errors"
	"testing"
	"time"

	"golang.org/x/crypto/bcrypt"
)

func TestService_RegisterLoginAndAuthenticate(t *testing.T) {
	t.Parallel()

	service := newTestService(t)
	registered, tokens, err := service.Register(t.Context(), "  Alice_1 ", "correct horse battery")
	if err != nil {
		t.Fatalf("Register() error = %v", err)
	}
	if registered.Login != "alice_1" {
		t.Errorf("login = %q, want alice_1", registered.Login)
	}
	if string(registered.PasswordHash) == "correct horse battery" {
		t.Error("password was stored in plaintext")
	}
	if tokens.AccessToken == "" || tokens.RefreshToken == "" {
		t.Fatal("issued tokens are empty")
	}

	claims, err := service.AuthenticateAccess(tokens.AccessToken)
	if err != nil {
		t.Fatalf("AuthenticateAccess() error = %v", err)
	}
	if claims.Subject != registered.ID || claims.Login != registered.Login {
		t.Errorf("claims = (%q, %q), want (%q, %q)", claims.Subject, claims.Login, registered.ID, registered.Login)
	}

	loggedIn, _, err := service.Login(t.Context(), "ALICE_1", "correct horse battery")
	if err != nil {
		t.Fatalf("Login() error = %v", err)
	}
	if loggedIn.ID != registered.ID {
		t.Errorf("login user ID = %q, want %q", loggedIn.ID, registered.ID)
	}
}

func TestService_RejectsDuplicateAndInvalidCredentials(t *testing.T) {
	t.Parallel()

	service := newTestService(t)
	if _, _, err := service.Register(t.Context(), "alice", "correct horse battery"); err != nil {
		t.Fatalf("Register() error = %v", err)
	}
	if _, _, err := service.Register(t.Context(), "ALICE", "another secure password"); !errors.Is(err, ErrUserExists) {
		t.Errorf("duplicate Register() error = %v, want ErrUserExists", err)
	}
	if _, _, err := service.Login(t.Context(), "alice", "wrong password"); !errors.Is(err, ErrInvalidCredentials) {
		t.Errorf("Login() error = %v, want ErrInvalidCredentials", err)
	}
	if _, _, err := service.Login(t.Context(), "missing", "wrong password"); !errors.Is(err, ErrInvalidCredentials) {
		t.Errorf("missing Login() error = %v, want ErrInvalidCredentials", err)
	}
}

func TestService_RefreshRotatesTokenAndLogoutRevokesIt(t *testing.T) {
	t.Parallel()

	service := newTestService(t)
	user, original, err := service.Register(t.Context(), "alice", "correct horse battery")
	if err != nil {
		t.Fatalf("Register() error = %v", err)
	}

	refreshedUser, replacement, err := service.Refresh(t.Context(), original.RefreshToken)
	if err != nil {
		t.Fatalf("Refresh() error = %v", err)
	}
	if refreshedUser.ID != user.ID || replacement.RefreshToken == original.RefreshToken {
		t.Error("refresh did not rotate token for the same user")
	}
	if _, _, err := service.Refresh(t.Context(), original.RefreshToken); !errors.Is(err, ErrRefreshTokenInvalid) {
		t.Errorf("reused refresh error = %v, want ErrRefreshTokenInvalid", err)
	}
	if err := service.Logout(t.Context(), replacement.RefreshToken); err != nil {
		t.Fatalf("Logout() error = %v", err)
	}
	if _, _, err := service.Refresh(t.Context(), replacement.RefreshToken); !errors.Is(err, ErrRefreshTokenInvalid) {
		t.Errorf("revoked refresh error = %v, want ErrRefreshTokenInvalid", err)
	}
}

func TestService_ValidatesLoginAndPassword(t *testing.T) {
	t.Parallel()

	service := newTestService(t)
	if _, _, err := service.Register(t.Context(), "a!", "correct horse battery"); !errors.Is(err, ErrInvalidLogin) {
		t.Errorf("invalid login error = %v, want ErrInvalidLogin", err)
	}
	if _, _, err := service.Register(t.Context(), "alice", "short"); !errors.Is(err, ErrInvalidPassword) {
		t.Errorf("short password error = %v, want ErrInvalidPassword", err)
	}
}

func newTestService(t *testing.T) *Service {
	t.Helper()
	manager, err := NewTokenManager(
		[]byte("test-secret-that-is-at-least-32-bytes-long"),
		"test-issuer",
		"test-audience",
		15*time.Minute,
		24*time.Hour,
	)
	if err != nil {
		t.Fatalf("NewTokenManager() error = %v", err)
	}
	service, err := NewService(NewMemoryStore(), manager, bcrypt.MinCost)
	if err != nil {
		t.Fatalf("NewService() error = %v", err)
	}
	return service
}
