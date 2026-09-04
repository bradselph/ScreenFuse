#pragma once
#include "common.hpp"
#include "net.hpp"
#include <atomic>
#include <cctype>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace screenfuse {
namespace gui {

constexpr int kMaxConnections = 32;
constexpr int kListenBacklog = 16;
constexpr size_t kMaxHeaderBytes = 32u * 1024u;
constexpr size_t kMaxRequestLineBytes = 8u * 1024u;
constexpr size_t kMaxBodyBytes = 1024u * 1024u;
constexpr DWORD kRecvTimeoutMs = 15000;
constexpr DWORD kSendTimeoutMs = 15000;
constexpr DWORD kStreamSendTimeoutMs = 10000;
constexpr DWORD kRejectSendTimeoutMs = 5000;

inline bool EqualsIgnoreCase(const std::string& text, const char* other) {
    size_t i = 0;
    for (; i < text.size() && other[i] != '\0'; ++i) {
        if (::tolower(static_cast<unsigned char>(text[i])) != ::tolower(static_cast<unsigned char>(other[i]))) {
            return false;
        }
    }
    return i == text.size() && other[i] == '\0';
}

inline std::string UrlDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < text.size() &&
            ::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            ::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            auto hexDigit = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                return h - 'A' + 10;
            };
            out.push_back(static_cast<char>((hexDigit(text[i + 1]) << 4) | hexDigit(text[i + 2])));
            i += 2;
        } else {
            out.push_back(c);
        }
    }

    return out;
}

inline bool HttpSendAll(SOCKET sock, const void* data, size_t length) {
    return SendAll(sock, data, length);
}

inline bool FindQueryParam(const std::string& query, const char* name, std::string* value) {
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const size_t end = amp == std::string::npos ? query.size() : amp;
        const size_t eq = query.find('=', pos);

        std::string key, raw;
        if (eq != std::string::npos && eq < end) {
            key = query.substr(pos, eq - pos);
            raw = query.substr(eq + 1, end - eq - 1);
        } else {
            key = query.substr(pos, end - pos);
        }

        if (UrlDecode(key) == name) {
            if (value) *value = UrlDecode(raw);
            return true;
        }

        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return false;
}

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    std::string Header(const char* name) const {
        for (const auto& kv : headers) {
            if (EqualsIgnoreCase(kv.first, name)) return kv.second;
        }
        return std::string();
    }

    std::string Param(const char* name) const {
        std::string value;
        FindQueryParam(query, name, &value);
        return value;
    }

    bool HasParam(const char* name) const {
        return FindQueryParam(query, name, nullptr);
    }
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> extraHeaders;
};

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

using HttpStreamHandler = std::function<bool(const HttpRequest&, SOCKET)>;

class HttpServer {
public:
    ~HttpServer() { Stop(); }

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer() = default;

    bool Start(uint16_t preferredPort, std::string& error) {
        std::lock_guard<std::mutex> stopLock(stopMutex_);
        if (running_.load()) {
            error = "server is already running";
            return false;
        }

        const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            error = "socket() failed: " + ErrorText(WSAGetLastError());
            return false;
        }

        BOOL exclusive = TRUE;
        setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(preferredPort);

