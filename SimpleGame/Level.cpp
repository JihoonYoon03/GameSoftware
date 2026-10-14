#include "stdafx.h"
#include "Level.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>

namespace game
{

// ============================================================================
// MineQuest
// ============================================================================
MineQuestStage MineQuest::GetStage() const
{
    return stage;
}
bool MineQuest::Advance(MineQuestStage expected)
{
    if (stage != expected || stage == MineQuestStage::Complete ||
        (expected == MineQuestStage::TravelToMine && !riaJoined))
    {
        return false;
    }
    stage = static_cast<MineQuestStage>(static_cast<int>(stage) + 1);
    return true;
}
bool MineQuest::BeginJourney()
{
    if (stage < MineQuestStage::TravelToMine)
    {
        return false;
    }
    riaJoined = true;
    return true;
}
bool MineQuest::IsRiaJoined() const
{
    return riaJoined;
}
bool MineQuest::IsPowerBypassed() const
{
    return stage >= MineQuestStage::InvestigateSignal;
}
bool MineQuest::AreCoordinatesFound() const
{
    return stage >= MineQuestStage::ReturnToHalo;
}
const char *MineQuest::GetObjective() const
{
    switch (stage)
    {
    case MineQuestStage::ShowRecord:
        return "리아에게 관문 신호 기록 보여 주기 [E]";
    case MineQuestStage::CheckCityRelay:
        return "도시 수신망 단말에서 중계 흔적 확인 [E]";
    case MineQuestStage::TravelToMine:
        return "리아와 제트 바이크로 광산 이동 [E]";
    case MineQuestStage::BypassPower:
        return "주 갱도 끝의 비상 배전반에서 전력 우회 [E]";
    case MineQuestStage::InvestigateSignal:
        return "격벽 너머 지하 유적의 구조 신호 조사 [E]";
    case MineQuestStage::InvestigateGlyph:
        return "신호 옆의 관측자 문양 조사 [E]";
    case MineQuestStage::CollectCoordinates:
        return "유적 안쪽 단말에서 중계 좌표 확보 [E]";
    case MineQuestStage::ReturnToHalo:
        return "광산 입구로 돌아가 헤일로에 보고 [E]";
    case MineQuestStage::Complete:
        return "낙하한 신호 완료 / 자유 탐사·구조·사냥 가능";
    }
    return "광산 조사";
}
namespace
{
constexpr float kCruiseSpeed = 58;
constexpr float kBoostSpeed = 108;
constexpr float kMinimumSpeed = 22;
constexpr float kSteeringRate = 2.8f;
constexpr float kBikeScreenX = 400;
constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr float kPi = tutorial::kPi;
} // namespace

// ============================================================================
// MineMap
// ============================================================================
MineMap::MineMap(MineLevel &owner) : level(owner), state(owner.GetState())
{
}
void MineMap::Carve(int x, int y)
{
    if (x > 0 && y > 0 && x < kMapSize - 1 && y < kMapSize - 1)
    {
        state.tiles[y * kMapSize + x] = Tile::Floor;
    }
}

void MineMap::Room(int left, int top, int right, int bottom)
{
    for (int y = top; y <= bottom; ++y)
    {
        for (int x = left; x <= right; ++x)
        {
            Carve(x, y);
        }
    }
}

void MineMap::Tunnel(int x, int y, int targetX, int targetY)
{
    for (;;)
    {
        Room(x - 1, y - 1, x + 1, y + 1);
        if (x == targetX && y == targetY)
        {
            break;
        }
        if (x != targetX)
        {
            x += x < targetX ? 1 : -1;
        }
        else
        {
            y += y < targetY ? 1 : -1;
        }
    }
}

std::vector<int> MineMap::Distances(tutorial::Vector2 destination, bool ignoreGate)
{
    std::vector<int> distances(kMapSize * kMapSize, -1);
    int x = int(std::floor(destination.x)), y = int(std::floor(destination.y));
    if (!IsWalkable(x, y))
    {
        return distances;
    }
    std::queue<int> frontier;
    frontier.push(y * kMapSize + x);
    distances[y * kMapSize + x] = 0;
    while (!frontier.empty())
    {
        int cell = frontier.front();
        frontier.pop();
        for (const auto &direction : kDirections)
        {
            int nx = cell % kMapSize + direction[0], ny = cell / kMapSize + direction[1];
            if (nx < 0 || nx >= kMapSize || ny < 0 || ny >= kMapSize)
            {
                continue;
            }
            int next = ny * kMapSize + nx;
            bool walkable = ignoreGate ? state.tiles[next] != Tile::Wall : IsWalkable(nx, ny);
            if (walkable && distances[next] < 0)
            {
                distances[next] = distances[cell] + 1;
                frontier.push(next);
            }
        }
    }
    return distances;
}

bool MineMap::IsGateCell(int x, int y)
{
    return x == 32 && y >= 28 && y <= 30;
}

bool MineMap::IsWalkable(int x, int y)
{
    return x >= 0 && y >= 0 && x < kMapSize && y < kMapSize && state.tiles.size() == kMapSize * kMapSize &&
           state.tiles[y * kMapSize + x] != Tile::Wall &&
           (!IsGateCell(x, y) || state.quest.IsPowerBypassed());
}

bool MineMap::IsBlocked(tutorial::Vector2 position)
{
    profiling::Count(profiling::Counter::CollisionQueries);
    constexpr float radius = .18f;
    for (float dx : {-radius, radius})
    {
        for (float dy : {-radius, radius})
        {
            if (!IsWalkable(int(std::floor(position.x + dx)), int(std::floor(position.y + dy))))
            {
                return true;
            }
        }
    }
    return false;
}

bool MineMap::HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to)
{
    profiling::Count(profiling::Counter::LineOfSightQueries);
    int steps = (std::max)(1, int(tutorial::Game::Distance(from, to) / .12f));
    for (int i = 0; i <= steps; ++i)
    {
        float t = float(i) / steps;
        if (!IsWalkable(int(std::floor(from.x + (to.x - from.x) * t)),
                        int(std::floor(from.y + (to.y - from.y) * t))))
        {
            return false;
        }
    }
    return true;
}

