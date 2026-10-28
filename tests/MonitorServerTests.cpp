#include "stdafx.h"
#include "Game.h"
#include "Monitoring/LocalMonitorServer.h"
#include "Monitoring/Monitor.h"
#include "Overworld.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace
{
std::string Get(unsigned short port, const std::string &path, const std::string &origin = {})
{
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(client != INVALID_SOCKET);
    DWORD timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    int connected = connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (connected != 0)
        std::cerr << "Connect " << path << " failed: " << WSAGetLastError() << std::endl;
    assert(connected == 0);
    std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (!origin.empty())
        request += "Origin: " + origin + "\r\n";
    request += "\r\n";
    assert(send(client, request.data(), int(request.size()), 0) == int(request.size()));
    std::string response;
    char buffer[32768];
    int received = 0;
    while ((received = recv(client, buffer, sizeof(buffer), 0)) > 0)
        response.append(buffer, received);
    assert(received == 0);
    closesocket(client);
    return response;
}
} // namespace

int main()
{
    tutorial::Game::Get().Reset();
    auto revision = tutorial::Overworld::Scenery().Revision();
    auto catalogJob = monitoring::ModelCatalog::Capture();
    auto expectedCatalog = monitoring::ModelCatalog::Build();
    auto catalogFuture = std::async(std::launch::async, std::move(catalogJob));
    tutorial::Game::Get().Reset(); // Main-thread world reset must not invalidate the captured geometry.
    assert(tutorial::Overworld::Scenery().Revision() != revision);
    assert(catalogFuture.get() == expectedCatalog);
    expectedCatalog.clear();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto cancelledJob = monitoring::ModelCatalog::Capture(cancel);
    cancel->store(true);
    assert(std::async(std::launch::async, std::move(cancelledJob)).get().empty());
    auto begin = std::chrono::steady_clock::now();
    std::string snapshot;
    for (int sample = 0; sample < 100; ++sample)
        snapshot = monitoring::WorldSnapshot::Build(sample, "http-test");
    double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / 100;
    std::ofstream("x64/RevisionChecks/state.json") << snapshot;
    constexpr unsigned short port = 17717;
    assert(monitoring::transport::Start("monitor", port));
    monitoring::transport::Publish(snapshot);
    auto state = Get(port, "/api/state?fresh=1", "http://localhost:5500");
    assert(state.find("200 OK") != std::string::npos);
    assert(state.find("Access-Control-Allow-Origin: http://localhost:5500\r\n") != std::string::npos);
    assert(state.substr(state.find("\r\n\r\n") + 4) == snapshot);
    assert(Get(port, "/").find("200 OK") != std::string::npos);
    assert(Get(port, "/app.js?v=2").find("200 OK") != std::string::npos);
    assert(!monitoring::transport::ModelCatalogRequested());
    monitoring::transport::PublishModels("{\"schema\":\"game_models_v1\",\"ready\":false,\"models\":[]}");
    assert(Get(port, "/api/models?fresh=1").find("\"ready\":false") != std::string::npos);
    assert(monitoring::transport::ModelCatalogRequested());
    assert(Get(port, "/api/state", "https://example.com").find("Access-Control-Allow-Origin") ==
           std::string::npos);
    assert(Get(port, "/api/state", "http://localhost.evil.test:5500").find("Access-Control-Allow-Origin") ==
           std::string::npos);
    std::string catalog = "{\"models\":\"" + std::string(6 * 1024 * 1024, 'x') + "\"}";
    monitoring::transport::PublishModels(catalog);
    auto models = Get(port, "/api/models");
    assert(models.substr(models.find("\r\n\r\n") + 4) == catalog);
    const std::string replacement = "{\"models\":\"" + std::string(6 * 1024 * 1024, 'y') + "\"}";
    std::thread reader([&]() {
        for (int i = 0; i < 4; ++i)
        {
            auto response = Get(port, "/api/models");
            const auto body = response.substr(response.find("\r\n\r\n") + 4);
            assert(body == catalog || body == replacement);
        }
    });
    for (int i = 0; i < 8; ++i)
    {
        monitoring::transport::PublishModels(i % 2 ? catalog : replacement);
        monitoring::transport::Publish(snapshot);
    }
    reader.join();
    monitoring::transport::Stop();
    assert(monitoring::transport::Start("monitor", port));
    assert(Get(port, "/api/state").find("200 OK") != std::string::npos);
    assert(!monitoring::transport::ModelCatalogRequested());
    monitoring::transport::Stop();
    std::cout << "Monitor HTTP, source assets, local-origin policy, query routes, 6 MiB catalog and restart "
                 "passed; snapshot "
              << ms << " ms.\n";
}
