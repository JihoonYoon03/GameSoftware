#include "stdafx.h"
#include "TransitLayout.h"
#include "Overworld.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tutorial
{
namespace
{
Vector2 Add(Vector2 a, Vector2 d, float amount)
{
    return {a.x + d.x * amount, a.y + d.y * amount};
}
struct Arm
{
    Vector2 direction;
    int lanes;
};
std::vector<Arm> Arms(const RoadNetwork &network, int junction)
{
    std::vector<Arm> arms;
    for (const auto &road : network.roads)
    {
        if (road.from != junction && road.to != junction)
        {
            continue;
        }
        auto from = network.junctions[junction].position;
        auto to = network.junctions[road.from == junction ? road.to : road.from].position;
        float length = Game::Distance(from, to);
        arms.push_back({{(to.x - from.x) / length, (to.y - from.y) / length}, road.lanes});
    }
    return arms;
}
std::string RoadPrefix(int lanes)
{
    return "Transit_Road_" + std::to_string(lanes) + "L_";
}
struct Plan
{
    std::string road, walk;
    Vector2 origin;
    float yaw = 0, radius = 0;
    int lanes = 2, wideLanes = 2;
    enum class Shape
    {
        Straight,
        Cross,
        Tee,
        Curve,
        Taper,
        Terminal
    } shape = Shape::Straight;
    Vector2 first = {}, second = {};
};
std::vector<Plan> &Plans()
{
    static std::vector<Plan> plans;
    return plans;
}
Plan Junction(const RoadNetwork &network, int index)
{
    auto arms = Arms(network, index);
    auto p = network.junctions[index].position;
    Plan plan;
    plan.origin = p;
    int lanes = arms.front().lanes;
    plan.lanes = plan.wideLanes = lanes;
    plan.first = arms[0].direction;
    if (arms.size() > 1)
    {
        plan.second = arms[1].direction;
    }
    if (arms.size() >= 3)
    {
        for (const auto &arm : arms)
        {
            if (arm.lanes != lanes)
            {
                throw std::runtime_error("Mixed-width junction requires an approach taper");
            }
        }
        const bool tee = arms.size() == 3;
        plan.shape = tee ? Plan::Shape::Tee : Plan::Shape::Cross;
        plan.road = RoadPrefix(lanes) + (tee ? "TJunction_R4" : "XJunction_R4");
        plan.walk =
            "Transit_WalkCorners_" + std::to_string(lanes) + "L_" + (tee ? "TJunction_R4" : "XJunction_R4");
        plan.radius = lanes * RoadNetwork::kLaneWidth * .5f + 2;
        if (tee)
        {
            Vector2 missing = {};
            for (const auto &arm : arms)
            {
                missing = Add(missing, arm.direction, -1);
            }
            plan.yaw = std::atan2(missing.y, missing.x) - kPi * .5f;
        }
    }
    else if (arms.size() == 2)
    {
        const auto a = arms[0], b = arms[1];
        float dot = a.direction.x * b.direction.x + a.direction.y * b.direction.y;
        if (dot < -.99f)
        {
            if (a.lanes != b.lanes)
            {
                auto small = a.lanes < b.lanes ? a : b;
                int low = (std::min)(a.lanes, b.lanes), high = (std::max)(a.lanes, b.lanes);
                plan.radius = low == 2 && high == 6 ? 9.f : 6.f;
                plan.shape = Plan::Shape::Taper;
                plan.lanes = low;
                plan.wideLanes = high;
                const auto suffix = std::to_string(low) + "L_" + std::to_string(high) + "L_" +
                                    (plan.radius == 9 ? "36m" : "24m");
                plan.road = "Transit_Road_Taper_" + suffix;
                plan.walk = "Transit_WalkSides_Taper_" + suffix;
                plan.yaw = std::atan2(-small.direction.y, -small.direction.x);
            }
        }
        else
        {
            if (std::fabs(dot) > .001f || a.lanes != b.lanes)
            {
                throw std::runtime_error("Transit corner must be orthogonal and equal-width");
            }
            plan.radius = 9; // Native Curve90_R18: 18 metres -> 9 world units.
            plan.shape = Plan::Shape::Curve;
            const bool left = (-a.direction.x) * b.direction.y - (-a.direction.y) * b.direction.x > 0;
            auto start = left ? a.direction : b.direction;
            plan.origin = Add(p, start, plan.radius);
            plan.yaw = std::atan2(-start.y, -start.x);
            plan.road = RoadPrefix(lanes) + "Curve90_R18";
            plan.walk = "Transit_WalkSides_" + std::to_string(lanes) + "L_Curve90_R18";
        }
    }
    else
    {
        // A finite mine access pad, not a wrapping vehicle route or an open dummy intersection.
        plan.radius = 2;
        plan.shape = Plan::Shape::Terminal;
        plan.origin = Add(p, arms[0].direction, 1);
        plan.yaw = std::atan2(arms[0].direction.y, arms[0].direction.x);
        plan.road = RoadPrefix(lanes) + "Straight_4m";
    }
    return plan;
}
void Place(std::vector<TransitPlacement> &placements, const std::string &asset, Vector2 p, float yaw,
           float radius = 10, float altitude = .015f, bool air = false)
{
    if (!asset.empty())
    {
        placements.push_back({asset, p, yaw, altitude, radius, air});
    }
}
void WalkStrip(std::vector<TransitPlacement> &placements, Vector2 from, Vector2 direction, float length)
{
    const float yaw = std::atan2(direction.y, direction.x);
    float cursor = 0;
    while (length - cursor > .01f)
    {
        float remaining = length - cursor;
        float part = remaining >= 4 ? 4 : remaining >= 2 ? 2 : remaining >= 1 ? 1 : .5f;
        std::string asset = part == .5f ? "Trim_Walk_W2_0.5"
                                        : "Transit_Walk_W2_Straight_" + std::to_string(int(part * 2)) + "m";
        Place(placements, asset, Add(from, direction, cursor + part * .5f), yaw, part + 2);
        cursor += part;
    }
}
std::vector<TransitPlacement> Build()
{
    const auto &network = RoadNetwork::Get();
    std::vector<TransitPlacement> placements;
    for (size_t i = 0; i < network.junctions.size(); ++i)
    {
        auto plan = Junction(network, int(i));
        Place(placements, plan.road, plan.origin, plan.yaw, plan.radius * 2 + 3);
        Place(placements, plan.walk, plan.origin, plan.yaw, plan.radius * 2 + 3);
    }
    for (const auto &road : network.roads)
    {
        auto a = network.junctions[road.from].position, b = network.junctions[road.to].position;
        float total = Game::Distance(a, b);
        Vector2 direction = {(b.x - a.x) / total, (b.y - a.y) / total};
        auto from = Add(a, direction, network.junctions[road.from].radius);
        float length = total - network.junctions[road.from].radius - network.junctions[road.to].radius;
        const float yaw = std::atan2(direction.y, direction.x);
        const bool startCrossing = network.IsSignalized(road.from),
                   endCrossing = network.IsSignalized(road.to);
        const float begin = startCrossing ? 2.f : 0.f, finish = length - (endCrossing ? 2.f : 0.f);
        float cursor = begin;
        while (finish - cursor > .01f)
        {
            float remaining = finish - cursor;
            float part = remaining >= 8   ? 8
                         : remaining >= 4 ? 4
                         : remaining >= 2 ? 2
                                          : std::round(remaining * 2) * .5f;
            if (part < .49f || part > remaining + .01f)
            {
                throw std::runtime_error("Transit ports require half-unit alignment");
            }
            std::string asset =
                part >= 2 ? RoadPrefix(road.lanes) + "Straight_" + std::to_string(int(part * 2)) + "m"
                          : "Trim_Road_" + std::to_string(road.lanes) + "L_" +
                                (part == .5f ? "0.5"
                                 : part == 1 ? "1"
                                             : "1.5");
            Place(placements, asset, Add(from, direction, cursor + part * .5f), yaw, part + 7);
            cursor += part;
        }
        auto right = RoadNetwork::RightOf(direction);
        for (float side : {-1.f, 1.f})
        {
            auto start = Add(Add(from, direction, begin), right,
                             side * (road.lanes * RoadNetwork::kLaneWidth * .5f + .5f));
            WalkStrip(placements, start, direction, finish - begin);
        }
        // Crosswalks replace the first/last two units, rather than covering another asphalt mesh.
        for (int junction : {road.from, road.to})
        {
            if (!network.IsSignalized(junction))
            {
                continue;
            }
            auto outward = junction == road.from ? direction : Vector2{-direction.x, -direction.y};
            auto p =
                Add(network.junctions[junction].position, outward, network.junctions[junction].radius + 1.f);
            Place(placements, RoadPrefix(road.lanes) + "Crosswalk_4m", p, yaw, road.lanes + 3);
            auto sideDirection = RoadNetwork::RightOf(outward);
            for (float side : {-1.f, 1.f})
            {
                auto normal = Vector2{sideDirection.x * side, sideDirection.y * side};
                const float half = road.lanes * RoadNetwork::kLaneWidth * .5f;
                auto ramp = Add(p, normal, half + 1);
                // +X is the flush end: connect it to the road, and the raised end to a 4m footway.
                Place(placements, "Transit_Walk_W4_AccessibleRamp_4m", ramp, std::atan2(-normal.y, -normal.x),
                      4);
                Place(placements, "Transit_Walk_W4_Straight_4m", Add(p, normal, half + 3),
                      std::atan2(normal.y, normal.x), 4);
            }
        }
    }
    // Native sparse guides follow the actual Bezier flight corridors at their actual altitude.
    for (int route = 0; route < int(network.airCorridors.size()); ++route)
    {
        for (float t = -.15f; t < 1.15f;)
        {
            auto tangent = AirTraffic::Tangent(route, t);
            float speed = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
            Place(placements, "Transit_AirGuide_Straight_16m", AirTraffic::Position(route, t),
                  std::atan2(tangent.y, tangent.x), 7, network.airCorridors[route].altitude, true);
            t += 8.f / (std::max)(1.f, speed);
        }
    }
    return placements;
}
} // namespace
void TransitLayout::ConfigureJunctions(RoadNetwork &network)
{
    Plans().clear();
    for (size_t i = 0; i < network.junctions.size(); ++i)
    {
        auto plan = Junction(network, int(i));
        // Keep the logical center separately from the curve module's entry origin.
        network.junctions[i].radius = plan.radius;
        Plans().push_back(plan);
    }
    for (const auto &road : network.roads)
    {
        float span =
            Game::Distance(network.junctions[road.from].position, network.junctions[road.to].position) -
            network.junctions[road.from].radius - network.junctions[road.to].radius;
        if (span < 4)
        {
            throw std::runtime_error("Transit junction ports overlap or leave an unusable lane");
        }
    }
}
bool TransitLayout::ContainsJunction(int junction, Vector2 position, float margin)
{
    const auto &network = RoadNetwork::Get();
    const auto &plan = Plans().at(size_t(junction));
    auto center = network.junctions[junction].position;
    Vector2 delta = {position.x - center.x, position.y - center.y};
    float h = plan.lanes * RoadNetwork::kLaneWidth * .5f;
    float radius = plan.radius;
    if (std::fabs(delta.x) > radius + h + margin || std::fabs(delta.y) > radius + h + margin)
    {
        return false;
    }
    if (plan.shape == Plan::Shape::Curve)
    {
        Vector2 q = {delta.x - radius * (plan.first.x + plan.second.x),
                     delta.y - radius * (plan.first.y + plan.second.y)};
        return q.x * plan.first.x + q.y * plan.first.y <= margin &&
               q.x * plan.second.x + q.y * plan.second.y <= margin &&
               std::fabs(std::sqrt(q.x * q.x + q.y * q.y) - radius) <= h + margin;
    }
    if (plan.shape == Plan::Shape::Terminal)
    {
        float along = delta.x * plan.first.x + delta.y * plan.first.y;
        float across = delta.x * plan.first.y - delta.y * plan.first.x;
        return along >= -margin && along <= radius + margin && std::fabs(across) <= h + margin;
    }
    float c = std::cos(plan.yaw), s = std::sin(plan.yaw);
    float x = delta.x * c + delta.y * s, y = -delta.x * s + delta.y * c;
    if (plan.shape == Plan::Shape::Taper)
    {
        float t = Game::Clamp((x + radius) / (radius * 2), 0, 1);
        t = t * t * (3 - 2 * t);
        float width = (plan.lanes + (plan.wideLanes - plan.lanes) * t) * RoadNetwork::kLaneWidth * .5f;
        return std::fabs(x) <= radius + margin && std::fabs(y) <= width + margin;
    }
    if (plan.shape == Plan::Shape::Straight)
    {
        return false;
    }
    if (std::fabs(x) > radius + margin || std::fabs(y) > radius + margin ||
        (plan.shape == Plan::Shape::Tee && y > h + margin))
    {
        return false;
    }
    if (std::fabs(x) <= h + margin || std::fabs(y) <= h + margin)
    {
        return true;
    }
    float dx = radius - std::fabs(x), dy = radius - std::fabs(y);
    float corner = (std::max)(0.f, 2.f - margin);
    return dx * dx + dy * dy >= corner * corner;
}
const std::vector<TransitPlacement> &TransitLayout::Get()
{
    static const auto placements = Build();
    return placements;
}
bool TransitLayout::OverlapsPavement(Vector2 minimum, Vector2 maximum, float clearance)
{
    struct Footprint
    {
        Vector2 minimum, maximum;
    };
    static const auto bounds = []() {
        std::vector<Footprint> footprints;
        for (const auto &module : Get())
        {
            if (module.air || module.asset.find("Walk") == std::string::npos)
            {
                continue;
            }
            const auto &mesh =
                models::ResourceAssets::Get(models::ModelLibrary::Transit(module.asset).resourceId);
            Footprint box = {{1.e9f, 1.e9f}, {-1.e9f, -1.e9f}};
            const float c = std::cos(module.yaw), s = std::sin(module.yaw);
            for (float x : {mesh.minimum.x, mesh.maximum.x})
            {
                for (float y : {mesh.minimum.y, mesh.maximum.y})
                {
                    float px = module.position.x + x * c - y * s, py = module.position.y + x * s + y * c;
                    box.minimum.x = (std::min)(box.minimum.x, px);
                    box.minimum.y = (std::min)(box.minimum.y, py);
                    box.maximum.x = (std::max)(box.maximum.x, px);
                    box.maximum.y = (std::max)(box.maximum.y, py);
                }
            }
            footprints.push_back(box);
        }
        return footprints;
    }();
    return std::any_of(bounds.begin(), bounds.end(), [&](const Footprint &box) {
        return minimum.x < box.maximum.x + clearance && maximum.x > box.minimum.x - clearance &&
               minimum.y < box.maximum.y + clearance && maximum.y > box.minimum.y - clearance;
    });
}
} // namespace tutorial
