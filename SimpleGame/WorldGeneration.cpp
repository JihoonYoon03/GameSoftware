#include "stdafx.h"
#include "WorldGeneration.h"
#include "WorldContent.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

namespace world
{
namespace
{
using G = SurfaceGeometry;
void Require(bool value, const char *message)
{
    if (!value)
    {
        throw std::invalid_argument(message);
    }
}
Vector3d Add(Vector3d a, Vector3d b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vector3d Mul(Vector3d a, double v)
{
    return {a.x * v, a.y * v, a.z * v};
}
double Length(Vector3d v)
{
    return std::hypot(std::hypot(v.x, v.y), v.z);
}
double Smooth(double x)
{
    x = std::clamp(x, 0., 1.);
    return x * x * (3 - 2 * x);
}
std::uint64_t Mix(std::uint64_t x)
{
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
struct Random
{
    std::uint64_t state;
    std::uint64_t Next()
    {
        state += 0x9e3779b97f4a7c15ull;
        return Mix(state);
    }
    double Unit()
    {
        return double(Next() >> 11) * 0x1.0p-53;
    }
};
double Distance(const PlanetDefinition &p, const SurfacePoint &a, const SurfacePoint &b)
{
    return G::Distance(p.radiusMeters, a, b);
}
WorldLocation Location(const PlanetDefinition &p, const std::string &region, SurfacePoint point)
{
    WorldLocation location;
    location.planetId = p.id;
    location.planetSeed = p.seed;
    location.generatorVersion = p.generatorVersion;
    location.regionId = region;
    location.position = point;
    return location;
}
SurfacePoint Arc(const PlanetDefinition &planet, const SurfacePoint &a, const SurfacePoint &b, double t)
{
    auto frame = G::Frame(a);
    double dot = std::clamp(G::Dot(a.normal, b.normal), -1., 1.);
    auto tangent = Add(b.normal, Mul(a.normal, -dot));
    tangent = Length(tangent) > 1.e-12 ? G::Unit(tangent) : frame.right;
    auto result = G::Advance(frame, Distance(planet, a, b) * t * G::Dot(tangent, frame.right),
                             Distance(planet, a, b) * t * G::Dot(tangent, frame.forward), planet.radiusMeters)
                      .origin;
    result.altitudeMeters = a.altitudeMeters + (b.altitudeMeters - a.altitudeMeters) * t;
    return result;
}
double PolygonArea(const PlanetDefinition &planet, const TangentFrame &frame,
                   const std::vector<Vector3d> &points)
{
    double angle = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        auto a = frame.origin.normal;
        auto b = G::FromLocal(frame, points[i], planet.radiusMeters).normal;
        auto c = G::FromLocal(frame, points[(i + 1) % points.size()], planet.radiusMeters).normal;
        angle += 2 * std::atan2(std::fabs(G::Dot(a, G::Cross(b, c))),
                                1 + G::Dot(a, b) + G::Dot(b, c) + G::Dot(c, a));
    }
    return angle * planet.radiusMeters * planet.radiusMeters;
}
double Area(const PlanetDefinition &p, const RegionDefinition &d)
{
    if (d.kind == RegionKind::Settlement)
    {
        return d.TargetAreaKm2() * 1.e6;
    }
    auto shapePlanet = p;
    if (d.storyFixed)
    {
        shapePlanet.seed = 0;
    }
    Random rng{WorldIdentity::StreamSeed(shapePlanet, d.id, "footprint_area")};
    switch (d.kind)
    {
    case RegionKind::Outpost:
        return (0.3 + rng.Unit() * .7) * 1.e6;
    case RegionKind::Mine:
        return (1 + rng.Unit() * 2) * 1.e6;
    case RegionKind::Outlaw:
        return (2 + rng.Unit() * 4) * 1.e6;
    case RegionKind::AncientRuins:
        return (1 + rng.Unit() * 3) * 1.e6;
    default:
        return (3 + rng.Unit() * 3) * 1.e6;
    }
}
void Footprint(GeneratedRegion &region, const PlanetDefinition &planet)
{
    auto shapePlanet = planet;
    if (region.definition.storyFixed)
    {
        shapePlanet.seed = 0;
    }
    Random rng{WorldIdentity::StreamSeed(shapePlanet, region.definition.id, "footprint_shape")};
    unsigned count = 9 + unsigned(rng.Next() % 5);
    double aspect = 1.05 + rng.Unit() * 1.45, rotation = rng.Unit() * 2 * G::Pi;
    region.footprint.clear();
    for (unsigned i = 0; i < count; ++i)
    {
        double angle = (i + .25 * (rng.Unit() - .5)) * 2 * G::Pi / count;
        double x = std::cos(angle) * std::sqrt(aspect), y = std::sin(angle) / std::sqrt(aspect);
        region.footprint.push_back({std::cos(rotation) * x - std::sin(rotation) * y,
                                    std::sin(rotation) * x + std::cos(rotation) * y, 0});
    }
    // Scale using actual spherical area, not a UV rectangle or a planar bounding box.
    // Start at metre scale large enough to avoid cancellation in tiny solid angles.
    for (auto &v : region.footprint)
    {
        v = Mul(v, 1000);
    }
    for (unsigned pass = 0; pass < 4; ++pass)
    {
        double measured = PolygonArea(planet, region.frame, region.footprint);
        Require(measured > 0 && std::isfinite(measured), "Degenerate region footprint");
        double scale = std::sqrt(region.areaMeters2 / measured);
        for (auto &v : region.footprint)
        {
            v = Mul(v, scale);
        }
    }
    region.boundingRadiusMeters = 0;
    for (auto v : region.footprint)
    {
        region.boundingRadiusMeters = (std::max)(region.boundingRadiusMeters, std::hypot(v.x, v.y));
    }
    Require(region.boundingRadiusMeters < planet.radiusMeters * .05,
            "Region too large for its planetary local patch");
}
GeneratedRegion Place(const PlanetDefinition &p, const RegionDefinition &definition, SurfacePoint access,
                      SpaceKind space = SpaceKind::Surface, std::int32_t layer = 0, double depth = 0)
{
    GeneratedRegion r;
    r.definition = definition;
    r.access = Location(p, definition.id, access);
    r.center = r.access;
    r.center.space = space;
    r.center.layer = layer;
    r.center.position.altitudeMeters -= depth;
    r.frame = G::Frame(r.center.position);
    r.areaMeters2 = Area(p, definition);
    Footprint(r, p);
    return r;
}
bool Overlaps(const PlanetDefinition &p, const GeneratedRegion &a, const GeneratedRegion &b, double gap)
{
    if (!a.center.SameSpace(b.center))
    {
        return false;
    }
    return Distance(p, a.center.position, b.center.position) <
           a.boundingRadiusMeters + b.boundingRadiusMeters + gap;
}
std::vector<Vector3d> CoverageSamples(const GeneratedRegion &r, double coverage = 0)
{
    std::vector<Vector3d> result = {{0, 0, 0}};
    for (std::size_t i = 0; i < r.footprint.size(); ++i)
    {
        auto a = r.footprint[i], b = r.footprint[(i + 1) % r.footprint.size()];
        result.push_back(a);
        result.push_back(Mul(Add(a, b), .5));
        result.push_back(Mul(a, .5));
        if (coverage > 0)
        {
            unsigned steps = unsigned(std::ceil(Length(Add(b, Mul(a, -1))) / (.2 * coverage)));
            for (unsigned step = 1; step < steps; ++step)
            {
                result.push_back(Add(Mul(a, 1 - double(step) / steps), Mul(b, double(step) / steps)));
            }
        }
    }
    if (coverage > 0)
    {
        // Interior lattice plus densely sampled edges bounds the distance to a sample.
        // A .7 coverage placement threshold leaves room for the .2 spacing and sphere distortion.
        double spacing = .2 * coverage, bound = r.boundingRadiusMeters;
        Require(std::pow(std::ceil(2 * bound / spacing) + 1, 2) <= 65536,
                "Relay coverage exceeds sampling budget");
        for (double x = -bound; x <= bound; x += spacing)
        {
            for (double y = -bound; y <= bound; y += spacing)
            {
                bool inside = true;
                for (std::size_t i = 0; i < r.footprint.size(); ++i)
                {
                    auto a = r.footprint[i], b = r.footprint[(i + 1) % r.footprint.size()];
                    if ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x) < 0)
                    {
                        inside = false;
                        break;
                    }
                }
                if (inside)
                {
                    result.push_back({x, y, 0});
                }
            }
        }
    }
    return result;
}
} // namespace