        bool bound = false;
        if (preferredPort != 0) {
            if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                bound = true;
            } else if (WSAGetLastError() != WSAEADDRINUSE) {
                error = "bind failed: " + ErrorText(WSAGetLastError());
                closesocket(sock);
                return false;
            }
        }

        if (!bound) {
            address.sin_port = 0;
            if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
                error = "bind failed: " + ErrorText(WSAGetLastError());
                closesocket(sock);
                return false;
            }
        }

        if (listen(sock, kListenBacklog) == SOCKET_ERROR) {
            error = "listen failed: " + ErrorText(WSAGetLastError());
            closesocket(sock);
            return false;
        }

        sockaddr_in actual{};
        int actualLen = sizeof(actual);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&actual), &actualLen) == SOCKET_ERROR) {
            error = "getsockname failed: " + ErrorText(WSAGetLastError());
            closesocket(sock);
            return false;
        }

        listenSocket_ = sock;
        port_ = ntohs(actual.sin_port);
        stopping_ = false;
        running_ = true;
        acceptThread_ = std::thread([this] { AcceptLoop(); });
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> stopLock(stopMutex_);
        if (!running_.load()) return;

        running_ = false;
        stopping_ = true;

        if (listenSocket_ != INVALID_SOCKET) {
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
        }

        {
            std::lock_guard<std::mutex> lock(connMutex_);
            for (auto& conn : connections_) {
                if (conn->sock != INVALID_SOCKET) {
                    closesocket(conn->sock);
                    conn->sock = INVALID_SOCKET;
                }
            }
        }

        if (acceptThread_.joinable()) acceptThread_.join();

        {
            std::lock_guard<std::mutex> lock(connMutex_);
            for (auto& conn : connections_) {
                if (conn->thread.joinable()) conn->thread.join();
            }
            connections_.clear();
        }

        port_ = 0;
    }

    uint16_t port() const { return port_.load(); }
    bool running() const { return running_.load(); }

    void SetHandler(HttpHandler handler) { handler_ = std::move(handler); }
    void SetStreamHandler(HttpStreamHandler handler) { streamHandler_ = std::move(handler); }

