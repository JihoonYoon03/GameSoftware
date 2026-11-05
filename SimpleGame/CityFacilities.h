#pragma once
#include "Game.h"
#include <array>
#include <functional>
#include <optional>

namespace tutorial
{
enum class FacilityRole
{
    Residence,
    Observation,
    Communications,
    Utilities
};
struct CityFacility
{
    const char *id, *label;
    FacilityRole role;
    InteractionTarget target;
    Vector2 exterior, arrival, exit, outward;
    bool hasInterior;
};

// Existing Halo facilities. Geometry/quest anchors are preserved; this is not a building generator.
class CityFacilities
{
  public:
    static const std::array<CityFacility, 4> &All();
    static const CityFacility *Find(InteractionTarget target);
    static const CityFacility *ExitAt(Vector2 exterior);
    static Vector2 StandingPoint(const CityFacility &facility);
    // Local city coordinates, 1 unit = 2 metres. Static reachability and live occupancy are separate.
    using ClearPoint = std::function<bool(Vector2)>;
    static std::optional<Vector2> FindExit(const CityFacility &facility, const ClearPoint &staticClear,
                                           const ClearPoint &available, unsigned *visitedNodes = nullptr);
};
} // namespace tutorial
