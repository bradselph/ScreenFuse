#pragma once
#include "controller_core.hpp"

#include "gui_assets.hpp"
#include "gui_http.hpp"
#include "gui_json.hpp"
#include "gui_shell.hpp"

#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace screenfuse {
namespace gui {

constexpr int kPreviewIntervalMs = 100;
constexpr int kPreviewMaxWidth = 1100;
constexpr int kPreviewQuality = 68;
constexpr size_t kLogCapacity = 4000;
constexpr int kCommandWaitMs = 45000;

struct LogEntry {
    uint64_t seq = 0;
    std::string time;
    std::string level;
    std::string text;
};

class LogRing {
public:
    void Append(const char* level, const char* stamp, const char* body) {
        std::lock_guard<std::mutex> lock(mutex_);

        LogEntry entry;
        entry.seq = ++seq_;
        entry.time = stamp ? stamp : "";
        entry.level = level ? level : "INFO";
        entry.text = body ? body : "";

        entries_.push_back(std::move(entry));
        while (entries_.size() > kLogCapacity) entries_.pop_front();
    }

    uint64_t seq() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return seq_;
    }

    std::vector<LogEntry> Since(uint64_t after, size_t limit) const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<LogEntry> out;
        for (const LogEntry& entry : entries_) {
            if (entry.seq <= after) continue;
            out.push_back(entry);
            if (out.size() >= limit) break;
        }
        return out;
    }

private:
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    uint64_t seq_ = 0;
};

class PreviewBuffer {
public:
    void Publish(std::vector<uint8_t> jpeg) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            frame_ = std::move(jpeg);
            seq_++;
        }
        cv_.notify_all();
    }

    void Clear() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            frame_.clear();
            seq_++;
        }
        cv_.notify_all();
    }

    uint64_t seq() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return seq_;
    }

    bool Latest(std::vector<uint8_t>& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frame_.empty()) return false;
        out = frame_;
        return true;
    }

    bool WaitNewer(uint64_t& seen, std::vector<uint8_t>& out, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);

        const bool got = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [&] { return closed_ || seq_ != seen; });

        if (!got || closed_) return false;

        seen = seq_;
        if (frame_.empty()) return false;

        out = frame_;
        return true;
    }

    void Close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<uint8_t> frame_;
    uint64_t seq_ = 0;
    bool closed_ = false;
};

enum class Cmd {
    Connect,
    Disconnect,
    Snapshot,
    StreamStart,
    StreamStop,
    SaveStill,
    RecordToggle,
    GenerateKey,
    Remerge,
    ApplyOptions,
    Refresh,
    Quit
};

struct CommandResult {
    int status = 200;
    std::string body = "{\"ok\":true}";
};

struct Command {
    Cmd kind = Cmd::Refresh;
    JsonValue body;
    std::shared_ptr<std::promise<CommandResult>> reply;
};

inline CommandResult ErrorResult(int status, const std::string& message) {
    JsonBuilder json;
    json.BeginObject().Field("error", message).EndObject();

    CommandResult result;
    result.status = status;
    result.body = json.take();
    return result;
}

struct Snapshot {
    std::string mode = "idle";
    std::string busy;
    bool connected = false;
    bool streaming = false;
    bool linkDead = false;

    std::wstring keyPath;
    bool keyPresent = false;
    std::string keyFingerprint;

    double clockOffsetMs = 0.0;
    double rttMs = 0.0;

    bool overlayResolved = false;
    int overlayIndex = -1;
    std::wstring overlayDevice;
    int overlayWidth = 0;
    int overlayHeight = 0;
    std::string overlayFrom = "primary";

    std::vector<MonitorTarget> monitors;
    int overlayWindowMonitor = -1;

    int streamWidth = 0;
    int streamHeight = 0;
    std::string streamDevice;
    bool streamGdi = false;
    double inFps = 0.0;
    double megabits = 0.0;
    double latencyMs = 0.0;
    double outFps = 0.0;
    double pairingMs = 0.0;

