#include "stdafx.h"
#include "Civic.h"
#include "Profiler.h"
#include "Drawing.h"
#include "Hud.h"
#include "Overworld.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

namespace tutorial
{
namespace
{
CivicState civic;
void ClearPursuit();
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
    while (!open.empty() && ++expansions < 2400)
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
                if (int(index) != ignoreResident && world.residents[index].health > 0 &&
                    !(index == Overworld::kKainResidentSlot &&
                      game::GameLevels::Chapter().GetState().kainJoined))
                {
                    separate(world.residents[index].position, .18f);
                }
            }
            for (size_t index = 0; index < std::size(kPeople); ++index)
            {
                if (civic.landmarkHealth[index] > 0 &&
                    (index != 2 || game::GameLevels::Chapter().GetState().kainJoined))
                {
                    separate(kPeople[index], .18f);
                }
            }
            for (size_t index = 0; index < civic.officers.size(); ++index)
            {
                const auto &officer = civic.officers[index];
                if (officer.health > 0 && int(index) != ignoreOfficer)
                {
                    separate(officer.position, .18f);
                }
            }
            if (!player && !world.isBikeMounted)
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
    world.playerPosition = world.cameraPosition = hospital.position;
    world.StopMotion();
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
    std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
    civic.heat = 0;
    ClearPursuit();
    civic.officers.clear();
    civic.bullets.clear();
    civic.tracers.clear();
    civic.airBullets.clear();
    civic.airTracers.clear();
    civic.playerImpact = {};
    civic.playerGrace = 5;
    mine.invulnerability = 5;
    ++civic.deaths;
    Game::Get().ShowNotice(
        std::string(Overworld::Cities()[hospital.city].name) + " 의료소 / 레벨 -" +
        std::to_string(civic.lostLevels) + " · 크레딧 -" + std::to_string(civic.lostCredits) +
        " · 거래 부품 -" + std::to_string(civic.lostSalvage) +
        ". 조사 기록은 유지되며 보안망의 평판은 남습니다." +
        (world.dialogue.suspended ? " J 기록에서 Enter로 중단한 대화를 재개할 수 있습니다." : ""));
}
} // namespace

