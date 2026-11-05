#pragma once
#include "FacilityApproaches.h"
#include <limits>

namespace tutorial
{
struct SceneryProp;
struct InfrastructureRole
{
    const char *key, *label;
    std::array<models::SceneryType, 2> assets;
};
struct CityInfrastructureProfile
{
    const char *regionId, *purpose;
    std::array<InfrastructureRole, 3> roles;
};
enum class InfrastructureAccess
{
    Missing,
    Blocked,
    Connected
};
struct CityInfrastructureSite
{
    std::string id, asset;
    int city = 0;
    size_t sourceIndex = (std::numeric_limits<size_t>::max)();
    InfrastructureAccess access = InfrastructureAccess::Missing;
    Vector2 equipment = {}, standing = {}, minimum = {}, maximum = {};
    models::Point nativeSizeMeters = {};
    std::vector<Vector2> path;
    float pathLengthMeters = 0;
    unsigned attempts = 0, visitedNodes = 0;
};
struct CityInfrastructurePlan : CityWalkwayPlan
{
    bool prepared = false;
    std::array<CityInfrastructureSite, 15> sites = {};
    std::vector<int> moduleCities;
};
// Existing external equipment only. No invented door ports, interiors, stores or service interactions.
class CityInfrastructure
{
  public:
    static const std::array<CityInfrastructureProfile, 5> &Profiles();
    static const char *AccessName(InfrastructureAccess state);
    // Perimeter inspection pads from the delivered rotated footprint, not authored building entrances.
    static std::array<Vector2, 4> Pads(const SceneryProp &prop);
    static CityInfrastructurePlan Prepare(const GameState &world, const FacilityApproachPlan &facilities);
    static void Publish(CityInfrastructurePlan plan) noexcept;
    static const CityInfrastructurePlan &Get();
    static bool Contains(Vector2 position);
};
} // namespace tutorial
