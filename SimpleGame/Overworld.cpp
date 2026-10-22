#include "stdafx.h"
#include "Overworld.h"
#include "ResourceAssets.h"
#include "Level.h"
#include "ResidentAI.h"
#include "TransitLayout.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace tutorial
{
namespace
{
Vector2 Add(Vector2 a, Vector2 b)
{
    return {a.x + b.x, a.y + b.y};
}
Vector2 Sub(Vector2 a, Vector2 b)
{
    return {a.x - b.x, a.y - b.y};
}
Vector2 Mul(Vector2 a, float s)
{
    return {a.x * s, a.y * s};
}
float Dot(Vector2 a, Vector2 b)
{
    return a.x * b.x + a.y * b.y;
}
float Cross(Vector2 a, Vector2 b)
{
    return a.x * b.y - a.y * b.x;
}
Vector2 Unit(Vector2 v)
{
    return Mul(v, 1.f / (std::max)(.001f, std::sqrt(Dot(v, v))));
}
TrafficTurn ClassifyTurn(Vector2 in, Vector2 out)
{
    return Dot(in, out) > .82f   ? TrafficTurn::Straight
           : Cross(in, out) >= 0 ? TrafficTurn::Left
                                 : TrafficTurn::Right;
}
float SegmentDistance(Vector2 p, Vector2 a, Vector2 b)
{
    auto d = Sub(b, a);
    return Game::Distance(
        p, Add(a, Mul(d, Game::Clamp(Dot(Sub(p, a), d) / (std::max)(.0001f, Dot(d, d)), 0, 1))));
}
Vector2 Bezier(Vector2 a, Vector2 c, Vector2 d, Vector2 b, float t)
{
    float s = 1 - t;
    return Add(Add(Mul(a, s * s * s), Mul(c, 3 * s * s * t)), Add(Mul(d, 3 * s * t * t), Mul(b, t * t * t)));
}
Vector2 TurnPoint(const TrafficVehicle &car, float t)
{
    const auto &net = RoadNetwork::Get();
    const auto &in = net.lanes[car.lane], &out = net.lanes[car.nextLane];
    const float dot = Dot(in.direction, out.direction);
    const float chord = Game::Distance(in.end, out.start);
    // Quarter-circle control points match the native 90-degree road module's lane arcs.
    float handle = chord * (std::fabs(dot) < .001f ? .3905243f : dot > .99f ? 1.f / 3 : 2.f / 3);
    return Bezier(in.end, Add(in.end, Mul(in.direction, handle)), Sub(out.start, Mul(out.direction, handle)),
                  out.start, t);
}
bool Signalized(const RoadJunction &j)
{
    const auto &net = RoadNetwork::Get();
    int roads = 0, last = -1;
    for (int lane : j.outgoing)
    {
        if (net.lanes[lane].road != last)
        {
            ++roads;
            last = net.lanes[lane].road;
        }
    }
    return roads >= 3;
}
} // namespace

// ============================================================================
// RoadNetwork — split every crossing so geometry and vehicle routes agree
// ============================================================================
const RoadNetwork &RoadNetwork::Get()
{
    static const RoadNetwork network;
    return network;
}
RoadNetwork::RoadNetwork()
{
    struct Source
    {
        Vector2 a, b;
        int lanes;
        float speed;
    };
    std::vector<Source> sources;
    auto add = [&](Vector2 a, Vector2 b, int lanes, float speed) { sources.push_back({a, b, lanes, speed}); };
    // Orthogonal city blocks and a regional ring replace overlapping diagonal shortcut spokes.
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        auto p = [center](float x, float y) { return Add(center, {x, y}); };
        int lanes = city == 1 ? 6 : city >= 3 ? 4 : 2;
        add(p(-36, -17), p(36, -17), lanes, 6);
        add(p(36, -17), p(36, 33), lanes, 5);
        add(p(36, 33), p(-36, 33), lanes, 5);
        add(p(-36, 33), p(-36, -17), lanes, 5);
        add(p(-36, 10), p(36, 10), lanes, 4);
        add(p(-12, -17), p(-12, 33), lanes, 4);
        if (city == 0)
        {
            continue;
        }
        Vector2 direction, gateway;
        if (std::fabs(center.x) > std::fabs(center.y))
        {
            direction = {center.x > 0 ? 1.f : -1.f, 0};
            gateway = p(direction.x * 36, 10);
        }
        else
        {
            direction = {0, center.y > 0 ? 1.f : -1.f};
            gateway = p(-12, direction.y > 0 ? 33.f : -17.f);
        }
        auto taper = Add(gateway, Mul(direction, 24));
        Vector2 ring =
            direction.x ? Vector2{direction.x * 390, gateway.y} : Vector2{gateway.x, direction.y * 390};
        add(gateway, taper, lanes, 7);
        add(taper, ring, 4, 10);
        auto airDirection = Unit(center);
        Vector2 airNormal = {-airDirection.y, airDirection.x};
        Vector2 hub = {0, 8};
        airCorridors.push_back({Sub(hub, Mul(airDirection, 28)),
                                Add(Add(hub, Mul(airDirection, 30)), Mul(airNormal, 12)),
                                Add(center, {12, 8}), 8.f + city * 3.f});
    }
    add({-390, -390}, {390, -390}, 4, 10);
    add({390, -390}, {390, 390}, 4, 10);
    add({390, 390}, {-390, 390}, 4, 10);
    add({-390, 390}, {-390, -390}, 4, 10);
    add({36, -17}, {60, -17}, 2, 6);
    add({60, -17}, {390, -17}, 4, 10);
    add({128, -17}, Overworld::kMineEntrance, 4, 5);
    std::vector<std::vector<float>> cuts(sources.size(), {0, 1});
    for (size_t i = 0; i < sources.size(); ++i)
    {
        for (size_t j = i + 1; j < sources.size(); ++j)
        {
            auto r = Sub(sources[i].b, sources[i].a), s = Sub(sources[j].b, sources[j].a);
            float denominator = Cross(r, s);
            if (std::fabs(denominator) < .001f)
            {
                continue;
            }
            auto delta = Sub(sources[j].a, sources[i].a);
            float t = Cross(delta, s) / denominator, u = Cross(delta, r) / denominator;
            if (t >= -.0001f && t <= 1.0001f && u >= -.0001f && u <= 1.0001f)
            {
                cuts[i].push_back(Game::Clamp(t, 0, 1));
                cuts[j].push_back(Game::Clamp(u, 0, 1));
            }
        }
    }
    auto node = [&](Vector2 p) {
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            if (Game::Distance(p, junctions[i].position) < .12f)
            {
                return int(i);
            }
        }
        junctions.push_back({p, 3, {}});
        return int(junctions.size() - 1);
    };
    for (size_t i = 0; i < sources.size(); ++i)
    {
        auto &split = cuts[i];
        std::sort(split.begin(), split.end());
        for (size_t j = 1; j < split.size(); ++j)
        {
            auto a = Add(sources[i].a, Mul(Sub(sources[i].b, sources[i].a), split[j - 1]));
            auto b = Add(sources[i].a, Mul(Sub(sources[i].b, sources[i].a), split[j]));
            if (Game::Distance(a, b) < .2f)
            {
                continue;
            }
            int from = node(a), to = node(b);
            if (from == to)
            {
                continue;
            }
            roads.push_back({from, to, sources[i].lanes, sources[i].speed});
        }
    }
    TransitLayout::ConfigureJunctions(*this);
    for (size_t i = 0; i < roads.size(); ++i)
    {
        const auto &road = roads[i];
        for (int direction = 0; direction < 2; ++direction)
        {
            int from = direction ? road.to : road.from, to = direction ? road.from : road.to;
            auto d = Unit(Sub(junctions[to].position, junctions[from].position));
            Vector2 right = RightOf(d);
            for (int slot = 0; slot < road.lanes / 2; ++slot)
            {
                auto offset = Mul(right, (slot + .5f) * kLaneWidth);
                auto start = Add(Add(junctions[from].position, Mul(d, junctions[from].radius)), offset);
                auto end = Add(Sub(junctions[to].position, Mul(d, junctions[to].radius)), offset);
                junctions[from].outgoing.push_back(int(lanes.size()));
                lanes.push_back(
                    {int(i), from, to, slot, start, end, d, Game::Distance(start, end), road.speedLimit});
            }
        }
    }
}
bool RoadNetwork::Contains(Vector2 position, float margin) const
{
    for (const auto &road : roads)
    {
        auto a = junctions[road.from].position, b = junctions[road.to].position;
        auto direction = Unit(Sub(b, a));
        auto start = Add(a, Mul(direction, junctions[road.from].radius));
        auto end = Sub(b, Mul(direction, junctions[road.to].radius));
        const auto offset = Sub(position, start);
        const float along = Dot(offset, direction);
        if (along >= -margin && along <= Game::Distance(start, end) + margin &&
            std::fabs(Cross(offset, direction)) <= road.lanes * kLaneWidth * .5f + margin)
        {
            return true;
        }
    }
    for (const auto &junction : junctions)
    {
        if (TransitLayout::ContainsJunction(int(&junction - junctions.data()), position, margin))
        {
            return true;
        }
    }
    return false;
}
Vector2 RoadNetwork::LanePosition(int lane, float distance) const
{
    return Add(lanes[lane].start, Mul(lanes[lane].direction, Game::Clamp(distance, 0, lanes[lane].length)));
}
bool RoadNetwork::CanTurn(int index, TrafficTurn turn) const
{
    const auto &lane = lanes[index];
    bool straight = false, left = false, right = false;
    int arms = 0;
    for (int outIndex : junctions[lane.to].outgoing)
    {
        const auto &out = lanes[outIndex];
        if (out.slot == 0)
        {
            ++arms;
        }
        if (out.road == lane.road)
        {
            continue;
        }
        auto direction = ClassifyTurn(lane.direction, out.direction);
        straight |= direction == TrafficTurn::Straight;
        left |= direction == TrafficTurn::Left;
        right |= direction == TrafficTurn::Right;
    }
    if (arms == 1)
    {
        return turn == TrafficTurn::Left;
    }
    bool exists = turn == TrafficTurn::Straight ? straight : turn == TrafficTurn::Left ? left : right;
    if (!exists)
    {
        return false;
    }
    if (arms == 2 || turn == TrafficTurn::Straight)
    {
        return true;
    }
    int count = roads[lane.road].lanes / 2;
    if (turn == TrafficTurn::Left)
    {
        return lane.slot == 0 || (!straight && (!right || lane.slot < count - 1));
    }
    return lane.slot == count - 1 || (!straight && !left);
}

