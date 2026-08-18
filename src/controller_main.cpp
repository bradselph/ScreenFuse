#include "common.hpp"
#include "net.hpp"
#include "capture.hpp"
#include "image.hpp"
#include "stream.hpp"

#include <atomic>
#include <memory>
#include <thread>

using namespace screenfuse;

namespace {

HWND FindOverlayWindow(const std::wstring& name) {
    if (name.empty()) return nullptr;
    if (HWND byClass = FindWindowW(name.c_str(), nullptr)) return byClass;
    return FindWindowW(nullptr, name.c_str());
}

struct Options {
    std::string host;
    uint16_t port = kDefaultPort;
    std::wstring keyPath;
    std::wstring outDir;

    int remoteMonitor = -1;
    int overlayMonitor = -1;
    std::wstring overlayWindow;
    int leadMs = 250;
    int overlayDelayMs = 0;
    int maxSkewMs = 33;
    int maxAgeMs = 250;
    int lowKey = 8;
    int highKey = 40;
    int blackThreshold = 12;
    double minBlackRatio = 0.0;
    bool screenMerge = false;
    bool png = false;
    bool once = false;
    bool keepAgent = false;
    bool keepRejected = true;
    bool listMonitors = false;
    bool generateKey = false;
    bool help = false;

    bool stream = false;
    int streamFps = 30;
    int streamQuality = 80;
    int streamScale = 100;
    int streamOutMonitor = -1;
    bool streamRaw = false;
    std::wstring streamTitle = L"ScreenFuse Output";


    std::wstring remergeRemote;
    std::wstring remergeOverlay;
    UINT hotkeyMods = MOD_CONTROL | MOD_SHIFT;
    UINT hotkeyVk = 'P';
};

void PrintUsage() {
    printf(
        "screenfuse - lays this PC's overlay display over another PC's display,\n"
        "             as verified screenshots or as a live merged video window\n"
        "\n"
        "  --host <ip>            PC running screenfuse_agent (required)\n"
        "  --port <n>             agent port (default %u)\n"
        "  --key <path>           shared key file (default screenfuse.key next to the exe)\n"
        "  --out <dir>            output folder (default Captured next to the exe)\n"
        "  --remote-monitor <n>   monitor index over there (default: its own default)\n"
        "  --overlay-monitor <n>  monitor index here, the overlay feed (default: primary,\n"
        "                         or the one --overlay-window is on)\n"
        "  --overlay-window <s>   find the overlay monitor by looking for this window class\n"
        "                         or title instead of naming an index\n"
        "  --lead <ms>            how far ahead the capture instant is scheduled (default 250)\n"
        "  --overlay-delay <ms>   capture the overlay this much later than the remote frame,\n"
        "                         when what it draws lags what it is drawn over (default 0)\n"
        "  --max-skew <ms>        reject a capture whose two halves land further apart than this (default 33)\n"
        "  --max-age <ms>         warn when a returned frame is older than this, meaning that\n"
        "                         display had stopped updating (default 250)\n"
        "  --once                 take one screenshot and exit\n"
        "  --png                  have the agent send PNG instead of raw pixels (slower CPU, less network)\n"
        "\n"
        " compositing:\n"
        "  --merge <key|screen>   luma key, or screen blend which keeps every overlay pixel\n"
        "                         and brightens what is underneath (default key)\n"
        "  --key-low <n>          overlay luma at or below this vanishes (default 8). Raise this\n"
        "                         when the overlay's black is not quite black.\n"
        "  --key-high <n>         overlay luma at or above this comes through solid (default 40).\n"
        "                         Everything between the two is faded, so a wide gap dims the\n"
        "                         overlay - widen it only if edges look jagged.\n"
        "  --min-black <0..1>     refuse a capture unless the overlay frame is at least this\n"
        "                         black, to catch the wrong monitor (default 0, off)\n"
        "\n"
        " streaming (live merged video instead of stills):\n"
        "  --stream               stream continuously into a window that OBS, Discord or\n"
        "                         anything else that captures a window can use as a source\n"
        "  --stream-fps <n>       frames a second, both captured and shown (default 30)\n"
        "  --stream-quality <n>   JPEG quality the remote PC encodes with, 1-100 (default 80)\n"
        "  --stream-scale <n>     percent of the remote PC's resolution to send (default 100)\n"
        "  --stream-raw           send uncompressed frames (wired gigabit only, ~14 fps at 1080p)\n"
        "  --stream-monitor <n>   show the output fullscreen on this monitor instead of\n"
        "                         in a window, for capturing a whole display\n"
        "  --stream-title <s>     title of the output window (default ScreenFuse Output)\n"
        "\n"
        " the screenshot hotkey is Ctrl+Shift+P and works from any window;\n"
        " while streaming it saves the frame currently on screen\n"
        "\n"
        " other:\n"
        "  --keep-agent           do not shut the agent down on exit\n"
        "  --no-keep-rejected     do not write out captures that failed validation\n"
        "  --remerge-remote <png> --remerge-overlay <png>\n"
        "                         merge two saved frames again with different key settings\n"
        "                         and exit - no capture, no network\n"
        "  --list                 list this PC's monitors and exit\n"
        "  --genkey               create a new key file and exit\n"
        "  --help                 this text\n",
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
        else if (arg == "--once") options.once = true;
        else if (arg == "--png") options.png = true;
        else if (arg == "--keep-agent") options.keepAgent = true;
        else if (arg == "--no-keep-rejected") options.keepRejected = false;
        else if (arg == "--stream") options.stream = true;
        else if (arg == "--stream-raw") options.streamRaw = true;
        else if (arg == "--stream-fps" && hasValue) { options.streamFps = atoi(value.c_str()); ++i; }
        else if (arg == "--stream-quality" && hasValue) { options.streamQuality = atoi(value.c_str()); ++i; }
        else if (arg == "--stream-scale" && hasValue) { options.streamScale = atoi(value.c_str()); ++i; }
        else if (arg == "--stream-monitor" && hasValue) { options.streamOutMonitor = atoi(value.c_str()); ++i; }
        else if (arg == "--stream-title" && hasValue) { options.streamTitle = ToWide(value); ++i; }
        else if (arg == "--host" && hasValue) { options.host = value; ++i; }
        else if (arg == "--port" && hasValue) { options.port = static_cast<uint16_t>(atoi(value.c_str())); ++i; }
        else if (arg == "--key" && hasValue) { options.keyPath = ToWide(value); ++i; }
        else if (arg == "--out" && hasValue) { options.outDir = ToWide(value); ++i; }
        else if (arg == "--remote-monitor" && hasValue) { options.remoteMonitor = atoi(value.c_str()); ++i; }
        else if (arg == "--overlay-monitor" && hasValue) { options.overlayMonitor = atoi(value.c_str()); ++i; }
        else if (arg == "--overlay-window" && hasValue) { options.overlayWindow = ToWide(value); ++i; }
        else if (arg == "--lead" && hasValue) { options.leadMs = atoi(value.c_str()); ++i; }
        else if (arg == "--overlay-delay" && hasValue) { options.overlayDelayMs = atoi(value.c_str()); ++i; }
        else if (arg == "--max-skew" && hasValue) { options.maxSkewMs = atoi(value.c_str()); ++i; }
        else if (arg == "--max-age" && hasValue) { options.maxAgeMs = atoi(value.c_str()); ++i; }
        else if (arg == "--key-low" && hasValue) { options.lowKey = atoi(value.c_str()); ++i; }
        else if (arg == "--key-high" && hasValue) { options.highKey = atoi(value.c_str()); ++i; }
        else if (arg == "--min-black" && hasValue) { options.minBlackRatio = atof(value.c_str()); ++i; }
        else if (arg == "--merge" && hasValue) { options.screenMerge = (value == "screen"); ++i; }
        else if (arg == "--remerge-remote" && hasValue) { options.remergeRemote = ToWide(value); ++i; }
        else if (arg == "--remerge-overlay" && hasValue) { options.remergeOverlay = ToWide(value); ++i; }
        else {
            LogErr("unknown argument: %s", arg.c_str());
            return false;
        }
    }

    if (options.keyPath.empty()) options.keyPath = ExeDir() + L"\\screenfuse.key";
    if (options.outDir.empty()) options.outDir = ExeDir() + L"\\Captured";
    if (options.leadMs < 50) options.leadMs = 50;

    if (options.streamFps < 1) options.streamFps = 1;
    if (options.streamFps > 240) options.streamFps = 240;
    if (options.streamQuality < 1) options.streamQuality = 1;
    if (options.streamQuality > 100) options.streamQuality = 100;
    if (options.streamScale < 10) options.streamScale = 10;
    if (options.streamScale > 100) options.streamScale = 100;

    return true;
}

void ListMonitors(const std::wstring& overlayWindow) {
    const std::vector<MonitorTarget> monitors = EnumerateMonitors();
    const HWND overlay = FindOverlayWindow(overlayWindow);
    const HMONITOR overlayMonitor = overlay ? MonitorFromWindow(overlay, MONITOR_DEFAULTTONULL) : nullptr;

    for (const MonitorTarget& monitor : monitors) {
        printf("  [%d] %-14s %4dx%-4d at (%d,%d)%s%s\n",
            monitor.index,
            ToUtf8(monitor.device).c_str(),
            monitor.width(), monitor.height(),
            static_cast<int>(monitor.rect.left), static_cast<int>(monitor.rect.top),
            monitor.primary ? "  [primary]" : "",
            (overlayMonitor && monitor.handle == overlayMonitor) ? "  [--overlay-window is here]" : "");
    }

    if (!overlayWindow.empty() && !overlay) {
        printf("\n  (no window matched \"%s\" - it is not running, or the name is a title\n"
               "   and a class was needed, or the other way round)\n", ToUtf8(overlayWindow).c_str());
    }
}

struct CheckLog {
    std::vector<std::string> lines;
    bool failed = false;

