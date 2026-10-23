#include "stdafx.h"
#include "Monitor.h"
#include "../Civic.h"
#include "../AssetCache.h"
#include "../Level.h"
#include "../ModelLibrary.h"
#include "../Profiler.h"
#include "LocalMonitorServer.h"
#include "Overworld.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>
#include <windows.h>

namespace monitoring
{

// ============================================================================
// WorldSnapshot
// ============================================================================
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
        char buffer[96];
        std::to_chars_result converted;
        if constexpr (std::is_floating_point_v<T>)
        {
            converted = std::to_chars(buffer, buffer + sizeof(buffer), std::isfinite(value) ? value : T(0),
                                      std::chars_format::fixed, 3);
        }
        else
        {
            converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        }
        Raw(key, converted.ec == std::errc() ? std::string(buffer, converted.ptr) : "0");
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
    switch (tutorial::Game::Get().GetState().mainQuestStage)
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
                      const char *role, const std::string &dialogue, const char *regionId = "halo_07")
{
    JsonObject result;
    result.Text("id", id);
    result.Text("region_id", regionId);
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
std::string ResidentNpc(const std::string &id, const tutorial::Resident &resident, bool active)
{
    JsonObject result;
    result.Text("id", id);
    result.Text("region_id", tutorial::Overworld::Cities()[resident.cityIndex].id);
    result.Text("name", resident.name);
    result.Text("role", tutorial::CityResidents::ProfessionName(resident.profession));
    result.Text("personality", resident.profession == tutorial::ResidentProfession::Guard
                                   ? "질서·안전 우선"
                                   : "직업 일정·생활 동선");
    result.Text("behavior_pattern", "직업 시설 방문 / 보행로 이동 / 자동 신호 대기·횡단 / 휴식");
    result.Text("activity", tutorial::CityResidents::ActivityName(resident.activity));
    result.Text("activity_label", tutorial::CityResidents::ActivityLabel(resident));
    result.Bool("simulation_active", active);
    result.Bool("following", false);
    result.Text("life_state", resident.health > 0 ? "alive" : "dead");
    result.Number("health", resident.health);
    result.Number("fear_seconds", resident.fearSeconds);
    result.Raw("position", Position(resident.position));
    result.Raw("home", Position(resident.home));
    result.Raw("destination", Position(resident.destination));
    result.Number("route_remaining", resident.route.size() - resident.routeCursor);
    result.Number("crossing_junction", resident.crossingJunction);
    result.Seconds("walking_speed", resident.speed);
    result.Seconds("decision_cooldown_seconds", resident.waitSeconds);
    result.Seconds("talk_seconds", resident.conversationSeconds);
    result.Number("routine_step", resident.routineStep);
    result.Text("dialogue", resident.dialogue);
    result.Raw("warning", resident.activity == tutorial::ResidentActivity::NoRoute
                              ? Quote("보행 경로 없음 · 재탐색 대기")
                              : "null");
    result.Text("source", "city_resident_simulation");
    return result.Finish();
}
} // namespace

