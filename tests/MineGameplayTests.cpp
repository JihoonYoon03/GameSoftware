#include "stdafx.h"
#include "LevelOne.h"
#include "JetTravel.h"
#include "Profiler.h"
#include "Scene/WorldScene.h"
#include "Monitoring/WorldSnapshot.h"
#include "Monitoring/LocalMonitorServer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <type_traits>

// Headless dependencies: exercise the production quest, gun, collision, lighting,
// projectiles, scanner, evacuation, and JSON code without opening the game or using OpenGL.
namespace tutorial
{
TutorialState g_state;
float Distance(Vector2 a, Vector2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}
float Clamp(float value, float low, float high)
{
    return (std::max)(low, (std::min)(high, value));
}
void ShowNotice(const std::string &text)
{
    g_state.noticeText = text;
    g_state.noticeSecondsRemaining = 12;
}
game::Scene &GetWorldScene(WorldSceneKind)
{
    static game::Scene scene;
    return scene;
}
} // namespace tutorial
namespace rendercache
{
void InvalidateTerrain()
{
}
} // namespace rendercache
namespace profiling
{
Scope::Scope(Timer value) : timer(value)
{
}
Scope::~Scope()
{
}
void Count(Counter, std::uint64_t)
{
}
const std::string &GetLatestReportJson()
{
    static const std::string empty = "null";
    return empty;
}
} // namespace profiling
namespace monitoring::transport
{
std::uint64_t ReadRequestCount()
{
    return 0;
}
} // namespace monitoring::transport

