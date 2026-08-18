#include "common.hpp"
#include "net.hpp"
#include "capture.hpp"
#include "stream.hpp"

using namespace screenfuse;

namespace {

struct Options {
    uint16_t port = kDefaultPort;
    std::string bind = "0.0.0.0";
    std::wstring keyPath;
    std::vector<std::string> allow;
    int monitorIndex = -1;
    int idleTimeoutSeconds = 0;
    bool listMonitors = false;
    bool generateKey = false;
    bool help = false;
};

void PrintUsage() {
    printf(
        "screenfuse_agent - display capture agent; runs on the PC being captured\n"
        "\n"
        "  --port <n>        listen port (default %u)\n"
        "  --bind <addr>     interface to listen on (default 0.0.0.0)\n"
        "  --key <path>      shared key file (default screenfuse.key next to the exe)\n"
        "  --allow <ip>      only accept this address; repeatable, default any\n"
        "  --monitor <n>     monitor index to capture (default: primary)\n"
        "  --idle-timeout <s>  drop a connected controller after this many seconds\n"
        "                    of silence (default 0, never - TCP keepalive already\n"
        "                    catches a peer that actually went away)\n"
        "  --list            list monitors and exit\n"
        "  --genkey          create a new key file and exit\n"
        "  --help            this text\n",
        static_cast<unsigned>(kDefaultPort));
}

bool ParseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = (i + 1 < argc) && argv[i + 1][0] != '-';
        const std::string value = hasValue ? argv[i + 1] : std::string();

        if (arg == "--help" || arg == "-h") options.help = true;
        else if (arg == "--list") options.listMonitors = true;
        else if (arg == "--genkey") options.generateKey = true;
        else if (arg == "--port" && hasValue) { options.port = static_cast<uint16_t>(atoi(value.c_str())); ++i; }
        else if (arg == "--bind" && hasValue) { options.bind = value; ++i; }
        else if (arg == "--key" && hasValue) { options.keyPath = ToWide(value); ++i; }
        else if (arg == "--allow" && hasValue) { options.allow.push_back(value); ++i; }
        else if (arg == "--monitor" && hasValue) { options.monitorIndex = atoi(value.c_str()); ++i; }
        else if (arg == "--idle-timeout" && hasValue) { options.idleTimeoutSeconds = atoi(value.c_str()); ++i; }
        else {
            LogErr("unknown argument: %s", arg.c_str());
            return false;
        }
    }

    if (options.keyPath.empty()) options.keyPath = ExeDir() + L"\\screenfuse.key";
    return true;
}

void ListMonitors() {
    const std::vector<MonitorTarget> monitors = EnumerateMonitors();
    if (monitors.empty()) {
        LogErr("no monitors found");
        return;
    }

    for (const MonitorTarget& monitor : monitors) {
        printf("  [%d] %-14s %4dx%-4d at (%d,%d)%s  on %s\n",
            monitor.index,
            ToUtf8(monitor.device).c_str(),
            monitor.width(), monitor.height(),
            static_cast<int>(monitor.rect.left), static_cast<int>(monitor.rect.top),
            monitor.primary ? "  [primary]" : "",
            ToUtf8(monitor.adapter).c_str());
    }
}

SOCKET g_listenSocket = INVALID_SOCKET;

BOOL WINAPI ConsoleHandler(DWORD) {
    if (g_listenSocket != INVALID_SOCKET) closesocket(g_listenSocket);
    return FALSE;
}

bool SendCaptureFailure(SOCKET client, uint32_t status, const std::string& text) {
    CaptureRespPayload response{};
    response.status = status;
    response.payloadBytes = static_cast<uint32_t>(text.size());
    return SendMsg2(client, Msg::CaptureResp, &response, sizeof(response), text.data(), text.size());
}

