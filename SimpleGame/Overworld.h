#pragma once
#include "Game.h"
#include <array>

namespace tutorial
{
struct CityInfo
{
    const char *id;
    const char *name;
    Vector2 center;
    const char *mood;
    const char *rumor;
};

// Connected outdoor world. Bike travel never changes the active scene or map.
class Overworld
{
  public:
    static constexpr int kCityCount = 5;
    static constexpr int kPopulationPerCity = 20;
    static constexpr int kKainResidentSlot = kPopulationPerCity - 3;
    static constexpr float kCitySpacing = 256;
    static constexpr float kBikeCruiseSpeed = kCitySpacing / 20.f;
    static constexpr Vector2 kMineEntrance = {128, 18};
    static const std::array<CityInfo, kCityCount> &Cities();
    static int CityAt(Vector2 position);
    static bool IsRoad(int x, int y);
    static void Populate(GameState &state);
    static void UpdateBike(GameState &state, float deltaSeconds);
    static void ToggleBike(GameState &state);
    static models::Palette BikePalette();
};
} // namespace tutorial
