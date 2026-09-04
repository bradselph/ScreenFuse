#pragma once
#include "common.hpp"
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <cstdlib>
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace screenfuse {
namespace gui {

inline bool LaunchedFromExplorer() {
    DWORD processList[16]{};
    return GetConsoleProcessList(processList, 16) == 1;
}

inline void HideConsoleWindow() {
    const HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);
}

inline void ShowMessageDialog(const std::wstring& title, const std::wstring& message, bool error) {
    MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

inline bool EnvPath(const wchar_t* name, std::wstring& out) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;

    out.assign(buffer, length);
    return true;
}

inline bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline std::wstring StripQuotes(const std::wstring& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && text[begin] == L' ') begin++;
    while (end > begin && text[end - 1] == L' ') end--;

    if (end - begin >= 2 && text[begin] == L'"' && text[end - 1] == L'"') {
        begin++;
        end--;
    }

    return text.substr(begin, end - begin);
}

inline bool FindEdgeInRegistry(std::wstring& outPath) {
    wchar_t buffer[MAX_PATH * 2]{};
    DWORD bytes = sizeof(buffer);

    const LONG result = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
        nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer, &bytes);
    if (result != ERROR_SUCCESS) return false;

    outPath = StripQuotes(buffer);
    return !outPath.empty();
}

inline bool FindEdge(std::wstring& outPath) {
    std::wstring candidate;
    if (FindEdgeInRegistry(candidate) && FileExists(candidate)) {
        outPath = candidate;
        return true;
    }

    const wchar_t* const roots[] = { L"ProgramFiles(x86)", L"ProgramFiles", L"LOCALAPPDATA" };
    for (const wchar_t* root : roots) {
        std::wstring base;
        if (!EnvPath(root, base)) continue;

        candidate = base + L"\\Microsoft\\Edge\\Application\\msedge.exe";
        if (FileExists(candidate)) {
            outPath = candidate;
            return true;
        }
    }

    return false;
}

inline void DeleteDirectoryRecursive(const std::wstring& path) {
    WIN32_FIND_DATAW findData{};
    const HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        const std::wstring name = findData.cFileName;
        if (name == L"." || name == L"..") continue;

        const std::wstring full = path + L"\\" + name;
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            DeleteDirectoryRecursive(full);
        } else {
            SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(full.c_str());
        }
    } while (FindNextFileW(find, &findData) != FALSE);

    FindClose(find);
    RemoveDirectoryW(path.c_str());
}

class AppWindow {
public:
    ~AppWindow() { Close(); }
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;
    AppWindow() = default;

    bool Open(const std::string& url, const std::wstring& title, std::string& error) {
        LogInfo("gui: opening the %s window", ToUtf8(title).c_str());

        wchar_t tempPath[MAX_PATH]{};
        const DWORD tempLen = GetTempPathW(MAX_PATH, tempPath);
        if (tempLen == 0 || tempLen >= MAX_PATH) {
            error = "cannot resolve a temp directory: " + ErrorText(GetLastError());
            return false;
        }

        tempRoot_.assign(tempPath, tempLen);
        profileDir_ = tempRoot_ + L"ScreenFuse-ui";

        if (!EnsureDirectory(profileDir_)) {
            error = "cannot create the browser profile directory " + ToUtf8(profileDir_);
            return false;
        }

        std::wstring edgePath;
        if (FindEdge(edgePath)) {
            std::wstring cmd = L"\"" + edgePath + L"\" --app=" + ToWide(url) +
                L" --user-data-dir=\"" + profileDir_ + L"\"" +
                L" --no-first-run --no-default-browser-check" +
                L" --disable-features=Translate,msEdgeIdentityFeatures" +
                L" --window-size=1280,860 --disable-sync";

            STARTUPINFOW startupInfo{ sizeof(startupInfo) };
            PROCESS_INFORMATION processInfo{};

            const bool created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                0, nullptr, nullptr, &startupInfo, &processInfo) != FALSE;

            if (created) {
                CloseHandle(processInfo.hThread);
                process_ = processInfo.hProcess;
                usingFallback_ = false;
                return true;
            }

            LogWarn("gui: could not launch Edge (%s), falling back to the default browser",
                ErrorText(GetLastError()).c_str());
        } else {
            LogWarn("gui: Edge was not found, falling back to the default browser");
        }

        const HINSTANCE shellResult = ShellExecuteW(nullptr, L"open", ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(shellResult) <= 32) {
            error = "cannot open a browser: " + ErrorText(GetLastError());
            return false;
        }

