#pragma once
#include "gui_engine.hpp"

namespace screenfuse {
namespace gui {

inline int Clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

inline std::string OptionsJson(const controller::Options& options) {
    JsonBuilder json;
    json.BeginObject()
        .Field("host", options.host)
        .Field("port", static_cast<int>(options.port))
        .Field("keyPath", options.keyPath)
        .Field("outDir", options.outDir)
        .Field("remoteMonitor", options.remoteMonitor)
        .Field("overlayMonitor", options.overlayMonitor)
        .Field("overlayWindow", options.overlayWindow)
        .Field("leadMs", options.leadMs)
        .Field("overlayDelayMs", options.overlayDelayMs)
        .Field("maxSkewMs", options.maxSkewMs)
        .Field("maxAgeMs", options.maxAgeMs)
        .Field("lowKey", options.lowKey)
        .Field("highKey", options.highKey)
        .Field("blackThreshold", options.blackThreshold)
        .Field("minBlackRatio", options.minBlackRatio)
        .Field("screenMerge", options.screenMerge)
        .Field("png", options.png)
        .Field("keepAgent", options.keepAgent)
        .Field("keepRejected", options.keepRejected)
        .Field("streamFps", options.streamFps)
        .Field("streamQuality", options.streamQuality)
        .Field("streamScale", options.streamScale)
        .Field("streamOutMonitor", options.streamOutMonitor)
        .Field("streamRaw", options.streamRaw)
        .Field("streamTitle", options.streamTitle)
        .Field("audio", options.audio)
        .Field("audioBufferMs", options.audioBufferMs)
        .Field("record", options.record)
        .Field("recordBitrateMbit", options.recordBitrateMbit)
        .EndObject();
    return json.take();
}

inline void MergeOptions(const JsonValue& body, controller::Options& options,
    std::vector<std::string>& warnings) {

    if (body.Has("host")) options.host = body.GetString("host", options.host);
    if (body.Has("port")) {
        const int port = Clamp(body.GetInt("port", options.port), 1, 65535);
        options.port = static_cast<uint16_t>(port);
    }
    if (body.Has("keyPath")) {
        const std::string path = body.GetString("keyPath", "");
        if (!path.empty()) options.keyPath = ToWide(path);
    }
    if (body.Has("outDir")) {
        const std::string path = body.GetString("outDir", "");
        if (!path.empty()) options.outDir = ToWide(path);
    }

    if (body.Has("remoteMonitor")) options.remoteMonitor = body.GetInt("remoteMonitor", options.remoteMonitor);
    if (body.Has("overlayMonitor")) options.overlayMonitor = body.GetInt("overlayMonitor", options.overlayMonitor);
    if (body.Has("overlayWindow")) options.overlayWindow = ToWide(body.GetString("overlayWindow", ""));

    if (body.Has("leadMs")) {
        options.leadMs = body.GetInt("leadMs", options.leadMs);
        if (options.leadMs < 50) {
            options.leadMs = 50;
            warnings.push_back("lead cannot be shorter than 50 ms - using 50");
        }
    }
    if (body.Has("overlayDelayMs")) options.overlayDelayMs = Clamp(body.GetInt("overlayDelayMs", options.overlayDelayMs), -1000, 1000);
    if (body.Has("maxSkewMs")) options.maxSkewMs = Clamp(body.GetInt("maxSkewMs", options.maxSkewMs), 1, 5000);
    if (body.Has("maxAgeMs")) options.maxAgeMs = Clamp(body.GetInt("maxAgeMs", options.maxAgeMs), 1, 60000);

    if (body.Has("lowKey")) options.lowKey = Clamp(body.GetInt("lowKey", options.lowKey), 0, 255);
    if (body.Has("highKey")) options.highKey = Clamp(body.GetInt("highKey", options.highKey), 0, 255);
    if (options.highKey <= options.lowKey) {
        options.highKey = options.lowKey + 1 > 255 ? 255 : options.lowKey + 1;
        warnings.push_back("key-high has to be above key-low - raised it to " + std::to_string(options.highKey));
    }

    if (body.Has("blackThreshold")) options.blackThreshold = Clamp(body.GetInt("blackThreshold", options.blackThreshold), 0, 255);
    if (body.Has("minBlackRatio")) {
        double ratio = body.GetNumber("minBlackRatio", options.minBlackRatio);
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        options.minBlackRatio = ratio;
    }

    if (body.Has("screenMerge")) options.screenMerge = body.GetBool("screenMerge", options.screenMerge);
    if (body.Has("png")) options.png = body.GetBool("png", options.png);
    if (body.Has("keepAgent")) options.keepAgent = body.GetBool("keepAgent", options.keepAgent);
    if (body.Has("keepRejected")) options.keepRejected = body.GetBool("keepRejected", options.keepRejected);

    if (body.Has("streamFps")) options.streamFps = Clamp(body.GetInt("streamFps", options.streamFps), 1, 240);
    if (body.Has("streamQuality")) options.streamQuality = Clamp(body.GetInt("streamQuality", options.streamQuality), 1, 100);
    if (body.Has("streamScale")) options.streamScale = Clamp(body.GetInt("streamScale", options.streamScale), 10, 100);
    if (body.Has("streamOutMonitor")) options.streamOutMonitor = body.GetInt("streamOutMonitor", options.streamOutMonitor);
    if (body.Has("streamRaw")) options.streamRaw = body.GetBool("streamRaw", options.streamRaw);
    if (body.Has("streamTitle")) {
        const std::string title = body.GetString("streamTitle", "");
        options.streamTitle = title.empty() ? L"ScreenFuse Output" : ToWide(title);
    }

    if (body.Has("audio")) options.audio = body.GetBool("audio", options.audio);
    if (body.Has("audioBufferMs")) {
        options.audioBufferMs = Clamp(body.GetInt("audioBufferMs", options.audioBufferMs), 20, 2000);
        options.audioBufferSet = true;
    }
    if (body.Has("record")) options.record = body.GetBool("record", options.record);
    if (body.Has("recordBitrateMbit")) {
        const int bitrate = body.GetInt("recordBitrateMbit", options.recordBitrateMbit);
        if (bitrate == 0) {
            options.recordBitrateMbit = 0;
        } else {
            const int clamped = Clamp(bitrate, 2, 50);
            if (clamped != bitrate) {
                warnings.push_back("recording bitrate has to be 0 or 2-50 Mbit/s - using " + std::to_string(clamped));
            }
            options.recordBitrateMbit = clamped;
        }
    }

    if (options.audio && options.streamRaw && !options.audioBufferSet) options.audioBufferMs = 150;
}

inline CommandResult Engine::ApplyLiveOptions(const JsonValue& body) {
    std::vector<std::string> warnings;
    controller::Options updated;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        updated = options_;
    }

