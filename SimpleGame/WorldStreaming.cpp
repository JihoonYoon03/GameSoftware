#include "stdafx.h"
#include "WorldStreaming.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

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
Vector3d Mul(Vector3d a, double n)
{
    return {a.x * n, a.y * n, a.z * n};
}
double Length(Vector3d v)
{
    return std::hypot(std::hypot(v.x, v.y), v.z);
}
double Distance(const PlanetDefinition &p, SurfacePoint a, SurfacePoint b)
{
    return G::Distance(p.radiusMeters, a, b);
}
GroundContact FieldContact(const PlanetTerrain &terrain, const SurfacePoint &point)
{
    auto frame = G::Frame(point);
    double radius = terrain.Planet().radiusMeters;
    constexpr double step = 4;
    double x = (terrain.Height(G::Advance(frame, step, 0, radius).origin) -
                terrain.Height(G::Advance(frame, -step, 0, radius).origin)) /
               (2 * step);
    double y = (terrain.Height(G::Advance(frame, 0, step, radius).origin) -
                terrain.Height(G::Advance(frame, 0, -step, radius).origin)) /
               (2 * step);
    double height = terrain.Height(point);
    return {height, std::hypot(x, y),
            G::Unit(Add(point.normal, Add(Mul(frame.right, -x), Mul(frame.forward, -y)))),
            height >= PlanetTerrain::SeaLevelMeters, false};
}
SurfacePoint Along(const PlanetDefinition &p, SurfacePoint a, SurfacePoint b, double t)
{
    auto frame = G::Frame(a);
    auto delta = G::ToLocal(frame, b, p.radiusMeters);
    return G::Advance(frame, delta.x * t, delta.y * t, p.radiusMeters).origin;
}
bool SameKeyWorld(const ChunkKey &key, const PlanetDefinition &planet)
{
    return key.planetId == planet.id && key.planetSeed == planet.seed &&
           key.generatorVersion == planet.generatorVersion;
}
} // namespace

