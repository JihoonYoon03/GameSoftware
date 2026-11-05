#pragma once
#include <cstddef>

namespace combat
{
enum class CombatArea
{
    Inactive,
    Mine,
    Frontier,
    Exterior
};
enum class CombatAction
{
    Fire,
    Melee,
    Reload
};
enum class CombatBlock
{
    Ready,
    Loading,
    Inactive,
    Menu,
    Paused,
    Map,
    WeaponWheel,
    Journal,
    History,
    Dialogue,
    Hospital,
    Dead,
    Mounted,
    RelayPreparing,
    MeleeRecovery,
    Unequipped,
    Reloading,
    FireCooldown,
    MagazineFull,
    ProjectileCapacity,
    Dodging
};
// Read-only transient view. Weapon/player state remains owned by the existing level and save adapters.
struct PlayerCombatContext
{
    CombatArea area = CombatArea::Inactive;
    bool loading = false, menu = false, paused = false, map = false, wheel = false;
    bool journal = false, history = false, dialogue = false, hospital = false;
    bool mounted = false, relayPreparing = false, pistolEquipped = false, reloading = false;
    bool dodging = false;
    int health = 0, rounds = 0, magazineSize = 0;
    float meleeRemaining = 0, fireCooldown = 0;
    size_t projectiles = 0, projectileLimit = 0;
};
class PlayerCombat
{
  public:
    static PlayerCombatContext Current();
    static CombatBlock EnvironmentBlock(const PlayerCombatContext &context, CombatArea expected);
    static CombatBlock Evaluate(const PlayerCombatContext &context, CombatAction action, CombatArea expected);
    // Count only action requests reaching an adapter; reading state/evaluating a snapshot never logs.
    static bool Allow(CombatAction action, CombatArea expected);
    static const char *AreaName(CombatArea area);
    static const char *BlockName(CombatBlock reason);
};
} // namespace combat
