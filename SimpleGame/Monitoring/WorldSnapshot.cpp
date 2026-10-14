#include "stdafx.h"
#include "WorldSnapshot.h"
#include "LocalMonitorServer.h"
#include "../LevelOne.h"
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
    const bool simulating = level.active && !world.isPaused;
    const bool cityLia = !level.active && !world.isJetTravel;
    const size_t cityPopulation = world.residents.size() + 1 + (cityLia ? 1 : 0);
    const size_t minePopulation = level.survivors.size() + (level.active ? 1 : 0);
    size_t aliveMobs = 0;
    for (const auto &enemy : level.enemies)
    {
        aliveMobs += enemy.health > 0 ? 1 : 0;
    }
    JsonObject result, counts, player, quests, city, mine, corridor, journey, network, flags;
    result.Text("schema", "game_monitor_v1");
    result.Bool("ready", true);
    result.Text("session_id", sessionId);
    result.Number("sequence", sequence);
    result.Number("sent_at_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    result.Number("publish_interval_ms", 500);
    result.Bool("paused", world.isPaused);
    result.Text("current_region_id", level.active ? "mine" : world.isJetTravel ? "jet_corridor" : "halo_07");
    result.Text("current_area", level.active            ? "광산 · 구조 신호 조사"
                                : world.isJetTravel     ? "광산행 제트 주행 회랑"
                                : world.isInObservatory ? "헤일로 전망대 관측실"
                                : world.isInsideHome    ? "플레이어의 집"
                                                        : "헤일로 주거 구역 07");
    result.Seconds("play_time_seconds", world.playTimeSeconds);
    counts.Number("npc_total", cityPopulation + minePopulation);
    counts.Number("npc_current_region", level.active        ? minePopulation
                                        : world.isJetTravel ? 0
                                                            : cityPopulation);
    counts.Number("npc_simulating", simulating ? minePopulation : 0);
    counts.Number("mob_total", level.enemies.size());
    counts.Number("mob_alive", aliveMobs);
    counts.Number("mob_respawning", level.enemies.size() - aliveMobs);
    counts.Number("mob_current_region_alive", level.active ? aliveMobs : 0);
    counts.Number("drops", level.drops.size());
    counts.Number("rescued_people", level.rescuedPeople);
    counts.Number("returned_people", level.returnedPeople);
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
    player.Number("magazine_rounds", level.pistol.GetRounds());
    player.Number("magazine_capacity", level.pistol.GetMagazineSize());
    player.Bool("reloading", level.pistol.IsReloading());
    player.Seconds("spread_radians", level.pistol.GetSpread());
    result.Raw("player", player.Finish());
    quests.Text("tutorial_stage", QuestStage());
    quests.Text("main_quest", level.quest.GetObjective());
    quests.Bool("gate_signal_heard", world.gateSignalHeard);
    quests.Bool("in_observatory", world.isInObservatory);
    quests.Bool("observatory_descended", world.hasDescendedObservatory);
    quests.Bool("tutorial_complete", world.isComplete);
    quests.Bool("mara_power_restored", world.isPowerRestored);
    quests.Bool("side_quest_accepted", world.isSideQuestAccepted);
    quests.Bool("side_quest_complete", world.isSideQuestComplete);
    result.Raw("quests", quests.Finish());
    auto stage = level.quest.GetStage();
    flags.Bool("main.signal_record_shown", stage >= levelone::MineQuestStage::CheckCityRelay);
    flags.Bool("main.mine_trace_confirmed", stage >= levelone::MineQuestStage::TravelToMine);
    flags.Bool("companion.ria_joined", level.quest.IsRiaJoined());
    flags.Bool("main.mine_power_bypassed", level.quest.IsPowerBypassed());
    flags.Bool("main.mine_signal_investigated", stage >= levelone::MineQuestStage::InvestigateGlyph);
    flags.Bool("main.observer_glyph_investigated", stage >= levelone::MineQuestStage::CollectCoordinates);
    flags.Bool("main.mine_relay_coordinate_found", level.quest.AreCoordinatesFound());
    flags.Bool("main.mine_returned_to_halo", stage == levelone::MineQuestStage::Complete);
    result.Raw("flags", flags.Finish());
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

    auto fillRegion = [](JsonObject &region, const char *id, const char *name, bool loaded, bool current,
                         bool simulation, size_t population, size_t mobs, size_t respawning) {
        region.Text("id", id);
        region.Text("name", name);
        region.Bool("loaded", loaded);
        region.Bool("current", current);
        region.Bool("simulation_active", simulation);
        region.Number("population", population);
        region.Text("population_source", "placed_npcs");
        region.Text("disposition", "구조 우선 · 신호와 기록의 출처 확인");
        region.Number("mob_alive", mobs);
        region.Number("mob_respawning", respawning);
        region.Raw("rumors", "[]");
    };
    fillRegion(city, "halo_07", "헤일로 주거 구역 07", true, cityLia, false, cityPopulation, 0, 0);
    city.Text("mood",
              level.quest.AreCoordinatesFound() ? "광산 중계 좌표 확보" : "관문 신호의 발신 경로 조사");
    city.Text("power", world.isPowerRestored ? "동쪽 전력 복구" : "동쪽 전력 장애");
    city.Text("gate_signal", world.gateSignalHeard ? "기록 확보 / 원인 미확인" : "미조사");
    city.Text("map_size_label", "도시 주거 구역");
    fillRegion(mine, "mine", "광산 · 폐쇄 갱도와 지하 유적", level.generated, level.active, simulating,
               minePopulation, aliveMobs, level.enemies.size() - aliveMobs);
    mine.Text("mood", level.quest.GetObjective());
    mine.Text("power", level.quest.IsPowerBypassed() ? "전력 우회 · 격벽 개방" : "단전 · 격벽 폐쇄");
    mine.Text("gate_signal", level.quest.AreCoordinatesFound() ? "중계 좌표 확보 / 헤일로 귀환"
                                                               : "광부·전투원 구조 요청 및 실종 탐사선 신호");
    mine.Number("map_width", levelone::kMapSize);
    mine.Number("map_height", levelone::kMapSize);
    mine.Number("map_seed", level.seed);
    fillRegion(corridor, "jet_corridor", "광산행 제트 주행 회랑", world.isJetTravel || world.isComplete,
               world.isJetTravel, world.isJetTravel && !world.isPaused, 0, 0, 0);
    corridor.Text("mood", "리아와 광산으로 이동");
    corridor.Text("power", "개인용 제트 추진기");
    corridor.Text("gate_signal", "광산 통신선의 중계 흔적 확인");
    corridor.Text("map_size_label", "주행 거리 1800");
    result.Raw("regions", "[" + city.Finish() + "," + mine.Finish() + "," + corridor.Finish() + "]");

    std::string npcArray;
    if (cityLia)
    {
        Append(npcArray,
               StaticNpc("halo/ria", "리아",
                         world.isInObservatory ? tutorial::kObservatoryLiaPosition : tutorial::kLiaPosition,
                         "정비사", level.quest.GetObjective()));
    }
    Append(npcArray, StaticNpc("halo/mara", "마라", tutorial::kMaraPosition, "주민",
                               world.isPowerRestored ? "불빛이 돌아왔어요." : "전력 장치를 확인해 주세요."));
    for (size_t i = 0; i < world.residents.size(); ++i)
    {
        const auto &resident = world.residents[i];
        Append(npcArray, StaticNpc("halo/resident/" + std::to_string(i), resident.name, resident.position,
                                   "주민", resident.dialogue));
    }
    auto mineNpc = [&](const std::string &id, const char *name, const char *role, tutorial::Vector2 position,
                       const char *activity, const char *label, size_t remaining, bool following, int group) {
        JsonObject item;
        item.Text("id", id);
        item.Text("region_id", "mine");
        item.Text("name", name);
        item.Text("role", role);
        item.Text("personality", following ? "실증적 · 기록 우선" : "구조 요청 중");
        item.Text("behavior_pattern", following ? "벽을 우회하며 플레이어 동행" : "구조 확인 후 귀환로 보행");
        item.Text("activity", activity);
        item.Text("activity_label", label);
        item.Bool("simulation_active", simulating);
        item.Bool("following", following);
        item.Text("life_state", "alive");
        item.Raw("health", "null");
        item.Raw("max_health", "null");
        item.Raw("team_id", "null");
        item.Raw("station_id", "null");
        item.Raw("target_enemy_id", "null");
        item.Number("kills", 0);
        item.Bool("carrying_supplies", false);
        item.Raw("position", Position(position));
        item.Raw("home", Position(level.entry));
        item.Raw("destination", Position(following ? world.playerPosition : level.entry));
        item.Number("route_remaining", remaining);
        item.Number("group_id", group);
        item.Text("source", "mine_story_actor");
        Append(npcArray, item.Finish());
    };
    if (level.active)
    {
        mineNpc("mine/ria", "리아", "동료 · 정비사", level.riaPosition, "following", "동행",
                level.riaRoute.size() - (std::min)(level.riaRoute.size(), level.riaRouteIndex), true, -1);
    }
    for (const auto &npc : level.survivors)
    {
        bool guard = level.rescueGroups[npc.group].combatants;
        mineNpc("mine/survivor/" + std::to_string(npc.id), guard ? "고립 전투원" : "광부",
                guard ? "전투원" : "광부", npc.position, npc.evacuating ? "evacuating" : "distress",
                npc.evacuating ? "헤일로로 철수" : "구조 요청",
                npc.route.size() - (std::min)(npc.route.size(), npc.routeIndex), false, int(npc.group));
    }
    result.Raw("npcs", "[" + npcArray + "]");
    result.Raw("workstations", "[]");
    std::string groups;
    for (size_t i = 0; i < level.rescueGroups.size(); ++i)
    {
        const auto &group = level.rescueGroups[i];
        JsonObject item;
        item.Number("id", i);
        item.Text("name", group.combatants ? "고립 전투원 무리" : "광부 구조 요청");
        item.Bool("rescued", group.rescued);
        item.Raw("position", Position(group.position));
        item.Number("members", 3);
        Append(groups, item.Finish());
    }
    JsonObject investigation;
    investigation.Text("quest", level.quest.GetObjective());
    investigation.Bool("ria_joined", level.quest.IsRiaJoined());
    investigation.Number("rescued_people", level.rescuedPeople);
    investigation.Number("returned_people", level.returnedPeople);
    investigation.Seconds("scanner_seconds", level.scannerSeconds);
    investigation.Seconds("scanner_cooldown", level.scannerCooldown);
    investigation.Number("active_bullets", level.bullets.size());
    investigation.Seconds("flashlight_angle", levelone::GetAimAngle());
    investigation.Raw("rescue_groups", "[" + groups + "]");
    result.Raw("mine", investigation.Finish());
    std::string mobArray;
    for (size_t i = 0; i < level.enemies.size(); ++i)
    {
        const auto &enemy = level.enemies[i];
        JsonObject item;
        item.Text("id", "mine/mob/" + std::to_string(i));
        item.Text("region_id", "mine");
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