    void Pass(const std::string& text) { lines.push_back("PASS " + text); }
    void Warn(const std::string& text) { lines.push_back("WARN " + text); }
    void Fail(const std::string& text) { lines.push_back("FAIL " + text); failed = true; }

    void Print() const {
        for (const std::string& line : lines) {
            if (line.rfind("FAIL", 0) == 0) LogErr("  %s", line.c_str());
            else if (line.rfind("WARN", 0) == 0) LogWarn("  %s", line.c_str());
            else LogInfo("  %s", line.c_str());
        }
    }
};

std::string JsonEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if (static_cast<unsigned char>(c) < 0x20) out += "?";
        else out.push_back(c);
    }
    return out;
}

bool WriteSidecar(const std::wstring& path, const std::string& json) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const bool ok = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) != FALSE;
    CloseHandle(file);
    return ok;
}

struct Session {
    Options options;
    std::vector<uint8_t> key;
    SOCKET sock = INVALID_SOCKET;
    ClockSync sync;
    MonitorTarget overlayTarget;
    bool linkDead = false;

    bool TakeScreenshot();
    bool Connect();
    bool EnsureConnected();
    bool Heartbeat();
    void MarkDead(const char* reason);
    void Close();
};

bool Session::TakeScreenshot() {
    CheckLog checks;

    const int64_t targetUs = NowUs() + static_cast<int64_t>(options.leadMs) * 1000;
    const int64_t overlayTargetUs = targetUs + static_cast<int64_t>(options.overlayDelayMs) * 1000;

    CaptureReqPayload request{};
    request.targetUs = ToAgentUs(sync, targetUs);
    request.monitorIndex = options.remoteMonitor;
    request.wireFormat = static_cast<uint8_t>(options.png ? WireFormat::Png : WireFormat::RawBgra);

    if (!SendMsg(sock, Msg::CaptureReq, &request, sizeof(request))) {
        MarkDead("cannot reach the agent");
        return false;
    }

    CaptureOutput overlayCapture;
    std::string overlayError;
    const bool overlayOk = CaptureAtTicks(overlayTarget, UsToTicks(overlayTargetUs), overlayCapture, overlayError);

    Msg type{};
    std::vector<uint8_t> payload;
    std::string error;

    if (!RecvMsg(sock, type, payload, error)) {
        MarkDead(error.c_str());
        return false;
    }

    if (type != Msg::CaptureResp || payload.size() < sizeof(CaptureRespPayload)) {
        LogErr("capture: unexpected reply from the agent");
        return false;
    }

    CaptureRespPayload response{};
    memcpy(&response, payload.data(), sizeof(response));

    const uint8_t* body = payload.data() + sizeof(response);
    const size_t bodyBytes = payload.size() - sizeof(response);

    if (response.status != kCaptureOk) {
        const std::string text(reinterpret_cast<const char*>(body), bodyBytes);
        LogErr("capture: agent reported failure: %s", text.c_str());
        return false;
    }

    if (bodyBytes != response.payloadBytes) {
        LogErr("capture: truncated payload (%zu of %u bytes)", bodyBytes, response.payloadBytes);
        return false;
    }
    checks.Pass("remote frame received (" + std::to_string(bodyBytes / 1024) + " KB)");

    Image remoteImage;
    if (static_cast<WireFormat>(response.wireFormat) == WireFormat::Png) {
        std::string decodeError;
        if (!DecodePng(body, bodyBytes, remoteImage, decodeError)) {
            LogErr("capture: %s", decodeError.c_str());
            return false;
        }
    } else {
        const size_t expected = static_cast<size_t>(response.width) * response.height * 4;
        if (bodyBytes != expected) {
            LogErr("capture: raw payload is %zu bytes, expected %zu for %dx%d",
                bodyBytes, expected, response.width, response.height);
            return false;
        }
        remoteImage.width = response.width;
        remoteImage.height = response.height;
        remoteImage.pixels.assign(body, body + bodyBytes);
    }

    if (!remoteImage.valid() || remoteImage.width != response.width || remoteImage.height != response.height) {
        LogErr("capture: remote frame geometry does not match the header");
        return false;
    }
    checks.Pass("remote frame decodes to " + std::to_string(remoteImage.width) + "x" + std::to_string(remoteImage.height) +
        " (source surface " + FormatName(response.sourceFormat) + ")");

    const ImageStats remoteStats = Measure(remoteImage, options.blackThreshold);
    if (remoteStats.uniform) checks.Fail("remote frame is a single flat colour - capture produced nothing");
    else checks.Pass("remote frame has content");

    if (response.late) checks.Warn("agent was already past the capture instant when the request arrived - raise --lead");
    if (static_cast<CaptureMethod>(response.method) == CaptureMethod::Gdi) {
        checks.Warn("remote frame came from the GDI fallback - no present timestamp, alignment is unverified");
    }

    if (!overlayOk) {
        LogErr("capture: local overlay capture failed: %s", overlayError.c_str());
        return false;
    }
    if (!overlayError.empty()) checks.Warn("overlay capture: " + overlayError);

    const Image& overlayImage = overlayCapture.image;
    if (!overlayImage.valid()) {
        LogErr("capture: overlay frame is invalid");
        return false;
    }
    checks.Pass("overlay frame captured on monitor " + std::to_string(overlayTarget.index) +
        " (" + ToUtf8(overlayTarget.device) + ") " +
        std::to_string(overlayImage.width) + "x" + std::to_string(overlayImage.height) +
        " (source surface " + FormatName(overlayCapture.sourceFormat) + ")");

    const ImageStats overlayStats = Measure(overlayImage, options.blackThreshold);
    char ratioText[64];
    snprintf(ratioText, sizeof(ratioText), "%.4f", overlayStats.blackRatio);

    if (overlayStats.uniform) {
        checks.Fail(std::string("overlay frame is a single flat colour - nothing was rendered on that monitor"));
    } else if (options.minBlackRatio > 0.0 && overlayStats.blackRatio < options.minBlackRatio) {
        checks.Fail("overlay frame is only " + std::string(ratioText) +
            " black - that looks like a normal desktop, not the overlay feed (wrong monitor?)");
    } else if (options.minBlackRatio > 0.0) {
        char thresholdText[32];
        snprintf(thresholdText, sizeof(thresholdText), "%.4f", options.minBlackRatio);
        checks.Pass("overlay frame is " + std::string(ratioText) + " black (--min-black " + thresholdText + ")");
    }

    if (!options.screenMerge) {
        const KeyBreakdown keying = MeasureKey(overlayImage, options.lowKey, options.highKey);
        char keyText[192];
        snprintf(keyText, sizeof(keyText),
            "key --key-low %d --key-high %d: %.1f%% of the overlay drops out, %.1f%% comes through solid, %.1f%% is faded",
            options.lowKey, options.highKey, keying.keyedOut * 100.0, keying.opaque * 100.0, keying.soft * 100.0);

        if (keying.soft > 0.02) {
            checks.Warn(std::string(keyText) + " - narrow the gap between the two to stop fading it");
        } else {
            checks.Pass(keyText);
        }
    }

    const int64_t remoteCaptureUs = ToControllerUs(sync, response.captureUs);
    const int64_t overlayCaptureUs = TicksToUs(overlayCapture.captureTicks);

    const double remoteCaptureSkewMs = (remoteCaptureUs - targetUs) / 1000.0;
    const double overlayCaptureSkewMs = (overlayCaptureUs - overlayTargetUs) / 1000.0;
    const double pairDeltaMs = (overlayCaptureUs - remoteCaptureUs) / 1000.0 - options.overlayDelayMs;

    const double remoteAgeMs = response.presentUs ? (response.captureUs - response.presentUs) / 1000.0 : -1.0;
    const double overlayAgeMs = overlayCapture.presentTicks
        ? TicksToUs(overlayCapture.captureTicks - overlayCapture.presentTicks) / 1000.0
        : -1.0;

    char timing[256];

    snprintf(timing, sizeof(timing), "remote PC captured %+.2f ms from the agreed instant", remoteCaptureSkewMs);
    if (remoteCaptureSkewMs < -options.maxSkewMs || remoteCaptureSkewMs > options.maxSkewMs) checks.Fail(timing);
    else checks.Pass(timing);

    snprintf(timing, sizeof(timing), "this PC captured %+.2f ms from its own instant (target %+d ms)",
        overlayCaptureSkewMs, options.overlayDelayMs);
    if (overlayCaptureSkewMs < -options.maxSkewMs || overlayCaptureSkewMs > options.maxSkewMs) checks.Fail(timing);
    else checks.Pass(timing);

    snprintf(timing, sizeof(timing), "the two captures are %+.2f ms apart after the %d ms overlay delay",
        pairDeltaMs, options.overlayDelayMs);
    if (pairDeltaMs < -options.maxSkewMs || pairDeltaMs > options.maxSkewMs) checks.Fail(timing);
    else checks.Pass(timing);

    if (remoteAgeMs >= 0.0) {
        snprintf(timing, sizeof(timing), "remote frame was %.1f ms old at capture", remoteAgeMs);
        if (remoteAgeMs > options.maxAgeMs) {
            checks.Warn(std::string(timing) + " - that display was not updating, check that display is actually updating");
        } else {
            checks.Pass(timing);
        }

        if (response.presentUs > response.captureUs + 2000) {
            checks.Fail("remote frame was presented after the capture instant - it is from the wrong side of the target");
        }
    } else {
        checks.Warn("remote frame carried no present timestamp - its age is unknown");
    }

    if (overlayAgeMs >= 0.0) {
        snprintf(timing, sizeof(timing), "overlay frame was %.1f ms old at capture", overlayAgeMs);
        if (overlayAgeMs > options.maxAgeMs) {
            checks.Warn(std::string(timing) + " - the overlay was not redrawing");
        } else {
            checks.Pass(timing);
        }
    } else {
        checks.Warn("overlay frame carried no present timestamp - its age is unknown");
    }

    if (remoteImage.width != overlayImage.width || remoteImage.height != overlayImage.height) {
        checks.Warn("resolutions differ - the overlay frame will be scaled to the remote frame");
    } else {
        checks.Pass("both frames are the same resolution");
    }

    checks.Print();

    const std::wstring stamp = TimestampForFilename();
    const std::wstring sourceDir = checks.failed ? options.outDir + L"\\rejected" : options.outDir + L"\\sources";

    if (checks.failed && !options.keepRejected) {
        LogErr("capture rejected - nothing written");
        return false;
    }

    if (!EnsureDirectory(options.outDir) || !EnsureDirectory(sourceDir)) {
        LogErr("cannot create output folder %s", ToUtf8(options.outDir).c_str());
        return false;
    }

    std::string saveError;
    SavePng(sourceDir + L"\\" + stamp + L"_remote.png", remoteImage, saveError);
    SavePng(sourceDir + L"\\" + stamp + L"_overlay.png", overlayImage, saveError);

    std::string json = "{\n";
    json += "  \"stamp\": \"" + ToUtf8(stamp) + "\",\n";
    json += "  \"accepted\": " + std::string(checks.failed ? "false" : "true") + ",\n";
    json += "  \"clock_offset_us\": " + std::to_string(sync.offsetUs) + ",\n";
    json += "  \"clock_rtt_us\": " + std::to_string(sync.rttUs) + ",\n";
    json += "  \"lead_ms\": " + std::to_string(options.leadMs) + ",\n";
    json += "  \"overlay_delay_ms\": " + std::to_string(options.overlayDelayMs) + ",\n";
    json += "  \"remote_capture_skew_ms\": " + std::to_string(remoteCaptureSkewMs) + ",\n";
    json += "  \"overlay_capture_skew_ms\": " + std::to_string(overlayCaptureSkewMs) + ",\n";
    json += "  \"pair_delta_ms\": " + std::to_string(pairDeltaMs) + ",\n";
    json += "  \"remote_frame_age_ms\": " + std::to_string(remoteAgeMs) + ",\n";
    json += "  \"overlay_frame_age_ms\": " + std::to_string(overlayAgeMs) + ",\n";
    json += "  \"remote_monitor\": " + std::to_string(response.monitorIndex) + ",\n";
    json += "  \"remote_device\": \"" + JsonEscape(ToUtf8(response.device)) + "\",\n";
    json += "  \"remote_method\": \"" + std::string(response.method == 0 ? "duplication" : "gdi") + "\",\n";
    json += "  \"remote_surface_format\": \"" + std::string(FormatName(response.sourceFormat)) + "\",\n";
    json += "  \"overlay_surface_format\": \"" + std::string(FormatName(overlayCapture.sourceFormat)) + "\",\n";
    json += "  \"overlay_monitor\": " + std::to_string(overlayTarget.index) + ",\n";
    json += "  \"overlay_device\": \"" + JsonEscape(ToUtf8(overlayTarget.device)) + "\",\n";
    json += "  \"overlay_method\": \"" + std::string(overlayCapture.usedGdi ? "gdi" : "duplication") + "\",\n";
    json += "  \"overlay_black_ratio\": " + std::to_string(overlayStats.blackRatio) + ",\n";
    json += "  \"checks\": [\n";
    for (size_t i = 0; i < checks.lines.size(); ++i) {
        json += "    \"" + JsonEscape(checks.lines[i]) + "\"" + (i + 1 < checks.lines.size() ? ",\n" : "\n");
    }
    json += "  ]\n}\n";

    WriteSidecar(sourceDir + L"\\" + stamp + L"_capture.json", json);

    if (checks.failed) {
        LogErr("capture rejected - sources kept in %s", ToUtf8(sourceDir).c_str());
        return false;
    }

    const Image merged = options.screenMerge
        ? MergeScreen(remoteImage, overlayImage)
        : MergeLumaKey(remoteImage, overlayImage, options.lowKey, options.highKey);

    if (!merged.valid()) {
        LogErr("merge produced nothing");
        return false;
    }

    const std::wstring mergedPath = options.outDir + L"\\" + stamp + L"_merged.png";
    if (!SavePng(mergedPath, merged, saveError)) {
        LogErr("cannot save %s: %s", ToUtf8(mergedPath).c_str(), saveError.c_str());
        return false;
    }

    LogInfo("saved %s", ToUtf8(mergedPath).c_str());
    return true;
}

