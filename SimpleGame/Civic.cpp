#include "stdafx.h"
#include "Civic.h"
#include "Drawing.h"
#include "Overworld.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace tutorial
{
namespace
{
CivicState civic;
struct Obstacle
{
    Vector2 minimum, maximum;
};
std::map<std::pair<int, int>, std::vector<Obstacle>> obstacleCells;
void BuildObstacles(const GameState &world)
{
    obstacleCells.clear();
    auto insert = [](Vector2 lo, Vector2 hi) {
        for (int x = int(std::floor(lo.x / 8)); x <= int(std::floor(hi.x / 8)); ++x)
        {
            for (int y = int(std::floor(lo.y / 8)); y <= int(std::floor(hi.y / 8)); ++y)
            {
                obstacleCells[{x, y}].push_back({lo, hi});
            }
        }
    };
    for (const auto &b : world.buildings)
    {
        insert({b.x, b.y}, {b.x + b.width, b.y + b.depth});
    }
    for (const auto &p : Overworld::Scenery().Props())
    {
        if (p.solid)
        {
            insert(p.minimum, p.maximum);
        }
    }
}
constexpr Vector2 kPeople[] = {kLiaPosition, kMaraPosition, kCityKainPosition};
Vector2 Add(Vector2 a, Vector2 b, float scale = 1)
{
    return {a.x + b.x * scale, a.y + b.y * scale};
}
Vector2 Sub(Vector2 a, Vector2 b)
{
    return {a.x - b.x, a.y - b.y};
}
Vector2 Unit(Vector2 p)
{
    float length = std::hypot(p.x, p.y);
    return length > .0001f ? Vector2{p.x / length, p.y / length} : Vector2{1, 0};
}
unsigned Random()
{
    civic.randomState ^= civic.randomState << 13;
    civic.randomState ^= civic.randomState >> 17;
    civic.randomState ^= civic.randomState << 5;
    return civic.randomState;
}
float PlayerHeight()
{
    const auto &world = Game::Get().GetState();
    return world.isBikeMounted ? world.bikeElevation : 0;
}
void Impact(BodyImpact &body, Vector2 impulse, bool dead = false)
{
    body.velocity = impulse;
    body.seconds = dead ? 2.f : .6f;
    body.invulnerability = .7f;
    body.response = dead                                   ? BodyResponse::Dead
                    : std::hypot(impulse.x, impulse.y) > 3 ? BodyResponse::Knockdown
                                                           : BodyResponse::Stagger;
}
bool SegmentClear(Vector2 a, Vector2 b, float radius = .3f)
{
    int steps = (std::max)(1, int(std::ceil(Game::Distance(a, b) / .2f)));
    for (int i = 0; i <= steps; ++i)
    {
        if (CivicJustice::Solid(Add(a, Sub(b, a), float(i) / steps), radius))
        {
            return false;
        }
    }
    return true;
}
// Bounded local emergency routing. Police may cross streets, but never buildings or solid props.
std::vector<Vector2> EmergencyRoute(Vector2 start, Vector2 target)
{
    constexpr int size = 49, half = size / 2, count = size * size;
    auto position = [=](int index) {
        return Vector2{start.x + (index % size - half) * 2, start.y + (index / size - half) * 2};
    };
    int gx = int(std::round((target.x - start.x) / 2)) + half,
        gy = int(std::round((target.y - start.y) / 2)) + half;
    gx = (std::max)(1, (std::min)(size - 2, gx));
    gy = (std::max)(1, (std::min)(size - 2, gy));
    int goal = gy * size + gx, origin = half * size + half;
    std::vector<float> costs(count, 1.e9f);
    std::vector<int> parents(count, -1);
    using Candidate = std::pair<float, int>;
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> open;
    costs[origin] = 0;
    parents[origin] = origin;
    open.push({0.f, origin});
    int best = origin;
    float closest = Game::Distance(start, target);
    int expansions = 0;
    while (!open.empty() && ++expansions < 2400)
    {
        int current = open.top().second;
        open.pop();
        auto p = position(current);
        float remaining = Game::Distance(p, target);
        if (remaining < closest)
        {
            closest = remaining;
            best = current;
        }
        if (current == goal || remaining < 1.5f)
        {
            best = current;
            break;
        }
        for (auto d : {Vector2{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}})
        {
            int x = current % size + int(d.x), y = current / size + int(d.y);
            if (x < 0 || y < 0 || x >= size || y >= size)
            {
                continue;
            }
            int next = y * size + x;
            float cost = costs[current] + std::hypot(d.x, d.y) * 2;
            if (cost >= costs[next] || !SegmentClear(p, position(next)))
            {
                continue;
            }
            parents[next] = current;
            costs[next] = cost;
            open.push({cost + Game::Distance(position(next), target), next});
        }
    }
    std::vector<Vector2> result;
    for (int node = best; node != origin && node >= 0; node = parents[node])
    {
        result.push_back(position(node));
    }
    std::reverse(result.begin(), result.end());
    return result;
}
void AdvanceImpact(Vector2 &position, BodyImpact &body, float dt)
{
    body.invulnerability = (std::max)(0.f, body.invulnerability - dt);
    if (body.seconds > 0)
    {
        auto next = Add(position, body.velocity, dt);
        if (SegmentClear(position, next, .32f))
        {
            position = next;
        }
        body.velocity.x *= std::exp(-7 * dt);
        body.velocity.y *= std::exp(-7 * dt);
        body.seconds = (std::max)(0.f, body.seconds - dt);
    }
    if (body.seconds <= 0 && body.response != BodyResponse::Dead)
    {
        body.response = BodyResponse::None;
    }
}
bool CarContains(const TrafficVehicle &car, Vector2 position, float radius)
{
    Vector2 d = {std::cos(car.yaw), std::sin(car.yaw)}, n = {-d.y, d.x}, q = Sub(position, car.position);
    const auto &model = models::ModelLibrary::Get().vehicles[size_t(car.id % 4)].body;
    const auto &mesh = models::ResourceAssets::Get(model.resourceId);
    float halfX = mesh.physicalSize.x * .5f, halfY = mesh.physicalSize.y * .5f;
    float x = q.x * d.x + q.y * d.y, y = q.x * n.x + q.y * n.y;
    float dx = (std::max)(0.f, std::fabs(x) - halfX), dy = (std::max)(0.f, std::fabs(y) - halfY);
    return dx * dx + dy * dy < radius * radius;
}
bool SweptCarContains(const TrafficVehicle &car, Vector2 p, float radius)
{
    float travel = Game::Distance(car.previousPosition, car.position);
    if (Game::Distance(p, car.position) > 4 + travel)
    {
        return false;
    }
    int steps = (std::max)(1, int(std::ceil(travel / .08f)));
    auto sample = car;
    for (int step = 0; step <= steps; ++step)
    {
        float t = float(step) / steps;
        sample.position = Add(car.previousPosition, Sub(car.position, car.previousPosition), t);
        sample.yaw = car.previousYaw + std::remainder(car.yaw - car.previousYaw, 2 * kPi) * t;
        if (CarContains(sample, p, radius))
        {
            return true;
        }
    }
    return false;
}
Vector2 PushFromCar(const TrafficVehicle &car, Vector2 p, float radius)
{
    auto &mesh =
        models::ResourceAssets::Get(models::ModelLibrary::Get().vehicles[size_t(car.id % 4)].body.resourceId);
    Vector2 d = {std::cos(car.yaw), std::sin(car.yaw)}, n = {-d.y, d.x}, q = Sub(p, car.position);
    float x = q.x * d.x + q.y * d.y, y = q.x * n.x + q.y * n.y, hx = mesh.physicalSize.x * .5f,
          hy = mesh.physicalSize.y * .5f;
    Vector2 result = p;
    float best = 1.e9f;
    for (float side : {-1.f, 1.f})
    {
        Vector2 candidates[] = {
            Add(Add(car.position, d, Game::Clamp(x, -hx, hx)), n, side * (hy + radius + .04f)),
            Add(Add(car.position, n, Game::Clamp(y, -hy, hy)), d, side * (hx + radius + .04f))};
        for (auto candidate : candidates)
        {
            if (Game::Distance(candidate, p) < best && SegmentClear(p, candidate, radius))
            {
                best = Game::Distance(candidate, p);
                result = candidate;
            }
        }
    }
    return result;
}
Vector2 Separate(Vector2 from, Vector2 to, float radius, int ignoreResident, bool player,
                 int ignoreOfficer = -1)
{
    auto &world = Game::Get().GetState();
    Vector2 result = from;
    int steps = (std::max)(1, int(std::ceil(Game::Distance(from, to) / .12f)));
    for (int step = 1; step <= steps; ++step)
    {
        Vector2 candidate = Add(result, Sub(to, from), 1.f / steps);
        if ((player ? Game::Get().IsPositionBlocked(candidate) : CivicJustice::Solid(candidate, radius)))
        {
            break;
        }
        if (PlayerHeight() < .7f || !player)
        {
            auto separate = [&](Vector2 other, float otherRadius) {
                auto delta = Sub(candidate, other);
                float distance = std::hypot(delta.x, delta.y), minimum = radius + otherRadius;
                if (distance < minimum)
                {
                    candidate = Add(other, Unit(distance < .0001f ? Sub(from, other) : delta), minimum);
                }
            };
            for (size_t index = 0; index < world.residents.size(); ++index)
            {
                if (int(index) != ignoreResident && world.residents[index].health > 0 &&
                    !(index == Overworld::kKainResidentSlot &&
                      game::GameLevels::Chapter().GetState().kainJoined))
                {
                    separate(world.residents[index].position, .29f);
                }
            }
            for (size_t index = 0; index < std::size(kPeople); ++index)
            {
                if (civic.landmarkHealth[index] > 0 &&
                    (index != 2 || game::GameLevels::Chapter().GetState().kainJoined))
                {
                    separate(kPeople[index], .30f);
                }
            }
            for (size_t index = 0; index < civic.officers.size(); ++index)
            {
                const auto &officer = civic.officers[index];
                if (officer.health > 0 && int(index) != ignoreOfficer)
                {
                    separate(officer.position, .30f);
                }
            }
            if (!player && !world.isBikeMounted)
            {
                separate(world.playerPosition, .32f);
            }
        }
        const auto &traffic = Overworld::Traffic();
        for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
        {
            const auto &car = traffic.Vehicles()[i];
            if (std::fabs(traffic.RenderElevation(i) - (player ? PlayerHeight() : 0)) < .7f &&
                CarContains(car, candidate, radius))
            {
                candidate = result;
            }
        }
        if ((player ? Game::Get().IsPositionBlocked(candidate) : CivicJustice::Solid(candidate, radius)))
        {
            break;
        }
        result = candidate;
    }
    return result;
}
void TransferToHospital()
{
    auto &world = Game::Get().GetState();
    auto &mine = game::GameLevels::Mine().GetState();
    auto &stats = mine.player;
    civic.lostLevels =
        stats.level > 1 ? (std::max)(1, int(std::ceil(stats.level * HospitalRecovery::kLevelLoss))) : 0;
    stats.level = (std::max)(1, stats.level - civic.lostLevels);
    stats.experience = game::MineLevel::ExperienceForLevel(stats.level);
    stats.maxHealth = 100 + (stats.level - 1) * 20;
    stats.attack = 18 + (stats.level - 1) * 4;
    stats.defense = 2 + (stats.level - 1) * 2;
    auto take = [](int &asset) {
        int amount = asset > 0 ? (std::max)(1, int(std::floor(asset * HospitalRecovery::kAssetLoss))) : 0;
        amount = (std::min)(asset, amount);
        asset -= amount;
        return amount;
    };
    civic.lostCredits = take(stats.credits);
    civic.lostSalvage = take(stats.salvage);
    stats.health = stats.maxHealth;
    mine.active = false;
    mine.bullets.clear();
    mine.tracers.clear();
    auto &chapter = game::GameLevels::Chapter().GetState();
    chapter.active = false;
    chapter.bullets.clear();
    chapter.tracers.clear();
    auto hospital =
        civic.hospitals.empty() ? HospitalSite{0, kHomeDoor} : civic.hospitals[size_t(civic.hospital)];
    world.isInsideHome = world.isInObservatory = world.isBikeMounted = world.mapVisible = world.mouseFiring =
        false;
    world.playerPosition = world.cameraPosition = hospital.position;
    world.bikeSpeed = world.bikeElevation = 0;
    world.bikePosition = hospital.position;
    for (Vector2 offset : {Vector2{1.5f, 0}, {-1.5f, 0}, {0, 1.5f}, {0, -1.5f}})
    {
        if (!CivicJustice::Solid(Add(hospital.position, offset), .65f))
        {
            world.bikePosition = Add(hospital.position, offset);
            break;
        }
    }
    std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
    civic.heat = 0;
    civic.wanted = 0;
    civic.officers.clear();
    civic.bullets.clear();
    civic.tracers.clear();
    civic.playerImpact = {};
    civic.playerGrace = 5;
    mine.invulnerability = 5;
    ++civic.deaths;
    Game::Get().ShowNotice(std::string(Overworld::Cities()[hospital.city].name) + " 의료소 / 레벨 -" +
                           std::to_string(civic.lostLevels) + " · 크레딧 -" +
                           std::to_string(civic.lostCredits) + " · 거래 부품 -" +
                           std::to_string(civic.lostSalvage) +
                           ". 조사 기록은 유지되며 보안망의 평판은 남습니다.");
}
} // namespace

CivicState &CivicJustice::State()
{
    return civic;
}
bool CivicJustice::IsExterior()
{
    const auto &world = Game::Get().GetState();
    return !game::GameLevels::Active() && !world.isInsideHome && !world.isInObservatory;
}
bool CivicJustice::Solid(Vector2 p, float radius)
{
    if (TransitLayout::IsBlocked(p, radius))
    {
        return true;
    }
    if (p.x - radius < kWorldMinX || p.y - radius < kWorldMinY || p.x + radius > kWorldMaxX ||
        p.y + radius > kWorldMaxY)
    {
        return true;
    }
    for (int x = int(std::floor((p.x - radius) / 8)); x <= int(std::floor((p.x + radius) / 8)); ++x)
    {
        for (int y = int(std::floor((p.y - radius) / 8)); y <= int(std::floor((p.y + radius) / 8)); ++y)
        {
            auto cell = obstacleCells.find({x, y});
            if (cell == obstacleCells.end())
            {
                continue;
            }
            for (const auto &b : cell->second)
            {
                if (p.x > b.minimum.x - radius && p.x < b.maximum.x + radius && p.y > b.minimum.y - radius &&
                    p.y < b.maximum.y + radius)
                {
                    return true;
                }
            }
        }
    }
    return false;
}
bool CivicJustice::ClearLine(Vector2 from, Vector2 to)
{
    return SegmentClear(from, to, .05f);
}
void CivicJustice::Reset(GameState &world)
{
    civic = CivicState{};
    BuildObstacles(world);
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        Vector2 best = center;
        float score = 1.e9f;
        for (const auto &connection : CityPlan::Connections())
        {
            const auto &building = world.buildings[connection.building];
            float distance = Game::Distance(connection.entrance, center);
            bool home = std::fabs(building.x - kHomeBuildingPosition.x) < .01f &&
                        std::fabs(building.y - kHomeBuildingPosition.y) < .01f;
            if (distance > 46 || home || building.type == models::BuildingType::Observatory ||
                Solid(connection.entrance, .35f))
            {
                continue;
            }
            float rank = distance + (building.type == models::BuildingType::TransitAtrium ? -20.f : 0);
            if (rank < score)
            {
                score = rank;
                best = connection.entrance;
            }
        }
        if (score == 1.e9f)
        {
            for (int i = 0; i < 1600; ++i)
            {
                auto p = Add(center, {float(i % 40) - 20, float(i / 40) - 20});
                if (!Solid(p, .5f) && !RoadNetwork::Get().Contains(p, 1))
                {
                    best = p;
                    break;
                }
            }
        }
        civic.hospitals.push_back({city, best});
    }
}
int CivicJustice::WantedForHeat(float heat)
{
    constexpr float thresholds[] = {20, 80, 180, 360, 650};
    int result = 0;
    for (float threshold : thresholds)
    {
        result += heat >= threshold;
    }
    return result;
}
float CivicJustice::PriceMultiplier()
{
    return 1 + civic.infamy * .0075f;
}
int CivicJustice::ShopPrice(int basePrice)
{
    return int((std::min)(double(std::numeric_limits<int>::max()),
                          std::ceil((std::max)(0, basePrice) * double(PriceMultiplier()))));
}
int CivicJustice::LivingPopulation(int city)
{
    auto &world = Game::Get().GetState();
    int count = 0;
    bool kain = game::GameLevels::Chapter().GetState().kainJoined;
    for (size_t i = 0; i < world.residents.size(); ++i)
    {
        if (world.residents[i].health > 0 && (city < 0 || world.residents[i].cityIndex == city) &&
            !(i == Overworld::kKainResidentSlot && kain))
        {
            ++count;
        }
    }
    if (city < 0 || city == 0)
    {
        count += (civic.landmarkHealth[0] > 0) + (civic.landmarkHealth[1] > 0);
        count += kain && civic.landmarkHealth[2] > 0;
    }
    return count;
}
void CivicJustice::Report(Offense offense, Vector2 position)
{
    if (HospitalRecovery::Active())
    {
        return;
    }
    constexpr float heat[] = {12, 35, 140, 15, 65, 60, 180, 30, 80};
    constexpr float infamy[] = {.3f, 3, 14, 1, 6, 5, 18, 2, 8};
    if (offense == Offense::Discharge && civic.dischargeCooldown > 0)
    {
        return;
    }
    if (offense == Offense::Discharge)
    {
        civic.dischargeCooldown = .75f;
    }
    civic.heat = Game::Clamp(civic.heat + heat[size_t(offense)], 0, 1000);
    civic.infamy = Game::Clamp(civic.infamy + infamy[size_t(offense)], 0, 100);
    civic.sinceCrime = civic.unseenSeconds = 0;
    civic.lastCrime = position;
    civic.wanted = WantedForHeat(civic.heat);
    auto &world = Game::Get().GetState();
    for (auto &person : world.residents)
    {
        if (person.health > 0 && Game::Distance(person.position, position) < 12 &&
            ClearLine(position, person.position))
        {
            person.fearSeconds = (std::max)(person.fearSeconds, 8.f);
        }
    }

    if (civic.wanted)
    {
        civic.spawnSeconds = (std::min)(civic.spawnSeconds, 2.f);
    }
}
void CivicJustice::Update(float dt)
{
    if (dt <= 0 || HospitalRecovery::Active())
    {
        return;
    }
    civic.sinceCrime += dt;
    civic.dischargeCooldown = (std::max)(0.f, civic.dischargeCooldown - dt);
    civic.playerGrace = (std::max)(0.f, civic.playerGrace - dt);
    for (size_t i = 0; i < civic.landmarkHealth.size(); ++i)
    {
        if (civic.landmarkHealth[i] <= 0)
        {
            civic.landmarkRecovery[i] -= dt;
            if (civic.landmarkRecovery[i] <= 0)
            {
                civic.landmarkHealth[i] = 100;
            }
        }
    }
    bool visible = false;
    auto &world = Game::Get().GetState();
    for (auto &officer : civic.officers)
    {
        officer.cooldown = (std::max)(0.f, officer.cooldown - dt);
        officer.muzzleFlash = (std::max)(0.f, officer.muzzleFlash - dt);
        if (officer.health <= 0)
        {
            officer.corpseSeconds += dt;
            continue;
        }
        if (!IsExterior())
        {
            continue;
        }
        auto delta = Sub(world.playerPosition, officer.position);
        float distance = std::hypot(delta.x, delta.y);
        bool sees =
            PlayerHeight() < .8f && distance < 26 && ClearLine(officer.position, world.playerPosition);
        visible |= sees;
        if (sees)
        {
            officer.lastKnown = world.playerPosition;
        }
        if (civic.wanted == 0)
        {
            officer.lastKnown = Add(officer.position, Unit(Sub(officer.position, world.playerPosition)), 15);
        }
        officer.navigationSeconds -= dt;
        if (officer.navigationSeconds <= 0)
        {
            officer.route = EmergencyRoute(officer.position, sees ? world.playerPosition : officer.lastKnown);
            officer.waypoint = 0;
            officer.navigationSeconds = 1.2f + float(officer.id % 4) * .15f;
        }
        auto previous = officer.position;
        if ((!sees || distance > 5) && officer.waypoint < officer.route.size() && officer.impact.seconds <= 0)
        {
            auto target = officer.route[officer.waypoint];
            float remaining = Game::Distance(target, officer.position);
            auto next = Add(officer.position, Unit(Sub(target, officer.position)),
                            (std::min)(remaining, (2.2f + .25f * officer.tier) * dt));
            if (SegmentClear(officer.position, next, .32f))
            {
                officer.position =
                    Separate(officer.position, next, .3f, -1, false, int(&officer - civic.officers.data()));
            }
            if (remaining < .3f)
            {
                ++officer.waypoint;
            }
        }
        officer.facing.Update(previous, officer.position);
        if (sees)
        {
            officer.facing.yaw = std::atan2(delta.y, delta.x);
        }
        if (civic.wanted > 0 && sees && distance < 8 + officer.tier * 1.5f && officer.cooldown <= 0)
        {
            auto &stats = game::GameLevels::Mine().GetState().player;
            int power =
                stats.defense + (std::max)(8 + officer.tier * 4, int(stats.maxHealth * .015f * officer.tier));
            CivicCombat::DamagePlayer(power, Add({}, Unit(delta), 1.5f));
            civic.tracers.push_back({officer.position, world.playerPosition, .12f, true});
            officer.cooldown = 1.6f - officer.tier * .2f;
            officer.muzzleFlash = .07f;
        }
    }
    civic.unseenSeconds = visible ? 0 : civic.unseenSeconds + dt;
    if (civic.sinceCrime > 20 && civic.unseenSeconds > 15)
    {
        civic.heat = (std::max)(0.f, civic.heat - 8 * dt);
        civic.wanted = WantedForHeat(civic.heat);
    }
    if (civic.sinceCrime > 90)
    {
        civic.infamy = (std::max)(0.f, civic.infamy - .005f * dt);
    }
    civic.officers.erase(
        std::remove_if(civic.officers.begin(), civic.officers.end(),
                       [&](const SecurityOfficer &o) {
                           return (o.health <= 0 && o.corpseSeconds > 20 &&
                                   TrafficSystem::OutsideView(o.position, world.cameraPosition, 80)) ||
                                  (civic.wanted == 0 &&
                                   TrafficSystem::OutsideView(o.position, world.cameraPosition, 100));
                       }),
        civic.officers.end());
    civic.spawnSeconds -= dt;
    size_t living = std::count_if(civic.officers.begin(), civic.officers.end(),
                                  [](const SecurityOfficer &o) { return o.health > 0; });
    if (civic.officers.size() >= 16 && living < size_t(civic.wanted * 2))
    {
        auto body = std::find_if(civic.officers.begin(), civic.officers.end(),
                                 [](const SecurityOfficer &o) { return o.health <= 0; });
        if (body != civic.officers.end())
        {
            civic.officers.erase(body);
        }
    }
    if (IsExterior() && civic.wanted > 0 && civic.spawnSeconds <= 0 && living < size_t(civic.wanted * 2) &&
        civic.officers.size() < 16)
    {
        civic.spawnSeconds = 3.5f;
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            float angle = float(Random() % 6283) * .001f;
            float range = 18 + float(Random() % 17);
            auto p = Add(world.playerPosition, {std::cos(angle), std::sin(angle)}, range);
            if (Solid(p, .6f) || !TrafficSystem::OutsideView(p, world.cameraPosition, 90))
            {
                continue;
            }
            auto route = EmergencyRoute(p, world.playerPosition);
            if (route.empty())
            {
                continue;
            }
            SecurityOfficer officer;
            officer.id = civic.nextOfficer++;
            officer.position = p;
            officer.lastKnown = world.playerPosition;
            officer.tier = civic.wanted;
            officer.health =
                60 + 40 * civic.wanted + 8 * (game::GameLevels::Mine().GetState().player.level - 1);
            officer.route = std::move(route);
            officer.navigationSeconds = 1;
            civic.officers.push_back(std::move(officer));
            break;
        }
    }
}

