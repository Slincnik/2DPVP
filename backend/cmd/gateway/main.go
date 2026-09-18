package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/auth"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/gateway"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchmaking"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/profile"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/storage/postgres"
	"golang.org/x/crypto/bcrypt"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run() error {
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	secret := os.Getenv("JWT_SECRET")
	if len(secret) < 32 {
		return errors.New("JWT_SECRET must contain at least 32 bytes")
	}
	tokenManager, err := auth.NewTokenManager(
		[]byte(secret),
		envOrDefault("JWT_ISSUER", "pvp-duel-gateway"),
		envOrDefault("JWT_AUDIENCE", "pvp-duel-client"),
		15*time.Minute,
		30*24*time.Hour,
	)
	if err != nil {
		return fmt.Errorf("configure tokens: %w", err)
	}
	databaseURL := os.Getenv("DATABASE_URL")
	if databaseURL == "" {
		return errors.New("DATABASE_URL is required")
	}
	store, err := postgres.Open(ctx, databaseURL)
	if err != nil {
		return err
	}
	defer store.Close()

	authService, err := auth.NewService(store, tokenManager, bcrypt.DefaultCost)
	if err != nil {
		return fmt.Errorf("configure auth: %w", err)
	}
	ticketManager, err := matchticket.NewManager(
		[]byte(os.Getenv("MATCH_TICKET_SECRET")),
		"pvp-duel-gateway",
		"pvp-duel-matchserver",
		time.Minute,
	)
	if err != nil {
		return fmt.Errorf("configure match tickets: %w", err)
	}
	profileService, err := profile.NewService(store)
	if err != nil {
		return fmt.Errorf("configure profile: %w", err)
	}
	internalResultSecret := os.Getenv("INTERNAL_MATCH_RESULT_SECRET")
	if len(internalResultSecret) < 32 {
		return errors.New("INTERNAL_MATCH_RESULT_SECRET must contain at least 32 bytes")
	}

	queue, err := matchmaking.New(
		ticketManager,
		envOrDefault("MATCH_SERVER_PUBLIC_ADDR", "localhost:4242"),
		5*time.Minute,
	)
	if err != nil {
		return fmt.Errorf("configure matchmaking: %w", err)
	}

	server := gateway.New(
		envOrDefault("GATEWAY_ADDR", ":8080"), authService, queue,
		profileService, []byte(internalResultSecret),
	)
	if err := server.ListenAndServe(ctx); err != nil && !errors.Is(err, context.Canceled) {
		return err
	}
	return nil
}

func envOrDefault(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
