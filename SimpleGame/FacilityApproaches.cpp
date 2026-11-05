#include "stdafx.h"
#include "FacilityApproaches.h"
#include "Overworld.h"
#include "Profiler.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
namespace
{
constexpr float kStep = .5f, kRadius = 16.f, kJoin = .75f, kProbe = .125f, kClearance = .4f;
constexpr int kHalf = 32, kWidth = kHalf * 2 + 1, kCount = kWidth * kWidth;
using SegmentCells = std::map<std::pair<int, int>, std::vector<WalkwaySegment>>;
FacilityApproachPlan published;
bool Finite(Vector2 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}
std::pair<int, int> Cell(Vector2 p)
{
    return {int(std::floor(p.x / 4)), int(std::floor(p.y / 4))};
}
Vector2 Project(Vector2 p, const WalkwaySegment &s)
{
    const float x = s.second.x - s.first.x, y = s.second.y - s.first.y;
    const float t = Game::Clamp(
        ((p.x - s.first.x) * x + (p.y - s.first.y) * y) / (std::max)(.00001f, x * x + y * y), 0, 1);
    return {s.first.x + x * t, s.first.y + y * t};
}
void Index(SegmentCells &cells, WalkwaySegment segment, float margin)
{
    auto lo = Cell({(std::min)(segment.first.x, segment.second.x) - margin,
                    (std::min)(segment.first.y, segment.second.y) - margin});
    auto hi = Cell({(std::max)(segment.first.x, segment.second.x) + margin,
                    (std::max)(segment.first.y, segment.second.y) + margin});
    for (int x = lo.first; x <= hi.first; ++x)
    {
        for (int y = lo.second; y <= hi.second; ++y)
        {
            cells[{x, y}].push_back(segment);
        }
    }
}
bool Swept(Vector2 a, Vector2 b, const CityFacilities::ClearPoint &clear)
{
    const int count = (std::max)(1, int(std::ceil(Game::Distance(a, b) / kProbe)));
    for (int i = 0; i <= count; ++i)
    {
        const float t = float(i) / count;
        if (!clear({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}))
        {
            return false;
        }
    }
    return true;
}
void Simplify(std::vector<Vector2> &path)
{
    std::vector<Vector2> result;
    for (auto p : path)
    {
        if (!result.empty() && Game::Distance(p, result.back()) < .0001f)
        {
            continue;
        }
        while (result.size() > 1)
        {
            const auto a = result[result.size() - 2], b = result.back();
            const float x = b.x - a.x, y = b.y - a.y, dx = p.x - b.x, dy = p.y - b.y;
            if (std::fabs(x * dy - y * dx) > .0001f || x * dx + y * dy < 0)
            {
                break;
            }
            result.pop_back();
        }
        result.push_back(p);
    }
    path.swap(result);
}
} // namespace

std::vector<Vector2> FacilityApproaches::Connect(Vector2 entrance,
                                                 const std::vector<WalkwaySegment> &sidewalks,
                                                 const CityFacilities::ClearPoint &clear,
                                                 unsigned *visitedNodes)
{
    if (visitedNodes)
    {
        *visitedNodes = 0;
    }
    if (!Finite(entrance) || !clear || !clear(entrance))
    {
        return {};
    }
    SegmentCells targets;
    for (auto segment : sidewalks)
    {
        if (!Finite(segment.first) || !Finite(segment.second) ||
            Game::Distance(entrance, Project(entrance, segment)) > kRadius + kJoin)
        {
            continue;
        }
        // Clip indexing to the bounded search area, including unusually long imported segments.
        const Vector2 lo{entrance.x - kRadius - kJoin, entrance.y - kRadius - kJoin};
        const Vector2 hi{entrance.x + kRadius + kJoin, entrance.y + kRadius + kJoin};
        auto first = Cell({(std::max)(lo.x, (std::min)(segment.first.x, segment.second.x) - kJoin),
                           (std::max)(lo.y, (std::min)(segment.first.y, segment.second.y) - kJoin)});
        auto last = Cell({(std::min)(hi.x, (std::max)(segment.first.x, segment.second.x) + kJoin),
                          (std::min)(hi.y, (std::max)(segment.first.y, segment.second.y) + kJoin)});
        for (int x = first.first; x <= last.first; ++x)
        {
            for (int y = first.second; y <= last.second; ++y)
            {
                targets[{x, y}].push_back(segment);
            }
        }
    }
    if (targets.empty())
    {
        return {};
    }
    // Heap-backed bounded scratch: never allocate a large navigation grid on the Windows stack.
    std::vector<int> parent(kCount, -1), queue;
    queue.reserve(kCount);
    constexpr int start = kHalf * kWidth + kHalf;
    parent[start] = start;
    queue.push_back(start);
    auto position = [&](int index) {
        return Vector2{entrance.x + (index % kWidth - kHalf) * kStep,
                       entrance.y + (index / kWidth - kHalf) * kStep};
    };
    constexpr std::array<std::pair<int, int>, 4> neighbours = {{{0, 1}, {1, 0}, {-1, 0}, {0, -1}}};
    for (size_t head = 0; head < queue.size(); ++head)
    {
        const int node = queue[head];
        if (visitedNodes)
        {
            ++*visitedNodes;
        }
        const auto p = position(node);
        auto candidates = targets.find(Cell(p));
        std::optional<Vector2> join;
        float best = kJoin + .0001f;
        if (candidates != targets.end())
        {
            for (const auto &segment : candidates->second)
            {
                const auto q = Project(p, segment);
                const float distance = Game::Distance(p, q);
                if (distance < best && Game::Distance(entrance, q) <= kRadius && Swept(p, q, clear))
                {
                    best = distance;
                    join = q;
                }
            }
        }
        if (join)
        {
            std::vector<Vector2> path{*join};
            for (int at = node;; at = parent[at])
            {
                path.push_back(position(at));
                if (at == start)
                {
                    break;
                }
            }
            std::reverse(path.begin(), path.end());
            Simplify(path);
            return path;
        }
        for (const auto &delta : neighbours)
        {
            const int x = node % kWidth + delta.first, y = node / kWidth + delta.second;
            if (x < 0 || x >= kWidth || y < 0 || y >= kWidth)
            {
                continue;
            }
            const int next = y * kWidth + x;
            const auto q = position(next);
            if (parent[next] >= 0 || Game::Distance(entrance, q) > kRadius || !Swept(p, q, clear))
            {
                continue;
            }
            parent[next] = node;
            queue.push_back(next);
        }
    }
    return {};
}