    bool recordingArmed = false;
    bool recorderPresent = false;

    bool haveCapture = false;
    controller::CaptureReport lastCapture;
    std::string lastCaptureStamp;

    int saved = 0;
    int rejected = 0;

    bool hotkey = false;
};

inline std::string FormatDouble(double value) {
    if (!(value == value)) return "0";
    char text[64];
    snprintf(text, sizeof(text), "%.3f", value);
    return text;
}

inline const std::vector<std::pair<const char*, const char*>>& HelpTable() {
    static const std::vector<std::pair<const char*, const char*>> table = {
        { "--genkey", "Creates a new random key file at the path above. It refuses to overwrite one that already exists, so delete the old one first if a fresh key is really wanted. Copy the file to the remote PC afterwards." },
        { "--host", "The remote PC's address - the machine running screenfuse_agent.exe." },
        { "--port", "The port the agent listens on. Both sides must agree. Default 20787." },
        { "--key", "The shared key file. Both PCs need the same one; it is what stops anything else on the network asking that machine for its screen." },
        { "--out", "Where merged screenshots, source frames and recordings are written." },
        { "--remote-monitor", "Which display to capture on the remote PC. Leave on the agent's default unless it has more than one." },
        { "--overlay-monitor", "Which display here carries the overlay - the graphics on a black field that go on top." },
        { "--overlay-window", "Find the overlay display by the window drawing on it instead of by index. Matches window classes first, then titles. Survives monitors being rearranged." },
        { "--lead", "How far ahead the shared capture instant is scheduled. Raise it if captures are rejected for skew. Minimum 50." },
        { "--overlay-delay", "Capture the overlay this much later than the remote frame, for an overlay that trails what it describes. Start at 0 and raise until it lines up." },
        { "--max-skew", "Reject a capture whose two halves land further apart than this." },
        { "--max-age", "Warn when a returned frame is older than this, which means that display had stopped updating." },
        { "--key-low", "Overlay luma at or below this vanishes. This is the knob to raise when the overlay's black is not quite black and its background washes over the picture underneath." },
        { "--key-high", "Overlay luma at or above this comes through solid. Everything between the two is faded, so a wide gap dims the overlay rather than softening it - keep it close to key-low." },
        { "--black-threshold", "The luma a pixel must be under to count as black when measuring how black the overlay frame is." },
        { "--min-black", "Refuse a capture unless the overlay frame is at least this black, to catch the wrong monitor. 0 turns it off." },
        { "--merge", "Screen blend keeps every overlay pixel, including dark ones a luma key would drop, at the cost of brightening what is underneath." },
        { "--png", "Have the agent send PNG instead of raw pixels: about 38x less network, about 100 ms more CPU there. Use it on Wi-Fi." },
        { "--keep-agent", "Leave the agent running when this program exits." },
        { "--no-keep-rejected", "Write out the source frames of captures that failed validation, so they can be looked at." },
        { "--stream-fps", "Frames a second, both captured and shown." },
        { "--stream-quality", "JPEG quality the remote PC encodes with, 1-100." },
        { "--stream-scale", "Percent of the remote PC's resolution to send. Costs less picture than quality does." },
        { "--stream-raw", "Send uncompressed frames. Wired gigabit only - about 14 fps at 1080p." },
        { "--high-quality", "Keeps coloured text and thin lines sharp in the stream and records at a higher bitrate, at the cost of more network and larger files." },
        { "--stream-monitor", "Show the merged output fullscreen on a display here instead of in a window. Never point it at the overlay's own monitor." },
        { "--stream-title", "Title of the output window, which is what OBS or Discord will list as a source." },
        { "--audio", "Also capture what the remote PC is playing and play it here. Crosses the network in the clear, like the pixels." },
        { "--audio-buffer", "How much audio is held here to ride out network jitter. Raise it if audio crackles; latency rises with it." },
        { "--record", "Record the merged stream to an MP4 in the output folder. Every start is a new file." },
        { "--record-bitrate", "H.264 bitrate in Mbit/s, 2-50. 0 lets it scale with the frame, about 6 at 1080p30." },
    };
    return table;
}