void Session::Close() {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    linkDead = false;
}

void Session::MarkDead(const char* reason) {
    LogErr("link to the agent failed: %s", reason && *reason ? reason : "unknown");
    linkDead = true;
}

bool Session::Connect() {
    Close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* resolved = nullptr;
    const std::string portText = std::to_string(options.port);

    if (getaddrinfo(options.host.c_str(), portText.c_str(), &hints, &resolved) != 0 || !resolved) {
        LogErr("cannot resolve %s", options.host.c_str());
        return false;
    }

    sock = socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(resolved);
        LogErr("socket() failed");
        return false;
    }

    const bool connected = connect(sock, resolved->ai_addr, static_cast<int>(resolved->ai_addrlen)) != SOCKET_ERROR;
    freeaddrinfo(resolved);

    if (!connected) {
        LogErr("cannot connect to %s:%u - %s", options.host.c_str(),
            static_cast<unsigned>(options.port), ErrorText(WSAGetLastError()).c_str());
        Close();
        return false;
    }

    ConfigureSocket(sock);

    HelloPayload hello{ kProtocolVersion, 0 };
    if (!SendMsg(sock, Msg::Hello, &hello, sizeof(hello))) {
        LogErr("handshake: send failed");
        Close();
        return false;
    }

    Msg type{};
    std::vector<uint8_t> payload;
    std::string error;

    if (!RecvMsg(sock, type, payload, error) || type != Msg::HelloAck || payload.size() != sizeof(HelloAckPayload)) {
        LogErr("handshake: %s", error.empty() ? "unexpected reply" : error.c_str());
        Close();
        return false;
    }

    HelloAckPayload ack{};
    memcpy(&ack, payload.data(), sizeof(ack));

    AuthPayload auth{};
    ComputeAuthMac(key, ack.nonce, auth.mac);

    if (!SendMsg(sock, Msg::Auth, &auth, sizeof(auth))) {
        LogErr("auth: send failed");
        Close();
        return false;
    }

    if (!RecvMsg(sock, type, payload, error)) {
        LogErr("auth: %s", error.c_str());
        Close();
        return false;
    }

    if (type != Msg::AuthOk) {
        const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
        LogErr("auth rejected by the agent: %s", text.empty() ? "no reason given" : text.c_str());
        Close();
        return false;
    }

    if (!RunClockSync(sock, 15, sync, error)) {
        LogErr("%s", error.c_str());
        Close();
        return false;
    }

    LogInfo("connected to %s:%u - clock offset %+.3f ms, round trip %.3f ms",
        options.host.c_str(), static_cast<unsigned>(options.port),
        sync.offsetUs / 1000.0, sync.rttUs / 1000.0);

    return true;
}

