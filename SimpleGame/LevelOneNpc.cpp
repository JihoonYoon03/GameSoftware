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
constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

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
bool IsThreatened(const QuarantineNpc &npc)
{
    for (const auto &enemy : g_level.enemies)
    {
        if (enemy.health > 0 && tutorial::Distance(enemy.position, npc.position) < 4 &&
            HasLineOfSight(enemy.position, npc.position))
        {
            return true;
        }
    }
    return false;
}
void Decide(QuarantineNpc &npc)
{
    profiling::Count(profiling::Counter::NpcDecisions);
    const auto &profile = kProfiles[npc.profileIndex];
    if (IsThreatened(npc))
    {
        npc.activity = NpcActivity::Retreating;
        PlanRoute(npc, npc.home);
        return;
    }
    if (npc.following)
    {
        npc.activity = NpcActivity::Following;
        if (tutorial::Distance(npc.position, tutorial::g_state.playerPosition) > 2)
        {
            PlanRoute(npc, tutorial::g_state.playerPosition);
        }
        else
        {
            npc.route.clear();
        }
        return;
    }
    if (npc.routeIndex < npc.route.size())
    {
        return;
    }
    // Alternate work pauses and movement; individuals have independent phase offsets.
    ++npc.routineStep;
    if (npc.routineStep % 2)
    {
        npc.activity = NpcActivity::Working;
        npc.decisionCooldown = 2.f + float(npc.profileIndex % 4);
        return;
    }
    float radius = 2;
    switch (profile.pattern)
    {
    case NpcPattern::Guard:
        radius = 5;
        break;
    case NpcPattern::Courier:
        radius = 10;
        break;
    case NpcPattern::Researcher:
        radius = 7;
        break;
    case NpcPattern::Engineer:
        radius = 3;
        break;
    case NpcPattern::Medic:
        radius = 1;
        break;
    case NpcPattern::Civilian:
        radius = 2;
        break;
    case NpcPattern::Trader:
        radius = 0;
        break;
    }
    const auto &direction = kDirections[(npc.routineStep / 2 + npc.profileIndex) % 4];
    auto target = npc.home;
    // Couriers return to their station after each supply trip.
    if (profile.pattern != NpcPattern::Courier || npc.routineStep % 4 == 0)
    {
        target.x += direction[0] * radius;
        target.y += direction[1] * radius;
    }
    npc.activity = radius > 0 ? NpcActivity::Walking : NpcActivity::Working;
    PlanRoute(npc, target);
}
} // namespace

const std::vector<QuarantineNpc> &GetNpcs()
{
    return npcs;
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
    }
    return "대기";
}
void ClearNpcs()
{
    npcs.clear();
}
void InitializeNpcs()
{
    npcs.clear();
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
        npc.decisionCooldown = float(i) * .13f;
        npcs.push_back(npc);
    }
}
void UpdateNpcs(float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::NpcUpdate);
    for (auto &npc : npcs)
    {
        npc.serviceCooldown = (std::max)(0.f, npc.serviceCooldown - deltaSeconds);
        npc.talkSeconds = (std::max)(0.f, npc.talkSeconds - deltaSeconds);
        if (npc.talkSeconds > 0)
        {
            continue;
        }
        npc.decisionCooldown -= deltaSeconds;
        if (npc.decisionCooldown <= 0)
        {
            npc.decisionCooldown = .8f + float(npc.profileIndex % 5) * .07f;
            Decide(npc);
        }
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
        float speed = npc.activity == NpcActivity::Retreating ? 2.8f : 1.25f;
        float step = (std::min)(distance, speed * deltaSeconds);
        tutorial::Vector2 next = {npc.position.x + (target.x - npc.position.x) * step / distance,
                                  npc.position.y + (target.y - npc.position.y) * step / distance};
        if (!IsBlocked(next))
        {
            npc.position = next;
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
