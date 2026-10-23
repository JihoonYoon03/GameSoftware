#include "stdafx.h"
#include "Game.h"
#include "Overworld.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <set>
#include <string>
#include <vector>

// CPU-only checks of the authored transport plan, surface layers and generated frontages.
namespace
{
using namespace tutorial;
void Require(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << "Road plan regression: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
Vector2 Add(Vector2 a, Vector2 b, float scale = 1)
{
    return {a.x + b.x * scale, a.y + b.y * scale};
}
Vector2 Sub(Vector2 a, Vector2 b)
{
    return {a.x - b.x, a.y - b.y};
}
float Dot(Vector2 a, Vector2 b)
{
    return a.x * b.x + a.y * b.y;
}
float Cross(Vector2 a, Vector2 b)
{
    return a.x * b.y - a.y * b.x;
}
Vector2 Unit(Vector2 a)
{
    float length = std::hypot(a.x, a.y);
    return {a.x / length, a.y / length};
}
std::pair<Vector2, Vector2> SurfaceEnds(const RoadNetwork &net, const RoadSegment &road)
{
    const auto &a = net.junctions[road.from], &b = net.junctions[road.to];
    auto direction = Unit(Sub(b.position, a.position));
    return {Add(a.position, direction, a.radius), Add(b.position, direction, -b.radius)};
}
void TestConnectivityAndJunctions()
{
    const auto &net = RoadNetwork::Get();
    Require(!net.roads.empty() && !net.junctions.empty(), "empty transport graph");
    std::vector<bool> reached(net.junctions.size());
    std::queue<int> pending;
    pending.push(0);
    reached[0] = true;
    while (!pending.empty())
    {
        int node = pending.front();
        pending.pop();
        for (int laneIndex : net.junctions[node].outgoing)
        {
            const auto &lane = net.lanes[laneIndex];
            Require(lane.from == node, "outgoing lane has wrong node");
            if (!reached[lane.to])
            {
                reached[lane.to] = true;
                pending.push(lane.to);
            }
        }
    }
    Require(std::all_of(reached.begin(), reached.end(), [](bool value) { return value; }),
            "regional graph has a disconnected component");
    for (const auto &city : Overworld::Cities())
    {
        bool linked = false;
        for (size_t j = 0; j < net.junctions.size(); ++j)
        {
            linked |= reached[j] && net.junctions[j].altitude < .1f &&
                      Game::Distance(net.junctions[j].position, city.center) < 45;
        }
        Require(linked, std::string("city lacks an accessible street: ") + city.id);
    }
    bool mineLinked = false;
    for (size_t j = 0; j < net.junctions.size(); ++j)
    {
        mineLinked |= reached[j] && Game::Distance(net.junctions[j].position, Overworld::kMineEntrance) < .1f;
    }
    Require(mineLinked, "mine entrance is disconnected from the regional road graph");
    for (size_t j = 0; j < net.junctions.size(); ++j)
    {
        int arms = 0, lanes = 0;
        for (const auto &road : net.roads)
        {
            if (road.from == int(j) || road.to == int(j))
            {
                ++arms;
                lanes += road.lanes;
            }
        }
        const std::string id = "junction " + std::to_string(j);
        Require(arms > 0 && arms <= 4, id + " has an invalid number of arms");
        Require(lanes <= 10, id + " funnels more than ten incident lanes into one crossing");
        Require(net.junctions[j].radius >= 0, id + " has a negative trim radius");
    }
    for (size_t r = 0; r < net.roads.size(); ++r)
    {
        const auto &road = net.roads[r];
        Require(road.lanes == 2 || road.lanes == 4 || road.lanes == 6, "unsupported road width");
        auto ends = SurfaceEnds(net, road);
        float span = Game::Distance(net.junctions[road.from].position, net.junctions[road.to].position) -
                     net.junctions[road.from].radius - net.junctions[road.to].radius;
        Require(span >= 7.9f, "road " + std::to_string(r) + " has no usable length after junction trims");
        for (float fraction : {.01f, .25f, .5f, .75f, .99f})
        {
            auto position = Add(ends.first, Sub(ends.second, ends.first), fraction);
            Require(net.ContainsSurface(position, net.RoadElevation(int(r), fraction), .02f),
                    "road center leaves the rendered driving surface");
        }
    }
}
void TestCrossingsAndDuplicateSpace()
{
    const auto &net = RoadNetwork::Get();
    bool gradeSeparated = false;
    for (size_t i = 0; i < net.roads.size(); ++i)
    {
        const auto &a = net.roads[i];
        auto origin = net.junctions[a.from].position;
        auto da = Sub(net.junctions[a.to].position, origin);
        for (size_t k = i + 1; k < net.roads.size(); ++k)
        {
            const auto &b = net.roads[k];
            auto delta = Sub(net.junctions[b.from].position, origin);
            auto db = Sub(net.junctions[b.to].position, net.junctions[b.from].position);
            const std::string ids = "roads " + std::to_string(i) + "/" + std::to_string(k);
            const float denominator = Cross(da, db);
            if (std::fabs(denominator) < .001f)
            {
                if (std::fabs(Cross(delta, Unit(da))) > .05f)
                {
                    continue;
                }
                float t0 = Dot(delta, da) / Dot(da, da), t1 = t0 + Dot(db, da) / Dot(da, da);
                float lo = (std::max)(0.f, (std::min)(t0, t1)), hi = (std::min)(1.f, (std::max)(t0, t1));
                if ((hi - lo) * std::hypot(da.x, da.y) <= .15f)
                {
                    continue;
                }
                float t = (lo + hi) * .5f;
                auto point = Add(origin, da, t);
                float u = Dot(Sub(point, net.junctions[b.from].position), db) / Dot(db, db);
                Require(std::fabs(net.RoadElevation(int(i), t) - net.RoadElevation(int(k), u)) > .15f,
                        ids + " duplicate the same road space");
                continue;
            }
            float t = Cross(delta, db) / denominator, u = Cross(delta, da) / denominator;
            if (t < -.0001f || t > 1.0001f || u < -.0001f || u > 1.0001f)
            {
                continue;
            }
            float separation = std::fabs(net.RoadElevation(int(i), t) - net.RoadElevation(int(k), u));
            const bool shared = a.from == b.from || a.from == b.to || a.to == b.from || a.to == b.to;
            if (separation < .05f)
            {
                Require(shared, ids + " cross at the same level without a common graph node");
            }
            else
            {
                Require(!shared, ids + " connect surfaces at contradictory heights");
                Require(separation >= 3.5f, ids + " lack clearance at a grade-separated crossing");
                gradeSeparated = true;
            }
        }
    }
    Require(gradeSeparated, "no functioning grade-separated regional route");
    const Vector2 crossing = {48, -17};
    Require(net.ContainsSurface(crossing, 0) && net.ContainsSurface(crossing, 4.2f),
            "eastern underpass must contain distinct ground and 8.4-metre deck surfaces");
    Require(std::none_of(net.junctions.begin(), net.junctions.end(),
                         [&](const RoadJunction &junction) {
                             return Game::Distance(junction.position, crossing) < .1f;
                         }),
            "grade-separated crossing was incorrectly turned into a junction");
    Require(std::fabs(net.DriveElevation(crossing, 0)) < .001f &&
                std::fabs(net.DriveElevation(crossing, 4.2f) - 4.2f) < .001f,
            "driving through the underpass changes deck level");
}
void TestRampsAndSurfaceContinuity()
{
    const auto &net = RoadNetwork::Get();
    unsigned ramps = 0;
    for (size_t r = 0; r < net.roads.size(); ++r)
    {
        const auto &road = net.roads[r];
        const auto &a = net.junctions[road.from], &b = net.junctions[road.to];
        if (std::fabs(a.altitude - b.altitude) < .01f)
        {
            continue;
        }
        ++ramps;
        auto ends = SurfaceEnds(net, road);
        float span = Game::Distance(ends.first, ends.second);
        Require(std::fabs(a.altitude - b.altitude) / span <= .09f,
                "ramp " + std::to_string(r) + " exceeds a nine percent grade");
        Require(std::fabs(net.RoadElevation(int(r), 0) - a.altitude) < .001f &&
                    std::fabs(net.RoadElevation(int(r), 1) - b.altitude) < .001f,
                "ramp endpoints do not match their junction levels");
        float previousHeight = net.RoadElevation(int(r), 0);
        for (int sample = 1; sample <= 440; ++sample)
        {
            float height = net.RoadElevation(int(r), sample / 440.f);
            Require(std::fabs(height - previousHeight) / (span / 440) < .09001f,
                    "authored ramp vertical curve exceeds nine percent grade");
            previousHeight = height;
        }
        Require(std::fabs(net.RoadElevation(int(r), .001f) - a.altitude) < .0001f &&
                    std::fabs(net.RoadElevation(int(r), .999f) - b.altitude) < .0001f,
                "native ramp endpoints must have horizontal tangents");
        for (bool reverse : {false, true})
        {
            float previous = reverse ? b.altitude : a.altitude;
            for (int sample = 0; sample <= 400; ++sample)
            {
                float t = reverse ? 1 - sample / 400.f : sample / 400.f;
                auto point = Add(ends.first, Sub(ends.second, ends.first), t);
                float actual = net.DriveElevation(point, previous), expected = net.RoadElevation(int(r), t);
                Require(std::fabs(actual - expected) < .04f,
                        "ramp " + std::to_string(r) + " loses its driving surface at t=" + std::to_string(t) +
                            " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) +
                            ")");
                previous = actual;
            }
        }
    }
    Require(ramps >= 2, "elevated deck has no paired accessible ramps");
}
// Query the actual imported/fitted triangles, rather than just repeating graph maths.
bool ArtSurface(Vector2 point, float height)
{
    for (const auto &placement : TransitLayout::Get())
    {
        if (placement.air || Game::Distance(point, placement.position) > placement.radius)
        {
            continue;
        }
        const auto &mesh =
            models::ResourceAssets::Get(models::ResourceAssets::Find("transit/" + placement.asset));
        Vector2 delta = Sub(point, placement.position);
        Vector2 local = {delta.x * std::cos(placement.yaw) + delta.y * std::sin(placement.yaw),
                         -delta.x * std::sin(placement.yaw) + delta.y * std::cos(placement.yaw)};
        for (size_t i = 0; i < mesh.vertices.size(); i += 3)
        {
            const auto &a = mesh.vertices[i], &b = mesh.vertices[i + 1], &c = mesh.vertices[i + 2];
            if (a.nz < .7f || a.surface == 4 || a.r > .3f)
            {
                continue;
            }
            Vector2 p = {a.x, a.y}, ab = {b.x - a.x, b.y - a.y}, ac = {c.x - a.x, c.y - a.y};
            float determinant = Cross(ab, ac);
            if (std::fabs(determinant) < .000001f)
            {
                continue;
            }
            float u = Cross(Sub(local, p), ac) / determinant, v = Cross(ab, Sub(local, p)) / determinant;
            if (u >= -.0001f && v >= -.0001f && u + v <= 1.0001f &&
                std::fabs(a.z + (b.z - a.z) * u + (c.z - a.z) * v + placement.altitude - height) < .004f)
            {
                return true;
            }
        }
    }
    return false;
}
void TestDeliveredMeshes()
{
    const auto &net = RoadNetwork::Get();
    int decks = 0, ramps = 0, piers = 0;
    for (const auto &p : TransitLayout::Get())
    {
        Require(p.asset.find("Draft") == std::string::npos, "blockout road remains in production layout");
        if (p.asset.find("Deck_2L") != std::string::npos)
        {
            ++decks;
        }
        if (p.asset.find("Ramp_2L") != std::string::npos)
        {
            ++ramps;
        }
        if (p.asset == "RoadPlan_Pier_H8")
        {
            ++piers;
            Require(!net.Contains(p.position, 2.2f), "support placed inside a ground road or sidewalk");
            auto leg = Add(p.position, RoadNetwork::RightOf({std::cos(p.yaw), std::sin(p.yaw)}), .9f);
            Require(TransitLayout::IsBlocked(leg), "imported pier leg has no collision");
            Require(!TransitLayout::IsBlocked(p.position),
                    "crosshead blocks the open span beneath the bridge");
        }
    }
    Require(decks == 12 && ramps == 2 && piers >= 4, "native bridge assembly is incomplete");
    for (size_t r = 0; r < net.roads.size(); ++r)
    {
        const auto &road = net.roads[r];
        auto ends = SurfaceEnds(net, road);
        auto d = Unit(Sub(ends.second, ends.first)), n = RoadNetwork::RightOf(d);
        // Native tile seams and both lane edges must be covered by actual surface geometry.
        int samples = int(std::ceil(Game::Distance(ends.first, ends.second) * 2));
        for (int i = 0; i <= samples; ++i)
        {
            float t = float(i) / samples;
            auto p = Add(ends.first, Sub(ends.second, ends.first), t);
            for (float across : {-.74f * road.lanes, 0.f, .74f * road.lanes})
            {
                Require(ArtSurface(Add(p, n, across), net.RoadElevation(int(r), t) + .025f),
                        "mesh/graph gap at road " + std::to_string(r) + " t=" + std::to_string(t));
            }
            if (std::fabs(net.junctions[road.from].altitude - net.junctions[road.to].altitude) > .1f)
            {
                float h = net.RoadElevation(int(r), t);
                Require(!TransitLayout::IsBlocked(p, .2f, h, true), "bike cannot climb the native ramp");
            }
        }
    }
    Require(ArtSurface({48, -17}, 4.225f) && ArtSurface({48, -17}, .025f),
            "rendered bridge does not preserve independent ground and deck surfaces");
}
void TestTurnSurfaces()
{
    const auto &net = RoadNetwork::Get();
    for (size_t i = 0; i < net.lanes.size(); ++i)
    {
        const auto &in = net.lanes[i];
        const auto &junction = net.junctions[in.to];
        for (int index : junction.outgoing)
        {
            const auto &out = net.lanes[index];
            float dot = Dot(in.direction, out.direction);
            TrafficTurn turn = dot > .82f                                ? TrafficTurn::Straight
                               : Cross(in.direction, out.direction) <= 0 ? TrafficTurn::Left
                                                                         : TrafficTurn::Right;
            int target = turn == TrafficTurn::Right && net.IsSignalized(in.to)
                             ? net.roads[out.road].lanes / 2 - 1
                             : (std::min)(in.slot, net.roads[out.road].lanes / 2 - 1);
            if (in.road == out.road || out.slot != target || !net.CanTurn(int(i), turn))
            {
                continue;
            }
            float handle = Game::Distance(in.end, out.start) * (std::fabs(dot) < .001f ? .3905243f
                                                                : dot > .99f           ? 1.f / 3
                                                                : dot < -.8f           ? 2.f / 3
                                                                                       : .45f);
            auto a = Add(in.end, in.direction, handle), b = Add(out.start, out.direction, -handle);
            for (int step = 0; step <= 32; ++step)
            {
                float t = step / 32.f, u = 1 - t;
                Vector2 p = {};
                p = Add(p, in.end, u * u * u);
                p = Add(p, a, 3 * u * u * t);
                p = Add(p, b, 3 * u * t * t);
                p = Add(p, out.start, t * t * t);
                Require(ArtSurface(p, junction.altitude + .025f),
                        "vehicle turn leaves delivered surface at junction " + std::to_string(in.to) +
                            " from lane " + std::to_string(i) + " to " + std::to_string(index));
            }
        }
    }
}
void TestBridgeDriving(Game &game)
{
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.isBikeMounted = true;
    for (bool reverse : {false, true})
    {
        world.bikeElevation = world.bikeSpeed = 0;
        world.bikeYaw = reverse ? -1.57079632679f : 1.57079632679f;
        world.playerPosition = world.bikePosition = {48, reverse ? 100.1f : -100.1f};
        world.pressedKeys['w'] = true;
        float highest = 0;
        for (int frame = 0; frame < 1800; ++frame)
        {
            Overworld::UpdateBike(world, 1.f / 60);
            highest = (std::max)(highest, world.bikeElevation);
            if ((reverse && world.playerPosition.y < -100) || (!reverse && world.playerPosition.y > 100))
            {
                break;
            }
        }
        Require(highest > 4.19f, "actual bike movement never reaches the delivered deck");
        Require(reverse ? world.playerPosition.y < -100 : world.playerPosition.y > 100,
                "actual bike cannot traverse both native ramps and the overpass");
        Require(world.bikeElevation < .01f, "actual bike does not return to the ground road");
    }
    world.pressedKeys['w'] = false;
    world.isBikeMounted = false;
    world.bikeElevation = 0;
}
void TestLotsAndFrontages(Game &game)
{
    const auto &net = RoadNetwork::Get();
    const auto &world = game.GetState();
    std::array<bool, size_t(models::BuildingType::Count)> styles = {};
    Require(CityPlan::Connections().size() == world.buildings.size(),
            "a generated building has no connected frontage");
    for (size_t i = 0; i < world.buildings.size(); ++i)
    {
        const auto &building = world.buildings[i];
        styles[size_t(building.type)] = true;
        Require(!CityLayout::OverlapsRoad(building, 0),
                "building " + std::to_string(i) + " covers road or sidewalk space");
        Require(!CityLayout::OverlapsQuestSpace(building), "a building covers a quest approach");
        const auto &mesh = models::ResourceAssets::Get(
            models::ModelLibrary::Get().buildings[size_t(building.type)].resourceId);
        const float top = building.height / 44.f * mesh.maximum.z;
        for (size_t r = 0; r < net.roads.size(); ++r)
        {
            const auto &road = net.roads[r];
            if ((std::max)(net.junctions[road.from].altitude, net.junctions[road.to].altitude) < .1f)
            {
                continue;
            }
            auto ends = SurfaceEnds(net, road);
            int count = int(std::ceil(Game::Distance(ends.first, ends.second) * 2));
            auto normal = RoadNetwork::RightOf(Unit(Sub(ends.second, ends.first)));
            float half = road.lanes * RoadNetwork::kLaneWidth * .5f;
            for (int sample = 0; sample <= count; ++sample)
            {
                float t = float(sample) / count;
                if (net.RoadElevation(int(r), t) > top + .5f)
                {
                    continue;
                }
                auto center = Add(ends.first, Sub(ends.second, ends.first), t);
                for (float side : {-half, 0.f, half})
                {
                    auto p = Add(center, normal, side);
                    Require(!(p.x > building.x - .2f && p.x < building.x + building.width + .2f &&
                              p.y > building.y - .2f && p.y < building.y + building.depth + .2f),
                            "building " + std::to_string(i) + " intersects elevated road " +
                                std::to_string(r));
                }
            }
        }
    }
    for (auto type : {models::BuildingType::SpireHelix, models::BuildingType::TwinBridge,
                      models::BuildingType::ArcologyTerrace, models::BuildingType::TransitAtrium,
                      models::BuildingType::CourtyardHabitat})
    {
        Require(styles[size_t(type)], "a newly imported architecture style has no usable lot");
    }
    for (const auto &connection : CityPlan::Connections())
    {
        Require(connection.building < world.buildings.size() && connection.path.size() >= 2,
                "invalid building-to-sidewalk connection");
        for (size_t edge = 1; edge < connection.path.size(); ++edge)
        {
            auto a = connection.path[edge - 1], b = connection.path[edge];
            int count = (std::max)(1, int(std::ceil(Game::Distance(a, b) * 4)));
            for (int step = 0; step <= count; ++step)
            {
                auto p = Add(a, Sub(b, a), float(step) / count);
                Require(!game.IsPositionBlocked(p) && !net.Contains(p),
                        "building frontage is blocked or terminates in a traffic lane");
            }
        }
    }
}
void TestQuestApproaches(Game &game)
{
    // Search actual outdoor collision at half-unit resolution, including placed props and buildings.
    constexpr int width = 201, height = 161;
    auto position = [width](int cell) {
        return Vector2{-50.f + (cell % width) * .5f, -35.f + (cell / width) * .5f};
    };
    auto cell = [width](Vector2 p) {
        return int(std::round((p.y + 35) * 2)) * width + int(std::round((p.x + 50) * 2));
    };
    std::vector<bool> clear(width * height), reached(width * height);
    for (int i = 0; i < width * height; ++i)
    {
        clear[i] = !game.IsPositionBlocked(position(i));
    }
    std::queue<int> pending;
    pending.push(cell(kHomeDoor));
    reached[pending.front()] = true;
    constexpr int directions[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!pending.empty())
    {
        int at = pending.front();
        pending.pop();
        for (const auto &d : directions)
        {
            int x = at % width + d[0], y = at / width + d[1];
            if (x < 0 || y < 0 || x >= width || y >= height)
            {
                continue;
            }
            int next = y * width + x;
            if (!reached[next] && clear[next])
            {
                reached[next] = true;
                pending.push(next);
            }
        }
    }
    for (Vector2 point : {kHomeDoor, kLiaPosition, kMaraPosition, kRelayPosition, kBeaconPosition,
                          kCityReceiverPosition, kCityKainPosition, kJetBikePosition, kBikeRoadExit})
    {
        Require(clear[cell(point)] && reached[cell(point)], "quest target or bike driveway is unreachable");
    }
}
} // namespace
int main()
{
    TestConnectivityAndJunctions();
    TestCrossingsAndDuplicateSpace();
    TestRampsAndSurfaceContinuity();
    TestDeliveredMeshes();
    TestTurnSurfaces();
    auto &game = tutorial::Game::Get();
    game.Reset();
    game.GetState().isInsideHome = false;
    TestLotsAndFrontages(game);
    TestQuestApproaches(game);
    TestBridgeDriving(game);
    std::cout << "Road plan checks passed: connected cities, sensible junctions, grade separation, ramps, "
                 "building frontages, architecture and quest access.\n";
}
