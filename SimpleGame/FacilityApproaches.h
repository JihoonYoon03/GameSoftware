#pragma once
#include "CityFacilities.h"
#include "TransitLayout.h"
#include <map>
#include <utility>

namespace tutorial
{
using WalkwaySegment = std::pair<Vector2, Vector2>;
struct FacilityApproach
{
    InteractionTarget target = InteractionTarget::None;
    std::vector<Vector2> path; // Entrance/front pad to a connected sidewalk, never a nearby free point.
    unsigned visitedNodes = 0;
};
struct FacilityApproachPlan
{
    std::array<FacilityApproach, 4> facilities = {};
    std::vector<TransitPlacement> modules;
    std::map<std::pair<int, int>, std::vector<WalkwaySegment>> cells;
};

// Derived game-thread overlay. It is deliberately excluded from saved city-core generation/digests.
class FacilityApproaches
{
  public:
    static std::vector<Vector2> Connect(Vector2 entrance, const std::vector<WalkwaySegment> &sidewalks,
                                        const CityFacilities::ClearPoint &clear,
                                        unsigned *visitedNodes = nullptr);
    static FacilityApproachPlan Prepare(const GameState &world);
    static void Publish(FacilityApproachPlan plan) noexcept;
    static const FacilityApproachPlan &Get();
    static bool Connected(InteractionTarget target);
    static bool Contains(Vector2 position);
};
} // namespace tutorial