private:
    struct Connection {
        SOCKET sock = INVALID_SOCKET;
        std::thread thread;
        std::atomic<bool> finished{ false };
    };

    static const char* ReasonPhrase(int status) {
        switch (status) {
            case 200: return "OK";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 409: return "Conflict";
            case 411: return "Length Required";
            case 413: return "Payload Too Large";
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 503: return "Service Unavailable";
            default: return "Unknown";
        }
    }

    static bool SendResponse(SOCKET sock, const HttpRequest& request, const HttpResponse& response) {
        std::string head;
        head += "HTTP/1.1 ";
        head += std::to_string(response.status);
        head += ' ';
        head += ReasonPhrase(response.status);
        head += "\r\n";
        head += "Content-Type: ";
        head += response.contentType;
        head += "\r\n";
        head += "Content-Length: ";
        head += std::to_string(response.body.size());
        head += "\r\n";
        head += "Cache-Control: no-store\r\n";
        head += "X-Content-Type-Options: nosniff\r\n";
        head += "Connection: close\r\n";
        for (const auto& kv : response.extraHeaders) {
            head += kv.first;
            head += ": ";
            head += kv.second;
            head += "\r\n";
        }
        head += "\r\n";

        if (!HttpSendAll(sock, head.data(), head.size())) return false;
        if (request.method == "HEAD" || response.body.empty()) return true;
        return HttpSendAll(sock, response.body.data(), response.body.size());
    }

    static void SendQuickStatus(SOCKET sock, int status) {
        HttpRequest request;
        HttpResponse response;
        response.status = status;
        SendResponse(sock, request, response);
    }

    void CloseConnectionSocket(Connection* conn) {
        std::lock_guard<std::mutex> lock(connMutex_);
        if (conn->sock != INVALID_SOCKET) {
            closesocket(conn->sock);
            conn->sock = INVALID_SOCKET;
        }
    }

    void ReapFinishedConnections() {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (size_t i = 0; i < connections_.size();) {
            if (connections_[i]->finished.load()) {
                if (connections_[i]->thread.joinable()) connections_[i]->thread.join();
                connections_.erase(connections_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    void ServeOneRequest(SOCKET sock) {
        std::string buffer;
        size_t headEnd = 0;
        char chunk[4096];

        for (;;) {
            const size_t crlf = buffer.find("\r\n\r\n");
            const size_t lf = buffer.find("\n\n");
            size_t term = std::string::npos;
            size_t termLen = 0;
            if (crlf != std::string::npos && (lf == std::string::npos || crlf <= lf)) {
                term = crlf;
                termLen = 4;
            } else if (lf != std::string::npos) {
                term = lf;
                termLen = 2;
            }
            if (term != std::string::npos) {
                headEnd = term + termLen;
                break;
            }

            if (buffer.size() > kMaxHeaderBytes) {
                SendQuickStatus(sock, 400);
                return;
            }

            const int got = recv(sock, chunk, sizeof(chunk), 0);
            if (got <= 0) return;
            buffer.append(chunk, static_cast<size_t>(got));
        }

        std::vector<std::string> lines;
        {
            size_t start = 0;
            while (start < headEnd) {
                const size_t nl = buffer.find('\n', start);
                if (nl == std::string::npos || nl >= headEnd) break;
                std::string line = buffer.substr(start, nl - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(std::move(line));
                start = nl + 1;
            }
        }

        if (lines.empty()) {
            SendQuickStatus(sock, 400);
            return;
        }

        const std::string& requestLine = lines[0];
        if (requestLine.size() > kMaxRequestLineBytes) {
            SendQuickStatus(sock, 400);
            return;
        }

        const size_t sp1 = requestLine.find(' ');
        const size_t sp2 = sp1 == std::string::npos ? std::string::npos : requestLine.find(' ', sp1 + 1);

        std::string method = sp1 == std::string::npos ? requestLine : requestLine.substr(0, sp1);
        std::string target = sp1 == std::string::npos ? std::string()
            : (sp2 == std::string::npos ? requestLine.substr(sp1 + 1) : requestLine.substr(sp1 + 1, sp2 - sp1 - 1));

        for (char& c : method) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));

        if (method.empty() || target.empty()) {
            SendQuickStatus(sock, 400);
            return;
        }

        HttpRequest request;
        request.method = method;
        request.target = target;

        const size_t qpos = target.find('?');
        request.path = UrlDecode(qpos == std::string::npos ? target : target.substr(0, qpos));
        request.query = qpos == std::string::npos ? std::string() : target.substr(qpos + 1);

        for (size_t i = 1; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            if (line.empty()) break;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = line.substr(0, colon);
            size_t valueStart = colon + 1;
            while (valueStart < line.size() && (line[valueStart] == ' ' || line[valueStart] == '\t')) ++valueStart;
            request.headers.emplace_back(std::move(name), line.substr(valueStart));
        }

        std::string transferEncoding = request.Header("Transfer-Encoding");
        for (char& c : transferEncoding) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (transferEncoding.find("chunked") != std::string::npos) {
            SendQuickStatus(sock, 501);
            return;
        }

        size_t contentLength = 0;
        const std::string contentLengthHeader = request.Header("Content-Length");
        if (!contentLengthHeader.empty()) {
            bool valid = true;
            size_t parsed = 0;
            for (char c : contentLengthHeader) {
                if (c < '0' || c > '9') {
                    valid = false;
                    break;
                }
                parsed = parsed * 10 + static_cast<size_t>(c - '0');
                if (parsed > kMaxBodyBytes) break;
            }
            if (!valid) {
                SendQuickStatus(sock, 400);
                return;
            }
            contentLength = parsed;
        }

        if (contentLength > kMaxBodyBytes) {
            SendQuickStatus(sock, 413);
            return;
        }

        std::string body = buffer.substr(headEnd);
        while (body.size() < contentLength) {
            const int got = recv(sock, chunk, sizeof(chunk), 0);
            if (got <= 0) return;
            body.append(chunk, static_cast<size_t>(got));
        }
        if (body.size() > contentLength) body.resize(contentLength);
        request.body = std::move(body);

        if (method == "OPTIONS") {
            HttpResponse response;
            response.status = 204;
            response.extraHeaders.emplace_back("Allow", "GET, POST, OPTIONS");
            SendResponse(sock, request, response);
            return;
        }

        if (method != "GET" && method != "HEAD" && method != "POST") {
            HttpResponse response;
            response.status = 405;
            response.extraHeaders.emplace_back("Allow", "GET, POST, OPTIONS");
            SendResponse(sock, request, response);
            return;
        }

        if (streamHandler_) {
            DWORD noTimeout = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&noTimeout), sizeof(noTimeout));
            DWORD streamTimeout = kStreamSendTimeoutMs;
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&streamTimeout), sizeof(streamTimeout));

            if (streamHandler_(request, sock)) return;
        }

        HttpResponse response;
        if (handler_) {
            handler_(request, response);
        } else {
            response.status = 404;
        }
        SendResponse(sock, request, response);
    }

    void ServeConnection(Connection* conn) {
        const SOCKET sock = conn->sock;

        DWORD recvTimeout = kRecvTimeoutMs;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));
        DWORD sendTimeout = kSendTimeoutMs;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));
        BOOL noDelay = TRUE;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

        ServeOneRequest(sock);

        CloseConnectionSocket(conn);
        activeCount_.fetch_sub(1);
        conn->finished.store(true);
    }

    void AcceptLoop() {
        for (;;) {
            sockaddr_in peerAddress{};
            int peerLength = sizeof(peerAddress);
            const SOCKET client = accept(listenSocket_, reinterpret_cast<sockaddr*>(&peerAddress), &peerLength);

            if (client == INVALID_SOCKET) {
                if (stopping_.load()) break;
                const int code = WSAGetLastError();
                if (code == WSAEINTR || code == WSAENOTSOCK) break;
                LogWarn("gui http: accept failed: %s", ErrorText(code).c_str());
                continue;
            }

            ReapFinishedConnections();

            if (activeCount_.fetch_add(1) >= kMaxConnections) {
                activeCount_.fetch_sub(1);
                DWORD rejectTimeout = kRejectSendTimeoutMs;
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&rejectTimeout), sizeof(rejectTimeout));
                SendQuickStatus(client, 503);
                closesocket(client);
                continue;
            }

            auto conn = std::make_unique<Connection>();
            conn->sock = client;
            Connection* connPtr = conn.get();

            bool started = false;
            {
                std::lock_guard<std::mutex> lock(connMutex_);
                if (!stopping_.load()) {
                    connections_.push_back(std::move(conn));
                    connPtr->thread = std::thread([this, connPtr] { ServeConnection(connPtr); });
                    started = true;
                }
            }

            if (!started) {
                activeCount_.fetch_sub(1);
                closesocket(client);
            }
        }
    }

    SOCKET listenSocket_ = INVALID_SOCKET;
    std::atomic<uint16_t> port_{ 0 };
    std::atomic<bool> running_{ false };
    std::atomic<bool> stopping_{ false };
    std::thread acceptThread_;
    std::mutex stopMutex_;

    std::mutex connMutex_;
    std::vector<std::unique_ptr<Connection>> connections_;
    std::atomic<int> activeCount_{ 0 };

    HttpHandler handler_;
    HttpStreamHandler streamHandler_;
};

inline bool WriteMultipartPreamble(SOCKET sock, const char* boundary) {
    std::string head;
    head += "HTTP/1.1 200 OK\r\n";
    head += "Content-Type: multipart/x-mixed-replace; boundary=";
    head += boundary;
    head += "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "Pragma: no-cache\r\n";
    head += "Connection: close\r\n";
    head += "\r\n";
    return HttpSendAll(sock, head.data(), head.size());
}

inline bool WriteMultipartJpeg(SOCKET sock, const char* boundary, const uint8_t* data, size_t length) {
    std::string head;
    head += "--";
    head += boundary;
    head += "\r\n";
    head += "Content-Type: image/jpeg\r\n";
    head += "Content-Length: ";
    head += std::to_string(length);
    head += "\r\n\r\n";

    if (!HttpSendAll(sock, head.data(), head.size())) return false;
    if (length > 0 && !HttpSendAll(sock, data, length)) return false;
    return HttpSendAll(sock, "\r\n", 2);
}

} // namespace gui
} // namespace screenfuse