void HandleCapture(SOCKET client, const CaptureReqPayload& request, int defaultMonitorIndex) {
    const std::vector<MonitorTarget> monitors = EnumerateMonitors();
    if (monitors.empty()) {
        LogErr("capture: no monitors attached");
        SendCaptureFailure(client, kCaptureNoMonitor, "no monitors attached");
        return;
    }

    const int wanted = request.monitorIndex >= 0 ? request.monitorIndex : defaultMonitorIndex;
    const MonitorTarget* target = wanted >= 0 ? FindMonitorByIndex(monitors, wanted) : PrimaryMonitor(monitors);

    if (!target) {
        const std::string text = "monitor index " + std::to_string(wanted) + " does not exist";
        LogErr("capture: %s", text.c_str());
        SendCaptureFailure(client, kCaptureNoMonitor, text);
        return;
    }

    CaptureOutput capture;
    std::string error;
    const int64_t targetTicks = UsToTicks(request.targetUs);

    if (!CaptureAtTicks(*target, targetTicks, capture, error)) {
        LogErr("capture failed: %s", error.c_str());
        SendCaptureFailure(client, kCaptureFailed, error);
        return;
    }

    if (!error.empty()) LogWarn("capture: %s", error.c_str());

    const auto format = static_cast<WireFormat>(request.wireFormat);

    std::vector<uint8_t> encoded;
    const uint8_t* body = capture.image.pixels.data();
    size_t bodyBytes = capture.image.pixels.size();

    if (format == WireFormat::Png) {
        std::string encodeError;
        if (!EncodePng(capture.image, encoded, encodeError)) {
            LogErr("capture: %s", encodeError.c_str());
            SendCaptureFailure(client, kCaptureEncodeFailed, encodeError);
            return;
        }
        body = encoded.data();
        bodyBytes = encoded.size();
    }

    CaptureRespPayload response{};
    response.status = kCaptureOk;
    response.width = capture.image.width;
    response.height = capture.image.height;
    response.presentUs = capture.presentTicks ? TicksToUs(capture.presentTicks) : 0;
    response.captureUs = TicksToUs(capture.captureTicks);
    response.monitorIndex = target->index;
    response.method = static_cast<uint8_t>(capture.usedGdi ? CaptureMethod::Gdi : CaptureMethod::DesktopDuplication);
    response.wireFormat = static_cast<uint8_t>(format);
    response.late = capture.late ? 1 : 0;
    response.payloadBytes = static_cast<uint32_t>(bodyBytes);
    response.sourceFormat = capture.sourceFormat;
    wcsncpy_s(response.device, target->device.c_str(), _TRUNCATE);

    LogInfo("capture: monitor %d (%s) %dx%d %s, %s, %d frame(s) seen, %.2f MB out%s",
        target->index, ToUtf8(target->device).c_str(),
        capture.image.width, capture.image.height,
        FormatName(capture.sourceFormat),
        capture.usedGdi ? "GDI" : "duplication",
        capture.framesSeen,
        static_cast<double>(bodyBytes) / (1024.0 * 1024.0),
        capture.late ? ", LATE (target already passed)" : "");

    if (!SendMsg2(client, Msg::CaptureResp, &response, sizeof(response), body, bodyBytes)) {
        LogErr("capture: send failed");
    }
}


enum class StreamOutcome {
    Stopped,
    Shutdown,
    LinkLost
};

bool SendStreamStatus(SOCKET client, uint32_t status, const MonitorTarget* target,
    int width, int height, bool gdi, uint32_t sourceFormat, const std::string& text) {

    StreamStatusPayload payload{};
    payload.status = status;
    payload.width = width;
    payload.height = height;
    payload.monitorIndex = target ? target->index : -1;
    payload.method = static_cast<uint8_t>(gdi ? CaptureMethod::Gdi : CaptureMethod::DesktopDuplication);
    payload.sourceFormat = sourceFormat;
    if (target) wcsncpy_s(payload.device, target->device.c_str(), _TRUNCATE);

    return SendMsg2(client, Msg::StreamStatus, &payload, sizeof(payload), text.data(), text.size());
}

int Clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