std::vector<tutorial::Vector2> MineMap::FindRoute(tutorial::Vector2 from, tutorial::Vector2 to)
{
    profiling::Scope timer(profiling::Timer::NpcNavigation);
    profiling::Count(profiling::Counter::NpcPathRequests);
    auto distances = Distances(to);
    std::vector<tutorial::Vector2> route;
    int x = int(std::floor(from.x)), y = int(std::floor(from.y));
    if (!IsWalkable(x, y) || distances[y * kMapSize + x] < 0)
    {
        return route;
    }
    // First reach the current cell center; turns then remain clear of wall corners.
    route.push_back({x + .5f, y + .5f});
    while (distances[y * kMapSize + x] > 0)
    {
        int current = distances[y * kMapSize + x];
        for (const auto &direction : kDirections)
        {
            int nx = x + direction[0], ny = y + direction[1];
            if (IsWalkable(nx, ny) && distances[ny * kMapSize + nx] == current - 1)
            {
                x = nx;
                y = ny;
                route.push_back({x + .5f, y + .5f});
                profiling::Count(profiling::Counter::NpcPathVisitedCells);
                break;
            }
        }
    }
    return route;
}

void MineMap::UpdateNavigation()
{
    auto &world = tutorial::Game::Get().GetState();
    profiling::Scope timer(profiling::Timer::Navigation);
    state.navigationDistances = Distances(world.playerPosition);
}

void MineMap::Generate(std::uint32_t seed)
{
    profiling::Scope timer(profiling::Timer::MapGeneration);
    state.mapRevision = ++level.generationRevision;
    state.seed = seed;
    state.tiles.assign(kMapSize * kMapSize, Tile::Wall);
    // Authored mine topology: branching drifts, one closed bulkhead, deeper ruins.
    // No four-team test arena or randomly scattered repair facilities remain.
    Room(5, 5, 13, 13);
    Tunnel(8, 10, 24, 10);
    Tunnel(24, 10, 24, 29);
    Room(22, 22, 29, 31);
    Tunnel(24, 29, 48, 29);
    Tunnel(48, 29, 48, 47);
    Room(42, 42, 55, 54);
    Room(13, 17, 21, 24);
    Tunnel(17, 20, 24, 20);
    Tunnel(12, 20, 12, 43);
    Tunnel(12, 20, 17, 20);
    Room(7, 40, 16, 47);
    Room(40, 11, 47, 18);
    Tunnel(44, 15, 44, 29);
    Room(49, 33, 57, 40);
    Tunnel(48, 36, 54, 36);
    // Wall x=32 can only be crossed at the powered bulkhead.
    for (int y = 1; y < kMapSize - 1; ++y)
    {
        if (!IsGateCell(32, y))
        {
            state.tiles[y * kMapSize + 32] = Tile::Wall;
        }
    }
    auto connected = Distances(state.entry, true);
    state.reachableCells.clear();
    std::mt19937 random(seed);
    for (int cell = 0; cell < kMapSize * kMapSize; ++cell)
    {
        if (connected[cell] < 0)
        {
            state.tiles[cell] = Tile::Wall;
        }
        else
        {
            state.reachableCells.push_back(cell);
        }
    }
    state.lights = {{state.entry, 4.8f},       {{17.5f, 10.5f}, 2.8f}, {{17.5f, 20.5f}, 3.2f},
                    {{11.5f, 43.5f}, 3.2f},    {kPowerBypass, 3.5f},   {{32.5f, 29.5f}, 4.f, true},
                    {{44.5f, 15.5f}, 3.2f},    {{53.5f, 36.5f}, 3.2f}, {kRuinSignal, 3.8f},
                    {kCoordinateTerminal, 3.f}};
    state.rescueGroups = {
        {{17.5f, 20.5f}, false}, {{11.5f, 43.5f}, true}, {{44.5f, 15.5f}, false}, {{53.5f, 36.5f}, true}};
    state.survivors.clear();
    std::uint64_t id = 1;
    for (size_t i = 0; i < state.rescueGroups.size(); ++i)
    {
        auto center = state.rescueGroups[i].position;
        for (int member = 0; member < 3; ++member)
        {
            state.survivors.push_back({id++, i, {center.x + (member - 1) * .6f, center.y + .5f}});
        }
    }
    std::shuffle(state.reachableCells.begin(), state.reachableCells.end(), random);
    state.enemies.clear();
    for (int cell : state.reachableCells)
    {
        tutorial::Vector2 position = {cell % kMapSize + .5f, cell / kMapSize + .5f};
        if (tutorial::Game::Distance(position, state.entry) < kEnemySpawnRadius ||
            tutorial::Game::Distance(position, kPowerBypass) < 2 ||
            IsGateCell(cell % kMapSize, cell / kMapSize))
        {
            continue;
        }
        state.enemies.push_back({position});
        if (state.enemies.size() == kEnemyPopulation)
        {
            break;
        }
    }
    state.drops.clear();
    state.bullets.clear();
    state.tracers.clear();
    state.scannerContacts.clear();
    state.riaPosition = {9.5f, 10.5f};
    state.riaRoute.clear();
    state.riaRouteIndex = 0;
    UpdateNavigation();
    state.generated = true;
}