bool Session::EnsureConnected() {
    if (sock != INVALID_SOCKET && !linkDead) return true;

    if (sock != INVALID_SOCKET) LogWarn("reconnecting to the agent");

    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (Connect()) return true;
        if (attempt < 3) Sleep(400);
    }

    LogErr("the agent is not answering - is screenfuse_agent.exe still running on %s?", options.host.c_str());
    return false;
}

bool Session::Heartbeat() {
    if (sock == INVALID_SOCKET || linkDead) return false;

    std::string error;
    ClockSync fresh;

    if (!RunClockSync(sock, 3, fresh, error)) {
        MarkDead(error.c_str());
        return false;
    }

    const int64_t movedUs = fresh.offsetUs - sync.offsetUs;
    sync = fresh;

    if (movedUs > 5000 || movedUs < -5000) {
        LogWarn("clock offset moved %+.1f ms since the last check", movedUs / 1000.0);
    }

    return true;
}

bool ResolveOverlayMonitor(Session& session) {
    const std::vector<MonitorTarget> monitors = EnumerateMonitors();
    if (monitors.empty()) {
        LogErr("no monitors attached to this PC");
        return false;
    }

    if (session.options.overlayMonitor >= 0) {
        const MonitorTarget* target = FindMonitorByIndex(monitors, session.options.overlayMonitor);
        if (!target) {
            LogErr("monitor index %d does not exist here (try --list)", session.options.overlayMonitor);
            return false;
        }
        session.overlayTarget = *target;
        LogInfo("overlay monitor: [%d] %s %dx%d (from --overlay-monitor)",
            target->index, ToUtf8(target->device).c_str(), target->width(), target->height());
        return true;
    }

    if (!session.options.overlayWindow.empty()) {
        const HWND overlay = FindOverlayWindow(session.options.overlayWindow);
        if (!overlay) {
            LogErr("no window matches --overlay-window \"%s\" - start it first, or name the monitor with --overlay-monitor",
                ToUtf8(session.options.overlayWindow).c_str());
            return false;
        }

        const HMONITOR handle = MonitorFromWindow(overlay, MONITOR_DEFAULTTONULL);
        const MonitorTarget* target = handle ? FindMonitorByHandle(monitors, handle) : nullptr;

        if (!target) {
            LogErr("that window is not on any enumerable monitor");
            return false;
        }

        session.overlayTarget = *target;
        LogInfo("overlay monitor: [%d] %s %dx%d (found from --overlay-window)",
            target->index, ToUtf8(target->device).c_str(), target->width(), target->height());
        return true;
    }

    const MonitorTarget* target = PrimaryMonitor(monitors);
    if (!target) {
        LogErr("no usable monitor here");
        return false;
    }

    session.overlayTarget = *target;
    LogInfo("overlay monitor: [%d] %s %dx%d (primary - use --overlay-monitor or --overlay-window "
        "if the overlay is on a different display; --list shows them)",
        target->index, ToUtf8(target->device).c_str(), target->width(), target->height());
    return true;
}