CivicState &CivicJustice::State()
{
    return civic;
}
bool CivicJustice::IsExterior()
{
    const auto &world = Game::Get().GetState();
    return !game::GameLevels::Active() && !world.isInsideHome && !world.isInObservatory;
}
bool CivicJustice::Solid(Vector2 p, float radius, float altitude, bool driving)
{
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
    return SegmentClear(from, to, .05f);
}
void CivicJustice::Reset(GameState &world)
{
    civic = CivicState{};
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
        count += (civic.landmarkHealth[0] > 0) + (civic.landmarkHealth[1] > 0);
        count += kain && civic.landmarkHealth[2] > 0;
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
    if (officer.health <= 0 || altitude >= .8f ||
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
        if (person.health > 0 && Game::Distance(person.position, position) < 12 &&
            ClearLine(position, person.position))
        {
            person.fearSeconds = (std::max)(person.fearSeconds, 8.f);
        }
    }
    for (auto &officer : civic.officers)
    {
        if (officer.health > 0)
        {
            officer.lastKnown = position;
        }
    }
    if (civic.wanted)
    {
        civic.spawnSeconds = (std::min)(civic.spawnSeconds, 2.f);
    }
}
void CivicJustice::Update(float dt)
{
    if (dt <= 0 || HospitalRecovery::Active())
    {
        return;
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
        if (!IsExterior())
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
        }
        if (civic.wanted == 0)
        {
            officer.lastKnown = Add(officer.position, Unit(Sub(officer.position, world.playerPosition)), 15);
        }
        officer.navigationSeconds -= dt;
        if (officer.navigationSeconds <= 0)
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
                    Separate(officer.position, next, .3f, -1, false, int(&officer - civic.officers.data()));
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
                civic.bullets.push_back({officer.position,
                                         {std::cos(shot.angle), std::sin(shot.angle)},
                                         shot.speed,
                                         shot.range,
                                         power,
                                         false});
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
    if (IsExterior() && civic.wanted > 0 && civic.spawnSeconds <= 0 && living < size_t(civic.wanted * 2) &&
        civic.officers.size() < 16)
    {
        civic.spawnSeconds = 3.5f;
        for (int attempt = 0; attempt < 64; ++attempt)
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
}

bool CivicCombat::Available()
{
    const auto &world = Game::Get().GetState();
    return CivicJustice::IsExterior() && !world.isBikeMounted && !HospitalRecovery::Active() &&
           !world.isPaused && !world.mapVisible && !world.weaponWheel.visible && !world.journalVisible &&
           !world.dialogueHistoryVisible && !Dialogue::LocksWorld();
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
        CivicJustice::Report(person.health ? Offense::Assault : Offense::Homicide, person.position);
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
        CivicJustice::Report(officer.health ? Offense::AssaultOfficer : Offense::OfficerKilled,
                             officer.position);
    }
    return true;
}
bool CivicCombat::DamageLandmark(size_t index, int damage, bool weaponOwned)
{
    if (index >= civic.landmarkHealth.size() ||
        (index == 2 && !game::GameLevels::Chapter().GetState().kainJoined) ||
        civic.landmarkHealth[index] <= 0 || damage <= 0)
    {
        return false;
    }
    civic.landmarkHealth[index] = (std::max)(0, civic.landmarkHealth[index] - damage);
    civic.landmarkRecovery[index] = 60;
    if (weaponOwned)
    {
        CivicJustice::Report(civic.landmarkHealth[index] ? Offense::Assault : Offense::AggravatedAssault,
                             kPeople[index]);
    }
    return true;
}
bool CivicCombat::LandmarkDown(Vector2 position)
{
    for (size_t i = 0; i < std::size(kPeople); ++i)
    {
        if (civic.landmarkHealth[i] <= 0 && Game::Distance(position, kPeople[i]) < .6f)
        {
            return true;
        }
    }
    return false;
}
namespace
{
bool AimAtAirTraffic(float shotAngle, models::Point &target)
{
    const auto &w = Game::Get().GetState();
    float best = 1.e20f;
    bool found = false;
    auto muzzle = WorldCamera::ToViewport(
        Drawing::WorldToScreen(w.playerPosition.x, w.playerPosition.y, models::WorldScale::aimHeightPixels));
    float radius = Game::Distance(w.cursorPosition, muzzle);
    auto cursor = WorldCamera::FromViewport(
        {muzzle.x + std::cos(shotAngle) * radius, muzzle.y + std::sin(shotAngle) * radius});
    for (const auto &craft : Overworld::Flights().Aircraft())
    {
        if (craft.health <= 0)
        {
            continue;
        }
        const auto &mesh = models::ResourceAssets::Get(
            models::ModelLibrary::Aircraft(craft.type, craft.alternate).resourceId);
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
    if (!Available() || civic.meleeSeconds > 0)
    {
        return;
    }
    auto &world = Game::Get().GetState();
    auto &mine = game::GameLevels::Mine();
    if (!mine.GetState().loadout.IsPistolEquipped())
    {
        Melee();
        return;
    }
    combat::Shot shot;
    if (civic.bullets.size() + civic.airBullets.size() >= 96 ||
        !mine.GetState().pistol.TryFire(game::GameLevels::FireAngle(), shot))
    {
        return;
    }
    world.playerAnimation.Fire();
    models::Point target{};
    if (Game::Get().IsAiming() && AimAtAirTraffic(shot.angle, target))
    {
        models::Point origin{world.playerPosition.x, world.playerPosition.y,
                             models::WorldScale::aimHeightPixels / 44};
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
                             shot.range, shot.damage + (mine.GetState().player.level - 1) * 4, true});
    civic.muzzleFlash = .06f;
}
void CivicCombat::Melee()
{
    if (!Available() || civic.meleeSeconds > 0)
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
        civic.tracers.push_back({start, bullet.position, .12f, !bullet.playerOwned});
    }
    civic.bullets.erase(std::remove_if(civic.bullets.begin(), civic.bullets.end(),
                                       [](const game::Bullet &b) { return b.remaining <= 0; }),
                        civic.bullets.end());
    if (civic.tracers.size() > 160)
    {
        civic.tracers.erase(civic.tracers.begin(), civic.tracers.end() - 160);
    }
}

