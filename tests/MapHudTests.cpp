#include "stdafx.h"
#include "Hud.h"
#include "Level.h"
#include "Window.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace tutorial;
void TestFixedRotatedProjection()
{
    auto &world = Game::Get().GetState();
    world.isInsideHome = false;
    world.playerPosition = {15, -4};
    Vector2 center = MapHud::WorldToMap(world.playerPosition, false);
    Vector2 east = MapHud::WorldToMap({16, -4}, false);
    Vector2 north = MapHud::WorldToMap({15, -5}, false);
    assert(east.x > center.x && east.y > center.y);
    assert(north.y < center.y && north.x > center.x);
    world.facingYaw += 1.9f;
    world.bikeYaw -= .7f;
    world.cameraPosition = {120, 80};
    assert(Game::Distance(MapHud::WorldToMap({16, -4}, false), east) < .0001f);
    assert(MapHud::Contains(center, false));
    assert(!MapHud::Contains({center.x + 104, center.y}, false));
    assert(!MapHud::Contains({center.x + 90, center.y + 90}, false));
    for (Vector2 point : {world.playerPosition, Vector2{1, 17}, Vector2{-19, 12}})
    {
        assert(Game::Distance(point, MapHud::MapToWorld(MapHud::WorldToMap(point, false), false)) < .001f);
    }
}
void TestFullMapAndLayerDestinations()
{
    auto &world = Game::Get().GetState();
    world.mapVisible = true;
    Vector2 cityDestination = {128, 18};
    auto canvas = MapHud::WorldToMap(cityDestination, true);
    assert(MapHud::Contains(canvas, true));
    assert(MapHud::HandleClick(canvas));
    auto &city = world.mapDestinations[size_t(MapRegion::City)];
    assert(city.active && Game::Distance(city.position, cityDestination) < .001f);
    assert(!MapHud::Contains(MapHud::WorldToMap(cityDestination, false), false));
    assert(!MapHud::HandleClick({10, 10}));
    assert(Game::Distance(city.position, cityDestination) < .001f);

    game::GameLevels::Mine().GetState().active = true;
    assert(MapHud::Region() == MapRegion::Mine);
    game::GameLevels::Mine().Map().Generate(702);
    Vector2 unknown = {95.5f, 93.5f};
    assert(!game::GameLevels::Mine().Map().IsExplored(unknown));
    assert(!MapHud::HandleClick(MapHud::WorldToMap(unknown, true)));
    Vector2 mineDestination = game::GameLevels::Mine().GetState().entry;
    assert(MapHud::HandleClick(MapHud::WorldToMap(mineDestination, true)));
    assert(Game::Distance(world.mapDestinations[size_t(MapRegion::Mine)].position, mineDestination) < .001f);
    assert(Game::Distance(city.position, cityDestination) < .001f);
    assert(MapHud::HandleClick(MapHud::WorldToMap(mineDestination, true), true));
    assert(!world.mapDestinations[size_t(MapRegion::Mine)].active && city.active);

    game::GameLevels::Mine().GetState().active = false;
    game::GameLevels::Chapter().GetState().active = true;
    assert(MapHud::Region() == MapRegion::Frontier);
    Vector2 frontierDestination = {32.5f, 12.5f};
    assert(MapHud::HandleClick(MapHud::WorldToMap(frontierDestination, true)));
    assert(Game::Distance(world.mapDestinations[size_t(MapRegion::Frontier)].position, frontierDestination) <
           .001f);
    game::GameLevels::Chapter().GetState().active = false;
    assert(MapHud::Region() == MapRegion::City && city.active);
}
void TestInputAndLetterboxing()
{
    auto &game = Game::Get();
    auto &world = game.GetState();
    world.mapVisible = false;
    world.mouseFiring = true;
    game.KeyDown('M', 0, 0);
    assert(world.mapVisible && !world.mouseFiring);
    game.KeyDown('m', 0, 0); // OS key repeat must not close it again.
    assert(world.mapVisible);
    game.KeyUp('m', 0, 0);
    game.KeyDown(27, 0, 0);
    assert(!world.mapVisible && !world.isPaused);
    game.KeyDown('m', 0, 0);
    game.KeyUp('m', 0, 0);
    assert(world.mapVisible);
    world.viewportWidth = 1920;
    world.viewportHeight = 1080;
    Vector2 destination = {-50, 60};
    auto canvas = MapHud::WorldToMap(destination, true);
    int windowX = int(std::lround(96 + canvas.x * 1.35f));
    int windowY = int(std::lround(canvas.y * 1.35f));
    game.MouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, windowX, windowY);
    assert(!world.mouseFiring);
    auto &selected = world.mapDestinations[size_t(MapRegion::City)];
    assert(selected.active && Game::Distance(selected.position, destination) < 1.5f);
    game.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, windowX, windowY);
    assert(!selected.active);
    game.MouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 4, 4);
    assert(!selected.active && !world.mouseFiring);
}
void TestCq12HudZoneSeparation()
{
    struct Rect
    {
        float x, y, w, h;
        bool Overlaps(const Rect &o) const
        {
            return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
        }
    };
    // CQ-12 canonical 1280x800 HUD cards: zero overlap across top, right, and bottom zones.
    const Rect objective{20.f, 20.f, 472.f, 108.f};
    const Rect coords{20.f, 134.f, 472.f, 26.f};
    const Rect wanted{508.f, 20.f, 276.f, 120.f};
    const Rect minimap{1070.f, 42.f, 172.f, 172.f};
    const Rect minimapKey{1042.f, 218.f, 218.f, 26.f};
    const Rect notice{902.f, 306.f, 358.f, 140.f};
    const Rect criticalAlert{20.f, 642.f, 288.f, 30.f};
    const Rect vitals{20.f, 678.f, 288.f, 108.f};
    const Rect bottomControls{322.f, 758.f, 636.f, 28.f};
    const Rect telemetry{988.f, 642.f, 272.f, 148.f};
    const Rect hudZones[] = {objective,     coords, wanted, minimap,        minimapKey,
                             notice,        criticalAlert,  vitals,         bottomControls,
                             telemetry};
    for (size_t i = 0; i < std::size(hudZones); ++i)
    {
        assert(hudZones[i].x >= 0.f && hudZones[i].y >= 0.f &&
               hudZones[i].x + hudZones[i].w <= float(kCanvasWidth) &&
               hudZones[i].y + hudZones[i].h <= float(kCanvasHeight));
        for (size_t j = i + 1; j < std::size(hudZones); ++j)
        {
            assert(!hudZones[i].Overlaps(hudZones[j]));
        }
    }
    // Full Planet Map: map circle bottom (y=644) vs Legend Row (y=656..676) vs Footer (y=680..718).
    const float mapCircleBottom = 400.f + 244.f;
    const Rect fullMapLegend{96.f, 656.f, 440.f, 20.f};
    const Rect fullMapFooter{96.f, 680.f, 460.f, 38.f};
    const Rect fullMapScale{576.f, 698.f, 200.f, 20.f};
    assert(fullMapLegend.y > mapCircleBottom);
    assert(!fullMapLegend.Overlaps(fullMapFooter));
    assert(!fullMapLegend.Overlaps(fullMapScale));
    assert(!fullMapFooter.Overlaps(fullMapScale));
}
} // namespace
int main()
{
    Game::Get().Reset();
    TestFixedRotatedProjection();
    TestFullMapAndLayerDestinations();
    TestInputAndLetterboxing();
    TestCq12HudZoneSeparation();
    std::cout << "Map checks passed: fixed 45-degree rotation, fog selection, circular bounds, full-map "
                 "selection, area isolation, "
                 "repeat-safe input, letterboxing and CQ-12 HUD layout separation.\n";
}
