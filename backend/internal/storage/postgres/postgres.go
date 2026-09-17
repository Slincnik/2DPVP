package postgres

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/auth"
	"github.com/jackc/pgx/v5/pgconn"
	_ "github.com/jackc/pgx/v5/stdlib"
)

type Store struct {
	db *sql.DB
}

func Open(ctx context.Context, databaseURL string) (*Store, error) {
	db, err := sql.Open("pgx", databaseURL)
	if err != nil {
		return nil, fmt.Errorf("open postgres: %w", err)
	}
	db.SetMaxOpenConns(20)
	db.SetMaxIdleConns(10)
	db.SetConnMaxLifetime(5 * time.Minute)
	db.SetConnMaxIdleTime(time.Minute)

	pingCtx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()
	if err := db.PingContext(pingCtx); err != nil {
		db.Close()
		return nil, fmt.Errorf("ping postgres: %w", err)
	}
	return &Store{db: db}, nil
}

func (s *Store) Close() error {
	return s.db.Close()
}

func (s *Store) CreateUser(ctx context.Context, user auth.User) error {
	_, err := s.db.ExecContext(
		ctx,
		`INSERT INTO users (id, login, password_hash, created_at) VALUES ($1, $2, $3, $4)`,
		user.ID,
		user.Login,
		user.PasswordHash,
		user.CreatedAt,
	)
	if isUniqueViolation(err) {
		return auth.ErrUserExists
	}
	if err != nil {
		return fmt.Errorf("insert user: %w", err)
	}
	return nil
}

func (s *Store) UserByID(ctx context.Context, id string) (auth.User, error) {
	return scanUser(s.db.QueryRowContext(
		ctx,
		`SELECT id, login, password_hash, created_at FROM users WHERE id = $1`,
		id,
	))
}

func (s *Store) UserByLogin(ctx context.Context, login string) (auth.User, error) {
	return scanUser(s.db.QueryRowContext(
		ctx,
		`SELECT id, login, password_hash, created_at FROM users WHERE login = $1`,
		login,
	))
}

func (s *Store) SaveRefreshToken(ctx context.Context, token auth.RefreshToken) error {
	_, err := s.db.ExecContext(
		ctx,
		`INSERT INTO refresh_tokens (token_hash, user_id, expires_at, created_at)
		 VALUES ($1, $2, $3, $4)`,
		token.Hash[:],
		token.UserID,
		token.ExpiresAt,
		token.CreatedAt,
	)
	if err != nil {
		return fmt.Errorf("insert refresh token: %w", err)
	}
	return nil
}

func (s *Store) RotateRefreshToken(
	ctx context.Context,
	oldHash [32]byte,
	replacement auth.RefreshToken,
) (string, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return "", fmt.Errorf("begin refresh rotation: %w", err)
	}
	defer tx.Rollback()

	var userID string
	var expiresAt time.Time
	err = tx.QueryRowContext(
		ctx,
		`DELETE FROM refresh_tokens WHERE token_hash = $1 RETURNING user_id, expires_at`,
		oldHash[:],
	).Scan(&userID, &expiresAt)
	if errors.Is(err, sql.ErrNoRows) {
		return "", auth.ErrRefreshTokenInvalid
	}
	if err != nil {
		return "", fmt.Errorf("consume refresh token: %w", err)
	}
	if !time.Now().Before(expiresAt) {
		if err := tx.Commit(); err != nil {
			return "", fmt.Errorf("delete expired refresh token: %w", err)
		}
		return "", auth.ErrRefreshTokenInvalid
	}

	_, err = tx.ExecContext(
		ctx,
		`INSERT INTO refresh_tokens (token_hash, user_id, expires_at, created_at)
		 VALUES ($1, $2, $3, $4)`,
		replacement.Hash[:],
		userID,
		replacement.ExpiresAt,
		replacement.CreatedAt,
	)
	if err != nil {
		return "", fmt.Errorf("insert rotated refresh token: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return "", fmt.Errorf("commit refresh rotation: %w", err)
	}
	return userID, nil
}

func (s *Store) RevokeRefreshToken(ctx context.Context, hash [32]byte) error {
	if _, err := s.db.ExecContext(
		ctx,
		`DELETE FROM refresh_tokens WHERE token_hash = $1`,
		hash[:],
	); err != nil {
		return fmt.Errorf("delete refresh token: %w", err)
	}
	return nil
}

type rowScanner interface {
	Scan(dest ...any) error
}

func scanUser(row rowScanner) (auth.User, error) {
	var user auth.User
	if err := row.Scan(&user.ID, &user.Login, &user.PasswordHash, &user.CreatedAt); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return auth.User{}, auth.ErrUserNotFound
		}
		return auth.User{}, fmt.Errorf("scan user: %w", err)
	}
	return user, nil
}

func isUniqueViolation(err error) bool {
	var postgresError *pgconn.PgError
	return errors.As(err, &postgresError) && postgresError.Code == "23505"
}
