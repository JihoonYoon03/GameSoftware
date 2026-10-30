#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace world
{
// CQ-01A: persisted values/IDs are independent of display names and local render coordinates.
enum class SettlementTier : std::uint8_t
{
    None = 0,
    District = 1,
    Town = 2,
    Metropolis = 3,
    SystemCapital = 4
};
enum class RegionKind : std::uint8_t
{
    Settlement = 0,
    Wilderness = 1,
    Outpost = 2,
    Mine = 3,
    Outlaw = 4,
    AncientRuins = 5
};
enum class SpaceKind : std::uint8_t
{
    Surface = 0,
    Subsurface = 1,
    Interior = 2,
    AnomalousDomain = 3
};
struct Vector3d
{
    double x = 0, y = 0, z = 0;
};
struct SurfacePoint
{
    Vector3d normal = {1, 0, 0};
    double altitudeMeters = 0;
};
struct TangentFrame
{
    SurfacePoint origin;
    Vector3d right = {0, 1, 0}, forward = {0, 0, 1};
};
struct PlanetDefinition
{
    std::string id, name, systemId;
    std::uint64_t seed = 0;
    std::uint32_t generatorVersion = 1;
    double radiusMeters = 0;
    bool storyPlanet = false;
    void Validate() const;
    bool SameWorld(const PlanetDefinition &other) const;
};
struct RegionDefinition
{
    std::string id, name, planetId;
    RegionKind kind = RegionKind::Wilderness;
    SettlementTier tier = SettlementTier::None;
    bool storyFixed = false;
    // City areas are targets; generating a footprint belongs to CQ-01B/D.
    double referenceAreaKm2 = 0;
    std::string areaReference;
    void Validate(const PlanetDefinition &planet) const;
    double TargetAreaKm2() const;
};
struct WorldLocation
{
    std::string planetId, regionId;
    std::uint64_t planetSeed = 0;
    std::uint32_t generatorVersion = 1;
    SpaceKind space = SpaceKind::Surface;
    std::int32_t layer = 0;
    std::string domainId;
    SurfacePoint position;
    // In an anomalous domain, position is its planetary return anchor, not its interior geometry.
    Vector3d domainLocalMeters;
    void Validate(const PlanetDefinition &planet) const;
    bool SameSpace(const WorldLocation &other) const;
};
struct StoryAnchor
{
    std::string id;
    WorldLocation location;
    void Validate(const PlanetDefinition &planet) const;
};
// ============================================================================
// WorldIdentity — stable serialization keys and portable, versioned random streams
// ============================================================================
class WorldIdentity
{
  public:
    static bool ValidId(std::string_view id);
    static std::uint64_t StreamSeed(const PlanetDefinition &planet, std::string_view regionId,
                                    std::string_view streamId);
    static std::string ContentKey(const PlanetDefinition &planet, std::string_view regionId,
                                  std::string_view slotId);
};
// ============================================================================
// SurfaceGeometry — double-precision sphere, exponential/log maps and transport
// ============================================================================
class SurfaceGeometry
{
  public:
    static constexpr double Pi = 3.1415926535897932384626433832795;
    static Vector3d Unit(Vector3d value);
    static double Dot(Vector3d a, Vector3d b);
    static Vector3d Cross(Vector3d a, Vector3d b);
    static SurfacePoint FromDegrees(double latitude, double longitude, double altitudeMeters = 0);
    static std::array<double, 2> ToDegrees(const SurfacePoint &point);
    static TangentFrame Frame(const SurfacePoint &origin);
    static void Validate(const SurfacePoint &point);
    static void Validate(const TangentFrame &frame);
    static double Circumference(double radiusMeters);
    static double Distance(double radiusMeters, const SurfacePoint &a, const SurfacePoint &b);
    static Vector3d Cartesian(double radiusMeters, const SurfacePoint &point);
    static Vector3d TransformDirection(Vector3d localDirection, const TangentFrame &from,
                                       const TangentFrame &to);
    // Transport the entire local basis; holding a local direction follows a great circle.
    static TangentFrame Advance(const TangentFrame &frame, double rightMeters, double forwardMeters,
                                double radiusMeters);
    // A local patch is injective only below half a circumference. Antipodes are ambiguous.
    static SurfacePoint FromLocal(const TangentFrame &frame, Vector3d localMeters, double radiusMeters);
    static Vector3d ToLocal(const TangentFrame &frame, const SurfacePoint &point, double radiusMeters);
};
// ============================================================================
// CubeSphere — canonical ownership and same-level adjacency across all six faces
// ============================================================================
enum class CubeFace : std::uint8_t
{
    PositiveX = 0,
    NegativeX = 1,
    PositiveY = 2,
    NegativeY = 3,
    PositiveZ = 4,
    NegativeZ = 5
};
enum class ChunkEdge : std::uint8_t
{
    Left = 0,
    Right = 1,
    Down = 2,
    Up = 3
};
struct ChunkKey
{
    std::string planetId;
    std::uint64_t planetSeed = 0;
    std::uint32_t generatorVersion = 1;
    CubeFace face = CubeFace::PositiveX;
    std::uint8_t level = 0;
    std::uint32_t x = 0, y = 0;
    std::int32_t layer = 0;
    void Validate() const;
    std::string StableKey() const;
    bool operator==(const ChunkKey &other) const;
    bool operator<(const ChunkKey &other) const;
};
class CubeSphere
{
  public:
    static constexpr std::uint8_t MaximumLevel = 30;
    static ChunkKey Address(const PlanetDefinition &planet, const SurfacePoint &point, std::uint8_t level,
                            std::int32_t layer = 0);
    static SurfacePoint Center(const ChunkKey &key);
    static SurfacePoint Corner(const ChunkKey &key, unsigned right, unsigned up);
    static SurfacePoint Point(const ChunkKey &key, double right, double up);
    static std::array<double, 2> Coordinates(const ChunkKey &key, const SurfacePoint &point);
    static ChunkKey Neighbor(const ChunkKey &key, ChunkEdge edge);
    static std::array<ChunkKey, 4> Children(const ChunkKey &key);
    static ChunkKey Parent(const ChunkKey &key);
};
// ============================================================================
// WorldCatalog — versioned metadata; it does not generate or activate new regions
// ============================================================================
class WorldCatalog
{
  public:
    static constexpr double CityAreaRatio = .25;
    static constexpr double CityLengthRatio = .5;
    static constexpr double MetersPerLegacyUnit = 2;
    static double CityAreaKm2(double referenceAreaKm2);
    static bool AllowsInstancing(SpaceKind space);
    static const PlanetDefinition &Erma();
    static const std::vector<RegionDefinition> &ErmaRegions();
    static const RegionDefinition &Region(std::string_view id);
};
// ============================================================================
// LegacyWorldAdapter — read-only bridge; old local maps remain authoritative in A
// ============================================================================
class LegacyWorldAdapter
{
  public:
    LegacyWorldAdapter(PlanetDefinition planet, TangentFrame frame, double metersPerUnit = 2);
    WorldLocation Locate(std::string regionId, Vector3d legacyPosition, SpaceKind space = SpaceKind::Surface,
                         std::int32_t layer = 0) const;
    Vector3d Local(const WorldLocation &location) const;
    StoryAnchor Anchor(std::string id, std::string regionId, Vector3d legacyPosition) const;
    static const LegacyWorldAdapter &ErmaExterior();

  private:
    PlanetDefinition planet;
    TangentFrame frame;
    double metersPerUnit;
};
} // namespace world
