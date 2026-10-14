#include "stdafx.h"
#include "LevelOne.h"
#include "RenderCache.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>

namespace levelone
{
namespace
{
constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
void Carve(int x, int y)
{
    if (x > 0 && y > 0 && x < kMapSize - 1 && y < kMapSize - 1)
    {
        g_level.tiles[y * kMapSize + x] = Tile::Floor;
    }
}
void Room(int left, int top, int right, int bottom)
{
    for (int y = top; y <= bottom; ++y)
    {
        for (int x = left; x <= right; ++x)
        {
            Carve(x, y);
        }
    }
}
void Tunnel(int x, int y, int targetX, int targetY)
{
    for (;;)
    {
        Room(x - 1, y - 1, x + 1, y + 1);
        if (x == targetX && y == targetY)
        {
            break;
        }
        if (x != targetX)
        {
            x += x < targetX ? 1 : -1;
        }
        else
        {
            y += y < targetY ? 1 : -1;
        }
    }
}
std::vector<int> Distances(tutorial::Vector2 destination, bool ignoreGate = false)
{
    std::vector<int> distances(kMapSize * kMapSize, -1);
    int x = int(std::floor(destination.x)), y = int(std::floor(destination.y));
    if (!IsWalkable(x, y))
    {
        return distances;
    }
    std::queue<int> frontier;
    frontier.push(y * kMapSize + x);
    distances[y * kMapSize + x] = 0;
    while (!frontier.empty())
    {
        int cell = frontier.front();
        frontier.pop();
        for (const auto &direction : kDirections)
        {
            int nx = cell % kMapSize + direction[0], ny = cell / kMapSize + direction[1];
            if (nx < 0 || nx >= kMapSize || ny < 0 || ny >= kMapSize)
            {
                continue;
            }
            int next = ny * kMapSize + nx;
            bool walkable = ignoreGate ? g_level.tiles[next] != Tile::Wall : IsWalkable(nx, ny);
            if (walkable && distances[next] < 0)
            {
                distances[next] = distances[cell] + 1;
                frontier.push(next);
            }
        }
    }
    return distances;
}
} // namespace

bool IsGateCell(int x, int y)
{
    return x == 32 && y >= 28 && y <= 30;
}
bool IsWalkable(int x, int y)
{
    return x >= 0 && y >= 0 && x < kMapSize && y < kMapSize && g_level.tiles.size() == kMapSize * kMapSize &&
           g_level.tiles[y * kMapSize + x] != Tile::Wall &&
           (!IsGateCell(x, y) || g_level.quest.IsPowerBypassed());
}
bool IsBlocked(tutorial::Vector2 position)
{
    profiling::Count(profiling::Counter::CollisionQueries);
    constexpr float radius = .18f;
    for (float dx : {-radius, radius})
    {
        for (float dy : {-radius, radius})
        {
            if (!IsWalkable(int(std::floor(position.x + dx)), int(std::floor(position.y + dy))))
            {
                return true;
            }
        }
    }
    return false;
}
bool HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to)
{
    profiling::Count(profiling::Counter::LineOfSightQueries);
    int steps = (std::max)(1, int(tutorial::Distance(from, to) / .12f));
    for (int i = 0; i <= steps; ++i)
    {
        float t = float(i) / steps;
        if (!IsWalkable(int(std::floor(from.x + (to.x - from.x) * t)),
                        int(std::floor(from.y + (to.y - from.y) * t))))
        {
            return false;
        }
    }
    return true;
}
std::vector<tutorial::Vector2> FindRoute(tutorial::Vector2 from, tutorial::Vector2 to)
{
    profiling::Scope timer(profiling::Timer::NpcNavigation);
    profiling::Count(profiling::Counter::NpcPathRequests);
    auto distances = Distances(to);
    std::vector<tutorial::Vector2> route;
    int x = int(std::floor(from.x)), y = int(std::floor(from.y));
    if (!IsWalkable(x, y) || distances[y * kMapSize + x] < 0)
    {
        return route;
    }
    // First reach the current cell center; turns then remain clear of wall corners.
    route.push_back({x + .5f, y + .5f});
    while (distances[y * kMapSize + x] > 0)
    {
        int current = distances[y * kMapSize + x];
        for (const auto &direction : kDirections)
        {
            int nx = x + direction[0], ny = y + direction[1];
            if (IsWalkable(nx, ny) && distances[ny * kMapSize + nx] == current - 1)
            {
                x = nx;
                y = ny;
                route.push_back({x + .5f, y + .5f});
                profiling::Count(profiling::Counter::NpcPathVisitedCells);
                break;
            }
        }
    }
    return route;
}
void UpdateNavigation()
{
    profiling::Scope timer(profiling::Timer::Navigation);
    g_level.navigationDistances = Distances(tutorial::g_state.playerPosition);
}

