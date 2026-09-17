package protoframe

import (
	"bytes"
	"encoding/binary"
	"errors"
	"testing"

	gamev1 "github.com/dprishchepa/2d-pvp-duel/backend/internal/proto/game/v1"
)

func TestWriteRead_RoundTripsMessage(t *testing.T) {
	t.Parallel()

	want := &gamev1.ClientEnvelope{
		Payload: &gamev1.ClientEnvelope_Authenticate{
			Authenticate: &gamev1.AuthenticateRequest{
				MatchToken: "signed-match-ticket",
				PlayerId:   "alice",
			},
		},
	}
	var buffer bytes.Buffer
	if err := Write(&buffer, want); err != nil {
		t.Fatalf("Write() error = %v", err)
	}

	var got gamev1.ClientEnvelope
	if err := Read(&buffer, &got); err != nil {
		t.Fatalf("Read() error = %v", err)
	}
	if got.GetAuthenticate().GetPlayerId() != "alice" {
		t.Errorf("player ID = %q, want alice", got.GetAuthenticate().GetPlayerId())
	}
	if got.GetAuthenticate().GetMatchToken() != "signed-match-ticket" {
		t.Errorf("match token = %q, want signed-match-ticket", got.GetAuthenticate().GetMatchToken())
	}
}

func TestRead_RejectsOversizedMessageBeforeAllocation(t *testing.T) {
	t.Parallel()

	var header [4]byte
	binary.BigEndian.PutUint32(header[:], MaxMessageSize+1)

	var message gamev1.ClientEnvelope
	err := Read(bytes.NewReader(header[:]), &message)
	if !errors.Is(err, ErrMessageTooLarge) {
		t.Errorf("Read() error = %v, want ErrMessageTooLarge", err)
	}
}
