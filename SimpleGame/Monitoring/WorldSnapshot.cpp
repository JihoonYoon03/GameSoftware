#include "stdafx.h"
#include "WorldSnapshot.h"
#include "LocalMonitorServer.h"
#include "../LevelOne.h"
#include "../LevelOneNpc.h"
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
        return "근거리 점검·대기";
    case levelone::NpcPattern::Guard:
        return "귀환로 순찰·안전 우선";
    case levelone::NpcPattern::Medic:
        return "집결지 대기·치료 지원";
    case levelone::NpcPattern::Courier:
        return "보급 왕복";
    case levelone::NpcPattern::Researcher:
        return "외곽 관측·기록";
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
    case tutorial::MainQuestStage::ReadGateSignal:
        return "관문 신호 조사";
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
    const auto &npcs = levelone::GetNpcs();
    const size_t cityPopulation = world.residents.size() + 2; // Separate Lia and Mara actors.
    size_t aliveMobs = 0, retreating = 0, followers = 0;
    for (const auto &enemy : level.enemies)
    {
        aliveMobs += enemy.health > 0 ? 1 : 0;
    }
    for (const auto &npc : npcs)
    {
        retreating += npc.activity == levelone::NpcActivity::Retreating ? 1 : 0;
        followers += npc.following ? 1 : 0;
    }
    const bool quarantineSimulating = level.active && !world.isPaused;
    JsonObject result, counts, player, quests, city, quarantine, network;
    result.Text("schema", "game_monitor_v1");
    result.Bool("ready", true);
    result.Text("session_id", sessionId);
    result.Number("sequence", sequence);
    result.Number("sent_at_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    result.Number("publish_interval_ms", 500);
    result.Bool("paused", world.isPaused);
    result.Text("current_region_id", level.active ? "quarantine" : "halo_07");
    result.Text("current_area", level.active         ? "격리 구역"
                                : world.isInsideHome ? "플레이어의 집"
                                                     : "헤일로 주거 구역 07");
    result.Seconds("play_time_seconds", world.playTimeSeconds);
    counts.Number("npc_total", cityPopulation + npcs.size());
    counts.Number("npc_current_region", level.active ? npcs.size() : cityPopulation);
    counts.Number("npc_simulating", quarantineSimulating ? npcs.size() : 0);
    counts.Number("mob_total", level.enemies.size());
    counts.Number("mob_alive", aliveMobs);
    counts.Number("mob_respawning", level.enemies.size() - aliveMobs);
    counts.Number("mob_current_region_alive", level.active ? aliveMobs : 0);
    counts.Number("drops", level.drops.size());
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
    quests.Bool("gate_signal_heard", world.isComplete);
    quests.Bool("mara_power_restored", world.isPowerRestored);
    quests.Bool("side_quest_accepted", world.isSideQuestAccepted);
    quests.Bool("side_quest_complete", world.isSideQuestComplete);
    quests.Bool("growth_goal_reached", level.growthGoalReached);
    result.Raw("quests", quests.Finish());

    city.Text("id", "halo_07");
    city.Text("name", "헤일로 주거 구역 07");
    city.Text("kind", "마을");
    city.Bool("loaded", true);
    city.Bool("current", !level.active);
    city.Bool("simulation_active", false); // Current city NPCs have no AI updates.
    city.Number("population", cityPopulation);
    city.Text("population_source", "placed_npcs");
    city.Text("mood", world.isComplete        ? "신호 확인 이후의 불안"
                      : world.isPowerRestored ? "복구된 불빛과 조심스러운 안도"
                                              : "정전 속 경이와 불안");
    city.Text("mood_source", "derived_from_quest_flags");
    city.Text("disposition", "개척 생활 · 이웃 협력 · 관문 경계");
    city.Text("disposition_source", "authored_context");
    city.Text("power", world.isPowerRestored ? "동쪽 전력 복구" : "동쪽 전력 장애");
    city.Text("gate_signal", world.isComplete ? "자기 목소리 신호 확인 / 원인 미확인" : "미조사");
    city.Number("buildings", world.buildings.size());
    city.Number("mob_alive", 0);
    city.Number("mob_respawning", 0);
    city.Number("following_npcs", 0);
    city.Number("retreating_npcs", 0);
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
    quarantine.Text("mood", !level.generated ? "아직 진입하지 않음"
                            : retreating > 0 ? "위험 감지 · 귀환 우선"
                                             : "경계 속 공동 작업");
    quarantine.Text("mood_source", "derived_from_npc_activity");
    quarantine.Text("disposition", "안전 우선 · 기록 보존 · 상호 지원");
    quarantine.Text("disposition_source", "authored_context");
    quarantine.Text("power", "중앙 집결지 운영");
    quarantine.Text("gate_signal", "반복 응답 · 원인 미확인");
    quarantine.Raw("buildings", "null");
    quarantine.Number("mob_alive", aliveMobs);
    quarantine.Number("mob_respawning", level.enemies.size() - aliveMobs);
    quarantine.Number("following_npcs", followers);
    quarantine.Number("retreating_npcs", retreating);
    quarantine.Number("map_width", levelone::kMapSize);
    quarantine.Number("map_height", levelone::kMapSize);
    quarantine.Number("map_seed", level.seed);
    quarantine.Raw("rumors", "[" +
                                 Rumor("현장 인원은 다섯인데 무전에는 여섯이 응답했다.", "도윤의 설정 대사") +
                                 "," + Rumor("질문을 보내기 전에 답이 왔다.", "시온의 설정 대사") + "]");
    result.Raw("regions", "[" + city.Finish() + "," + quarantine.Finish() + "]");

    std::string npcArray;
    Append(npcArray, StaticNpc("halo/lia", "리아", tutorial::kLiaPosition, "정비사",
                               "전망대에서 관문 신호를 측정해 줘."));
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
        item.Text("life_state", "alive");
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
