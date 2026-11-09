#include "stdafx.h"
#include "PlayerMobility.h"
#include "PlayerCombat.h"
#include "Game.h"
#include "Level.h"
#include "Civic.h"
#include "Hud.h"
#include "Save.h"
#include "Window.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>

namespace
{
using namespace tutorial;
float Length(models::Point p)
{
    return std::hypot(p.x, p.y);
}
void TestMobilityRules()
{
    for (int fps : {30, 60, 144})
    {
        PlayerMobility mobility;
        mobility.RequestDodge();
        float distance = 0;
        for (int i = 0; i < fps; ++i)
        {
            auto delta = mobility.Update(1.f / fps, {1, 1, 0}, 0, false, false, true, false);
            assert(std::fabs(delta.x - delta.y) < .0001f);
            distance += Length(delta);
        }
        assert(std::fabs(distance - PlayerMobility::DodgeSpeed * PlayerMobility::DodgeSeconds) < .001f);
        assert(!mobility.Dodging() && mobility.Stamina() >= 70 && mobility.Stamina() < 80);
        mobility.RestoreStamina(29, PlayerMobility::RegenDelaySeconds);
        mobility.RequestDodge(); // A request made without enough stamina must not fire later.
        for (int i = 0; i < fps; ++i)
        {
            assert(Length(mobility.Update(1.f / fps, {}, 0, false, false, true, false)) == 0);
        }
        assert(mobility.Stamina() > PlayerMobility::DodgeCost && !mobility.Dodging());
    }
    PlayerMobility mobility;
    mobility.Update(.016f, {1, 0, 0}, 0, true, false, true, false);
    assert(mobility.Sprinting() && mobility.Stamina() < PlayerMobility::MaxStamina);
    mobility.Update(.016f, {1, 0, 0}, 0, true, true, true, false);
    assert(!mobility.Sprinting());
    mobility.Update(.016f, {1, 0, 0}, 0, true, false, true, true);
    assert(!mobility.Sprinting());
    mobility.RequestDodge();
    assert(Length(mobility.Update(0, {}, 0, false, false, true, false)) == 0);
    const auto first = mobility.Update(.016f, {}, kPi * .5f, false, false, true, false);
    assert(first.y > 0 && std::fabs(first.x) < .001f);
    mobility.RotateDirection(1, 0);
    const auto rotated = mobility.Update(.016f, {0, 1, 0}, 0, false, false, true, false);
    assert(rotated.x > 0 && rotated.y == 0);
    const float stamina = mobility.Stamina();
    mobility.Cancel();
    assert(!mobility.Dodging() && mobility.Stamina() == stamina);
    GameState world;
    world.playerMotion.mobility.RestoreStamina(75.f);
    world.playerMotion.velocity = {3, 2};
    world.StopMotion();
    assert(world.playerMotion.mobility.Stamina() == 75.f && world.playerMotion.velocity.x == 0);
    mobility.RestoreStamina(PlayerMobility::MaxStamina);
    mobility.RequestDodge();
    Vector2 position = {};
    bool contacted = false;
    for (int i = 0; i < 12; ++i)
    {
        const auto d = mobility.Update(.03f, {1, 0, 0}, 0, false, false, true, false);
        const auto next = BodyCollision::Slide(position, {position.x + d.x, position.y + d.y},
                                               [](Vector2 p) { return p.x >= 1; });
        assert(next.x < 1);
        contacted |= mobility.Contact(Length(d), Game::Distance(position, next));
        position = next;
    }
    assert(contacted && !mobility.Dodging() && mobility.Stamina() == 70);

    mobility.RestoreStamina(1);
    mobility.Update(.05f, {1, 0, 0}, 0, true, false, true, false);
    assert(mobility.Sprinting());
    for (int i = 0; i < 30; ++i)
    {
        mobility.Update(.05f, {1, 0, 0}, 0, true, false, true, false);
        assert(!mobility.Sprinting());
    }
    mobility.Update(.05f, {1, 0, 0}, 0, false, false, true, false);
    mobility.Update(.05f, {1, 0, 0}, 0, true, false, true, false);
    assert(mobility.Sprinting());
}
void TestGameInput()
{
    auto &g = Game::Get();
    g.Reset();
    auto &w = g.GetState();
    auto &m = game::GameLevels::Mine().GetState();
    game::GameLevels::Mine().Map().Generate(17);
    std::fill(m.tiles.begin(), m.tiles.end(), game::Tile::Floor);
    m.enemies.clear();
    m.survivors.clear();
    m.active = true;
    w.isInsideHome = false;
    w.playerPosition = w.cameraPosition = {20.5f, 20.5f};
    w.facingYaw = -kPi * .25f;
    g.KeyDown('d', 0, 0);
    g.KeyDown(platform::SprintKey, 0, 0);
    for (int i = 0; i < 45; ++i)
    {
        g.Update(1.f / 60);
    }
    assert(w.playerMotion.mobility.Sprinting() && w.movementSpeed > kMovementSpeed * 1.3f);
    g.KeyDown(' ', 0, 0);
    g.Update(1.f / 60);
    assert(w.playerMotion.mobility.Dodging());
    assert(combat::PlayerCombat::Evaluate(combat::PlayerCombat::Current(), combat::CombatAction::Reload,
                                          combat::CombatArea::Mine) == combat::CombatBlock::Dodging);
    game::GameLevels::Mine().Combat().Melee();
    assert(m.meleeSeconds == 0);
    const float stamina = w.playerMotion.mobility.Stamina();
    Hud::OpenMenu(UiScreen::Pause);
    assert(!w.playerMotion.mobility.Dodging() && w.playerMotion.mobility.Stamina() == stamina);
    g.Update(.05f);
    assert(w.playerMotion.mobility.Stamina() == stamina);
    Hud::CloseMenu();
    g.KeyUp(' ', 0, 0);
    g.Update(.016f);
    assert(!w.playerMotion.mobility.Dodging());
    g.Reset();
}
void Wait()
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(35);
    auto &g = Game::Get();
    while ((SaveSystem::Loading() || !g.Surface()->Ready()) && std::chrono::steady_clock::now() < until)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!SaveSystem::Loading() && g.Surface()->Ready());
}
void TestSurfaceDodgeInput()
{
    auto &g = Game::Get();
    assert(g.EnterTestStage(TestStage::SurfaceSurvey));
    Wait();
    auto &w = g.GetState();
    assert(w.surfaceExploration && !g.IsSurfaceMine() && !w.isBikeMounted);
    g.KeyDown(' ', 0, 0);
    assert(w.playerMotion.mobility.Dodging());
    g.KeyUp(' ', 0, 0);
    w.StopMotion();
    w.isBikeMounted = true;
    g.KeyDown(' ', 0, 0);
    assert(!w.playerMotion.mobility.Dodging() && w.pressedKeys[' ']);
    g.KeyUp(' ', 0, 0);
    g.Reset();
}
void TestDodgePoseHandoff()
{
    models::CharacterAnimation animation;
    animation.StartDodge({0, -1, 0}, 0);
    animation.Update(.025f, {0, -7.5f, 0}, 0, true, false);
    models::CharacterPose pose{};
    animation.Apply(pose);
    assert(pose.dodgeSample == 12 && pose.dodgeWeight > 0 && pose.dodgeWeight < 1);
    for (int i = 0; i < 4; ++i)
    {
        animation.Update(.05f, {0, -7.5f, 0}, 0, true, false);
    }
    animation.Apply(pose);
    assert(pose.dodgeTime > PlayerMobility::DodgeSeconds && pose.dodgeWeight > .99f);
    const float sampledTime = pose.dodgeTime;
    animation.InterruptDodge();
    animation.Update(.05f, {}, 0, false, false);
    animation.Apply(pose);
    assert(pose.dodgeTime == sampledTime && pose.dodgeWeight > 0 && pose.dodgeWeight < .99f);
    animation.Update(.05f, {}, 0, false, false);
    animation.Apply(pose);
    assert(pose.dodgeSample < 0 && pose.dodgeWeight == 0);
}
void TestSavedStamina()
{
    auto &g = Game::Get();
    g.StartNewGame();
    Wait();
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("mobility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "current.sav", legacy = folder / "schema9.sav", bad = folder / "invalid.sav";
    g.GetState().playerMotion.mobility.RestoreStamina(75.f, .3f);
    assert(SaveSystem::Save(file) && SaveSystem::CheckFile(file));
    std::ifstream input(file, std::ios::binary);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto number = [&](size_t offset) {
        std::uint64_t n = 0;
        for (unsigned i = 0; i < 8; ++i)
        {
            n |= std::uint64_t(bytes[offset + i]) << (8 * i);
        }
        return n;
    };
    auto put = [&](size_t offset, std::uint64_t n) {
        for (unsigned i = 0; i < 8; ++i)
        {
            bytes[offset + i] = static_cast<unsigned char>(n >> (8 * i));
        }
    };
    auto write = [&](const auto &path) {
        std::uint64_t hash = 14695981039346656037ull;
        for (size_t i = 40; i < bytes.size(); ++i)
        {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
        put(24, bytes.size() - 40);
        put(32, hash);
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        assert(output.good());
    };
    assert(number(8) == 10);
    const auto extensionSize = number(bytes.size() - 8);
    assert(number(bytes.size() - 8 - size_t(extensionSize)) == 1);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(bytes.data() + bytes.size() - 16, &nan, sizeof(nan));
    write(bad);
    assert(!SaveSystem::CheckFile(bad));
    // Schema 9 stored only one float per planet: the obsolete cooldown.
    bytes.erase(bytes.end() - 12, bytes.end() - 8);
    const float oldCooldown = .75f;
    std::memcpy(bytes.data() + bytes.size() - 12, &oldCooldown, sizeof(oldCooldown));
    put(bytes.size() - 8, extensionSize - sizeof(float));
    put(8, 9);
    write(legacy);
    assert(SaveSystem::CheckFile(legacy));
    g.GetState().playerMotion.mobility.RestoreStamina(10.f);
    assert(SaveSystem::Load(file));
    Wait();
    assert(g.GetState().playerMotion.mobility.Stamina() == 75.f &&
           g.GetState().playerMotion.mobility.RegenDelay() == .3f &&
           !g.GetState().playerMotion.mobility.Dodging());
    assert(SaveSystem::Load(legacy));
    Wait();
    assert(g.GetState().playerMotion.mobility.Stamina() == PlayerMobility::MaxStamina);
    g.Reset();
}
} // namespace
int main()
{
    TestMobilityRules();
    TestGameInput();
    TestSurfaceDodgeInput();
    TestDodgePoseHandoff();
    TestSavedStamina();
    std::cout << "Sprint, evasive dash, collision, input and stamina save checks passed.\n";
}
