package postgres

import (
	"context"
	"database/sql"
	"errors"
	"fmt"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/auth"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/profile"
)

func (s *Store) RecordMatchResult(ctx context.Context, result profile.MatchResult) (bool, error) {
	outcome, err := s.db.ExecContext(
		ctx,
		`INSERT INTO match_results (
            match_id, player_a_id, player_b_id, winner_player_id, finish_reason, ended_at
        ) VALUES ($1, $2, $3, NULLIF($4, '')::uuid, $5, $6)
        ON CONFLICT (match_id) DO NOTHING`,
		result.MatchID,
		result.PlayerAID,
		result.PlayerBID,
		result.WinnerID,
		result.Reason,
		result.EndedAt,
	)
	if err != nil {
		return false, fmt.Errorf("insert match result: %w", err)
	}
	changed, err := outcome.RowsAffected()
	if err != nil {
		return false, fmt.Errorf("match result rows affected: %w", err)
	}
	return changed == 1, nil
}

func (s *Store) ProfileByUserID(ctx context.Context, userID string) (profile.Profile, error) {
	var result profile.Profile
	err := s.db.QueryRowContext(
		ctx,
		`SELECT u.id, u.login, u.created_at,
            COUNT(m.match_id) AS played,
            COUNT(m.match_id) FILTER (WHERE m.winner_player_id = u.id) AS wins,
            COUNT(m.match_id) FILTER (WHERE m.winner_player_id IS NULL) AS draws,
            COUNT(m.match_id) FILTER (
                WHERE m.winner_player_id IS NOT NULL AND m.winner_player_id <> u.id
            ) AS losses
        FROM users u
        LEFT JOIN match_results m ON m.player_a_id = u.id OR m.player_b_id = u.id
        WHERE u.id = $1
        GROUP BY u.id, u.login, u.created_at`,
		userID,
	).Scan(
		&result.UserID,
		&result.Login,
		&result.CreatedAt,
		&result.Statistics.Played,
		&result.Statistics.Wins,
		&result.Statistics.Draws,
		&result.Statistics.Losses,
	)
	if errors.Is(err, sql.ErrNoRows) {
		return profile.Profile{}, auth.ErrUserNotFound
	}
	if err != nil {
		return profile.Profile{}, fmt.Errorf("query profile: %w", err)
	}
	return result, nil
}

var _ profile.Store = (*Store)(nil)
