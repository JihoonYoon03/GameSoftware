#include "stdafx.h"
#include "LevelOneNpc.h"
#include "LevelOne.h"
#include "Profiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <string>

namespace levelone
{
namespace
{
// Scenario-inspired test roles, not story progression or companion-join flags.
const NpcProfile kProfiles[] = {
    {"리아", "실증적인 정비사", NpcPattern::Engineer,
     "장비가 거짓말할 수는 있어. 그래도 어디서부터 틀렸는지는 찾아봐야지.",
     "먼저 안전한 곳에서 장비를 보자."},
    {"카인", "신중한 경비원", NpcPattern::Guard,
     "대답이 온다고 그쪽으로 가면 안 됩니다. 먼저 여기 있는 사람부터 옮기죠.",
     "무전보다 눈앞의 사람부터 지킵시다."},
    {"마라", "낙관적인 전력 기사", NpcPattern::Engineer,
     "비상등 하나라도 더 켜 두면 돌아오는 길이 보일 거야.", "전원을 끊고 집결지로 가자."},
    {"윤서", "침착한 의무관", NpcPattern::Medic, "다친 곳부터 보여 주세요. 기록은 치료 뒤에 해도 됩니다.",
     "부상자는 집결지로. 뛰지 마세요."},
    {"도윤", "꼼꼼한 운전자", NpcPattern::Courier,
     "사람은 다섯인데 응답은 여섯이었어. 명단부터 다시 확인할게.", "화물보다 사람이 먼저야."},
    {"세라", "호기심 많은 측량사", NpcPattern::Researcher,
     "어제의 좌표와 달라요. 결론 대신 측정값 두 개를 남겨 둘래요.", "이쪽 측정은 중단할게요."},
    {"준", "겁이 많은 주민", NpcPattern::Civilian, "내 목소리를 들었다고 해도 문을 열지는 마세요.",
     "누가 불렀어요. 난 돌아갈래요."},
    {"하린", "실용적인 상인", NpcPattern::Trader,
     "회수 부품 셋이면 회복 키트 하나. 지금 필요한 것부터 챙겨요.", "거래는 돌아와서 해요."},
    {"이안", "원칙적인 순찰대원", NpcPattern::Guard, "표식 안쪽은 집결지입니다. 통로를 비워 주세요.",
     "안전선을 확인하고 후퇴합니다."},
    {"나디아", "양심적인 관측국 기술자", NpcPattern::Engineer,
     "중단 요청 사본을 남겼어요. 모두가 확대 가동에 동의한 건 아니에요.", "가동 시험보다 사람이 중요해요."},
    {"오웬", "회의적인 기록관", NpcPattern::Researcher, "같은 목소리라고 같은 발신자라는 증거는 아니지요.",
     "장비를 두고 먼저 나갑시다."},
    {"민", "성실한 배달원", NpcPattern::Courier, "보급소와 집결지를 왕복해요. 전달 확인은 꼭 직접 받고요.",
     "우회로로 보급을 옮길게요."},
    {"유리", "다정한 간호사", NpcPattern::Medic, "괜찮다고만 하지 말아요. 잠시 쉬어도 길은 기다려 줘요.",
     "같이 천천히 돌아가요."},
    {"태오", "과묵한 광부", NpcPattern::Engineer, "멈춘 계기가 또 움직여. 손대기 전에 기록부터 해.",
     "오늘 작업은 여기까지다."},
    {"로아", "대담한 탐사자", NpcPattern::Researcher,
     "빛나는 식생은 아름답지. 그래도 혼자 표식을 넘지는 않아.", "용기와 무모함은 다르니까."},
    {"에단", "책임감 강한 호송원", NpcPattern::Guard, "마지막 사람이 돌아올 때까지 귀환로를 확인합니다.",
     "서로 보이는 거리를 유지하세요."},
    {"소미", "수다스러운 주민", NpcPattern::Civilian, "같은 물건을 어제도 샀대요. 난 오늘 처음 왔는데요.",
     "이 이야기는 안전한 데서 할게요."},
    {"베크", "신중한 회수업자", NpcPattern::Trader, "장부는 남겨 둬. 물건보다 출처가 중요한 날도 있어.",
     "살아 돌아와야 거래도 하지."},
    {"레나", "협동적인 운송 기사", NpcPattern::Courier,
     "차가 멈춰도 운반은 할 수 있어요. 나눠 들면 되니까요.", "짐을 내려놓고 서로 손을 잡아요."},
    {"시온", "불안한 통신원", NpcPattern::Researcher,
     "내가 보내기 전에 답이 왔어요. 우연인지 아직 모르겠어요.", "반복 응답을 따라가지 마세요."}};
std::vector<QuarantineNpc> npcs;
std::vector<Workstation> workstations;
constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr float kGuardRange = 6.f;
constexpr float kGuardInterval = .7f;
constexpr int kGuardDamage = 12;
constexpr float kRepairRate = 3.f;
constexpr float kWorkRadius = 1.4f;
constexpr float kRecoverySeconds = 8.f;

bool IsDefender(const QuarantineNpc &npc)
{
    return kProfiles[npc.profileIndex].pattern == NpcPattern::Guard || npc.profileIndex == 14 ||
           npc.profileIndex == 19;
}

tutorial::Vector2 CellCenter(int cell)
{
    return {cell % kMapSize + .5f, cell / kMapSize + .5f};
}
int CellAt(tutorial::Vector2 position)
{
    return int(std::floor(position.y)) * kMapSize + int(std::floor(position.x));
}
tutorial::Vector2 NearestFloor(tutorial::Vector2 desired)
{
    if (IsWalkable(int(std::floor(desired.x)), int(std::floor(desired.y))))
    {
        return {std::floor(desired.x) + .5f, std::floor(desired.y) + .5f};
    }
    float best = (std::numeric_limits<float>::max)();
    auto result = g_level.entry;
    for (int cell : g_level.reachableCells)
    {
        auto position = CellCenter(cell);
        float distance = tutorial::Distance(position, desired);
        if (distance < best)
        {
            best = distance;
            result = position;
        }
    }
    return result;
}
void PlanRoute(QuarantineNpc &npc, tutorial::Vector2 target)
{
    profiling::Scope timer(profiling::Timer::NpcNavigation);
    profiling::Count(profiling::Counter::NpcPathRequests);
    npc.route.clear();
    npc.routeIndex = 0;
    npc.destination = NearestFloor(target);
    int start = CellAt(npc.position), goal = CellAt(npc.destination);
    if (start < 0 || start >= kMapSize * kMapSize || goal < 0 || goal >= kMapSize * kMapSize)
    {
        return;
    }
    std::array<int, kMapSize * kMapSize> parents;
    parents.fill(-1);
    std::queue<int> frontier;
    parents[start] = start;
    frontier.push(start);
    while (!frontier.empty() && parents[goal] < 0)
    {
        int cell = frontier.front();
        frontier.pop();
        profiling::Count(profiling::Counter::NpcPathVisitedCells);
        for (const auto &direction : kDirections)
        {
            int x = cell % kMapSize + direction[0], y = cell / kMapSize + direction[1];
            if (!IsWalkable(x, y))
            {
                continue;
            }
            int next = y * kMapSize + x;
            if (parents[next] < 0)
            {
                parents[next] = cell;
                frontier.push(next);
            }
        }
    }
    if (parents[goal] < 0)
    {
        return;
    }
    for (int cell = goal; cell != start; cell = parents[cell])
    {
        npc.route.push_back(CellCenter(cell));
    }
    // Recentering first prevents diagonal corner cutting when replanning mid-step.
    npc.route.push_back(CellCenter(start));
    std::reverse(npc.route.begin(), npc.route.end());
}
int NearestThreat(tutorial::Vector2 position, float radius, bool requireSight)
{
    int result = -1;
    for (size_t i = 0; i < g_level.enemies.size(); ++i)
    {
        const auto &enemy = g_level.enemies[i];
        float distance = tutorial::Distance(enemy.position, position);
        if (enemy.health > 0 && distance < radius &&
            (!requireSight || HasLineOfSight(enemy.position, position)))
        {
            radius = distance;
            result = int(i);
        }
    }
    return result;
}
bool IsThreatened(const QuarantineNpc &npc)
{
    return NearestThreat(npc.position, 2.6f, true) >= 0;
}
void GoTo(QuarantineNpc &npc, tutorial::Vector2 destination, NpcActivity activity)
{
    npc.activity = activity;
    auto target = NearestFloor(destination);
    // Keep the existing path until its target changes, rather than rebuilding it every decision.
    if (tutorial::Distance(npc.destination, target) > .5f || npc.routeIndex >= npc.route.size())
    {
        PlanRoute(npc, target);
    }
}
void BuildStationNavigation(Workstation &station)
{
    profiling::Scope timer(profiling::Timer::NpcNavigation);
    station.navigationDistances.assign(kMapSize * kMapSize, -1);
    std::queue<int> frontier;
    int start = CellAt(station.position);
    station.navigationDistances[start] = 0;
    frontier.push(start);
    while (!frontier.empty())
    {
        int cell = frontier.front();
        frontier.pop();
        profiling::Count(profiling::Counter::NpcPathVisitedCells);
        for (const auto &direction : kDirections)
        {
            int x = cell % kMapSize + direction[0], y = cell / kMapSize + direction[1];
            if (!IsWalkable(x, y))
            {
                continue;
            }
            int next = y * kMapSize + x;
            if (station.navigationDistances[next] < 0)
            {
                station.navigationDistances[next] = station.navigationDistances[cell] + 1;
                frontier.push(next);
            }
        }
    }
}
void Decide(QuarantineNpc &npc)
{
    profiling::Count(profiling::Counter::NpcDecisions);
    const auto &profile = kProfiles[npc.profileIndex];
    npc.targetEnemy = -1;
    if (npc.health < 30 && !IsDefender(npc))
    {
        GoTo(npc, npc.home, NpcActivity::Retreating);
        return;
    }
    if (npc.following)
    {
        if (tutorial::Distance(npc.position, tutorial::g_state.playerPosition) > 2)
        {
            GoTo(npc, tutorial::g_state.playerPosition, NpcActivity::Following);
        }
        else
        {
            npc.route.clear();
            npc.activity = NpcActivity::Following;
        }
        return;
    }
    if (npc.stationId >= 0)
    {
        auto &station = workstations[npc.stationId];
        if (IsDefender(npc))
        {
            npc.targetEnemy = NearestThreat(npc.position, kGuardRange, true);
            if (npc.targetEnemy >= 0)
            {
                npc.activity = NpcActivity::Defending;
                npc.route.clear();
                return;
            }
            int threat = NearestThreat(station.position, 8.f, false);
            if (threat >= 0)
            {
                GoTo(npc, g_level.enemies[threat].position, NpcActivity::Defending);
                return;
            }
            if (npc.routeIndex >= npc.route.size())
            {
                ++npc.routineStep;
                const auto &direction = kDirections[(npc.routineStep + npc.profileIndex) % 4];
                GoTo(npc, {station.position.x + direction[0] * 2, station.position.y + direction[1] * 2},
                     NpcActivity::Defending);
            }
            return;
        }
        if (profile.pattern == NpcPattern::Engineer)
        {
            if (IsThreatened(npc))
            {
                GoTo(npc, {station.position.x - .5f, station.position.y + 2}, NpcActivity::Retreating);
            }
            else
            {
                GoTo(npc, {station.position.x - 1, station.position.y}, NpcActivity::Repairing);
            }
            return;
        }
        if (profile.pattern == NpcPattern::Courier)
        {
            GoTo(npc, npc.carryingSupplies ? station.position : npc.home, NpcActivity::Supplying);
            return;
        }
        if (profile.pattern == NpcPattern::Medic)
        {
            QuarantineNpc *patient = nullptr;
            float best = 999.f;
            for (auto &other : npcs)
            {
                if (other.health <= 0 || other.health >= other.maxHealth)
                {
                    continue;
                }
                float distance = tutorial::Distance(other.position, npc.position);
                if (distance < best)
                {
                    best = distance;
                    patient = &other;
                }
            }
            GoTo(npc, patient ? patient->position : station.position, NpcActivity::Healing);
            return;
        }
        if (IsThreatened(npc))
        {
            GoTo(npc, npc.home, NpcActivity::Retreating);
        }
        else if (npc.routeIndex >= npc.route.size())
        {
            ++npc.routineStep;
            const auto &direction = kDirections[(npc.routineStep + npc.profileIndex) % 4];
            GoTo(npc, {station.position.x + direction[0], station.position.y + direction[1]},
                 NpcActivity::Investigating);
        }
        return;
    }
    if (npc.routeIndex < npc.route.size())
    {
        return;
    }
    // Residents and traders remain in the safe muster area, with independent idle routines.
    ++npc.routineStep;
    if (npc.routineStep % 2)
    {
        npc.activity = NpcActivity::Working;
        npc.decisionCooldown = 2.f + float(npc.profileIndex % 4);
        return;
    }
    float radius = profile.pattern == NpcPattern::Trader ? 0.f : 1.f;
    const auto &direction = kDirections[(npc.routineStep / 2 + npc.profileIndex) % 4];
    auto target = npc.home;
    target.x += direction[0] * radius;
    target.y += direction[1] * radius;
    npc.activity = radius > 0 ? NpcActivity::Walking : NpcActivity::Working;
    PlanRoute(npc, target);
}
void UpdateActions(QuarantineNpc &npc, float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::NpcCooperation);
    const auto &profile = kProfiles[npc.profileIndex];
    if (IsDefender(npc) && npc.combatCooldown <= 0)
    {
        int target = NearestThreat(npc.position, kGuardRange, true);
        npc.targetEnemy = target;
        if (target >= 0)
        {
            profiling::Count(profiling::Counter::NpcShots);
            npc.combatCooldown = kGuardInterval;
            npc.actionFlash = .16f;
            npc.actionTarget = g_level.enemies[target].position;
            if (!npc.following)
            {
                npc.activity = NpcActivity::Defending;
            }
            if (DamageEnemy(size_t(target), kGuardDamage, false))
            {
                ++npc.kills;
                profiling::Count(profiling::Counter::NpcEnemyKills);
            }
        }
    }
    if (npc.following || npc.stationId < 0)
    {
        return;
    }
    auto &station = workstations[npc.stationId];
    float distance = tutorial::Distance(npc.position, station.position);
    if (profile.pattern == NpcPattern::Courier)
    {
        if (!npc.carryingSupplies && tutorial::Distance(npc.position, npc.home) < .3f)
        {
            // The southern transport specialist shares deliveries with the western repair team.
            if (npc.profileIndex == 18)
            {
                npc.stationId = npc.stationId == 2 ? 3 : 2;
                npc.teamId = npc.stationId;
            }
            npc.carryingSupplies = true;
            npc.decisionCooldown = 0;
        }
        else if (npc.carryingSupplies && distance < .8f)
        {
            profiling::Count(profiling::Counter::WorkstationSupplyDeliveries);
            station.supplies = (std::min)(4, station.supplies + 2);
            npc.carryingSupplies = false;
            npc.actionFlash = .25f;
            npc.actionTarget = station.position;
            npc.decisionCooldown = 0;
        }
    }
    if (profile.pattern == NpcPattern::Engineer && npc.activity == NpcActivity::Repairing &&
        distance <= kWorkRadius && !IsThreatened(npc) && station.repairProgress < 100 && station.supplies > 0)
    {
        float rate = kRepairRate;
        for (const auto &other : npcs)
        {
            if (other.teamId == npc.teamId && other.activity == NpcActivity::Investigating &&
                tutorial::Distance(other.position, station.position) < 2)
            {
                rate += 1.5f;
            }
        }
        float previous = station.repairProgress;
        station.repairProgress = (std::min)(100.f, previous + rate * deltaSeconds);
        if (int(previous / 25) < int(station.repairProgress / 25))
        {
            --station.supplies;
        }
        npc.actionTarget = station.position;
        npc.actionFlash = .1f;
        if (station.repairProgress >= 100)
        {
            profiling::Count(profiling::Counter::WorkstationRepairCompletions);
            station.integrity = 100;
            station.cycleCooldown = 20;
            ++station.completedCycles;
            // Equipment support is separate from the player's combat experience and story quest flags.
            g_level.player.recoveryKits = (std::min)(999, g_level.player.recoveryKits + 1);
            if (tutorial::Distance(tutorial::g_state.playerPosition, station.position) < 9)
            {
                tutorial::ShowNotice(std::string(station.name) + " / 공동 수리 완료. 회복 키트 1 지원.");
            }
        }
    }
    if (profile.pattern == NpcPattern::Medic && npc.combatCooldown <= 0)
    {
        for (auto &other : npcs)
        {
            if (other.health > 0 && other.health < other.maxHealth &&
                tutorial::Distance(other.position, npc.position) < 2 &&
                HasLineOfSight(other.position, npc.position))
            {
                other.health = (std::min)(other.maxHealth, other.health + 16);
                npc.combatCooldown = 1.2f;
                npc.actionFlash = .3f;
                npc.actionTarget = other.position;
                break;
            }
        }
    }
}
} // namespace