std::string WorldSnapshot::Build(std::uint64_t sequence, const std::string &sessionId)
{
    const auto &world = tutorial::Game::Get().GetState();
    const auto &level = game::GameLevels::Mine().GetState();
    const auto &chapter = game::GameLevels::Chapter().GetState();
    const size_t fieldScrappers = std::count_if(chapter.scrappers.begin(), chapter.scrappers.end(),
                                                [](const game::FrontierPerson &p) { return !p.arrived; });
    const size_t frontierPopulation =
        chapter.generated ? chapter.survivors.size() + (chapter.active ? 1 : 0) +
                                (!chapter.kainJoined || chapter.active ? 1 : 0) + fieldScrappers
                          : 0;
    const bool simulating = level.active && !world.isPaused;
    const bool citySimulation =
        !level.active && !chapter.active && !world.isPaused && !world.isInsideHome && !world.isInObservatory;
    const bool cityLia = !level.active && !chapter.active;
    const bool cityKain = chapter.kainJoined && !chapter.active;
    const size_t haloPopulation = 19 + (cityLia ? 1 : 0) - (chapter.kainJoined ? 1 : 0) + (cityKain ? 1 : 0);
    const size_t cityPopulation =
        haloPopulation + (tutorial::Overworld::kCityCount - 1) * tutorial::Overworld::kPopulationPerCity;
    int currentCity = tutorial::Overworld::CityAt(world.playerPosition);
    const char *outdoorId = currentCity >= 0 ? tutorial::Overworld::Cities()[currentCity].id : "overworld";
    const char *outdoorName =
        currentCity >= 0 ? tutorial::Overworld::Cities()[currentCity].name : "도시 간 연결 도로";
    const size_t minePopulation = level.survivors.size() + (level.active ? 1 : 0);
    size_t aliveMobs = 0;
    for (const auto &enemy : level.enemies)
    {
        aliveMobs += enemy.health > 0 ? 1 : 0;
    }
    JsonObject result, counts, player, quests, city, mine, corridor, frontier, journey, network, flags;
    result.Text("schema", "game_monitor_v1");
    result.Bool("ready", true);
    result.Text("session_id", sessionId);
    result.Number("sequence", sequence);
    result.Number("sent_at_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    result.Number("publish_interval_ms", 500);
    result.Bool("paused", world.isPaused);
    result.Text("current_region_id", chapter.active ? "frontier" : level.active ? "mine" : outdoorId);
    result.Text("current_area", chapter.active          ? "빛나는 개척지 · 발전기/운송대/폐설비"
                                : level.active          ? "광산 · 구조 신호 조사"
                                : world.isInObservatory ? "헤일로 전망대 관측실"
                                : world.isInsideHome    ? "플레이어의 집"
                                                        : outdoorName);
    result.Seconds("play_time_seconds", world.playTimeSeconds);
    counts.Number("npc_total", cityPopulation + minePopulation + frontierPopulation);
    counts.Number("npc_current_region", chapter.active     ? frontierPopulation
                                        : level.active     ? minePopulation
                                        : currentCity == 0 ? haloPopulation
                                        : currentCity > 0  ? tutorial::Overworld::kPopulationPerCity
                                                           : 0);
    counts.Number("npc_simulating", chapter.active && !world.isPaused ? frontierPopulation
                                    : simulating                      ? minePopulation
                                    : citySimulation ? world.residents.size() - (chapter.kainJoined ? 1 : 0)
                                                     : 0);
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
    player.Number("credits", level.player.credits);
    player.Number("kills", level.kills);
    player.Number("magazine_rounds", level.pistol.GetRounds());
    player.Number("magazine_capacity", level.pistol.GetMagazineSize());
    player.Bool("reloading", level.pistol.IsReloading());
    player.Seconds("spread_radians", level.pistol.GetSpread());
    result.Raw("player", player.Finish());
    const auto &law = tutorial::CivicJustice::State();
    JsonObject security;
    security.Number("wanted_stars", law.wanted);
    security.Number("heat", law.heat);
    security.Number("infamy", law.infamy);
    security.Number("price_multiplier", tutorial::CivicJustice::PriceMultiplier());
    security.Number("officers", law.officers.size());
    security.Number("hospital_transfers", law.deaths);
    security.Bool("recovering", tutorial::HospitalRecovery::Active());
    security.Number("recovery_opacity", tutorial::HospitalRecovery::Opacity());
    security.Number("lost_levels", law.lostLevels);
    security.Number("lost_credits", law.lostCredits);
    security.Number("lost_salvage", law.lostSalvage);
    result.Raw("security", security.Finish());

    quests.Text("tutorial_stage", QuestStage());
    quests.Text("main_quest", game::GameLevels::Chapter().IsAvailable()
                                  ? game::GameLevels::Chapter().Objective()
                                  : level.quest.GetObjective());
    quests.Bool("gate_signal_heard", world.gateSignalHeard);
    quests.Bool("in_observatory", world.isInObservatory);
    quests.Bool("observatory_descended", world.hasDescendedObservatory);
    quests.Bool("tutorial_complete", world.isComplete);
    quests.Bool("mara_power_restored", world.isPowerRestored);
    quests.Bool("side_quest_accepted", world.isSideQuestAccepted);
    quests.Bool("side_quest_complete", world.isSideQuestComplete);
    result.Raw("quests", quests.Finish());
    auto stage = level.quest.GetStage();
    flags.Bool("main.signal_record_shown", stage >= game::MineQuestStage::CheckCityRelay);
    flags.Bool("main.mine_trace_confirmed", stage >= game::MineQuestStage::TravelToMine);
    flags.Bool("companion.ria_joined", level.quest.IsRiaJoined());
    flags.Bool("main.mine_power_bypassed", level.quest.IsPowerBypassed());
    flags.Bool("main.mine_signal_investigated", stage >= game::MineQuestStage::InvestigateGlyph);
    flags.Bool("main.observer_glyph_investigated", stage >= game::MineQuestStage::CollectCoordinates);
    flags.Bool("main.mine_relay_coordinate_found", level.quest.AreCoordinatesFound());
    flags.Bool("main.mine_returned_to_halo", stage == game::MineQuestStage::Complete);
    flags.Bool("main.city_generator_restored", chapter.generatorRestored);
    flags.Bool("main.convoy_survivors_rescued", chapter.convoyRescued);
    flags.Bool("companion.kain_joined", chapter.kainJoined);
    flags.Bool("main.scrapper_ledger_found", chapter.ledgerFound);
    flags.Bool("side.scrapper_detail_ledger_found", chapter.detailLedgerFound);
    flags.Bool("side.convoy_vehicle_repaired", chapter.vehicleRepaired);
    flags.Bool("side.ria_portable_light_repaired", chapter.portableLightRepaired);
    flags.Bool("main.chapter1_complete", chapter.recordsCompared);
    flags.Bool("main.outer_waterway_unlocked", chapter.waterwayUnlocked);
    result.Raw("flags", flags.Finish());
    journey.Text("mode", "direct_overworld_driving");
    journey.Bool("active", world.isBikeMounted);
    journey.Raw("position", Position(world.bikePosition));
    journey.Seconds("speed", world.bikeSpeed);
    journey.Seconds("speed_kmh", world.bikeSpeed * models::WorldScale::metersPerUnit * 3.6f);
    journey.Number("cruise_speed_kmh",
                   tutorial::Overworld::kBikeCruiseSpeed * models::WorldScale::metersPerUnit * 3.6f);
    journey.Text("road_layout", "seeded_radial_districts");
    result.Raw("jet_travel", journey.Finish());
    JsonObject traffic;
    const auto &trafficSystem = tutorial::Overworld::Traffic();
    const auto &roads = tutorial::RoadNetwork::Get();
    traffic.Number("active_vehicles", trafficSystem.Vehicles().size());
    traffic.Number("vehicle_limit", tutorial::TrafficSystem::kVehicleLimit);
    traffic.Number("lanes", roads.lanes.size());
    traffic.Number("junctions", roads.junctions.size());
    traffic.Number("air_corridors", roads.airCorridors.size());
    traffic.Number("aircraft", tutorial::Overworld::Flights().Aircraft().size());
    traffic.Number("aircraft_limit", tutorial::AirTraffic::kAircraftLimit);
    traffic.Number("scenery_props", tutorial::Overworld::Scenery().Props().size());
    traffic.Text("driving_side", "right");
    traffic.Text("pedestrian_mode", "road_context_crossing_cycle");
    traffic.Number("pedestrian_navigation_nodes", tutorial::CityResidents::Get().NavigationNodeCount());
    traffic.Text("road_layout", "road_first_compact_junctions_and_elevated_bypass");
    size_t elevatedRoads = 0;
    for (size_t i = 0; i < roads.roads.size(); ++i)
    {
        elevatedRoads += (std::max)(roads.junctions[roads.roads[i].from].altitude,
                                    roads.junctions[roads.roads[i].to].altitude) > .1f;
    }
    traffic.Number("elevated_roads", elevatedRoads);
    traffic.Text("road_geometry", "procedural_plan_blockout");
    traffic.Number("transit_module_instances", tutorial::TransitLayout::Get().size());
    traffic.Number("connected_building_frontages", tutorial::CityPlan::Connections().size());
    traffic.Number("pedestrian_access_modules", tutorial::CityPlan::Walkways().size());
    size_t waitingResidents = 0, crossingResidents = 0, noRouteResidents = 0;
    for (const auto &resident : world.residents)
    {
        if (&resident - world.residents.data() == tutorial::Overworld::kKainResidentSlot &&
            chapter.kainJoined)
        {
            continue;
        }
        waitingResidents += resident.activity == tutorial::ResidentActivity::WaitingSignal;
        crossingResidents += resident.activity == tutorial::ResidentActivity::Crossing;
        noRouteResidents += resident.activity == tutorial::ResidentActivity::NoRoute;
    }
    traffic.Number("residents_waiting_signal", waitingResidents);
    traffic.Number("residents_crossing", crossingResidents);
    traffic.Number("residents_without_route", noRouteResidents);
    std::string junctionSignals;
    for (size_t junction = 0; junction < trafficSystem.Signals().size(); ++junction)
    {
        if (!roads.IsSignalized(int(junction)))
        {
            continue;
        }
        const auto &signal = trafficSystem.Signals()[junction];
        JsonObject item;
        item.Number("junction", junction);
        item.Number("phase", int(signal.phase));
        item.Number("pedestrian_group", signal.walkCycle);
        item.Number("pedestrian_groups", trafficSystem.PedestrianGroups(int(junction)));
        item.Seconds("elapsed_seconds", signal.elapsed);
        item.Seconds("remaining_seconds",
                     (std::max)(0.f, trafficSystem.PhaseDuration(int(junction)) - signal.elapsed));
        item.Bool("pedestrian_green", trafficSystem.PedestrianGreen(int(junction)));
        item.Bool("crossing_occupied", trafficSystem.CrossingOccupied(world, int(junction)));
        std::string crossings;
        for (int lane : roads.junctions[junction].outgoing)
        {
            if (roads.lanes[lane].slot != 0)
            {
                continue;
            }
            JsonObject crossing;
            crossing.Number("road", roads.lanes[lane].road);
            crossing.Bool("green", trafficSystem.PedestrianGreen(int(junction), roads.lanes[lane].road));
            Append(crossings, crossing.Finish());
        }
        item.Raw("crossings", "[" + crossings + "]");
        Append(junctionSignals, item.Finish());
    }
    traffic.Raw("signals", "[" + junctionSignals + "]");
    traffic.Bool("simulating", !level.active && !chapter.active && !world.isInsideHome &&
                                   !world.isInObservatory && !world.isPaused);
    std::string cars = "[";
    for (const auto &car : trafficSystem.Vehicles())
    {
        JsonObject item;
        item.Number("id", car.id);
        item.Number("lane", car.lane);
        item.Raw("position", Position(car.position));
        item.Seconds("speed", car.speed);
        item.Bool("turning", car.turnDistance >= 0);
        item.Text("turn", car.turn == tutorial::TrafficTurn::Left    ? "left"
                          : car.turn == tutorial::TrafficTurn::Right ? "right"
                                                                     : "straight");
        Append(cars, item.Finish());
    }
    traffic.Raw("vehicles", cars + "]");
    std::string flights = "[";
    for (const auto &craft : tutorial::Overworld::Flights().Aircraft())
    {
        JsonObject item;
        item.Number("id", craft.id);
        item.Number("route", craft.route);
        item.Raw("position", Position(craft.position));
        item.Seconds("altitude_m", craft.altitude * 2);
        item.Number("type", unsigned(craft.type));
        item.Bool("alternate", craft.alternate);
        Append(flights, item.Finish());
    }
    traffic.Raw("flights", flights + "]");
    result.Raw("traffic", traffic.Finish());

    auto fillRegion = [](JsonObject &region, const char *id, const char *name, bool loaded, bool current,
                         bool simulation, size_t population, size_t mobs, size_t respawning,
                         const char *rumor = nullptr) {
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
        region.Raw("rumors", rumor ? "[" + Rumor(rumor, "주민 구전") + "]" : "[]");
    };
    fillRegion(city, "halo_07", "헤일로 주거 구역 07", true, currentCity == 0 && cityLia, false,
               haloPopulation, 0, 0, tutorial::Overworld::Cities()[0].rumor);
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
    mine.Number("map_width", game::kMapSize);
    mine.Number("map_height", game::kMapSize);
    mine.Number("map_seed", level.seed);
    mine.Number("generation_revision", level.mapRevision);
    mine.Number("walkable_cells", level.reachableCells.size());
    fillRegion(corridor, "overworld", "도시 간 연결 도로", true, currentCity < 0 && cityLia, false, 0, 0, 0);
    corridor.Text("mood", "다섯 도시와 광산을 잇는 개방 도로");
    corridor.Number("map_width", tutorial::kWorldMaxX - tutorial::kWorldMinX);
    corridor.Number("map_height", tutorial::kWorldMaxY - tutorial::kWorldMinY);
    size_t activeScrappers = 0;
    for (const auto &person : chapter.scrappers)
    {
        activeScrappers += person.combat.health > 0 && !person.arrived;
    }
    fillRegion(frontier, "frontier", "빛나는 개척지", chapter.generated, chapter.active,
               chapter.active && !world.isPaused, frontierPopulation, 0, 0);
    frontier.Text("mood", game::GameLevels::Chapter().Objective());
    std::string regions = city.Finish();
    for (int i = 1; i < tutorial::Overworld::kCityCount; ++i)
    {
        const auto &info = tutorial::Overworld::Cities()[i];
        JsonObject region;
        fillRegion(region, info.id, info.name, true, cityLia && currentCity == i, false,
                   tutorial::Overworld::kPopulationPerCity, 0, 0, info.rumor);
        region.Text("mood", info.mood);
        region.Raw("center", Position(info.center));
        Append(regions, region.Finish());
    }
    result.Raw("regions",
               "[" + regions + "," + mine.Finish() + "," + corridor.Finish() + "," + frontier.Finish() + "]");

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
        if (i == tutorial::Overworld::kKainResidentSlot && chapter.kainJoined)
        {
            continue;
        }
        const auto &resident = world.residents[i];
        Append(npcArray, ResidentNpc(std::string(tutorial::Overworld::Cities()[resident.cityIndex].id) +
                                         "/resident/" + std::to_string(i),
                                     resident, citySimulation));
    }
    auto mineNpc = [&](const std::string &id, const char *name, const char *role, tutorial::Vector2 position,
                       const char *activity, const char *label, size_t remaining, bool following, int group,
                       const game::NpcCombatState &combat) {
        JsonObject item;
        item.Text("id", id);
        item.Text("region_id", "mine");
        item.Text("name", name);
        item.Text("role", role);
        item.Text("personality", following ? "실증적 · 기록 우선" : "구조 요청 중");
        item.Text("behavior_pattern", following ? "벽을 우회하며 플레이어 동행" : "구조 확인 후 귀환로 보행");
        item.Text("activity", combat.health <= 0 ? "downed"
                              : combat.evading   ? "evading"
                              : combat.engaged   ? "defending"
                                                 : activity);
        item.Text("activity_label", combat.health <= 0 ? "응급 처치 대기"
                                    : combat.evading   ? "회피"
                                    : combat.engaged   ? "자체 방어"
                                                       : label);
        item.Bool("simulation_active", simulating);
        item.Bool("following", following);
        item.Text("life_state", combat.health <= 0 ? "downed" : "alive");
        item.Number("health", combat.health);
        item.Number("max_health", 100);
        item.Number("magazine_rounds", combat.pistol.GetRounds());
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
                level.riaRoute.size() - (std::min)(level.riaRoute.size(), level.riaRouteIndex), true, -1,
                level.riaCombat);
    }
    for (const auto &npc : level.survivors)
    {
        bool guard = level.rescueGroups[npc.group].combatants;
        mineNpc("mine/" + std::to_string(level.mapRevision) + "/survivor/" + std::to_string(npc.id),
                guard ? "고립 전투원" : "광부", guard ? "전투원" : "광부", npc.position,
                npc.evacuating ? "evacuating" : "distress", npc.evacuating ? "헤일로로 철수" : "구조 요청",
                npc.route.size() - (std::min)(npc.route.size(), npc.routeIndex), false, int(npc.group),
                npc.combat);
    }
    auto frontierNpc = [&](const std::string &id, const char *name, const game::FrontierPerson &person,
                           bool following, bool hostile) {
        JsonObject item;
        item.Text("id", id);
        item.Text("region_id", "frontier");
        item.Text("name", name);
        item.Text("role", hostile ? "폐설비 점거자" : following ? "동료" : "운송대 생존자");
        item.Raw("position", Position(person.position));
        item.Number("health", person.combat.health);
        item.Number("max_health", hostile ? 60 : 100);
        item.Text("life_state", person.combat.health > 0 ? "alive" : "downed");
        item.Text("activity", person.combat.engaged   ? "defending"
                              : person.facing.walking ? "walking"
                                                      : "idle");
        item.Bool("simulation_active", chapter.active && !world.isPaused && !person.arrived);
        item.Bool("following", following);
        item.Number("magazine_rounds", person.combat.pistol.GetRounds());
        item.Text("source", "chapter_one_actor");
        Append(npcArray, item.Finish());
    };
    if (chapter.generated)
    {
        if (chapter.active)
        {
            frontierNpc("frontier/ria", "리아", chapter.ria, true, false);
        }
        if (!chapter.kainJoined || chapter.active)
        {
            frontierNpc("frontier/kain", "카인", chapter.kain, chapter.kainJoined, false);
        }
        else if (cityKain)
        {
            Append(npcArray, StaticNpc("halo/kain", "카인", tutorial::kCityKainPosition, "동료",
                                       "N-0 외곽 수로로 돌아갈 표식을 남깁시다."));
        }
        for (size_t i = 0; i < chapter.survivors.size(); ++i)
        {
            frontierNpc("frontier/convoy/" + std::to_string(i), "운송대 생존자", chapter.survivors[i], false,
                        false);
        }
        for (size_t i = 0; i < chapter.scrappers.size(); ++i)
        {
            if (!chapter.scrappers[i].arrived)
            {
                frontierNpc("frontier/scrapper/" + std::to_string(i), "폐설비 점거자", chapter.scrappers[i],
                            false, true);
            }
        }
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
    investigation.Number("map_seed", level.seed);
    investigation.Number("generation_revision", level.mapRevision);
    investigation.Number("walkable_cells", level.reachableCells.size());
    investigation.Seconds("flashlight_angle", game::GameLevels::Mine().GetAimAngle());
    investigation.Raw("rescue_groups", "[" + groups + "]");
    result.Raw("mine", investigation.Finish());
    JsonObject field;
    field.Bool("active", chapter.active);
    field.Number("completed_sites", game::GameLevels::Chapter().CompletedSites());
    field.Number("survey_markers", chapter.surveyMarkers);
    field.Bool("escort_started", chapter.escortStarted);
    field.Bool("receiver_replay_confirmed", chapter.receiverInspected);
    field.Bool("negotiated", chapter.negotiated);
    field.Bool("hostile", chapter.hostile);
    field.Number("active_scrappers", chapter.negotiated ? 0 : activeScrappers);
    field.Seconds("comparison_seconds", chapter.comparisonSeconds);
    result.Raw("chapter_one", field.Finish());
    std::string mobArray;
    for (size_t i = 0; i < level.enemies.size(); ++i)
    {
        const auto &enemy = level.enemies[i];
        JsonObject item;
        item.Text("id", "mine/" + std::to_string(level.mapRevision) + "/mob/" + std::to_string(i));
        item.Text("region_id", "mine");
        item.Number("health", (std::max)(0, enemy.health));
        item.Number("max_health", game::kEnemyMaxHealth);
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
// ============================================================================
// ModelCatalog
// ============================================================================
namespace
{
void PointJson(std::ostream &out, models::Point point)
{
    out << '[' << point.x << ',' << point.y << ',' << point.z << ']';
}
void ModelJson(std::ostream &out, const models::Model &model, const char *category, const char *name,
               size_t type, const models::VehicleModel *vehicle = nullptr, const char *gender = nullptr)
{
    out << "{\"category\":\"" << category << "\",\"name\":\"" << name << "\",\"type\":" << type
        << ",\"space\":\"" << (model.space == models::CoordinateSpace::World ? "world" : "screen")
        << "\",\"validated\":" << (models::ModelFactory::ValidateModel(model) ? "true" : "false")
        << ",\"parts\":[";
    bool character = std::string(category) == "character";
    const auto bindPose = models::ModelRig::Pose(
        0, false, false, 0, false,
        gender && std::string(gender) == "female" ? models::Gender::Female : models::Gender::Male);
    bool firstPart = true;
    if (model.resourceId)
    {
        auto appendResource = [&](unsigned int resourceId) {
            const auto &resource = models::ResourceAssets::Get(resourceId);
            models::SkinMatrices matrices;
            models::ResourceAssets::Sample(resourceId, nullptr, matrices);
            for (size_t triangle = 0; triangle < resource.vertices.size(); triangle += 3)
            {
                const auto &v = resource.vertices[triangle];
                if (v.visibility == 1)
                {
                    continue;
                }
                out << (firstPart ? "" : ",") << "{\"kind\":0,\"palette\":" << unsigned(v.palette)
                    << ",\"width\":1,\"color\":[" << v.r << ',' << v.g << ',' << v.b << ',' << v.a
                    << "],\"points\":[";
                for (size_t j = 0; j < 3; ++j)
                {
                    out << (j ? "," : "");
                    PointJson(out,
                              models::ResourceAssets::SkinPoint(resource.vertices[triangle + j], matrices));
                }
                out << "]}";
                firstPart = false;
            }
        };
        appendResource(model.resourceId);
        if (character &&
            type % size_t(models::CharacterType::Count) != size_t(models::CharacterType::HeavyCombat))
        {
            constexpr const char *hairs[] = {"SidePart", "Ponytail", "Buzz",     "Bun",  "Bob",
                                             "Buzz",     "Buzz",     "SidePart", "Curls"};
            appendResource(models::ResourceAssets::Find(std::string("hair/") +
                                                        hairs[type % size_t(models::CharacterType::Count)]));
        }
    }
    for (size_t i = 0; i < model.count; ++i)
    {
        const auto &part = model.parts[i];
        if (character && part.joint == models::Joint::Weapon)
        {
            continue;
        }
        out << (firstPart ? "" : ",") << "{\"kind\":" << unsigned(part.kind)
            << ",\"palette\":" << unsigned(part.palette) << ",\"width\":" << part.width << ",\"color\":[";
        for (size_t j = 0; j < 4; ++j)
        {
            out << (j ? "," : "") << part.color[j];
        }
        out << "],\"points\":[";
        for (size_t j = 0; j < part.count; ++j)
        {
            if (j)
            {
                out << ',';
            }
            PointJson(out, character ? bindPose.Apply(part.points[j], part.joint) : part.points[j]);
        }
        out << "]}";
        firstPart = false;
    }
    out << ']';
    if (model.resourceId)
    {
        const auto &resource = models::ResourceAssets::Get(model.resourceId);
        out << ",\"source\":" << Quote(resource.source + ".glb")
            << ",\"triangles\":" << resource.vertices.size() / 3;
        models::Point previewScale = {1, 1, 1};
        if (std::string(category) == "building")
        {
            previewScale = {resource.physicalSize.x, resource.physicalSize.y,
                            resource.physicalSize.z / resource.maximum.z};
        }
        else if (character)
        {
            previewScale = {models::WorldScale::characterScale, models::WorldScale::characterScale,
                            models::WorldScale::characterScale};
        }
        out << ",\"preview_scale\":";
        PointJson(out, previewScale);
    }
    if (gender)
    {
        out << ",\"gender\":\"" << gender << "\"";
    }
    if (vehicle)
    {
        out << ",\"exhausts\":[";
        for (size_t i = 0; i < 2; ++i)
        {
            if (i)
            {
                out << ',';
            }
            PointJson(out, vehicle->exhausts[i]);
        }
        out << "],\"headlights\":[";
        for (size_t i = 0; i < 2; ++i)
        {
            if (i)
            {
                out << ',';
            }
            PointJson(out, vehicle->headlights[i]);
        }
        out << ']';
    }
    out << '}';
}
} // namespace
std::string ModelCatalog::Build()
{
    const auto &library = models::ModelLibrary::Get();
    constexpr const char *kVehicleNames[] = {"쿠페", "여객 셔틀", "화물 수송차", "순찰차",
                                             "개인용 제트 오토바이"};
    constexpr const char *kCharacterNames[] = {"탐험가", "정비사",        "경비원", "의무관", "연구원",
                                               "광부",   "중장갑 전투원", "조종사", "주민"};
    constexpr const char *kBuildingNames[] = {
        "압력 주거 모듈", "적층 거주 포드", "자기장 정비 격납고", "리브 돔",       "전망대",
        "나선 첨탑",      "연결 교량 타워", "테라스 아콜로지",    "환승 아트리움", "중정 주거동"};
    static_assert(std::size(kBuildingNames) == size_t(models::BuildingType::Count));
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(5) << "{\"schema\":\"game_models_v1\",\"models\":[";
    bool first = true;
    for (size_t i = 0; i < library.vehicles.size(); ++i)
    {
        if (!first)
        {
            out << ',';
        }
        first = false;
        ModelJson(out, library.vehicles[i].body, "vehicle", kVehicleNames[i], i, &library.vehicles[i]);
    }
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        out << ',';
        ModelJson(out, library.characters[i], "character", kCharacterNames[i], i, nullptr, "male");
        out << ',';
        ModelJson(out, library.femaleCharacters[i], "character", kCharacterNames[i],
                  i + library.characters.size(), nullptr, "female");
    }
    for (size_t i = 0; i < library.buildings.size(); ++i)
    {
        out << ',';
        ModelJson(out, library.buildings[i], "building", kBuildingNames[i], i);
    }
    constexpr const char *aircraftNames[] = {"운송기", "에어택시", "화물 인양기", "탐사기"};
    for (size_t i = 0; i < library.aircraft.size(); ++i)
    {
        out << ',';
        auto name = std::string(aircraftNames[i / 2]) + (i % 2 ? " / 대체 모듈" : " / 기본 모듈");
        ModelJson(out, library.aircraft[i], "aircraft", name.c_str(), i);
    }
    for (size_t i = 0; i < library.scenery.size(); ++i)
    {
        out << ',';
        ModelJson(out, library.scenery[i], "scenery",
                  models::ModelLibrary::SceneryName(static_cast<models::SceneryType>(i)), i);
    }
    std::set<std::string> transitNames;
    for (const auto &module : tutorial::TransitLayout::Get())
    {
        transitNames.insert(module.asset);
    }
    size_t transitType = 0;
    for (const auto &name : transitNames)
    {
        out << ',';
        ModelJson(out, models::ModelLibrary::Transit(name), "transit", name.c_str(), transitType++);
    }
    out << "],\"character_base\":null}";
    return out.str();
}
// ============================================================================
// GameMonitor
// ============================================================================
namespace
{
using Clock = std::chrono::steady_clock;
constexpr unsigned short kPort = 17707;
constexpr auto kPublishInterval = std::chrono::milliseconds(500);

} // namespace
void GameMonitor::Initialize()
{
    if (enabled)
    {
        return;
    }
    wchar_t module[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (!length || length >= MAX_PATH)
    {
        std::fprintf(stderr, "[Monitor] cannot resolve executable directory\n");
        return;
    }
    sessionId = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    sequence = 0;
    lastRequestCount = 0;
    transport::Publish(WorldSnapshot::Build(++sequence, sessionId));
    const auto executable = std::filesystem::path(module).parent_path();
    // Support both deployed binaries and starting a test/debug executable from the source tree.
    for (const auto &directory : {executable / L"Monitor", executable / L".." / L".." / L"monitor",
                                  std::filesystem::current_path() / L"monitor"})
    {
        if (!std::filesystem::exists(directory / L"index.html"))
        {
            continue;
        }
        enabled = transport::Start(directory, kPort);
        break;
    }
    if (enabled)
    {
        transport::PublishModels(ModelCatalog::Build());
        nextPublish = Clock::now() + kPublishInterval;
        std::printf("\n[Monitor] Open browser: http://127.0.0.1:%u/\n[Monitor] JSON: /api/state (500 ms)\n\n",
                    unsigned(kPort));
    }
}
void GameMonitor::Update()
{
    if (!enabled || Clock::now() < nextPublish)
    {
        return;
    }
    nextPublish = Clock::now() + kPublishInterval;
    const auto requests = transport::ReadRequestCount();
    if (requests == lastRequestCount)
    {
        return; // No serialization work with no dashboard polling.
    }
    lastRequestCount = requests;
    profiling::Scope timer(profiling::Timer::MonitorSnapshot);
    transport::Publish(WorldSnapshot::Build(++sequence, sessionId));
    profiling::Count(profiling::Counter::MonitorSnapshots);
    nextPublish = Clock::now() + kPublishInterval;
}
void GameMonitor::Shutdown()
{
    transport::Stop();
    enabled = false;
}
GameMonitor &GameMonitor::Get()
{
    static GameMonitor monitor;
    return monitor;
}

} // namespace monitoring
