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
// Native-size modules share their port geometry with traffic lane endpoints and pedestrian routing.
class TransitLayout
{
  public:
    static const std::vector<TransitPlacement> &Get();
    static void ConfigureJunctions(RoadNetwork &network);
    static bool ContainsJunction(int junction, Vector2 position, float margin);
    static bool OverlapsPavement(Vector2 minimum, Vector2 maximum, float clearance = .3f);
};
} // namespace tutorial