void Disconnect(Session& session) {
    if (session.sock == INVALID_SOCKET) return;

    if (!session.options.keepAgent && !session.linkDead) {
        LogInfo("telling the agent to shut down");
        SendMsg(session.sock, Msg::Shutdown, nullptr, 0);

        Msg type{};
        std::vector<uint8_t> payload;
        std::string error;
        if (RecvMsg(session.sock, type, payload, error) && type == Msg::Bye) LogInfo("agent acknowledged");
    }

    session.Close();
}

char WaitForCommand(bool hotkeyRegistered, DWORD timeoutMs) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

    const int64_t deadlineTicks = QpcTicks() + UsToTicks(static_cast<int64_t>(timeoutMs) * 1000);

    for (;;) {
        const int64_t remainUs = TicksToUs(deadlineTicks - QpcTicks());
        if (remainUs <= 0) return 0;

        const DWORD result = MsgWaitForMultipleObjects(1, &input, FALSE,
            static_cast<DWORD>(remainUs / 1000), QS_ALLINPUT);

        if (result == WAIT_TIMEOUT) return 0;

        if (result == WAIT_OBJECT_0) {
            INPUT_RECORD records[16]{};
            DWORD count = 0;
            if (!ReadConsoleInputW(input, records, 16, &count)) return 'q';

            for (DWORD i = 0; i < count; ++i) {
                if (records[i].EventType != KEY_EVENT || !records[i].Event.KeyEvent.bKeyDown) continue;

                const wchar_t ch = records[i].Event.KeyEvent.uChar.UnicodeChar;
                const WORD vk = records[i].Event.KeyEvent.wVirtualKeyCode;

                if (ch == L'q' || ch == L'Q' || vk == VK_ESCAPE) return 'q';
                if (ch == L'c' || ch == L'C' || vk == VK_RETURN) return 'c';
            }
            continue;
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (hotkeyRegistered && message.message == WM_HOTKEY) return 'c';
        }
    }
}

struct StreamState {
    LatestFrame remoteFrames;

    FrameRing overlayFrames{ 12 };

    std::atomic<int> targetWidth{ 0 };
    std::atomic<int> targetHeight{ 0 };

    std::atomic<bool> stop{ false };
    std::atomic<bool> linkLost{ false };
    std::atomic<bool> overlayFailed{ false };
    std::atomic<bool> netDone{ false };
    std::atomic<uint64_t> decodeFailures{ 0 };
};

