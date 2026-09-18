// Package quicserver exposes the reliable-stream transport used by phase 2.
package quicserver

import (
	"context"
	"crypto/tls"
	"fmt"
	"io"
	"time"

	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/protoframe"
	"github.com/quic-go/quic-go"
	"google.golang.org/protobuf/proto"
)

const (
	ALPN                = "pvp-duel-v2"
	MatchEndGracePeriod = 2 * time.Second
)

type Session interface {
	SubmitInput(input *gamev1.PlayerInput) error
	MatchStart() *gamev1.MatchStart
	AcknowledgeMatchStart()
	Snapshots() <-chan *gamev1.WorldSnapshot
	MatchEnds() <-chan *gamev1.MatchEnd
	Close()
}

type Authenticator interface {
	Authenticate(ctx context.Context, request *gamev1.AuthenticateRequest) (Session, error)
}

type Server struct {
	addr                string
	tlsConfig           *tls.Config
	authenticator       Authenticator
	matchEndGracePeriod time.Duration
}

func New(addr string, tlsConfig *tls.Config, authenticator Authenticator) *Server {
	config := tlsConfig.Clone()
	config.NextProtos = []string{ALPN}
	return &Server{
		addr:                addr,
		tlsConfig:           config,
		authenticator:       authenticator,
		matchEndGracePeriod: MatchEndGracePeriod,
	}
}

func (s *Server) ListenAndServe(ctx context.Context) error {
	listener, err := quic.ListenAddr(s.addr, s.tlsConfig, &quic.Config{
		MaxIdleTimeout:  30 * time.Second,
		EnableDatagrams: true,
	})
	if err != nil {
		return fmt.Errorf("listen QUIC: %w", err)
	}
	defer listener.Close()

	for {
		connection, err := listener.Accept(ctx)
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			return fmt.Errorf("accept QUIC connection: %w", err)
		}
		go s.handleConnection(ctx, connection)
	}
}

func (s *Server) handleConnection(ctx context.Context, connection *quic.Conn) {
	stream, err := connection.AcceptStream(ctx)
	if err != nil {
		return
	}
	defer stream.Close()

	var envelope gamev1.ClientEnvelope
	if err := protoframe.Read(stream, &envelope); err != nil {
		s.writeProtocolError(stream, "INVALID_FRAME", "invalid authentication frame")
		return
	}
	authentication := envelope.GetAuthenticate()
	if authentication == nil {
		s.writeProtocolError(stream, "AUTH_REQUIRED", "first message must authenticate")
		return
	}

	session, err := s.authenticator.Authenticate(connection.Context(), authentication)
	if err != nil {
		s.writeProtocolError(stream, "AUTH_FAILED", "authentication failed")
		return
	}

	defer session.Close()

	ready := &gamev1.ServerEnvelope{
		Payload: &gamev1.ServerEnvelope_Ready{
			Ready: &gamev1.MatchReady{DatagramsEnabled: true, TickRate: 30},
		},
	}
	if err := protoframe.Write(stream, ready); err != nil {
		return
	}
	start := &gamev1.ServerEnvelope{
		Payload: &gamev1.ServerEnvelope_MatchStart{MatchStart: session.MatchStart()},
	}
	if err := protoframe.Write(stream, start); err != nil {
		return
	}
	// The room clock is gated until both handlers have successfully written
	// MatchStart, so neither client loses countdown ticks to transport setup.
	session.AcknowledgeMatchStart()

	receiveDone := make(chan struct{})
	go func() {
		defer close(receiveDone)
		s.receiveInputs(connection, session)
	}()

	snapshots := session.Snapshots()
	matchEnds := session.MatchEnds()
	for {
		select {
		case <-connection.Context().Done():
			return
		case <-receiveDone:
			return
		case snapshot, open := <-snapshots:
			if !open {
				snapshots = nil
				continue
			}
			response, err := proto.Marshal(snapshot)
			if err != nil {
				return
			}
			_ = connection.SendDatagram(response)
		case matchEnd, open := <-matchEnds:
			if !open {
				return
			}
			final := &gamev1.ServerEnvelope{
				Payload: &gamev1.ServerEnvelope_MatchEnd{MatchEnd: matchEnd},
			}
			if err := protoframe.Write(stream, final); err != nil {
				return
			}
			// Keep the stream and session alive briefly so the reliable terminal
			// event can be acknowledged before a graceful server close.
			timer := time.NewTimer(s.matchEndGracePeriod)
			select {
			case <-timer.C:
			case <-connection.Context().Done():
				if !timer.Stop() {
					<-timer.C
				}
				return
			}
			_ = connection.CloseWithError(0, "match finished")
			return
		}
	}
}

func (s *Server) receiveInputs(connection *quic.Conn, session Session) {
	for {
		payload, err := connection.ReceiveDatagram(connection.Context())
		if err != nil {
			return
		}
		if len(payload) > protoframe.MaxMessageSize {
			continue
		}

		input := &gamev1.PlayerInput{}
		if err := proto.Unmarshal(payload, input); err != nil {
			continue
		}
		if err := session.SubmitInput(input); err != nil {
			return
		}
	}
}

func (s *Server) writeProtocolError(stream io.Writer, code, message string) {
	response := &gamev1.ServerEnvelope{
		Payload: &gamev1.ServerEnvelope_Error{
			Error: &gamev1.ProtocolError{Code: code, Message: message},
		},
	}
	_ = protoframe.Write(stream, response)
}