Vector2 RoadNetwork::RightOf(Vector2 direction)
{
    // Game-space +Y is left of +X, independent of the isometric screen projection.
    return {direction.y, -direction.x};
}
bool RoadNetwork::IsSignalized(int junction) const
{
    return junction >= 0 && junction < int(junctions.size()) && Signalized(junctions[junction]);
}
int RoadNetwork::SignalAxis(int index) const
{
    const auto &lane = lanes[index];
    const auto &junction = junctions[lane.to];
    int reference = junction.outgoing.front();
    for (int candidate : junction.outgoing)
    {
        if (roads[lanes[candidate].road].lanes > roads[lanes[reference].road].lanes)
        {
            reference = candidate;
        }
    }
    return std::fabs(Dot(lane.direction, lanes[reference].direction)) >= .7071f ? 0 : 1;
}

// ============================================================================
// TrafficSystem — simulation is bounded; rendering never advances vehicle state
// ============================================================================
TrafficSystem &Overworld::Traffic()
{
    static TrafficSystem traffic;
    return traffic;
}
unsigned int TrafficSystem::Random()
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
void TrafficSystem::Reset()
{
    vehicles.clear();
    signals.assign(RoadNetwork::Get().junctions.size(), {});
    randomState = 0x71b39a5u;
    nextId = 0;
    accumulator = spawnTimer = impactCooldown = 0;
    for (size_t i = 0; i < signals.size(); ++i)
    {
        signals[i].phase = static_cast<SignalPhase>(int(i % 4) * 3);
    }
}
bool TrafficSystem::OutsideView(Vector2 p, Vector2 camera, float padding, float altitude)
{
    Vector2 screen = WorldCamera::ToViewport(
        {kCanvasWidth * .5f + (p.x - p.y - camera.x + camera.y) * 36,
         kCanvasHeight * .57f + (p.x + p.y - camera.x - camera.y) * 18 - altitude * 44});
    return screen.x < -padding || screen.x > kCanvasWidth + padding || screen.y < -padding ||
           screen.y > kCanvasHeight + padding;
}
Vector2 TrafficSystem::RenderPosition(size_t index) const
{
    const auto &car = vehicles[index];
    return Add(car.previousPosition, Mul(Sub(car.position, car.previousPosition), accumulator * 30));
}
float TrafficSystem::RenderYaw(size_t index) const
{
    const auto &car = vehicles[index];
    return car.previousYaw + std::remainder(car.yaw - car.previousYaw, 2 * kPi) * accumulator * 30;
}
TrafficLight TrafficSystem::Light(int laneIndex, TrafficTurn turn) const
{
    const auto &net = RoadNetwork::Get();
    const auto &lane = net.lanes[laneIndex];
    if (!net.CanTurn(laneIndex, turn))
    {
        return TrafficLight::Red;
    }
    if (!Signalized(net.junctions[lane.to]))
    {
        return TrafficLight::Green;
    }
    if (signals.empty())
    {
        return TrafficLight::Red;
    }
    int phase = int(signals[lane.to].phase);
    int axis = net.SignalAxis(laneIndex) * 6;
    int greenPhase = axis + (turn == TrafficTurn::Left ? 3 : 0);
    if (phase == greenPhase)
    {
        return TrafficLight::Green;
    }
    return phase == greenPhase + 1 ? TrafficLight::Amber : TrafficLight::Red;
}
bool TrafficSystem::PedestrianGreen(int junction) const
{
    return junction >= 0 && junction < int(signals.size()) && signals[junction].phase == SignalPhase::Walk &&
           Signalized(RoadNetwork::Get().junctions[junction]);
}
float TrafficSystem::PhaseDuration(int junction) const
{
    const auto phase = signals.at(size_t(junction)).phase;
    if (phase == SignalPhase::Walk)
    {
        float width = 0;
        const auto &net = RoadNetwork::Get();
        for (int lane : net.junctions[junction].outgoing)
        {
            width = (std::max)(width, net.roads[net.lanes[lane].road].lanes * RoadNetwork::kLaneWidth);
        }
        return (std::max)(8.f, (width + 1.9f) / .5f + 3.f);
    }
    if (phase == SignalPhase::WalkClear)
    {
        return 2;
    }
    return int(phase) % 3 == 0 ? (int(phase) % 6 == 0 ? 8.f : 4.f) : 1.5f;
}
bool TrafficSystem::CanStartCrossing(int junction, float travelSeconds) const
{
    return PedestrianGreen(junction) &&
           PhaseDuration(junction) - signals[junction].elapsed >= travelSeconds + 1.f;
}
bool TrafficSystem::CrossingOccupied(const GameState &world, int junction) const
{
    if (!world.isBikeMounted && RoadNetwork::Get().Contains(world.playerPosition, .5f) &&
        NearbyCrossing(world.playerPosition) == junction)
    {
        return true;
    }
    return std::any_of(
        world.residents.begin(), world.residents.end(), [&world, junction](const Resident &resident) {
            if (&resident - world.residents.data() == Overworld::kKainResidentSlot &&
                game::GameLevels::Chapter().GetState().kainJoined)
            {
                return false;
            }
            return resident.activity == ResidentActivity::Crossing && resident.crossingJunction == junction;
        });
}
int TrafficSystem::NearbyCrossing(Vector2 position) const
{
    const auto &net = RoadNetwork::Get();
    int result = -1;
    float best = 3.2f;
    for (size_t i = 0; i < net.junctions.size(); ++i)
    {
        const auto &j = net.junctions[i];
        if (!Signalized(j))
        {
            continue;
        }
        for (int index : j.outgoing)
        {
            const auto &lane = net.lanes[index];
            auto across = Add(j.position, Mul(lane.direction, j.radius + 1.f));
            Vector2 normal = {-lane.direction.y, lane.direction.x};
            float half = net.roads[lane.road].lanes * RoadNetwork::kLaneWidth * .5f + .7f;
            float distance =
                SegmentDistance(position, Add(across, Mul(normal, half)), Sub(across, Mul(normal, half)));
            if (distance < best)
            {
                best = distance;
                result = int(i);
            }
        }
    }
    return result;
}
bool TrafficSystem::SweptHit(Vector2 from, Vector2 to, float yaw, Vector2 p, float padding)
{
    // Minkowski-expanded vehicle footprint, swept through this fixed simulation step.
    Vector2 direction = {std::cos(yaw), std::sin(yaw)}, side = {-direction.y, direction.x};
    auto a = Sub(p, from), delta = Sub(to, from);
    float minT = 0, maxT = 1;
    for (int axis = 0; axis < 2; ++axis)
    {
        auto basis = axis ? side : direction;
        float extent = (axis ? .58f : 1.45f) + padding;
        float origin = Dot(a, basis), speed = -Dot(delta, basis);
        if (std::fabs(speed) < .00001f)
        {
            if (std::fabs(origin) > extent)
            {
                return false;
            }
            continue;
        }
        float t0 = (-extent - origin) / speed, t1 = (extent - origin) / speed;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        minT = (std::max)(minT, t0);
        maxT = (std::min)(maxT, t1);
        if (minT > maxT)
        {
            return false;
        }
    }
    return true;
}
void TrafficSystem::ChooseExit(TrafficVehicle &car)
{
    const auto &net = RoadNetwork::Get();
    const auto &lane = net.lanes[car.lane];
    std::vector<int> candidates;
    for (int index : net.junctions[lane.to].outgoing)
    {
        const auto &out = net.lanes[index];
        if (out.road == lane.road)
        {
            continue;
        }
        TrafficTurn turn = ClassifyTurn(lane.direction, out.direction);
        if (!net.CanTurn(car.lane, turn))
        {
            continue;
        }
        int targetSlot = turn == TrafficTurn::Right && Signalized(net.junctions[lane.to])
                             ? net.roads[out.road].lanes / 2 - 1
                             : (std::min)(lane.slot, net.roads[out.road].lanes / 2 - 1);
        if (out.slot == targetSlot)
        {
            candidates.push_back(index);
        }
    }
    if (candidates.empty())
    {
        // A terminal or a lane drop has a continuous, reserved U-turn; no teleport/wrap.
        for (int index : net.junctions[lane.to].outgoing)
        {
            if (net.lanes[index].road == lane.road && net.lanes[index].slot == 0)
            {
                candidates.push_back(index);
                break;
            }
        }
    }
    car.nextLane = candidates[Random() % candidates.size()];
    const auto &out = net.lanes[car.nextLane];
    car.turn = ClassifyTurn(lane.direction, out.direction);
    car.turnLength = 0;
    auto previous = TurnPoint(car, 0);
    for (int i = 1; i <= 24; ++i)
    {
        auto p = TurnPoint(car, i / 24.f);
        car.turnLength += Game::Distance(p, previous);
        previous = p;
    }
    car.turnLength = (std::max)(.1f, car.turnLength);
}
void TrafficSystem::Populate(const GameState &world)
{
    const auto &net = RoadNetwork::Get();
    // Candidate positions are sampled locally on every directed lane, even on very long roads.
    for (int attempt = 0; attempt < 100 && vehicles.size() < kVehicleLimit; ++attempt)
    {
        int laneIndex = int(Random() % net.lanes.size());
        const auto &lane = net.lanes[laneIndex];
        float projection = Dot(Sub(world.playerPosition, lane.start), lane.direction);
        float distance =
            Game::Clamp(projection + (int(Random() % 181) - 90), 2, (std::max)(2.f, lane.length - 3));
        if (lane.length < 8)
        {
            continue;
        }
        auto p = net.LanePosition(laneIndex, distance);
        float range = Game::Distance(p, world.playerPosition);
        if (range < 28 || range > 100 || !OutsideView(p, world.cameraPosition, 240) ||
            !OutsideView(p, world.playerPosition, 240))
        {
            continue;
        }
        bool crowded = false;
        for (const auto &car : vehicles)
        {
            if (Game::Distance(p, car.position) < 7)
            {
                crowded = true;
                break;
            }
        }
        if (crowded)
        {
            continue;
        }
        TrafficVehicle car;
        car.id = nextId++;
        car.lane = laneIndex;
        car.distance = distance;
        car.position = car.previousPosition = p;
        car.speed = lane.speedLimit * .65f;
        car.yaw = car.previousYaw = std::atan2(lane.direction.y, lane.direction.x);
        ChooseExit(car);
        vehicles.push_back(car);
    }
}
void TrafficSystem::Update(GameState &world, float dt)
{
    if (signals.empty())
    {
        Reset();
    }
    accumulator += Game::Clamp(dt, 0, .1f);
    // 30 Hz decisions; motion follows the lane continuously between decision boundaries.
    constexpr float step = 1.f / 30;
    while (accumulator >= step)
    {
        Step(world, step);
        accumulator -= step;
    }
}
void TrafficSystem::Step(GameState &world, float dt)
{
    const auto &net = RoadNetwork::Get();
    impactCooldown = (std::max)(0.f, impactCooldown - dt);
    for (size_t i = 0; i < signals.size(); ++i)
    {
        auto &signal = signals[i];
        signal.elapsed += dt;
        float duration = PhaseDuration(int(i));
        if (signal.elapsed < duration)
        {
            continue;
        }
        bool occupied = false;
        for (const auto &car : vehicles)
        {
            if (car.turnDistance >= 0 && net.lanes[car.lane].to == int(i))
            {
                occupied = true;
                break;
            }
        }
        const bool clearance = signal.phase == SignalPhase::WalkClear || int(signal.phase) % 3 == 2;
        const auto next = static_cast<SignalPhase>((int(signal.phase) + 1) % 14);
        if (clearance && (occupied || (next != SignalPhase::Walk && CrossingOccupied(world, int(i)))))
        {
            continue;
        }
        signal.phase = next;
        signal.elapsed = 0;
    }
    // Reservations are rebuilt once per step, including cars entering earlier in this step.
    std::vector<std::uint64_t> occupied(net.junctions.size(), UINT64_MAX);
    for (const auto &car : vehicles)
    {
        if (car.turnDistance >= 0)
        {
            occupied[net.lanes[car.lane].to] = car.id;
        }
    }
    for (auto &car : vehicles)
    {
        car.previousPosition = car.position;
        car.previousYaw = car.yaw;
        const auto &lane = net.lanes[car.lane];
        float target = lane.speedLimit;
        float gap = 10000;
        for (const auto &other : vehicles)
        {
            if (car.id == other.id)
            {
                continue;
            }
            if (other.lane == car.lane && other.turnDistance < 0 && other.distance > car.distance)
            {
                gap = (std::min)(gap, other.distance - car.distance - 3.5f);
            }
            if (car.turnDistance < 0 && other.turnDistance >= 0 && net.lanes[other.lane].to == lane.to)
            {
                gap = (std::min)(gap, lane.length - car.distance - 2);
            }
        }
        if (car.turnDistance < 0)
        {
            bool exitBlocked = false;
            for (const auto &other : vehicles)
            {
                if (other.id != car.id && other.lane == car.nextLane && other.distance < 5)
                {
                    exitBlocked = true;
                    break;
                }
            }
            bool red = Light(car.lane, car.turn) != TrafficLight::Green;
            bool reserved = occupied[lane.to] != UINT64_MAX && occupied[lane.to] != car.id;
            if (red || reserved || exitBlocked)
            {
                gap = (std::min)(gap, lane.length - car.distance - 3.6f);
            }
            if (gap < 15)
            {
                target = (std::min)(target, std::sqrt((std::max)(0.f, 2 * 5.f * (gap - .25f))));
            }
            // Legal pedestrian crossings have an exclusive phase. Drivers also yield to a person
            // who remains on the crossing as it closes; a jaywalker in a live lane can be struck.
            if (PedestrianGreen(lane.to) ||
                (signals[lane.to].phase == SignalPhase::WalkClear && CrossingOccupied(world, lane.to)))
            {
                gap = (std::min)(gap, lane.length - car.distance - 3.6f);
                target = (std::min)(target, (std::max)(0.f, gap * 2));
            }
        }
        else
        {
            target = (std::min)(lane.speedLimit, car.turn == TrafficTurn::Straight ? 5.f : 3.2f);
        }
        car.speed += Game::Clamp(target - car.speed, -7.f * dt, 3.f * dt);
        float travel = car.speed * dt;
        if (car.turnDistance < 0)
        {
            travel = (std::min)(travel, (std::max)(0.f, gap));
            if (travel < car.speed * dt - .0001f)
            {
                car.speed = travel / dt;
            }
            car.distance += travel;
            if (car.distance >= lane.length)
            {
                car.turnDistance = car.distance - lane.length;
                occupied[lane.to] = car.id;
            }
            else
            {
                car.position = net.LanePosition(car.lane, car.distance);
            }
        }
        else
        {
            car.turnDistance += travel;
        }
        if (car.turnDistance >= 0)
        {
            float t = Game::Clamp(car.turnDistance / car.turnLength, 0, 1);
            car.position = TurnPoint(car, t);
            auto tangent =
                Sub(TurnPoint(car, (std::min)(1.f, t + .005f)), TurnPoint(car, (std::max)(0.f, t - .005f)));
            car.yaw = std::atan2(tangent.y, tangent.x);
            if (t >= 1)
            {
                car.lane = car.nextLane;
                car.distance = 0;
                car.turnDistance = -1;
                ChooseExit(car);
            }
        }
        else
        {
            car.yaw = std::atan2(lane.direction.y, lane.direction.x);
        }
        if (impactCooldown <= 0 && car.speed > .8f &&
            SweptHit(car.previousPosition, car.position, car.yaw, world.playerPosition,
                     world.isBikeMounted ? .5f : .25f))
        {
            auto &player = game::GameLevels::Mine().GetState().player;
            int damage = int(Game::Clamp(car.speed * 4, 8, 38));
            player.health = (std::max)(0, player.health - damage);
            impactCooldown = 1.5f;
            world.bikeSpeed = 0;
            Game::Get().ShowNotice(
                "교통 사고 / 체력이 감소했습니다. 바닥의 보행 신호가 청록색일 때 건너세요.");
            if (player.health == 0)
            {
                int city = Overworld::CityAt(world.playerPosition);
                auto center = Overworld::Cities()[city < 0 ? 0 : city].center;
                auto refuge = Add(center, {0, 2});
                for (int attempt = 0; attempt < 400; ++attempt)
                {
                    Vector2 candidate = Add(center, {float(attempt % 20) - 10, float(attempt / 20) - 10});
                    if (!Game::Get().IsPositionBlocked(candidate) && !net.Contains(candidate, 1))
                    {
                        refuge = candidate;
                        break;
                    }
                }
                world.playerPosition = refuge;
                if (world.isBikeMounted)
                {
                    world.bikePosition = world.playerPosition;
                }
                player.health = player.maxHealth;
                impactCooldown = 4;
                Game::Get().ShowNotice(
                    "도시 응급 구조 / 광장으로 이송되었습니다. 조사 기록과 소지품은 보존됩니다.");
            }
        }
    }
    vehicles.erase(
        std::remove_if(vehicles.begin(), vehicles.end(),
                       [&](const TrafficVehicle &car) {
                           return Game::Distance(car.position, world.playerPosition) > kDespawnDistance &&
                                  Game::Distance(car.position, world.cameraPosition) > kDespawnDistance &&
                                  OutsideView(car.position, world.cameraPosition, 320);
                       }),
        vehicles.end());
    spawnTimer -= dt;
    if (spawnTimer <= 0)
    {
        Populate(world);
        spawnTimer = .5f;
    }
}

