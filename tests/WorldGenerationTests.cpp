#include "stdafx.h"
#include "Overworld.h"
#include "WorldGeneration.h"
#include "WorldContent.h"
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
void RouteVisibility(const GeneratedPlanet &plan)
{
    RouteSpatialIndex index(plan);
    const auto all = index.Query(G::FromDegrees(0, 0), G::Pi * plan.terrain.Planet().radiusMeters);
    Check(all.size() == plan.connections.size(), "Planet view retains every nonempty connection");
    for (size_t route = 0; route < plan.connections.size(); ++route)
    {
        const auto &edge = plan.connections[route];
        for (size_t i = 1; i < edge.points.size(); ++i)
        {
            const auto middle = Between(plan, edge.points[i - 1], edge.points[i], .5);
            const auto visible = index.Query(middle, 0);
            Check(std::binary_search(visible.begin(), visible.end(), route),
                  "Spherical route culling never removes an intersecting segment");
        }
    }
    Throws([&] { index.Query(G::FromDegrees(0, 0), -1); }, "Negative route query rejected");
    GeneratedPlanet wrap{PlanetTerrain(plan.terrain.Planet())};
    auto a = G::FromDegrees(0, 179.9), b = G::FromDegrees(0, -179.9);
    wrap.connections.push_back({0, 1, RouteMode::Road, {a, b}, Distance(wrap, a, b)});
    RouteSpatialIndex seam(wrap);
    Check(seam.Query(G::FromDegrees(0, 180), 1).size() == 1, "Dateline-spanning road remains visible");
    Check(seam.Query(G::FromDegrees(0, 0), 160).empty(), "Distant road can be rejected");
    wrap.connections.clear();
    a = G::FromDegrees(89, -45);
    b = G::FromDegrees(89, 45);
    wrap.connections.push_back({0, 1, RouteMode::Air, {a, b}, Distance(wrap, a, b)});
    RouteSpatialIndex pole(wrap);
    Check(pole.Query(Between(wrap, a, b, .5), 1).size() == 1, "Polar route remains visible");
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
void DistributedJunctions(const GeneratedPlanet &plan)
{
    Check(plan.layoutRevision == GeneratedPlanet::CurrentLayoutRevision && plan.Connected(),
          "New worlds use a connected distributed road layout");
    std::vector<unsigned> degrees(plan.nodes.size(), 0);
    for (const auto &edge : plan.connections)
    {
        ++degrees[edge.from];
        ++degrees[edge.to];
    }
    unsigned chainedRelays = 0;
    for (size_t i = 0; i < degrees.size(); ++i)
    {
        const auto &node = plan.nodes[i];
        const bool approach = !node.relay && plan.cityCores && plan.cityCores->Find(node.regionId);
        Check(degrees[i] <= (approach ? 3u : 4u), "No regional centre or relay becomes a many-arm hub");
    }
    for (const auto &edge : plan.connections)
    {
        if (plan.nodes[edge.from].relay && plan.nodes[edge.to].relay)
        {
            ++chainedRelays;
        }
    }
    Check(chainedRelays > 0,
          "Local relays share nearby access rather than all connecting to the city centre");
    unsigned sharedPads = 0;
    for (const auto &site : plan.nodes)
    {
        if (site.relay)
        {
            continue;
        }
        auto onPad = [&](unsigned node) {
            return plan.nodes[node].regionId == site.regionId &&
                   Distance(plan, plan.nodes[node].location.position, site.location.position) < .5 &&
                   std::abs(plan.nodes[node].location.position.altitudeMeters -
                            site.location.position.altitudeMeters) < .5;
        };
        unsigned members = 0, arms = 0;
        for (unsigned i = 0; i < plan.nodes.size(); ++i)
        {
            if (onPad(i))
            {
                ++members;
            }
        }
        if (members < 2)
        {
            continue;
        }
        ++sharedPads;
        for (const auto &edge : plan.connections)
        {
            if (onPad(edge.from) != onPad(edge.to))
            {
                ++arms;
            }
        }
        Check(arms <= (plan.cityCores && plan.cityCores->Find(site.regionId) ? 3u : 4u),
              "A co-located city entrance and relay share one physical intersection budget");
    }
    Check(sharedPads > 0, "Regression includes coincident region/relay nodes");
    auto generator = PlanetGenerator::Erma(tutorial::Overworld::StoryAnchors()).WithLayoutRevision(3);
    auto legacy = generator.Generate();
    Check(legacy.layoutRevision == 3 && legacy.Connected(),
          "Previous saves retain their connected revision-3 roads");
    Check(legacy.Digest() == generator.WithSeed(legacy.terrain.Planet().seed).Generate().Digest(),
          "Seed copies retain the requested legacy layout and digest");
    Check(legacy.Digest() != plan.Digest(), "Road layout revisions cannot share a traffic manifest identity");
    std::vector<unsigned> oldDegrees(legacy.nodes.size(), 0);
    for (const auto &edge : legacy.connections)
    {
        ++oldDegrees[edge.from];
        ++oldDegrees[edge.to];
    }
    Check(*std::max_element(oldDegrees.begin(), oldDegrees.end()) > 4,
          "Legacy compatibility preserves the old star topology rather than silently moving saved actors");
    auto rev4 = PlanetGenerator::Erma(tutorial::Overworld::StoryAnchors()).WithLayoutRevision(4).Generate();
    Check(rev4.layoutRevision == 4 && rev4.Connected() && rev4.Digest() != plan.Digest(),
          "Previous revision-4 saves remain supported alongside revision-5 worlds");
    Throws([&] { generator.WithLayoutRevision(999); }, "Unknown layouts are rejected explicitly");
    SurfaceStructure building;
    building.form = ContentForm::Building;
    building.foundationMeters = .15;
    Check(building.BaseClearanceMeters() == .08, "Building floors sit above terrain and their plinth top");
    Check(building.FoundationTopMeters() > building.BaseClearanceMeters() &&
              building.FoundationTopMeters() == .10,
          "Plinth embeds the building floor without sharing its plane or leaving an air gap");
    building.form = ContentForm::TransitMesh;
    Check(building.BaseClearanceMeters() == 0, "Road connection heights are not lifted with buildings");
    building.form = ContentForm::Building;
    building.solid = false;
    Check(building.BaseClearanceMeters() == 0,
          "Non-solid decorative planes retain their authored elevations");
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
        DistributedJunctions(plan);
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
void RelayContracts(const GeneratedPlanet &plan)
{
    RelayNetwork progress;
    const auto &relay = plan.relays.front();
    auto location = relay.location;
    auto wrong = location;
    wrong.planetSeed++;
    progress.Observe(plan, wrong);
    Check(progress.Discoveries().empty() && !progress.Register(plan, relay.id, wrong),
          "Discoveries reject another world identity");
    auto underground = location;
    underground.space = SpaceKind::Subsurface;
    underground.layer = -1;
    progress.Observe(plan, underground);
    Check(progress.Discoveries().empty(), "A subsurface map cannot register a surface relay");
    progress.Observe(plan, location);
    Check(progress.Discovered(relay.id) && !progress.Registered(relay.id),
          "Physical discovery and explicit registration are separate");
    Check(progress.Register(plan, relay.id, location) && !progress.Register(plan, relay.id, location),
          "Relay registration is idempotent");
    for (const auto &candidate : plan.relays)
    {
        Check(candidate.location.space == SpaceKind::Surface && candidate.location.layer == 0 &&
                  plan.terrain.Sample(candidate.location.position).land,
              "Every relay has a real surface landing, including mine entrances");
        if (Distance(plan, location.position, candidate.location.position) > RelayNetwork::UseMeters)
        {
            Check(!progress.Register(plan, candidate.id, location),
                  "Registration cannot be performed remotely");
        }
    }
}
void RelayWorldScope(const GeneratedPlanet &plan)
{
    RelayNetwork network;
    const auto &relay = plan.relays.front();
    Check(network.Register(plan, relay.id, relay.location), "Register a physical relay in its world");
    auto another = plan.terrain.Planet();
    another.seed++;
    network.Bind(another);
    Check(network.Registrations().empty() && network.Discoveries().empty(),
          "A new world cannot reuse another seed's registrations");
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
void SelectableProfiles()
{
    const auto &profiles = PlanetProfiles::All();
    Check(profiles.size() == 3 && PlanetProfiles::Find("unknown") == nullptr,
          "Only catalogued planet profiles can be selected");
    std::set<std::string> identities;
    std::set<double> circumferences;
    for (const auto &profile : profiles)
    {
        profile.Validate();
        Check(PlanetProfiles::Supported(profile), "Every selectable profile has a stable identity");
        RelayNetwork network;
        network.Bind(profile);
        Check(identities.insert(network.WorldKey()).second, "Planet ledgers cannot share a key");
        circumferences.insert(G::Circumference(profile.radiusMeters));
        auto incompatible = profile;
        ++incompatible.seed;
        Check(!PlanetProfiles::Supported(incompatible), "A changed seed is not the same saved profile");
        Throws([&] { PlanetProfiles::Generator(incompatible); },
               "Unknown profile identity is rejected before generation");
        if (profile.storyPlanet)
        {
            continue;
        }
        auto plan = PlanetProfiles::Generator(profile).Generate();
        plan.Validate();
        Check(
            plan.regions.size() == 37 && plan.Connected() && plan.anchors.empty() && !plan.cityCores,
            "Generic profiles generate independent connected regions without Erma actors or authored cores");
        Check(plan.Region("landing_zone").definition.storyFixed && plan.terrain.Homeland().has_value(),
              "A deterministic landing reservation anchors playable land");
        Check(plan.terrain.Sample(plan.Region("landing_zone").access.position).land,
              "Landing reservation is above sea level");
        std::set<SettlementTier> tiers;
        for (const auto &r : plan.regions)
        {
            Check(r.definition.planetId == profile.id && r.definition.id != "halo_07" &&
                      r.definition.id != "frontier" && r.definition.id != "mq00_mine",
                  "Erma-only places never leak into a generic planet");
            if (r.definition.kind == RegionKind::Settlement)
            {
                tiers.insert(r.definition.tier);
            }
            Check(std::any_of(plan.relays.begin(), plan.relays.end(),
                              [&](const auto &relay) { return relay.regionId == r.definition.id; }),
                  "Every generated region has its own relay coverage");
        }
        Check(tiers.size() == 4, "All four settlement tiers remain available on each generic planet");
        if (profile.id == "seira" && profile.generatorVersion == 2)
        {
            Check(plan.regions.size() == 37,
                  "Seira retains all named and procedural regions after placement fallback");
            const auto &capital = plan.Region("system_hub");
            double maximumSlope = 0;
            auto inspect = [&](Vector3d local) {
                const auto point = G::FromLocal(capital.frame, local, profile.radiusMeters);
                const auto sample = plan.terrain.Sample(point);
                Check(sample.land && sample.elevationMeters >= PlanetTerrain::SeaLevelMeters + 12,
                      "Terrain fallback does not allow ocean-side city footprint samples");
                maximumSlope = (std::max)(maximumSlope, sample.slope);
            };
            inspect({0, 0, 0});
            for (size_t i = 0; i < capital.footprint.size(); ++i)
            {
                const auto a = capital.footprint[i],
                           b = capital.footprint[(i + 1) % capital.footprint.size()];
                inspect(a);
                inspect({a.x * .5, a.y * .5, 0});
                inspect({(a.x + b.x) * .5, (a.y + b.y) * .5, 0});
            }
            Check(
                maximumSlope <= .06001,
                "Seira keeps capital footprint slopes bounded with radius-normalized terrain");
            Check(std::abs(capital.areaMeters2 - 1200 * .25e6) < .01,
                  "Capital quarter-area contract is preserved");
        }
        Check(PlanetProfiles::Generator(profile).Generate().Digest() == plan.Digest(),
              "Profile revisits reproduce the same layout");
    }
    Check(circumferences.size() == profiles.size(),
          "Different radii produce distinct round-the-planet travel lengths");
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
void TestCq04bUrbanAndSignals(const GeneratedPlanet &plan)
{
    // 1) Multi-portal perimeter nodes & settlement terracing pads exist in Layout Revision 5.
    unsigned portalNodes = 0;
    for (const auto &node : plan.nodes)
    {
        if (node.id.rfind("portal_", 0) == 0)
        {
            ++portalNodes;
        }
    }
    Check(portalNodes >= 8, "CQ-04B creates directional perimeter portals on larger settlements");
    Check(!plan.terrain.Terraces().empty(), "CQ-04B generates settlement terracing pads");

    // 2) Inspect SystemCapital (lucerne_prime): intersections, grade separation, roundabouts, unique landmarks,
    //    perimeter block OBB non-overlap, and 1:1 measured building ratios.
    std::size_t capIdx = plan.regions.size();
    for (std::size_t i = 0; i < plan.regions.size(); ++i)
    {
        if (plan.regions[i].definition.id == "lucerne_prime")
        {
            capIdx = i;
            break;
        }
    }
    Check(capIdx < plan.regions.size(), "SystemCapital lucerne_prime present");
    const auto &cap = plan.regions[capIdx];

    unsigned unsignalized = 0, roundabouts = 0, signalized = 0, gradeSeparated = 0;
    unsigned ringRoads = 0, elevated = 0, underpass = 0, tapers = 0, plazas = 0;
    unsigned capAdminCount = 0, capForumCount = 0;
    unsigned totalSolidBuildings = 0;
    auto inspectCell = [&](std::int64_t cx, std::int64_t cy) {
        const auto cell = ContentLayout::Generate(plan, {capIdx, cx, cy});
        for (const auto &inter : cell.intersections)
        {
            if (inter.control == IntersectionControl::Unsignalized)
            {
                ++unsignalized;
            }
            else if (inter.control == IntersectionControl::Roundabout)
            {
                ++roundabouts;
                Check(inter.islandRadiusMeters >= 7. && inter.radiusMeters > inter.islandRadiusMeters,
                      "Roundabout has valid central island and outer radius");
            }
            else if (inter.control == IntersectionControl::Signalized)
            {
                ++signalized;
                Check(inter.cycleSeconds > 0. && inter.cycleSeconds <= 70.,
                      "Signalized intersection cycle stays within 70s budget");
            }
            else if (inter.control == IntersectionControl::GradeSeparated)
            {
                ++gradeSeparated;
            }
        }
        for (const auto &st : cell.streets)
        {
            if (st.kind == StreetKind::RingRoad) ++ringRoads;
            if (st.kind == StreetKind::Elevated || st.kind == StreetKind::ElevatedRamp) ++elevated;
            if (st.kind == StreetKind::Underpass || st.kind == StreetKind::UnderpassRamp) ++underpass;
            if (st.kind == StreetKind::Taper) ++tapers;
            if (st.kind == StreetKind::PedestrianPlaza) ++plazas;
        }
        // Verify zero OBB overlap within the cell and 1:1 authored ratios on measured buildings.
        std::vector<const SurfaceStructure *> solids;
        for (const auto &s : cell.structures)
        {
            if (s.form == ContentForm::MeasuredBuilding)
            {
                if (s.variant == 8) ++capAdminCount;
                if (s.variant == 9) ++capForumCount;
                // Verify no forced minification: smallest authored measured footprint is 8m wide.
                Check(s.sizeMeters.x >= 7.9 && s.sizeMeters.y >= 7.9,
                      "Measured building preserves 1:1 authored dimensions without forced minification");
            }
            if (s.solid)
            {
                ++totalSolidBuildings;
                solids.push_back(&s);
            }
        }
        for (std::size_t i = 0; i < solids.size(); ++i)
        {
            const auto la = G::ToLocal(cap.frame, solids[i]->frame.origin, plan.terrain.Planet().radiusMeters);
            for (std::size_t j = i + 1; j < solids.size(); ++j)
            {
                const auto lb =
                    G::ToLocal(cap.frame, solids[j]->frame.origin, plan.terrain.Planet().radiusMeters);
                const double dist = std::hypot(la.x - lb.x, la.y - lb.y);
                const double minHalf = (std::min)(solids[i]->sizeMeters.x, solids[i]->sizeMeters.y) * .5 +
                                       (std::min)(solids[j]->sizeMeters.x, solids[j]->sizeMeters.y) * .5;
                Check(dist >= minHalf - .1, "Perimeter block buildings do not overlap");
            }
        }
    };
    for (std::int64_t cy = -6; cy <= 6; ++cy)
    {
        for (std::int64_t cx = -6; cx <= 6; ++cx)
        {
            if ((std::abs(cx) <= 3 && std::abs(cy) <= 3) || cx == 0 || cy == 0 || cx == 2)
            {
                inspectCell(cx, cy);
            }
        }
    }
    // Also inspect along the positive X arterial from inner core out to the outer ring road perimeter.
    const std::int64_t maxRingStep = (std::max)(std::int64_t(12), std::int64_t(cap.boundingRadiusMeters * .74 / 92.));
    for (std::int64_t cx = 7; cx <= maxRingStep; ++cx)
    {
        inspectCell(cx, 0);
    }
    Check(unsignalized > 0 && signalized > 0 && gradeSeparated >= 2,
          "Capital contains unsignalized 2x2, signalized 4/6-lane, and grade-separated intersections");
    Check(elevated >= 3 && underpass >= 3,
          "Capital contains elevated overpass (+8.4m) and open-cut underpass (-5.4m) corridors");
    Check(ringRoads > 0 || tapers > 0 || roundabouts > 0,
          "Capital outer perimeter emits ring roads, tapers, or roundabouts");
    Check(capAdminCount <= 1 && capForumCount <= 1,
          "Unique capital landmarks are never spammed across multiple blocks");
    Check(totalSolidBuildings >= 20, "Perimeter block parceling populates urban blocks");

    // 3) Procedural IntersectionSignalController contracts: concurrent walk, protected left non-conflict,
    //    4s flashing walk, Green Wave offset, unsignalized no-light, and emergency preemption.
    SurfaceIntersection sigInter;
    sigInter.control = IntersectionControl::Signalized;
    sigInter.maxLanes = 6;
    sigInter.mainAxisYaw = 0.;
    sigInter.greenWaveOffsetSeconds = 5.;
    bool sawMainConcurrentWalk = false, sawFlashing = false, sawProtectedLeftNoWalk = false;
    for (int step = 0; step < 140; ++step)
    {
        const auto st = tutorial::IntersectionSignalController::Evaluate(sigInter, step * .5);
        Check(st.cycleSeconds <= 70., "Procedural 6-lane signal cycle <= 70s");
        if (st.mainThrough == tutorial::TrafficLight::Green && st.mainWalkGreen)
        {
            sawMainConcurrentWalk = true;
            if (st.mainWalkFlashing)
            {
                sawFlashing = true;
            }
        }
        if (st.mainLeft == tutorial::TrafficLight::Green || st.sideLeft == tutorial::TrafficLight::Green)
        {
            Check(!st.mainWalkGreen && !st.sideWalkGreen,
                  "Protected left turn never conflicts with a green pedestrian crosswalk");
            sawProtectedLeftNoWalk = true;
        }
    }
    Check(sawMainConcurrentWalk && sawFlashing && sawProtectedLeftNoWalk,
          "Signal controller produces concurrent walk, 4s flashing walk, and conflict-free protected left");
    const auto preempt = tutorial::IntersectionSignalController::Evaluate(sigInter, 0., true, true, true, 1);
    Check(preempt.emergencyPreempted && preempt.sideThrough == tutorial::TrafficLight::Green &&
              preempt.mainThrough == tutorial::TrafficLight::Red,
          "Emergency preemption clears the requested responder axis");
    SurfaceIntersection local2x2;
    local2x2.control = IntersectionControl::Unsignalized;
    Check(tutorial::IntersectionSignalController::Evaluate(local2x2, 12.).cycleSeconds == 0.,
          "Unsignalized 2x2 local intersection runs without traffic lights");
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        auto start = std::chrono::steady_clock::now();
        const auto &plan = tutorial::Overworld::PlanetPlan();
        const auto &adapter = LegacyWorldAdapter::ErmaExterior();
        Check(Distance(plan, plan.Region("frontier").access.position,
                       adapter.Locate("frontier", {1500, 1000, 0}).position) < .001,
              "Frontier travel distance is one quarter while its field geometry keeps physical units");
        Check(Distance(plan, plan.Region("n0_outskirts").access.position,
                       adapter.Locate("n0_outskirts", {3625, -1750, 0}).position) < .001,
              "N-0 travel anchor follows compact profile");
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
        RouteVisibility(plan);
        RelayContracts(plan);
        RelayWorldScope(plan);
        Queries(plan);
        Reproducibility(plan);
        DefensiveContracts(plan);
        ProfileScales();
        SelectableProfiles();
        TestCq04bUrbanAndSignals(plan);
        std::cout << "World generation tests passed: " << checks << " checks\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "World generation regression after " << checks << " checks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