// ============================================================================
// MineCombat
// ============================================================================
MineCombat::MineCombat(MineLevel &owner) : level(owner), state(owner.GetState())
{
}
void MineCombat::RespawnPlayer()
{
    auto &world = tutorial::Game::Get().GetState();
    state.player.health = state.player.maxHealth;
    world.playerPosition = state.entry;
    world.cameraPosition = state.entry;
    state.invulnerability = 3;
    state.navigationCooldown = 0;
    state.bullets.clear();
    state.tracers.clear();
    state.pistol.Reset();
    level.ClearInput();
    tutorial::Game::Get().ShowNotice("리아 / 입구로 구조했어. 조사 기록·구조 성과·경험치는 그대로야.");
}

void MineCombat::MoveEnemy(Enemy &enemy, float deltaSeconds)
{
    auto &world = tutorial::Game::Get().GetState();
    int x = int(std::floor(enemy.position.x)), y = int(std::floor(enemy.position.y));
    if (!level.Map().IsWalkable(x, y) || state.navigationDistances.empty())
    {
        return;
    }
    int best = state.navigationDistances[y * kMapSize + x];
    if (best < 0 || best > 19 ||
        tutorial::Game::Distance(world.playerPosition, state.entry) < kSafeAreaRadius)
    {
        return;
    }
    auto target = world.playerPosition;
    if (!level.Map().HasLineOfSight(enemy.position, target))
    {
        target = {x + .5f, y + .5f};
        constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &direction : directions)
        {
            int nx = x + direction[0], ny = y + direction[1];
            if (level.Map().IsWalkable(nx, ny))
            {
                int distance = state.navigationDistances[ny * kMapSize + nx];
                if (distance >= 0 && distance < best)
                {
                    best = distance;
                    target = {nx + .5f, ny + .5f};
                }
            }
        }
    }
    float distance = tutorial::Game::Distance(enemy.position, target);
    if (distance < .05f)
    {
        return;
    }
    float step = (std::min)(distance, 1.35f * deltaSeconds);
    auto next = tutorial::Vector2{enemy.position.x + (target.x - enemy.position.x) / distance * step,
                                  enemy.position.y + (target.y - enemy.position.y) / distance * step};
    if (tutorial::Game::Distance(next, state.entry) >= kSafeAreaRadius && !level.Map().IsBlocked(next))
    {
        enemy.position = next;
    }
    else if (tutorial::Game::Distance(next, state.entry) >= kSafeAreaRadius)
    {
        if (!level.Map().IsBlocked({next.x, enemy.position.y}))
        {
            enemy.position.x = next.x;
        }
        if (!level.Map().IsBlocked({enemy.position.x, next.y}))
        {
            enemy.position.y = next.y;
        }
    }
}

void MineCombat::AdvanceBullet(Bullet &bullet, float movement)
{
    // Swept substeps prevent tunnelling through walls or enemies on slow frames.
    int steps = (std::max)(1, int(std::ceil(movement / .08f)));
    float step = movement / steps;
    for (int i = 0; i < steps && bullet.remaining > 0; ++i)
    {
        tutorial::Vector2 next = {bullet.position.x + bullet.direction.x * step,
                                  bullet.position.y + bullet.direction.y * step};
        if (!level.Map().IsWalkable(int(std::floor(next.x)), int(std::floor(next.y))))
        {
            bullet.remaining = 0;
            break;
        }
        bullet.position = next;
        bullet.remaining = (std::max)(0.f, bullet.remaining - step);
        for (size_t j = 0; j < state.enemies.size(); ++j)
        {
            if (state.enemies[j].health > 0 &&
                tutorial::Game::Distance(next, state.enemies[j].position) < .4f)
            {
                DamageEnemy(j, bullet.damage);
                bullet.remaining = 0;
                break;
            }
        }
    }
}
void MineCombat::UpdateBullets(float deltaSeconds)
{
    for (auto &bullet : state.bullets)
    {
        auto start = bullet.position;
        AdvanceBullet(bullet, (std::min)(bullet.remaining, bullet.speed * deltaSeconds));
        state.tracers.push_back({start, bullet.position});
    }
    auto &bullets = state.bullets;
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                                 [](const Bullet &bullet) { return bullet.remaining <= 0; }),
                  bullets.end());
    if (state.tracers.size() > 256)
    {
        state.tracers.erase(state.tracers.begin(), state.tracers.end() - 256);
    }
}

void MineCombat::Attack()
{
    auto &world = tutorial::Game::Get().GetState();
    if (state.bullets.size() >= 128)
    {
        return;
    }
    combat::Shot shot;
    if (!state.pistol.TryFire(level.GetAimAngle(), shot))
    {
        return;
    }
    auto aim = level.GetAimDirection(level.GetAimAngle());
    auto pose = models::ModelRig::Pose(0, false, true, 1);
    auto muzzle = models::ModelRig::Muzzle(pose);
    float muzzleDistance = std::sqrt(muzzle.x * muzzle.x + muzzle.y * muzzle.y);
    Bullet bullet = {world.playerPosition, aim, shot.speed, shot.range,
                     shot.damage + (state.player.level - 1) * 4};
    // A gun pressed against a wall cannot spawn a projectile on its far side.
    AdvanceBullet(bullet, muzzleDistance);
    if (bullet.remaining > 0)
    {
        bullet.direction = level.GetAimDirection(shot.angle);
        state.bullets.push_back(bullet);
    }
    state.muzzleFlash = .055f;
}