// ============================================================================
// AirTraffic — extrapolate beyond route ends until the entire craft is offscreen
// ============================================================================
AirTraffic &Overworld::Flights()
{
    static AirTraffic traffic;
    return traffic;
}
void AirTraffic::Reset()
{
    aircraft.clear();
    nextId = 0;
    randomState = 0x85ab32u;
    spawnTimer = 0;
}
Vector2 AirTraffic::Tangent(int index, float t)
{
    const auto &route = RoadNetwork::Get().airCorridors.at(size_t(index));
    t = Game::Clamp(t, 0, 1);
    return Mul(Add(Mul(Sub(route.control, route.start), 1 - t), Mul(Sub(route.end, route.control), t)), 2);
}
Vector2 AirTraffic::Position(int index, float t)
{
    const auto &route = RoadNetwork::Get().airCorridors.at(size_t(index));
    float clamped = Game::Clamp(t, 0, 1), s = 1 - clamped;
    auto point = Add(Add(Mul(route.start, s * s), Mul(route.control, 2 * s * clamped)),
                     Mul(route.end, clamped * clamped));
    return Add(point, Mul(Tangent(index, clamped), t - clamped));
}
bool AirTraffic::OutsideVisibleFootprint(const AircraftActor &craft, Vector2 camera, float padding)
{
    const auto &model = models::ModelLibrary::Aircraft(craft.type, craft.alternate);
    const auto &resource = models::ResourceAssets::Get(model.resourceId);
    models::Transform3D transform;
    transform.yaw = craft.yaw;
    transform.roll = craft.bank;
    transform.position.z = craft.altitude;
    float minX = 1.e20f, minY = 1.e20f, maxX = -1.e20f, maxY = -1.e20f;
    // Include banked hull, moving gear and the renderer's projected, blurred ground shadow.
    for (int corner = 0; corner < 8; ++corner)
    {
        auto p = transform.Apply({corner & 1 ? resource.maximum.x + 1 : resource.minimum.x - 1,
                                  corner & 2 ? resource.maximum.y + 1 : resource.minimum.y - 1,
                                  corner & 4 ? resource.maximum.z + 1 : resource.minimum.z - 1});
        for (int shadow = 0; shadow < 2; ++shadow)
        {
            float x = p.x + (shadow ? .38f * p.z : 0), y = p.y + (shadow ? .55f * p.z : 0),
                  z = shadow ? 0 : p.z;
            auto screen = WorldCamera::ToViewport(
                {kCanvasWidth * .5f +
                     (craft.position.x + x - craft.position.y - y - camera.x + camera.y) * 36,
                 kCanvasHeight * .57f +
                     (craft.position.x + x + craft.position.y + y - camera.x - camera.y) * 18 - z * 44});
            minX = (std::min)(minX, screen.x);
            maxX = (std::max)(maxX, screen.x);
            minY = (std::min)(minY, screen.y);
            maxY = (std::max)(maxY, screen.y);
        }
    }
    return maxX < -padding || minX > kCanvasWidth + padding || maxY < -padding ||
           minY > kCanvasHeight + padding;
}
void AirTraffic::Update(const GameState &world, float dt)
{
    dt = Game::Clamp(dt, 0, .05f);
    for (auto &craft : aircraft)
    {
        auto oldDirection = Unit(Mul(Tangent(craft.route, craft.parameter), float(craft.direction)));
        auto tangent = Tangent(craft.route, craft.parameter);
        craft.parameter +=
            craft.direction * craft.speed * dt / (std::max)(1.f, std::sqrt(Dot(tangent, tangent)));
        craft.position = Position(craft.route, craft.parameter);
        auto direction = Unit(Mul(Tangent(craft.route, craft.parameter), float(craft.direction)));
        craft.yaw = std::atan2(direction.y, direction.x);
        float targetBank = dt > 0 ? Game::Clamp(-Cross(oldDirection, direction) / dt * .25f, -.18f, .18f) : 0;
        craft.bank += (targetBank - craft.bank) * (1 - std::exp(-4 * dt));
    }
    aircraft.erase(std::remove_if(aircraft.begin(), aircraft.end(),
                                  [&](const AircraftActor &craft) {
                                      return Game::Distance(craft.position, world.playerPosition) > 180 &&
                                             Game::Distance(craft.position, world.cameraPosition) > 180 &&
                                             OutsideVisibleFootprint(craft, world.cameraPosition, 340) &&
                                             OutsideVisibleFootprint(craft, world.playerPosition, 340);
                                  }),
                   aircraft.end());
    spawnTimer -= dt;
    if (spawnTimer > 0 || dt <= 0)
    {
        return;
    }
    spawnTimer = 1;
    auto random = [&]() {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return randomState;
    };
    for (int attempt = 0; attempt < 96 && aircraft.size() < kAircraftLimit; ++attempt)
    {
        int route = int(random() % RoadNetwork::Get().airCorridors.size());
        float t = float(random() % 1400) / 1000.f - .2f;
        auto position = Position(route, t);
        float altitude = RoadNetwork::Get().airCorridors[route].altitude;
        bool crowded = std::any_of(aircraft.begin(), aircraft.end(), [&](const AircraftActor &craft) {
            return std::fabs(craft.altitude - altitude) < 2.5f &&
                   Game::Distance(craft.position, position) < 14;
        });
        if (crowded)
        {
            continue;
        }
        AircraftActor craft;
        craft.id = nextId;
        craft.route = route;
        craft.parameter = t;
        // Each corridor is one way; its altitude separates it from the other routes.
        craft.direction = route % 2 ? -1 : 1;
        craft.type = static_cast<models::AircraftType>((craft.id / 2) % size_t(models::AircraftType::Count));
        craft.alternate = craft.id % 2 != 0;
        craft.speed = craft.type == models::AircraftType::Skycrane ? 14.f : 20.f;
        craft.position = position;
        craft.altitude = altitude;
        auto heading = Mul(Tangent(route, t), float(craft.direction));
        craft.yaw = std::atan2(heading.y, heading.x);
        if (Game::Distance(position, world.playerPosition) > 115 ||
            !OutsideVisibleFootprint(craft, world.cameraPosition, 280) ||
            !OutsideVisibleFootprint(craft, world.playerPosition, 280))
        {
            continue;
        }
        ++nextId;
        aircraft.push_back(craft);
    }
}