class Engine : public controller::ControlSource, public controller::StreamObserver {
public:
    Engine() = default;
    ~Engine() { Shutdown(); }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    LogRing& log() { return log_; }
    PreviewBuffer& preview() { return preview_; }

    void SetOptions(const controller::Options& options) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        options_ = options;
    }

    std::string busy() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return snapshot_.busy;
    }

    bool connected() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return snapshot_.connected;
    }

    bool streaming() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return snapshot_.streaming;
    }

    void MarkBusy(const std::string& what) { SetBusy(what); }

    void CancelInFlight() { session_.Cancel(); }

    controller::Options options() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return options_;
    }

    void Launch() {
        thread_ = std::thread([this] { Body(); });
    }

    void RequestQuit() {
        quitting_ = true;
        session_.Cancel();
        Push(Cmd::Quit, JsonValue{}, nullptr);
        cv_.notify_all();
    }

    void Shutdown() {
        RequestQuit();
        if (thread_.joinable()) thread_.join();
        preview_.Close();
    }

    bool quitting() const { return quitting_.load(); }

    CommandResult Submit(Cmd kind, JsonValue body) {
        if (quitting_) return ErrorResult(503, "ScreenFuse is shutting down");

        auto reply = std::make_shared<std::promise<CommandResult>>();
        std::future<CommandResult> answer = reply->get_future();

        Push(kind, std::move(body), reply);

        if (answer.wait_for(std::chrono::milliseconds(kCommandWaitMs)) != std::future_status::ready) {
            return ErrorResult(504, "the engine did not answer in time");
        }

        return answer.get();
    }

    void Fire(Cmd kind, JsonValue body) {
        if (quitting_) return;
        Push(kind, std::move(body), nullptr);
    }

   char Poll() override {
        if (pendingControl_) {
            const char pending = pendingControl_;
            pendingControl_ = 0;
            return pending;
        }

        for (;;) {
            Command command;
            if (!TryPop(command)) return 0;

            switch (command.kind) {
            case Cmd::SaveStill:
                Answer(command, CommandResult{});
                return 's';
            case Cmd::RecordToggle:
                Answer(command, CommandResult{});
                return 'r';
            case Cmd::StreamStop:
                Answer(command, CommandResult{});
                return 'q';
            case Cmd::Quit:
                quitting_ = true;
                Answer(command, CommandResult{});
                return 'q';
            case Cmd::Disconnect:
                disconnectAfterStream_ = true;
                Answer(command, CommandResult{});
                return 'q';
            case Cmd::Refresh:
                RefreshMonitors();
                Answer(command, CommandResult{});
                continue;
            case Cmd::ApplyOptions:
                Answer(command, ApplyLiveOptions(command.body));
                continue;
            default:
                Answer(command, ErrorResult(409, "not while the stream is running - stop it first"));
                continue;
            }
        }
    }

    bool ShouldQuit() override {
        return quitting_ || controller::g_consoleQuit.load();
    }

    void OnRecorder(class StreamRecorder* recorder) override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        recorder_ = recorder;
        snapshot_.recorderPresent = recorder != nullptr;
        snapshot_.recordingArmed = recorder ? recorder->armed() : false;
    }

    void OnStreamStarted(int width, int height, const std::string& device, bool gdi) override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.streamWidth = width;
        snapshot_.streamHeight = height;
        snapshot_.streamDevice = device;
        snapshot_.streamGdi = gdi;
    }

    void OnMergedFrame(const Image& merged, double pairingMs) override {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            snapshot_.pairingMs = pairingMs;
            snapshot_.recordingArmed = recorder_ ? recorder_->armed() : false;
        }

        const int64_t nowUs = NowUs();
        if (nowUs - lastPreviewUs_ < static_cast<int64_t>(kPreviewIntervalMs) * 1000) return;
        lastPreviewUs_ = nowUs;

        OfferPreview(merged);
    }

    void OnInputRate(double framesPerSecond, double megabits, double latencyMs) override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.inFps = framesPerSecond;
        snapshot_.megabits = megabits;
        snapshot_.latencyMs = latencyMs;
    }

    void OnOutputRate(double framesPerSecond) override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.outFps = framesPerSecond;
    }

    void OnStreamEnded() override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.streamWidth = 0;
        snapshot_.streamHeight = 0;
        snapshot_.inFps = snapshot_.megabits = snapshot_.latencyMs = 0.0;
        snapshot_.outFps = snapshot_.pairingMs = 0.0;
    }

    std::string StateJson() const;
    std::string HelpJson() const;
    std::string CapturesJson() const;

    bool PathIsServable(const std::wstring& path, std::wstring& full) const;

