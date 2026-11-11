#include "stdafx.h"
#include "Civic.h"
#include "PlayerCombat.h"
#include "Profiler.h"
#include "Drawing.h"
#include "Hud.h"
#include "Overworld.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include "RenderQueue.h"
#include "WorldContent.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>
#include <set>
#include <stdexcept>

namespace tutorial
{
namespace
{
CivicState civic;
PlanetSecurityState planetary;
world::ContentCollision shotCollision;
std::optional<world::SurfacePoint> reportedPoint;
int navigationBudget = 2;
struct SurfaceTarget
{
    Vector2 position;
    float elevation = 0;
    int resident = -1, landmark = -1, city = -1;
    std::uint64_t vehicle = 0;
    bool vehicleBody = false;
    float yaw = 0;
    models::Point minimum, maximum;
};
std::vector<SurfaceTarget> surfaceTargets;
void PrepareSurfaceTargets()
{
    auto &g = Game::Get();
    surfaceTargets.clear();
    if (!g.SurfaceCities() || !CivicJustice::IsExterior())
    {
        return;
    }
    auto add = [&](int city, Vector2 p, int resident, int landmark, const TrafficVehicle *vehicle) {
        auto point = g.SurfaceCities()->Point(*g.Surface(), city, p);
        if (!point || world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, *point,
                                                       g.Surface()->Pose().origin) > 150)
        {
            return;
        }
        SurfaceTarget target;
        target.position = PlanetSecurity::Local(*point);
        target.elevation = PlanetSecurity::Elevation(*point);
        target.city = city;
        target.resident = resident;
        target.landmark = landmark;
        if (vehicle)
        {
            target.vehicle = vehicle->id;
            target.vehicleBody = true;
            target.yaw = g.SurfaceCities()->Yaw(*g.Surface(), city, vehicle->yaw);
            const auto &mesh = models::ResourceAssets::Get(
                models::ModelLibrary::Get().vehicles[size_t(vehicle->id % 4)].body.resourceId);
            target.minimum = mesh.minimum;
            target.maximum = mesh.maximum;
        }
        surfaceTargets.push_back(std::move(target));
    };
    const auto &residents = g.GetState().residents;
    for (size_t i = 0; i < residents.size(); ++i)
    {
        const auto &r = residents[i];
        if (r.health > 0 &&
            !(i == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined))
        {
            add(r.cityIndex, r.position, int(i), -1, nullptr);
        }
    }
    if (!g.SurfaceParty() || !g.SurfaceParty()->Joined(CompanionId::Ria))
    {
        add(0, kLiaPosition, -1, 0, nullptr);
    }
    add(0, kMaraPosition, -1, 1, nullptr);
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        for (size_t i = 0; i < g.SurfaceCities()->Traffic(city).Vehicles().size(); ++i)
        {
            const auto &traffic = g.SurfaceCities()->Traffic(city);
            const auto &car = traffic.Vehicles()[i];
            const auto count = surfaceTargets.size();
            add(city, car.position, -1, -1, &car);
            if (surfaceTargets.size() > count)
            {
                surfaceTargets.back().elevation += traffic.RenderElevation(i);
            }
        }
    }
}
void ClearPursuit();
int ObservedCity()
{
    const auto cities = Game::Get().SurfaceCities();
    return Game::Get().GetState().surfaceExploration && cities ? cities->ObservedCity() : -1;
}
bool PlayerPresent()
{
    if (Game::Get().GetState().aircraft.mounted)
    {
        return false; // Ground body contacts must not move the airborne authoritative pose.
    }
    const auto cities = Game::Get().SurfaceCities();
    return !Game::Get().GetState().surfaceExploration || (cities && cities->PlayerObserved());
}
bool ResidentPresent(const Resident &resident)
{
    const int city = ObservedCity();
    return city < 0 || resident.cityIndex == city;
}
bool LandmarksPresent()
{
    return ObservedCity() <= 0;
}
bool LandmarkPresent(size_t index)
{
    const auto *party = Game::Get().SurfaceParty();
    return LandmarksPresent() && !(party && ((index == 0 && party->Joined(CompanionId::Ria)) || index == 2));
}
bool OfficersPresent()
{
    return !Game::Get().GetState().surfaceExploration;
}
struct Footprint
{
    std::vector<Vector2> polygon;
    Vector2 minimum, maximum;
};
struct Obstacle
{
    Vector2 minimum, maximum;
    Vector2 origin;
    std::shared_ptr<std::vector<Footprint>> surfaces;
};
std::map<std::pair<int, int>, std::vector<Obstacle>> obstacleCells;
float EdgeDistance(Vector2 p, Vector2 a, Vector2 b)
{
    float x = b.x - a.x, y = b.y - a.y;
    float t = Game::Clamp(((p.x - a.x) * x + (p.y - a.y) * y) / (std::max)(.0000001f, x * x + y * y), 0, 1);
    return std::hypot(p.x - a.x - x * t, p.y - a.y - y * t);
}
bool Touches(const Footprint &shape, Vector2 p, float radius)
{
    if (p.x < shape.minimum.x - radius || p.x > shape.maximum.x + radius || p.y < shape.minimum.y - radius ||
        p.y > shape.maximum.y + radius)
    {
        return false;
    }
    bool positive = false, negative = false;
    float area = 0;
    for (size_t i = 0; i < shape.polygon.size(); ++i)
    {
        auto a = shape.polygon[i], b = shape.polygon[(i + 1) % shape.polygon.size()];
        if (EdgeDistance(p, a, b) <= radius)
        {
            return true;
        }
        float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        positive |= cross > .000001f;
        negative |= cross < -.000001f;
        area += a.x * b.y - a.y * b.x;
    }
    return std::fabs(area) > .000001f && !(positive && negative);
}
Obstacle ClipObstacle(const models::ResourceMesh &mesh, models::Point scale, float yaw)
{
    Obstacle obstacle{{1.e9f, 1.e9f}, {-1.e9f, -1.e9f}, {}, std::make_shared<std::vector<Footprint>>()};
    float c = std::cos(yaw), s = std::sin(yaw);
    for (size_t i = 0; i < mesh.vertices.size(); i += 3)
    {
        float z0 = mesh.vertices[i].z * scale.z, z1 = mesh.vertices[i + 1].z * scale.z,
              z2 = mesh.vertices[i + 2].z * scale.z;
        if ((std::max)({z0, z1, z2}) < .04f || (std::min)({z0, z1, z2}) > 1.f)
        {
            continue;
        }
        std::array<models::Point, 8> polygon, clipped;
        size_t count = 3;
        for (size_t k = 0; k < count; ++k)
        {
            const auto &v = mesh.vertices[i + k];
            polygon[k] = {v.x * scale.x * c - v.y * scale.y * s, v.x * scale.x * s + v.y * scale.y * c,
                          v.z * scale.z};
        }
        // A triangle clipped to the body's height band needs at most five points; no per-plane allocations.
        for (int plane = 0; plane < 2 && count; ++plane)
        {
            float boundary = plane == 0 ? .04f : 1.f;
            size_t out = 0;
            auto previous = polygon[count - 1];
            for (size_t k = 0; k < count; ++k)
            {
                auto current = polygon[k];
                bool a = plane == 0 ? previous.z >= boundary : previous.z <= boundary;
                bool b = plane == 0 ? current.z >= boundary : current.z <= boundary;
                if (a != b)
                {
                    float t = (boundary - previous.z) / (current.z - previous.z);
                    clipped[out++] = {previous.x + (current.x - previous.x) * t,
                                      previous.y + (current.y - previous.y) * t, boundary};
                }
                if (b)
                {
                    clipped[out++] = current;
                }
                previous = current;
            }
            count = out;
            std::copy_n(clipped.begin(), count, polygon.begin());
        }
        if (!count)
        {
            continue;
        }
        Footprint shape{{}, {1.e9f, 1.e9f}, {-1.e9f, -1.e9f}};
        shape.polygon.reserve(count);
        for (size_t k = 0; k < count; ++k)
        {
            const auto &v = polygon[k];
            shape.polygon.push_back({v.x, v.y});
            shape.minimum.x = (std::min)(shape.minimum.x, v.x);
            shape.minimum.y = (std::min)(shape.minimum.y, v.y);
            shape.maximum.x = (std::max)(shape.maximum.x, v.x);
            shape.maximum.y = (std::max)(shape.maximum.y, v.y);
        }
        obstacle.minimum.x = (std::min)(obstacle.minimum.x, shape.minimum.x);
        obstacle.minimum.y = (std::min)(obstacle.minimum.y, shape.minimum.y);
        obstacle.maximum.x = (std::max)(obstacle.maximum.x, shape.maximum.x);
        obstacle.maximum.y = (std::max)(obstacle.maximum.y, shape.maximum.y);
        obstacle.surfaces->push_back(std::move(shape));
    }
    return obstacle;
}
void BuildObstacles(const GameState &world)
{
    profiling::Scope startupTimer(profiling::Timer::MeshBuild);
    obstacleCells.clear();
    std::map<std::tuple<unsigned, float, float, float, float>, Obstacle> shapes;
    auto insert = [&](unsigned id, Vector2 position, models::Point scale, float yaw) {
        auto key = std::make_tuple(id, scale.x, scale.y, scale.z, yaw);
        auto found = shapes.find(key);
        if (found == shapes.end())
        {
            found = shapes.emplace(key, ClipObstacle(models::ResourceAssets::Get(id), scale, yaw)).first;
        }
        auto obstacle = found->second;
        if (obstacle.surfaces->empty())
        {
            return;
        }
        // Instances share local collision geometry, exactly as they share their render mesh.
        obstacle.origin = position;
        obstacle.minimum.x += position.x;
        obstacle.minimum.y += position.y;
        obstacle.maximum.x += position.x;
        obstacle.maximum.y += position.y;
        for (int x = int(std::floor(obstacle.minimum.x / 8)); x <= int(std::floor(obstacle.maximum.x / 8));
             ++x)
        {
            for (int y = int(std::floor(obstacle.minimum.y / 8));
                 y <= int(std::floor(obstacle.maximum.y / 8)); ++y)
            {
                obstacleCells[{x, y}].push_back(obstacle);
            }
        }
    };
    for (const auto &b : world.buildings)
    {
        const auto &model = models::ModelLibrary::Get().buildings[size_t(b.type)];
        insert(model.resourceId, {b.x, b.y}, {b.width, b.depth, b.height / 44}, 0);
    }
    for (const auto &p : Overworld::Scenery().Props())
    {
        if (p.solid && p.model->resourceId)
        {
            insert(p.model->resourceId, p.position, {1, 1, 1}, p.yaw);
        }
    }
}
constexpr Vector2 kPeople[] = {kLiaPosition, kMaraPosition, kCityKainPosition};
Vector2 Add(Vector2 a, Vector2 b, float scale = 1)
{
    return {a.x + b.x * scale, a.y + b.y * scale};
}
Vector2 Sub(Vector2 a, Vector2 b)
{
    return {a.x - b.x, a.y - b.y};
}
Vector2 Unit(Vector2 p)
{
    float length = std::hypot(p.x, p.y);
    return length > .0001f ? Vector2{p.x / length, p.y / length} : Vector2{1, 0};
}
unsigned Random()
{
    civic.randomState ^= civic.randomState << 13;
    civic.randomState ^= civic.randomState >> 17;
    civic.randomState ^= civic.randomState << 5;
    return civic.randomState;
}
float PlayerHeight()
{
    const auto &world = Game::Get().GetState();
    if (world.aircraft.mounted && world.aircraft.position)
    {
        return PlanetSecurity::Elevation(*world.aircraft.position);
    }
    return world.isBikeMounted ? world.bikeElevation : 0;
}
void Impact(BodyImpact &body, Vector2 impulse, bool dead = false)
{
    body.velocity = impulse;
    body.seconds = dead ? 2.f : .6f;
    body.invulnerability = .7f;
    body.response = dead                                   ? BodyResponse::Dead
                    : std::hypot(impulse.x, impulse.y) > 3 ? BodyResponse::Knockdown
                                                           : BodyResponse::Stagger;
}
bool SegmentClear(Vector2 a, Vector2 b, float radius = .3f)
{
    int steps = (std::max)(1, int(std::ceil(Game::Distance(a, b) / .2f)));
    for (int i = 0; i <= steps; ++i)
    {
        if (CivicJustice::Solid(Add(a, Sub(b, a), float(i) / steps), radius))
        {
            return false;
        }
    }
    return true;
}
// Bounded local emergency routing. Police may cross streets, but never buildings or solid props.
std::vector<Vector2> EmergencyRoute(Vector2 start, Vector2 target)
{
    profiling::Scope navigation(profiling::Timer::NpcNavigation);
    profiling::Count(profiling::Counter::NpcPathRequests);
    const bool surfaceMode = Game::Get().GetState().surfaceExploration;
    if (surfaceMode && Game::Distance(start, target) < 40 && SegmentClear(start, target))
    {
        return {target};
    }
    constexpr int size = 49, half = size / 2, count = size * size;
    auto position = [=](int index) {
        return Vector2{start.x + (index % size - half) * 2, start.y + (index / size - half) * 2};
    };
    int gx = int(std::round((target.x - start.x) / 2)) + half,
        gy = int(std::round((target.y - start.y) / 2)) + half;
    gx = (std::max)(1, (std::min)(size - 2, gx));
    gy = (std::max)(1, (std::min)(size - 2, gy));
    int goal = gy * size + gx, origin = half * size + half;
    std::vector<float> costs(count, 1.e9f);
    std::vector<int> parents(count, -1);
    using Candidate = std::pair<float, int>;
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> open;
    costs[origin] = 0;
    parents[origin] = origin;
    open.push({0.f, origin});
    int best = origin;
    float closest = Game::Distance(start, target);
    int expansions = 0;
    while (!open.empty() && ++expansions < (surfaceMode ? 256 : 2400))
    {
        int current = open.top().second;
        open.pop();
        auto p = position(current);
        float remaining = Game::Distance(p, target);
        if (remaining < closest)
        {
            closest = remaining;
            best = current;
        }
        if (current == goal || remaining < 1.5f)
        {
            best = current;
            break;
        }
        for (auto d : {Vector2{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}})
        {
            int x = current % size + int(d.x), y = current / size + int(d.y);
            if (x < 0 || y < 0 || x >= size || y >= size)
            {
                continue;
            }
            int next = y * size + x;
            float cost = costs[current] + std::hypot(d.x, d.y) * 2;
            if (cost >= costs[next] || !SegmentClear(p, position(next)))
            {
                continue;
            }
            parents[next] = current;
            costs[next] = cost;
            open.push({cost + Game::Distance(position(next), target), next});
        }
    }
    std::vector<Vector2> result;
    profiling::Count(profiling::Counter::NpcPathVisitedCells, expansions);
    for (int node = best; node != origin && node >= 0; node = parents[node])
    {
        result.push_back(position(node));
    }
    std::reverse(result.begin(), result.end());
    return result;
}
void AdvanceImpact(Vector2 &position, BodyImpact &body, float dt, bool applyDisplacement = true)
{
    body.invulnerability = (std::max)(0.f, body.invulnerability - dt);
    if (body.seconds > 0)
    {
        if (applyDisplacement)
        {
            auto next = Add(position, body.velocity, dt);
            if (SegmentClear(position, next, .18f))
            {
                position = next;
            }
        }
        body.velocity.x *= std::exp(-7 * dt);
        body.velocity.y *= std::exp(-7 * dt);
        body.seconds = (std::max)(0.f, body.seconds - dt);
    }
    if (body.seconds <= 0 && body.response != BodyResponse::Dead)
    {
        body.response = BodyResponse::None;
    }
}
bool CarContains(const TrafficVehicle &car, Vector2 position, float radius)
{
    Vector2 d = {std::cos(car.yaw), std::sin(car.yaw)}, n = {-d.y, d.x}, q = Sub(position, car.position);
    const auto &model = models::ModelLibrary::Get().vehicles[size_t(car.id % 4)].body;
    const auto &mesh = models::ResourceAssets::Get(model.resourceId);
    float halfX = mesh.physicalSize.x * .5f, halfY = mesh.physicalSize.y * .5f;
    float x = q.x * d.x + q.y * d.y, y = q.x * n.x + q.y * n.y;
    x -= (mesh.minimum.x + mesh.maximum.x) * .5f;
    y -= (mesh.minimum.y + mesh.maximum.y) * .5f;
    float dx = (std::max)(0.f, std::fabs(x) - halfX), dy = (std::max)(0.f, std::fabs(y) - halfY);
    return dx * dx + dy * dy < radius * radius;
}
Vector2 BikeClosest(Vector2 p)
{
    const auto &world = Game::Get().GetState();
    Vector2 d = {std::cos(world.bikeYaw), std::sin(world.bikeYaw)};
    auto q = Sub(p, world.bikePosition);
    float along = Game::Clamp(q.x * d.x + q.y * d.y, -.4023f, .4023f);
    return Add(world.bikePosition, d, along);
}
bool BikeTouches(Vector2 p, float radius)
{
    return Game::Distance(p, BikeClosest(p)) < .22271f + radius;
}
void DeflectBike(Vector2 normal)
{
    auto &world = Game::Get().GetState();
    if (!world.isBikeMounted)
    {
        return;
    }
    normal = Unit(normal);
    Vector2 velocity = world.bikeVelocity;
    if (std::hypot(velocity.x, velocity.y) < .001f)
    {
        velocity = {std::cos(world.bikeYaw) * world.bikeSpeed, std::sin(world.bikeYaw) * world.bikeSpeed};
    }
    float inward = velocity.x * normal.x + velocity.y * normal.y;
    if (inward >= 0)
    {
        return;
    }
    velocity = Add(velocity, normal, -inward);
    world.bikeVelocity = velocity;
    float speed = std::hypot(velocity.x, velocity.y), sign = world.bikeSpeed < 0 ? -1.f : 1.f;
    world.bikeSpeed = sign * speed;
    if (speed > .05f)
    {
        world.bikeYaw = std::atan2(velocity.y * sign, velocity.x * sign);
    }
}
void SeparateBikeFromPerson(Vector2 other)
{
    auto &world = Game::Get().GetState();
    auto point = BikeClosest(other), normal = Unit(Sub(point, other));
    float overlap = .22271f + .18f - Game::Distance(point, other);
    if (overlap > 0)
    {
        auto desired = Add(world.bikePosition, normal, overlap + .002f);
        world.bikePosition = BodyCollision::Slide(world.bikePosition, desired,
                                                  [&](Vector2 p) { return CivicJustice::Solid(p, .23f); });
        world.playerPosition = world.bikePosition;
    }
    DeflectBike(normal);
}
bool SweptCarContains(const TrafficVehicle &car, Vector2 p, float radius)
{
    float travel = Game::Distance(car.previousPosition, car.position);
    if (Game::Distance(p, car.position) > 4 + travel)
    {
        return false;
    }
    int steps = (std::max)(1, int(std::ceil(travel / .08f)));
    auto sample = car;
    for (int step = 0; step <= steps; ++step)
    {
        float t = float(step) / steps;
        sample.position = Add(car.previousPosition, Sub(car.position, car.previousPosition), t);
        sample.yaw = car.previousYaw + std::remainder(car.yaw - car.previousYaw, 2 * kPi) * t;
        if (CarContains(sample, p, radius))
        {
            return true;
        }
    }
    return false;
}
Vector2 PushFromCar(const TrafficVehicle &car, Vector2 p, float radius, float altitude = 0)
{
    auto &mesh =
        models::ResourceAssets::Get(models::ModelLibrary::Get().vehicles[size_t(car.id % 4)].body.resourceId);
    Vector2 d = {std::cos(car.yaw), std::sin(car.yaw)}, n = {-d.y, d.x};
    Vector2 center = Add(Add(car.position, d, (mesh.minimum.x + mesh.maximum.x) * .5f), n,
                         (mesh.minimum.y + mesh.maximum.y) * .5f);
    auto q = Sub(p, center);
    float x = q.x * d.x + q.y * d.y, y = q.x * n.x + q.y * n.y, hx = mesh.physicalSize.x * .5f,
          hy = mesh.physicalSize.y * .5f;
    Vector2 result = p;
    float best = 1.e9f;
    for (float side : {-1.f, 1.f})
    {
        Vector2 candidates[] = {Add(Add(center, d, Game::Clamp(x, -hx, hx)), n, side * (hy + radius + .04f)),
                                Add(Add(center, n, Game::Clamp(y, -hy, hy)), d, side * (hx + radius + .04f))};
        for (auto candidate : candidates)
        {
            if (Game::Distance(candidate, p) < best && !CarContains(car, candidate, radius) &&
                SegmentClear(p, candidate, radius))
            {
                best = Game::Distance(candidate, p);
                result = candidate;
            }
        }
    }
    if (best == 1.e9f)
    {
        // A car may already have pushed the body into a wall; the segment's starting point
        // is then blocked, so a segment-only test can never find an exit.
        result = BodyCollision::Slide(p, p, [&](Vector2 point) {
            return CarContains(car, point, radius + .002f) || CivicJustice::Solid(point, radius, altitude);
        });
    }
    return result;
}
Vector2 Separate(Vector2 from, Vector2 to, float radius, int ignoreResident, bool player,
                 int ignoreOfficer = -1)
{
    auto &world = Game::Get().GetState();
    const auto &traffic = Overworld::Traffic();
    std::array<const TrafficVehicle *, TrafficSystem::kVehicleLimit> nearby{};
    size_t nearbyCount = 0;
    const float searchRadius = 8 + Game::Distance(from, to);
    for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
    {
        const auto &car = traffic.Vehicles()[i];
        if (Game::Distance(car.position, from) < searchRadius &&
            std::fabs(traffic.RenderElevation(i) - (player ? PlayerHeight() : 0)) < .7f)
        {
            nearby[nearbyCount++] = &car;
        }
    }
    auto blocked = [&](Vector2 p) {
        if (player ? Game::Get().IsPositionBlocked(p) : CivicJustice::Solid(p, radius))
        {
            return true;
        }
        for (size_t i = 0; i < nearbyCount; ++i)
        {
            if (CarContains(*nearby[i], p, radius))
            {
                return true;
            }
        }
        return false;
    };
    Vector2 result = from;
    int steps = (std::max)(1, int(std::ceil(Game::Distance(from, to) / .12f)));
    for (int step = 1; step <= steps; ++step)
    {
        Vector2 candidate = BodyCollision::Slide(result, Add(result, Sub(to, from), 1.f / steps), blocked);
        if (PlayerHeight() < .7f || !player)
        {
            auto separate = [&](Vector2 other, float otherRadius) {
                auto delta = Sub(candidate, other);
                float distance = std::hypot(delta.x, delta.y), minimum = radius + otherRadius;
                if (distance < minimum)
                {
                    candidate = Add(other, Unit(distance < .0001f ? Sub(from, other) : delta), minimum);
                }
            };
            for (size_t index = 0; index < world.residents.size(); ++index)
            {
                if (int(index) != ignoreResident && ResidentPresent(world.residents[index]) &&
                    world.residents[index].health > 0 &&
                    !(index == Overworld::kKainResidentSlot &&
                      game::GameLevels::Chapter().GetState().kainJoined))
                {
                    separate(world.residents[index].position, .18f);
                }
            }
            for (size_t index = 0; index < std::size(kPeople); ++index)
            {
                if (LandmarkPresent(index) && civic.landmarkHealth[index] > 0 &&
                    (index != 2 || game::GameLevels::Chapter().GetState().kainJoined))
                {
                    separate(kPeople[index], .18f);
                }
            }
            for (size_t index = 0; index < civic.officers.size(); ++index)
            {
                const auto &officer = civic.officers[index];
                if (OfficersPresent() && officer.health > 0 && int(index) != ignoreOfficer)
                {
                    separate(officer.position, .18f);
                }
            }
            if (!player && PlayerPresent() && !world.Mounted())
            {
                separate(world.playerPosition, .18f);
            }
        }
        // Crowd separation must not leave the character inside a wall or another vehicle.
        if (blocked(candidate))
        {
            candidate = BodyCollision::Slide(candidate, candidate, blocked);
        }
        if (blocked(candidate))
        {
            break;
        }
        result = candidate;
    }
    return result;
}
void TransferToHospital()
{
    auto &world = Game::Get().GetState();
    world.car.mounted = false;
    if (world.aircraft.mounted && !world.aircraft.grounded)
    {
        world.aircraft.health = 0; // An evacuated pilot must not leave an immortal craft in mid-air.
    }
    world.aircraft.mounted = false;
    world.aircraft.Stop();
    auto &mine = game::GameLevels::Mine().GetState();
    auto &stats = mine.player;
    civic.lostLevels =
        stats.level > 1 ? (std::max)(1, int(std::ceil(stats.level * HospitalRecovery::kLevelLoss))) : 0;
    stats.level = (std::max)(1, stats.level - civic.lostLevels);
    stats.experience = game::MineLevel::ExperienceForLevel(stats.level);
    stats.maxHealth = 100 + (stats.level - 1) * 20;
    stats.attack = 18 + (stats.level - 1) * 4;
    stats.defense = 2 + (stats.level - 1) * 2;
    auto take = [](int &asset) {
        int amount = asset > 0 ? (std::max)(1, int(std::floor(asset * HospitalRecovery::kAssetLoss))) : 0;
        amount = (std::min)(asset, amount);
        asset -= amount;
        return amount;
    };
    civic.lostCredits = take(stats.credits);
    civic.lostSalvage = take(stats.salvage);
    stats.health = stats.maxHealth;
    mine.active = false;
    mine.bullets.clear();
    mine.tracers.clear();
    auto &chapter = game::GameLevels::Chapter().GetState();
    chapter.active = false;
    chapter.bullets.clear();
    chapter.tracers.clear();
    auto hospital =
        civic.hospitals.empty() ? HospitalSite{0, kHomeDoor} : civic.hospitals[size_t(civic.hospital)];
    world.isInsideHome = world.isInObservatory = world.isBikeMounted = world.mapVisible = world.mouseFiring =
        world.mouseAiming = false;
    world.StopMotion();
    bool transported = true;
    if (world.surfaceExploration)
    {
        // Keep planetary coordinates and the parked bike; never install legacy city XY into the surface
        // frame.
        if (!Game::Get().RecoverSurfaceAt(hospital.city, hospital.position))
        {
            mine.active = Game::Get().IsSurfaceMine();
            transported = false;
        }
    }
    else
    {
        world.playerPosition = world.cameraPosition = hospital.position;
        world.bikeElevation = 0;
        world.bikePosition = hospital.position;
        for (Vector2 offset : {Vector2{1.5f, 0}, {-1.5f, 0}, {0, 1.5f}, {0, -1.5f}})
        {
            if (!CivicJustice::Solid(Add(hospital.position, offset), .65f))
            {
                world.bikePosition = Add(hospital.position, offset);
                break;
            }
        }
    }
    std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
    civic.heat = 0;
    ClearPursuit();
    civic.officers.clear();
    planetary.officers.clear();
    civic.bullets.clear();
    civic.tracers.clear();
    civic.airBullets.clear();
    civic.airTracers.clear();
    civic.playerImpact = {};
    civic.playerGrace = 5;
    mine.invulnerability = 5;
    ++civic.deaths;
    Game::Get().ShowNotice(
        (transported
             ? (Game::Get().IsStoryWorld() ? std::string(Overworld::Cities()[hospital.city].name) + " 의료소"
                                           : Game::Get().Surface()->Planet().name + " 착륙 지원소") +
                   " / 레벨 -"
             : "의료 지원 / 의료소 도착 지점이 막혀 현장에서 회복했습니다. 레벨 -") +
        std::to_string(civic.lostLevels) + " · 크레딧 -" + std::to_string(civic.lostCredits) +
        " · 거래 부품 -" + std::to_string(civic.lostSalvage) +
        ". 조사 기록은 유지되며 보안망의 평판은 남습니다." +
        (world.dialogue.suspended ? " J 기록에서 Enter로 중단한 대화를 재개할 수 있습니다." : ""));
}
} // namespace