// ============================================================================
// WorldScenery — placement, collision and rendering share the imported bounds
// ============================================================================
WorldScenery &Overworld::Scenery()
{
    static WorldScenery scenery;
    return scenery;
}
bool WorldScenery::IsBlocked(Vector2 p, float clearance) const
{
    return std::any_of(props.begin(), props.end(), [&](const SceneryProp &prop) {
        return prop.solid && p.x > prop.minimum.x - clearance && p.x < prop.maximum.x + clearance &&
               p.y > prop.minimum.y - clearance && p.y < prop.maximum.y + clearance;
    });
}
bool WorldScenery::Place(const GameState &world, const models::Model &model, models::SceneryType type,
                         Vector2 position, float yaw, bool solid)
{
    const auto &mesh = models::ResourceAssets::Get(model.resourceId);
    SceneryProp prop;
    prop.model = &model;
    prop.type = type;
    prop.position = position;
    prop.yaw = yaw;
    prop.solid = solid;
    prop.minimum = {1.e9f, 1.e9f};
    prop.maximum = {-1.e9f, -1.e9f};
    for (float x : {mesh.minimum.x, mesh.maximum.x})
    {
        for (float y : {mesh.minimum.y, mesh.maximum.y})
        {
            Vector2 p = {position.x + x * std::cos(yaw) - y * std::sin(yaw),
                         position.y + x * std::sin(yaw) + y * std::cos(yaw)};
            prop.minimum.x = (std::min)(prop.minimum.x, p.x);
            prop.minimum.y = (std::min)(prop.minimum.y, p.y);
            prop.maximum.x = (std::max)(prop.maximum.x, p.x);
            prop.maximum.y = (std::max)(prop.maximum.y, p.y);
        }
    }
    if (prop.minimum.x < kWorldMinX + 1 || prop.maximum.x > kWorldMaxX - 1 ||
        prop.minimum.y < kWorldMinY + 1 || prop.maximum.y > kWorldMaxY - 1)
    {
        return false;
    }
    Building footprint{prop.minimum.x, prop.minimum.y, prop.maximum.x - prop.minimum.x,
                       prop.maximum.y - prop.minimum.y, 0};
    if (CityLayout::OverlapsQuestSpace(footprint, .7f))
    {
        return false;
    }
    auto overlap = [&](Vector2 minimum, Vector2 maximum, float padding) {
        return prop.minimum.x < maximum.x + padding && prop.maximum.x > minimum.x - padding &&
               prop.minimum.y < maximum.y + padding && prop.maximum.y > minimum.y - padding;
    };
    for (const auto &b : world.buildings)
    {
        if (overlap({b.x, b.y}, {b.x + b.width, b.y + b.depth}, .55f))
        {
            return false;
        }
    }
    for (const auto &other : props)
    {
        if (other.solid && overlap(other.minimum, other.maximum, .6f))
        {
            return false;
        }
    }
    if (solid && TransitLayout::OverlapsPavement(prop.minimum, prop.maximum, .35f))
    {
        return false;
    }
    for (const auto &person : world.residents)
    {
        if (overlap(person.position, person.position, 1.1f))
        {
            return false;
        }
    }
    for (float x = prop.minimum.x - .4f; x <= prop.maximum.x + .4f; x += .4f)
    {
        for (float y = prop.minimum.y - .4f; y <= prop.maximum.y + .4f; y += .4f)
        {
            if (RoadNetwork::Get().Contains({x, y}, .6f))
            {
                return false;
            }
        }
    }
    if (type == models::SceneryType::LandingPad || type == models::SceneryType::Sidewalk)
    {
        prop.elevation = .03f - mesh.maximum.z;
    }
    props.push_back(prop);
    return true;
}
void WorldScenery::Generate(const GameState &world)
{
    using S = models::SceneryType;
    props.clear();
    auto place = [&](S type, Vector2 p, float yaw = 0) {
        return Place(world, models::ModelLibrary::Scenery(type), type, p, yaw,
                     type != S::LandingPad && type != S::Sidewalk);
    };
    constexpr S streets[] = {S::Bench,      S::Planter,    S::Bollard,     S::TransitShelter,
                             S::Wayfinding, S::ChargePost, S::Streetlight, S::CrashBarrier};
    constexpr S industry[] = {S::Transformer, S::CargoContainer, S::HVAC,     S::PipeRack,
                              S::CommRelay,   S::SolarArray,     S::WaterTank};
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        // Deterministic candidates try open setbacks first, then the outer service apron.
        for (int index = 0; index < 26; ++index)
        {
            S type = index < 18 ? streets[index % std::size(streets)]
                                : industry[(index - 18) % std::size(industry)];
            for (int attempt = 0; attempt < 48; ++attempt)
            {
                float x = -27.f + float((index * 7 + attempt * 11) % 66);
                float y = -10.f + float((index * 11 + attempt * 7) % 34);
                if (attempt >= 24)
                {
                    x = 42.f + float(attempt % 6) * 2;
                    y = -25.f + float((index * 7 + attempt) % 48);
                }
                if (place(type, Add(center, {x, y}), index % 2 ? kPi * .5f : 0))
                {
                    break;
                }
            }
        }
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            Vector2 pad = Add(center, {54.f + float(attempt / 3) * 13, 11.f + float(attempt % 3) * 12});
            if (!place(S::LandingPad, pad))
            {
                continue;
            }
            auto type = static_cast<models::AircraftType>(city % int(models::AircraftType::Count));
            Place(world, models::ModelLibrary::Aircraft(type, city % 2 != 0), S::Count, pad, -.35f, true);
            place(S::AirfieldBeacon, Add(pad, {4.6f, 4.6f}));
            place(S::AirfieldBeacon, Add(pad, {-4.6f, 4.6f}));
            place(S::TransitShelter, Add(pad, {6.5f, 0}));
            break;
        }
        for (int i = 0; i < 8; ++i)
        {
            for (int attempt = 0; attempt < 12; ++attempt)
            {
                if (place(i % 2 ? S::PrismBloom : S::SailTree,
                          Add(center,
                              {-35.f + float((i * 13 + attempt * 7) % 78), 35.f + float(attempt % 4) * 3})))
                {
                    break;
                }
            }
        }
    }
    for (auto type : {S::FieldShelter, S::SurveyTripod, S::MiningConveyor, S::CargoContainer, S::Transformer})
    {
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            if (place(type, {137.f + float(attempt % 4) * 3, 12.f + float(attempt / 4) * 4}))
            {
                break;
            }
        }
    }
}