    const std::wstring beforeWindow = updated.overlayWindow;
    const int beforeMonitor = updated.overlayMonitor;

    MergeOptions(body, updated, warnings);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        options_ = updated;
        if (!snapshot_.streaming) session_.options = updated;
    }

    if (updated.overlayWindow != beforeWindow || updated.overlayMonitor != beforeMonitor) {
        RefreshMonitors();
    }
    RefreshKeyState();

    JsonBuilder json;
    json.BeginObject()
        .Field("ok", true)
        .Key("options").Raw(OptionsJson(updated))
        .Key("warnings").BeginArray();
    for (const std::string& warning : warnings) json.Str(warning);
    json.EndArray().EndObject();

    CommandResult result;
    result.body = json.take();
    return result;
}

inline std::string Engine::StateJson() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const Snapshot& snap = snapshot_;

    JsonBuilder json;
    json.BeginObject()
        .Field("mode", snap.mode)
        .Field("busy", snap.busy)
        .Field("connected", snap.connected)
        .Field("streaming", snap.streaming)
        .Field("linkDead", snap.linkDead)
        .Field("protocolVersion", static_cast<int>(kProtocolVersion))
        .Field("hotkey", snap.hotkey);

    json.Key("key").BeginObject()
        .Field("path", snap.keyPath)
        .Field("present", snap.keyPresent)
        .Field("fingerprint", snap.keyFingerprint)
        .EndObject();

    json.Key("agent").BeginObject()
        .Field("host", options_.host)
        .Field("port", static_cast<int>(options_.port))
        .Field("clockOffsetMs", snap.clockOffsetMs)
        .Field("rttMs", snap.rttMs)
        .EndObject();

    json.Key("overlay").BeginObject()
        .Field("resolved", snap.overlayResolved)
        .Field("index", snap.overlayIndex)
        .Field("device", snap.overlayDevice)
        .Field("width", snap.overlayWidth)
        .Field("height", snap.overlayHeight)
        .Field("from", snap.overlayFrom)
        .EndObject();

    json.Key("monitors").BeginArray();
    for (const MonitorTarget& monitor : snap.monitors) {
        json.BeginObject()
            .Field("index", monitor.index)
            .Field("device", monitor.device)
            .Field("adapter", monitor.adapter)
            .Field("width", monitor.width())
            .Field("height", monitor.height())
            .Field("left", static_cast<int>(monitor.rect.left))
            .Field("top", static_cast<int>(monitor.rect.top))
            .Field("primary", monitor.primary)
            .Field("hasOverlayWindow", monitor.index == snap.overlayWindowMonitor)
            .EndObject();
    }
    json.EndArray();

    json.Key("options").Raw(OptionsJson(options_));

    json.Key("stream").BeginObject()
        .Field("active", snap.streaming)
        .Field("width", snap.streamWidth)
        .Field("height", snap.streamHeight)
        .Field("device", snap.streamDevice)
        .Field("gdi", snap.streamGdi)
        .Field("inFps", snap.inFps)
        .Field("megabits", snap.megabits)
        .Field("latencyMs", snap.latencyMs)
        .Field("outFps", snap.outFps)
        .Field("pairingMs", snap.pairingMs)
        .EndObject();

    json.Key("recording").BeginObject()
        .Field("armed", snap.recordingArmed)
        .Field("available", snap.recorderPresent)
        .EndObject();

    json.Key("lastCapture").BeginObject()
        .Field("have", snap.haveCapture)
        .Field("accepted", snap.lastCapture.accepted)
        .Field("path", snap.lastCapture.mergedPath)
        .Field("sourceDir", snap.lastCapture.sourceDir)
        .Field("stamp", snap.lastCaptureStamp)
        .Field("width", snap.lastCapture.width)
        .Field("height", snap.lastCapture.height)
        .Field("remoteSkewMs", snap.lastCapture.remoteSkewMs)
        .Field("overlaySkewMs", snap.lastCapture.overlaySkewMs)
        .Field("pairDeltaMs", snap.lastCapture.pairDeltaMs)
        .Field("overlayBlackRatio", snap.lastCapture.overlayBlackRatio)
        .Key("checks").BeginArray();
    for (const std::string& check : snap.lastCapture.checks) json.Str(check);
    json.EndArray().EndObject();

    json.Key("counters").BeginObject()
        .Field("saved", snap.saved)
        .Field("rejected", snap.rejected)
        .EndObject();

    json.Field("previewSeq", static_cast<long long>(preview_.seq()));
    json.EndObject();
    return json.take();
}

