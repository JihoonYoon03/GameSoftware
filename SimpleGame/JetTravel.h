#pragma once
#include "TutorialState.h"
#include <array>

namespace jettravel
{
constexpr float kRouteLength = 1800;
constexpr int kObstacleCount = 7;
struct Obstacle
{
    float distance;
    float lane;
    bool passed = false;
};
struct State
{
    bool active = false;
    float elapsedSeconds = 0;
    float distance = 0;
    float speed = 35;
    float lateralOffset = 0;
    float lateralVelocity = 0;
    float boostCharge = 1;
    float speedPenaltySeconds = 0;
    int avoidedObstacles = 0;
    int collisionCount = 0;
    std::array<Obstacle, kObstacleCount> obstacles = {};
};
extern State g_travel;
void Reset();
void Begin();
void Update(float deltaSeconds);
bool IsActive();
float GetObstacleScreenX(const Obstacle &obstacle);
tutorial::Vector2 GetBikeScreenPosition();
models::Palette GetBikePalette();
void DrawBackground();
void DrawWorld();
void DrawHud();
} // namespace jettravel