const std::array<CityInfo, Overworld::kCityCount> &Overworld::Cities()
{
    static const std::array<CityInfo, kCityCount> cities = [] {
        std::array<CityInfo, kCityCount> result = {
            {{"halo_07", "헤일로", {0, 0}, "익숙한 일상 속 불안", "관문에서 발신자 없는 응답이 들린다."},
             {"cinder", "신더", {0, 0}, "분주한 공업 도시", "광산 교대조의 출근 기록만 남아 있다."},
             {"veil", "베일", {0, 0}, "고요한 관측 도시", "어젯밤 별자리 하나가 사라졌다."},
             {"lumen", "루멘", {0, 0}, "빛으로 버티는 피난 도시", "꺼진 등 아래에서는 이름을 부르지 마라."},
             {"echo", "에코", {0, 0}, "끊어진 통신을 기다리는 도시", "수신기에서 내일의 목소리가 들린다."}}};
        unsigned int seed = 0x458a91u;
        for (int i = 1; i < kCityCount; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            float angle = .25f + (i - 1) * 1.57f + float(seed % 350) / 1000.f;
            float radius = 260.f + float((seed >> 12) % 85);
            result[i].center = {std::round(std::cos(angle) * radius), std::round(std::sin(angle) * radius)};
        }
        return result;
    }();
    return cities;
}