private:
    void Push(Cmd kind, JsonValue body, std::shared_ptr<std::promise<CommandResult>> reply) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            Command command;
            command.kind = kind;
            command.body = std::move(body);
            command.reply = std::move(reply);
            queue_.push_back(std::move(command));
        }
        cv_.notify_all();
    }

    bool TryPop(Command& out) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool WaitPop(Command& out, int timeoutMs) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    static void Answer(Command& command, CommandResult result) {
        if (command.reply) command.reply->set_value(std::move(result));
        command.reply.reset();
    }

    void DrainQueue() {
        std::deque<Command> leftovers;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            leftovers.swap(queue_);
        }
        for (Command& command : leftovers) {
            Answer(command, ErrorResult(503, "ScreenFuse is shutting down"));
        }
    }

    void Body();
    void Dispatch(Command& command);

    CommandResult DoConnect();
    CommandResult DoDisconnect();
    CommandResult DoSnapshot();
    CommandResult DoStream();
    CommandResult DoGenerateKey();
    CommandResult DoRemerge(const JsonValue& body);
    CommandResult ApplyLiveOptions(const JsonValue& body);

    void RefreshMonitors();
    void RefreshKeyState();
    void SetBusy(const std::string& what);
    void OfferPreview(const Image& merged);
    void PreviewEncoderBody();

    LogRing log_;
    PreviewBuffer preview_;

    mutable std::mutex stateMutex_;
    controller::Options options_;
    Snapshot snapshot_;
    class StreamRecorder* recorder_ = nullptr;

    std::mutex queueMutex_;
    std::condition_variable cv_;
    std::deque<Command> queue_;

    controller::Session session_;
    std::vector<uint8_t> key_;

    std::thread thread_;
    std::atomic<bool> quitting_{ false };
    bool hotkeyRegistered_ = false;
    bool disconnectAfterStream_ = false;
    char pendingControl_ = 0;
    int64_t lastPreviewUs_ = 0;

    std::thread previewThread_;
    std::mutex previewMutex_;
    std::condition_variable previewCv_;
    Image previewPending_;
    bool previewHasWork_ = false;
    bool previewStop_ = false;
};

inline void Engine::SetBusy(const std::string& what) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    snapshot_.busy = what;
}

inline void Engine::RefreshMonitors() {
    std::vector<MonitorTarget> monitors = EnumerateMonitors();

    std::wstring windowName;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        windowName = options_.overlayWindow;
    }

    int windowMonitor = -1;
    if (!windowName.empty()) {
        if (HWND found = controller::FindOverlayWindow(windowName)) {
            if (HMONITOR handle = MonitorFromWindow(found, MONITOR_DEFAULTTONULL)) {
                if (const MonitorTarget* target = FindMonitorByHandle(monitors, handle)) {
                    windowMonitor = target->index;
                }
            }
        }
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    snapshot_.monitors = std::move(monitors);
    snapshot_.overlayWindowMonitor = windowMonitor;
}