void StreamSettings::Validate() const
{
    Require(cells >= 4 && cells <= 32 && (cells & (cells - 1)) == 0,
            "Chunk cells must be a power of two, 4..32");
    Require(maxResident >= 8 && maxResident <= 256 && maxQueued >= 1 && maxQueued <= 256 &&
                maxCompleted >= 1 && maxCompleted <= 8 && publishPerUpdate >= 1 && publishPerUpdate <= 8,
            "Invalid stream queue budgets");
    for (double value : {targetChunkMeters, loadRadiusMeters, retainRadiusMeters, predictionSeconds,
                         maxPredictionMeters, originRadiusMeters})
    {
        Require(std::isfinite(value) && value > 0, "Invalid streaming distance/time");
    }
    Require(targetChunkMeters >= 16 && loadRadiusMeters >= 64 && retainRadiusMeters > loadRadiusMeters &&
                originRadiusMeters >= 16 && originRadiusMeters <= 4096,
            "Invalid streaming coverage/origin");
    Require(memoryBudgetBytes >= 4 * 1024 * 1024 && memoryBudgetBytes <= 256 * 1024 * 1024,
            "Invalid stream memory budget");
}
std::uint8_t StreamSettings::Level(const PlanetDefinition &planet) const
{
    Validate();
    planet.Validate();
    return std::uint8_t(
        std::clamp(std::ceil(std::log2(2 * planet.radiusMeters / targetChunkMeters)), 0., 30.));
}
std::size_t SurfaceChunk::Bytes() const
{
    std::size_t bytes = sizeof(*this) + key.planetId.capacity() +
                        vertices.capacity() * sizeof(TerrainVertex) +
                        regions.capacity() * sizeof(std::size_t) + roads.capacity() * sizeof(RoadRibbon) +
                        structures.capacity() * sizeof(SurfaceStructure);
    for (const auto &road : roads)
    {
        bytes += road.points.capacity() * sizeof(SurfacePoint);
    }
    for (const auto &structure : structures)
    {
        bytes += structure.id.capacity() + structure.regionId.capacity();
    }
    return bytes;
}
GroundContact SurfaceChunk::Contact(const SurfacePoint &point) const
{
    Require(cells >= 4 && vertices.size() == std::size_t(cells + 1) * (cells + 1),
            "Invalid terrain contact grid");
    auto uv = CubeSphere::Coordinates(key, point);
    Require(uv[0] >= -1.e-5 && uv[0] <= 1 + 1.e-5 && uv[1] >= -1.e-5 && uv[1] <= 1 + 1.e-5,
            "Contact outside chunk");
    double x = std::clamp(uv[0], 0., 1.) * cells, y = std::clamp(uv[1], 0., 1.) * cells;
    unsigned ix = (std::min)(cells - 1, unsigned(x)), iy = (std::min)(cells - 1, unsigned(y));
    double u = x - ix, v = y - iy;
    std::array<unsigned, 3> indices;
    std::array<double, 3> weights;
    unsigned a = iy * (cells + 1) + ix, b = a + 1, c = a + cells + 1, d = c + 1;
    if (u + v <= 1)
    {
        indices = {a, b, c};
        weights = {1 - u - v, u, v};
    }
    else
    {
        indices = {d, c, b};
        weights = {u + v - 1, 1 - u, 1 - v};
    }
    GroundContact result;
    result.normal = {};
    result.detailed = true;
    for (unsigned i = 0; i < 3; ++i)
    {
        result.heightMeters += vertices[indices[i]].point.altitudeMeters * weights[i];
        result.normal = Add(result.normal, Mul(vertices[indices[i]].normal, weights[i]));
    }
    result.normal = G::Unit(result.normal);
    double up = std::clamp(G::Dot(result.normal, point.normal), 1.e-9, 1.);
    result.slope = std::sqrt((std::max)(0., 1 - up * up)) / up;
    result.land = result.heightMeters >= PlanetTerrain::SeaLevelMeters;
    return result;
}
std::shared_ptr<const SurfaceChunk> SurfaceChunk::Build(const GeneratedPlanet &plan, const ChunkKey &key,
                                                        unsigned cells, const std::atomic_bool &cancel)
{
    key.Validate();
    const auto &p = plan.terrain.Planet();
    Require(SameKeyWorld(key, p) && key.layer == 0, "Surface job belongs to another world/layer");
    Require(cells >= 4 && cells <= 32 && (cells & (cells - 1)) == 0, "Invalid surface mesh resolution");
    auto result = std::make_shared<SurfaceChunk>();
    result->key = key;
    result->cells = cells;
    auto center = CubeSphere::Center(key);
    center.altitudeMeters = (std::max)(PlanetTerrain::SeaLevelMeters, plan.terrain.Height(center));
    result->frame = G::Frame(center);
    result->regions = plan.RegionsInChunk(key);
    result->vertices.reserve((cells + 1) * (cells + 1));
    for (unsigned y = 0; y <= cells; ++y)
    {
        if (cancel.load())
        {
            return {};
        }
        for (unsigned x = 0; x <= cells; ++x)
        {
            auto point = CubeSphere::Point(key, double(x) / cells, double(y) / cells);
            auto contact = FieldContact(plan.terrain, point);
            point.altitudeMeters = contact.heightMeters;
            auto biome = plan.terrain.Environment(point, contact.heightMeters, contact.slope).biome;
            result->vertices.push_back({point, contact.normal, biome});
        }
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        result->radiusMeters =
            (std::max)(result->radiusMeters, Distance(p, center, CubeSphere::Corner(key, i % 2, i / 2)));
    }
    std::size_t roadPoints = 0;
    for (const auto &edge : plan.connections)
    {
        if (cancel.load())
        {
            return {};
        }
        if (edge.mode != RouteMode::Road)
        {
            continue;
        }
        RoadRibbon ribbon;
        auto flush = [&] {
            if (ribbon.points.size() > 1)
            {
                result->roads.push_back(std::move(ribbon));
                ribbon = RoadRibbon{};
            }
        };
        for (std::size_t i = 1; i < edge.points.size(); ++i)
        {
            auto a = edge.points[i - 1], b = edge.points[i];
            double length = Distance(p, a, b);
            if (length < .01)
            {
                continue;
            }
            auto middle = G::Unit(Add(a.normal, b.normal));
            double cap = (length * .5 + result->radiusMeters + 8) / p.radiusMeters;
            if (cap < G::Pi && G::Dot(center.normal, middle) < std::cos(cap))
            {
                flush();
                continue;
            }
            unsigned steps = unsigned(std::ceil(length / 4));
            for (unsigned step = 0; step < steps; ++step)
            {
                if ((step & 31) == 0 && cancel.load())
                {
                    return {};
                }
                auto left = Along(p, a, b, double(step) / steps),
                     right = Along(p, a, b, double(step + 1) / steps);
                auto midpoint = Along(p, a, b, (step + .5) / steps);
                if (!(CubeSphere::Address(p, midpoint, key.level) == key))
                {
                    flush();
                    continue;
                }
                if (roadPoints + (ribbon.points.empty() ? 2u : 1u) > 1024)
                {
                    flush();
                    roadPoints = 1024;
                    break;
                } // Fixed per-chunk route payload budget.
                if (ribbon.points.empty())
                {
                    left.altitudeMeters = plan.terrain.Height(left) + .08;
                    ribbon.points.push_back(left);
                    ++roadPoints;
                }
                right.altitudeMeters = plan.terrain.Height(right) + .08;
                ribbon.points.push_back(right);
                ++roadPoints;
            }
        }
        flush();
        if (roadPoints >= 1024)
        {
            break;
        }
    }
    auto content = ContentLayout::Publish(plan, key, result->radiusMeters, cancel);
    if (cancel.load())
    {
        return {};
    }
    result->structures = std::move(content.structures);
    for (auto &street : content.streets)
    {
        result->roads.push_back(
            {std::move(street.points), street.widthMeters, street.sidewalkMeters, street.lanes});
    }
    return result;
}

