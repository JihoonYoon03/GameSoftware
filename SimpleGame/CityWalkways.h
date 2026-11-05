#pragma once
#include "CityFacilities.h"
#include "TransitLayout.h"
#include <map>
#include <utility>

namespace tutorial
{
using WalkwaySegment = std::pair<Vector2, Vector2>;
struct CityWalkwayPlan
{
    std::vector<TransitPlacement> modules;
    std::map<std::pair<int, int>, std::vector<WalkwaySegment>> cells;
};
// Shared derived access geometry. Never inserts into the authored city-core/digest input.
class CityWalkways
{
  public:
    static CityFacilities::ClearPoint StaticClearance(const GameState &world);
    static std::vector<WalkwaySegment> BaseSegments();
    static void Append(CityWalkwayPlan &plan, const std::vector<Vector2> &path,
                       std::vector<WalkwaySegment> &connected);
    static bool Contains(const CityWalkwayPlan &plan, Vector2 position);
    static Vector2 Project(Vector2 position, WalkwaySegment segment);
};
} // namespace tutorial