Vector2 BodyCollision::Slide(Vector2 from, Vector2 desired, const std::function<bool(Vector2)> &blocked)
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
    // Smaller than the narrowest bike contact probe: a fast step cannot skip a thin fence/edge.
    int steps = (std::max)(1, int(std::ceil(std::hypot(displacement.x, displacement.y) / .02f)));
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
void BodyCollision::Update(float dt)
{
    if (!CivicJustice::IsExterior())
    {
        return;
    }
    auto &world = Game::Get().GetState();
    if (!world.isBikeMounted)
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
void BodyCollision::VehicleContacts(float)
{
    if (!CivicJustice::IsExterior() || HospitalRecovery::Active())
    {
        return;
    }
    auto &world = Game::Get().GetState();
    const auto &traffic = Overworld::Traffic();
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
        if (std::fabs(height - PlayerHeight()) < .7f &&
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
            if (resident.health > 0 && resident.impact.invulnerability <= 0 &&
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
            if (officer.health <= 0 || officer.impact.invulnerability > 0 ||
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
    if (world.isBikeMounted && PlayerHeight() < .7f)
    {
        for (size_t i = 0; i < world.residents.size(); ++i)
        {
            auto &resident = world.residents[i];
            if (i == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined)
            {
                continue;
            }
            if (resident.health <= 0 || !BikeTouches(resident.position, .18f))
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
            if (civic.officers[i].health > 0 && BikeTouches(civic.officers[i].position, .18f))
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
            if (civic.landmarkHealth[i] > 0 &&
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
    RecoverBike();
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
    renderer.BeginModels();
    for (const auto &officer : civic.officers)
    {
        if (officer.corpseSeconds > 20 &&
            TrafficSystem::OutsideView(officer.position, Game::Get().GetState().cameraPosition, 80))
        {
            continue;
        }
        Drawing::DrawPerson(
            officer.position, {.18f, .36f, .48f}, false,
            officer.tier >= 3 ? models::CharacterType::HeavyCombat : models::CharacterType::Guard,
            officer.facing.walking, officer.facing.yaw, models::Gender::Male, models::HairStyle::Buzz, true,
            officer.muzzleFlash, officer.health <= 0 || officer.impact.response == BodyResponse::Knockdown,
            2.5f, officer.weapon.IsReloading() ? officer.weapon.GetReloadProgress() : -1);
    }
    renderer.EndModels();
    for (const auto &hospital : civic.hospitals)
    {
        auto p = Drawing::WorldToScreen(hospital.position.x, hospital.position.y, 35);
        Drawing::DrawLabel(p.x - 8, p.y, "+", {.35f, 1, .72f}, GLUT_BITMAP_HELVETICA_18);
        if (Game::Distance(hospital.position, Game::Get().GetState().playerPosition) < 3)
        {
            Drawing::DrawLabel(p.x - 30, p.y - 18,
                               std::string(Overworld::Cities()[hospital.city].name) + " 의료소",
                               {.6f, 1, .8f});
        }
    }
    for (const auto &tracer : civic.tracers)
    {
        auto a = Drawing::WorldToScreen(tracer.start.x, tracer.start.y, models::WorldScale::aimHeightPixels);
        auto b = Drawing::WorldToScreen(tracer.end.x, tracer.end.y, models::WorldScale::aimHeightPixels);
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
