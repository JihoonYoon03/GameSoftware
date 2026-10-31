#pragma once
#include "WorldGeneration.h"
#include <map>

namespace world
{
// Resource-independent forms. Only the renderer resolves these to shared imported models.
enum class ContentForm : std::uint8_t
{
    Habitat,
    Terrace,
    Workshop,
    Dome,
    Spire,
    TwinBridge,
    Arcology,
    Transit,
    Courtyard,
    Shelter,
    Cargo,
    Relay,
    Solar,
    WaterTank,
    Conveyor,
    Monolith,
    Arch
};
enum class ContentUse : std::uint8_t
{
    Housing,
    Industry,
    Services,
    Administration,
    Expedition,
    Mining,
    Salvage,
    Archaeology
};
struct SurfaceStructure
{
    std::string id, regionId;
    ContentForm form = ContentForm::Habitat;
    ContentUse use = ContentUse::Housing;
    TangentFrame frame;
    Vector3d sizeMeters;
    double yaw = 0, foundationMeters = 0;
    std::uint64_t appearance = 0;
    bool solid = true;
    bool Contains(const SurfacePoint &point, double planetRadius, double clearance = 0) const;
};
struct SurfaceStreet
{
    std::vector<SurfacePoint> points;
    double widthMeters = 6, sidewalkMeters = 0;
    unsigned lanes = 2;
};
struct ContentCellKey
{
    std::size_t region = 0;
    std::int64_t x = 0, y = 0;
    bool operator<(const ContentCellKey &other) const;
    bool operator==(const ContentCellKey &other) const;
};
struct ContentCell
{
    ContentCellKey key;
    std::vector<SurfaceStructure> structures;
    std::vector<SurfaceStreet> streets;
};
struct SurfaceContentPatch
{
    std::vector<SurfaceStructure> structures;
    std::vector<SurfaceStreet> streets;
};
// ============================================================================
// ContentLayout — seeded, bounded neighbourhoods; no GL, assets, quests or visit state
// ============================================================================
class ContentLayout
{
  public:
    static constexpr unsigned Revision = 1;
    static std::vector<ContentCellKey> Near(const GeneratedPlanet &planet, const SurfacePoint &point,
                                            double radiusMeters);
    static ContentCell Generate(const GeneratedPlanet &planet, ContentCellKey key,
                                const std::atomic_bool *cancel = nullptr);
    // Canonical centre ownership for objects; street fragments own their midpoint's chunk.
    static SurfaceContentPatch Publish(const GeneratedPlanet &planet, const ChunkKey &key,
                                       double radiusMeters, const std::atomic_bool &cancel);
    static const char *UseName(ContentUse use);
};
// ============================================================================
// ContentCollision — bounded local cache; collision never depends on a render chunk arriving
// ============================================================================
class ContentCollision
{
  public:
    static constexpr std::size_t MaximumCells = 64;
    bool Blocked(const GeneratedPlanet &planet, const SurfacePoint &point, double clearance);
    void Clear();
    std::size_t CachedCells() const
    {
        return cells.size();
    }
    // Main-thread views, valid until the next collision query/cache clear.
    std::vector<const SurfaceStructure *> Structures() const;

  private:
    struct Entry
    {
        ContentCell cell;
        std::uint64_t touched = 0;
    };
    std::map<ContentCellKey, Entry> cells;
    std::optional<PlanetDefinition> owner;
    std::uint64_t clock = 0;
};
} // namespace world