// ============================================================================
// PlanetTerrain — continuous 3D lattice noise avoids cube-face/longitude seams
// ============================================================================
PlanetTerrain::PlanetTerrain(PlanetDefinition p, std::vector<TerrainReservation> r,
                             std::optional<SurfacePoint> home)
    : planet(std::move(p)), reservations(std::move(r)), homeland(home)
{
    planet.Validate();
    terrainSeed = WorldIdentity::StreamSeed(planet, "overworld", "macro_terrain");
    if (homeland)
    {
        G::Validate(*homeland);
    }
    std::sort(reservations.begin(), reservations.end(), [](const auto &a, const auto &b) {
        return std::tie(a.priority, a.id) < std::tie(b.priority, b.id);
    });
    std::set<std::string> ids;
    for (const auto &pad : reservations)
    {
        G::Validate(pad.center);
        Require(WorldIdentity::ValidId(pad.id) && ids.insert(pad.id).second,
                "Invalid/duplicate terrain reservation");
        Require(std::isfinite(pad.radiusMeters) && pad.radiusMeters > 0 && std::isfinite(pad.featherMeters) &&
                    pad.featherMeters > 0 && std::isfinite(pad.elevationMeters),
                "Invalid reservation dimensions");
        for (const auto &other : reservations)
        {
            if (pad.id < other.id && pad.priority == other.priority &&
                pad.elevationMeters != other.elevationMeters)
            {
                Require(Distance(planet, pad.center, other.center) >=
                            pad.radiusMeters + pad.featherMeters + other.radiusMeters + other.featherMeters,
                        "Conflicting protected terrain elevations");
            }
        }
    }
}
double PlanetTerrain::Noise(Vector3d p, double frequency, std::uint64_t channel) const
{
    double x = p.x * frequency, y = p.y * frequency, z = p.z * frequency;
    auto ix = std::int64_t(std::floor(x)), iy = std::int64_t(std::floor(y)), iz = std::int64_t(std::floor(z));
    double fx = Smooth(x - ix), fy = Smooth(y - iy), fz = Smooth(z - iz), value = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        auto hash = Mix(terrainSeed ^ channel ^ Mix(std::uint64_t(ix + int(i & 1))) ^
                        Mix(std::uint64_t(iy + int((i >> 1) & 1)) + 0x7337ull) ^
                        Mix(std::uint64_t(iz + int((i >> 2) & 1)) + 0xa57bull));
        value += double(hash >> 11) * 0x1.0p-53 * ((i & 1) ? fx : 1 - fx) * ((i & 2) ? fy : 1 - fy) *
                 ((i & 4) ? fz : 1 - fz);
    }
    return value;
}
double PlanetTerrain::Fractal(Vector3d p, double frequency, unsigned octaves, std::uint64_t channel) const
{
    double value = 0, weight = 1, total = 0;
    for (unsigned i = 0; i < octaves; ++i)
    {
        value += Noise(p, frequency, channel + i * 131) * weight;
        total += weight;
        frequency *= 2.07;
        weight *= .5;
    }
    return value / total;
}
double PlanetTerrain::Height(const SurfacePoint &point) const
{
    G::Validate(point);
    double continent = Fractal(point.normal, 2.3, 4, 1);
    double ridge = 1 - std::fabs(Fractal(point.normal, 42, 3, 87) * 2 - 1);
    double height = (continent - .49) * 7200 + std::pow(ridge, 5) * 650 * Smooth((continent - .48) * 12) +
                    (Fractal(point.normal, 240, 3, 231) - .5) * 140;
    if (homeland)
    {
        double radius = (std::min)(180000., planet.radiusMeters * .12);
        double coast = radius * (.9 + Noise(point.normal, 38, 291) * .2);
        double weight = 1 - Smooth((Distance(planet, point, *homeland) / coast - .70) / .50);
        double floor = SeaLevelMeters + 180 + Fractal(point.normal, 65, 3, 355) * 220;
        height += (std::max)(0., floor - height) * weight;
    }
    for (const auto &pad : reservations)
    {
        double distance = Distance(planet, point, pad.center);
        if (distance >= pad.radiusMeters + pad.featherMeters)
        {
            continue;
        }
        double weight = 1 - Smooth((distance - pad.radiusMeters) / pad.featherMeters);
        height += (pad.elevationMeters - height) * weight;
    }
    return height;
}
TerrainSample PlanetTerrain::Sample(const SurfacePoint &point) const
{
    TerrainSample result;
    result.elevationMeters = Height(point);
    auto frame = G::Frame(point);
    constexpr double step = 250;
    double dx = Height(G::Advance(frame, step, 0, planet.radiusMeters).origin) -
                Height(G::Advance(frame, -step, 0, planet.radiusMeters).origin);
    double dy = Height(G::Advance(frame, 0, step, planet.radiusMeters).origin) -
                Height(G::Advance(frame, 0, -step, planet.radiusMeters).origin);
    result.slope = std::hypot(dx, dy) / (2 * step);
    return Environment(point, result.elevationMeters, result.slope);
}
TerrainSample PlanetTerrain::Environment(const SurfacePoint &point, double elevation, double slope) const
{
    G::Validate(point);
    Require(std::isfinite(elevation) && std::isfinite(slope) && slope >= 0,
            "Invalid terrain contact environment");
    TerrainSample result;
    result.elevationMeters = elevation;
    result.slope = slope;
    result.moisture = Fractal(point.normal, 7, 4, 611);
    result.temperature =
        std::clamp(.9 - std::fabs(point.normal.z) * .8 + (Noise(point.normal, 5, 701) - .5) * .25 -
                       (std::max)(0., result.elevationMeters) / 7000,
                   0., 1.);
    result.minerals = std::clamp(Fractal(point.normal, 37, 3, 823) * .8 + result.slope * 2, 0., 1.);
    result.land = result.elevationMeters >= SeaLevelMeters;
    result.biome = !result.land                                   ? Biome::Ocean
                   : result.elevationMeters < SeaLevelMeters + 80 ? Biome::Coast
                   : result.temperature < .16                     ? Biome::Ice
                   : result.elevationMeters > 1700                ? Biome::Alpine
                   : result.slope > .055                          ? Biome::Rocky
                   : result.moisture < .40                        ? Biome::Desert
                   : result.moisture > .59                        ? Biome::Forest
                                                                  : Biome::Grassland;
    return result;
}
void GenerationSettings::Validate() const
{
    Require(extraRegions <= 512 && clusters >= 2 && clusters <= 64 && placementAttempts >= 16 &&
                placementAttempts <= 4096,
            "Generation counts exceed bounded manifest budgets");
    for (double value :
         {regionGapMeters, clusterRadiusMeters, roadLimitMeters, maximumRoadGrade, relayCoverageMeters})
    {
        Require(std::isfinite(value) && value > 0, "Invalid generation metric");
    }
    Require(maximumRoadGrade <= .25 && relayCoverageMeters >= 500,
            "Invalid road grade/relay coverage budget");
}

// ============================================================================
// GeneratedPlanet — stable queries never generate or activate actors
// ============================================================================
GeneratedPlanet::GeneratedPlanet(PlanetTerrain t) : terrain(std::move(t))
{
}
const GeneratedRegion &GeneratedPlanet::Region(std::string_view id) const
{
    for (const auto &region : regions)
    {
        if (region.definition.id == id)
        {
            return region;
        }
    }
    throw std::out_of_range("Unknown generated region");
}
std::vector<std::size_t> GeneratedPlanet::RegionsInChunk(const ChunkKey &key) const
{
    key.Validate();
    const auto &p = terrain.Planet();
    Require(key.planetId == p.id && key.planetSeed == p.seed && key.generatorVersion == p.generatorVersion,
            "Chunk query belongs to another world");
    auto center = CubeSphere::Center(key);
    double radius = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        radius = (std::max)(radius, Distance(p, center, CubeSphere::Corner(key, i % 2, i / 2)));
    }
    radius = radius * 1.01 + 1;
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < regions.size(); ++i)
    {
        const auto &r = regions[i];
        if ((r.center.layer == key.layer &&
             Distance(p, center, r.center.position) <= radius + r.boundingRadiusMeters) ||
            (key.layer == 0 && Distance(p, center, r.access.position) <= radius))
        {
            result.push_back(i);
        }
    }
    return result;
}
bool GeneratedPlanet::Connected(bool roadsOnly) const
{
    if (nodes.empty())
    {
        return regions.empty();
    }
    std::vector<bool> seen(nodes.size(), false);
    std::vector<unsigned> pending = {0};
    seen[0] = true;
    while (!pending.empty())
    {
        unsigned node = pending.back();
        pending.pop_back();
        for (const auto &edge : connections)
        {
            if (roadsOnly && edge.mode != RouteMode::Road)
            {
                continue;
            }
            if (edge.from >= nodes.size() || edge.to >= nodes.size())
            {
                return false;
            }
            unsigned next = edge.from == node ? edge.to
                            : edge.to == node ? edge.from
                                              : unsigned(nodes.size());
            if (next < nodes.size() && !seen[next])
            {
                seen[next] = true;
                pending.push_back(next);
            }
        }
    }
    return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
}

