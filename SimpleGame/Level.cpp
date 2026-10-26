#include "stdafx.h"
#include "Level.h"
#include "Civic.h"
#include "Profiler.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <utility>

namespace game
{

namespace
{
template <class Walkable> bool BodyHitsTile(tutorial::Vector2 p, Walkable walkable)
{
    constexpr float radius = .16f;
    for (int y = int(std::floor(p.y - radius)); y <= int(std::floor(p.y + radius)); ++y)
    {
        for (int x = int(std::floor(p.x - radius)); x <= int(std::floor(p.x + radius)); ++x)
        {
            if (!walkable(x, y))
            {
                float dx = p.x - tutorial::Game::Clamp(p.x, float(x), float(x + 1));
                float dy = p.y - tutorial::Game::Clamp(p.y, float(y), float(y + 1));
                if (dx * dx + dy * dy < radius * radius)
                {
                    return true;
                }
            }
        }
    }
    return false;
}
} // namespace

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
        return "집 근처 리아에게 신호 기록 보여 주기 [E]";
    case MineQuestStage::CheckCityRelay:
        return "광장 수신망 단말에서 신호 경로 확인 [E]";
    case MineQuestStage::TravelToMine:
        return "바이크로 광산 이동 → 하차 후 입장 [E]";
    case MineQuestStage::BypassPower:
        return "주 갱도 끝의 비상 배전반에서 전력 우회 [E]";
    case MineQuestStage::InvestigateSignal:
        return "격벽 너머 지하 유적의 구조 신호 조사 [E]";
    case MineQuestStage::InvestigateGlyph:
        return "구조 신호 옆 벽면의 문양 조사 [E]";
    case MineQuestStage::CollectCoordinates:
        return "유적 안쪽 단말에서 중계 좌표 확보 [E]";
    case MineQuestStage::ReturnToHalo:
        return "광산을 나와 헤일로 수신망에 좌표 보고 [E]";
    case MineQuestStage::Complete:
        return "광산 조사 완료 / 헤일로 수신망에서 다음 조사 시작";
    }
    return "광산 조사";
}
const char *MineQuest::GetRiaDialogue() const
{
    switch (stage)
    {
    case MineQuestStage::ShowRecord:
        return "리아 / 전망대에서 받은 기록을 보여 줘. 수신 경로부터 살펴보자.";
    case MineQuestStage::CheckCityRelay:
        return "리아 / 목소리만으로는 누가 보냈는지 몰라. 광장 수신망에 기록을 대조해 보자.";
    case MineQuestStage::TravelToMine:
        return "리아 / 신호가 광산 통신선을 거쳤어. 바이크로 가자. 입구에서는 내려야 해.";
    case MineQuestStage::BypassPower:
        return "리아 / 격벽에 전력이 안 들어와. 주 갱도 끝 배전반에서 비상 전력을 연결하자.";
    case MineQuestStage::InvestigateSignal:
        return "리아 / 격벽이 열렸어. 불이 들어온 길을 따라 유적의 구조 신호를 확인하자.";
    case MineQuestStage::InvestigateGlyph:
        return "리아 / 신호가 나오는 벽에 사람 같은 문양이 있어. 가까이서 봐 줄래?";
    case MineQuestStage::CollectCoordinates:
        return "리아 / 문양은 기록해 뒀어. 안쪽 단말에 신호가 거쳐 간 좌표가 남아 있어.";
    case MineQuestStage::ReturnToHalo:
        return "리아 / 좌표는 챙겼어. 입구로 나가서 헤일로로 돌아가자. 수신망에 대조할 기록이 있을 거야.";
    case MineQuestStage::Complete:
        return "리아 / 광산 보고는 끝났어. 다음 현장은 헤일로 수신망에서 확인하자.";
    }
    return "리아 / 기록부터 다시 확인해 보자.";
}
namespace
{
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
    return x == kMineGateX && y >= kMineGateY - 3 && y <= kMineGateY + 3;
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
    return BodyHitsTile(position, [this](int x, int y) { return IsWalkable(x, y); });
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

bool MineMap::IsExplored(tutorial::Vector2 p) const
{
    int x = int(std::floor(p.x)), y = int(std::floor(p.y));
    return x >= 0 && y >= 0 && x < kMapSize && y < kMapSize && state.explored.size() == kMapSize * kMapSize &&
           state.explored[y * kMapSize + x] != 0;
}
void MineMap::Reveal(tutorial::Vector2 position)
{
    if (state.explored.size() != kMapSize * kMapSize)
    {
        state.explored.assign(kMapSize * kMapSize, 0);
    }
    constexpr float radius = 4;
    for (int y = (std::max)(0, int(position.y - radius));
         y <= (std::min)(kMapSize - 1, int(position.y + radius)); ++y)
    {
        for (int x = (std::max)(0, int(position.x - radius));
             x <= (std::min)(kMapSize - 1, int(position.x + radius)); ++x)
        {
            tutorial::Vector2 center = {x + .5f, y + .5f};
            if (tutorial::Game::Distance(position, center) > radius)
            {
                continue;
            }
            // The final wall cell may be remembered, but nothing behind that wall is disclosed.
            float distance = tutorial::Game::Distance(position, center);
            float fraction = distance > .7f ? (distance - .7f) / distance : 0;
            tutorial::Vector2 near = {position.x + (center.x - position.x) * fraction,
                                      position.y + (center.y - position.y) * fraction};
            if (HasLineOfSight(position, near))
            {
                state.explored[y * kMapSize + x] = 1;
            }
        }
    }
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
    std::mt19937 random(seed);
    auto integer = [&random](int low, int high) {
        return std::uniform_int_distribution<int>(low, high)(random);
    };
    auto chamber = [this](int x, int y, int rx, int ry) {
        for (int dy = -ry; dy <= ry; ++dy)
        {
            for (int dx = -rx; dx <= rx; ++dx)
            {
                if (float(dx * dx) / (rx * rx) + float(dy * dy) / (ry * ry) <= 1.15f)
                {
                    Carve(x + dx, y + dy);
                }
            }
        }
    };
    auto passage = [&chamber, &integer](tutorial::Vector2 from, tutorial::Vector2 to) {
        // A drifting midpoint breaks up straight grid corridors; circular brushes widen turns.
        tutorial::Vector2 middle = {(from.x + to.x) * .5f + integer(-5, 5),
                                    (from.y + to.y) * .5f + integer(-5, 5)};
        for (auto segment : {std::make_pair(from, middle), std::make_pair(middle, to)})
        {
            int steps =
                (std::max)(1, int(std::ceil(tutorial::Game::Distance(segment.first, segment.second) * 2)));
            int radius = integer(3, 4);
            for (int step = 0; step <= steps; ++step)
            {
                float t = float(step) / steps;
                chamber(int(segment.first.x + (segment.second.x - segment.first.x) * t),
                        int(segment.first.y + (segment.second.y - segment.first.y) * t), radius, radius);
            }
        }
    };
    // Each half has its own connected, loop-rich graph. The quest bulkhead is the only crossing.
    std::vector<tutorial::Vector2> nodes;
    for (int side = 0; side < 2; ++side)
    {
        for (int row = 0; row < 6; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                tutorial::Vector2 p = {float(side * 64 + 12 + column * 18 + integer(-3, 3)),
                                       float(12 + row * 20 + integer(-4, 4))};
                nodes.push_back(p);
                chamber(int(p.x), int(p.y), integer(5, 9), integer(5, 9));
                if (column > 0)
                {
                    passage(nodes.back(), nodes[nodes.size() - 2]);
                }
                if (row > 0)
                {
                    passage(nodes.back(), nodes[nodes.size() - 4]);
                }
            }
        }
    }
    auto attach = [&nodes, &chamber, &passage](tutorial::Vector2 p) {
        const tutorial::Vector2 *nearest = nullptr;
        float distance = 1.e9f;
        for (const auto &node : nodes)
        {
            if ((node.x < kMineGateX) != (p.x < kMineGateX))
            {
                continue;
            }
            float candidate = tutorial::Game::Distance(p, node);
            if (candidate < distance)
            {
                distance = candidate;
                nearest = &node;
            }
        }
        chamber(int(p.x), int(p.y), 5, 5);
        if (nearest)
        {
            passage(p, *nearest);
        }
    };
    for (auto p : {state.entry,
                   kPowerBypass,
                   kRuinSignal,
                   kObserverGlyph,
                   kCoordinateTerminal,
                   tutorial::Vector2{35.5f, 41.5f},
                   {23.5f, 87.5f},
                   {89.5f, 31.5f},
                   {107.5f, 73.5f},
                   {54.5f, kMineGateY + .5f},
                   {74.5f, kMineGateY + .5f}})
    {
        attach(p);
    }
    Room(54, kMineGateY - 3, 74, kMineGateY + 3);
    for (int y = 1; y < kMapSize - 1; ++y)
    {
        if (!IsGateCell(kMineGateX, y))
        {
            state.tiles[y * kMapSize + kMineGateX] = Tile::Wall;
        }
    }
    auto connected = Distances(state.entry, true);
    state.reachableCells.clear();
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
    state.lights = {{state.entry, 4.8f},
                    {{35.5f, 41.5f}, 3.2f},
                    {{23.5f, 87.5f}, 3.2f},
                    {kPowerBypass, 3.5f},
                    {{kMineGateX + .5f, kMineGateY + .5f}, 4.f, true},
                    {{89.5f, 31.5f}, 3.2f},
                    {{107.5f, 73.5f}, 3.2f},
                    {kRuinSignal, 3.8f},
                    {kCoordinateTerminal, 3.f}};
    state.rescueGroups = {
        {{35.5f, 41.5f}, false}, {{23.5f, 87.5f}, true}, {{89.5f, 31.5f}, false}, {{107.5f, 73.5f}, true}};
    state.survivors.clear();
    state.rescuedPeople = 0;
    state.returnedPeople = 0;
    state.scannerCooldown = 0;
    state.scannerSeconds = 0;
    state.scannerOrigin = state.entry;
    state.scannerRadius = 0;
    std::uint64_t id = 1;
    for (size_t i = 0; i < state.rescueGroups.size(); ++i)
    {
        auto center = state.rescueGroups[i].position;
        for (int member = 0; member < 3; ++member)
        {
            state.survivors.push_back({id++, i, {center.x + (member - 1) * .6f, center.y + .5f}});
            state.survivors.back().gender = (member + i) % 2 ? models::Gender::Female : models::Gender::Male;
            state.survivors.back().hair =
                models::HairStyle((member + i * 3) % size_t(models::HairStyle::Count));
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
    state.riaPosition = {state.entry.x + 1, state.entry.y + 1};
    state.riaFacing = {};
    state.riaCombat = {};
    state.riaRoute.clear();
    state.riaRouteIndex = 0;
    state.navigationDistances = Distances(state.entry);
    state.generated = true;
    state.explored.assign(kMapSize * kMapSize, 0);
    state.meleeSeconds = 0;
    Reveal(state.entry);
}

// ============================================================================
// MineCombat
// ============================================================================
MineCombat::MineCombat(MineLevel &owner) : level(owner), state(owner.GetState())
{
}
void MineCombat::MoveEnemy(Enemy &enemy, float deltaSeconds, tutorial::Vector2 target, bool playerTarget)
{
    int x = int(std::floor(enemy.position.x)), y = int(std::floor(enemy.position.y));
    if (!level.Map().IsWalkable(x, y) || state.navigationDistances.empty())
    {
        return;
    }
    int best = state.navigationDistances[y * kMapSize + x];
    if ((playerTarget && (best < 0 || best > 19)) ||
        tutorial::Game::Distance(target, state.entry) < kSafeAreaRadius)
    {
        return;
    }
    auto previous = enemy.position;
    if (playerTarget && !level.Map().HasLineOfSight(enemy.position, target))
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
    enemy.facing.Update(previous, enemy.position);
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
                DamageEnemy(j, bullet.damage, bullet.playerOwned);
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
        state.tracers.push_back({start, bullet.position, .12f, !bullet.playerOwned});
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
    if (tutorial::Dialogue::LocksWorld() || state.meleeSeconds > 0 || state.bullets.size() >= 128)
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
    float muzzleDistance =
        std::sqrt(muzzle.x * muzzle.x + muzzle.y * muzzle.y) * models::WorldScale::characterScale;
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

void MineCombat::Melee()
{
    auto &world = tutorial::Game::Get().GetState();
    if (!state.active || world.isPaused || world.mapVisible || state.player.health <= 0 ||
        tutorial::HospitalRecovery::Active() || tutorial::Dialogue::LocksWorld() || world.journalVisible ||
        world.dialogueHistoryVisible || state.meleeSeconds > 0)
    {
        return;
    }
    state.meleeSeconds = .65f;
    auto aim = level.GetAimDirection(level.GetAimAngle());
    size_t closest = state.enemies.size();
    float best = .85f;
    for (size_t i = 0; i < state.enemies.size(); ++i)
    {
        const auto &enemy = state.enemies[i];
        float dx = enemy.position.x - world.playerPosition.x, dy = enemy.position.y - world.playerPosition.y;
        float distance = std::hypot(dx, dy);
        if (enemy.health > 0 && distance < best &&
            (distance < .35f || dx * aim.x + dy * aim.y > distance * .5f) &&
            level.Map().HasLineOfSight(world.playerPosition, enemy.position))
        {
            best = distance;
            closest = i;
        }
    }
    if (closest < state.enemies.size())
    {
        DamageEnemy(closest, 30 + (state.player.level - 1) * 4);
    }
}

void MineCombat::Reload()
{
    if (state.pistol.Reload())
    {
        tutorial::Game::Get().ShowNotice("권총 / 재장전 중. 예비 탄창은 제한 없이 보급됩니다.");
    }
}

bool MineCombat::DamageEnemy(size_t index, int damage, bool playerCredit)
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
    enemy.respawnSeconds = 15;
    if (!playerCredit)
    {
        profiling::Count(profiling::Counter::NpcEnemyKills);
        return true; // NPC defence does not grant unattended player rewards.
    }
    ++state.kills;
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
    state.meleeSeconds = (std::max)(0.f, state.meleeSeconds - deltaSeconds);
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

void MineCombat::UpdateNpcs(float deltaSeconds)
{
    auto defend = [&](tutorial::Vector2 &position, tutorial::MovementFacing &facing, NpcCombatState &combat,
                      bool armed, bool evacuating, tutorial::Vector2 anchor) {
        combat.pistol.Update(deltaSeconds);
        combat.cooldown = (std::max)(0.f, combat.cooldown - deltaSeconds);
        combat.hitGrace = (std::max)(0.f, combat.hitGrace - deltaSeconds);
        combat.muzzleFlash = (std::max)(0.f, combat.muzzleFlash - deltaSeconds);
        combat.engaged = combat.evading = false;
        if (combat.health <= 0)
        {
            facing.walking = false;
            combat.downedSeconds -= deltaSeconds;
            if (combat.downedSeconds <= 0)
            {
                combat.health = 50;
                combat.hitGrace = 3;
            }
            return;
        }
        profiling::Count(profiling::Counter::NpcDecisions);
        size_t target = state.enemies.size();
        float nearest = armed ? 7.f : 3.f;
        for (size_t i = 0; i < state.enemies.size(); ++i)
        {
            const auto &enemy = state.enemies[i];
            float distance = tutorial::Game::Distance(position, enemy.position);
            if (enemy.health > 0 && distance < nearest &&
                level.Map().HasLineOfSight(position, enemy.position))
            {
                nearest = distance;
                target = i;
            }
        }
        if (target == state.enemies.size())
        {
            return;
        }
        auto threat = state.enemies[target].position;
        combat.aimYaw = std::atan2(threat.y - position.y, threat.x - position.x);
        combat.engaged = true;
        if (armed)
        {
            if (combat.pistol.GetRounds() == 0)
            {
                combat.pistol.Reload();
            }
            if (combat.cooldown > 0 || state.bullets.size() >= 128)
            {
                return;
            }
            // Weapon spread is measured in projected aim space, just like the player's shots.
            float aim = std::atan2((threat.x - position.x + threat.y - position.y) * .5f,
                                   threat.x - position.x - threat.y + position.y);
            combat::Shot shot;
            if (!combat.pistol.TryFire(aim, shot))
            {
                return;
            }
            Bullet bullet = {position, level.GetAimDirection(shot.angle), shot.speed, 7.5f, 10, false};
            auto muzzle = models::ModelRig::Muzzle(models::ModelRig::Pose(0, false, true, 1));
            AdvanceBullet(bullet, std::sqrt(muzzle.x * muzzle.x + muzzle.y * muzzle.y) *
                                      models::WorldScale::characterScale);
            profiling::Count(profiling::Counter::NpcShots);
            if (bullet.remaining > 0)
            {
                state.bullets.push_back(bullet);
            }
            combat.cooldown = .55f;
            combat.muzzleFlash = .055f;
        }
        else
        {
            if (nearest < 1.15f && combat.cooldown <= 0)
            {
                DamageEnemy(target, 8, false);
                combat.cooldown = 1.1f;
            }
            // Miners back away within their distress-signal area; evacuees keep following the exit route.
            if (!evacuating && nearest > .01f)
            {
                auto previous = position;
                float step = 1.8f * deltaSeconds;
                tutorial::Vector2 next = {position.x + (position.x - threat.x) / nearest * step,
                                          position.y + (position.y - threat.y) / nearest * step};
                if (!level.Map().IsBlocked(next) && tutorial::Game::Distance(next, anchor) < 3)
                {
                    position = next;
                }
                facing.Update(previous, position);
                combat.evading = facing.walking;
            }
        }
    };
    defend(state.riaPosition, state.riaFacing, state.riaCombat, true, false, state.riaPosition);
    for (auto &npc : state.survivors)
    {
        const auto &group = state.rescueGroups[npc.group];
        defend(npc.position, npc.facing, npc.combat, group.combatants, npc.evacuating, group.position);
    }
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
                occupied = occupied || tutorial::Game::Distance(position, state.riaPosition) < 4 ||
                           std::any_of(
                               state.survivors.begin(), state.survivors.end(),
                               [&](const RescueNpc &npc) {
                                   return tutorial::Game::Distance(position, npc.position) < 4;
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
        // Select the closest reachable combatant. NPC targets require a clear sight line;
        // the player can also be reached through the existing navigation field.
        tutorial::Vector2 target = world.playerPosition;
        NpcCombatState *victim = nullptr;
        int x = int(std::floor(enemy.position.x)), y = int(std::floor(enemy.position.y));
        int navigation =
            x >= 0 && y >= 0 && x < kMapSize && y < kMapSize && !state.navigationDistances.empty()
                ? state.navigationDistances[y * kMapSize + x]
                : -1;
        bool playerTarget = tutorial::Game::Distance(target, state.entry) >= kSafeAreaRadius &&
                            navigation >= 0 && navigation <= 19;
        float nearest = playerTarget ? tutorial::Game::Distance(enemy.position, target) : 1000.f;
        auto consider = [&](tutorial::Vector2 position, NpcCombatState &combat) {
            float distance = tutorial::Game::Distance(enemy.position, position);
            if (combat.health > 0 && distance < 10 && distance < nearest &&
                tutorial::Game::Distance(position, state.entry) >= kSafeAreaRadius &&
                level.Map().HasLineOfSight(enemy.position, position))
            {
                nearest = distance;
                target = position;
                victim = &combat;
                playerTarget = false;
            }
        };
        consider(state.riaPosition, state.riaCombat);
        for (auto &npc : state.survivors)
        {
            consider(npc.position, npc.combat);
        }
        enemy.facing.walking = false;
        if (!playerTarget && !victim)
        {
            continue;
        }
        enemy.target = target;
        MoveEnemy(enemy, deltaSeconds, target, playerTarget);
        if (enemy.attackCooldown > 0 || tutorial::Game::Distance(enemy.position, target) >= .8f ||
            !level.Map().HasLineOfSight(enemy.position, target))
        {
            continue;
        }
        enemy.facing.yaw = std::atan2(target.y - enemy.position.y, target.x - enemy.position.x);
        if (victim)
        {
            if (victim->hitGrace > 0)
            {
                continue;
            }
            victim->health = (std::max)(0, victim->health - 12);
            victim->hitGrace = .45f;
            if (!victim->health)
            {
                victim->downedSeconds = 12;
                victim->engaged = false;
            }
            enemy.attackCooldown = 1;
        }
        else if (state.invulnerability <= 0)
        {
            state.player.health -= (std::max)(1, 10 - state.player.defense);
            enemy.attackCooldown = 1;
            state.invulnerability = .45f;
            if (state.player.health <= 0)
            {
                tutorial::HospitalRecovery::Begin();
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
                            size_t &index, float amount, tutorial::MovementFacing &facing)
{
    float travelled = 0;
    facing.walking = false;
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
        facing.Update(position, next);
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
    UpdateScanner(deltaSeconds);
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
    if (state.riaCombat.health > 0)
    {
        state.riaMovement = WalkRoute(state.riaPosition, state.riaRoute, state.riaRouteIndex,
                                      4.8f * deltaSeconds, state.riaFacing);
    }
    else
    {
        state.riaFacing.walking = false;
    }
    for (auto &npc : state.survivors)
    {
        if (npc.combat.health <= 0)
        {
            npc.facing.walking = false;
            continue;
        }
        if (!npc.evacuating)
        {
            if (!npc.combat.evading)
            {
                npc.facing.walking = false;
            }
            continue;
        }
        npc.walkedDistance +=
            WalkRoute(npc.position, npc.route, npc.routeIndex, 2.1f * deltaSeconds, npc.facing);
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

void MineRescue::UpdateScanner(float deltaSeconds)
{
    for (auto &contact : state.scannerContacts)
    {
        contact.secondsRemaining -= deltaSeconds;
    }
    auto &contacts = state.scannerContacts;
    contacts.erase(std::remove_if(contacts.begin(), contacts.end(),
                                  [&](const ScannerContact &contact) {
                                      return contact.secondsRemaining <= 0 ||
                                             contact.group >= state.rescueGroups.size() ||
                                             state.rescueGroups[contact.group].rescued;
                                  }),
                   contacts.end());
    if (state.scannerSeconds <= 0)
    {
        return;
    }
    const float elapsed = kScannerDuration - state.scannerSeconds;
    const float frameEnd = elapsed + deltaSeconds;
    state.scannerSeconds = (std::max)(0.f, state.scannerSeconds - deltaSeconds);
    state.scannerRadius = (std::min)(kScannerRange, frameEnd * kScannerSpeed);
    for (size_t groupIndex = 0; groupIndex < state.rescueGroups.size(); ++groupIndex)
    {
        const auto &group = state.rescueGroups[groupIndex];
        const float distance = tutorial::Game::Distance(state.scannerOrigin, group.position);
        if (group.rescued || distance > state.scannerRadius || distance > kScannerRange ||
            std::any_of(contacts.begin(), contacts.end(),
                        [=](const ScannerContact &contact) { return contact.group == groupIndex; }))
        {
            continue;
        }
        // Discovery and contact lifetime use the wave's actual arrival time, including long frames.
        const float lifetime = kScannerContactSeconds - (frameEnd - distance / kScannerSpeed);
        if (lifetime <= 0)
        {
            continue;
        }
        float dx = (group.position.x - state.scannerOrigin.x) - (group.position.y - state.scannerOrigin.y);
        float dy =
            ((group.position.x - state.scannerOrigin.x) + (group.position.y - state.scannerOrigin.y)) * .5f;
        float angle = std::round(std::atan2(dy, dx) / (kPi / 8)) * (kPi / 8);
        float approximateDistance = (std::min)(kScannerRange, std::floor(distance / 6) * 6 + 3);
        tutorial::Vector2 bearing = {std::cos(angle) + 2 * std::sin(angle),
                                     2 * std::sin(angle) - std::cos(angle)};
        float magnitude = std::hypot(bearing.x, bearing.y);
        float estimate = distance < .1f ? 0 : approximateDistance / magnitude;
        tutorial::Vector2 marker = {state.scannerOrigin.x + bearing.x * estimate,
                                    state.scannerOrigin.y + bearing.y * estimate};
        contacts.push_back({angle, approximateDistance, groupIndex, marker, lifetime});
    }
}

void MineRescue::Scan()
{
    if (state.scannerCooldown > 0)
    {
        return;
    }
    state.scannerOrigin = tutorial::Game::Get().GetState().playerPosition;
    state.scannerSeconds = kScannerDuration;
    state.scannerRadius = 0;
    state.scannerCooldown = 6;
    state.scannerContacts.clear();
    UpdateScanner(0); // A transmitter at the scan origin is reached immediately.
    tutorial::Game::Get().ShowNotice(
        "스캐너 / 펄스가 구조 신호에 도달하면 방향을 표시합니다. 표시된 신호는 잠시 후 사라집니다.");
}

bool MineRescue::TryInteract()
{
    auto aid = [&](tutorial::Vector2 position, NpcCombatState &combat) {
        if (combat.health > 0 || !level.IsNear(position))
        {
            return false;
        }
        combat.health = 50;
        combat.downedSeconds = 0;
        combat.hitGrace = 3;
        tutorial::Game::Get().ShowNotice("응급 처치 / 다시 움직일 수 있습니다.");
        return true;
    };
    if (aid(state.riaPosition, state.riaCombat))
    {
        return true;
    }
    for (auto &npc : state.survivors)
    {
        if (npc.evacuating && aid(npc.position, npc.combat))
        {
            return true;
        }
    }
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
                    if (npc.combat.health <= 0)
                    {
                        npc.combat.health = 50;
                        npc.combat.downedSeconds = 0;
                        npc.combat.hitGrace = 3;
                    }
                    npc.route = level.Map().FindRoute(npc.position, state.entry);
                    npc.routeIndex = 0;
                    npc.evacuating = true;
                    ++state.rescuedPeople;
                }
            }
            tutorial::Game::Get().ShowNotice(group.combatants
                                                 ? "고립 전투원 / 신호를 보셨군요. 지원에 감사드립니다. "
                                                   "확보된 갱도를 따라 입구로 철수하겠습니다."
                                                 : "광부 / 신호가 닿았군요! 아무도 못 듣는 줄 알았어요. 알려 "
                                                   "주신 길로 입구까지 나갈게요.");
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
    if (state.scannerSeconds > 0 && !level.Map().IsBlocked(position))
    {
        const float band =
            std::fabs(tutorial::Game::Distance(state.scannerOrigin, position) - state.scannerRadius) /
            (std::max)(.05f, state.scannerRadius * .024f);
        const float opacity = (std::min)(1.f, state.scannerSeconds / .3f);
        light = (std::max)(light, (std::max)(0.f, 1 - band) * opacity);
    }
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
    if (state.riaCombat.muzzleFlash > 0)
    {
        light = (std::max)(light, RadialLight(state.riaPosition, position, 3));
    }
    for (const auto &npc : state.survivors)
    {
        if (npc.combat.muzzleFlash > 0)
        {
            light = (std::max)(light, RadialLight(npc.position, position, 3));
        }
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
    return tutorial::WorldCamera::ToViewport(
        {tutorial::kCanvasWidth * .5f + (p.x - p.y - camera.x + camera.y) * 36,
         tutorial::kCanvasHeight * .57f + (p.x + p.y - camera.x - camera.y) * 18 - height});
}

void MineLevel::ClearInput()
{
    auto &world = tutorial::Game::Get().GetState();
    world.mapVisible = false;
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
    // Every entry is a fresh instance; story flags, inventory and RPG stats survive regeneration.
    Map().Generate(std::random_device{}());
    state.active = true;
    world.isInsideHome = false;
    world.isInObservatory = false;
    world.isBikeMounted = false;
    world.bikeSpeed = 0;
    world.playerPosition = state.entry;
    world.cameraPosition = state.entry;
    state.riaPosition = {state.entry.x + 1, state.entry.y + 1};
    state.riaRoute.clear();
    state.riaRouteIndex = 0;
    state.riaNavigationCooldown = 0;
    state.bullets.clear();
    state.tracers.clear();
    state.scannerSeconds = 0;
    state.scannerRadius = 0;
    state.scannerContacts.clear();
    ClearInput();
    state.invulnerability = 2;
    state.navigationCooldown = 0;
    tutorial::Game::Get().ShowNotice(state.quest.GetRiaDialogue());
}

void MineLevel::Leave()
{
    auto &world = tutorial::Game::Get().GetState();
    state.active = false;
    state.bullets.clear();
    state.tracers.clear();
    state.scannerSeconds = 0;
    state.scannerRadius = 0;
    state.scannerContacts.clear();
    ClearInput();
    world.isInsideHome = false;
    world.isInObservatory = false;
    world.playerPosition = world.mineReturnPosition;
    world.cameraPosition = world.playerPosition;
    if (state.quest.GetStage() == MineQuestStage::Complete)
    {
        GameLevels::Chapter().Unlock();
    }
    tutorial::Game::Get().ShowNotice(
        state.quest.GetStage() == MineQuestStage::ReturnToHalo
            ? "광산 입구 / 바이크로 헤일로에 돌아가 도시 수신망에서 중계 좌표를 보고하세요."
            : "광산 입구 / 조사 기록과 보유품은 유지됩니다. 다시 입장하면 갱도와 현장 인원이 새로 "
              "배치됩니다. 주차한 바이크로 이동하거나 입구에서 E로 재입장하세요.");
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
    auto origin = Project(world.playerPosition, models::WorldScale::aimHeightPixels);
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
    const auto &world = tutorial::Game::Get().GetState();
    if (tutorial::HospitalRecovery::Active() || tutorial::Dialogue::Active() || world.mapVisible ||
        world.isPaused || world.journalVisible || world.dialogueHistoryVisible)
    {
        return;
    }
    auto tell = [&](const std::string &message) {
        const char *action = IsNear(kPowerBypass)          ? "power"
                             : IsNear(kRuinSignal)         ? "signal"
                             : IsNear(kObserverGlyph)      ? "glyph"
                             : IsNear(kCoordinateTerminal) ? "coordinates"
                                                           : "ria";
        tutorial::Dialogue::Say(
            std::string("MQ00/mine/") + action + "/" + std::to_string(int(state.quest.GetStage())), message);
    };
    if (IsNear(kPowerBypass))
    {
        if (state.quest.Advance(MineQuestStage::BypassPower))
        {
            state.navigationCooldown = 0;
            tell("리아 / 전력이 들어왔어. 동쪽 격벽도 열렸고. 비상등이 켜진 길을 따라가 보자.");
        }
        else
        {
            tell("배전반 / 비상 전력이 공급되고 있습니다. 동쪽 격벽은 열린 상태입니다.");
        }
        return;
    }
    if (IsNear(kRuinSignal))
    {
        if (state.quest.Advance(MineQuestStage::InvestigateSignal))
        {
            tell("리아 / 식별자는 오래전에 실종된 탐사선의 것이야. 그런데 통신선은 이 벽 앞에서 끊겨 있어. "
                 "신호는 벽 안쪽에서 오고 있고.");
        }
        else
        {
            tell("구조 신호 / 실종 탐사선 식별자. 신호의 원인과 발신자의 정체는 아직 미확인.");
        }
        return;
    }
    if (IsNear(kObserverGlyph))
    {
        if (state.quest.Advance(MineQuestStage::InvestigateGlyph))
        {
            tell("리아 / 이 문양, 너랑 닮았네. 그래도 널 새긴 거라고 단정하진 "
                 "말자. 기록해 둘게. 안쪽 단말도 확인해 보자.");
        }
        else
        {
            tell(state.quest.GetStage() < MineQuestStage::InvestigateGlyph
                     ? "벽면 문양 / 사람을 닮은 형상입니다. 먼저 옆에서 들리는 구조 신호를 조사하세요."
                     : "문양 조사 기록 / 당신과 닮은 형상입니다. 제작 시기와 의미는 알 수 없습니다. 조사 "
                       "기록에 보관했습니다.");
        }
        return;
    }
    if (IsNear(kCoordinateTerminal))
    {
        if (state.quest.Advance(MineQuestStage::CollectCoordinates))
        {
            tell("중계 좌표 저장 / 신호가 거쳐 간 좌표를 확보했습니다. 광산 입구로 나가 바이크로 헤일로에 "
                 "돌아가세요. 광장 수신망에서 좌표를 대조할 수 있습니다.");
        }
        else
        {
            tell(state.quest.AreCoordinatesFound() ? "단말 / 중계 좌표를 이미 확보했습니다."
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
                                       state.player.credits = (std::min)(9999999, state.player.credits + 5);
                                   }
                                   ++collected;
                                   return true;
                               }),
                drops.end());
    if (collected > 0)
    {
        tutorial::Game::Get().ShowNotice("물자 회수 / 주변 물자를 챙겼습니다. 보유품에 반영했습니다.");
    }
    else if (IsNear(state.riaPosition))
    {
        tell(state.quest.GetRiaDialogue());
    }
    else
    {
        tutorial::Game::Get().ShowNotice(
            "E 조사·구조·회수 / F 구조 신호 스캔. 불빛과 광산의 주 갱도를 따라 이동하세요.");
    }
}

void MineLevel::UseRecoveryKit()
{
    if (tutorial::Dialogue::LocksWorld())
    {
        return;
    }
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
    map.Reveal(tutorial::Game::Get().GetState().playerPosition);
    combat.Update(deltaSeconds);
    combat.UpdateNpcs(deltaSeconds);
    rescue.Update(deltaSeconds);
    combat.UpdateEnemies(deltaSeconds);
}

// ============================================================================
// ChapterOneLevel : Level — Light of the Frontier
// ============================================================================
ChapterOneLevel::ChapterOneLevel() : map(*this), combat(*this), party(*this)
{
}
FrontierMap::FrontierMap(ChapterOneLevel &owner) : state(owner.GetState())
{
}
FrontierCombat::FrontierCombat(ChapterOneLevel &owner) : level(owner), state(owner.GetState())
{
}
FrontierParty::FrontierParty(ChapterOneLevel &owner) : level(owner), state(owner.GetState())
{
}
bool ChapterOneLevel::IsBlocked(tutorial::Vector2 p) const
{
    return map.IsBlocked(p);
}
bool ChapterOneLevel::HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to) const
{
    return map.HasLineOfSight(from, to);
}
std::vector<tutorial::Vector2> ChapterOneLevel::FindRoute(tutorial::Vector2 from, tutorial::Vector2 to) const
{
    return map.FindRoute(from, to);
}
void ChapterOneLevel::Attack()
{
    combat.Attack();
}
ChapterOneState &ChapterOneLevel::GetState()
{
    return state;
}
bool ChapterOneLevel::IsAvailable() const
{
    return state.unlocked || GameLevels::Mine().GetState().quest.GetStage() == MineQuestStage::Complete;
}
bool ChapterOneLevel::IsActive() const
{
    return state.active;
}
bool ChapterOneLevel::Unlock()
{
    if (!IsAvailable())
    {
        return false;
    }
    state.unlocked = true;
    return true;
}
void ChapterOneLevel::Reset()
{
    state = ChapterOneState{};
}
void FrontierMap::Generate()
{
    state.tiles.resize(kFrontierWidth * kFrontierHeight);
    for (int y = 0; y < kFrontierHeight; ++y)
    {
        for (int x = 0; x < kFrontierWidth; ++x)
        {
            auto tile = x < 24 ? FrontierTile::Grass : FrontierTile::Sand;
            if ((x >= 4 && x <= 6) || (y >= 19 && y <= 21) || (x >= 23 && x <= 25) ||
                (y >= 29 && y <= 31 && x >= 23))
            {
                tile = FrontierTile::Road;
            }
            // A dry, collapsed channel. Surveyed crossings at both ends remain usable.
            if (y >= 15 && y <= 16 && x >= 27 && x <= 35)
            {
                tile = FrontierTile::Canal;
            }
            state.tiles[y * kFrontierWidth + x] = tile;
        }
    }
    state.structures = {
        {8, 7, 5, 2, 100, models::BuildingType::Workshop, {{.30f, .46f, .5f, 1}, {.7f, .74f, .5f, 1}}},
        {14, 7, 3, 4, 125, models::BuildingType::Habitat, {{.3f, .43f, .5f, 1}, {.65f, .5f, .32f, 1}}},
        {31, 28, 3, 2, 85, models::BuildingType::Workshop, {{.33f, .36f, .4f, 1}, {.68f, .39f, .2f, 1}}},
        {36, 30, 3, 5, 120, models::BuildingType::Terraced, {{.35f, .32f, .34f, 1}, {.5f, .42f, .29f, 1}}}};
    for (auto &b : state.structures)
    {
        // Fit imported proportions inside the authored foundations without blocking quest approaches.
        const auto &model = models::ModelLibrary::Get().buildings[size_t(b.type)];
        const auto &resource = models::ResourceAssets::Get(model.resourceId);
        const float scale = (std::min)(b.width / resource.physicalSize.x, b.depth / resource.physicalSize.y);
        b.width = resource.physicalSize.x * scale;
        b.depth = resource.physicalSize.y * scale;
        b.height = resource.physicalSize.z / resource.maximum.z * scale * 44.f;
        for (int y = int(b.y); y < int(b.y + b.depth); ++y)
        {
            for (int x = int(b.x); x < int(b.x + b.width); ++x)
            {
                state.tiles[y * kFrontierWidth + x] = FrontierTile::Foundation;
            }
        }
    }
    state.ria.position = {6.5f, 6.5f};
    state.kain.position = {30.5f, 10.5f};
    for (int i = 0; i < 5; ++i)
    {
        FrontierPerson survivor;
        survivor.position = {31.f + (i % 3) * .7f, 12.f + (i / 3) * .8f};
        state.survivors.push_back(survivor);
    }
    for (tutorial::Vector2 p : {tutorial::Vector2{32.5f, 31.5f}, {34.5f, 29.5f}, {31.5f, 34.5f}})
    {
        FrontierPerson raider;
        raider.position = p;
        raider.combat.health = 60;
        state.scrappers.push_back(raider);
    }
    state.generated = true;
}
bool FrontierMap::IsWalkable(int x, int y) const
{
    if (x < 1 || y < 1 || x >= kFrontierWidth - 1 || y >= kFrontierHeight - 1 || state.tiles.empty())
    {
        return false;
    }
    auto tile = state.tiles[y * kFrontierWidth + x];
    return tile != FrontierTile::Canal && tile != FrontierTile::Foundation;
}
bool FrontierMap::IsBlocked(tutorial::Vector2 position) const
{
    return BodyHitsTile(position, [this](int x, int y) { return IsWalkable(x, y); });
}
bool FrontierMap::HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to) const
{
    float distance = tutorial::Game::Distance(from, to);
    int steps = (std::max)(1, int(std::ceil(distance / .12f)));
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
std::vector<tutorial::Vector2> FrontierMap::FindRoute(tutorial::Vector2 from, tutorial::Vector2 to) const
{
    int sx = int(std::floor(from.x)), sy = int(std::floor(from.y)), tx = int(std::floor(to.x)),
        ty = int(std::floor(to.y));
    if (!IsWalkable(sx, sy) || !IsWalkable(tx, ty))
    {
        return {};
    }
    std::vector<int> previous(kFrontierWidth * kFrontierHeight, -1), queue;
    int start = sy * kFrontierWidth + sx, end = ty * kFrontierWidth + tx;
    previous[start] = start;
    queue.push_back(start);
    for (size_t i = 0; i < queue.size() && previous[end] < 0; ++i)
    {
        int cell = queue[i];
        for (auto direction : {tutorial::Vector2{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
        {
            int x = cell % kFrontierWidth + int(direction.x), y = cell / kFrontierWidth + int(direction.y);
            if (!IsWalkable(x, y))
            {
                continue;
            }
            int next = y * kFrontierWidth + x;
            if (previous[next] >= 0)
            {
                continue;
            }
            previous[next] = cell;
            queue.push_back(next);
        }
    }
    if (previous[end] < 0)
    {
        return {};
    }
    std::vector<tutorial::Vector2> result;
    for (int cell = end; cell != start; cell = previous[cell])
    {
        result.push_back({cell % kFrontierWidth + .5f, cell / kFrontierWidth + .5f});
    }
    std::reverse(result.begin(), result.end());
    result.push_back(to);
    return result;
}
bool FrontierParty::Move(FrontierPerson &person, float amount)
{
    person.facing.walking = false;
    if (person.combat.health <= 0)
    {
        return false;
    }
    while (amount > 0 && person.waypoint < person.route.size())
    {
        auto to = person.route[person.waypoint];
        float distance = tutorial::Game::Distance(person.position, to);
        if (distance < .02f)
        {
            ++person.waypoint;
            continue;
        }
        float step = (std::min)(distance, amount);
        tutorial::Vector2 next = {person.position.x + (to.x - person.position.x) * step / distance,
                                  person.position.y + (to.y - person.position.y) * step / distance};
        if (level.IsBlocked(next))
        {
            break;
        }
        person.facing.Update(person.position, next);
        person.position = next;
        amount -= step;
        if (step >= distance)
        {
            ++person.waypoint;
        }
    }
    return person.waypoint >= person.route.size();
}
void ChapterOneLevel::Enter(bool debugEntry)
{
    tutorial::Game::Get().GetState().mapVisible = false;
    if (debugEntry)
    {
        state.unlocked = true;
    }
    if (!Unlock())
    {
        tutorial::Game::Get().ShowNotice("리아 / 광산 중계 좌표를 확보하고 헤일로에 보고한 뒤 출발하자.");
        return;
    }
    if (!state.generated)
    {
        map.Generate();
    }
    GameLevels::Mine().GetState().active = false;
    state.active = true;
    state.invulnerability = 3;
    state.bullets.clear();
    state.tracers.clear();
    auto &world = tutorial::Game::Get().GetState();
    world.isBikeMounted = false;
    world.bikeSpeed = 0;
    world.isInsideHome = world.isInObservatory = world.mouseFiring = false;
    std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
    world.playerPosition = world.cameraPosition = kFrontierEntry;
    state.ria.position = {6.5f, 6.5f};
    state.ria.route.clear();
    state.ria.waypoint = 0;
    if (state.kainJoined)
    {
        state.kain.position = {6.5f, 7.5f};
        state.kain.route.clear();
        state.kain.waypoint = 0;
    }
    tutorial::Game::Get().ShowNotice(
        "1장 / 빛나는 개척지. 발전기·실종 운송대·폐설비를 원하는 순서로 조사하자. M 지도, J 기록. 북서쪽 "
        "통행 게이트에서 헤일로로 돌아갈 수 있다.");
}
void ChapterOneLevel::Leave()
{
    tutorial::Game::Get().GetState().mapVisible = false;
    state.active = false;
    state.bullets.clear();
    state.tracers.clear();
    auto &world = tutorial::Game::Get().GetState();
    world.mouseFiring = false;
    std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
    world.playerPosition = world.cameraPosition = tutorial::kCityReceiverPosition;
    tutorial::Game::Get().ShowNotice(
        CompletedSites() == 3 && !state.recordsCompared
            ? "헤일로 / 세 현장의 기록이 모였습니다. 도시 수신망 단말에서 대조하세요. [E]"
            : "헤일로 / 조사 기록과 현장 상태는 유지됩니다. 수신망 단말에서 다시 출발할 수 있습니다.");
}
int ChapterOneLevel::CompletedSites() const
{
    return int(state.generatorRestored) + int(state.convoyRescued) + int(state.ledgerFound);
}
const char *ChapterOneLevel::Objective() const
{
    if (state.recordsCompared)
    {
        return "1장 완료 / 수로 진입로 기록 확보 · 후속 지역 준비 중";
    }
    if (state.comparisonStarted)
    {
        return "기록 대조 / N-0 외곽의 순차 점등을 관측 중";
    }
    if (CompletedSites() == 3)
    {
        return "헤일로 수신망에서 세 현장 기록 대조 [E]";
    }
    if (state.escortStarted && !state.convoyRescued)
    {
        return "운송대 5명이 측량 우회로를 따라 안전 지점으로 이동 중";
    }
    return "발전기 · 운송대 · 폐설비 / 원하는 순서로 조사";
}
bool ChapterOneLevel::RelayClear() const
{
    return state.negotiated || std::all_of(state.scrappers.begin(), state.scrappers.end(),
                                           [](const FrontierPerson &p) { return p.combat.health <= 0; });
}
std::vector<ChapterInteraction> ChapterOneLevel::Interactions() const
{
    std::vector<ChapterInteraction> result = {{ChapterAction::Exit, kFrontierEntry, "헤일로 귀환 [E]"}};
    if (!state.generatorInspected)
    {
        result.push_back({ChapterAction::Generator, {10.5f, 10.5f}, "발전기 / 전력 유출 조사 [E]"});
    }
    else if (!state.generatorRestored)
    {
        result.push_back({ChapterAction::IsolateLine, {12.5f, 10.5f}, "폐쇄 선로 분리 [E]"});
    }
    if (state.generatorRestored && !state.portableLightRepaired)
    {
        result.push_back({ChapterAction::PortableLight, {9.5f, 13.5f}, "리아 / 휴대 조명 수리 [E]"});
    }
    if (!state.convoyBriefed)
    {
        result.push_back({ChapterAction::Kain, {30.5f, 10.5f}, "카인 / 현장 인원 확인 [E]"});
    }
    if (!state.receiverInspected)
    {
        result.push_back({ChapterAction::Receiver, {33.5f, 9.5f}, "끊어진 수신기 / 교신 확인 [E]"});
    }
    if (state.convoyBriefed && state.surveyMarkers < 3)
    {
        constexpr tutorial::Vector2 markers[] = {{36.5f, 13.5f}, {36.5f, 18.5f}, {29.5f, 18.5f}};
        result.push_back({ChapterAction(int(ChapterAction::SurveyOne) + state.surveyMarkers),
                          markers[state.surveyMarkers], "측량 표식 / 안전 우회로 확인 [E]"});
    }
    if (state.surveyMarkers == 3 && !state.escortStarted)
    {
        result.push_back({ChapterAction::Escort, {32.5f, 12.5f}, "운송대 / 5명 대피 시작 [E]"});
    }
    if (state.convoyRescued && !state.vehicleRepaired)
    {
        result.push_back({ChapterAction::Vehicle, {34.5f, 10.5f}, "선택 / 운송 차량 수리 [E]"});
    }
    if (!state.tradeProofFound && !state.ledgerFound)
    {
        result.push_back({ChapterAction::TradeProof, {24.5f, 30.5f}, "적재함 / 거래 기록 확인 [E]"});
    }
    if (!RelayClear() && !state.ledgerFound)
    {
        result.push_back({ChapterAction::Scrappers,
                          {28.5f, 27.5f},
                          state.tradeProofFound ? "약탈자 / 거래 기록 제시 [E]" : "약탈자 / 대화 [E]"});
    }
    if (RelayClear() && !state.ledgerFound)
    {
        result.push_back({ChapterAction::Ledger, {33.5f, 32.5f}, "중계 단말 / 기본 장부 확보 [E]"});
    }
    if (RelayClear() && !state.detailLedgerFound)
    {
        result.push_back({ChapterAction::DetailLedger, {35.5f, 33.5f}, "선택 / 상세 구매 장부 [E]"});
    }
    if (state.convoyRescued)
    {
        result.push_back({ChapterAction::ConvoySupply, {24.5f, 20.5f}, "운송대 보급 / 회복 키트 교환 [E]"});
    }
    return result;
}
bool ChapterOneLevel::NearestInteraction(ChapterInteraction &result) const
{
    float best = 1.5f;
    bool found = false;
    for (const auto &point : Interactions())
    {
        float distance =
            tutorial::Game::Distance(tutorial::Game::Get().GetState().playerPosition, point.position);
        if (distance < best)
        {
            best = distance;
            result = point;
            found = true;
        }
    }
    return found;
}
void ChapterOneLevel::Interact()
{
    const auto &world = tutorial::Game::Get().GetState();
    if (tutorial::HospitalRecovery::Active() || tutorial::Dialogue::Active() || world.mapVisible ||
        world.isPaused || world.journalVisible || world.dialogueHistoryVisible)
    {
        return;
    }
    ChapterInteraction point{};
    if (!NearestInteraction(point))
    {
        return;
    }
    auto &game = tutorial::Game::Get();
    auto tell = [&](const std::string &message) {
        tutorial::Dialogue::Say("chapter1/action/" + std::to_string(int(point.action)) + "/" +
                                    std::to_string(state.generatorRestored) + "/" +
                                    std::to_string(state.convoyRescued) + "/" +
                                    std::to_string(state.negotiated),
                                message);
    };
    switch (point.action)
    {
    case ChapterAction::Exit:
        Leave();
        return;
    case ChapterAction::Generator:
        state.generatorInspected = true;
        tell("리아 / 고장이 아니야. 폐쇄된 N-0 외곽 선로가 일정한 주기로 전력을 가져가고 있어. "
             "오른쪽 차단기를 분리하자.");
        break;
    case ChapterAction::IsolateLine:
        state.generatorRestored = true;
        tell("리아 / 도시 쪽 전력은 안정됐어. 이상하네… 끊어 놓은 선로의 계기판이 아직 움직여. "
             "같은 간격이야. 이것도 기록해 두자.");
        break;
    case ChapterAction::PortableLight:
        state.portableLightRepaired = true;
        tell("리아 / 됐다, 켜지네. 접점만 손보면 되는 걸 버릴 뻔했어. 다음에 야영할 때 가져가자. "
             "네가 길 좀 밝혀 줘. 잠깐 이 불빛 아래서 쉬었다 가자.");
        break;
    case ChapterAction::Kain:
        state.convoyBriefed = true;
        tell("카인 / 눈앞에는 다섯 명인데 무전에는 여섯 명이 답합니다. 저 소리를 따라가진 맙시다. "
             "여기 있는 사람부터 옮기죠. 남쪽 측량 표식 세 곳을 확인해 주십시오.");
        break;
    case ChapterAction::Receiver:
        state.receiverInspected = true;
        tell("수신기 조사 / 전선이 끊겼는데도 같은 응답이 반복됩니다. 앞서 녹음된 교신과 "
             "일치합니다. 이상 발생 시각과 설비 번호 N-0/17을 저장했습니다.");
        break;
    case ChapterAction::SurveyOne:
    case ChapterAction::SurveyTwo:
    case ChapterAction::SurveyThree:
        ++state.surveyMarkers;
        tell(state.surveyMarkers == 3
                 ? "카인 / 세 표식이 이어지는 길은 안전합니다. 운송대로 돌아와 주십시오. 다섯 "
                   "명을 그 길로 옮기겠습니다."
                 : "측량 표식 / 우회로가 끊긴 수로를 돌아 이어집니다. 다음 표식을 확인하세요.");
        break;
    case ChapterAction::Escort:
        for (auto &person : state.survivors)
        {
            person.route.clear();
            auto from = person.position;
            for (auto to : {tutorial::Vector2{36.5f, 13.5f}, {36.5f, 18.5f}, {29.5f, 18.5f}, {24.5f, 20.5f}})
            {
                auto leg = FindRoute(from, to);
                person.route.insert(person.route.end(), leg.begin(), leg.end());
                from = to;
            }
            auto destination =
                tutorial::Vector2{23.5f + float(&person - state.survivors.data()) * .55f,
                                  20.5f + float((&person - state.survivors.data()) % 2) * .55f};
            auto lastLeg = FindRoute(from, destination);
            person.route.insert(person.route.end(), lastLeg.begin(), lastLeg.end());
            person.waypoint = 0;
            person.departureDelay = float(&person - state.survivors.data()) * .8f;
        }
        state.escortStarted = true;
        tell("카인 / 다섯 분 모두 제 뒤로 오십시오. 안전 지점에서 다시 인원을 확인하겠습니다. "
             "[진행 안내] 운송대가 이동하는 동안 다른 현장도 조사할 수 있습니다.");
        break;
    case ChapterAction::Vehicle:
        state.vehicleRepaired = true;
        tell("운송 차량 수리 완료 / 구동계를 복구했습니다. [선택 의뢰 완료] 운송대가 차량을 "
             "회수할 수 있게 됐습니다.");
        break;
    case ChapterAction::TradeProof:
        state.tradeProofFound = true;
        tell("거래 기록 확보 / 이름을 숨긴 구매자가 N-0/17 부품을 시세보다 비싸게 샀습니다. 이 "
             "기록을 점거자들에게 보여 주고 거래 경위를 물을 수 있습니다.");
        break;
    case ChapterAction::Scrappers:
        if (!state.tradeProofFound)
        {
            tell("약탈자 / 우린 고철을 팔았을 뿐이야. 발주 내역은 입구 적재함에 있어. 확인하고 "
                 "와. 안쪽 경계선을 넘거나 먼저 쏘면 가만있지 않겠어.");
            break;
        }
        state.negotiated = true;
        state.hostile = false;
        state.bullets.clear();
        state.tracers.clear();
        for (auto &person : state.scrappers)
        {
            person.route = FindRoute(person.position, {44.5f, 39.5f});
            person.waypoint = 0;
        }
        tell("약탈자 / 주문자는 몰라. 부품을 빼고 나서 폐설비에 새 전원이 들어왔어. 장부는 단말에 "
             "있다. 우린 철수하지. 여기서 무슨 일이 일어나는지는 우리도 몰라.");
        break;
    case ChapterAction::Ledger:
        state.ledgerFound = true;
        tell("기본 장부 확보 / 발주처 이름은 지워졌습니다. 설비 N-0/17의 좌표·전력 사용 주기·교신 "
             "시각이 남아 있습니다. 다른 현장 기록도 모아 헤일로 수신망에서 대조하세요.");
        break;
    case ChapterAction::ConvoySupply: {
        auto &stats = GameLevels::Mine().GetState().player;
        int price = tutorial::CivicJustice::ShopPrice(2);
        if (stats.salvage >= price && stats.recoveryKits < 999)
        {
            stats.salvage -= price;
            ++stats.recoveryKits;
            game.ShowNotice("보급품 교환 완료 / 부품 " + std::to_string(price) +
                            "개를 건네고 회복 키트 1개를 받았습니다.");
        }
        else
        {
            game.ShowNotice(stats.recoveryKits >= 999
                                ? "보급품 교환 / 회복 키트를 더 소지할 수 없습니다."
                                : "운송대 보급상 / 회복 키트 하나에 부품 " + std::to_string(price) +
                                      "개가 필요해요. 지금은 부품이 모자라네요. [거래 안내] 악명이 높으면 "
                                      "가격이 오릅니다.");
        }
        break;
    }
    case ChapterAction::DetailLedger:
        state.detailLedgerFound = true;
        tell("상세 장부 확보 / 구매자 추적 단서를 보관합니다. 삭제된 발주처의 정체는 아직 "
             "확인되지 않았습니다.");
        break;
    }
}
void ChapterOneLevel::InteractHub()
{
    using tutorial::Dialogue;
    if (!Unlock() || Dialogue::Active() || tutorial::HospitalRecovery::Active())
    {
        return;
    }
    if (CompletedSites() == 3 && !state.recordsCompared)
    {
        bool started = Dialogue::Start(
            "MQ01D/comparison",
            {{"match",
              "수신망",
              "단말 기록",
              "광산 좌표·발전기 전력 유출·운송대 교신 시각이 설비 N-0/17의 기록과 일치합니다. 외곽 설비의 "
              "원격 화면을 연결합니다.",
              {}},
             {"observation", "나", "현장", "관문은 멈춰 있는데, 그 주변에는 차례로 불이 들어온다.", {}},
             {"ria",
              "리아",
              "현장",
              "관문 가동 기록은 없는데 외곽 설비에는 전력이 공급되고 있어. 멈춘 관문 주변에서 무언가 계속 "
              "움직이고 있었던 거야.",
              {}},
             {"route",
              "카인",
              "현장",
              "지도에 있던 다리는 사라졌습니다. 아래 수로의 옛 진입로는 기록해 뒀습니다.",
              {}},
             {"next", "카인", "현장", "먼저 그 길이 남아 있는지 확인합시다.", {}}},
            [this](const std::string &) {
                state.recordsCompared = state.waterwayUnlocked = true;
                tutorial::Game::Get().ShowNotice("1장 완료 / 수로 진입로 기록 확보. 2장 지역은 아직 진입할 "
                                                 "수 없습니다. 1장 재방문과 선택 의뢰를 계속할 수 있습니다.");
            });
        if (started && !state.comparisonStarted)
        {
            state.comparisonStarted = true;
            state.comparisonSeconds = 0;
        }
        return;
    }
    Dialogue::Start(
        "chapter1/departure",
        {{"confirm",
          "리아",
          "현장",
          "현장으로 출발할까? 헤일로 수신망으로 다시 돌아올 수 있어.",
          {{"depart", "현장으로 출발한다"}, {"prepare", "아직 준비한다"}}}},
        [this](const std::string &choice) {
            if (choice == "depart")
            {
                Enter();
            }
        },
        true);
}
float ChapterOneLevel::GetAimAngle() const
{
    const auto &world = tutorial::Game::Get().GetState();
    auto p = world.playerPosition, c = world.cameraPosition;
    auto origin =
        tutorial::WorldCamera::ToViewport({tutorial::kCanvasWidth * .5f + (p.x - p.y - c.x + c.y) * 36,
                                           tutorial::kCanvasHeight * .57f + (p.x + p.y - c.x - c.y) * 18 -
                                               models::WorldScale::aimHeightPixels});
    return std::atan2(world.cursorPosition.y - origin.y, world.cursorPosition.x - origin.x);
}
void FrontierCombat::Attack()
{
    if (tutorial::Dialogue::LocksWorld())
    {
        return;
    }
    if (!state.active || state.bullets.size() >= 128)
    {
        return;
    }
    auto &mine = GameLevels::Mine().GetState();
    combat::Shot shot;
    if (!mine.pistol.TryFire(level.GetAimAngle(), shot))
    {
        return;
    }
    ChapterProjectile projectile{{tutorial::Game::Get().GetState().playerPosition,
                                  MineLevel::GetAimDirection(shot.angle), shot.speed, shot.range,
                                  shot.damage + (mine.player.level - 1) * 4, true},
                                 ChapterTeam::Player};
    auto muzzle = models::ModelRig::Muzzle(models::ModelRig::Pose(0, false, true, 1));
    Advance(projectile, muzzle.x * models::WorldScale::characterScale);
    if (projectile.shot.remaining > 0)
    {
        state.bullets.push_back(projectile);
    }
    state.muzzleFlash = .055f;
}
void FrontierCombat::Fire(FrontierPerson &person, tutorial::Vector2 target, ChapterTeam team)
{
    auto &combat = person.combat;
    combat.engaged = true;
    combat.aimYaw = std::atan2(target.y - person.position.y, target.x - person.position.x);
    if (combat.pistol.GetRounds() == 0)
    {
        combat.pistol.Reload();
    }
    if (combat.cooldown > 0 || state.bullets.size() >= 128)
    {
        return;
    }
    float x = target.x - person.position.x, y = target.y - person.position.y;
    combat::Shot shot;
    if (!combat.pistol.TryFire(std::atan2((x + y) * .5f, x - y), shot))
    {
        return;
    }
    ChapterProjectile projectile{{person.position, MineLevel::GetAimDirection(shot.angle), shot.speed, 9,
                                  team == ChapterTeam::Companion ? 10 : 8, false},
                                 team};
    Advance(projectile, .38f);
    if (projectile.shot.remaining > 0)
    {
        state.bullets.push_back(projectile);
    }
    combat.cooldown = team == ChapterTeam::Companion ? .6f : .8f;
    combat.muzzleFlash = .055f;
    if (team == ChapterTeam::Companion)
    {
        profiling::Count(profiling::Counter::NpcShots);
    }
}
void FrontierCombat::Advance(ChapterProjectile &projectile, float distance)
{
    auto &shot = projectile.shot;
    int steps = (std::max)(1, int(std::ceil(distance / .08f)));
    float step = distance / steps;
    auto damagePerson = [&](FrontierPerson &person) {
        if (person.combat.health <= 0 || person.combat.hitGrace > 0 ||
            tutorial::Game::Distance(shot.position, person.position) > .35f)
        {
            return false;
        }
        person.combat.health = (std::max)(0, person.combat.health - shot.damage);
        person.combat.hitGrace = .25f;
        if (!person.combat.health)
        {
            person.combat.downedSeconds = 12;
            person.combat.engaged = false;
        }
        return true;
    };
    for (int i = 0; i < steps && shot.remaining > 0; ++i)
    {
        shot.position.x += shot.direction.x * step;
        shot.position.y += shot.direction.y * step;
        shot.remaining = (std::max)(0.f, shot.remaining - step);
        if (!level.HasLineOfSight(shot.position, shot.position))
        {
            shot.remaining = 0;
            break;
        }
        if (projectile.team == ChapterTeam::Scrapper)
        {
            auto &world = tutorial::Game::Get().GetState();
            if (state.invulnerability <= 0 &&
                tutorial::Game::Distance(shot.position, world.playerPosition) < .35f)
            {
                auto &stats = GameLevels::Mine().GetState().player;
                stats.health -= (std::max)(1, shot.damage - stats.defense);
                state.invulnerability = .45f;
                shot.remaining = 0;
            }
            else if (damagePerson(state.ria) || (state.kainJoined && damagePerson(state.kain)))
            {
                shot.remaining = 0;
            }
        }
        else if (!state.negotiated)
        {
            for (auto &person : state.scrappers)
            {
                if (person.combat.health > 0 &&
                    tutorial::Game::Distance(shot.position, person.position) < .4f)
                {
                    state.hostile = true;
                    person.combat.health = (std::max)(0, person.combat.health - shot.damage);
                    if (person.combat.health == 0 && projectile.team == ChapterTeam::Player)
                    {
                        GameLevels::Mine().AddExperience(kExperiencePerKill);
                    }
                    shot.remaining = 0;
                    break;
                }
            }
        }
    }
}
void FrontierCombat::Update(float deltaSeconds)
{
    auto &world = tutorial::Game::Get().GetState();
    auto &mine = GameLevels::Mine().GetState();
    mine.pistol.Update(deltaSeconds);
    state.invulnerability = (std::max)(0.f, state.invulnerability - deltaSeconds);
    state.muzzleFlash = (std::max)(0.f, state.muzzleFlash - deltaSeconds);
    for (auto &tracer : state.tracers)
    {
        tracer.remaining -= deltaSeconds;
    }
    state.tracers.erase(std::remove_if(state.tracers.begin(), state.tracers.end(),
                                       [](const Tracer &t) { return t.remaining <= 0; }),
                        state.tracers.end());
    if (world.mouseFiring)
    {
        Attack();
    }
    if (!state.ledgerFound && !state.negotiated &&
        tutorial::Game::Distance(world.playerPosition, {33, 32}) < 3.8f)
    {
        state.hostile = true;
    }
    auto updatePerson = [&](FrontierPerson &person, bool friendly) {
        auto &combat = person.combat;
        combat.pistol.Update(deltaSeconds);
        combat.cooldown = (std::max)(0.f, combat.cooldown - deltaSeconds);
        combat.hitGrace = (std::max)(0.f, combat.hitGrace - deltaSeconds);
        combat.muzzleFlash = (std::max)(0.f, combat.muzzleFlash - deltaSeconds);
        combat.engaged = false;
        if (combat.health <= 0)
        {
            if (friendly)
            {
                combat.downedSeconds -= deltaSeconds;
                if (combat.downedSeconds <= 0)
                {
                    combat.health = 50;
                    combat.hitGrace = 3;
                }
            }
            return;
        }
        if (!state.hostile || state.negotiated)
        {
            return;
        }
        if (friendly)
        {
            const FrontierPerson *target = nullptr;
            float nearest = 7;
            for (const auto &enemy : state.scrappers)
            {
                float distance = tutorial::Game::Distance(person.position, enemy.position);
                if (enemy.combat.health > 0 && distance < nearest &&
                    level.HasLineOfSight(person.position, enemy.position))
                {
                    nearest = distance;
                    target = &enemy;
                }
            }
            if (target)
            {
                Fire(person, target->position, ChapterTeam::Companion);
            }
        }
        else
        {
            tutorial::Vector2 target = world.playerPosition;
            float nearest = tutorial::Game::Distance(person.position, target);
            for (const auto *ally : {&state.ria, &state.kain})
            {
                if (ally == &state.kain && !state.kainJoined)
                {
                    continue;
                }
                float distance = tutorial::Game::Distance(person.position, ally->position);
                if (ally->combat.health > 0 && distance < nearest &&
                    level.HasLineOfSight(person.position, ally->position))
                {
                    target = ally->position;
                    nearest = distance;
                }
            }
            if (nearest < 8 && level.HasLineOfSight(person.position, target))
            {
                Fire(person, target, ChapterTeam::Scrapper);
            }
        }
    };
    updatePerson(state.ria, true);
    if (state.kainJoined)
    {
        updatePerson(state.kain, true);
    }
    for (auto &person : state.scrappers)
    {
        updatePerson(person, false);
    }
    for (auto &projectile : state.bullets)
    {
        auto start = projectile.shot.position;
        Advance(projectile, (std::min)(projectile.shot.remaining, projectile.shot.speed * deltaSeconds));
        state.tracers.push_back(
            {start, projectile.shot.position, .12f, projectile.team == ChapterTeam::Companion});
    }
    state.bullets.erase(std::remove_if(state.bullets.begin(), state.bullets.end(),
                                       [](const ChapterProjectile &p) { return p.shot.remaining <= 0; }),
                        state.bullets.end());
    if (state.tracers.size() > 256)
    {
        state.tracers.erase(state.tracers.begin(), state.tracers.end() - 256);
    }
    if (mine.player.health <= 0)
    {
        tutorial::HospitalRecovery::Begin();
    }
}
void FrontierParty::Update(float deltaSeconds)
{
    auto &world = tutorial::Game::Get().GetState();
    auto follow = [&](FrontierPerson &person, float spacing) {
        if (person.combat.health <= 0)
        {
            person.facing.walking = false;
            return;
        }
        if (tutorial::Game::Distance(person.position, world.playerPosition) > spacing)
        {
            if (person.waypoint >= person.route.size() ||
                tutorial::Game::Distance(person.route.back(), world.playerPosition) > 2)
            {
                person.route = level.FindRoute(person.position, world.playerPosition);
                person.waypoint = 0;
            }
            Move(person, 4 * deltaSeconds);
        }
        else
        {
            person.facing.walking = false;
            person.route.clear();
            person.waypoint = 0;
        }
    };
    follow(state.ria, 1.6f);
    if (state.kainJoined)
    {
        follow(state.kain, 2.4f);
    }
    if (state.escortStarted && !state.convoyRescued)
    {
        bool all = true;
        for (auto &person : state.survivors)
        {
            if (person.departureDelay > 0)
            {
                person.departureDelay = (std::max)(0.f, person.departureDelay - deltaSeconds);
                person.facing.walking = false;
            }
            else if (!person.arrived)
            {
                person.arrived = Move(person, 2.7f * deltaSeconds);
            }
            if (person.arrived)
            {
                person.facing.walking = false;
            }
            all = all && person.arrived;
        }
        if (all)
        {
            state.convoyRescued = state.kainJoined = true;
            auto &stats = GameLevels::Mine().GetState().player;
            stats.salvage = (std::min)(9999, stats.salvage + 3);
            state.receiverInspected =
                true; // The wrapped receiver is logged at the final headcount, even if skipped earlier.
            state.kain.position = {25.5f, 20.5f};
            tutorial::Dialogue::Say("chapter1/convoy/received",
                                    "카인 / 다섯 명, 전원 확인했습니다. …그 기록에도 N-0이 있군요. "
                                    "예전에 그 외곽에서 근무했습니다. 저도 동행하겠습니다. [구조 "
                                    "완료] 카인 합류 · 보급품 교환 가능 · 부품 3개 획득");
        }
    }
    if (state.negotiated)
    {
        for (auto &person : state.scrappers)
        {
            if (person.combat.health > 0 && !person.arrived)
            {
                person.arrived = Move(person, 2.7f * deltaSeconds);
            }
            else
            {
                person.facing.walking = false;
            }
        }
    }
}
void ChapterOneLevel::Update(float deltaSeconds)
{
    // Cosmetic lamps never commit story progress. Reading the final page commits MQ01D.
    if (state.comparisonStarted)
    {
        state.comparisonSeconds = (std::min)(12.5f, state.comparisonSeconds + deltaSeconds);
    }
    if (tutorial::Dialogue::LocksWorld())
    {
        return;
    }
    if (!state.active)
    {
        return;
    }
    combat.Update(deltaSeconds);
    party.Update(deltaSeconds);
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
ChapterOneLevel &GameLevels::Chapter()
{
    return Get().chapter;
}
bool GameLevels::IsCombatActive()
{
    return Mine().IsActive() || Chapter().IsActive();
}
float GameLevels::AimAngle()
{
    return Chapter().IsActive() ? Chapter().GetAimAngle() : Mine().GetAimAngle();
}
float GameLevels::MuzzleFlash()
{
    return Chapter().IsActive() ? Chapter().GetState().muzzleFlash : Mine().GetState().muzzleFlash;
}
Level *GameLevels::Active()
{
    if (Chapter().IsActive())
    {
        return &Chapter();
    }
    if (Mine().IsActive())
    {
        return &Mine();
    }
    return nullptr;
}
void GameLevels::Reset()
{
    for (Level *level : {static_cast<Level *>(&Mine()), static_cast<Level *>(&Chapter())})
    {
        level->Reset();
    }
}
} // namespace game