void MineCombat::Reload()
{
    if (state.pistol.Reload())
    {
        tutorial::Game::Get().ShowNotice("권총 / 재장전 중. 예비 탄창은 제한 없이 보급됩니다.");
    }
}

bool MineCombat::DamageEnemy(size_t index, int damage)
{
    if (index >= state.enemies.size() || damage <= 0 || state.enemies[index].health <= 0)
    {
        return false;
    }
    auto &enemy = state.enemies[index];
    enemy.health = (std::max)(0, enemy.health - damage);
    if (enemy.health > 0)
    {
        return false;
    }
    ++state.totalEnemyDeaths;
    ++state.kills;
    enemy.respawnSeconds = 15;
    state.drops.push_back({enemy.position,
                           state.totalEnemyDeaths % 3 == 0 ? ItemKind::RecoveryKit : ItemKind::Salvage,
                           state.nextDropActorId++});
    if (state.drops.size() > 128)
    {
        state.drops.erase(state.drops.begin());
    }
    level.AddExperience(kExperiencePerKill);
    return true;
}

void MineCombat::Update(float deltaSeconds)
{
    auto &world = tutorial::Game::Get().GetState();
    state.pistol.Update(deltaSeconds);
    float angle = level.GetAimAngle();
    float aimX = std::cos(angle), aimY = std::sin(angle);
    world.facingDirection = std::fabs(aimX) > std::fabs(aimY) ? (aimX < 0 ? 1 : 2) : (aimY < 0 ? 3 : 0);
    state.invulnerability = (std::max)(0.f, state.invulnerability - deltaSeconds);
    state.muzzleFlash = (std::max)(0.f, state.muzzleFlash - deltaSeconds);
    for (auto &tracer : state.tracers)
    {
        tracer.remaining -= deltaSeconds;
    }
    auto &tracers = state.tracers;
    tracers.erase(std::remove_if(tracers.begin(), tracers.end(),
                                 [](const Tracer &tracer) { return tracer.remaining <= 0; }),
                  tracers.end());
    if (world.mouseFiring)
    {
        Attack();
    }
    UpdateBullets(deltaSeconds);
}

