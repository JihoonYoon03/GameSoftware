#pragma once
#include <chrono>
#include <cstdint>

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
} // namespace profiling