inline void Engine::RefreshKeyState() {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        path = options_.keyPath;
    }

    std::vector<uint8_t> key;
    std::string error;
    const bool present = ReadKeyFile(path, key, error);

    std::string fingerprint;
    if (present) {
        static const char label[] = "SCREENFUSE-KEY-ID";
        uint8_t mac[kHmacBytes]{};
        if (Hmac(key, reinterpret_cast<const uint8_t*>(label), sizeof(label) - 1, mac)) {
            fingerprint = ToHex(mac, 4);
        }
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    snapshot_.keyPath = path;
    snapshot_.keyPresent = present;
    snapshot_.keyFingerprint = fingerprint;
    if (present) key_ = key;
}

inline void Engine::OfferPreview(const Image& merged) {
    if (!merged.valid()) return;

    std::lock_guard<std::mutex> lock(previewMutex_);
    if (previewHasWork_) return;

    previewPending_ = merged;
    previewHasWork_ = true;
    previewCv_.notify_one();
}

inline void Engine::PreviewEncoderBody() {
    ComScope com;
    WicFactory wic;

    Image source;
    Image scaled;
    std::vector<uint8_t> jpeg;
    JpegEncoder encoder;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(previewMutex_);
            previewCv_.wait(lock, [&] { return previewStop_ || previewHasWork_; });
            if (previewStop_) return;

            std::swap(source, previewPending_);
            previewHasWork_ = false;
        }

        if (!wic.ok() || !source.valid()) continue;

        const Image* toEncode = &source;
        if (source.width > kPreviewMaxWidth) {
            const int height = static_cast<int>(
                static_cast<int64_t>(source.height) * kPreviewMaxWidth / source.width);
            ScaleToInto(source, scaled, kPreviewMaxWidth, height > 0 ? height : 1);
            if (scaled.valid()) toEncode = &scaled;
        }

        std::string error;
        if (encoder.Encode(wic.get(), *toEncode, kPreviewQuality, false, jpeg, error)) {
            preview_.Publish(jpeg);
        }
    }
}

inline CommandResult Engine::DoConnect() {
    SetBusy("connecting");

    session_.ClearCancel();

    controller::Options options = this->options();
    if (options.host.empty()) {
        SetBusy("");
        return ErrorResult(400, "the remote PC's address is required");
    }

    RefreshKeyState();

    std::vector<uint8_t> key;
    std::string error;
    if (!ReadKeyFile(options.keyPath, key, error)) {
        SetBusy("");
        LogErr("%s", error.c_str());
        return ErrorResult(400, error + " - generate a key, then copy it to the remote PC");
    }

    session_.options = options;
    session_.key = key;

    if (!controller::ResolveOverlayMonitor(session_)) {
        SetBusy("");
        return ErrorResult(400, "cannot work out which display carries the overlay - pick one, or name the window drawing on it");
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.overlayResolved = true;
        snapshot_.overlayIndex = session_.overlayTarget.index;
        snapshot_.overlayDevice = session_.overlayTarget.device;
        snapshot_.overlayWidth = session_.overlayTarget.width();
        snapshot_.overlayHeight = session_.overlayTarget.height();
        snapshot_.overlayFrom = options.overlayMonitor >= 0
            ? "index"
            : (!options.overlayWindow.empty() ? "window" : "primary");
    }

    const bool ok = session_.Connect();

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.busy.clear();
        snapshot_.connected = ok;
        snapshot_.linkDead = session_.linkDead;
        snapshot_.clockOffsetMs = session_.sync.offsetUs / 1000.0;
        snapshot_.rttMs = session_.sync.rttUs / 1000.0;
        snapshot_.mode = ok ? "connected" : "idle";
    }

    if (!ok) return ErrorResult(502, "cannot reach the agent - check the address, the port and that screenfuse_agent.exe is running");
    return CommandResult{};
}

inline CommandResult Engine::DoDisconnect() {
    SetBusy("stopping");
    controller::Disconnect(session_);
    session_.ClearCancel();

    std::lock_guard<std::mutex> lock(stateMutex_);
    snapshot_.busy.clear();
    snapshot_.connected = false;
    snapshot_.linkDead = false;
    snapshot_.mode = "idle";
    snapshot_.clockOffsetMs = 0.0;
    snapshot_.rttMs = 0.0;
    return CommandResult{};
}

