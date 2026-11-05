#include "stdafx.h"
#include "FacilityApproaches.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
namespace
{
constexpr float kStep = .5f, kRadius = 16.f, kJoin = .75f, kProbe = .125f;
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
    return CityWalkways::Project(p, s);
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
    auto sidewalks = CityWalkways::BaseSegments();
    const auto clear = CityWalkways::StaticClearance(world);
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
        CityWalkways::Append(plan, approach.path, sidewalks);
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
    return CityWalkways::Contains(published, position);
}
} // namespace tutorial
