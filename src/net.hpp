#pragma once

#include "common.hpp"

#include <mstcpip.h>

namespace screenfuse {

constexpr uint32_t kMagic = 0x53554653;
constexpr uint16_t kProtocolVersion = 1;
constexpr uint32_t kMaxPayloadBytes = 64u * 1024u * 1024u;
constexpr uint16_t kDefaultPort = 20787;

enum class Msg : uint16_t {
    Hello = 1,
    HelloAck = 2,
    Auth = 3,
    AuthOk = 4,
    AuthFail = 5,
    Ping = 6,
    Pong = 7,
    CaptureReq = 8,
    CaptureResp = 9,
    Shutdown = 10,
    Bye = 11,
    Error = 12,
    StreamStart = 13,
    StreamStop = 14,
    StreamStatus = 15,
    StreamFrame = 16
};

enum class WireFormat : uint8_t {
    RawBgra = 0,
    Png = 1,
    Jpeg = 2
};

enum class CaptureMethod : uint8_t {
    DesktopDuplication = 0,
    Gdi = 1
};

enum CaptureStatus : uint32_t {
    kCaptureOk = 0,
    kCaptureNoMonitor = 1,
    kCaptureFailed = 2,
    kCaptureEncodeFailed = 3
};

enum StreamStatusCode : uint32_t {
    kStreamStarted = 0,
    kStreamStopped = 1,
    kStreamFailed = 2
};

constexpr uint8_t kStreamFlagHeartbeat = 1 << 0;

#pragma pack(push, 1)

struct MsgHeader {
    uint32_t magic;
    uint16_t type;
    uint16_t version;
    uint32_t length;
};

struct HelloPayload {
    uint16_t protocolVersion;
    uint16_t reserved;
};

struct HelloAckPayload {
    uint16_t protocolVersion;
    uint16_t reserved;
    uint8_t nonce[kNonceBytes];
};

struct AuthPayload {
    uint8_t mac[kHmacBytes];
};

struct PingPayload {
    int64_t clientSendUs;
};

struct PongPayload {
    int64_t clientSendUs;
    int64_t agentRecvUs;
    int64_t agentSendUs;
};

struct CaptureReqPayload {
    int64_t targetUs;
    int32_t monitorIndex;
    uint8_t wireFormat;
    uint8_t reserved[3];
};

struct CaptureRespPayload {
    uint32_t status;
    int32_t width;
    int32_t height;
    int64_t presentUs;
    int64_t captureUs;
    int32_t monitorIndex;
    uint8_t method;
    uint8_t wireFormat;
    uint8_t late;
    uint8_t reserved;
    uint32_t payloadBytes;
    uint32_t sourceFormat;
    wchar_t device[32];
};

struct StreamStartPayload {
    int32_t monitorIndex;
    uint16_t fps;
    uint16_t scalePercent;
    uint8_t quality;
    uint8_t wireFormat;
    uint8_t reserved[2];
};

struct StreamStatusPayload {
    uint32_t status;
    int32_t width;
    int32_t height;
    int32_t monitorIndex;
    uint8_t method;
    uint8_t reserved[3];
    uint32_t sourceFormat;
    wchar_t device[32];
};

struct StreamFrameHeader {
    uint32_t seq;
    int64_t captureUs;
    int64_t presentUs;
    int32_t width;
    int32_t height;
    uint32_t payloadBytes;
    uint8_t wireFormat;
    uint8_t flags;
    uint8_t reserved[2];
};

#pragma pack(pop)

constexpr char kAuthLabel[] = "SCREENFUSE-AUTH-v1";

inline void ComputeAuthMac(const std::vector<uint8_t>& key, const uint8_t nonce[kNonceBytes], uint8_t out[kHmacBytes]) {
    std::vector<uint8_t> buffer(kNonceBytes + sizeof(kAuthLabel) - 1);
    memcpy(buffer.data(), nonce, kNonceBytes);
    memcpy(buffer.data() + kNonceBytes, kAuthLabel, sizeof(kAuthLabel) - 1);
    Hmac(key, buffer.data(), buffer.size(), out);
}

struct WinsockScope {
    bool ok = false;

    WinsockScope() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockScope() {
        if (ok) WSACleanup();
    }
};

inline void ConfigureSocket(SOCKET sock, int timeoutSeconds = 30) {
    BOOL noDelay = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    DWORD timeout = static_cast<DWORD>(timeoutSeconds) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    tcp_keepalive keepAlive{ 1, 5000, 1000 };
    DWORD returned = 0;
    WSAIoctl(sock, SIO_KEEPALIVE_VALS, &keepAlive, sizeof(keepAlive), nullptr, 0, &returned, nullptr, nullptr);
}

inline bool WaitReadable(SOCKET sock, int timeoutMs) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(sock, &readable);

    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    const int ready = select(0, &readable, nullptr, nullptr, timeoutMs < 0 ? nullptr : &tv);
    return ready > 0;
}

inline bool SendAll(SOCKET sock, const void* data, size_t length) {
    const char* cursor = static_cast<const char*>(data);
    size_t remaining = length;

    while (remaining > 0) {
        const int chunk = send(sock, cursor, static_cast<int>(remaining > 1048576 ? 1048576 : remaining), 0);
        if (chunk <= 0) return false;
        cursor += chunk;
        remaining -= static_cast<size_t>(chunk);
    }

    return true;
}

enum class RecvStatus {
    Ok,
    Idle,
    Closed,
    Protocol
};