namespace
{
SurfacePoint RandomSurface(Random &rng)
{
    double z = rng.Unit() * 2 - 1, angle = rng.Unit() * 2 * G::Pi,
           scale = std::sqrt((std::max)(0., 1 - z * z));
    return {G::Unit({std::cos(angle) * scale, std::sin(angle) * scale, z}), 0};
}
bool Suitable(const PlanetTerrain &terrain, const GeneratedRegion &r)
{
    for (auto v : CoverageSamples(r))
    {
        auto point = G::FromLocal(r.frame, v, terrain.Planet().radiusMeters);
        auto sample = terrain.Sample(point);
        if (!sample.land || sample.elevationMeters < PlanetTerrain::SeaLevelMeters + 12 ||
            sample.slope > (r.definition.kind == RegionKind::Settlement ? .045 : .08))
        {
            return false;
        }
    }
    return true;
}
double Suitability(const TerrainSample &s, RegionKind kind)
{
    switch (kind)
    {
    case RegionKind::Settlement:
        return s.slope * 30 + std::fabs(s.moisture - .52) + std::fabs(s.temperature - .55);
    case RegionKind::Mine:
        return 1 - s.minerals + s.slope * 2;
    case RegionKind::AncientRuins:
        return std::fabs(s.moisture - .3) + s.slope;
    case RegionKind::Outlaw:
        return std::fabs(s.moisture - .38) + s.slope * 2;
    default:
        return s.slope * 10 + std::fabs(s.moisture - .48);
    }
}
std::string ProceduralName(unsigned index, Random &rng, RegionKind kind)
{
    static const char *prefix[] = {"아르", "벨",   "카르", "시오", "헤르", "네오", "오르", "라스",
                                   "탈",   "이르", "세르", "노르", "루아", "키르", "에스", "드라"};
    static const char *middle[] = {"카", "디", "루", "베", "테", "나", "리", "오",
                                   "세", "마", "티", "르", "소", "하", "라", "케"};
    static const char *suffix[] = {"온", "덴", "스",   "움", "라", "넬", "크", "사",
                                   "린", "드", "시아", "론", "바", "딘", "렐", "테"};
    // Stable syllable selection; the manifest resolves rare collisions by stable ID.
    unsigned word = (index * 37 + unsigned(rng.Next() % 4096)) % 4096;
    const char *type = kind == RegionKind::Mine           ? " 광구"
                       : kind == RegionKind::Outlaw       ? " 황야"
                       : kind == RegionKind::AncientRuins ? " 잔해지"
                       : kind == RegionKind::Outpost      ? " 전초"
                                                          : "";
    return std::string(prefix[word % 16]) + middle[(word / 16) % 16] + suffix[word / 256] + type;
}
void AddRelays(GeneratedPlanet &result, const GenerationSettings &settings, const std::atomic_bool *cancel)
{
    const auto &planet = result.terrain.Planet();
    auto pads = result.terrain.Reservations();
    auto place = [&](const GeneratedRegion &region, SurfacePoint target) {
        auto frame = G::Frame(target);
        for (unsigned ring = 0; ring <= 16; ++ring)
        {
            for (unsigned step = 0; step < (ring ? 16u : 1u); ++step)
            {
                double distance = (std::min)(96., settings.relayCoverageMeters * .045) * ring / 16;
                auto point = G::Advance(frame, distance * std::cos(step * G::Pi / 8),
                                        distance * std::sin(step * G::Pi / 8), planet.radiusMeters)
                                 .origin;
                double height = result.terrain.Height(point);
                if (height < PlanetTerrain::SeaLevelMeters + 4)
                {
                    continue;
                }
                if (result.terrain.Sample(point).slope > .25)
                {
                    continue;
                }
                bool clear = true;
                for (const auto &anchor : result.anchors)
                {
                    if (anchor.location.space == SpaceKind::Surface &&
                        Distance(planet, point, anchor.location.position) < 22)
                    {
                        clear = false;
                    }
                }
                if (auto core = result.cityCores ? result.cityCores->Find(region.definition.id) : nullptr)
                {
                    auto local = G::ToLocal(region.frame, point, planet.radiusMeters);
                    for (const auto &lot : core->lots)
                    {
                        if (!lot.solid)
                        {
                            continue;
                        }
                        double dx = local.x - lot.positionMeters.x, dy = local.y - lot.positionMeters.y;
                        if (std::fabs(dx * std::cos(lot.yaw) + dy * std::sin(lot.yaw)) <
                                lot.sizeMeters.x * .5 + 4 &&
                            std::fabs(-dx * std::sin(lot.yaw) + dy * std::cos(lot.yaw)) <
                                lot.sizeMeters.y * .5 + 4)
                        {
                            clear = false;
                        }
                    }
                    for (const auto &road : core->roads)
                    {
                        auto d = Add(road.end, Mul(road.start, -1));
                        double t =
                            std::clamp(((local.x - road.start.x) * d.x + (local.y - road.start.y) * d.y) /
                                           (d.x * d.x + d.y * d.y),
                                       0., 1.);
                        if (std::hypot(local.x - road.start.x - d.x * t, local.y - road.start.y - d.y * t) <
                            road.lanes * 1.5 + 3)
                        {
                            clear = false;
                        }
                    }
                }
                if (clear)
                {
                    point.altitudeMeters = height;
                    return point;
                }
            }
        }
        throw std::invalid_argument("No accessible relay pad: " + region.definition.id);
    };
    for (const auto &region : result.regions)
    {
        if (cancel && cancel->load(std::memory_order_relaxed))
        {
            throw std::runtime_error("Relay preparation cancelled");
        }
        std::vector<SurfacePoint> samples;
        for (auto v : CoverageSamples(region, settings.relayCoverageMeters))
        {
            auto point = G::FromLocal(region.frame, v, planet.radiusMeters);
            point.altitudeMeters = result.terrain.Height(point);
            samples.push_back(point);
        }
        std::vector<SurfacePoint> points = {region.access.position};
        for (unsigned step = 0; step < 64; ++step)
        {
            double largest = 0;
            std::size_t farthest = 0;
            for (std::size_t i = 0; i < samples.size(); ++i)
            {
                double nearest = std::numeric_limits<double>::infinity();
                for (auto point : points)
                {
                    nearest = (std::min)(nearest, Distance(planet, point, samples[i]));
                }
                if (nearest > largest)
                {
                    largest = nearest;
                    farthest = i;
                }
            }
            if (largest <= settings.relayCoverageMeters * (planet.generatorVersion >= 2 ? .65 : .7))
            {
                break;
            }
            points.push_back(samples[farthest]);
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            std::string id = "relay_" + region.definition.id + "_" + std::to_string(i);
            if (planet.generatorVersion >= 2)
            {
                points[i] = place(region, points[i]);
                pads.push_back({"pad_" + id, points[i], 12, 8, points[i].altitudeMeters, 2});
            }
            result.relays.push_back(
                {id, region.definition.id, Location(planet, region.definition.id, points[i])});
        }
    }
    if (planet.generatorVersion >= 2)
    {
        result.terrain = PlanetTerrain(planet, std::move(pads), result.terrain.Homeland());
    }
}
} // namespace