// ============================================================================
// TerrainStream — worker owns CPU scratch; game/GL/resource registries stay on main
// ============================================================================
struct TerrainStream::Data
{
    struct Job
    {
        ChunkKey key;
        std::shared_ptr<std::atomic_bool> cancel;
    };
    struct Result
    {
        Job job;
        std::shared_ptr<const SurfaceChunk> chunk;
        std::string error;
    };
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::atomic_bool stopping{false};
    std::deque<Job> jobs;
    std::deque<Result> completed;
    std::map<ChunkKey, std::shared_ptr<std::atomic_bool>> pending;
    std::map<ChunkKey, std::shared_ptr<const SurfaceChunk>> resident;
    std::set<ChunkKey> desired;
    std::set<ChunkKey> failed;
    std::shared_ptr<const GeneratedPlanet> prepared, planet;
    std::shared_ptr<const SurfaceSnapshot> published = std::make_shared<SurfaceSnapshot>();
    std::string prepareError;
    StreamStatistics stats;
};
TerrainStream::TerrainStream(StreamSettings s, Builder b)
    : data(std::make_unique<Data>()), settings(s), builder(std::move(b))
{
    settings.Validate();
    Require(bool(builder), "Missing terrain builder");
}
TerrainStream::~TerrainStream()
{
    Close();
}
void TerrainStream::Close()
{
    auto &d = *data;
    d.stopping.store(true);
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        for (auto &entry : d.pending)
        {
            entry.second->store(true);
        }
    }
    d.changed.notify_all();
    if (d.worker.joinable())
    {
        d.worker.join();
    }
    std::lock_guard<std::mutex> lock(d.mutex);
    auto epoch = d.stats.epoch + 1;
    d.jobs.clear();
    d.completed.clear();
    d.pending.clear();
    d.resident.clear();
    d.desired.clear();
    d.failed.clear();
    d.prepared.reset();
    d.planet.reset();
    d.prepareError.clear();
    d.stats = {};
    d.stats.epoch = epoch;
    auto snapshot = std::make_shared<SurfaceSnapshot>();
    snapshot->statistics = d.stats;
    std::atomic_store(&d.published, std::shared_ptr<const SurfaceSnapshot>(snapshot));
}
void TerrainStream::Start(
    std::function<std::shared_ptr<const GeneratedPlanet>(const std::atomic_bool &)> prepare)
{
    Close();
    auto &d = *data;
    d.stopping.store(false);
    d.stats.preparing = true;
    d.worker = std::thread([this, prepare = std::move(prepare)] {
        auto &work = *data;
        std::shared_ptr<const GeneratedPlanet> plan;
        try
        {
            plan = prepare(work.stopping);
            if (work.stopping.load())
            {
                return;
            }
            Require(bool(plan), "Planet preparation returned no world");
            plan->terrain.Planet().Validate();
            {
                std::lock_guard<std::mutex> lock(work.mutex);
                work.prepared = plan;
            }
        }
        catch (const std::exception &error)
        {
            std::lock_guard<std::mutex> lock(work.mutex);
            if (!work.stopping.load())
            {
                work.prepareError = error.what();
            }
            return;
        }
        for (;;)
        {
            Data::Job job;
            {
                std::unique_lock<std::mutex> lock(work.mutex);
                work.changed.wait(lock, [&] { return work.stopping.load() || !work.jobs.empty(); });
                if (work.stopping.load())
                {
                    return;
                }
                job = std::move(work.jobs.front());
                work.jobs.pop_front();
            }
            Data::Result result;
            result.job = job;
            try
            {
                if (!job.cancel->load())
                {
                    result.chunk = builder(*plan, job.key, settings.cells, *job.cancel);
                }
            }
            catch (const std::exception &error)
            {
                result.error = error.what();
            }
            std::unique_lock<std::mutex> lock(work.mutex);
            work.changed.wait(lock, [&] {
                return work.stopping.load() || job.cancel->load() ||
                       work.completed.size() < settings.maxCompleted;
            });
            if (work.stopping.load())
            {
                return;
            }
            if (job.cancel->load())
            {
                ++work.stats.cancelled;
                continue;
            }
            work.completed.push_back(std::move(result));
        }
    });
}
void TerrainStream::Open(PlanetGenerator generator)
{
    Start([generator = std::move(generator)](const std::atomic_bool &cancel) {
        return std::make_shared<GeneratedPlanet>(generator.Generate(&cancel));
    });
}
void TerrainStream::Open(std::shared_ptr<const GeneratedPlanet> planet)
{
    Require(bool(planet), "Cannot stream a null planet");
    Start([planet](const std::atomic_bool &) { return planet; });
}
std::shared_ptr<const SurfaceSnapshot> TerrainStream::Snapshot() const
{
    return std::atomic_load(&data->published);
}
void TerrainStream::Update(const TangentFrame &focus, Vector3d velocity)
{
    G::Validate(focus);
    Require(std::isfinite(Length(velocity)), "Invalid streaming velocity");
    auto &d = *data;
    // Only this main-thread method mutates retained chunks and publishes snapshots.
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (d.prepared)
        {
            d.planet = std::move(d.prepared);
            d.stats.preparing = false;
        }
        if (!d.prepareError.empty())
        {
            d.stats.error = d.prepareError;
            d.stats.preparing = false;
        }
        unsigned published = 0;
        while (!d.completed.empty() && published < settings.publishPerUpdate)
        {
            auto result = std::move(d.completed.front());
            d.completed.pop_front();
            auto pending = d.pending.find(result.job.key);
            bool current = pending != d.pending.end() && pending->second == result.job.cancel;
            if (current)
            {
                d.pending.erase(pending);
            }
            if (!current || result.job.cancel->load() || !d.desired.count(result.job.key))
            {
                ++d.stats.discarded;
                continue;
            }
            if (!result.error.empty())
            {
                d.stats.error = result.error;
                d.failed.insert(result.job.key);
                continue;
            }
            if (!result.chunk || !(result.chunk->key == result.job.key) ||
                result.chunk->cells != settings.cells ||
                result.chunk->vertices.size() != std::size_t(settings.cells + 1) * (settings.cells + 1))
            {
                d.stats.error = "Invalid worker terrain publication";
                d.failed.insert(result.job.key);
                continue;
            }
            d.resident[result.job.key] = std::move(result.chunk);
            ++d.stats.generated;
            ++d.stats.revision;
            ++published;
        }
    }
    if (d.planet)
    {
        const auto &p = d.planet->terrain.Planet();
        auto level = settings.Level(p);
        double speed = std::hypot(velocity.x, velocity.y),
               ahead = (std::min)(settings.maxPredictionMeters, speed * settings.predictionSeconds);
        auto lead = speed > .001 ? G::Advance(focus, velocity.x / speed * ahead, velocity.y / speed * ahead,
                                              p.radiusMeters)
                                       .origin
                                 : focus.origin;
        struct Candidate
        {
            ChunkKey key;
            double priority;
        };
        std::vector<Candidate> candidates;
        std::set<ChunkKey> visited;
        std::deque<ChunkKey> search;
        auto enqueue = [&](ChunkKey key) {
            if (visited.insert(key).second)
            {
                search.push_back(std::move(key));
            }
        };
        enqueue(CubeSphere::Address(p, focus.origin, level));
        enqueue(CubeSphere::Address(p, lead, level));
        while (!search.empty() && visited.size() <= 1024)
        {
            auto key = search.front();
            search.pop_front();
            auto center = CubeSphere::Center(key);
            double bound = 0;
            for (unsigned i = 0; i < 4; ++i)
            {
                bound = (std::max)(bound, Distance(p, center, CubeSphere::Corner(key, i % 2, i / 2)));
            }
            double here = Distance(p, focus.origin, center), there = Distance(p, lead, center);
            if ((std::min)(here, there) > settings.loadRadiusMeters + bound)
            {
                continue;
            }
            candidates.push_back({key, (std::min)(here, there + settings.loadRadiusMeters * .35)});
            for (unsigned edge = 0; edge < 4; ++edge)
            {
                enqueue(CubeSphere::Neighbor(key, ChunkEdge(edge)));
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
            return a.priority != b.priority ? a.priority < b.priority : a.key < b.key;
        });
        if (candidates.size() > settings.maxResident)
        {
            candidates.resize(settings.maxResident);
        }
        d.desired.clear();
        for (const auto &candidate : candidates)
        {
            d.desired.insert(candidate.key);
        }
        std::lock_guard<std::mutex> lock(d.mutex);
        for (auto it = d.failed.begin(); it != d.failed.end();)
        {
            if (!d.desired.count(*it))
            {
                it = d.failed.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = d.pending.begin(); it != d.pending.end();)
        {
            if (!d.desired.count(it->first))
            {
                it->second->store(true);
                it = d.pending.erase(it);
            }
            else
            {
                ++it;
            }
        }
        d.jobs.erase(
            std::remove_if(d.jobs.begin(), d.jobs.end(), [](const auto &job) { return job.cancel->load(); }),
            d.jobs.end());
        for (const auto &candidate : candidates)
        {
            if (d.pending.size() >= settings.maxQueued)
            {
                break;
            }
            if (d.resident.count(candidate.key) || d.pending.count(candidate.key) ||
                d.failed.count(candidate.key))
            {
                continue;
            }
            auto cancel = std::make_shared<std::atomic_bool>(false);
            d.pending[candidate.key] = cancel;
            d.jobs.push_back({candidate.key, cancel});
        }
        std::map<ChunkKey, double> priorities;
        for (const auto &candidate : candidates)
        {
            priorities.emplace(candidate.key, candidate.priority);
        }
        std::sort(d.jobs.begin(), d.jobs.end(), [&](const auto &a, const auto &b) {
            double x = priorities.at(a.key), y = priorities.at(b.key);
            return x != y ? x < y : a.key < b.key;
        });
        std::size_t bytes = 0;
        for (const auto &entry : d.resident)
        {
            bytes += entry.second->Bytes();
        }
        for (auto it = d.resident.begin(); it != d.resident.end();)
        {
            auto center = it->second->frame.origin;
            if ((std::min)(Distance(p, focus.origin, center), Distance(p, lead, center)) >
                settings.retainRadiusMeters + it->second->radiusMeters)
            {
                bytes -= it->second->Bytes();
                it = d.resident.erase(it);
                ++d.stats.evicted;
                ++d.stats.revision;
            }
            else
            {
                ++it;
            }
        }
        while (d.resident.size() > settings.maxResident || bytes > settings.memoryBudgetBytes)
        {
            auto far = d.resident.begin();
            double farthest = -1;
            for (auto it = d.resident.begin(); it != d.resident.end(); ++it)
            {
                double distance = Distance(p, focus.origin, it->second->frame.origin);
                if (distance > farthest)
                {
                    farthest = distance;
                    far = it;
                }
            }
            bytes -= far->second->Bytes();
            d.resident.erase(far);
            ++d.stats.evicted;
            ++d.stats.revision;
        }
        d.stats.bytes = bytes;
        d.stats.resident = d.resident.size();
    }
    auto snapshot = std::make_shared<SurfaceSnapshot>();
    snapshot->planet = d.planet;
    for (const auto &entry : d.resident)
    {
        snapshot->chunks.push_back(entry.second);
    }
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        d.stats.queued = d.pending.size();
        d.stats.completed = d.completed.size();
        snapshot->statistics = d.stats;
    }
    std::atomic_store(&d.published, std::shared_ptr<const SurfaceSnapshot>(snapshot));
    d.changed.notify_all();
}
GroundContact TerrainStream::Contact(const SurfacePoint &point) const
{
    G::Validate(point);
    auto snapshot = Snapshot();
    Require(bool(snapshot->planet), "Surface world is not ready");
    auto key = CubeSphere::Address(snapshot->planet->terrain.Planet(), point,
                                   settings.Level(snapshot->planet->terrain.Planet()));
    auto found = std::lower_bound(snapshot->chunks.begin(), snapshot->chunks.end(), key,
                                  [](const auto &chunk, const auto &value) { return chunk->key < value; });
    if (found != snapshot->chunks.end() && (*found)->key == key)
    {
        return (*found)->Contact(point);
    }
    return FieldContact(snapshot->planet->terrain, point);
}