void StreamNetThreadBody(Session& session, StreamState& state) {
    ComScope com;
    WicFactory wic;

    if (!wic.ok()) {
        LogErr("stream: WIC is unavailable, cannot decode frames");
        state.linkLost = true;
        return;
    }

    RateMeter meter;

    while (!state.stop) {
        Msg type{};
        std::vector<uint8_t> payload;
        std::string error;

        const RecvStatus status = RecvMsgEx(session.sock, type, payload, error);

        if (status == RecvStatus::Idle) {
            if (!state.stop) LogErr("stream: no frames and no heartbeat from the agent");
            state.linkLost = true;
            return;
        }

        if (status != RecvStatus::Ok) {
            if (!state.stop) LogErr("stream: %s", error.c_str());
            state.linkLost = true;
            return;
        }

        if (type == Msg::StreamStatus) {
            if (payload.size() >= sizeof(StreamStatusPayload)) {
                StreamStatusPayload statusPayload{};
                memcpy(&statusPayload, payload.data(), sizeof(statusPayload));

                const std::string text(reinterpret_cast<const char*>(payload.data()) + sizeof(statusPayload),
                    payload.size() - sizeof(statusPayload));

                if (statusPayload.status == kStreamFailed) {
                    LogErr("stream: the agent stopped: %s", text.empty() ? "no reason given" : text.c_str());
                    state.linkLost = true;
                    return;
                }

                if (statusPayload.status == kStreamStopped) return;
            }
            continue;
        }

        if (type == Msg::Error) {
            const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
            LogWarn("stream: agent says: %s", text.c_str());
            continue;
        }

        if (type != Msg::StreamFrame || payload.size() < sizeof(StreamFrameHeader)) continue;

        StreamFrameHeader header{};
        memcpy(&header, payload.data(), sizeof(header));

        if (header.flags & kStreamFlagHeartbeat) continue;

        const uint8_t* body = payload.data() + sizeof(header);
        const size_t bodyBytes = payload.size() - sizeof(header);

        if (bodyBytes != header.payloadBytes) {
            state.decodeFailures++;
            continue;
        }

        auto image = std::make_shared<Image>();

        if (static_cast<WireFormat>(header.wireFormat) == WireFormat::RawBgra) {
            const size_t expected = static_cast<size_t>(header.width) * header.height * 4;
            if (bodyBytes != expected) {
                state.decodeFailures++;
                continue;
            }
            image->width = header.width;
            image->height = header.height;
            image->pixels.assign(body, body + bodyBytes);
        } else {
            std::string decodeError;
            if (!DecodeImageBytes(wic.get(), body, bodyBytes, *image, decodeError)) {
                state.decodeFailures++;
                continue;
            }
        }

        TimedFrame frame;
        frame.image = image;
        frame.captureUs = ToControllerUs(session.sync, header.captureUs);
        frame.presentUs = header.presentUs ? ToControllerUs(session.sync, header.presentUs) : 0;
        frame.arrivedUs = NowUs();
        frame.wireBytes = payload.size();

        meter.Add(payload.size());
        meter.AddLatency(frame.arrivedUs - frame.captureUs);

        state.remoteFrames.Publish(std::move(frame));

        double perSecond = 0.0, megabits = 0.0, latencyMs = 0.0;
        if (meter.Elapsed(NowUs(), 5000000, perSecond, megabits, latencyMs)) {
            LogInfo("stream in: %.1f fps, %.1f Mbit/s, %.0f ms behind the remote PC%s",
                perSecond, megabits, latencyMs,
                state.decodeFailures.load() ? " (some frames failed to decode)" : "");
        }
    }
}

void StreamNetThread(Session& session, StreamState& state) {
    StreamNetThreadBody(session, state);
    state.netDone = true;
}

void StreamOverlayThread(MonitorTarget target, StreamState& state, int fps) {
    ContinuousCapture capture;
    std::string error;

    if (!capture.Open(target, error)) {
        LogErr("stream: cannot capture the overlay monitor: %s", error.c_str());
        state.overlayFailed = true;
        return;
    }
    if (!error.empty()) LogWarn("stream: %s", error.c_str());

    const int captureFps = fps * 2 > 120 ? 120 : fps * 2;
    const int64_t intervalTicks = UsToTicks(1000000 / captureFps);
    int64_t dueTicks = QpcTicks();

    Image scaled;

    while (!state.stop) {
        Image image;
        bool fresh = false;
        int64_t presentTicks = 0;
        int64_t captureTicks = 0;

        int64_t waitUs = TicksToUs(dueTicks - QpcTicks());
        if (waitUs < 0) waitUs = 0;
        if (waitUs > 50000) waitUs = 50000;

        if (!capture.Next(static_cast<int>(waitUs / 1000), image, presentTicks, captureTicks, fresh, error)) {
            LogWarn("stream: overlay capture: %s", error.c_str());
            if (!capture.Open(target, error)) {
                LogErr("stream: overlay capture is gone: %s", error.c_str());
                state.overlayFailed = true;
                return;
            }
            Sleep(50);
            continue;
        }

        if (!fresh || QpcTicks() < dueTicks) continue;

        dueTicks = QpcTicks() + intervalTicks;

        const int targetWidth = state.targetWidth.load();
        const int targetHeight = state.targetHeight.load();

        std::shared_ptr<const Image> stored;
        if (targetWidth > 0 && targetHeight > 0 && (image.width != targetWidth || image.height != targetHeight)) {
            ScaleToInto(image, scaled, targetWidth, targetHeight);
            stored = std::make_shared<const Image>(scaled);
        } else {
            stored = std::make_shared<const Image>(std::move(image));
        }

        TimedFrame frame;
        frame.image = std::move(stored);
        frame.captureUs = TicksToUs(captureTicks);
        frame.presentUs = presentTicks ? TicksToUs(presentTicks) : 0;
        frame.arrivedUs = frame.captureUs;

        state.overlayFrames.Push(std::move(frame));
    }
}

char PollConsole() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE) return 0;

    for (;;) {
        DWORD pending = 0;
        if (!GetNumberOfConsoleInputEvents(input, &pending) || pending == 0) return 0;

        INPUT_RECORD records[16]{};
        DWORD count = 0;
        if (!ReadConsoleInputW(input, records, 16, &count)) return 0;

        for (DWORD i = 0; i < count; ++i) {
            if (records[i].EventType != KEY_EVENT || !records[i].Event.KeyEvent.bKeyDown) continue;

            const wchar_t ch = records[i].Event.KeyEvent.uChar.UnicodeChar;
            const WORD vk = records[i].Event.KeyEvent.wVirtualKeyCode;

            if (ch == L'q' || ch == L'Q' || vk == VK_ESCAPE) return 'q';
            if (ch == L's' || ch == L'S' || vk == VK_RETURN) return 's';
        }
    }
}

void SaveStreamStill(const Options& options, const Image& merged) {
    if (!merged.valid()) return;

    auto copy = std::make_shared<Image>(merged);
    const std::wstring outDir = options.outDir;

    std::thread([copy, outDir] {
        ComScope com;
        if (!EnsureDirectory(outDir)) {
            LogErr("cannot create %s", ToUtf8(outDir).c_str());
            return;
        }

        const std::wstring path = outDir + L"\\" + TimestampForFilename() + L"_stream.png";
        std::string error;

        if (SavePng(path, *copy, error)) LogInfo("saved %s", ToUtf8(path).c_str());
        else LogErr("cannot save %s: %s", ToUtf8(path).c_str(), error.c_str());
    }).detach();
}