// ============================================================================
// RouteSpatialIndex — broad phase only; exact projection/clipping remains the caller's responsibility
// ============================================================================
RouteSpatialIndex::RouteSpatialIndex(const GeneratedPlanet &planet)
    : planetRadius(planet.terrain.Planet().radiusMeters)
{
    planet.terrain.Planet().Validate();
    bounds.reserve(planet.connections.size());
    for (const auto &route : planet.connections)
    {
        Bound bound;
        bound.angle = -1;
        if (route.points.size() >= 2)
        {
            Vector3d sum = {};
            for (const auto &point : route.points)
            {
                G::Validate(point);
                sum = Add(sum, point.normal);
            }
            bound.center = G::Dot(sum, sum) > 1.e-16 ? G::Unit(sum) : route.points.front().normal;
            SurfacePoint center{bound.center, 0};
            double endpointAngle = 0, halfSegmentAngle = 0;
            for (size_t i = 0; i < route.points.size(); ++i)
            {
                endpointAngle = (std::max)(endpointAngle,
                                           G::Distance(planetRadius, center, route.points[i]) / planetRadius);
                if (i)
                {
                    halfSegmentAngle =
                        (std::max)(halfSegmentAngle,
                                   G::Distance(planetRadius, route.points[i - 1], route.points[i]) /
                                       (2 * planetRadius));
                }
            }
            // Every point on a segment lies within half its length of at least one endpoint.
            bound.angle = (std::min)(G::Pi, endpointAngle + halfSegmentAngle + 1.e-9);
            // A smaller hemisphere is geodesically convex, including coarse map shortcuts.
            // For a wider cap, retain the connection everywhere rather than risk an atlas false negative.
            if (bound.angle >= G::Pi * .5)
            {
                bound.angle = G::Pi;
            }
        }
        bounds.push_back(bound);
    }
}
std::vector<std::size_t> RouteSpatialIndex::Query(const SurfacePoint &focus, double radiusMeters) const
{
    G::Validate(focus);
    Require(std::isfinite(radiusMeters) && radiusMeters >= 0, "Invalid route query radius");
    std::vector<std::size_t> result;
    const double angle = (std::min)(G::Pi, radiusMeters / planetRadius);
    for (size_t i = 0; i < bounds.size(); ++i)
    {
        const auto &bound = bounds[i];
        if (bound.angle < 0)
        {
            continue;
        }
        const double reach = (std::min)(G::Pi, bound.angle + angle);
        if (reach >= G::Pi || G::Dot(bound.center, focus.normal) >= std::cos(reach) - 1.e-12)
        {
            result.push_back(i);
        }
    }
    return result;
}

// ============================================================================
// RelayNetwork — discoveries remain physical facts, not quest or equipment unlocks
// ============================================================================
const RelayCandidate *RelayNetwork::Find(const GeneratedPlanet &planet, std::string_view id)
{
    auto found = std::find_if(planet.relays.begin(), planet.relays.end(),
                              [&](const auto &relay) { return relay.id == id; });
    return found == planet.relays.end() ? nullptr : &*found;
}
const RelayCandidate *RelayNetwork::Nearest(const GeneratedPlanet &planet, const SurfacePoint &position,
                                            double range)
{
    const RelayCandidate *found = nullptr;
    for (const auto &relay : planet.relays)
    {
        double distance = Distance(planet.terrain.Planet(), position, relay.location.position);
        if (distance <= range)
        {
            range = distance;
            found = &relay;
        }
    }
    return found;
}
bool RelayNetwork::Discovered(std::string_view id) const
{
    return discovered.count(std::string(id)) != 0;
}
bool RelayNetwork::Registered(std::string_view id) const
{
    return registered.count(std::string(id)) != 0;
}
bool RelayNetwork::RegionVisited(std::string_view id) const
{
    return visited.count(std::string(id)) != 0;
}
void RelayNetwork::Bind(const PlanetDefinition &planet)
{
    const auto key = WorldIdentity::ContentKey(planet, "overworld", "relay_registry");
    if (key == worldKey)
    {
        return;
    }
    worldKey = key;
    discovered.clear();
    registered.clear();
    visited.clear();
}
void RelayNetwork::Observe(const GeneratedPlanet &planet, const WorldLocation &location)
{
    const auto &p = planet.terrain.Planet();
    if (location.planetId != p.id || location.planetSeed != p.seed ||
        location.generatorVersion != p.generatorVersion || location.space != SpaceKind::Surface ||
        location.layer != 0)
    {
        return;
    }
    Bind(p);
    for (const auto &relay : planet.relays)
    {
        if (Distance(p, location.position, relay.location.position) <= DiscoverMeters)
        {
            discovered.insert(relay.id);
        }
    }
    for (const auto &region : planet.regions)
    {
        if (region.center.space != SpaceKind::Surface)
        {
            if (Distance(p, location.position, region.access.position) < DiscoverMeters)
            {
                visited.insert(region.definition.id);
            }
            continue;
        }
        if (Distance(p, location.position, region.center.position) > region.boundingRadiusMeters)
        {
            continue;
        }
        const auto local = G::ToLocal(region.frame, location.position, p.radiusMeters);
        bool positive = false, negative = false;
        for (size_t i = 0; i < region.footprint.size(); ++i)
        {
            auto a = region.footprint[i], b = region.footprint[(i + 1) % region.footprint.size()];
            double cross = (b.x - a.x) * (local.y - a.y) - (b.y - a.y) * (local.x - a.x);
            positive |= cross > 1.e-6;
            negative |= cross < -1.e-6;
        }
        if (!(positive && negative))
        {
            visited.insert(region.definition.id);
        }
    }
}
bool RelayNetwork::Register(const GeneratedPlanet &planet, std::string_view id, const WorldLocation &location)
{
    Observe(planet, location);
    const auto *relay = Find(planet, id);
    if (!relay || location.space != SpaceKind::Surface || location.layer != 0 || !Discovered(id) ||
        location.planetId != planet.terrain.Planet().id ||
        location.planetSeed != planet.terrain.Planet().seed ||
        location.generatorVersion != planet.terrain.Planet().generatorVersion ||
        Distance(planet.terrain.Planet(), location.position, relay->location.position) > UseMeters)
    {
        return false;
    }
    return registered.insert(std::string(id)).second;
}

// ============================================================================
// PlanetGenerator — sorted, independent streams and reservations precede all sites
// ============================================================================
PlanetGenerator::PlanetGenerator(PlanetDefinition p, std::vector<RegionDefinition> d,
                                 std::vector<FixedRegionPlacement> f, std::vector<StoryAnchor> a,
                                 GenerationSettings s)
    : planet(std::move(p)), definitions(std::move(d)), fixed(std::move(f)), anchors(std::move(a)), settings(s)
{
    planet.Validate();
    settings.Validate();
    std::sort(definitions.begin(), definitions.end(),
              [](const auto &x, const auto &y) { return x.id < y.id; });
    std::sort(fixed.begin(), fixed.end(),
              [](const auto &x, const auto &y) { return x.regionId < y.regionId; });
    std::sort(anchors.begin(), anchors.end(), [](const auto &x, const auto &y) { return x.id < y.id; });
    std::set<std::string> ids, fixedIds, anchorIds;
    for (const auto &definition : definitions)
    {
        definition.Validate(planet);
        Require(ids.insert(definition.id).second, "Duplicate region definition");
    }
    for (const auto &placement : fixed)
    {
        G::Validate(placement.access);
        Require(ids.count(placement.regionId) && fixedIds.insert(placement.regionId).second,
                "Invalid fixed region");
        auto definition = std::find_if(definitions.begin(), definitions.end(),
                                       [&](const auto &x) { return x.id == placement.regionId; });
        Require(definition->storyFixed && placement.space != SpaceKind::AnomalousDomain &&
                    std::isfinite(placement.depthMeters) && placement.depthMeters >= 0,
                "Invalid fixed physical region placement");
        auto candidate = Place(planet, *definition, placement.access, placement.space, placement.layer,
                               placement.depthMeters);
        candidate.center.Validate(planet);
        candidate.access.Validate(planet);
    }
    for (const auto &definition : definitions)
    {
        Require(!definition.storyFixed || fixedIds.count(definition.id),
                "Story region has no fixed placement");
    }
    for (const auto &anchor : anchors)
    {
        anchor.Validate(planet);
        Require(anchorIds.insert(anchor.id).second && fixedIds.count(anchor.location.regionId),
                "Unreserved story anchor");
    }
}
PlanetGenerator PlanetGenerator::WithCityCores(const CityCorePlan &cores) const
{
    cores.Validate();
    auto result = *this;
    result.cityCores = std::make_shared<const CityCorePlan>(cores);
    return result;
}
PlanetGenerator PlanetGenerator::WithSeed(std::uint64_t seed) const
{
    auto p = planet;
    p.seed = seed;
    auto a = anchors;
    for (auto &anchor : a)
    {
        anchor.location.planetSeed = seed;
    }
    auto result = PlanetGenerator(p, definitions, fixed, std::move(a), settings);
    result.cityCores = cityCores;
    return result;
}
PlanetGenerator PlanetGenerator::Erma(const std::vector<StoryAnchor> &anchors, GenerationSettings settings)
{
    auto planet = WorldCatalog::Erma();
    const auto &adapter = LegacyWorldAdapter::ErmaExterior();
    auto mine =
        std::find_if(anchors.begin(), anchors.end(), [](const auto &a) { return a.id == "mine_entrance"; });
    Require(mine != anchors.end(), "Erma requires its existing mine entrance anchor");
    std::vector<FixedRegionPlacement> fixed = {
        {"halo_07", adapter.Locate("halo_07", {}).position},
        {"mq00_mine", mine->location.position, SpaceKind::Subsurface, -1, 30},
        {"frontier", adapter
                         .Locate("frontier", {6000 * WorldCatalog::ExplorationDistanceScale,
                                              4000 * WorldCatalog::ExplorationDistanceScale, 0})
                         .position},
        {"n0_outskirts", adapter
                             .Locate("n0_outskirts", {14500 * WorldCatalog::ExplorationDistanceScale,
                                                      -7000 * WorldCatalog::ExplorationDistanceScale, 0})
                             .position}};
    return PlanetGenerator(planet, WorldCatalog::ErmaRegions(), std::move(fixed), anchors, settings);
}

