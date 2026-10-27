#include "stdafx.h"
#include "Level.h"
#include "Hud.h"
#include "Civic.h"
#include "Drawing.h"
#include "Monitoring/LocalMonitorServer.h"
#include "Monitoring/Monitor.h"
#include "Overworld.h"
#include "Profiler.h"
#include "ResidentAI.h"
#include "Scene.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include "RenderAssets.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <type_traits>
#include <unordered_map>

// Link the production core; tests exercise CPU rules without creating a window or GL context.
namespace
{
using namespace game;
MineState &mineState = GameLevels::Mine().GetState();
using tutorial::Vector2;
void ReadDialogue()
{
    auto &d = tutorial::Game::Get().GetState().dialogue;
    int pages = 0;
    while (tutorial::Dialogue::Active())
    {
        assert(++pages < 200);
        if (!d.pages[d.page].choices.empty())
        {
            assert(tutorial::Dialogue::Select(0));
        }
        assert(tutorial::Dialogue::Advance());
    }
}
void ReadyMine(unsigned int seed = 13)
{
    GameLevels::Mine().Reset();
    mineState.loadout.GrantPistol();
    mineState.loadout.Select(combat::WeaponSlot::Secondary);
    assert(mineState.quest.Advance(MineQuestStage::ShowRecord));
    assert(mineState.quest.Advance(MineQuestStage::CheckCityRelay));
    assert(mineState.quest.BeginJourney());
    assert(mineState.quest.Advance(MineQuestStage::TravelToMine));
    tutorial::Game::Get().GetState().playerPosition = kMineEntry;
    tutorial::Game::Get().GetState().cameraPosition = kMineEntry;
    GameLevels::Mine().Map().Generate(seed);
    mineState.active = true;
    mineState.invulnerability = 1000;
}
void TestStageSelectionAndLoadout()
{
    using namespace tutorial;
    using combat::WeaponSlot;
    auto &g = Game::Get();
    auto &w = g.GetState();
    auto press = [&](unsigned char key) {
        g.KeyDown(key, 0, 0);
        g.KeyUp(key, 0, 0);
    };
    g.Reset();
    g.Resize(1280, 800);
    assert(!mineState.loadout.HasPistol() && mineState.loadout.Selected() == WeaponSlot::Unarmed);
    assert(!mineState.loadout.Select(WeaponSlot::Primary));
    assert(!mineState.loadout.Select(WeaponSlot::Secondary));
    for (int i = 0; i < int(TestStage::Count); ++i)
    {
        Hud::OpenMenu(i % 2 ? UiScreen::Pause : UiScreen::Title);
        press('s');
        press(13);
        assert(w.ui.screen == UiScreen::Stages);
        auto controls = Hud::Controls();
        assert(controls.size() == size_t(TestStage::Count) + 1);
        for (int j = 0; j < i; ++j)
        {
            press('s');
        }
        press(13);
        assert(w.ui.screen == UiScreen::None && !w.isPaused && !w.weaponWheel.visible);
        assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
        assert(mineState.loadout.HasPistol() == (i >= int(TestStage::WeaponUnlocked)));
        assert(mineState.active == (i == int(TestStage::Mine)));
        assert(GameLevels::Chapter().IsActive() == (i == int(TestStage::ChapterOne)));
        if (i == int(TestStage::Prologue))
        {
            assert(w.isInsideHome && w.mainQuestStage == MainQuestStage::ReadHomeTerminal);
        }
        if (i == int(TestStage::Observatory))
        {
            assert(w.isInObservatory && !w.gateSignalHeard);
        }
        if (i >= int(TestStage::BikeUnlocked))
        {
            assert(w.gateSignalHeard && w.hasDescendedObservatory);
        }
        if (i == int(TestStage::BikeUnlocked))
        {
            assert(mineState.quest.GetStage() == MineQuestStage::TravelToMine &&
                   mineState.quest.IsRiaJoined() && w.isBikeMounted &&
                   !BodyCollision::BikeBlocked(w.bikePosition, w.bikeYaw, w.bikeElevation));
        }
        if (i == int(TestStage::WeaponUnlocked))
        {
            assert(!w.isBikeMounted && !mineState.active &&
                   mineState.quest.GetStage() == MineQuestStage::TravelToMine &&
                   Game::Distance(w.playerPosition, Overworld::kMineEntrance) < 2 &&
                   !g.IsPositionBlocked(w.playerPosition));
        }
        if (i == int(TestStage::Mine))
        {
            assert(mineState.quest.IsRiaJoined() &&
                   mineState.quest.GetStage() == MineQuestStage::BypassPower);
        }
        if (i == int(TestStage::ChapterOne))
        {
            assert(mineState.quest.GetStage() == MineQuestStage::Complete && w.isComplete &&
                   GameLevels::Chapter().IsAvailable());
        }
    }
    assert(!g.EnterTestStage(TestStage::Count) && GameLevels::Chapter().IsActive());
    // The normal narrative gives the pistol once, only after reading Ria's entrance handover.
    assert(g.EnterTestStage(TestStage::BikeUnlocked));
    w.isBikeMounted = false;
    w.playerPosition = Overworld::kMineEntrance;
    press('e');
    assert(Dialogue::Active() && !mineState.loadout.HasPistol() && !mineState.active);
    ReadDialogue();
    assert(mineState.active && mineState.loadout.HasPistol() && !mineState.loadout.IsPistolEquipped());
    assert(!mineState.loadout.GrantPistol());
    auto choose = [&](Vector2 offset) {
        g.MouseMotion(640, 400);
        g.KeyDown('q', 0, 0);
        assert(w.weaponWheel.visible && !w.mouseFiring);
        g.MouseMotion(int(640 + offset.x), int(400 + offset.y));
        g.KeyUp('q', 0, 0);
        assert(!w.weaponWheel.visible && !w.mouseFiring);
    };
    int kits = mineState.player.recoveryKits;
    mineState.player.health = 20;
    choose({140, 70});
    assert(mineState.loadout.IsPistolEquipped() && mineState.player.recoveryKits == kits);
    int rounds = mineState.pistol.GetRounds();
    GameLevels::Mine().Combat().Attack();
    assert(mineState.pistol.GetRounds() == rounds - 1);
    choose({-140, 70});
    assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
    GameLevels::Mine().Combat().Reload();
    assert(!mineState.pistol.IsReloading());
    GameLevels::Mine().Combat().Attack();
    assert(mineState.pistol.GetRounds() == rounds - 1 && mineState.meleeSeconds > 0);
    choose({0, -150}); // Empty primary cannot replace the currently selected hands.
    assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
    g.MouseMotion(640, 400);
    g.KeyDown('q', 0, 0);
    g.KeyDown('q', 0, 0); // Auto-repeat cannot consume a kit or reopen the wheel.
    g.MouseMotion(780, 470);
    GameLevels::Mine().Combat().Attack();
    g.MouseButton(GLUT_LEFT_BUTTON, GLUT_DOWN, 780, 470);
    assert(!w.mouseFiring && mineState.pistol.GetRounds() == rounds - 1);
    press(27); // Cancel preserves the prior weapon; Q auto-repeat cannot reopen it.
    g.KeyDown('q', 0, 0);
    assert(!w.weaponWheel.visible);
    g.KeyUp('q', 0, 0);
    assert(!w.weaponWheel.visible && !mineState.loadout.IsPistolEquipped());
    choose({140, 70});
    g.MouseMotion(640, 400);
    g.KeyDown('q', 0, 0);
    g.MouseMotion(500, 470);
    g.MouseEntry(GLUT_LEFT);
    g.KeyUp('q', 0, 0);
    assert(mineState.loadout.IsPistolEquipped());
    Hud::OpenMenu(UiScreen::Pause);
    assert(!WeaponWheel::Open());
    press('q');
    assert(w.ui.screen == UiScreen::Pause);
    Hud::CloseMenu();
    w.mapVisible = true;
    assert(!WeaponWheel::Open());
    w.mapVisible = false;
    press('g');
    assert(mineState.player.recoveryKits == kits - 1 && mineState.player.health > 20);
    GameLevels::Mine().Leave();
    w.playerPosition = Overworld::kMineEntrance;
    press('e');
    assert(mineState.active && !Dialogue::Active() && mineState.pistol.GetRounds() == rounds - 1);
    // Mouse deltas and release also work through the window's letterbox transform.
    g.Resize(1920, 1080);
    g.MouseMotion(960, 540);
    g.KeyDown('q', 0, 0);
    g.MouseMotion(771, 634);
    g.KeyUp('q', 0, 0);
    assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
    g.Resize(1280, 800);
    g.Reset();
    w.isInsideHome = false;
    CivicCombat::Fire();
    assert(!mineState.loadout.HasPistol() && mineState.pistol.GetRounds() == 12 &&
           CivicJustice::State().meleeSeconds > 0);
    g.Reset();
}
void TestUiMenusAndTransactions()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &w = g.GetState();
    g.Reset();
    auto press = [&](unsigned char key) {
        g.KeyDown(key, 0, 0);
        g.KeyUp(key, 0, 0);
    };
    Hud::OpenMenu(UiScreen::Title);
    assert(w.isPaused && !w.mouseFiring);
    auto title = Hud::Controls();
    assert(title.front().action == UiAction::Start && title.size() == 5);
    Hud::OpenMenu(UiScreen::Options);
    auto slider = Hud::Controls().front();
    // Fit a 1280x800 canvas into a 1920x1080 window; input must share the same inverse transform.
    g.Resize(1920, 1080);
    auto click = [&](Vector2 p) {
        float scale = 1.35f;
        int x = int(96 + p.x * scale), y = int(p.y * scale);
        g.MouseButton(GLUT_LEFT_BUTTON, GLUT_DOWN, x, y);
        g.MouseButton(GLUT_LEFT_BUTTON, GLUT_UP, x, y);
    };
    click({620, slider.position.y + 24});
    assert(std::fabs(w.uiPreferences.background - .8f) < .001f && !w.mouseFiring);
    click({848, slider.position.y + 24});
    assert(std::fabs(w.uiPreferences.background - 1) < .001f);
    for (int i = 0; i < 30; ++i)
    {
        press('a');
    }
    assert(std::fabs(w.uiPreferences.background - .8f) < .001f);
    press('s');
    press('s'); // Content layer has a separate readability floor.
    for (int i = 0; i < 30; ++i)
    {
        press('a');
    }
    assert(std::fabs(w.uiPreferences.content - .85f) < .001f);
    g.Reset();
    assert(w.ui.screen == UiScreen::None && !w.isPaused && w.uiPreferences.content == .85f);
    w.uiPreferences = {};
    g.Resize(1280, 800);
    // Cancel a destructive action; entering the confirmation is not a reset.
    w.isSideQuestAccepted = true;
    Hud::OpenMenu(UiScreen::Pause);
    press('r');
    assert(w.ui.screen == UiScreen::Confirm && w.isSideQuestAccepted);
    press('s');
    press(13);
    assert(w.ui.screen == UiScreen::Pause && w.isSideQuestAccepted);
    press(27);
    assert(!w.isPaused && w.ui.screen == UiScreen::None);
    // A cancelled/disabled exchange consumes no goods, and valid purchases use live reputation pricing.
    auto &level = game::GameLevels::Chapter();
    level.Enter(true);
    level.GetState().convoyRescued = true;
    w.playerPosition = {24.5f, 20.5f};
    auto &stats = game::GameLevels::Mine().GetState().player;
    CivicJustice::State().infamy = 80;
    stats.salvage = 0;
    stats.recoveryKits = 1;
    Hud::OpenMenu(UiScreen::Supply);
    assert(!Hud::Controls().front().enabled);
    press(13);
    assert(stats.salvage == 0 && stats.recoveryKits == 1);
    stats.salvage = 100;
    int price = CivicJustice::ShopPrice(2);
    press('e');
    assert(stats.salvage == 100 - price && stats.recoveryKits == 2);
    g.KeyDown('e', 0, 0); // A fresh press buys once; OS repeat cannot buy again.
    g.KeyDown('e', 0, 0);
    assert(stats.salvage == 100 - price * 2 && stats.recoveryKits == 3);
    g.KeyUp('e', 0, 0);
    press(27);
    assert(w.ui.screen == UiScreen::None);
    g.Reset();
}
void TestDialogueProgress()
{
    using namespace tutorial;
    auto &game = Game::Get();
    auto &world = game.GetState();
    game.Reset();
    game.KeyDown('j', 0, 0);
    assert(world.journalVisible && Dialogue::LocksWorld());
    auto journal = Dialogue::Journal();
    for (const auto &entry : journal)
    {
        assert(entry.find("문양:") == std::string::npos && entry.find("N-0/17") == std::string::npos);
    }
    game.KeyUp('j', 0, 0);
    game.KeyDown('j', 0, 0);
    game.KeyUp('j', 0, 0);
    const std::string korean = "긴 한글 문장과 English words 123을 실제 글꼴 폭에 맞춰 누락 없이 읽는다.";
    auto rows = Dialogue::Wrap(korean, 135);
    std::string joined;
    for (const auto &row : rows)
    {
        assert(RenderAssets::MeasureUtf8(row, 19) <= 135);
        joined += row;
    }
    auto compact = [](std::string text) {
        text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
        return text;
    };
    assert(compact(joined) == compact(korean));
    int completions = 0;
    auto scene = [&]() {
        return Dialogue::Start("test/choice",
                               {{"first", "검증 화자", "단말 기록", korean + korean + korean + korean, {}},
                                {"choice",
                                 "리아",
                                 "현장",
                                 "출발 여부를 선택해 줘.",
                                 {{"depart", "출발한다"}, {"prepare", "아직 준비한다"}}}},
                               [&](const std::string &choice) {
                                   assert(choice == "prepare");
                                   ++completions;
                               });
    };
    assert(scene() && Dialogue::LocksWorld());
    size_t page = world.dialogue.page;
    world.mouseFiring = true;
    game.KeyDown('e', 0, 0);
    assert(world.dialogue.page == page + 1);
    game.KeyDown('e', 0, 0);
    assert(world.dialogue.page == page + 1); // Held E consumes exactly one page.
    game.KeyUp('e', 0, 0);
    while (world.dialogue.pages[world.dialogue.page].choices.empty())
    {
        assert(Dialogue::Advance());
    }
    assert(!Dialogue::Advance() && completions == 0);
    assert(Dialogue::Select(1) && completions == 0);
    auto savedPage = world.dialogue.page;
    game.ShowNotice("재장전 / 전투 알림은 대사 본문을 덮지 않는다.");
    assert(world.dialogue.page == savedPage && world.dialogue.selection == 1);
    game.KeyDown(27, 0, 0);
    assert(!Dialogue::Advance() && !world.dialogue.scenes.at("test/choice").choice.size());
    game.KeyDown(27, 0, 0);
    HospitalRecovery::Begin();
    assert(world.dialogue.suspended && !Dialogue::Active() && !Dialogue::Resume());
    for (int step = 0; step < 80; ++step)
    {
        HospitalRecovery::Update(.05f);
    }
    assert(!HospitalRecovery::Active());
    assert(world.ui.screen == UiScreen::Medical);
    game.KeyDown(13, 0, 0);
    game.KeyUp(13, 0, 0);
    game.KeyDown('j', 0, 0);
    game.KeyUp('j', 0, 0);
    game.KeyDown(13, 0, 0);
    assert(Dialogue::Active());
    assert(world.dialogue.page == savedPage && world.dialogue.selection == 1);
    game.KeyDown(13, 0, 0); // Resume must retain the Enter latch even when movement keys are cleared.
    assert(Dialogue::Active() && world.dialogue.page == savedPage && completions == 0);
    game.KeyUp(13, 0, 0);
    assert(Dialogue::Advance() && completions == 1);
    assert(world.dialogue.scenes.at("test/choice").choice == "prepare");
    assert(scene());
    while (world.dialogue.pages[world.dialogue.page].choices.empty())
    {
        assert(Dialogue::Advance());
    }
    assert(Dialogue::Select(1) && Dialogue::Advance() && completions == 1);
    auto history = world.dialogue.history.size();
    assert(history > 1);
    world.isInsideHome = false;
    CivicJustice::State().wanted = 1;
    assert(Dialogue::Start("test/radio", {{"radio", "리아", "무전", "안전 지점으로 이동해.", {}}}));
    assert(Dialogue::Danger() && !Dialogue::LocksWorld() && CivicCombat::Available());
    game.KeyDown('d', 0, 0);
    assert(world.pressedKeys['d']);
    ReadDialogue();
    assert(world.dialogue.history.size() == history + 1);
    game.Reset();
    world.isInsideHome = false;
    world.isInObservatory = true;
    world.playerPosition = kObservatoryConsole;
    game.InteractWithNearbyTarget();
    assert(world.gateSignalHeard && world.dialogue.pages.size() == 3);
    assert(world.dialogue.pages[0].speaker != world.dialogue.pages[1].speaker &&
           world.dialogue.pages[1].speaker != world.dialogue.pages[2].speaker);
    ReadDialogue();
    game.Reset();
}
void TestNpcFacing()
{
    tutorial::MovementFacing facing;
    for (Vector2 delta : {Vector2{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, -1}})
    {
        facing.Update({0, 0}, delta);
        assert(facing.walking);
        assert(std::fabs(facing.yaw - std::atan2(delta.y, delta.x)) < .00001f);
        float lastYaw = facing.yaw;
        facing.Update(delta, delta);
        assert(!facing.walking && facing.yaw == lastYaw);
    }
    ReadyMine();
    mineState.survivors.clear();
    RescueNpc npc{100, 0, {8.5f, 9.5f}};
    npc.evacuating = true;
    npc.route = {{9.5f, 9.5f}, {9.5f, 10.5f}};
    mineState.survivors.push_back(npc);
    mineState.riaPosition = {8.5f, 9.5f};
    mineState.riaRoute = npc.route;
    mineState.riaNavigationCooldown = 100;
    GameLevels::Mine().Rescue().Update(.1f);
    assert(mineState.survivors.front().facing.walking);
    assert(std::fabs(mineState.survivors.front().facing.yaw) < .0001f);
    assert(mineState.riaFacing.walking && std::fabs(mineState.riaFacing.yaw) < .0001f);
    for (int i = 0; i < 12; ++i)
    {
        GameLevels::Mine().Rescue().Update(.1f);
    }
    const auto &rescued = mineState.survivors.front();
    assert(!rescued.facing.walking && std::fabs(rescued.facing.yaw - tutorial::kPi / 2) < .0001f);
    assert(!mineState.riaFacing.walking && std::fabs(mineState.riaFacing.yaw - tutorial::kPi / 2) < .0001f);
}
void AimAt(Vector2 target)
{
    auto camera = tutorial::Game::Get().GetState().cameraPosition;
    tutorial::Game::Get().GetState().cursorPosition = tutorial::WorldCamera::ToViewport(
        {tutorial::kCanvasWidth * .5f + (target.x - target.y - camera.x + camera.y) * 36,
         tutorial::kCanvasHeight * .57f + (target.x + target.y - camera.x - camera.y) * 18 -
             models::WorldScale::aimHeightPixels});
}
void Step(float seconds)
{
    for (int i = 0; i < int(std::ceil(seconds / .025f)); ++i)
    {
        GameLevels::Mine().Update(.025f);
    }
}
void TestLevelLifecycle()
{
    GameLevels::Reset();
    assert(GameLevels::Active() == nullptr);
    ReadyMine();
    assert(GameLevels::Active() == &GameLevels::Mine());
    assert(GameLevels::Active()->UsesWalkingControls());
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
    static_assert(kMapSize * kMapSize == 4 * 64 * 64);
    std::vector<Tile> previous;
    for (unsigned seed = 0; seed < 30; ++seed)
    {
        ReadyMine(seed);
        if (!previous.empty())
        {
            assert(previous != mineState.tiles);
        }
        previous = mineState.tiles;
        GameLevels::Mine().Map().Generate(seed);
        assert(previous == mineState.tiles); // A reported seed reproduces the exact layout.
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kPowerBypass).empty());
        assert(GameLevels::Mine().Map().FindRoute(mineState.entry, kRuinSignal).empty());
        assert(!GameLevels::Mine().Map().IsWalkable(kMineGateX, kMineGateY));
        assert(mineState.survivors.size() == 12 && mineState.rescueGroups.size() == 4);
        assert(mineState.enemies.size() == kEnemyPopulation);
        assert(mineState.quest.Advance(MineQuestStage::BypassPower));
        assert(GameLevels::Mine().Map().IsWalkable(kMineGateX, kMineGateY));
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kRuinSignal).empty());
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kObserverGlyph).empty());
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kCoordinateTerminal).empty());
        for (const auto &npc : mineState.survivors)
        {
            assert(!GameLevels::Mine().Map().IsBlocked(npc.position));
            assert(!GameLevels::Mine().Map().FindRoute(npc.position, mineState.entry).empty());
        }
        tutorial::Game::Get().GetState().playerPosition = mineState.entry;
        GameLevels::Mine().Map().UpdateNavigation();
        for (int cell : mineState.reachableCells)
        {
            assert(mineState.navigationDistances[cell] >= 0);
        }
        int junctions = 0;
        for (int cell : mineState.reachableCells)
        {
            int x = cell % kMapSize, y = cell / kMapSize;
            int exits = 0;
            for (Vector2 d : {Vector2{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
            {
                exits += GameLevels::Mine().Map().IsWalkable(x + int(d.x), y + int(d.y)) ? 1 : 0;
            }
            junctions += exits >= 3 ? 1 : 0;
        }
        assert(junctions > 100 && mineState.reachableCells.size() > 3000);
    }
}
void TestConnectedOverworld()
{
    auto &game = tutorial::Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    std::array<int, tutorial::Overworld::kCityCount> population = {2, 0, 0, 0, 0};
    for (const auto &resident : world.residents)
    {
        ++population[resident.cityIndex];
        assert(!game.IsPositionBlocked(resident.position));
    }
    for (int count : population)
    {
        assert(count == tutorial::Overworld::kPopulationPerCity);
    }
    const auto &cities = tutorial::Overworld::Cities();
    const auto &network = tutorial::RoadNetwork::Get();
    std::vector<bool> reached(network.junctions.size(), false);
    std::vector<int> pending = {0};
    reached[0] = true;
    for (size_t i = 0; i < pending.size(); ++i)
    {
        for (int lane : network.junctions[pending[i]].outgoing)
        {
            int next = network.lanes[lane].to;
            if (!reached[next])
            {
                reached[next] = true;
                pending.push_back(next);
            }
        }
    }
    assert(std::all_of(reached.begin(), reached.end(), [](bool b) { return b; }));
    // City positions occupy different sectors around Halo, with reproducible organic offsets.
    assert(cities[1].center.x > 0 && cities[2].center.y > 0 && cities[3].center.x < 0 &&
           cities[4].center.y < 0);
    for (const auto &city : cities)
    {
        assert(city.center.x > tutorial::kWorldMinX + 50 && city.center.x < tutorial::kWorldMaxX - 50);
    }
    for (float yaw : {0.f, tutorial::kPi * .5f})
    {
        world.bikePosition = {70, 50};
        world.bikeYaw = yaw;
        tutorial::Overworld::ToggleBike(world);
        assert(world.isBikeMounted && GameLevels::Active() == nullptr);
        world.pressedKeys['w'] = true;
        for (int frame = 0; frame < 120; ++frame)
        {
            tutorial::Overworld::UpdateBike(world, 1.f / 60);
        }
        assert(tutorial::Game::Distance(world.playerPosition, Vector2{70, 50}) > 12);
        assert(tutorial::Game::Distance(world.playerPosition, Vector2{70, 50}) < 15);
        assert(std::fabs(world.bikeSpeed - tutorial::Overworld::kBikeCruiseSpeed) < .001f);
        world.pressedKeys['w'] = false;
        for (int frame = 0; frame < 30; ++frame)
        {
            tutorial::Overworld::UpdateBike(world, 1.f / 60);
        }
        auto parked = world.bikePosition;
        tutorial::Overworld::ToggleBike(world);
        assert(!world.isBikeMounted && world.bikeSpeed == 0);
        assert(tutorial::Game::Distance(parked, world.bikePosition) < .001f);
    }
    world.bikePosition = tutorial::Overworld::kMineEntrance;
    world.playerPosition = {world.bikePosition.x + 1.2f, world.bikePosition.y};
    assert(game.FindNearbyInteraction() == tutorial::InteractionTarget::MineEntrance);
    auto snapshot = monitoring::WorldSnapshot::Build(1, "overworld-test");
    for (const auto &city : cities)
    {
        assert(snapshot.find(std::string("\"id\":\"") + city.id + "\"") != std::string::npos);
    }
    game.Reset();
}
void TestCityQuestAccess()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    bool home = false, observatory = false;
    for (const auto &building : world.buildings)
    {
        assert(!CityLayout::OverlapsQuestSpace(building));
        home |= building.type == models::BuildingType::Habitat &&
                Game::Distance({building.x, building.y}, kHomeBuildingPosition) < .001f;
        observatory |= building.type == models::BuildingType::Observatory &&
                       Game::Distance({building.x, building.y}, kObservatoryBuildingPosition) < .001f;
    }
    assert(home && observatory);
    for (const auto &resident : world.residents)
    {
        assert(!CityLayout::IsQuestSpace(resident.position, .5f));
    }
    auto clear = [&](Vector2 position) {
        for (float dx : {-.65f, 0.f, .65f})
        {
            for (float dy : {-.65f, 0.f, .65f})
            {
                assert(!game.IsPositionBlocked({position.x + dx, position.y + dy}));
            }
        }
    };
    for (Vector2 point : {kHomeDoor, kLiaPosition, kMaraPosition, kRelayPosition, kBeaconPosition,
                          kCityReceiverPosition, kCityKainPosition, kJetBikePosition})
    {
        clear(point);
        assert(CityLayout::IsQuestSpace(point));
        assert(!RoadNetwork::Get().Contains(point));
    }
    // Verify the actual approach paths, rather than only testing individual spawn centers.
    const Vector2 route[] = {kHomeDoor,        kLiaPosition, kMaraPosition,         kRelayPosition,
                             kBeaconPosition,  kLiaPosition, kCityReceiverPosition, {kJetBikePosition.x, -3},
                             kJetBikePosition, kBikeRoadExit};
    for (size_t i = 1; i < std::size(route); ++i)
    {
        int steps = int(std::ceil(Game::Distance(route[i - 1], route[i]) / .1f));
        for (int step = 0; step <= steps; ++step)
        {
            float t = float(step) / steps;
            clear({route[i - 1].x + (route[i].x - route[i - 1].x) * t,
                   route[i - 1].y + (route[i].y - route[i - 1].y) * t});
        }
    }
    game.Reset();
    // Follow the production interaction sequence through the relocated outdoor targets.
    world.playerPosition = {1, -1.2f};
    game.InteractWithNearbyTarget();
    ReadDialogue();
    world.playerPosition = {2.4f, 2.4f};
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(!world.isInsideHome && Game::Distance(world.playerPosition, kHomeDoor) < .001f);
    auto interact = [&](Vector2 position, InteractionTarget target) {
        world.playerPosition = position;
        assert(game.FindNearbyInteraction() == target);
        game.InteractWithNearbyTarget();
        ReadDialogue();
    };
    interact(kLiaPosition, InteractionTarget::Lia);
    interact(kMaraPosition, InteractionTarget::Mara);
    interact(kRelayPosition, InteractionTarget::PowerRelay);
    interact(kMaraPosition, InteractionTarget::Mara);
    assert(world.isSideQuestComplete);
    interact(kBeaconPosition, InteractionTarget::GateBeacon);
    assert(world.isInObservatory);
    interact(kObservatoryConsole, InteractionTarget::ObservatorySignal);
    interact(kObservatoryStairs, InteractionTarget::ObservatoryExit);
    assert(!world.isInObservatory && Game::Distance(world.playerPosition, kBeaconPosition) < .001f);
    interact(kLiaPosition, InteractionTarget::Lia);
    interact(kCityReceiverPosition, InteractionTarget::CityReceiver);
    assert(mineState.quest.GetStage() == MineQuestStage::TravelToMine);
    assert(Game::Distance(world.bikePosition, kJetBikePosition) < .001f && world.bikeYaw == kJetBikeYaw);
    interact({kJetBikePosition.x + 1.2f, kJetBikePosition.y}, InteractionTarget::JetBike);
    assert(world.isBikeMounted);
    world.pressedKeys['w'] = true;
    for (int frame = 0; frame < 120 && world.playerPosition.y > kBikeRoadExit.y; ++frame)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(world.playerPosition.y <= kBikeRoadExit.y && world.bikeSpeed > 0);
    world.pressedKeys['w'] = false;
    for (int frame = 0; frame < 30; ++frame)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(!game.IsPositionBlocked(world.bikePosition)); // Braking may coast onto the open shoulder.
    auto parked = world.bikePosition;
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(!world.isBikeMounted && Game::Distance(parked, world.bikePosition) < .001f);
    assert(!game.IsPositionBlocked(world.playerPosition));
    game.Reset();
}
void TestImportedWorldAndFlights()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    const auto &props = Overworld::Scenery().Props();
    assert(props.size() > 100);
    size_t pads = 0, parked = 0;
    for (const auto &prop : props)
    {
        assert(prop.model && prop.model->resourceId);
        assert(
            !CityLayout::OverlapsQuestSpace({prop.minimum.x, prop.minimum.y, prop.maximum.x - prop.minimum.x,
                                             prop.maximum.y - prop.minimum.y, 0},
                                            .7f));
        pads += prop.type == models::SceneryType::LandingPad;
        parked += prop.type == models::SceneryType::Count;
        if (prop.solid)
        {
            // An open shelter's AABB centre is walkable. Probe an actual low body surface
            // instead of incorrectly requiring its empty centre/canopy to be solid.
            const auto &mesh = models::ResourceAssets::Get(prop.model->resourceId);
            for (const auto &vertex : mesh.vertices)
            {
                if (vertex.z > .05f && vertex.z < .9f)
                {
                    Vector2 point = {
                        prop.position.x + vertex.x * std::cos(prop.yaw) - vertex.y * std::sin(prop.yaw),
                        prop.position.y + vertex.x * std::sin(prop.yaw) + vertex.y * std::cos(prop.yaw)};
                    assert(game.IsPositionBlocked(point));
                    break;
                }
            }
        }
    }
    assert(pads == Overworld::kCityCount && parked == pads);
    for (const auto &person : world.residents)
    {
        assert(!game.IsPositionBlocked(person.position));
    }
    for (int route = 0; route < int(RoadNetwork::Get().airCorridors.size()); ++route)
    {
        for (float t : {-.1f, 0.f, .1f, .3f, .5f, .7f, .9f, 1.f, 1.1f})
        {
            auto tangent = AirTraffic::Tangent(route, t);
            assert(std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y) > 30);
            assert(Game::Distance(AirTraffic::Position(route, t - .0001f),
                                  AirTraffic::Position(route, t + .0001f)) < .3f);
        }
    }
    auto &traffic = Overworld::Flights();
    std::unordered_map<std::uint64_t, AircraftActor> previous;
    bool visible = false;
    unsigned int variants = 0;
    for (int frame = 0; frame < 3600; ++frame)
    {
        traffic.Update(world, 1.f / 60);
        assert(traffic.Aircraft().size() <= AirTraffic::kAircraftLimit);
        std::unordered_map<std::uint64_t, AircraftActor> current;
        for (const auto &craft : traffic.Aircraft())
        {
            variants |= 1u << (unsigned(craft.type) * 2 + (craft.alternate ? 1 : 0));
            auto old = previous.find(craft.id);
            if (old == previous.end())
            {
                assert(TrafficSystem::OutsideView(craft.position, world.cameraPosition, 270, craft.altitude));
            }
            else
            {
                assert(Game::Distance(old->second.position, craft.position) < .8f);
            }
            visible |= !TrafficSystem::OutsideView(craft.position, world.cameraPosition, 0, craft.altitude);
            assert(std::fabs(craft.bank) <= .181f);
            current.emplace(craft.id, craft);
        }
        for (const auto &old : previous)
        {
            if (!current.count(old.first))
            {
                assert(Game::Distance(old.second.position, world.playerPosition) > 179);
                assert(TrafficSystem::OutsideView(old.second.position, world.cameraPosition, 330,
                                                  old.second.altitude));
                // The last sampled hull AND shadow must already be outside before despawn.
                assert(AirTraffic::OutsideVisibleFootprint(old.second, world.cameraPosition, 300));
            }
        }
        previous.swap(current);
    }
    assert(visible && variants == 255);
    game.Reset();
}
void TestUrbanTraffic()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.cameraPosition = world.playerPosition = {0, 0};
    const auto &network = RoadNetwork::Get();
    bool two = false, four = false, elevated = false, inclined = false;
    for (const auto &road : network.roads)
    {
        two |= road.lanes == 2;
        four |= road.lanes == 4;
        elevated |= network.junctions[road.from].altitude > 1 || network.junctions[road.to].altitude > 1;
        assert(road.lanes <= 4);
        auto a = network.junctions[road.from].position, b = network.junctions[road.to].position;
        inclined |= std::fabs(a.x - b.x) > 2 && std::fabs(a.y - b.y) > 2;
    }
    assert(two && four && elevated && inclined && network.airCorridors.size() == 4);
    assert(!TransitLayout::Get().empty());
    for (const auto &module : TransitLayout::Get())
    {
        assert(models::ModelLibrary::Transit(module.asset).resourceId);
    }
    for (const auto &lane : network.lanes)
    {
        assert(lane.length > 3.5f && std::fabs(Game::Distance(lane.start, lane.end) - lane.length) < .001f);
        auto mid = network.LanePosition(int(&lane - network.lanes.data()), lane.length * .5f);
        assert(network.ContainsSurface(mid, network.RoadElevation(lane.road, .5f)));
        auto origin = network.junctions[lane.from].position;
        auto right = RoadNetwork::RightOf(lane.direction);
        assert((mid.x - origin.x) * right.x + (mid.y - origin.y) * right.y > 0);
        // Test the displayed driving side independently of RightOf's implementation.
        auto screenDirection =
            Vector2{(lane.direction.x - lane.direction.y) * 36, (lane.direction.x + lane.direction.y) * 18};
        auto screenOffset = Vector2{(right.x - right.y) * 36, (right.x + right.y) * 18};
        assert(screenDirection.x * screenOffset.y - screenDirection.y * screenOffset.x > 0);
    }
    assert(!network.Contains(kLiaPosition) && !network.Contains(kMaraPosition));
    assert(TrafficSystem::SweptHit({-5, 0}, {5, 0}, 0, {0, 0}));
    assert(!TrafficSystem::SweptHit({-5, 0}, {5, 0}, 0, {0, 2}));
    assert(TrafficSystem::SweptHit({0, -5}, {0, 5}, kPi * .5f, {0, 0}));
    auto &traffic = Overworld::Traffic();
    std::unordered_map<std::uint64_t, TrafficVehicle> previous;
    bool sawTurn = false, sawStop = false, sawWalk = false, sawCar = false;
    bool sawSingleWalk = false, sawAxisWalk = false, sawRedRight = false;
    for (int frame = 0; frame < 7200; ++frame)
    {
        traffic.Update(world, 1.f / 60);
        assert(traffic.Vehicles().size() <= TrafficSystem::kVehicleLimit);
        std::unordered_map<std::uint64_t, TrafficVehicle> current;
        for (const auto &car : traffic.Vehicles())
        {
            sawCar = true;
            assert(network.ContainsSurface(
                car.position, traffic.RenderElevation(size_t(&car - traffic.Vehicles().data())), .25f));
            auto old = previous.find(car.id);
            if (old == previous.end())
            {
                assert(TrafficSystem::OutsideView(
                    car.position, world.cameraPosition, 230,
                    traffic.RenderElevation(size_t(&car - traffic.Vehicles().data()))));
            }
            else
            {
                assert(Game::Distance(old->second.position, car.position) < .8f);
                if (old->second.lane == car.lane && old->second.reservedFrom != car.lane &&
                    car.reservedFrom == car.lane && network.IsSignalized(network.lanes[car.lane].to))
                {
                    auto light = traffic.Light(car.lane, car.turn);
                    if (car.turn == TrafficTurn::Right && light == TrafficLight::Red)
                    {
                        assert(!traffic.HasRightTurnSignal(car.lane));
                        assert(old->second.stoppedSeconds >= .75f && car.speed <= 1.51f);
                        sawRedRight = true;
                    }
                    else
                    {
                        assert(light == TrafficLight::Green);
                    }
                }
                auto rendered = traffic.RenderPosition(size_t(&car - traffic.Vehicles().data()));
                assert(Game::Distance(rendered, car.position) < .8f);
            }
            sawTurn |= car.turnDistance >= 0 && car.turn != TrafficTurn::Straight;
            sawStop |= car.speed < .05f && traffic.Light(car.lane, car.turn) == TrafficLight::Red;
            current.emplace(car.id, car);
        }
        for (const auto &old : previous)
        {
            if (current.count(old.first) == 0)
            {
                assert(Game::Distance(old.second.position, world.playerPosition) >
                       TrafficSystem::kDespawnDistance - 1);
                assert(TrafficSystem::OutsideView(old.second.position, world.cameraPosition, 300));
            }
        }
        for (size_t j = 0; j < network.junctions.size(); ++j)
        {
            size_t arms = 0;
            for (int lane : network.junctions[j].outgoing)
            {
                arms += network.lanes[lane].slot == 0;
            }
            if (arms < 3 || !traffic.PedestrianGreen(int(j)))
            {
                continue;
            }
            sawWalk = true;
            int greens = 0, width = 0;
            for (int outgoing : network.junctions[j].outgoing)
            {
                if (network.lanes[outgoing].slot != 0)
                {
                    continue;
                }
                int road = network.lanes[outgoing].road;
                width = (std::max)(width, network.roads[road].lanes);
                greens += traffic.PedestrianGreen(int(j), road);
                if (!traffic.PedestrianGreen(int(j), road))
                {
                    assert(!traffic.CanStartCrossing(int(j), 1, road));
                }
            }
            sawSingleWalk |= width == 2 && greens == 1;
            sawAxisWalk |= width == 4 && greens > 0 && greens < int(arms);
            for (size_t lane = 0; lane < network.lanes.size(); ++lane)
            {
                if (network.lanes[lane].to != int(j))
                {
                    continue;
                }
                for (auto turn : {TrafficTurn::Straight, TrafficTurn::Left, TrafficTurn::Right})
                {
                    assert(traffic.Light(int(lane), turn) == TrafficLight::Red);
                }
            }
        }
        previous.swap(current);
    }
    assert(sawCar && sawTurn && sawStop && sawWalk);
    if (!sawSingleWalk || !sawAxisWalk)
    {
        std::cerr << "walk coverage " << sawSingleWalk << " " << sawAxisWalk << std::endl;
        for (size_t j = 0; j < network.junctions.size(); ++j)
        {
            if (network.IsSignalized(int(j)))
            {
                std::cerr << "junction " << j << " at " << network.junctions[j].position.x << ","
                          << network.junctions[j].position.y << " phase " << int(traffic.Signals()[j].phase)
                          << " elapsed " << traffic.Signals()[j].elapsed << std::endl;
            }
        }
    }
    assert(sawSingleWalk && sawAxisWalk);
    // sawWalk above must occur without any key or crossing request.
    // Production impact path, using an approaching moving vehicle rather than a damage mock.
    auto hit =
        std::find_if(traffic.Vehicles().begin(), traffic.Vehicles().end(), [&](const TrafficVehicle &car) {
            return car.speed > 1 && traffic.RenderElevation(size_t(&car - traffic.Vehicles().data())) < .1f;
        });
    assert(hit != traffic.Vehicles().end());
    world.playerPosition = hit->position;
    mineState.player.health = mineState.player.maxHealth;
    traffic.Update(world, .05f);
    assert(mineState.player.health < mineState.player.maxHealth);
    game.Reset();
}
void TestCityPlanAndSignalProgress()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    const auto &net = RoadNetwork::Get();
    assert(CityPlan::Connections().size() == world.buildings.size());
    assert(!CityPlan::Walkways().empty());
    for (const auto &connection : CityPlan::Connections())
    {
        assert(connection.path.size() >= 2);
        for (size_t segment = 1; segment < connection.path.size(); ++segment)
        {
            auto a = connection.path[segment - 1], b = connection.path[segment];
            int samples = int(std::ceil(Game::Distance(a, b) * 6)) + 1;
            for (int step = 0; step <= samples; ++step)
            {
                Vector2 p = {a.x + (b.x - a.x) * step / samples, a.y + (b.y - a.y) * step / samples};
                assert(!net.Contains(p, .1f));
                assert(!Overworld::Scenery().IsBlocked(p, .15f));
                for (const auto &building : world.buildings)
                {
                    assert(!(p.x > building.x - .2f && p.x < building.x + building.width + .2f &&
                             p.y > building.y - .2f && p.y < building.y + building.depth + .2f));
                }
            }
        }
    }
    auto home = std::find_if(world.buildings.begin(), world.buildings.end(), [](const Building &b) {
        return b.x == kHomeBuildingPosition.x && b.y == kHomeBuildingPosition.y;
    });
    assert(home != world.buildings.end());
    assert(world.bikePosition.x > home->x + home->width &&
           world.bikePosition.x < home->x + home->width + 1.6f);
    assert(world.bikePosition.y > home->y - 1 && world.bikePosition.y < home->y + home->depth + 1);
    world.playerPosition = world.cameraPosition = {-425, -425};
    world.residents.clear();
    Resident lingering;
    lingering.crossingJunction = 0;
    while (!net.IsSignalized(lingering.crossingJunction))
    {
        ++lingering.crossingJunction;
    }
    lingering.position = net.junctions[lingering.crossingJunction].position;
    world.residents.push_back(lingering);
    std::vector<int> changes(net.junctions.size(), 0);
    std::vector<SignalPhase> phases;
    for (const auto &signal : Overworld::Traffic().Signals())
    {
        phases.push_back(signal.phase);
    }
    std::vector<bool> crossingServed(net.roads.size() * 2, false);
    for (int frame = 0; frame < 18000; ++frame)
    {
        Overworld::Traffic().Update(world, 1.f / 60);
        for (size_t j = 0; j < phases.size(); ++j)
        {
            if (!net.IsSignalized(int(j)))
            {
                continue;
            }
            if (frame < 7200)
            {
                for (int lane : net.junctions[j].outgoing)
                {
                    if (net.lanes[lane].slot == 0 &&
                        Overworld::Traffic().PedestrianGreen(int(j), net.lanes[lane].road))
                    {
                        int road = net.lanes[lane].road;
                        crossingServed[road * 2 + (net.roads[road].from == int(j) ? 0 : 1)] = true;
                    }
                }
            }
            const auto &signal = Overworld::Traffic().Signals()[j];
            assert(signal.elapsed <= Overworld::Traffic().PhaseDuration(int(j)) + .1f);
            if (signal.phase != phases[j])
            {
                ++changes[j];
                phases[j] = signal.phase;
            }
        }
    }
    for (size_t j = 0; j < changes.size(); ++j)
    {
        if (net.IsSignalized(int(j)))
        {
            assert(changes[j] >= 14);
        }
    }
    for (size_t road = 0; road < net.roads.size(); ++road)
    {
        if (net.IsSignalized(net.roads[road].from))
        {
            assert(crossingServed[road * 2]);
        }
        if (net.IsSignalized(net.roads[road].to))
        {
            assert(crossingServed[road * 2 + 1]);
        }
    }
    game.Reset();
}
void TestVehicleQueueProgress()
{
    using namespace tutorial;
    auto &game = Game::Get();
    auto &traffic = Overworld::Traffic();
    const auto &net = RoadNetwork::Get();
    for (int city : {0, 1})
    {
        game.Reset();
        auto &world = game.GetState();
        world.isInsideHome = false;
        world.residents.clear();
        world.playerPosition = world.cameraPosition = Overworld::Cities()[city].center;
        for (int frame = 0; frame < 9000; ++frame)
        {
            traffic.Update(world, 1.f / 30);
            for (size_t j = 0; j < traffic.Signals().size(); ++j)
            {
                if (net.IsSignalized(int(j)))
                {
                    if (traffic.Signals()[j].elapsed >= traffic.PhaseDuration(int(j)) + 20)
                    {
                        std::cerr << "Held junction " << j << " at " << net.junctions[j].position.x << ","
                                  << net.junctions[j].position.y << " phase "
                                  << int(traffic.Signals()[j].phase) << std::endl;
                        for (const auto &car : traffic.Vehicles())
                        {
                            if (car.reservedFrom >= 0 && net.lanes[car.reservedFrom].to == int(j))
                            {
                                std::cerr << "car " << car.id << " lane " << car.lane << " next "
                                          << car.nextLane << " dist " << car.distance << " length "
                                          << net.lanes[car.lane].length << " speed " << car.speed << " turn "
                                          << car.turnDistance << " reservation " << car.reservedFrom << " -> "
                                          << car.reservedTo << std::endl;
                            }
                        }
                    }
                    assert(traffic.Signals()[j].elapsed < traffic.PhaseDuration(int(j)) + 20);
                }
            }
            for (const auto &car : traffic.Vehicles())
            {
                assert(net.ContainsSurface(
                    car.position, traffic.RenderElevation(size_t(&car - traffic.Vehicles().data())), .25f));
                if (car.speed > .01f || car.turnDistance >= 0 || car.distance < 1 ||
                    car.distance > net.lanes[car.lane].length - 5)
                {
                    continue;
                }
                bool queued = std::any_of(
                    traffic.Vehicles().begin(), traffic.Vehicles().end(), [&](const TrafficVehicle &leader) {
                        return leader.id != car.id && leader.lane == car.lane && leader.turnDistance < 0 &&
                               leader.distance > car.distance && leader.distance - car.distance < 4.1f;
                    });
                assert(queued); // A free approach must keep moving toward its marked stop line.
            }
        }
    }
    game.Reset();
}
void TestCityResidentsAndShadowLifetime()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.playerPosition = world.cameraPosition = {0, 0};
    assert(CityResidents::Get().NavigationNodeCount() > 1000);
    const auto initial = world.residents;
    bool moved = false, patrol = false, waiting = false, crossed = false, worked = false;
    for (int frame = 0; frame < 14400; ++frame)
    {
        Overworld::Traffic().Update(world, 1.f / 60);
        CityResidents::Get().Update(world, 1.f / 60);
        for (size_t i = 0; i < world.residents.size(); ++i)
        {
            const auto &resident = world.residents[i];
            moved |= Game::Distance(initial[i].position, resident.position) > 2;
            patrol |= resident.activity == ResidentActivity::Patrolling;
            waiting |= resident.activity == ResidentActivity::WaitingSignal;
            crossed |= resident.activity == ResidentActivity::Crossing;
            worked |= resident.activity == ResidentActivity::Working;
            assert(resident.routeCursor <= resident.route.size());
            assert(!Overworld::Scenery().IsBlocked(resident.position, .2f));
            assert(!CityLayout::IsQuestSpace(resident.position, .2f));
            if (resident.activity == ResidentActivity::Crossing)
            {
                const auto &signal = Overworld::Traffic().Signals()[resident.crossingJunction];
                if (signal.phase != SignalPhase::Walk && signal.phase != SignalPhase::WalkClear)
                {
                    for (const auto &car : Overworld::Traffic().Vehicles())
                    {
                        assert(car.reservedFrom < 0 ||
                               RoadNetwork::Get().lanes[car.reservedFrom].to != resident.crossingJunction);
                    }
                }
            }
            else
            {
                assert(!RoadNetwork::Get().Contains(resident.position, .2f));
            }
        }
    }
    assert(moved && patrol && waiting && crossed && worked);
    // A high hull can be offscreen while its projected shadow remains centered in the view.
    AircraftActor craft;
    craft.altitude = 200;
    craft.position = {-craft.altitude * .38f, -craft.altitude * .55f};
    assert(TrafficSystem::OutsideView(craft.position, {0, 0}, 0, craft.altitude));
    assert(!AirTraffic::OutsideVisibleFootprint(craft, {0, 0}));
    craft.position = {1000, 1000};
    assert(AirTraffic::OutsideVisibleFootprint(craft, {0, 0}));
    game.Reset();
}
void TestAimLightingAndWallShots()
{
    ReadyMine();
    // Combat fixtures are explicit; procedural topology is covered separately by TestMapAndGate.
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.lights.clear();
    for (int y = 0; y < kMapSize; ++y)
    {
        if (!GameLevels::Mine().Map().IsGateCell(kMineGateX, y))
        {
            mineState.tiles[y * kMapSize + kMineGateX] = Tile::Wall;
        }
    }
    mineState.enemies.clear();
    tutorial::Game::Get().GetState().playerPosition = {24.5f, 17.5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    AimAt({24.5f, 23.5f});
    auto direction = MineLevel::GetAimDirection(GameLevels::Mine().GetAimAngle());
    assert(std::fabs(direction.x) < .0001f && direction.y > .99f);
    assert(GameLevels::Mine().Lighting().At({24.5f, 21.5f}) > .3f);
    assert(GameLevels::Mine().Lighting().At({24.5f, 13.5f}) < .01f);
    GameLevels::Mine().Combat().Attack();
    assert(mineState.bullets.size() == 1);
    auto muzzle = models::ModelRig::Muzzle(models::ModelRig::Pose(0, false, true, 1));
    // Wall-safe muzzle advance follows the aim ray, before spread is applied.
    float muzzleDistance = std::hypot(muzzle.x, muzzle.y) * models::WorldScale::characterScale;
    assert(std::fabs(mineState.bullets[0].position.x - 24.5f) < .0001f);
    assert(std::fabs(mineState.bullets[0].position.y - 17.5f - muzzleDistance) < .0001f);
    mineState.bullets.clear();
    mineState.pistol.Reset();
    tutorial::Game::Get().GetState().playerPosition = {kMineGateX - 2.5f, kMineGateY + .5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    Vector2 beyondGate = {kMineGateX + 3.5f, kMineGateY + .5f};
    AimAt(beyondGate);
    mineState.enemies.push_back({beyondGate});
    assert(GameLevels::Mine().Lighting().At(beyondGate) == 0);
    GameLevels::Mine().Combat().Attack();
    Step(.3f);
    assert(mineState.enemies[0].health == kEnemyMaxHealth);
    assert(mineState.bullets.empty());
    assert(mineState.quest.Advance(MineQuestStage::BypassPower));
    assert(GameLevels::Mine().Lighting().At(beyondGate) > .1f);
    tutorial::Game::Get().GetState().playerPosition = {20.5f, 10.5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    mineState.enemies = {{{23.f, 10.5f}}};
    mineState.pistol.Reset();
    for (int i = 0; i < 3; ++i)
    {
        AimAt(mineState.enemies[0].position);
        GameLevels::Mine().Combat().Attack();
        Step(.225f);
    }
    assert(mineState.enemies[0].health == 0);
    assert(mineState.kills == 1 && mineState.player.experience == kExperiencePerKill);
    assert(mineState.drops.size() == 1);
}
void TestRescueAndScanner()
{
    ReadyMine();
    mineState.enemies.clear();
    tutorial::Game::Get().GetState().playerPosition = mineState.rescueGroups[0].position;
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    GameLevels::Mine().Rescue().Scan();
    assert(mineState.scannerSeconds > 0 && !mineState.scannerContacts.empty());
    float cooldown = mineState.scannerCooldown;
    GameLevels::Mine().Rescue().Scan();
    assert(mineState.scannerCooldown == cooldown);
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(mineState.rescueGroups[0].rescued && mineState.rescuedPeople == 3);
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(mineState.rescuedPeople == 3);
    for (const auto &npc : mineState.survivors)
    {
        if (npc.group == 0)
        {
            assert(npc.evacuating && !npc.route.empty());
        }
    }
    Step(3.f);
    assert(mineState.scannerSeconds == 0);
    assert(mineState.survivors.size() == 12); // Visible evacuees are never erased early.
    tutorial::Game::Get().GetState().playerPosition = {52.5f, 51.5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    Step(3.f);
    assert(mineState.survivors.size() == 9 && mineState.returnedPeople == 3);
}
void TestScannerPropagation()
{
    ReadyMine();
    auto &level = GameLevels::Mine();
    auto &world = tutorial::Game::Get().GetState();
    const Vector2 origin = world.playerPosition;
    mineState.survivors.clear();
    mineState.riaNavigationCooldown = 1000;
    mineState.rescueGroups = {{{origin.x + 15, origin.y}, false},
                              {{origin.x + 30, origin.y}, true},
                              {{origin.x + 43, origin.y}, false}};
    level.Rescue().Scan();
    assert(mineState.scannerRadius == 0 && mineState.scannerContacts.empty());
    level.Rescue().Update(.5f);
    assert(std::fabs(mineState.scannerRadius - 7.5f) < .001f && mineState.scannerContacts.empty());
    level.Rescue().Update(.51f);
    assert(mineState.scannerContacts.size() == 1 && mineState.scannerContacts.front().group == 0);
    assert(std::fabs(mineState.scannerContacts.front().secondsRemaining - 4.99f) < .001f);
    world.playerPosition = {origin.x + 80, origin.y + 80};
    level.Rescue().Scan(); // Cooldown cannot move or restart a wave already in flight.
    assert(tutorial::Game::Distance(mineState.scannerOrigin, origin) < .001f);
    level.Rescue().Update(1.01f);
    assert(std::fabs(mineState.scannerRadius - 30.3f) < .001f && mineState.scannerContacts.size() == 2);
    level.Rescue().Update(1);
    assert(mineState.scannerSeconds == 0 && mineState.scannerRadius == kScannerRange);
    assert(mineState.scannerContacts.size() == 2); // The pulse never detects the 43-unit transmitter.
    level.Rescue().Update(3.1f);
    assert(mineState.scannerContacts.size() == 1 && mineState.scannerContacts.front().group == 1);
    level.Rescue().Update(1);
    assert(mineState.scannerContacts.empty());

    world.playerPosition = origin;
    level.Rescue().Scan();
    level.Rescue().Update(1.1f);
    assert(mineState.scannerContacts.size() == 1);
    mineState.rescueGroups[0].rescued = true;
    level.Rescue().Update(0);
    assert(mineState.scannerContacts.empty());

    // A long frame ages discoveries from their arrival time rather than granting five fresh seconds.
    mineState.scannerCooldown = 0;
    mineState.rescueGroups[0].rescued = false;
    level.Rescue().Scan();
    level.Rescue().Update(6.2f);
    assert(mineState.scannerSeconds == 0 && mineState.scannerContacts.size() == 1);
    assert(mineState.scannerContacts.front().group == 1 &&
           std::fabs(mineState.scannerContacts.front().secondsRemaining - .8f) < .001f);
    level.Rescue().Update(1);
    assert(mineState.scannerContacts.empty());
}
void TestNpcCombat()
{
    ReadyMine();
    auto &level = GameLevels::Mine();
    auto &world = tutorial::Game::Get().GetState();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.survivors.clear();
    mineState.riaPosition = mineState.entry;
    mineState.rescueGroups[0].position = {15.5f, 15.5f};
    mineState.rescueGroups[0].combatants = true;
    mineState.survivors.push_back({100, 0, {15.5f, 15.5f}});
    auto &npc = mineState.survivors.front();
    mineState.enemies = {{{16.f, 15.5f}}};
    int playerHealth = mineState.player.health;
    level.Combat().UpdateEnemies(.05f);
    assert(npc.combat.health == 88 && mineState.player.health == playerHealth);
    assert(mineState.enemies.front().target.x == npc.position.x);
    // A guard returns fire with its own magazine. NPC kills grant no player rewards.
    mineState.enemies = {{{18.5f, 15.5f}}};
    int xp = mineState.player.experience;
    level.Combat().UpdateNpcs(.05f);
    assert(npc.combat.engaged && npc.combat.pistol.GetRounds() == 11);
    assert(!mineState.bullets.empty() && !mineState.bullets.front().playerOwned);
    for (int i = 0; i < 80; ++i)
    {
        level.Combat().UpdateNpcs(.05f);
        level.Combat().Update(.05f);
    }
    assert(mineState.enemies.front().health == 0 && mineState.totalEnemyDeaths == 1);
    assert(mineState.kills == 0 && mineState.player.experience == xp && mineState.drops.empty());
    mineState.enemies = {{{18.5f, 15.5f}, 5000}};
    bool reloaded = false, reloading = false;
    int previous = npc.combat.pistol.GetRounds();
    for (int i = 0; i < 400; ++i)
    {
        level.Combat().UpdateNpcs(.05f);
        level.Combat().Update(.05f);
        int rounds = npc.combat.pistol.GetRounds();
        reloading |= npc.combat.pistol.IsReloading();
        reloaded |= rounds > previous;
        previous = rounds;
    }
    assert(reloading && reloaded && mineState.enemies.front().health < 5000);
    // A wall blocks both guard fire and monster acquisition.
    npc.combat.pistol.Reset();
    npc.combat.cooldown = 0;
    for (int y = 0; y < kMapSize; ++y)
    {
        mineState.tiles[y * kMapSize + 17] = Tile::Wall;
    }
    mineState.bullets.clear();
    level.Combat().UpdateNpcs(.1f);
    assert(!npc.combat.engaged && npc.combat.pistol.GetRounds() == 12);
    int health = npc.combat.health;
    level.Combat().UpdateEnemies(.1f);
    assert(npc.combat.health == health);
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    // Miners retreat and defend at close range without a firearm.
    mineState.rescueGroups[0].combatants = false;
    npc.position = {15.5f, 15.5f};
    mineState.enemies = {{{16.f, 15.5f}}};
    npc.combat.cooldown = 0;
    level.Combat().UpdateNpcs(.1f);
    assert(npc.combat.evading && npc.facing.walking && npc.position.x < 15.5f);
    assert(mineState.enemies.front().health == 32 && npc.combat.pistol.GetRounds() == 12);
    // Downed people stop walking and can recover or receive aid without losing rescue state.
    npc.combat.health = 12;
    npc.combat.hitGrace = 0;
    mineState.enemies = {{{npc.position.x + .2f, npc.position.y}}};
    level.Combat().UpdateEnemies(.1f);
    assert(npc.combat.health == 0 && npc.combat.downedSeconds > 0);
    auto position = npc.position;
    npc.evacuating = true;
    npc.route = {{15.5f, 14.5f}};
    level.Rescue().Update(.1f);
    assert(tutorial::Game::Distance(position, npc.position) < .001f);
    level.Combat().UpdateNpcs(12.1f);
    assert(npc.combat.health == 50 && npc.evacuating);
    npc.combat.health = 0;
    npc.combat.downedSeconds = 12;
    world.playerPosition = npc.position;
    assert(level.Rescue().TryInteract() && npc.combat.health == 50);
    npc.evacuating = false;
    npc.combat.health = 0;
    npc.combat.downedSeconds = 12;
    assert(!mineState.rescueGroups[0].rescued);
    assert(level.Rescue().TryInteract());
    assert(mineState.rescueGroups[0].rescued && npc.evacuating && npc.combat.health == 50 &&
           !npc.route.empty());
    // Player projectiles never turn a rescue NPC into a PK target.
    mineState.enemies.clear();
    mineState.bullets = {{{npc.position.x - 1, npc.position.y}, {1, 0}, 30, 5, 100, true}};
    level.Combat().Update(.1f);
    assert(npc.combat.health == 50);
    // Ria can also be attacked independently of the player.
    world.playerPosition = mineState.entry;
    mineState.survivors.clear();
    mineState.riaPosition = {20.5f, 20.5f};
    mineState.enemies = {{{21.f, 20.5f}}};
    level.Combat().UpdateEnemies(.1f);
    assert(mineState.riaCombat.health == 88);
    mineState.riaPosition = mineState.entry;
    mineState.survivors.push_back({101, 0, {mineState.entry.x + 1, mineState.entry.y}});
    mineState.enemies = {{{mineState.entry.x + 1.5f, mineState.entry.y}}};
    level.Combat().UpdateEnemies(.1f);
    assert(mineState.survivors.front().combat.health == 100 && !mineState.enemies.front().facing.walking);
    // NPC muzzle flashes reveal their source in darkness and still obey walls.
    mineState.lights.clear();
    mineState.tracers.clear();
    mineState.muzzleFlash = 0;
    auto &flashingNpc = mineState.survivors.front();
    flashingNpc.position = {40.5f, 40.5f};
    flashingNpc.combat.muzzleFlash = .05f;
    assert(level.Lighting().At(flashingNpc.position) > .99f);
    assert(level.Lighting().At({42.5f, 40.5f}) > .5f);
    mineState.tiles[40 * kMapSize + 41] = Tile::Wall;
    assert(level.Lighting().At({42.5f, 40.5f}) < .01f);
    flashingNpc.combat.muzzleFlash = 0;
    assert(level.Lighting().At(flashingNpc.position) < .01f);
    mineState.riaPosition = {45.5f, 45.5f};
    mineState.riaCombat.muzzleFlash = .05f;
    assert(level.Lighting().At(mineState.riaPosition) > .99f);
}
void TestWorldCamera()
{
    using tutorial::WorldCamera;
    for (Vector2 point : {Vector2{0, 0}, {1280, 800}, {640, 456}, {930, 390}})
    {
        auto restored = WorldCamera::FromViewport(WorldCamera::ToViewport(point));
        assert(tutorial::Game::Distance(point, restored) < .001f);
    }
    assert(WorldCamera::zoom > 1 && WorldCamera::zoom < 1.5f);
    ReadyMine();
    auto &world = tutorial::Game::Get().GetState();
    world.cameraPosition = {12, 9};
    for (Vector2 direction : {Vector2{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {1, -1}})
    {
        Vector2 target = {world.playerPosition.x + direction.x * 4, world.playerPosition.y + direction.y * 4};
        AimAt(target);
        auto actual = GameLevels::Mine().GetAimDirection(GameLevels::Mine().GetAimAngle());
        float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        assert(std::fabs(actual.x - direction.x / length) < .001f);
        assert(std::fabs(actual.y - direction.y / length) < .001f);
    }
}
void ChapterInteract(game::ChapterAction action)
{
    auto &level = game::GameLevels::Chapter();
    bool found = false;
    for (auto point : level.Interactions())
    {
        if (point.action == action)
        {
            assert(!level.FindRoute(game::kFrontierEntry, point.position).empty());
            tutorial::Game::Get().GetState().playerPosition = point.position;
            found = true;
            break;
        }
    }
    assert(found);
    ReadDialogue();
    level.Interact();
    if (action == game::ChapterAction::ConvoySupply)
    {
        assert(tutorial::Game::Get().GetState().ui.screen == tutorial::UiScreen::Supply);
        tutorial::Hud::ExchangeSupply();
        tutorial::Hud::CloseMenu();
    }
    ReadDialogue();
}
void CompleteChapterSite(int site, bool peaceful = true)
{
    auto &level = GameLevels::Chapter();
    auto &s = level.GetState();
    if (site == 0)
    {
        ChapterInteract(ChapterAction::Generator);
        assert(s.generatorInspected && !s.generatorRestored);
        ChapterInteract(ChapterAction::IsolateLine);
        assert(s.generatorRestored);
    }
    else if (site == 1)
    {
        ChapterInteract(ChapterAction::Kain);
        assert(s.convoyBriefed && s.survivors.size() == 5);
        // The repeated sixth response is evidence, never an extra rescue actor.
        ChapterInteract(ChapterAction::Receiver);
        assert(s.receiverInspected && s.survivors.size() == 5);
        ChapterInteract(ChapterAction::SurveyOne);
        ChapterInteract(ChapterAction::SurveyTwo);
        ChapterInteract(ChapterAction::SurveyThree);
        ChapterInteract(ChapterAction::Escort);
        assert(s.escortStarted && !s.convoyRescued);
        for (int i = 0; i < 700 && !s.convoyRescued; ++i)
        {
            level.Update(.05f);
        }
        ReadDialogue();
        assert(s.convoyRescued && s.kainJoined);
        for (const auto &person : s.survivors)
        {
            assert(person.arrived && !level.IsBlocked(person.position));
        }
    }
    else
    {
        if (peaceful)
        {
            ChapterInteract(ChapterAction::Scrappers);
            assert(!s.negotiated && !s.hostile);
            ChapterInteract(ChapterAction::TradeProof);
            ChapterInteract(ChapterAction::Scrappers);
            assert(s.negotiated && !s.hostile);
            for (const auto &person : s.scrappers)
            {
                assert(person.combat.health == 60);
            }
        }
        else
        {
            auto &world = tutorial::Game::Get().GetState();
            mineState.loadout.Select(combat::WeaponSlot::Secondary);
            s.invulnerability = 1000;
            for (size_t i = 0; i < s.scrappers.size(); ++i)
            {
                auto target = s.scrappers[i].position;
                bool found = false;
                for (Vector2 delta : {Vector2{2, 0}, {-2, 0}, {0, 2}, {0, -2}})
                {
                    Vector2 p = {target.x + delta.x, target.y + delta.y};
                    if (!level.IsBlocked(p) && level.HasLineOfSight(p, target))
                    {
                        world.playerPosition = world.cameraPosition = p;
                        found = true;
                        break;
                    }
                }
                assert(found);
                AimAt(target);
                world.mouseFiring = true;
                for (int frame = 0; frame < 300 && s.scrappers[i].combat.health > 0; ++frame)
                {
                    level.Update(.025f);
                }
                world.mouseFiring = false;
                assert(s.scrappers[i].combat.health == 0);
            }
            assert(!s.negotiated);
        }
        ChapterInteract(ChapterAction::Ledger);
        assert(s.ledgerFound);
    }
}
void TestChapterOne()
{
    auto &game = tutorial::Game::Get();
    auto &level = GameLevels::Chapter();
    game.Reset();
    level.Enter();
    assert(!level.IsActive() && !level.IsAvailable());
    ReadyMine();
    for (auto stage : {MineQuestStage::BypassPower, MineQuestStage::InvestigateSignal,
                       MineQuestStage::InvestigateGlyph, MineQuestStage::CollectCoordinates})
    {
        assert(mineState.quest.Advance(stage));
    }
    GameLevels::Mine().Leave();
    assert(!level.IsAvailable() && !level.IsActive());
    game.GetState().playerPosition = tutorial::kCityReceiverPosition;
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(level.IsAvailable() && !level.IsActive());
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(level.IsActive() && SceneManager::Current() == SceneKind::Frontier);
    level.Leave();
    assert(!level.IsActive() && SceneManager::Current() == SceneKind::Exterior);
    // All six permutations must reach the same chapter conclusion.
    std::array<int, 3> order = {0, 1, 2};
    do
    {
        game.Reset();
        level.Enter(true);
        for (int site : order)
        {
            CompleteChapterSite(site);
        }
        auto &s = level.GetState();
        assert(level.CompletedSites() == 3 && !s.recordsCompared);
        assert(!s.vehicleRepaired && !s.portableLightRepaired && !s.detailLedgerFound);
        level.Leave();
        game.InteractWithNearbyTarget();
        assert(s.comparisonStarted && !s.recordsCompared && !level.IsActive());
        level.Update(3.1f);
        assert(!s.recordsCompared);
        level.Update(500.f);
        assert(!s.recordsCompared && !s.waterwayUnlocked);
        ReadDialogue();
        assert(s.recordsCompared && s.waterwayUnlocked);
        level.Enter();
        assert(level.CompletedSites() == 3 && s.kainJoined && s.recordsCompared);
        auto snapshot = monitoring::WorldSnapshot::Build(1, "chapter-test");
        assert(snapshot.find("\"current_region_id\":\"frontier\"") != std::string::npos);
        assert(snapshot.find("\"main.chapter1_complete\":true") != std::string::npos);
    } while (std::next_permutation(order.begin(), order.end()));
    // Departure requires an explicit choice, and the confirmation key cannot also exit the field.
    game.Reset();
    level.Enter(true);
    level.Leave();
    game.KeyDown('e', 0, 0);
    assert(tutorial::Dialogue::Active() && !level.IsActive());
    game.KeyDown('e', 0, 0);
    assert(!level.IsActive());
    game.KeyUp('e', 0, 0);
    game.KeyDown('1', 0, 0);
    game.KeyUp('1', 0, 0);
    game.KeyDown('e', 0, 0);
    assert(level.IsActive());
    game.KeyDown('e', 0, 0);
    assert(level.IsActive());
    game.KeyUp('e', 0, 0);
    game.KeyDown('e', 0, 0);
    assert(!level.IsActive());
    game.KeyUp('e', 0, 0);
    game.Reset();
    level.Enter(true);
    ChapterInteract(ChapterAction::Kain);
    ChapterInteract(ChapterAction::SurveyOne);
    ChapterInteract(ChapterAction::SurveyTwo);
    ChapterInteract(ChapterAction::SurveyThree);
    ChapterInteract(ChapterAction::Escort);
    level.Update(1);
    auto pausedPosition = level.GetState().survivors.front().position;
    level.Leave();
    level.Update(5);
    assert(tutorial::Game::Distance(pausedPosition, level.GetState().survivors.front().position) < .001f);
    level.Enter();
    for (int frame = 0; frame < 700 && !level.GetState().convoyRescued; ++frame)
    {
        level.Update(.05f);
    }
    assert(level.GetState().convoyRescued);
    for (const auto &person : level.GetState().survivors)
    {
        assert(person.arrived && !person.facing.walking);
    }
    game.Reset();
    level.Enter(true);
    CompleteChapterSite(2, false);
    assert(level.GetState().ledgerFound && !level.GetState().negotiated);
    ChapterInteract(ChapterAction::DetailLedger);
    assert(level.GetState().detailLedgerFound);
    CompleteChapterSite(0);
    ChapterInteract(ChapterAction::PortableLight);
    CompleteChapterSite(1);
    ChapterInteract(ChapterAction::Vehicle);
    int kits = mineState.player.recoveryKits, salvage = mineState.player.salvage;
    assert(salvage >= 2);
    ChapterInteract(ChapterAction::ConvoySupply);
    assert(mineState.player.recoveryKits == kits + 1 && mineState.player.salvage == salvage - 2);
    assert(level.GetState().portableLightRepaired && level.GetState().vehicleRepaired);
    auto &s = level.GetState();
    // No quest or optional outcome is lost on an early return/re-entry.
    level.Leave();
    level.Enter();
    assert(s.detailLedgerFound && s.portableLightRepaired && s.vehicleRepaired);
    game.Reset();
    level.Enter(true);
    // Companions retaliate, take damage and recover without blocking the story.
    s.hostile = true;
    s.ria.position = {31.5f, 31.5f};
    int rounds = s.ria.combat.pistol.GetRounds();
    for (int i = 0; i < 40; ++i)
    {
        level.Update(.05f);
    }
    assert(s.ria.combat.pistol.GetRounds() < rounds && s.ria.combat.health < 100);
    s.hostile = false;
    s.bullets.clear();
    s.ria.combat.health = 0;
    s.ria.combat.downedSeconds = 12;
    level.Update(12.1f);
    assert(s.ria.combat.health == 50);
    // Swept projectiles stop at a facility wall, and friendly shots cannot harm evacuees.
    // Imported proportions can change the footprint; locate an actual foundation edge.
    auto wall = std::find(s.tiles.begin(), s.tiles.end(), FrontierTile::Foundation);
    assert(wall != s.tiles.end());
    const int wallCell = int(wall - s.tiles.begin());
    Vector2 wallApproach = {float(wallCell % kFrontierWidth) + .5f, float(wallCell / kFrontierWidth) - .5f};
    s.bullets = {{{wallApproach, {0, 1}, 100, 20, 100, true}, ChapterTeam::Player}};
    level.Update(.1f);
    assert(s.bullets.empty());
    auto person = s.survivors.front().position;
    s.bullets = {{{{person.x - 1, person.y}, {1, 0}, 100, 20, 100, true}, ChapterTeam::Player}};
    level.Update(.02f);
    assert(s.survivors.front().combat.health == 100);
    mineState.player.health = 0;
    bool generator = s.generatorRestored;
    level.Update(.05f);
    assert(tutorial::HospitalRecovery::Active() && mineState.player.health <= 0);
    for (int i = 0; i < 60; ++i)
    {
        tutorial::HospitalRecovery::Update(.05f);
    }
    assert(!tutorial::HospitalRecovery::Active() && mineState.player.health == mineState.player.maxHealth);
    assert(s.generatorRestored == generator && !s.active);
    const auto &clinic =
        tutorial::CivicJustice::State().hospitals[size_t(tutorial::CivicJustice::State().hospital)];
    assert(tutorial::Game::Distance(game.GetState().playerPosition, clinic.position) < .1f);
    game.Reset();
}
void TestStoryAndReentry()
{
    ReadyMine();
    mineState.enemies.clear();
    tutorial::Game::Get().GetState().playerPosition = kCoordinateTerminal;
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(!mineState.quest.AreCoordinatesFound());
    tutorial::Game::Get().GetState().playerPosition = kPowerBypass;
    GameLevels::Mine().Interact();
    ReadDialogue();
    tutorial::Game::Get().GetState().playerPosition = kRuinSignal;
    GameLevels::Mine().Interact();
    ReadDialogue();
    tutorial::Game::Get().GetState().playerPosition = kObserverGlyph;
    GameLevels::Mine().Interact();
    ReadDialogue();
    tutorial::Game::Get().GetState().playerPosition = kCoordinateTerminal;
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(mineState.quest.GetStage() == MineQuestStage::ReturnToHalo);
    auto revision = mineState.mapRevision;
    mineState.player.salvage = 7;
    tutorial::Game::Get().GetState().playerPosition = mineState.entry;
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(!GameLevels::Mine().IsActive() && mineState.quest.GetStage() == MineQuestStage::ReturnToHalo);
    auto &world = tutorial::Game::Get().GetState();
    assert(tutorial::Game::Distance(world.playerPosition, world.mineReturnPosition) < .001f);
    world.isBikeMounted = false;
    world.playerPosition = tutorial::kCityReceiverPosition;
    tutorial::Game::Get().InteractWithNearbyTarget();
    ReadDialogue();
    assert(mineState.quest.GetStage() == MineQuestStage::Complete);
    GameLevels::Mine().Enter();
    assert(GameLevels::Mine().IsActive() && mineState.mapRevision > revision &&
           mineState.player.salvage == 7);
    assert(mineState.quest.GetStage() == MineQuestStage::Complete);
}
void TestExplorationAndMelee()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &level = GameLevels::Mine();
    level.Enter(true);
    auto &state = level.GetState();
    auto &world = game.GetState();
    state.tiles.assign(kMapSize * kMapSize, Tile::Floor);
    state.explored.assign(kMapSize * kMapSize, 0);
    for (int y = 0; y < kMapSize; ++y)
    {
        state.tiles[y * kMapSize + 15] = Tile::Wall;
    }
    world.playerPosition = world.cameraPosition = {12.5f, 12.5f};
    level.Map().Reveal(world.playerPosition);
    assert(level.Map().IsExplored({14.5f, 12.5f}));
    assert(level.Map().IsExplored({15.5f, 12.5f}));
    assert(!level.Map().IsExplored({16.5f, 12.5f}));
    assert(!level.Map().IsExplored({90.5f, 90.5f}));
    level.Map().Reveal({20.5f, 12.5f});
    assert(level.Map().IsExplored({12.5f, 12.5f}) && level.Map().IsExplored({20.5f, 12.5f}));
    state.tiles.assign(kMapSize * kMapSize, Tile::Floor);
    state.tiles[13 * kMapSize + 13] = Tile::Wall;
    assert(!level.Map().IsBlocked({12.88f, 12.88f}));
    assert(level.Map().IsBlocked({12.9f, 12.9f}));
    state.tiles[13 * kMapSize + 13] = Tile::Floor;
    auto target = Drawing::WorldToScreen(14, 12.5f, models::WorldScale::aimHeightPixels);
    world.cursorPosition = target;
    auto aim = level.GetAimDirection(level.GetAimAngle());
    state.enemies = {{{world.playerPosition.x + aim.x * .8f, world.playerPosition.y + aim.y * .8f}},
                     {{world.playerPosition.x - aim.x * .8f, world.playerPosition.y - aim.y * .8f}},
                     {{world.playerPosition.x + aim.x * 3.f, world.playerPosition.y + aim.y * 3.f}}};
    int ammunition = state.pistol.GetRounds();
    game.KeyDown('v', 0, 0);
    game.KeyUp('v', 0, 0);
    assert(state.meleeSeconds > 0 && state.enemies[0].health == 10);
    assert(state.enemies[1].health == kEnemyMaxHealth && state.enemies[2].health == kEnemyMaxHealth);
    level.Combat().Melee();
    assert(state.enemies[0].health == 10 && state.pistol.GetRounds() == ammunition);
    level.Combat().Update(.7f);
    level.Combat().Melee();
    assert(state.enemies[0].health == 0 && state.kills == 1 && state.player.experience >= kExperiencePerKill);
    state.meleeSeconds = 0;
    world.playerPosition = world.cameraPosition = {12.9f, 13.2f};
    world.cursorPosition = Drawing::WorldToScreen(13.2f, 12.9f, models::WorldScale::aimHeightPixels);
    state.enemies = {{{13.2f, 12.9f}}};
    state.tiles[13 * kMapSize + 13] = Tile::Wall;
    level.Combat().Melee();
    assert(state.enemies[0].health == kEnemyMaxHealth); // No melee damage around a solid wall corner.
    state.meleeSeconds = 0;
    world.mapVisible = true;
    level.Combat().Melee();
    assert(state.meleeSeconds == 0);
    world.mapVisible = false;
    level.Map().Generate(703);
    assert(!level.Map().IsExplored({90.5f, 90.5f}));
    assert(level.Map().IsExplored(state.entry));
    game.Reset();
}
void TestBikeSpeedsAndSliding()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.isBikeMounted = true;
    world.playerPosition = world.bikePosition = {70, 50};
    world.bikeYaw = 0;
    world.pressedKeys['w'] = true;
    for (int i = 0; i < 120; ++i)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(std::fabs(world.bikeSpeed - Overworld::kBikeCruiseSpeed) < .001f);
    world.pressedKeys[' '] = true;
    for (int i = 0; i < 120; ++i)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(std::fabs(world.bikeSpeed - Overworld::kBikeBoostSpeed) < .001f);
    world.pressedKeys[' '] = world.pressedKeys['w'] = false;
    world.pressedKeys['s'] = true;
    for (int i = 0; i < 150; ++i)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(std::fabs(world.bikeSpeed + Overworld::kBikeReverseSpeed) < .001f);
    auto before = world.playerPosition;
    Overworld::UpdateBike(world, .1f);
    assert(world.playerPosition.x < before.x && world.movementSpeed > 0 && world.movementAmount > 0);
    auto straight = BodyCollision::Slide({0, 0}, {2, 1}, [](Vector2 p) { return p.x > .5f; });
    assert(straight.x <= .501f && straight.y > .9f);
    auto skew = BodyCollision::Slide({0, 0}, {2, .5f}, [](Vector2 p) { return p.x + p.y > 1; });
    assert(skew.x + skew.y <= 1.001f && skew.x > 1 && skew.y < 0);
    auto overlap = BodyCollision::Slide({.52f, 0}, {.52f, 1}, [](Vector2 p) { return p.x > .5f; });
    assert(overlap.x <= .501f && overlap.y > .9f);
    game.Reset();
}
} // namespace
int main(int argc, char **argv)
{
    static_assert(std::is_base_of<game::Level, game::MineLevel>::value);
    static_assert(!std::is_copy_constructible<game::MineLevel>::value);
    static_assert(std::is_base_of<game::Scene, game::MineScene>::value);
    if (argc > 1 && std::string(argv[1]) == "--urban")
    {
        TestUrbanTraffic();
        TestCityPlanAndSignalProgress();
        TestVehicleQueueProgress();
        TestCityResidentsAndShadowLifetime();
        std::ofstream("x64/RevisionChecks/state.json") << monitoring::WorldSnapshot::Build(1, "urban-test");
        std::cout << "Urban traffic, crossing and resident checks passed.\n";
        return 0;
    }
    TestStageSelectionAndLoadout();
    TestUiMenusAndTransactions();
    TestDialogueProgress();
    TestExplorationAndMelee();
    TestBikeSpeedsAndSliding();
    TestLevelLifecycle();
    TestWeapon();
    TestQuest();
    TestMapAndGate();
    TestNpcFacing();
    TestWorldCamera();
    TestNpcCombat();
    TestAimLightingAndWallShots();
    TestRescueAndScanner();
    TestScannerPropagation();
    TestStoryAndReentry();
    TestChapterOne();
    TestConnectedOverworld();
    TestCityQuestAccess();
    TestImportedWorldAndFlights();
    TestUrbanTraffic();
    TestCityPlanAndSignalProgress();
    TestVehicleQueueProgress();
    TestCityResidentsAndShadowLifetime();
    if (argc > 1)
    {
        std::ofstream snapshot(argv[1]);
        snapshot << monitoring::WorldSnapshot::Build(1, "mine-tests");
    }
    std::cout << "Mine gameplay checks passed: weapon, quest, 30 maps, aiming, lighting, walls, combat, "
                 "scanner, rescue, reentry.\n";
}
