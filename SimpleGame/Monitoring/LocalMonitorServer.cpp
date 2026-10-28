#include "stdafx.h"
#include "LocalMonitorServer.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

namespace monitoring::transport
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr size_t kMaximumClients = 16;
constexpr size_t kMaximumHeaderBytes = 4096;
constexpr size_t kMaximumAssetBytes = 1024 * 1024;
constexpr auto kClientTimeout = std::chrono::seconds(10);
struct Asset
{
    const char *path;
    const char *mime;
    std::string body;
};
struct Client
{
    SOCKET socket = INVALID_SOCKET;
    std::string request;
    std::string response;
    size_t sentBytes = 0;
    Clock::time_point opened;
};
Asset assets[] = {{"/", "text/html; charset=utf-8", {}},
                  {"/app.js", "text/javascript; charset=utf-8", {}},
                  {"/style.css", "text/css; charset=utf-8", {}},
                  {"/models.html", "text/html; charset=utf-8", {}},
                  {"/models.js", "text/javascript; charset=utf-8", {}},
                  {"/models.css", "text/css; charset=utf-8", {}}};
std::atomic<bool> running{false};
std::atomic<std::uint64_t> requestCount{0};
std::thread serverThread;
SOCKET listener = INVALID_SOCKET;
bool winsockInitialized = false;
std::mutex snapshotMutex;
std::shared_ptr<const std::string> latestSnapshot =
    std::make_shared<const std::string>("{\"schema\":\"game_monitor_v1\",\"ready\":false}");
std::shared_ptr<const std::string> modelCatalog =
    std::make_shared<const std::string>("{\"schema\":\"game_models_v1\",\"models\":[]}");