int Overworld::CityAt(Vector2 position)
{
    for (int i = 0; i < kCityCount; ++i)
    {
        if (Game::Distance(position, Cities()[i].center) <= 45)
        {
            return i;
        }
    }
    return -1;
}

bool Overworld::IsRoad(int x, int y)
{
    return RoadNetwork::Get().Contains({float(x) + .5f, float(y) + .5f});
}

models::Palette Overworld::BikePalette()
{
    models::Palette palette;
    palette.body = {.21f, .32f, .39f, 1};
    palette.accent = {.91f, .63f, .27f, 1};
    palette.emission = {.28f, .86f, 1, 1};
    return palette;
}

void Overworld::Populate(GameState &state)
{
    Traffic().Reset();
    Flights().Reset();
    // Preserve each GLB's footprint/aspect instead of stretching imported buildings into old pillars.
    const auto &library = models::ModelLibrary::Get();
    for (auto &building : state.buildings)
    {
        const auto &resource =
            models::ResourceAssets::Get(library.buildings[size_t(building.type)].resourceId);
        building.width = resource.physicalSize.x;
        building.depth = resource.physicalSize.y;
        // Building vertices use nominal height; antennae may extend above normalized z=1.
        building.height = resource.physicalSize.z / resource.maximum.z * 44.f;
    }
    // Keep the authored Halo landmarks; duplicate only its architectural vocabulary.
    const auto haloBuildings = state.buildings;
    for (int cityIndex = 1; cityIndex < kCityCount; ++cityIndex)
    {
        const auto center = Cities()[cityIndex].center;
        for (size_t i = 0; i < haloBuildings.size(); ++i)
        {
            auto building = haloBuildings[i];
            if (building.type == models::BuildingType::Observatory)
            {
                continue;
            }
            // District-specific setbacks break the repeated square-block silhouette.
            float angle = float((i * 47 + cityIndex * 71) % 360) * kPi / 180;
            building.x += center.x + std::cos(angle) * (cityIndex == 1 ? 1.1f : 2.8f);
            building.y += center.y + std::sin(angle) * 2.4f;
            std::rotate(building.palette.body.begin(), building.palette.body.begin() + cityIndex % 3,
                        building.palette.body.begin() + 3);
            state.buildings.push_back(building);
        }
    }
    // Reserve the quest landmark before nearby blocks: its new footprint must not be discarded.
    std::stable_partition(state.buildings.begin(), state.buildings.end(), [](const Building &building) {
        return building.type == models::BuildingType::Observatory;
    });
    CityLayout::ValidateBuildings(state.buildings);
    // Lia + Mara + eighteen residents = twenty Halo citizens. Kain replaces a reserved citizen.
    constexpr Vector2 positions[] = {{-5, 3}, {-1, 3},   {5, 3},    {8, -3},  {-11, 3}, {10, 6},  {-8, 8},
                                     {2, 8},  {-10, -7}, {11, -3},  {11, -5}, {-12, 7}, {-6, -7}, {-2, -7},
                                     {4, 8},  {8, 8},    {-13, -3}, {12, 8},  {-3, 3},  {0, 3}};
    constexpr const char *roles[] = {"배송 기사", "교대 근무자", "정비공", "경비원",    "학생",
                                     "상인",      "주민",        "관측원", "통신 기사", "탐사원"};
    constexpr models::CharacterType appearances[] = {
        models::CharacterType::Pilot,    models::CharacterType::Civilian,   models::CharacterType::Engineer,
        models::CharacterType::Guard,    models::CharacterType::Civilian,   models::CharacterType::Civilian,
        models::CharacterType::Civilian, models::CharacterType::Researcher, models::CharacterType::Engineer,
        models::CharacterType::Explorer};
    state.residents.clear();
    for (int cityIndex = 0; cityIndex < kCityCount; ++cityIndex)
    {
        const auto &city = Cities()[cityIndex];
        int count = cityIndex == 0 ? kPopulationPerCity - 2 : kPopulationPerCity;
        for (int i = 0; i < count; ++i)
        {
            Vector2 position = {city.center.x + positions[i].x, city.center.y + positions[i].y};
            // Place every resident on a genuinely reachable outdoor footprint.
            auto blocked = [&state](Vector2 p) {
                bool building =
                    std::any_of(state.buildings.begin(), state.buildings.end(), [p](const Building &b) {
                        return p.x > b.x - .3f && p.x < b.x + b.width + .3f && p.y > b.y - .3f &&
                               p.y < b.y + b.depth + .3f;
                    });
                return building || RoadNetwork::Get().Contains(p, .7f) || CityLayout::IsQuestSpace(p, .5f) ||
                       std::any_of(state.residents.begin(), state.residents.end(),
                                   [p](const Resident &r) { return Game::Distance(p, r.position) < .8f; });
            };
            for (int attempt = 0; attempt < 200 && blocked(position); ++attempt)
            {
                position = {city.center.x - 24 + float((attempt + i * 7) % 49),
                            city.center.y + 3.5f + float(attempt / 49) * 1.2f};
            }
            Resident resident;
            resident.position = position;
            resident.name = std::string(city.name) + " " + roles[i % 10] + " " + std::to_string(i + 1);
            resident.dialogue = resident.name + " / " + city.rumor;
            resident.type = appearances[i % 10];
            resident.coat = {.3f + .08f * (i % 4), .36f + .06f * (cityIndex % 4), .46f + .04f * (i % 5), 1};
            resident.gender = i % 2 ? models::Gender::Female : models::Gender::Male;
            resident.hair = models::HairStyle(i % int(models::HairStyle::Count));
            resident.cityIndex = cityIndex;
            resident.profession = static_cast<ResidentProfession>(i % 10);
            state.residents.push_back(resident);
        }
    }
    Scenery().Generate(state);
    CityResidents::Get().Reset(state);
}

