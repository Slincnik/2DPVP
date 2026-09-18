#include "net/quic_client.h"

#include "game/v1/duel.pb.h"
#include "protocol/match_protocol_adapter.h"

#include <msquic.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace duel::net {
namespace {

constexpr std::string_view kAlpn = "pvp-duel-v2";
constexpr std::uint32_t kMaxFrameSize = 64U << 10U;

struct SendContext {
    explicit SendContext(std::vector<std::uint8_t> value) : bytes(std::move(value)) {
        buffer.Buffer = bytes.data();
        buffer.Length = static_cast<std::uint32_t>(bytes.size());
    }

    std::vector<std::uint8_t> bytes;
    QUIC_BUFFER buffer{};
};

std::vector<std::uint8_t> Serialize(const google::protobuf::MessageLite& message) {
    const auto payloadSize = message.ByteSizeLong();
    if (payloadSize > kMaxFrameSize) {
        return {};
    }
    std::vector<std::uint8_t> payload(payloadSize);
    if (!message.SerializeToArray(payload.data(), static_cast<int>(payloadSize))) {
        return {};
    }
    return payload;
}

std::vector<std::uint8_t> Frame(const google::protobuf::MessageLite& message) {
    const auto payloadSize = message.ByteSizeLong();
    if (payloadSize > kMaxFrameSize) {
        return {};
    }

    std::vector<std::uint8_t> frame(4 + payloadSize);
    const auto size = static_cast<std::uint32_t>(payloadSize);
    frame[0] = static_cast<std::uint8_t>(size >> 24U);
    frame[1] = static_cast<std::uint8_t>(size >> 16U);
    frame[2] = static_cast<std::uint8_t>(size >> 8U);
    frame[3] = static_cast<std::uint8_t>(size);
    if (!message.SerializeToArray(frame.data() + 4, static_cast<int>(payloadSize))) {
        return {};
    }
    return frame;
}

} // namespace

class QuicClient::Impl {
public:
    ~Impl() { Close(); }

    bool Connect(
        const std::string& host,
        std::uint16_t port,
        const std::string& matchToken,
        const std::string& playerId
    ) {
        matchToken_ = matchToken;
        playerId_ = playerId;

        if (QUIC_FAILED(MsQuicOpen2(&api_))) {
            SetError("MsQuicOpen2 failed");
            return false;
        }

        const QUIC_REGISTRATION_CONFIG registrationConfig{
            "pvp-duel-client",
            QUIC_EXECUTION_PROFILE_LOW_LATENCY,
        };
        if (QUIC_FAILED(api_->RegistrationOpen(&registrationConfig, &registration_))) {
            SetError("RegistrationOpen failed");
            return false;
        }

        const QUIC_BUFFER alpn{
            static_cast<std::uint32_t>(kAlpn.size()),
            reinterpret_cast<std::uint8_t*>(const_cast<char*>(kAlpn.data())),
        };
        QUIC_SETTINGS settings{};
        settings.IdleTimeoutMs = 30'000;
        settings.IsSet.IdleTimeoutMs = TRUE;
        settings.DatagramReceiveEnabled = TRUE;
        settings.IsSet.DatagramReceiveEnabled = TRUE;
        if (QUIC_FAILED(api_->ConfigurationOpen(
                registration_, &alpn, 1, &settings, sizeof(settings), nullptr, &configuration_))) {
            SetError("ConfigurationOpen failed");
            return false;
        }

        QUIC_CREDENTIAL_CONFIG credentials{};
        credentials.Type = QUIC_CREDENTIAL_TYPE_NONE;
        credentials.Flags = static_cast<QUIC_CREDENTIAL_FLAGS>(
            QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION
        );
        if (QUIC_FAILED(api_->ConfigurationLoadCredential(configuration_, &credentials))) {
            SetError("ConfigurationLoadCredential failed");
            return false;
        }

        if (QUIC_FAILED(api_->ConnectionOpen(
                registration_, ConnectionCallback, this, &connection_))) {
            SetError("ConnectionOpen failed");
            return false;
        }
        if (QUIC_FAILED(api_->ConnectionStart(
                connection_, configuration_, QUIC_ADDRESS_FAMILY_UNSPEC, host.c_str(), port))) {
            SetError("ConnectionStart failed");
            return false;
        }

        std::unique_lock lock(mutex_);
        condition_.wait_for(lock, std::chrono::seconds(5), [this] {
            return connected_ || failed_;
        });
        if (!connected_ && !failed_) {
            error_ = "QUIC connection timed out";
            failed_ = true;
        }
        return connected_;
    }