bool CivicCombat::Available()
{
    return CivicJustice::IsExterior() && !Game::Get().GetState().isBikeMounted && !HospitalRecovery::Active();
}
void CivicCombat::DamagePlayer(int damage, Vector2 impulse)
{
    auto &stats = game::GameLevels::Mine().GetState().player;
    if (damage <= 0 || stats.health <= 0 || HospitalRecovery::Active() || civic.playerGrace > 0 ||
        civic.playerImpact.invulnerability > 0)
    {
        return;
    }
    stats.health = (std::max)(0, stats.health - (std::max)(1, damage - stats.defense));
    Impact(civic.playerImpact, impulse, stats.health == 0);
    if (stats.health == 0)
    {
        HospitalRecovery::Begin();
    }
}
bool CivicCombat::DamageResident(size_t index, int damage, Vector2 impulse, bool playerOwned)
{
    auto &world = Game::Get().GetState();
    if (index >= world.residents.size() || damage <= 0 ||
        (index == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined))
    {
        return false;
    }
    auto &person = world.residents[index];
    if (person.health <= 0)
    {
        return false;
    }
    person.health = (std::max)(0, person.health - damage);
    person.fearSeconds = 15;
    person.conversationSeconds = 0;
    person.route.clear();
    person.routeCursor = 0;
    person.crossingJunction = -1;
    Impact(person.impact, impulse, person.health == 0);
    person.activity = person.health ? ResidentActivity::Injured : ResidentActivity::Dead;
    if (playerOwned)
    {
        CivicJustice::Report(person.health ? Offense::Assault : Offense::Homicide, person.position);
    }
    return true;
}
bool CivicCombat::DamageOfficer(size_t index, int damage, Vector2 impulse, bool playerOwned)
{
    if (index >= civic.officers.size() || damage <= 0 || civic.officers[index].health <= 0)
    {
        return false;
    }
    auto &officer = civic.officers[index];
    officer.health = (std::max)(0, officer.health - damage);
    Impact(officer.impact, impulse, officer.health == 0);
    if (playerOwned)
    {
        CivicJustice::Report(officer.health ? Offense::AssaultOfficer : Offense::OfficerKilled,
                             officer.position);
    }
    return true;
}
bool CivicCombat::DamageLandmark(size_t index, int damage)
{
    if (index >= civic.landmarkHealth.size() ||
        (index == 2 && !game::GameLevels::Chapter().GetState().kainJoined) ||
        civic.landmarkHealth[index] <= 0 || damage <= 0)
    {
        return false;
    }
    civic.landmarkHealth[index] = (std::max)(0, civic.landmarkHealth[index] - damage);
    civic.landmarkRecovery[index] = 60;
    CivicJustice::Report(civic.landmarkHealth[index] ? Offense::Assault : Offense::AggravatedAssault,
                         kPeople[index]);
    return true;
}
bool CivicCombat::LandmarkDown(Vector2 position)
{
    for (size_t i = 0; i < std::size(kPeople); ++i)
    {
        if (civic.landmarkHealth[i] <= 0 && Game::Distance(position, kPeople[i]) < .6f)
        {
            return true;
        }
    }
    return false;
}
void CivicCombat::Fire()
{
    if (!Available() || civic.meleeSeconds > 0)
    {
        return;
    }
    auto &world = Game::Get().GetState();
    auto &mine = game::GameLevels::Mine();
    combat::Shot shot;
    if (civic.bullets.size() >= 96 || !mine.GetState().pistol.TryFire(mine.GetAimAngle(), shot))
    {
        return;
    }
    civic.bullets.push_back({world.playerPosition, game::MineLevel::GetAimDirection(shot.angle), shot.speed,
                             shot.range, shot.damage + (mine.GetState().player.level - 1) * 4, true});
    civic.muzzleFlash = .06f;
    CivicJustice::Report(Offense::Discharge, world.playerPosition);
}
void CivicCombat::Melee()
{
    if (!Available() || civic.meleeSeconds > 0)
    {
        return;
    }
    civic.meleeSeconds = .65f;
    auto &world = Game::Get().GetState();
    auto aim = game::MineLevel::GetAimDirection(game::GameLevels::Mine().GetAimAngle());
    auto hit = [&](Vector2 p) {
        auto d = Sub(p, world.playerPosition);
        float range = std::hypot(d.x, d.y);
        return range < 1.45f && (range < .45f || d.x * aim.x + d.y * aim.y > range * .25f) &&
               CivicJustice::ClearLine(world.playerPosition, p);
    };
    auto impulse = Add({}, aim, 4);
    for (size_t i = 0; i < civic.officers.size(); ++i)
    {
        if (hit(civic.officers[i].position) && DamageOfficer(i, 30, impulse))
        {
            return;
        }
    }
    for (size_t i = 0; i < world.residents.size(); ++i)
    {
        if (hit(world.residents[i].position) && DamageResident(i, 30, impulse, true))
        {
            return;
        }
    }
    for (size_t i = 0; i < std::size(kPeople); ++i)
    {
        if (hit(kPeople[i]) && DamageLandmark(i, 30))
        {
            return;
        }
    }
    for (const auto &car : Overworld::Traffic().Vehicles())
    {
        if (CarContains(car, Add(world.playerPosition, aim, .8f), .5f))
        {
            Overworld::Traffic().DamageVehicle(car.id, 22);
            return;
        }
    }
}
void CivicCombat::Update(float dt)
{
    civic.meleeSeconds = (std::max)(0.f, civic.meleeSeconds - dt);
    civic.muzzleFlash = (std::max)(0.f, civic.muzzleFlash - dt);
    if (Available())
    {
        game::GameLevels::Mine().GetState().pistol.Update(dt);
    }
    if (Available() && Game::Get().GetState().mouseFiring)
    {
        Fire();
    }
    for (auto &tracer : civic.tracers)
    {
        tracer.remaining -= dt;
    }
    civic.tracers.erase(std::remove_if(civic.tracers.begin(), civic.tracers.end(),
                                       [](const game::Tracer &t) { return t.remaining <= 0; }),
                        civic.tracers.end());
    if (!CivicJustice::IsExterior())
    {
        civic.bullets.clear();
        civic.tracers.clear();
        return;
    }
    auto &world = Game::Get().GetState();
    auto &traffic = Overworld::Traffic();
    for (auto &bullet : civic.bullets)
    {
        auto start = bullet.position;
        float movement = (std::min)(bullet.remaining, bullet.speed * dt);
        int steps = (std::max)(1, int(std::ceil(movement / .08f)));
        for (int step = 0; step < steps && bullet.remaining > 0; ++step)
        {
            auto next = Add(bullet.position, bullet.direction, movement / steps);
            if (CivicJustice::Solid(next, .03f))
            {
                bullet.remaining = 0;
                break;
            }
            bullet.position = next;
            bullet.remaining = (std::max)(0.f, bullet.remaining - movement / steps);
            auto impulse = Add({}, bullet.direction, 2);
            for (size_t i = 0; i < civic.officers.size(); ++i)
            {
                if (Game::Distance(next, civic.officers[i].position) < .35f &&
                    DamageOfficer(i, bullet.damage, impulse))
                {
                    bullet.remaining = 0;
                    break;
                }
            }
            if (bullet.remaining <= 0)
            {
                break;
            }
            for (size_t i = 0; i < world.residents.size(); ++i)
            {
                if (Game::Distance(next, world.residents[i].position) < .35f &&
                    DamageResident(i, bullet.damage, impulse, true))
                {
                    bullet.remaining = 0;
                    break;
                }
            }
            if (bullet.remaining <= 0)
            {
                break;
            }
            for (size_t i = 0; i < std::size(kPeople); ++i)
            {
                if (Game::Distance(next, kPeople[i]) < .35f && DamageLandmark(i, bullet.damage))
                {
                    bullet.remaining = 0;
                    break;
                }
            }
            if (bullet.remaining <= 0)
            {
                break;
            }
            for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
            {
                if (traffic.RenderElevation(i) < .7f && CarContains(traffic.Vehicles()[i], next, .04f))
                {
                    traffic.DamageVehicle(traffic.Vehicles()[i].id, bullet.damage);
                    bullet.remaining = 0;
                    break;
                }
            }
        }
        civic.tracers.push_back({start, bullet.position, .12f, false});
    }
    civic.bullets.erase(std::remove_if(civic.bullets.begin(), civic.bullets.end(),
                                       [](const game::Bullet &b) { return b.remaining <= 0; }),
                        civic.bullets.end());
    if (civic.tracers.size() > 160)
    {
        civic.tracers.erase(civic.tracers.begin(), civic.tracers.end() - 160);
    }
}

