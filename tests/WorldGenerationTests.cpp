#include "stdafx.h"
#include "Overworld.h"
#include "WorldGeneration.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <set>

namespace
{
using namespace world;
using G = SurfaceGeometry;
std::size_t checks = 0;
void Check(bool value, const char *message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
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
double Distance(const GeneratedPlanet &plan, SurfacePoint a, SurfacePoint b)
{
    return G::Distance(plan.terrain.Planet().radiusMeters, a, b);
}
SurfacePoint Between(const GeneratedPlanet &plan, SurfacePoint a, SurfacePoint b, double t)
{
    auto frame = G::Frame(a);
    auto local = G::ToLocal(frame, b, plan.terrain.Planet().radiusMeters);
    return G::Advance(frame, local.x * t, local.y * t, plan.terrain.Planet().radiusMeters).origin;
}
bool Inside(const GeneratedRegion &r, double x, double y)
{
    for (std::size_t i = 0; i < r.footprint.size(); ++i)
    {
        auto a = r.footprint[i], b = r.footprint[(i + 1) % r.footprint.size()];
        if ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x) < -1.e-5)
        {
            return false;
        }
    }
    return true;
}
void Layout(const GeneratedPlanet &plan, unsigned extras)
{
    plan.Validate();
    Check(plan.regions.size() == 14 + extras, "All named regions and procedural slots must exist");
    Check(plan.clusters.size() == 12, "Bounded organic population clusters");
    std::set<std::string> names;
    for (const auto &r : plan.regions)
    {
        Check(names.insert(r.definition.name).second, "Unique readable region names");
        Check(r.center.space != SpaceKind::AnomalousDomain, "Physical sites cannot become instanced domains");
        double twiceArea = 0;
        for (std::size_t i = 0; i < r.footprint.size(); ++i)
        {
            auto a = r.footprint[i], b = r.footprint[(i + 1) % r.footprint.size()];
            twiceArea += a.x * b.y - b.x * a.y;
            Check(Inside(r, (a.x + b.x) * .5, (a.y + b.y) * .5), "Convex footprints contain their edges");
        }
        Check(std::fabs(twiceArea * .5 / r.areaMeters2 - 1) < .001,
              "Footprint area scale, independently measured in metres");
        if (r.definition.kind == RegionKind::Settlement)
        {
            Check(std::fabs(r.areaMeters2 - r.definition.referenceAreaKm2 * .25e6) < .01,
                  "Quarter area, not quarter length");
        }
        Check(plan.terrain.Sample(r.access.position).land, "Every region has a land entrance");
        for (const auto &other : plan.regions)
        {
            if (r.definition.id < other.definition.id && r.center.SameSpace(other.center))
            {
                Check(Distance(plan, r.center.position, other.center.position) + .01 >=
                          r.boundingRadiusMeters + other.boundingRadiusMeters +
                              (r.definition.storyFixed && other.definition.storyFixed ? 0
                                                                                      : plan.regionGapMeters),
                      "Organic placement retains physical separation");
            }
        }
        unsigned relays = 0;
        for (const auto &relay : plan.relays)
        {
            relays += relay.regionId == r.definition.id;
        }
        Check(relays > 0, "Every site covered by relay candidates");
        if (r.definition.tier >= SettlementTier::Metropolis)
        {
            Check(relays > 1, "Large cities need multiple relays");
        }
        // Independent, offset lattice probes interior points, not just generation's vertices.
        double spacing = plan.relayCoverageMeters * .247;
        for (double x = -r.boundingRadiusMeters + spacing * .37; x < r.boundingRadiusMeters; x += spacing)
        {
            for (double y = -r.boundingRadiusMeters + spacing * .61; y < r.boundingRadiusMeters; y += spacing)
            {
                if (!Inside(r, x, y))
                {
                    continue;
                }
                auto point = G::FromLocal(r.frame, {x, y, 0}, plan.terrain.Planet().radiusMeters);
                double nearest = 1.e99;
                for (const auto &relay : plan.relays)
                {
                    if (relay.regionId == r.definition.id)
                    {
                        nearest = (std::min)(nearest, Distance(plan, point, relay.location.position));
                    }
                }
                Check(nearest <= plan.relayCoverageMeters, "Relay coverage must reach city interiors");
            }
        }
    }
    const auto &mine = plan.Region("mq00_mine");
    Check(mine.center.space == SpaceKind::Subsurface && mine.center.layer == -1 && mine.access.layer == 0,
          "Mine is continuous subsurface space with a surface entrance");
    Check(plan.Region("halo_07").definition.storyFixed, "Halo retains its fixed narrative role");
    Check(Distance(plan, plan.Region("cinder").access.position, plan.Region("halo_07").access.position) <
              70000,
          "Legacy district names remain in Halo's surrounding region");
}
void Routes(const GeneratedPlanet &plan)
{
    Check(plan.Connected(), "Road and air connections reach every site and relay");
    unsigned roads = 0, air = 0, bends = 0;
    for (const auto &edge : plan.connections)
    {
        edge.mode == RouteMode::Road ? ++roads : ++air;
        if (edge.mode == RouteMode::Air)
        {
            continue;
        }
        for (std::size_t i = 1; i < edge.points.size(); ++i)
        {
            const auto a = edge.points[i - 1], b = edge.points[i];
            double length = Distance(plan, a, b);
            unsigned steps = (std::max)(1u, unsigned(std::ceil(length / 211)));
            double previous = plan.terrain.Height(a);
            for (unsigned step = 1; step <= steps; ++step)
            {
                auto point = Between(plan, a, b, double(step) / steps);
                double height = plan.terrain.Height(point);
                Check(height >= PlanetTerrain::SeaLevelMeters + 4, "A road cannot silently bridge an ocean");
                if (length > .1)
                {
                    Check(std::fabs(height - previous) / (length / steps) <= plan.maximumRoadGrade + .003,
                          "Road segments respect their terrain grade budget");
                }
                previous = height;
            }
        }
        if (edge.points.size() > 3 && Distance(plan, edge.points.front(), edge.points.back()) > 1000)
        {
            auto midpoint = edge.points[edge.points.size() / 2];
            bends +=
                Distance(plan, midpoint, Between(plan, edge.points.front(), edge.points.back(), .5)) > 50;
        }
    }
    Check(roads > plan.regions.size() && air > 0 && bends > 0,
          "Local roads bend, distant continents use explicit air routes");
    Check(plan.connections.size() > plan.nodes.size() - 1, "Regional graph offers alternate routes");
    // Five nearby cities must be reachable from Halo on the ground, not only by an air fallback.
    unsigned start = 0;
    for (unsigned i = 0; i < plan.nodes.size(); ++i)
    {
        if (plan.nodes[i].id == "site_halo_07")
        {
            start = i;
        }
    }
    std::set<unsigned> seen = {start};
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto &edge : plan.connections)
        {
            if (edge.mode == RouteMode::Road)
            {
                if (seen.count(edge.from))
                {
                    changed = seen.insert(edge.to).second || changed;
                }
                if (seen.count(edge.to))
                {
                    changed = seen.insert(edge.from).second || changed;
                }
            }
        }
    }
    for (unsigned i = 0; i < plan.nodes.size(); ++i)
    {
        if (plan.nodes[i].id == "site_cinder" || plan.nodes[i].id == "site_veil" ||
            plan.nodes[i].id == "site_lumen" || plan.nodes[i].id == "site_echo")
        {
            Check(seen.count(i) != 0, "Halo's neighboring cities must have ground access");
        }
    }
}
void Reproducibility(const GeneratedPlanet &baseline)
{
    auto definitions = WorldCatalog::ErmaRegions();
    auto anchors = tutorial::Overworld::StoryAnchors();
    std::vector<FixedRegionPlacement> fixed;
    for (const auto &r : baseline.regions)
    {
        if (r.definition.storyFixed)
        {
            fixed.push_back({r.definition.id, r.access.position, r.center.space, r.center.layer,
                             r.access.position.altitudeMeters - r.center.position.altitudeMeters});
        }
    }
    std::reverse(definitions.begin(), definitions.end());
    std::reverse(anchors.begin(), anchors.end());
    std::reverse(fixed.begin(), fixed.end());
    auto reordered = PlanetGenerator(WorldCatalog::Erma(), definitions, fixed, anchors).Generate();
    Check(reordered.Digest() == baseline.Digest(), "Input order cannot affect generation");
    Check(&tutorial::Overworld::PlanetPlan() == &baseline, "Cached plan has a stable, read-only lifetime");
    GenerationSettings settings;
    settings.extraRegions = 16;
    auto generator = PlanetGenerator::Erma(anchors, settings);
    for (std::uint64_t seed :
         {0ull, 1ull, 7ull, 42ull, 65537ull, 0x458a91ull, 0xffffffffffffffffull, 912345ull})
    {
        auto plan = generator.WithSeed(seed).Generate();
        Layout(plan, 16);
        Routes(plan);
        for (const auto &r : baseline.regions)
        {
            if (r.definition.storyFixed)
            {
                const auto &other = plan.Region(r.definition.id);
                Check(Distance(plan, r.access.position, other.access.position) < .001 &&
                          r.areaMeters2 == other.areaMeters2 &&
                          r.footprint.front().x == other.footprint.front().x,
                      "Fixed reservations do not drift with a planet seed");
            }
        }
        if (seed != WorldCatalog::Erma().seed)
        {
            Check(Distance(plan, baseline.Region("cinder").access.position,
                           plan.Region("cinder").access.position) > 100,
                  "A different seed actually changes procedural sites");
        }
        std::cout << "Validated seed " << seed << std::endl;
    }
    Throws(
        [&] {
            GenerationSettings bad;
            bad.clusters = 0;
            PlanetGenerator::Erma(anchors, bad);
        },
        "Reject unbounded/empty clusters");
    Throws(
        [&] {
            auto bad = definitions;
            bad.push_back(bad.front());
            PlanetGenerator(WorldCatalog::Erma(), bad, fixed, anchors);
        },
        "Reject duplicate definitions");
    Throws(
        [&] {
            auto bad = fixed;
            bad.pop_back();
            PlanetGenerator(WorldCatalog::Erma(), definitions, bad, anchors);
        },
        "Reject missing story reservations");
    Throws(
        [&] {
            auto bad = fixed;
            bad[0].access = bad[1].access;
            PlanetGenerator(WorldCatalog::Erma(), definitions, bad, anchors).Generate();
        },
        "Reject overlapping fixed sites");
    Throws(
        [&] {
            auto bad = anchors;
            bad.front().location.regionId = "cinder";
            PlanetGenerator(WorldCatalog::Erma(), definitions, fixed, bad);
        },
        "Reject anchors without fixed reservations");
}
void Queries(const GeneratedPlanet &plan)
{
    auto anchorPoint = plan.Region("halo_07").access.position;
    auto sampled = plan.terrain.Sample(anchorPoint);
    auto reused = plan.terrain.Environment(anchorPoint, sampled.elevationMeters, sampled.slope);
    Check(reused.biome == sampled.biome && reused.moisture == sampled.moisture &&
              reused.minerals == sampled.minerals,
          "Contact grids can reuse the exact same environment rules without extra height sampling");
    auto digest = plan.Digest();
    const auto &planet = plan.terrain.Planet();
    std::vector<SurfacePoint> points;
    for (unsigned i = 0; i < 1000; ++i)
    {
        points.push_back(G::FromDegrees(-89.9 + 179.8 * i / 999., -180 + 360. * ((i * 37) % 1000) / 999.));
    }
    std::vector<double> expected;
    for (auto point : points)
    {
        expected.push_back(plan.terrain.Height(point));
    }
    std::vector<std::future<bool>> workers;
    for (unsigned worker = 0; worker < 4; ++worker)
    {
        workers.push_back(std::async(std::launch::async, [&, worker] {
            for (unsigned i = worker; i < points.size(); i += 4)
            {
                if (plan.terrain.Height(points[999 - i]) != expected[999 - i])
                {
                    return false;
                }
            }
            return true;
        }));
    }
    for (auto &worker : workers)
    {
        Check(worker.get(), "Terrain queries are independent of thread/visit order");
    }
    for (const auto &r : plan.regions)
    {
        auto key = CubeSphere::Address(planet, r.center.position, 14, r.center.layer);
        auto result = plan.RegionsInChunk(key);
        Check(std::find(result.begin(), result.end(), std::size_t(&r - plan.regions.data())) != result.end(),
              "Chunk query includes containing footprints, across all faces");
        auto entrance = CubeSphere::Address(planet, r.access.position, 18, 0);
        result = plan.RegionsInChunk(entrance);
        Check(std::find(result.begin(), result.end(), std::size_t(&r - plan.regions.data())) != result.end(),
              "Surface chunks also expose subsurface entrances");
    }
    for (auto normal : std::vector<Vector3d>{{1, 1, 0}, {1, 0, 1}, {-1, 1, 1}, {0, 0, 1}, {-1, 1.e-10, 0}})
    {
        SurfacePoint seam{G::Unit(normal), 0};
        auto frame = G::Frame(seam);
        auto a = G::Advance(frame, -.01, 0, planet.radiusMeters).origin,
             b = G::Advance(frame, .01, 0, planet.radiusMeters).origin;
        Check(std::fabs(plan.terrain.Height(a) - plan.terrain.Height(b)) < .1,
              "Terrain has no cube/longitude/pole seam");
    }
    auto key = CubeSphere::Address(planet, points[0], 12, 0);
    ++key.planetSeed;
    Throws([&] { plan.RegionsInChunk(key); }, "Cross-world chunk queries are rejected");
    Throws([&] { plan.terrain.Height({{2, 0, 0}, 0}); }, "Invalid normals cannot enter terrain sampling");
    Check(plan.Digest() == digest, "Queries never mutate the manifest");
}
void DefensiveContracts(const GeneratedPlanet &plan)
{
    auto home = plan.Region("halo_07").access.position;
    TerrainReservation region{"region", home, 1000, 8000, 0, 0}, anchor{"anchor", home, 60, 200, 40, 1};
    PlanetTerrain protectedTerrain(plan.terrain.Planet(), {anchor, region});
    Check(protectedTerrain.Height(home) == 40,
          "Story anchor height takes precedence over regional flattening");
    PlanetTerrain reordered(plan.terrain.Planet(), {region, anchor});
    Check(reordered.Height(home) == protectedTerrain.Height(home), "Reservation input order is irrelevant");
    auto conflicting = anchor;
    conflicting.id = "other_anchor";
    conflicting.elevationMeters = 41;
    Throws([&] { PlanetTerrain bad(plan.terrain.Planet(), {anchor, conflicting}); },
           "Conflicting protected heights are rejected");
    Throws(
        [&] {
            auto bad = plan;
            bad.relayCoverageMeters = 0;
            bad.Validate();
        },
        "Malformed coverage budget rejected before sampling");
    Throws(
        [&] {
            auto bad = plan;
            bad.regions.front().center.regionId = "halo_07";
            bad.Validate();
        },
        "Reject geometry ownership mismatch");
    Throws(
        [&] {
            auto bad = plan;
            bad.connections.front().lengthMeters += 1;
            bad.Validate();
        },
        "Reject invalid route distance");
    Throws(
        [&] {
            auto bad = plan;
            bad.relays.front().location.position = G::FromDegrees(20, 20);
            bad.Validate();
        },
        "Reject detached relay points");
    GenerationSettings dense;
    dense.extraRegions = 128;
    auto expanded = PlanetGenerator::Erma(tutorial::Overworld::StoryAnchors(), dense).Generate();
    Layout(expanded, 128);
    Check(expanded.Connected(), "Expanded manifests retain connectivity");
    // Existing game state is not consumed to generate this future world plan.
    auto &game = tutorial::Game::Get();
    game.Reset();
    const auto &state = game.GetState();
    auto position = state.playerPosition;
    auto objective = state.mainQuestStage;
    auto buildings = state.buildings.size(), residents = state.residents.size();
    tutorial::Overworld::PlanetPlan();
    Check(state.playerPosition.x == position.x && state.playerPosition.y == position.y &&
              state.mainQuestStage == objective && state.buildings.size() == buildings &&
              state.residents.size() == residents,
          "Read-only world planning does not advance quests or relocate the player");
}
void ProfileScales()
{
    GenerationSettings settings;
    settings.extraRegions = 8;
    settings.clusters = 6;
    for (double radius : {1737400., 7000000.})
    {
        auto planet = WorldCatalog::Erma();
        planet.id = "survey_world";
        planet.name = "절차 행성 검사";
        planet.storyPlanet = false;
        planet.radiusMeters = radius;
        std::vector<RegionDefinition> definitions;
        for (const char *id : {"karden", "asterion", "calyx_outpost"})
        {
            auto definition = WorldCatalog::Region(id);
            definition.planetId = planet.id;
            definitions.push_back(definition);
        }
        auto generated = PlanetGenerator(planet, definitions, {}, {}, settings).Generate();
        Check(generated.regions.size() == 11 && generated.Connected(),
              "Generic radius profiles generate connected sites");
        Check(std::fabs(generated.Region("asterion").areaMeters2 - 151312500.) < .01,
              "City areas stay in square metres on different-radius planets");
        Check(std::fabs(G::Circumference(radius) - 2 * G::Pi * radius) < .001,
              "Profile radius controls planetary travel scale");
    }
}
void WritePreview(const GeneratedPlanet &plan, const char *path)
{
    std::ofstream file(path);
    Check(bool(file), "Open preview manifest");
    file << std::setprecision(17);
    auto point = [&](SurfacePoint p) {
        auto ll = G::ToDegrees(p);
        file << '[' << ll[0] << ',' << ll[1] << ',' << p.altitudeMeters << ']';
    };
    file << "{\"seed\":" << plan.terrain.Planet().seed << ",\"digest\":\"" << plan.Digest()
         << "\",\"regions\":[";
    bool comma = false;
    for (const auto &r : plan.regions)
    {
        if (comma)
        {
            file << ',';
        }
        comma = true;
        file << "{\"id\":\"" << r.definition.id << "\",\"name\":\"" << r.definition.name
             << "\",\"kind\":" << unsigned(r.definition.kind) << ",\"tier\":" << unsigned(r.definition.tier)
             << ",\"fixed\":" << r.definition.storyFixed << ",\"areaKm2\":" << r.areaMeters2 / 1.e6
             << ",\"center\":";
        point(r.access.position);
        file << ",\"outline\":[";
        bool separator = false;
        for (auto v : r.footprint)
        {
            if (separator)
            {
                file << ',';
            }
            separator = true;
            point(G::FromLocal(r.frame, v, plan.terrain.Planet().radiusMeters));
        }
        file << "]}";
    }
    file << "],\"routes\":[";
    comma = false;
    for (const auto &e : plan.connections)
    {
        if (comma)
        {
            file << ',';
        }
        comma = true;
        file << "{\"mode\":" << unsigned(e.mode) << ",\"points\":[";
        bool separator = false;
        for (auto p : e.points)
        {
            if (separator)
            {
                file << ',';
            }
            separator = true;
            point(p);
        }
        file << "]}";
    }
    file << "],\"relays\":[";
    comma = false;
    for (const auto &r : plan.relays)
    {
        if (comma)
        {
            file << ',';
        }
        comma = true;
        point(r.location.position);
    }
    file << "],\"terrain\":[";
    for (unsigned y = 0; y < 90; ++y)
    {
        if (y)
        {
            file << ',';
        }
        file << '[';
        for (unsigned x = 0; x < 180; ++x)
        {
            if (x)
            {
                file << ',';
            }
            file << int(plan.terrain.Height(G::FromDegrees(89. - y * 2, -179. + x * 2)));
        }
        file << ']';
    }
    file << "]}";
    Check(bool(file), "Write preview manifest");
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        auto start = std::chrono::steady_clock::now();
        const auto &plan = tutorial::Overworld::PlanetPlan();
        auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                .count();
        std::cout << "Generated " << plan.regions.size() << " regions, " << plan.relays.size() << " relays, "
                  << plan.connections.size() << " routes in " << milliseconds << "ms; digest "
                  << plan.Digest() << std::endl;
        if (argc > 1)
        {
            WritePreview(plan, argv[1]);
        }
        Layout(plan, 64);
        Routes(plan);
        Queries(plan);
        Reproducibility(plan);
        DefensiveContracts(plan);
        ProfileScales();
        std::cout << "World generation tests passed: " << checks << " checks\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "World generation regression after " << checks << " checks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
