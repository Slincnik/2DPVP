package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"fmt"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"github.com/google/uuid"
)

var ErrAccessTokenInvalid = errors.New("auth: access token invalid")

type AccessClaims struct {
	Login string `json:"login"`
	jwt.RegisteredClaims
}

type TokenManager struct {
	secret     []byte
	issuer     string
	audience   string
	accessTTL  time.Duration
	refreshTTL time.Duration
	now        func() time.Time
}

func NewTokenManager(
	secret []byte,
	issuer string,
	audience string,
	accessTTL time.Duration,
	refreshTTL time.Duration,
) (*TokenManager, error) {
	if len(secret) < 32 {
		return nil, errors.New("auth: JWT secret must contain at least 32 bytes")
	}
	if issuer == "" || audience == "" || accessTTL <= 0 || refreshTTL <= 0 {
		return nil, errors.New("auth: invalid token configuration")
	}
	return &TokenManager{
		secret:     append([]byte(nil), secret...),
		issuer:     issuer,
		audience:   audience,
		accessTTL:  accessTTL,
		refreshTTL: refreshTTL,
		now:        time.Now,
	}, nil
}

func (m *TokenManager) IssueAccess(user User) (string, time.Time, error) {
	now := m.now().UTC()
	expiresAt := now.Add(m.accessTTL)
	claims := AccessClaims{
		Login: user.Login,
		RegisteredClaims: jwt.RegisteredClaims{
			Issuer:    m.issuer,
			Subject:   user.ID,
			Audience:  jwt.ClaimStrings{m.audience},
			ExpiresAt: jwt.NewNumericDate(expiresAt),
			NotBefore: jwt.NewNumericDate(now),
			IssuedAt:  jwt.NewNumericDate(now),
			ID:        uuid.NewString(),
		},
	}
	signed, err := jwt.NewWithClaims(jwt.SigningMethodHS256, claims).SignedString(m.secret)
	if err != nil {
		return "", time.Time{}, fmt.Errorf("sign access token: %w", err)
	}
	return signed, expiresAt, nil
}

func (m *TokenManager) ParseAccess(raw string) (AccessClaims, error) {
	claims := AccessClaims{}
	token, err := jwt.ParseWithClaims(
		raw,
		&claims,
		func(token *jwt.Token) (any, error) {
			if token.Method != jwt.SigningMethodHS256 {
				return nil, ErrAccessTokenInvalid
			}
			return m.secret, nil
		},
		jwt.WithIssuer(m.issuer),
		jwt.WithAudience(m.audience),
		jwt.WithExpirationRequired(),
		jwt.WithValidMethods([]string{jwt.SigningMethodHS256.Alg()}),
		jwt.WithTimeFunc(m.now),
	)
	if err != nil || !token.Valid || claims.Subject == "" || claims.Login == "" {
		return AccessClaims{}, ErrAccessTokenInvalid
	}
	return claims, nil
}

func (m *TokenManager) NewRefresh(userID string) (string, RefreshToken, error) {
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		return "", RefreshToken{}, fmt.Errorf("generate refresh token: %w", err)
	}
	raw := base64.RawURLEncoding.EncodeToString(bytes)
	now := m.now().UTC()
	return raw, RefreshToken{
		Hash:      sha256.Sum256([]byte(raw)),
		UserID:    userID,
		ExpiresAt: now.Add(m.refreshTTL),
		CreatedAt: now,
	}, nil
}

func refreshHash(raw string) [32]byte {
	return sha256.Sum256([]byte(raw))
}
