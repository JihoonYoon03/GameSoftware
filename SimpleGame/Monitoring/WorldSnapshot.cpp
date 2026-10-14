#include "stdafx.h"
#include "WorldSnapshot.h"
#include "LocalMonitorServer.h"
#include "../LevelOne.h"
#include "../LevelOneNpc.h"
#include "../JetTravel.h"
#include "../Profiler.h"
#include "../TutorialState.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>

namespace monitoring
{
namespace
{
std::string Quote(std::string_view value)
{
    std::string result = "\"";
    constexpr char kHex[] = "0123456789abcdef";
    for (unsigned char character : value)
    {
        if (character == '"' || character == '\\')
        {
            result += '\\';
            result += char(character);
        }
        else if (character < 32)
        {
            result += "\\u00";
            result += kHex[character >> 4];
            result += kHex[character & 15];
        }
        else
        {
            result += char(character);
        }
    }
    return result + '"';
}
class JsonObject
{
  public:
    void Raw(const char *key, const std::string &json)
    {
        fields += (fields.empty() ? "" : ",") + Quote(key) + ":" + json;
    }
    void Text(const char *key, std::string_view value)
    {
        Raw(key, Quote(value));
    }
    void Bool(const char *key, bool value)
    {
        Raw(key, value ? "true" : "false");
    }
    template <typename T> void Number(const char *key, T value)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(3) << value;
        Raw(key, out.str());
    }
    void Seconds(const char *key, float value)
    {
        Number(key, std::isfinite(value) ? value : 0.f);
    }
    std::string Finish() const
    {
        return "{" + fields + "}";
    }

