#include "stdafx.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace world
{
namespace
{
Vector3d Add(Vector3d a, Vector3d b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vector3d Scale(Vector3d v, double s)
{
    return {v.x * s, v.y * s, v.z * s};
}
double Length(Vector3d v)
{
    return std::hypot(std::hypot(v.x, v.y), v.z);
}
bool Finite(Vector3d v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
void Require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::invalid_argument(message);
    }
}
void Radius(double value)
{
    Require(std::isfinite(value) && value > 0, "Planet radius must be finite and positive");
}
void Id(std::string_view value)
{
    Require(WorldIdentity::ValidId(value), "Invalid persistent world ID");
}
Vector3d Rotate(Vector3d v, Vector3d axis, double angle)
{
    double c = std::cos(angle), s = std::sin(angle);
    return Add(Add(Scale(v, c), Scale(SurfaceGeometry::Cross(axis, v), s)),
               Scale(axis, SurfaceGeometry::Dot(axis, v) * (1 - c)));
}
Vector3d CubeVector(CubeFace face, double u, double v)
{
    switch (face)
    {
    case CubeFace::PositiveX:
        return {1, u, v};
    case CubeFace::NegativeX:
        return {-1, -u, v};
    case CubeFace::PositiveY:
        return {-u, 1, v};
    case CubeFace::NegativeY:
        return {u, -1, v};
    case CubeFace::PositiveZ:
        return {-v, u, 1};
    case CubeFace::NegativeZ:
        return {v, u, -1};
    }
    throw std::invalid_argument("Invalid cube face");
}
CubeFace Face(Vector3d v)
{
    double x = std::fabs(v.x), y = std::fabs(v.y), z = std::fabs(v.z);
    // Edge/corner ownership is canonical: X before Y before Z.
    if (x >= y && x >= z)
    {
        return v.x >= 0 ? CubeFace::PositiveX : CubeFace::NegativeX;
    }
    if (y >= z)
    {
        return v.y >= 0 ? CubeFace::PositiveY : CubeFace::NegativeY;
    }
    return v.z >= 0 ? CubeFace::PositiveZ : CubeFace::NegativeZ;
}
std::array<double, 2> FaceUV(CubeFace face, Vector3d v)
{
    switch (face)
    {
    case CubeFace::PositiveX:
        return {v.y / std::fabs(v.x), v.z / std::fabs(v.x)};
    case CubeFace::NegativeX:
        return {-v.y / std::fabs(v.x), v.z / std::fabs(v.x)};
    case CubeFace::PositiveY:
        return {-v.x / std::fabs(v.y), v.z / std::fabs(v.y)};
    case CubeFace::NegativeY:
        return {v.x / std::fabs(v.y), v.z / std::fabs(v.y)};
    case CubeFace::PositiveZ:
        return {v.y / std::fabs(v.z), -v.x / std::fabs(v.z)};
    case CubeFace::NegativeZ:
        return {v.y / std::fabs(v.z), v.x / std::fabs(v.z)};
    }
    throw std::invalid_argument("Invalid cube face");
}
void HashInteger(std::uint64_t &hash, std::uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
    {
        hash ^= (value >> (i * 8)) & 255u;
        hash *= 1099511628211ull;
    }
}
void HashText(std::uint64_t &hash, std::string_view value)
{
    HashInteger(hash, value.size(), 4);
    for (unsigned char byte : value)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
}
} // namespace

// ============================================================================
// World definitions / identity — immutable contracts, no geometry generation
// ============================================================================
bool WorldIdentity::ValidId(std::string_view id)
{
    if (id.empty() || id.size() > 96)
    {
        return false;
    }
    for (unsigned char c : id)
    {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-' || c == '.'))
        {
            return false;
        }
    }
    return id != "." && id != "..";
}
void PlanetDefinition::Validate() const
{
    Id(id);
    Id(systemId);
    Require(!name.empty() && generatorVersion > 0, "Planet name/version missing");
    Radius(radiusMeters);
    Require(std::isfinite(4 * SurfaceGeometry::Pi * radiusMeters * radiusMeters), "Planet area overflow");
}
bool PlanetDefinition::SameWorld(const PlanetDefinition &other) const
{
    return id == other.id && systemId == other.systemId && seed == other.seed &&
           generatorVersion == other.generatorVersion && radiusMeters == other.radiusMeters;
}
void RegionDefinition::Validate(const PlanetDefinition &planet) const
{
    planet.Validate();
    Id(id);
    Require(planetId == planet.id && !name.empty(), "Region must belong to its named planet");
    Require(unsigned(kind) <= unsigned(RegionKind::AncientRuins) &&
                unsigned(tier) <= unsigned(SettlementTier::SystemCapital),
            "Invalid region kind/tier");
    bool city = kind == RegionKind::Settlement;
    Require(city == (tier != SettlementTier::None), "Only settlements have a city tier");
    if (city)
    {
        Require(!areaReference.empty() && TargetAreaKm2() > 0, "City area reference missing");
        Require(TargetAreaKm2() * 1.e6 < 4 * SurfaceGeometry::Pi * planet.radiusMeters * planet.radiusMeters,
                "City footprint exceeds planet area");
    }
    else
    {
        Require(referenceAreaKm2 == 0, "Non-city has a city reference area");
    }
}
double RegionDefinition::TargetAreaKm2() const
{
    return WorldCatalog::CityAreaKm2(referenceAreaKm2);
}
void WorldLocation::Validate(const PlanetDefinition &planet) const
{
    planet.Validate();
    Id(regionId);
    Require(planetId == planet.id && planetSeed == planet.seed && generatorVersion == planet.generatorVersion,
            "Location belongs to another planet seed/version");
    Require(unsigned(space) <= unsigned(SpaceKind::AnomalousDomain), "Invalid space kind");
    SurfaceGeometry::Validate(position);
    Require(position.altitudeMeters > -planet.radiusMeters && Finite(domainLocalMeters),
            "Invalid world height/position");
    if (space == SpaceKind::AnomalousDomain)
    {
        Id(domainId);
    }
    else
    {
        Require(domainId.empty() && Length(domainLocalMeters) == 0,
                "Planetary location contains domain coordinates");
    }
    if (space == SpaceKind::Surface)
    {
        Require(layer == 0, "Surface location must use layer zero");
    }
}
bool WorldLocation::SameSpace(const WorldLocation &other) const
{
    return !planetId.empty() && planetId == other.planetId && planetSeed == other.planetSeed &&
           generatorVersion == other.generatorVersion && space == other.space && layer == other.layer &&
           domainId == other.domainId;
}
void StoryAnchor::Validate(const PlanetDefinition &planet) const
{
    Id(id);
    location.Validate(planet);
}
std::uint64_t WorldIdentity::StreamSeed(const PlanetDefinition &planet, std::string_view regionId,
                                        std::string_view streamId)
{
    planet.Validate();
    Id(regionId);
    Id(streamId);
    std::uint64_t hash = 14695981039346656037ull;
    HashInteger(hash, 1, 4); // Identity stream encoding version; fixed little endian.
    HashText(hash, planet.id);
    HashInteger(hash, planet.seed, 8);
    HashInteger(hash, planet.generatorVersion, 4);
    HashText(hash, regionId);
    HashText(hash, streamId);
    return hash;
}
std::string WorldIdentity::ContentKey(const PlanetDefinition &planet, std::string_view regionId,
                                      std::string_view slotId)
{
    planet.Validate();
    Id(regionId);
    Id(slotId);
    return "p/" + planet.id + "/s/" + std::to_string(planet.seed) + "/v/" +
           std::to_string(planet.generatorVersion) + "/r/" + std::string(regionId) + "/e/" +
           std::string(slotId);
}

// ============================================================================
// SurfaceGeometry — geographic coordinates never pass through single precision
// ============================================================================
double SurfaceGeometry::Dot(Vector3d a, Vector3d b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vector3d SurfaceGeometry::Cross(Vector3d a, Vector3d b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vector3d SurfaceGeometry::Unit(Vector3d v)
{
    double length = Length(v);
    Require(Finite(v) && std::isfinite(length) && length > 0, "Invalid direction vector");
    return {v.x / length, v.y / length, v.z / length};
}
void SurfaceGeometry::Validate(const SurfacePoint &p)
{
    Require(Finite(p.normal) && std::isfinite(p.altitudeMeters) && std::fabs(Length(p.normal) - 1) < 1.e-10,
            "Surface point requires a finite unit normal and height");
}
void SurfaceGeometry::Validate(const TangentFrame &f)
{
    Validate(f.origin);
    Require(Finite(f.right) && Finite(f.forward) && std::fabs(Length(f.right) - 1) < 1.e-9 &&
                std::fabs(Length(f.forward) - 1) < 1.e-9 && std::fabs(Dot(f.right, f.forward)) < 1.e-9 &&
                std::fabs(Dot(f.right, f.origin.normal)) < 1.e-9 &&
                std::fabs(Dot(f.forward, f.origin.normal)) < 1.e-9 &&
                Dot(Cross(f.right, f.forward), f.origin.normal) > 1 - 1.e-9,
            "Invalid tangent basis");
}
SurfacePoint SurfaceGeometry::FromDegrees(double latitude, double longitude, double altitude)
{
    Require(std::isfinite(latitude) && std::fabs(latitude) <= 90 && std::isfinite(longitude) &&
                std::isfinite(altitude),
            "Invalid latitude/longitude/altitude");
    double lat = latitude * Pi / 180, lon = std::remainder(longitude, 360.) * Pi / 180;
    return {Unit({std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)}), altitude};
}
std::array<double, 2> SurfaceGeometry::ToDegrees(const SurfacePoint &p)
{
    Validate(p);
    return {std::atan2(p.normal.z, std::hypot(p.normal.x, p.normal.y)) * 180 / Pi,
            std::atan2(p.normal.y, p.normal.x) * 180 / Pi};
}
TangentFrame SurfaceGeometry::Frame(const SurfacePoint &p)
{
    Validate(p);
    Vector3d right =
        std::hypot(p.normal.x, p.normal.y) > 1.e-12 ? Unit(Cross({0, 0, 1}, p.normal)) : Vector3d{0, 1, 0};
    // At the poles choose a deterministic meridian, then transport rather than recomputing it.
    right = Unit(Add(right, Scale(p.normal, -Dot(right, p.normal))));
    return {p, right, Unit(Cross(p.normal, right))};
}
double SurfaceGeometry::Circumference(double radius)
{
    Radius(radius);
    return 2 * Pi * radius;
}
double SurfaceGeometry::Distance(double radius, const SurfacePoint &a, const SurfacePoint &b)
{
    Radius(radius);
    Validate(a);
    Validate(b);
    // atan2 preserves millimetre-scale angles that acos(dot) would round to zero.
    return radius *
           std::atan2(Length(Cross(a.normal, b.normal)), std::clamp(Dot(a.normal, b.normal), -1., 1.));
}
Vector3d SurfaceGeometry::Cartesian(double radius, const SurfacePoint &p)
{
    Radius(radius);
    Validate(p);
    Require(std::isfinite(radius + p.altitudeMeters) && radius + p.altitudeMeters > 0,
            "Height reaches planet center");
    return Scale(p.normal, radius + p.altitudeMeters);
}
Vector3d SurfaceGeometry::TransformDirection(Vector3d local, const TangentFrame &from, const TangentFrame &to)
{
    Validate(from);
    Validate(to);
    Require(Finite(local), "Non-finite local direction");
    auto direction = Add(Add(Scale(from.right, local.x), Scale(from.forward, local.y)),
                         Scale(from.origin.normal, local.z));
    return {Dot(direction, to.right), Dot(direction, to.forward), Dot(direction, to.origin.normal)};
}
TangentFrame SurfaceGeometry::Advance(const TangentFrame &f, double right, double forward, double radius)
{
    Radius(radius);
    Validate(f);
    Require(std::isfinite(right) && std::isfinite(forward), "Non-finite surface displacement");
    double distance = std::hypot(right, forward);
    Require(std::isfinite(distance) && std::isfinite(distance / radius), "Surface displacement overflow");
    if (distance == 0)
    {
        return f;
    }
    Vector3d direction = Add(Scale(f.right, right / distance), Scale(f.forward, forward / distance));
    Vector3d axis = Unit(Cross(f.origin.normal, direction));
    double angle = std::remainder(distance / radius, 2 * Pi);
    TangentFrame result = f;
    result.origin.normal = Unit(Rotate(f.origin.normal, axis, angle));
    result.right = Rotate(f.right, axis, angle);
    result.right =
        Unit(Add(result.right, Scale(result.origin.normal, -Dot(result.right, result.origin.normal))));
    result.forward = Unit(Cross(result.origin.normal, result.right));
    return result;
}
SurfacePoint SurfaceGeometry::FromLocal(const TangentFrame &f, Vector3d local, double radius)
{
    Radius(radius);
    Validate(f);
    Require(Finite(local) && std::hypot(local.x, local.y) / radius < Pi - 1.e-10,
            "Local patch reaches ambiguous antipode");
    auto p = Advance(f, local.x, local.y, radius).origin;
    p.altitudeMeters += local.z;
    Require(std::isfinite(p.altitudeMeters) && p.altitudeMeters > -radius, "Invalid local height");
    return p;
}
Vector3d SurfaceGeometry::ToLocal(const TangentFrame &f, const SurfacePoint &p, double radius)
{
    Radius(radius);
    Validate(f);
    Validate(p);
    double dot = std::clamp(Dot(f.origin.normal, p.normal), -1., 1.);
    double sine = Length(Cross(f.origin.normal, p.normal));
    double angle = std::atan2(sine, dot);
    if (angle >= Pi - 1.e-10)
    {
        throw std::domain_error("Antipode has no unique local direction");
    }
    if (sine < 1.e-15)
    {
        return {0, 0, p.altitudeMeters - f.origin.altitudeMeters};
    }
    auto tangent = Scale(Add(p.normal, Scale(f.origin.normal, -dot)), 1 / sine);
    return {Dot(tangent, f.right) * angle * radius, Dot(tangent, f.forward) * angle * radius,
            p.altitudeMeters - f.origin.altitudeMeters};
}

// ============================================================================
// ChunkKey / CubeSphere — exact integer edge transforms, including very fine LOD
// ============================================================================
void ChunkKey::Validate() const
{
    Id(planetId);
    Require(generatorVersion > 0 && unsigned(face) <= unsigned(CubeFace::NegativeZ) &&
                level <= CubeSphere::MaximumLevel,
            "Invalid chunk version/face/level");
    std::uint64_t cells = 1ull << level;
    Require(x < cells && y < cells, "Chunk coordinates exceed face bounds");
}
bool ChunkKey::operator==(const ChunkKey &b) const
{
    return std::tie(planetId, planetSeed, generatorVersion, face, level, x, y, layer) ==
           std::tie(b.planetId, b.planetSeed, b.generatorVersion, b.face, b.level, b.x, b.y, b.layer);
}
bool ChunkKey::operator<(const ChunkKey &b) const
{
    return std::tie(planetId, planetSeed, generatorVersion, face, level, x, y, layer) <
           std::tie(b.planetId, b.planetSeed, b.generatorVersion, b.face, b.level, b.x, b.y, b.layer);
}
std::string ChunkKey::StableKey() const
{
    Validate();
    return "p/" + planetId + "/s/" + std::to_string(planetSeed) + "/v/" + std::to_string(generatorVersion) +
           "/l/" + std::to_string(layer) + "/f/" + std::to_string(unsigned(face)) + "/q/" +
           std::to_string(level) + "/x/" + std::to_string(x) + "/y/" + std::to_string(y);
}
ChunkKey CubeSphere::Address(const PlanetDefinition &planet, const SurfacePoint &p, std::uint8_t level,
                             std::int32_t layer)
{
    planet.Validate();
    SurfaceGeometry::Validate(p);
    Require(level <= MaximumLevel, "Chunk level exceeds supported integer precision");
    auto face = Face(p.normal);
    auto uv = FaceUV(face, p.normal);
    std::uint64_t cells = 1ull << level;
    auto cell = [cells](double v) {
        return std::uint32_t(std::clamp(std::floor((v + 1) * .5 * double(cells)), 0., double(cells - 1)));
    };
    return {planet.id, planet.seed, planet.generatorVersion, face, level, cell(uv[0]), cell(uv[1]), layer};
}
SurfacePoint CubeSphere::Center(const ChunkKey &key)
{
    key.Validate();
    double cells = double(1ull << key.level);
    return {SurfaceGeometry::Unit(CubeVector(key.face, (2 * double(key.x) + 1) / cells - 1,
                                             (2 * double(key.y) + 1) / cells - 1)),
            0};
}
SurfacePoint CubeSphere::Corner(const ChunkKey &key, unsigned right, unsigned up)
{
    key.Validate();
    Require(right <= 1 && up <= 1, "Invalid chunk corner");
    double cells = double(1ull << key.level);
    return {SurfaceGeometry::Unit(CubeVector(key.face, 2 * (double(key.x) + right) / cells - 1,
                                             2 * (double(key.y) + up) / cells - 1)),
            0};
}
ChunkKey CubeSphere::Neighbor(const ChunkKey &key, ChunkEdge edge)
{
    key.Validate();
    Require(unsigned(edge) <= unsigned(ChunkEdge::Up), "Invalid chunk edge");
    auto result = key;
    std::uint32_t last = std::uint32_t((1ull << key.level) - 1);
    if (edge == ChunkEdge::Left && key.x > 0)
    {
        --result.x;
        return result;
    }
    if (edge == ChunkEdge::Right && key.x < last)
    {
        ++result.x;
        return result;
    }
    if (edge == ChunkEdge::Down && key.y > 0)
    {
        --result.y;
        return result;
    }
    if (edge == ChunkEdge::Up && key.y < last)
    {
        ++result.y;
        return result;
    }
    bool vertical = edge == ChunkEdge::Left || edge == ChunkEdge::Right;
    double sign = edge == ChunkEdge::Left || edge == ChunkEdge::Down ? -1. : 1.;
    result.face = Face(CubeVector(key.face, vertical ? sign * 1.01 : 0, vertical ? 0 : sign * 1.01));
    auto middle = FaceUV(result.face, CubeVector(key.face, vertical ? sign : 0, vertical ? 0 : sign));
    auto along = FaceUV(result.face, CubeVector(key.face, vertical ? sign : .5, vertical ? .5 : sign));
    std::uint32_t index = vertical ? key.y : key.x;
    // Project only the axis orientation; never floor projected floats at a seam.
    if (std::fabs(middle[0]) > .5)
    {
        result.x = middle[0] < 0 ? 0 : last;
        result.y = along[1] > 0 ? index : last - index;
    }
    else
    {
        result.y = middle[1] < 0 ? 0 : last;
        result.x = along[0] > 0 ? index : last - index;
    }
    return result;
}
std::array<ChunkKey, 4> CubeSphere::Children(const ChunkKey &key)
{
    key.Validate();
    Require(key.level < MaximumLevel, "Leaf level cannot be subdivided");
    std::array<ChunkKey, 4> result;
    for (unsigned i = 0; i < 4; ++i)
    {
        result[i] = key;
        ++result[i].level;
        result[i].x = key.x * 2 + (i % 2);
        result[i].y = key.y * 2 + (i / 2);
    }
    return result;
}
ChunkKey CubeSphere::Parent(const ChunkKey &key)
{
    key.Validate();
    Require(key.level > 0, "Root face has no parent");
    auto result = key;
    --result.level;
    result.x /= 2;
    result.y /= 2;
    return result;
}

// ============================================================================
// WorldCatalog — areas other than Seoul are explicit design reference assumptions
// ============================================================================
double WorldCatalog::CityAreaKm2(double reference)
{
    Require(std::isfinite(reference) && reference >= 0, "Invalid city reference area");
    return reference * CityAreaRatio;
}
bool WorldCatalog::AllowsInstancing(SpaceKind space)
{
    return space == SpaceKind::AnomalousDomain;
}
const PlanetDefinition &WorldCatalog::Erma()
{
    static const PlanetDefinition result = {"erma", "에르마", "erma_system", 0x458a91ull, 1, 4800000, true};
    return result;
}
const std::vector<RegionDefinition> &WorldCatalog::ErmaRegions()
{
    static const std::vector<RegionDefinition> result = {
        {"halo_07", "헤일로", "erma", RegionKind::Settlement, SettlementTier::District, true, 4,
         "design-reference:district-v1"},
        {"cinder", "신더", "erma", RegionKind::Settlement, SettlementTier::District, false, 4,
         "design-reference:district-v1"},
        {"veil", "베일", "erma", RegionKind::Settlement, SettlementTier::District, false, 4,
         "design-reference:district-v1"},
        {"lumen", "루멘", "erma", RegionKind::Settlement, SettlementTier::District, false, 4,
         "design-reference:district-v1"},
        {"echo", "에코", "erma", RegionKind::Settlement, SettlementTier::District, false, 4,
         "design-reference:district-v1"},
        {"karden", "카르덴", "erma", RegionKind::Settlement, SettlementTier::Town, false, 64,
         "design-reference:town-v1"},
        {"asterion", "아스테리온", "erma", RegionKind::Settlement, SettlementTier::Metropolis, false, 605.25,
         "Seoul-2025:605.25km2:https://data.seoul.go.kr/dataVisual/seoul/seoul100List.do"},
        {"lucerne_prime", "루케른 프라임", "erma", RegionKind::Settlement, SettlementTier::SystemCapital,
         false, 1200, "design-reference:system-capital-v1"},
        {"calyx_outpost", "칼릭스 전초기지", "erma", RegionKind::Outpost},
        {"mq00_mine", "오르딘 광산", "erma", RegionKind::Mine, SettlementTier::None, true},
        {"grey_basin", "회색분지", "erma", RegionKind::Outlaw},
        {"white_ring_ruins", "백색환 유적지", "erma", RegionKind::AncientRuins},
        {"frontier", "빛나는 개척지", "erma", RegionKind::Wilderness, SettlementTier::None, true},
        {"n0_outskirts", "N-0 외곽", "erma", RegionKind::AncientRuins, SettlementTier::None, true},
        {"overworld", "에르마 야외", "erma", RegionKind::Wilderness}};
    return result;
}
const RegionDefinition &WorldCatalog::Region(std::string_view id)
{
    for (const auto &region : ErmaRegions())
    {
        if (region.id == id)
        {
            return region;
        }
    }
    throw std::out_of_range("Unknown Erma region ID");
}

// ============================================================================
// LegacyWorldAdapter — explicit planet/seed/layer checks; no hidden map activation
// ============================================================================
LegacyWorldAdapter::LegacyWorldAdapter(PlanetDefinition p, TangentFrame f, double units)
    : planet(std::move(p)), frame(f), metersPerUnit(units)
{
    planet.Validate();
    SurfaceGeometry::Validate(frame);
    Require(std::isfinite(units) && units > 0, "Invalid legacy world unit scale");
}
WorldLocation LegacyWorldAdapter::Locate(std::string region, Vector3d local, SpaceKind space,
                                         std::int32_t layer) const
{
    Require(space != SpaceKind::AnomalousDomain, "Domain interiors need their own coordinate adapter");
    WorldLocation result;
    result.planetId = planet.id;
    result.planetSeed = planet.seed;
    result.generatorVersion = planet.generatorVersion;
    result.regionId = std::move(region);
    result.space = space;
    result.layer = layer;
    result.position = SurfaceGeometry::FromLocal(frame, Scale(local, metersPerUnit), planet.radiusMeters);
    result.Validate(planet);
    return result;
}
Vector3d LegacyWorldAdapter::Local(const WorldLocation &location) const
{
    location.Validate(planet);
    Require(location.space != SpaceKind::AnomalousDomain, "A domain is not a planetary local patch");
    return Scale(SurfaceGeometry::ToLocal(frame, location.position, planet.radiusMeters), 1 / metersPerUnit);
}
StoryAnchor LegacyWorldAdapter::Anchor(std::string id, std::string region, Vector3d local) const
{
    StoryAnchor result = {std::move(id), Locate(std::move(region), local)};
    result.Validate(planet);
    return result;
}
const LegacyWorldAdapter &LegacyWorldAdapter::ErmaExterior()
{
    static const LegacyWorldAdapter result(WorldCatalog::Erma(),
                                           SurfaceGeometry::Frame(SurfaceGeometry::FromDegrees(0, 0)),
                                           WorldCatalog::MetersPerLegacyUnit);
    return result;
}
} // namespace world