StreamOutcome RunStream(SOCKET client, const std::string& peer, const StreamStartPayload& start, int defaultMonitorIndex) {
    const std::vector<MonitorTarget> monitors = EnumerateMonitors();
    const int wanted = start.monitorIndex >= 0 ? start.monitorIndex : defaultMonitorIndex;
    const MonitorTarget* target = monitors.empty()
        ? nullptr
        : (wanted >= 0 ? FindMonitorByIndex(monitors, wanted) : PrimaryMonitor(monitors));

    if (!target) {
        const std::string text = monitors.empty()
            ? "no monitors attached"
            : "monitor index " + std::to_string(wanted) + " does not exist";
        LogErr("stream: %s", text.c_str());
        SendStreamStatus(client, kStreamFailed, nullptr, 0, 0, false, 0, text);
        return StreamOutcome::Stopped;
    }

    ContinuousCapture capture;
    std::string openError;
    if (!capture.Open(*target, openError)) {
        LogErr("stream: %s", openError.c_str());
        SendStreamStatus(client, kStreamFailed, target, 0, 0, false, 0, openError);
        return StreamOutcome::Stopped;
    }
    if (!openError.empty()) LogWarn("stream: %s", openError.c_str());

    const int fps = Clamp(start.fps ? start.fps : 30, 1, 240);
    const int quality = Clamp(start.quality ? start.quality : 80, 1, 100);
    const int scalePercent = Clamp(start.scalePercent ? start.scalePercent : 100, 10, 100);
    const WireFormat format = static_cast<WireFormat>(start.wireFormat);

    const int outWidth = target->width() * scalePercent / 100;
    const int outHeight = target->height() * scalePercent / 100;

    WicFactory wic;
    if (format == WireFormat::Jpeg && !wic.ok()) {
        const std::string text = "WIC is unavailable, so JPEG cannot be encoded here";
        LogErr("stream: %s", text.c_str());
        SendStreamStatus(client, kStreamFailed, target, 0, 0, capture.usedGdi(), 0, text);
        return StreamOutcome::Stopped;
    }

    if (!SendStreamStatus(client, kStreamStarted, target, outWidth, outHeight,
        capture.usedGdi(), capture.sourceFormat(), openError)) {
        return StreamOutcome::LinkLost;
    }

    const std::string formatText = format == WireFormat::Jpeg
        ? "JPEG quality " + std::to_string(quality)
        : std::string("raw BGRA");

    LogInfo("stream: monitor %d (%s) %dx%d -> %dx%d, %d fps cap, %s",
        target->index, ToUtf8(target->device).c_str(),
        target->width(), target->height(), outWidth, outHeight, fps, formatText.c_str());

    Image frame;
    Image scaled;
    std::vector<uint8_t> encoded;

    const int64_t frameTicks = UsToTicks(1000000 / fps);
    int64_t dueTicks = QpcTicks();
    int64_t lastSendUs = NowUs();

    uint32_t seq = 0;
    bool pending = false;
    int64_t pendingCaptureTicks = 0;
    int64_t pendingPresentTicks = 0;
    int reopenAttempts = 0;

    RateMeter meter;

    for (;;) {
        while (WaitReadable(client, 0)) {
            Msg type{};
            std::vector<uint8_t> payload;
            std::string error;

            const RecvStatus status = RecvMsgEx(client, type, payload, error);
            if (status == RecvStatus::Idle) break;

            if (status != RecvStatus::Ok) {
                LogInfo("%s: disconnected during the stream (%s)", peer.c_str(), error.c_str());
                return StreamOutcome::LinkLost;
            }

            if (type == Msg::StreamStop) {
                LogInfo("%s: stream stopped", peer.c_str());
                SendStreamStatus(client, kStreamStopped, target, outWidth, outHeight,
                    capture.usedGdi(), capture.sourceFormat(), std::string());
                return StreamOutcome::Stopped;
            }

            if (type == Msg::Shutdown) {
                LogInfo("%s: shutdown requested during the stream", peer.c_str());
                SendMsg(client, Msg::Bye, nullptr, 0);
                return StreamOutcome::Shutdown;
            }

            if (type == Msg::Ping && payload.size() == sizeof(PingPayload)) {
                const int64_t receivedUs = NowUs();
                PingPayload ping{};
                memcpy(&ping, payload.data(), sizeof(ping));
                PongPayload pong{ ping.clientSendUs, receivedUs, NowUs() };
                if (!SendMsg(client, Msg::Pong, &pong, sizeof(pong))) return StreamOutcome::LinkLost;
                continue;
            }

            if (type == Msg::CaptureReq) {
                SendText(client, Msg::Error, "the agent is streaming - stop the stream before asking for a still");
                continue;
            }
        }

        int64_t waitUs = TicksToUs(dueTicks - QpcTicks());
        if (waitUs < 0) waitUs = 0;
        if (waitUs > 100000) waitUs = 100000;

        bool fresh = false;
        int64_t presentTicks = 0;
        int64_t captureTicks = 0;
        std::string error;

        if (!capture.Next(static_cast<int>(waitUs / 1000), frame, presentTicks, captureTicks, fresh, error)) {
            LogWarn("stream: %s", error.c_str());

            if (++reopenAttempts > 3 || !capture.Open(*target, error)) {
                LogErr("stream: giving up on this monitor: %s", error.c_str());
                SendStreamStatus(client, kStreamFailed, target, outWidth, outHeight,
                    capture.usedGdi(), capture.sourceFormat(), error);
                return StreamOutcome::Stopped;
            }

            Sleep(50);
            continue;
        }

        if (fresh) {
            pending = true;
            pendingCaptureTicks = captureTicks;
            pendingPresentTicks = presentTicks;
        }

        const int64_t now = QpcTicks();
        if (now < dueTicks) continue;

        const int64_t nowUs = TicksToUs(now);

        if (!pending) {
            if (nowUs - lastSendUs < 1000000) continue;

            StreamFrameHeader header{};
            header.seq = ++seq;
            header.captureUs = nowUs;
            header.flags = kStreamFlagHeartbeat;

            if (!SendMsg(client, Msg::StreamFrame, &header, sizeof(header))) return StreamOutcome::LinkLost;

            lastSendUs = nowUs;
            dueTicks = now + frameTicks;
            continue;
        }

        const Image* source = &frame;
        if (scalePercent != 100) {
            ScaleToInto(frame, scaled, frame.width * scalePercent / 100, frame.height * scalePercent / 100);
            source = &scaled;
        }

        const uint8_t* body = source->pixels.data();
        size_t bodyBytes = source->pixels.size();

        if (format == WireFormat::Jpeg) {
            std::string encodeError;
            if (!EncodeJpeg(wic.get(), *source, quality, encoded, encodeError)) {
                LogErr("stream: %s", encodeError.c_str());
                SendStreamStatus(client, kStreamFailed, target, outWidth, outHeight,
                    capture.usedGdi(), capture.sourceFormat(), encodeError);
                return StreamOutcome::Stopped;
            }
            body = encoded.data();
            bodyBytes = encoded.size();
        }

        StreamFrameHeader header{};
        header.seq = ++seq;
        header.captureUs = TicksToUs(pendingCaptureTicks);
        header.presentUs = pendingPresentTicks ? TicksToUs(pendingPresentTicks) : 0;
        header.width = source->width;
        header.height = source->height;
        header.payloadBytes = static_cast<uint32_t>(bodyBytes);
        header.wireFormat = static_cast<uint8_t>(format);

        if (!SendMsg2(client, Msg::StreamFrame, &header, sizeof(header), body, bodyBytes)) {
            LogInfo("%s: send failed during the stream", peer.c_str());
            return StreamOutcome::LinkLost;
        }

        pending = false;
        reopenAttempts = 0;
        lastSendUs = nowUs;
        dueTicks = now + frameTicks;

        meter.Add(bodyBytes + sizeof(header) + sizeof(MsgHeader));

        double perSecond = 0.0, megabits = 0.0, latencyMs = 0.0;
        if (meter.Elapsed(nowUs, 5000000, perSecond, megabits, latencyMs)) {
            LogInfo("stream: %.1f fps out, %.1f Mbit/s, %dx%d", perSecond, megabits, source->width, source->height);
        }
    }
}

