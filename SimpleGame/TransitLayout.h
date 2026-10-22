#pragma once
#include "Game.h"
#include <string>
#include <vector>

namespace tutorial
{
class RoadNetwork;
struct TransitPlacement
{
    std::string asset;
    Vector2 position;
    float yaw = 0, altitude = 0, radius = 10;
    bool air = false;
};
// Cached native surfaces and fitted joins share their ports with traffic and pedestrian routing.
class TransitLayout
{
  public:
    static const std::vector<TransitPlacement> &Get();
    static void ConfigureJunctions(RoadNetwork &network);
    static bool ContainsJunction(int junction, Vector2 position, float margin);
    static bool OverlapsPavement(Vector2 minimum, Vector2 maximum, float clearance = .3f);
};
// ============================================================================
// CityPlan — connected building frontages, pedestrian alleys and district ground.
// ============================================================================
enum class DistrictGround
{
    Pavement,
    Grass,
    Stone,
    Soil
};
struct BuildingConnection
{
    size_t building = 0;
    Vector2 entrance = {};
    std::vector<Vector2> path;
};
struct TerrainPatch
{
    Vector2 position;
    float length;
    DistrictGround ground;
};
class CityPlan
{
  public:
    static void Rebuild(GameState &world);
    static const std::vector<BuildingConnection> &Connections();
    static const std::vector<TransitPlacement> &Walkways();
    static bool IsWalkway(Vector2 position);
    static bool OverlapsAccess(Vector2 minimum, Vector2 maximum, float padding = .3f);
    static const std::vector<TerrainPatch> &Terrain(int x, int y, int size);
    static const models::Model &Ground(DistrictGround ground);
};
} // namespace tutorial