inline CommandResult Engine::DoSnapshot() {
    SetBusy("capturing");

    if (!session_.EnsureConnected()) {
        SetBusy("");
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.connected = false;
        snapshot_.mode = "idle";
        snapshot_.rejected++;
        return ErrorResult(502, "the agent is not answering");
    }

    controller::CaptureReport report;
    bool ok = session_.TakeScreenshot(&report);

    if (!ok && session_.linkDead && session_.EnsureConnected()) {
        report = controller::CaptureReport{};
        ok = session_.TakeScreenshot(&report);
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    snapshot_.busy.clear();
    snapshot_.connected = session_.sock != INVALID_SOCKET && !session_.linkDead;
    snapshot_.mode = snapshot_.connected ? "connected" : "idle";
    snapshot_.haveCapture = !report.checks.empty();
    snapshot_.lastCapture = report;
    snapshot_.lastCaptureStamp = ToUtf8(TimestampForFilename());

    if (ok) snapshot_.saved++;
    else snapshot_.rejected++;

    if (!ok) return ErrorResult(422, "the capture did not pass its checks - the list below says which");
    return CommandResult{};
}

inline CommandResult Engine::DoStream() {
    SetBusy("starting");

    session_.ClearCancel();
    disconnectAfterStream_ = false;
    pendingControl_ = 0;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        session_.options = options_;
        snapshot_.streaming = true;
        snapshot_.mode = "streaming";
        snapshot_.busy.clear();
    }

    if (!session_.overlayTarget.width()) {
        if (!controller::ResolveOverlayMonitor(session_)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            snapshot_.streaming = false;
            snapshot_.mode = snapshot_.connected ? "connected" : "idle";
            return ErrorResult(400, "cannot work out which display carries the overlay");
        }
    }

    controller::RunStreamMode(session_, hotkeyRegistered_, *this, this, false);

    preview_.Clear();

    if (disconnectAfterStream_) {
        disconnectAfterStream_ = false;
        controller::Disconnect(session_);
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    snapshot_.streaming = false;
    snapshot_.connected = session_.sock != INVALID_SOCKET && !session_.linkDead;
    snapshot_.mode = snapshot_.connected ? "connected" : "idle";
    snapshot_.recorderPresent = false;
    snapshot_.recordingArmed = false;
    return CommandResult{};
}

inline CommandResult Engine::DoGenerateKey() {
    const controller::Options options = this->options();

    std::vector<uint8_t> key(kKeyBytes);
    std::string error;

    if (!RandomBytes(key.data(), key.size())) {
        return ErrorResult(500, "the system random number generator refused");
    }

    if (!WriteKeyFile(options.keyPath, key, error)) {
        return ErrorResult(409, error);
    }

    LogInfo("wrote %s", ToUtf8(options.keyPath).c_str());
    LogInfo("copy this file to the remote PC, next to screenfuse_agent.exe");

    RefreshKeyState();

    JsonBuilder json;
    json.BeginObject().Field("ok", true).Field("path", options.keyPath).EndObject();

    CommandResult result;
    result.body = json.take();
    return result;
}

inline CommandResult Engine::DoRemerge(const JsonValue& body) {
    const controller::Options options = this->options();

    const std::wstring remotePath = ToWide(body.GetString("remote", ""));
    const std::wstring overlayPath = ToWide(body.GetString("overlay", ""));

    if (remotePath.empty() || overlayPath.empty()) {
        return ErrorResult(400, "pick both a remote frame and an overlay frame");
    }

    SetBusy("remerging");

    Image remote, overlay;
    std::string error;

    if (!LoadPng(remotePath, remote, error) || !LoadPng(overlayPath, overlay, error)) {
        SetBusy("");
        return ErrorResult(400, error);
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
        SetBusy("");
        return ErrorResult(500, "the merge produced nothing");
    }

    if (!EnsureDirectory(options.outDir)) {
        SetBusy("");
        return ErrorResult(500, "cannot create " + ToUtf8(options.outDir));
    }

    const std::wstring path = options.outDir + L"\\" + TimestampForFilename() + L"_remerged.png";
    if (!SavePng(path, merged, error)) {
        SetBusy("");
        return ErrorResult(500, error);
    }

    LogInfo("remote %dx%d + overlay %dx%d, %s merge -> %s",
        remote.width, remote.height, overlay.width, overlay.height,
        options.screenMerge ? "screen" : "luma key", ToUtf8(path).c_str());

    SetBusy("");

    JsonBuilder json;
    json.BeginObject().Field("ok", true).Field("path", path).EndObject();

    CommandResult result;
    result.body = json.take();
    return result;
}

inline void Engine::Body() {
    ComScope com;

    previewThread_ = std::thread([this] { PreviewEncoderBody(); });

    hotkeyRegistered_ = RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'P') != FALSE;
    if (!hotkeyRegistered_) LogWarn("could not register the Ctrl+Shift+P hotkey - the buttons still work");

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.hotkey = hotkeyRegistered_;
    }

    RefreshMonitors();
    RefreshKeyState();

    int64_t nextHeartbeatUs = NowUs() + 5000000;

    while (!ShouldQuit()) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_HOTKEY) Push(Cmd::Snapshot, JsonValue{}, nullptr);
        }

        Command command;
        if (WaitPop(command, 100)) {
            Dispatch(command);
            continue;
        }

        const int64_t nowUs = NowUs();
        if (nowUs < nextHeartbeatUs) continue;

        bool connected = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            connected = snapshot_.connected;
        }

        if (!connected) {
            nextHeartbeatUs = nowUs + 5000000;
            continue;
        }

        const bool alive = session_.linkDead || session_.sock == INVALID_SOCKET
            ? session_.EnsureConnected()
            : session_.Heartbeat();

        nextHeartbeatUs = NowUs() + (alive ? 5000000 : 15000000);

        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.connected = alive;
        snapshot_.linkDead = session_.linkDead;
        snapshot_.mode = alive ? "connected" : "idle";
        snapshot_.clockOffsetMs = session_.sync.offsetUs / 1000.0;
        snapshot_.rttMs = session_.sync.rttUs / 1000.0;
    }

    if (hotkeyRegistered_) UnregisterHotKey(nullptr, 1);

    const controller::Options options = this->options();
    if (!options.keepAgent) controller::Disconnect(session_);
    else session_.Close();

    {
        std::lock_guard<std::mutex> lock(previewMutex_);
        previewStop_ = true;
    }
    previewCv_.notify_all();
    if (previewThread_.joinable()) previewThread_.join();

    DrainQueue();
}

