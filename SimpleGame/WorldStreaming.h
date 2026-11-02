#pragma once
#include "WorldContent.h"
#include <functional>
#include <memory>
#include <string>

namespace world
{
struct StreamSettings
{
    unsigned cells = 16, maxResident = 96, maxQueued = 48, maxCompleted = 4, publishPerUpdate = 2;
    double targetChunkMeters = 128, loadRadiusMeters = 160, retainRadiusMeters = 260;
    double predictionSeconds = 2, maxPredictionMeters = 180, originRadiusMeters = 128;
    std::size_t memoryBudgetBytes = 32 * 1024 * 1024;
    void Validate() const;
    std::uint8_t Level(const PlanetDefinition &planet) const;
};
struct GroundContact
{
    double heightMeters = 0, slope = 0;
    Vector3d normal = {1, 0, 0};
    bool land = true, detailed = false;
};
struct TerrainVertex
{
    SurfacePoint point;
    Vector3d normal;
    Biome biome = Biome::Grassland;
};
struct RoadRibbon
{
    std::vector<SurfacePoint> points;
    double widthMeters = 6;
    double sidewalkMeters = 0;
    unsigned lanes = 2;
};
// ============================================================================
// SurfaceChunk — immutable CPU terrain, contact grid and clipped route publication
// ============================================================================
struct SurfaceChunk
{
    ChunkKey key;
    TangentFrame frame;
    unsigned cells = 0;
    std::vector<TerrainVertex> vertices;
    std::vector<std::size_t> regions;
    std::vector<RoadRibbon> roads;
    std::vector<SurfaceStructure> structures;
    double radiusMeters = 0;
    std::size_t Bytes() const;
    GroundContact Contact(const SurfacePoint &point) const;
    static std::shared_ptr<const SurfaceChunk> Build(const GeneratedPlanet &planet, const ChunkKey &key,
                                                     unsigned cells, const std::atomic_bool &cancel);
};
struct StreamStatistics
{
    std::uint64_t epoch = 0, revision = 0, generated = 0, cancelled = 0, discarded = 0, evicted = 0;
    std::size_t resident = 0, queued = 0, completed = 0, bytes = 0;
    bool preparing = false;
    std::string error;
};
struct SurfaceSnapshot
{
    std::shared_ptr<const GeneratedPlanet> planet;
    std::vector<std::shared_ptr<const SurfaceChunk>> chunks;
    StreamStatistics statistics;
};
// ============================================================================
// TerrainStream — one bounded CPU worker, cancellable jobs and main-thread publication
// ============================================================================
class TerrainStream
{
  public:
    using Builder = std::function<std::shared_ptr<const SurfaceChunk>(
        const GeneratedPlanet &, const ChunkKey &, unsigned, const std::atomic_bool &)>;
    explicit TerrainStream(StreamSettings settings = {}, Builder builder = SurfaceChunk::Build);
    ~TerrainStream();
    TerrainStream(const TerrainStream &) = delete;
    TerrainStream &operator=(const TerrainStream &) = delete;
    void Open(PlanetGenerator generator);
    void Open(std::shared_ptr<const GeneratedPlanet> planet);
    void Close();
    // Main thread only. A published snapshot may be read on other threads while retained.
    void Update(const TangentFrame &focus, Vector3d velocityMeters = {},
                std::optional<SurfacePoint> destination = std::nullopt);
    bool Detailed(const SurfacePoint &point, double radiusMeters = 0) const;
    bool FailedAt(const SurfacePoint &point, double radiusMeters = 0) const;
    std::shared_ptr<const SurfaceSnapshot> Snapshot() const;
    GroundContact Contact(const SurfacePoint &point) const;
    const StreamSettings &Settings() const
    {
        return settings;
    }

  private:
    struct Data;
    std::unique_ptr<Data> data;
    StreamSettings settings;
    Builder builder;
    void Start(std::function<std::shared_ptr<const GeneratedPlanet>(const std::atomic_bool &)> prepare);
};
// ============================================================================
// SurfaceSession — authoritative spherical travel and a transported floating origin
// ============================================================================
class SurfaceSession
{
  public:
    SurfaceSession(PlanetDefinition planet, SurfacePoint spawn, StreamSettings settings = {});
    void Start(PlanetGenerator generator);
    void Start(std::shared_ptr<const GeneratedPlanet> planet);
    void Stop();
    void Update(Vector3d velocityMeters = {}, std::optional<SurfacePoint> destination = std::nullopt);
    bool Ready() const;
    const TangentFrame &Pose() const
    {
        return pose;
    }
    const TangentFrame &Origin() const
    {
        return origin;
    }
    const PlanetDefinition &Planet() const
    {
        return planet;
    }
    const TerrainStream &Stream() const
    {
        return stream;
    }
    Vector3d Local(const SurfacePoint &point) const;
    SurfacePoint PointAt(double rightMeters, double forwardMeters) const;
    GroundContact ContactAt(double rightMeters, double forwardMeters) const;
    bool Blocked(double rightMeters, double forwardMeters, bool hovering = false) const;
    // Swept terrain/object contact, <=0.4m substeps; missing chunks use the same global rules.
    void Move(double rightMeters, double forwardMeters, bool hovering = false);
    // Developer survey relocation. Same world/stream, safe land placement; no quest or travel unlock.
    bool Relocate(const SurfacePoint &point, double clearanceMeters = .65, double searchRadiusMeters = 32);
    // Remote landing checks do not project through the player's tangent plane or change the live pose.
    bool LandingClear(const SurfacePoint &point, double clearanceMeters = .65) const;
    std::optional<TangentFrame> Rebase();
    WorldLocation Location() const;
    std::size_t CollisionCells() const
    {
        return contentCollision.CachedCells();
    }
    std::vector<const SurfaceStructure *> NearbyStructures() const
    {
        return contentCollision.Structures();
    }

  private:
    PlanetDefinition planet;
    TangentFrame pose, origin;
    TerrainStream stream;
    mutable ContentCollision contentCollision;
};
} // namespace world
