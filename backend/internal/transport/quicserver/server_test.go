package quicserver

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"sync"
	"testing"
	"time"

	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
	"github.com/dprishchepa/2d-pvp-duel/backend/internal/transport/protoframe"
	"github.com/quic-go/quic-go"
)

func TestServerReliableLifecycleOrderingAndGracefulTerminalDelivery(t *testing.T) {
	t.Parallel()

	serverTLS := testTLSConfig(t)
	session := &testSession{
		start: &gamev1.MatchStart{
			InitialSnapshot: &gamev1.WorldSnapshot{Status: gamev1.MatchStatus_MATCH_STATUS_COUNTDOWN},
			TickRate:        30,
		},
		snapshots: make(chan *gamev1.WorldSnapshot),
		matchEnds: make(chan *gamev1.MatchEnd, 1),
		started:   make(chan struct{}),
		closed:    make(chan struct{}),
	}
	server := New("", serverTLS, testAuthenticator{session: session})
	server.matchEndGracePeriod = 250 * time.Millisecond

	listener, err := quic.ListenAddr("127.0.0.1:0", server.tlsConfig, &quic.Config{EnableDatagrams: true})
	if err != nil {
		t.Fatalf("ListenAddr() error = %v", err)
	}
	defer listener.Close()

	ctx, cancel := context.WithTimeout(t.Context(), 3*time.Second)
	defer cancel()
	serverDone := make(chan struct{})
	go func() {
		defer close(serverDone)
		connection, acceptErr := listener.Accept(ctx)
		if acceptErr == nil {
			server.handleConnection(ctx, connection)
		}
	}()

	connection, err := quic.DialAddr(ctx, listener.Addr().String(), &tls.Config{
		MinVersion:         tls.VersionTLS13,
		NextProtos:         []string{ALPN},
		InsecureSkipVerify: true, // Ephemeral test certificate.
	}, &quic.Config{EnableDatagrams: true})
	if err != nil {
		t.Fatalf("DialAddr() error = %v", err)
	}
	defer connection.CloseWithError(0, "test complete")
	stream, err := connection.OpenStreamSync(ctx)
	if err != nil {
		t.Fatalf("OpenStreamSync() error = %v", err)
	}
	if err := protoframe.Write(stream, &gamev1.ClientEnvelope{
		Payload: &gamev1.ClientEnvelope_Authenticate{Authenticate: &gamev1.AuthenticateRequest{
			MatchToken: "ticket",
			PlayerId:   "alice",
		}},
	}); err != nil {
		t.Fatalf("write authenticate: %v", err)
	}

	ready := readServerEnvelope(t, stream)
	if ready.GetReady() == nil {
		t.Fatalf("first reliable response = %T, want MatchReady", ready.GetPayload())
	}
	start := readServerEnvelope(t, stream)
	if start.GetMatchStart() == nil {
		t.Fatalf("second reliable response = %T, want MatchStart", start.GetPayload())
	}
	select {
	case <-session.started:
	case <-time.After(time.Second):
		t.Fatal("session did not acknowledge successful MatchStart write")
	}

	session.matchEnds <- &gamev1.MatchEnd{
		FinalSnapshot:  &gamev1.WorldSnapshot{Status: gamev1.MatchStatus_MATCH_STATUS_FINISHED},
		WinnerPlayerId: "alice",
		Reason:         gamev1.MatchFinishReason_MATCH_FINISH_REASON_KO,
	}
	// Deliberately read well after publication, but within the grace period.
	time.Sleep(100 * time.Millisecond)
	select {
	case <-session.closed:
		t.Fatal("session closed before terminal grace period elapsed")
	default:
	}
	end := readServerEnvelope(t, stream).GetMatchEnd()
	if end == nil || end.GetWinnerPlayerId() != "alice" || end.GetReason() != gamev1.MatchFinishReason_MATCH_FINISH_REASON_KO {
		t.Fatalf("terminal response = %+v, want alice KO MatchEnd", end)
	}
	if end.GetFinalSnapshot().GetStatus() != gamev1.MatchStatus_MATCH_STATUS_FINISHED {
		t.Errorf("final status = %v, want finished", end.GetFinalSnapshot().GetStatus())
	}

	select {
	case <-connection.Context().Done():
	case <-time.After(time.Second):
		t.Fatal("server did not close connection after terminal grace period")
	}
	select {
	case <-session.closed:
	case <-time.After(time.Second):
		t.Fatal("server did not close session after terminal grace period")
	}
	<-serverDone
}

func readServerEnvelope(t *testing.T, stream *quic.Stream) *gamev1.ServerEnvelope {
	t.Helper()
	var envelope gamev1.ServerEnvelope
	if err := protoframe.Read(stream, &envelope); err != nil {
		t.Fatalf("read reliable response: %v", err)
	}
	return &envelope
}

type testAuthenticator struct {
	session Session
}

func (a testAuthenticator) Authenticate(context.Context, *gamev1.AuthenticateRequest) (Session, error) {
	return a.session, nil
}

type testSession struct {
	start     *gamev1.MatchStart
	snapshots chan *gamev1.WorldSnapshot
	matchEnds chan *gamev1.MatchEnd
	started   chan struct{}
	closed    chan struct{}
	startOnce sync.Once
	closeOnce sync.Once
}

func (s *testSession) SubmitInput(*gamev1.PlayerInput) error   { return nil }
func (s *testSession) MatchStart() *gamev1.MatchStart          { return s.start }
func (s *testSession) AcknowledgeMatchStart()                  { s.startOnce.Do(func() { close(s.started) }) }
func (s *testSession) Snapshots() <-chan *gamev1.WorldSnapshot { return s.snapshots }
func (s *testSession) MatchEnds() <-chan *gamev1.MatchEnd      { return s.matchEnds }
func (s *testSession) Close()                                  { s.closeOnce.Do(func() { close(s.closed) }) }

func testTLSConfig(t *testing.T) *tls.Config {
	t.Helper()
	privateKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("GenerateKey() error = %v", err)
	}
	now := time.Now()
	certificateDER, err := x509.CreateCertificate(rand.Reader, &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: "localhost"},
		NotBefore:    now.Add(-time.Minute),
		NotAfter:     now.Add(time.Minute),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		DNSNames:     []string{"localhost"},
	}, &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: "localhost"},
	}, &privateKey.PublicKey, privateKey)
	if err != nil {
		t.Fatalf("CreateCertificate() error = %v", err)
	}
	privateKeyDER, err := x509.MarshalECPrivateKey(privateKey)
	if err != nil {
		t.Fatalf("MarshalECPrivateKey() error = %v", err)
	}
	certificate, err := tls.X509KeyPair(
		pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certificateDER}),
		pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: privateKeyDER}),
	)
	if err != nil {
		t.Fatalf("X509KeyPair() error = %v", err)
	}
	return &tls.Config{MinVersion: tls.VersionTLS13, Certificates: []tls.Certificate{certificate}}
}