void MineCombat::UpdateEnemies(float deltaSeconds)
{
    auto &world = tutorial::Game::Get().GetState();
    state.navigationCooldown -= deltaSeconds;
    if (state.navigationCooldown <= 0)
    {
        level.Map().UpdateNavigation();
        state.navigationCooldown = .3f;
    }
    for (auto &enemy : state.enemies)
    {
        if (enemy.health <= 0)
        {
            enemy.respawnSeconds -= deltaSeconds;
            if (enemy.respawnSeconds > 0)
            {
                continue;
            }
            for (size_t i = 0; i < state.reachableCells.size(); ++i)
            {
                int cell =
                    state.reachableCells[(i + state.totalEnemyDeaths * 17u) % state.reachableCells.size()];
                tutorial::Vector2 position = {cell % kMapSize + .5f, cell / kMapSize + .5f};
                if (!level.Map().IsWalkable(cell % kMapSize, cell / kMapSize) ||
                    tutorial::Game::Distance(position, world.playerPosition) < 10 ||
                    tutorial::Game::Distance(position, state.entry) < kEnemySpawnRadius ||
                    level.Lighting().At(position) > .2f)
                {
                    continue;
                }
                bool occupied =
                    std::any_of(state.enemies.begin(), state.enemies.end(), [&](const Enemy &other) {
                        return other.health > 0 && tutorial::Game::Distance(other.position, position) < 1;
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
        if (enemy.attackCooldown <= 0 && state.invulnerability <= 0 &&
            tutorial::Game::Distance(world.playerPosition, state.entry) >= kSafeAreaRadius &&
            tutorial::Game::Distance(enemy.position, world.playerPosition) < .8f &&
            level.Map().HasLineOfSight(enemy.position, world.playerPosition))
        {
            state.player.health -= (std::max)(1, 10 - state.player.defense);
            enemy.attackCooldown = 1;
            state.invulnerability = .45f;
            if (state.player.health <= 0)
            {
                RespawnPlayer();
                break;
            }
        }
    }
}

// ============================================================================
// MineRescue
// ============================================================================
MineRescue::MineRescue(MineLevel &owner) : level(owner), state(owner.GetState())
{
}
float MineRescue::WalkRoute(tutorial::Vector2 &position, const std::vector<tutorial::Vector2> &route,
                            size_t &index, float amount)
{
    float travelled = 0;
    while (index < route.size() && amount > 0)
    {
        auto target = route[index];
        float distance = tutorial::Game::Distance(position, target);
        if (distance < .025f)
        {
            ++index;
            continue;
        }
        float step = (std::min)(amount, distance);
        tutorial::Vector2 next = {position.x + (target.x - position.x) / distance * step,
                                  position.y + (target.y - position.y) / distance * step};
        if (level.Map().IsBlocked(next))
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

void MineRescue::Update(float deltaSeconds)
{
    auto &world = tutorial::Game::Get().GetState();
    state.scannerSeconds = (std::max)(0.f, state.scannerSeconds - deltaSeconds);
    state.scannerCooldown = (std::max)(0.f, state.scannerCooldown - deltaSeconds);
    profiling::Scope timer(profiling::Timer::NpcUpdate);
    state.riaNavigationCooldown -= deltaSeconds;
    state.riaMovement = 0;
    if (state.riaNavigationCooldown <= 0)
    {
        state.riaNavigationCooldown = .65f;
        if (tutorial::Game::Distance(state.riaPosition, world.playerPosition) > 2)
        {
            state.riaRoute = level.Map().FindRoute(state.riaPosition, world.playerPosition);
            state.riaRouteIndex = 0;
        }
        else
        {
            state.riaRoute.clear();
            state.riaRouteIndex = 0;
        }
    }
    state.riaMovement =
        WalkRoute(state.riaPosition, state.riaRoute, state.riaRouteIndex, 4.8f * deltaSeconds);
    for (auto &npc : state.survivors)
    {
        if (!npc.evacuating)
        {
            continue;
        }
        npc.walkedDistance += WalkRoute(npc.position, npc.route, npc.routeIndex, 2.1f * deltaSeconds);
        auto screen = level.Project(npc.position, 25);
        bool outside = screen.x < -240 || screen.x > tutorial::kCanvasWidth + 240 || screen.y < -200 ||
                       screen.y > tutorial::kCanvasHeight + 200;
        npc.outsideSeconds = outside && npc.walkedDistance > 4 ? npc.outsideSeconds + deltaSeconds : 0;
    }
    auto &people = state.survivors;
    people.erase(std::remove_if(people.begin(), people.end(),
                                [this](const RescueNpc &npc) {
                                    if (npc.evacuating && npc.outsideSeconds > 1.2f)
                                    {
                                        ++state.returnedPeople;
                                        return true;
                                    }
                                    return false;
                                }),
                 people.end());
}

void MineRescue::Scan()
{
    auto &world = tutorial::Game::Get().GetState();
    if (state.scannerCooldown > 0)
    {
        return;
    }
    state.scannerSeconds = 2.8f;
    state.scannerCooldown = 6;
    state.scannerContacts.clear();
    auto origin = world.playerPosition;
    for (size_t i = 0; i < state.rescueGroups.size(); ++i)
    {
        const auto &group = state.rescueGroups[i];
        float distance = tutorial::Game::Distance(origin, group.position);
        if (group.rescued || distance > 42)
        {
            continue;
        }
        float dx = (group.position.x - origin.x) - (group.position.y - origin.y);
        float dy = ((group.position.x - origin.x) + (group.position.y - origin.y)) * .5f;
        float angle = std::round(std::atan2(dy, dx) / (kPi / 8)) * (kPi / 8);
        float approximateDistance = (std::min)(42.f, std::floor(distance / 6) * 6 + 3);
        state.scannerContacts.push_back({angle, approximateDistance, i});
    }
    tutorial::Game::Get().ShowNotice(
        "스캐너 / 실제 구조 요청의 대략적인 방향·거리만 표시합니다. 펄스가 지나간 뒤 사라집니다.");
}

bool MineRescue::TryInteract()
{
    for (auto &group : state.rescueGroups)
    {
        size_t index = size_t(&group - state.rescueGroups.data());
        bool nearMember =
            std::any_of(state.survivors.begin(), state.survivors.end(), [this, index](const RescueNpc &npc) {
                return npc.group == index && level.IsNear(npc.position);
            });
        if (!group.rescued && (level.IsNear(group.position) || nearMember))
        {
            group.rescued = true;
            for (auto &npc : state.survivors)
            {
                if (npc.group == index)
                {
                    npc.route = level.Map().FindRoute(npc.position, state.entry);
                    npc.routeIndex = 0;
                    npc.evacuating = true;
                    ++state.rescuedPeople;
                }
            }
            tutorial::Game::Get().ShowNotice(
                group.combatants ? "고립 전투원 / 실제 구조 요청을 찾아줘서 고맙습니다. 열린 갱도를 따라 "
                                   "헤일로로 철수하겠습니다."
                                 : "광부 / 우리 신호가 닿았군요! 알려준 귀환로로 마을까지 걸어가겠습니다.");
            return true;
        }
    }
    return false;
}

// ============================================================================
// MineLighting
// ============================================================================
MineLighting::MineLighting(MineLevel &owner) : level(owner), state(owner.GetState())
{
}
bool MineLighting::LightPath(tutorial::Vector2 from, tutorial::Vector2 to)
{
    float distance = tutorial::Game::Distance(from, to);
    int steps = (std::max)(1, int(std::ceil(distance / .22f)));
    // Allow the final rock face to receive light, but never illuminate through it.
    for (int i = 0; i < steps; ++i)
    {
        float t = float(i) / steps;
        if (!level.Map().IsWalkable(int(std::floor(from.x + (to.x - from.x) * t)),
                                    int(std::floor(from.y + (to.y - from.y) * t))))
        {
            return false;
        }
    }
    return true;
}

float MineLighting::RadialLight(tutorial::Vector2 from, tutorial::Vector2 to, float radius)
{
    float distance = tutorial::Game::Distance(from, to);
    if (distance >= radius || !LightPath(from, to))
    {
        return 0;
    }
    return (std::min)(1.f, (1 - distance / radius) * 1.8f);
}

float MineLighting::At(tutorial::Vector2 position)
{
    auto &world = tutorial::Game::Get().GetState();
    auto player = world.playerPosition;
    float light = RadialLight(player, position, 2.8f) * .8f;
    float distance = tutorial::Game::Distance(player, position);
    if (distance < 12)
    {
        float sx = (position.x - player.x) - (position.y - player.y);
        float sy = ((position.x - player.x) + (position.y - player.y)) * .5f;
        float length = std::sqrt(sx * sx + sy * sy);
        float angle = level.GetAimAngle();
        float alignment = length > .001f ? (sx * std::cos(angle) + sy * std::sin(angle)) / length : 1;
        float cone = tutorial::Game::Clamp((alignment - .86f) / .1f, 0, 1);
        if (cone > 0 && LightPath(player, position))
        {
            light = (std::max)(light, cone * (std::min)(1.f, (1 - distance / 12) * 1.65f));
        }
    }
    for (const auto &lamp : state.lights)
    {
        if (!lamp.needsPower || state.quest.IsPowerBypassed())
        {
            light = (std::max)(light, RadialLight(lamp.position, position, lamp.radius));
        }
    }
    for (const auto &tracer : state.tracers)
    {
        float dx = tracer.end.x - tracer.start.x, dy = tracer.end.y - tracer.start.y;
        float squaredLength = dx * dx + dy * dy;
        float t = squaredLength > .0001f ? tutorial::Game::Clamp(((position.x - tracer.start.x) * dx +
                                                                  (position.y - tracer.start.y) * dy) /
                                                                     squaredLength,
                                                                 0, 1)
                                         : 0;
        tutorial::Vector2 closest = {tracer.start.x + dx * t, tracer.start.y + dy * t};
        light = (std::max)(light, RadialLight(closest, position, 1.3f) * tracer.remaining / .12f);
    }
    if (state.muzzleFlash > 0)
    {
        light = (std::max)(light, RadialLight(player, position, 3.5f));
    }
    return tutorial::Game::Clamp(light, 0, 1);
}

// ============================================================================
// MineLevel
// ============================================================================
tutorial::Vector2 MineLevel::Project(tutorial::Vector2 p, float height)
{
    auto &world = tutorial::Game::Get().GetState();
    const auto camera = world.cameraPosition;
    return {tutorial::kCanvasWidth * .5f + (p.x - p.y - camera.x + camera.y) * 36,
            tutorial::kCanvasHeight * .57f + (p.x + p.y - camera.x - camera.y) * 18 - height};
}

void MineLevel::ClearInput()
{
    auto &world = tutorial::Game::Get().GetState();
    std::fill(world.pressedKeys, world.pressedKeys + 256, false);
    world.mouseFiring = false;
}

bool MineLevel::IsNear(tutorial::Vector2 position)
{
    auto &world = tutorial::Game::Get().GetState();
    return tutorial::Game::Distance(world.playerPosition, position) < 1.8f &&
           Map().HasLineOfSight(world.playerPosition, position);
}

bool MineLevel::IsActive() const
{
    return state.active;
}

int MineLevel::ExperienceForLevel(int level)
{
    return 30 * (level - 1) * level;
}

void MineLevel::AddExperience(int amount)
{
    auto &stats = state.player;
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
        tutorial::Game::Get().ShowNotice(
            "성장 / 최대 체력·총기 피해·방어력이 상승했습니다. 조사를 계속하거나 사냥할 수 있습니다.");
    }
}

void MineLevel::Enter(bool debugEntry)
{
    auto &world = tutorial::Game::Get().GetState();
    if (debugEntry && state.quest.GetStage() < MineQuestStage::TravelToMine)
    {
        state.quest.Advance(MineQuestStage::ShowRecord);
        state.quest.Advance(MineQuestStage::CheckCityRelay);
    }
    if (!state.quest.BeginJourney())
    {
        tutorial::Game::Get().ShowNotice(
            "리아 / 신호 기록을 보여 주고 도시 수신망에서 광산 중계 흔적부터 확인하자.");
        return;
    }
    state.quest.Advance(MineQuestStage::TravelToMine);
    if (!state.generated)
    {
        Map().Generate(std::random_device{}());
    }
    state.active = true;
    world.isInsideHome = false;
    world.isInObservatory = false;
    world.isJetTravel = false;
    world.playerPosition = state.entry;
    world.cameraPosition = state.entry;
    state.riaPosition = {state.entry.x + 1, state.entry.y + 1};
    state.riaRoute.clear();
    state.riaRouteIndex = 0;
    state.riaNavigationCooldown = 0;
    state.bullets.clear();
    state.tracers.clear();
    state.scannerSeconds = 0;
    ClearInput();
    state.invulnerability = 2;
    state.navigationCooldown = 0;
    tutorial::Game::Get().ShowNotice(
        "리아 / 수신망 주소가 이 광산이야. 전력부터 우회하자. 좌클릭 사격, R 재장전, F 구조 스캔.");
}

void MineLevel::Leave()
{
    auto &world = tutorial::Game::Get().GetState();
    state.quest.Advance(MineQuestStage::ReturnToHalo);
    state.active = false;
    state.bullets.clear();
    state.tracers.clear();
    state.scannerSeconds = 0;
    ClearInput();
    world.isInsideHome = false;
    world.isInObservatory = false;
    world.isJetTravel = false;
    world.playerPosition = tutorial::kJetBikePosition;
    world.cameraPosition = tutorial::kJetBikePosition;
    tutorial::Game::Get().ShowNotice(
        state.quest.GetStage() == MineQuestStage::Complete
            ? "낙하한 신호 완료 / 중계 좌표를 헤일로에 보고했습니다. N-0 외곽 설비가 다음 조사 대상입니다."
            : "헤일로 / 조사와 구조 기록은 유지됩니다. 준비되면 바이크로 광산에 다시 갈 수 있습니다.");
}

void MineLevel::Reset()
{
    auto &world = tutorial::Game::Get().GetState();
    state = MineState{};
    world.mouseFiring = false;
}

void MineLevel::Restart()
{
    Reset();
    Enter(true);
}

float MineLevel::GetAimAngle()
{
    auto &world = tutorial::Game::Get().GetState();
    auto origin = Project(world.playerPosition, 26);
    return std::atan2(world.cursorPosition.y - origin.y, world.cursorPosition.x - origin.x);
}

tutorial::Vector2 MineLevel::GetAimDirection(float angle)
{
    float sx = std::cos(angle), sy = std::sin(angle);
    tutorial::Vector2 direction = {sx / 72 + sy / 36, sy / 36 - sx / 72};
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    return {direction.x / length, direction.y / length};
}

void MineLevel::Interact()
{
    if (IsNear(kPowerBypass))
    {
        if (state.quest.Advance(MineQuestStage::BypassPower))
        {
            state.navigationCooldown = 0;
            tutorial::Game::Get().ShowNotice(
                "리아 / 끊긴 선로를 우회했어. 동쪽 격벽이 열렸고 유적 쪽 비상등이 들어왔어.");
        }
        else
        {
            tutorial::Game::Get().ShowNotice("배전반 / 비상 전력 우회 가동 중. 동쪽 격벽 너머를 조사하세요.");
        }
        return;
    }
    if (IsNear(kRuinSignal))
    {
        if (state.quest.Advance(MineQuestStage::InvestigateSignal))
        {
            tutorial::Game::Get().ShowNotice(
                "리아 / 오래전 실종 탐사선의 구조 신호야. 인간의 통신선은 벽 앞에서 "
                "끝나는데 신호는 안쪽에서 와.");
        }
        else
        {
            tutorial::Game::Get().ShowNotice(
                "구조 신호 / 실종 탐사선 식별자. 신호의 원인과 발신자의 정체는 아직 미확인.");
        }
        return;
    }
    if (IsNear(kObserverGlyph))
    {
        if (state.quest.Advance(MineQuestStage::InvestigateGlyph))
        {
            tutorial::Game::Get().ShowNotice(
                "리아 / 닮았다는 것하고, 널 가리킨다는 건 다른 얘기야. 아직은. 안쪽 단말에 "
                "중계 좌표가 있어.");
        }
        else
        {
            tutorial::Game::Get().ShowNotice(
                "관측자 문양 / 플레이어와 닮은 형상. 먼저 가까운 구조 신호의 기록을 조사하세요.");
        }
        return;
    }
    if (IsNear(kCoordinateTerminal))
    {
        if (state.quest.Advance(MineQuestStage::CollectCoordinates))
        {
            tutorial::Game::Get().ShowNotice(
                "중계 좌표 확보 / N-0 외곽 폐설비와 연결됩니다. 입구로 귀환해 헤일로에 "
                "보고하세요. 구조·사냥은 계속 가능합니다.");
        }
        else
        {
            tutorial::Game::Get().ShowNotice(state.quest.AreCoordinatesFound()
                                                 ? "단말 / 중계 좌표를 이미 확보했습니다."
                                                 : "리아 / 먼저 구조 신호와 관측자 문양을 조사하자.");
        }
        return;
    }
    if (Rescue().TryInteract())
    {
        return;
    }
    if (IsNear(state.entry))
    {
        Leave();
        return;
    }
    int collected = 0;
    auto &drops = state.drops;
    drops.erase(std::remove_if(drops.begin(), drops.end(),
                               [&](const Drop &drop) {
                                   if (!IsNear(drop.position))
                                   {
                                       return false;
                                   }
                                   if (drop.kind == ItemKind::RecoveryKit)
                                   {
                                       state.player.recoveryKits =
                                           (std::min)(999, state.player.recoveryKits + 1);
                                   }
                                   else
                                   {
                                       state.player.salvage = (std::min)(9999, state.player.salvage + 1);
                                   }
                                   ++collected;
                                   return true;
                               }),
                drops.end());
    if (collected > 0)
    {
        tutorial::Game::Get().ShowNotice("회수 / 부품과 회복 키트를 챙겼습니다.");
    }
    else if (IsNear(state.riaPosition))
    {
        tutorial::Game::Get().ShowNotice(std::string("리아 / ") + state.quest.GetObjective());
    }
    else
    {
        tutorial::Game::Get().ShowNotice(
            "E 조사·구조·회수 / F 구조 신호 스캔. 불빛과 광산의 주 갱도를 따라 이동하세요.");
    }
}

void MineLevel::UseRecoveryKit()
{
    auto &stats = state.player;
    if (stats.recoveryKits > 0 && stats.health < stats.maxHealth)
    {
        --stats.recoveryKits;
        stats.health = (std::min)(stats.maxHealth, stats.health + 45);
        tutorial::Game::Get().ShowNotice("회복 키트 / 체력이 45 회복되었습니다.");
    }
}

MineLevel::MineLevel() : map(*this), combat(*this), rescue(*this), lighting(*this)
{
}
MineState &MineLevel::GetState()
{
    return state;
}
MineMap &MineLevel::Map()
{
    return map;
}
MineCombat &MineLevel::Combat()
{
    return combat;
}
MineRescue &MineLevel::Rescue()
{
    return rescue;
}
MineLighting &MineLevel::Lighting()
{
    return lighting;
}
void MineLevel::Update(float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::Combat);
    combat.Update(deltaSeconds);
    rescue.Update(deltaSeconds);
    combat.UpdateEnemies(deltaSeconds);
}

// ============================================================================
// JetTravelLevel : Level
// ============================================================================
void JetTravelLevel::Reset()
{
    state = TravelState{};
    tutorial::Game::Get().GetState().isJetTravel = false;
}
void JetTravelLevel::Enter(bool)
{
    auto &world = tutorial::Game::Get().GetState();
    if (!GameLevels::Mine().GetState().quest.BeginJourney())
    {
        tutorial::Game::Get().ShowNotice("리아 / 도시 수신망에서 광산 중계 흔적부터 확인하자.");
        return;
    }
    Reset();
    state.active = true;
    constexpr float lanes[kObstacleCount] = {0, -.65f, .6f, -.35f, .75f, -.75f, .15f};
    for (int i = 0; i < kObstacleCount; ++i)
    {
        state.obstacles[i] = {240.f + i * 215.f, lanes[i], false};
    }
    world.isJetTravel = true;
    world.isInsideHome = false;
    world.isInObservatory = false;
    world.cameraPosition = {0, 0};
    world.movementAmount = 0;
    std::fill(world.pressedKeys, world.pressedKeys + 256, false);
    tutorial::Game::Get().ShowNotice(
        "제트 바이크 / W·S로 위아래 조종, D 가속, A 감속, Space 부스트. 붉은 장애물을 "
        "피하며 광산으로 이동하세요.");
}
bool JetTravelLevel::IsActive() const
{
    return state.active;
}
tutorial::Vector2 JetTravelLevel::GetBikeScreenPosition()
{
    return {kBikeScreenX, 485 + state.lateralOffset * 115};
}
float JetTravelLevel::GetObstacleScreenX(const Obstacle &obstacle)
{
    return kBikeScreenX + (obstacle.distance - state.distance) * kRoutePixelScale;
}
models::Palette JetTravelLevel::GetBikePalette()
{
    models::Palette palette;
    palette.body = {.62f, .69f, .74f, 1};
    palette.accent = {.9f, .5f, .22f, 1};
    palette.emission = {.3f, .9f, 1, 1};
    return palette;
}
void JetTravelLevel::Update(float deltaSeconds)
{
    auto &world = tutorial::Game::Get().GetState();
    if (!state.active)
    {
        return;
    }
    const auto &keys = world.pressedKeys;
    state.elapsedSeconds += deltaSeconds;
    float steering = (keys['s'] ? 1.f : 0) - (keys['w'] ? 1.f : 0);
    float smoothing = 1 - std::exp(-8 * deltaSeconds);
    state.lateralVelocity += (steering * kSteeringRate - state.lateralVelocity) * smoothing;
    state.lateralOffset =
        tutorial::Game::Clamp(state.lateralOffset + state.lateralVelocity * deltaSeconds, -1, 1);
    if ((state.lateralOffset >= 1 && state.lateralVelocity > 0) ||
        (state.lateralOffset <= -1 && state.lateralVelocity < 0))
    {
        state.lateralVelocity = 0;
    }
    float targetRoll =
        -tutorial::Game::Clamp(state.lateralVelocity / kSteeringRate, -1, 1) * tutorial::kPi / 6;
    state.rollRadians += (targetRoll - state.rollRadians) * smoothing;
    bool boosting = keys[' '] && state.boostCharge > .03f;
    state.boostCharge =
        tutorial::Game::Clamp(state.boostCharge + (boosting ? -.32f : .18f) * deltaSeconds, 0, 1);
    state.speedPenaltySeconds = (std::max)(0.f, state.speedPenaltySeconds - deltaSeconds);
    float targetSpeed = boosting ? kBoostSpeed : keys['d'] ? 82.f : keys['a'] ? kMinimumSpeed : kCruiseSpeed;
    if (state.speedPenaltySeconds > 0)
    {
        targetSpeed = kMinimumSpeed;
    }
    state.speed += (targetSpeed - state.speed) * (1 - std::exp(-2 * deltaSeconds));
    state.distance = (std::min)(kRouteLength, state.distance + state.speed * deltaSeconds);
    for (auto &obstacle : state.obstacles)
    {
        if (obstacle.passed || obstacle.distance > state.distance)
        {
            continue;
        }
        obstacle.passed = true;
        if (std::fabs(state.lateralOffset - obstacle.lane) < .3f)
        {
            ++state.collisionCount;
            state.speed *= .45f;
            state.speedPenaltySeconds = .8f;
            tutorial::Game::Get().ShowNotice(
                "차체 경고 / 부유 잔해에 접촉했습니다. 자세를 회복하고 주행을 계속하세요.");
        }
        else
        {
            ++state.avoidedObstacles;
        }
    }
    if (state.distance >= kRouteLength)
    {
        state.active = false;
        world.isJetTravel = false;
        world.isComplete = true;
        world.mainQuestStage = tutorial::MainQuestStage::Complete;
        GameLevels::Mine().Enter();
        tutorial::Game::Get().ShowNotice(
            "광산 / 제트 바이크를 입구에 주차했습니다. 구조 요청이 갱도 곳곳에서 들립니다. "
            "마우스 사격 / R 재장전 / F 구조 스캔 / E 조사·구조.");
    }
}

TravelState &JetTravelLevel::GetState()
{
    return state;
}

// ============================================================================
// GameLevels
// ============================================================================

GameLevels &GameLevels::Get()
{
    static GameLevels levels;
    return levels;
}
MineLevel &GameLevels::Mine()
{
    return Get().mine;
}
JetTravelLevel &GameLevels::Travel()
{
    return Get().travel;
}
Level *GameLevels::Active()
{
    if (Travel().IsActive())
    {
        return &Travel();
    }
    if (Mine().IsActive())
    {
        return &Mine();
    }
    return nullptr;
}
void GameLevels::Reset()
{
    for (Level *level : {static_cast<Level *>(&Mine()), static_cast<Level *>(&Travel())})
    {
        level->Reset();
    }
}
} // namespace game
