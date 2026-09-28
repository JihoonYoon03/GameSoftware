#include "stdafx.h"
#include "Profiler.h"
#include "Dependencies/glew.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <locale>
#include <vector>
#pragma comment(lib, "psapi.lib")

namespace profiling
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr size_t kCounterCount = static_cast<size_t>(Counter::Count);
constexpr size_t kTimerCount = static_cast<size_t>(Timer::Count);
const char *counterNames[] = {"draw_calls",
                              "submitted_primitives",
                              "submitted_vertices",
                              "batch_state_breaks",
                              "batch_capacity_breaks",
                              "batch_barrier_breaks",
                              "mesh_cache_hits",
                              "mesh_cache_misses",
                              "mesh_upload_bytes",
                              "stream_upload_bytes",
                              "texture_upload_bytes",
                              "text_cache_hits",
                              "text_cache_misses",
                              "text_cache_evictions",
                              "scene_visited_nodes",
                              "scene_rejected_subtrees",
                              "scene_visible_actors",
                              "scene_placements",
                              "disk_cache_hits",
                              "disk_cache_misses",
                              "minimap_rebuilds",
                              "gpu_query_skipped",
                              "instanced_draw_calls",
                              "rendered_instances",
                              "collision_queries",
                              "line_of_sight_queries",
                              "navigation_visited_cells",
                              "scene_bounds_recomputed",
                              "npc_decisions",
                              "npc_path_requests",
                              "npc_path_visited_cells"};
const char *timerNames[] = {"frame_cpu",
                            "update",
                            "movement",
                            "combat",
                            "navigation",
                            "scene_update",
                            "scene_placement",
                            "scene_cull_sort",
                            "background",
                            "world",
                            "hud",
                            "queue_flush",
                            "mesh_build",
                            "post_process",
                            "present",
                            "texture_upload",
                            "text_rasterize",
                            "disk_cache_read",
                            "disk_cache_write",
                            "map_generation",
                            "log_output",
                            "resource_initialize",
                            "shader_compile",
                            "npc_update",
                            "npc_navigation"};
