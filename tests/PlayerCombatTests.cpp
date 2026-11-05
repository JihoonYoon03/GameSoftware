#include "stdafx.h"
#include "PlayerCombat.h"
#include "Civic.h"
#include "Game.h"
#include "Level.h"
#include "Monitoring/Monitor.h"
#include "Profiler.h"
#include <array>
#include <cassert>
#include <iostream>
#include <string>

namespace
{
using namespace combat;
PlayerCombatContext Ready(CombatArea area)
{
    PlayerCombatContext context;
    context.area = area;
    context.health = 100;
    context.pistolEquipped = true;
    context.rounds = 11;
    context.magazineSize = 12;
    context.projectileLimit = area == CombatArea::Exterior ? 96 : 128;
    return context;
}
void TestPolicy()
{
    struct Lock
    {
        bool PlayerCombatContext::*flag;
        CombatBlock reason;
    };
    const std::array<Lock, 12> locks = {{{&PlayerCombatContext::loading, CombatBlock::Loading},
                                         {&PlayerCombatContext::menu, CombatBlock::Menu},
                                         {&PlayerCombatContext::paused, CombatBlock::Paused},
                                         {&PlayerCombatContext::map, CombatBlock::Map},
                                         {&PlayerCombatContext::wheel, CombatBlock::WeaponWheel},
                                         {&PlayerCombatContext::journal, CombatBlock::Journal},
                                         {&PlayerCombatContext::history, CombatBlock::History},
                                         {&PlayerCombatContext::dialogue, CombatBlock::Dialogue},
                                         {&PlayerCombatContext::hospital, CombatBlock::Hospital},
                                         {&PlayerCombatContext::mounted, CombatBlock::Mounted},
                                         {&PlayerCombatContext::relayPreparing, CombatBlock::RelayPreparing},
                                         {nullptr, CombatBlock::Dead}}};
    for (auto area : {CombatArea::Mine, CombatArea::Frontier, CombatArea::Exterior})
    {
        for (auto action : {CombatAction::Fire, CombatAction::Melee, CombatAction::Reload})
        {
            const auto ready = Ready(area);
            assert(PlayerCombat::Evaluate(ready, action, area) == CombatBlock::Ready);
            for (const auto &lock : locks)
            {
                auto context = ready;
                if (lock.flag)
                {
                    context.*(lock.flag) = true;
                }
                else
                {
                    context.health = 0;
                }
                assert(PlayerCombat::Evaluate(context, action, area) == lock.reason);
                assert(std::string(PlayerCombat::BlockName(lock.reason)) != "unknown");
            }
            assert(PlayerCombat::Evaluate(ready, action, CombatArea::Inactive) == CombatBlock::Inactive);
            auto inactive = ready;
            inactive.area = CombatArea::Inactive;
            assert(PlayerCombat::Evaluate(inactive, action, inactive.area) == CombatBlock::Inactive);
        }
        auto c = Ready(area);
        c.dodging = true;
        for (auto action : {CombatAction::Fire, CombatAction::Melee, CombatAction::Reload})
        {
            assert(PlayerCombat::Evaluate(c, action, area) == CombatBlock::Dodging);
        }
        c.dodging = false;
        c.projectiles = c.projectileLimit;
        assert(PlayerCombat::Evaluate(c, CombatAction::Fire, area) == CombatBlock::ProjectileCapacity);
        assert(PlayerCombat::Evaluate(c, CombatAction::Melee, area) == CombatBlock::Ready);
        assert(PlayerCombat::Evaluate(c, CombatAction::Reload, area) == CombatBlock::Ready);
        c.pistolEquipped = false;
        assert(PlayerCombat::Evaluate(c, CombatAction::Fire, area) == CombatBlock::Ready);
        assert(PlayerCombat::Evaluate(c, CombatAction::Reload, area) == CombatBlock::Unequipped);
        c = Ready(area);
        c.meleeRemaining = .65f;
        assert(PlayerCombat::Evaluate(c, CombatAction::Fire, area) == CombatBlock::MeleeRecovery);
        assert(PlayerCombat::Evaluate(c, CombatAction::Melee, area) == CombatBlock::MeleeRecovery);
        assert(PlayerCombat::Evaluate(c, CombatAction::Reload, area) == CombatBlock::Ready);
        c = Ready(area);
        c.reloading = true;
        assert(PlayerCombat::Evaluate(c, CombatAction::Fire, area) == CombatBlock::Reloading);
        assert(PlayerCombat::Evaluate(c, CombatAction::Reload, area) == CombatBlock::Reloading);
        assert(PlayerCombat::Evaluate(c, CombatAction::Melee, area) == CombatBlock::Ready);
        c = Ready(area);
        c.fireCooldown = .2f;
        assert(PlayerCombat::Evaluate(c, CombatAction::Fire, area) == CombatBlock::FireCooldown);
        assert(PlayerCombat::Evaluate(c, CombatAction::Melee, area) == CombatBlock::Ready);
        c.fireCooldown = 0;
        c.rounds = c.magazineSize;
        assert(PlayerCombat::Evaluate(c, CombatAction::Reload, area) == CombatBlock::MagazineFull);
        c.rounds = 0;
        assert(PlayerCombat::Evaluate(c, CombatAction::Fire, area) == CombatBlock::Ready);
    }
}
void TestAdapters()
{
    auto &g = tutorial::Game::Get();
    g.Reset();
    auto &world = g.GetState();
    auto &mine = game::GameLevels::Mine();
    auto &m = mine.GetState();
    auto &chapter = game::GameLevels::Chapter();
    auto &f = chapter.GetState();
    auto &city = tutorial::CivicJustice::State();
    world.ui.screen = tutorial::UiScreen::None;
    world.isInsideHome = world.isInObservatory = world.isPaused = false;
    world.residents.clear();
    world.playerPosition = world.cameraPosition = {2, 0};
    m.loadout.GrantPistol();
    assert(m.loadout.Select(WeaponSlot::Secondary));
    Shot shot;
    assert(m.pistol.TryFire(0, shot));
    const int rounds = m.pistol.GetRounds();
    m.player.health = 0;
    assert(!tutorial::CivicCombat::Available());
    tutorial::CivicCombat::Fire();
    tutorial::CivicCombat::Melee();
    mine.Combat().Reload();
    assert(m.pistol.GetRounds() == rounds && !m.pistol.IsReloading() && city.meleeSeconds == 0);
    m.player.health = 100;
    world.isBikeMounted = true;
    mine.Combat().Reload();
    assert(!m.pistol.IsReloading());
    world.isBikeMounted = false;

    // Exercise the actual left-click adapters with their real projectile limits, without advancing AI.
    assert(m.loadout.Select(WeaponSlot::Unarmed));
    city.bullets.resize(96);
    tutorial::CivicCombat::Fire();
    assert(city.meleeSeconds > 0 && m.pistol.GetRounds() == rounds);
    city.bullets.clear();
    m.active = true;
    m.enemies.clear();
    m.bullets.resize(128);
    mine.Combat().Attack();
    assert(m.meleeSeconds > 0 && m.pistol.GetRounds() == rounds);
    m.active = false;
    f.active = true;
    f.negotiated = true;
    f.bullets.resize(128);
    chapter.Attack();
    assert(f.meleeSeconds > 0 && m.pistol.GetRounds() == rounds);

    assert(m.loadout.Select(WeaponSlot::Secondary));
    for (auto area : {CombatArea::Mine, CombatArea::Frontier, CombatArea::Exterior})
    {
        m.active = area == CombatArea::Mine;
        f.active = area == CombatArea::Frontier;
        m.meleeSeconds = f.meleeSeconds = city.meleeSeconds = 0;
        m.pistol.Reset();
        assert(m.pistol.TryFire(0, shot));
        assert(m.pistol.GetCooldownRemaining() > 0);
        assert(PlayerCombat::Current().area == area);
        mine.Combat().Reload();
        assert(m.pistol.IsReloading());
        const auto before = profiling::ReadCounter(profiling::Counter::CombatActionRequests);
        const auto context = PlayerCombat::Current();
        assert(PlayerCombat::Evaluate(context, CombatAction::Fire, area) == CombatBlock::Reloading);
        if (area == CombatArea::Exterior)
        {
            const auto json = monitoring::WorldSnapshot::Build(1, "combat-check");
            assert(json.find("\"primary_block_reason\":\"weapon_reloading\"") != std::string::npos);
        }
        assert(profiling::ReadCounter(profiling::Counter::CombatActionRequests) == before);
        assert(m.pistol.GetRounds() == 11 && m.pistol.IsReloading() && m.pistol.GetReloadProgress() == 0);
    }

    m.active = true;
    f.active = false;
    m.bullets.resize(128);
    m.pistol.Reset();
    mine.Combat().Attack();
    assert(m.pistol.GetRounds() == 12); // Capacity denies a gunshot before consuming ammunition.
    m.bullets.clear();
    for (int i = 0; i < 12; ++i)
    {
        assert(m.pistol.TryFire(0, shot));
        m.pistol.Update(.2f);
    }
    mine.Combat().Attack();
    assert(m.pistol.IsReloading() && m.pistol.GetRounds() == 0 && m.bullets.empty());
    m.active = false;
    world.isInsideHome = true;
    m.pistol.Reset();
    assert(m.pistol.TryFire(0, shot));
    mine.Combat().Reload();
    assert(!m.pistol.IsReloading());
    g.Reset();
}
} // namespace
int main()
{
    TestPolicy();
    TestAdapters();
    std::cout << "Player combat policy, adapters and read-only diagnostics checks passed.\n";
}
