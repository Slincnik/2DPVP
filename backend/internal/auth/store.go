package auth

import (
	"context"
	"errors"
	"sync"
	"time"
)

var (
	ErrUserExists          = errors.New("auth: user already exists")
	ErrUserNotFound        = errors.New("auth: user not found")
	ErrRefreshTokenInvalid = errors.New("auth: refresh token invalid")
)

type User struct {
	ID           string
	Login        string
	PasswordHash []byte
	CreatedAt    time.Time
}

type RefreshToken struct {
	Hash      [32]byte
	UserID    string
	ExpiresAt time.Time
	CreatedAt time.Time
}

type Store interface {
	CreateUser(ctx context.Context, user User) error
	UserByID(ctx context.Context, id string) (User, error)
	UserByLogin(ctx context.Context, login string) (User, error)
	SaveRefreshToken(ctx context.Context, token RefreshToken) error
	RotateRefreshToken(ctx context.Context, oldHash [32]byte, replacement RefreshToken) (string, error)
	RevokeRefreshToken(ctx context.Context, hash [32]byte) error
}

// MemoryStore is concurrency-safe and intended for tests and single-process
// development. The Store contract allows replacing it with Postgres unchanged.
type MemoryStore struct {
	mutex         sync.RWMutex
	usersByID     map[string]User
	userIDByLogin map[string]string
	refreshTokens map[[32]byte]RefreshToken
}

func NewMemoryStore() *MemoryStore {
	return &MemoryStore{
		usersByID:     make(map[string]User),
		userIDByLogin: make(map[string]string),
		refreshTokens: make(map[[32]byte]RefreshToken),
	}
}

func (s *MemoryStore) CreateUser(_ context.Context, user User) error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	if _, exists := s.userIDByLogin[user.Login]; exists {
		return ErrUserExists
	}
	s.usersByID[user.ID] = cloneUser(user)
	s.userIDByLogin[user.Login] = user.ID
	return nil
}

func (s *MemoryStore) UserByID(_ context.Context, id string) (User, error) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	user, exists := s.usersByID[id]
	if !exists {
		return User{}, ErrUserNotFound
	}
	return cloneUser(user), nil
}

func (s *MemoryStore) UserByLogin(_ context.Context, login string) (User, error) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	id, exists := s.userIDByLogin[login]
	if !exists {
		return User{}, ErrUserNotFound
	}
	return cloneUser(s.usersByID[id]), nil
}

func (s *MemoryStore) SaveRefreshToken(_ context.Context, token RefreshToken) error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	s.refreshTokens[token.Hash] = token
	return nil
}

func (s *MemoryStore) RotateRefreshToken(
	_ context.Context,
	oldHash [32]byte,
	replacement RefreshToken,
) (string, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	old, exists := s.refreshTokens[oldHash]
	if !exists || !time.Now().Before(old.ExpiresAt) {
		delete(s.refreshTokens, oldHash)
		return "", ErrRefreshTokenInvalid
	}
	delete(s.refreshTokens, oldHash)
	replacement.UserID = old.UserID
	s.refreshTokens[replacement.Hash] = replacement
	return old.UserID, nil
}

func (s *MemoryStore) RevokeRefreshToken(_ context.Context, hash [32]byte) error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	delete(s.refreshTokens, hash)
	return nil
}

func cloneUser(user User) User {
	user.PasswordHash = append([]byte(nil), user.PasswordHash...)
	return user
}
