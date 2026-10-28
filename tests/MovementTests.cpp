#include "stdafx.h"
#include "Game.h"
#include "Level.h"
#include "Civic.h"
#include "Overworld.h"
#include "Hud.h"
#include "Dependencies/freeglut.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace tutorial;
namespace
{
float Speed(Vector2 v)
{
    return std::hypot(v.x, v.y);
}
void TestStartBrakePivot()
{
    PlayerMotion motion;
    float yaw = 0, dt = 1.f / 60;
    motion.Update({3.2f, 0}, yaw, false, dt);
    assert(Speed(motion.velocity) > 0 && Speed(motion.velocity) < .4f);
    for (int i = 0; i < 60; ++i)
    {
        motion.Update({3.2f, 0}, yaw, false, dt);
    }
    assert(std::fabs(motion.velocity.x - 3.2f) < .001f);
    motion.Update({-3.2f, 0}, yaw, false, dt);
    assert(motion.pivoting && motion.velocity.x > 2.7f && std::fabs(yaw) < .11f);
    bool nearlyStopped = false;
    int plantFrames = 0;
    for (int i = 0; i < 60; ++i)
    {
        motion.Update({-3.2f, 0}, yaw, false, dt);
        nearlyStopped |= Speed(motion.velocity) < .4f;
        plantFrames += motion.pivoting && Speed(motion.velocity) < .008f;
        if (motion.velocity.x < -.4f)
        {
            assert(std::cos(yaw) < -.75f);
        }
        assert(Speed(motion.velocity) <= 3.201f);
    }
    assert(nearlyStopped && plantFrames >= 2 && motion.velocity.x < -3.1f && !motion.pivoting);
    assert(std::cos(yaw) < -.999f);
    motion.Update({}, yaw, false, dt);
    assert(Speed(motion.velocity) > 2.7f && Speed(motion.velocity) < 3.1f);
    for (int i = 0; i < 20; ++i)
    {
        motion.Update({}, yaw, false, dt);
    }
    assert(Speed(motion.velocity) == 0);
    motion.velocity = {3.2f, 0};
    yaw = .7f;
    motion.Update({-3.2f, 0}, yaw, true, dt);
    assert(yaw == .7f && !motion.pivoting && motion.velocity.x > 0);
    // Removing wall-normal momentum must retain the tangent and never inject speed.
    auto slide = MotionMath::ContactVelocity({2, 3}, {.2f, .3f}, {0, .3f});
    assert(std::fabs(slide.x) < .001f && slide.y == 3);
    auto rounded = MotionMath::ContactVelocity({3.2f, 0}, {.0533333f, 0}, {.053321f, 0});
    assert(rounded.x == 3.2f); // Float accumulation during substeps must not behave as a wall.
}
void TestRateAndResponse()
{
    auto run = [](int fps) {
        PlayerMotion motion;
        float yaw = 0;
        Vector2 position = {};
        for (int frame = 0; frame < fps; ++frame)
        {
            motion.Update({3.2f, 0}, yaw, false, 1.f / fps);
            position.x += motion.velocity.x / fps;
        }
        return position.x;
    };
    static_assert(MotionMath::kResponse == 1.3f);
    assert(std::fabs(run(30) - run(144)) < .055f);
    assert(std::fabs(MotionMath::Turn(kPi - .01f, -kPi + .01f, .03f) + kPi - .01f) < .001f);
}
void TestGameContextsAndInput()
{
    auto &g = Game::Get();
    auto &w = g.GetState();
    for (auto stage : {TestStage::Prologue, TestStage::Mine, TestStage::ChapterOne})
    {
        g.EnterTestStage(stage);
        if (stage == TestStage::Mine)
        {
            auto &mine = game::GameLevels::Mine().GetState();
            std::fill(mine.tiles.begin(), mine.tiles.end(), game::Tile::Floor);
            mine.enemies.clear();
            w.playerPosition = w.cameraPosition = {20.5f, 20.5f};
        }
        w.facingYaw = -kPi / 4;
        g.KeyDown('d', 0, 0);
        g.Update(1.f / 60);
        assert(w.movementSpeed > 0 && w.movementSpeed < .5f);
        g.KeyUp('d', 0, 0);
        g.Update(1.f / 60);
        assert(Speed(w.playerMotion.velocity) == 0);
        g.KeyDown('d', 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            g.Update(1.f / 60);
        }
        assert(w.movementSpeed > 1);
        Hud::OpenMenu(UiScreen::Pause);
        assert(Speed(w.playerMotion.velocity) == 0 && w.movementSpeed == 0);
        Hud::CloseMenu();
        auto position = w.playerPosition;
        g.Update(1.f / 60);
        assert(Game::Distance(position, w.playerPosition) < .001f);
    }
    g.Reset();
}
void TestBikeDriftAndRecovery()
{
    auto &g = Game::Get();
    auto &w = g.GetState();
    g.Reset();
    w.isInsideHome = false;
    w.bikePosition = {80, 0};
    w.bikeYaw = 0;
    Overworld::ToggleBike(w);
    w.pressedKeys['w'] = true;
    for (int i = 0; i < 45; ++i)
    {
        Overworld::UpdateBike(w, 1.f / 60);
    }
    assert(w.bikeSpeed > 7 && w.bikeSpeed <= Overworld::kBikeCruiseSpeed + .001f);
    w.pressedKeys['d'] = true;
    for (int i = 0; i < 8; ++i)
    {
        Overworld::UpdateBike(w, 1.f / 60);
    }
    float slip = std::remainder(std::atan2(w.bikeVelocity.y, w.bikeVelocity.x) - w.bikeYaw, 2 * kPi);
    assert(std::fabs(slip) > .015f && std::fabs(slip) < .4f);
    w.pressedKeys['d'] = false;
    for (int i = 0; i < 45; ++i)
    {
        Overworld::UpdateBike(w, 1.f / 60);
    }
    slip = std::remainder(std::atan2(w.bikeVelocity.y, w.bikeVelocity.x) - w.bikeYaw, 2 * kPi);
    assert(std::fabs(slip) < .025f && std::fabs(w.bikeYawSpeed) < .001f);
    w.pressedKeys['w'] = false;
    for (int i = 0; i < 40; ++i)
    {
        Overworld::UpdateBike(w, 1.f / 60);
    }
    assert(std::fabs(w.bikeSpeed) < .001f && Speed(w.bikeVelocity) < .001f);
    w.pressedKeys['s'] = true;
    for (int i = 0; i < 45; ++i)
    {
        Overworld::UpdateBike(w, 1.f / 60);
    }
    assert(w.bikeSpeed < -.6f && w.bikeSpeed >= -Overworld::kBikeReverseSpeed - .001f);
    g.Reset();
}
} // namespace
int main()
{
    TestStartBrakePivot();
    TestRateAndResponse();
    TestGameContextsAndInput();
    TestBikeDriftAndRecovery();

    std::cout
        << "Motion checks passed: start/stop/pivot, frame rates, world contexts, bike grip, resets at fixed "
           "130% response.\n";
}