inline std::string Engine::HelpJson() const {
    JsonBuilder json;
    json.BeginObject().Key("flags").BeginObject();
    for (const auto& entry : HelpTable()) json.Field(entry.first, entry.second);
    json.EndObject().EndObject();
    return json.take();
}

inline bool Engine::PathIsServable(const std::wstring& path, std::wstring& full) const {
    if (path.empty()) return false;

    std::wstring outDir;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        outDir = options_.outDir;
    }

    wchar_t resolved[MAX_PATH * 2]{};
    wchar_t root[MAX_PATH * 2]{};

    if (!GetFullPathNameW(path.c_str(), MAX_PATH * 2, resolved, nullptr)) return false;
    if (!GetFullPathNameW(outDir.c_str(), MAX_PATH * 2, root, nullptr)) return false;

    std::wstring candidate = resolved;
    std::wstring prefix = root;
    if (!prefix.empty() && prefix.back() != L'\\') prefix.push_back(L'\\');

    if (candidate.size() < prefix.size()) return false;
    if (CompareStringOrdinal(candidate.c_str(), static_cast<int>(prefix.size()),
            prefix.c_str(), static_cast<int>(prefix.size()), TRUE) != CSTR_EQUAL) {
        return false;
    }

    if (candidate.find(L"..") != std::wstring::npos) return false;

    full = candidate;
    return true;
}

