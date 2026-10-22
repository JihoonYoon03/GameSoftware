#include "stdafx.h"
#include "ResidentAI.h"
#include "Level.h"
#include "Overworld.h"
#include "TransitLayout.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace tutorial
{
namespace
{
Vector2 Offset(Vector2 p, Vector2 d, float amount)
{
    return {p.x + d.x * amount, p.y + d.y * amount};
}
unsigned int Random(Resident &resident)
{
    auto &state = resident.randomState;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
bool Clear(const GameState &world, Vector2 p)
{
    if (p.x < kWorldMinX || p.x > kWorldMaxX || p.y < kWorldMinY || p.y > kWorldMaxY ||
        CityLayout::IsQuestSpace(p, .4f) || Overworld::Scenery().IsBlocked(p, .3f))
    {
        return false;
    }
    for (const auto &b : world.buildings)
    {
        if (p.x > b.x - .3f && p.x < b.x + b.width + .3f && p.y > b.y - .3f && p.y < b.y + b.depth + .3f)
        {
            return false;
        }
    }
    return true;
}
bool Suitable(ResidentProfession profession, models::SceneryType type)
{
    using P = ResidentProfession;
    using S = models::SceneryType;
    switch (profession)
    {
    case P::Courier:
        return type == S::CargoContainer || type == S::TransitShelter;
    case P::Worker:
        return type == S::LandingPad || type == S::CargoContainer;
    case P::Engineer:
        return type == S::Transformer || type == S::HVAC;
    case P::Technician:
        return type == S::CommRelay || type == S::ChargePost;
    case P::Student:
        return type == S::TransitShelter || type == S::Bench;
    case P::Merchant:
        return type == S::TransitShelter || type == S::CargoContainer;
    case P::Observer:
        return type == S::SurveyTripod || type == S::CommRelay;
    case P::Explorer:
        return type == S::Wayfinding || type == S::LandingPad;
    default:
        return type == S::Bench || type == S::Planter;
    }
}
} // namespace

CityResidents &CityResidents::Get()
{
    static CityResidents residents;
    return residents;
}
size_t CityResidents::NavigationNodeCount() const
{
    return nodes.size();
}
bool CityResidents::CanWalkSegment(const GameState &world, Vector2 from, Vector2 to)
{
    const int steps = (std::max)(1, int(std::ceil(Game::Distance(from, to) / .25f)));
    for (int i = 0; i <= steps; ++i)
    {
        Vector2 p = {from.x + (to.x - from.x) * i / steps, from.y + (to.y - from.y) * i / steps};
        if (!Clear(world, p) || RoadNetwork::Get().Contains(p, .32f))
        {
            return false;
        }
    }
    return true;
}
void CityResidents::Build(const GameState &world)
{
    profiling::Scope timer(profiling::Timer::NpcNavigation);
    nodes.clear();
    const auto &net = RoadNetwork::Get();
    constexpr int width = 87, height = 79;
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        std::vector<int> grid(width * height, -1);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                Vector2 p = {center.x + x - 43, center.y + y - 39};
                if (CanWalkSegment(world, p, p))
                {
                    grid[y * width + x] = int(nodes.size());
                    nodes.push_back({p, city, -1, {}});
                    nodes.back().sidewalk = net.Contains(p, 1.5f) || CityPlan::IsWalkway(p);
                }
            }
        }
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int a = grid[y * width + x];
                if (a < 0)
                {
                    continue;
                }
                for (Vector2 delta : {Vector2{1, 0}, {0, 1}, {1, 1}, {-1, 1}})
                {
                    int nx = x + int(delta.x), ny = y + int(delta.y);
                    if (nx < 0 || nx >= width || ny >= height)
                    {
                        continue;
                    }
                    int b = grid[ny * width + nx];
                    if (b >= 0 && CanWalkSegment(world, nodes[a].position, nodes[b].position))
                    {
                        nodes[a].links.push_back({b, -1});
                        nodes[b].links.push_back({a, -1});
                    }
                }
            }
        }
        // Crossing endpoints and the holograms use exactly the same arm geometry.
        for (size_t j = 0; j < net.junctions.size(); ++j)
        {
            const auto &junction = net.junctions[j];
            if (!net.IsSignalized(int(j)) || Game::Distance(center, junction.position) > 48)
            {
                continue;
            }
            for (int arm : junction.outgoing)
            {
                const auto &lane = net.lanes[arm];
                if (lane.slot != 0)
                {
                    continue;
                }
                Vector2 normal = {-lane.direction.y, lane.direction.x};
                auto across = Offset(junction.position, lane.direction, junction.radius + 1.f);
                float half = net.roads[lane.road].lanes * RoadNetwork::kLaneWidth * .5f + .95f;
                for (int direction = 0; direction < 2; ++direction)
                {
                    // Opposing pedestrians use separated tracks, avoiding a head-on crossing deadlock.
                    const float side = direction ? -1.f : 1.f;
                    auto track = Offset(across, lane.direction, side * .27f);
                    Vector2 ends[] = {Offset(track, normal, half * side),
                                      Offset(track, normal, -half * side)};
                    int anchors[] = {Nearest(world, ends[0], city, 2.5f),
                                     Nearest(world, ends[1], city, 2.5f)};
                    if (anchors[0] < 0 || anchors[1] < 0)
                    {
                        continue;
                    }
                    bool clear = true;
                    const int steps = int(std::ceil(half * 8));
                    for (int sample = 0; sample <= steps; ++sample)
                    {
                        clear &= Clear(world, Offset(ends[0], normal, -half * side * 2 * sample / steps));
                    }
                    if (!clear)
                    {
                        continue;
                    }
                    const int a = int(nodes.size()), b = a + 1;
                    nodes.push_back({ends[0], city, -1, {{anchors[0], -1}, {b, int(j)}}, true});
                    nodes.push_back({ends[1], city, -1, {{anchors[1], -1}}, true});
                    nodes[anchors[0]].links.push_back({a, -1});
                    nodes[anchors[1]].links.push_back({b, -1});
                }
            }
        }
    }
    int component = 0;
    for (size_t start = 0; start < nodes.size(); ++start)
    {
        if (nodes[start].component >= 0)
        {
            continue;
        }
        std::vector<int> open = {int(start)};
        nodes[start].component = component;
        for (size_t next = 0; next < open.size(); ++next)
        {
            for (const auto &link : nodes[open[next]].links)
            {
                if (nodes[link.to].component < 0)
                {
                    nodes[link.to].component = component;
                    open.push_back(link.to);
                }
            }
        }
        ++component;
    }
    workplaces.assign(Overworld::kCityCount * 10, {});
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        const auto center = Overworld::Cities()[city].center;
        for (const auto &prop : Overworld::Scenery().Props())
        {
            if (Game::Distance(center, prop.position) > 48)
            {
                continue;
            }
            for (int profession = 0; profession < 10; ++profession)
            {
                if (!Suitable(static_cast<ResidentProfession>(profession), prop.type))
                {
                    continue;
                }
                for (float x : {prop.minimum.x - .8f, prop.maximum.x + .8f})
                {
                    int target = Nearest(world, {x, prop.position.y}, city, 3);
                    if (target >= 0)
                    {
                        workplaces[city * 10 + profession].push_back(target);
                    }
                }
            }
        }
    }
}
int CityResidents::Nearest(const GameState &world, Vector2 position, int city, float limit) const
{
    int nearest = -1;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].city != city)
        {
            continue;
        }
        float distance = Game::Distance(position, nodes[i].position);
        if (distance < limit && CanWalkSegment(world, position, nodes[i].position))
        {
            nearest = int(i);
            limit = distance;
        }
    }
    return nearest;
}
void CityResidents::Reset(GameState &world)
{
    decisionCursor = 0;
    // Reuse the fixed city's graph across resets unless physical obstacle bounds change.
    size_t signature = 0x63816bu;
    auto mix = [&signature](float value) {
        signature ^= std::hash<float>{}(value) + size_t(0x9e3779b9u) + (signature << 6) + (signature >> 2);
    };
    for (const auto &b : world.buildings)
    {
        mix(b.x);
        mix(b.y);
        mix(b.width);
        mix(b.depth);
    }
    for (const auto &prop : Overworld::Scenery().Props())
    {
        mix(prop.minimum.x);
        mix(prop.minimum.y);
        mix(prop.maximum.x);
        mix(prop.maximum.y);
        mix(prop.solid ? 1.f : 0.f);
        mix(float(prop.type));
    }
    if (nodes.empty() || signature != layoutSignature)
    {
        Build(world);
        layoutSignature = signature;
    }
    for (size_t i = 0; i < world.residents.size(); ++i)
    {
        auto &resident = world.residents[i];
        resident.home = resident.destination = resident.position;
        resident.route.clear();
        resident.routeCursor = 0;
        resident.crossingJunction = -1;
        resident.routineStep = 0;
        resident.randomState = 0x639b27u + unsigned(i) * 7919;
        resident.speed = .5f + (Random(resident) % 26) * .01f;
        if (resident.profession == ResidentProfession::Guard)
        {
            resident.speed += .15f;
        }
        if (resident.profession == ResidentProfession::Courier)
        {
            resident.speed += .3f;
        }
        resident.waitSeconds = float(i % 19) * .13f;
        resident.activity = ResidentActivity::Resting;
        resident.conversationSeconds = resident.blockedSeconds = 0;
        resident.destinationIsWork = false;
        resident.facing.walking = false;
    }
}
bool CityResidents::FindRoute(int from, int to, Resident &resident) const
{
    profiling::Scope timer(profiling::Timer::NpcNavigation);
    profiling::Count(profiling::Counter::NpcPathRequests);
    std::vector<float> costs(nodes.size(), (std::numeric_limits<float>::max)());
    std::vector<int> previous(nodes.size(), -1), crossings(nodes.size(), -1);
    using Candidate = std::pair<float, int>;
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> open;
    costs[from] = 0;
    open.push({Game::Distance(nodes[from].position, nodes[to].position), from});
    while (!open.empty())
    {
        auto [priority, current] = open.top();
        open.pop();
        if (priority > costs[current] + Game::Distance(nodes[current].position, nodes[to].position) + .001f)
        {
            continue;
        }
        profiling::Count(profiling::Counter::NpcPathVisitedCells);
        if (current == to)
        {
            break;
        }
        for (const auto &link : nodes[current].links)
        {
            const float cost = costs[current] +
                               Game::Distance(nodes[current].position, nodes[link.to].position) *
                                   (nodes[link.to].sidewalk ? 1.f : 1.35f) +
                               (link.crossing >= 0 ? 3.f : 0.f);
            if (cost < costs[link.to])
            {
                costs[link.to] = cost;
                previous[link.to] = current;
                crossings[link.to] = link.crossing;
                open.push({cost + Game::Distance(nodes[link.to].position, nodes[to].position), link.to});
            }
        }
    }
    if (previous[to] < 0)
    {
        return false;
    }
    resident.route.clear();
    for (int step = to; step != from; step = previous[step])
    {
        resident.route.push_back({nodes[step].position, crossings[step]});
    }
    resident.route.push_back({nodes[from].position, -1});
    std::reverse(resident.route.begin(), resident.route.end());
    resident.routeCursor = 0;
    resident.destination = nodes[to].position;
    resident.crossingJunction = -1;
    ++resident.routineStep;
    return true;
}
bool CityResidents::Plan(GameState &world, Resident &resident)
{
    profiling::Count(profiling::Counter::NpcDecisions);
    int from = Nearest(world, resident.position, resident.cityIndex, 3);
    if (from < 0)
    {
        return false;
    }
    resident.destinationIsWork = false;
    int to = -1;
    const bool duty = resident.profession != ResidentProfession::Guard && resident.routineStep % 3 != 2;
    if (duty)
    {
        std::vector<int> sites;
        for (int target : workplaces[resident.cityIndex * 10 + int(resident.profession)])
        {
            if (nodes[target].component == nodes[from].component &&
                Game::Distance(resident.position, nodes[target].position) > 4)
            {
                sites.push_back(target);
            }
        }
        if (!sites.empty())
        {
            to = sites[Random(resident) % sites.size()];
            resident.destinationIsWork = true;
        }
    }
    else if (resident.profession != ResidentProfession::Guard)
    {
        to = Nearest(world, resident.home, resident.cityIndex, 4);
    }
    if (to < 0 || nodes[to].component != nodes[from].component ||
        Game::Distance(nodes[from].position, nodes[to].position) < 4)
    {
        // A bounded reachable fallback, not a teleport or an unwalkable random world coordinate.
        resident.destinationIsWork = false;
        std::vector<int> candidates;
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].city == resident.cityIndex && nodes[i].sidewalk &&
                nodes[i].component == nodes[from].component &&
                Game::Distance(nodes[i].position, resident.position) > 7)
            {
                candidates.push_back(int(i));
            }
        }
        if (candidates.empty())
        {
            return false;
        }
        to = candidates[Random(resident) % candidates.size()];
    }
    return FindRoute(from, to, resident);
}
void CityResidents::Update(GameState &world, float dt)
{
    profiling::Scope timer(profiling::Timer::NpcUpdate);
    dt = Game::Clamp(dt, 0, .05f);
    if (dt <= 0 || world.isPaused)
    {
        return;
    }
    // Bound replanning bursts when a city loads; every resident still advances movement each frame.
    int budget = 2;
    for (size_t step = 0; step < world.residents.size(); ++step)
    {
        size_t index = (decisionCursor + step) % world.residents.size();
        auto &resident = world.residents[index];
        resident.facing.walking = false;
        if (index == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined)
        {
            resident.activity = ResidentActivity::Resting;
            resident.crossingJunction = -1;
            resident.route.clear();
            resident.routeCursor = 0;
            continue;
        }
        const auto before = resident.position;
        if (resident.conversationSeconds > 0 && resident.activity != ResidentActivity::Crossing)
        {
            resident.conversationSeconds = (std::max)(0.f, resident.conversationSeconds - dt);
            resident.activity = ResidentActivity::Talking;
            continue;
        }
        resident.waitSeconds = (std::max)(0.f, resident.waitSeconds - dt);
        if (resident.activity == ResidentActivity::Talking)
        {
            resident.activity = ResidentActivity::Resting;
        }
        if (resident.routeCursor >= resident.route.size())
        {
            if (resident.waitSeconds > 0 || budget <= 0)
            {
                continue;
            }
            --budget;
            if (!Plan(world, resident))
            {
                resident.activity = ResidentActivity::NoRoute;
                resident.waitSeconds = 4;
            }
            continue;
        }
        auto waypoint = resident.route[resident.routeCursor];
        if (waypoint.crossing >= 0 && resident.crossingJunction < 0)
        {
            const float travel = Game::Distance(resident.position, waypoint.position) / resident.speed;
            if (!Overworld::Traffic().CanStartCrossing(
                    waypoint.crossing, travel,
                    Overworld::Traffic().CrossingRoad(waypoint.crossing, resident.position)))
            {
                resident.activity = ResidentActivity::WaitingSignal;
                continue;
            }
            resident.crossingJunction = waypoint.crossing;
        }
        resident.activity = resident.crossingJunction >= 0                     ? ResidentActivity::Crossing
                            : resident.profession == ResidentProfession::Guard ? ResidentActivity::Patrolling
                                                                               : ResidentActivity::Walking;
        const float distance = Game::Distance(before, waypoint.position);
        const float amount = (std::min)(distance, resident.speed * dt);
        Vector2 next = distance > .0001f
                           ? Vector2{before.x + (waypoint.position.x - before.x) * amount / distance,
                                     before.y + (waypoint.position.y - before.y) * amount / distance}
                           : waypoint.position;
        // Graph edges (including the initial connector) were validated against immutable obstacles.
        // Don't rescan every building/prop/road for every moving resident every frame.
        bool blocked = false;
        if (!world.isBikeMounted && Game::Distance(next, world.playerPosition) < .65f)
        {
            blocked = true;
        }
        for (size_t other = 0; other < world.residents.size() && !blocked; ++other)
        {
            if (other == index ||
                (other == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined))
            {
                continue;
            }
            if (Game::Distance(next, world.residents[other].position) < .38f &&
                Game::Distance(next, world.residents[other].position) <
                    Game::Distance(before, world.residents[other].position))
            {
                blocked = true;
            }
        }
        if (blocked)
        {
            resident.blockedSeconds += dt;
            if (resident.crossingJunction < 0)
            {
                resident.activity = ResidentActivity::Yielding;
                if (resident.blockedSeconds > 2)
                {
                    resident.route.clear();
                    resident.routeCursor = 0;
                    resident.waitSeconds = .4f + (Random(resident) % 10) * .1f;
                    resident.blockedSeconds = 0;
                }
            }
            continue;
        }
        resident.blockedSeconds = 0;
        resident.position = next;
        resident.facing.Update(before, next);
        if (distance <= amount + .001f)
        {
            resident.crossingJunction = -1;
            resident.activity = resident.profession == ResidentProfession::Guard
                                    ? ResidentActivity::Patrolling
                                    : ResidentActivity::Walking;
            ++resident.routeCursor;
            if (resident.routeCursor == resident.route.size())
            {
                bool resting = !resident.destinationIsWork || resident.routineStep % 3 == 0 ||
                               resident.profession == ResidentProfession::Citizen ||
                               resident.profession == ResidentProfession::Student;
                resident.activity = resting ? ResidentActivity::Resting : ResidentActivity::Working;
                resident.waitSeconds = resident.profession == ResidentProfession::Guard ? 1.5f
                                       : resident.profession == ResidentProfession::Courier
                                           ? 2.5f
                                           : 5.f + Random(resident) % 8;
            }
        }
    }
    if (!world.residents.empty())
    {
        decisionCursor = (decisionCursor + 1) % world.residents.size();
    }
}
const char *CityResidents::ProfessionName(ResidentProfession profession)
{
    constexpr const char *names[] = {"배송 기사", "교대 근무자", "정비공", "경비원",    "학생",
                                     "상인",      "주민",        "관측원", "통신 기사", "탐사원"};
    return names[size_t(profession)];
}
const char *CityResidents::ActivityName(ResidentActivity activity)
{
    constexpr const char *names[] = {"resting", "walking", "patrolling", "waiting_signal", "crossing",
                                     "working", "talking", "yielding",   "no_route"};
    return names[size_t(activity)];
}
const char *CityResidents::ActivityLabel(const Resident &resident)
{
    if (resident.activity == ResidentActivity::Working)
    {
        switch (resident.profession)
        {
        case ResidentProfession::Engineer:
            return "전력·공조 설비 점검";
        case ResidentProfession::Technician:
            return "통신·충전 설비 점검";
        case ResidentProfession::Courier:
            return "물류 전달 확인";
        case ResidentProfession::Worker:
            return "교대 작업장 확인";
        case ResidentProfession::Merchant:
            return "물류·거래 장소 확인";
        case ResidentProfession::Observer:
            return "관측 장치 기록";
        case ResidentProfession::Explorer:
            return "탐사·출발 준비";
        default:
            return "직업 활동";
        }
    }
    constexpr const char *labels[] = {
        "휴식",      "보행로 이동",     "구역 순찰",   "횡단 신호 대기",  "횡단보도 이동",
        "직업 활동", "플레이어와 대화", "보행자 양보", "경로 재탐색 대기"};
    return labels[size_t(resident.activity)];
}
} // namespace tutorial
