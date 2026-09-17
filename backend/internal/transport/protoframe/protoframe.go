// Package protoframe reads and writes length-prefixed protobuf messages.
package protoframe

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"

	"google.golang.org/protobuf/proto"
)

const MaxMessageSize = 64 << 10

var ErrMessageTooLarge = errors.New("protobuf message exceeds maximum size")

func Read(r io.Reader, message proto.Message) error {
	var header [4]byte
	if _, err := io.ReadFull(r, header[:]); err != nil {
		return fmt.Errorf("read frame size: %w", err)
	}

	size := binary.BigEndian.Uint32(header[:])
	if size > MaxMessageSize {
		return fmt.Errorf("%w: %d bytes", ErrMessageTooLarge, size)
	}

	payload := make([]byte, size)
	if _, err := io.ReadFull(r, payload); err != nil {
		return fmt.Errorf("read frame payload: %w", err)
	}
	if err := proto.Unmarshal(payload, message); err != nil {
		return fmt.Errorf("unmarshal protobuf: %w", err)
	}
	return nil
}

func Write(w io.Writer, message proto.Message) error {
	payload, err := proto.Marshal(message)
	if err != nil {
		return fmt.Errorf("marshal protobuf: %w", err)
	}
	if len(payload) > MaxMessageSize {
		return fmt.Errorf("%w: %d bytes", ErrMessageTooLarge, len(payload))
	}

	var header [4]byte
	binary.BigEndian.PutUint32(header[:], uint32(len(payload)))
	if err := writeAll(w, header[:]); err != nil {
		return fmt.Errorf("write frame size: %w", err)
	}
	if err := writeAll(w, payload); err != nil {
		return fmt.Errorf("write frame payload: %w", err)
	}
	return nil
}

func writeAll(w io.Writer, payload []byte) error {
	for len(payload) > 0 {
		written, err := w.Write(payload)
		if err != nil {
			return err
		}
		if written == 0 {
			return io.ErrShortWrite
		}
		payload = payload[written:]
	}
	return nil
}