namespace
{
using namespace levelone;
using tutorial::Vector2;
void ReadyMine(unsigned int seed = 13)
{
    ResetProgress();
    assert(g_level.quest.Advance(MineQuestStage::ShowRecord));
    assert(g_level.quest.Advance(MineQuestStage::CheckCityRelay));
    assert(g_level.quest.BeginJourney());
    assert(g_level.quest.Advance(MineQuestStage::TravelToMine));
    tutorial::g_state.playerPosition = kMineEntry;
    tutorial::g_state.cameraPosition = kMineEntry;
    GenerateMap(seed);
    g_level.active = true;
    g_level.invulnerability = 1000;
}
void AimAt(Vector2 target)
{
    auto camera = tutorial::g_state.cameraPosition;
    tutorial::g_state.cursorPosition = {
        tutorial::kCanvasWidth * .5f + (target.x - target.y - camera.x + camera.y) * 36,
        tutorial::kCanvasHeight * .57f + (target.x + target.y - camera.x - camera.y) * 18 - 26};
}
void Step(float seconds)
{
    for (int i = 0; i < int(std::ceil(seconds / .025f)); ++i)
    {
        Update(.025f);
    }
}
void TestWeapon()
{
    static_assert(std::is_base_of<combat::Weapon, combat::Firearm>::value);
    static_assert(std::is_base_of<combat::Firearm, combat::Pistol>::value);
    combat::Pistol gun;
    combat::Weapon &weapon = gun;
    combat::Shot shot;
    assert(!weapon.Reload());
    assert(weapon.TryFire(.5f, shot));
    assert(std::fabs(shot.angle - .5f) <= .0081f);
    assert(gun.GetRounds() == 11);
    assert(!weapon.TryFire(.5f, shot));
    float initial = gun.GetSpread();
    for (int i = 1; i < 12; ++i)
    {
        weapon.Update(.2f);
        assert(weapon.TryFire(.5f, shot));
    }
    assert(gun.GetRounds() == 0 && gun.GetSpread() > initial);
    weapon.Update(.2f);
    assert(!weapon.TryFire(.5f, shot) && gun.IsReloading());
    assert(!weapon.TryFire(.5f, shot) && !weapon.Reload());
    weapon.Update(1.f);
    assert(gun.IsReloading() && gun.GetRounds() == 0);
    weapon.Update(.5f);
    assert(!gun.IsReloading() && gun.GetRounds() == 12);
    assert(weapon.TryFire(.5f, shot));
    assert(weapon.Reload());
    weapon.Update(2.f);
    assert(gun.GetRounds() == 12 && gun.GetSpread() <= .0081f);
    assert(!weapon.TryFire(std::numeric_limits<float>::quiet_NaN(), shot));
    weapon.Update(-1);
    assert(gun.GetRounds() == 12);
}
void TestQuest()
{
    MineQuest quest;
    assert(!quest.BeginJourney());
    assert(!quest.Advance(MineQuestStage::ReturnToHalo));
    assert(quest.Advance(MineQuestStage::ShowRecord));
    assert(!quest.Advance(MineQuestStage::ShowRecord));
    assert(quest.Advance(MineQuestStage::CheckCityRelay));
    assert(!quest.Advance(MineQuestStage::TravelToMine));
    assert(quest.BeginJourney() && quest.IsRiaJoined());
    assert(quest.Advance(MineQuestStage::TravelToMine));
    assert(!quest.IsPowerBypassed());
    assert(quest.Advance(MineQuestStage::BypassPower));
    assert(quest.IsPowerBypassed());
    assert(quest.Advance(MineQuestStage::InvestigateSignal));
    assert(quest.Advance(MineQuestStage::InvestigateGlyph));
    assert(quest.Advance(MineQuestStage::CollectCoordinates));
    assert(quest.AreCoordinatesFound());
    assert(quest.Advance(MineQuestStage::ReturnToHalo));
    assert(!quest.Advance(MineQuestStage::Complete));
}
void TestMapAndGate()
{
    for (unsigned seed = 0; seed < 30; ++seed)
    {
        ReadyMine(seed);
        assert(!FindRoute(g_level.entry, kPowerBypass).empty());
        assert(FindRoute(g_level.entry, kRuinSignal).empty());
        assert(!IsWalkable(32, 29));
        assert(g_level.survivors.size() == 12 && g_level.rescueGroups.size() == 4);
        assert(g_level.enemies.size() == kEnemyPopulation);
        assert(g_level.quest.Advance(MineQuestStage::BypassPower));
        assert(IsWalkable(32, 29));
        assert(!FindRoute(g_level.entry, kRuinSignal).empty());
        assert(!FindRoute(g_level.entry, kObserverGlyph).empty());
        assert(!FindRoute(g_level.entry, kCoordinateTerminal).empty());
        for (const auto &npc : g_level.survivors)
        {
            assert(!IsBlocked(npc.position));
            assert(!FindRoute(npc.position, g_level.entry).empty());
        }
        tutorial::g_state.playerPosition = g_level.entry;
        UpdateNavigation();
        for (int cell : g_level.reachableCells)
        {
            assert(g_level.navigationDistances[cell] >= 0);
        }
    }
}
void TestAimLightingAndWallShots()
{
    ReadyMine();
    g_level.enemies.clear();
    tutorial::g_state.playerPosition = {24.5f, 17.5f};
    tutorial::g_state.cameraPosition = tutorial::g_state.playerPosition;
    AimAt({24.5f, 23.5f});
    auto direction = GetAimDirection(GetAimAngle());
    assert(std::fabs(direction.x) < .0001f && direction.y > .99f);
    assert(LightAt({24.5f, 21.5f}) > .3f);
    assert(LightAt({24.5f, 13.5f}) < .01f);
    tutorial::g_state.playerPosition = {29.5f, 29.5f};
    tutorial::g_state.cameraPosition = tutorial::g_state.playerPosition;
    AimAt({35.5f, 29.5f});
    g_level.enemies.push_back({{35.5f, 29.5f}});
    assert(LightAt({35.5f, 29.5f}) == 0);
    Attack();
    Step(.3f);
    assert(g_level.enemies[0].health == kEnemyMaxHealth);
    assert(g_level.bullets.empty());
    assert(g_level.quest.Advance(MineQuestStage::BypassPower));
    assert(LightAt({35.5f, 29.5f}) > .1f);
    tutorial::g_state.playerPosition = {20.5f, 10.5f};
    tutorial::g_state.cameraPosition = tutorial::g_state.playerPosition;
    g_level.enemies = {{{23.f, 10.5f}}};
    g_level.pistol.Reset();
    for (int i = 0; i < 3; ++i)
    {
        AimAt(g_level.enemies[0].position);
        Attack();
        Step(.225f);
    }
    assert(g_level.enemies[0].health == 0);
    assert(g_level.kills == 1 && g_level.player.experience == kExperiencePerKill);
    assert(g_level.drops.size() == 1);
}
void TestRescueAndScanner()
{
    ReadyMine();
    g_level.enemies.clear();
    tutorial::g_state.playerPosition = g_level.rescueGroups[0].position;
    tutorial::g_state.cameraPosition = tutorial::g_state.playerPosition;
    Scan();
    assert(g_level.scannerSeconds > 0 && !g_level.scannerContacts.empty());
    float cooldown = g_level.scannerCooldown;
    Scan();
    assert(g_level.scannerCooldown == cooldown);
    Interact();
    assert(g_level.rescueGroups[0].rescued && g_level.rescuedPeople == 3);
    Interact();
    assert(g_level.rescuedPeople == 3);
    for (const auto &npc : g_level.survivors)
    {
        if (npc.group == 0)
        {
            assert(npc.evacuating && !npc.route.empty());
        }
    }
    Step(3.f);
    assert(g_level.scannerSeconds == 0);
    assert(g_level.survivors.size() == 12); // Visible evacuees are never erased early.
    tutorial::g_state.playerPosition = {52.5f, 51.5f};
    tutorial::g_state.cameraPosition = tutorial::g_state.playerPosition;
    Step(3.f);
    assert(g_level.survivors.size() == 9 && g_level.returnedPeople == 3);
}
void TestStoryAndReentry()
{
    ReadyMine();
    g_level.enemies.clear();
    tutorial::g_state.playerPosition = kCoordinateTerminal;
    Interact();
    assert(!g_level.quest.AreCoordinatesFound());
    tutorial::g_state.playerPosition = kPowerBypass;
    Interact();
    tutorial::g_state.playerPosition = kRuinSignal;
    Interact();
    tutorial::g_state.playerPosition = kObserverGlyph;
    Interact();
    tutorial::g_state.playerPosition = kCoordinateTerminal;
    Interact();
    assert(g_level.quest.GetStage() == MineQuestStage::ReturnToHalo);
    unsigned seed = g_level.seed;
    g_level.player.salvage = 7;
    tutorial::g_state.playerPosition = g_level.entry;
    Interact();
    assert(!IsActive() && g_level.quest.GetStage() == MineQuestStage::Complete);
    Enter();
    assert(IsActive() && g_level.seed == seed && g_level.player.salvage == 7);
    assert(g_level.quest.GetStage() == MineQuestStage::Complete);
}
} // namespace
int main(int argc, char **argv)
{
    TestWeapon();
    TestQuest();
    TestMapAndGate();
    TestAimLightingAndWallShots();
    TestRescueAndScanner();
    TestStoryAndReentry();
    if (argc > 1)
    {
        std::ofstream snapshot(argv[1]);
        snapshot << monitoring::BuildWorldSnapshot(1, "mine-tests");
    }
    std::cout << "Mine gameplay checks passed: weapon, quest, 30 maps, aiming, lighting, walls, combat, "
                 "scanner, rescue, reentry.\n";
}