namespace
{
void BuildConnections(GeneratedPlanet &result, const GenerationSettings &settings,
                      const std::atomic_bool *cancel);
}
const std::vector<PlanetDefinition> &PlanetProfiles::All()
{
    static const std::vector<PlanetDefinition> profiles = {
        WorldCatalog::Erma(),
        {"seira", "세이라", "survey_system", 0x5e17a31ull, 2,
         1737400 * WorldCatalog::ExplorationDistanceScale, false},
        {"orren", "오르엔", "survey_system", 0x077e493ull, 2,
         7000000 * WorldCatalog::ExplorationDistanceScale, false}};
    return profiles;
}
const PlanetDefinition *PlanetProfiles::Find(std::string_view id)
{
    const auto &profiles = All();
    auto found = std::find_if(profiles.begin(), profiles.end(), [&](const auto &p) { return p.id == id; });
    return found == profiles.end() ? nullptr : &*found;
}
bool PlanetProfiles::Supported(const PlanetDefinition &p)
{
    auto profile = Find(p.id);
    return profile && p.SameWorld(*profile) && p.storyPlanet == profile->storyPlanet;
}
PlanetGenerator PlanetProfiles::Generator(const PlanetDefinition &p, const std::vector<StoryAnchor> &anchors)
{
    Require(Supported(p), "Unknown or incompatible planet profile");
    if (p.storyPlanet)
    {
        return PlanetGenerator::Erma(anchors);
    }
    std::vector<RegionDefinition> definitions = {
        {"landing_zone", p.name + " 착륙 거점", p.id, RegionKind::Outpost, SettlementTier::None, true},
        {"district_hub", p.name + " 시내", p.id, RegionKind::Settlement, SettlementTier::District, false, 4,
         "district-profile-v1"},
        {"trade_city", p.name + " 물류시", p.id, RegionKind::Settlement, SettlementTier::Town, false, 64,
         "town-profile-v1"},
        {"high_city", p.name + " 광역시", p.id, RegionKind::Settlement, SettlementTier::Metropolis, false,
         605.25, "metropolis-profile-v1"},
        {"system_hub", p.name + " 수도권", p.id, RegionKind::Settlement, SettlementTier::SystemCapital, false,
         1200, "capital-profile-v1"}};
    GenerationSettings settings;
    settings.clusters = 6;
    settings.extraRegions = 32;
    return PlanetGenerator(p, std::move(definitions), {{"landing_zone", SurfaceGeometry::FromDegrees(0, 0)}},
                           {}, settings);
}
GeneratedPlanet PlanetGenerator::Generate(const std::atomic_bool *cancel) const
{
    auto cancelled = [&] {
        if (cancel && cancel->load())
        {
            throw std::runtime_error("Planet generation cancelled");
        }
    };
    cancelled();
    std::vector<GeneratedRegion> protectedRegions;
    std::vector<TerrainReservation> pads;
    std::optional<SurfacePoint> homeland;
    for (const auto &placement : fixed)
    {
        const auto &definition = *std::find_if(definitions.begin(), definitions.end(),
                                               [&](const auto &d) { return d.id == placement.regionId; });
        auto r = Place(planet, definition, placement.access, placement.space, placement.layer,
                       placement.depthMeters);
        for (const auto &other : protectedRegions)
        {
            Require(!Overlaps(planet, r, other, 0), "Fixed region footprints overlap");
        }
        double radius = r.center.space == SpaceKind::Surface ? r.boundingRadiusMeters + 300 : 100;
        // Broad transitions keep protected legacy elevations reachable by ground transport.
        pads.push_back(
            {"region_" + definition.id, placement.access, radius, 32000, placement.access.altitudeMeters});
        if (definition.id == "halo_07" || definition.id == "landing_zone")
        {
            homeland = placement.access;
        }
        protectedRegions.push_back(std::move(r));
    }
    for (const auto &anchor : anchors)
    {
        pads.push_back({"anchor_" + anchor.id, anchor.location.position, 60, 200,
                        anchor.location.position.altitudeMeters, 1});
    }
    GeneratedPlanet result(PlanetTerrain(planet, std::move(pads), homeland));
    result.anchors = anchors;
    result.regions = std::move(protectedRegions);
    result.relayCoverageMeters = settings.relayCoverageMeters;
    result.regionGapMeters = settings.regionGapMeters;
    result.maximumRoadGrade = settings.maximumRoadGrade;
    if (homeland)
    {
        result.clusters.push_back(*homeland);
    }
    Random clusterRng{WorldIdentity::StreamSeed(planet, "overworld", "population_clusters")};
    for (unsigned slot = unsigned(result.clusters.size()); slot < settings.clusters; ++slot)
    {
        cancelled();
        double best = -1;
        SurfacePoint selected;
        bool found = false;
        for (unsigned trial = 0; trial < 512; ++trial)
        {
            if (trial % 16 == 0)
            {
                cancelled();
            }
            auto candidate = RandomSurface(clusterRng);
            auto sample = result.terrain.Sample(candidate);
            if (!sample.land || sample.elevationMeters < PlanetTerrain::SeaLevelMeters + 120 ||
                sample.slope > .035)
            {
                continue;
            }
            double nearest = G::Pi * planet.radiusMeters;
            for (auto center : result.clusters)
            {
                nearest = (std::min)(nearest, Distance(planet, center, candidate));
            }
            if (nearest < planet.radiusMeters * .15)
            {
                continue;
            }
            double score = nearest / planet.radiusMeters + sample.moisture * .1 - sample.slope * 5;
            if (score > best)
            {
                best = score;
                selected = candidate;
                found = true;
            }
        }
        Require(found, "Could not place bounded land population clusters");
        selected.altitudeMeters = result.terrain.Height(selected);
        result.clusters.push_back(selected);
    }
    std::vector<RegionDefinition> pending;
    for (const auto &definition : definitions)
    {
        if (!definition.storyFixed && definition.id != "overworld")
        {
            pending.push_back(definition);
        }
    }
    static const RegionKind types[] = {RegionKind::Settlement,   RegionKind::Outpost,
                                       RegionKind::Mine,         RegionKind::Outlaw,
                                       RegionKind::AncientRuins, RegionKind::Wilderness};
    for (unsigned slot = 0; slot < settings.extraRegions; ++slot)
    {
        std::string id = "proc_" + std::to_string(slot);
        Random rng{WorldIdentity::StreamSeed(planet, id, "region_identity")};
        RegionDefinition definition;
        definition.id = id;
        definition.planetId = planet.id;
        definition.kind = types[rng.Next() % 6];
        definition.name = ProceduralName(slot, rng, definition.kind);
        if (definition.kind == RegionKind::Settlement)
        {
            definition.tier = rng.Next() % 5 ? SettlementTier::District : SettlementTier::Town;
            definition.referenceAreaKm2 = definition.tier == SettlementTier::District ? 4 : 64;
            definition.areaReference = "design-reference:procedural-settlement-v1";
        }
        pending.push_back(std::move(definition));
    }
    std::sort(pending.begin(), pending.end(), [](const auto &a, const auto &b) {
        double aa = a.kind == RegionKind::Settlement ? a.referenceAreaKm2 : 0;
        double bb = b.kind == RegionKind::Settlement ? b.referenceAreaKm2 : 0;
        return aa != bb ? aa > bb : a.id < b.id;
    });
    std::set<std::string> ids, names;
    for (const auto &r : result.regions)
    {
        ids.insert(r.definition.id);
        names.insert(r.definition.name);
    }
    for (auto definition : pending)
    {
        cancelled();
        Require(ids.insert(definition.id).second, "Generated region ID collision");
        if (!names.insert(definition.name).second)
        {
            definition.name += " · " + definition.id;
            Require(names.insert(definition.name).second, "Generated name collision");
        }
        Random rng{WorldIdentity::StreamSeed(planet, definition.id, "region_site")};
        bool nearby = definition.id == "cinder" || definition.id == "veil" || definition.id == "lumen" ||
                      definition.id == "echo";
        unsigned cluster = nearby && homeland ? 0 : unsigned(rng.Next() % result.clusters.size());
        std::optional<GeneratedRegion> selected;
        double best = std::numeric_limits<double>::infinity();
        unsigned valid = 0;
        double spread = (std::min)(settings.clusterRadiusMeters, planet.radiusMeters * .03);
        for (unsigned trial = 0; trial < settings.placementAttempts; ++trial)
        {
            if (trial % 16 == 0)
            {
                cancelled();
            }
            unsigned index = nearby ? cluster : (cluster + trial / 48) % unsigned(result.clusters.size());
            double angle = rng.Unit() * 2 * G::Pi, radius = spread * (.12 + .88 * std::sqrt(rng.Unit()));
            auto point = G::Advance(G::Frame(result.clusters[index]), std::cos(angle) * radius,
                                    std::sin(angle) * radius, planet.radiusMeters)
                             .origin;
            auto sample = result.terrain.Sample(point);
            if (!sample.land || sample.slope > .045)
            {
                continue;
            }
            point.altitudeMeters = sample.elevationMeters;
            auto candidate = Place(planet, definition, point);
            bool overlap = false;
            for (const auto &other : result.regions)
            {
                if (Overlaps(planet, candidate, other, settings.regionGapMeters))
                {
                    overlap = true;
                    break;
                }
            }
            if (overlap || !Suitable(result.terrain, candidate))
            {
                continue;
            }
            double score = Suitability(sample, definition.kind);
            if (definition.kind == RegionKind::Outlaw)
            {
                for (const auto &other : result.regions)
                {
                    if (other.definition.kind == RegionKind::Settlement)
                    {
                        score +=
                            (std::max)(0., 1 - Distance(planet, point, other.center.position) / 12000) * 2;
                    }
                }
            }
            if (score < best)
            {
                best = score;
                candidate.biome = sample.biome;
                selected = std::move(candidate);
            }
            if (++valid >= 12)
            {
                break;
            }
        }
        if (!selected)
        {
            throw std::invalid_argument("Bounded region placement failed: " + definition.id + " (seed " +
                                        std::to_string(planet.seed) + ")");
        }
        result.regions.push_back(std::move(*selected));
    }
    std::sort(result.regions.begin(), result.regions.end(),
              [](const auto &a, const auto &b) { return a.definition.id < b.definition.id; });
    for (auto &r : result.regions)
    {
        r.biome = result.terrain.Sample(r.access.position).biome;
    }
    if (cityCores)
    {
        auto prepared = std::make_shared<CityCorePlan>(*cityCores);
        prepared->Prepare(result, cancel);
        result.cityCores = std::move(prepared);
    }
    AddRelays(result, settings, cancel);
    cancelled();
    BuildConnections(result, settings, cancel);
    cancelled();
    result.Validate();
    return result;
}

