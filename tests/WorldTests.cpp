#include "stdafx.h"
#include "World.h"
#include "Game.h"
#include "Level.h"
#include "Overworld.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
using namespace world;
std::size_t checks = 0;
void Check(bool value, const char *message)
{
    ++checks;
    if (!value)
    {
        std::cerr << "World regression: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
double Norm(Vector3d v)
{
    return std::hypot(std::hypot(v.x, v.y), v.z);
}
double Difference(Vector3d a, Vector3d b)
{
    return Norm({a.x - b.x, a.y - b.y, a.z - b.z});
}
template <class F> void Throws(F action, const char *message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    Check(rejected, message);
}
void DefinitionsAndIdentity()
{
    const auto &planet = WorldCatalog::Erma();
    planet.Validate();
    Check(planet.id == "erma" && planet.radiusMeters == 4800000, "Erma profile contract");
    static_assert(WorldCatalog::MetersPerLegacyUnit == models::WorldScale::metersPerUnit);
    std::set<std::string> ids, names;
    unsigned districts = 0, towns = 0, metropolises = 0, capitals = 0;
    for (const auto &region : WorldCatalog::ErmaRegions())
    {
        region.Validate(planet);
        Check(ids.insert(region.id).second && names.insert(region.name).second,
              "Region IDs/names must be unique");
        districts += region.tier == SettlementTier::District;
        towns += region.tier == SettlementTier::Town;
        metropolises += region.tier == SettlementTier::Metropolis;
        capitals += region.tier == SettlementTier::SystemCapital;
    }
    Check(districts == 5 && towns == 1 && metropolises == 1 && capitals == 1,
          "Four settlement tiers are distinct");
    Check(!WorldCatalog::AllowsInstancing(SpaceKind::Surface) &&
              !WorldCatalog::AllowsInstancing(SpaceKind::Subsurface) &&
              !WorldCatalog::AllowsInstancing(SpaceKind::Interior) &&
              WorldCatalog::AllowsInstancing(SpaceKind::AnomalousDomain),
          "Only anomalous domains permit instancing");
    Check(std::fabs(WorldCatalog::Region("asterion").TargetAreaKm2() - 151.3125) < 1.e-10,
          "Seoul quarter-area target");
    Check(WorldCatalog::CityAreaKm2(100) == 25 && WorldCatalog::CityLengthRatio == .5,
          "Area and length scale differ");
    Check(WorldCatalog::Region("mq00_mine").storyFixed && WorldCatalog::Region("frontier").storyFixed,
          "Existing story regions must stay fixed");
    Check(WorldIdentity::StreamSeed(planet, "halo_07", "terrain") == 16501998420092516863ull,
          "Portable seed golden (fixed-width little-endian encoding)");
    auto renamed = planet;
    renamed.name = "표시명 변경";
    Check(renamed.SameWorld(planet) && WorldIdentity::ContentKey(renamed, "halo_07", "home") ==
                                           WorldIdentity::ContentKey(planet, "halo_07", "home"),
          "Display name must not affect identity");
    for (unsigned change = 0; change < 3; ++change)
    {
        auto other = planet;
        if (change == 0)
        {
            ++other.seed;
        }
        if (change == 1)
        {
            ++other.generatorVersion;
        }
        if (change == 2)
        {
            other.id = "other_planet";
        }
        Check(!other.SameWorld(planet) && WorldIdentity::StreamSeed(other, "halo_07", "terrain") !=
                                              WorldIdentity::StreamSeed(planet, "halo_07", "terrain"),
              "Planet/seed/version isolation");
        Check(WorldIdentity::ContentKey(other, "halo_07", "home") !=
                  WorldIdentity::ContentKey(planet, "halo_07", "home"),
              "Persistent content key must isolate worlds");
    }
    Check(WorldIdentity::StreamSeed(planet, "halo_07", "terrain") !=
              WorldIdentity::StreamSeed(planet, "halo_07", "roads"),
          "Independent generation streams");
    renamed = planet;
    renamed.radiusMeters += 1;
    Check(!renamed.SameWorld(planet), "Radius mismatch must be detectable on state restoration");
    for (const char *id : {"", "..", "with/slash", "with:colon", "bad name", "한글"})
    {
        Check(!WorldIdentity::ValidId(id), "Unsafe/ambiguous persistent ID");
    }
    Throws(
        [&] {
            auto p = planet;
            p.radiusMeters = 0;
            p.Validate();
        },
        "Zero radius");
    Throws(
        [&] {
            auto p = planet;
            p.radiusMeters = std::numeric_limits<double>::infinity();
            p.Validate();
        },
        "Infinite radius");
    Throws(
        [&] {
            auto p = planet;
            p.generatorVersion = 0;
            p.Validate();
        },
        "Missing generator version");
    Throws(
        [&] {
            auto r = WorldCatalog::Region("mq00_mine");
            r.tier = SettlementTier::Town;
            r.Validate(planet);
        },
        "Non-city tier");
    Throws(
        [&] {
            auto r = WorldCatalog::Region("halo_07");
            r.referenceAreaKm2 = -1;
            r.Validate(planet);
        },
        "Negative city area");
    Throws([&] { WorldCatalog::Region("not_registered"); }, "Unknown region ID");
    Throws([&] { WorldIdentity::ContentKey(planet, "halo_07", "../../alias"); }, "No separator alias in IDs");
}
void GeographicRoundTrips()
{
    using G = SurfaceGeometry;
    for (double radius : {2440000., 3389500., 4800000., 6371000.})
    {
        for (double latitude : {-90., -89.99, -45., 0., 37.5, 89.99, 90.})
        {
            for (double longitude : {-180., -128., 0., 90., 179.99, 540.})
            {
                auto point = G::FromDegrees(latitude, longitude, 17.25);
                auto frame = G::Frame(point);
                G::Validate(frame);
                auto degrees = G::ToDegrees(point);
                Check(std::fabs(degrees[0] - latitude) < 1.e-10, "Latitude round trip");
                if (std::fabs(latitude) < 90)
                {
                    Check(std::fabs(std::remainder(degrees[1] - longitude, 360.)) < 1.e-10, "Longitude wrap");
                }
                Check(std::fabs(Norm(G::Cartesian(radius, point)) - radius - 17.25) < 1.e-8,
                      "Altitude in metres");
                for (auto local :
                     {Vector3d{0, 0, 0}, {.001, -.002, .003}, {-880, 880, -20}, {30000, -45000, 120}})
                {
                    auto moved = G::FromLocal(frame, local, radius);
                    Check(Difference(local, G::ToLocal(frame, moved, radius)) < 1.e-6,
                          "Local exponential/log round trip including poles");
                    Check(std::fabs(G::Distance(radius, point, moved) - std::hypot(local.x, local.y)) < 1.e-6,
                          "Surface metre distance independent of projection");
                }
            }
        }
        auto frame = G::Frame(G::FromDegrees(89.99, 179.99));
        auto acrossPole = G::Advance(frame, 0, 5000, radius);
        Check(G::Dot(frame.right, acrossPole.right) > .99, "Transported basis cannot flip at north pole");
        auto newOrigin = G::Advance(frame, 50000, -80000, radius);
        Vector3d direction = {3.2, -1.1, .4};
        auto transformed = G::TransformDirection(direction, frame, newOrigin);
        Check(std::fabs(Norm(transformed) - Norm(direction)) < 1.e-10,
              "Origin rebase preserves direction magnitude");
        Check(Difference(G::TransformDirection(transformed, newOrigin, frame), direction) < 1.e-10,
              "Origin rebase direction inverse");
        auto remote = G::FromLocal(frame, {60000, -45000, 8}, radius);
        auto restored = G::FromLocal(newOrigin, G::ToLocal(newOrigin, remote, radius), radius);
        Check(G::Distance(radius, remote, restored) < 1.e-6,
              "Changing local origin preserves absolute location");
    }
    auto frame = G::Frame(G::FromDegrees(0, 0));
    Check(Difference(G::Unit({1.e-320, 0, 0}), {1, 0, 0}) == 0,
          "Normalizing a tiny finite direction must not overflow its reciprocal");
    Throws([&] { G::Unit({0, 0, 0}); }, "Zero normal");
    Throws([&] { G::FromDegrees(90.1, 0); }, "Invalid latitude");
    Throws([&] { G::FromDegrees(0, std::numeric_limits<double>::quiet_NaN()); }, "NaN longitude");
    Throws([&] { G::FromLocal(frame, {G::Pi * 4800000, 0, 0}, 4800000); }, "Ambiguous local antipode");
    Throws([&] { G::ToLocal(frame, G::FromDegrees(0, 180), 4800000); }, "Antipode has no unique inverse");
    Throws(
        [&] {
            auto bad = frame;
            bad.right = {1, 0, 0};
            G::Advance(bad, 1, 0, 4800000);
        },
        "Degenerate tangent frame");
    Throws([&] { G::Cartesian(100, G::FromDegrees(0, 0, -101)); }, "Height crosses planet center");
}
void Circumnavigation()
{
    using G = SurfaceGeometry;
    for (double radius : {2440000., 3389500., 4800000., 6371000.})
    {
        for (auto start : {G::FromDegrees(0, 0), G::FromDegrees(90, 0), G::FromDegrees(-90, 0),
                           G::FromDegrees(37.5, 128), SurfacePoint{G::Unit({1, 1, 1}), 0}})
        {
            for (double heading : {0., .3, 1.3, 2.8})
            {
                auto frame = G::Frame(start), moved = frame;
                double x = std::cos(heading), y = std::sin(heading), circumference = G::Circumference(radius);
                auto full = G::Advance(frame, x * circumference, y * circumference, radius);
                Check(G::Distance(radius, start, full.origin) < 1.e-6, "One-step great circle closure");
                for (unsigned i = 0; i < 4096; ++i)
                {
                    moved = G::Advance(moved, x * circumference / 4096, y * circumference / 4096, radius);
                }
                Check(G::Distance(radius, start, moved.origin) < .001,
                      "Incremental great circle closure within a millimetre");
                Check(Difference(frame.right, moved.right) < 1.e-9 &&
                          Difference(frame.forward, moved.forward) < 1.e-9,
                      "Heading returns after a complete great circle");
                auto outward = G::Advance(frame, x * 120000, y * 120000, radius);
                auto backward = G::Advance(outward, -x * 120000, -y * 120000, radius);
                Check(G::Distance(radius, start, backward.origin) < 1.e-5,
                      "Transported reverse movement returns to start");
                auto half = G::Advance(frame, x * circumference * .5, y * circumference * .5, radius);
                Check(std::fabs(G::Distance(radius, start, half.origin) - circumference * .5) < 1.e-6,
                      "Antipodal shortest distance");
            }
        }
    }
    Check(G::Circumference(6371000) > G::Circumference(3389500), "Planet radius controls travel distance");
}
unsigned SharedCorners(const ChunkKey &a, const ChunkKey &b)
{
    unsigned count = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        for (unsigned j = 0; j < 4; ++j)
        {
            if (Difference(CubeSphere::Corner(a, i % 2, i / 2).normal,
                           CubeSphere::Corner(b, j % 2, j / 2).normal) < 1.e-12)
            {
                ++count;
            }
        }
    }
    return count;
}
void CheckNeighbors(const ChunkKey &key)
{
    std::set<ChunkKey> neighbors;
    for (unsigned direction = 0; direction < 4; ++direction)
    {
        auto next = CubeSphere::Neighbor(key, ChunkEdge(direction));
        next.Validate();
        Check(!(next == key) && neighbors.insert(next).second, "Distinct cardinal neighbors");
        Check(next.planetId == key.planetId && next.planetSeed == key.planetSeed &&
                  next.generatorVersion == key.generatorVersion && next.level == key.level &&
                  next.layer == key.layer,
              "Neighbors preserve world/version/layer");
        bool reciprocal = false;
        for (unsigned reverse = 0; reverse < 4; ++reverse)
        {
            reciprocal |= CubeSphere::Neighbor(next, ChunkEdge(reverse)) == key;
        }
        Check(reciprocal, "Face seam neighbor reciprocity");
        Check(SharedCorners(key, next) == 2, "Adjacent chunks share their full edge");
    }
}
void CubeSeamsAndKeys()
{
    const auto &planet = WorldCatalog::Erma();
    for (unsigned level = 0; level <= 4; ++level)
    {
        unsigned cells = 1u << level;
        for (unsigned face = 0; face < 6; ++face)
        {
            for (unsigned y = 0; y < cells; ++y)
            {
                for (unsigned x = 0; x < cells; ++x)
                {
                    ChunkKey key{planet.id,
                                 planet.seed,
                                 planet.generatorVersion,
                                 CubeFace(face),
                                 std::uint8_t(level),
                                 x,
                                 y,
                                 -2};
                    auto center = CubeSphere::Center(key);
                    Check(CubeSphere::Address(planet, center, std::uint8_t(level), -2) == key,
                          "Canonical center ownership");
                    CheckNeighbors(key);
                    auto children = CubeSphere::Children(key);
                    std::set<ChunkKey> unique;
                    for (const auto &child : children)
                    {
                        Check(CubeSphere::Parent(child) == key && unique.insert(child).second,
                              "Quadtree parent/child contract");
                    }
                }
            }
        }
    }
    // Fine seams must not floor a projected float to the next row through cancellation.
    constexpr std::uint32_t cells = 1u << 30;
    for (unsigned face = 0; face < 6; ++face)
    {
        for (std::uint32_t along : {0u, 1u, cells / 2, cells - 2, cells - 1})
        {
            for (unsigned edge = 0; edge < 4; ++edge)
            {
                ChunkKey key{planet.id,
                             planet.seed,
                             planet.generatorVersion,
                             CubeFace(face),
                             30,
                             edge < 2 ? (edge == 0 ? 0 : cells - 1) : along,
                             edge < 2 ? along : (edge == 2 ? 0 : cells - 1),
                             0};
                CheckNeighbors(key);
                Check(CubeSphere::Address(planet, CubeSphere::Center(key), 30) == key,
                      "Fine center round trip");
            }
        }
    }
    auto corner = SurfacePoint{SurfaceGeometry::Unit({1, 1, 1}), 0};
    Check(CubeSphere::Address(planet, corner, 0).face == CubeFace::PositiveX,
          "Canonical exact corner ownership");
    auto key = CubeSphere::Address(planet, SurfaceGeometry::FromDegrees(0, 0), 2);
    auto second = planet;
    ++second.seed;
    Check(!(CubeSphere::Address(second, CubeSphere::Center(key), 2) == key), "Chunk seed isolation");
    Check(key.StableKey() == "p/erma/s/4557457/v/1/l/0/f/0/q/2/x/2/y/2", "Stable chunk key encoding");
    Throws(
        [&] {
            auto bad = key;
            bad.x = 4;
            bad.Validate();
        },
        "Out-of-face chunk address");
    Throws(
        [&] {
            auto bad = key;
            bad.level = 31;
            bad.Validate();
        },
        "Unsupported chunk level");
    Throws(
        [&] {
            auto bad = key;
            bad.face = CubeFace(99);
            bad.Validate();
        },
        "Invalid cube face");
    Throws([&] { CubeSphere::Neighbor(key, ChunkEdge(99)); }, "Invalid chunk direction");
    Throws([&] { CubeSphere::Parent(CubeSphere::Address(planet, corner, 0)); }, "Root has no parent");
    Throws(
        [&] {
            auto leaf = key;
            leaf.level = 30;
            CubeSphere::Children(leaf);
        },
        "Leaf cannot overflow integer key");
}
void LegacyIntegration()
{
    const auto &planet = WorldCatalog::Erma();
    const auto &adapter = LegacyWorldAdapter::ErmaExterior();
    for (auto local : {Vector3d{}, Vector3d{-440, 440, 0}, Vector3d{128, 18, -10}, Vector3d{302, 97, 4.2}})
    {
        auto p = adapter.Locate("overworld", local);
        p.Validate(planet);
        Check(Difference(local, adapter.Local(p)) < 1.e-7, "Legacy coordinates round trip with metre scale");
    }
    auto surface = adapter.Locate("halo_07", {}),
         underground = adapter.Locate("mq00_mine", {128, 18, -5}, SpaceKind::Subsurface, -1);
    Check(!surface.SameSpace(underground), "Surface/subsurface path layers cannot alias");
    Check(surface.SameSpace(adapter.Locate("cinder", {302, 97, 0})),
          "Region boundary alone does not split physical surface");
    auto domain = surface;
    domain.space = SpaceKind::AnomalousDomain;
    domain.domainId = "gate_domain";
    domain.domainLocalMeters = {3, 4, 5};
    domain.Validate(planet);
    Throws([&] { adapter.Local(domain); },
           "Domain interior cannot be projected as ordinary planetary ground");
    Throws(
        [&] {
            auto old = surface;
            ++old.planetSeed;
            adapter.Local(old);
        },
        "Foreign seed local conversion");
    Throws(
        [&] {
            auto old = surface;
            ++old.generatorVersion;
            adapter.Local(old);
        },
        "Foreign version local conversion");
    Throws(
        [&] {
            auto old = surface;
            old.layer = 1;
            old.Validate(planet);
        },
        "Surface layer mismatch");
    auto &game = tutorial::Game::Get();
    auto &state = game.GetState();
    game.Reset();
    Check(!game.PlanetLocation(), "Unmigrated home stays a legacy interior");
    state.isInsideHome = false;
    for (const auto &city : tutorial::Overworld::Cities())
    {
        state.playerPosition = city.center;
        auto before = state.playerPosition;
        auto p = game.PlanetLocation();
        Check(p && p->regionId == city.id, "Live city region uses existing stable ID");
        Check(city.tier == SettlementTier::District && WorldCatalog::Region(city.id).name == city.name,
              "Five live cities share city tier/name metadata");
        auto restored = adapter.Local(*p);
        Check(std::hypot(restored.x - before.x, restored.y - before.y) < 1.e-7 &&
                  state.playerPosition.x == before.x && state.playerPosition.y == before.y,
              "Read-only live location bridge");
    }
    const auto &anchors = tutorial::Overworld::StoryAnchors();
    Check(anchors.size() == 5, "Five existing exterior story anchors");
    std::set<std::string> anchorIds;
    for (const auto &anchor : anchors)
    {
        anchor.Validate(planet);
        Check(anchorIds.insert(anchor.id).second && WorldCatalog::Region(anchor.location.regionId).storyFixed,
              "Fixed anchor identity and registered story region");
    }
    auto home = adapter.Local(anchors.front().location);
    Check(std::hypot(home.x - tutorial::kHomeDoor.x, home.y - tutorial::kHomeDoor.y) < 1.e-7,
          "Existing home coordinate is the source of truth");
    game.EnterTestStage(tutorial::TestStage::Mine);
    Check(!game.PlanetLocation(), "A does not pretend the legacy mine is seamless");
    game.EnterTestStage(tutorial::TestStage::ChapterOne);
    Check(!game.PlanetLocation(), "A does not pretend the legacy frontier is seamless");
    game.Reset();
}
} // namespace
int main()
{
    DefinitionsAndIdentity();
    GeographicRoundTrips();
    Circumnavigation();
    CubeSeamsAndKeys();
    LegacyIntegration();
    std::cout << "Planet foundation checks passed: " << checks
              << " assertions, definitions/IDs, sphere closure, poles, "
                 "precision, cube seams/LOD and read-only legacy integration.\n";
}
