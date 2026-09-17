package main

import (
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchserver"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/quicserver"
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

	certificate, err := tls.LoadX509KeyPair(
		envOrDefault("MATCHSERVER_TLS_CERT", "../deploy/certs/server.crt"),
		envOrDefault("MATCHSERVER_TLS_KEY", "../deploy/certs/server.key"),
	)
	if err != nil {
		return fmt.Errorf("load QUIC TLS certificate (run `make dev-cert`): %w", err)
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	errorsChannel := make(chan error, 2)
	go func() {
		errorsChannel <- serveHealth(ctx, envOrDefault("MATCHSERVER_HTTP_ADDR", ":8081"))
	}()
	ticketManager, err := matchticket.NewManager(
		[]byte(os.Getenv("MATCH_TICKET_SECRET")),
		"pvp-duel-gateway",
		"pvp-duel-matchserver",
		time.Minute,
	)
	if err != nil {
		return fmt.Errorf("configure match tickets: %w", err)
	}

	go func() {
		authenticator := matchserver.NewMatchManager(ticketManager)
		server := quicserver.New(
			envOrDefault("MATCHSERVER_QUIC_ADDR", ":4242"),
			&tls.Config{
				Certificates: []tls.Certificate{certificate},
				MinVersion:   tls.VersionTLS13,
			},
			authenticator,
		)
		errorsChannel <- server.ListenAndServe(ctx)
	}()

	err = <-errorsChannel
	cancel()
	if err != nil && !errors.Is(err, context.Canceled) {
		return err
	}
	return nil
}

func serveHealth(ctx context.Context, addr string) error {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		_, _ = w.Write([]byte(`{"status":"ok","transport":"quic-stream"}`))
	})

	server := &http.Server{
		Addr:              addr,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}
	serverErrors := make(chan error, 1)
	go func() {
		serverErrors <- server.ListenAndServe()
	}()

	select {
	case err := <-serverErrors:
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return fmt.Errorf("serve health endpoint: %w", err)
	case <-ctx.Done():
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := server.Shutdown(shutdownCtx); err != nil {
			return fmt.Errorf("shutdown health endpoint: %w", err)
		}
		return nil
	}
}

func envOrDefault(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
