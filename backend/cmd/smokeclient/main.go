// Command smokeclient verifies a two-player QUIC match end to end.
package main

import (
	"context"
	"crypto/tls"
	"fmt"
	"os"
	"time"

	"github.com/dprishchepa/2d-pvp-duel/backend/internal/matchticket"
	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/protoframe"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/quicserver"
	"github.com/google/uuid"
	"github.com/quic-go/quic-go"
	"google.golang.org/protobuf/proto"
)

type playerResult struct {
	playerID string
	snapshot *gamev1.WorldSnapshot
	err      error
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run() error {
	ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancel()

	playerA := envOrDefault("SMOKE_PLAYER_A_ID", "smoke-a")
	playerB := envOrDefault("SMOKE_PLAYER_B_ID", "smoke-b")
	aliceTicket := os.Getenv("SMOKE_PLAYER_A_TICKET")
	bobTicket := os.Getenv("SMOKE_PLAYER_B_TICKET")
	if aliceTicket == "" || bobTicket == "" {
		tickets, err := matchticket.NewManager(
			[]byte(envOrDefault("MATCH_TICKET_SECRET", "local-match-ticket-secret-change-me-123")),
			"pvp-duel-gateway",
			"pvp-duel-matchserver",
			time.Minute,
		)
		if err != nil {
			return fmt.Errorf("configure match tickets: %w", err)
		}
		matchID := uuid.NewString()
		aliceTicket, _, err = tickets.Issue(matchID, playerA, playerB)
		if err != nil {
			return fmt.Errorf("issue first smoke ticket: %w", err)
		}
		bobTicket, _, err = tickets.Issue(matchID, playerB, playerA)
		if err != nil {
			return fmt.Errorf("issue second smoke ticket: %w", err)
		}
	}

	results := make(chan playerResult, 2)
	go runPlayer(ctx, playerA, aliceTicket, 1, results)
	go runPlayer(ctx, playerB, bobTicket, -1, results)

	for range 2 {
		result := <-results
		if result.err != nil {
			return fmt.Errorf("%s: %w", result.playerID, result.err)
		}
		player := findPlayer(result.snapshot, result.playerID)
		if player == nil {
			return fmt.Errorf("%s: player missing from shared snapshot", result.playerID)
		}
		fmt.Printf(
			"QUIC OK: player=%s server_tick=%d x=%d ack=%d\n",
			result.playerID,
			result.snapshot.GetServerTick(),
			player.GetPositionX(),
			player.GetLastAckedInputTick(),
		)
	}
	return nil
}

func runPlayer(
	ctx context.Context,
	playerID string,
	ticket string,
	moveX int32,
	results chan<- playerResult,
) {
	snapshot, err := play(ctx, playerID, ticket, moveX)
	results <- playerResult{playerID: playerID, snapshot: snapshot, err: err}
}

func play(ctx context.Context, playerID, ticket string, moveX int32) (*gamev1.WorldSnapshot, error) {
	connection, err := quic.DialAddr(
		ctx,
		envOrDefault("MATCHSERVER_QUIC_ADDR", "localhost:4242"),
		&tls.Config{
			MinVersion:         tls.VersionTLS13,
			NextProtos:         []string{quicserver.ALPN},
			InsecureSkipVerify: true, // Development self-signed certificate only.
		},
		&quic.Config{EnableDatagrams: true},
	)
	if err != nil {
		return nil, fmt.Errorf("connect: %w", err)
	}
	defer connection.CloseWithError(0, "smoke test complete")

	stream, err := connection.OpenStreamSync(ctx)
	if err != nil {
		return nil, fmt.Errorf("open stream: %w", err)
	}
	defer stream.Close()

	if err := protoframe.Write(stream, &gamev1.ClientEnvelope{
		Payload: &gamev1.ClientEnvelope_Authenticate{
			Authenticate: &gamev1.AuthenticateRequest{
				MatchToken: ticket,
				PlayerId:   playerID,
			},
		},
	}); err != nil {
		return nil, fmt.Errorf("send authentication: %w", err)
	}

	var readyResponse gamev1.ServerEnvelope
	if err := protoframe.Read(stream, &readyResponse); err != nil {
		return nil, fmt.Errorf("receive match ready: %w", err)
	}
	if readyResponse.GetReady() == nil || !readyResponse.GetReady().GetDatagramsEnabled() {
		return nil, fmt.Errorf("server did not enable datagrams")
	}

	inputPayload, err := proto.Marshal(&gamev1.PlayerInput{Tick: 1, MoveX: moveX})
	if err != nil {
		return nil, fmt.Errorf("marshal input: %w", err)
	}
	if err := connection.SendDatagram(inputPayload); err != nil {
		return nil, fmt.Errorf("send input datagram: %w", err)
	}

	for {
		responsePayload, err := connection.ReceiveDatagram(ctx)
		if err != nil {
			return nil, fmt.Errorf("receive snapshot datagram: %w", err)
		}
		var snapshot gamev1.WorldSnapshot
		if err := proto.Unmarshal(responsePayload, &snapshot); err != nil {
			return nil, fmt.Errorf("decode snapshot datagram: %w", err)
		}
		player := findPlayer(&snapshot, playerID)
		if player != nil && player.GetLastAckedInputTick() >= 1 {
			return &snapshot, nil
		}
	}
}

func findPlayer(snapshot *gamev1.WorldSnapshot, playerID string) *gamev1.PlayerState {
	for _, player := range snapshot.GetPlayers() {
		if player.GetPlayerId() == playerID {
			return player
		}
	}
	return nil
}

func envOrDefault(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