bool ServeClient(SOCKET client, const std::string& peer, const std::vector<uint8_t>& key,
    int defaultMonitorIndex, int idleTimeoutSeconds) {
    ConfigureSocket(client);

    Msg type{};
    std::vector<uint8_t> payload;
    std::string error;

    if (!RecvMsg(client, type, payload, error) || type != Msg::Hello || payload.size() != sizeof(HelloPayload)) {
        LogWarn("%s: bad handshake (%s)", peer.c_str(), error.empty() ? "unexpected message" : error.c_str());
        return true;
    }

    HelloAckPayload ack{};
    ack.protocolVersion = kProtocolVersion;
    if (!RandomBytes(ack.nonce, kNonceBytes)) {
        LogErr("%s: cannot generate a nonce", peer.c_str());
        return true;
    }

    if (!SendMsg(client, Msg::HelloAck, &ack, sizeof(ack))) return true;

    if (!RecvMsg(client, type, payload, error) || type != Msg::Auth || payload.size() != sizeof(AuthPayload)) {
        LogWarn("%s: no auth response (%s)", peer.c_str(), error.empty() ? "unexpected message" : error.c_str());
        return true;
    }

    uint8_t expected[kHmacBytes]{};
    ComputeAuthMac(key, ack.nonce, expected);

    if (!ConstantTimeEqual(expected, payload.data(), kHmacBytes)) {
        LogWarn("%s: AUTH FAILED - wrong key", peer.c_str());
        SendText(client, Msg::AuthFail, "wrong key");
        return true;
    }

    SendMsg(client, Msg::AuthOk, nullptr, 0);
    LogInfo("%s: authenticated", peer.c_str());

    int64_t lastActivityUs = NowUs();

    for (;;) {
        const RecvStatus status = RecvMsgEx(client, type, payload, error);

        if (status == RecvStatus::Idle) {
            if (idleTimeoutSeconds > 0 &&
                NowUs() - lastActivityUs > static_cast<int64_t>(idleTimeoutSeconds) * 1000000) {
                LogInfo("%s: idle for %d seconds, closing", peer.c_str(), idleTimeoutSeconds);
                return true;
            }
            continue;
        }

        if (status != RecvStatus::Ok) {
            LogInfo("%s: disconnected (%s)", peer.c_str(), error.c_str());
            return true;
        }

        lastActivityUs = NowUs();

        switch (type) {
        case Msg::Ping: {
            if (payload.size() != sizeof(PingPayload)) return true;

            const int64_t receivedUs = NowUs();
            PingPayload ping{};
            memcpy(&ping, payload.data(), sizeof(ping));

            PongPayload pong{ ping.clientSendUs, receivedUs, NowUs() };
            if (!SendMsg(client, Msg::Pong, &pong, sizeof(pong))) return true;
            break;
        }

        case Msg::CaptureReq: {
            if (payload.size() != sizeof(CaptureReqPayload)) return true;

            CaptureReqPayload request{};
            memcpy(&request, payload.data(), sizeof(request));
            HandleCapture(client, request, defaultMonitorIndex);
            break;
        }

        case Msg::StreamStart: {
            if (payload.size() != sizeof(StreamStartPayload)) return true;

            StreamStartPayload start{};
            memcpy(&start, payload.data(), sizeof(start));

            const StreamOutcome outcome = RunStream(client, peer, start, defaultMonitorIndex);
            if (outcome == StreamOutcome::Shutdown) return false;
            if (outcome == StreamOutcome::LinkLost) return true;

            lastActivityUs = NowUs();
            break;
        }

        case Msg::StreamStop:
            SendStreamStatus(client, kStreamStopped, nullptr, 0, 0, false, 0, std::string());
            break;

        case Msg::Shutdown:
            LogInfo("%s: shutdown requested", peer.c_str());
            SendMsg(client, Msg::Bye, nullptr, 0);
            return false;

        default:
            LogWarn("%s: unexpected message type %u", peer.c_str(), static_cast<unsigned>(type));
            SendText(client, Msg::Error, "unexpected message");
            break;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, options)) return 1;

    if (options.help) {
        PrintUsage();
        return 0;
    }

    EnableDpiAwareness();
    RaiseTimerResolution();
    ComScope com;

    if (options.listMonitors) {
        ListMonitors();
        return 0;
    }

    if (options.generateKey) {
        std::vector<uint8_t> key(kKeyBytes);
        std::string error;

        if (!RandomBytes(key.data(), key.size()) || !WriteKeyFile(options.keyPath, key, error)) {
            LogErr("%s", error.empty() ? "cannot generate key" : error.c_str());
            return 1;
        }

        LogInfo("wrote %s", ToUtf8(options.keyPath).c_str());
        LogInfo("copy this file to the controller PC - both sides need the same key");
        return 0;
    }

    std::vector<uint8_t> key;
    std::string error;
    if (!ReadKeyFile(options.keyPath, key, error)) {
        LogErr("%s", error.c_str());
        LogErr("run with --genkey once, then copy the key file to the controller PC");
        return 1;
    }

    WinsockScope winsock;
    if (!winsock.ok) {
        LogErr("WSAStartup failed");
        return 1;
    }

    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) {
        LogErr("socket() failed: %s", ErrorText(WSAGetLastError()).c_str());
        return 1;
    }

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (inet_pton(AF_INET, options.bind.c_str(), &address.sin_addr) != 1) {
        LogErr("bad --bind address: %s", options.bind.c_str());
        return 1;
    }

    if (bind(g_listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        LogErr("bind failed: %s", ErrorText(WSAGetLastError()).c_str());
        return 1;
    }

    if (listen(g_listenSocket, 1) == SOCKET_ERROR) {
        LogErr("listen failed: %s", ErrorText(WSAGetLastError()).c_str());
        return 1;
    }

    const std::vector<MonitorTarget> monitors = EnumerateMonitors();
    const MonitorTarget* defaultTarget = options.monitorIndex >= 0
        ? FindMonitorByIndex(monitors, options.monitorIndex)
        : PrimaryMonitor(monitors);

    LogInfo("screenfuse_agent listening on %s:%u", options.bind.c_str(), static_cast<unsigned>(options.port));
    if (defaultTarget) {
        LogInfo("default monitor: [%d] %s %dx%d",
            defaultTarget->index, ToUtf8(defaultTarget->device).c_str(),
            defaultTarget->width(), defaultTarget->height());
    } else {
        LogWarn("configured monitor index %d not found - captures will fail until it exists", options.monitorIndex);
    }

    if (options.allow.empty()) {
        LogWarn("no --allow list: any host with the key may request a screenshot");
    } else {
        for (const std::string& allowed : options.allow) LogInfo("allowed client: %s", allowed.c_str());
    }

    for (;;) {
        sockaddr_in peerAddress{};
        int peerLength = sizeof(peerAddress);
        const SOCKET client = accept(g_listenSocket, reinterpret_cast<sockaddr*>(&peerAddress), &peerLength);

        if (client == INVALID_SOCKET) {
            const int code = WSAGetLastError();
            if (code == WSAEINTR || code == WSAENOTSOCK) break;
            LogWarn("accept failed: %s", ErrorText(code).c_str());
            continue;
        }

        char peerIp[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &peerAddress.sin_addr, peerIp, sizeof(peerIp));
        const std::string peer = std::string(peerIp) + ":" + std::to_string(ntohs(peerAddress.sin_port));

        bool allowed = options.allow.empty();
        for (const std::string& entry : options.allow) {
            if (entry == peerIp) { allowed = true; break; }
        }

        if (!allowed) {
            LogWarn("%s: REJECTED - not in the --allow list", peer.c_str());
            closesocket(client);
            continue;
        }

        LogInfo("%s: connected", peer.c_str());
        const bool keepListening = ServeClient(client, peer, key,
            defaultTarget ? defaultTarget->index : -1, options.idleTimeoutSeconds);
        closesocket(client);

        if (!keepListening) break;
    }

    if (g_listenSocket != INVALID_SOCKET) closesocket(g_listenSocket);
    RestoreTimerResolution();
    LogInfo("agent stopped");
    return 0;
}
