#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <timeapi.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winmm.lib")

namespace screenfuse {


inline int64_t QpcFreq() {
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return freq;
}

inline int64_t QpcTicks() {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

inline int64_t TicksToUs(int64_t ticks) {
    const int64_t f = QpcFreq();
    const int64_t whole = ticks / f;
    const int64_t rest = ticks % f;
    return whole * 1000000 + (rest * 1000000) / f;
}

inline int64_t UsToTicks(int64_t us) {
    const int64_t f = QpcFreq();
    const int64_t whole = us / 1000000;
    const int64_t rest = us % 1000000;
    return whole * f + (rest * f) / 1000000;
}

inline int64_t NowUs() { return TicksToUs(QpcTicks()); }

inline void RaiseTimerResolution() { timeBeginPeriod(1); }
inline void RestoreTimerResolution() { timeEndPeriod(1); }

inline void WaitUntilTicks(int64_t targetTicks) {
    for (;;) {
        const int64_t now = QpcTicks();
        if (now >= targetTicks) return;

        const int64_t remainUs = TicksToUs(targetTicks - now);
        if (remainUs > 2000) {
            Sleep(static_cast<DWORD>((remainUs - 1500) / 1000));
        } else if (remainUs > 150) {
            Sleep(0);
        } else {
            YieldProcessor();
        }
    }
}

inline void LogLine(const char* level, const char* fmt, va_list args) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    char body[2048];
    vsnprintf(body, sizeof(body), fmt, args);

    printf("%02u:%02u:%02u.%03u %-5s %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level, body);
    fflush(stdout);
}

inline void LogInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogLine("INFO", fmt, args);
    va_end(args);
}

inline void LogWarn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogLine("WARN", fmt, args);
    va_end(args);
}

inline void LogErr(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogLine("ERROR", fmt, args);
    va_end(args);
}


inline std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

inline std::wstring ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

inline std::wstring ExeDir() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

inline std::string ErrorText(DWORD code) {
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<char*>(&buffer), 0, nullptr);

    std::string text = length && buffer ? std::string(buffer, length) : "unknown error";
    if (buffer) LocalFree(buffer);

    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text + " (" + std::to_string(code) + ")";
}

inline std::wstring TimestampForFilename() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t buffer[64]{};
    swprintf(buffer, 64, L"%04u%02u%02u-%02u%02u%02u-%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

inline bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;

    std::wstring partial;
    partial.reserve(path.size());

    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            if (partial.size() > 2 || (partial.size() == 2 && partial[1] != L':')) {
                if (!CreateDirectoryW(partial.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
            }
            if (i == path.size()) break;
            partial.push_back(L'\\');
        } else {
            partial.push_back(path[i]);
        }
    }

    return true;
}

constexpr size_t kKeyBytes = 32;
constexpr size_t kNonceBytes = 32;
constexpr size_t kHmacBytes = 32;

inline std::string ToHex(const uint8_t* data, size_t length) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

inline bool FromHex(const std::string& text, std::vector<uint8_t>& out) {
    std::string clean;
    for (char c : text) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        clean.push_back(c);
    }
    if (clean.empty() || clean.size() % 2 != 0) return false;

    out.clear();
    out.reserve(clean.size() / 2);

    for (size_t i = 0; i < clean.size(); i += 2) {
        int value = 0;
        for (int half = 0; half < 2; ++half) {
            const char c = clean[i + half];
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return false;
            value = (value << 4) | digit;
        }
        out.push_back(static_cast<uint8_t>(value));
    }

    return true;
}

inline bool RandomBytes(uint8_t* out, size_t length) {
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

inline bool Hmac(const std::vector<uint8_t>& key, const uint8_t* data, size_t length, uint8_t out[kHmacBytes]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) {
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0,
        const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) >= 0;

    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(length), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, out, static_cast<ULONG>(kHmacBytes), 0) >= 0;

    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

inline bool ConstantTimeEqual(const uint8_t* left, const uint8_t* right, size_t length) {
    uint8_t diff = 0;
    for (size_t i = 0; i < length; ++i) diff |= static_cast<uint8_t>(left[i] ^ right[i]);
    return diff == 0;
}

inline bool ReadKeyFile(const std::wstring& path, std::vector<uint8_t>& key, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot open key file " + ToUtf8(path) + ": " + ErrorText(GetLastError());
        return false;
    }

    char buffer[256]{};
    DWORD read = 0;
    const bool ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr) != FALSE;
    CloseHandle(file);

    if (!ok) {
        error = "cannot read key file: " + ErrorText(GetLastError());
        return false;
    }

    if (!FromHex(std::string(buffer, read), key) || key.size() != kKeyBytes) {
        error = "key file must contain " + std::to_string(kKeyBytes * 2) + " hex characters";
        return false;
    }

    return true;
}

inline bool WriteKeyFile(const std::wstring& path, const std::vector<uint8_t>& key, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        error = code == ERROR_FILE_EXISTS
            ? "key file already exists: " + ToUtf8(path) + " (delete it first if you really want a new key)"
            : "cannot create key file: " + ErrorText(code);
        return false;
    }

    const std::string hex = ToHex(key.data(), key.size()) + "\n";
    DWORD written = 0;
    const bool ok = WriteFile(file, hex.data(), static_cast<DWORD>(hex.size()), &written, nullptr) != FALSE;
    CloseHandle(file);

    if (!ok) {
        error = "cannot write key file: " + ErrorText(GetLastError());
        return false;
    }

    return true;
}

} // namespace screenfuse
