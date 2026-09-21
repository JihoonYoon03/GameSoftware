#include "stdafx.h"
#include "LevelOne.h"
#include "Scene/WorldScene.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace levelone
{
State g_level;
namespace
{
constexpr float kAttackRange = 1.8f;
constexpr float kAttackInterval = .45f;

void RespawnPlayer()
{
    g_level.player.health = g_level.player.maxHealth;
    tutorial::g_state.playerPosition = g_level.entry;
    tutorial::g_state.cameraPosition = g_level.entry;
    g_level.invulnerability = 3;
    g_level.navigationCooldown = 0;
    tutorial::ShowNotice("구조 신호 / 입구로 귀환했습니다. 경험치와 아이템은 유지됩니다.");
}

void MoveEnemy(Enemy &enemy, float deltaSeconds)
{
    int x = int(std::floor(enemy.position.x)), y = int(std::floor(enemy.position.y));
    if (!IsWalkable(x, y))
    {
        return;
    }
    int cell = y * kMapSize + x;
    int best = g_level.navigationDistances[cell];
    if (best < 0 || best > 11)
    {
        return;
    }
    tutorial::Vector2 target = tutorial::g_state.playerPosition;
    if (!HasLineOfSight(enemy.position, target))
    {
        target = {x + .5f, y + .5f};
        constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &direction : directions)
        {
            int nextX = x + direction[0], nextY = y + direction[1];
            if (!IsWalkable(nextX, nextY))
            {
                continue;
            }
            int distance = g_level.navigationDistances[nextY * kMapSize + nextX];
            if (distance >= 0 && distance < best)
            {
                best = distance;
                target = {nextX + .5f, nextY + .5f};
            }
        }
    }
    float distance = tutorial::Distance(enemy.position, target);
    if (distance < .05f)
    {
        return;
    }
    float step = (std::min)(distance, 1.25f * deltaSeconds);
    tutorial::Vector2 candidate = {enemy.position.x + (target.x - enemy.position.x) / distance * step,
                                   enemy.position.y + (target.y - enemy.position.y) / distance * step};
    if (!IsBlocked(candidate))
    {
        enemy.position = candidate;
    }
    else
    {
        // Axis resolution allows pursuit to slide through corridor corners.
        if (!IsBlocked({candidate.x, enemy.position.y}))
        {
            enemy.position.x = candidate.x;
        }
        if (!IsBlocked({enemy.position.x, candidate.y}))
        {
            enemy.position.y = candidate.y;
        }
    }
}
} // namespace

bool IsActive()
{
    return g_level.active;
}

int ExperienceForLevel(int level)
{
    return 30 * (level - 1) * level;
}

void AddExperience(int amount)
{
    // A bounded prototype progression avoids integer overflow in long grinding sessions.
    auto &stats = g_level.player;
    stats.experience = (std::min)(297000, stats.experience + (std::max)(0, amount));
    int oldLevel = stats.level;
    while (stats.level < 100 && stats.experience >= ExperienceForLevel(stats.level + 1))
    {
        ++stats.level;
    }
    stats.maxHealth = 100 + (stats.level - 1) * 20;
    stats.attack = 12 + (stats.level - 1) * 4;
    stats.defense = 2 + (stats.level - 1) * 2;
    if (stats.level > oldLevel)
    {
        stats.health = stats.maxHealth;
        tutorial::ShowNotice("레벨 상승 / 체력·공격력·방어력이 증가하고 체력이 회복되었습니다.");
    }
    if (stats.level >= 3 && !g_level.growthGoalReached)
    {
        g_level.growthGoalReached = true;
        tutorial::ShowNotice(
            "첫 레벨 목표 달성 / 레벨 3 성장과 스탯 반영 완료. 계속 사냥하거나 입구로 귀환하세요.");
    }
}

void Enter()
{
    if (!g_level.generated)
    {
        GenerateMap(std::random_device{}());
    }
    g_level.active = true;
    tutorial::g_state.isInsideHome = false;
    tutorial::g_state.playerPosition = g_level.entry;
    tutorial::g_state.cameraPosition = g_level.entry;
    std::fill(tutorial::g_state.pressedKeys, tutorial::g_state.pressedKeys + 256, false);
    g_level.invulnerability = 2;
    g_level.navigationCooldown = 0;
    tutorial::ShowNotice(
        "격리 구역 / Space 근거리 펄스 공격, E 아이템 획득, Q 회복약. 레벨 3까지 성장하세요.");
}

void Leave()
{
    g_level.active = false;
    tutorial::g_state.playerPosition = tutorial::kBeaconPosition;
    tutorial::g_state.cameraPosition = tutorial::kBeaconPosition;
    tutorial::ShowNotice("헤일로 / 경험치와 가방은 유지됩니다. E를 누르면 다시 사냥터에 진입합니다.");
}

void ResetProgress()
{
    auto &scene = tutorial::GetWorldScene(tutorial::WorldSceneKind::Hunting);
    scene.BeginPlacement();
    scene.EndPlacement();
    scene.GetGraph().Clear();
    g_level = State{};
}