inline void Engine::Dispatch(Command& command) {
    switch (command.kind) {
    case Cmd::Connect:
        Answer(command, DoConnect());
        break;
    case Cmd::Disconnect:
        Answer(command, DoDisconnect());
        break;
    case Cmd::Snapshot:
        Answer(command, DoSnapshot());
        break;
    case Cmd::StreamStart:
        Answer(command, DoStream());
        break;
    case Cmd::StreamStop:
        Answer(command, ErrorResult(409, "no stream is running"));
        break;
    case Cmd::SaveStill:
        Answer(command, ErrorResult(409, "no stream is running - take a screenshot instead"));
        break;
    case Cmd::RecordToggle:
        Answer(command, ErrorResult(409, "recording only runs alongside a stream"));
        break;
    case Cmd::GenerateKey:
        Answer(command, DoGenerateKey());
        break;
    case Cmd::Remerge:
        Answer(command, DoRemerge(command.body));
        break;
    case Cmd::ApplyOptions:
        Answer(command, ApplyLiveOptions(command.body));
        break;
    case Cmd::Refresh:
        RefreshMonitors();
        RefreshKeyState();
        Answer(command, CommandResult{});
        break;
    case Cmd::Quit:
        quitting_ = true;
        Answer(command, CommandResult{});
        break;
    }
}

} // namespace gui
} // namespace screenfuse