inline std::string Engine::CapturesJson() const {
    std::wstring outDir;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        outDir = options_.outDir;
    }

    struct Entry {
        std::wstring name;
        std::wstring path;
        long long bytes = 0;
        long long modifiedMs = 0;
        std::string kind;
    };

    std::vector<Entry> found;

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = outDir + L"\\*.png";
    HANDLE handle = FindFirstFileW(pattern.c_str(), &data);

    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            Entry entry;
            entry.name = data.cFileName;
            entry.path = outDir + L"\\" + entry.name;
            entry.bytes = (static_cast<long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

            ULARGE_INTEGER written{};
            written.LowPart = data.ftLastWriteTime.dwLowDateTime;
            written.HighPart = data.ftLastWriteTime.dwHighDateTime;
            entry.modifiedMs = static_cast<long long>(written.QuadPart / 10000ULL);

            if (entry.name.find(L"_merged") != std::wstring::npos) entry.kind = "merged";
            else if (entry.name.find(L"_stream") != std::wstring::npos) entry.kind = "stream";
            else if (entry.name.find(L"_remerged") != std::wstring::npos) entry.kind = "remerged";
            else continue;

            found.push_back(std::move(entry));
        } while (FindNextFileW(handle, &data));

        FindClose(handle);
    }

    std::sort(found.begin(), found.end(), [](const Entry& left, const Entry& right) {
        return left.modifiedMs > right.modifiedMs;
    });

    if (found.size() > 36) found.resize(36);

    JsonBuilder json;
    json.BeginObject().Key("files").BeginArray();
    for (const Entry& entry : found) {
        json.BeginObject()
            .Field("name", entry.name)
            .Field("path", entry.path)
            .Field("bytes", entry.bytes)
            .Field("modifiedMs", entry.modifiedMs)
            .Field("kind", entry.kind)
            .EndObject();
    }
    json.EndArray().EndObject();
    return json.take();
}

inline bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));

    size_t done = 0;
    while (done < out.size()) {
        DWORD read = 0;
        const DWORD want = static_cast<DWORD>(out.size() - done > 1048576 ? 1048576 : out.size() - done);
        if (!ReadFile(file, out.data() + done, want, &read, nullptr) || read == 0) {
            CloseHandle(file);
            return false;
        }
        done += read;
    }

    CloseHandle(file);
    return true;
}

class GuiApp {
public:
    int Run(controller::Options options);

private:
    void Route(const HttpRequest& request, HttpResponse& response);
    bool RouteStream(const HttpRequest& request, SOCKET sock);
    bool Authorised(const HttpRequest& request) const;

    Engine engine_;
    HttpServer server_;
    std::string token_;

    std::atomic<int64_t> lastContactUs_{ 0 };
};

inline bool GuiApp::Authorised(const HttpRequest& request) const {
    const std::string header = request.Header("X-SF-Token");
    if (!header.empty()) return header == token_;

    return request.Param("token") == token_;
}