void Overworld::UpdateBike(GameState &state, float deltaSeconds)
{
    float steering = float(state.pressedKeys['d']) - float(state.pressedKeys['a']);
    state.bikeYaw = std::remainder(state.bikeYaw + steering * 1.8f * deltaSeconds, 2 * kPi);
    bool accelerating = state.pressedKeys['w'] && !state.pressedKeys['s'];
    float targetSpeed = accelerating ? kBikeCruiseSpeed : 0;
    float acceleration = accelerating ? 32.f : 48.f;
    state.bikeSpeed +=
        Game::Clamp(targetSpeed - state.bikeSpeed, -acceleration * deltaSeconds, acceleration * deltaSeconds);
    // Substeps prevent tunnelling through a narrow building at maximum speed.
    int steps = (std::max)(1, int(std::ceil(state.bikeSpeed * deltaSeconds / .15f)));
    for (int step = 0; step < steps; ++step)
    {
        Vector2 next = {
            state.playerPosition.x + std::cos(state.bikeYaw) * state.bikeSpeed * deltaSeconds / steps,
            state.playerPosition.y + std::sin(state.bikeYaw) * state.bikeSpeed * deltaSeconds / steps};
        bool blocked = false;
        for (float dx : {-.45f, .45f})
        {
            for (float dy : {-.45f, .45f})
            {
                blocked = blocked || Game::Get().IsPositionBlocked({next.x + dx, next.y + dy});
            }
        }
        if (blocked)
        {
            state.bikeSpeed = 0;
            break;
        }
        state.playerPosition = next;
    }
    state.bikePosition = state.playerPosition;
    state.facingYaw = state.bikeYaw;
    state.movementAmount = state.bikeSpeed / kBikeCruiseSpeed;
    state.movementSpeed = state.bikeSpeed;
}