Vector2 BodyCollision::MovePlayer(Vector2 from, Vector2 desired)
{
    if (!CivicJustice::IsExterior())
    {
        return desired;
    }
    return Separate(from, desired, .32f, -1, true);
}
Vector2 BodyCollision::MoveResident(size_t index, Vector2 from, Vector2 desired)
{
    return Separate(from, desired, .29f, int(index), false);
}
void BodyCollision::Update(float dt)
{
    if (!CivicJustice::IsExterior())
    {
        return;
    }
    auto &world = Game::Get().GetState();
    if (!world.isBikeMounted)
    {
        auto before = world.playerPosition;
        AdvanceImpact(world.playerPosition, civic.playerImpact, dt);
        world.playerPosition = MovePlayer(before, world.playerPosition);
    }
    else
    {
        civic.playerImpact.invulnerability = (std::max)(0.f, civic.playerImpact.invulnerability - dt);
    }
    for (auto &resident : world.residents)
    {
        auto before = resident.position;
        AdvanceImpact(resident.position, resident.impact, dt);
        resident.position =
            MoveResident(size_t(&resident - world.residents.data()), before, resident.position);
        if (resident.health <= 0)
        {
            resident.corpseSeconds += dt;
        }
    }
    for (auto &officer : civic.officers)
    {
        AdvanceImpact(officer.position, officer.impact, dt);
    }
}
void BodyCollision::VehicleContacts(float)
{
    if (!CivicJustice::IsExterior() || HospitalRecovery::Active())
    {
        return;
    }
    auto &world = Game::Get().GetState();
    const auto &traffic = Overworld::Traffic();
    for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
    {
        const auto &car = traffic.Vehicles()[i];
        float height = traffic.RenderElevation(i);
        if (std::fabs(height - PlayerHeight()) < .7f &&
            SweptCarContains(car, world.playerPosition, world.isBikeMounted ? .65f : .3f))
        {
            float speed = (std::max)(car.speed, world.isBikeMounted ? world.bikeSpeed : 0);
            if (speed > .8f && civic.playerImpact.invulnerability <= 0)
            {
                auto impulse =
                    Add({}, Unit(Sub(world.playerPosition, car.position)), (std::min)(7.f, 2 + speed));
                bool reckless = world.isBikeMounted && world.bikeSpeed > 3;
                CivicCombat::DamagePlayer(int(8 + speed * 4), impulse);
                if (reckless)
                {
                    Overworld::Traffic().DamageVehicle(car.id, int(speed * 8));
                    CivicJustice::Report(Offense::RecklessDriving, world.playerPosition);
                }
                world.bikeSpeed = 0;
            }
            if (CarContains(car, world.playerPosition, world.isBikeMounted ? .65f : .32f))
            {
                world.playerPosition =
                    PushFromCar(car, world.playerPosition, world.isBikeMounted ? .65f : .32f);
                if (world.isBikeMounted)
                {
                    world.bikePosition = world.playerPosition;
                }
                Overworld::Traffic().StopForImpact(car.id);
            }
        }
        if (height > .7f)
        {
            continue;
        }
        for (size_t person = 0; person < world.residents.size(); ++person)
        {
            auto &resident = world.residents[person];
            if (person == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined)
            {
                continue;
            }
            if (resident.health > 0 && resident.impact.invulnerability <= 0 &&
                SweptCarContains(car, resident.position, .3f))
            {
                float impactSpeed = car.speed;
                if (impactSpeed > .8f)
                {
                    CivicCombat::DamageResident(
                        person, int(5 + impactSpeed * 7),
                        Add({}, Unit(Sub(resident.position, car.position)), (std::min)(7.f, impactSpeed)),
                        false);
                }
                if (CarContains(car, resident.position, .3f))
                {
                    resident.position = PushFromCar(car, resident.position, .3f);
                    if (impactSpeed > .05f)
                    {
                        Overworld::Traffic().StopForImpact(car.id);
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
    {
        const auto &car = traffic.Vehicles()[i];
        if (traffic.RenderElevation(i) > .7f)
        {
            continue;
        }
        for (size_t person = 0; person < civic.officers.size(); ++person)
        {
            auto &officer = civic.officers[person];
            if (officer.health <= 0 || officer.impact.invulnerability > 0 ||
                !SweptCarContains(car, officer.position, .3f))
            {
                continue;
            }
            float speed = car.speed;
            if (speed > .8f)
            {
                CivicCombat::DamageOfficer(
                    person, int(5 + speed * 7),
                    Add({}, Unit(Sub(officer.position, car.position)), (std::min)(7.f, speed)), false);
            }
            if (CarContains(car, officer.position, .3f))
            {
                officer.position = PushFromCar(car, officer.position, .3f);
                if (speed > .05f)
                {
                    Overworld::Traffic().StopForImpact(car.id);
                }
            }
        }
    }
    if (world.isBikeMounted && PlayerHeight() < .7f)
    {
        for (size_t i = 0; i < world.residents.size(); ++i)
        {
            auto &resident = world.residents[i];
            if (i == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined)
            {
                continue;
            }
            if (resident.health <= 0 || Game::Distance(resident.position, world.bikePosition) > .95f)
            {
                continue;
            }
            if (world.bikeSpeed > 2 && resident.impact.invulnerability <= 0)
            {
                CivicCombat::DamageResident(i, int(world.bikeSpeed * 9),
                                            Add({}, Unit(Sub(resident.position, world.bikePosition)), 5),
                                            true);
            }
            world.bikeSpeed = 0;
        }
        for (size_t i = 0; i < civic.officers.size(); ++i)
        {
            if (civic.officers[i].health > 0 &&
                Game::Distance(civic.officers[i].position, world.bikePosition) < .95f)
            {
                if (world.bikeSpeed > 2 && civic.officers[i].impact.invulnerability <= 0)
                {
                    CivicCombat::DamageOfficer(
                        i, int(world.bikeSpeed * 9),
                        Add({}, Unit(Sub(civic.officers[i].position, world.bikePosition)), 5));
                }
                world.bikeSpeed = 0;
            }
        }
        for (size_t i = 0; i < std::size(kPeople); ++i)
        {
            if (civic.landmarkHealth[i] > 0 &&
                (i != 2 || game::GameLevels::Chapter().GetState().kainJoined) &&
                Game::Distance(kPeople[i], world.bikePosition) < .95f)
            {
                if (world.bikeSpeed > 2)
                {
                    CivicCombat::DamageLandmark(i, int(world.bikeSpeed * 9));
                }
                world.bikeSpeed = 0;
            }
        }
    }
}

bool HospitalRecovery::Active()
{
    return civic.recovery != RecoveryPhase::None;
}
void HospitalRecovery::Begin()
{
    if (Active())
    {
        return;
    }
    auto &world = Game::Get().GetState();
    auto source = game::GameLevels::Mine().IsActive()      ? Overworld::kMineEntrance
                  : game::GameLevels::Chapter().IsActive() ? kCityReceiverPosition
                                                           : world.playerPosition;
    float best = 1.e9f;
    civic.hospital = 0;
    for (size_t i = 0; i < civic.hospitals.size(); ++i)
    {
        float distance = Game::Distance(source, civic.hospitals[i].position);
        if (distance < best)
        {
            best = distance;
            civic.hospital = int(i);
        }
    }
    civic.recovery = RecoveryPhase::FadeOut;
    civic.recoverySeconds = 0;
    world.mouseFiring = false;
    world.mapVisible = false;
    world.isPaused = false;
    world.bikeSpeed = 0;
    std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
}
void HospitalRecovery::Update(float dt)
{
    auto &stats = game::GameLevels::Mine().GetState().player;
    if (!Active())
    {
        if (stats.health <= 0)
        {
            Begin();
        }
        return;
    }
    civic.recoverySeconds += dt;
    if (civic.recovery == RecoveryPhase::FadeOut && civic.recoverySeconds >= .85f)
    {
        civic.recoverySeconds -= .85f;
        civic.recovery = RecoveryPhase::Transfer;
        TransferToHospital();
    }
    if (civic.recovery == RecoveryPhase::Transfer && civic.recoverySeconds >= .35f)
    {
        civic.recoverySeconds -= .35f;
        civic.recovery = RecoveryPhase::FadeIn;
    }
    if (civic.recovery == RecoveryPhase::FadeIn && civic.recoverySeconds >= 1.1f)
    {
        civic.recovery = RecoveryPhase::None;
        civic.recoverySeconds = 0;
    }
}
float HospitalRecovery::Opacity()
{
    if (civic.recovery == RecoveryPhase::None)
    {
        return 0;
    }
    if (civic.recovery == RecoveryPhase::FadeOut)
    {
        return Game::Clamp(civic.recoverySeconds / .85f, 0, 1);
    }
    if (civic.recovery == RecoveryPhase::Transfer)
    {
        return 1;
    }
    return Game::Clamp(1 - civic.recoverySeconds / 1.1f, 0, 1);
}
void CivicPresentation::DrawRecovery()
{
    if (!HospitalRecovery::Active())
    {
        return;
    }
    Drawing::DrawRectangle(0, 0, kCanvasWidth, kCanvasHeight, {0, 0, 0, HospitalRecovery::Opacity()});
    if (HospitalRecovery::Opacity() > .75f)
    {
        Drawing::DrawLabel(500, 400, "의료 이송 / 생체 신호 안정화", {.5f, .9f, .85f});
    }
}
void CivicPresentation::DrawWorld()
{
    if (!CivicJustice::IsExterior())
    {
        return;
    }
    auto &renderer = GameRenderer::Get().Backend();
    renderer.BeginModels();
    for (const auto &officer : civic.officers)
    {
        if (officer.corpseSeconds > 20 &&
            TrafficSystem::OutsideView(officer.position, Game::Get().GetState().cameraPosition, 80))
        {
            continue;
        }
        Drawing::DrawPerson(officer.position, {.18f, .36f, .48f}, false,
                            officer.tier >= 3 ? models::CharacterType::HeavyCombat
                                              : models::CharacterType::Guard,
                            officer.facing.walking, officer.facing.yaw, models::Gender::Male,
                            models::HairStyle::Buzz, true, officer.muzzleFlash,
                            officer.health <= 0 || officer.impact.response == BodyResponse::Knockdown, 2.5f);
    }
    renderer.EndModels();
    for (const auto &hospital : civic.hospitals)
    {
        auto p = Drawing::WorldToScreen(hospital.position.x, hospital.position.y, 35);
        Drawing::DrawLabel(p.x - 8, p.y, "+", {.35f, 1, .72f}, GLUT_BITMAP_HELVETICA_18);
        if (Game::Distance(hospital.position, Game::Get().GetState().playerPosition) < 3)
        {
            Drawing::DrawLabel(p.x - 30, p.y - 18,
                               std::string(Overworld::Cities()[hospital.city].name) + " 의료소",
                               {.6f, 1, .8f});
        }
    }
    for (const auto &tracer : civic.tracers)
    {
        auto a = Drawing::WorldToScreen(tracer.start.x, tracer.start.y, models::WorldScale::aimHeightPixels);
        auto b = Drawing::WorldToScreen(tracer.end.x, tracer.end.y, models::WorldScale::aimHeightPixels);
        Drawing::DrawLine(a, b,
                          tracer.npcOwned ? ColorRGBA(.25f, .8f, 1, tracer.remaining / .12f)
                                          : ColorRGBA(1, .83f, .38f, tracer.remaining / .12f),
                          2);
    }
}
void CivicPresentation::DrawHud()
{
    auto &world = Game::Get().GetState();
    if (CivicJustice::IsExterior() && !world.mapVisible)
    {
        const auto &stats = game::GameLevels::Mine().GetState().player;
        const auto &pistol = game::GameLevels::Mine().GetState().pistol;
        std::string stars;
        for (int i = 0; i < 5; ++i)
        {
            stars += i < civic.wanted ? "★" : "☆";
        }
        Drawing::DrawRectangle(22, 300, 370, 95, {.02f, .04f, .055f, .88f});
        Drawing::DrawLabel(38, 322, "개척지 보안망  " + stars, {1, .65f, .35f});
        Drawing::DrawRectangle(38, 334, 330, 5, {.15f, .2f, .22f});
        Drawing::DrawRectangle(38, 334, 330 * civic.heat / 1000, 5, {1, .38f, .16f});
        char line[180];
        std::snprintf(line, sizeof(line), "악명 %.0f / 거래 할증 +%.0f%% · %d 크레딧", civic.infamy,
                      (CivicJustice::PriceMultiplier() - 1) * 100, stats.credits);
        Drawing::DrawLabel(38, 357, line, {.8f, .86f, .82f});
        std::snprintf(line, sizeof(line), "권총 %d/%d · R 재장전 · V 근접 공격", pistol.GetRounds(),
                      pistol.GetMagazineSize());
        Drawing::DrawLabel(38, 382, line, {.9f, .83f, .56f});
        auto cursor = world.cursorPosition;
        Drawing::DrawLine({cursor.x - 8, cursor.y}, {cursor.x + 8, cursor.y}, {.7f, 1, 1}, 1);
        Drawing::DrawLine({cursor.x, cursor.y - 8}, {cursor.x, cursor.y + 8}, {.7f, 1, 1}, 1);
    }
    DrawRecovery();
}
} // namespace tutorial