inline bool GuiApp::RouteStream(const HttpRequest& request, SOCKET sock) {
    if (request.path != "/api/preview.mjpeg") return false;
    if (!Authorised(request)) return false;

    lastContactUs_ = NowUs();

    static const char boundary[] = "screenfuseframe";
    if (!WriteMultipartPreamble(sock, boundary)) return true;

    uint64_t seen = 0;
    std::vector<uint8_t> frame;

    if (engine_.preview().Latest(frame)) {
        if (!WriteMultipartJpeg(sock, boundary, frame.data(), frame.size())) return true;
    }
    seen = engine_.preview().seq();

    while (!engine_.quitting()) {
        if (!engine_.preview().WaitNewer(seen, frame, 1000)) {
            if (engine_.quitting()) break;
            continue;
        }
        if (frame.empty()) continue;
        if (!WriteMultipartJpeg(sock, boundary, frame.data(), frame.size())) break;
    }

    return true;
}

inline void GuiApp::Route(const HttpRequest& request, HttpResponse& response) {
    const std::string& path = request.path;

    if (path == "/" || path == "/index.html") {
        lastContactUs_ = NowUs();
        response.contentType = "text/html; charset=utf-8";
        response.body = IndexHtml();
        return;
    }
    if (path == "/style.css") {
        response.contentType = "text/css; charset=utf-8";
        response.body = StyleCss();
        return;
    }
    if (path == "/app.js") {
        response.contentType = "text/javascript; charset=utf-8";
        response.body = AppJs();
        return;
    }
    if (path == "/favicon.ico") {
        response.status = 204;
        response.contentType = "image/x-icon";
        return;
    }

    if (path.rfind("/api/", 0) != 0) {
        response.status = 404;
        response.body = "{\"error\":\"no such page\"}";
        return;
    }

    if (!Authorised(request)) {
        response.status = 401;
        response.body = "{\"error\":\"this window is not the one that was opened for this session\"}";
        return;
    }

    lastContactUs_ = NowUs();

    const bool post = request.method == "POST";

    if (path == "/api/ping") {
        response.body = "{\"ok\":true}";
        return;
    }

    if (path == "/api/state" && !post) {
        response.body = engine_.StateJson();
        return;
    }

    if (path == "/api/help" && !post) {
        response.body = engine_.HelpJson();
        return;
    }

    if (path == "/api/captures" && !post) {
        response.body = engine_.CapturesJson();
        return;
    }

    if (path == "/api/log" && !post) {
        uint64_t since = 0;
        const std::string sinceText = request.Param("since");
        if (!sinceText.empty()) since = strtoull(sinceText.c_str(), nullptr, 10);

        const std::vector<LogEntry> lines = engine_.log().Since(since, 400);

        JsonBuilder json;
        json.BeginObject()
            .Field("seq", static_cast<long long>(engine_.log().seq()))
            .Key("lines").BeginArray();
        for (const LogEntry& entry : lines) {
            json.BeginObject()
                .Field("seq", static_cast<long long>(entry.seq))
                .Field("time", entry.time)
                .Field("level", entry.level)
                .Field("text", entry.text)
                .EndObject();
        }
        json.EndArray().EndObject();
        response.body = json.take();
        return;
    }

    if (path == "/api/preview.jpg" && !post) {
        std::vector<uint8_t> frame;
        if (!engine_.preview().Latest(frame)) {
            response.status = 404;
            response.body = "{\"error\":\"no preview frame yet\"}";
            return;
        }
        response.contentType = "image/jpeg";
        response.body.assign(reinterpret_cast<const char*>(frame.data()), frame.size());
        return;
    }

    if (path == "/api/file" && !post) {
        std::wstring full;
        if (!engine_.PathIsServable(ToWide(request.Param("path")), full)) {
            response.status = 403;
            response.body = "{\"error\":\"that file is not in the output folder\"}";
            return;
        }

        std::string bytes;
        if (!ReadWholeFile(full, bytes)) {
            response.status = 404;
            response.body = "{\"error\":\"cannot read that file\"}";
            return;
        }

        const bool jpeg = full.size() > 4 &&
            (full.compare(full.size() - 4, 4, L".jpg") == 0 || full.compare(full.size() - 5, 5, L".jpeg") == 0);
        response.contentType = jpeg ? "image/jpeg" : "image/png";
        response.body = std::move(bytes);
        return;
    }

    if (!post) {
        response.status = 405;
        response.body = "{\"error\":\"that endpoint wants a POST\"}";
        return;
    }

    JsonValue body;
    if (!request.body.empty()) {
        std::string parseError;
        if (!ParseJson(request.body, body, parseError)) {
            response.status = 400;
            JsonBuilder json;
            json.BeginObject().Field("error", "the request body is not valid JSON: " + parseError).EndObject();
            response.body = json.take();
            return;
        }
    }

    if (path == "/api/pick") {
        const std::string kind = body.GetString("kind", "file");
        const std::string purpose = body.GetString("purpose", "");
        const controller::Options options = engine_.options();

        std::wstring picked;
        if (kind == "folder") {
            picked = PickFolder(L"Choose the output folder", options.outDir);
        } else if (purpose == "key") {
            picked = PickFile(L"Choose the shared key file", ExeDir(), L"Key file", L"*.key");
        } else {
            picked = PickFile(L"Choose a saved frame", options.outDir + L"\\sources", L"PNG image", L"*.png");
        }

        JsonBuilder json;
        json.BeginObject().Field("path", picked).EndObject();
        response.body = json.take();
        return;
    }

    if (path == "/api/open") {
        const std::string target = body.GetString("target", "outdir");
        const controller::Options options = engine_.options();

        std::wstring where;
        if (target == "outdir") {
            where = options.outDir;
            EnsureDirectory(where);
        } else if (target == "key") {
            where = options.keyPath;
        } else {
            std::wstring full;
            if (!engine_.PathIsServable(ToWide(body.GetString("path", "")), full)) {
                response.status = 403;
                response.body = "{\"error\":\"that path is not in the output folder\"}";
                return;
            }
            where = full;
        }

        if (!RevealInExplorer(where)) {
            response.status = 500;
            response.body = "{\"error\":\"Explorer would not open that\"}";
            return;
        }
        return;
    }

    if (path == "/api/quit") {
        engine_.RequestQuit();
        return;
    }

    if (path == "/api/disconnect" && !engine_.busy().empty()) engine_.CancelInFlight();

    Cmd kind;
    if (path == "/api/connect") kind = Cmd::Connect;
    else if (path == "/api/disconnect") kind = Cmd::Disconnect;
    else if (path == "/api/snapshot") kind = Cmd::Snapshot;
    else if (path == "/api/stream/start") kind = Cmd::StreamStart;
    else if (path == "/api/stream/stop") kind = Cmd::StreamStop;
    else if (path == "/api/stream/still") kind = Cmd::SaveStill;
    else if (path == "/api/record/toggle") kind = Cmd::RecordToggle;
    else if (path == "/api/genkey") kind = Cmd::GenerateKey;
    else if (path == "/api/remerge") kind = Cmd::Remerge;
    else if (path == "/api/options") kind = Cmd::ApplyOptions;
    else if (path == "/api/refresh") kind = Cmd::Refresh;
    else {
        response.status = 404;
        response.body = "{\"error\":\"no such endpoint\"}";
        return;
    }

    if (kind == Cmd::Connect || kind == Cmd::Snapshot || kind == Cmd::StreamStart) {
        const std::string busy = engine_.busy();
        if (!busy.empty()) {
            response.status = 409;
            JsonBuilder json;
            json.BeginObject().Field("error", "already " + busy + " - wait for that to finish").EndObject();
            response.body = json.take();
            return;
        }

        std::string refusal;
        if (engine_.streaming()) {
            refusal = "stop the stream first";
        } else if (kind == Cmd::Connect && engine_.connected()) {
            refusal = "already connected";
        } else if (kind != Cmd::Connect && !engine_.connected()) {
            refusal = "connect to the agent first";
        }

        if (!refusal.empty()) {
            response.status = 409;
            JsonBuilder json;
            json.BeginObject().Field("error", refusal).EndObject();
            response.body = json.take();
            return;
        }

        engine_.MarkBusy(kind == Cmd::Connect ? "connecting"
            : (kind == Cmd::Snapshot ? "capturing" : "starting"));
        engine_.Fire(kind, std::move(body));
        return;
    }

    const CommandResult result = engine_.Submit(kind, std::move(body));
    response.status = result.status;
    response.body = result.body;
}

