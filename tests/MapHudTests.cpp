#include "stdafx.h"
#include "Hud.h"
#include "Level.h"
#include "Dependencies/freeglut.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace tutorial;
void TestNorthUpProjection()
{
    auto &world = Game::Get().GetState();
    world.isInsideHome = false;
    world.playerPosition = {15, -4};
    Vector2 center = MapHud::WorldToMap(world.playerPosition, false);
    Vector2 east = MapHud::WorldToMap({16, -4}, false);
    Vector2 north = MapHud::WorldToMap({15, -5}, false);
    assert(east.x > center.x && east.y == center.y);
    assert(north.y < center.y && north.x == center.x);
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
    Vector2 mineDestination = {95.5f, 93.5f};
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
    game.MouseButton(GLUT_LEFT_BUTTON, GLUT_DOWN, windowX, windowY);
    assert(!world.mouseFiring);
    auto &selected = world.mapDestinations[size_t(MapRegion::City)];
    assert(selected.active && Game::Distance(selected.position, destination) < 1.5f);
    game.MouseButton(GLUT_RIGHT_BUTTON, GLUT_DOWN, windowX, windowY);
    assert(!selected.active);
    game.MouseButton(GLUT_LEFT_BUTTON, GLUT_DOWN, 4, 4);
    assert(!selected.active && !world.mouseFiring);
}
} // namespace
int main()
{
    Game::Get().Reset();
    TestNorthUpProjection();
    TestFullMapAndLayerDestinations();
    TestInputAndLetterboxing();
    std::cout << "Map checks passed: fixed north, circular bounds, full-map selection, area isolation, "
                 "repeat-safe input and letterboxing.\n";
}
