package auth

import (
	"context"
	"errors"
	"fmt"
	"regexp"
	"strings"
	"time"
	"unicode/utf8"

	"github.com/google/uuid"
	"golang.org/x/crypto/bcrypt"
)

var (
	ErrInvalidCredentials = errors.New("auth: invalid credentials")
	ErrInvalidLogin       = errors.New("auth: invalid login")
	ErrInvalidPassword    = errors.New("auth: invalid password")
)

var loginPattern = regexp.MustCompile(`^[a-z0-9_]{3,32}$`)

type Tokens struct {
	AccessToken      string
	RefreshToken     string
	AccessExpiresAt  time.Time
	RefreshExpiresAt time.Time
}

type Service struct {
	store      Store
	tokens     *TokenManager
	bcryptCost int
	dummyHash  []byte
	now        func() time.Time
}

func NewService(store Store, tokens *TokenManager, bcryptCost int) (*Service, error) {
	if store == nil || tokens == nil {
		return nil, errors.New("auth: store and token manager are required")
	}
	if bcryptCost < bcrypt.MinCost || bcryptCost > bcrypt.MaxCost {
		return nil, errors.New("auth: invalid bcrypt cost")
	}
	dummyHash, err := bcrypt.GenerateFromPassword([]byte("invalid-password-placeholder"), bcryptCost)
	if err != nil {
		return nil, fmt.Errorf("create dummy password hash: %w", err)
	}
	return &Service{
		store:      store,
		tokens:     tokens,
		bcryptCost: bcryptCost,
		dummyHash:  dummyHash,
		now:        time.Now,
	}, nil
}

func (s *Service) Register(ctx context.Context, login, password string) (User, Tokens, error) {
	login = normalizeLogin(login)
	if !loginPattern.MatchString(login) {
		return User{}, Tokens{}, ErrInvalidLogin
	}
	if err := validatePassword(password); err != nil {
		return User{}, Tokens{}, err
	}

	passwordHash, err := bcrypt.GenerateFromPassword([]byte(password), s.bcryptCost)
	if err != nil {
		return User{}, Tokens{}, fmt.Errorf("hash password: %w", err)
	}
	user := User{
		ID:           uuid.NewString(),
		Login:        login,
		PasswordHash: passwordHash,
		CreatedAt:    s.now().UTC(),
	}
	if err := s.store.CreateUser(ctx, user); err != nil {
		return User{}, Tokens{}, fmt.Errorf("create user: %w", err)
	}
	tokens, err := s.issueTokens(ctx, user)
	if err != nil {
		return User{}, Tokens{}, err
	}
	return user, tokens, nil
}

func (s *Service) Login(ctx context.Context, login, password string) (User, Tokens, error) {
	user, err := s.store.UserByLogin(ctx, normalizeLogin(login))
	if err != nil {
		if errors.Is(err, ErrUserNotFound) {
			// Equalize the expensive path to reduce login enumeration timing leaks.
			_ = bcrypt.CompareHashAndPassword(s.dummyHash, []byte(password))
			return User{}, Tokens{}, ErrInvalidCredentials
		}
		return User{}, Tokens{}, fmt.Errorf("get user by login: %w", err)
	}
	if err := bcrypt.CompareHashAndPassword(user.PasswordHash, []byte(password)); err != nil {
		return User{}, Tokens{}, ErrInvalidCredentials
	}
	tokens, err := s.issueTokens(ctx, user)
	if err != nil {
		return User{}, Tokens{}, err
	}
	return user, tokens, nil
}

func (s *Service) Refresh(ctx context.Context, rawRefreshToken string) (User, Tokens, error) {
	if rawRefreshToken == "" {
		return User{}, Tokens{}, ErrRefreshTokenInvalid
	}

	newRaw, replacement, err := s.tokens.NewRefresh("")
	if err != nil {
		return User{}, Tokens{}, err
	}
	userID, err := s.store.RotateRefreshToken(ctx, refreshHash(rawRefreshToken), replacement)
	if err != nil {
		if errors.Is(err, ErrRefreshTokenInvalid) {
			return User{}, Tokens{}, ErrRefreshTokenInvalid
		}
		return User{}, Tokens{}, fmt.Errorf("rotate refresh token: %w", err)
	}
	user, err := s.store.UserByID(ctx, userID)
	if err != nil {
		return User{}, Tokens{}, fmt.Errorf("get refresh token user: %w", err)
	}
	access, accessExpiry, err := s.tokens.IssueAccess(user)
	if err != nil {
		return User{}, Tokens{}, err
	}
	return user, Tokens{
		AccessToken:      access,
		RefreshToken:     newRaw,
		AccessExpiresAt:  accessExpiry,
		RefreshExpiresAt: replacement.ExpiresAt,
	}, nil
}

func (s *Service) Logout(ctx context.Context, rawRefreshToken string) error {
	if rawRefreshToken == "" {
		return nil
	}
	if err := s.store.RevokeRefreshToken(ctx, refreshHash(rawRefreshToken)); err != nil {
		return fmt.Errorf("revoke refresh token: %w", err)
	}
	return nil
}

func (s *Service) AuthenticateAccess(rawAccessToken string) (AccessClaims, error) {
	return s.tokens.ParseAccess(rawAccessToken)
}

func (s *Service) UserByID(ctx context.Context, id string) (User, error) {
	return s.store.UserByID(ctx, id)
}

func (s *Service) issueTokens(ctx context.Context, user User) (Tokens, error) {
	access, accessExpiry, err := s.tokens.IssueAccess(user)
	if err != nil {
		return Tokens{}, err
	}
	rawRefresh, refresh, err := s.tokens.NewRefresh(user.ID)
	if err != nil {
		return Tokens{}, err
	}
	if err := s.store.SaveRefreshToken(ctx, refresh); err != nil {
		return Tokens{}, fmt.Errorf("save refresh token: %w", err)
	}
	return Tokens{
		AccessToken:      access,
		RefreshToken:     rawRefresh,
		AccessExpiresAt:  accessExpiry,
		RefreshExpiresAt: refresh.ExpiresAt,
	}, nil
}

func normalizeLogin(login string) string {
	return strings.ToLower(strings.TrimSpace(login))
}

func validatePassword(password string) error {
	byteLength := len([]byte(password))
	if !utf8.ValidString(password) || byteLength < 10 || byteLength > 72 {
		return ErrInvalidPassword
	}
	return nil
}