inline int GuiApp::Run(controller::Options options) {
    uint8_t random[16]{};
    if (!RandomBytes(random, sizeof(random))) {
        ShowMessageDialog(L"ScreenFuse", L"The system random number generator refused, so the control panel cannot be secured. Nothing was started.", true);
        return 1;
    }
    token_ = ToHex(random, sizeof(random));

    InstanceLock instance;
    if (!instance.Acquire()) {
        uint16_t otherPort = 0;
        std::string otherToken;
        if (instance.ReadPublished(otherPort, otherToken) && otherPort != 0) {
            AppWindow window;
            std::string error;
            const std::string url = "http://127.0.0.1:" + std::to_string(otherPort) + "/?token=" + otherToken;
            window.Open(url, L"ScreenFuse", error);
            window.Detach();
            return 0;
        }

        ShowMessageDialog(L"ScreenFuse", L"ScreenFuse is already running.", false);
        return 0;
    }

    engine_.SetOptions(options);

    g_logHookUser = &engine_;
    g_logHook = [](const char* level, const char* stamp, const char* body, void* user) {
        static_cast<Engine*>(user)->log().Append(level, stamp, body);
    };

    WinsockScope winsock;
    if (!winsock.ok) {
        ShowMessageDialog(L"ScreenFuse", L"Windows sockets would not start, so nothing can run.", true);
        return 1;
    }

    std::string error;
    if (!server_.Start(0, error)) {
        ShowMessageDialog(L"ScreenFuse", ToWide("The control panel could not open its local port: " + error), true);
        return 1;
    }

    server_.SetHandler([this](const HttpRequest& request, HttpResponse& response) {
        Route(request, response);
    });
    server_.SetStreamHandler([this](const HttpRequest& request, SOCKET sock) {
        return RouteStream(request, sock);
    });

    instance.Publish(server_.port(), token_);

    engine_.Launch();

    const std::string url = "http://127.0.0.1:" + std::to_string(server_.port()) + "/?token=" + token_;
    LogInfo("control panel listening on 127.0.0.1:%u", static_cast<unsigned>(server_.port()));

    AppWindow window;
    if (!window.Open(url, L"ScreenFuse", error)) {
        LogErr("%s", error.c_str());
        ShowMessageDialog(L"ScreenFuse",
            ToWide("The control panel is running at " + url + " but no browser could be opened for it."), true);
    }

    const int64_t openedUs = NowUs();
    bool pageArrived = false;

    while (!engine_.quitting() && !controller::g_consoleQuit) {
        Sleep(200);

        const int64_t last = lastContactUs_.load();
        if (last != 0) pageArrived = true;

        if (!pageArrived) {
            if (NowUs() - openedUs > 60000000) {
                LogErr("no browser window ever reached the control panel - giving up");
                break;
            }
            continue;
        }

        if (NowUs() - last > 8000000) {
            LogInfo("the control panel window is gone");
            break;
        }
    }

    LogInfo("shutting down");

    server_.Stop();
    engine_.Shutdown();
    window.Close();

    g_logHook = nullptr;
    g_logHookUser = nullptr;

    RestoreTimerResolution();
    return 0;
}

inline int RunGui(controller::Options options) {
    GuiApp app;
    return app.Run(std::move(options));
}

} // namespace gui
} // namespace screenfuse