void Attack()
{
    if (g_level.attackCooldown > 0)
    {
        return;
    }
    g_level.attackCooldown = kAttackInterval;
    g_level.attackFlash = .18f;
    for (auto &enemy : g_level.enemies)
    {
        if (enemy.health <= 0 ||
            tutorial::Distance(enemy.position, tutorial::g_state.playerPosition) > kAttackRange ||
            !HasLineOfSight(enemy.position, tutorial::g_state.playerPosition))
        {
            continue;
        }
        enemy.health -= g_level.player.attack;
        if (enemy.health > 0)
        {
            continue;
        }
        ++g_level.kills;
        enemy.respawnSeconds = 12;
        g_level.drops.push_back({enemy.position,
                                 g_level.kills % 3 == 0 ? ItemKind::RecoveryKit : ItemKind::Salvage,
                                 g_level.nextDropActorId++});
        // Limit abandoned drops to keep the endless hunting loop bounded.
        if (g_level.drops.size() > 128)
        {
            g_level.drops.erase(g_level.drops.begin());
        }
        AddExperience(kExperiencePerKill);
    }
}

void Interact()
{
    int collected = 0;
    auto &drops = g_level.drops;
    drops.erase(
        std::remove_if(drops.begin(), drops.end(),
                       [&](const Drop &drop) {
                           if (tutorial::Distance(drop.position, tutorial::g_state.playerPosition) > 1.3f ||
                               !HasLineOfSight(drop.position, tutorial::g_state.playerPosition))
                           {
                               return false;
                           }
                           if (drop.kind == ItemKind::RecoveryKit)
                           {
                               g_level.player.recoveryKits = (std::min)(999, g_level.player.recoveryKits + 1);
                           }
                           else
                           {
                               g_level.player.salvage = (std::min)(9999, g_level.player.salvage + 1);
                           }
                           ++collected;
                           return true;
                       }),
        drops.end());
    if (collected > 0)
    {
        tutorial::ShowNotice("아이템 획득 / 가방에 보관했습니다. 회복 키트는 Q로 사용합니다.");
    }
    else if (tutorial::Distance(tutorial::g_state.playerPosition, g_level.entry) < 1.5f)
    {
        Leave();
    }
    else
    {
        tutorial::ShowNotice("가까운 드롭 아이템에서 E를 누르세요. 입구의 관문에서는 도시로 귀환합니다.");
    }
}

void UseRecoveryKit()
{
    auto &stats = g_level.player;
    if (stats.recoveryKits <= 0 || stats.health == stats.maxHealth)
    {
        return;
    }
    --stats.recoveryKits;
    stats.health = (std::min)(stats.maxHealth, stats.health + 45);
    tutorial::ShowNotice("회복 키트 사용 / 체력이 45만큼 회복되었습니다.");
}

void Update(float deltaSeconds)
{
    g_level.attackCooldown = (std::max)(0.f, g_level.attackCooldown - deltaSeconds);
    g_level.attackFlash = (std::max)(0.f, g_level.attackFlash - deltaSeconds);
    g_level.invulnerability = (std::max)(0.f, g_level.invulnerability - deltaSeconds);
    g_level.navigationCooldown -= deltaSeconds;
    if (g_level.navigationCooldown <= 0)
    {
        UpdateNavigation();
        g_level.navigationCooldown = .3f;
    }
    if (tutorial::g_state.pressedKeys[' '])
    {
        Attack();
    }
    for (auto &enemy : g_level.enemies)
    {
        if (enemy.health <= 0)
        {
            enemy.respawnSeconds -= deltaSeconds;
            if (enemy.respawnSeconds > 0)
            {
                continue;
            }
            // Deterministic rotating search; every spawn is on the connected floor component.
            for (size_t i = 0; i < g_level.reachableCells.size(); ++i)
            {
                int cell = g_level.reachableCells[(i + g_level.kills * 17u) % g_level.reachableCells.size()];
                tutorial::Vector2 position = {cell % kMapSize + .5f, cell / kMapSize + .5f};
                if (tutorial::Distance(position, tutorial::g_state.playerPosition) < 6 ||
                    tutorial::Distance(position, g_level.entry) < 4)
                {
                    continue;
                }
                bool occupied =
                    std::any_of(g_level.enemies.begin(), g_level.enemies.end(), [&](const Enemy &other) {
                        return other.health > 0 && tutorial::Distance(other.position, position) < 1;
                    });
                if (occupied)
                {
                    continue;
                }
                enemy.position = position;
                enemy.health = kEnemyMaxHealth;
                enemy.attackCooldown = 1;
                break;
            }
            continue;
        }
        enemy.attackCooldown -= deltaSeconds;
        MoveEnemy(enemy, deltaSeconds);
        if (g_level.invulnerability <= 0 && enemy.attackCooldown <= 0 &&
            tutorial::Distance(enemy.position, tutorial::g_state.playerPosition) < .8f &&
            HasLineOfSight(enemy.position, tutorial::g_state.playerPosition))
        {
            g_level.player.health -= (std::max)(1, 10 - g_level.player.defense);
            enemy.attackCooldown = 1;
            g_level.invulnerability = .45f;
            if (g_level.player.health <= 0)
            {
                RespawnPlayer();
                break;
            }
        }
    }
}
} // namespace levelone