FacilityApproachPlan FacilityApproaches::Prepare(const GameState &world)
{
    profiling::Scope timer(profiling::Timer::FacilityApproachPrepare);
    FacilityApproachPlan plan;
    std::vector<WalkwaySegment> sidewalks;
    for (const auto &connection : CityPlan::Connections())
    {
        for (size_t i = 1; i < connection.path.size(); ++i)
        {
            sidewalks.push_back({connection.path[i - 1], connection.path[i]});
        }
    }
    struct Bounds
    {
        Vector2 lo, hi;
    };
    std::vector<Bounds> obstacles;
    for (const auto &b : world.buildings)
    {
        obstacles.push_back({{b.x, b.y}, {b.x + b.width, b.y + b.depth}});
    }
    const auto &library = models::ModelLibrary::Get();
    for (const auto &device : {std::make_pair(kBeaconPosition, &library.beacon),
                               std::make_pair(kCityReceiverPosition, &library.console),
                               std::make_pair(kRelayPosition, &library.breaker)})
    {
        const auto &mesh = models::ResourceAssets::Get(device.second->resourceId);
        obstacles.push_back({{device.first.x + mesh.minimum.x, device.first.y + mesh.minimum.y},
                             {device.first.x + mesh.maximum.x, device.first.y + mesh.maximum.y}});
    }
    auto clear = [&](Vector2 p) {
        if (RoadNetwork::Get().Contains(p, kClearance) || TransitLayout::IsBlocked(p, kClearance) ||
            Overworld::Scenery().IsBlocked(p, kClearance))
        {
            return false;
        }
        for (const auto &b : obstacles)
        {
            if (p.x > b.lo.x - kClearance && p.x < b.hi.x + kClearance && p.y > b.lo.y - kClearance &&
                p.y < b.hi.y + kClearance)
            {
                return false;
            }
        }
        return true;
    };
    for (size_t i = 0; i < plan.facilities.size(); ++i)
    {
        const auto &facility = CityFacilities::All()[i];
        auto &approach = plan.facilities[i];
        approach.target = facility.target;
        const auto entrance = facility.role == FacilityRole::Residence
                                  ? facility.exterior
                                  : CityFacilities::StandingPoint(facility);
        approach.path = Connect(entrance, sidewalks, clear, &approach.visitedNodes);
        profiling::Count(profiling::Counter::FacilityApproachVisitedNodes, approach.visitedNodes);
        profiling::Count(approach.path.empty() ? profiling::Counter::FacilityApproachFailures
                                               : profiling::Counter::FacilityApproachConnections);
        for (size_t j = 1; j < approach.path.size(); ++j)
        {
            const WalkwaySegment edge{approach.path[j - 1], approach.path[j]};
            const bool covered = std::any_of(sidewalks.begin(), sidewalks.end(), [&](const auto &s) {
                return Game::Distance(edge.first, Project(edge.first, s)) < .001f &&
                       Game::Distance(edge.second, Project(edge.second, s)) < .001f;
            });
            if (!covered)
            {
                auto modules = CityPlan::FitWalkway({edge.first, edge.second});
                for (auto &module : modules)
                {
                    // Separate overlay joins from the existing coplanar sidewalk surface.
                    module.altitude += .003f;
                }
                plan.modules.insert(plan.modules.end(), modules.begin(), modules.end());
            }
            Index(plan.cells, edge, .5f);
            // Later facilities can share a proven connection instead of drawing overlapping full routes.
            sidewalks.push_back(edge);
        }
        // A pad already on a sidewalk has no new span, but remains a connected facility.
        if (approach.path.size() == 1)
        {
            Index(plan.cells, {entrance, entrance}, .5f);
        }
    }
    return plan;
}
void FacilityApproaches::Publish(FacilityApproachPlan plan) noexcept
{
    published = std::move(plan);
}
const FacilityApproachPlan &FacilityApproaches::Get()
{
    return published;
}
bool FacilityApproaches::Connected(InteractionTarget target)
{
    for (const auto &facility : published.facilities)
    {
        if (facility.target == target)
        {
            return !facility.path.empty();
        }
    }
    return false;
}
bool FacilityApproaches::Contains(Vector2 position)
{
    const auto found = published.cells.find(Cell(position));
    if (found != published.cells.end())
    {
        for (const auto &segment : found->second)
        {
            if (Game::Distance(position, Project(position, segment)) < .5f)
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace tutorial
