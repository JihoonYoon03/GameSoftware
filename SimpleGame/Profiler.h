#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace profiling
{
enum class Counter
{
    DrawCalls,
    SubmittedPrimitives,
    SubmittedVertices,
    BatchStateBreaks,
    BatchCapacityBreaks,
    BatchBarrierBreaks,
    MeshCacheHits,
    MeshCacheMisses,
    MeshUploadBytes,
    StreamUploadBytes,
    TextureUploadBytes,
    TextCacheHits,
    TextCacheMisses,
    TextCacheEvictions,
    SceneVisited,
    SceneRejected,
    SceneVisible,
    ScenePlacements,
    DiskCacheHits,
    DiskCacheMisses,
    MinimapRebuilds,
    GpuQuerySkipped,
    InstancedDrawCalls,
    RenderedInstances,
    CollisionQueries,
    LineOfSightQueries,
    NavigationVisitedCells,
    SceneBoundsRecomputed,
    NpcDecisions,
    NpcPathRequests,
    NpcPathVisitedCells,
    MonitorSnapshots,
    NpcShots,
    NpcEnemyKills,
    WorkstationRepairCompletions,
    WorkstationSupplyDeliveries,
    NpcRescues,
    Count
};
enum class Timer
{
    FrameCpu,
    Update,
    Movement,
    Combat,
    Navigation,
    SceneUpdate,
    ScenePlacement,
    SceneCullSort,
    Background,
    World,
    Hud,
    QueueFlush,
    MeshBuild,
    PostProcess,
    Present,
    TextureUpload,
    TextRasterize,
    DiskCacheRead,
    DiskCacheWrite,
    MapGeneration,
    LogOutput,
    ResourceInitialize,
    ShaderCompile,
    NpcUpdate,
    NpcNavigation,
    MonitorSnapshot,
    NpcCooperation,
    MineLighting,
    Count
};
class Scope
{
  public:
    explicit Scope(Timer timer);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    Timer timer;
    std::uint64_t drawStart = 0;
    std::chrono::steady_clock::time_point start;
};
class GpuScope
{
  public:
    explicit GpuScope(Timer timer);
    ~GpuScope();
    GpuScope(const GpuScope &) = delete;
    GpuScope &operator=(const GpuScope &) = delete;

  private:
    int slot = -1;
};
void Initialize();
void Shutdown();
void BeginFrame(const char *scene);
void EndGpuFrame();
void EndFrame();
void Count(Counter counter, std::uint64_t amount = 1);
std::uint64_t ReadCounter(Counter counter);
// Latest completed one-second window. Read only on the game thread.
const std::string &GetLatestReportJson();
} // namespace profiling

// Compatibility draw-call instrumentation.

namespace renderdebug
{
// Compatibility wrapper for remaining immediate-mode submissions (post-processing).
// Batched glDrawArrays/instanced calls are counted at submission in RenderQueue.
void BeginFrame();
void BeginPrimitive(unsigned int mode);
void EndFrame();
} // namespace renderdebug