  private:
    std::string fields;
};
std::string Position(tutorial::Vector2 position)
{
    JsonObject result;
    result.Seconds("x", position.x);
    result.Seconds("y", position.y);
    return result.Finish();
}
void Append(std::string &array, const std::string &object)
{
    array += (array.empty() ? "" : ",") + object;
}
const char *Role(levelone::NpcPattern pattern)
{
    switch (pattern)
    {
    case levelone::NpcPattern::Engineer:
        return "정비·전력";
    case levelone::NpcPattern::Guard:
        return "경비·순찰";
    case levelone::NpcPattern::Medic:
        return "의료";
    case levelone::NpcPattern::Courier:
        return "운송·보급";
    case levelone::NpcPattern::Researcher:
        return "관측·기록";
    case levelone::NpcPattern::Civilian:
        return "주민";
    case levelone::NpcPattern::Trader:
        return "거래";
    }
    return "미분류";
}
const char *Pattern(levelone::NpcPattern pattern)
{
    switch (pattern)
    {
    case levelone::NpcPattern::Engineer:
        return "담당 설비 수리·보급 소비";
    case levelone::NpcPattern::Guard:
        return "팀 작업장 경계·접근 몹 처치";
    case levelone::NpcPattern::Medic:
        return "부상 팀원 치료·플레이어 지원";
    case levelone::NpcPattern::Courier:
        return "집결지와 작업장 보급 왕복";
    case levelone::NpcPattern::Researcher:
        return "팀 작업장 조사·수리 지원";
    case levelone::NpcPattern::Civilian:
        return "주변 산책·위험 회피";
    case levelone::NpcPattern::Trader:
        return "고정 대기·부품 교환";
    }
    return "미분류";
}
const char *Activity(levelone::NpcActivity activity)
{
    switch (activity)
    {
    case levelone::NpcActivity::Working:
        return "working";
    case levelone::NpcActivity::Walking:
        return "walking";
    case levelone::NpcActivity::Following:
        return "following";
    case levelone::NpcActivity::Retreating:
        return "retreating";
    case levelone::NpcActivity::Talking:
        return "talking";
    case levelone::NpcActivity::Repairing:
        return "repairing";
    case levelone::NpcActivity::Defending:
        return "defending";
    case levelone::NpcActivity::Supplying:
        return "supplying";
    case levelone::NpcActivity::Healing:
        return "healing";
    case levelone::NpcActivity::Investigating:
        return "investigating";
    case levelone::NpcActivity::Recovering:
        return "recovering";
    }
    return "unknown";
}
const char *StationStatus(levelone::WorkstationStatus status)
{
    switch (status)
    {
    case levelone::WorkstationStatus::AwaitingSupplies:
        return "awaiting_supplies";
    case levelone::WorkstationStatus::Repairing:
        return "repairing";
    case levelone::WorkstationStatus::Threatened:
        return "threatened";
    case levelone::WorkstationStatus::Operational:
        return "operational";
    }
    return "unknown";
}
const char *QuestStage()
{
    switch (tutorial::g_state.mainQuestStage)
    {
    case tutorial::MainQuestStage::ReadHomeTerminal:
        return "집 단말 확인";
    case tutorial::MainQuestStage::MeetLia:
        return "리아와 대화";
    case tutorial::MainQuestStage::EnterObservatory:
        return "전망대 진입";
    case tutorial::MainQuestStage::ReadGateSignal:
        return "전망대 관측 단말 조사";
    case tutorial::MainQuestStage::DescendObservatory:
        return "전망대에서 내려오기";
    case tutorial::MainQuestStage::MountJetBike:
        return "개인용 제트 오토바이 탑승";
    case tutorial::MainQuestStage::Complete:
        return "튜토리얼 완료";
    }
    return "미확인";
}
std::string Rumor(const char *text, const char *source)
{
    JsonObject result;
    result.Text("text", text);
    result.Text("source", source);
    result.Bool("confirmed", false);
    return result.Finish();
}
std::string StaticNpc(const std::string &id, const std::string &name, tutorial::Vector2 position,
                      const char *role, const std::string &dialogue)
{
    JsonObject result;
    result.Text("id", id);
    result.Text("region_id", "halo_07");
    result.Text("name", name);
    result.Text("role", role);
    result.Raw("personality", "null");
    result.Text("behavior_pattern", "정위치 대기");
    result.Text("activity", "idle");
    result.Text("activity_label", "정위치 대기");
    result.Bool("simulation_active", false);
    result.Bool("following", false);
    result.Text("life_state", "alive");
    result.Raw("health", "null");
    result.Raw("max_health", "null");
    result.Raw("team_id", "null");
    result.Raw("station_id", "null");
    result.Raw("target_enemy_id", "null");
    result.Raw("combat_cooldown_seconds", "null");
    result.Number("kills", 0);
    result.Bool("carrying_supplies", false);
    result.Raw("action_target", "null");
    result.Raw("position", Position(position));
    result.Raw("home", Position(position));
    result.Raw("destination", "null");
    result.Number("route_remaining", 0);
    result.Raw("decision_cooldown_seconds", "null");
    result.Raw("talk_seconds", "null");
    result.Raw("service_cooldown_seconds", "null");
    result.Raw("routine_step", "null");
    result.Text("dialogue", dialogue);
    result.Raw("warning", "null");
    result.Text("source", "tutorial_static_actor");
    return result.Finish();
}
} // namespace