static_assert(sizeof(counterNames) / sizeof(*counterNames) == kCounterCount);
static_assert(sizeof(timerNames) / sizeof(*timerNames) == kTimerCount);
struct Sample
{
    double total = 0, maximum = 0;
    std::uint64_t count = 0;
    std::uint64_t drawCalls = 0;
};
struct Query
{
    GLuint id = 0;
    bool pending = false;
};
std::array<std::uint64_t, kCounterCount> counters = {};
std::array<Sample, kTimerCount> timers = {};
std::array<Query, 8> queries = {};
struct TimestampPair
{
    GLuint ids[2] = {};
    Timer timer = Timer::World;
    bool busy = false;
    bool pending = false;
};
std::array<TimestampPair, 32> timestampPairs = {};
std::array<Sample, kTimerCount> gpuTimers = {};
Sample gpu;
Clock::time_point windowStart, frameStart, lastFrame;
std::vector<double> intervals;
std::uint64_t frames = 0, frameId = 0, frameDraws = 0, lastDraws = 0, maxDraws = 0, sumDraws = 0;
std::uint64_t intervalCount = 0;
double intervalMax = 0;
bool initialized = false, inFrame = false, timerSupported = false, mixedScene = false;
int activeQuery = -1;
const char *sceneName = "unknown";
FILE *logFile = nullptr;
std::filesystem::path logBase;
std::uint64_t logBytes = 0;
unsigned int logSegment = 0;
constexpr std::uint64_t kLogSegmentBytes = 16 * 1024 * 1024;
std::string vendorJson = "null", rendererJson = "null", versionJson = "null";
std::uint64_t windowIndex = 0;
std::string JsonString(const GLubyte *text)
{
    if (!text)
    {
        return "null";
    }
    std::string result = "\"";
    for (const auto *cursor = text; *cursor; ++cursor)
    {
        if (*cursor == '"' || *cursor == '\\')
        {
            result += '\\';
            result += char(*cursor);
        }
        else if (*cursor < 32)
        {
            char escaped[7];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(*cursor));
            result += escaped;
        }
        else
        {
            result += char(*cursor);
        }
    }
    return result + '"';
}
double Milliseconds(Clock::duration value)
{
    return std::chrono::duration<double, std::milli>(value).count();
}
void Add(Sample &sample, double value)
{
    sample.total += value;
    sample.maximum = (std::max)(sample.maximum, value);
    ++sample.count;
}
void PollGpu()
{
    for (auto &pair : timestampPairs)
    {
        if (!pair.pending)
        {
            continue;
        }
        GLint ready = 0;
        glGetQueryObjectiv(pair.ids[1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            GLuint64 start = 0, end = 0;
            glGetQueryObjectui64v(pair.ids[0], GL_QUERY_RESULT, &start);
            glGetQueryObjectui64v(pair.ids[1], GL_QUERY_RESULT, &end);
            if (end >= start)
            {
                Add(gpuTimers[static_cast<size_t>(pair.timer)], double(end - start) / 1000000.0);
            }
            pair.busy = pair.pending = false;
        }
    }
    for (auto &query : queries)
    {
        if (!query.pending)
        {
            continue;
        }
        GLint ready = 0;
        glGetQueryObjectiv(query.id, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(query.id, GL_QUERY_RESULT, &elapsed);
            Add(gpu, double(elapsed) / 1000000.0);
            query.pending = false;
        }
    }
}
// Console formatting only; preserve the machine-readable JSONL file.
std::string FormatConsoleReport(const std::string &json)
{
    std::string result;
    result.reserve(json.size() + 1024);
    size_t depth = 0;
    bool inString = false, escaped = false;
    auto newLine = [&]() {
        result += '\n';
        result.append(depth * 2, ' ');
    };
    for (char character : json)
    {
        if (inString)
        {
            result += character;
            if (escaped)
            {
                escaped = false;
            }
            else if (character == '\\')
            {
                escaped = true;
            }
            else if (character == '"')
            {
                inString = false;
            }
            continue;
        }
        switch (character)
        {
        case '"':
            inString = true;
            result += character;
            break;
        case '{':
            result += character;
            ++depth;
            if (depth <= 2)
            {
                newLine();
            }
            break;
        case '}':
            --depth;
            if (depth < 2)
            {
                newLine();
            }
            result += character;
            break;
        case ',':
            result += character;
            if (depth <= 2)
            {
                newLine();
            }
            else
            {
                result += ' ';
            }
            break;
        case ':':
            result += ": ";
            break;
        default:
            result += character;
            break;
        }
    }
    result += '\n';
    return result;
}
void Report(Clock::time_point now)
{
    const auto reportStart = Clock::now();
    const double seconds = std::chrono::duration<double>(now - windowStart).count();
    std::sort(intervals.begin(), intervals.end());
    auto percentile = [](double p) {
        return intervals.empty() ? 0.0 : intervals[size_t((intervals.size() - 1) * p)];
    };
    std::ostringstream out;
    PROCESS_MEMORY_COUNTERS_EX memory = {};
    memory.cb = sizeof(memory);
    bool memoryAvailable =
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory)) != FALSE;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3)
        << "{\"schema\":\"game_performance_v1\",\"event\":\"performance_window\",\"frame_id\":" << frameId
        << ",\"window_index\":" << ++windowIndex << ",\"opengl\":{\"vendor\":" << vendorJson
        << ",\"renderer\":" << rendererJson << ",\"version\":" << versionJson << "}"
#ifdef _DEBUG
        << ",\"build_config\":\"debug\""
#else
        << ",\"build_config\":\"release\""
