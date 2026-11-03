#pragma once
#include "World.h"
#include <atomic>
#include <memory>
#include <set>
namespace tutorial
{
class SaveArchive;
}

namespace world
{
class CityCorePlan;
enum class Biome : std::uint8_t
{
    Ocean,
    Coast,
    Grassland,
    Forest,
    Desert,
    Rocky,
    Alpine,
    Ice
};
enum class RouteMode : std::uint8_t
{
    Road,
    Air
};
struct TerrainSample
{
    double elevationMeters = 0, slope = 0, moisture = 0, temperature = 0, minerals = 0;
    Biome biome = Biome::Ocean;
    bool land = false;
};
struct TerrainReservation
{
    std::string id;
    SurfacePoint center;
    double radiusMeters = 0, featherMeters = 0, elevationMeters = 0;
    std::uint8_t priority = 0;
};
// ============================================================================
// PlanetTerrain — stateless spherical noise and protected story land/anchor pads
// ============================================================================
class PlanetTerrain
{
  public:
    static constexpr double SeaLevelMeters = -120;
    PlanetTerrain(PlanetDefinition planet, std::vector<TerrainReservation> reservations = {},
                  std::optional<SurfacePoint> homeland = std::nullopt);
    double Height(const SurfacePoint &point) const;
    TerrainSample Sample(const SurfacePoint &point) const;
    // Reuse a contact grid's height/slope without resampling the height field.
    TerrainSample Environment(const SurfacePoint &point, double elevationMeters, double slope) const;
    const PlanetDefinition &Planet() const
    {
        return planet;
    }
    const std::vector<TerrainReservation> &Reservations() const
    {
        return reservations;
    }

    const std::optional<SurfacePoint> &Homeland() const
    {
        return homeland;
    }

  private:
    double Noise(Vector3d point, double frequency, std::uint64_t channel) const;
    double Fractal(Vector3d point, double frequency, unsigned octaves, std::uint64_t channel) const;
    PlanetDefinition planet;
    std::vector<TerrainReservation> reservations;
    std::optional<SurfacePoint> homeland;
    std::uint64_t terrainSeed = 0;
};
struct GenerationSettings
{
    unsigned extraRegions = 64, clusters = 12, placementAttempts = 256;
    double regionGapMeters = 375, clusterRadiusMeters = 16250;
    double roadLimitMeters = 45000, maximumRoadGrade = .09, relayCoverageMeters = 5000;
    void Validate() const;
};
struct FixedRegionPlacement
{
    std::string regionId;
    SurfacePoint access;
    SpaceKind space = SpaceKind::Surface;
    std::int32_t layer = 0;
    double depthMeters = 0;
};
struct GeneratedRegion
{
    RegionDefinition definition;
    WorldLocation center, access;
    // Convex, irregular local metre polygon. Its spherical area equals areaMeters2.
    TangentFrame frame;
    std::vector<Vector3d> footprint;
    double areaMeters2 = 0, boundingRadiusMeters = 0;
    Biome biome = Biome::Grassland;
};
struct RelayCandidate
{
    std::string id, regionId;
    WorldLocation location;
};
struct TravelNode
{
    std::string id, regionId;
    WorldLocation location;
    bool relay = false;
};
struct TravelConnection
{
    unsigned from = 0, to = 0;
    RouteMode mode = RouteMode::Road;
    std::vector<SurfacePoint> points;
    double lengthMeters = 0;
};
// ============================================================================
// GeneratedPlanet — immutable-by-convention manifest for streaming and later systems
// ============================================================================
class GeneratedPlanet
{
  public:
    static constexpr unsigned CurrentLayoutRevision = 4, LegacyLayoutRevision = 3;
    unsigned layoutRevision = CurrentLayoutRevision;
    explicit GeneratedPlanet(PlanetTerrain terrain);
    PlanetTerrain terrain;
    std::vector<StoryAnchor> anchors;
    std::vector<GeneratedRegion> regions;
    std::vector<RelayCandidate> relays;
    std::vector<TravelNode> nodes;
    std::vector<TravelConnection> connections;
    std::vector<SurfacePoint> clusters;
    std::shared_ptr<const CityCorePlan> cityCores;
    double relayCoverageMeters = 5000;
    double regionGapMeters = 1500, maximumRoadGrade = .09;
    const GeneratedRegion &Region(std::string_view id) const;
    // Conservative geometry query, independent of visit/load order. Includes surface entrances.
    std::vector<std::size_t> RegionsInChunk(const ChunkKey &key) const;
    bool Connected(bool roadsOnly = false) const;
    std::uint64_t Digest() const;
    void Validate() const;
};
// ============================================================================
// RouteSpatialIndex — conservative spherical caps for immutable connections, without render state
// ============================================================================
class RouteSpatialIndex
{
  public:
    explicit RouteSpatialIndex(const GeneratedPlanet &planet);
    std::vector<std::size_t> Query(const SurfacePoint &focus, double radiusMeters) const;