std::string BuildWorldSnapshot(std::uint64_t sequence, const std::string &sessionId)
{
    const auto &world = tutorial::g_state;
    const auto &level = levelone::g_level;
    const auto &travel = jettravel::g_travel;
    const auto &npcs = levelone::GetNpcs();
    const auto &stations = levelone::GetWorkstations();
    const size_t cityPopulation = world.residents.size() + 2; // Separate Lia and Mara actors.
    size_t aliveMobs = 0, retreating = 0, followers = 0, repairing = 0, defending = 0, supplying = 0;
    size_t operationalStations = 0, threatenedStations = 0;
    for (const auto &enemy : level.enemies)
    {
        aliveMobs += enemy.health > 0 ? 1 : 0;
    }
    for (const auto &npc : npcs)
    {
        retreating += npc.activity == levelone::NpcActivity::Retreating ? 1 : 0;
        followers += npc.following ? 1 : 0;
        repairing += npc.activity == levelone::NpcActivity::Repairing ? 1 : 0;
        defending += npc.activity == levelone::NpcActivity::Defending ? 1 : 0;
        supplying += npc.activity == levelone::NpcActivity::Supplying ? 1 : 0;
    }
    for (const auto &station : stations)
    {
        operationalStations += station.status == levelone::WorkstationStatus::Operational ? 1 : 0;
        threatenedStations += station.status == levelone::WorkstationStatus::Threatened ? 1 : 0;
    }
    const bool quarantineSimulating = level.active && !world.isPaused;
    JsonObject result, counts, player, quests, city, quarantine, corridor, journey, network;
    result.Text("schema", "game_monitor_v1");
    result.Bool("ready", true);
    result.Text("session_id", sessionId);
    result.Number("sequence", sequence);
    result.Number("sent_at_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    result.Number("publish_interval_ms", 500);
    result.Bool("paused", world.isPaused);
    result.Text("current_region_id", level.active        ? "quarantine"
                                     : world.isJetTravel ? "jet_corridor"
                                                         : "halo_07");
    result.Text("current_area", level.active            ? "격리 구역"
                                : world.isJetTravel     ? "관문 외곽 제트 주행 회랑"
                                : world.isInObservatory ? "헤일로 전망대 관측실"
                                : world.isInsideHome    ? "플레이어의 집"
                                                        : "헤일로 주거 구역 07");
    result.Seconds("play_time_seconds", world.playTimeSeconds);
    counts.Number("npc_total", cityPopulation + npcs.size());
    counts.Number("npc_current_region", level.active ? npcs.size() : world.isJetTravel ? 0 : cityPopulation);
    counts.Number("npc_simulating", quarantineSimulating ? npcs.size() : 0);
    counts.Number("mob_total", level.enemies.size());
    counts.Number("mob_alive", aliveMobs);
    counts.Number("mob_respawning", level.enemies.size() - aliveMobs);
    counts.Number("mob_current_region_alive", level.active ? aliveMobs : 0);
    counts.Number("drops", level.drops.size());
    counts.Number("repairing_npcs", repairing);
    counts.Number("defending_npcs", defending);
    counts.Number("supplying_npcs", supplying);
    counts.Number("workstations", stations.size());
    counts.Number("operational_workstations", operationalStations);
    counts.Number("threatened_workstations", threatenedStations);
    result.Raw("counts", counts.Finish());
    player.Raw("position", Position(world.playerPosition));
    player.Number("level", level.player.level);
    player.Number("health", level.player.health);
    player.Number("max_health", level.player.maxHealth);
    player.Number("experience", level.player.experience);
    player.Number("attack", level.player.attack);
    player.Number("defense", level.player.defense);
    player.Number("recovery_kits", level.player.recoveryKits);
    player.Number("salvage", level.player.salvage);
    player.Number("kills", level.kills);
    result.Raw("player", player.Finish());
    quests.Text("tutorial_stage", QuestStage());
    quests.Bool("gate_signal_heard", world.gateSignalHeard);
    quests.Bool("in_observatory", world.isInObservatory);
    quests.Bool("observatory_descended", world.hasDescendedObservatory);
    quests.Bool("tutorial_complete", world.isComplete);
    quests.Bool("mara_power_restored", world.isPowerRestored);
    quests.Bool("side_quest_accepted", world.isSideQuestAccepted);
    quests.Bool("side_quest_complete", world.isSideQuestComplete);
    quests.Bool("growth_goal_reached", level.growthGoalReached);
    result.Raw("quests", quests.Finish());
    journey.Bool("active", world.isJetTravel);
    journey.Seconds("elapsed_seconds", travel.elapsedSeconds);
    journey.Seconds("distance", travel.distance);
    journey.Number("route_length", jettravel::kRouteLength);
    journey.Seconds("progress_percent",
                    tutorial::Clamp(travel.distance / jettravel::kRouteLength * 100, 0, 100));
    journey.Seconds("speed", travel.speed);
    journey.Seconds("lateral_offset", travel.lateralOffset);
    journey.Seconds("lateral_velocity", travel.lateralVelocity);
    journey.Seconds("boost_charge", travel.boostCharge);
    journey.Seconds("speed_penalty_seconds", travel.speedPenaltySeconds);
    journey.Number("avoided_obstacles", travel.avoidedObstacles);
    journey.Number("collision_count", travel.collisionCount);
    result.Raw("jet_travel", journey.Finish());

    city.Text("id", "halo_07");
    city.Text("name", "헤일로 주거 구역 07");
    city.Text("kind", "마을");
    city.Bool("loaded", true);
    city.Bool("current", !level.active && !world.isJetTravel);
    city.Bool("simulation_active", false); // Current city NPCs have no AI updates.
    city.Number("population", cityPopulation);
    city.Text("population_source", "placed_npcs");
    city.Text("mood", world.gateSignalHeard   ? "신호 확인 이후의 불안"
                      : world.isPowerRestored ? "복구된 불빛과 조심스러운 안도"
                                              : "정전 속 경이와 불안");
    city.Text("mood_source", "derived_from_quest_flags");
    city.Text("disposition", "개척 생활 · 이웃 협력 · 관문 경계");
    city.Text("disposition_source", "authored_context");
    city.Text("power", world.isPowerRestored ? "동쪽 전력 복구" : "동쪽 전력 장애");
    city.Text("gate_signal", world.gateSignalHeard ? "자기 목소리 신호 확인 / 원인 미확인" : "미조사");
    city.Number("buildings", world.buildings.size());
    city.Number("mob_alive", 0);
    city.Number("mob_respawning", 0);
    city.Number("following_npcs", 0);
    city.Number("retreating_npcs", 0);
    city.Number("repairing_npcs", 0);
    city.Number("defending_npcs", 0);
    city.Number("supplying_npcs", 0);
    city.Number("workstations", 0);
    city.Number("operational_workstations", 0);
    city.Number("threatened_workstations", 0);
    city.Number("map_width", 36);
    city.Number("map_height", 26);
    city.Raw("rumors", "[" + Rumor("관문 쪽 배송이 이유 없이 취소되었다.", "주민 대사") + "," +
                           Rumor("발신 기록이 없는 응답이 수신된다.", "주민 대사") + "]");

    quarantine.Text("id", "quarantine");
    quarantine.Text("name", "관문 외곽 격리 구역 · 집결지");
    quarantine.Text("kind", "시험 거점");
    quarantine.Bool("loaded", level.generated);
    quarantine.Bool("current", level.active);
    quarantine.Bool("simulation_active", quarantineSimulating);
    quarantine.Number("population", npcs.size());
    quarantine.Text("population_source", "placed_npcs");
    quarantine.Text("mood", !level.generated          ? "아직 진입하지 않음"
                            : threatenedStations > 0  ? "작업장 위협 · 팀 방어 진행"
                            : retreating > 0          ? "위험 감지 · 부상자와 귀환로 보호"
                            : operationalStations > 0 ? "설비 가동 · 공동 정비 유지"
                                                      : "경계 속 공동 작업");
    quarantine.Text("mood_source", "derived_from_npc_activity");
    quarantine.Text("disposition", "안전 우선 · 기록 보존 · 상호 지원");
    quarantine.Text("disposition_source", "authored_context");
    quarantine.Text("power", "작업장 " + std::to_string(operationalStations) + "/" +
                                 std::to_string(stations.size()) + " 가동 · 중앙 집결지 운영");
    quarantine.Text("gate_signal", "반복 응답 · 원인 미확인");
    quarantine.Raw("buildings", "null");
    quarantine.Number("mob_alive", aliveMobs);
    quarantine.Number("mob_respawning", level.enemies.size() - aliveMobs);
    quarantine.Number("following_npcs", followers);
    quarantine.Number("retreating_npcs", retreating);
    quarantine.Number("repairing_npcs", repairing);
    quarantine.Number("defending_npcs", defending);
    quarantine.Number("supplying_npcs", supplying);
    quarantine.Number("workstations", stations.size());
    quarantine.Number("operational_workstations", operationalStations);
    quarantine.Number("threatened_workstations", threatenedStations);
    quarantine.Number("map_width", levelone::kMapSize);
    quarantine.Number("map_height", levelone::kMapSize);
    quarantine.Number("map_seed", level.seed);
    quarantine.Raw("rumors", "[" +
                                 Rumor("현장 인원은 다섯인데 무전에는 여섯이 응답했다.", "도윤의 설정 대사") +
                                 "," + Rumor("질문을 보내기 전에 답이 왔다.", "시온의 설정 대사") + "]");
    corridor.Text("id", "jet_corridor");
    corridor.Text("name", "관문 외곽 제트 주행 회랑");
    corridor.Text("kind", "이동 구간");
    corridor.Bool("loaded", world.isJetTravel || world.isComplete);
    corridor.Bool("current", world.isJetTravel);
    corridor.Bool("simulation_active", world.isJetTravel && !world.isPaused);
    corridor.Number("population", 0);
    corridor.Text("population_source", "placed_npcs");
    corridor.Text("mood", world.isJetTravel ? "빠른 주행 · 격리 구역으로 접근" : "주행 대기 / 이동 기록");
    corridor.Text("mood_source", "derived_from_travel_state");
    corridor.Text("disposition", "주거 구역과 격리 구역을 연결하는 이동로");
    corridor.Text("disposition_source", "authored_context");
    corridor.Text("power", "개인용 제트 추진기");
    corridor.Text("gate_signal", world.gateSignalHeard ? "관측 기록 보유" : "미조사");
    corridor.Number("buildings", 0);
    corridor.Number("mob_alive", 0);
    corridor.Number("mob_respawning", 0);
    corridor.Number("following_npcs", 0);
    corridor.Number("retreating_npcs", 0);
    corridor.Number("repairing_npcs", 0);
    corridor.Number("defending_npcs", 0);
    corridor.Number("supplying_npcs", 0);
    corridor.Number("workstations", 0);
    corridor.Number("operational_workstations", 0);
    corridor.Number("threatened_workstations", 0);
    corridor.Number("map_width", jettravel::kRouteLength);
    corridor.Raw("map_height", "null");
    corridor.Text("map_size_label", "주행 거리 " + std::to_string(int(jettravel::kRouteLength)));
    corridor.Raw("rumors", "[]");
    result.Raw("regions", "[" + city.Finish() + "," + quarantine.Finish() + "," + corridor.Finish() + "]");

    std::string npcArray;
    Append(npcArray,
           StaticNpc("halo/lia", "리아",
                     world.isInObservatory ? tutorial::kObservatoryLiaPosition : tutorial::kLiaPosition,
                     "정비사", "전망대에서 관문 신호를 측정해 줘."));
    Append(npcArray,
           StaticNpc("halo/mara", "마라", tutorial::kMaraPosition, "주민",
                     world.isPowerRestored ? "집에 불빛이 돌아왔어요." : "동쪽 전력 장치를 확인해 주세요."));
    for (size_t i = 0; i < world.residents.size(); ++i)
    {
        const auto &resident = world.residents[i];
        Append(npcArray, StaticNpc("halo/resident/" + std::to_string(i), resident.name, resident.position,
                                   "주민", resident.dialogue));
    }
    for (size_t i = 0; i < npcs.size(); ++i)
    {
        const auto &npc = npcs[i];
        const auto &profile = levelone::GetNpcProfile(i);
        JsonObject item;
        item.Text("id", "quarantine/npc/" + std::to_string(i));
        item.Text("region_id", "quarantine");
        item.Text("name", profile.name);
        item.Text("role", Role(profile.pattern));
        item.Text("personality", profile.personality);
        item.Text("behavior_pattern", Pattern(profile.pattern));
        item.Text("activity", Activity(npc.activity));
        item.Text("activity_label", levelone::GetNpcActivityName(npc.activity));
        item.Bool("simulation_active", quarantineSimulating);
        item.Bool("following", npc.following);
        item.Text("life_state", npc.activity == levelone::NpcActivity::Recovering ? "recovering" : "alive");
        item.Number("health", (std::max)(0, npc.health));
        item.Number("max_health", npc.maxHealth);
        if (npc.teamId >= 0)
        {
            item.Number("team_id", npc.teamId);
        }
        else
        {
            item.Raw("team_id", "null");
        }
        if (npc.stationId >= 0)
        {
            item.Number("station_id", npc.stationId);
        }
        else
        {
            item.Raw("station_id", "null");
        }
        if (npc.targetEnemy >= 0)
        {
            item.Text("target_enemy_id", "quarantine/mob/" + std::to_string(npc.targetEnemy));
        }
        else
        {
            item.Raw("target_enemy_id", "null");
        }
        item.Seconds("combat_cooldown_seconds", npc.combatCooldown);
        item.Number("kills", npc.kills);
        item.Bool("carrying_supplies", npc.carryingSupplies);
        item.Raw("action_target", npc.actionFlash > 0 ? Position(npc.actionTarget) : "null");
        item.Raw("position", Position(npc.position));
        item.Raw("home", Position(npc.home));
        item.Raw("destination", Position(npc.destination));
        item.Number("route_remaining", npc.route.size() - (std::min)(npc.routeIndex, npc.route.size()));
        item.Seconds("decision_cooldown_seconds", npc.decisionCooldown);
        item.Seconds("talk_seconds", npc.talkSeconds);
        item.Seconds("service_cooldown_seconds", npc.serviceCooldown);
        item.Number("routine_step", npc.routineStep);
        item.Text("dialogue", profile.dialogue);
        item.Text("warning", profile.warning);
        item.Text("source", "quarantine_test_actor");
        Append(npcArray, item.Finish());
    }
    result.Raw("npcs", "[" + npcArray + "]");
    std::string stationArray;
    for (const auto &station : stations)
    {
        JsonObject item;
        item.Number("id", station.id);
        item.Number("team_id", station.teamId);
        item.Text("region_id", "quarantine");
        item.Text("name", station.name);
        item.Raw("position", Position(station.position));
        item.Seconds("integrity", station.integrity);
        item.Seconds("repair_progress_percent", station.repairProgress);
        item.Number("supplies", station.supplies);
        item.Number("completed_cycles", station.completedCycles);
        item.Seconds("cycle_cooldown_seconds", station.cycleCooldown);
        item.Text("status", StationStatus(station.status));
        item.Text("status_label", levelone::GetWorkstationStatusName(station.status));
        item.Bool("simulation_active", quarantineSimulating);
        Append(stationArray, item.Finish());
    }
    result.Raw("workstations", "[" + stationArray + "]");
    std::string mobArray;
    for (size_t i = 0; i < level.enemies.size(); ++i)
    {
        const auto &enemy = level.enemies[i];
        JsonObject item;
        item.Text("id", "quarantine/mob/" + std::to_string(i));
        item.Text("region_id", "quarantine");
        item.Number("health", (std::max)(0, enemy.health));
        item.Number("max_health", levelone::kEnemyMaxHealth);
        item.Bool("alive", enemy.health > 0);
        item.Seconds("respawn_seconds", (std::max)(0.f, enemy.respawnSeconds));
        item.Raw("position", Position(enemy.position));
        Append(mobArray, item.Finish());
    }
    result.Raw("mobs", "[" + mobArray + "]");
    result.Raw("performance", profiling::GetLatestReportJson());
    network.Text("protocol", "http_json_poll");
    network.Number("requests_served", transport::ReadRequestCount());
    result.Raw("transport", network.Finish());
    result.Text("notice", world.noticeSecondsRemaining > 0 ? world.noticeText : "");
    return result.Finish();
}
} // namespace monitoring
