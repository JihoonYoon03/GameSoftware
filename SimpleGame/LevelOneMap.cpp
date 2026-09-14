#include "stdafx.h"
#include "LevelOne.h"
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
} // namespace

bool IsWalkable(int x, int y)
{
    return x >= 0 && y >= 0 && x < kMapSize && y < kMapSize && g_level.tiles.size() == kMapSize * kMapSize &&
           g_level.tiles[y * kMapSize + x] != Tile::Wall;
}

bool IsBlocked(tutorial::Vector2 position)
{
    // Match the player's footprint, not just its center, against map collision.
    constexpr float radius = .18f;
    for (float offsetX : {-radius, radius})
    {
        for (float offsetY : {-radius, radius})
        {
            if (!IsWalkable(int(std::floor(position.x + offsetX)), int(std::floor(position.y + offsetY))))
            {
                return true;
            }
        }
    }
    return false;
}

bool HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to)
{
    const int steps = (std::max)(1, int(tutorial::Distance(from, to) / .12f));
    for (int step = 0; step <= steps; ++step)
    {
        float fraction = float(step) / steps;
        if (IsBlocked({from.x + (to.x - from.x) * fraction, from.y + (to.y - from.y) * fraction}))
        {
            return false;
        }
    }
    return true;
}

void UpdateNavigation()
{
    g_level.navigationDistances.assign(kMapSize * kMapSize, -1);
    int x = int(std::floor(tutorial::g_state.playerPosition.x));
    int y = int(std::floor(tutorial::g_state.playerPosition.y));
    if (!IsWalkable(x, y))
    {
        return;
    }
    std::queue<int> frontier;
    int start = y * kMapSize + x;
    frontier.push(start);
    g_level.navigationDistances[start] = 0;
    while (!frontier.empty())
    {
        int cell = frontier.front();
        frontier.pop();
        for (const auto &direction : kDirections)
        {
            int nextX = cell % kMapSize + direction[0];
            int nextY = cell / kMapSize + direction[1];
            if (!IsWalkable(nextX, nextY))
            {
                continue;
            }
            int next = nextY * kMapSize + nextX;
            if (g_level.navigationDistances[next] >= 0)
            {
                continue;
            }
            g_level.navigationDistances[next] = g_level.navigationDistances[cell] + 1;
            frontier.push(next);
        }
    }
}

void GenerateMap(std::uint32_t seed)
{
    g_level.seed = seed;
    g_level.tiles.assign(kMapSize * kMapSize, Tile::Wall);
    std::mt19937 random(seed);
    std::uniform_int_distribution<int> coordinate(3, kMapSize - 4);
    int previousX = 16, previousY = 16;
    // Every room is joined to the preceding room before it is carved.
    // Thus random rooms cannot introduce a disconnected playable island.
    for (int room = 0; room < 20; ++room)
    {
        int centerX = room == 0 ? 16 : coordinate(random);
        int centerY = room == 0 ? 16 : coordinate(random);
        // Fixed distant anchor rooms guarantee enough hunting space even for degenerate random draws.
        if (room == 18)
        {
            centerX = 4;
            centerY = 4;
        }
        if (room == 19)
        {
            centerX = kMapSize - 5;
            centerY = kMapSize - 5;
        }
        while (previousX != centerX)
        {
            Carve(previousX, previousY);
            Carve(previousX, previousY + 1);
            previousX += previousX < centerX ? 1 : -1;
        }
        while (previousY != centerY)
        {
            Carve(previousX, previousY);
            Carve(previousX + 1, previousY);
            previousY += previousY < centerY ? 1 : -1;
        }
        for (int y = centerY - 2; y <= centerY + 2; ++y)
        {
            for (int x = centerX - 2; x <= centerX + 2; ++x)
            {
                Carve(x, y);
            }
        }
    }
    tutorial::g_state.playerPosition = g_level.entry;
    UpdateNavigation();
    g_level.reachableCells.clear();
    for (int cell = 0; cell < kMapSize * kMapSize; ++cell)
    {
        if (g_level.navigationDistances[cell] < 0)
        {
            g_level.tiles[cell] = Tile::Wall;
            continue;
        }
        g_level.reachableCells.push_back(cell);
        // Shallow water is traversable and never breaks the connectivity contract.
        if (random() % 13 == 0)
        {
            g_level.tiles[cell] = Tile::Water;
        }
    }
    g_level.enemies.clear();
    std::shuffle(g_level.reachableCells.begin(), g_level.reachableCells.end(), random);
    for (int cell : g_level.reachableCells)
    {
        tutorial::Vector2 position = {cell % kMapSize + .5f, cell / kMapSize + .5f};
        if (tutorial::Distance(position, g_level.entry) < 5)
        {
            continue;
        }
        g_level.enemies.push_back({position});
        if (g_level.enemies.size() == 14)
        {
            break;
        }
    }
    g_level.drops.clear();
    g_level.generated = true;
}
} // namespace levelone
