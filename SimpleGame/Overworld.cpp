#include "stdafx.h"
#include "Overworld.h"
#include "Civic.h"
#include "Level.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include "Profiler.h"
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
           : Cross(in, out) <= 0 ? TrafficTurn::Left
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
    float handle = chord * (std::fabs(dot) < .001f ? .3905243f
                            : dot > .99f           ? 1.f / 3
                            : dot < -.8f           ? 2.f / 3
                                                   : .45f);
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
        float speed, heightA, heightB;
    };
    std::vector<Source> sources;
    auto add = [&](Vector2 a, Vector2 b, int lanes, float speed, float heightA = 0, float heightB = 0) {
        sources.push_back({a, b, lanes, speed, heightA, heightB});
    };
    // Streets are the primary plan: one collector with spaced T junctions, then local access loops.
    // No automatic nearby-node welding: a short link must be redesigned, never folded into a mega-junction.
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        auto p = [center](float x, float y) { return Add(center, {x, y}); };
        int collector = city <= 1 || city == 4 ? 4 : 2;
        add(p(-90, -17), p(60, -17), collector, 6);
        add(p(-32, -17), p(-32, 32), 2, 3.5f);
        add(p(30, -17), p(30, 32), 2, 3.5f);
        add(p(-32, 32), p(30, 32), 2, 3.5f);
        add(p(-32, 10), p(30, 10), 2, 3);
        add(p(-12, 10), p(-12, 32), 2, 3);
    }
    // Four independent regional portals preserve the scattered city positions without funneling all
    // traffic through one intersection. Narrow two-lane links remain subordinate to local collectors.
    auto cinder = Overworld::Cities()[1].center;
    add({60, -17}, {128, -17}, 2, 8);
    add({128, -17}, {180, -17}, 2, 8);
    add({180, -17}, Add(cinder, {-90, -17}), 2, 8);
    add({128, -17}, Overworld::kMineEntrance, 2, 4);
    auto south = Overworld::Cities()[2].center;
    add({30, 32}, {30, 100}, 2, 5);
    add({30, 100}, {48, 103.5f}, 2, 5);
    add({30, 100}, {-40, 150}, 2, 8);
    add({-40, 150}, Add(south, {-90, -17}), 2, 8);
    auto west = Overworld::Cities()[3].center;
    add({-90, -17}, {-155, -70}, 2, 8);
    add({-155, -70}, Add(west, {60, -17}), 2, 8);
    auto north = Overworld::Cities()[4].center;
    add({-60, -17}, {-60, -75}, 2, 5);
    add({-60, -75}, {48, -103.5f}, 2, 7);
    add({48, -103.5f}, Add(north, {-90, -17}), 2, 8);
    // Eight-metre clearance over the eastern collector. The 110 m ramps keep the usable driving grade below
    // 9%. The crossing at (48,-17) has no graph node: traffic above and below is independent.
    add({48, -103.5f}, {48, -45}, 2, 6, 0, 4.2f);
    add({48, -45}, {48, 45}, 2, 7, 4.2f, 4.2f);
    add({48, 45}, {48, 103.5f}, 2, 6, 4.2f, 0);
    auto halo = Overworld::Cities()[0].center;
    for (int city = 1; city < Overworld::kCityCount; ++city)
    {
        auto c = Overworld::Cities()[city].center;
        auto airDirection = Unit(Sub(c, halo));
        Vector2 airNormal = {-airDirection.y, airDirection.x};
        auto hub = Add(halo, {0, 8});
        airCorridors.push_back({Sub(hub, Mul(airDirection, 28)),
                                Add(Add(hub, Mul(airDirection, 30)), Mul(airNormal, 12)), Add(c, {12, 8}),
                                8.f + city * 3.f});
    }
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
                float hi = sources[i].heightA + (sources[i].heightB - sources[i].heightA) * t;
                float hj = sources[j].heightA + (sources[j].heightB - sources[j].heightA) * u;
                if (std::fabs(hi - hj) < .05f)
                {
                    cuts[i].push_back(Game::Clamp(t, 0, 1));
                    cuts[j].push_back(Game::Clamp(u, 0, 1));
                }
            }
        }
    }
    auto node = [&](Vector2 p, float height) {
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            if (Game::Distance(p, junctions[i].position) < .12f &&
                std::fabs(height - junctions[i].altitude) < .05f)
            {
                return int(i);
            }
        }
        junctions.push_back({p, 3, {}, height});
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
            auto height = [&](float t) {
                return sources[i].heightA + (sources[i].heightB - sources[i].heightA) * t;
            };
            int from = node(a, height(split[j - 1])), to = node(b, height(split[j]));
            if (from == to)
            {
                continue;
            }
            roads.push_back({from, to, sources[i].lanes, sources[i].speed});
        }
    }
    std::vector<int> remap(junctions.size(), -1);
    std::vector<RoadJunction> compact;
    for (const auto &r : roads)
    {
        for (int node : {r.from, r.to})
        {
            if (remap[node] < 0)
            {
                remap[node] = int(compact.size());
                compact.push_back(junctions[node]);
            }
        }
    }
    for (auto &r : roads)
    {
        r.from = remap[r.from];
        r.to = remap[r.to];
    }
    junctions.swap(compact);
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
    // Broad phase shared by collision, zoning and pedestrian graph construction.
    auto insert = [&](Vector2 a, Vector2 b, int index, bool junction) {
        for (int x = int(std::floor((std::min)(a.x, b.x) / 16));
             x <= int(std::floor((std::max)(a.x, b.x) / 16)); ++x)
        {
            for (int y = int(std::floor((std::min)(a.y, b.y) / 16));
                 y <= int(std::floor((std::max)(a.y, b.y) / 16)); ++y)
            {
                (junction ? cells[{x, y}].junctions : cells[{x, y}].roads).push_back(index);
            }
        }
    };
    for (size_t i = 0; i < roads.size(); ++i)
    {
        auto a = junctions[roads[i].from].position, b = junctions[roads[i].to].position;
        insert({(std::min)(a.x, b.x) - 8, (std::min)(a.y, b.y) - 8},
               {(std::max)(a.x, b.x) + 8, (std::max)(a.y, b.y) + 8}, int(i), false);
    }
    for (size_t i = 0; i < junctions.size(); ++i)
    {
        auto p = junctions[i].position;
        float r = junctions[i].radius + 12;
        insert({p.x - r, p.y - r}, {p.x + r, p.y + r}, int(i), true);
    }
}
bool RoadNetwork::Contains(Vector2 position, float margin) const
{
    auto roadContains = [&](int index) {
        const auto &road = roads[index];
        auto a = junctions[road.from].position, b = junctions[road.to].position;
        auto direction = Unit(Sub(b, a));
        auto start = Add(a, Mul(direction, junctions[road.from].radius));
        float span = Game::Distance(a, b) - junctions[road.from].radius - junctions[road.to].radius;
        auto offset = Sub(position, start);
        float along = Dot(offset, direction);
        if (RoadElevation(index, along / span) > 1.4f)
        {
            return false; // Keep low ramp mouths reserved; the clear span can be walked below.
        }
        return along >= -margin && along <= span + margin &&
               std::fabs(Cross(offset, direction)) <= road.lanes * kLaneWidth * .5f + margin;
    };
    if (margin <= 2)
    {
        auto found = cells.find({int(std::floor(position.x / 16)), int(std::floor(position.y / 16))});
        if (found == cells.end())
        {
            return false;
        }
        for (int road : found->second.roads)
        {
            if (roadContains(road))
            {
                return true;
            }
        }
        for (int junction : found->second.junctions)
        {
            if (junctions[junction].altitude < .1f &&
                TransitLayout::ContainsJunction(junction, position, margin))
            {
                return true;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < roads.size(); ++i)
        {
            if (roadContains(int(i)))
            {
                return true;
            }
        }
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            if (junctions[i].altitude < .1f && TransitLayout::ContainsJunction(int(i), position, margin))
            {
                return true;
            }
        }
    }
    return false;
}
Vector2 RoadNetwork::LanePosition(int lane, float distance) const
{
    return Add(lanes[lane].start, Mul(lanes[lane].direction, Game::Clamp(distance, 0, lanes[lane].length)));
}
bool RoadNetwork::ContainsFootprint(Vector2 position, float margin) const
{
    for (const auto &road : roads)
    {
        auto a = junctions[road.from].position, b = junctions[road.to].position;
        if (SegmentDistance(position, a, b) <= road.lanes * kLaneWidth * .5f + margin)
        {
            return true;
        }
    }
    for (size_t j = 0; j < junctions.size(); ++j)
    {
        if (TransitLayout::ContainsJunction(int(j), position, margin))
        {
            return true;
        }
    }
    return false;
}
float RoadNetwork::RoadElevation(int index, float fraction) const
{
    const auto &road = roads[index];
    float t = Game::Clamp(fraction, 0, 1);
    const float a = junctions[road.from].altitude, b = junctions[road.to].altitude;
    if (std::fabs(a - b) > .1f)
    {
        // The delivered 110m ramp has 16m parabolic end curves and 78m constant grade.
        // Its actual surface is faceted at 0.5m authoring intervals: interpolate those
        // same heights so wheels cannot sink into the authored vertical curves.
        auto profile = [](float x) {
            constexpr float grade = 8.4f / 94.f;
            return x < 16   ? grade * x * x / 32
                   : x > 94 ? 8.4f - grade * (110 - x) * (110 - x) / 32
                            : grade * (x - 8);
        };
        float x = t * 110, low = std::floor(x * 2) * .5f;
        t = (profile(low) + (profile((std::min)(110.f, low + .5f)) - profile(low)) * (x - low) * 2) / 8.4f;
    }
    return a + (b - a) * t;
}
bool RoadNetwork::ContainsSurface(Vector2 position, float height, float margin) const
{
    for (size_t index = 0; index < roads.size(); ++index)
    {
        const auto &r = roads[index];
        auto a = junctions[r.from].position, b = junctions[r.to].position, d = Unit(Sub(b, a));
        a = Add(a, Mul(d, junctions[r.from].radius));
        b = Sub(b, Mul(d, junctions[r.to].radius));
        float span = Game::Distance(a, b), along = Dot(Sub(position, a), d);
        if (along >= -margin && along <= span + margin &&
            std::fabs(Cross(Sub(position, a), d)) <= r.lanes * kLaneWidth * .5f + margin &&
            std::fabs(RoadElevation(int(index), along / span) - height) < .15f)
        {
            return true;
        }
    }
    for (size_t j = 0; j < junctions.size(); ++j)
    {
        if (std::fabs(junctions[j].altitude - height) < .15f &&
            TransitLayout::ContainsJunction(int(j), position, margin))
        {
            return true;
        }
    }
    return false;
}
float RoadNetwork::DriveElevation(Vector2 position, float previousHeight) const
{
    float result = 0, best = 10000;
    for (size_t index = 0; index < roads.size(); ++index)
    {
        const auto &road = roads[index];
        auto a = junctions[road.from].position, b = junctions[road.to].position, d = Unit(Sub(b, a));
        a = Add(a, Mul(d, junctions[road.from].radius));
        b = Sub(b, Mul(d, junctions[road.to].radius));
        float t = Game::Clamp(Dot(Sub(position, a), Sub(b, a)) / Dot(Sub(b, a), Sub(b, a)), 0, 1);
        float distance = Game::Distance(position, Add(a, Mul(Sub(b, a), t)));
        float height = RoadElevation(int(index), t);
        if (distance > road.lanes * kLaneWidth * .5f + .2f || std::fabs(height - previousHeight) > .6f)
        {
            continue;
        }
        float score = distance + std::fabs(height - previousHeight) * 8;
        if (score < best)
        {
            best = score;
            result = height;
        }
    }
    for (size_t j = 0; j < junctions.size(); ++j)
    {
        if (std::fabs(junctions[j].altitude - previousHeight) < .6f &&
            TransitLayout::ContainsJunction(int(j), position, 0) && best > 1)
        {
            return junctions[j].altitude;
        }
    }
    return result;
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
    // The screen's downward Y axis makes this the displayed driver's right.
    return {-direction.y, direction.x};
}
bool RoadNetwork::IsSignalized(int junction) const
{
    return junction >= 0 && junction < int(junctions.size()) && Signalized(junctions[junction]);
}
int RoadNetwork::SignalAxis(int index, int atJunction) const
{
    const auto &lane = lanes[index];
    const auto &junction = junctions[atJunction < 0 ? lane.to : atJunction];
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
float TrafficSystem::RenderElevation(size_t index) const
{
    const auto &car = vehicles[index];
    const auto &net = RoadNetwork::Get();
    const auto &lane = net.lanes[car.lane];
    if (car.turnDistance >= 0)
    {
        return net.junctions[lane.to].altitude;
    }
    float fraction = car.distance / (std::max)(.001f, lane.length);
    if (lane.from != net.roads[lane.road].from)
    {
        fraction = 1 - fraction;
    }
    return net.RoadElevation(lane.road, fraction);
}
bool TrafficSystem::DamageVehicle(std::uint64_t id, int damage, bool playerOwned)
{
    auto car =
        std::find_if(vehicles.begin(), vehicles.end(), [=](const TrafficVehicle &v) { return v.id == id; });
    if (car == vehicles.end() || car->health <= 0 || damage <= 0)
    {
        return false;
    }
    car->health = (std::max)(0, car->health - damage);
    car->hitFlash = .15f;
    if (playerOwned && car->occupied && car->health == 0)
    {
        CivicJustice::Report(Offense::VehicleDestruction, car->position);
    }
    if (car->health <= 0)
    {
        car->speed = 0;
        car->reservedFrom = car->reservedTo = -1;
    }
    return true;
}
void TrafficSystem::StopForImpact(std::uint64_t id)
{
    auto car =
        std::find_if(vehicles.begin(), vehicles.end(), [=](const TrafficVehicle &v) { return v.id == id; });
    if (car == vehicles.end())
    {
        return;
    }
    car->speed = 0;
    car->impactCooldown = (std::max)(car->impactCooldown, .6f);
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
bool TrafficSystem::HasRightTurnSignal(int lane) const
{
    const auto &net = RoadNetwork::Get();
    return net.IsSignalized(net.lanes[lane].to) && net.roads[net.lanes[lane].road].lanes >= 6;
}
bool TrafficSystem::PedestrianGreen(int junction, int road) const
{
    if (junction < 0 || junction >= int(signals.size()) || signals[junction].phase != SignalPhase::Walk)
    {
        return false;
    }
    const auto &net = RoadNetwork::Get();
    if (!net.IsSignalized(junction))
    {
        return false;
    }
    std::vector<int> arms;
    int width = 0;
    for (int lane : net.junctions[junction].outgoing)
    {
        if (net.lanes[lane].slot != 0)
        {
            continue;
        }
        arms.push_back(lane);
        width = (std::max)(width, net.roads[net.lanes[lane].road].lanes);
    }
    if (road < 0 || width >= 6)
    {
        return true;
    }
    // Local streets serve one crossing per cycle; collectors serve an opposing pair.
    const auto cycle = signals[junction].walkCycle;
    if (width <= 2)
    {
        return net.lanes[arms[cycle % arms.size()]].road == road;
    }
    bool hasAxis[2] = {false, false};
    for (int lane : arms)
    {
        hasAxis[net.SignalAxis(lane, junction)] = true;
    }
    int selected = hasAxis[0] && hasAxis[1] ? int(cycle % 2) : hasAxis[0] ? 0 : 1;
    for (int lane : arms)
    {
        if (net.lanes[lane].road == road)
        {
            return net.SignalAxis(lane, junction) == selected;
        }
    }
    return false;
}
unsigned int TrafficSystem::PedestrianGroups(int junction) const
{
    const auto &net = RoadNetwork::Get();
    unsigned int arms = 0;
    int width = 0;
    bool axes[2] = {false, false};
    for (int lane : net.junctions[junction].outgoing)
    {
        if (net.lanes[lane].slot != 0)
        {
            continue;
        }
        ++arms;
        width = (std::max)(width, net.roads[net.lanes[lane].road].lanes);
        axes[net.SignalAxis(lane, junction)] = true;
    }
    return width >= 6 ? 1 : width <= 2 ? arms : unsigned(axes[0]) + unsigned(axes[1]);
}
int TrafficSystem::CrossingRoad(int junction, Vector2 position) const
{
    const auto &net = RoadNetwork::Get();
    if (junction < 0 || junction >= int(net.junctions.size()))
    {
        return -1;
    }
    int result = -1;
    float best = 1.e9f;
    const auto &j = net.junctions[junction];
    for (int index : j.outgoing)
    {
        const auto &lane = net.lanes[index];
        if (lane.slot != 0)
        {
            continue;
        }
        auto center = Add(j.position, Mul(lane.direction, j.radius + 1.f));
        auto normal = RoadNetwork::RightOf(lane.direction);
        float half = net.roads[lane.road].lanes * RoadNetwork::kLaneWidth * .5f + 1.f;
        float distance =
            SegmentDistance(position, Add(center, Mul(normal, half)), Sub(center, Mul(normal, half)));
        if (distance < best)
        {
            best = distance;
            result = lane.road;
        }
    }
    return result;
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
            if (PedestrianGreen(junction, net.lanes[lane].road))
            {
                width = (std::max)(width, net.roads[net.lanes[lane].road].lanes * RoadNetwork::kLaneWidth);
            }
        }
        return (std::max)(8.f, (width + 1.9f) / .5f + 3.f);
    }
    if (phase == SignalPhase::WalkClear)
    {
        return 2;
    }
    return int(phase) % 3 == 0 ? (int(phase) % 6 == 0 ? 10.f : 5.f) : 3.f;
}
bool TrafficSystem::CanStartCrossing(int junction, float travelSeconds, int road) const
{
    return PedestrianGreen(junction, road) &&
           PhaseDuration(junction) - signals[junction].elapsed >= travelSeconds + 1.f;
}
bool TrafficSystem::CrossingOccupied(const GameState &world, int junction) const
{
    if (!world.isBikeMounted)
    {
        const auto &net = RoadNetwork::Get();
        const auto &j = net.junctions[junction];
        for (int index : j.outgoing)
        {
            const auto &lane = net.lanes[index];
            if (lane.slot != 0)
            {
                continue;
            }
            auto center = Add(j.position, Mul(lane.direction, j.radius + 1));
            auto offset = Sub(world.playerPosition, center);
            if (std::fabs(Dot(offset, lane.direction)) <= 1.05f &&
                std::fabs(Cross(offset, lane.direction)) <=
                    net.roads[lane.road].lanes * RoadNetwork::kLaneWidth * .5f + .25f)
            {
                return true;
            }
        }
    }
    return std::any_of(world.residents.begin(), world.residents.end(),
                       [&world, junction](const Resident &resident) {
                           if (&resident - world.residents.data() == Overworld::kKainResidentSlot &&
                               game::GameLevels::Chapter().GetState().kainJoined)
                           {
                               return false;
                           }
                           return resident.crossingJunction == junction;
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
    float localLength = 0;
    for (const auto &lane : net.lanes)
    {
        float projection = Dot(Sub(world.playerPosition, lane.start), lane.direction);
        auto nearest =
            net.LanePosition(int(&lane - net.lanes.data()), Game::Clamp(projection, 0, lane.length));
        if (Game::Distance(nearest, world.playerPosition) < 100)
        {
            localLength += (std::min)(lane.length, 120.f);
        }
    }
    const auto desired = (std::min)(kVehicleLimit, size_t((std::max)(12.f, localLength / 22)));
    // Leave queue storage between adjacent junctions; short links are populated only by incoming cars.
    for (int attempt = 0; attempt < 100 && vehicles.size() < desired; ++attempt)
    {
        int laneIndex = int(Random() % net.lanes.size());
        const auto &lane = net.lanes[laneIndex];
        float projection = Dot(Sub(world.playerPosition, lane.start), lane.direction);
        float distance =
            Game::Clamp(projection + (int(Random() % 181) - 90), 2, (std::max)(2.f, lane.length - 5));
        if (lane.length < 18)
        {
            continue;
        }
        auto p = net.LanePosition(laneIndex, distance);
        float range = Game::Distance(p, world.playerPosition);
        float fraction = distance / lane.length;
        if (lane.from != net.roads[lane.road].from)
        {
            fraction = 1 - fraction;
        }
        float height = net.RoadElevation(lane.road, fraction);
        if (range < 28 || range > 100 || !OutsideView(p, world.cameraPosition, 240, height) ||
            !OutsideView(p, world.playerPosition, 240, height))
        {
            continue;
        }
        bool crowded = false;
        for (const auto &car : vehicles)
        {
            if (Game::Distance(p, car.position) < 7 || (car.reservedTo == laneIndex && distance < 7))
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
            if (car.reservedFrom >= 0 && net.lanes[car.reservedFrom].to == int(i))
            {
                occupied = true;
                break;
            }
        }
        const bool clearance = signal.phase == SignalPhase::WalkClear || int(signal.phase) % 3 == 2;
        auto next = static_cast<SignalPhase>((int(signal.phase) + 1) % 14);
        for (int skipped = 0; skipped < 4 && int(next) < 12 && int(next) % 3 == 0; ++skipped)
        {
            bool available = false;
            int axis = int(next) / 6;
            bool left = int(next) % 6 == 3;
            for (size_t lane = 0; lane < net.lanes.size(); ++lane)
            {
                if (net.lanes[lane].to == int(i) && net.SignalAxis(int(lane)) == axis &&
                    (left ? net.CanTurn(int(lane), TrafficTurn::Left)
                          : net.CanTurn(int(lane), TrafficTurn::Straight) ||
                                net.CanTurn(int(lane), TrafficTurn::Right)))
                {
                    available = true;
                    break;
                }
            }
            if (available)
            {
                break;
            }
            next = static_cast<SignalPhase>((int(next) + 3) % 14);
        }
        if (clearance && occupied)
        {
            continue;
        }
        if (signal.phase == SignalPhase::WalkClear)
        {
            // Serve every crossing group within this cycle, not one group every several minutes.
            if (++signal.walkCycle < PedestrianGroups(int(i)))
            {
                next = SignalPhase::Walk;
            }
            else
            {
                signal.walkCycle = 0;
            }
        }
        signal.phase = next;
        signal.elapsed = 0;
    }
    // Reservations cover intersecting paths, rather than locking every lane of a junction.
    auto conflicts = [&](int from, int to, const TrafficVehicle &other) {
        if (other.reservedFrom < 0 || net.lanes[other.reservedFrom].to != net.lanes[from].to)
        {
            return false;
        }
        if (to == other.reservedTo)
        {
            return true;
        }
        TrafficVehicle candidate, existing;
        candidate.lane = from;
        candidate.nextLane = to;
        existing.lane = other.reservedFrom;
        existing.nextLane = other.reservedTo;
        for (int a = 0; a < 12; ++a)
        {
            auto p = TurnPoint(candidate, a / 12.f), q = TurnPoint(candidate, (a + 1) / 12.f);
            for (int b = 0; b < 12; ++b)
            {
                auto r = TurnPoint(existing, b / 12.f), t = TurnPoint(existing, (b + 1) / 12.f);
                auto pq = Sub(q, p), rt = Sub(t, r);
                float denom = Cross(pq, rt);
                if (std::fabs(denom) > .00001f)
                {
                    auto delta = Sub(r, p);
                    float u = Cross(delta, rt) / denom, v = Cross(delta, pq) / denom;
                    if (u >= 0 && u <= 1 && v >= 0 && v <= 1)
                    {
                        return true;
                    }
                }
                if ((std::min)({SegmentDistance(p, r, t), SegmentDistance(q, r, t), SegmentDistance(r, p, q),
                                SegmentDistance(t, p, q)}) < 1.35f)
                {
                    return true;
                }
            }
        }
        return false;
    };
    for (auto &car : vehicles)
    {
        if (car.reservedFrom >= 0 && car.lane != car.reservedFrom && car.turnDistance < 0 &&
            car.distance >= 2)
        {
            car.reservedFrom = car.reservedTo = -1;
        }
    }
    for (auto &car : vehicles)
    {
        car.impactCooldown = (std::max)(0.f, car.impactCooldown - dt);
        car.hitFlash = (std::max)(0.f, car.hitFlash - dt);
        if (car.health <= 0)
        {
            car.previousPosition = car.position;
            car.speed = 0;
            car.wreckSeconds += dt;
            car.reservedFrom = car.reservedTo = -1;
            continue;
        }
        car.previousPosition = car.position;
        car.previousYaw = car.yaw;
        const auto &lane = net.lanes[car.lane];
        float target = car.impactCooldown > 0 ? 0 : lane.speedLimit;
        float gap = 10000;
        bool permissiveRight = false;
        bool committed = car.reservedFrom == car.lane;
        if (committed && car.yieldingRight)
        {
            target = (std::min)(target, 1.5f);
        }
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
        }
        if (car.turnDistance < 0)
        {
            bool exitBlocked = false;
            for (const auto &other : vehicles)
            {
                if (other.id != car.id && other.lane == car.nextLane && other.distance < 6.5f)
                {
                    exitBlocked = true;
                    break;
                }
            }
            bool red = !committed && Light(car.lane, car.turn) != TrafficLight::Green;
            if (car.distance >= lane.length - 4.1f && car.speed < .08f)
            {
                car.stoppedSeconds += dt;
            }
            else if (car.distance < lane.length - 4.1f)
            {
                car.stoppedSeconds = 0;
            }
            // Ordinary Korean right turns: stop on red, then creep only after yielding.
            // Six-lane arterials have a dedicated right-turn signal instead.
            permissiveRight =
                red && car.turn == TrafficTurn::Right && !HasRightTurnSignal(car.lane) &&
                car.stoppedSeconds >= .8f && Light(car.lane, TrafficTurn::Right) == TrafficLight::Red &&
                int(signals[lane.to].phase) % 3 == 0 && signals[lane.to].phase != SignalPhase::WalkClear &&
                !PedestrianGreen(lane.to, lane.road) &&
                !PedestrianGreen(lane.to, net.lanes[car.nextLane].road) && !CrossingOccupied(world, lane.to);
            if (permissiveRight)
            {
                for (const auto &other : vehicles)
                {
                    const auto &approach = net.lanes[other.lane];
                    if (other.id != car.id && approach.to == lane.to && other.turnDistance < 0 &&
                        approach.length - other.distance < 18 &&
                        Light(other.lane, other.turn) == TrafficLight::Green)
                    {
                        permissiveRight = false;
                        break;
                    }
                }
            }
            red &= !permissiveRight;
            if (permissiveRight)
            {
                target = (std::min)(target, 1.5f);
            }
            bool reserved = false;
            if (!committed && car.distance >= lane.length - 8)
            {
                for (const auto &other : vehicles)
                {
                    if (other.id != car.id && conflicts(car.lane, car.nextLane, other))
                    {
                        reserved = true;
                        break;
                    }
                }
            }
            if (committed)
            {
                exitBlocked = false;
            }
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
            if (!committed &&
                (PedestrianGreen(lane.to) ||
                 (signals[lane.to].phase == SignalPhase::WalkClear && CrossingOccupied(world, lane.to)) ||
                 std::any_of(world.residents.begin(), world.residents.end(),
                             [&](const Resident &r) { return r.crossingJunction == lane.to; })))
            {
                gap = (std::min)(gap, lane.length - car.distance - 3.6f);
                target = (std::min)(target, (std::max)(0.f, gap * 2));
            }
        }
        else
        {
            target = (std::min)(lane.speedLimit, car.yieldingRight                   ? 1.5f
                                                 : car.turn == TrafficTurn::Straight ? 5.f
                                                                                     : 3.2f);
        }
        if (car.turnDistance < 0 && !committed && car.distance >= lane.length - 4.8f &&
            gap >= lane.length - car.distance - .05f && target > .1f)
        {
            car.reservedFrom = car.lane;
            car.reservedTo = car.nextLane;
            car.yieldingRight = permissiveRight;
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
                car.stoppedSeconds = 0;
                car.yieldingRight = false;
                ChooseExit(car);
            }
        }
        else
        {
            car.yaw = std::atan2(lane.direction.y, lane.direction.x);
        }
    }
    // Oriented hulls are a final physical constraint even when routing normally avoids contact.
    for (size_t a = 0; a < vehicles.size(); ++a)
    {
        for (size_t b = a + 1; b < vehicles.size(); ++b)
        {
            auto &first = vehicles[a], &second = vehicles[b];
            if (first.impactCooldown > 0 || second.impactCooldown > 0 ||
                Game::Distance(first.position, second.position) > 4 ||
                std::fabs(RenderElevation(a) - RenderElevation(b)) > .7f)
            {
                continue;
            }
            auto extents = [](const TrafficVehicle &car) {
                auto &mesh = models::ResourceAssets::Get(
                    models::ModelLibrary::Get().vehicles[size_t(car.id % 4)].body.resourceId);
                return Vector2{mesh.physicalSize.x * .5f, mesh.physicalSize.y * .5f};
            };
            auto ha = extents(first), hb = extents(second);
            Vector2 ax = {std::cos(first.yaw), std::sin(first.yaw)}, ay = {-ax.y, ax.x};
            Vector2 bx = {std::cos(second.yaw), std::sin(second.yaw)}, by = {-bx.y, bx.x};
            bool overlap = true;
            for (auto axis : {ax, ay, bx, by})
            {
                float ra = std::fabs(Dot(ax, axis)) * ha.x + std::fabs(Dot(ay, axis)) * ha.y;
                float rb = std::fabs(Dot(bx, axis)) * hb.x + std::fabs(Dot(by, axis)) * hb.y;
                overlap &= std::fabs(Dot(Sub(first.position, second.position), axis)) < ra + rb - .02f;
            }
            if (!overlap)
            {
                continue;
            }
            float speed = std::hypot(first.speed * ax.x - second.speed * bx.x,
                                     first.speed * ax.y - second.speed * bx.y);
            if (speed < .15f)
            {
                continue;
            }
            for (auto *car : {&first, &second})
            {
                car->impactCooldown = .8f;
                car->speed = 0;
                car->position = car->previousPosition;
                if (car->turnDistance >= 0)
                {
                    car->turnDistance = (std::max)(0.f, car->turnDistance - .25f);
                }
                else
                {
                    car->distance = (std::max)(0.f, car->distance - .25f);
                }
                if (speed > 3)
                {
                    DamageVehicle(car->id, int(speed * 6), false);
                }
            }
        }
    }
    BodyCollision::VehicleContacts(dt);

    vehicles.erase(
        std::remove_if(vehicles.begin(), vehicles.end(),
                       [&](const TrafficVehicle &car) {
                           if (car.health <= 0 && (car.wreckSeconds > 60 ||
                                                   (car.wreckSeconds > 12 &&
                                                    OutsideView(car.position, world.cameraPosition, 180))))
                           {
                               return true;
                           }
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
bool AirTraffic::Contains(const AircraftActor &craft, models::Point p)
{
    const auto &mesh =
        models::ResourceAssets::Get(models::ModelLibrary::Aircraft(craft.type, craft.alternate).resourceId);
    float x = p.x - craft.position.x, y = p.y - craft.position.y, z = p.z - craft.altitude;
    float localX = x * std::cos(craft.yaw) + y * std::sin(craft.yaw);
    float localY = -x * std::sin(craft.yaw) + y * std::cos(craft.yaw);
    float unbankedY = localY * std::cos(craft.bank) + z * std::sin(craft.bank);
    float unbankedZ = -localY * std::sin(craft.bank) + z * std::cos(craft.bank);
    return localX >= mesh.minimum.x - .04f && localX <= mesh.maximum.x + .04f &&
           unbankedY >= mesh.minimum.y - .04f && unbankedY <= mesh.maximum.y + .04f &&
           unbankedZ >= mesh.minimum.z - .04f && unbankedZ <= mesh.maximum.z + .04f;
}
bool AirTraffic::DamageAircraft(std::uint64_t id, int damage, bool playerOwned)
{
    auto found =
        std::find_if(aircraft.begin(), aircraft.end(), [=](const AircraftActor &c) { return c.id == id; });
    if (found == aircraft.end() || found->health <= 0 || damage <= 0)
    {
        return false;
    }
    found->health = (std::max)(0, found->health - damage);
    if (found->health == 0)
    {
        found->speed = 0;
        if (playerOwned && found->occupied)
        {
            CivicJustice::Report(Offense::VehicleDestruction, found->position);
        }
    }
    return true;
}
void AirTraffic::Update(const GameState &world, float dt)
{
    dt = Game::Clamp(dt, 0, .05f);
    for (auto &craft : aircraft)
    {
        if (craft.health <= 0)
        {
            craft.wreckSeconds += dt;
            craft.fallSpeed += 4.9f * dt;
            craft.altitude = (std::max)(.2f, craft.altitude - craft.fallSpeed * dt);
            continue;
        }
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
                                      if (craft.health <= 0 && craft.wreckSeconds > 20 &&
                                          OutsideVisibleFootprint(craft, world.cameraPosition, 80))
                                      {
                                          return true;
                                      }
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
    const float padding = (std::max)(0.f, clearance);
    for (int x = int(std::floor((p.x - padding) / 8)); x <= int(std::floor((p.x + padding) / 8)); ++x)
    {
        for (int y = int(std::floor((p.y - padding) / 8)); y <= int(std::floor((p.y + padding) / 8)); ++y)
        {
            auto cell = solidCells.find({x, y});
            if (cell == solidCells.end())
            {
                continue;
            }
            for (auto index : cell->second)
            {
                const auto &prop = props[index];
                if (p.x > prop.minimum.x - clearance && p.x < prop.maximum.x + clearance &&
                    p.y > prop.minimum.y - clearance && p.y < prop.maximum.y + clearance)
                {
                    return true;
                }
            }
        }
    }
    return false;
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
    if (CityLayout::OverlapsQuestSpace(footprint, .7f) ||
        CityPlan::OverlapsAccess(prop.minimum, prop.maximum))
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
    if (prop.solid)
    {
        for (int x = int(std::floor(prop.minimum.x / 8)); x <= int(std::floor(prop.maximum.x / 8)); ++x)
        {
            for (int y = int(std::floor(prop.minimum.y / 8)); y <= int(std::floor(prop.maximum.y / 8)); ++y)
            {
                solidCells[{x, y}].push_back(props.size() - 1);
            }
        }
    }
    return true;
}
void WorldScenery::Generate(const GameState &world)
{
    profiling::Scope startupTimer(profiling::Timer::ResourceInitialize);
    using S = models::SceneryType;
    props.clear();
    solidCells.clear();
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
            {{"halo_07",
              "헤일로",
              {0, 0},
              "익숙한 일상 속 불안",
              "요즘 관문 쪽 수신기에 잡음이 많대요. 정비가 밀렸나 봐요."},
             {"cinder",
              "신더",
              {0, 0},
              "분주한 공업 도시",
              "광산 교대조가 아직 안 돌아왔대요. 길이 막힌 건 아닌지 모르겠네요."},
             {"veil",
              "베일",
              {0, 0},
              "고요한 관측 도시",
              "관측소에서 별자리 기록을 다시 맞춘다더군요. 어제 것과 안 맞는대요."},
             {"lumen",
              "루멘",
              {0, 0},
              "빛으로 버티는 피난 도시",
              "밤길엔 불 켜진 길로 다니세요. 저도 요즘은 돌아가요."},
             {"echo",
              "에코",
              {0, 0},
              "끊어진 통신을 기다리는 도시",
              "수신기가 지난 교신을 자꾸 되풀이해요. 새 연락인 줄 알고 매번 들여다보게 되네요."}}};
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
    // Fix the street hierarchy before allocating any ordinary lots. Only the two story landmarks
    // have authored footprints; every other building is a setback from a real local street frontage.
    const auto &library = models::ModelLibrary::Get();
    Building home = state.buildings.front();
    Building observatory =
        *std::find_if(state.buildings.begin(), state.buildings.end(),
                      [](const Building &b) { return b.type == models::BuildingType::Observatory; });
    auto physical = [&](Building &building) {
        const auto &resource =
            models::ResourceAssets::Get(library.buildings[size_t(building.type)].resourceId);
        building.width = resource.physicalSize.x;
        building.depth = resource.physicalSize.y;
        building.height = resource.physicalSize.z / resource.maximum.z * 44.f;
    };
    physical(home);
    physical(observatory);
    state.buildings = {home, observatory};
    const auto &network = RoadNetwork::Get();
    unsigned lot = 0;
    for (int city = 0; city < kCityCount; ++city)
    {
        auto center = Cities()[city].center;
        for (const auto &road : network.roads)
        {
            const auto &a = network.junctions[road.from], &b = network.junctions[road.to];
            if (a.altitude > .1f || b.altitude > .1f)
            {
                continue;
            }
            auto d = Unit(Sub(b.position, a.position)), normal = RoadNetwork::RightOf(d);
            float length = Game::Distance(a.position, b.position);
            for (float along = a.radius + 8; along < length - b.radius - 6; along += 12)
            {
                auto anchor = Add(a.position, Mul(d, along));
                if (std::fabs(anchor.x - center.x) > 50 || std::fabs(anchor.y - center.y) > 43)
                {
                    continue;
                }
                for (float side : {-1.f, 1.f})
                {
                    Building building = {};
                    ++lot;
                    unsigned count = unsigned(models::BuildingType::Count);
                    unsigned type = (lot + city) % 4;
                    if (count > 5 && lot % 5 == 0 && Game::Distance(anchor, center) > 24)
                    {
                        type = 5 + (lot / 5 + city) % (count - 5);
                    }
                    building.type = models::BuildingType(type);
                    physical(building);
                    float extent = std::fabs(normal.x) * building.width * .5f +
                                   std::fabs(normal.y) * building.depth * .5f;
                    auto position = Add(anchor, Mul(normal, side * (road.lanes * .75f + 2.2f + extent)));
                    building.x = position.x - building.width * .5f;
                    building.y = position.y - building.depth * .5f;
                    building.palette.body = {.32f + .055f * (lot % 4), .38f + .04f * (city % 3),
                                             .43f + .035f * (lot % 5), 1};
                    building.palette.accent = {.32f, .7f, .78f, 1};
                    building.palette.emission = {.25f, .75f, .85f, 1};
                    state.buildings.push_back(building);
                }
            }
        }
    }
    CityLayout::ValidateBuildings(state.buildings);
    CityPlan::Rebuild(state);
    // Lia + Mara + eighteen residents = twenty Halo citizens. Kain replaces a reserved citizen.
    constexpr Vector2 positions[] = {{-5, 3}, {-1, 3},   {5, 3},    {8, -3},  {-11, 3}, {10, 6},  {-8, 8},
                                     {2, 8},  {-10, -7}, {11, -3},  {11, -5}, {-12, 7}, {-6, -7}, {-2, -7},
                                     {4, 8},  {8, 8},    {-13, -3}, {12, 8},  {-3, 3},  {0, 3}};
    constexpr const char *roles[] = {"배송 기사", "교대 근무자", "정비공", "경비원",    "학생",
                                     "상인",      "주민",        "관측원", "통신 기사", "탐사원"};
    constexpr const char *smallTalk[] = {
        "다음 도시까지 배송이 남았어요. 오늘은 길이 덜 막혔으면 좋겠네요.",
        "이제 교대하러 가요. 돌아올 때도 저 가게가 열려 있으면 좋겠는데.",
        "부품은 남아 있는데 손이 모자라네요. 다들 고장 나기 전엔 안 가져오거든요.",
        "횡단보도 신호가 바뀌면 건너세요. 조용해도 화물차가 금방 와요.",
        "관측소 견학을 신청했어요. 관문이 멈추기 전에는 어땠는지 궁금해서요.",
        "물건은 운송편이 오는 대로 채워요. 필요한 게 있으면 미리 말해 주세요.",
        "해 질 때 도시 밖 풀밭을 보셨어요? 바람 부는 쪽부터 빛이 나요.",
        "기록 하나가 다르면 오차일 수 있죠. 그래서 어제 것부터 다시 보고 있어요.",
        "응답이 왔다고 연결된 건 아니에요. 발신 기록까지 확인해야죠.",
        "돌아올 길에 표식을 남겨 두세요. 여기선 익숙한 풍경도 꽤 비슷해 보여요."};
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
            resident.dialogue = resident.name + " / " + (i % 3 == 0 ? city.rumor : smallTalk[i % 10]);
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
    BodyCollision::RecoverBike();
    Vector2 previous = state.playerPosition;
    float oldYaw = state.bikeYaw;
    float steering = float(state.pressedKeys['d']) - float(state.pressedKeys['a']);
    state.bikeYaw = std::remainder(
        state.bikeYaw + steering * (state.bikeSpeed < -.05f ? -1 : 1) * 1.8f * deltaSeconds, 2 * kPi);
    bool forward = state.pressedKeys['w'] && !state.pressedKeys['s'];
    bool reverse = state.pressedKeys['s'] && !state.pressedKeys['w'];
    float targetSpeed = forward   ? (state.pressedKeys[' '] ? kBikeBoostSpeed : kBikeCruiseSpeed)
                        : reverse ? -kBikeReverseSpeed
                                  : 0;
    bool braking =
        state.bikeSpeed * targetSpeed < 0 || (targetSpeed == 0 && std::fabs(state.bikeSpeed) > .01f);
    float acceleration = braking ? 24.f : reverse ? 4.f : 18.f;
    state.bikeSpeed +=
        Game::Clamp(targetSpeed - state.bikeSpeed, -acceleration * deltaSeconds, acceleration * deltaSeconds);
    auto blocked = [&](Vector2 p) {
        float h = RoadNetwork::Get().DriveElevation(p, state.bikeElevation);
        if (state.bikeElevation > .5f && h < state.bikeElevation - .6f)
        {
            return true;
        }
        return BodyCollision::BikeBlocked(p, state.bikeYaw, h, false);
    };
    if (blocked(previous))
    {
        float proposedYaw = state.bikeYaw;
        state.bikeYaw = oldYaw;
        if (blocked(previous))
        {
            state.bikeYaw = proposedYaw;
        }
    }
    Vector2 desired = {previous.x + std::cos(state.bikeYaw) * state.bikeSpeed * deltaSeconds,
                       previous.y + std::sin(state.bikeYaw) * state.bikeSpeed * deltaSeconds};
    state.playerPosition = BodyCollision::Slide(previous, desired, blocked);
    float requested = Game::Distance(previous, desired),
          actual = Game::Distance(previous, state.playerPosition);
    if (requested > .0001f && actual < requested * .99f)
    {
        state.bikeSpeed *= Game::Clamp(actual / requested, 0, 1);
    }
    state.bikeElevation = RoadNetwork::Get().DriveElevation(state.playerPosition, state.bikeElevation);
    state.bikePosition = state.playerPosition;
    state.facingYaw = state.bikeYaw;
    state.movementSpeed = deltaSeconds > 0 ? actual / deltaSeconds : 0;
    state.movementAmount = Game::Clamp(state.movementSpeed / kBikeBoostSpeed, 0, 1);
}

void Overworld::ToggleBike(GameState &state)
{
    if (Dialogue::Active() || state.journalVisible || state.dialogueHistoryVisible ||
        HospitalRecovery::Active())
    {
        return;
    }
    if (state.isBikeMounted)
    {
        if (state.bikeElevation > .3f)
        {
            Game::Get().ShowNotice("바이크 / 고가도로에서는 하차할 수 없습니다. 진출 경사로로 이동하세요.");
            return;
        }
        if (std::fabs(state.bikeSpeed) > 1)
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
                state.mouseFiring = state.mouseAiming = false;
                state.bikeSpeed = 0;
                Game::Get().ShowNotice("바이크 / 하차했습니다. 주차 위치는 유지됩니다.");
                return;
            }
        }
        Game::Get().ShowNotice("바이크 / 내릴 공간이 부족합니다. 넓은 곳으로 이동하세요.");
        return;
    }
    state.isBikeMounted = true;
    state.mouseFiring = state.mouseAiming = false;
    state.playerPosition = state.bikePosition;
    state.bikeSpeed = 0;
    Game::Get().ShowNotice(
        "바이크 / W 가속 · S 제동 · A/D 조향 · 정차 후 E 하차. 교차로의 홀로그램 신호를 확인하세요.");
}
} // namespace tutorial