  private:
    struct Bound
    {
        Vector3d center;
        double angle = 0;
    };
    double planetRadius = 0;
    std::vector<Bound> bounds;
};

// ============================================================================
// RelayNetwork — per-world discoveries/registrations, independent of mission participation
// ============================================================================
class RelayNetwork
{
    friend class tutorial::SaveArchive;

  public:
    static constexpr double DiscoverMeters = 100, UseMeters = 5;
    void Bind(const PlanetDefinition &planet);
    const std::string &WorldKey() const
    {
        return worldKey;
    }
    void Observe(const GeneratedPlanet &planet, const WorldLocation &location);
    bool Register(const GeneratedPlanet &planet, std::string_view id, const WorldLocation &location);
    bool Discovered(std::string_view id) const;
    bool Registered(std::string_view id) const;
    bool RegionVisited(std::string_view id) const;
    const std::set<std::string> &Discoveries() const
    {
        return discovered;
    }
    const std::set<std::string> &Registrations() const
    {
        return registered;
    }
    const std::set<std::string> &VisitedRegions() const
    {
        return visited;
    }
    static const RelayCandidate *Find(const GeneratedPlanet &planet, std::string_view id);
    static const RelayCandidate *Nearest(const GeneratedPlanet &planet, const SurfacePoint &position,
                                         double rangeMeters = UseMeters);

  private:
    std::string worldKey;
    std::set<std::string> discovered, registered, visited;
};
// ============================================================================
// PlanetGenerator — fixed reservations first, then seeded sites, relays and routes
// ============================================================================
class PlanetGenerator
{
  public:
    PlanetGenerator(PlanetDefinition planet, std::vector<RegionDefinition> definitions,
                    std::vector<FixedRegionPlacement> fixed, std::vector<StoryAnchor> anchors,
                    GenerationSettings settings = {});
    // Cancellation does not alter generated values or the generator version.
    GeneratedPlanet Generate(const std::atomic_bool *cancel = nullptr) const;
    const PlanetDefinition &Planet() const
    {
        return planet;
    }
    PlanetGenerator WithSeed(std::uint64_t seed) const;
    PlanetGenerator WithCityCores(const CityCorePlan &cores) const;
    PlanetGenerator WithLayoutRevision(unsigned revision) const;
    static PlanetGenerator Erma(const std::vector<StoryAnchor> &anchors, GenerationSettings settings = {});

  private:
    PlanetDefinition planet;
    std::vector<RegionDefinition> definitions;
    std::vector<FixedRegionPlacement> fixed;
    std::vector<StoryAnchor> anchors;
    GenerationSettings settings;
    std::shared_ptr<const CityCorePlan> cityCores;
    unsigned layoutRevision = GeneratedPlanet::CurrentLayoutRevision;
};
// ============================================================================
// PlanetProfiles — reproducible presets; only Erma owns the existing story actors
// ============================================================================
class PlanetProfiles
{
  public:
    static const std::vector<PlanetDefinition> &All();
    static const PlanetDefinition *Find(std::string_view id);
    static bool Supported(const PlanetDefinition &planet);
    static PlanetGenerator Generator(const PlanetDefinition &planet,
                                     const std::vector<StoryAnchor> &ermaAnchors = {});
};
} // namespace world
