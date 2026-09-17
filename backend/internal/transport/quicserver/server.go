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

const ALPN = "pvp-duel-v1"

type Session interface {
	SubmitInput(input *gamev1.PlayerInput) error
	Snapshots() <-chan *gamev1.WorldSnapshot
	Close()
}

type Authenticator interface {
	Authenticate(ctx context.Context, request *gamev1.AuthenticateRequest) (Session, error)
}

type Server struct {
	addr          string
	tlsConfig     *tls.Config
	authenticator Authenticator
}

func New(addr string, tlsConfig *tls.Config, authenticator Authenticator) *Server {
	config := tlsConfig.Clone()
	config.NextProtos = []string{ALPN}
	return &Server{
		addr:          addr,
		tlsConfig:     config,
		authenticator: authenticator,
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

	receiveDone := make(chan struct{})
	go func() {
		defer close(receiveDone)
		s.receiveInputs(connection, session)
	}()

	for {
		select {
		case <-connection.Context().Done():
			return
		case <-receiveDone:
			return
		case snapshot, open := <-session.Snapshots():
			if !open {
				_ = connection.CloseWithError(0, "session ended")
				return
			}
			response, err := proto.Marshal(snapshot)
			if err != nil {
				return
			}
			_ = connection.SendDatagram(response)
			if snapshot.GetStatus() == gamev1.MatchStatus_MATCH_STATUS_FINISHED {
				final := &gamev1.ServerEnvelope{
					Payload: &gamev1.ServerEnvelope_Snapshot{Snapshot: snapshot},
				}
				_ = protoframe.Write(stream, final)
				_ = connection.CloseWithError(0, "match finished")
				return
			}
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