    bool SendInput(const protocol::PlayerInput& input) {
        return SendDatagram(protocol::ToProtobuf(input));
    }

    std::optional<protocol::MatchStart> PollMatchStart() {
        std::scoped_lock lock(mutex_);
        if (!matchStart_) {
            return std::nullopt;
        }
        auto start = std::move(matchStart_);
        matchStart_.reset();
        return start;
    }

    std::optional<protocol::WorldSnapshot> PollSnapshot() {
        std::scoped_lock lock(mutex_);
        if (snapshots_.empty()) {
            return std::nullopt;
        }
        auto snapshot = std::move(snapshots_.back());
        snapshots_.clear();
        return snapshot;
    }

    std::optional<protocol::MatchEnd> PollMatchEnd() {
        std::scoped_lock lock(mutex_);
        if (!matchEnd_) {
            return std::nullopt;
        }
        auto end = std::move(matchEnd_);
        matchEnd_.reset();
        return end;
    }

    std::string Error() const {
        std::scoped_lock lock(mutex_);
        return error_;
    }

    bool IsConnected() const {
        std::scoped_lock lock(mutex_);
        return connected_ && !failed_;
    }

private:
    static QUIC_STATUS QUIC_API ConnectionCallback(
        HQUIC connection,
        void* context,
        QUIC_CONNECTION_EVENT* event
    ) {
        return static_cast<Impl*>(context)->OnConnectionEvent(connection, event);
    }

    static QUIC_STATUS QUIC_API StreamCallback(
        HQUIC stream,
        void* context,
        QUIC_STREAM_EVENT* event
    ) {
        return static_cast<Impl*>(context)->OnStreamEvent(stream, event);
    }

    QUIC_STATUS OnConnectionEvent(HQUIC, QUIC_CONNECTION_EVENT* event) {
        switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            if (!OpenStreamAndAuthenticate()) {
                api_->ConnectionShutdown(connection_, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 1);
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            ServerClosed("QUIC transport closed the connection");
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            ServerClosed("match server closed the connection");
            break;
        case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED: {
            std::scoped_lock lock(mutex_);
            datagramSendEnabled_ = event->DATAGRAM_STATE_CHANGED.SendEnabled == TRUE;
            UpdateConnected();
            break;
        }
        case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
            ReceiveDatagram(*event->DATAGRAM_RECEIVED.Buffer);
            break;
        case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
            if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(event->DATAGRAM_SEND_STATE_CHANGED.State)) {
                delete static_cast<SendContext*>(event->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
            std::scoped_lock lock(mutex_);
            shutdownComplete_ = true;
            connected_ = false;
            condition_.notify_all();
            break;
        }
        default:
            break;
        }
        return QUIC_STATUS_SUCCESS;
    }