#endif
        << ",\"scene\":\"" << (mixedScene ? "mixed" : sceneName) << "\",\"window_seconds\":" << seconds
        << ",\"frames\":" << frames << ",\"fps\":" << frames / seconds
        << ",\"target_fps\":60,\"below_target_fps\":" << (frames / seconds < 60 ? "true" : "false")
        << ",\"frame_interval_ms\":{\"p95\":" << percentile(.95) << ",\"p99\":" << percentile(.99)
        << ",\"max\":" << intervalMax << ",\"samples\":" << intervals.size()
        << ",\"dropped_samples\":" << intervalCount - intervals.size() << "}"
        << ",\"draw_calls_per_frame\":{\"last\":" << lastDraws
        << ",\"avg\":" << (frames ? double(sumDraws) / frames : 0) << ",\"max\":" << maxDraws << "}"
        << ",\"gpu_elapsed_ms\":";
    if (gpu.count)
    {
        out << "{\"avg\":" << gpu.total / gpu.count << ",\"max\":" << gpu.maximum
            << ",\"samples\":" << gpu.count << "}";
    }
    else
    {
        out << "null";
    }
    out << ",\"process_memory_bytes\":";
    if (memoryAvailable)
    {
        out << "{\"working_set\":" << memory.WorkingSetSize << ",\"private_commit\":" << memory.PrivateUsage
            << "}";
    }
    else
    {
        out << "null";
    }
    out << ",\"gpu_timer_supported\":" << (timerSupported ? "true" : "false")
        << ",\"file_logging_enabled\":" << (logFile ? "true" : "false") << ",\"counters\":{";
    for (size_t i = 0; i < kCounterCount; ++i)
    {
        out << (i ? "," : "") << '"' << counterNames[i] << "\":" << counters[i];
    }
    out << "},\"cpu_scopes_ms\":{";
    for (size_t i = 0; i < kTimerCount; ++i)
    {
        const auto &sample = timers[i];
        out << (i ? "," : "") << '"' << timerNames[i] << "\":{\"total\":" << sample.total
            << ",\"avg\":" << (sample.count ? sample.total / sample.count : 0)
            << ",\"max\":" << sample.maximum << ",\"samples\":" << sample.count
            << ",\"draw_calls_total\":" << sample.drawCalls << "}";
    }
    out << "},\"gpu_scopes_ms\":{";
    bool firstGpu = true;
    for (size_t i = 0; i < kTimerCount; ++i)
    {
        const auto &sample = gpuTimers[i];
        if (!sample.count)
        {
            continue;
        }
        out << (firstGpu ? "" : ",") << '"' << timerNames[i] << "\":{\"avg\":" << sample.total / sample.count
            << ",\"max\":" << sample.maximum << ",\"samples\":" << sample.count << "}";
        firstGpu = false;
    }
    out << "}}\n";
    counters = {};
    timers = {};
    gpu = {};
    gpuTimers = {};
    frames = sumDraws = maxDraws = intervalCount = 0;
    intervalMax = 0;
    intervals.clear();
    windowStart = now;
    mixedScene = false;
    auto logStart = reportStart;
    const std::string line = out.str();
    const std::string consoleReport = FormatConsoleReport(line);
    std::fwrite(consoleReport.data(), 1, consoleReport.size(), stdout);
    std::fflush(stdout);
    if (logFile)
    {
        if (logBytes + line.size() > kLogSegmentBytes)
        {
            std::fclose(logFile);
            logFile = nullptr;
            logSegment = (logSegment + 1) % 4;
            auto path = logBase;
            path += L"-" + std::to_wstring(logSegment) + L".jsonl";
            _wfopen_s(&logFile, path.c_str(), L"wb");
            logBytes = 0;
        }
    }
    if (logFile)
    {
        if (std::fwrite(line.data(), 1, line.size(), logFile) != line.size() || std::fflush(logFile) != 0)
        {
            std::fclose(logFile);
            logFile = nullptr;
        }
        logBytes += line.size();
    }
    Add(timers[static_cast<size_t>(Timer::LogOutput)], Milliseconds(Clock::now() - logStart));
}
} // namespace
Scope::Scope(Timer value) : timer(value), start(Clock::now())
{
    drawStart = counters[static_cast<size_t>(Counter::DrawCalls)];
}
Scope::~Scope()
{
    auto &sample = timers[static_cast<size_t>(timer)];
    Add(sample, Milliseconds(Clock::now() - start));
    const auto draws = counters[static_cast<size_t>(Counter::DrawCalls)];
    sample.drawCalls += draws >= drawStart ? draws - drawStart : 0;
}
GpuScope::GpuScope(Timer timer)
{
    if (!timerSupported || !inFrame)
    {
        return;
    }
    for (size_t i = 0; i < timestampPairs.size(); ++i)
    {
        auto &pair = timestampPairs[i];
        if (!pair.busy)
        {
            slot = int(i);
            pair.busy = true;
            pair.timer = timer;
            glQueryCounter(pair.ids[0], GL_TIMESTAMP);
            return;
        }
    }
    Count(Counter::GpuQuerySkipped);
}
GpuScope::~GpuScope()
{
    if (slot >= 0)
    {
        auto &pair = timestampPairs[slot];
        glQueryCounter(pair.ids[1], GL_TIMESTAMP);
        pair.pending = true;
    }
}
void Count(Counter counter, std::uint64_t amount)
{
    counters[static_cast<size_t>(counter)] += amount;
    if (inFrame && counter == Counter::DrawCalls)
    {
        frameDraws += amount;
    }
}
std::uint64_t ReadCounter(Counter counter)
{
    return counters[static_cast<size_t>(counter)];
}
void Initialize()
{
    if (initialized)
    {
        return;
    }
    initialized = true;
    windowIndex = 0;
    vendorJson = JsonString(glGetString(GL_VENDOR));
    rendererJson = JsonString(glGetString(GL_RENDERER));
    versionJson = JsonString(glGetString(GL_VERSION));
    frames = frameId = sumDraws = maxDraws = lastDraws = intervalCount = 0;
    intervalMax = 0;
    logBytes = logSegment = 0;
    intervals.clear();
    windowStart = Clock::now();
    intervals.reserve(4096);
    timerSupported = GLEW_VERSION_3_3 || GLEW_ARB_timer_query;
    if (timerSupported)
    {
        for (auto &pair : timestampPairs)
        {
            glGenQueries(2, pair.ids);
        }
        for (auto &query : queries)
        {
            glGenQueries(1, &query.id);
        }
    }
    wchar_t module[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length && length < MAX_PATH)
    {
        std::filesystem::path directory = std::filesystem::path(module).parent_path() / L"Logs";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (!error)
        {
            logBase = directory / (L"performance-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                   std::to_wstring(GetTickCount64()));
            auto file = logBase;
            file += L"-0.jsonl";
            _wfopen_s(&logFile, file.c_str(), L"wb");
        }
    }
}
void BeginFrame(const char *scene)
{
    if (!initialized)
    {
        Initialize();
    }
    auto now = Clock::now();
    if (frameId)
    {
        double interval = Milliseconds(now - lastFrame);
        ++intervalCount;
        intervalMax = (std::max)(intervalMax, interval);
        if (intervals.size() < 4096)
        {
            intervals.push_back(interval);
        }
    }
    if (frames && std::string(sceneName) != scene)
    {
        mixedScene = true;
    }
    sceneName = scene;
    frameStart = lastFrame = now;
    ++frameId;
    frameDraws = 0;
    inFrame = true;
    if (timerSupported)
    {
        PollGpu();
        for (size_t i = 0; i < queries.size(); ++i)
        {
            if (!queries[i].pending)
            {
                activeQuery = int(i);
                glBeginQuery(GL_TIME_ELAPSED, queries[i].id);
                return;
            }
        }
        Count(Counter::GpuQuerySkipped);
    }
}
void EndGpuFrame()
{
    if (activeQuery >= 0)
    {
        glEndQuery(GL_TIME_ELAPSED);
        queries[activeQuery].pending = true;
        activeQuery = -1;
    }
}
void EndFrame()
{
    EndGpuFrame();
    auto now = Clock::now();
    Add(timers[static_cast<size_t>(Timer::FrameCpu)], Milliseconds(now - frameStart));
    timers[static_cast<size_t>(Timer::FrameCpu)].drawCalls += frameDraws;
    inFrame = false;
    ++frames;
    lastDraws = frameDraws;
    sumDraws += frameDraws;
    maxDraws = (std::max)(maxDraws, frameDraws);
    if (now - windowStart >= std::chrono::seconds(1))
    {
        Report(now);
    }
}
void Shutdown()
{
    EndGpuFrame();
    if (frames)
    {
        Report(Clock::now());
    }
    for (auto &query : queries)
    {
        if (query.id)
        {
            glDeleteQueries(1, &query.id);
        }
        query = {};
    }
    for (auto &pair : timestampPairs)
    {
        if (pair.ids[0])
        {
            glDeleteQueries(2, pair.ids);
        }
        pair = {};
    }
    if (logFile)
    {
        std::fclose(logFile);
        logFile = nullptr;
    }
    initialized = false;
    timerSupported = false;
}
} // namespace profiling