// ============================================================================
// SurfaceSession — travel never waits on a detailed chunk and never wraps a rectangle
// ============================================================================
SurfaceSession::SurfaceSession(PlanetDefinition p, SurfacePoint spawn, StreamSettings settings)
    : planet(std::move(p)), pose(G::Frame(spawn)), origin(pose), stream(settings)
{
    planet.Validate();
}
void SurfaceSession::Start(PlanetGenerator generator)
{
    Require(generator.Planet().SameWorld(planet), "Session generator belongs to another world");
    contentCollision.Clear();
    stream.Open(std::move(generator));
}
void SurfaceSession::Start(std::shared_ptr<const GeneratedPlanet> plan)
{
    Require(plan && plan->terrain.Planet().SameWorld(planet), "Session world mismatch");
    contentCollision.Clear();
    stream.Open(std::move(plan));
}
void SurfaceSession::Stop()
{
    stream.Close();
    contentCollision.Clear();
}
bool SurfaceSession::Ready() const
{
    auto snapshot = stream.Snapshot();
    return snapshot->planet && snapshot->planet->terrain.Planet().SameWorld(planet);
}
void SurfaceSession::Update(Vector3d velocity)
{
    stream.Update(pose, velocity);
    if (Ready())
    {
        pose.origin.altitudeMeters =
            (std::max)(PlanetTerrain::SeaLevelMeters, stream.Contact(pose.origin).heightMeters);
        // Also provides a tiny CPU-only object fallback before a detailed render chunk is published.
        contentCollision.Blocked(*stream.Snapshot()->planet, pose.origin, .32);
    }
}
Vector3d SurfaceSession::Local(const SurfacePoint &point) const
{
    return G::ToLocal(origin, point, planet.radiusMeters);
}
SurfacePoint SurfaceSession::PointAt(double x, double y) const
{
    return G::FromLocal(origin, {x, y, 0}, planet.radiusMeters);
}
GroundContact SurfaceSession::ContactAt(double x, double y) const
{
    return stream.Contact(PointAt(x, y));
}
bool SurfaceSession::Blocked(double x, double y, bool hovering) const
{
    if (!Ready())
    {
        return true;
    }
    auto contact = ContactAt(x, y);
    return (!contact.land && !hovering) || (contact.land && contact.slope > (hovering ? .35 : .65)) ||
           contentCollision.Blocked(*stream.Snapshot()->planet, PointAt(x, y), hovering ? .65 : .32);
}
void SurfaceSession::Move(double x, double y, bool hovering)
{
    Require(std::isfinite(x) && std::isfinite(y) && std::hypot(x, y) <= 4096,
            "Invalid surface movement sweep");
    if (!Ready())
    {
        return;
    }
    // Small object sweeps complement the existing local sliding solver; no tunnelling through props.
    unsigned steps = (std::max)(1u, unsigned(std::ceil(std::hypot(x, y) / .4)));
    for (unsigned step = 0; step < steps; ++step)
    {
        auto next = G::Advance(pose, x / steps, y / steps, planet.radiusMeters);
        auto contact = stream.Contact(next.origin);
        if ((!contact.land && !hovering) || (contact.land && contact.slope > (hovering ? .35 : .65)) ||
            contentCollision.Blocked(*stream.Snapshot()->planet, next.origin, hovering ? .65 : .32))
        {
            break;
        }
        next.origin.altitudeMeters = (std::max)(PlanetTerrain::SeaLevelMeters, contact.heightMeters);
        pose = next;
    }
}
bool SurfaceSession::Relocate(const SurfacePoint &point)
{
    G::Validate(point);
    if (!Ready())
    {
        return false;
    }
    auto plan = stream.Snapshot()->planet;
    auto frame = G::Frame(point);
    for (unsigned ring = 0; ring <= 8; ++ring)
    {
        for (unsigned step = 0; step < (ring == 0 ? 1u : 8u); ++step)
        {
            double angle = step * G::Pi / 4;
            auto candidate = G::Advance(frame, ring * 4 * std::cos(angle), ring * 4 * std::sin(angle),
                                        planet.radiusMeters);
            auto contact = stream.Contact(candidate.origin);
            if (!contact.land || contact.slope > .3 || contentCollision.Blocked(*plan, candidate.origin, .65))
            {
                continue;
            }
            candidate.origin.altitudeMeters = contact.heightMeters;
            pose = origin = candidate;
            stream.Update(pose);
            return true;
        }
    }
    return false;
}
std::optional<TangentFrame> SurfaceSession::Rebase()
{
    if (Distance(planet, pose.origin, origin.origin) < stream.Settings().originRadiusMeters &&
        std::fabs(pose.origin.altitudeMeters - origin.origin.altitudeMeters) < 64)
    {
        return std::nullopt;
    }
    auto old = origin;
    origin = pose;
    return old;
}
WorldLocation SurfaceSession::Location() const
{
    WorldLocation location;
    location.planetId = planet.id;
    location.planetSeed = planet.seed;
    location.generatorVersion = planet.generatorVersion;
    location.regionId = "overworld";
    location.position = pose.origin;
    auto snapshot = stream.Snapshot();
    if (!snapshot->planet)
    {
        return location;
    }
    auto key = CubeSphere::Address(planet, pose.origin, stream.Settings().Level(planet));
    for (auto index : snapshot->planet->RegionsInChunk(key))
    {
        const auto &r = snapshot->planet->regions[index];
        if (r.center.space != SpaceKind::Surface)
        {
            continue;
        }
        auto local = G::ToLocal(r.frame, pose.origin, planet.radiusMeters);
        bool inside = true;
        for (std::size_t i = 0; i < r.footprint.size(); ++i)
        {
            auto a = r.footprint[i], b = r.footprint[(i + 1) % r.footprint.size()];
            if ((b.x - a.x) * (local.y - a.y) - (b.y - a.y) * (local.x - a.x) < 0)
            {
                inside = false;
                break;
            }
        }
        if (inside)
        {
            location.regionId = r.definition.id;
            break;
        }
    }
    return location;
}
} // namespace world
