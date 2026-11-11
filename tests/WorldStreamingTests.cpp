#include "stdafx.h"
#include "WorldStreaming.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

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
    bool failed = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    Check(failed, message);
}
template <class F, class P> void Pump(F update, P predicate)
{
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate() && std::chrono::steady_clock::now() < until)
    {
        update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(predicate(), "Timed out waiting for a bounded streaming job");
}
std::shared_ptr<const GeneratedPlanet> FlatPlanet(double radius = 1000, std::uint64_t seed = 17,
                                                  unsigned version = 1)
{
    PlanetDefinition p{"stream_fixture", "지형 검사", "test_system", seed, version, radius, false};
    auto plan = std::make_shared<GeneratedPlanet>(
        PlanetTerrain(p, {{"flat", G::FromDegrees(0, 0), G::Pi * radius + 1, 1, 0}}));
    plan->Validate();
    return plan;
}
double Norm(Vector3d v)
{
    return std::hypot(std::hypot(v.x, v.y), v.z);
}
double Difference(Vector3d a, Vector3d b)
{
    return Norm({a.x - b.x, a.y - b.y, a.z - b.z});
}
std::shared_ptr<GeneratedPlanet> ContentFixture()
{
    auto plan = std::make_shared<GeneratedPlanet>(*FlatPlanet(4800000));
    auto base = G::Frame(G::FromDegrees(0, 0));
    // Geometry-only fixture: real catalog area/footprint validation stays in WorldGenerationTests.
    for (unsigned tier = 1; tier <= 4; ++tier)
    {
        GeneratedRegion region;
        region.definition = {"content_city_" + std::to_string(tier), "도시 검사", plan->terrain.Planet().id,
                             RegionKind::Settlement, SettlementTier(tier)};
        region.center = {plan->terrain.Planet().id, region.definition.id, plan->terrain.Planet().seed, 1};
        region.center.position = G::Advance(base, tier * 4000, 0, plan->terrain.Planet().radiusMeters).origin;
        region.access = region.center;
        region.frame = G::Frame(region.center.position);
        region.footprint = {{-600, -600, 0}, {600, -600, 0}, {600, 600, 0}, {-600, 600, 0}};
        region.boundingRadiusMeters = 850;
        plan->regions.push_back(std::move(region));
    }
    for (auto kind : {RegionKind::Outpost, RegionKind::Mine, RegionKind::Outlaw, RegionKind::AncientRuins})
    {
        auto region = plan->regions.front();
        region.definition.id = "content_zone_" + std::to_string(unsigned(kind));
        region.definition.kind = kind;
        region.definition.tier = SettlementTier::None;
        region.center.regionId = region.definition.id;
        region.center.position =
            G::Advance(base, double(plan->regions.size() + 1) * 4000, 0, plan->terrain.Planet().radiusMeters)
                .origin;
        region.access = region.center;
        region.frame = G::Frame(region.center.position);
        if (kind == RegionKind::Mine)
        {
            region.center.space = SpaceKind::Subsurface;
            region.center.layer = -1;
            region.center.position.altitudeMeters = -30;
        }
        plan->regions.push_back(std::move(region));
    }
    return plan;
}
void ContentContracts()
{
    auto plan = ContentFixture();
    const auto &p = plan->terrain.Planet();
    auto junctionCell = ContentLayout::Generate(*plan, {0, 1, 1});
    auto east = ContentLayout::Generate(*plan, {0, 2, 1});
    auto north = ContentLayout::Generate(*plan, {0, 1, 2});
    Check(junctionCell.streets.size() == 2 && east.streets.size() == 2 && north.streets.size() == 2,
          "Interior blocks publish shared street nodes");
    Check(G::Distance(p.radiusMeters, junctionCell.streets[0].points.back(), east.streets[0].points.front()) <
                  1.e-6 &&
              G::Distance(p.radiusMeters, junctionCell.streets[1].points.back(),
                          north.streets[1].points.front()) < 1.e-6,
          "Neighbouring streets join the same physical junction without gaps");
    double districtMaximum = 0, capitalMaximum = 0;
    std::vector<SurfaceStructure> structures;
    for (std::size_t region = 0; region < plan->regions.size(); ++region)
    {
        std::size_t count = 0;
        for (std::int64_t y = -4; y <= 4; ++y)
        {
            for (std::int64_t x = -4; x <= 4; ++x)
            {
                ContentCellKey key{region, x, y};
                auto cell = ContentLayout::Generate(*plan, key);
                auto again = ContentLayout::Generate(*plan, key);
                Check(cell.structures.size() == again.structures.size() &&
                          cell.streets.size() == again.streets.size(),
                      "Layout is independent of cache/visit order");
                for (std::size_t i = 0; i < cell.structures.size(); ++i)
                {
                    const auto &s = cell.structures[i], &a = again.structures[i];
                    Check(s.id == a.id && s.yaw == a.yaw &&
                              Difference(s.frame.origin.normal, a.frame.origin.normal) < 1.e-12 &&
                              Difference(s.sizeMeters, a.sizeMeters) < 1.e-12,
                          "Stable identity, geometry and orientation");
                    Check(s.Contains(s.frame.origin, p.radiusMeters),
                          "The rendered rectangle's centre is solid");
                    auto outside = G::Advance(s.frame, std::cos(s.yaw) * (s.sizeMeters.x * .5 + 1),
                                              std::sin(s.yaw) * (s.sizeMeters.x * .5 + 1), p.radiusMeters)
                                       .origin;
                    Check(!s.Contains(outside, p.radiusMeters, .2),
                          "Collision respects mesh-sized rotated bounds");
                    for (const auto &road : cell.streets)
                    {
                        for (auto point : road.points)
                        {
                            Check(!s.Contains(point, p.radiusMeters,
                                              road.widthMeters * .5 + road.sidewalkMeters),
                                  "Buildings keep roads and sidewalks clear");
                        }
                    }
                    if (region == 0)
                    {
                        districtMaximum = (std::max)(districtMaximum, s.sizeMeters.z);
                    }
                    if (region == 3)
                    {
                        capitalMaximum = (std::max)(capitalMaximum, s.sizeMeters.z);
                    }
                    ++count;
                    structures.push_back(s);
                }
                for (const auto &road : cell.streets)
                {
                    Check(road.lanes == 2 || road.lanes == 4 || road.lanes == 6,
                          "City road hierarchy remains two/four/six lanes");
                    for (size_t i = 1; i < road.points.size(); ++i)
                    {
                        auto a = G::ToLocal(plan->regions[region].frame, road.points[i - 1], p.radiusMeters);
                        auto b = G::ToLocal(plan->regions[region].frame, road.points[i], p.radiusMeters);
                        double dx = std::fabs(b.x - a.x), dy = std::fabs(b.y - a.y);
                        Check((std::min)(dx, dy) < std::hypot(dx, dy) * .3,
                              "Generated settlement street axes match Halo's quarter-view basis while "
                              "retaining gentle warps");
                    }
                }
            }
        }
        Check(count > 0, "All city tiers and non-city zone types place usable content");
    }
    Check(capitalMaximum > districtMaximum * 2, "Settlement tiers have distinct building scales");
    Check(!structures.empty(), "Content fixture has physical structures");
    const auto target = structures.front();
    auto protectedPlan = *plan;
    WorldLocation anchorLocation{p.id, target.regionId, p.seed, p.generatorVersion};
    anchorLocation.position = target.frame.origin;
    protectedPlan.anchors.push_back({"authored_core", anchorLocation});
    bool protectedSlot = true;
    for (auto address : ContentLayout::Near(protectedPlan, target.frame.origin, 0))
    {
        for (const auto &item : ContentLayout::Generate(protectedPlan, address).structures)
        {
            protectedSlot &= item.id != target.id;
        }
    }
    Check(protectedSlot, "Fixed story anchors reserve their physical footprint before generated lots");
    std::atomic_bool cancel{false};
    auto key = CubeSphere::Address(p, target.frame.origin, StreamSettings{}.Level(p));
    std::set<std::string> ids;
    bool publishedTarget = false;
    std::set<ChunkKey> keys{key};
    for (auto edge : {ChunkEdge::Left, ChunkEdge::Right, ChunkEdge::Down, ChunkEdge::Up})
    {
        keys.insert(CubeSphere::Neighbor(key, edge));
    }
    for (auto address : keys)
    {
        auto patch = ContentLayout::Publish(*plan, address, 180, cancel);
        for (const auto &s : patch.structures)
        {
            Check(ids.insert(s.id).second, "Cross-chunk objects have exactly one owner");
            Check(CubeSphere::Address(p, s.frame.origin, address.level) == address,
                  "Content owner is canonical");
            publishedTarget |= s.id == target.id;
        }
        for (const auto &road : patch.streets)
        {
            for (std::size_t i = 1; i < road.points.size(); ++i)
            {
                auto a = road.points[i - 1], b = road.points[i];
                SurfacePoint middle{
                    G::Unit({a.normal.x + b.normal.x, a.normal.y + b.normal.y, a.normal.z + b.normal.z}), 0};
                Check(CubeSphere::Address(p, middle, address.level) == address,
                      "Road fragments agree across chunk boundaries");
            }
        }
    }
    Check(publishedTarget, "A detailed chunk publishes its physical structures");
    ContentCollision collision;
    Check(collision.Blocked(*plan, target.frame.origin, .32),
          "Physical content works before any render publication");
    for (const auto &s : structures)
    {
        Check(collision.Blocked(*plan, s.frame.origin, .32),
              "Collision survives neighbourhood eviction and return");
        Check(collision.CachedCells() <= ContentCollision::MaximumCells,
              "Collision cache has a hard cell bound");
    }
    Check(collision.Blocked(*plan, target.frame.origin, .32), "Evicted content regenerates identically");
    for (auto point : {G::FromDegrees(0, 45), G::FromDegrees(90, 0), G::FromDegrees(-90, 0)})
    {
        Check(!collision.Blocked(*plan, point, .32), "Far-side/polar queries never create phantom content");
    }
    cancel.store(true);
    Check(ContentLayout::Publish(*plan, key, 180, cancel).structures.empty(),
          "Cancelled content publishes no partial objects");
    auto bad = key;
    ++bad.planetSeed;
    Throws([&] { ContentLayout::Publish(*plan, bad, 180, cancel); },
           "Cross-world content publication is rejected");
    Throws([&] { ContentLayout::Near(*plan, target.frame.origin, -1); },
           "Invalid local content queries are rejected");
    auto changed = *plan;
    changed.terrain = PlanetTerrain(
        PlanetDefinition{p.id, p.name, p.systemId, p.seed + 1, p.generatorVersion, p.radiusMeters, false},
        {{"flat", G::FromDegrees(0, 0), G::Pi * p.radiusMeters + 1, 1, 0}});
    bool different = false;
    for (std::int64_t x = -2; x <= 2; ++x)
    {
        auto a = ContentLayout::Generate(*plan, {0, x, 0}), b = ContentLayout::Generate(changed, {0, x, 0});
        different |= a.structures.size() != b.structures.size();
        if (!a.structures.empty() && !b.structures.empty())
        {
            different |= Difference(a.structures.front().frame.origin.normal,
                                    b.structures.front().frame.origin.normal) > 1.e-7 ||
                         Difference(a.structures.front().sizeMeters, b.structures.front().sizeMeters) > 1.e-6;
        }
    }
    Check(different, "Another seed does not reuse the same city layout/identities");
    collision.Blocked(changed, changed.regions.front().center.position, .32);
    Check(collision.CachedCells() < ContentCollision::MaximumCells,
          "Changing worlds drops old collision cells");

    auto start = G::Advance(target.frame, -std::cos(target.yaw) * (target.sizeMeters.x * .5 + 5),
                            -std::sin(target.yaw) * (target.sizeMeters.x * .5 + 5), p.radiusMeters)
                     .origin;
    SurfaceSession session(p, start);
    session.Start(plan);
    Pump([&] { session.Update(); }, [&] { return session.Ready(); });
    auto direction =
        G::TransformDirection({std::cos(target.yaw), std::sin(target.yaw), 0}, target.frame, session.Pose());
    session.Move(direction.x * (target.sizeMeters.x + 10), direction.y * (target.sizeMeters.x + 10));
    Check(!target.Contains(session.Pose().origin, p.radiusMeters),
          "Fast movement cannot tunnel through a generated building");
    Check(G::Distance(p.radiusMeters, start, session.Pose().origin) < target.sizeMeters.x + 9,
          "Building sweep stops at contact");
    auto epoch = session.Stream().Snapshot()->statistics.epoch;
    Check(session.Relocate(target.frame.origin), "Survey relocation finds nearby unoccupied ground");
    Check(!collision.Blocked(*plan, session.Pose().origin, .65), "Survey landing is safe for its test bike");
    Check(session.Stream().Snapshot()->statistics.epoch == epoch,
          "Survey relocation retains the same world and worker");
    session.Stop();
}
void AuthoredCoreContracts()
{
    auto plan = ContentFixture();
    const auto &p = plan->terrain.Planet();
    auto cores = std::make_shared<CityCorePlan>();
    CityCore core;
    core.regionId = plan->regions.front().definition.id;
    core.radiusMeters = 120;
    CoreLot house;
    house.id = "home";
    house.positionMeters = {-30, 40, 0};
    house.sizeMeters = {12, 16, 8};
    house.variant = 3;
    core.lots.push_back(house);
    core.roads = {{1, 0, {-80, 0, 0}, {0, 0, 0}, 2},
                  {0, 2, {0, 0, 0}, {80, 0, 0}, 2},
                  {0, 3, {0, 0, 0}, {0, 80, 0}, 2}};
    cores->cities.push_back(core);
    auto digest = cores->Digest();
    auto footprint = plan->regions.front().footprint;
    cores->Prepare(*plan);
    plan->cityCores = cores;
    Check(cores->Digest() == digest, "Preparing a city never changes authored identities/geometry");
    Check(plan->regions.front().footprint.size() == footprint.size(),
          "Authored cores keep the generated city footprint");
    Check(cores->cities.front().approaches.size() == 3,
          "Every authored street terminal has a physical local-street approach");
    std::set<std::pair<std::int64_t, std::int64_t>> joins;
    for (const auto &approach : cores->cities.front().approaches)
    {
        Check(joins.insert({approach.cellX, approach.cellY}).second,
              "Approaches never stack on the same T-junction");
        auto point = G::FromLocal(plan->regions.front().frame, approach.join, p.radiusMeters);
        unsigned incident = 0;
        for (auto address : ContentLayout::Near(*plan, point, 20))
        {
            for (const auto &road : ContentLayout::Generate(*plan, address).streets)
            {
                for (std::size_t i = 1; i < road.points.size(); ++i)
                {
                    bool first = G::Distance(p.radiusMeters, road.points[i - 1], point) < 1.e-5;
                    bool second = G::Distance(p.radiusMeters, road.points[i], point) < 1.e-5;
                    incident += first || second ? 1 : 0;
                }
            }
        }
        Check(incident == 3,
              "Approaches meet an exact shared road vertex as a T, not a disconnected/five-way crossing");
    }
    auto point = G::FromLocal(plan->regions.front().frame, house.positionMeters, p.radiusMeters);
    ContentCollision collision;
    Check(collision.Blocked(*plan, point, .32),
          "Authored buildings participate in the same chunk-independent collision");
    auto plaza = G::FromLocal(plan->regions.front().frame, {20, 20, 0}, p.radiusMeters);
    Check(!collision.Blocked(*plan, plaza, .32), "Generated lots do not overwrite an authored plaza");
    std::size_t authored = 0;
    for (auto address : ContentLayout::Near(*plan, point, 180))
    {
        for (const auto &s : ContentLayout::Generate(*plan, address).structures)
        {
            if (s.id == WorldIdentity::ContentKey(p, core.regionId, "core1.home"))
            {
                ++authored;
                Check(s.form == ContentForm::Building && s.variant == house.variant &&
                          Difference(s.sizeMeters, house.sizeMeters) < 1.e-12,
                      "Authored model/physical size survives worker preparation");
                Check(G::Distance(p.radiusMeters, s.frame.origin, point) < 1.e-5,
                      "Authored points keep their physical planet location");
            }
        }
    }
    Check(authored == 1, "Authored lots have one local-cell owner");
    auto duplicate = *cores;
    duplicate.cities.front().lots.push_back(house);
    Throws([&] { duplicate.Validate(); }, "Duplicate authored object identities are rejected");
    auto changed = *cores;
    changed.cities.front().lots.front().sizeMeters.x += 1;
    Check(changed.Digest() != digest, "Core content changes affect the world manifest digest");
    std::atomic_bool stop{true};
    Throws([&] { changed.Prepare(*plan, &stop); },
           "Authored preparation cooperatively cancels before publishing");
    auto templateCore = core;
    templateCore.regionId = "core_test";
    CityCorePlan input;
    input.cities.push_back(templateCore);
    GenerationSettings settings;
    settings.extraRegions = 0;
    settings.clusters = 2;
    RegionDefinition definition{
        "core_test", "핵심 구역", p.id, RegionKind::Settlement, SettlementTier::District, true, 4, "fixture"};
    auto generator = PlanetGenerator(p, {definition}, {{"core_test", G::FromDegrees(0, 0)}}, {}, settings)
                         .WithCityCores(input);
    input.cities.front().lots.front().sizeMeters.x = 77;
    auto generated = generator.Generate();
    Check(generated.cityCores &&
              generated.cityCores->cities.front().lots.front().sizeMeters.x == house.sizeMeters.x,
          "Generator owns an immutable copy of authored input");
    auto anotherSeed = generator.WithSeed(p.seed + 1).Generate();
    Check(anotherSeed.cityCores && anotherSeed.cityCores->cities.front().lots.size() == 1,
          "Changing the planet seed retains its authored core contract");
}
void MeshSeamsAndContacts()
{
    auto plan = FlatPlanet(4800000);
    const auto &p = plan->terrain.Planet();
    std::atomic_bool cancel{false};
    for (auto seam : std::vector<SurfacePoint>{G::FromDegrees(0, 45),
                                               G::FromDegrees(45, 0),
                                               G::FromDegrees(-45, 90),
                                               {G::Unit({1, 1, 1}), 0},
                                               G::FromDegrees(90, 0),
                                               G::FromDegrees(0, 180)})
    {
        StreamSettings settings;
        auto key = CubeSphere::Address(p, seam, settings.Level(p));
        auto chunk = SurfaceChunk::Build(*plan, key, 16, cancel);
        Check(bool(chunk), "Build spherical surface chunk");
        for (unsigned edge = 0; edge < 4; ++edge)
        {
            auto neighbor =
                SurfaceChunk::Build(*plan, CubeSphere::Neighbor(key, ChunkEdge(edge)), 16, cancel);
            for (unsigned i = 0; i <= 16; ++i)
            {
                unsigned index = edge == 0 ? i * 17 : edge == 1 ? i * 17 + 16 : edge == 2 ? i : 16 * 17 + i;
                const auto &vertex = chunk->vertices[index];
                double nearest = 1.e99;
                const TerrainVertex *shared = nullptr;
                for (const auto &other : neighbor->vertices)
                {
                    double d = Difference(vertex.point.normal, other.point.normal);
                    if (d < nearest)
                    {
                        nearest = d;
                        shared = &other;
                    }
                }
                Check(nearest < 1.e-13, "Adjacent cube faces share every terrain edge sample");
                Check(shared && std::fabs(shared->point.altitudeMeters - vertex.point.altitudeMeters) < 1.e-8,
                      "Height seams are identical");
                Check(shared && Difference(shared->normal, vertex.normal) < 1.e-9,
                      "Lighting normals agree at seams");
                Check(shared && shared->biome == vertex.biome,
                      "Biome classification is shared across chunk boundaries");
            }
        }
        for (unsigned y = 0; y <= 16; ++y)
        {
            for (unsigned x = 0; x <= 16; ++x)
            {
                auto point = CubeSphere::Point(key, double(x) / 16, double(y) / 16);
                auto contact = chunk->Contact(point);
                Check(contact.detailed && contact.land && std::fabs(contact.heightMeters) < 1.e-8,
                      "Contact grid agrees with rendered vertices");
                auto uv = CubeSphere::Coordinates(key, point);
                Check(std::fabs(uv[0] - double(x) / 16) < 1.e-8 && std::fabs(uv[1] - double(y) / 16) < 1.e-8,
                      "Cube sample/coordinate round trip");
            }
        }
        Check(chunk->Bytes() < 512 * 1024, "Bounded terrain/road payload");
    }
    cancel.store(true);
    Check(!SurfaceChunk::Build(*plan, CubeSphere::Address(p, G::FromDegrees(0, 0), 12), 16, cancel),
          "Cancelled builds do not publish partial meshes");
    auto key = CubeSphere::Address(p, G::FromDegrees(0, 0), 12);
    ++key.planetSeed;
    Throws([&] { SurfaceChunk::Build(*plan, key, 16, cancel); }, "Reject cross-world chunk jobs");
    Throws(
        [&] {
            auto s = StreamSettings{};
            s.cells = 15;
            s.Validate();
        },
        "Reject non-power-of-two seam grids");
    Throws(
        [&] {
            auto s = StreamSettings{};
            s.maxCompleted = 0;
            s.Validate();
        },
        "Reject unbounded completion queues");
    Throws([&] { CubeSphere::Point(key, -1, 0); }, "Reject invalid sample coordinates");
    cancel.store(false);
    auto roadPlan = std::make_shared<GeneratedPlanet>(*plan);
    key = CubeSphere::Address(p, G::FromDegrees(0, 0), 16);
    auto a = CubeSphere::Point(key, 0, .5), b = CubeSphere::Point(key, 1, .5);
    roadPlan->connections.push_back({0, 1, RouteMode::Road, {a, b}, G::Distance(p.radiusMeters, a, b)});
    auto roadChunk = SurfaceChunk::Build(*roadPlan, key, 16, cancel);
    Check(!roadChunk->roads.empty(), "Published chunks contain clipped connection roads");
    for (const auto &road : roadChunk->roads)
    {
        Check(road.points.size() > 1, "Published road ribbons contain complete segments");
        for (const auto &point : road.points)
        {
            Check(std::fabs(point.altitudeMeters - .08) < 1.e-8, "Roads follow the terrain height field");
        }
    }
}
void QueueCancellationAndWorldSwitch()
{
    auto plan = FlatPlanet();
    StreamSettings settings;
    settings.maxResident = 8;
    settings.maxQueued = 8;
    settings.maxCompleted = 1;
    settings.publishPerUpdate = 1;
    settings.loadRadiusMeters = 64;
    settings.retainRadiusMeters = 144;
    std::atomic_bool gate{true};
    std::atomic<unsigned> active{0}, started{0}, peak{0};
    TerrainStream stream(settings, [&](const GeneratedPlanet &p, const ChunkKey &key, unsigned cells,
                                       const std::atomic_bool &cancel) {
        unsigned count = ++active;
        peak.store((std::max)(peak.load(), count));
        ++started;
        while (gate.load() && !cancel.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto result = SurfaceChunk::Build(p, key, cells, cancel);
        --active;
        return result;
    });
    auto frame = G::Frame(G::FromDegrees(0, 44.9));
    stream.Open(plan);
    Pump([&] { stream.Update(frame, {200, 0, 0}); }, [&] { return started.load() > 0; });
    auto early = stream.Snapshot();
    Check(early->planet && early->chunks.empty(), "Planet publishes independently of detailed chunks");
    Check(!stream.Contact(frame.origin).detailed, "Missing detail has a procedural contact fallback");
    auto next = G::Advance(frame, 600, 0, plan->terrain.Planet().radiusMeters);
    stream.Update(next, {200, 0, 0});
    gate.store(false);
    Pump([&] { stream.Update(next, {200, 0, 0}); }, [&] { return !stream.Snapshot()->chunks.empty(); });
    auto held = stream.Snapshot();
    Check(held->statistics.cancelled > 0, "Retargeting cancels stale jobs");
    Check(peak.load() == 1, "Only one CPU worker can build terrain");
    for (unsigned i = 0; i < 100; ++i)
    {
        next = G::Advance(next, 7, 1, plan->terrain.Planet().radiusMeters);
        stream.Update(next, {500, 0, 0});
        auto snapshot = stream.Snapshot();
        Check(snapshot->statistics.resident <= settings.maxResident,
              "Resident count bounded during high speed travel");
        Check(snapshot->statistics.queued <= settings.maxQueued &&
                  snapshot->statistics.completed <= settings.maxCompleted,
              "Queues stay bounded");
        Check(snapshot->statistics.bytes <= settings.memoryBudgetBytes,
              "CPU retention stays inside its budget");
        Check(std::isfinite(stream.Contact(next.origin).heightMeters),
              "Unloaded terrain contact remains available during traversal");
    }
    auto oldEpoch = held->statistics.epoch;
    auto different = FlatPlanet(2000, 999, 2);
    stream.Open(different);
    auto otherFrame = G::Frame(G::FromDegrees(-80, 179));
    Pump([&] { stream.Update(otherFrame); }, [&] { return !stream.Snapshot()->chunks.empty(); });
    auto snapshot = stream.Snapshot();
    Check(snapshot->statistics.epoch > oldEpoch, "World switch advances the publication epoch");
    for (const auto &chunk : snapshot->chunks)
    {
        Check(chunk->key.planetSeed == 999 && chunk->key.generatorVersion == 2,
              "Old-world jobs cannot enter the new cache");
    }
    for (const auto &chunk : held->chunks)
    {
        Check(chunk->key.planetSeed == 17, "Previously retained snapshots remain immutable");
    }
    auto read = std::async(std::launch::async,
                           [held] { return held->chunks.front()->vertices.front().point.normal; });
    stream.Close();
    Check(stream.Snapshot()->chunks.empty() && !stream.Snapshot()->planet,
          "Close releases the active CPU world");
    Check(std::isfinite(Norm(read.get())), "Retained snapshots survive stream shutdown");
    Check(active.load() == 0, "All workers are reclaimed on close");
}
void FaultAndShutdown()
{
    auto plan = FlatPlanet();
    StreamSettings settings;
    settings.maxResident = 8;
    settings.maxQueued = 8;
    std::map<std::string, unsigned> attempts;
    std::mutex attemptsMutex;
    TerrainStream broken(settings,
                         [&](const GeneratedPlanet &, const ChunkKey &key, unsigned,
                             const std::atomic_bool &) -> std::shared_ptr<const SurfaceChunk> {
                             std::lock_guard<std::mutex> lock(attemptsMutex);
                             ++attempts[key.StableKey()];
                             throw std::runtime_error("injected terrain error");
                         });
    auto frame = G::Frame(G::FromDegrees(0, 0));
    broken.Open(plan);
    Pump([&] { broken.Update(frame); }, [&] { return !broken.Snapshot()->statistics.error.empty(); });
    for (unsigned i = 0; i < 20; ++i)
    {
        broken.Update(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(std::isfinite(broken.Contact(frame.origin).heightMeters),
          "A failed detail job retains the base surface");
    broken.Close();
    for (const auto &entry : attempts)
    {
        Check(entry.second == 1, "Failed chunks do not enter an automatic retry loop");
    }
    std::atomic_bool entered{false};
    TerrainStream waiting(settings,
                          [&](const GeneratedPlanet &, const ChunkKey &, unsigned,
                              const std::atomic_bool &cancel) -> std::shared_ptr<const SurfaceChunk> {
                              entered.store(true);
                              while (!cancel.load())
                              {
                                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                              }
                              return {};
                          });
    waiting.Open(plan);
    Pump([&] { waiting.Update(frame); }, [&] { return entered.load(); });
    auto start = std::chrono::steady_clock::now();
    waiting.Close();
    Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(1),
          "Shutdown signals cancellation before joining the worker");
}
void RemotePreparation()
{
    auto plan = FlatPlanet(1200000);
    const auto source = G::FromDegrees(0, 0), target = G::FromDegrees(0, 180);
    SurfaceSession session(plan->terrain.Planet(), source);
    session.Start(plan);
    Pump([&] { session.Update(); }, [&] { return session.Ready() && session.Stream().Detailed(source, 12); });
    const auto epoch = session.Stream().Snapshot()->statistics.epoch;
    const SurfaceView camera{G::Advance(session.Pose(), -240, -240, session.Planet().radiusMeters).origin,
                             96};
    Pump([&] { session.Update({}, std::nullopt, true, camera); },
         [&] {
             return session.Stream().Detailed(camera.center, 64) && session.Stream().Detailed(source, 12);
         });
    Check(G::Distance(session.Planet().radiusMeters, source, session.Pose().origin) < 1.e-6,
          "An elevated camera loads the visible ground without moving the pilot");
    Check(session.Stream().Snapshot()->statistics.resident <= session.Stream().Settings().maxResident &&
              session.Stream().Snapshot()->statistics.bytes <= session.Stream().Settings().memoryBudgetBytes,
          "Camera coverage shares the bounded streaming budget");
    Pump([&] { session.Update({}, target); },
         [&] { return session.Stream().Detailed(target, 12) && session.Stream().Detailed(source, 12); });
    Check(G::Distance(session.Planet().radiusMeters, source, session.Pose().origin) < 1.e-6,
          "Prefetch never moves the authoritative player");
    session.Update(); // Cancellation removes remote-only jobs/chunks while retaining the source.
    Check(!session.Stream().Detailed(target) && session.Stream().Detailed(source),
          "Cancelling a remote focus keeps the source ready");
    Pump([&] { session.Update({}, target); }, [&] { return session.Stream().Detailed(target, 12); });
    Check(session.LandingClear(target) && session.Relocate(target, .65, 0),
          "A prepared antipodal landing uses direct spherical contact checks");
    Check(session.Stream().Snapshot()->statistics.epoch == epoch,
          "Travel commits within the same world epoch");
    Check(session.Stream().Snapshot()->statistics.resident <= session.Stream().Settings().maxResident &&
              session.Stream().Snapshot()->statistics.bytes <= session.Stream().Settings().memoryBudgetBytes,
          "Two-focus preparation retains the existing memory budget");
    TerrainStream failed({}, [&](const GeneratedPlanet &p, const ChunkKey &key, unsigned cells,
                                 const std::atomic_bool &cancel) {
        if (key.face == CubeFace::NegativeX)
        {
            throw std::runtime_error("remote fixture failure");
        }
        return SurfaceChunk::Build(p, key, cells, cancel);
    });
    failed.Open(plan);
    Pump([&] { failed.Update(G::Frame(source), {}, target); },
         [&] { return failed.FailedAt(target) && failed.Detailed(source); });
    Check(!failed.Detailed(target),
          "A failed destination never becomes ready while the source remains usable");
}
void SphericalTravelAndOrigin()
{
    for (double radius : {1000., 4800000.})
    {
        auto plan = FlatPlanet(radius);
        StreamSettings settings;
        settings.originRadiusMeters = 32;
        auto spawn = radius < 2000 ? G::FromDegrees(84, 44.9999) : G::FromDegrees(0, 44.9999);
        SurfaceSession session(plan->terrain.Planet(), spawn, settings);
        session.Start(plan);
        Pump([&] { session.Update(); }, [&] { return session.Ready(); });
        unsigned steps = radius < 2000 ? 512 : 128;
        double step = radius < 2000 ? G::Circumference(radius) / steps : 16;
        auto initial = session.Pose();
        unsigned rebases = 0;
        bool crossedFace = false;
        auto firstFace = CubeSphere::Address(plan->terrain.Planet(), spawn, 0).face;
        for (unsigned i = 0; i < steps; ++i)
        {
            auto before = session.Pose().origin;
            session.Move(radius < 2000 ? 0 : step, radius < 2000 ? step : 0, true);
            crossedFace |=
                CubeSphere::Address(plan->terrain.Planet(), session.Pose().origin, 0).face != firstFace;
            Check(std::fabs(G::Distance(radius, before, session.Pose().origin) - step) < 1.e-6,
                  "Travel advances the requested spherical distance");
            auto camera = G::Advance(session.Pose(), -3, -2, radius).origin;
            auto local = session.Local(camera);
            auto old = session.Rebase();
            if (old)
            {
                ++rebases;
                auto restored = G::FromLocal(*old, local, radius);
                Check(G::Distance(radius, camera, restored) < 1.e-6,
                      "Origin shifts preserve world camera positions");
                auto v = G::TransformDirection({.6, .8, 0}, *old, session.Origin());
                Check(std::fabs(Norm(v) - 1) < 1.e-10, "Rebasing preserves velocity magnitude");
            }
            auto p = session.Local(session.Pose().origin);
            Check(std::hypot(p.x, p.y) <= settings.originRadiusMeters + .001,
                  "Render coordinates remain near the local origin");
            if (i % 16 == 0)
            {
                session.Update({0, 500, 0});
            }
        }
        Check(rebases > 0, "Long exploration shifts the origin");
        Check(crossedFace, "Travel crosses cube faces at both validation and realistic radii");
        if (radius < 2000)
        {
            Check(G::Distance(radius, spawn, session.Pose().origin) < 1.e-6,
                  "A full spherical traversal returns to the starting point");
            Check(Difference(initial.right, session.Pose().right) < 1.e-8,
                  "Parallel transport preserves heading after a great-circle loop");
        }
        session.Stop();
        Check(!session.Ready(), "Stopped sessions are not playable");
    }
    std::atomic_bool cancel{true};
    Throws(
        [&] {
            PlanetGenerator::Erma(
                {LegacyWorldAdapter::ErmaExterior().Anchor("mine_entrance", "mq00_mine", {128, 18, 0})})
                .Generate(&cancel);
        },
        "Planet preparation supports cooperative cancellation");
}
} // namespace
int main()
{
    try
    {
        MeshSeamsAndContacts();
        QueueCancellationAndWorldSwitch();
        FaultAndShutdown();
        RemotePreparation();
        SphericalTravelAndOrigin();
        ContentContracts();
        AuthoredCoreContracts();
        std::cout << "World streaming tests passed: " << checks << " checks\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "World streaming regression after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
