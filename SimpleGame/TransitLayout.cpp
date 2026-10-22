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
std::string RoadPrefix(int lanes)
{
    return "Transit_Road_" + std::to_string(lanes) + "L_";
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
models::ResourceVertex MaterialVertex(const std::string &asset)
{
    const auto &m = Source(asset);
    for (auto v : m.vertices)
    {
        if (v.nz > .9f && v.surface != 4)
        {
            return v;
        }
    }
    return m.vertices.front();
}
void Triangle(models::ResourceMesh &mesh, Vector2 a, Vector2 b, Vector2 c, float height,
              models::ResourceVertex material)
{
    material.z = height;
    material.nx = material.ny = 0;
    material.nz = 1;
    for (auto p : {a, b, c})
    {
        material.x = p.x;
        material.y = p.y;
        mesh.vertices.push_back(material);
    }
}
void Quad(models::ResourceMesh &mesh, Vector2 a, Vector2 b, Vector2 c, Vector2 d, float z,
          models::ResourceVertex material)
{
    Triangle(mesh, a, b, c, z, material);
    Triangle(mesh, a, c, d, z, material);
}
// Road blockout deliberately does not use the imported road modules. Geometry and ports are
// derived from the transport graph, so art cannot silently change the plan's lane count or height.
models::ResourceVertex DraftMaterial(float r, float g, float b)
{
    models::ResourceVertex v = {};
    v.nz = 1;
    v.r = r;
    v.g = g;
    v.b = b;
    v.a = 1;
    v.surface = 2;
    v.joints = {31, 31, 31, 31};
    v.weights = {1, 0, 0, 0};
    return v;
}
void SlopedQuad(models::ResourceMesh &mesh, Vector2 a, Vector2 b, Vector2 c, Vector2 d, float za, float zb,
                models::ResourceVertex material)
{
    size_t first = mesh.vertices.size();
    Quad(mesh, a, b, c, d, 0, material);
    float heights[] = {za, zb, zb, za, zb, za};
    for (size_t i = 0; i < 6; ++i)
    {
        mesh.vertices[first + i].z = heights[i];
    }
    // Cross product for each ramp triangle retains correct lighting, rather than a flat fake normal.
    auto &v = mesh.vertices;
    for (size_t i = first; i < v.size(); i += 3)
    {
        models::Point u = {v[i + 1].x - v[i].x, v[i + 1].y - v[i].y, v[i + 1].z - v[i].z};
        models::Point w = {v[i + 2].x - v[i].x, v[i + 2].y - v[i].y, v[i + 2].z - v[i].z};
        models::Point n = {u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x};
        float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        for (size_t k = 0; k < 3; ++k)
        {
            v[i + k].nx = n.x / length;
            v[i + k].ny = n.y / length;
            v[i + k].nz = n.z / length;
        }
    }
}
std::vector<TransitPlacement> Build()
{
    const auto &net = RoadNetwork::Get();
    std::vector<TransitPlacement> result;
    const auto asphalt = DraftMaterial(.105f, .145f, .17f);
    const auto pavement = DraftMaterial(.34f, .40f, .41f);
    const auto paint = DraftMaterial(.72f, .78f, .72f);
    auto save = [&](models::ResourceMesh mesh, std::string name, Vector2 position, float radius) {
        mesh.source = "procedural/road_plan_v2";
        Place(result, Register(std::move(mesh), name), position, 0, radius, 0);
    };
    for (size_t index = 0; index < net.roads.size(); ++index)
    {
        const auto &road = net.roads[index];
        const auto &from = net.junctions[road.from], &to = net.junctions[road.to];
        auto direction = Unit(Sub(to.position, from.position)), normal = RoadNetwork::RightOf(direction);
        auto start = Add(from.position, direction, from.radius),
             end = Add(to.position, direction, -to.radius);
        float length = Game::Distance(start, end), half = road.lanes * RoadNetwork::kLaneWidth * .5f;
        bool elevated = from.altitude > .1f || to.altitude > .1f;
        for (float cursor = 0; cursor < length - .001f; cursor += 8)
        {
            float finish = (std::min)(length, cursor + 8),
                  beginHeight = net.RoadElevation(int(index), cursor / length),
                  endHeight = net.RoadElevation(int(index), finish / length);
            auto center = Add(start, direction, (cursor + finish) * .5f);
            auto a = Add({}, direction, -(finish - cursor) * .5f),
                 b = Add({}, direction, (finish - cursor) * .5f);
            models::ResourceMesh mesh;
            auto strip = [&](float lo, float hi, float raise, models::ResourceVertex material) {
                SlopedQuad(mesh, Add(a, normal, lo), Add(b, normal, lo), Add(b, normal, hi),
                           Add(a, normal, hi), beginHeight + raise, endHeight + raise, material);
            };
            strip(-half, half, .025f, asphalt);
            if (!elevated)
            {
                strip(-half - 1, -half, .075f, pavement);
                strip(half, half + 1, .075f, pavement);
            }
            else
            {
                // Thin raised parapets show the deck's separate level; no false sidewalk on a highway.
                strip(-half - .20f, -half, .45f, pavement);
                strip(half, half + .20f, .45f, pavement);
            }
            for (float side : {-1.f, 1.f})
            {
                strip(side * half - .045f, side * half + .045f, .035f, paint);
            }
            strip(-.035f, .035f, .04f, DraftMaterial(.78f, .65f, .30f));
            for (int lane = 1; lane < road.lanes; ++lane)
            {
                if (lane == road.lanes / 2)
                {
                    continue;
                }
                float across = -half + lane * RoadNetwork::kLaneWidth;
                // Two-metre marks and four-metre gaps retain the same world phase across chunk seams.
                for (float mark = std::ceil(cursor / 3) * 3; mark < finish; mark += 3)
                {
                    float mEnd = (std::min)(finish, mark + 1);
                    auto ma = Add(start, direction, mark), mb = Add(start, direction, mEnd);
                    auto la = Sub(ma, center), lb = Sub(mb, center);
                    SlopedQuad(mesh, Add(la, normal, across - .025f), Add(lb, normal, across - .025f),
                               Add(lb, normal, across + .025f), Add(la, normal, across + .025f),
                               net.RoadElevation(int(index), mark / length) + .04f,
                               net.RoadElevation(int(index), mEnd / length) + .04f, paint);
                }
            }
            save(std::move(mesh), "DraftRoad_" + std::to_string(index) + "_" + std::to_string(int(cursor)),
                 center, finish - cursor + half + 2);
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
        models::ResourceMesh mesh;
        if (plan.arms.size() == 2)
        {
            auto a = Add({}, plan.arms[0].direction, plan.radius),
                 b = Add({}, plan.arms[1].direction, plan.radius);
            auto da = Add({}, plan.arms[0].direction, -1), db = plan.arms[1].direction;
            for (int piece = 0; piece < 32; ++piece)
            {
                float t0 = piece / 32.f, t1 = (piece + 1) / 32.f;
                auto p = Curve(a, b, da, db, t0), q = Curve(a, b, da, db, t1),
                     n = RoadNetwork::RightOf(Unit(Sub(q, p)));
                float h0 = (plan.arms[0].lanes + (plan.arms[1].lanes - plan.arms[0].lanes) * t0) * .75f;
                float h1 = (plan.arms[0].lanes + (plan.arms[1].lanes - plan.arms[0].lanes) * t1) * .75f;
                Quad(mesh, Add(p, n, -h0), Add(q, n, -h1), Add(q, n, h1), Add(p, n, h0),
                     junction.altitude + .025f, asphalt);
                if (junction.altitude < .1f)
                {
                    for (float side : {-1.f, 1.f})
                    {
                        Quad(mesh, Add(p, n, side * h0), Add(q, n, side * h1), Add(q, n, side * (h1 + 1)),
                             Add(p, n, side * (h0 + 1)), .075f, pavement);
                    }
                }
            }
        }
        else if (plan.polygon.size() >= 3)
        {
            for (size_t p = 1; p + 1 < plan.polygon.size(); ++p)
            {
                Triangle(mesh, plan.polygon[0], plan.polygon[p], plan.polygon[p + 1],
                         junction.altitude + .025f, asphalt);
            }
            if (junction.altitude < .1f)
            {
                for (size_t edge = 0; edge < plan.polygon.size(); ++edge)
                {
                    auto a = plan.polygon[edge], b = plan.polygon[(edge + 1) % plan.polygon.size()];
                    Vector2 middle = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
                    bool mouth = false;
                    for (const auto &arm : plan.arms)
                    {
                        mouth |= Dot(middle, arm.direction) > plan.radius - .1f;
                    }
                    if (mouth)
                    {
                        continue;
                    }
                    auto n = Add({}, RoadNetwork::RightOf(Unit(Sub(b, a))), -1);
                    Quad(mesh, a, b, Add(b, n), Add(a, n), .075f, pavement);
                    auto next = plan.polygon[(edge + 2) % plan.polygon.size()];
                    auto nn = Add({}, RoadNetwork::RightOf(Unit(Sub(next, b))), -1);
                    Triangle(mesh, b, Add(b, n), Add(b, nn), .075f, pavement);
                }
            }
        }
        else
        {
            continue;
        }
        save(std::move(mesh), "DraftJunction_" + std::to_string(index), junction.position, plan.radius + 8);
    }
    // New paint-only meshes preserve a single road surface, avoiding two overlapping asphalt slabs.
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
            Place(result, "Transit_CrosswalkPaint_" + std::to_string(road.lanes) + "L", p,
                  std::atan2(d.y, d.x), road.lanes + 3, .045f);
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
            float half =
                (plan.arms[0].lanes + (plan.arms[1].lanes - plan.arms[0].lanes) * (t - (1 - u) / 24)) *
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
            if (m.air || (m.asset.find("Walk") == std::string::npos && m.asset.find("DraftRoad_") != 0 &&
                          m.asset.find("DraftJunction_") != 0))
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
    if (RoadNetwork::Get().Contains(p, .20f))
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
                Span(accessModules, "Transit_Walk_W2_Straight_4m", a, b, .75f);
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