bool SetNonblocking(SOCKET socket)
{
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
std::string Response(const char *status, const char *mime, const std::string &body,
                     const std::string &origin = {})
{
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\nContent-Type: " << mime << "\r\nContent-Length: " << body.size()
        << "\r\nCache-Control: no-store\r\nConnection: close\r\n"
        << "X-Content-Type-Options: nosniff\r\n";
    if (!origin.empty())
        out << "Access-Control-Allow-Origin: " << origin << "\r\nVary: Origin\r\n";
    out << "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; "
           "connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'\r\n\r\n"
        << body;
    return out.str();
}
std::string Route(const std::string &request)
{
    ++requestCount;
    const size_t end = request.find("\r\n");
    std::istringstream firstLine(request.substr(0, end));
    std::string method, path, version;
    firstLine >> method >> path >> version;
    std::string origin;
    std::istringstream headers(request.substr(end + 2));
    std::string header;
    while (std::getline(headers, header))
    {
        auto colon = header.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = header.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (name != "origin")
            continue;
        auto value = header.substr(colon + 1);
        auto begin = value.find_first_not_of(" \t");
        auto finish = value.find_last_not_of(" \t\r");
        if (begin == std::string::npos)
            continue;
        value = value.substr(begin, finish - begin + 1);
        // A separately served local dashboard may read telemetry; external sites may not.
        for (const auto *host : {"http://127.0.0.1", "http://localhost", "http://[::1]"})
        {
            const std::string base(host);
            if (value == base ||
                (value.compare(0, base.size() + 1, base + ":") == 0 && value.size() > base.size() + 1 &&
                 value.size() <= base.size() + 6 &&
                 value.find_first_not_of("0123456789", base.size() + 1) == std::string::npos))
                origin = value;
        }
    }
    auto respond = [&](const char *status, const char *mime, const std::string &body) {
        return Response(status, mime, body, origin);
    };
    if (method != "GET")
    {
        return respond("405 Method Not Allowed", "text/plain; charset=utf-8", "GET only");
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
    {
        return respond("400 Bad Request", "text/plain; charset=utf-8", "Invalid HTTP request");
    }
    path = path.substr(0, path.find('?'));
    if (path == "/api/state" || path == "/api/models")
    {
        std::shared_ptr<const std::string> snapshot;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            snapshot = path == "/api/models" ? modelCatalog : latestSnapshot;
        }
        return respond("200 OK", "application/json; charset=utf-8", *snapshot);
    }
    if (path == "/index.html")
    {
        path = "/";
    }
    for (const auto &asset : assets)
    {
        if (path == asset.path)
        {
            return respond("200 OK", asset.mime, asset.body);
        }
    }
    return respond("404 Not Found", "text/plain; charset=utf-8", "Not found");
}
void Run()
{
    std::vector<Client> clients;
    clients.reserve(kMaximumClients);
    while (running)
    {
        fd_set readSet, writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        if (clients.size() < kMaximumClients)
        {
            FD_SET(listener, &readSet);
        }
        for (const auto &client : clients)
        {
            if (client.response.empty())
            {
                FD_SET(client.socket, &readSet);
            }
            else
            {
                FD_SET(client.socket, &writeSet);
            }
        }
        timeval timeout = {0, 100000};
        if (select(0, &readSet, &writeSet, nullptr, &timeout) == SOCKET_ERROR)
        {
            std::fprintf(stderr, "[Monitor] socket select failed: %d\n", WSAGetLastError());
            break;
        }
        if (FD_ISSET(listener, &readSet))
        {
            SOCKET accepted = accept(listener, nullptr, nullptr);
            if (accepted != INVALID_SOCKET)
            {
                if (SetNonblocking(accepted))
                {
                    clients.push_back({accepted, {}, {}, 0, Clock::now()});
                }
                else
                {
                    closesocket(accepted);
                }
            }
        }
        for (auto it = clients.begin(); it != clients.end();)
        {
            bool close = Clock::now() - it->opened >= kClientTimeout;
            if (!close && FD_ISSET(it->socket, &readSet))
            {
                char buffer[2048];
                int received = recv(it->socket, buffer, sizeof(buffer), 0);
                if (received > 0)
                {
                    it->opened = Clock::now();
                    it->request.append(buffer, size_t(received));
                    if (it->request.size() > kMaximumHeaderBytes)
                    {
                        it->response =
                            Response("431 Request Header Fields Too Large", "text/plain", "Header too large");
                    }
                    else if (it->request.find("\r\n\r\n") != std::string::npos)
                    {
                        it->response = Route(it->request);
                    }
                }
                else if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
                {
                    close = true;
                }
            }
            if (!close && FD_ISSET(it->socket, &writeSet))
            {
                size_t remaining = it->response.size() - it->sentBytes;
                // Bound work per iteration even when several dashboards are open.
                int bytes = int((std::min)(remaining, size_t(65536)));
                int sent = send(it->socket, it->response.data() + it->sentBytes, bytes, 0);
                if (sent > 0)
                {
                    it->opened = Clock::now();
                    it->sentBytes += size_t(sent);
                    close = it->sentBytes == it->response.size();
                }
                else if (sent == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
                {
                    close = true;
                }
            }
            if (close)
            {
                closesocket(it->socket);
                it = clients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (auto &client : clients)
    {
        closesocket(client.socket);
    }
    running = false;
}
} // namespace

bool Start(const std::filesystem::path &assetDirectory, unsigned short port)
{
    Stop();
    const wchar_t *files[] = {L"index.html",  L"app.js",    L"style.css",
                              L"models.html", L"models.js", L"models.css"};
    for (size_t i = 0; i < sizeof(assets) / sizeof(*assets); ++i)
    {
        std::error_code error;
        auto path = assetDirectory / files[i];
        auto bytes = std::filesystem::file_size(path, error);
        if (error || bytes == 0 || bytes > kMaximumAssetBytes)
        {
            std::fprintf(stderr, "[Monitor] dashboard asset missing or invalid: %ls\n", path.c_str());
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        assets[i].body.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (assets[i].body.size() != bytes)
        {
            std::fprintf(stderr, "[Monitor] dashboard asset read failed\n");
            return false;
        }
    }
    WSADATA data = {};
    int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error != 0)
    {
        std::fprintf(stderr, "[Monitor] WSAStartup failed: %d\n", error);
        return false;
    }
    winsockInitialized = true;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE;
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (listener == INVALID_SOCKET ||
        setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
                   sizeof(exclusive)) == SOCKET_ERROR ||
        bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, int(kMaximumClients)) == SOCKET_ERROR || !SetNonblocking(listener))
    {
        std::fprintf(stderr, "[Monitor] cannot listen on 127.0.0.1:%u (error=%d)\n", unsigned(port),
                     WSAGetLastError());
        Stop();
        return false;
    }
    requestCount = 0;
    running = true;
    try
    {
        serverThread = std::thread(Run);
    }
    catch (...)
    {
        Stop();
        std::fprintf(stderr, "[Monitor] cannot start network thread\n");
        return false;
    }
    return true;
}
void Publish(std::string snapshot)
{
    auto next = std::make_shared<const std::string>(std::move(snapshot));
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        latestSnapshot.swap(next);
    }
    // Large response allocation/copy/destruction never holds the game-thread publication lock.
}
void Stop()
{
    running = false;
    if (serverThread.joinable())
    {
        serverThread.join();
    }
    if (listener != INVALID_SOCKET)
    {
        closesocket(listener);
        listener = INVALID_SOCKET;
    }
    if (winsockInitialized)
    {
        WSACleanup();
        winsockInitialized = false;
    }
}
void PublishModels(std::string catalog)
{
    auto next = std::make_shared<const std::string>(std::move(catalog));
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        modelCatalog.swap(next);
    }
}
std::uint64_t ReadRequestCount()
{
    return requestCount.load();
}
} // namespace monitoring::transport
