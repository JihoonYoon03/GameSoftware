#include "stdafx.h"
#include "Profiler.h"
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
constexpr float kPi = 3.14159265f;
tutorial::Vector2 Project(tutorial::Vector2 p, float height = 0)
{
    const auto camera = tutorial::g_state.cameraPosition;
    return {tutorial::kCanvasWidth * .5f + (p.x - p.y - camera.x + camera.y) * 36,
            tutorial::kCanvasHeight * .57f + (p.x + p.y - camera.x - camera.y) * 18 - height};
}
void ClearInput()
{
    std::fill(tutorial::g_state.pressedKeys, tutorial::g_state.pressedKeys + 256, false);
    tutorial::g_state.mouseFiring = false;
}
float WalkRoute(tutorial::Vector2 &position, const std::vector<tutorial::Vector2> &route, size_t &index,
                float amount)
{
    float travelled = 0;
    while (index < route.size() && amount > 0)
    {
        auto target = route[index];
        float distance = tutorial::Distance(position, target);
        if (distance < .025f)
        {
            ++index;
            continue;
        }
        float step = (std::min)(amount, distance);
        tutorial::Vector2 next = {position.x + (target.x - position.x) / distance * step,
                                  position.y + (target.y - position.y) / distance * step};
        if (IsBlocked(next))
        {
            break;
        }
        position = next;
        travelled += step;
        amount -= step;
        if (step >= distance)
        {
            ++index;
        }
    }
    return travelled;
}
void RespawnPlayer()
{
    g_level.player.health = g_level.player.maxHealth;
    tutorial::g_state.playerPosition = g_level.entry;
    tutorial::g_state.cameraPosition = g_level.entry;
    g_level.invulnerability = 3;
    g_level.navigationCooldown = 0;
    g_level.bullets.clear();
    g_level.tracers.clear();
    g_level.pistol.Reset();
    ClearInput();
    tutorial::ShowNotice("리아 / 입구로 구조했어. 조사 기록·구조 성과·경험치는 그대로야.");
}
void MoveEnemy(Enemy &enemy, float deltaSeconds)
{
    int x = int(std::floor(enemy.position.x)), y = int(std::floor(enemy.position.y));
    if (!IsWalkable(x, y) || g_level.navigationDistances.empty())
    {
        return;
    }
    int best = g_level.navigationDistances[y * kMapSize + x];
    if (best < 0 || best > 19 ||
        tutorial::Distance(tutorial::g_state.playerPosition, g_level.entry) < kSafeAreaRadius)
    {
        return;
    }
    auto target = tutorial::g_state.playerPosition;
    if (!HasLineOfSight(enemy.position, target))
    {
        target = {x + .5f, y + .5f};
        constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &direction : directions)
        {
            int nx = x + direction[0], ny = y + direction[1];
            if (IsWalkable(nx, ny))
            {
                int distance = g_level.navigationDistances[ny * kMapSize + nx];
                if (distance >= 0 && distance < best)
                {
                    best = distance;
                    target = {nx + .5f, ny + .5f};
                }
            }
        }
    }
    float distance = tutorial::Distance(enemy.position, target);
    if (distance < .05f)
    {
        return;
    }
    float step = (std::min)(distance, 1.35f * deltaSeconds);
    auto next = tutorial::Vector2{enemy.position.x + (target.x - enemy.position.x) / distance * step,
                                  enemy.position.y + (target.y - enemy.position.y) / distance * step};
    if (tutorial::Distance(next, g_level.entry) >= kSafeAreaRadius && !IsBlocked(next))
    {
        enemy.position = next;
    }
    else if (tutorial::Distance(next, g_level.entry) >= kSafeAreaRadius)
    {
        if (!IsBlocked({next.x, enemy.position.y}))
        {
            enemy.position.x = next.x;
        }
        if (!IsBlocked({enemy.position.x, next.y}))
        {
            enemy.position.y = next.y;
        }
    }
}
void UpdateBullets(float deltaSeconds)
{
    for (auto &bullet : g_level.bullets)
    {
        auto start = bullet.position;
        float movement = (std::min)(bullet.remaining, bullet.speed * deltaSeconds);
        // Swept substeps prevent tunnelling through walls or enemies on slow frames.
        int steps = (std::max)(1, int(std::ceil(movement / .08f)));
        float step = movement / steps;
        for (int i = 0; i < steps && bullet.remaining > 0; ++i)
        {
            tutorial::Vector2 next = {bullet.position.x + bullet.direction.x * step,
                                      bullet.position.y + bullet.direction.y * step};
            if (!IsWalkable(int(std::floor(next.x)), int(std::floor(next.y))))
            {
                bullet.remaining = 0;
                break;
            }
            bullet.position = next;
            bullet.remaining = (std::max)(0.f, bullet.remaining - step);
            for (size_t j = 0; j < g_level.enemies.size(); ++j)
            {
                if (g_level.enemies[j].health > 0 &&
                    tutorial::Distance(next, g_level.enemies[j].position) < .4f)
                {
                    DamageEnemy(j, bullet.damage);
                    bullet.remaining = 0;
                    break;
                }
            }
        }
        g_level.tracers.push_back({start, bullet.position});
    }
    auto &bullets = g_level.bullets;
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                                 [](const Bullet &bullet) { return bullet.remaining <= 0; }),
                  bullets.end());
    if (g_level.tracers.size() > 256)
    {
        g_level.tracers.erase(g_level.tracers.begin(), g_level.tracers.end() - 256);
    }
}
void UpdatePeople(float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::NpcUpdate);
    g_level.riaNavigationCooldown -= deltaSeconds;
    g_level.riaMovement = 0;
    if (g_level.riaNavigationCooldown <= 0)
    {
        g_level.riaNavigationCooldown = .65f;
        if (tutorial::Distance(g_level.riaPosition, tutorial::g_state.playerPosition) > 2)
        {
            g_level.riaRoute = FindRoute(g_level.riaPosition, tutorial::g_state.playerPosition);
            g_level.riaRouteIndex = 0;
        }
        else
        {
            g_level.riaRoute.clear();
            g_level.riaRouteIndex = 0;
        }
    }
    g_level.riaMovement =
        WalkRoute(g_level.riaPosition, g_level.riaRoute, g_level.riaRouteIndex, 4.8f * deltaSeconds);
    for (auto &npc : g_level.survivors)
    {
        if (!npc.evacuating)
        {
            continue;
        }
        npc.walkedDistance += WalkRoute(npc.position, npc.route, npc.routeIndex, 2.1f * deltaSeconds);
        auto screen = Project(npc.position, 25);
        bool outside = screen.x < -240 || screen.x > tutorial::kCanvasWidth + 240 || screen.y < -200 ||
                       screen.y > tutorial::kCanvasHeight + 200;
        npc.outsideSeconds = outside && npc.walkedDistance > 4 ? npc.outsideSeconds + deltaSeconds : 0;
    }
    auto &people = g_level.survivors;
    people.erase(std::remove_if(people.begin(), people.end(),
                                [](const RescueNpc &npc) {
                                    if (npc.evacuating && npc.outsideSeconds > 1.2f)
                                    {
                                        ++g_level.returnedPeople;
                                        return true;
                                    }
                                    return false;
                                }),
                 people.end());
}
bool Near(tutorial::Vector2 position)
{
    return tutorial::Distance(tutorial::g_state.playerPosition, position) < 1.8f &&
           HasLineOfSight(tutorial::g_state.playerPosition, position);
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
    auto &stats = g_level.player;
    stats.experience = (std::min)(297000, stats.experience + (std::max)(0, amount));
    int old = stats.level;
    while (stats.level < 100 && stats.experience >= ExperienceForLevel(stats.level + 1))
    {
        ++stats.level;
    }
    stats.maxHealth = 100 + (stats.level - 1) * 20;
    stats.attack = 18 + (stats.level - 1) * 4;
    stats.defense = 2 + (stats.level - 1) * 2;
    if (old != stats.level)
    {
        stats.health = stats.maxHealth;
        tutorial::ShowNotice(
            "성장 / 최대 체력·총기 피해·방어력이 상승했습니다. 조사를 계속하거나 사냥할 수 있습니다.");
    }
}
void Enter(bool debugEntry)
{
    if (debugEntry && g_level.quest.GetStage() < MineQuestStage::TravelToMine)
    {
        g_level.quest.Advance(MineQuestStage::ShowRecord);
        g_level.quest.Advance(MineQuestStage::CheckCityRelay);
    }
    if (!g_level.quest.BeginJourney())
    {
        tutorial::ShowNotice("리아 / 신호 기록을 보여 주고 도시 수신망에서 광산 중계 흔적부터 확인하자.");
        return;
    }
    g_level.quest.Advance(MineQuestStage::TravelToMine);
    if (!g_level.generated)
    {
        GenerateMap(std::random_device{}());
    }
    g_level.active = true;
    tutorial::g_state.isInsideHome = false;
    tutorial::g_state.isInObservatory = false;
    tutorial::g_state.isJetTravel = false;
    tutorial::g_state.playerPosition = g_level.entry;
    tutorial::g_state.cameraPosition = g_level.entry;
    g_level.riaPosition = {g_level.entry.x + 1, g_level.entry.y + 1};
    g_level.riaRoute.clear();
    g_level.riaRouteIndex = 0;
    g_level.riaNavigationCooldown = 0;
    g_level.bullets.clear();
    g_level.tracers.clear();
    g_level.scannerSeconds = 0;
    ClearInput();
    g_level.invulnerability = 2;
    g_level.navigationCooldown = 0;
    tutorial::ShowNotice(
        "리아 / 수신망 주소가 이 광산이야. 전력부터 우회하자. 좌클릭 사격, R 재장전, F 구조 스캔.");
}
void Leave()
{
    g_level.quest.Advance(MineQuestStage::ReturnToHalo);
    g_level.active = false;
    g_level.bullets.clear();
    g_level.tracers.clear();
    g_level.scannerSeconds = 0;
    ClearInput();
    tutorial::g_state.isInsideHome = false;
    tutorial::g_state.isInObservatory = false;
    tutorial::g_state.isJetTravel = false;
    tutorial::g_state.playerPosition = tutorial::kJetBikePosition;
    tutorial::g_state.cameraPosition = tutorial::kJetBikePosition;
    tutorial::ShowNotice(
        g_level.quest.GetStage() == MineQuestStage::Complete
            ? "낙하한 신호 완료 / 중계 좌표를 헤일로에 보고했습니다. N-0 외곽 설비가 다음 조사 대상입니다."
            : "헤일로 / 조사와 구조 기록은 유지됩니다. 준비되면 바이크로 광산에 다시 갈 수 있습니다.");
}
void ResetProgress()
{
    auto &scene = tutorial::GetWorldScene(tutorial::WorldSceneKind::Mine);
    scene.BeginPlacement();
    scene.EndPlacement();
    scene.GetGraph().Clear();
    g_level = State{};
    tutorial::g_state.mouseFiring = false;
}
void RestartMine()
{
    ResetProgress();
    Enter(true);
}
float GetAimAngle()
{
    auto origin = Project(tutorial::g_state.playerPosition, 26);
    return std::atan2(tutorial::g_state.cursorPosition.y - origin.y,
                      tutorial::g_state.cursorPosition.x - origin.x);
}
tutorial::Vector2 GetAimDirection(float angle)
{
    float sx = std::cos(angle), sy = std::sin(angle);
    tutorial::Vector2 direction = {sx / 72 + sy / 36, sy / 36 - sx / 72};
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    return {direction.x / length, direction.y / length};
}
void Attack()
{
    if (g_level.bullets.size() >= 128)
    {
        return;
    }
    combat::Shot shot;
    if (!g_level.pistol.TryFire(GetAimAngle(), shot))
    {
        return;
    }
    auto direction = GetAimDirection(shot.angle);
    auto position = tutorial::g_state.playerPosition;
    // Start at the body, then sweep the complete muzzle path instead of spawning through a wall.
    g_level.bullets.push_back(
        {position, direction, shot.speed, shot.range, shot.damage + (g_level.player.level - 1) * 4});
    g_level.muzzleFlash = .055f;
}
void Reload()
{
    if (g_level.pistol.Reload())
    {
        tutorial::ShowNotice("권총 / 재장전 중. 예비 탄창은 제한 없이 보급됩니다.");
    }
}
void Scan()
{
    if (g_level.scannerCooldown > 0)
    {
        return;
    }
    g_level.scannerSeconds = 2.8f;
    g_level.scannerCooldown = 6;
    g_level.scannerContacts.clear();
    auto origin = tutorial::g_state.playerPosition;
    for (size_t i = 0; i < g_level.rescueGroups.size(); ++i)
    {
        const auto &group = g_level.rescueGroups[i];
        float distance = tutorial::Distance(origin, group.position);
        if (group.rescued || distance > 42)
        {
            continue;
        }
        float dx = (group.position.x - origin.x) - (group.position.y - origin.y);
        float dy = ((group.position.x - origin.x) + (group.position.y - origin.y)) * .5f;
        float angle = std::round(std::atan2(dy, dx) / (kPi / 8)) * (kPi / 8);
        float approximateDistance = (std::min)(42.f, std::floor(distance / 6) * 6 + 3);
        g_level.scannerContacts.push_back({angle, approximateDistance, i});
    }
    tutorial::ShowNotice(
        "스캐너 / 실제 구조 요청의 대략적인 방향·거리만 표시합니다. 펄스가 지나간 뒤 사라집니다.");
}
bool DamageEnemy(size_t index, int damage)
{
    if (index >= g_level.enemies.size() || damage <= 0 || g_level.enemies[index].health <= 0)
    {
        return false;
    }
    auto &enemy = g_level.enemies[index];
    enemy.health = (std::max)(0, enemy.health - damage);
    if (enemy.health > 0)
    {
        return false;
    }
    ++g_level.totalEnemyDeaths;
    ++g_level.kills;
    enemy.respawnSeconds = 15;
    g_level.drops.push_back({enemy.position,
                             g_level.totalEnemyDeaths % 3 == 0 ? ItemKind::RecoveryKit : ItemKind::Salvage,
                             g_level.nextDropActorId++});
    if (g_level.drops.size() > 128)
    {
        g_level.drops.erase(g_level.drops.begin());
    }
    AddExperience(kExperiencePerKill);
    return true;
}
void Interact()
{
    if (Near(kPowerBypass))
    {
        if (g_level.quest.Advance(MineQuestStage::BypassPower))
        {
            g_level.navigationCooldown = 0;
            tutorial::ShowNotice(
                "리아 / 끊긴 선로를 우회했어. 동쪽 격벽이 열렸고 유적 쪽 비상등이 들어왔어.");
        }
        else
        {
            tutorial::ShowNotice("배전반 / 비상 전력 우회 가동 중. 동쪽 격벽 너머를 조사하세요.");
        }
        return;
    }
    if (Near(kRuinSignal))
    {
        if (g_level.quest.Advance(MineQuestStage::InvestigateSignal))
        {
            tutorial::ShowNotice("리아 / 오래전 실종 탐사선의 구조 신호야. 인간의 통신선은 벽 앞에서 "
                                 "끝나는데 신호는 안쪽에서 와.");
        }
        else
        {
            tutorial::ShowNotice(
                "구조 신호 / 실종 탐사선 식별자. 신호의 원인과 발신자의 정체는 아직 미확인.");
        }
        return;
    }
    if (Near(kObserverGlyph))
    {
        if (g_level.quest.Advance(MineQuestStage::InvestigateGlyph))
        {
            tutorial::ShowNotice("리아 / 닮았다는 것하고, 널 가리킨다는 건 다른 얘기야. 아직은. 안쪽 단말에 "
                                 "중계 좌표가 있어.");
        }
        else
        {
            tutorial::ShowNotice(
                "관측자 문양 / 플레이어와 닮은 형상. 먼저 가까운 구조 신호의 기록을 조사하세요.");
        }
        return;
    }
    if (Near(kCoordinateTerminal))
    {
        if (g_level.quest.Advance(MineQuestStage::CollectCoordinates))
        {
            tutorial::ShowNotice("중계 좌표 확보 / N-0 외곽 폐설비와 연결됩니다. 입구로 귀환해 헤일로에 "
                                 "보고하세요. 구조·사냥은 계속 가능합니다.");
        }
        else
        {
            tutorial::ShowNotice(g_level.quest.AreCoordinatesFound()
                                     ? "단말 / 중계 좌표를 이미 확보했습니다."
                                     : "리아 / 먼저 구조 신호와 관측자 문양을 조사하자.");
        }
        return;
    }
    for (auto &group : g_level.rescueGroups)
    {
        size_t index = size_t(&group - g_level.rescueGroups.data());
        bool nearMember =
            std::any_of(g_level.survivors.begin(), g_level.survivors.end(),
                        [index](const RescueNpc &npc) { return npc.group == index && Near(npc.position); });
        if (!group.rescued && (Near(group.position) || nearMember))
        {
            group.rescued = true;
            for (auto &npc : g_level.survivors)
            {
                if (npc.group == index)
                {
                    npc.route = FindRoute(npc.position, g_level.entry);
                    npc.routeIndex = 0;
                    npc.evacuating = true;
                    ++g_level.rescuedPeople;
                }
            }
            tutorial::ShowNotice(
                group.combatants ? "고립 전투원 / 실제 구조 요청을 찾아줘서 고맙습니다. 열린 갱도를 따라 "
                                   "헤일로로 철수하겠습니다."
                                 : "광부 / 우리 신호가 닿았군요! 알려준 귀환로로 마을까지 걸어가겠습니다.");
            return;
        }
    }
    if (Near(g_level.entry))
    {
        Leave();
        return;
    }
    int collected = 0;
    auto &drops = g_level.drops;
    drops.erase(std::remove_if(drops.begin(), drops.end(),
                               [&](const Drop &drop) {
                                   if (!Near(drop.position))
                                   {
                                       return false;
                                   }
                                   if (drop.kind == ItemKind::RecoveryKit)
                                   {
                                       g_level.player.recoveryKits =
                                           (std::min)(999, g_level.player.recoveryKits + 1);
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
        tutorial::ShowNotice("회수 / 부품과 회복 키트를 챙겼습니다.");
    }
    else if (Near(g_level.riaPosition))
    {
        tutorial::ShowNotice(std::string("리아 / ") + g_level.quest.GetObjective());
    }
    else
    {
        tutorial::ShowNotice("E 조사·구조·회수 / F 구조 신호 스캔. 불빛과 광산의 주 갱도를 따라 이동하세요.");
    }
}
void UseRecoveryKit()
{
    auto &stats = g_level.player;
    if (stats.recoveryKits > 0 && stats.health < stats.maxHealth)
    {
        --stats.recoveryKits;
        stats.health = (std::min)(stats.maxHealth, stats.health + 45);
        tutorial::ShowNotice("회복 키트 / 체력이 45 회복되었습니다.");
    }
}
void Update(float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::Combat);
    g_level.pistol.Update(deltaSeconds);
    float angle = GetAimAngle();
    float aimX = std::cos(angle), aimY = std::sin(angle);
    tutorial::g_state.facingDirection =
        std::fabs(aimX) > std::fabs(aimY) ? (aimX < 0 ? 1 : 2) : (aimY < 0 ? 3 : 0);
    g_level.invulnerability = (std::max)(0.f, g_level.invulnerability - deltaSeconds);
    g_level.scannerSeconds = (std::max)(0.f, g_level.scannerSeconds - deltaSeconds);
    g_level.scannerCooldown = (std::max)(0.f, g_level.scannerCooldown - deltaSeconds);
    g_level.muzzleFlash = (std::max)(0.f, g_level.muzzleFlash - deltaSeconds);
    for (auto &tracer : g_level.tracers)
    {
        tracer.remaining -= deltaSeconds;
    }
    auto &tracers = g_level.tracers;
    tracers.erase(std::remove_if(tracers.begin(), tracers.end(),
                                 [](const Tracer &tracer) { return tracer.remaining <= 0; }),
                  tracers.end());
    if (tutorial::g_state.mouseFiring)
    {
        Attack();
    }
    UpdateBullets(deltaSeconds);
    UpdatePeople(deltaSeconds);
    g_level.navigationCooldown -= deltaSeconds;
    if (g_level.navigationCooldown <= 0)
    {
        UpdateNavigation();
        g_level.navigationCooldown = .3f;
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
            for (size_t i = 0; i < g_level.reachableCells.size(); ++i)
            {
                int cell =
                    g_level
                        .reachableCells[(i + g_level.totalEnemyDeaths * 17u) % g_level.reachableCells.size()];
                tutorial::Vector2 position = {cell % kMapSize + .5f, cell / kMapSize + .5f};
                if (!IsWalkable(cell % kMapSize, cell / kMapSize) ||
                    tutorial::Distance(position, tutorial::g_state.playerPosition) < 10 ||
                    tutorial::Distance(position, g_level.entry) < kEnemySpawnRadius ||
                    LightAt(position) > .2f)
                {
                    continue;
                }
                bool occupied =
                    std::any_of(g_level.enemies.begin(), g_level.enemies.end(), [&](const Enemy &other) {
                        return other.health > 0 && tutorial::Distance(other.position, position) < 1;
                    });
                if (!occupied)
                {
                    enemy.position = position;
                    enemy.health = kEnemyMaxHealth;
                    enemy.attackCooldown = 1;
                    break;
                }
            }
            continue;
        }
        enemy.attackCooldown -= deltaSeconds;
        MoveEnemy(enemy, deltaSeconds);
        if (enemy.attackCooldown <= 0 && g_level.invulnerability <= 0 &&
            tutorial::Distance(tutorial::g_state.playerPosition, g_level.entry) >= kSafeAreaRadius &&
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