    QUIC_STATUS OnStreamEvent(HQUIC, QUIC_STREAM_EVENT* event) {
        switch (event->Type) {
        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            delete static_cast<SendContext*>(event->SEND_COMPLETE.ClientContext);
            break;
        case QUIC_STREAM_EVENT_RECEIVE:
            Receive(event->RECEIVE.Buffers, event->RECEIVE.BufferCount);
            break;
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
            ServerClosed("match server closed the stream");
            break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
            std::scoped_lock lock(mutex_);
            streamShutdownComplete_ = true;
            condition_.notify_all();
            break;
        }
        default:
            break;
        }
        return QUIC_STATUS_SUCCESS;
    }

    bool OpenStreamAndAuthenticate() {
        if (QUIC_FAILED(api_->StreamOpen(
                connection_, QUIC_STREAM_OPEN_FLAG_NONE, StreamCallback, this, &stream_))) {
            SetError("StreamOpen failed");
            return false;
        }
        if (QUIC_FAILED(api_->StreamStart(stream_, QUIC_STREAM_START_FLAG_IMMEDIATE))) {
            SetError("StreamStart failed");
            return false;
        }

        ::game::v1::ClientEnvelope envelope;
        auto* authentication = envelope.mutable_authenticate();
        authentication->set_match_token(matchToken_);
        authentication->set_player_id(playerId_);
        if (!Send(envelope)) {
            return false;
        }

        return true;
    }

    bool SendDatagram(const google::protobuf::MessageLite& message) {
        {
            std::scoped_lock lock(mutex_);
            if (!connected_ || connection_ == nullptr || failed_) {
                return false;
            }
        }

        auto payload = Serialize(message);
        if (payload.empty()) {
            SetError("could not serialize protobuf datagram");
            return false;
        }
        auto* context = new SendContext(std::move(payload));
        const auto status = api_->DatagramSend(
            connection_, &context->buffer, 1, QUIC_SEND_FLAG_NONE, context);
        if (QUIC_FAILED(status)) {
            delete context;
            return false;
        }
        return true;
    }

    bool Send(const google::protobuf::MessageLite& message) {
        {
            std::scoped_lock lock(mutex_);
            if (stream_ == nullptr || failed_) {
                return false;
            }
        }

        auto frame = Frame(message);
        if (frame.empty()) {
            SetError("could not serialize protobuf frame");
            return false;
        }
        auto* context = new SendContext(std::move(frame));
        const auto status = api_->StreamSend(
            stream_, &context->buffer, 1, QUIC_SEND_FLAG_NONE, context);
        if (QUIC_FAILED(status)) {
            delete context;
            SetError("StreamSend failed");
            return false;
        }
        return true;
    }

    void Receive(const QUIC_BUFFER* buffers, std::uint32_t count) {
        std::scoped_lock lock(mutex_);
        for (std::uint32_t index = 0; index < count; ++index) {
            receiveBuffer_.insert(
                receiveBuffer_.end(),
                buffers[index].Buffer,
                buffers[index].Buffer + buffers[index].Length
            );
        }

        while (receiveBuffer_.size() >= 4) {
            const auto size =
                (static_cast<std::uint32_t>(receiveBuffer_[0]) << 24U) |
                (static_cast<std::uint32_t>(receiveBuffer_[1]) << 16U) |
                (static_cast<std::uint32_t>(receiveBuffer_[2]) << 8U) |
                static_cast<std::uint32_t>(receiveBuffer_[3]);
            if (size > kMaxFrameSize) {
                error_ = "server frame exceeds 64 KiB";
                failed_ = true;
                condition_.notify_all();
                return;
            }
            if (receiveBuffer_.size() < 4 + size) {
                return;
            }

            ::game::v1::ServerEnvelope envelope;
            if (!envelope.ParseFromArray(receiveBuffer_.data() + 4, static_cast<int>(size))) {
                error_ = "invalid protobuf response";
                failed_ = true;
                condition_.notify_all();
                return;
            }
            receiveBuffer_.erase(receiveBuffer_.begin(), receiveBuffer_.begin() + 4 + size);
            if (envelope.has_error()) {
                error_ = envelope.error().code() + ": " + envelope.error().message();
                failed_ = true;
                condition_.notify_all();
                continue;
            }
            if (envelope.has_ready()) {
                authenticated_ = envelope.ready().datagrams_enabled();
                UpdateConnected();
            }
            if (envelope.has_match_start()) {
                matchStart_ = protocol::FromProtobuf(envelope.match_start());
            }
            if (envelope.has_snapshot()) {
                snapshots_.push_back(protocol::FromProtobuf(envelope.snapshot()));
            }
            if (envelope.has_match_end()) {
                matchEnd_ = protocol::FromProtobuf(envelope.match_end());
                matchEnded_ = true;
            }
        }
    }

    void ReceiveDatagram(const QUIC_BUFFER& buffer) {
        if (buffer.Length > kMaxFrameSize) {
            return;
        }
        ::game::v1::WorldSnapshot snapshot;
        if (!snapshot.ParseFromArray(buffer.Buffer, static_cast<int>(buffer.Length))) {
            return;
        }
        std::scoped_lock lock(mutex_);
        snapshots_.push_back(protocol::FromProtobuf(snapshot));
    }

    void UpdateConnected() {
        connected_ = authenticated_ && datagramSendEnabled_ && !failed_;
        condition_.notify_all();
    }

    void ServerClosed(std::string message) {
        std::scoped_lock lock(mutex_);
        connected_ = false;
        if (!matchEnded_) {
            if (error_.empty()) {
                error_ = std::move(message);
            }
            failed_ = true;
        }
        condition_.notify_all();
    }

    void SetError(std::string message) {
        std::scoped_lock lock(mutex_);
        if (error_.empty()) {
            error_ = std::move(message);
        }
        failed_ = true;
        connected_ = false;
        condition_.notify_all();
    }

    void Close() {
        if (api_ != nullptr && connection_ != nullptr) {
            api_->ConnectionShutdown(connection_, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            std::unique_lock lock(mutex_);
            condition_.wait_for(lock, std::chrono::seconds(2), [this] {
                return shutdownComplete_;
            });
        }
        if (api_ != nullptr && stream_ != nullptr) {
            api_->StreamClose(stream_);
            stream_ = nullptr;
        }
        if (api_ != nullptr && connection_ != nullptr) {
            api_->ConnectionClose(connection_);
            connection_ = nullptr;
        }
        if (api_ != nullptr && configuration_ != nullptr) {
            api_->ConfigurationClose(configuration_);
            configuration_ = nullptr;
        }
        if (api_ != nullptr && registration_ != nullptr) {
            api_->RegistrationClose(registration_);
            registration_ = nullptr;
        }
        if (api_ != nullptr) {
            MsQuicClose(api_);
            api_ = nullptr;
        }
    }

    const QUIC_API_TABLE* api_ = nullptr;
    HQUIC registration_ = nullptr;
    HQUIC configuration_ = nullptr;
    HQUIC connection_ = nullptr;
    HQUIC stream_ = nullptr;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool connected_ = false;
    bool authenticated_ = false;
    bool datagramSendEnabled_ = false;
    bool failed_ = false;
    bool matchEnded_ = false;
    bool shutdownComplete_ = false;
    bool streamShutdownComplete_ = false;
    std::string error_;
    std::string matchToken_;
    std::string playerId_;
    std::vector<std::uint8_t> receiveBuffer_;
    std::optional<protocol::MatchStart> matchStart_;
    std::deque<protocol::WorldSnapshot> snapshots_;
    std::optional<protocol::MatchEnd> matchEnd_;
};

QuicClient::QuicClient() : impl_(std::make_unique<Impl>()) {}
QuicClient::~QuicClient() = default;

bool QuicClient::Connect(
    const std::string& host,
    std::uint16_t port,
    const std::string& matchToken,
    const std::string& playerId
) {
    return impl_->Connect(host, port, matchToken, playerId);
}

bool QuicClient::SendInput(const protocol::PlayerInput& input) {
    return impl_->SendInput(input);
}

std::optional<protocol::MatchStart> QuicClient::PollMatchStart() {
    return impl_->PollMatchStart();
}

std::optional<protocol::WorldSnapshot> QuicClient::PollSnapshot() {
    return impl_->PollSnapshot();
}

std::optional<protocol::MatchEnd> QuicClient::PollMatchEnd() {
    return impl_->PollMatchEnd();
}

std::string QuicClient::Error() const { return impl_->Error(); }
bool QuicClient::IsConnected() const { return impl_->IsConnected(); }

} // namespace duel::net
