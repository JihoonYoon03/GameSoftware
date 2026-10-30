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
        SphericalTravelAndOrigin();
        std::cout << "World streaming tests passed: " << checks << " checks\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "World streaming regression after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
