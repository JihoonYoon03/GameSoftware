#include "stdafx.h"
#include "TransitLayout.h"
#include "Overworld.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <functional>
#include <queue>
#include <set>

namespace tutorial
{
namespace
{
Vector2 Add(Vector2 a, Vector2 d, float amount = 1)
{
    return {a.x + d.x * amount, a.y + d.y * amount};
}
Vector2 Sub(Vector2 a, Vector2 b)
{
    return {a.x - b.x, a.y - b.y};
}
float Cross(Vector2 a, Vector2 b)
{
    return a.x * b.y - a.y * b.x;
}
float Dot(Vector2 a, Vector2 b)
{
    return a.x * b.x + a.y * b.y;
}
Vector2 Unit(Vector2 a)
{
    float n = std::hypot(a.x, a.y);
    return {a.x / (std::max)(n, .001f), a.y / (std::max)(n, .001f)};
}
Vector2 Curve(Vector2 a, Vector2 b, Vector2 da, Vector2 db, float t)
{
    float chord = Game::Distance(a, b), dot = Dot(da, db);
    float handle = chord * (std::fabs(dot) < .001f ? .3905243f
                            : dot > .99f           ? 1.f / 3
                            : dot < -.8f           ? 2.f / 3
                                                   : .45f);
    Vector2 c = Add(a, da, handle), d = Add(b, db, -handle);
    float s = 1 - t;
    return {a.x * s * s * s + 3 * c.x * s * s * t + 3 * d.x * s * t * t + b.x * t * t * t,
            a.y * s * s * s + 3 * c.y * s * s * t + 3 * d.y * s * t * t + b.y * t * t * t};
}
struct Arm
{
    Vector2 direction;
    int lanes, road;
};
struct Plan
{
    std::vector<Arm> arms;
    std::vector<Vector2> polygon;
    float radius = 0;
};
std::vector<Plan> plans;
std::vector<Vector2> Hull(std::vector<Vector2> points)
{
    std::sort(points.begin(), points.end(),
              [](Vector2 a, Vector2 b) { return a.x == b.x ? a.y < b.y : a.x < b.x; });
    std::vector<Vector2> result;
    for (auto p : points)
    {
        while (result.size() > 1 &&
               Cross(Sub(result.back(), result[result.size() - 2]), Sub(p, result.back())) <= .00001f)
        {
            result.pop_back();
        }
        result.push_back(p);
    }
    size_t lower = result.size();
    for (int i = int(points.size()) - 2; i >= 0; --i)
    {
        auto p = points[i];
        while (result.size() > lower &&
               Cross(Sub(result.back(), result[result.size() - 2]), Sub(p, result.back())) <= .00001f)
        {
            result.pop_back();
        }
        result.push_back(p);
    }
    if (result.size() > 1)
    {
        result.pop_back();
    }
    return result;
}
const models::ResourceMesh &Source(const std::string &name)
{
    return models::ResourceAssets::Get(models::ResourceAssets::Find("transit/" + name));
}
std::string Register(models::ResourceMesh mesh, const std::string &name)
{
    mesh.name = "transit/" + name;
    models::ResourceAssets::RegisterStatic(std::move(mesh));
    return name;
}
std::string Fit(const std::string &asset, float length, float width = 1)
{
    const auto &source = Source(asset);
    float original = source.maximum.x - source.minimum.x;
    if (std::fabs(original - length) < .0001f && width == 1)
    {
        return asset;
    }
    std::string key = "Fit_" + asset + "_" + std::to_string(int(std::round(length * 10000))) + "_" +
                      std::to_string(int(width * 1000));
    static std::set<std::string> ready;
    if (ready.insert(key).second)
    {
        auto mesh = source;
        float sx = length / original;
        for (auto &v : mesh.vertices)
        {
            v.x = (v.x - (source.minimum.x + source.maximum.x) * .5f) * sx;
            v.y *= width;
            v.nx /= sx;
            v.ny /= width;
            float n = std::hypot(std::hypot(v.nx, v.ny), v.nz);
            v.nx /= n;
            v.ny /= n;
            v.nz /= n;
        }
        Register(std::move(mesh), key);
    }
    return key;
}
void Place(std::vector<TransitPlacement> &list, const std::string &asset, Vector2 p, float yaw = 0,
           float radius = 10, float altitude = .015f, bool air = false)
{
    list.push_back({asset, p, yaw, altitude, radius, air});
}
void Span(std::vector<TransitPlacement> &list, const std::string &asset, Vector2 from, Vector2 to,
          float width = 1)
{
    float length = Game::Distance(from, to);
    if (length < .01f)
    {
        return;
    }
    float native = Source(asset).maximum.x - Source(asset).minimum.x;
    auto d = Unit(Sub(to, from));
    float cursor = 0;
    while (cursor < length - .001f)
    {
        float part = (std::min)(native, length - cursor);
        Place(list, Fit(asset, part, width), Add(from, d, cursor + part * .5f), std::atan2(d.y, d.x),
              part * .5f + (Source(asset).maximum.y - Source(asset).minimum.y) * width * .5f + 1);
        cursor += part;
    }
}
// The artist's ports and companion offsets are compiled from the delivery catalogue.
// Assemblies retain all component geometry/materials and share one immutable cached mesh.
struct ArtPort
{
    Vector2 position, direction;
    int lanes;
};
struct ArtCompanion
{
    std::string name;
    Vector2 position;
    float altitude, yaw;
};
struct ArtModule
{
    std::vector<ArtPort> ports;
    std::vector<ArtCompanion> companions;
};
#include "RoadPlanCatalog.h"
struct Support
{
    Vector2 position;
    float yaw;
};
std::vector<Support> supports;
void Append(models::ResourceMesh &mesh, const models::ResourceMesh &part, Vector2 offset = {},
            float altitude = 0, float yaw = 0)
{
    float c = std::cos(yaw), s = std::sin(yaw);
    for (auto v : part.vertices)
    {
        float x = v.x, nx = v.nx;
        v.x = offset.x + x * c - v.y * s;
        v.y = offset.y + x * s + v.y * c;
        v.z += altitude;
        v.nx = nx * c - v.ny * s;
        v.ny = nx * s + v.ny * c;
        mesh.vertices.push_back(v);
    }
}
std::string Paint(const std::string &asset)
{
    std::string name = "RoadPlanPaint_" + asset;
    static std::set<std::string> ready;
    if (ready.insert(name).second)
    {
        auto mesh = Source(asset);
        mesh.vertices.clear();
        const auto &vertices = Source(asset).vertices;
        for (size_t i = 0; i < vertices.size(); i += 3)
        {
            if (vertices[i].nz > .5f)
            {
                for (size_t k = 0; k < 3; ++k)
                {
                    auto v = vertices[i + k];
                    v.z += .006f;
                    mesh.vertices.push_back(v);
                }
            }
        }
        // Paint has sub-millimetre closed boxes in the source. Their buried undersides
        // alias against the top with the game's 24-bit isometric depth range.
        Register(std::move(mesh), name);
    }
    return name;
}
std::string Assembly(const std::string &asset)
{
    std::string name = "RoadPlanAssembly_" + asset;
    static std::set<std::string> ready;
    if (ready.insert(name).second)
    {
        auto mesh = Source(asset);
        for (const auto &part : roadArt.at(asset).companions)
        {
            if (part.name.find("JunctionMarking_") != std::string::npos)
            {
                continue; // Traffic draws the stop bars before the external crosswalk, at its actual stop
                          // line.
            }
            auto name = part.name.find("Marking_") != std::string::npos ? Paint(part.name) : part.name;
            Append(mesh, Source(name), part.position, part.altitude, part.yaw);
        }
        Register(std::move(mesh), name);
    }
    return name;
}
// Fit only a short closing piece. Full-length modules always keep their native dimensions.
void RoadSpan(std::vector<TransitPlacement> &result, const std::string &prefix, Vector2 a, Vector2 b,
              float height)
{
    float length = Game::Distance(a, b), cursor = 0;
    auto d = Unit(Sub(b, a));
    while (cursor < length - .001f)
    {
        float left = length - cursor;
        int meters = left >= 8 - .001f ? 16 : left >= 4 - .001f ? 8 : 4;
        float part = (std::min)(meters * .5f, left);
        auto asset = Assembly(prefix + std::to_string(meters));
        Place(result, Fit(asset, part), Add(a, d, cursor + part * .5f), std::atan2(d.y, d.x), part * .5f + 6,
              height + .025f);
        cursor += part;
    }
}
// Position fitting transforms normals by the inverse transpose of the local Jacobian.
// The source GLBs stay untouched; fitted joins are cached once per transport plan.
std::string Deform(const std::string &asset, const std::string &key,
                   const std::function<Vector2(Vector2)> &map)
{
    auto mesh = Source(Assembly(asset));
    for (auto &v : mesh.vertices)
    {
        Vector2 p = {v.x, v.y}, q = map(p);
        auto dx = Sub(map(Add(p, {1, 0}, .001f)), q), dy = Sub(map(Add(p, {0, 1}, .001f)), q);
        float determinant = Cross(dx, dy) / .000001f;
        if (std::fabs(determinant) > .00001f)
        {
            float nx = (dy.y * v.nx - dx.y * v.ny) / (.001f * determinant);
            float ny = (-dy.x * v.nx + dx.x * v.ny) / (.001f * determinant);
            float n = std::hypot(std::hypot(nx, ny), v.nz);
            v.nx = nx / n;
            v.ny = ny / n;
            v.nz /= n;
        }
        v.x = q.x;
        v.y = q.y;
    }
    return Register(std::move(mesh), key);
}
std::string JunctionArt(const Plan &plan, size_t index)
{
    std::string asset = plan.arms.size() == 4 ? "RoadPlan_X_2L" : "RoadPlan_T_2L";
    for (const auto &arm : plan.arms)
    {
        if (arm.lanes == 4)
        {
            asset = "RoadPlan_T_4L_2L";
        }
    }
    auto ports = roadArt.at(asset).ports;
    auto arms = plan.arms;
    auto angle = [](Vector2 p) { return std::atan2(p.y, p.x); };
    std::sort(ports.begin(), ports.end(),
              [&](auto a, auto b) { return angle(a.direction) < angle(b.direction); });
    std::sort(arms.begin(), arms.end(),
              [&](auto a, auto b) { return angle(a.direction) < angle(b.direction); });
    int selected = 0;
    float best = 1.e9f;
    for (int offset = 0; offset < int(arms.size()); ++offset)
    {
        float score = 0;
        Vector2 rotation = {};
        for (size_t i = 0; i < ports.size(); ++i)
        {
            const auto &arm = arms[(i + offset) % arms.size()];
            score += arm.lanes == ports[i].lanes ? 0 : 100;
            rotation.x += Dot(ports[i].direction, arm.direction);
            rotation.y += Cross(ports[i].direction, arm.direction);
        }
        score -= std::hypot(rotation.x, rotation.y);
        if (score < best)
        {
            best = score;
            selected = offset;
        }
    }
    // Corresponding port-edge corners form a cage. Each mouth keeps its exact lane width,
    // including skew regional T junctions; a single continuous mesh spans the centre.
    std::vector<std::pair<Vector2, Vector2>> cage;
    for (size_t i = 0; i < ports.size(); ++i)
    {
        const auto &p = ports[i];
        const auto &a = arms[(i + selected) % arms.size()];
        for (float side : {-1.f, 1.f})
        {
            cage.push_back({Add(p.position, RoadNetwork::RightOf(p.direction), side * p.lanes * .75f),
                            Add(Add({}, a.direction, plan.radius), RoadNetwork::RightOf(a.direction),
                                side * a.lanes * .75f)});
        }
    }
    std::sort(cage.begin(), cage.end(), [&](auto a, auto b) { return angle(a.first) < angle(b.first); });
    return Deform(asset, "RoadPlanJoin_" + std::to_string(index), [=](Vector2 p) {
        if (std::hypot(p.x, p.y) < .00001f)
        {
            return Vector2{};
        }
        for (size_t i = 0; i < cage.size(); ++i)
        {
            auto a = cage[i], b = cage[(i + 1) % cage.size()];
            float det = Cross(a.first, b.first);
            float u = Cross(p, b.first) / det, v = Cross(a.first, p) / det;
            if (u >= -.0001f && v >= -.0001f)
            {
                return Add(Add({}, a.second, u), b.second, v);
            }
        }
        return p;
    });
}
std::vector<TransitPlacement> Build()
{
    const auto &net = RoadNetwork::Get();
    std::vector<TransitPlacement> result;
    for (size_t index = 0; index < net.roads.size(); ++index)
    {
        const auto &road = net.roads[index];
        const auto &from = net.junctions[road.from], &to = net.junctions[road.to];
        auto direction = Unit(Sub(to.position, from.position));
        auto start = Add(from.position, direction, from.radius),
             end = Add(to.position, direction, -to.radius);
        if (std::fabs(from.altitude - to.altitude) > .1f)
        {
            // Both native 110m ramps rise from their local origin. Reverse the descending one.
            bool rising = to.altitude > from.altitude;
            auto lower = rising ? start : end;
            auto uphill = rising ? direction : Add({}, direction, -1);
            Place(result, Assembly("RoadPlan_Ramp_2L_Rise8p4_L110"), lower, std::atan2(uphill.y, uphill.x),
                  58, .025f);
        }
        else
        {
            std::string prefix = from.altitude > .1f
                                     ? "RoadPlan_Deck_2L_L"
                                     : "RoadPlan_Surface_" + std::to_string(road.lanes) + "L_L";
            RoadSpan(result, prefix, start, end, from.altitude);
        }
    }
    for (size_t index = 0; index < plans.size(); ++index)
    {
        const auto &plan = plans[index];
        const auto &junction = net.junctions[index];
        if (plan.radius < .001f || plan.arms.empty())
        {
            continue;
        }
        if (plan.arms.size() >= 3)
        {
            Place(result, JunctionArt(plan, index), junction.position, 0, plan.radius + 9,
                  junction.altitude + .025f);
        }
        else if (plan.arms.size() == 2)
        {
            auto a = Add({}, plan.arms[0].direction, plan.radius),
                 b = Add({}, plan.arms[1].direction, plan.radius);
            if (plan.arms[0].lanes != plan.arms[1].lanes)
            {
                auto wide = plan.arms[0].lanes == 4 ? plan.arms[0].direction : plan.arms[1].direction;
                auto narrow = plan.arms[0].lanes == 2 ? plan.arms[0].direction : plan.arms[1].direction;
                auto d = Add({}, wide, -1);
                if (Dot(wide, narrow) < -.9999f)
                {
                    Place(result, Assembly("RoadPlan_Transition_4L_2L"), junction.position,
                          std::atan2(d.y, d.x), 12, .025f);
                }
                else
                {
                    auto start = Add({}, wide, plan.radius), finish = Add({}, narrow, plan.radius);
                    auto asset =
                        Deform("RoadPlan_Transition_4L_2L",
                               "RoadPlanJoin_Transition_" + std::to_string(index), [=](Vector2 p) {
                                   float t = Game::Clamp((p.x + 6) / 12, 0, 1);
                                   auto center = Curve(start, finish, d, narrow, t);
                                   auto tangent = Unit(
                                       Sub(Curve(start, finish, d, narrow, (std::min)(1.f, t + .0005f)),
                                           Curve(start, finish, d, narrow, (std::max)(0.f, t - .0005f))));
                                   return Add(center, RoadNetwork::RightOf(tangent), p.y);
                               });
                    Place(result, asset, junction.position, 0, 15, .025f);
                }
            }
            else
            {
                auto da = Add({}, plan.arms[0].direction, -1), db = plan.arms[1].direction;
                // Bend the delivered R7 surface, markings and paving together along the same
                // cubic used by vehicle turns. Tangents and full-width ports meet the straights.
                auto asset = Deform(
                    "RoadPlan_Curve_2L_A90_R7", "RoadPlanBend_" + std::to_string(index), [=](Vector2 p) {
                        constexpr float radius = 3.5f, quarter = 1.57079632679f;
                        float theta = std::atan2(p.x, radius - p.y), t = Game::Clamp(theta / quarter, 0, 1);
                        float across = radius - std::hypot(p.x, p.y - radius);
                        auto center = Curve(a, b, da, db, t);
                        auto tangent = Unit(Sub(Curve(a, b, da, db, (std::min)(1.f, t + .0005f)),
                                                Curve(a, b, da, db, (std::max)(0.f, t - .0005f))));
                        return Add(center, RoadNetwork::RightOf(tangent), across);
                    });
                Place(result, asset, junction.position, 0, plan.radius + 9, .025f);
            }
        }
        else
        {
            auto end = Add(junction.position, plan.arms[0].direction, plan.radius);
            RoadSpan(result, "RoadPlan_Surface_" + std::to_string(plan.arms[0].lanes) + "L_L",
                     junction.position, end, 0);
            Place(result, "RoadPlan_EdgeCap_" + std::to_string(plan.arms[0].lanes) + "L", junction.position,
                  std::atan2(plan.arms[0].direction.y, plan.arms[0].direction.x), 5, .025f);
        }
    }
    // The 180m deck is eleven 16m spans plus one 4m closing span. Supports are separate,
    // placed only at stations clear of the ground street and its two pedestrian pavements.
    for (float y : {-41.f, -33.f, -5.f, 11.f, 27.f, 43.f})
    {
        Vector2 p = {48, y};
        if (!net.Contains(p, 2.2f))
        {
            Place(result, "RoadPlan_Pier_H8", p, 1.57079632679f, 6, .025f);
            supports.push_back({p, 1.57079632679f});
        }
    }
    for (const auto &road : net.roads)
    {
        for (int j : {road.from, road.to})
        {
            if (!net.IsSignalized(j))
            {
                continue;
            }
            auto d = Unit(
                Sub(net.junctions[j == road.from ? road.to : road.from].position, net.junctions[j].position));
            auto p = Add(net.junctions[j].position, d, net.junctions[j].radius + 1);
            Place(result, Paint("RoadPlan_CrosswalkPaint_" + std::to_string(road.lanes) + "L"), p,
                  std::atan2(d.y, d.x), road.lanes + 3, .025f);
        }
    }
    for (int route = 0; route < int(net.airCorridors.size()); ++route)
    {
        for (float t = -.15f; t < 1.15f;)
        {
            auto tangent = AirTraffic::Tangent(route, t);
            Place(result, "Transit_AirGuide_Straight_16m", AirTraffic::Position(route, t),
                  std::atan2(tangent.y, tangent.x), 7, net.airCorridors[route].altitude, true);
            t += 8.f / (std::max)(1.f, std::hypot(tangent.x, tangent.y));
        }
    }
    return result;
}
} // namespace
void TransitLayout::ConfigureJunctions(RoadNetwork &net)
{
    plans.assign(net.junctions.size(), {});
    for (size_t j = 0; j < net.junctions.size(); ++j)
    {
        auto &plan = plans[j];
        float closest = 1.e9f;
        int widest = 0;
        for (size_t i = 0; i < net.roads.size(); ++i)
        {
            const auto &r = net.roads[i];
            if (r.from != int(j) && r.to != int(j))
            {
                continue;
            }
            auto delta =
                Sub(net.junctions[r.from == int(j) ? r.to : r.from].position, net.junctions[j].position);
            closest = (std::min)(closest, std::hypot(delta.x, delta.y));
            widest = (std::max)(widest, r.lanes);
            plan.arms.push_back({Unit(delta), r.lanes, int(i)});
        }
        plan.radius = (std::min)(widest * RoadNetwork::kLaneWidth * .5f + 2, (closest - 8) * .5f);
        if (plan.arms.empty())
        {
            plan.radius = 0;
        }
        if (plan.arms.size() == 2 && Dot(plan.arms[0].direction, plan.arms[1].direction) < -.9999f &&
            plan.arms[0].lanes == plan.arms[1].lanes)
        {
            plan.radius = 0;
        }
        if (plan.arms.size() == 2 && plan.arms[0].lanes != plan.arms[1].lanes)
        {
            plan.radius = 6; // Native 24m width transition, twelve metres on either side.
        }
        net.junctions[j].radius = plan.radius;
        std::vector<Vector2> corners;
        for (const auto &arm : plan.arms)
        {
            auto n = RoadNetwork::RightOf(arm.direction);
            float half = arm.lanes * RoadNetwork::kLaneWidth * .5f;
            corners.push_back(Add(Add({}, arm.direction, plan.radius), n, half));
            corners.push_back(Add(Add({}, arm.direction, plan.radius), n, -half));
        }
        if (plan.arms.size() == 1)
        {
            auto d = plan.arms[0].direction, n = RoadNetwork::RightOf(d);
            float half = widest * RoadNetwork::kLaneWidth * .5f;
            corners.push_back(Add({}, n, half));
            corners.push_back(Add({}, n, -half));
        }
        if (plan.arms.size() != 2 && !plan.arms.empty())
        {
            float half = widest * RoadNetwork::kLaneWidth * .5f;
            for (float x : {-half, half})
            {
                for (float y : {-half, half})
                {
                    corners.push_back({x, y});
                }
            }
        }
        plan.polygon = Hull(corners);
    }
}
bool TransitLayout::ContainsJunction(int j, Vector2 position, float margin)
{
    const auto &net = RoadNetwork::Get();
    const auto &plan = plans[j];
    if (plan.radius < .001f)
    {
        return false;
    }
    auto p = Sub(position, net.junctions[j].position);
    if (std::hypot(p.x, p.y) > plan.radius + 9 + margin)
    {
        return false;
    }
    if (plan.arms.size() == 2)
    {
        auto a = Add({}, plan.arms[0].direction, plan.radius),
             b = Add({}, plan.arms[1].direction, plan.radius);
        Vector2 previous = a;
        for (int step = 1; step <= 24; ++step)
        {
            float t = step / 24.f;
            auto next = Curve(a, b, Add({}, plan.arms[0].direction, -1), plan.arms[1].direction, t);
            auto delta = Sub(next, previous);
            float u =
                Game::Clamp(Dot(Sub(p, previous), delta) / (std::max)(.00001f, Dot(delta, delta)), 0, 1);
            auto at = Add(previous, delta, u);
            float blend = t - (1 - u) / 24;
            if (plan.arms[0].lanes != plan.arms[1].lanes)
            {
                blend = blend * blend * (3 - 2 * blend); // Native 24m smooth width transition.
            }
            float half = (plan.arms[0].lanes + (plan.arms[1].lanes - plan.arms[0].lanes) * blend) *
                         RoadNetwork::kLaneWidth * .5f;
            if (Game::Distance(at, p) <= half + margin)
            {
                return true;
            }
            previous = next;
        }
        return false;
    }
    if (plan.polygon.size() < 3)
    {
        return false;
    }
    for (size_t i = 0; i < plan.polygon.size(); ++i)
    {
        auto a = plan.polygon[i], b = plan.polygon[(i + 1) % plan.polygon.size()];
        if (Cross(Sub(b, a), Sub(p, a)) < -margin * Game::Distance(a, b))
        {
            return false;
        }
    }
    return true;
}
const std::vector<TransitPlacement> &TransitLayout::Get()
{
    static const auto result = Build();
    return result;
}
bool TransitLayout::IsBlocked(Vector2 position, float radius, float altitude, bool driving)
{
    Get(); // Builds the support stations once, before any collision query.
    if (altitude < 3.8f)
    {
        // Two independent legs; the overhead crosshead never blocks the underpass.
        for (const auto &support : supports)
        {
            auto delta = Sub(position, support.position);
            Vector2 p = {Dot(delta, {std::cos(support.yaw), std::sin(support.yaw)}),
                         Dot(delta, {-std::sin(support.yaw), std::cos(support.yaw)})};
            if (std::fabs(p.x) < .35f + radius && std::fabs(p.y) > .57815f - radius &&
                std::fabs(p.y) < 1.25f + radius)
            {
                return true;
            }
        }
    }
    for (size_t i = 0; i < RoadNetwork::Get().roads.size(); ++i)
    {
        const auto &net = RoadNetwork::Get();
        const auto &r = net.roads[i];
        const auto &a = net.junctions[r.from], &b = net.junctions[r.to];
        if (a.altitude < .1f && b.altitude < .1f)
        {
            continue;
        }
        auto d = Unit(Sub(b.position, a.position)), start = Add(a.position, d, a.radius);
        float span = Game::Distance(a.position, b.position) - a.radius - b.radius;
        float along = Dot(Sub(position, start), d), across = std::fabs(Cross(Sub(position, start), d));
        if (along < 0 || along > span)
        {
            continue;
        }
        float top = net.RoadElevation(int(i), along / span), half = r.lanes * .75f;
        if (!driving && altitude < .1f && top > .08f && top < 1.3f && across < half + .2f + radius)
        {
            return true; // Low ramp undersides are not traversable pedestrian tunnels.
        }
        if (altitude >= .1f && std::fabs(altitude - top) < .5f && across > half - .05f - radius &&
            across < half + .35f + radius)
        {
            return true; // Native parapet, at the current driving level only.
        }
    }
    return false;
}
bool TransitLayout::OverlapsPavement(Vector2 minimum, Vector2 maximum, float clearance)
{
    struct Footprint
    {
        Vector2 lo, hi;
    };
    static const auto footprints = [] {
        std::vector<Footprint> result;
        for (const auto &m : Get())
        {
            if (m.air ||
                (m.asset.find("Walk") == std::string::npos && m.asset.find("RoadPlanAssembly_") != 0 &&
                 m.asset.find("RoadPlanJoin_") != 0 && m.asset.find("RoadPlanBend_") != 0))
            {
                continue;
            }
            const auto &mesh = Source(m.asset);
            float c = std::cos(m.yaw), s = std::sin(m.yaw);
            Vector2 lo = {1.e9f, 1.e9f}, hi = {-1.e9f, -1.e9f};
            for (float x : {mesh.minimum.x, mesh.maximum.x})
            {
                for (float y : {mesh.minimum.y, mesh.maximum.y})
                {
                    Vector2 p = {m.position.x + x * c - y * s, m.position.y + x * s + y * c};
                    lo.x = (std::min)(lo.x, p.x);
                    lo.y = (std::min)(lo.y, p.y);
                    hi.x = (std::max)(hi.x, p.x);
                    hi.y = (std::max)(hi.y, p.y);
                }
            }
            result.push_back({lo, hi});
        }
        return result;
    }();
    for (const auto &box : footprints)
    {
        if (minimum.x < box.hi.x + clearance && maximum.x > box.lo.x - clearance &&
            minimum.y < box.hi.y + clearance && maximum.y > box.lo.y - clearance)
        {
            return true;
        }
    }
    return false;
}

// ============================================================================
// CityPlan — multi-source sidewalk routing shared by placement and the renderer.
// ============================================================================
namespace
{
std::vector<BuildingConnection> connections;
std::vector<TransitPlacement> accessModules;
std::map<std::pair<int, int>, std::vector<std::pair<Vector2, Vector2>>> accessCells;
std::set<std::pair<int, int>> plotGround;
std::map<std::pair<int, int>, std::vector<TerrainPatch>> terrainChunks;
std::map<std::pair<int, int>, std::vector<Building>> frontageCells;
size_t plannedSignature = 0;
std::vector<Building> plannedBuildings;
bool ClearFrontage(const GameState &world, Vector2 p)
{
    if (RoadNetwork::Get().Contains(p, .20f) || TransitLayout::IsBlocked(p, .4f))
    {
        return false;
    }
    auto cell = frontageCells.find({int(std::floor(p.x / 8)), int(std::floor(p.y / 8))});
    if (cell == frontageCells.end())
    {
        return true;
    }
    for (const auto &b : cell->second)
    {
        if (p.x > b.x - .4f && p.x < b.x + b.width + .4f && p.y > b.y - .4f && p.y < b.y + b.depth + .4f)
        {
            return false;
        }
    }
    return true;
}
bool ClearFrontageSegment(const GameState &world, Vector2 a, Vector2 b)
{
    int samples = (std::max)(1, int(std::ceil(Game::Distance(a, b) * 5)));
    for (int i = 0; i <= samples; ++i)
    {
        if (!ClearFrontage(world, Add(a, Sub(b, a), float(i) / samples)))
        {
            return false;
        }
    }
    return true;
}
} // namespace
void CityPlan::Rebuild(GameState &world)
{
    size_t signature = world.buildings.size();
    for (const auto &b : world.buildings)
    {
        for (float value : {b.x, b.y, b.width, b.depth, b.height})
        {
            signature ^= std::hash<float>{}(value) + 0x9e3779b9 + (signature << 6) + (signature >> 2);
        }
    }
    for (const auto &b : world.buildings)
    {
        signature ^= size_t(b.type) + 0x9e3779b9 + (signature << 6) + (signature >> 2);
        for (const auto &colour : {b.palette.body, b.palette.accent, b.palette.emission})
        {
            for (float value : colour)
            {
                signature ^= std::hash<float>{}(value) + 0x9e3779b9 + (signature << 6) + (signature >> 2);
            }
        }
    }
    if (plannedSignature == signature && !plannedBuildings.empty())
    {
        world.buildings = plannedBuildings;
        return;
    }
    constexpr int size = 101, offset = 50;
    for (int pass = 0; pass < 2; ++pass)
    {
        connections.clear();
        accessModules.clear();
        accessCells.clear();
        plotGround.clear();
        terrainChunks.clear();
        frontageCells.clear();
        for (const auto &b : world.buildings)
        {
            for (int x = int(std::floor((b.x - .4f) / 8)); x <= int(std::floor((b.x + b.width + .4f) / 8));
                 ++x)
            {
                for (int y = int(std::floor((b.y - .4f) / 8));
                     y <= int(std::floor((b.y + b.depth + .4f) / 8)); ++y)
                {
                    frontageCells[{x, y}].push_back(b);
                }
            }
        }
        std::vector<bool> reached(world.buildings.size(), false);
        for (int city = 0; city < Overworld::kCityCount; ++city)
        {
            auto center = Overworld::Cities()[city].center;
            std::vector<int> parent(size * size, -2), distance(size * size, 0);
            std::queue<int> queue;
            auto position = [&](int index) {
                return Vector2{center.x + index % size - offset, center.y + index / size - offset};
            };
            for (int index = 0; index < size * size; ++index)
            {
                auto p = position(index);
                if (!ClearFrontage(world, p))
                {
                    continue;
                }
                parent[index] = -1;
                if (RoadNetwork::Get().Contains(p, 1.3f))
                {
                    parent[index] = index;
                    queue.push(index);
                }
            }
            while (!queue.empty())
            {
                int current = queue.front();
                queue.pop();
                auto p = position(current);
                for (auto d : {Vector2{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}})
                {
                    int x = current % size + int(d.x), y = current / size + int(d.y);
                    if (x < 0 || y < 0 || x >= size || y >= size)
                    {
                        continue;
                    }
                    int next = y * size + x;
                    if (parent[next] != -1 || !ClearFrontageSegment(world, p, position(next)))
                    {
                        continue;
                    }
                    parent[next] = current;
                    distance[next] = distance[current] + 1;
                    queue.push(next);
                }
            }
            for (size_t index = 0; index < world.buildings.size(); ++index)
            {
                const auto &b = world.buildings[index];
                Vector2 middle = {b.x + b.width * .5f, b.y + b.depth * .5f};
                if (Overworld::CityAt(middle) != city)
                {
                    continue;
                }
                bool home = std::fabs(b.x - kHomeBuildingPosition.x) < .01f &&
                            std::fabs(b.y - kHomeBuildingPosition.y) < .01f;
                std::vector<Vector2> entrances = home ? std::vector<Vector2>{kHomeDoor}
                                                      : std::vector<Vector2>{{middle.x, b.y + b.depth + .55f},
                                                                             {b.x - .55f, middle.y},
                                                                             {b.x + b.width + .55f, middle.y},
                                                                             {middle.x, b.y - .55f}};
                int best = -1;
                float cost = 1.e9f;
                Vector2 entrance = {};
                for (auto e : entrances)
                {
                    for (int dy = -2; dy <= 2; ++dy)
                    {
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            int x = int(std::floor(e.x - center.x)) + offset + dx,
                                y = int(std::floor(e.y - center.y)) + offset + dy;
                            if (x < 0 || y < 0 || x >= size || y >= size)
                            {
                                continue;
                            }
                            int cell = y * size + x;
                            if (parent[cell] < 0 || Game::Distance(e, position(cell)) > 2 ||
                                !ClearFrontageSegment(world, e, position(cell)))
                            {
                                continue;
                            }
                            float score = distance[cell] + Game::Distance(e, position(cell));
                            if (score < cost)
                            {
                                cost = score;
                                best = cell;
                                entrance = e;
                            }
                        }
                    }
                }
                if (best < 0)
                {
                    continue;
                }
                reached[index] = true;
                BuildingConnection connection;
                connection.building = index;
                connection.entrance = entrance;
                connection.path.push_back(entrance);
                for (int node = best;; node = parent[node])
                {
                    connection.path.push_back(position(node));
                    if (parent[node] == node)
                    {
                        break;
                    }
                }
                // Collapse collinear grid edges, preserving obstacle-tested corners.
                std::vector<Vector2> path;
                for (auto p : connection.path)
                {
                    while (path.size() > 1 && std::fabs(Cross(Sub(path.back(), path[path.size() - 2]),
                                                              Sub(p, path.back()))) < .0001f)
                    {
                        path.pop_back();
                    }
                    path.push_back(p);
                }
                connection.path = std::move(path);
                connections.push_back(std::move(connection));
            }
        }
        if (pass == 0)
        {
            std::vector<Building> accessible;
            for (size_t i = 0; i < world.buildings.size(); ++i)
            {
                if (reached[i] || world.buildings[i].type == models::BuildingType::Observatory ||
                    (world.buildings[i].x == kHomeBuildingPosition.x &&
                     world.buildings[i].y == kHomeBuildingPosition.y))
                {
                    accessible.push_back(world.buildings[i]);
                }
            }
            if (accessible.size() != world.buildings.size())
            {
                world.buildings.swap(accessible);
                continue;
            }
        }
        break;
    }
    plannedSignature = signature;
    plannedBuildings = world.buildings;
    std::set<std::string> edges;
    for (const auto &connection : connections)
    {
        for (size_t i = 1; i < connection.path.size(); ++i)
        {
            auto a = connection.path[i - 1], b = connection.path[i];
            auto name = [](Vector2 p) {
                return std::to_string(int(std::round(p.x * 1000))) + "," +
                       std::to_string(int(std::round(p.y * 1000)));
            };
            auto na = name(a), nb = name(b);
            std::string key = na < nb ? na + "/" + nb : nb + "/" + na;
            if (edges.insert(key).second)
            {
                Span(accessModules, "RoadPlan_Walk_W2_L4", a, b, .75f);
                for (int x = int(std::floor(((std::min)(a.x, b.x) - 1) / 8));
                     x <= int(std::floor(((std::max)(a.x, b.x) + 1) / 8)); ++x)
                {
                    for (int y = int(std::floor(((std::min)(a.y, b.y) - 1) / 8));
                         y <= int(std::floor(((std::max)(a.y, b.y) + 1) / 8)); ++y)
                    {
                        accessCells[{x, y}].push_back({a, b});
                    }
                }
            }
        }
    }
    for (const auto &b : world.buildings)
    {
        for (int y = int(std::floor(b.y - 1)); y <= int(std::ceil(b.y + b.depth + 1)); ++y)
        {
            for (int x = int(std::floor(b.x - 1)); x <= int(std::ceil(b.x + b.width + 1)); ++x)
            {
                plotGround.insert({x, y});
            }
        }
    }
}
const std::vector<BuildingConnection> &CityPlan::Connections()
{
    return connections;
}
const std::vector<TransitPlacement> &CityPlan::Walkways()
{
    return accessModules;
}
bool CityPlan::IsWalkway(Vector2 p)
{
    auto found = accessCells.find({int(std::floor(p.x / 8)), int(std::floor(p.y / 8))});
    if (found == accessCells.end())
    {
        return false;
    }
    for (auto segment : found->second)
    {
        auto delta = Sub(segment.second, segment.first);
        float t =
            Game::Clamp(Dot(Sub(p, segment.first), delta) / (std::max)(.00001f, Dot(delta, delta)), 0, 1);
        if (Game::Distance(p, Add(segment.first, delta, t)) < .5f)
        {
            return true;
        }
    }
    return false;
}
bool CityPlan::OverlapsAccess(Vector2 lo, Vector2 hi, float padding)
{
    for (const auto &c : connections)
    {
        for (size_t i = 1; i < c.path.size(); ++i)
        {
            auto a = c.path[i - 1], b = c.path[i];
            int steps = (std::max)(1, int(std::ceil(Game::Distance(a, b) * 3)));
            for (int step = 0; step <= steps; ++step)
            {
                auto p = Add(a, Sub(b, a), float(step) / steps);
                if (p.x > lo.x - padding - .4f && p.x < hi.x + padding + .4f && p.y > lo.y - padding - .4f &&
                    p.y < hi.y + padding + .4f)
                {
                    return true;
                }
            }
        }
    }
    return false;
}
const std::vector<TerrainPatch> &CityPlan::Terrain(int x, int y, int size)
{
    auto key = std::make_pair(x, y);
    auto found = terrainChunks.find(key);
    if (found != terrainChunks.end())
    {
        return found->second;
    }
    auto cover = [](int x, int y) {
        Vector2 p = {x + .5f, y + .5f};
        int city = Overworld::CityAt(p);
        if (plotGround.count({x, y}) || CityLayout::IsQuestSpace(p) || RoadNetwork::Get().Contains(p, 1.5f))
        {
            return DistrictGround::Pavement;
        }
        if (city >= 0)
        {
            auto center = Overworld::Cities()[city].center;
            auto local = Sub(p, center);
            if (city == 1 || local.y < -21 || std::fabs(local.x) > 32)
            {
                return DistrictGround::Stone;
            }
            return DistrictGround::Grass;
        }
        unsigned int patch =
            unsigned(int(std::floor(p.x / 14))) * 73856093u ^ unsigned(int(std::floor(p.y / 14))) * 19349663u;
        return patch % 7 < 4   ? DistrictGround::Grass
               : patch % 7 < 6 ? DistrictGround::Stone
                               : DistrictGround::Soil;
    };
    std::vector<TerrainPatch> result;
    for (int row = y; row < y + size && row < CityLayout::kTerrainMaxY; ++row)
    {
        for (int column = x; column < x + size && column < CityLayout::kTerrainMaxX;)
        {
            auto ground = cover(column, row);
            int end = column + 1;
            while (end < x + size && end < CityLayout::kTerrainMaxX && cover(end, row) == ground)
            {
                ++end;
            }
            result.push_back({{float(column), float(row)}, float(end - column), ground});
            column = end;
        }
    }
    return terrainChunks.emplace(key, std::move(result)).first->second;
}
const models::Model &CityPlan::Ground(DistrictGround ground)
{
    static auto groundModels = [] {
        std::array<std::unique_ptr<models::Model>, 4> result;
        const char *names[] = {"ground/concrete", "ground/grass", "ground/mine", "ground/concrete"};
        for (int i = 0; i < 4; ++i)
        {
            result[i] = std::make_unique<models::Model>();
            result[i]->ground = true;
            auto id = models::ResourceAssets::Find(names[i]);
            if (i == 0)
            {
                auto mesh = models::ResourceAssets::Get(id);
                mesh.name = "ground/city-pavement";
                for (auto &v : mesh.vertices)
                {
                    float light = (v.r + v.g + v.b) / 3;
                    v.r = .35f + light * .15f;
                    v.g = .40f + light * .15f;
                    v.b = .43f + light * .15f;
                    v.surface = float(models::Surface::Stone);
                }
                id = models::ResourceAssets::RegisterStatic(std::move(mesh));
            }
            result[i]->resourceId = id;
            result[i]->space = models::CoordinateSpace::World;
        }
        return result;
    }();
    return *groundModels[size_t(ground)];
}
} // namespace tutorial