// ============================================================================
// PlanetSecurity — main-thread spherical authority and bounded local projections
// ============================================================================
PlanetSecurityState &PlanetSecurity::State()
{
    return planetary;
}
void PlanetSecurity::Reset()
{
    planetary = {};
    shotCollision.Clear();
    reportedPoint.reset();
    surfaceTargets.clear();
}
void PlanetSecurity::Migrate(PlanetSecurityState &s, const CivicState &law,
                             const world::PlanetDefinition &planet, const world::TangentFrame &frame)
{
    if (!s.worldKey.empty())
    {
        return;
    }
    s.worldKey = world::WorldIdentity::ContentKey(planet, "overworld", "security_v1");
    s.frame = frame;
    auto point = [&](Vector2 p) {
        return world::SurfaceGeometry::FromLocal(frame, {p.x * 2, p.y * 2, 0}, planet.radiusMeters);
    };
    if (law.wanted > 0)
    {
        s.incident = s.search = point(law.lastCrime);
    }
    for (const auto &o : law.officers)
    {
        PlanetOfficer record;
        record.id = o.id;
        record.position = point(o.position);
        record.lastKnown = point(o.lastKnown);
        record.heading = world::SurfaceGeometry::Unit(
            {frame.right.x * std::cos(o.facing.yaw) + frame.forward.x * std::sin(o.facing.yaw),
             frame.right.y * std::cos(o.facing.yaw) + frame.forward.y * std::sin(o.facing.yaw),
             frame.right.z * std::cos(o.facing.yaw) + frame.forward.z * std::sin(o.facing.yaw)});
        record.jurisdiction = "overworld";
        s.officers.push_back(std::move(record));
    }
}
void PlanetSecurity::Validate(const PlanetSecurityState &s, const CivicState &law,
                              const world::PlanetDefinition &planet)
{
    if (s.worldKey.empty())
    {
        if (!s.officers.empty() || s.incident || s.search)
        {
            throw std::runtime_error("치안 기록의 월드 키가 없습니다.");
        }
        return;
    }
    if (s.worldKey != world::WorldIdentity::ContentKey(planet, "overworld", "security_v1") ||
        s.officers.size() != law.officers.size() || s.officers.size() > 16)
    {
        throw std::runtime_error("행성 보안대의 소유권/인원 기록이 다릅니다.");
    }
    world::SurfaceGeometry::Validate(s.frame);
    if (s.incident)
    {
        world::SurfaceGeometry::Validate(*s.incident);
    }
    if (s.search)
    {
        world::SurfaceGeometry::Validate(*s.search);
    }
    if (law.wanted > 0 && (!s.incident || !s.search))
    {
        throw std::runtime_error("수배 사건 위치가 없습니다.");
    }
    std::set<std::uint64_t> ids;
    for (const auto &o : s.officers)
    {
        world::SurfaceGeometry::Validate(o.position);
        world::SurfaceGeometry::Validate(o.lastKnown);
        if (!ids.insert(o.id).second || o.jurisdiction.size() > 128 || !std::isfinite(o.heading.x) ||
            !std::isfinite(o.heading.y) || !std::isfinite(o.heading.z) ||
            std::abs(world::SurfaceGeometry::Dot(o.heading, o.heading) - 1) > .01 ||
            std::none_of(law.officers.begin(), law.officers.end(),
                         [&](const auto &v) { return v.id == o.id; }))
        {
            throw std::runtime_error("보안대 위치/방향/ID가 올바르지 않습니다.");
        }
    }
}
world::SurfacePoint PlanetSecurity::Point(Vector2 p)
{
    auto &g = Game::Get();
    if (g.SurfaceCities() && g.SurfaceCities()->Observing())
    {
        return *g.SurfaceCities()->Point(*g.Surface(), g.SurfaceCities()->ObservedCity(), p);
    }
    if (game::GameLevels::Chapter().Surface().Observing())
    {
        return game::GameLevels::Chapter().Surface().Point(p);
    }
    return g.Surface()->PointAt(p.x * 2, p.y * 2);
}
Vector2 PlanetSecurity::Local(const world::SurfacePoint &p)
{
    const auto &s = *Game::Get().Surface();
    auto local = world::SurfaceAtlas::Project(s.Origin(), p, s.Planet().radiusMeters);
    return {float(local.x / 2), float(local.y / 2)};
}
float PlanetSecurity::Elevation(const world::SurfacePoint &p)
{
    return float((p.altitudeMeters - Game::Get().Surface()->Origin().origin.altitudeMeters) / 2);
}
void PlanetSecurity::Project()
{
    profiling::Scope profile(profiling::Timer::PlanetSecurity);
    auto &g = Game::Get();
    if (!g.GetState().surfaceExploration || !g.Surface() || !g.Surface()->Ready())
    {
        return;
    }
    const auto &s = *g.Surface();
    Migrate(planetary, civic, s.Planet(), s.Origin());
    navigationBudget = 2;
    using G = world::SurfaceGeometry;
    const bool changed =
        G::Distance(s.Planet().radiusMeters, planetary.frame.origin, s.Origin().origin) > .001 ||
        std::abs(planetary.frame.origin.altitudeMeters - s.Origin().origin.altitudeMeters) > .001 ||
        G::Dot(planetary.frame.right, s.Origin().right) < 1 - 1.e-10;
    if (changed)
    {
        auto convert = [&](models::Point p) {
            auto point = G::FromLocal(planetary.frame, {p.x * 2, p.y * 2, p.z * 2}, s.Planet().radiusMeters);
            auto local = world::SurfaceAtlas::Project(s.Origin(), point, s.Planet().radiusMeters);
            return models::Point{float(local.x / 2), float(local.y / 2), float(local.z / 2)};
        };
        const bool transport =
            G::Distance(s.Planet().radiusMeters, planetary.frame.origin, s.Origin().origin) > 1000;
        if (transport || !CivicJustice::IsExterior())
        {
            civic.bullets.clear();
            civic.airBullets.clear();
            civic.tracers.clear();
            civic.airTracers.clear();
        }
        else
        {
            for (auto &b : civic.bullets)
            {
                auto p = convert({b.position.x, b.position.y, b.elevation});
                b.position = {p.x, p.y};
                b.elevation = p.z;
                auto d =
                    G::TransformDirection({b.direction.x, b.direction.y, 0}, planetary.frame, s.Origin());
                b.direction = {float(d.x), float(d.y)};
            }
            for (auto &t : civic.tracers)
            {
                auto a = convert({t.start.x, t.start.y, t.elevation});
                auto b = convert({t.end.x, t.end.y, t.elevation});
                t.start = {a.x, a.y};
                t.end = {b.x, b.y};
                t.elevation = a.z;
            }
            for (auto &b : civic.airBullets)
            {
                b.position = convert(b.position);
                auto d = G::TransformDirection({b.direction.x, b.direction.y, b.direction.z}, planetary.frame,
                                               s.Origin());
                b.direction = {float(d.x), float(d.y), float(d.z)};
            }
            for (auto &t : civic.airTracers)
            {
                t.start = convert(t.start);
                t.end = convert(t.end);
            }
        }
        for (auto &o : civic.officers)
        {
            auto velocity = G::TransformDirection({o.impact.velocity.x, o.impact.velocity.y, 0},
                                                  planetary.frame, s.Origin());
            o.impact.velocity = {float(velocity.x), float(velocity.y)};
            o.route.clear();
            o.waypoint = 0;
            o.navigationSeconds = 0;
        }
    }
    planetary.frame = s.Origin();
    for (auto &o : civic.officers)
    {
        auto record = std::find_if(planetary.officers.begin(), planetary.officers.end(),
                                   [&](const auto &r) { return r.id == o.id; });
        if (record == planetary.officers.end())
        {
            continue;
        }
        o.position = Local(record->position);
        o.lastKnown = Local(record->lastKnown);
        o.facing.yaw = float(std::atan2(G::Dot(record->heading, s.Origin().forward),
                                        G::Dot(record->heading, s.Origin().right)));
    }
    if (planetary.search)
    {
        civic.lastCrime = Local(*planetary.search);
    }
    if (!CivicJustice::IsExterior())
    {
        civic.bullets.clear();
        civic.airBullets.clear();
        civic.tracers.clear();
        civic.airTracers.clear();
    }
    PrepareSurfaceTargets();
}
void PlanetSecurity::Capture()
{
    const auto *session = Game::Get().Surface();
    if (!session || planetary.worldKey.empty())
    {
        return;
    }
    const auto &frame = session->Origin();
    for (const auto &o : civic.officers)
    {
        auto record = std::find_if(planetary.officers.begin(), planetary.officers.end(),
                                   [&](const auto &r) { return r.id == o.id; });
        if (record == planetary.officers.end() ||
            Game::Distance(o.position, Game::Get().GetState().playerPosition) > 400)
        {
            continue;
        }
        record->position = session->PointAt(o.position.x * 2, o.position.y * 2);
        record->position.altitudeMeters = session->Stream().Contact(record->position).heightMeters;
        if (Game::Distance(o.lastKnown, Game::Get().GetState().playerPosition) < 1000)
        {
            record->lastKnown = session->PointAt(o.lastKnown.x * 2, o.lastKnown.y * 2);
        }
        record->heading = world::SurfaceGeometry::Unit(
            {frame.right.x * std::cos(o.facing.yaw) + frame.forward.x * std::sin(o.facing.yaw),
             frame.right.y * std::cos(o.facing.yaw) + frame.forward.y * std::sin(o.facing.yaw),
             frame.right.z * std::cos(o.facing.yaw) + frame.forward.z * std::sin(o.facing.yaw)});
    }
    planetary.officers.erase(std::remove_if(planetary.officers.begin(), planetary.officers.end(),
                                            [&](const auto &r) {
                                                return std::none_of(
                                                    civic.officers.begin(), civic.officers.end(),
                                                    [&](const auto &o) { return r.id == o.id; });
                                            }),
                             planetary.officers.end());
}
void PlanetSecurity::Report(Offense offense, const world::SurfacePoint &p)
{
    const auto previous = reportedPoint;
    reportedPoint = p;
    CivicJustice::Report(offense, Local(p));
    reportedPoint = previous;
}
void PlanetSecurity::SawPlayer()
{
    planetary.search = Game::Get().Surface()->Pose().origin;
}
bool PlanetSecurity::CanNavigate()
{
    if (navigationBudget <= 0)
    {
        return false;
    }
    --navigationBudget;
    return true;
}
bool PlanetSecurity::BodyBlocked(Vector2 p, double radius)
{
    auto &g = Game::Get();
    auto point = g.Surface()->PointAt(p.x * 2, p.y * 2);
    return !g.Surface()->LandingClear(point, radius) ||
           (g.SurfaceCities() && g.SurfaceCities()->OccupiedAt(g.GetState(), *g.Surface(), point)) ||
           (g.SurfaceTraffic() && g.SurfaceTraffic()->Blocked(point, radius)) ||
           (g.SurfaceParty() && g.SurfaceParty()->Blocked(point, radius)) ||
           (g.SurfaceCommunity() && g.SurfaceCommunity()->Blocked(point, radius));
}
bool PlanetSecurity::OfficerBlocked(const world::SurfacePoint &point, double radius)
{
    const auto *surface = Game::Get().Surface();
    if (!surface)
    {
        return false;
    }
    for (const auto &record : planetary.officers)
    {
        auto officer = std::find_if(civic.officers.begin(), civic.officers.end(),
                                    [&](const auto &o) { return o.id == record.id; });
        if (officer != civic.officers.end() && officer->health > 0 &&
            world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, point, record.position) <
                radius + .32)
        {
            return true;
        }
    }
    return false;
}
bool PlanetSecurity::ShotBlocked(models::Point p)
{
    auto &g = Game::Get();
    auto point = g.Surface()->PointAt(p.x * 2, p.y * 2);
    point.altitudeMeters = g.Surface()->Origin().origin.altitudeMeters + p.z * 2;
    return point.altitudeMeters <= g.Surface()->Stream().Contact(point).heightMeters + .03 ||
           shotCollision.Blocked(*g.Surface()->Stream().Snapshot()->planet, point, .04, true);
}
bool PlanetSecurity::Spawn()
{
    using G = world::SurfaceGeometry;
    auto &g = Game::Get();
    if (!planetary.search || civic.officers.size() >= 16)
    {
        return false;
    }
    auto frame = G::Frame(*planetary.search);
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        double angle = double(Random() % 6283) * .001, range = 40 + double(Random() % 33);
        auto p = G::Advance(frame, std::cos(angle) * range, std::sin(angle) * range,
                            g.Surface()->Planet().radiusMeters)
                     .origin;
        auto local = Local(p);
        if (!g.Surface()->LandingClear(p, .65) ||
            (CivicJustice::IsExterior() &&
             G::Distance(g.Surface()->Planet().radiusMeters, p, g.Surface()->Pose().origin) < 1000 &&
             !TrafficSystem::OutsideView(local, g.GetState().cameraPosition, 90)) ||
            (g.SurfaceTraffic() && g.SurfaceTraffic()->Blocked(p, .8)) ||
            (g.SurfaceCities() && g.SurfaceCities()->OccupiedAt(g.GetState(), *g.Surface(), p)) ||
            (g.SurfaceParty() && g.SurfaceParty()->Blocked(p, .8)) ||
            (g.SurfaceCommunity() && g.SurfaceCommunity()->Blocked(p, .8)))
        {
            continue;
        }
        if (std::any_of(planetary.officers.begin(), planetary.officers.end(), [&](const auto &o) {
                return G::Distance(g.Surface()->Planet().radiusMeters, o.position, p) < 2;
            }))
        {
            continue;
        }
        p.altitudeMeters = g.Surface()->Stream().Contact(p).heightMeters;
        SecurityOfficer o;
        o.id = civic.nextOfficer++;
        o.position = local;
        o.lastKnown = Local(*planetary.search);
        o.facing.yaw = std::atan2(o.lastKnown.y - local.y, o.lastKnown.x - local.x);
        o.tier = civic.wanted;
        o.health = 60 + 40 * o.tier + 8 * (game::GameLevels::Mine().GetState().player.level - 1);
        PlanetOfficer record;
        record.id = o.id;
        record.position = p;
        record.lastKnown = *planetary.search;
        record.heading = G::Unit({g.Surface()->Origin().right.x * std::cos(o.facing.yaw) +
                                      g.Surface()->Origin().forward.x * std::sin(o.facing.yaw),
                                  g.Surface()->Origin().right.y * std::cos(o.facing.yaw) +
                                      g.Surface()->Origin().forward.y * std::sin(o.facing.yaw),
                                  g.Surface()->Origin().right.z * std::cos(o.facing.yaw) +
                                      g.Surface()->Origin().forward.z * std::sin(o.facing.yaw)});
        auto location = g.Surface()->Stream().Snapshot()->planet;
        record.jurisdiction = "overworld";
        for (const auto &region : location->regions)
        {
            if (G::Distance(g.Surface()->Planet().radiusMeters, region.center.position, p) <
                region.boundingRadiusMeters)
            {
                record.jurisdiction = region.definition.id;
                break;
            }
        }
        planetary.officers.push_back(std::move(record));
        civic.officers.push_back(std::move(o));
        if (!civic.responseStarted)
        {
            civic.responseStarted = true;
            civic.initialUnseenSeconds = 0;
        }
        return true;
    }
    return false;
}
bool PlanetSecurity::Hit(models::Point p, int damage, bool playerOwned, bool companionOwned)
{
    auto &g = Game::Get();
    auto point = Point({p.x, p.y});
    point.altitudeMeters = (game::GameLevels::Chapter().Surface().Observing() ||
                            (g.SurfaceCities() && g.SurfaceCities()->Observing()))
                               ? point.altitudeMeters + p.z * 2
                               : g.Surface()->Origin().origin.altitudeMeters + p.z * 2;
    Vector2 local = Local(point);
    float z = Elevation(point);
    auto &aircraft = g.GetState().aircraft;
    if (aircraft.Contains(*g.Surface(), point, .06))
    {
        aircraft.health = (std::max)(0, aircraft.health - damage);
        return true;
    }
    auto &car = g.GetState().car;
    if (car.Contains(*g.Surface(), point, .03f, true))
    {
        car.health = (std::max)(0, car.health - damage);
        return true;
    }
    auto body = [&](Vector2 q, float ground) {
        return Game::Distance(local, q) < .35f && z >= ground && z <= ground + .95f;
    };
    if (!g.GetState().aircraft.mounted && !playerOwned && !companionOwned &&
        body(g.GetState().playerPosition, Elevation(g.Surface()->Pose().origin)))
    {
        CivicCombat::DamagePlayer(damage);
        return true;
    }
    if (playerOwned || companionOwned)
    {
        for (size_t i = 0; i < civic.officers.size(); ++i)
        {
            const auto &o = civic.officers[i];
            auto record = std::find_if(planetary.officers.begin(), planetary.officers.end(),
                                       [&](const auto &r) { return r.id == o.id; });
            if (record != planetary.officers.end() && body(o.position, Elevation(record->position)) &&
                CivicCombat::DamageOfficer(i, damage, {}, playerOwned))
            {
                return true;
            }
        }
    }
    if (!companionOwned && g.SurfaceParty())
    {
        for (size_t i = 0; i < g.SurfaceParty()->State().members.size(); ++i)
        {
            const auto &m = g.SurfaceParty()->State().members[i];
            if (m.joined && m.place != CompanionPlace::Mine && m.combat.health > 0 &&
                body(Local(m.position), Elevation(m.position)))
            {
                g.SurfaceParty()->Damage(CompanionId(i), damage, playerOwned);
                return true;
            }
        }
    }
    if (g.SurfaceCommunity() && g.SurfaceCommunity()->Hit(point, damage, playerOwned))
    {
        return true;
    }
    for (const auto &target : surfaceTargets)
    {
        if (target.vehicleBody)
        {
            if (Game::Distance(local, target.position) > 8)
            {
                continue;
            }
            auto delta = Sub(local, target.position);
            float c = std::cos(target.yaw), s = std::sin(target.yaw);
            const float x = delta.x * c + delta.y * s, y = -delta.x * s + delta.y * c;
            if (x < target.minimum.x || x > target.maximum.x || y < target.minimum.y ||
                y > target.maximum.y || z < target.elevation + target.minimum.z ||
                z > target.elevation + target.maximum.z)
            {
                continue;
            }
            g.SurfaceCities()->Observe(g.GetState(), *g.Surface(), target.city, [&] {
                if (auto *traffic = g.SurfaceCities()->ActiveTraffic())
                {
                    traffic->DamageVehicle(target.vehicle, damage, playerOwned);
                }
            });
            return true;
        }
        if (!body(target.position, target.elevation))
        {
            continue;
        }
        if (target.resident >= 0 &&
            CivicCombat::DamageResident(size_t(target.resident), damage, {}, playerOwned))
        {
            return true;
        }
        if (target.landmark >= 0 && CivicCombat::DamageLandmark(size_t(target.landmark), damage, playerOwned))
        {
            return true;
        }
    }
    if (auto *traffic = g.SurfaceTraffic())
    {
        for (const auto &actor : traffic->State().actors)
        {
            if (traffic->Contains(actor, point, .04))
            {
                traffic->Damage(actor.id, damage, playerOwned);
                return true;
            }
        }
    }
    return false;
}
void PlanetSecurity::UpdateCompanions(float seconds)
{
    auto &g = Game::Get();
    if (seconds <= 0 || !CivicJustice::IsExterior() || !g.SurfaceParty() || civic.wanted == 0 ||
        !civic.identified)
    {
        return;
    }
    for (size_t i = 0; i < g.SurfaceParty()->State().members.size(); ++i)
    {
        const auto &m = g.SurfaceParty()->State().members[i];
        if (!m.joined || m.place != CompanionPlace::Surface || m.combat.health <= 0)
        {
            continue;
        }
        Vector2 p = Local(m.position);
        const SecurityOfficer *target = nullptr;
        float nearest = 8;
        for (const auto &o : civic.officers)
        {
            const float d = Game::Distance(p, o.position);
            if (o.health > 0 && d < nearest && CivicJustice::ClearLine(p, o.position))
            {
                target = &o;
                nearest = d;
            }
        }
        if (target)
        {
            g.SurfaceParty()->Defend(CompanionId(i),
                                     std::atan2(target->position.y - p.y, target->position.x - p.x));
        }
    }
}

