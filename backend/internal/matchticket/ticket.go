// Package matchticket signs and validates short-lived credentials for QUIC matches.
package matchticket

import (
	"errors"
	"fmt"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"github.com/google/uuid"
)

var ErrInvalid = errors.New("match ticket: invalid")

type Claims struct {
	MatchID    string `json:"matchId"`
	OpponentID string `json:"opponentId"`
	jwt.RegisteredClaims
}

func (c Claims) PlayerID() string {
	return c.Subject
}

type Manager struct {
	secret   []byte
	issuer   string
	audience string
	ttl      time.Duration
	now      func() time.Time
}

func NewManager(
	secret []byte,
	issuer string,
	audience string,
	ttl time.Duration,
) (*Manager, error) {
	if len(secret) < 32 {
		return nil, errors.New("match ticket: secret must contain at least 32 bytes")
	}
	if issuer == "" || audience == "" || ttl <= 0 {
		return nil, errors.New("match ticket: invalid configuration")
	}
	return &Manager{
		secret:   append([]byte(nil), secret...),
		issuer:   issuer,
		audience: audience,
		ttl:      ttl,
		now:      time.Now,
	}, nil
}

func (m *Manager) Issue(matchID, playerID, opponentID string) (string, time.Time, error) {
	if matchID == "" || playerID == "" || opponentID == "" || playerID == opponentID {
		return "", time.Time{}, ErrInvalid
	}
	now := m.now().UTC()
	expiresAt := now.Add(m.ttl)
	claims := Claims{
		MatchID:    matchID,
		OpponentID: opponentID,
		RegisteredClaims: jwt.RegisteredClaims{
			Issuer:    m.issuer,
			Subject:   playerID,
			Audience:  jwt.ClaimStrings{m.audience},
			ExpiresAt: jwt.NewNumericDate(expiresAt),
			NotBefore: jwt.NewNumericDate(now),
			IssuedAt:  jwt.NewNumericDate(now),
			ID:        uuid.NewString(),
		},
	}
	raw, err := jwt.NewWithClaims(jwt.SigningMethodHS256, claims).SignedString(m.secret)
	if err != nil {
		return "", time.Time{}, fmt.Errorf("sign match ticket: %w", err)
	}
	return raw, expiresAt, nil
}

func (m *Manager) Verify(raw string) (Claims, error) {
	claims := Claims{}
	token, err := jwt.ParseWithClaims(
		raw,
		&claims,
		func(token *jwt.Token) (any, error) {
			if token.Method != jwt.SigningMethodHS256 {
				return nil, ErrInvalid
			}
			return m.secret, nil
		},
		jwt.WithIssuer(m.issuer),
		jwt.WithAudience(m.audience),
		jwt.WithExpirationRequired(),
		jwt.WithValidMethods([]string{jwt.SigningMethodHS256.Alg()}),
		jwt.WithTimeFunc(m.now),
	)
	if err != nil || !token.Valid || claims.MatchID == "" || claims.PlayerID() == "" ||
		claims.OpponentID == "" || claims.ID == "" {
		return Claims{}, ErrInvalid
	}
	return claims, nil
}