inline RecvStatus RecvAllStatus(SOCKET sock, void* data, size_t length) {
    char* cursor = static_cast<char*>(data);
    size_t remaining = length;

    while (remaining > 0) {
        const int chunk = recv(sock, cursor, static_cast<int>(remaining > 1048576 ? 1048576 : remaining), 0);

        if (chunk == 0) return RecvStatus::Closed;

        if (chunk < 0) {
            const int code = WSAGetLastError();
            if (code == WSAETIMEDOUT && remaining == length) return RecvStatus::Idle;
            return RecvStatus::Closed;
        }

        cursor += chunk;
        remaining -= static_cast<size_t>(chunk);
    }

    return RecvStatus::Ok;
}

inline bool RecvAll(SOCKET sock, void* data, size_t length) {
    return RecvAllStatus(sock, data, length) == RecvStatus::Ok;
}

inline bool SendMsg(SOCKET sock, Msg type, const void* payload, size_t length) {
    if (length > kMaxPayloadBytes) return false;

    MsgHeader header{ kMagic, static_cast<uint16_t>(type), kProtocolVersion, static_cast<uint32_t>(length) };
    if (!SendAll(sock, &header, sizeof(header))) return false;
    if (length > 0 && !SendAll(sock, payload, length)) return false;
    return true;
}

inline bool SendMsg2(SOCKET sock, Msg type, const void* head, size_t headLength, const void* body, size_t bodyLength) {
    const size_t total = headLength + bodyLength;
    if (total > kMaxPayloadBytes) return false;

    MsgHeader header{ kMagic, static_cast<uint16_t>(type), kProtocolVersion, static_cast<uint32_t>(total) };
    if (!SendAll(sock, &header, sizeof(header))) return false;
    if (headLength > 0 && !SendAll(sock, head, headLength)) return false;
    if (bodyLength > 0 && !SendAll(sock, body, bodyLength)) return false;
    return true;
}

inline bool SendText(SOCKET sock, Msg type, const std::string& text) {
    return SendMsg(sock, type, text.data(), text.size());
}

inline RecvStatus RecvMsgEx(SOCKET sock, Msg& type, std::vector<uint8_t>& payload, std::string& error) {
    MsgHeader header{};
    const RecvStatus headerStatus = RecvAllStatus(sock, &header, sizeof(header));

    if (headerStatus == RecvStatus::Idle) return RecvStatus::Idle;

    if (headerStatus != RecvStatus::Ok) {
        error = "connection closed while reading a message header";
        return RecvStatus::Closed;
    }

    if (header.magic != kMagic) {
        error = "bad frame magic - the peer is not ScreenFuse";
        return RecvStatus::Protocol;
    }

    if (header.version != kProtocolVersion) {
        error = "protocol version mismatch: peer speaks " + std::to_string(header.version) +
            ", this build speaks " + std::to_string(kProtocolVersion) +
            " - copy the matching screenfuse_agent.exe to the remote PC";
        return RecvStatus::Protocol;
    }

    if (header.length > kMaxPayloadBytes) {
        error = "peer announced an oversized payload (" + std::to_string(header.length) + " bytes)";
        return RecvStatus::Protocol;
    }

    payload.resize(header.length);
    if (header.length > 0 && RecvAllStatus(sock, payload.data(), payload.size()) != RecvStatus::Ok) {
        error = "connection closed while reading a " + std::to_string(header.length) + " byte payload";
        return RecvStatus::Closed;
    }

    type = static_cast<Msg>(header.type);
    return RecvStatus::Ok;
}

inline bool RecvMsg(SOCKET sock, Msg& type, std::vector<uint8_t>& payload, std::string& error) {
    const RecvStatus status = RecvMsgEx(sock, type, payload, error);
    if (status == RecvStatus::Idle) error = "the peer did not answer in time";
    return status == RecvStatus::Ok;
}

struct ClockSync {
    int64_t offsetUs = 0;
    int64_t rttUs = 0;
    int samples = 0;
};

inline int64_t ToAgentUs(const ClockSync& sync, int64_t controllerUs) { return controllerUs + sync.offsetUs; }
inline int64_t ToControllerUs(const ClockSync& sync, int64_t agentUs) { return agentUs - sync.offsetUs; }

inline bool RunClockSync(SOCKET sock, int samples, ClockSync& out, std::string& error) {
    ClockSync best;
    best.rttUs = INT64_MAX;

    for (int i = 0; i < samples; ++i) {
        PingPayload ping{ NowUs() };
        if (!SendMsg(sock, Msg::Ping, &ping, sizeof(ping))) {
            error = "clock sync: send failed";
            return false;
        }

        Msg type{};
        std::vector<uint8_t> payload;
        if (!RecvMsg(sock, type, payload, error)) return false;

        const int64_t clientRecvUs = NowUs();

        if (type != Msg::Pong || payload.size() != sizeof(PongPayload)) {
            error = "clock sync: unexpected reply from agent";
            return false;
        }

        PongPayload pong{};
        memcpy(&pong, payload.data(), sizeof(pong));

        const int64_t rtt = (clientRecvUs - pong.clientSendUs) - (pong.agentSendUs - pong.agentRecvUs);
        const int64_t offset = ((pong.agentRecvUs - pong.clientSendUs) + (pong.agentSendUs - clientRecvUs)) / 2;

        if (rtt >= 0 && rtt < best.rttUs) {
            best.rttUs = rtt;
            best.offsetUs = offset;
        }

        best.samples++;
        Sleep(2);
    }

    if (best.rttUs == INT64_MAX) {
        error = "clock sync: no usable samples";
        return false;
    }

    out = best;
    return true;
}

} // namespace screenfuse