namespace
{
bool GroundSegment(const PlanetTerrain &terrain, const SurfacePoint &a, const SurfacePoint &b, double grade)
{
    const auto &p = terrain.Planet();
    double distance = Distance(p, a, b);
    unsigned steps = (std::max)(1u, unsigned(std::ceil(distance / 500)));
    double previous = terrain.Height(a), step = distance / steps;
    if (previous < PlanetTerrain::SeaLevelMeters + 4)
    {
        return false;
    }
    for (unsigned i = 1; i <= steps; ++i)
    {
        double height = terrain.Height(Arc(p, a, b, double(i) / steps));
        if (height < PlanetTerrain::SeaLevelMeters + 4 ||
            (step > .01 && std::fabs(height - previous) / step > grade))
        {
            return false;
        }
        previous = height;
    }
    return true;
}
std::vector<SurfacePoint> GroundPath(const GeneratedPlanet &result, const TravelNode &from,
                                     const TravelNode &to, const GenerationSettings &settings)
{
    const auto &p = result.terrain.Planet();
    auto a = from.location.position, b = to.location.position;
    double distance = Distance(p, a, b);
    if (distance > settings.roadLimitMeters)
    {
        return {};
    }
    if (distance < .01)
    {
        return {a, b};
    }
    auto frame = G::Frame(a);
    auto delta = G::ToLocal(frame, b, p.radiusMeters);
    double dx = delta.x / distance, dy = delta.y / distance;
    constexpr int columns = 25, rows = 13, count = columns * rows;
    constexpr int start = rows / 2, goal = (columns - 1) * rows + rows / 2;
    double width = (std::max)(4000., distance * .35);
    struct Cap
    {
        Vector3d normal;
        double dot, chordRadius;
    };
    std::vector<Cap> forbidden;
    auto addCap = [&](Vector3d normal, double radius) {
        double angle = radius / p.radiusMeters;
        forbidden.push_back({normal, std::cos(angle), 2 * std::sin(angle * .5)});
    };
    for (const auto &region : result.regions)
    {
        if (region.center.space != SpaceKind::Surface || region.definition.id == from.regionId ||
            region.definition.id == to.regionId)
        {
            continue;
        }
        double radius = region.boundingRadiusMeters;
        if (Distance(p, a, region.center.position) <= radius ||
            Distance(p, b, region.center.position) <= radius)
        {
            continue;
        }
        addCap(region.center.position.normal, radius);
    }
    if (result.cityCores)
    {
        for (const auto &core : result.cityCores->cities)
        {
            if (core.regionId != from.regionId && core.regionId != to.regionId)
            {
                continue;
            }
            for (const auto &cell : core.cells)
            {
                for (const auto &object : cell.second.structures)
                {
                    if (object.solid)
                    {
                        addCap(object.frame.origin.normal,
                               std::hypot(object.sizeMeters.x, object.sizeMeters.y) * .5 + 3.2);
                    }
                }
            }
        }
    }
    auto crosses = [&](const SurfacePoint &first, const SurfacePoint &last, const Cap &cap) {
        double reach = std::sqrt((std::max)(0., 2 - 2 * G::Dot(first.normal, last.normal))) + cap.chordRadius;
        if (2 - 2 * G::Dot(first.normal, cap.normal) > reach * reach + 1.e-15)
        {
            return false;
        }
        if (G::Dot(first.normal, cap.normal) > cap.dot || G::Dot(last.normal, cap.normal) > cap.dot)
        {
            return true;
        }
        auto normal = G::Cross(first.normal, last.normal);
        double square = G::Dot(normal, normal);
        if (square < 1.e-24)
        {
            return false;
        }
        double perpendicular = G::Dot(cap.normal, normal) / square;
        Vector3d projected{cap.normal.x - normal.x * perpendicular, cap.normal.y - normal.y * perpendicular,
                           cap.normal.z - normal.z * perpendicular};
        if (G::Dot(projected, projected) < 1.e-24)
        {
            return false;
        }
        projected = G::Unit(projected);
        return G::Dot(G::Cross(first.normal, projected), normal) >= -1.e-18 &&
               G::Dot(G::Cross(projected, last.normal), normal) >= -1.e-18 &&
               G::Dot(projected, cap.normal) > cap.dot;
    };
    std::array<SurfacePoint, count> points;
    std::array<bool, count> allowed;
    for (int x = 0; x < columns; ++x)
    {
        for (int y = 0; y < rows; ++y)
        {
            int index = x * rows + y;
            double along = distance * x / (columns - 1), across = width * (y - rows / 2) / (rows / 2);
            auto point =
                G::FromLocal(frame, {dx * along - dy * across, dy * along + dx * across, 0}, p.radiusMeters);
            point.altitudeMeters = result.terrain.Height(point);
            bool valid = point.altitudeMeters >= PlanetTerrain::SeaLevelMeters + 4;
            for (const auto &cap : forbidden)
            {
                if (G::Dot(point.normal, cap.normal) > cap.dot)
                {
                    valid = false;
                    break;
                }
            }
            points[index] = point;
            allowed[index] = valid;
        }
    }
    points[start] = a;
    points[goal] = b;
    allowed[start] = allowed[goal] = true;
    std::array<double, count> costs;
    costs.fill(std::numeric_limits<double>::infinity());
    std::array<int, count> parent;
    parent.fill(-1);
    struct Item
    {
        double total, cost;
        int index;
    };
    struct Later
    {
        bool operator()(const Item &a, const Item &b) const
        {
            return std::tie(a.total, a.cost, a.index) > std::tie(b.total, b.cost, b.index);
        }
    };
    std::priority_queue<Item, std::vector<Item>, Later> queue;
    Random rng{WorldIdentity::StreamSeed(p, from.id, to.id)};
    double bend = (rng.Unit() > .5 ? 1 : -1) * (1 + rng.Unit() * 1.5);
    costs[start] = 0;
    queue.push({distance, 0, start});
    while (!queue.empty())
    {
        auto current = queue.top();
        queue.pop();
        if (current.cost != costs[current.index])
        {
            continue;
        }
        if (current.index == goal)
        {
            break;
        }
        int x = current.index / rows, y = current.index % rows;
        for (int ox = -1; ox <= 1; ++ox)
        {
            for (int oy = -1; oy <= 1; ++oy)
            {
                int nx = x + ox, ny = y + oy, next = nx * rows + ny;
                if ((ox == 0 && oy == 0) || nx < 0 || nx >= columns || ny < 0 || ny >= rows || !allowed[next])
                {
                    continue;
                }
                double step = Distance(p, points[current.index], points[next]);
                double grade =
                    step > .01
                        ? std::fabs(points[next].altitudeMeters - points[current.index].altitudeMeters) / step
                        : 0;
                if (grade > settings.maximumRoadGrade)
                {
                    continue;
                }
                // A road segment must avoid an authored building even between distant search samples.
                if (result.cityCores && std::any_of(forbidden.begin(), forbidden.end(), [&](const auto &cap) {
                        return crosses(points[current.index], points[next], cap);
                    }))
                {
                    continue;
                }
                auto middle = Arc(p, points[current.index], points[next], .5);
                if (result.terrain.Height(middle) < PlanetTerrain::SeaLevelMeters + 4)
                {
                    continue;
                }
                double preferred = rows / 2 + std::sin(G::Pi * nx / (columns - 1)) * bend;
                double cost = current.cost + step * (1 + grade * 8 + std::fabs(ny - preferred) * .07);
                if (cost >= costs[next])
                {
                    continue;
                }
                if (!GroundSegment(result.terrain, points[current.index], points[next],
                                   settings.maximumRoadGrade))
                {
                    continue;
                }
                costs[next] = cost;
                parent[next] = current.index;
                queue.push({cost + Distance(p, points[next], b), cost, next});
            }
        }
    }
    if (parent[goal] < 0)
    {
        return {};
    }
    std::vector<SurfacePoint> path;
    for (int node = goal; node >= 0; node = parent[node])
    {
        path.push_back(points[node]);
        if (node == start)
        {
            break;
        }
    }
    std::reverse(path.begin(), path.end());
    auto usable = [&](const std::vector<SurfacePoint> &candidate) {
        for (std::size_t i = 1; i < candidate.size(); ++i)
        {
            if (result.cityCores && std::any_of(forbidden.begin(), forbidden.end(), [&](const auto &cap) {
                    return crosses(candidate[i - 1], candidate[i], cap);
                }))
            {
                return false;
            }
            if (!GroundSegment(result.terrain, candidate[i - 1], candidate[i], settings.maximumRoadGrade))
            {
                return false;
            }
            unsigned steps =
                (std::max)(1u, unsigned(std::ceil(Distance(p, candidate[i - 1], candidate[i]) / 500)));
            for (unsigned step = 0; step <= steps; ++step)
            {
                auto point = Arc(p, candidate[i - 1], candidate[i], double(step) / steps);
                for (const auto &cap : forbidden)
                {
                    if (G::Dot(point.normal, cap.normal) > cap.dot)
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    };
    if (!usable(path))
    {
        return {};
    }
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        std::vector<SurfacePoint> smooth = {path.front()};
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            for (double t : {.25, .75})
            {
                auto point = Arc(p, path[i - 1], path[i], t);
                point.altitudeMeters = result.terrain.Height(point);
                smooth.push_back(point);
            }
        }
        smooth.push_back(path.back());
        if (!usable(smooth))
        {
            break;
        }
        path = std::move(smooth);
    }
    return path;
}
TravelConnection Connect(const GeneratedPlanet &result, unsigned from, unsigned to,
                         const GenerationSettings &settings)
{
    TravelConnection edge;
    edge.from = from;
    edge.to = to;
    edge.points = GroundPath(result, result.nodes[from], result.nodes[to], settings);
    const auto &p = result.terrain.Planet();
    if (edge.points.empty())
    {
        edge.mode = RouteMode::Air;
        const auto &a = result.nodes[from].location.position, &b = result.nodes[to].location.position;
        unsigned steps = (std::max)(4u, unsigned(std::ceil(Distance(p, a, b) / 100000)));
        for (unsigned i = 0; i <= steps; ++i)
        {
            auto point = Arc(p, a, b, double(i) / steps);
            if (i > 0 && i < steps)
            {
                point.altitudeMeters = (std::max)(point.altitudeMeters, result.terrain.Height(point) + 300);
            }
            edge.points.push_back(point);
        }
        edge.points.front() = a;
        edge.points.back() = b;
    }
    for (std::size_t i = 1; i < edge.points.size(); ++i)
    {
        edge.lengthMeters += Distance(p, edge.points[i - 1], edge.points[i]);
    }
    return edge;
}
void BuildConnections(GeneratedPlanet &result, const GenerationSettings &settings,
                      const std::atomic_bool *cancel)
{
    const auto &p = result.terrain.Planet();
    for (const auto &region : result.regions)
    {
        auto access = region.access;
        if (result.cityCores)
        {
            if (auto core = result.cityCores->Find(region.definition.id); core && !core->approaches.empty())
            {
                access.position = G::FromLocal(region.frame, core->approaches.front().join, p.radiusMeters);
                access.position.altitudeMeters = result.terrain.Height(access.position);
            }
        }
        result.nodes.push_back({"site_" + region.definition.id, region.definition.id, access, false});
    }
    const unsigned sites = unsigned(result.nodes.size());
    if (sites == 0)
    {
        return;
    }
    struct Pair
    {
        double cost;
        unsigned a, b;
    };
    std::vector<Pair> pairs;
    for (unsigned a = 0; a < sites; ++a)
    {
        for (unsigned b = a + 1; b < sites; ++b)
        {
            double distance =
                Distance(p, result.nodes[a].location.position, result.nodes[b].location.position);
            bool land = result.terrain.Height(Arc(p, result.nodes[a].location.position,
                                                  result.nodes[b].location.position, .5)) >
                        PlanetTerrain::SeaLevelMeters;
            pairs.push_back({distance * (land && distance < settings.roadLimitMeters ? 1. : 1.4), a, b});
        }
    }
    std::sort(pairs.begin(), pairs.end(),
              [](auto a, auto b) { return std::tie(a.cost, a.a, a.b) < std::tie(b.cost, b.a, b.b); });
    std::vector<unsigned> group(sites);
    std::iota(group.begin(), group.end(), 0u);
    auto root = [&](unsigned node) {
        while (group[node] != node)
        {
            group[node] = group[group[node]];
            node = group[node];
        }
        return node;
    };
    std::set<std::pair<unsigned, unsigned>> used;
    auto connect = [&](unsigned a, unsigned b) {
        if (cancel && cancel->load())
        {
            throw std::runtime_error("Planet routes cancelled");
        }
        if (a > b)
        {
            std::swap(a, b);
        }
        if (!used.insert({a, b}).second)
        {
            return;
        }
        result.connections.push_back(Connect(result, a, b, settings));
    };
    for (auto pair : pairs)
    {
        unsigned a = root(pair.a), b = root(pair.b);
        if (a == b)
        {
            continue;
        }
        connect(pair.a, pair.b);
        group[a] = b;
    }
    for (unsigned node = 0; node < sites; ++node)
    {
        unsigned added = 0;
        for (auto pair : pairs)
        {
            if (pair.cost > settings.roadLimitMeters * .65)
            {
                break;
            }
            if (pair.a != node && pair.b != node)
            {
                continue;
            }
            if (!used.count({pair.a, pair.b}))
            {
                connect(pair.a, pair.b);
                if (++added == 1)
                {
                    break;
                }
            }
        }
    }
    for (const auto &relay : result.relays)
    {
        unsigned nearest = 0;
        double distance = std::numeric_limits<double>::infinity();
        for (unsigned i = 0; i < sites; ++i)
        {
            if (result.nodes[i].regionId == relay.regionId)
            {
                double candidate = Distance(p, result.nodes[i].location.position, relay.location.position);
                if (candidate < distance)
                {
                    distance = candidate;
                    nearest = i;
                }
            }
        }
        unsigned index = unsigned(result.nodes.size());
        result.nodes.push_back({relay.id, relay.regionId, relay.location, true});
        connect(nearest, index);
    }
}
} // namespace

void GeneratedPlanet::Validate() const
{
    const auto &p = terrain.Planet();
    p.Validate();
    Require(std::isfinite(relayCoverageMeters) && relayCoverageMeters >= 500 &&
                std::isfinite(regionGapMeters) && regionGapMeters > 0 && std::isfinite(maximumRoadGrade) &&
                maximumRoadGrade > 0 && maximumRoadGrade <= .25,
            "Invalid generated manifest budgets");
    std::set<std::string> ids, names, nodeIds, relayIds;
    for (const auto &r : regions)
    {
        r.definition.Validate(p);
        r.center.Validate(p);
        r.access.Validate(p);
        G::Validate(r.frame);
        Require(r.center.regionId == r.definition.id && r.access.regionId == r.definition.id &&
                    Distance(p, r.frame.origin, r.center.position) < .01 &&
                    std::fabs(r.frame.origin.altitudeMeters - r.center.position.altitudeMeters) < .01,
                "Region geometry and ownership disagree");
        Require(ids.insert(r.definition.id).second && names.insert(r.definition.name).second,
                "Duplicate generated identity");
        Require(r.footprint.size() >= 3 && r.areaMeters2 > 0 && std::isfinite(r.areaMeters2),
                "Invalid region footprint");
        Require(std::isfinite(r.boundingRadiusMeters) && r.boundingRadiusMeters > 0, "Invalid region bound");
        if (r.definition.kind == RegionKind::Settlement)
        {
            Require(std::fabs(r.areaMeters2 - r.definition.TargetAreaKm2() * 1.e6) < .01,
                    "City target area mismatch");
        }
        Require(std::fabs(PolygonArea(p, r.frame, r.footprint) / r.areaMeters2 - 1) < 1.e-5,
                "Spherical footprint area mismatch");
        Require(terrain.Sample(r.access.position).land, "Region entrance is under water");
        for (auto v : r.footprint)
        {
            Require(std::hypot(v.x, v.y) <= r.boundingRadiusMeters + .01, "Invalid footprint bounds");
        }
        for (const auto &other : regions)
        {
            if (r.definition.id < other.definition.id)
            {
                Require(
                    !Overlaps(p, r, other,
                              r.definition.storyFixed && other.definition.storyFixed ? 0 : regionGapMeters),
                    "Generated physical regions overlap or lose their reserved gap");
            }
        }
        std::vector<SurfacePoint> candidates;
        for (const auto &relay : relays)
        {
            if (relay.regionId == r.definition.id)
            {
                candidates.push_back(relay.location.position);
            }
        }
        Require(!candidates.empty(), "Region has no relay candidate");
        for (auto local : CoverageSamples(r, relayCoverageMeters))
        {
            auto point = G::FromLocal(r.frame, local, p.radiusMeters);
            double nearest = std::numeric_limits<double>::infinity();
            for (auto candidate : candidates)
            {
                nearest = (std::min)(nearest, Distance(p, point, candidate));
            }
            Require(nearest <= relayCoverageMeters * .7 + .01,
                    "Relay candidates do not cover the regional footprint samples");
        }
    }
    for (const auto &anchor : anchors)
    {
        anchor.Validate(p);
        const auto &r = Region(anchor.location.regionId);
        Require(r.definition.storyFixed, "Story anchor has no fixed reservation");
        Require(std::fabs(terrain.Height(anchor.location.position) -
                          anchor.location.position.altitudeMeters) < .001,
                "Terrain moved a fixed story anchor");
    }
    for (const auto &node : nodes)
    {
        node.location.Validate(p);
        Require(WorldIdentity::ValidId(node.id) && nodeIds.insert(node.id).second &&
                    ids.count(node.regionId) && node.regionId == node.location.regionId,
                "Invalid travel node identity");
    }
    for (const auto &relay : relays)
    {
        relay.location.Validate(p);
        Require(relayIds.insert(relay.id).second && nodeIds.count(relay.id),
                "Relay not attached to the travel graph");
        Require(ids.count(relay.regionId) && relay.location.regionId == relay.regionId,
                "Invalid relay ownership");
        const auto &node =
            *std::find_if(nodes.begin(), nodes.end(), [&](const auto &n) { return n.id == relay.id; });
        Require(node.relay && node.regionId == relay.regionId &&
                    Distance(p, node.location.position, relay.location.position) < .01,
                "Detached relay node");
        Require(terrain.Sample(relay.location.position).land, "Relay candidate under water");
    }
    for (const auto &edge : connections)
    {
        Require(edge.from < nodes.size() && edge.to < nodes.size() && edge.from != edge.to &&
                    edge.points.size() >= 2,
                "Invalid travel connection");
        Require(Distance(p, edge.points.front(), nodes[edge.from].location.position) < .01 &&
                    Distance(p, edge.points.back(), nodes[edge.to].location.position) < .01 &&
                    std::fabs(edge.points.front().altitudeMeters -
                              nodes[edge.from].location.position.altitudeMeters) < .01 &&
                    std::fabs(edge.points.back().altitudeMeters -
                              nodes[edge.to].location.position.altitudeMeters) < .01,
                "Detached travel endpoints");
        Require(std::isfinite(edge.lengthMeters) && edge.lengthMeters >= 0, "Invalid travel length");
        for (const auto &point : edge.points)
        {
            G::Validate(point);
        }
        Require(edge.mode == RouteMode::Road || edge.mode == RouteMode::Air, "Unknown route mode");
        double length = 0;
        for (std::size_t i = 1; i < edge.points.size(); ++i)
        {
            length += Distance(p, edge.points[i - 1], edge.points[i]);
            if (edge.mode == RouteMode::Road)
            {
                Require(GroundSegment(terrain, edge.points[i - 1], edge.points[i], maximumRoadGrade),
                        "Road crosses water or an excessive grade");
            }
        }
        Require(std::fabs(length - edge.lengthMeters) < .01, "Travel length does not match its path");
    }
    Require(Connected(), "Generated travel graph is disconnected");
}
std::uint64_t GeneratedPlanet::Digest() const
{
    std::uint64_t hash = 14695981039346656037ull;
    auto number = [&](std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i)
        {
            hash ^= (value >> (i * 8)) & 255;
            hash *= 1099511628211ull;
        }
    };
    auto scalar = [&](double value) {
        std::uint64_t bits;
        std::memcpy(&bits, &value, 8);
        number(bits);
    };
    auto text = [&](std::string_view value) {
        number(value.size());
        for (unsigned char c : value)
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
    };
    auto point = [&](SurfacePoint p) {
        scalar(p.normal.x);
        scalar(p.normal.y);
        scalar(p.normal.z);
        scalar(p.altitudeMeters);
    };
    const auto &p = terrain.Planet();
    text(p.id);
    number(p.seed);
    number(p.generatorVersion);
    scalar(p.radiusMeters);
    scalar(regionGapMeters);
    scalar(maximumRoadGrade);
    scalar(relayCoverageMeters);
    number(terrain.Homeland().has_value());
    if (terrain.Homeland())
    {
        point(*terrain.Homeland());
    }
    number(terrain.Reservations().size());
    for (const auto &pad : terrain.Reservations())
    {
        text(pad.id);
        point(pad.center);
        scalar(pad.radiusMeters);
        scalar(pad.featherMeters);
        scalar(pad.elevationMeters);
        number(pad.priority);
    }
    number(anchors.size());
    for (const auto &anchor : anchors)
    {
        text(anchor.id);
        text(anchor.location.regionId);
        point(anchor.location.position);
    }
    number(clusters.size());
    for (const auto &center : clusters)
    {
        point(center);
    }
    number(regions.size());
    for (const auto &r : regions)
    {
        text(r.definition.id);
        text(r.definition.name);
        number(unsigned(r.definition.kind));
        number(unsigned(r.definition.tier));
        number(unsigned(r.center.space));
        number(std::uint64_t(r.center.layer));
        point(r.center.position);
        point(r.access.position);
        scalar(r.areaMeters2);
        number(unsigned(r.biome));
        number(r.footprint.size());
        for (auto v : r.footprint)
        {
            scalar(v.x);
            scalar(v.y);
        }
    }
    number(relays.size());
    for (const auto &relay : relays)
    {
        text(relay.id);
        text(relay.regionId);
        point(relay.location.position);
    }
    number(nodes.size());
    for (const auto &node : nodes)
    {
        text(node.id);
        text(node.regionId);
        number(node.relay);
        point(node.location.position);
    }
    number(connections.size());
    for (const auto &edge : connections)
    {
        number(edge.from);
        number(edge.to);
        number(unsigned(edge.mode));
        scalar(edge.lengthMeters);
        number(edge.points.size());
        for (auto position : edge.points)
        {
            point(position);
        }
    }
    if (cityCores)
    {
        number(cityCores->Digest());
    }
    return hash;
}
} // namespace world