bool StartStream(Session& session, int& width, int& height) {
    StreamStartPayload start{};
    start.monitorIndex = session.options.remoteMonitor;
    start.fps = static_cast<uint16_t>(session.options.streamFps);
    start.scalePercent = static_cast<uint16_t>(session.options.streamScale);
    start.quality = static_cast<uint8_t>(session.options.streamQuality);
    start.wireFormat = static_cast<uint8_t>(session.options.streamRaw ? WireFormat::RawBgra : WireFormat::Jpeg);

    if (!SendMsg(session.sock, Msg::StreamStart, &start, sizeof(start))) {
        session.MarkDead("cannot ask the agent to start streaming");
        return false;
    }

    for (;;) {
        Msg type{};
        std::vector<uint8_t> payload;
        std::string error;

        if (!RecvMsg(session.sock, type, payload, error)) {
            session.MarkDead(error.c_str());
            return false;
        }

        if (type == Msg::Pong) continue;

        if (type != Msg::StreamStatus || payload.size() < sizeof(StreamStatusPayload)) {
            LogErr("stream: unexpected reply from the agent");
            return false;
        }

        StreamStatusPayload status{};
        memcpy(&status, payload.data(), sizeof(status));

        const std::string text(reinterpret_cast<const char*>(payload.data()) + sizeof(status),
            payload.size() - sizeof(status));

        if (status.status != kStreamStarted) {
            LogErr("stream: the agent refused: %s", text.empty() ? "no reason given" : text.c_str());
            return false;
        }

        if (!text.empty()) LogWarn("stream: %s", text.c_str());

        width = status.width;
        height = status.height;

        LogInfo("streaming remote monitor %d (%s) %dx%d over %s%s",
            status.monitorIndex, ToUtf8(status.device).c_str(), status.width, status.height,
            status.method == static_cast<uint8_t>(CaptureMethod::Gdi) ? "GDI" : "duplication",
            session.options.streamRaw ? ", raw BGRA" : ", JPEG");
        return true;
    }
}

void StopStream(Session& session) {
    if (session.sock == INVALID_SOCKET || session.linkDead) return;
    SendMsg(session.sock, Msg::StreamStop, nullptr, 0);
}

bool RunStreamOnce(Session& session, OutputWindow& window, bool& windowReady, bool hotkeyRegistered) {
    int width = 0, height = 0;
    if (!StartStream(session, width, height)) return session.linkDead;

    StreamState state;
    std::thread net([&] { StreamNetThread(session, state); });
    std::thread overlay([&] { StreamOverlayThread(session.overlayTarget, state, session.options.streamFps); });

    Compositor compositor;
    Image merged;

    const int64_t overlayDelayUs = static_cast<int64_t>(session.options.overlayDelayMs) * 1000;
    const int64_t frameTicks = UsToTicks(1000000 / session.options.streamFps);
    int64_t dueTicks = QpcTicks();

    uint64_t lastRemoteSerial = 0;
    uint64_t lastOverlaySerial = 0;
    bool reportedKey = false;
    bool quit = false;
    RateMeter outMeter;
    int64_t waitingSinceUs = NowUs();
    bool warnedNoFrames = false;

    while (!state.linkLost && !state.overlayFailed) {
        if (windowReady) {
            window.Pump();
            if (window.closed()) { quit = true; break; }
            if (window.TakeHotkey()) SaveStreamStill(session.options, merged);
        } else {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (hotkeyRegistered && message.message == WM_HOTKEY) SaveStreamStill(session.options, merged);
            }
        }

        const char command = PollConsole();
        if (command == 'q') { quit = true; break; }
        if (command == 's') SaveStreamStill(session.options, merged);

        const TimedFrame remote = state.remoteFrames.Get();

        if (!remote.valid()) {
            if (!warnedNoFrames && NowUs() - waitingSinceUs > 5000000) {
                LogWarn("stream: no frames from the remote PC yet");
                warnedNoFrames = true;
            }
            Sleep(2);
            continue;
        }

        state.targetWidth = remote.image->width;
        state.targetHeight = remote.image->height;

        const uint64_t overlaySerial = state.overlayFrames.serial();
        const bool anythingNew = remote.serial != lastRemoteSerial || overlaySerial != lastOverlaySerial;

        const int64_t now = QpcTicks();
        if (!anythingNew || now < dueTicks) {
            Sleep(1);
            continue;
        }

        const TimedFrame overlayFrame = state.overlayFrames.Nearest(remote.captureUs + overlayDelayUs);
        if (!overlayFrame.valid()) {
            Sleep(2);
            continue;
        }

        if (!reportedKey && !session.options.screenMerge) {
            reportedKey = true;
            const KeyBreakdown key = MeasureKey(*overlayFrame.image, session.options.lowKey, session.options.highKey);
            LogInfo("key --key-low %d --key-high %d: %.1f%% of the overlay drops out, "
                "%.1f%% comes through solid, %.1f%% is faded%s",
                session.options.lowKey, session.options.highKey,
                key.keyedOut * 100.0, key.opaque * 100.0, key.soft * 100.0,
                key.soft > 0.02 ? " - narrow the gap between the two to stop fading it" : "");
        }

        if (!compositor.Merge(*remote.image, *overlayFrame.image, merged,
            session.options.screenMerge, session.options.lowKey, session.options.highKey)) {
            Sleep(2);
            continue;
        }

        if (!windowReady) {
            const std::vector<MonitorTarget> monitors = EnumerateMonitors();
            const MonitorTarget* fullscreenOn = session.options.streamOutMonitor >= 0
                ? FindMonitorByIndex(monitors, session.options.streamOutMonitor)
                : nullptr;

            if (session.options.streamOutMonitor >= 0 && !fullscreenOn) {
                LogWarn("monitor %d does not exist here - showing the output in a window instead",
                    session.options.streamOutMonitor);
            }

            std::string windowError;
            if (!window.Create(session.options.streamTitle, merged.width, merged.height, fullscreenOn, windowError)) {
                LogErr("%s", windowError.c_str());
                quit = true;
                break;
            }

            windowReady = true;

            if (window.clientWidth() == merged.width && window.clientHeight() == merged.height) {
                LogInfo("output window \"%s\" is up at %dx%d - point a window capture at it",
                    ToUtf8(session.options.streamTitle).c_str(), merged.width, merged.height);
            } else {
                LogInfo("output window \"%s\" is up at %dx%d - the %dx%d frame does not fit on this "
                    "display, so a capture of the window gets the smaller size (--stream-monitor <n> "
                    "shows it full size instead)",
                    ToUtf8(session.options.streamTitle).c_str(),
                    window.clientWidth(), window.clientHeight(), merged.width, merged.height);
            }
        }

        std::string presentError;
        if (!window.Present(merged, presentError)) {
            LogErr("stream: %s", presentError.c_str());
            quit = true;
            break;
        }

        lastRemoteSerial = remote.serial;
        lastOverlaySerial = overlaySerial;
        dueTicks = now + frameTicks;

        outMeter.Add();
        double perSecond = 0.0, megabits = 0.0, latencyMs = 0.0;
        if (outMeter.Elapsed(TicksToUs(now), 5000000, perSecond, megabits, latencyMs)) {
            LogInfo("stream out: %.1f fps merged, overlay paired %+.1f ms from the remote frame",
                perSecond, (overlayFrame.captureUs - remote.captureUs) / 1000.0);
        }
    }

    state.stop = true;

    if (!state.linkLost) StopStream(session);

    for (int waited = 0; waited < 200 && !state.netDone; ++waited) Sleep(10);

    if (!state.netDone) {
        session.linkDead = true;
        shutdown(session.sock, SD_BOTH);
    }

    net.join();
    overlay.join();

    if (state.linkLost) session.linkDead = true;

    if (state.overlayFailed) return false;
    return !quit;
}

