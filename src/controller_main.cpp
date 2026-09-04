#include "controller_core.hpp"
#include "gui_api.hpp"

using namespace screenfuse;
using namespace screenfuse::controller;


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
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    const bool doubleClicked = argc == 1 && gui::LaunchedFromExplorer();

    if (options.gui || (doubleClicked && !options.noGui)) {
        if (doubleClicked && !options.gui) gui::HideConsoleWindow();
        return gui::RunGui(options);
    }

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
        ConsoleControl control;
        const int result = RunStreamMode(session, hotkeyRegistered, control, nullptr, true);
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