void GenerateMap(std::uint32_t seed)
{
    profiling::Scope timer(profiling::Timer::MapGeneration);
    rendercache::InvalidateTerrain();
    g_level.seed = seed;
    g_level.tiles.assign(kMapSize * kMapSize, Tile::Wall);
    // Authored mine topology: branching drifts, one closed bulkhead, deeper ruins.
    // No four-team test arena or randomly scattered repair facilities remain.
    Room(5, 5, 13, 13);
    Tunnel(8, 10, 24, 10);
    Tunnel(24, 10, 24, 29);
    Room(22, 22, 29, 31);
    Tunnel(24, 29, 48, 29);
    Tunnel(48, 29, 48, 47);
    Room(42, 42, 55, 54);
    Room(13, 17, 21, 24);
    Tunnel(17, 20, 24, 20);
    Tunnel(12, 20, 12, 43);
    Tunnel(12, 20, 17, 20);
    Room(7, 40, 16, 47);
    Room(40, 11, 47, 18);
    Tunnel(44, 15, 44, 29);
    Room(49, 33, 57, 40);
    Tunnel(48, 36, 54, 36);
    // Wall x=32 can only be crossed at the powered bulkhead.
    for (int y = 1; y < kMapSize - 1; ++y)
    {
        if (!IsGateCell(32, y))
        {
            g_level.tiles[y * kMapSize + 32] = Tile::Wall;
        }
    }
    auto connected = Distances(g_level.entry, true);
    g_level.reachableCells.clear();
    std::mt19937 random(seed);
    for (int cell = 0; cell < kMapSize * kMapSize; ++cell)
    {
        if (connected[cell] < 0)
        {
            g_level.tiles[cell] = Tile::Wall;
        }
        else
        {
            g_level.reachableCells.push_back(cell);
        }
    }
    g_level.lights = {{g_level.entry, 4.8f},     {{17.5f, 10.5f}, 2.8f}, {{17.5f, 20.5f}, 3.2f},
                      {{11.5f, 43.5f}, 3.2f},    {kPowerBypass, 3.5f},   {{32.5f, 29.5f}, 4.f, true},
                      {{44.5f, 15.5f}, 3.2f},    {{53.5f, 36.5f}, 3.2f}, {kRuinSignal, 3.8f},
                      {kCoordinateTerminal, 3.f}};
    g_level.rescueGroups = {
        {{17.5f, 20.5f}, false}, {{11.5f, 43.5f}, true}, {{44.5f, 15.5f}, false}, {{53.5f, 36.5f}, true}};
    g_level.survivors.clear();
    std::uint64_t id = 1;
    for (size_t i = 0; i < g_level.rescueGroups.size(); ++i)
    {
        auto center = g_level.rescueGroups[i].position;
        for (int member = 0; member < 3; ++member)
        {
            g_level.survivors.push_back({id++, i, {center.x + (member - 1) * .6f, center.y + .5f}});
        }
    }
    std::shuffle(g_level.reachableCells.begin(), g_level.reachableCells.end(), random);
    g_level.enemies.clear();
    for (int cell : g_level.reachableCells)
    {
        tutorial::Vector2 position = {cell % kMapSize + .5f, cell / kMapSize + .5f};
        if (tutorial::Distance(position, g_level.entry) < kEnemySpawnRadius ||
            tutorial::Distance(position, kPowerBypass) < 2 || IsGateCell(cell % kMapSize, cell / kMapSize))
        {
            continue;
        }
        g_level.enemies.push_back({position});
        if (g_level.enemies.size() == kEnemyPopulation)
        {
            break;
        }
    }
    g_level.drops.clear();
    g_level.bullets.clear();
    g_level.tracers.clear();
    g_level.scannerContacts.clear();
    g_level.riaPosition = {9.5f, 10.5f};
    g_level.riaRoute.clear();
    g_level.riaRouteIndex = 0;
    UpdateNavigation();
    g_level.generated = true;
}
} // namespace levelone
