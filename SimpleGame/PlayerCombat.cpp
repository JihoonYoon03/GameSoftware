#include "stdafx.h"
#include "PlayerCombat.h"
#include "Game.h"
#include "Level.h"
#include "Civic.h"
#include "Save.h"
#include "Profiler.h"

namespace combat
{
PlayerCombatContext PlayerCombat::Current()
{
    const auto &world = tutorial::Game::Get().GetState();
    const auto &mine = game::GameLevels::Mine().GetState();
    const auto &chapter = game::GameLevels::Chapter().GetState();
    const auto &civic = tutorial::CivicJustice::State();
    PlayerCombatContext context;
    if (mine.active)
    {
        context.area = CombatArea::Mine;
        context.meleeRemaining = mine.meleeSeconds;
        context.projectiles = mine.bullets.size();
        context.projectileLimit = 128;
    }
    else if (chapter.active)
    {
        context.area = CombatArea::Frontier;
        context.meleeRemaining = chapter.meleeSeconds;
        context.projectiles = chapter.bullets.size();
        context.projectileLimit = 128;
    }
    else if (tutorial::CivicJustice::IsExterior())
    {
        context.area = CombatArea::Exterior;
        context.meleeRemaining = civic.meleeSeconds;
        context.projectiles = civic.bullets.size() + civic.airBullets.size();
        context.projectileLimit = 96;
    }
    context.loading = tutorial::SaveSystem::Loading();
    context.menu = world.ui.screen != tutorial::UiScreen::None;
    context.paused = world.isPaused;
    context.map = world.mapVisible;
    context.wheel = world.weaponWheel.visible;
    context.journal = world.journalVisible;
    context.history = world.dialogueHistoryVisible;
    // Journal/history have their own flags. Avoid a danger/line-of-sight scan when no blocking dialogue
    // exists.
    context.dialogue = world.dialogue.active && world.dialogue.blocking && tutorial::Dialogue::LocksWorld();
    context.hospital = tutorial::HospitalRecovery::Active();
    context.mounted = world.isBikeMounted;
    context.dodging = world.playerMotion.mobility.Dodging();
    context.relayPreparing = world.relayPreparing;
    context.health = mine.player.health;
    context.pistolEquipped = mine.loadout.IsPistolEquipped();
    context.reloading = mine.pistol.IsReloading();
    context.rounds = mine.pistol.GetRounds();
    context.magazineSize = mine.pistol.GetMagazineSize();
    context.fireCooldown = mine.pistol.GetCooldownRemaining();
    return context;
}
CombatBlock PlayerCombat::EnvironmentBlock(const PlayerCombatContext &c, CombatArea expected)
{
    if (c.loading)
    {
        return CombatBlock::Loading;
    }
    if (c.area == CombatArea::Inactive || c.area != expected)
    {
        return CombatBlock::Inactive;
    }
    if (c.menu)
    {
        return CombatBlock::Menu;
    }
    if (c.paused)
    {
        return CombatBlock::Paused;
    }
    if (c.map)
    {
        return CombatBlock::Map;
    }
    if (c.wheel)
    {
        return CombatBlock::WeaponWheel;
    }
    if (c.journal)
    {
        return CombatBlock::Journal;
    }
    if (c.history)
    {
        return CombatBlock::History;
    }
    if (c.dialogue)
    {
        return CombatBlock::Dialogue;
    }
    if (c.hospital)
    {
        return CombatBlock::Hospital;
    }
    if (c.health <= 0)
    {
        return CombatBlock::Dead;
    }
    if (c.mounted)
    {
        return CombatBlock::Mounted;
    }
    if (c.relayPreparing)
    {
        return CombatBlock::RelayPreparing;
    }
    return CombatBlock::Ready;
}
CombatBlock PlayerCombat::Evaluate(const PlayerCombatContext &c, CombatAction action, CombatArea expected)
{
    const auto environment = EnvironmentBlock(c, expected);
    if (environment != CombatBlock::Ready)
    {
        return environment;
    }
    if (c.dodging)
    {
        return CombatBlock::Dodging;
    }
    if (action != CombatAction::Reload && c.meleeRemaining > 0)
    {
        return CombatBlock::MeleeRecovery;
    }
    // Unarmed left-click is melee. Full projectile pools must never disable a punch.
    if (action == CombatAction::Melee || (action == CombatAction::Fire && !c.pistolEquipped))
    {
        return CombatBlock::Ready;
    }
    if (!c.pistolEquipped)
    {
        return CombatBlock::Unequipped;
    }
    if (c.reloading)
    {
        return CombatBlock::Reloading;
    }
    if (action == CombatAction::Reload)
    {
        return c.rounds == c.magazineSize ? CombatBlock::MagazineFull : CombatBlock::Ready;
    }
    if (c.projectiles >= c.projectileLimit)
    {
        return CombatBlock::ProjectileCapacity;
    }
    if (c.fireCooldown > 0)
    {
        return CombatBlock::FireCooldown;
    }
    // Empty-magazine fire deliberately reaches Firearm::TryFire, preserving its automatic reload.
    return CombatBlock::Ready;
}
bool PlayerCombat::Allow(CombatAction action, CombatArea expected)
{
    profiling::Count(profiling::Counter::CombatActionRequests);
    const auto reason = Evaluate(Current(), action, expected);
    switch (reason)
    {
    case CombatBlock::Ready:
        return true;
    case CombatBlock::MeleeRecovery:
    case CombatBlock::Dodging:
    case CombatBlock::FireCooldown:
        profiling::Count(profiling::Counter::CombatRecoveryBlocks);
        break;
    case CombatBlock::Unequipped:
    case CombatBlock::Reloading:
    case CombatBlock::MagazineFull:
        profiling::Count(profiling::Counter::CombatWeaponBlocks);
        break;
    case CombatBlock::ProjectileCapacity:
        profiling::Count(profiling::Counter::CombatCapacityBlocks);
        break;
    default:
        profiling::Count(profiling::Counter::CombatContextBlocks);
        break;
    }
    return false;
}
const char *PlayerCombat::AreaName(CombatArea area)
{
    switch (area)
    {
    case CombatArea::Inactive:
        return "inactive";
    case CombatArea::Mine:
        return "mine";
    case CombatArea::Frontier:
        return "frontier";
    case CombatArea::Exterior:
        return "exterior";
    }
    return "unknown";
}
const char *PlayerCombat::BlockName(CombatBlock reason)
{
    switch (reason)
    {
    case CombatBlock::Ready:
        return "ready";
    case CombatBlock::Loading:
        return "world_loading";
    case CombatBlock::Inactive:
        return "inactive_combat_area";
    case CombatBlock::Menu:
        return "menu_open";
    case CombatBlock::Paused:
        return "paused";
    case CombatBlock::Map:
        return "map_open";
    case CombatBlock::WeaponWheel:
        return "weapon_wheel_open";
    case CombatBlock::Journal:
        return "journal_open";
    case CombatBlock::History:
        return "dialogue_history_open";
    case CombatBlock::Dialogue:
        return "blocking_dialogue";
    case CombatBlock::Hospital:
        return "hospital_transfer";
    case CombatBlock::Dead:
        return "player_dead";
    case CombatBlock::Mounted:
        return "vehicle_mounted";
    case CombatBlock::RelayPreparing:
        return "relay_preparing";
    case CombatBlock::MeleeRecovery:
        return "melee_recovery";
    case CombatBlock::Unequipped:
        return "firearm_not_equipped";
    case CombatBlock::Reloading:
        return "weapon_reloading";
    case CombatBlock::FireCooldown:
        return "fire_cooldown";
    case CombatBlock::MagazineFull:
        return "magazine_full";
    case CombatBlock::ProjectileCapacity:
        return "projectile_capacity";
    case CombatBlock::Dodging:
        return "dodge_active";
    }
    return "unknown";
}
} // namespace combat
