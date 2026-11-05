#include "stdafx.h"
#include "CityWalkways.h"
#include "Overworld.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
namespace
{
constexpr float kClearance = .4f;
std::pair<int, int> Cell(Vector2 p)
{
    return {int(std::floor(p.x / 4)), int(std::floor(p.y / 4))};
}
void Index(CityWalkwayPlan &plan, WalkwaySegment segment)
{
    const auto lo = Cell({(std::min)(segment.first.x, segment.second.x) - .5f,
                          (std::min)(segment.first.y, segment.second.y) - .5f});
    const auto hi = Cell({(std::max)(segment.first.x, segment.second.x) + .5f,
                          (std::max)(segment.first.y, segment.second.y) + .5f});
    for (int x = lo.first; x <= hi.first; ++x)
    {
        for (int y = lo.second; y <= hi.second; ++y)
        {
            plan.cells[{x, y}].push_back(segment);
        }
    }
}
} // namespace
Vector2 CityWalkways::Project(Vector2 p, WalkwaySegment segment)
{
    const float x = segment.second.x - segment.first.x, y = segment.second.y - segment.first.y;
    const float t = Game::Clamp(((p.x - segment.first.x) * x + (p.y - segment.first.y) * y) /
                                    (std::max)(.00001f, x * x + y * y),
                                0, 1);
    return {segment.first.x + x * t, segment.first.y + y * t};
}
CityFacilities::ClearPoint CityWalkways::StaticClearance(const GameState &world)
{
    struct Bounds
    {
        Vector2 lo, hi;
    };
    std::map<std::pair<int, int>, std::vector<Bounds>> obstacles;
    auto add = [&](Bounds bounds) {
        const auto lo = Cell({bounds.lo.x - kClearance, bounds.lo.y - kClearance});
        const auto hi = Cell({bounds.hi.x + kClearance, bounds.hi.y + kClearance});
        for (int x = lo.first; x <= hi.first; ++x)
        {
            for (int y = lo.second; y <= hi.second; ++y)
            {
                obstacles[{x, y}].push_back(bounds);
            }
        }
    };
    for (const auto &b : world.buildings)
    {
        add({{b.x, b.y}, {b.x + b.width, b.y + b.depth}});
    }
    const auto &library = models::ModelLibrary::Get();
    for (const auto &device : {std::make_pair(kBeaconPosition, &library.beacon),
                               std::make_pair(kCityReceiverPosition, &library.console),
                               std::make_pair(kRelayPosition, &library.breaker)})
    {
        const auto &mesh = models::ResourceAssets::Get(device.second->resourceId);
        add({{device.first.x + mesh.minimum.x, device.first.y + mesh.minimum.y},
             {device.first.x + mesh.maximum.x, device.first.y + mesh.maximum.y}});
    }
    // Own the candidate world's bounds; never capture a temporary or the current surface observer frame.
    return [obstacles = std::move(obstacles)](Vector2 p) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || RoadNetwork::Get().Contains(p, kClearance) ||
            TransitLayout::IsBlocked(p, kClearance) || Overworld::Scenery().IsBlocked(p, kClearance))
        {
            return false;
        }
        const auto found = obstacles.find(Cell(p));
        if (found == obstacles.end())
        {
            return true;
        }
        for (const auto &b : found->second)
        {
            if (p.x > b.lo.x - kClearance && p.x < b.hi.x + kClearance && p.y > b.lo.y - kClearance &&
                p.y < b.hi.y + kClearance)
            {
                return false;
            }
        }
        return true;
    };
}
std::vector<WalkwaySegment> CityWalkways::BaseSegments()
{
    std::vector<WalkwaySegment> segments;
    for (const auto &connection : CityPlan::Connections())
    {
        for (size_t i = 1; i < connection.path.size(); ++i)
        {
            segments.push_back({connection.path[i - 1], connection.path[i]});
        }
    }
    return segments;
}
void CityWalkways::Append(CityWalkwayPlan &plan, const std::vector<Vector2> &path,
                          std::vector<WalkwaySegment> &connected)
{
    for (size_t i = 1; i < path.size(); ++i)
    {
        const WalkwaySegment edge{path[i - 1], path[i]};
        const bool covered = std::any_of(connected.begin(), connected.end(), [&](const auto &s) {
            return Game::Distance(edge.first, Project(edge.first, s)) < .001f &&
                   Game::Distance(edge.second, Project(edge.second, s)) < .001f;
        });
        if (!covered)
        {
            auto modules = CityPlan::FitWalkway({edge.first, edge.second});
            for (auto &module : modules)
            {
                module.altitude += .003f; // Separate joins from the existing coplanar sidewalk surface.
            }
            plan.modules.insert(plan.modules.end(), modules.begin(), modules.end());
        }
        Index(plan, edge);
        connected.push_back(edge);
    }
    if (path.size() == 1)
    {
        Index(plan, {path.front(), path.front()});
    }
}
bool CityWalkways::Contains(const CityWalkwayPlan &plan, Vector2 position)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
    {
        return false;
    }
    const auto found = plan.cells.find(Cell(position));
    if (found != plan.cells.end())
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
