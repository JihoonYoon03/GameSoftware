#pragma once
#include "CityWalkways.h"

namespace tutorial
{
struct FacilityApproach
{
    InteractionTarget target = InteractionTarget::None;
    std::vector<Vector2> path; // Entrance/front pad to a connected sidewalk, never a nearby free point.
    unsigned visitedNodes = 0;
};
struct FacilityApproachPlan : CityWalkwayPlan
{
    std::array<FacilityApproach, 4> facilities = {};
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
