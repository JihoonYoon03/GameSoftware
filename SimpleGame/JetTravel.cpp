#include "stdafx.h"
#include "JetTravel.h"
#include "LevelOne.h"
#include <algorithm>
#include <cmath>

namespace jettravel
{
State g_travel;
namespace
{
constexpr float kCruiseSpeed = 58;
constexpr float kBoostSpeed = 108;
constexpr float kMinimumSpeed = 22;
constexpr float kSteeringRate = 2.8f;
constexpr float kBikeScreenX = 400;
constexpr float kRoutePixelScale = 2.3f;
} // namespace
void Reset()
{
    g_travel = State{};
}
void Begin()
{
    if (!levelone::g_level.quest.BeginJourney())
    {
        tutorial::ShowNotice("리아 / 도시 수신망에서 광산 중계 흔적부터 확인하자.");
        return;
    }
    Reset();
    g_travel.active = true;
    constexpr float lanes[kObstacleCount] = {0, -.65f, .6f, -.35f, .75f, -.75f, .15f};
    for (int i = 0; i < kObstacleCount; ++i)
    {
        g_travel.obstacles[i] = {240.f + i * 215.f, lanes[i], false};
    }
    tutorial::g_state.isJetTravel = true;
    tutorial::g_state.isInsideHome = false;
    tutorial::g_state.isInObservatory = false;
    tutorial::g_state.cameraPosition = {0, 0};
    tutorial::g_state.movementAmount = 0;
    std::fill(tutorial::g_state.pressedKeys, tutorial::g_state.pressedKeys + 256, false);
    tutorial::ShowNotice("제트 바이크 / W·S로 위아래 조종, D 가속, A 감속, Space 부스트. 붉은 장애물을 "
                         "피하며 광산으로 이동하세요.");
}
bool IsActive()
{
    return g_travel.active;
}
tutorial::Vector2 GetBikeScreenPosition()
{
    return {kBikeScreenX, 485 + g_travel.lateralOffset * 115};
}
float GetObstacleScreenX(const Obstacle &obstacle)
{
    return kBikeScreenX + (obstacle.distance - g_travel.distance) * kRoutePixelScale;
}
models::Palette GetBikePalette()
{
    models::Palette palette;
    palette.body = {.62f, .69f, .74f, 1};
    palette.accent = {.9f, .5f, .22f, 1};
    palette.emission = {.3f, .9f, 1, 1};
    return palette;
}
void Update(float deltaSeconds)
{
    if (!g_travel.active)
    {
        return;
    }
    const auto &keys = tutorial::g_state.pressedKeys;
    g_travel.elapsedSeconds += deltaSeconds;
    float steering = (keys['s'] ? 1.f : 0) - (keys['w'] ? 1.f : 0);
    float smoothing = 1 - std::exp(-8 * deltaSeconds);
    g_travel.lateralVelocity += (steering * kSteeringRate - g_travel.lateralVelocity) * smoothing;
    g_travel.lateralOffset =
        tutorial::Clamp(g_travel.lateralOffset + g_travel.lateralVelocity * deltaSeconds, -1, 1);
    bool boosting = keys[' '] && g_travel.boostCharge > .03f;
    g_travel.boostCharge =
        tutorial::Clamp(g_travel.boostCharge + (boosting ? -.32f : .18f) * deltaSeconds, 0, 1);
    g_travel.speedPenaltySeconds = (std::max)(0.f, g_travel.speedPenaltySeconds - deltaSeconds);
    float targetSpeed = boosting ? kBoostSpeed : keys['d'] ? 82.f : keys['a'] ? kMinimumSpeed : kCruiseSpeed;
    if (g_travel.speedPenaltySeconds > 0)
    {
        targetSpeed = kMinimumSpeed;
    }
    g_travel.speed += (targetSpeed - g_travel.speed) * (1 - std::exp(-2 * deltaSeconds));
    g_travel.distance = (std::min)(kRouteLength, g_travel.distance + g_travel.speed * deltaSeconds);
    for (auto &obstacle : g_travel.obstacles)
    {
        if (obstacle.passed || obstacle.distance > g_travel.distance)
        {
            continue;
        }
        obstacle.passed = true;
        if (std::fabs(g_travel.lateralOffset - obstacle.lane) < .3f)
        {
            ++g_travel.collisionCount;
            g_travel.speed *= .45f;
            g_travel.speedPenaltySeconds = .8f;
            tutorial::ShowNotice("차체 경고 / 부유 잔해에 접촉했습니다. 자세를 회복하고 주행을 계속하세요.");
        }
        else
        {
            ++g_travel.avoidedObstacles;
        }
    }
    if (g_travel.distance >= kRouteLength)
    {
        g_travel.active = false;
        tutorial::g_state.isJetTravel = false;
        tutorial::g_state.isComplete = true;
        tutorial::g_state.mainQuestStage = tutorial::MainQuestStage::Complete;
        levelone::Enter();
        tutorial::ShowNotice("광산 / 제트 바이크를 입구에 주차했습니다. 구조 요청이 갱도 곳곳에서 들립니다. "
                             "마우스 사격 / R 재장전 / F 구조 스캔 / E 조사·구조.");
    }
}
} // namespace jettravel