const std::vector<QuarantineNpc> &GetNpcs()
{
    return npcs;
}
const std::vector<Workstation> &GetWorkstations()
{
    return workstations;
}
const NpcProfile &GetNpcProfile(size_t index)
{
    return kProfiles[npcs.at(index).profileIndex];
}
const char *GetNpcActivityName(NpcActivity activity)
{
    switch (activity)
    {
    case NpcActivity::Working:
        return "작업·대기";
    case NpcActivity::Walking:
        return "순찰·이동";
    case NpcActivity::Following:
        return "동행";
    case NpcActivity::Retreating:
        return "위험 회피";
    case NpcActivity::Talking:
        return "대화";
    case NpcActivity::Repairing:
        return "설비 수리";
    case NpcActivity::Defending:
        return "팀 엄호·교전";
    case NpcActivity::Supplying:
        return "부품 운반";
    case NpcActivity::Healing:
        return "팀 치료";
    case NpcActivity::Investigating:
        return "설비 측정·지원";
    case NpcActivity::Recovering:
        return "집결지 회복";
    }
    return "대기";
}
const char *GetWorkstationStatusName(WorkstationStatus status)
{
    switch (status)
    {
    case WorkstationStatus::AwaitingSupplies:
        return "보급 대기";
    case WorkstationStatus::Repairing:
        return "공동 수리";
    case WorkstationStatus::Threatened:
        return "접근 위협·엄호";
    case WorkstationStatus::Operational:
        return "정상 작동";
    }
    return "대기";
}
int FindNpcThreatTarget(tutorial::Vector2 position, int teamId)
{
    int result = -1;
    float best = 4.f;
    for (size_t i = 0; i < npcs.size(); ++i)
    {
        const auto &npc = npcs[i];
        if (npc.health <= 0 || npc.teamId != teamId ||
            tutorial::Distance(npc.position, g_level.entry) < kSafeAreaRadius)
        {
            continue;
        }
        float distance = tutorial::Distance(position, npc.position);
        if (distance < best && HasLineOfSight(position, npc.position))
        {
            best = distance;
            result = int(i);
        }
    }
    return result;
}
void DamageNpc(size_t index, int damage)
{
    if (index >= npcs.size() || damage <= 0)
    {
        return;
    }
    auto &npc = npcs[index];
    if (npc.health <= 0)
    {
        return;
    }
    npc.health = (std::max)(0, npc.health - damage);
    if (npc.health == 0)
    {
        profiling::Count(profiling::Counter::NpcRescues);
        // The test level rescues injured workers; it cannot trigger permanent companion loss.
        npc.position = npc.destination = npc.home;
        npc.route.clear();
        npc.activity = NpcActivity::Recovering;
        npc.decisionCooldown = kRecoverySeconds;
        npc.targetEnemy = -1;
        npc.actionFlash = 0;
        npc.talkSeconds = 0;
    }
    else
    {
        npc.decisionCooldown = 0;
    }
}
void DamageWorkstation(size_t index, float damage)
{
    if (index >= workstations.size() || damage <= 0)
    {
        return;
    }
    auto &station = workstations[index];
    station.integrity = (std::max)(0.f, station.integrity - damage);
    // Damage to completed equipment starts a new repair cycle, preventing repeated finish rewards.
    station.repairProgress =
        station.repairProgress >= 100 ? 0.f : (std::max)(0.f, station.repairProgress - damage * .5f);
    station.cycleCooldown = 0;
    station.status = WorkstationStatus::Threatened;
}
void ClearNpcs()
{
    npcs.clear();
    workstations.clear();
}
void InitializeNpcs()
{
    ClearNpcs();
    const tutorial::Vector2 offsets[] = {{0, -12}, {12, 0}, {0, 12}, {-12, 0}};
    const char *names[] = {"북측 통신 중계기", "동측 비상 전원", "남측 수로 펌프", "서측 회수 설비"};
    const WorkstationType types[] = {WorkstationType::CommunicationRelay, WorkstationType::EmergencyPower,
                                     WorkstationType::WaterPump, WorkstationType::SalvageCrane};
    for (int i = 0; i < 4; ++i)
    {
        Workstation station;
        station.id = station.teamId = i;
        station.name = names[i];
        station.type = types[i];
        station.position = NearestFloor({g_level.entry.x + offsets[i].x, g_level.entry.y + offsets[i].y});
        BuildStationNavigation(station);
        workstations.push_back(std::move(station));
    }
    constexpr int assignments[] = {0, 0, 1, 0, 0, 0, -1, -1, 1, 2, 1, 1, 2, 3, 3, 2, -1, -1, 2, 3};
    for (size_t i = 0; i < sizeof(kProfiles) / sizeof(*kProfiles); ++i)
    {
        QuarantineNpc npc;
        npc.profileIndex = i;
        // Twenty separate stations around the central return portal.
        npc.home = {g_level.entry.x - 4 + float(i % 5) * 2, g_level.entry.y - 4 + float(i / 5) * 2};
        if (tutorial::Distance(npc.home, g_level.entry) < 1)
        {
            npc.home.y += 5;
        }
        npc.position = npc.destination = npc.home;
        npc.teamId = npc.stationId = assignments[i];
        // Work teams start at their assigned equipment, while couriers start with depot deliveries.
        if (npc.stationId >= 0 && kProfiles[i].pattern != NpcPattern::Courier)
        {
            const auto &station = workstations[npc.stationId];
            float offsetY = kProfiles[i].pattern == NpcPattern::Medic        ? -1.f
                            : kProfiles[i].pattern == NpcPattern::Researcher ? 1.f
                            : IsDefender(npc)                                ? float(i % 3) - 1
                                                                             : 0.f;
            npc.position = npc.destination =
                NearestFloor({station.position.x + (IsDefender(npc) ? 1 : -1), station.position.y + offsetY});
        }
        npc.decisionCooldown = float(i) * .13f;
        npcs.push_back(npc);
    }
}
void UpdateNpcs(float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::NpcUpdate);
    for (auto &station : workstations)
    {
        if (station.repairProgress >= 100)
        {
            station.cycleCooldown -= deltaSeconds;
            if (station.cycleCooldown <= 0)
            {
                station.repairProgress = 0;
                station.integrity = 80;
            }
        }
        station.status = NearestThreat(station.position, 4.f, true) >= 0 ? WorkstationStatus::Threatened
                         : station.repairProgress >= 100                 ? WorkstationStatus::Operational
                         : station.supplies > 0 ? WorkstationStatus::Repairing
                                                : WorkstationStatus::AwaitingSupplies;
    }
    for (auto &npc : npcs)
    {
        npc.serviceCooldown = (std::max)(0.f, npc.serviceCooldown - deltaSeconds);
        npc.combatCooldown = (std::max)(0.f, npc.combatCooldown - deltaSeconds);
        npc.actionFlash = (std::max)(0.f, npc.actionFlash - deltaSeconds);
        npc.talkSeconds = (std::max)(0.f, npc.talkSeconds - deltaSeconds);
        if (npc.targetEnemy >= 0 && g_level.enemies[npc.targetEnemy].health <= 0)
        {
            npc.targetEnemy = -1;
        }
        if (npc.health <= 0)
        {
            npc.decisionCooldown -= deltaSeconds;
            if (npc.decisionCooldown <= 0)
            {
                npc.health = npc.maxHealth;
                npc.activity = NpcActivity::Working;
            }
            continue;
        }
        if (tutorial::Distance(npc.position, npc.home) < 1 && npc.health < npc.maxHealth &&
            npc.serviceCooldown <= 0)
        {
            npc.health = (std::min)(npc.maxHealth, npc.health + 10);
            npc.serviceCooldown = 1;
        }
        if (npc.talkSeconds > 0)
        {
            continue;
        }
        npc.decisionCooldown -= deltaSeconds;
        if (npc.decisionCooldown <= 0)
        {
            npc.decisionCooldown = .65f + float(npc.profileIndex % 5) * .07f;
            Decide(npc);
        }
        UpdateActions(npc, deltaSeconds);
        if (npc.routeIndex >= npc.route.size())
        {
            continue;
        }
        auto target = npc.route[npc.routeIndex];
        float distance = tutorial::Distance(npc.position, target);
        if (distance < .03f)
        {
            npc.position = target;
            ++npc.routeIndex;
            continue;
        }
        float speed = npc.activity == NpcActivity::Retreating  ? 2.8f
                      : npc.activity == NpcActivity::Supplying ? 2.6f
                                                               : 1.7f;
        float step = (std::min)(distance, speed * deltaSeconds);
        tutorial::Vector2 next = {npc.position.x + (target.x - npc.position.x) * step / distance,
                                  npc.position.y + (target.y - npc.position.y) * step / distance};
        if (!IsBlocked(next))
        {
            npc.position = next;
            npc.walkPhase += step * 5;
        }
        else
        {
            npc.route.clear();
            npc.decisionCooldown = 0;
        }
    }
}
bool InteractWithNpc()
{
    QuarantineNpc *nearest = nullptr;
    float best = 1.7f;
    for (auto &npc : npcs)
    {
        if (npc.health <= 0)
        {
            continue;
        }
        float distance = tutorial::Distance(npc.position, tutorial::g_state.playerPosition);
        if (distance < best && HasLineOfSight(npc.position, tutorial::g_state.playerPosition))
        {
            best = distance;
            nearest = &npc;
        }
    }
    if (!nearest)
    {
        return false;
    }
    auto &npc = *nearest;
    const auto &profile = kProfiles[npc.profileIndex];
    std::string message = std::string(profile.name) + " / ";
    if (IsThreatened(npc))
    {
        message += profile.warning;
        npc.decisionCooldown = 0;
    }
    else
    {
        message += profile.dialogue;
        if (npc.profileIndex < 2)
        {
            npc.following = !npc.following;
            message += npc.following ? " [시험 동행 시작]" : " [동행 종료]";
        }
        else if (profile.pattern == NpcPattern::Medic)
        {
            if (npc.serviceCooldown <= 0 && g_level.player.health < g_level.player.maxHealth)
            {
                g_level.player.health = (std::min)(g_level.player.maxHealth, g_level.player.health + 35);
                npc.serviceCooldown = 20;
                message += " [체력 35 회복]";
            }
            else
            {
                message += " [치료는 부상 시, 20초 간격]";
            }
        }
        else if (profile.pattern == NpcPattern::Trader)
        {
            if (g_level.player.salvage >= 3 && g_level.player.recoveryKits < 999)
            {
                g_level.player.salvage -= 3;
                ++g_level.player.recoveryKits;
                message += " [부품 3 → 회복 키트 1]";
            }
            else
            {
                message += " [부품 3 필요 / 키트 최대 999]";
            }
        }
        npc.activity = NpcActivity::Talking;
        npc.talkSeconds = 2;
        npc.decisionCooldown = 0;
        npc.route.clear();
    }
    tutorial::ShowNotice(message);
    return true;
}
} // namespace levelone