int RunStreamMode(Session& session, bool hotkeyRegistered) {
    OutputWindow window;
    bool windowReady = false;

    LogInfo("streaming: [s] or Enter saves the merged frame, [q] or Esc quits%s",
        hotkeyRegistered ? ", Ctrl+Shift+P saves from anywhere" : "");

    for (;;) {
        if (!session.EnsureConnected()) {
            LogWarn("waiting for the agent to come back");
            Sleep(2000);

            if (PollConsole() == 'q') break;
            continue;
        }

        if (!RunStreamOnce(session, window, windowReady, hotkeyRegistered)) break;

        LogWarn("the stream stopped - reconnecting");
        Sleep(500);
    }

    window.Destroy();
    Disconnect(session);
    return 0;
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
        ListMonitors(options.overlayWindow);
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
        LogInfo("copy this file to the remote PC next to screenfuse_agent.exe");
        return 0;
    }

    if (!options.remergeRemote.empty() || !options.remergeOverlay.empty()) {
        if (options.remergeRemote.empty() || options.remergeOverlay.empty()) {
            LogErr("--remerge-remote and --remerge-overlay must be given together");
            return 1;
        }

        Image remote, overlay;
        std::string loadError;

        if (!LoadPng(options.remergeRemote, remote, loadError) || !LoadPng(options.remergeOverlay, overlay, loadError)) {
            LogErr("%s", loadError.c_str());
            return 1;
        }

        if (!options.screenMerge) {
            const KeyBreakdown keying = MeasureKey(overlay, options.lowKey, options.highKey);
            LogInfo("key --key-low %d --key-high %d: %.1f%% of the overlay drops out, "
                "%.1f%% comes through solid, %.1f%% is faded%s",
                options.lowKey, options.highKey,
                keying.keyedOut * 100.0, keying.opaque * 100.0, keying.soft * 100.0,
                keying.soft > 0.02 ? " - narrow the gap between the two to stop fading it" : "");
        }

        const Image merged = options.screenMerge
            ? MergeScreen(remote, overlay)
            : MergeLumaKey(remote, overlay, options.lowKey, options.highKey);

        if (!merged.valid()) {
            LogErr("merge produced nothing");
            return 1;
        }

        if (!EnsureDirectory(options.outDir)) {
            LogErr("cannot create %s", ToUtf8(options.outDir).c_str());
            return 1;
        }

        const std::wstring path = options.outDir + L"\\" + TimestampForFilename() + L"_remerged.png";
        std::string saveError;
        if (!SavePng(path, merged, saveError)) {
            LogErr("%s", saveError.c_str());
            return 1;
        }

        LogInfo("remote %dx%d + overlay %dx%d, %s merge -> %s",
            remote.width, remote.height, overlay.width, overlay.height,
            options.screenMerge ? "screen" : "luma key",
            ToUtf8(path).c_str());
        return 0;
    }

    if (options.host.empty()) {
        LogErr("--host is required (the remote PC's address)");
        PrintUsage();
        return 1;
    }

    std::vector<uint8_t> key;
    std::string error;
    if (!ReadKeyFile(options.keyPath, key, error)) {
        LogErr("%s", error.c_str());
        LogErr("run with --genkey once, then copy the key file to the remote PC");
        return 1;
    }

    WinsockScope winsock;
    if (!winsock.ok) {
        LogErr("WSAStartup failed");
        return 1;
    }

    Session session;
    session.options = options;
    session.key = key;

    if (!ResolveOverlayMonitor(session)) return 1;
    if (!session.Connect()) {
        session.Close();
        return 1;
    }

    int captured = 0;
    int rejected = 0;

    if (options.once) {
        const bool ok = session.TakeScreenshot();
        Disconnect(session);
        RestoreTimerResolution();
        return ok ? 0 : 1;
    }

    const bool hotkeyRegistered = RegisterHotKey(nullptr, 1, options.hotkeyMods | MOD_NOREPEAT, options.hotkeyVk) != FALSE;
    if (!hotkeyRegistered) LogWarn("could not register the Ctrl+Shift+P hotkey - console keys still work");

    if (options.stream) {
        const int result = RunStreamMode(session, hotkeyRegistered);
        if (hotkeyRegistered) UnregisterHotKey(nullptr, 1);
        RestoreTimerResolution();
        return result;
    }

    LogInfo("ready: [c] or Enter = screenshot, [q] or Esc = quit%s",
        hotkeyRegistered ? ", or Ctrl+Shift+P from anywhere" : "");

    int64_t nextCheckUs = NowUs() + 5000000;

    for (;;) {
        const char command = WaitForCommand(hotkeyRegistered, 1000);
        if (command == 'q') break;

        if (command == 0) {
            const int64_t nowUs = NowUs();
            if (nowUs < nextCheckUs) continue;

            if (session.linkDead || session.sock == INVALID_SOCKET) {
                nextCheckUs = nowUs + (session.EnsureConnected() ? 5000000 : 30000000);
            } else {
                nextCheckUs = nowUs + (session.Heartbeat() ? 5000000 : 0);
            }

            continue;
        }

        if (!session.EnsureConnected()) {
            rejected++;
            nextCheckUs = NowUs() + 30000000;
            continue;
        }

        bool ok = session.TakeScreenshot();

        if (!ok && session.linkDead && session.EnsureConnected()) ok = session.TakeScreenshot();

        if (ok) captured++;
        else rejected++;

        nextCheckUs = NowUs() + 5000000;
    }

    if (hotkeyRegistered) UnregisterHotKey(nullptr, 1);

    LogInfo("session done: %d saved, %d rejected", captured, rejected);
    Disconnect(session);
    RestoreTimerResolution();
    return 0;
}
