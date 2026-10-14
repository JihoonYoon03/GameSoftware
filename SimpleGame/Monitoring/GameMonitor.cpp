#include "stdafx.h"
#include "GameMonitor.h"
#include "LocalMonitorServer.h"
#include "WorldSnapshot.h"
#include "ModelCatalog.h"
#include "../Profiler.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

namespace monitoring
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr unsigned short kPort = 17707;
constexpr auto kPublishInterval = std::chrono::milliseconds(500);
bool enabled = false;
std::uint64_t sequence = 0;
std::string sessionId;
Clock::time_point nextPublish;
} // namespace
void Initialize()
{
    if (enabled)
    {
        return;
    }
    wchar_t module[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (!length || length >= MAX_PATH)
    {
        std::fprintf(stderr, "[Monitor] cannot resolve executable directory\n");
        return;
    }
    sessionId = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    sequence = 0;
    transport::PublishModels(BuildModelCatalog());
    transport::Publish(BuildWorldSnapshot(++sequence, sessionId));
    enabled = transport::Start(std::filesystem::path(module).parent_path() / L"Monitor", kPort);
    if (enabled)
    {
        nextPublish = Clock::now() + kPublishInterval;
        std::printf("\n[Monitor] Open browser: http://127.0.0.1:%u/\n[Monitor] JSON: /api/state (500 ms)\n\n",
                    unsigned(kPort));
    }
}
void Update()
{
    if (!enabled || Clock::now() < nextPublish)
    {
        return;
    }
    profiling::Scope timer(profiling::Timer::MonitorSnapshot);
    transport::Publish(BuildWorldSnapshot(++sequence, sessionId));
    profiling::Count(profiling::Counter::MonitorSnapshots);
    nextPublish = Clock::now() + kPublishInterval;
}
void Shutdown()
{
    transport::Stop();
    enabled = false;
}
} // namespace monitoring