void Overworld::ToggleBike(GameState &state)
{
    if (state.isBikeMounted)
    {
        if (state.bikeSpeed > 1)
        {
            Game::Get().ShowNotice("바이크 / 방향키를 놓고 정차한 뒤 E로 내리세요.");
            return;
        }
        for (Vector2 offset : {Vector2{1.2f, 0}, {-1.2f, 0}, {0, 1.2f}, {0, -1.2f}})
        {
            Vector2 position = {state.bikePosition.x + offset.x, state.bikePosition.y + offset.y};
            if (!Game::Get().IsPositionBlocked(position))
            {
                state.playerPosition = position;
                state.isBikeMounted = false;
                state.bikeSpeed = 0;
                Game::Get().ShowNotice("바이크 / 하차했습니다. 주차 위치는 유지됩니다.");
                return;
            }
        }
        Game::Get().ShowNotice("바이크 / 내릴 공간이 부족합니다. 넓은 곳으로 이동하세요.");
        return;
    }
    state.isBikeMounted = true;
    state.playerPosition = state.bikePosition;
    state.bikeSpeed = 0;
    Game::Get().ShowNotice(
        "바이크 / W 가속 · S 제동 · A/D 조향 · 정차 후 E 하차. 교차로의 홀로그램 신호를 확인하세요.");
}
} // namespace tutorial