CivicState &CivicJustice::State()
{
    return civic;
}
bool CivicJustice::IsExterior()
{
    const auto &world = Game::Get().GetState();
    return !world.isInsideHome && !world.isInObservatory &&
           (world.surfaceExploration
                ? Game::Get().Surface() && Game::Get().Surface()->Ready() && !Game::Get().IsSurfaceMine()
                : !game::GameLevels::Active());
}
bool CivicJustice::Solid(Vector2 p, float radius, float altitude, bool driving)
{
    auto &g = Game::Get();
    if (g.GetState().surfaceExploration && g.Surface() && g.Surface()->Ready() &&
        g.GetState().aircraft.position && CivicJustice::IsExterior())
    {
        auto point = PlanetSecurity::Point(p);
        point.altitudeMeters += altitude * 2;
        if (g.GetState().aircraft.Contains(*g.Surface(), point, radius * 2 + .15))
        {
            return true;
        }
    }
    if (Game::Get().GetState().surfaceExploration && ObservedCity() < 0 &&
        !game::GameLevels::Chapter().Surface().Observing())
    {
        return PlanetSecurity::BodyBlocked(p, radius * 2);
    }
    if (TransitLayout::IsBlocked(p, radius, altitude, driving))
    {
        return true;
    }
    if (p.x - radius < kWorldMinX || p.y - radius < kWorldMinY || p.x + radius > kWorldMaxX ||
        p.y + radius > kWorldMaxY)
    {
        return true;
    }
    for (int x = int(std::floor((p.x - radius) / 8)); x <= int(std::floor((p.x + radius) / 8)); ++x)
    {
        for (int y = int(std::floor((p.y - radius) / 8)); y <= int(std::floor((p.y + radius) / 8)); ++y)
        {
            auto cell = obstacleCells.find({x, y});
            if (cell == obstacleCells.end())
            {
                continue;
            }
            for (const auto &b : cell->second)
            {
                if (p.x > b.minimum.x - radius && p.x < b.maximum.x + radius && p.y > b.minimum.y - radius &&
                    p.y < b.maximum.y + radius)
                {
                    for (const auto &shape : *b.surfaces)
                    {
                        if (Touches(shape, {p.x - b.origin.x, p.y - b.origin.y}, radius))
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}
bool CivicJustice::ClearLine(Vector2 from, Vector2 to)
{
    if (Game::Get().GetState().surfaceExploration && ObservedCity() < 0 &&
        !game::GameLevels::Chapter().Surface().Observing())
    {
        const auto &surface = *Game::Get().Surface();
        const float eye = models::WorldScale::aimHeightPixels / 44;
        float a = PlanetSecurity::Elevation(surface.Pose().origin);
        auto start = surface.PointAt(from.x * 2, from.y * 2), end = surface.PointAt(to.x * 2, to.y * 2);
        start.altitudeMeters = surface.Stream().Contact(start).heightMeters;
        end.altitudeMeters = surface.Stream().Contact(end).heightMeters;
        a = PlanetSecurity::Elevation(start) + eye;
        float b = PlanetSecurity::Elevation(end) + eye;
        const int steps = (std::max)(1, int(std::ceil(Game::Distance(from, to) / .4f)));
        for (int i = 0; i <= steps; ++i)
        {
            const float t = float(i) / steps;
            auto p = Add(from, Sub(to, from), t);
            if (PlanetSecurity::ShotBlocked({p.x, p.y, a + (b - a) * t}))
            {
                return false;
            }
        }
        return true;
    }
    return SegmentClear(from, to, .05f);
}
void CivicJustice::Reset(GameState &world)
{
    civic = CivicState{};
    PlanetSecurity::Reset();
    BuildObstacles(world);
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        Vector2 best = center;
        float score = 1.e9f;
        for (const auto &connection : CityPlan::Connections())
        {
            const auto &building = world.buildings[connection.building];
            float distance = Game::Distance(connection.entrance, center);
            bool home = std::fabs(building.x - kHomeBuildingPosition.x) < .01f &&
                        std::fabs(building.y - kHomeBuildingPosition.y) < .01f;
            if (distance > 46 || home || building.type == models::BuildingType::Observatory ||
                Solid(connection.entrance, .35f))
            {
                continue;
            }
            float rank = distance + (building.type == models::BuildingType::TransitAtrium ? -20.f : 0);
            if (rank < score)
            {
                score = rank;
                best = connection.entrance;
            }
        }
        if (score == 1.e9f)
        {
            for (int i = 0; i < 1600; ++i)
            {
                auto p = Add(center, {float(i % 40) - 20, float(i / 40) - 20});
                if (!Solid(p, .5f) && !RoadNetwork::Get().Contains(p, 1))
                {
                    best = p;
                    break;
                }
            }
        }
        civic.hospitals.push_back({city, best});
    }
}
int CivicJustice::WantedForHeat(float heat)
{
    constexpr float thresholds[] = {20, 180, 400, 650, 900};
    int result = 0;
    for (float threshold : thresholds)
    {
        result += heat >= threshold;
    }
    return result;
}
float CivicJustice::PriceMultiplier()
{
    return 1 + civic.infamy * .0075f;
}
int CivicJustice::ShopPrice(int basePrice)
{
    return int((std::min)(double(std::numeric_limits<int>::max()),
                          std::ceil((std::max)(0, basePrice) * double(PriceMultiplier()))));
}
int CivicJustice::LivingPopulation(int city)
{
    if (!Game::Get().IsStoryWorld())
    {
        return 0;
    }
    auto &world = Game::Get().GetState();
    int count = 0;
    bool kain = game::GameLevels::Chapter().GetState().kainJoined;
    for (size_t i = 0; i < world.residents.size(); ++i)
    {
        if (world.residents[i].health > 0 && (city < 0 || world.residents[i].cityIndex == city) &&
            !(i == Overworld::kKainResidentSlot && kain))
        {
            ++count;
        }
    }
    if (city < 0 || city == 0)
    {
        const auto *party = Game::Get().SurfaceParty();
        count += (!(party && party->Joined(CompanionId::Ria)) && civic.landmarkHealth[0] > 0) +
                 (civic.landmarkHealth[1] > 0);
        count += !party && kain && civic.landmarkHealth[2] > 0;
    }
    if (const auto *party = Game::Get().SurfaceParty(); party && Game::Get().Surface()->Ready())
    {
        const auto &surface = *Game::Get().Surface();
        for (const auto &member : party->State().members)
        {
            if (!member.joined || member.place == CompanionPlace::Mine || member.combat.health <= 0)
            {
                continue;
            }
            if (city < 0 ||
                (city < Overworld::kCityCount &&
                 ::world::SurfaceGeometry::Distance(surface.Planet().radiusMeters, member.position,
                                                    surface.Stream()
                                                        .Snapshot()
                                                        ->planet->Region(Overworld::Cities()[size_t(city)].id)
                                                        .center.position) < 300))
            {
                ++count;
            }
        }
    }
    return count;
}
namespace
{
void ClearPursuit()
{
    civic.heat = 0;
    civic.wanted = 0;
    civic.pendingInfamy = civic.pursuitSeconds = civic.initialUnseenSeconds = 0;
    civic.responseStarted = civic.identified = false;
}
} // namespace
float CivicJustice::SightDistance(const SecurityOfficer &officer, float angle)
{
    Vector2 direction{std::cos(angle), std::sin(angle)};
    float previous = 0;
    for (float distance = .5f; distance <= kSightRange; distance += .5f)
    {
        if (Solid(Add(officer.position, direction, distance), .05f))
        {
            return previous;
        }
        previous = distance;
    }
    return kSightRange;
}
bool CivicJustice::CanSee(const SecurityOfficer &officer, Vector2 target, float altitude)
{
    if (officer.health <= 0 || (!Game::Get().GetState().surfaceExploration && altitude >= .8f) ||
        (officer.impact.response == BodyResponse::Knockdown && officer.impact.seconds > 0))
    {
        return false;
    }
    auto delta = Sub(target, officer.position);
    float distance = std::hypot(delta.x, delta.y);
    if (distance > kSightRange)
    {
        return false;
    }
    if (distance > .001f && (delta.x * std::cos(officer.facing.yaw) +
                             delta.y * std::sin(officer.facing.yaw)) < distance * std::cos(kSightHalfAngle))
    {
        return false;
    }
    return ClearLine(officer.position, target);
}
void CivicJustice::Report(Offense offense, Vector2 position)
{
    if (HospitalRecovery::Active())
    {
        return;
    }
    // Only weapon injuries/deaths and occupied-vehicle destruction are criminal events.
    // Discharge, incidental contact and nonlethal property damage intentionally do nothing.
    constexpr float heat[] = {0, 20, 70, 0, 50, 30, 90, 0, 45};
    constexpr float infamy[] = {0, 2, 10, 0, 5, 3, 14, 0, 6};
    size_t index = size_t(offense);
    if (index >= std::size(heat) || heat[index] <= 0)
    {
        return;
    }
    if (civic.wanted == 0)
    {
        ClearPursuit();
        civic.spawnSeconds = 0;
    }
    if (Game::Get().GetState().surfaceExploration)
    {
        const auto &surface = *Game::Get().Surface();
        PlanetSecurity::Migrate(planetary, civic, surface.Planet(), surface.Origin());
        const auto point = reportedPoint ? *reportedPoint : PlanetSecurity::Point(position);
        planetary.incident = planetary.search = point;
        position = PlanetSecurity::Local(point);
        for (auto &officer : planetary.officers)
        {
            officer.lastKnown = point;
        }
    }
    civic.heat = Game::Clamp(civic.heat + heat[index], 0, kMaximumHeat);
    civic.sinceCrime = civic.unseenSeconds = 0;
    civic.lastCrime = position;
    if (civic.identified)
    {
        civic.infamy = Game::Clamp(civic.infamy + infamy[index], 0, 100);
    }
    else
    {
        civic.pendingInfamy = Game::Clamp(civic.pendingInfamy + infamy[index], 0, 100);
    }
    constexpr float escalationTime[] = {0, 0, 25, 60, 105, 165};
    int next = (std::min)(5, civic.wanted + 1);
    if (WantedForHeat(civic.heat) >= next && civic.pursuitSeconds >= escalationTime[next])
    {
        civic.wanted = next;
    }
    auto &world = Game::Get().GetState();
    for (auto &person : world.residents)
    {
        Vector2 p = person.position;
        if (world.surfaceExploration)
        {
            auto point = Game::Get().SurfaceCities()
                             ? Game::Get().SurfaceCities()->Point(*Game::Get().Surface(), person.cityIndex,
                                                                  person.position)
                             : std::optional<::world::SurfacePoint>{};
            if (!point)
            {
                continue;
            }
            p = PlanetSecurity::Local(*point);
        }
        if (ResidentPresent(person) && person.health > 0 && Game::Distance(p, position) < 12 &&
            (world.surfaceExploration || ClearLine(position, p)))
        {
            person.fearSeconds = (std::max)(person.fearSeconds, 8.f);
        }
    }
    for (auto &officer : civic.officers)
    {
        if ((OfficersPresent() || world.surfaceExploration) && officer.health > 0)
        {
            officer.lastKnown = position;
        }
    }
    if (civic.wanted)
    {
        civic.spawnSeconds = (std::min)(civic.spawnSeconds, 2.f);
    }
}
void CivicJustice::AdvanceSurfaceTimers(float dt)
{
    dt = Game::Clamp(dt, 0, .05f);
    if (dt <= 0)
    {
        return;
    }
    civic.playerGrace = (std::max)(0.f, civic.playerGrace - dt);
    civic.dischargeCooldown = (std::max)(0.f, civic.dischargeCooldown - dt);
    for (size_t i = 0; i < civic.landmarkHealth.size(); ++i)
    {
        if (civic.landmarkHealth[i] <= 0)
        {
            civic.landmarkRecovery[i] = (std::max)(0.f, civic.landmarkRecovery[i] - dt);
            if (civic.landmarkRecovery[i] <= 0)
            {
                civic.landmarkHealth[i] = 100;
            }
        }
    }
    const auto cities = Game::Get().SurfaceCities();
    if (!cities || cities->ActiveCity() < 0)
    {
        auto &position = Game::Get().GetState().playerPosition;
        AdvanceImpact(position, civic.playerImpact, dt, false);
    }
}
void CivicJustice::Update(float dt)
{
    profiling::Scope profile(profiling::Timer::PlanetSecurity);
    if (dt <= 0 || HospitalRecovery::Active())
    {
        return;
    }
    const bool surfaceMode = Game::Get().GetState().surfaceExploration;
    if (surfaceMode && (!Game::Get().SurfaceCities() || Game::Get().SurfaceCities()->ActiveCity() < 0))
    {
        AdvanceImpact(Game::Get().GetState().playerPosition, civic.playerImpact, dt, false);
    }
    bool unconfirmedAtStart = civic.wanted > 0 && !civic.identified;
    civic.sinceCrime += dt;
    if (civic.wanted > 0)
    {
        civic.pursuitSeconds += dt;
    }
    civic.dischargeCooldown = (std::max)(0.f, civic.dischargeCooldown - dt);
    civic.playerGrace = (std::max)(0.f, civic.playerGrace - dt);
    for (size_t i = 0; i < civic.landmarkHealth.size(); ++i)
    {
        if (civic.landmarkHealth[i] <= 0)
        {
            civic.landmarkRecovery[i] -= dt;
            if (civic.landmarkRecovery[i] <= 0)
            {
                civic.landmarkHealth[i] = 100;
            }
        }
    }
    bool visible = false;
    auto &world = Game::Get().GetState();
    for (auto &officer : civic.officers)
    {
        if (officer.weaponTier != officer.tier)
        {
            officer.weapon =
                combat::SecurityCarbine(officer.tier, std::uint32_t(officer.id * 2654435761u) ^ 0x914e31u);
            officer.weaponTier = officer.tier;
        }
        officer.weapon.Update(dt);
        officer.cooldown = (std::max)(0.f, officer.cooldown - dt);
        officer.muzzleFlash = (std::max)(0.f, officer.muzzleFlash - dt);
        if (officer.health <= 0)
        {
            officer.corpseSeconds += dt;
            continue;
        }
        if (surfaceMode)
        {
            AdvanceImpact(officer.position, officer.impact, dt, IsExterior());
        }
        if (!IsExterior() || (surfaceMode && Game::Distance(world.playerPosition, officer.position) > 300))
        {
            continue;
        }
        auto delta = Sub(world.playerPosition, officer.position);
        float distance = std::hypot(delta.x, delta.y);
        bool sees = CanSee(officer, world.playerPosition, PlayerHeight());
        visible |= sees;
        if (sees)
        {
            officer.lastKnown = world.playerPosition;
            if (surfaceMode)
            {
                PlanetSecurity::SawPlayer();
            }
        }
        if (civic.wanted == 0)
        {
            officer.lastKnown = Add(officer.position, Unit(Sub(officer.position, world.playerPosition)), 15);
        }
        officer.navigationSeconds -= dt;
        if (officer.navigationSeconds <= 0 && (!surfaceMode || PlanetSecurity::CanNavigate()))
        {
            officer.route = EmergencyRoute(officer.position, sees ? world.playerPosition : officer.lastKnown);
            officer.waypoint = 0;
            officer.navigationSeconds = 1.2f + float(officer.id % 4) * .15f;
        }
        auto previous = officer.position;
        if ((!sees || distance > 5) && officer.waypoint < officer.route.size() && officer.impact.seconds <= 0)
        {
            auto target = officer.route[officer.waypoint];
            float remaining = Game::Distance(target, officer.position);
            auto next = Add(officer.position, Unit(Sub(target, officer.position)),
                            (std::min)(remaining, (2.2f + .25f * officer.tier) * dt));
            if (SegmentClear(officer.position, next, .32f))
            {
                officer.position =
                    surfaceMode
                        ? BodyCollision::Slide(officer.position, next,
                                               [](Vector2 p) { return PlanetSecurity::BodyBlocked(p); })
                        : Separate(officer.position, next, .3f, -1, false,
                                   int(&officer - civic.officers.data()));
            }
            if (remaining < .3f)
            {
                ++officer.waypoint;
            }
        }
        officer.facing.Update(previous, officer.position);
        if (!sees && !officer.facing.walking && Game::Distance(officer.position, officer.lastKnown) < 1)
        {
            officer.facing.yaw += dt * .8f;
        }
        sees = CanSee(officer, world.playerPosition, PlayerHeight());
        visible |= sees;
        if (sees)
        {
            officer.facing.yaw = std::atan2(delta.y, delta.x);
        }
        if (civic.wanted > 0 && sees && distance < 8 + officer.tier * 1.5f && officer.impact.seconds <= 0 &&
            civic.bullets.size() < 128)
        {
            combat::Shot shot;
            if (officer.weapon.TryFire(std::atan2(delta.y, delta.x), shot))
            {
                const auto &stats = game::GameLevels::Mine().GetState().player;
                int power = stats.defense +
                            (std::max)(8 + officer.tier * 4, int(stats.maxHealth * .015f * officer.tier));
                civic.bullets.push_back(
                    {officer.position,
                     {std::cos(shot.angle), std::sin(shot.angle)},
                     shot.speed,
                     shot.range,
                     power,
                     false,
                     false,
                     surfaceMode ? float((Game::Get()
                                              .Surface()
                                              ->ContactAt(officer.position.x * 2, officer.position.y * 2)
                                              .heightMeters -
                                          Game::Get().Surface()->Origin().origin.altitudeMeters) /
                                         2) +
                                       models::WorldScale::aimHeightPixels / 44
                                 : 0});
                officer.muzzleFlash = .07f;
            }
        }
    }
    if (civic.wanted > 0)
    {
        bool responders = std::any_of(civic.officers.begin(), civic.officers.end(),
                                      [](const SecurityOfficer &o) { return o.health > 0; });
        bool justStarted = !civic.responseStarted && responders;
        if (justStarted)
        {
            civic.responseStarted = true;
            civic.initialUnseenSeconds = 0;
        }
        if (visible && !civic.identified)
        {
            civic.identified = true;
            civic.infamy = Game::Clamp(civic.infamy + civic.pendingInfamy, 0, 100);
            civic.pendingInfamy = 0;
        }
        civic.unseenSeconds = visible ? 0 : civic.unseenSeconds + dt;
        if (civic.responseStarted && !civic.identified && !justStarted)
        {
            civic.initialUnseenSeconds += dt;
            if (civic.initialUnseenSeconds >= kUnidentifiedEscapeSeconds)
            {
                ClearPursuit();
                Game::Get().ShowNotice(
                    "수배 해제 / 보안대에 식별되지 않았습니다. 이번 사건은 악명에 반영되지 않습니다.");
            }
        }
        else if (civic.identified && civic.unseenSeconds >= 20)
        {
            ClearPursuit();
            Game::Get().ShowNotice("수배 해제 / 보안대의 추적을 벗어났습니다. 기존 악명은 유지됩니다.");
        }
    }
    if (!unconfirmedAtStart && civic.sinceCrime > 90)
    {
        civic.infamy = (std::max)(0.f, civic.infamy - .005f * dt);
    }
    // New reputation-relief activities remain a design-only follow-up.
    civic.officers.erase(
        std::remove_if(civic.officers.begin(), civic.officers.end(),
                       [&](const SecurityOfficer &o) {
                           return (o.health <= 0 && o.corpseSeconds > 20 &&
                                   TrafficSystem::OutsideView(o.position, world.cameraPosition, 80)) ||
                                  (civic.wanted == 0 &&
                                   TrafficSystem::OutsideView(o.position, world.cameraPosition, 100));
                       }),
        civic.officers.end());
    civic.spawnSeconds -= dt;
    size_t living = std::count_if(civic.officers.begin(), civic.officers.end(),
                                  [](const SecurityOfficer &o) { return o.health > 0; });
    if (civic.officers.size() >= 16 && living < size_t(civic.wanted * 2))
    {
        auto body = std::find_if(civic.officers.begin(), civic.officers.end(),
                                 [](const SecurityOfficer &o) { return o.health <= 0; });
        if (body != civic.officers.end())
        {
            civic.officers.erase(body);
        }
    }
    if ((IsExterior() || surfaceMode) && civic.wanted > 0 && civic.spawnSeconds <= 0 &&
        living < size_t(civic.wanted * 2) && civic.officers.size() < 16)
    {
        civic.spawnSeconds = 3.5f;
        if (surfaceMode)
        {
            PlanetSecurity::Spawn();
        }
        for (int attempt = 0; !surfaceMode && attempt < 64; ++attempt)
        {
            float angle = float(Random() % 6283) * .001f;
            float range = 18 + float(Random() % 17);
            auto p = Add(world.playerPosition, {std::cos(angle), std::sin(angle)}, range);
            if (Solid(p, .6f) || !TrafficSystem::OutsideView(p, world.cameraPosition, 90))
            {
                continue;
            }
            auto route = EmergencyRoute(p, civic.lastCrime);
            if (route.empty())
            {
                continue;
            }
            SecurityOfficer officer;
            officer.id = civic.nextOfficer++;
            officer.position = p;
            officer.lastKnown = civic.lastCrime;
            officer.facing.yaw = std::atan2(civic.lastCrime.y - p.y, civic.lastCrime.x - p.x);
            officer.tier = civic.wanted;
            officer.health =
                60 + 40 * civic.wanted + 8 * (game::GameLevels::Mine().GetState().player.level - 1);
            officer.route = std::move(route);
            officer.navigationSeconds = 1;
            civic.officers.push_back(std::move(officer));
            if (!civic.responseStarted)
            {
                civic.responseStarted = true;
                civic.initialUnseenSeconds = 0;
            }
            break;
        }
    }
    if (surfaceMode)
    {
        PlanetSecurity::Capture();
    }
}

bool CivicCombat::Available()
{
    return combat::PlayerCombat::EnvironmentBlock(combat::PlayerCombat::Current(),
                                                  combat::CombatArea::Exterior) == combat::CombatBlock::Ready;
}
void CivicCombat::DamagePlayer(int damage, Vector2 impulse)
{
    auto &stats = game::GameLevels::Mine().GetState().player;
    if (damage <= 0 || stats.health <= 0 || HospitalRecovery::Active() || civic.playerGrace > 0 ||
        civic.playerImpact.invulnerability > 0)
    {
        return;
    }
    stats.health = (std::max)(0, stats.health - (std::max)(1, damage - stats.defense));
    Game::Get().GetState().InterruptDodge();
    Impact(civic.playerImpact, impulse, stats.health == 0);
    if (stats.health == 0)
    {
        HospitalRecovery::Begin();
    }
}
bool CivicCombat::DamageResident(size_t index, int damage, Vector2 impulse, bool playerOwned)
{
    auto &world = Game::Get().GetState();
    if (index >= world.residents.size() || damage <= 0 ||
        (index == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined))
    {
        return false;
    }
    auto &person = world.residents[index];
    if (person.health <= 0)
    {
        return false;
    }
    person.health = (std::max)(0, person.health - damage);
    person.fearSeconds = 15;
    person.conversationSeconds = 0;
    person.route.clear();
    person.routeCursor = 0;
    person.crossingJunction = -1;
    Impact(person.impact, impulse, person.health == 0);
    person.activity = person.health ? ResidentActivity::Injured : ResidentActivity::Dead;
    if (playerOwned)
    {
        if (world.surfaceExploration && Game::Get().SurfaceCities())
        {
            PlanetSecurity::Report(person.health ? Offense::Assault : Offense::Homicide,
                                   *Game::Get().SurfaceCities()->Point(*Game::Get().Surface(),
                                                                       person.cityIndex, person.position));
        }
        else
        {
            CivicJustice::Report(person.health ? Offense::Assault : Offense::Homicide, person.position);
        }
    }
    return true;
}
bool CivicCombat::DamageOfficer(size_t index, int damage, Vector2 impulse, bool playerOwned)
{
    if (index >= civic.officers.size() || damage <= 0 || civic.officers[index].health <= 0)
    {
        return false;
    }
    auto &officer = civic.officers[index];
    officer.health = (std::max)(0, officer.health - damage);
    Impact(officer.impact, impulse, officer.health == 0);
    if (playerOwned)
    {
        if (Game::Get().GetState().surfaceExploration)
        {
            PlanetSecurity::Report(
                officer.health ? Offense::AssaultOfficer : Offense::OfficerKilled,
                Game::Get().Surface()->PointAt(officer.position.x * 2, officer.position.y * 2));
        }
        else
        {
            CivicJustice::Report(officer.health ? Offense::AssaultOfficer : Offense::OfficerKilled,
                                 officer.position);
        }
    }
    return true;
}
bool CivicCombat::DamageLandmark(size_t index, int damage, bool weaponOwned)
{
    if (index >= civic.landmarkHealth.size() || !LandmarkPresent(index) ||
        (index == 2 && !game::GameLevels::Chapter().GetState().kainJoined) ||
        civic.landmarkHealth[index] <= 0 || damage <= 0)
    {
        return false;
    }
    civic.landmarkHealth[index] = (std::max)(0, civic.landmarkHealth[index] - damage);
    civic.landmarkRecovery[index] = 60;
    if (weaponOwned)
    {
        if (Game::Get().GetState().surfaceExploration)
        {
            PlanetSecurity::Report(
                civic.landmarkHealth[index] ? Offense::Assault : Offense::AggravatedAssault,
                *Game::Get().SurfaceCities()->Point(*Game::Get().Surface(), 0, kPeople[index]));
        }
        else
        {
            CivicJustice::Report(civic.landmarkHealth[index] ? Offense::Assault : Offense::AggravatedAssault,
                                 kPeople[index]);
        }
    }
    return true;
}
bool CivicCombat::LandmarkDown(Vector2 position)
{
    for (size_t i = 0; i < std::size(kPeople); ++i)
    {
        if (LandmarkPresent(i) && civic.landmarkHealth[i] <= 0 && Game::Distance(position, kPeople[i]) < .6f)
        {
            return true;
        }
    }
    return false;
}
namespace
{
bool AimAtVehicles(float shotAngle, models::Point &target)
{
    const auto &w = Game::Get().GetState();
    float best = 1.e20f;
    bool found = false;
    auto muzzle = WorldCamera::ToViewport(
        Drawing::WorldToScreen(w.playerPosition.x, w.playerPosition.y, models::WorldScale::aimHeightPixels));
    float radius = Game::Distance(w.cursorPosition, muzzle);
    auto cursor = WorldCamera::FromViewport(
        {muzzle.x + std::cos(shotAngle) * radius, muzzle.y + std::sin(shotAngle) * radius});
    std::vector<AircraftActor> candidates;
    if (w.surfaceExploration && Game::Get().SurfaceTraffic())
    {
        const auto &surface = *Game::Get().Surface();
        for (const auto &actor : Game::Get().SurfaceTraffic()->State().actors)
        {
            if (actor.city >= 0 || actor.health <= 0 ||
                ::world::SurfaceGeometry::Distance(surface.Planet().radiusMeters, actor.position,
                                                   surface.Pose().origin) > 250)
            {
                continue;
            }
            AircraftActor craft;
            craft.id = actor.id;
            craft.position = PlanetSecurity::Local(actor.position);
            craft.altitude = PlanetSecurity::Elevation(actor.position);
            craft.health = actor.health;
            craft.type = actor.airborne
                             ? models::AircraftType((actor.id / 2) % size_t(models::AircraftType::Count))
                             : models::AircraftType::Count;
            craft.alternate = actor.id % 2 != 0;
            craft.bank = actor.bank;
            craft.yaw =
                float(std::atan2(::world::SurfaceGeometry::Dot(actor.heading, surface.Origin().forward),
                                 ::world::SurfaceGeometry::Dot(actor.heading, surface.Origin().right)));
            candidates.push_back(craft);
        }
    }
    if (w.surfaceExploration)
    {
        for (const auto &body : surfaceTargets)
        {
            if (!body.vehicleBody)
            {
                continue;
            }
            AircraftActor craft;
            craft.id = body.vehicle;
            craft.position = body.position;
            craft.altitude = body.elevation;
            craft.type = models::AircraftType::Count;
            craft.yaw = body.yaw;
            candidates.push_back(craft);
        }
    }
    const auto &aircraft = w.surfaceExploration ? candidates : Overworld::Flights().Aircraft();
    for (const auto &craft : aircraft)
    {
        if (craft.health <= 0)
        {
            continue;
        }
        const auto &mesh = models::ResourceAssets::Get(
            craft.type == models::AircraftType::Count
                ? models::ModelLibrary::Get().vehicles[size_t(craft.id % 4)].body.resourceId
                : models::ModelLibrary::Aircraft(craft.type, craft.alternate).resourceId);
        models::Transform3D transform;
        transform.yaw = craft.yaw;
        transform.roll = craft.bank;
        transform.position.z = craft.altitude;
        Vector2 lo{1.e20f, 1.e20f}, hi{-1.e20f, -1.e20f};
        for (int i = 0; i < 8; ++i)
        {
            auto p = transform.Apply({i & 1 ? mesh.maximum.x : mesh.minimum.x,
                                      i & 2 ? mesh.maximum.y : mesh.minimum.y,
                                      i & 4 ? mesh.maximum.z : mesh.minimum.z});
            auto screen = WorldCamera::ToViewport(
                Drawing::WorldToScreen(craft.position.x + p.x, craft.position.y + p.y, p.z * 44));
            lo.x = (std::min)(lo.x, screen.x);
            lo.y = (std::min)(lo.y, screen.y);
            hi.x = (std::max)(hi.x, screen.x);
            hi.y = (std::max)(hi.y, screen.y);
        }
        if (w.cursorPosition.x < lo.x || w.cursorPosition.x > hi.x || w.cursorPosition.y < lo.y ||
            w.cursorPosition.y > hi.y)
        {
            continue;
        }
        float z = craft.altitude + (mesh.minimum.z + mesh.maximum.z) * .5f;
        float sx = (cursor.x - kCanvasWidth * .5f) / 36, sy = (cursor.y - kCanvasHeight * .57f + z * 44) / 18;
        models::Point p{w.cameraPosition.x + (sx + sy) * .5f, w.cameraPosition.y + (sy - sx) * .5f, z};
        float range = std::hypot(Game::Distance({p.x, p.y}, w.playerPosition), z);
        if (range < best)
        {
            target = p;
            best = range;
            found = true;
        }
    }
    return found;
}
bool AirShotBlocked(models::Point p)
{
    if (Game::Get().GetState().surfaceExploration)
    {
        return PlanetSecurity::ShotBlocked(p);
    }
    if (p.z < 1 && CivicJustice::Solid({p.x, p.y}, .03f))
    {
        return true;
    }
    for (const auto &b : Game::Get().GetState().buildings)
    {
        if (p.z < b.height / 44 && p.x > b.x && p.x < b.x + b.width && p.y > b.y && p.y < b.y + b.depth)
        {
            return true;
        }
    }
    for (const auto &prop : Overworld::Scenery().Props())
    {
        if (prop.solid && p.x > prop.minimum.x && p.x < prop.maximum.x && p.y > prop.minimum.y &&
            p.y < prop.maximum.y &&
            p.z < prop.elevation + models::ResourceAssets::Get(prop.model->resourceId).maximum.z)
        {
            return true;
        }
    }
    return false;
}
void UpdateAirProjectiles(float dt)
{
    for (auto &t : civic.airTracers)
    {
        t.remaining -= dt;
    }
    civic.airTracers.erase(std::remove_if(civic.airTracers.begin(), civic.airTracers.end(),
                                          [](const AirTracer &t) { return t.remaining <= 0; }),
                           civic.airTracers.end());
    for (auto &bullet : civic.airBullets)
    {
        auto start = bullet.position;
        float movement = (std::min)(bullet.remaining, bullet.speed * dt);
        int steps = (std::max)(1, int(std::ceil(movement / .08f)));
        for (int i = 0; i < steps && bullet.remaining > 0; ++i)
        {
            float step = movement / steps;
            bullet.position.x += bullet.direction.x * step;
            bullet.position.y += bullet.direction.y * step;
            bullet.position.z += bullet.direction.z * step;
            bullet.remaining = (std::max)(0.f, bullet.remaining - step);
            if (AirShotBlocked(bullet.position))
            {
                bullet.remaining = 0;
                break;
            }
            if (Game::Get().GetState().surfaceExploration)
            {
                if (PlanetSecurity::Hit(bullet.position, bullet.damage, true))
                {
                    bullet.remaining = 0;
                }
                continue;
            }
            for (const auto &craft : Overworld::Flights().Aircraft())
            {
                if (craft.health > 0 && AirTraffic::Contains(craft, bullet.position))
                {
                    Overworld::Flights().DamageAircraft(craft.id, bullet.damage);
                    bullet.remaining = 0;
                    break;
                }
            }
        }
        civic.airTracers.push_back({start, bullet.position, .12f});
    }
    civic.airBullets.erase(std::remove_if(civic.airBullets.begin(), civic.airBullets.end(),
                                          [](const AirProjectile &b) { return b.remaining <= 0; }),
                           civic.airBullets.end());
    if (civic.airTracers.size() > 96)
    {
        civic.airTracers.erase(civic.airTracers.begin(), civic.airTracers.end() - 96);
    }
}
} // namespace
void CivicCombat::Fire()
{
    auto &world = Game::Get().GetState();
    auto &mine = game::GameLevels::Mine();
    if (!mine.GetState().loadout.IsPistolEquipped())
    {
        Melee();
        return;
    }
    if (!combat::PlayerCombat::Allow(combat::CombatAction::Fire, combat::CombatArea::Exterior))
    {
        return;
    }
    combat::Shot shot;
    if (!mine.GetState().pistol.TryFire(game::GameLevels::FireAngle(), shot))
    {
        return;
    }
    world.playerAnimation.Fire();
    models::Point target{};
    if (Game::Get().IsAiming() && AimAtVehicles(shot.angle, target))
    {
        models::Point origin{world.playerPosition.x, world.playerPosition.y,
                             models::WorldScale::aimHeightPixels / 44 +
                                 (world.surfaceExploration
                                      ? PlanetSecurity::Elevation(Game::Get().Surface()->Pose().origin)
                                      : 0)};
        models::Point d{target.x - origin.x, target.y - origin.y, target.z - origin.z};
        float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (length > .001f)
        {
            civic.airBullets.push_back({origin,
                                        {d.x / length, d.y / length, d.z / length},
                                        shot.speed,
                                        shot.range,
                                        shot.damage + (mine.GetState().player.level - 1) * 4});
        }
        civic.muzzleFlash = .06f;
        return;
    }
    civic.bullets.push_back({world.playerPosition, game::MineLevel::GetAimDirection(shot.angle), shot.speed,
                             shot.range, shot.damage + (mine.GetState().player.level - 1) * 4, true, false,
                             world.surfaceExploration
                                 ? PlanetSecurity::Elevation(Game::Get().Surface()->Pose().origin) +
                                       models::WorldScale::aimHeightPixels / 44
                                 : 0});
    civic.muzzleFlash = .06f;
}
void CivicCombat::Melee()
{
    if (!combat::PlayerCombat::Allow(combat::CombatAction::Melee, combat::CombatArea::Exterior))
    {
        return;
    }
    civic.meleeSeconds = .65f;
    auto &world = Game::Get().GetState();
    auto aim = game::MineLevel::GetAimDirection(game::GameLevels::FireAngle());
    auto hit = [&](Vector2 p) {
        auto d = Sub(p, world.playerPosition);
        float range = std::hypot(d.x, d.y);
        return range < 1.45f && (range < .45f || d.x * aim.x + d.y * aim.y > range * .25f) &&
               CivicJustice::ClearLine(world.playerPosition, p);
    };
    if (world.surfaceExploration)
    {
        float z = PlanetSecurity::Elevation(Game::Get().Surface()->Pose().origin) +
                  models::WorldScale::aimHeightPixels / 44;
        for (int i = 1; i <= 12; ++i)
        {
            auto p = Add(world.playerPosition, aim, i * .1f);
            if (PlanetSecurity::ShotBlocked({p.x, p.y, z}))
            {
                break;
            }
            if (PlanetSecurity::Hit({p.x, p.y, z}, 30, true))
            {
                break;
            }
        }
        return;
    }
    auto impulse = Add({}, aim, 4);
    for (size_t i = 0; i < civic.officers.size(); ++i)
    {
        if (hit(civic.officers[i].position) && DamageOfficer(i, 30, impulse))
        {
            return;
        }
    }
    for (size_t i = 0; i < world.residents.size(); ++i)
    {
        if (hit(world.residents[i].position) && DamageResident(i, 30, impulse, true))
        {
            return;
        }
    }
    for (size_t i = 0; i < std::size(kPeople); ++i)
    {
        if (hit(kPeople[i]) && DamageLandmark(i, 30))
        {
            return;
        }
    }
    for (const auto &car : Overworld::Traffic().Vehicles())
    {
        if (CarContains(car, Add(world.playerPosition, aim, .8f), .5f))
        {
            Overworld::Traffic().DamageVehicle(car.id, 22);
            return;
        }
    }
}
void CivicCombat::Update(float dt)
{
    civic.meleeSeconds = (std::max)(0.f, civic.meleeSeconds - dt);
    civic.muzzleFlash = (std::max)(0.f, civic.muzzleFlash - dt);
    if (Available())
    {
        game::GameLevels::Mine().GetState().pistol.Update(dt);
    }
    if (Available() && Game::Get().GetState().mouseFiring)
    {
        Fire();
    }
    for (auto &tracer : civic.tracers)
    {
        tracer.remaining -= dt;
    }
    civic.tracers.erase(std::remove_if(civic.tracers.begin(), civic.tracers.end(),
                                       [](const game::Tracer &t) { return t.remaining <= 0; }),
                        civic.tracers.end());
    if (!CivicJustice::IsExterior())
    {
        civic.bullets.clear();
        civic.airBullets.clear();
        civic.airTracers.clear();
        civic.tracers.clear();
        return;
    }
    UpdateAirProjectiles(dt);
    auto &world = Game::Get().GetState();
    auto &traffic = Overworld::Traffic();
    for (auto &bullet : civic.bullets)
    {
        auto start = bullet.position;
        float movement = (std::min)(bullet.remaining, bullet.speed * dt);
        int steps = (std::max)(1, int(std::ceil(movement / .08f)));
        for (int step = 0; step < steps && bullet.remaining > 0; ++step)
        {
            auto next = Add(bullet.position, bullet.direction, movement / steps);
            if (world.surfaceExploration)
            {
                bullet.position = next;
                bullet.remaining = (std::max)(0.f, bullet.remaining - movement / steps);
                if (PlanetSecurity::ShotBlocked({next.x, next.y, bullet.elevation}) ||
                    PlanetSecurity::Hit({next.x, next.y, bullet.elevation}, bullet.damage, bullet.playerOwned,
                                        bullet.companionOwned))
                {
                    bullet.remaining = 0;
                }
                continue;
            }
            if (CivicJustice::Solid(next, .03f))
            {
                bullet.remaining = 0;
                break;
            }
            bullet.position = next;
            bullet.remaining = (std::max)(0.f, bullet.remaining - movement / steps);
            auto impulse = Add({}, bullet.direction, 2);
            if (!bullet.playerOwned)
            {
                if (PlayerHeight() < .8f && Game::Distance(next, world.playerPosition) < .18f)
                {
                    DamagePlayer(bullet.damage, impulse);
                    bullet.remaining = 0;
                }
                for (size_t i = 0; i < traffic.Vehicles().size() && bullet.remaining > 0; ++i)
                {
                    if (traffic.RenderElevation(i) < .7f && CarContains(traffic.Vehicles()[i], next, .02f))
                    {
                        bullet.remaining = 0;
                    }
                }
                continue;
            }
            for (size_t i = 0; i < civic.officers.size(); ++i)
            {
                if (Game::Distance(next, civic.officers[i].position) < .35f &&
                    DamageOfficer(i, bullet.damage, impulse))
                {
                    bullet.remaining = 0;
                    break;
                }
            }
            if (bullet.remaining <= 0)
            {
                break;
            }
            for (size_t i = 0; i < world.residents.size(); ++i)
            {
                if (Game::Distance(next, world.residents[i].position) < .35f &&
                    DamageResident(i, bullet.damage, impulse, true))
                {
                    bullet.remaining = 0;
                    break;
                }
            }
            if (bullet.remaining <= 0)
            {
                break;
            }
            for (size_t i = 0; i < std::size(kPeople); ++i)
            {
                if (Game::Distance(next, kPeople[i]) < .35f && DamageLandmark(i, bullet.damage))
                {
                    bullet.remaining = 0;
                    break;
                }
            }
            if (bullet.remaining <= 0)
            {
                break;
            }
            for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
            {
                if (traffic.RenderElevation(i) < .7f && CarContains(traffic.Vehicles()[i], next, .04f))
                {
                    traffic.DamageVehicle(traffic.Vehicles()[i].id, bullet.damage);
                    bullet.remaining = 0;
                    break;
                }
            }
        }
        civic.tracers.push_back({start, bullet.position, .12f, !bullet.playerOwned, bullet.elevation});
    }
    civic.bullets.erase(std::remove_if(civic.bullets.begin(), civic.bullets.end(),
                                       [](const game::Bullet &b) { return b.remaining <= 0; }),
                        civic.bullets.end());
    if (civic.tracers.size() > 160)
    {
        civic.tracers.erase(civic.tracers.begin(), civic.tracers.end() - 160);
    }
}
void CivicCombat::ClearProjectiles()
{
    civic.bullets.clear();
    civic.airBullets.clear();
    civic.tracers.clear();
    civic.airTracers.clear();
    civic.muzzleFlash = 0;
}

Vector2 BodyCollision::Slide(Vector2 from, Vector2 desired, const std::function<bool(Vector2)> &blocked,
                             float sweepStep)
{
    Vector2 result = from;
    if (blocked(result))
    {
        // Moving traffic and a changed bike yaw can cause deeper overlaps than a body radius.
        // Resolve the nearest free surface, preferring the requested escape direction on ties.
        bool freed = false;
        auto escape = Unit(Sub(desired, from));
        for (int ring = 1; ring <= 55 && !freed; ++ring)
        {
            float radius = ring <= 20 ? ring * .025f : .5f + (ring - 20) * .1f;
            float best = -2;
            for (int i = 0; i < 64; ++i)
            {
                float angle = i * kPi / 32;
                Vector2 direction = {std::cos(angle), std::sin(angle)};
                auto p = Add(from, direction, radius);
                float score = direction.x * escape.x + direction.y * escape.y;
                if (score > best && !blocked(p))
                {
                    result = p;
                    freed = true;
                    best = score;
                }
            }
        }
        if (!freed)
        {
            return from;
        }
    }
    auto displacement = Sub(desired, from);
    // Subdivide the motion so a fast step cannot skip a thin fence or edge.
    int steps = (std::max)(1, int(std::ceil(std::hypot(displacement.x, displacement.y) / sweepStep)));
    auto increment = Add({}, displacement, 1.f / steps);
    for (int step = 0; step < steps; ++step)
    {
        auto target = Add(result, increment);
        if (!blocked(target))
        {
            result = target;
            continue;
        }
        float lo = 0, hi = 1;
        for (int i = 0; i < 8; ++i)
        {
            float mid = (lo + hi) * .5f;
            if (blocked(Add(result, increment, mid)))
            {
                hi = mid;
            }
            else
            {
                lo = mid;
            }
        }
        result = Add(result, increment, (std::max)(0.f, lo - .005f));
        auto remaining = Add({}, increment, 1 - lo);
        Vector2 best = result;
        float progress = 0;
        // Project the residual onto nearby tangents. This also follows skew/curved mesh
        // walls, unlike an X-then-Y fallback, without adding a separation gap.
        for (int i = 0; i < 32; ++i)
        {
            float angle = i * kPi / 16;
            Vector2 tangent = {std::cos(angle), std::sin(angle)};
            float distance = tangent.x * remaining.x + tangent.y * remaining.y;
            if (distance <= .00001f)
            {
                continue;
            }
            auto slide = Add(result, tangent, distance);
            float score = distance * distance;
            if (score > progress && !blocked(slide) && !blocked(Add(result, Sub(slide, result), .5f)))
            {
                best = slide;
                progress = score;
            }
        }
        result = best;
    }
    return result;
}
bool BodyCollision::BikeBlocked(Vector2 position, float yaw, float elevation, bool includeVehicles)
{
    const auto &mesh = models::ResourceAssets::Get(
        models::ModelLibrary::Get().vehicles[size_t(models::VehicleType::JetBike)].body.resourceId);
    Vector2 d = {std::cos(yaw), std::sin(yaw)}, n = {-d.y, d.x};
    const auto &traffic = Overworld::Traffic();
    std::array<const TrafficVehicle *, TrafficSystem::kVehicleLimit> nearby{};
    size_t nearbyCount = 0;
    if (includeVehicles)
    {
        for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
        {
            const auto &car = traffic.Vehicles()[i];
            if (Game::Distance(car.position, position) < 8 &&
                std::fabs(traffic.RenderElevation(i) - elevation) < .7f)
            {
                nearby[nearbyCount++] = &car;
            }
        }
    }
    for (int sample = 0; sample <= 4; ++sample)
    {
        float along = mesh.minimum.x + .035f + (mesh.physicalSize.x - .07f) * sample / 4;
        for (float across : {mesh.minimum.y + .035f, mesh.maximum.y - .035f})
        {
            Vector2 q = Add(Add(position, d, along), n, across);
            if (q.x < kWorldMinX || q.x > kWorldMaxX || q.y < kWorldMinY || q.y > kWorldMaxY ||
                TransitLayout::IsBlocked(q, .035f, elevation, true) ||
                (elevation <= 1 && CivicJustice::Solid(q, .035f, elevation, true)))
            {
                return true;
            }
            for (size_t i = 0; i < nearbyCount; ++i)
            {
                if (CarContains(*nearby[i], q, .035f))
                {
                    return true;
                }
            }
        }
    }
    return false;
}
void BodyCollision::RecoverBike()
{
    auto &w = Game::Get().GetState();
    if (!w.isBikeMounted || !CivicJustice::IsExterior())
    {
        return;
    }
    if (BikeBlocked(w.bikePosition, w.bikeYaw, w.bikeElevation))
    {
        if (w.bikeSafePoseValid && Game::Distance(w.bikePosition, w.bikeSafePosition) < 4 &&
            std::fabs(w.bikeSafeElevation - w.bikeElevation) < .6f &&
            !BikeBlocked(w.bikeSafePosition, w.bikeSafeYaw, w.bikeSafeElevation))
        {
            w.bikePosition = w.bikeSafePosition;
            w.bikeYaw = w.bikeSafeYaw;
            w.bikeElevation = w.bikeSafeElevation;
        }
        else
        {
            w.bikePosition = Slide(w.bikePosition, w.bikePosition, [&](Vector2 p) {
                float height = RoadNetwork::Get().DriveElevation(p, w.bikeElevation);
                return std::fabs(height - w.bikeElevation) > .6f || BikeBlocked(p, w.bikeYaw, height);
            });
            w.bikeElevation = RoadNetwork::Get().DriveElevation(w.bikePosition, w.bikeElevation);
        }
        w.playerPosition = w.bikePosition;
        w.facingYaw = w.bikeYaw;
        w.StopMotion();
    }
    if (!BikeBlocked(w.bikePosition, w.bikeYaw, w.bikeElevation))
    {
        w.bikeSafePoseValid = true;
        w.bikeSafePosition = w.bikePosition;
        w.bikeSafeYaw = w.bikeYaw;
        w.bikeSafeElevation = w.bikeElevation;
    }
}
Vector2 BodyCollision::MovePlayer(Vector2 from, Vector2 desired)
{
    if (Game::Get().GetState().surfaceExploration && ObservedCity() < 0)
    {
        return desired;
    }
    if (!CivicJustice::IsExterior())
    {
        return desired;
    }
    return Separate(from, desired, .18f, -1, true);
}
Vector2 BodyCollision::MoveResident(size_t index, Vector2 from, Vector2 desired)
{
    return Separate(from, desired, .18f, int(index), false);
}
void BodyCollision::AgeResident(Resident &resident, float dt)
{
    AdvanceImpact(resident.position, resident.impact, dt, false);
    if (resident.health <= 0)
    {
        resident.corpseSeconds += dt;
    }
}
void BodyCollision::UpdateCity(float dt, int city, bool playerPresent)
{
    if (ObservedCity() != city || dt <= 0)
    {
        return;
    }
    auto &world = Game::Get().GetState();
    if (playerPresent)
    {
        auto before = world.playerPosition;
        AdvanceImpact(world.playerPosition, civic.playerImpact, dt, !world.Mounted());
        if (!world.Mounted())
        {
            world.playerPosition = Separate(before, world.playerPosition, .18f, -1, true);
        }
    }
    for (size_t index = 0; index < world.residents.size(); ++index)
    {
        auto &resident = world.residents[index];
        if (resident.cityIndex != city ||
            (index == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined))
        {
            continue;
        }
        auto before = resident.position;
        AdvanceImpact(resident.position, resident.impact, dt);
        if (Game::Distance(before, resident.position) > .00001f)
        {
            resident.position = MoveResident(index, before, resident.position);
        }
        if (resident.health <= 0)
        {
            resident.corpseSeconds += dt;
        }
    }
}
void BodyCollision::Update(float dt)
{
    if (!CivicJustice::IsExterior())
    {
        return;
    }
    auto &world = Game::Get().GetState();
    if (!world.Mounted())
    {
        auto before = world.playerPosition;
        AdvanceImpact(world.playerPosition, civic.playerImpact, dt);
        world.playerPosition = MovePlayer(before, world.playerPosition);
    }
    else
    {
        // Rider reaction/recovery still advances while mounted. Only pedestrian displacement
        // is skipped; applying it here would separate the rider from the bike's transform.
        AdvanceImpact(world.playerPosition, civic.playerImpact, dt, false);
    }
    for (auto &resident : world.residents)
    {
        auto before = resident.position;
        AdvanceImpact(resident.position, resident.impact, dt);
        resident.position =
            MoveResident(size_t(&resident - world.residents.data()), before, resident.position);
        if (resident.health <= 0)
        {
            resident.corpseSeconds += dt;
        }
    }
    for (auto &officer : civic.officers)
    {
        AdvanceImpact(officer.position, officer.impact, dt);
    }
}
Vector2 BodyCollision::SurfaceVehicleContacts(const world::SurfacePoint &from, const world::SurfacePoint &to,
                                              float previousYaw, float impactSpeed)
{
    auto &g = Game::Get();
    auto &w = g.GetState();
    const auto *session = g.Surface();
    if (!session)
    {
        return w.playerPosition;
    }
    const auto a = session->Local(from), b = session->Local(to);
    const Vector2 start{float(a.x / 2), float(a.y / 2)}, finish{float(b.x / 2), float(b.y / 2)};
    if (!w.Mounted() || !CivicJustice::IsExterior() || !std::isfinite(impactSpeed))
    {
        return finish;
    }
    const float travel = Game::Distance(start, finish);
    if (travel > 32)
    {
        return finish; // Developer relocation is not a physical vehicle sweep.
    }
    float halfAxle = .4023f, radius = .40271f, lever = .65f;
    if (w.car.mounted)
    {
        const auto &model = models::ModelLibrary::Get().vehicles[size_t(models::VehicleType::Coupe)].body;
        const auto &mesh = models::ResourceAssets::Get(model.resourceId);
        const float halfLength = mesh.physicalSize.x * .5f, halfWidth = mesh.physicalSize.y * .5f;
        halfAxle = (std::max)(0.f, halfLength - halfWidth);
        radius = halfWidth + .18f;
        lever = halfLength;
    }
    // Test a small spherical cap before any tangent projection (including the antipode).
    const double reachCosine =
        std::cos((travel + halfAxle + radius + 1) * 2 / session->Planet().radiusMeters);
    auto nearby = [&](const world::SurfacePoint &point) {
        return world::SurfaceGeometry::Dot(from.normal, point.normal) >= reachCosine;
    };
    float &vehicleYaw = w.car.mounted ? w.facingYaw : w.bikeYaw;
    const float turn = std::remainder(vehicleYaw - previousYaw, 2 * kPi);
    const int steps =
        (std::min)(256, (std::max)(1, int(std::ceil((travel + std::abs(turn) * lever) / .12f))));
    struct Hit
    {
        Vector2 normal;
        std::function<void(Vector2)> damage;
    };
    std::vector<Hit> hits;
    float first = 1;
    auto center = [&](float t) { return Add(start, Sub(finish, start), t); };
    auto closest = [&](Vector2 person, float t) {
        const float yaw = previousYaw + turn * t;
        const Vector2 direction{std::cos(yaw), std::sin(yaw)};
        const auto delta = Sub(person, center(t));
        return Add(center(t), direction,
                   Game::Clamp(delta.x * direction.x + delta.y * direction.y, -halfAxle, halfAxle));
    };
    auto offer = [&](Vector2 person, float height, std::function<void(Vector2)> damage) {
        if (Game::Distance(person, start) > travel + halfAxle + radius + .4f ||
            std::abs(height - float((a.z + b.z) / 4)) > .8f)
        {
            return;
        }
        auto distance = [&](float t) { return Game::Distance(person, closest(person, t)); };
        const float initial = distance(0);
        // Allow a pre-existing overlap to escape; never lock the rider inside a person.
        if (initial < radius && distance(1) > initial + .001f)
        {
            return;
        }
        for (int step = 0; step <= steps; ++step)
        {
            float upper = float(step) / steps;
            if (distance(upper) >= radius)
            {
                continue;
            }
            float lower = step ? float(step - 1) / steps : 0;
            for (int i = 0; i < 8 && upper > lower; ++i)
            {
                const float middle = (lower + upper) * .5f;
                if (distance(middle) < radius)
                {
                    upper = middle;
                }
                else
                {
                    lower = middle;
                }
            }
            if (upper < first - .001f)
            {
                first = upper;
                hits.clear();
            }
            if (upper <= first + .001f)
            {
                const auto delta = Sub(closest(person, upper), person);
                const auto normal = std::hypot(delta.x, delta.y) > .0001f
                                        ? Unit(delta)
                                        : Vector2{-std::cos(vehicleYaw), -std::sin(vehicleYaw)};
                hits.push_back({normal, std::move(damage)});
            }
            break;
        }
    };
    const int damage = impactSpeed > 2 ? int((std::min)(impactSpeed * 9, 1000.f)) : 0;
    for (const auto &target : surfaceTargets)
    {
        if (target.vehicleBody)
        {
            continue;
        }
        if (target.resident >= 0)
        {
            const size_t index = size_t(target.resident);
            if (index >= w.residents.size() || w.residents[index].health <= 0)
            {
                continue;
            }
            offer(target.position, target.elevation, [&, index, city = target.city](Vector2 impulse) {
                if (w.residents[index].impact.invulnerability > 0)
                {
                    return;
                }
                const auto &frame =
                    session->Stream().Snapshot()->planet->Region(Overworld::Cities()[size_t(city)].id).frame;
                auto v = world::SurfaceGeometry::TransformDirection({impulse.x, impulse.y, 0},
                                                                    session->Origin(), frame);
                CivicCombat::DamageResident(index, damage, {float(v.x), float(v.y)}, false);
            });
        }
        else if (target.landmark >= 0 && civic.landmarkHealth[size_t(target.landmark)] > 0)
        {
            offer(target.position, target.elevation, [index = size_t(target.landmark), damage](Vector2) {
                CivicCombat::DamageLandmark(index, damage, false);
            });
        }
    }
    for (size_t i = 0; i < civic.officers.size(); ++i)
    {
        const auto &officer = civic.officers[i];
        if (officer.health <= 0)
        {
            continue;
        }
        auto record = std::find_if(planetary.officers.begin(), planetary.officers.end(),
                                   [&](const auto &v) { return v.id == officer.id; });
        if (record == planetary.officers.end())
        {
            continue;
        }
        offer(officer.position, PlanetSecurity::Elevation(record->position), [&, i](Vector2 impulse) {
            if (civic.officers[i].impact.invulnerability <= 0)
            {
                CivicCombat::DamageOfficer(i, damage, impulse, false);
            }
        });
    }
    if (auto *party = g.SurfaceParty())
    {
        for (size_t i = 0; i < party->State().members.size(); ++i)
        {
            const auto &person = party->State().members[i];
            if (!person.joined || person.place == CompanionPlace::Mine || person.combat.health <= 0 ||
                !nearby(person.position))
            {
                continue;
            }
            auto p = session->Local(person.position);
            offer({float(p.x / 2), float(p.y / 2)}, float(p.z / 2), [party, i, damage](Vector2) {
                if (party->State().members[i].combat.hitGrace <= 0)
                {
                    party->Damage(CompanionId(i), damage, false);
                }
            });
        }
    }
    if (auto *community = g.SurfaceCommunity())
    {
        for (const auto &site : community->State().sites)
        {
            for (const auto &person : site.residents)
            {
                if (person.health <= 0 || !nearby(person.position))
                {
                    continue;
                }
                auto p = session->Local(person.position);
                offer({float(p.x / 2), float(p.y / 2)}, float(p.z / 2),
                      [community, id = person.id, damage](Vector2) {
                          for (const auto &site : community->State().sites)
                          {
                              for (const auto &person : site.residents)
                              {
                                  if (person.id == id && person.hitFlash <= 0)
                                  {
                                      community->Damage(id, damage, false);
                                      return;
                                  }
                              }
                          }
                      });
            }
        }
    }
    auto &chapter = game::GameLevels::Chapter();
    auto &field = chapter.GetState();
    if (g.IsStoryWorld() && field.generated)
    {
        auto frontierPerson = [&](game::FrontierPerson &person) {
            if (person.combat.health <= 0)
            {
                return;
            }
            const auto point = chapter.Surface().Point(person.position);
            if (!nearby(point))
            {
                return;
            }
            const auto local = session->Local(point);
            auto *victim = &person;
            offer({float(local.x / 2), float(local.y / 2)}, float(local.z / 2), [victim, damage](Vector2) {
                if (victim->combat.hitGrace > 0)
                {
                    return;
                }
                victim->combat.health = (std::max)(0, victim->combat.health - damage);
                victim->combat.hitGrace = .25f;
                victim->route.clear();
                victim->waypoint = 0;
                if (victim->combat.health == 0)
                {
                    victim->combat.downedSeconds = 12;
                    victim->combat.engaged = false;
                }
            });
        };
        if (!field.kainJoined)
        {
            frontierPerson(field.kain);
        }
        for (auto &person : field.survivors)
        {
            frontierPerson(person);
        }
        for (auto &person : field.scrappers)
        {
            if (!(field.negotiated && person.arrived))
            {
                frontierPerson(person);
            }
        }
    }
    if (hits.empty())
    {
        return finish;
    }
    for (const auto &hit : hits)
    {
        if (damage > 0)
        {
            hit.damage(Add({}, hit.normal, -5));
        }
        if (w.car.mounted)
        {
            auto &velocity = w.car.velocity;
            const float inward = velocity.x * hit.normal.x + velocity.y * hit.normal.y;
            velocity = Add(velocity, hit.normal, -(std::min)(0.f, inward));
        }
        else
        {
            DeflectBike(hit.normal);
        }
    }
    const float safe = (std::max)(0.f, first - .01f / (std::max)(.01f, travel + std::abs(turn) * lever));
    vehicleYaw = previousYaw + turn * safe;
    return center(safe);
}

void BodyCollision::VehicleContacts(float)
{
    if ((!CivicJustice::IsExterior() && ObservedCity() < 0) || HospitalRecovery::Active())
    {
        return;
    }
    auto &world = Game::Get().GetState();
    auto &traffic = Overworld::Traffic();
    for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
    {
        const auto &car = traffic.Vehicles()[i];
        float height = traffic.RenderElevation(i);
        auto bikeHitsCar = [&](bool swept) {
            Vector2 d = {std::cos(world.bikeYaw), std::sin(world.bikeYaw)};
            for (float offset : {-.4023f, 0.f, .4023f})
            {
                auto p = Add(world.bikePosition, d, offset);
                if (swept ? SweptCarContains(car, p, .22271f) : CarContains(car, p, .22271f))
                {
                    return true;
                }
            }
            return false;
        };
        if (PlayerPresent() && !world.car.mounted && std::fabs(height - PlayerHeight()) < .7f &&
            (world.isBikeMounted ? bikeHitsCar(true) : SweptCarContains(car, world.playerPosition, .18f)))
        {
            float speed = (std::max)(car.speed, world.isBikeMounted ? std::fabs(world.bikeSpeed) : 0);
            if (speed > .8f && civic.playerImpact.invulnerability <= 0)
            {
                auto impulse =
                    Add({}, Unit(Sub(world.playerPosition, car.position)), (std::min)(7.f, 2 + speed));
                bool reckless = world.isBikeMounted && world.bikeSpeed > 3;
                CivicCombat::DamagePlayer(int(8 + speed * 4), impulse);
                if (reckless)
                {
                    Overworld::Traffic().DamageVehicle(car.id, int(speed * 8));
                }
                DeflectBike(Sub(world.playerPosition, car.position));
            }
            if (world.isBikeMounted ? bikeHitsCar(false) : CarContains(car, world.playerPosition, .18f))
            {
                auto contact = world.isBikeMounted ? BikeClosest(car.position) : world.playerPosition;
                auto corrected =
                    PushFromCar(car, contact, world.isBikeMounted ? .22271f : .18f, PlayerHeight());
                world.playerPosition = Add(world.playerPosition, Sub(corrected, contact));
                DeflectBike(Sub(contact, car.position));
                if (world.isBikeMounted)
                {
                    world.bikePosition = world.playerPosition;
                }
                Overworld::Traffic().StopForImpact(car.id);
            }
        }
        if (world.surfaceExploration && ObservedCity() >= 0)
        {
            const auto &surface = *Game::Get().Surface();
            const int city = ObservedCity();
            const auto &region =
                surface.Stream().Snapshot()->planet->Region(Overworld::Cities()[size_t(city)].id);
            auto contact = [&](const ::world::SurfacePoint &point) {
                if (::world::SurfaceGeometry::Distance(surface.Planet().radiusMeters, point,
                                                       region.center.position) > 300)
                {
                    return false;
                }
                auto local =
                    ::world::SurfaceGeometry::ToLocal(region.frame, point, surface.Planet().radiusMeters);
                auto center = Overworld::Cities()[size_t(city)].center;
                return std::abs(float(local.z / 2) - height) < .9f &&
                       SweptCarContains(car, {center.x + float(local.x / 2), center.y + float(local.y / 2)},
                                        .3f);
            };
            if (auto *party = Game::Get().SurfaceParty())
            {
                for (size_t n = 0; n < party->State().members.size(); ++n)
                {
                    const auto &member = party->State().members[n];
                    if (member.joined && member.place != CompanionPlace::Mine && member.combat.health > 0 &&
                        contact(member.position))
                    {
                        if (car.speed > .8f)
                        {
                            party->Damage(CompanionId(n), int(5 + car.speed * 7), false);
                        }
                        traffic.StopForImpact(car.id);
                    }
                }
            }
            for (const auto &record : planetary.officers)
            {
                auto o = std::find_if(civic.officers.begin(), civic.officers.end(),
                                      [&](const auto &v) { return v.id == record.id; });
                if (o != civic.officers.end() && o->health > 0 && contact(record.position))
                {
                    if (car.speed > .8f && o->impact.invulnerability <= 0)
                    {
                        CivicCombat::DamageOfficer(size_t(o - civic.officers.begin()), int(5 + car.speed * 7),
                                                   {}, false);
                    }
                    traffic.StopForImpact(car.id);
                }
            }
        }
        if (height > .7f)
        {
            continue;
        }
        for (size_t person = 0; person < world.residents.size(); ++person)
        {
            auto &resident = world.residents[person];
            if (person == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined)
            {
                continue;
            }
            if (ResidentPresent(resident) && resident.health > 0 && resident.impact.invulnerability <= 0 &&
                SweptCarContains(car, resident.position, .3f))
            {
                float impactSpeed = car.speed;
                if (impactSpeed > .8f)
                {
                    CivicCombat::DamageResident(
                        person, int(5 + impactSpeed * 7),
                        Add({}, Unit(Sub(resident.position, car.position)), (std::min)(7.f, impactSpeed)),
                        false);
                }
                if (CarContains(car, resident.position, .3f))
                {
                    resident.position = PushFromCar(car, resident.position, .3f);
                    if (impactSpeed > .05f)
                    {
                        Overworld::Traffic().StopForImpact(car.id);
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
    {
        const auto &car = traffic.Vehicles()[i];
        if (traffic.RenderElevation(i) > .7f)
        {
            continue;
        }
        for (size_t person = 0; person < civic.officers.size(); ++person)
        {
            auto &officer = civic.officers[person];
            if (!OfficersPresent() || officer.health <= 0 || officer.impact.invulnerability > 0 ||
                !SweptCarContains(car, officer.position, .3f))
            {
                continue;
            }
            float speed = car.speed;
            if (speed > .8f)
            {
                CivicCombat::DamageOfficer(
                    person, int(5 + speed * 7),
                    Add({}, Unit(Sub(officer.position, car.position)), (std::min)(7.f, speed)), false);
            }
            if (CarContains(car, officer.position, .3f))
            {
                officer.position = PushFromCar(car, officer.position, .3f);
                if (speed > .05f)
                {
                    Overworld::Traffic().StopForImpact(car.id);
                }
            }
        }
    }
    if (PlayerPresent() && world.isBikeMounted && PlayerHeight() < .7f)
    {
        for (size_t i = 0; i < world.residents.size(); ++i)
        {
            auto &resident = world.residents[i];
            if (i == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined)
            {
                continue;
            }
            if (!ResidentPresent(resident) || resident.health <= 0 || !BikeTouches(resident.position, .18f))
            {
                continue;
            }
            if (world.bikeSpeed > 2 && resident.impact.invulnerability <= 0)
            {
                CivicCombat::DamageResident(i, int(world.bikeSpeed * 9),
                                            Add({}, Unit(Sub(resident.position, world.bikePosition)), 5),
                                            false);
            }
            SeparateBikeFromPerson(resident.position);
        }
        for (size_t i = 0; i < civic.officers.size(); ++i)
        {
            if (OfficersPresent() && civic.officers[i].health > 0 &&
                BikeTouches(civic.officers[i].position, .18f))
            {
                if (world.bikeSpeed > 2 && civic.officers[i].impact.invulnerability <= 0)
                {
                    CivicCombat::DamageOfficer(
                        i, int(world.bikeSpeed * 9),
                        Add({}, Unit(Sub(civic.officers[i].position, world.bikePosition)), 5), false);
                }
                SeparateBikeFromPerson(civic.officers[i].position);
            }
        }
        for (size_t i = 0; i < std::size(kPeople); ++i)
        {
            if (LandmarkPresent(i) && civic.landmarkHealth[i] > 0 &&
                (i != 2 || game::GameLevels::Chapter().GetState().kainJoined) &&
                BikeTouches(kPeople[i], .18f))
            {
                if (world.bikeSpeed > 2)
                {
                    CivicCombat::DamageLandmark(i, int(world.bikeSpeed * 9), false);
                }
                SeparateBikeFromPerson(kPeople[i]);
            }
        }
    }
    if (!world.surfaceExploration)
    {
        RecoverBike();
    }
}

bool HospitalRecovery::Active()
{
    return civic.recovery != RecoveryPhase::None;
}
void HospitalRecovery::Begin()
{
    if (Active())
    {
        return;
    }
    WeaponWheel::Close();
    auto &world = Game::Get().GetState();
    auto source = game::GameLevels::Mine().IsActive()      ? Overworld::kMineEntrance
                  : game::GameLevels::Chapter().IsActive() ? kCityReceiverPosition
                                                           : world.playerPosition;
    float best = 1.e9f;
    civic.hospital = 0;
    for (size_t i = 0; i < civic.hospitals.size(); ++i)
    {
        float distance = Game::Distance(source, civic.hospitals[i].position);
        if (world.surfaceExploration && Game::Get().SurfaceCities())
        {
            auto location = Game::Get().PlanetLocation();
            auto destination = Game::Get().SurfaceCities()->Point(
                *Game::Get().Surface(), civic.hospitals[i].city, civic.hospitals[i].position);
            if (location && destination)
            {
                distance = float(::world::SurfaceGeometry::Distance(
                    Game::Get().Surface()->Planet().radiusMeters, location->position, *destination));
            }
        }
        if (distance < best)
        {
            best = distance;
            civic.hospital = int(i);
        }
    }
    Dialogue::Interrupt();
    civic.recovery = RecoveryPhase::FadeOut;
    civic.recoverySeconds = 0;
    world.mouseFiring = world.mouseAiming = false;
    world.mapVisible = false;
    world.isPaused = false;
    world.StopMotion();
    std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
}
void HospitalRecovery::Update(float dt)
{
    auto &stats = game::GameLevels::Mine().GetState().player;
    if (!Active())
    {
        if (stats.health <= 0)
        {
            Begin();
        }
        return;
    }
    civic.recoverySeconds += dt;
    if (civic.recovery == RecoveryPhase::FadeOut && civic.recoverySeconds >= .85f)
    {
        civic.recoverySeconds -= .85f;
        civic.recovery = RecoveryPhase::Transfer;
        TransferToHospital();
    }
    if (civic.recovery == RecoveryPhase::Transfer && civic.recoverySeconds >= .35f)
    {
        civic.recoverySeconds -= .35f;
        civic.recovery = RecoveryPhase::FadeIn;
    }
    if (civic.recovery == RecoveryPhase::FadeIn && civic.recoverySeconds >= 1.1f)
    {
        civic.recovery = RecoveryPhase::None;
        Hud::OpenMenu(UiScreen::Medical);
        civic.recoverySeconds = 0;
    }
}
float HospitalRecovery::Opacity()
{
    if (civic.recovery == RecoveryPhase::None)
    {
        return 0;
    }
    if (civic.recovery == RecoveryPhase::FadeOut)
    {
        return Game::Clamp(civic.recoverySeconds / .85f, 0, 1);
    }
    if (civic.recovery == RecoveryPhase::Transfer)
    {
        return 1;
    }
    return Game::Clamp(1 - civic.recoverySeconds / 1.1f, 0, 1);
}
void CivicPresentation::DrawRecovery()
{
    Hud::DrawMedicalFade();
}
void CivicPresentation::DrawWorld()
{
    if (!CivicJustice::IsExterior())
    {
        return;
    }
    auto &renderer = GameRenderer::Get().Backend();
    const bool surfaceMode = Game::Get().GetState().surfaceExploration;
    if (!surfaceMode)
    {
        renderer.BeginModels();
    }
    for (const auto &officer : civic.officers)
    {
        if (officer.corpseSeconds > 20 &&
            TrafficSystem::OutsideView(officer.position, Game::Get().GetState().cameraPosition, 80))
        {
            continue;
        }
        float elevation = 0;
        if (surfaceMode)
        {
            auto record = std::find_if(planetary.officers.begin(), planetary.officers.end(),
                                       [&](const auto &r) { return r.id == officer.id; });
            if (record == planetary.officers.end() ||
                Game::Distance(officer.position, Game::Get().GetState().playerPosition) > 130)
            {
                continue;
            }
            elevation = PlanetSecurity::Elevation(record->position);
        }
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -elevation * 44, elevation * (1296.f / 44));
        Drawing::DrawPerson(
            officer.position, {.18f, .36f, .48f}, false,
            officer.tier >= 3 ? models::CharacterType::HeavyCombat : models::CharacterType::Guard,
            officer.facing.walking, officer.facing.yaw, models::Gender::Male, models::HairStyle::Buzz, true,
            officer.muzzleFlash, officer.health <= 0 || officer.impact.response == BodyResponse::Knockdown,
            2.5f, officer.weapon.IsReloading() ? officer.weapon.GetReloadProgress() : -1);
        renderqueue::PopMatrix();
    }
    if (!surfaceMode)
    {
        renderer.EndModels();
    }
    for (const auto &hospital : civic.hospitals)
    {
        if (surfaceMode)
        {
            continue;
        }
        auto p = Drawing::WorldToScreen(hospital.position.x, hospital.position.y, 35);
        Drawing::DrawLabel(p.x - 8, p.y, "+", {.35f, 1, .72f}, LabelFont::Large);
        if (Game::Distance(hospital.position, Game::Get().GetState().playerPosition) < 3)
        {
            Drawing::DrawLabel(p.x - 30, p.y - 18,
                               std::string(Overworld::Cities()[hospital.city].name) + " 의료소",
                               {.6f, 1, .8f});
        }
    }
    for (const auto &tracer : civic.tracers)
    {
        auto a =
            Drawing::WorldToScreen(tracer.start.x, tracer.start.y,
                                   surfaceMode ? tracer.elevation * 44 : models::WorldScale::aimHeightPixels);
        auto b =
            Drawing::WorldToScreen(tracer.end.x, tracer.end.y,
                                   surfaceMode ? tracer.elevation * 44 : models::WorldScale::aimHeightPixels);
        Drawing::DrawLine(a, b,
                          tracer.npcOwned ? ColorRGBA(.25f, .8f, 1, tracer.remaining / .12f)
                                          : ColorRGBA(1, .83f, .38f, tracer.remaining / .12f),
                          2);
    }
    for (const auto &t : civic.airTracers)
    {
        Drawing::DrawLine(Drawing::WorldToScreen(t.start.x, t.start.y, t.start.z * 44),
                          Drawing::WorldToScreen(t.end.x, t.end.y, t.end.z * 44),
                          {1, .83f, .38f, t.remaining / .12f}, 2);
    }
}
void CivicPresentation::DrawHud()
{
    // Runtime HUD lives in Hud; retain the existing public entry for the medical overlay.
    Hud::DrawMedicalFade();
}
} // namespace tutorial