        usingFallback_ = true;
        return true;
    }

    bool WaitForExit(DWORD timeoutMs) {
        if (!process_) return false;
        return WaitForSingleObject(process_, timeoutMs) == WAIT_OBJECT_0;
    }

    void Close() {
        if (process_) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(process_, &exitCode) != FALSE && exitCode == STILL_ACTIVE) {
                TerminateProcess(process_, 1);
                WaitForSingleObject(process_, 2000);
            }
            CloseHandle(process_);
            process_ = nullptr;
        }

        if (!profileDir_.empty() && !tempRoot_.empty() && profileDir_.compare(0, tempRoot_.size(), tempRoot_) == 0) {
            DeleteDirectoryRecursive(profileDir_);
        }
        profileDir_.clear();
    }

    void Detach() {
        if (process_) {
            CloseHandle(process_);
            process_ = nullptr;
        }
        profileDir_.clear();
    }

    bool usingFallbackBrowser() const { return usingFallback_; }

private:
    HANDLE process_ = nullptr;
    std::wstring tempRoot_;
    std::wstring profileDir_;
    bool usingFallback_ = false;
};

inline std::wstring RunItemDialog(bool pickFolders, const std::wstring& title, const std::wstring& startAt,
                                   const std::wstring& filterLabel, const std::wstring& filterPattern) {
    std::wstring result;

    std::thread worker([&] {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return;

        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            DWORD flags = 0;
            dialog->GetOptions(&flags);
            flags |= FOS_FORCEFILESYSTEM;
            if (pickFolders) flags |= FOS_PICKFOLDERS;
            dialog->SetOptions(flags);

            if (!title.empty()) dialog->SetTitle(title.c_str());

            if (!filterLabel.empty() && !filterPattern.empty()) {
                const COMDLG_FILTERSPEC filter[] = { { filterLabel.c_str(), filterPattern.c_str() } };
                dialog->SetFileTypes(1, filter);
            }

            if (!startAt.empty() && GetFileAttributesW(startAt.c_str()) != INVALID_FILE_ATTRIBUTES) {
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(startAt.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                    dialog->SetFolder(folder);
                    folder->Release();
                }
            }

            if (SUCCEEDED(dialog->Show(nullptr))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item))) {
                    wchar_t* path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        result = path;
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }

            dialog->Release();
        }

        CoUninitialize();
    });

    worker.join();
    return result;
}

inline std::wstring PickFolder(const std::wstring& title, const std::wstring& startAt) {
    return RunItemDialog(true, title, startAt, L"", L"");
}

inline std::wstring PickFile(const std::wstring& title, const std::wstring& startAt,
                              const std::wstring& filterLabel, const std::wstring& filterPattern) {
    return RunItemDialog(false, title, startAt, filterLabel, filterPattern);
}

inline bool RevealInExplorer(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    const std::wstring args = L"/select,\"" + path + L"\"";
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

inline std::wstring EndpointFilePath() {
    std::wstring dir;
    if (!EnvPath(L"LOCALAPPDATA", dir)) return L"";

    dir += L"\\ScreenFuse";
    if (!EnsureDirectory(dir)) return L"";

    return dir + L"\\gui-endpoint.txt";
}

class InstanceLock {
public:
    ~InstanceLock() {
        if (!publishedPath_.empty()) DeleteFileW(publishedPath_.c_str());
        if (mutex_) {
            if (owner_) ReleaseMutex(mutex_);
            CloseHandle(mutex_);
        }
    }
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
    InstanceLock() = default;

    bool Acquire() {
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\ScreenFuseGuiSingleInstance");
        if (!mutex_) return false;

        owner_ = GetLastError() != ERROR_ALREADY_EXISTS;
        return owner_;
    }

    bool ReadPublished(uint16_t& port, std::string& token) {
        const std::wstring path = EndpointFilePath();
        if (path.empty()) return false;

        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;

        char buffer[512]{};
        DWORD read = 0;
        const bool ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr) != FALSE;
        CloseHandle(file);
        if (!ok || read == 0) return false;

        std::string text(buffer, read);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();

        const size_t space = text.find(' ');
        if (space == std::string::npos || space == 0 || space + 1 >= text.size()) return false;

        char* end = nullptr;
        const long parsed = strtol(text.c_str(), &end, 10);
        if (end != text.c_str() + space || parsed <= 0 || parsed > 65535) return false;

        port = static_cast<uint16_t>(parsed);
        token = text.substr(space + 1);
        return !token.empty();
    }

    bool Publish(uint16_t port, const std::string& token) {
        const std::wstring path = EndpointFilePath();
        if (path.empty()) return false;

        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;

        const std::string text = std::to_string(port) + " " + token + "\n";
        DWORD written = 0;
        const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE;
        CloseHandle(file);

        if (ok) publishedPath_ = path;
        return ok;
    }

private:
    HANDLE mutex_ = nullptr;
    bool owner_ = false;
    std::wstring publishedPath_;
};

} // namespace gui
} // namespace screenfuse
