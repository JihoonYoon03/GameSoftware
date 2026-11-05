#include "stdafx.h"
#include "Save.h"
#include "Civic.h"
#include "FacilityApproaches.h"
#include "CityInfrastructure.h"
#include "ResidentAI.h"
#include "Level.h"
#include "Overworld.h"
#include "Scene.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace tutorial
{
namespace
{
constexpr std::uint64_t kMaximumFile = 16 * 1024 * 1024;
constexpr unsigned kSchema = 9;
void Require(bool value, const char *message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
struct Checkpoint
{
    world::PlanetDefinition planet;
    unsigned layout = world::ContentLayout::Revision;
    std::uint64_t digest = 0;
    world::TangentFrame pose, origin;
    std::optional<world::SurfacePoint> bike, mineReturn;
    std::optional<world::Vector3d> bikeHeading;
    GameState game;
    ::game::MineState mine;
    ::game::ChapterOneState chapter;
    CivicState civic;
    PlanetCities cities;
    AirTraffic flights;
    PlanetTrafficState transit;
    PlanetPartyState party;
    PlanetSecurityState security;
    std::vector<PlanetTrafficLink> trafficLinks;
    PlanetCommunityState community;
    bool legacyLeftTraffic = false;
};
std::string WorldKey(const world::PlanetDefinition &planet)
{
    world::RelayNetwork identity;
    identity.Bind(planet);
    return identity.WorldKey();
}
struct Campaign
{
    std::string active;
    std::map<std::string, Checkpoint> worlds;
};
Campaign campaign;
std::string message;
bool available = false;
struct PendingLoad
{
    Checkpoint checkpoint;
    Campaign candidate;
    std::unique_ptr<world::SurfaceSession> session;
    std::unique_ptr<PlanetTraffic> traffic;
    std::unique_ptr<PlanetParty> party;
    std::unique_ptr<PlanetCommunity> community;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    bool validated = false, restored = false, backup = false;
    bool switching = false, fresh = false;
};
std::unique_ptr<PendingLoad> pending;

template <class T> constexpr unsigned EnumCount()
{
    if constexpr (std::is_same_v<T, world::SpaceKind>)
    {
        return 4;
    }
    else if constexpr (std::is_same_v<T, MainQuestStage>)
    {
        return unsigned(MainQuestStage::Complete) + 1;
    }
    else if constexpr (std::is_same_v<T, MissionId>)
    {
        return unsigned(MissionId::Count) + 1; // Count represents automatic/no explicit story selection.
    }
    else if constexpr (std::is_same_v<T, MissionStatus>)
    {
        return 4;
    }
    else if constexpr (std::is_same_v<T, DialoguePhase>)
    {
        return 3;
    }
    else if constexpr (std::is_same_v<T, BodyResponse>)
    {
        return 4;
    }
    else if constexpr (std::is_same_v<T, ResidentProfession>)
    {
        return unsigned(ResidentProfession::Explorer) + 1;
    }
    else if constexpr (std::is_same_v<T, ResidentActivity>)
    {
        return unsigned(ResidentActivity::Dead) + 1;
    }
    else if constexpr (std::is_same_v<T, models::BuildingType>)
    {
        return unsigned(models::BuildingType::Count);
    }
    else if constexpr (std::is_same_v<T, models::CharacterType>)
    {
        return unsigned(models::CharacterType::Count);
    }
    else if constexpr (std::is_same_v<T, models::Gender>)
    {
        return 2;
    }
    else if constexpr (std::is_same_v<T, models::HairStyle>)
    {
        return unsigned(models::HairStyle::Count);
    }
    else if constexpr (std::is_same_v<T, models::AircraftType>)
    {
        return unsigned(models::AircraftType::Count);
    }
    else if constexpr (std::is_same_v<T, game::Tile>)
    {
        return 3;
    }
    else if constexpr (std::is_same_v<T, game::FrontierTile>)
    {
        return 5;
    }
    else if constexpr (std::is_same_v<T, game::ItemKind>)
    {
        return 2;
    }
    else if constexpr (std::is_same_v<T, game::MineQuestStage>)
    {
        return unsigned(game::MineQuestStage::Complete) + 1;
    }
    else if constexpr (std::is_same_v<T, combat::WeaponSlot>)
    {
        return 3;
    }
    else if constexpr (std::is_same_v<T, TrafficTurn>)
    {
        return 3;
    }
    else if constexpr (std::is_same_v<T, CompanionPlace>)
    {
        return 3;
    }
    else if constexpr (std::is_same_v<T, SignalPhase>)
    {
        return unsigned(SignalPhase::WalkClear) + 1;
    }
    else
    {
        static_assert(!sizeof(T), "Archive enum needs an explicit compatibility range");
    }
}
bool Utf8(const std::string &text)
{
    return text.empty() || (text.find('\0') == std::string::npos &&
                            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
                                                nullptr, 0) > 0);
}
} // namespace

// ============================================================================
// SaveArchive — bounded, little-endian values; no raw objects, pointers, GL resources or callbacks
// ============================================================================
class SaveArchive
{
  public:
    static Checkpoint Capture()
    {
        const auto &g = Game::Get();
        PlanetSecurity::Project();
        PlanetSecurity::Capture();
        if (g.surfaceTraffic && g.surfaceCities)
        {
            for (int city = 0; city < Overworld::kCityCount; ++city)
            {
                g.surfaceTraffic->SyncCity(city, g.surfaceCities->Traffic(city), *g.surface);
            }
        }
        if (g.surfaceParty)
        {
            g.surfaceParty->Capture();
        }
        Checkpoint c;
        c.planet = g.surface->Planet();
        c.digest = g.surface->Stream().Snapshot()->planet->Digest();
        c.layout = g.surface->Stream().Snapshot()->planet->layoutRevision;
        c.pose = g.surface->Pose();
        c.origin = g.surface->Origin();
        c.bike = g.surfaceBike;
        c.bikeHeading = g.surfaceBikeHeading;
        c.mineReturn = g.surfaceMineReturn;
        c.game = g.state;
        c.mine = game::GameLevels::Mine().GetState();
        c.chapter = game::GameLevels::Chapter().GetState();
        c.civic = CivicJustice::State();
        c.security = PlanetSecurity::State();
        if (g.surfaceCities)
        {
            c.cities = *g.surfaceCities;
        }
        c.flights = Overworld::Flights();
        if (g.surfaceTraffic)
        {
            c.transit = g.surfaceTraffic->State();
            c.trafficLinks = g.surfaceTraffic->Links();
        }
        if (g.surfaceParty)
        {
            c.party = g.surfaceParty->State();
        }
        if (g.surfaceCommunity)
        {
            c.community = g.surfaceCommunity->State();
        }
        // The memory ledger follows disk persistence rules too: no held inputs, callbacks,
        // projectiles, open panels, render resources or temporary worker ownership survive a visit.
        // Activity selection is serialized explicitly after the physical/community records.
        SaveArchive writer(false);
        writer.Value(c);
        writer.Value(c.transit);
        writer.Value(c.party);
        writer.Value(c.security);
        writer.Value(c.trafficLinks);
        writer.Value(c.community);
        writer.Tracking(c);
        Checkpoint clean;
        SaveArchive reader(true, writer.Bytes());
        reader.Value(clean);
        reader.Value(clean.transit);
        reader.Value(clean.party);
        reader.Value(clean.security);
        reader.Value(clean.trafficLinks);
        reader.Value(clean.community);
        reader.Tracking(clean);
        Require(reader.Finished(), "메모리 월드 기록에 지원하지 않는 데이터가 있습니다.");
        ApplyTrafficLinks(clean);
        return clean;
    }
    static void ApplyTrafficLinks(Checkpoint &c)
    {
        for (auto &actor : c.transit.actors)
        {
            actor.city = -1;
            actor.cityVehicle = 0;
        }
        for (auto &traffic : c.cities.traffic)
        {
            for (auto &car : traffic.vehicles)
            {
                car.globalId = 0;
            }
        }
        Require(c.trafficLinks.size() <= PlanetTraffic::GroundLimit, "차량 인계 기록이 너무 많습니다.");
        std::set<std::uint64_t> actors;
        std::set<std::pair<int, std::uint64_t>> projections;
        std::array<size_t, Overworld::kCityCount> counts = {};
        for (const auto &link : c.trafficLinks)
        {
            Require(c.planet.storyPlanet && link.city >= 0 && link.city < Overworld::kCityCount &&
                        actors.insert(link.actor).second &&
                        projections.insert({link.city, link.vehicle}).second,
                    "차량 인계의 지역/중복 ID가 올바르지 않습니다.");
            Require(++counts[size_t(link.city)] <= PlanetTraffic::CityLimit,
                    "도시 전역 차량 수가 상한을 초과했습니다.");
            auto actor = std::find_if(c.transit.actors.begin(), c.transit.actors.end(),
                                      [&](const auto &v) { return v.id == link.actor; });
            auto &traffic = c.cities.traffic[size_t(link.city)];
            auto car = std::find_if(traffic.vehicles.begin(), traffic.vehicles.end(),
                                    [&](const auto &v) { return v.id == link.vehicle; });
            Require(actor != c.transit.actors.end() && !actor->airborne && car != traffic.vehicles.end() &&
                        TrafficSystem::CoreLane(car->lane, link.city) && actor->health == car->health &&
                        actor->occupied == car->occupied && actor->id % 4 == car->id % 4,
                    "도시 차량과 전역 차량의 소유권/체력/외형이 다릅니다.");
            actor->city = link.city;
            actor->cityVehicle = link.vehicle;
            car->globalId = link.actor;
        }
    }
    static void ValidateTrafficLinks(const Checkpoint &c)
    {
        size_t owned = 0;
        for (const auto &actor : c.transit.actors)
        {
            if (actor.city < 0)
            {
                continue;
            }
            ++owned;
            Require(std::any_of(c.trafficLinks.begin(), c.trafficLinks.end(),
                                [&](const auto &link) {
                                    return link.actor == actor.id && link.city == actor.city &&
                                           link.vehicle == actor.cityVehicle;
                                }),
                    "도시가 맡은 차량의 인계 기록이 없습니다.");
        }
        Require(owned == c.trafficLinks.size(), "차량 인계 기록의 소유권 수가 다릅니다.");
        for (size_t city = 0; city < c.cities.traffic.size(); ++city)
        {
            for (const auto &car : c.cities.traffic[city].vehicles)
            {
                if (car.globalId)
                {
                    Require(std::any_of(c.trafficLinks.begin(), c.trafficLinks.end(),
                                        [&](const auto &link) {
                                            return link.actor == car.globalId && link.city == int(city) &&
                                                   link.vehicle == car.id;
                                        }),
                            "도시 차량에 대응하는 전역 개체가 없습니다.");
                }
            }
        }
    }
    explicit SaveArchive(bool reading, std::vector<unsigned char> bytes = {})
        : reading(reading), bytes(std::move(bytes))
    {
    }
    template <class... T> void Fields(T &...values)
    {
        (Value(values), ...);
    }
    const std::vector<unsigned char> &Bytes() const
    {
        return bytes;
    }
    bool Finished() const
    {
        return offset == bytes.size();
    }
    void Raw(void *data, size_t count)
    {
        Require(count <= kMaximumFile, "저장 데이터가 허용 크기를 초과했습니다.");
        if (reading)
        {
            Require(offset <= bytes.size() && count <= bytes.size() - offset,
                    "저장 데이터가 중간에 잘렸습니다.");
            if (count)
            {
                std::memcpy(data, bytes.data() + offset, count);
            }
            offset += count;
        }
        else
        {
            Require(bytes.size() + count <= kMaximumFile, "저장 데이터가 허용 크기를 초과했습니다.");
            auto *begin = static_cast<unsigned char *>(data);
            if (count)
            {
                bytes.insert(bytes.end(), begin, begin + count);
            }
        }
    }
    void Number(std::uint64_t &value, unsigned count)
    {
        unsigned char data[8]{};
        if (!reading)
        {
            for (unsigned i = 0; i < count; ++i)
            {
                data[i] = static_cast<unsigned char>(value >> (i * 8));
            }
        }
        Raw(data, count);
        if (reading)
        {
            value = 0;
            for (unsigned i = 0; i < count; ++i)
            {
                value |= std::uint64_t(data[i]) << (i * 8);
            }
        }
    }
    template <class T> void Value(T &value)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            std::uint64_t n = !reading && value ? 1 : 0;
            Number(n, 1);
            Require(n <= 1, "저장된 참/거짓 값이 올바르지 않습니다.");
            value = n != 0;
        }
        else if constexpr (std::is_enum_v<T>)
        {
            std::uint64_t n = reading ? 0 : static_cast<unsigned>(value);
            Number(n, 4);
            Require(n < EnumCount<T>(), "저장된 상태 종류를 지원하지 않습니다.");
            value = static_cast<T>(n);
        }
        else if constexpr (std::is_integral_v<T>)
        {
            std::uint64_t n = reading ? 0 : static_cast<std::uint64_t>(value);
            Number(n, 8);
            if (reading)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    std::int64_t signedValue;
                    std::memcpy(&signedValue, &n, 8);
                    Require(signedValue >= std::numeric_limits<T>::min() &&
                                signedValue <= std::numeric_limits<T>::max(),
                            "저장된 정수 범위가 올바르지 않습니다.");
                    value = static_cast<T>(signedValue);
                }
                else
                {
                    Require(n <= std::numeric_limits<T>::max(), "저장된 정수 범위가 올바르지 않습니다.");
                    value = static_cast<T>(n);
                }
            }
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            std::uint64_t n = 0;
            if constexpr (sizeof(T) == 4)
            {
                std::uint32_t part = 0;
                if (!reading)
                {
                    std::memcpy(&part, &value, 4);
                }
                n = part;
                Number(n, 4);
                part = std::uint32_t(n);
                std::memcpy(&value, &part, 4);
            }
            else
            {
                if (!reading)
                {
                    std::memcpy(&n, &value, 8);
                }
                Number(n, 8);
                std::memcpy(&value, &n, 8);
            }
            Require(std::isfinite(value) && std::fabs(value) <= 1.e12,
                    "저장된 좌표 또는 수치가 올바르지 않습니다.");
        }
        else
        {
            static_assert(!sizeof(T), "Archive type requires explicit fields");
        }
    }
    std::uint64_t Count(size_t size)
    {
        std::uint64_t count = size;
        Value(count);
        Require(count <= 65536 && elements + count <= 200000, "저장된 목록 크기가 허용 범위를 초과했습니다.");
        if (reading)
        {
            Require(count <= bytes.size() - offset, "저장된 목록이 중간에 잘렸습니다.");
        }
        elements += count;
        return count;
    }
    void Value(std::string &text)
    {
        std::uint64_t count = text.size();
        Value(count);
        Require(count <= 1024 * 1024, "저장된 문자열이 너무 깁니다.");
        if (reading)
        {
            Require(count <= bytes.size() - offset, "저장된 문자열이 중간에 잘렸습니다.");
            text.resize(size_t(count));
        }
        Raw(text.data(), size_t(count));
        Require(Utf8(text), "저장된 한글/문자열의 UTF-8 인코딩이 올바르지 않습니다.");
    }
    template <class T> void Value(std::vector<T> &items)
    {
        auto count = Count(items.size());
        if (reading)
        {
            items.resize(size_t(count));
        }
        for (auto &item : items)
        {
            Value(item);
        }
    }
    template <class T, size_t N> void Value(std::array<T, N> &items)
    {
        for (auto &item : items)
        {
            Value(item);
        }
    }
    template <class T> void Value(std::optional<T> &item)
    {
        bool present = item.has_value();
        Value(present);
        if (reading)
        {
            if (present)
            {
                item.emplace();
            }
            else
            {
                item.reset();
            }
        }
        if (present)
        {
            Value(*item);
        }
    }
    template <class T> void Value(std::set<T> &items)
    {
        auto count = Count(items.size());
        if (reading)
        {
            items.clear();
            for (std::uint64_t i = 0; i < count; ++i)
            {
                T item{};
                Value(item);
                Require(items.insert(std::move(item)).second, "저장된 ID가 중복됐습니다.");
            }
        }
        else
        {
            for (auto item : items)
            {
                Value(item);
            }
        }
    }
    template <class T> void Value(std::map<std::string, T> &items)
    {
        auto count = Count(items.size());
        if (reading)
        {
            items.clear();
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                T item{};
                Fields(key, item);
                Require(items.emplace(std::move(key), std::move(item)).second,
                        "저장된 기록 ID가 중복됐습니다.");
            }
        }
        else
        {
            for (auto &item : items)
            {
                auto key = item.first;
                Fields(key, item.second);
            }
        }
    }
    void Value(Vector2 &v)
    {
        Fields(v.x, v.y);
    }
    void Value(world::Vector3d &v)
    {
        Fields(v.x, v.y, v.z);
    }
    void Value(world::SurfacePoint &v)
    {
        Fields(v.normal, v.altitudeMeters);
    }
    void Value(world::TangentFrame &v)
    {
        Fields(v.origin, v.right, v.forward);
    }
    void Value(world::PlanetDefinition &v)
    {
        Fields(v.id, v.name, v.systemId, v.seed, v.generatorVersion, v.radiusMeters, v.storyPlanet);
    }
    void Value(models::Palette &v)
    {
        Fields(v.body, v.accent, v.emission);
    }
    void Value(Building &v)
    {
        Fields(v.x, v.y, v.width, v.depth, v.height, v.type, v.palette);
    }
    void Value(MovementFacing &v)
    {
        Value(v.yaw);
        v.walking = false;
    }
    void Value(BodyImpact &v)
    {
        Fields(v.velocity, v.seconds, v.invulnerability, v.response);
    }
    void Value(ResidentWaypoint &v)
    {
        Fields(v.position, v.crossing);
    }
    void Value(Resident &v)
    {
        Fields(v.position, v.home, v.destination, v.name, v.dialogue, v.type, v.coat, v.facing, v.gender,
               v.hair, v.cityIndex, v.profession, v.activity, v.route, v.routeCursor, v.crossingJunction,
               v.routineStep, v.speed, v.waitSeconds, v.conversationSeconds, v.blockedSeconds, v.randomState,
               v.destinationIsWork, v.health, v.fearSeconds, v.corpseSeconds, v.impact);
    }
    void Value(QuestParticipation &v)
    {
        Value(v.states);
    }
    void Value(DialogueChoice &v)
    {
        Fields(v.id, v.text);
    }
    void Value(DialogueLine &v)
    {
        Fields(v.id, v.speaker, v.channel, v.text, v.choices);
    }
    void Value(DialogueRecord &v)
    {
        Fields(v.phase, v.nextPage, v.choice, v.pendingChoice);
    }
    void Value(DialogueState &v)
    {
        Fields(v.history, v.scenes, v.events);
    }
    void Value(UiPreferences &v)
    {
        Fields(v.background, v.border, v.content);
    }
    void Value(GameState::MapViewState &v)
    {
        Fields(v.zoom, v.pan);
    }
    void Value(MapDestination &v)
    {
        Fields(v.active, v.position);
    }
    void Value(world::RelayNetwork &v)
    {
        Fields(v.worldKey, v.discovered, v.registered, v.visited);
    }
    void Value(GameState &v)
    {
        Fields(v.surfaceExploration, v.mainQuestStage, v.missions, v.homeMessageRead, v.isInsideHome,
               v.isInObservatory, v.isBikeMounted, v.bikeYaw, v.mineReturnPosition, v.gateSignalHeard,
               v.hasDescendedObservatory, v.mapViews, v.planetMapRange, v.planetMapPan, v.planetDestination,
               v.dialogue, v.uiPreferences, v.mapDestinations, v.isComplete, v.isSideQuestAccepted,
               v.isPowerRestored, v.isSideQuestComplete, v.playTimeSeconds, v.animationTimeSeconds,
               v.facingDirection, v.facingYaw, v.playerPosition, v.playerGender, v.playerHair, v.buildings,
               v.residents, v.relayNetwork, v.selectedRelay, v.mapRegisteredRelaysOnly, v.planetMapSurface,
               v.relayPage);
    }
    void Value(combat::PlayerLoadout &v)
    {
        Fields(v.pistolOwned, v.selected);
        Require(v.selected != combat::WeaponSlot::Primary &&
                    (v.pistolOwned || v.selected == combat::WeaponSlot::Unarmed),
                "소유하지 않은 무기가 선택된 저장입니다.");
    }
    void Value(combat::FirearmSettings &v)
    {
        Fields(v.magazineSize, v.damage, v.fireInterval, v.reloadSeconds, v.bulletSpeed, v.range,
               v.minimumSpread, v.maximumSpread, v.spreadPerShot, v.spreadRecovery);
    }
    void Value(combat::Firearm &v)
    {
        Value(v.settings);
        Fields(v.rounds, v.cooldown, v.reloadRemaining, v.spread, v.randomState);
        Require(v.settings.magazineSize > 0 && v.settings.magazineSize <= 1024 && v.rounds >= 0 &&
                    v.rounds <= v.settings.magazineSize && v.settings.reloadSeconds > 0 &&
                    v.reloadRemaining >= 0 && v.reloadRemaining <= v.settings.reloadSeconds &&
                    v.settings.fireInterval > 0 && v.settings.bulletSpeed > 0 && v.settings.range > 0 &&
                    v.settings.damage > 0 && v.settings.minimumSpread >= 0 &&
                    v.settings.maximumSpread >= v.settings.minimumSpread && v.spread >= 0 &&
                    v.spread <= v.settings.maximumSpread && v.cooldown >= 0,
                "저장된 무기 상태가 올바르지 않습니다.");
    }
    void Value(combat::Pistol &v)
    {
        Value(static_cast<combat::Firearm &>(v));
    }
    void Value(combat::SecurityCarbine &v)
    {
        Value(static_cast<combat::Firearm &>(v));
    }
    void Value(game::MineQuest &v)
    {
        Fields(v.stage, v.riaJoined);
    }
    void Value(game::PlayerStats &v)
    {
        Fields(v.experience, v.level, v.maxHealth, v.health, v.attack, v.defense, v.recoveryKits, v.salvage,
               v.credits);
    }
    void Value(game::Enemy &v)
    {
        Fields(v.position, v.health, v.attackCooldown, v.respawnSeconds, v.facing, v.target);
    }
    void Value(game::Drop &v)
    {
        Fields(v.position, v.kind, v.actorId);
    }
    void Value(game::MineLight &v)
    {
        Fields(v.position, v.radius, v.needsPower);
    }
    void Value(game::RescueGroup &v)
    {
        Fields(v.position, v.combatants, v.rescued);
    }
    void Value(game::NpcCombatState &v)
    {
        Fields(v.health, v.cooldown, v.hitGrace, v.downedSeconds, v.engaged, v.evading, v.aimYaw, v.pistol);
    }
    void Value(game::RescueNpc &v)
    {
        Fields(v.id, v.group, v.position, v.evacuating, v.walkedDistance, v.outsideSeconds, v.routeIndex,
               v.route, v.facing, v.gender, v.hair, v.combat);
    }
    void Value(game::MineState &v)
    {
        Fields(v.active, v.generated, v.powerBypassed, v.signalRecorded, v.glyphRecorded,
               v.coordinatesRecorded, v.mapRevision, v.seed, v.quest, v.player, v.loadout, v.pistol, v.tiles,
               v.explored, v.reachableCells, v.navigationDistances, v.enemies, v.drops, v.lights,
               v.rescueGroups, v.survivors, v.entry, v.riaPosition, v.riaRoute, v.riaRouteIndex,
               v.riaNavigationCooldown, v.riaFacing, v.riaCombat, v.invulnerability, v.scannerCooldown,
               v.kills, v.rescuedPeople, v.returnedPeople, v.totalEnemyDeaths, v.nextDropActorId);
    }
    void Value(game::FrontierPerson &v)
    {
        Fields(v.position, v.facing, v.combat, v.route, v.waypoint, v.arrived, v.departureDelay);
    }
    void Value(game::ChapterOneState &v)
    {
        Fields(v.unlocked, v.active, v.generated, v.observedRecords, v.convoyEvacuated, v.convoyRewardClaimed,
               v.generatorInspected, v.generatorRestored, v.portableLightRepaired, v.convoyBriefed,
               v.receiverInspected, v.escortStarted, v.convoyRescued, v.surveyMarkers, v.kainJoined,
               v.vehicleRepaired, v.tradeProofFound, v.hostile, v.negotiated, v.ledgerFound,
               v.detailLedgerFound, v.comparisonStarted, v.recordsCompared, v.waterwayUnlocked,
               v.comparisonSeconds, v.invulnerability, v.tiles, v.surfaceObstacles, v.structures, v.survivors,
               v.scrappers, v.ria, v.kain);
    }
    void Value(JunctionSignal &v)
    {
        Fields(v.phase, v.elapsed, v.walkCycle);
    }
    void Value(TrafficVehicle &v)
    {
        Fields(v.id, v.lane, v.nextLane, v.distance, v.speed, v.turnDistance, v.turnLength, v.turn,
               v.position, v.previousPosition, v.yaw, v.previousYaw, v.stoppedSeconds, v.yieldingRight,
               v.reservedFrom, v.reservedTo, v.health, v.occupied, v.wreckSeconds, v.impactCooldown,
               v.hitFlash);
    }
    void Value(TrafficSystem &v)
    {
        Fields(v.vehicles, v.signals, v.randomState, v.nextId, v.accumulator, v.spawnTimer, v.impactCooldown,
               v.cityFilter);
        const auto &net = RoadNetwork::Get();
        Require(v.cityFilter >= -1 && v.cityFilter < Overworld::kCityCount &&
                    v.vehicles.size() <= TrafficSystem::kVehicleLimit &&
                    (v.signals.empty() || v.signals.size() == net.junctions.size()),
                "저장된 교통망이 현재 도로와 맞지 않습니다.");
        std::set<std::uint64_t> ids;
        for (const auto &car : v.vehicles)
        {
            Require(car.lane >= 0 && car.lane < int(net.lanes.size()) && car.nextLane >= -1 &&
                        car.nextLane < int(net.lanes.size()) && car.reservedFrom >= -1 &&
                        car.reservedFrom < int(net.lanes.size()) && car.reservedTo >= -1 &&
                        car.reservedTo < int(net.lanes.size()) && ids.insert(car.id).second &&
                        car.id < v.nextId && car.turnLength > 0,
                    "저장된 차량의 차선/ID가 올바르지 않습니다.");
        }
    }
    void Value(PlanetCities &v)
    {
        Fields(v.activeCity, v.traffic);
        Require(v.activeCity >= -1 && v.activeCity < Overworld::kCityCount,
                "저장된 활성 도시 ID가 올바르지 않습니다.");
    }
    void Value(AircraftActor &v)
    {
        Fields(v.id, v.route, v.direction, v.type, v.alternate, v.parameter, v.speed, v.altitude, v.yaw,
               v.bank, v.position, v.health, v.occupied, v.wreckSeconds, v.fallSpeed);
    }
    void Value(AirTraffic &v)
    {
        Fields(v.aircraft, v.randomState, v.nextId, v.spawnTimer);
        Require(v.aircraft.size() <= AirTraffic::kAircraftLimit, "저장된 비행체가 너무 많습니다.");
        std::set<std::uint64_t> ids;
        for (const auto &craft : v.aircraft)
        {
            Require(craft.route >= 0 && craft.route < int(RoadNetwork::Get().airCorridors.size()) &&
                        (craft.direction == 1 || craft.direction == -1) && ids.insert(craft.id).second &&
                        craft.id < v.nextId,
                    "저장된 비행 경로/ID가 올바르지 않습니다.");
        }
    }
    void Value(HospitalSite &v)
    {
        Fields(v.city, v.position);
    }
    void Value(SecurityOfficer &v)
    {
        Fields(v.id, v.position, v.lastKnown, v.facing, v.tier, v.health, v.cooldown, v.navigationSeconds,
               v.corpseSeconds, v.impact, v.weapon, v.weaponTier, v.route, v.waypoint);
    }
    void Value(CivicState &v)
    {
        Fields(v.heat, v.infamy, v.sinceCrime, v.unseenSeconds, v.spawnSeconds, v.dischargeCooldown,
               v.playerGrace, v.wanted, v.lastCrime, v.landmarkHealth, v.landmarkRecovery, v.officers,
               v.hospitals, v.nextOfficer, v.playerImpact, v.hospital, v.deaths, v.lostLevels, v.lostCredits,
               v.lostSalvage, v.pursuitSeconds, v.initialUnseenSeconds, v.pendingInfamy, v.responseStarted,
               v.identified, v.randomState);
    }
    void Value(PlanetTrafficActor &v)
    {
        Fields(v.id, v.routeId, v.airborne, v.occupied, v.collisionReady, v.direction, v.health,
               v.distanceMeters, v.speedMeters, v.altitudeMeters, v.position, v.heading, v.bank,
               v.wreckSeconds, v.fallSpeed, v.farSeconds);
    }
    void Value(PlanetTrafficState &v)
    {
        Fields(v.worldKey, v.manifestDigest, v.nextId, v.randomState, v.spawnSeconds);
        const auto count = Count(v.actors.size());
        Require(count <= PlanetTraffic::GroundLimit + PlanetTraffic::AirLimit,
                "저장된 행성 교통 개체 수가 상한을 초과했습니다.");
        if (reading)
        {
            v.actors.resize(size_t(count));
        }
        for (auto &actor : v.actors)
        {
            Value(actor);
        }
    }
    void TrafficRecords(Campaign &v)
    {
        std::uint64_t count = v.worlds.size();
        Value(count);
        Require(count == v.worlds.size(), "행성 교통 확장의 월드 수가 일치하지 않습니다.");
        if (reading)
        {
            std::set<std::string> keys;
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                PlanetTrafficState state;
                Fields(key, state);
                Require(v.worlds.count(key) && keys.insert(key).second,
                        "행성 교통 확장의 월드 ID가 올바르지 않습니다.");
                v.worlds.at(key).transit = std::move(state);
            }
        }
        else
        {
            for (auto &entry : v.worlds)
            {
                auto key = entry.first;
                Fields(key, entry.second.transit);
            }
        }
    }
    void Value(PlanetCompanion &v)
    {
        Fields(v.joined, v.place, v.position, v.heading, v.combat);
        // Walking/targets/muzzle flashes and navigation paths restart from the physical position.
        v.walking = false;
        if (reading)
        {
            v.combat.engaged = v.combat.evading = false;
            v.combat.muzzleFlash = 0;
        }
    }
    void Value(PlanetPartyState &v)
    {
        Fields(v.worldKey, v.members);
    }
    void PartyRecords(Campaign &v)
    {
        std::uint64_t count = v.worlds.size();
        Value(count);
        Require(count == v.worlds.size(), "동료 확장의 월드 수가 일치하지 않습니다.");
        if (reading)
        {
            std::set<std::string> keys;
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                PlanetPartyState state;
                Fields(key, state);
                Require(v.worlds.count(key) && keys.insert(key).second,
                        "동료 확장의 월드 ID가 올바르지 않습니다.");
                v.worlds.at(key).party = std::move(state);
            }
        }
        else
        {
            for (auto &entry : v.worlds)
            {
                auto key = entry.first;
                Fields(key, entry.second.party);
            }
        }
    }
    void Value(PlanetOfficer &v)
    {
        Fields(v.id, v.position, v.lastKnown, v.heading, v.jurisdiction);
    }
    void Value(PlanetSecurityState &v)
    {
        Fields(v.worldKey, v.frame, v.incident, v.search);
        const auto count = Count(v.officers.size());
        Require(count <= 16, "행성 보안대 수가 상한을 초과했습니다.");
        if (reading)
        {
            v.officers.resize(size_t(count));
        }
        for (auto &officer : v.officers)
        {
            Value(officer);
        }
    }
    void SecurityRecords(Campaign &v)
    {
        std::uint64_t count = v.worlds.size();
        Value(count);
        Require(count == v.worlds.size(), "치안 확장의 월드 수가 다릅니다.");
        std::set<std::string> keys;
        if (reading)
        {
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                PlanetSecurityState state;
                Fields(key, state);
                Require(v.worlds.count(key) && keys.insert(key).second,
                        "치안 확장의 월드 ID가 올바르지 않습니다.");
                v.worlds.at(key).security = std::move(state);
            }
        }
        else
        {
            for (auto &entry : v.worlds)
            {
                auto key = entry.first;
                Fields(key, entry.second.security);
            }
        }
    }
    void Value(PlanetTrafficLink &v)
    {
        Fields(v.actor, v.vehicle, v.city);
    }
    void Value(std::vector<PlanetTrafficLink> &v)
    {
        const auto count = Count(v.size());
        Require(count <= PlanetTraffic::GroundLimit, "차량 인계 기록이 너무 많습니다.");
        if (reading)
        {
            v.resize(size_t(count));
        }
        for (auto &link : v)
        {
            Value(link);
        }
    }
    void LinkRecords(Campaign &v)
    {
        std::uint64_t count = v.worlds.size();
        Value(count);
        Require(count == v.worlds.size(), "차량 인계 확장의 월드 수가 다릅니다.");
        std::set<std::string> keys;
        if (reading)
        {
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                std::vector<PlanetTrafficLink> links;
                Fields(key, links);
                Require(v.worlds.count(key) && keys.insert(key).second,
                        "차량 인계 확장의 월드 ID가 올바르지 않습니다.");
                v.worlds.at(key).trafficLinks = std::move(links);
            }
        }
        else
        {
            for (auto &entry : v.worlds)
            {
                auto key = entry.first;
                Fields(key, entry.second.trafficLinks);
            }
        }
    }
    void Value(CommunityResident &v)
    {
        Fields(v.id, v.appearance, v.path, v.position, v.heading, v.distanceMeters, v.speed, v.fearSeconds,
               v.hitFlash, v.corpseSeconds, v.direction, v.health);
    }
    void Value(CommunitySite &v)
    {
        Fields(v.regionId, v.contact, v.target, v.task);
        const auto count = Count(v.residents.size());
        Require(count <= PlanetCommunity::ResidentsPerSite, "현장 주민 수가 상한을 초과했습니다.");
        if (reading)
        {
            v.residents.resize(size_t(count));
        }
        for (auto &person : v.residents)
        {
            Value(person);
        }
    }
    void Value(PlanetCommunityState &v)
    {
        Value(v.worldKey);
        const auto count = Count(v.sites.size());
        Require(count <= PlanetCommunity::SiteLimit, "현장 거점 수가 상한을 초과했습니다.");
        if (reading)
        {
            v.sites.resize(size_t(count));
        }
        for (auto &site : v.sites)
        {
            Value(site);
        }
    }
    void CommunityRecords(Campaign &v)
    {
        std::uint64_t count = v.worlds.size();
        Value(count);
        Require(count == v.worlds.size(), "지역 활동 확장의 월드 수가 다릅니다.");
        std::set<std::string> keys;
        if (reading)
        {
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                bool legacy = false;
                PlanetCommunityState community;
                Fields(key, legacy, community);
                Require(v.worlds.count(key) && keys.insert(key).second,
                        "지역 활동 확장의 월드 ID가 올바르지 않습니다.");
                v.worlds.at(key).community = std::move(community);
                v.worlds.at(key).legacyLeftTraffic = legacy;
            }
        }
        else
        {
            for (auto &entry : v.worlds)
            {
                auto key = entry.first;
                Fields(key, entry.second.legacyLeftTraffic, entry.second.community);
            }
        }
    }
    void Value(ActivityTrackingState &v)
    {
        Fields(v.mission, v.communityWorldKey, v.communityRegion);
        Require(v.communityWorldKey.size() <= 512 && v.communityRegion.size() <= 512,
                "활동 추적 식별자가 허용 길이를 초과했습니다.");
    }
    void Tracking(Checkpoint &v)
    {
        auto tracking = reading ? ActivityTrackingState{}
                                : ActivityJournal::CaptureTracking(v.game, v.community, v.planet);
        Value(tracking);
        if (reading)
        {
            ActivityJournal::RestoreTracking(v.game, tracking, v.community, v.planet);
        }
    }
    void TrackingRecords(Campaign &v)
    {
        std::uint64_t count = v.worlds.size();
        Value(count);
        Require(count == v.worlds.size(), "활동 추적 확장의 월드 수가 다릅니다.");
        std::set<std::string> keys;
        if (reading)
        {
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                Value(key);
                Require(v.worlds.count(key) && keys.insert(key).second,
                        "활동 추적 확장의 월드 ID가 올바르지 않습니다.");
                Tracking(v.worlds.at(key));
            }
        }
        else
        {
            for (auto &entry : v.worlds)
            {
                auto key = entry.first;
                Value(key);
                Tracking(entry.second);
            }
        }
    }
    void MobilityRecords(Campaign &v)
    {
        std::uint64_t count = v.worlds.size();
        Value(count);
        Require(count == v.worlds.size(), "기동 상태 확장의 월드 수가 다릅니다.");
        std::set<std::string> keys;
        auto entry = v.worlds.begin();
        for (std::uint64_t i = 0; i < count; ++i)
        {
            std::string key = reading ? std::string{} : (entry++)->first;
            Value(key);
            Require(v.worlds.count(key) && keys.insert(key).second,
                    "기동 상태 확장의 월드 ID가 올바르지 않습니다.");
            auto &mobility = v.worlds.at(key).game.playerMotion.mobility;
            float cooldown = reading ? 0.f : mobility.Cooldown();
            Value(cooldown);
            Require(std::isfinite(cooldown) && cooldown >= 0 &&
                        cooldown <= PlayerMobility::DodgeSeconds + PlayerMobility::RecoverySeconds,
                    "저장된 회피 대기 시간이 올바르지 않습니다.");
            if (reading)
            {
                mobility.RestoreCooldown(cooldown);
            }
        }
    }
    void Value(Checkpoint &v)
    {
        Fields(v.planet, v.layout, v.digest, v.pose, v.origin, v.bike, v.bikeHeading, v.mineReturn, v.game,
               v.mine, v.chapter, v.civic, v.cities, v.flights);
    }
    void Value(Campaign &v)
    {
        Value(v.active);
        std::uint64_t count = reading ? 0 : v.worlds.size();
        Value(count);
        Require(count > 0 && count <= world::PlanetProfiles::All().size(),
                "저장된 행성 수가 올바르지 않습니다.");
        if (reading)
        {
            v.worlds.clear();
            for (std::uint64_t i = 0; i < count; ++i)
            {
                std::string key;
                Checkpoint checkpoint;
                Fields(key, checkpoint);
                Require(v.worlds.emplace(std::move(key), std::move(checkpoint)).second,
                        "행성 기록이 중복되어 있습니다.");
            }
        }
        else
        {
            for (auto &entry : v.worlds)
            {
                auto key = entry.first;
                Fields(key, entry.second);
            }
        }
    }

  private:
    bool reading;
    std::vector<unsigned char> bytes;
    size_t offset = 0;
    std::uint64_t elements = 0;
};

namespace
{
using G = world::SurfaceGeometry;
void Validate(const Checkpoint &c)
{
    c.planet.Validate();
    Require(world::PlanetProfiles::Supported(c.planet), "지원하지 않거나 버전이 다른 행성 프로필입니다.");
    const bool story = c.planet.storyPlanet;
    PlanetTraffic::Validate(c.transit, c.planet, c.digest);
    PlanetParty::Validate(c.party, c.planet);
    PlanetSecurity::Validate(c.security, c.civic, c.planet);
    SaveArchive::ValidateTrafficLinks(c);
    PlanetCommunity::Validate(c.community, c.planet);
    if (!c.party.worldKey.empty() && c.planet.storyPlanet)
    {
        Require(c.party.members[0].joined == c.mine.quest.IsRiaJoined() &&
                    c.party.members[1].joined == c.chapter.kainJoined,
                "동료의 합류 기록과 임무 기록이 다릅니다.");
        for (const auto &member : c.party.members)
        {
            Require(!member.joined || member.place != CompanionPlace::Mine || c.mine.generated,
                    "동료가 있는 광산 데이터가 없습니다.");
            Require(!member.joined || member.place != CompanionPlace::Frontier || c.chapter.generated,
                    "동료가 있는 개척지 데이터가 없습니다.");
        }
    }
    Require(world::ContentLayout::SupportsRevision(c.layout),
            "도시 배치 버전이 다릅니다. 원본 저장을 보존하고 같은 버전으로 불러와 주세요.");
    G::Validate(c.pose);
    G::Validate(c.origin);
    Require(G::Distance(c.planet.radiusMeters, c.pose.origin, c.origin.origin) <= 130,
            "저장된 월드 원점이 올바르지 않습니다.");
    if (c.bike)
    {
        G::Validate(*c.bike);
    }
    if (c.mineReturn)
    {
        G::Validate(*c.mineReturn);
    }
    if (c.bikeHeading)
    {
        Require(std::fabs(G::Dot(*c.bikeHeading, *c.bikeHeading) - 1) < 1.e-6,
                "바이크 방향 기록이 올바르지 않습니다.");
    }
    const auto &w = c.game;
    const auto &m = c.mine;
    const auto &f = c.chapter;
    Require(w.surfaceExploration && !(w.isInsideHome && w.isInObservatory) &&
                c.mineReturn.has_value() == m.active &&
                (!m.active || (!w.isInsideHome && !w.isInObservatory && !w.isBikeMounted)),
            "저장된 공간/층 상태가 올바르지 않습니다.");
    Require(m.player.health > 0 && m.player.health <= m.player.maxHealth && m.player.maxHealth <= 100000 &&
                m.player.level >= 1 && m.player.level <= 1000 && m.player.experience >= 0 &&
                m.player.credits >= 0 && m.player.credits <= game::PlayerStats::kMaximumCredits &&
                m.player.salvage >= 0 && m.player.salvage <= 9999 && m.player.recoveryKits >= 0 &&
                m.player.recoveryKits <= 999,
            "저장된 성장/보유품 상태가 올바르지 않습니다.");
    Require(m.player.attack >= 0 && m.player.attack <= 10000 && m.player.defense >= 0 &&
                m.player.defense <= 10000,
            "저장된 전투 능력치가 올바르지 않습니다.");
    Require(w.residents.size() == (story ? size_t(Overworld::ResidentPopulation()) : 0),
            "저장된 주민 수가 현재 도시 생성 규칙과 다릅니다.");
    Require(w.buildings.size() <= 4096, "저장된 건물 수가 허용 범위를 초과했습니다.");
    for (int city = 0; story && city < Overworld::kCityCount; ++city)
    {
        Require(std::count_if(w.residents.begin(), w.residents.end(),
                              [&](const auto &resident) { return resident.cityIndex == city; }) ==
                    Overworld::ResidentPopulation(city),
                "도시별 주민 배치가 현재 생성 규칙과 다릅니다.");
    }
    Require(w.planetMapRange >= 64 &&
                w.planetMapRange <= world::SurfaceAtlas::MaximumRange(c.planet.radiusMeters) + 1 &&
                std::hypot(w.planetMapPan.x, w.planetMapPan.y) <= G::Pi * c.planet.radiusMeters + 1024 &&
                w.relayPage >= 0 && w.relayPage <= 65536,
            "저장된 지도 범위가 올바르지 않습니다.");
    if (w.planetDestination)
    {
        G::Validate(*w.planetDestination);
    }
    Require(w.uiPreferences.background >= .8f && w.uiPreferences.background <= 1 &&
                w.uiPreferences.border >= .8f && w.uiPreferences.border <= 1 &&
                w.uiPreferences.content >= .85f && w.uiPreferences.content <= 1,
            "저장된 UI 설정 범위가 올바르지 않습니다.");
    for (const auto &view : w.mapViews)
    {
        Require(view.zoom >= .25f && view.zoom <= 32, "저장된 현장 지도 확대 값이 올바르지 않습니다.");
    }
    if (!m.active && !w.isInsideHome && !w.isInObservatory)
    {
        auto p = G::ToLocal(c.origin, c.pose.origin, c.planet.radiusMeters);
        Require(std::hypot(p.x - w.playerPosition.x * 2, p.y - w.playerPosition.y * 2) < .2,
                "플레이어 좌표가 저장된 행성 위치와 다릅니다.");
    }
    const auto &net = RoadNetwork::Get();
    for (const auto &r : w.residents)
    {
        Require(r.cityIndex >= 0 && r.cityIndex < Overworld::kCityCount && r.routeCursor <= r.route.size() &&
                    r.crossingJunction >= -1 && r.crossingJunction < int(net.junctions.size()) &&
                    r.health >= 0 && r.health <= 100,
                "저장된 주민 상태가 올바르지 않습니다.");
        for (const auto &point : r.route)
        {
            Require(point.crossing >= -1 && point.crossing < int(net.junctions.size()),
                    "주민 횡단 경로가 현재 도로와 맞지 않습니다.");
        }
    }
    if (m.generated)
    {
        const size_t cells = game::kMapSize * game::kMapSize;
        Require(m.tiles.size() == cells && m.explored.size() == cells &&
                    m.navigationDistances.size() == cells && m.enemies.size() == game::kEnemyPopulation &&
                    m.survivors.size() <= 1024 && m.rescueGroups.size() <= 128 &&
                    m.riaRouteIndex <= m.riaRoute.size(),
                "광산 현장 크기/인원 계약이 다릅니다.");
        for (auto cell : m.reachableCells)
        {
            Require(cell >= 0 && size_t(cell) < cells, "광산 경로 인덱스가 올바르지 않습니다.");
        }
        for (auto cell : m.navigationDistances)
        {
            Require(cell >= -1 && cell < int(cells), "광산 길 찾기 값이 올바르지 않습니다.");
        }
        for (auto fog : m.explored)
        {
            Require(fog <= 1, "광산 탐험 안개 값이 올바르지 않습니다.");
        }
        std::set<std::uint64_t> ids;
        for (const auto &npc : m.survivors)
        {
            Require(npc.group < m.rescueGroups.size() && npc.routeIndex <= npc.route.size() &&
                        ids.insert(npc.id).second,
                    "광산 구조 인원 기록이 올바르지 않습니다.");
        }
        ids.clear();
        for (const auto &drop : m.drops)
        {
            Require(ids.insert(drop.actorId).second && drop.actorId < m.nextDropActorId,
                    "광산 물자 ID 기록이 올바르지 않습니다.");
        }
        if (m.active)
        {
            Require(w.playerPosition.x >= 0 && w.playerPosition.y >= 0 &&
                        w.playerPosition.x < game::kMapSize && w.playerPosition.y < game::kMapSize,
                    "광산 내 플레이어 좌표가 올바르지 않습니다.");
            int x = int(std::floor(w.playerPosition.x)), y = int(std::floor(w.playerPosition.y));
            Require(x >= 0 && y >= 0 && x < game::kMapSize && y < game::kMapSize &&
                        m.tiles[size_t(y * game::kMapSize + x)] != game::Tile::Wall,
                    "광산 내 플레이어 위치가 올바르지 않습니다.");
        }
    }
    else
    {
        Require(!m.active && m.tiles.empty() && m.enemies.empty() && m.survivors.empty(),
                "생성되지 않은 광산에 현장 데이터가 있습니다.");
    }
    if (story)
    {
        Require(f.generated && f.tiles.size() == game::kFrontierWidth * game::kFrontierHeight &&
                    f.surfaceObstacles.size() == f.tiles.size() && f.scrappers.size() == 3 &&
                    f.survivors.size() == 5,
                "개척지 현장 계약이 다릅니다.");
    }
    else
    {
        Require(w.buildings.empty() && !w.isInsideHome && !w.isInObservatory && !c.mineReturn &&
                    !m.generated && !m.active && !m.quest.IsRiaJoined() && !f.generated && !f.active &&
                    f.tiles.empty() && f.survivors.empty() && f.scrappers.empty() &&
                    w.dialogue.scenes.empty() && w.dialogue.events.empty() && w.dialogue.history.empty() &&
                    !w.homeMessageRead && !w.gateSignalHeard && !w.hasDescendedObservatory &&
                    !w.isSideQuestAccepted && !w.isPowerRestored && !w.isSideQuestComplete && !w.isComplete &&
                    !f.kainJoined,
                "일반 행성에 에르마 전용 현장/인물/진행 기록이 섞여 있습니다.");
        for (int i = 0; i < int(MissionId::Count); ++i)
        {
            Require(w.missions.Status(MissionId(i)) == MissionStatus::NotAccepted,
                    "다른 행성에 에르마 임무가 복제되어 있습니다.");
        }
    }
    Require(f.surveyMarkers >= 0 && f.surveyMarkers <= 3 && (f.observedRecords & ~31u) == 0,
            "개척지 조사 기록이 올바르지 않습니다.");
    auto person = [](const game::FrontierPerson &p) {
        Require(p.waypoint <= p.route.size(), "현장 인원의 이동 경로가 올바르지 않습니다.");
    };
    person(f.ria);
    person(f.kain);
    for (const auto &p : f.survivors)
    {
        person(p);
    }
    for (const auto &p : f.scrappers)
    {
        person(p);
    }
    Require(c.civic.wanted >= 0 && c.civic.wanted <= 5 && c.civic.heat >= 0 &&
                c.civic.heat <= CivicJustice::kMaximumHeat && c.civic.infamy >= 0 && c.civic.infamy <= 100 &&
                c.civic.officers.size() <= 512 &&
                c.civic.hospitals.size() == (story ? Overworld::kCityCount : 0),
            "저장된 치안 상태가 올바르지 않습니다.");
    for (const auto &p : c.civic.officers)
    {
        Require(p.tier >= 1 && p.tier <= 5 && p.waypoint <= p.route.size(),
                "보안대 상태가 올바르지 않습니다.");
    }
    std::set<std::uint64_t> officerIds;
    for (const auto &p : c.civic.officers)
    {
        Require(p.id < c.civic.nextOfficer && officerIds.insert(p.id).second,
                "보안대 고유 ID가 올바르지 않습니다.");
    }
    Require(story ? (c.civic.hospital >= 0 && c.civic.hospital < int(c.civic.hospitals.size()))
                  : c.civic.hospital == 0,
            "저장된 의료소 ID가 올바르지 않습니다.");
    for (const auto &hospital : c.civic.hospitals)
    {
        Require(hospital.city >= 0 && hospital.city < Overworld::kCityCount,
                "저장된 의료소 관할이 올바르지 않습니다.");
    }
}
std::uint64_t Checksum(const std::vector<unsigned char> &bytes)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : bytes)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}
struct FileHandle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    ~FileHandle()
    {
        if (value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
        }
    }
};
std::vector<unsigned char> ReadBytes(const std::filesystem::path &path)
{
    FileHandle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    Require(file.value != INVALID_HANDLE_VALUE, "저장 파일을 열 수 없습니다.");
    LARGE_INTEGER size{};
    Require(GetFileSizeEx(file.value, &size) && size.QuadPart >= 40 &&
                size.QuadPart <= static_cast<LONGLONG>(kMaximumFile + 40),
            "저장 파일 크기가 올바르지 않습니다.");
    std::vector<unsigned char> bytes(size_t(size.QuadPart));
    DWORD read = 0;
    Require(ReadFile(file.value, bytes.data(), DWORD(bytes.size()), &read, nullptr) && read == bytes.size(),
            "저장 파일을 끝까지 읽지 못했습니다.");
    return bytes;
}
struct Envelope
{
    unsigned schema = 0, layout = 0;
    std::vector<unsigned char> payload;
};
Envelope ReadEnvelope(const std::filesystem::path &path)
{
    auto bytes = ReadBytes(path);
    SaveArchive header(true, std::vector<unsigned char>(bytes.begin(), bytes.begin() + 40));
    unsigned char magic[8]{};
    header.Raw(magic, 8);
    const unsigned char expected[8] = {'S', 'G', 'W', 'O', 'R', 'L', 'D', 0};
    Require(std::memcmp(magic, expected, 8) == 0, "지원하지 않는 저장 파일 형식입니다.");
    unsigned schema = 0, layout = 0;
    std::uint64_t size = 0, checksum = 0;
    header.Fields(schema, layout, size, checksum);
    Require(schema > 0, "저장 형식 버전이 올바르지 않습니다.");
    Require(size == bytes.size() - 40, "저장 파일의 길이 정보가 올바르지 않습니다.");
    std::vector<unsigned char> payload(bytes.begin() + 40, bytes.end());
    Require(Checksum(payload) == checksum, "저장 파일의 무결성 검사에 실패했습니다.");
    return {schema, layout, std::move(payload)};
}
void ValidateCampaign(const Campaign &records)
{
    Require(!records.worlds.empty() && records.worlds.size() <= world::PlanetProfiles::All().size() &&
                records.worlds.count(records.active) &&
                records.worlds.count(WorldKey(world::WorldCatalog::Erma())),
            "저장된 활성 행성 또는 에르마 귀환 기록이 없습니다.");
    for (const auto &entry : records.worlds)
    {
        Require(entry.first == WorldKey(entry.second.planet), "행성 기록의 식별자가 일치하지 않습니다.");
        Validate(entry.second);
    }
}
Campaign Decode(const std::filesystem::path &path)
{
    auto envelope = ReadEnvelope(path);
    Require(envelope.schema >= 1 && envelope.schema <= kSchema,
            "저장 형식 버전이 다릅니다. 원본 파일은 변경하지 않았습니다.");
    Require(world::ContentLayout::SupportsRevision(envelope.layout),
            "저장된 도시 배치 버전이 현재 게임과 다릅니다.");
    std::vector<unsigned char> mobilityExtension;
    if (envelope.schema >= 9)
    {
        Require(envelope.payload.size() >= 8, "기동 상태 확장의 길이 정보가 없습니다.");
        SaveArchive footer(true,
                           std::vector<unsigned char>(envelope.payload.end() - 8, envelope.payload.end()));
        std::uint64_t size = 0;
        footer.Value(size);
        Require(size > 0 && size <= envelope.payload.size() - 8,
                "기동 상태 확장의 길이가 올바르지 않습니다.");
        const size_t start = envelope.payload.size() - 8 - size_t(size);
        mobilityExtension.assign(envelope.payload.begin() + start, envelope.payload.end() - 8);
        envelope.payload.resize(start);
    }
    std::vector<unsigned char> trackingExtension;
    if (envelope.schema >= 8)
    {
        Require(envelope.payload.size() >= 8, "활동 추적 확장의 길이 정보가 없습니다.");
        SaveArchive footer(true,
                           std::vector<unsigned char>(envelope.payload.end() - 8, envelope.payload.end()));
        std::uint64_t size = 0;
        footer.Value(size);
        Require(size > 0 && size <= envelope.payload.size() - 8,
                "활동 추적 확장의 길이가 올바르지 않습니다.");
        const size_t start = envelope.payload.size() - 8 - size_t(size);
        trackingExtension.assign(envelope.payload.begin() + start, envelope.payload.end() - 8);
        envelope.payload.resize(start);
    }
    std::vector<unsigned char> communityExtension;
    if (envelope.schema >= 7)
    {
        Require(envelope.payload.size() >= 8, "지역 활동 확장의 길이 정보가 없습니다.");
        SaveArchive footer(true,
                           std::vector<unsigned char>(envelope.payload.end() - 8, envelope.payload.end()));
        std::uint64_t size = 0;
        footer.Value(size);
        Require(size > 0 && size <= envelope.payload.size() - 8,
                "지역 활동 확장의 길이가 올바르지 않습니다.");
        const size_t start = envelope.payload.size() - 8 - size_t(size);
        communityExtension.assign(envelope.payload.begin() + start, envelope.payload.end() - 8);
        envelope.payload.resize(start);
    }
    std::vector<unsigned char> linkExtension;
    if (envelope.schema >= 6)
    {
        Require(envelope.payload.size() >= 8, "차량 인계 확장의 길이 정보가 없습니다.");
        SaveArchive footer(true,
                           std::vector<unsigned char>(envelope.payload.end() - 8, envelope.payload.end()));
        std::uint64_t size = 0;
        footer.Value(size);
        Require(size > 0 && size <= envelope.payload.size() - 8,
                "차량 인계 확장의 길이가 올바르지 않습니다.");
        const size_t start = envelope.payload.size() - 8 - size_t(size);
        linkExtension.assign(envelope.payload.begin() + start, envelope.payload.end() - 8);
        envelope.payload.resize(start);
    }
    std::vector<unsigned char> securityExtension;
    if (envelope.schema >= 5)
    {
        Require(envelope.payload.size() >= 8, "치안 확장의 길이 정보가 없습니다.");
        SaveArchive footer(true,
                           std::vector<unsigned char>(envelope.payload.end() - 8, envelope.payload.end()));
        std::uint64_t size = 0;
        footer.Value(size);
        Require(size > 0 && size <= envelope.payload.size() - 8, "치안 확장의 길이가 올바르지 않습니다.");
        const size_t start = envelope.payload.size() - 8 - size_t(size);
        securityExtension.assign(envelope.payload.begin() + start, envelope.payload.end() - 8);
        envelope.payload.resize(start);
    }
    std::vector<unsigned char> partyExtension;
    if (envelope.schema >= 4)
    {
        Require(envelope.payload.size() >= 8, "동료 확장의 길이 정보가 없습니다.");
        SaveArchive footer(true,
                           std::vector<unsigned char>(envelope.payload.end() - 8, envelope.payload.end()));
        std::uint64_t size = 0;
        footer.Value(size);
        Require(size > 0 && size <= envelope.payload.size() - 8, "동료 확장의 길이가 올바르지 않습니다.");
        const size_t start = envelope.payload.size() - 8 - size_t(size);
        partyExtension.assign(envelope.payload.begin() + start, envelope.payload.end() - 8);
        envelope.payload.resize(start);
    }
    std::vector<unsigned char> extension;
    if (envelope.schema >= 3)
    {
        Require(envelope.payload.size() >= 8, "행성 교통 확장의 길이 정보가 없습니다.");
        SaveArchive footer(true,
                           std::vector<unsigned char>(envelope.payload.end() - 8, envelope.payload.end()));
        std::uint64_t size = 0;
        footer.Value(size);
        Require(size > 0 && size <= envelope.payload.size() - 8,
                "행성 교통 확장의 길이가 올바르지 않습니다.");
        const size_t start = envelope.payload.size() - 8 - size_t(size);
        extension.assign(envelope.payload.begin() + start, envelope.payload.end() - 8);
        envelope.payload.resize(start);
    }
    SaveArchive reader(true, std::move(envelope.payload));
    Campaign result;
    if (envelope.schema == 1)
    {
        Checkpoint previous;
        reader.Value(previous);
        Require(previous.planet.storyPlanet, "이전 저장 형식은 에르마만 지원합니다.");
        result.active = WorldKey(previous.planet);
        result.worlds.emplace(result.active, std::move(previous));
    }
    else
    {
        reader.Value(result);
    }
    Require(reader.Finished(), "저장 파일 끝에 지원하지 않는 데이터가 있습니다.");
    if (envelope.schema >= 3)
    {
        SaveArchive traffic(true, std::move(extension));
        traffic.TrafficRecords(result);
        Require(traffic.Finished(), "행성 교통 확장에 지원하지 않는 데이터가 있습니다.");
    }
    if (envelope.schema >= 4)
    {
        SaveArchive party(true, std::move(partyExtension));
        party.PartyRecords(result);
        Require(party.Finished(), "동료 확장에 지원하지 않는 데이터가 있습니다.");
    }
    if (envelope.schema >= 5)
    {
        SaveArchive security(true, std::move(securityExtension));
        security.SecurityRecords(result);
        Require(security.Finished(), "치안 확장에 지원하지 않는 데이터가 있습니다.");
    }
    if (envelope.schema >= 6)
    {
        SaveArchive links(true, std::move(linkExtension));
        links.LinkRecords(result);
        Require(links.Finished(), "차량 인계 확장에 지원하지 않는 데이터가 있습니다.");
        for (auto &entry : result.worlds)
        {
            SaveArchive::ApplyTrafficLinks(entry.second);
        }
    }
    if (envelope.schema >= 7)
    {
        SaveArchive community(true, std::move(communityExtension));
        community.CommunityRecords(result);
        Require(community.Finished(), "지역 활동 확장에 지원하지 않는 데이터가 있습니다.");
    }
    else
    {
        for (auto &entry : result.worlds)
        {
            entry.second.legacyLeftTraffic = true;
        }
    }
    if (envelope.schema >= 8)
    {
        SaveArchive tracking(true, std::move(trackingExtension));
        tracking.TrackingRecords(result);
        Require(tracking.Finished(), "활동 추적 확장에 지원하지 않는 데이터가 있습니다.");
    }
    if (envelope.schema >= 9)
    {
        SaveArchive mobility(true, std::move(mobilityExtension));
        mobility.MobilityRecords(result);
        Require(mobility.Finished(), "기동 상태 확장에 지원하지 않는 데이터가 있습니다.");
    }
    ValidateCampaign(result);
    return result;
}
std::filesystem::path Backup(const std::filesystem::path &path)
{
    auto backup = path;
    backup += L".bak";
    return backup;
}
void WriteAtomic(const std::filesystem::path &path, const std::vector<unsigned char> &bytes)
{
    std::error_code error;
    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    Require(!error, "저장 폴더를 만들 수 없습니다.");
    auto temp = path;
    temp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    auto backupTemp = temp;
    backupTemp += L".bak";
    try
    {
        {
            FileHandle file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr)};
            Require(file.value != INVALID_HANDLE_VALUE, "임시 저장 파일을 만들 수 없습니다.");
            DWORD written = 0;
            Require(WriteFile(file.value, bytes.data(), DWORD(bytes.size()), &written, nullptr) &&
                        written == bytes.size() && FlushFileBuffers(file.value),
                    "저장 데이터를 디스크에 기록하지 못했습니다.");
        }
        bool validOld = false;
        try
        {
            auto envelope = ReadEnvelope(path);
            // Preserve intact older/newer version files too; unreadable corruption must not replace a good
            // backup.
            if ((envelope.schema >= 1 && envelope.schema <= kSchema) &&
                world::ContentLayout::SupportsRevision(envelope.layout))
            {
                Decode(path);
            }
            validOld = true;
        }
        catch (const std::exception &)
        {
        }
        if (validOld)
        {
            Require(CopyFileW(path.c_str(), backupTemp.c_str(), FALSE) != FALSE,
                    "이전 저장의 백업을 만들지 못했습니다.");
            if (!MoveFileExW(backupTemp.c_str(), Backup(path).c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::filesystem::remove(backupTemp, error);
                throw std::runtime_error("이전 저장의 백업 교체에 실패했습니다.");
            }
        }
        Require(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
                    FALSE,
                "저장 파일을 교체하지 못했습니다. 이전 파일은 유지합니다.");
    }
    catch (...)
    {
        std::filesystem::remove(temp, error);
        std::filesystem::remove(backupTemp, error);
        throw;
    }
}
void MigrateParty(Checkpoint &c, const world::GeneratedPlanet &plan)
{
    if (!c.party.worldKey.empty())
    {
        return;
    }
    c.party.worldKey = world::WorldIdentity::ContentKey(c.planet, "overworld", "party_v1");
    if (!c.planet.storyPlanet)
    {
        return;
    }
    for (size_t i = 0; i < c.party.members.size(); ++i)
    {
        auto &member = c.party.members[i];
        member.joined = i == 0 ? c.mine.quest.IsRiaJoined() : c.chapter.kainJoined;
        if (!member.joined)
        {
            continue;
        }
        // Formats 1-3 never had a mine Kain. Recover the actor actually shown in that format.
        const bool mine = i == 0 && c.mineReturn.has_value();
        const bool field = !mine && c.chapter.active;
        member.place = mine    ? CompanionPlace::Mine
                       : field ? CompanionPlace::Frontier
                               : CompanionPlace::Surface;
        const auto &region = plan.Region(mine ? "mq00_mine" : field ? "frontier" : "halo_07");
        Vector2 position, origin;
        float yaw = .78539816f;
        if (mine)
        {
            position = c.mine.riaPosition;
            origin = c.mine.entry;
            yaw = c.mine.riaFacing.yaw;
            member.combat = c.mine.riaCombat;
        }
        else if (field)
        {
            const auto &person = i == 0 ? c.chapter.ria : c.chapter.kain;
            position = person.position;
            origin = game::kFrontierEntry;
            yaw = person.facing.yaw;
            member.combat = person.combat;
        }
        else
        {
            position = i == 0 ? kLiaPosition : kCityKainPosition;
            origin = Overworld::Cities()[0].center;
            member.combat.health = c.civic.landmarkHealth[i == 0 ? 0 : 2];
            member.combat.downedSeconds = member.combat.health <= 0 ? 12.f : 0.f;
        }
        member.combat.downedSeconds = (std::max)(0.f, member.combat.downedSeconds);
        member.position =
            G::FromLocal(region.frame, {(position.x - origin.x) * 2, (position.y - origin.y) * 2, 0},
                         c.planet.radiusMeters);
        if (!mine)
        {
            const auto *core =
                plan.cityCores ? plan.cityCores->Find(field ? "frontier" : "halo_07") : nullptr;
            member.position.altitudeMeters =
                core ? core->elevationMeters : plan.terrain.Height(member.position);
        }
        member.heading =
            G::Unit({region.frame.right.x * std::cos(yaw) + region.frame.forward.x * std::sin(yaw),
                     region.frame.right.y * std::cos(yaw) + region.frame.forward.y * std::sin(yaw),
                     region.frame.right.z * std::cos(yaw) + region.frame.forward.z * std::sin(yaw)});
    }
}
void ValidateRegistry(const Checkpoint &c, const world::GeneratedPlanet &plan)
{
    Require(c.layout == plan.layoutRevision, "저장과 생성된 월드의 배치 버전이 다릅니다.");
    auto communityPlan = std::shared_ptr<const world::GeneratedPlanet>(&plan, [](const auto *) {});
    PlanetCommunity community(c.community);
    community.Bind(std::move(communityPlan));
    world::RelayNetwork expected;
    expected.Bind(c.planet);
    Require(c.game.relayNetwork.WorldKey() == expected.WorldKey(),
            "중계 기록의 월드 ID가 일치하지 않습니다.");
    for (const auto &id : c.game.relayNetwork.Discoveries())
    {
        Require(world::RelayNetwork::Find(plan, id) != nullptr, "존재하지 않는 중계환 발견 기록입니다.");
    }
    for (const auto &id : c.game.relayNetwork.Registrations())
    {
        Require(c.game.relayNetwork.Discovered(id), "발견되지 않은 중계환 등록 기록입니다.");
    }
    for (const auto &id : c.game.relayNetwork.VisitedRegions())
    {
        plan.Region(id);
    }
    Require(c.game.selectedRelay.empty() || world::RelayNetwork::Find(plan, c.game.selectedRelay),
            "선택한 중계환이 현재 월드에 없습니다.");
    for (const auto &link : c.trafficLinks)
    {
        const auto &region = plan.Region(Overworld::Cities()[size_t(link.city)].id);
        const auto *core = plan.cityCores ? plan.cityCores->Find(region.definition.id) : nullptr;
        Require(core != nullptr, "차량을 소유한 도시의 도로 블록이 없습니다.");
        const auto &cars = c.cities.Traffic(link.city).Vehicles();
        const auto car =
            std::find_if(cars.begin(), cars.end(), [&](const auto &v) { return v.id == link.vehicle; });
        const auto actor = std::find_if(c.transit.actors.begin(), c.transit.actors.end(),
                                        [&](const auto &v) { return v.id == link.actor; });
        Require(car != cars.end() && actor != c.transit.actors.end(), "저장된 차량 인계 대상이 없습니다.");
        const auto center = Overworld::Cities()[size_t(link.city)].center;
        auto expected = G::FromLocal(region.frame,
                                     {(car->position.x - center.x) * 2, (car->position.y - center.y) * 2, 0},
                                     c.planet.radiusMeters);
        expected.altitudeMeters = Game::Distance(car->position, center) * 2 <= core->radiusMeters
                                      ? core->elevationMeters
                                      : plan.terrain.Height(expected);
        Require(G::Distance(c.planet.radiusMeters, expected, actor->position) <= .25 &&
                    std::abs(expected.altitudeMeters - actor->position.altitudeMeters) <= .25 &&
                    std::abs(actor->speedMeters - car->speed * 2) <= .01 &&
                    std::abs(actor->wreckSeconds - car->wreckSeconds) <= .01,
                "도시 차량과 전역 차량의 실제 위치/속도/잔해 시간이 다릅니다.");
    }
    for (const auto &member : c.party.members)
    {
        if (!member.joined || member.place == CompanionPlace::Surface)
        {
            continue;
        }
        const bool mine = member.place == CompanionPlace::Mine;
        auto local = G::ToLocal(plan.Region(mine ? "mq00_mine" : "frontier").frame, member.position,
                                c.planet.radiusMeters);
        const double x = (mine ? c.mine.entry.x : game::kFrontierEntry.x) + local.x / 2,
                     y = (mine ? c.mine.entry.y : game::kFrontierEntry.y) + local.y / 2;
        Require(x >= 0 && y >= 0 && x < (mine ? game::kMapSize : game::kFrontierWidth) &&
                    y < (mine ? game::kMapSize : game::kFrontierHeight),
                "동료 위치가 소속 현장의 범위를 벗어났습니다.");
        if (mine)
        {
            Require(c.mine.tiles[size_t(int(y) * game::kMapSize + int(x))] != game::Tile::Wall,
                    "광산 동료 위치가 벽 내부에 있습니다.");
        }
    }
}
void Prepare(PendingLoad &load)
{
    auto &c = load.checkpoint;
    auto generator = world::PlanetProfiles::Generator(
        c.planet, c.planet.storyPlanet ? Overworld::StoryAnchors() : std::vector<world::StoryAnchor>{});
    generator = generator.WithLayoutRevision(c.layout);
    if (c.planet.storyPlanet)
    {
        struct BorrowChapter
        {
            game::ChapterOneState &live;
            game::ChapterOneState previous;
            BorrowChapter() : live(game::GameLevels::Chapter().GetState()), previous(std::move(live))
            {
            }
            ~BorrowChapter()
            {
                live = std::move(previous);
            }
        } borrowed;
        borrowed.live = c.chapter;
        generator = generator.WithCityCores(PlanetCities::Describe(c.game));
    }
    load.session = std::make_unique<world::SurfaceSession>(c.planet, c.pose.origin);
    load.session->Start(std::move(generator));
}
void CarryTraveler(const Checkpoint &source, Checkpoint &target)
{
    // Ownership and growth travel with the player; each planet owns its own people, sites and wanted
    // response.
    target.mine.player = source.mine.player;
    target.mine.loadout = source.mine.loadout;
    target.mine.pistol = source.mine.pistol;
    target.game.playerGender = source.game.playerGender;
    target.game.playerHair = source.game.playerHair;
    target.game.uiPreferences = source.game.uiPreferences;
    target.game.playTimeSeconds = source.game.playTimeSeconds;
    target.civic.infamy = source.civic.infamy;
    target.civic.deaths = source.civic.deaths;
    target.civic.lostLevels = source.civic.lostLevels;
    target.civic.lostCredits = source.civic.lostCredits;
    target.civic.lostSalvage = source.civic.lostSalvage;
}
void FreezeInput()
{
    WeaponWheel::Close();
    auto &s = Game::Get().GetState();
    s.StopMotion();
    s.mouseAiming = s.mouseFiring = false;
    std::fill(std::begin(s.pressedKeys), std::end(s.pressedKeys), false);
}
} // namespace

// ============================================================================
// SaveSystem — disk replacement and asynchronous, failure-preserving world restoration
// ============================================================================
std::filesystem::path SaveSystem::DefaultPath()
{
    wchar_t directory[32768]{};
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, DWORD(std::size(directory)));
    if (size > 0 && size < std::size(directory))
    {
        return std::filesystem::path(directory) / L"GameSoftware" / L"Saves" / L"world.sav";
    }
    size = GetModuleFileNameW(nullptr, directory, DWORD(std::size(directory)));
    if (size > 0 && size < std::size(directory))
    {
        return std::filesystem::path(directory).parent_path() / L"Saves" / L"world.sav";
    }
    return std::filesystem::path(L"Saves") / L"world.sav";
}
bool SaveSystem::Loading()
{
    return bool(pending);
}
const std::string &SaveSystem::Message()
{
    return message;
}
void SaveSystem::Refresh()
{
    std::error_code error;
    available = std::filesystem::exists(DefaultPath(), error) ||
                std::filesystem::exists(Backup(DefaultPath()), error);
}
bool SaveSystem::Available()
{
    static bool checked = false;
    if (!checked)
    {
        Refresh();
        checked = true;
    }
    return available;
}
const char *SaveSystem::Restriction()
{
    auto &g = Game::Get();
    const auto &s = g.GetState();
    if (Loading())
    {
        return "월드를 불러오는 중입니다.";
    }
    if (!s.surfaceExploration || !g.Surface() || !g.Surface()->Ready() ||
        (g.IsStoryWorld() && !g.SurfaceCities()))
    {
        return "저장은 행성 탐사를 시작한 뒤 사용할 수 있습니다.";
    }
    if (g.SurfaceCities() && g.SurfaceCities()->Observing())
    {
        return "도시 물리 갱신을 마친 뒤 저장할 수 있습니다.";
    }
    if (game::GameLevels::Chapter().Surface().Observing())
    {
        return "개척지 물리 갱신을 마친 뒤 저장할 수 있습니다.";
    }
    if (HospitalRecovery::Active() || game::GameLevels::Mine().GetState().player.health <= 0)
    {
        return "의료 이송을 마친 뒤 저장해 주세요.";
    }
    if (Dialogue::Active() || s.dialogue.suspended || !s.dialogue.pending.empty())
    {
        return "진행 중이거나 중단한 대화를 마친 뒤 저장해 주세요.";
    }
    return nullptr;
}
bool SaveSystem::CheckFile(const std::filesystem::path &path)
{
    try
    {
        Decode(path);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}
bool SaveSystem::Save(const std::filesystem::path &path)
{
    if (auto reason = Restriction())
    {
        message = reason;
        Game::Get().ShowNotice(message);
        return false;
    }
    try
    {
        auto &g = Game::Get();
        auto c = SaveArchive::Capture();
        Validate(c);
        ValidateRegistry(c, *g.surface->Stream().Snapshot()->planet);
        auto candidate = campaign;
        candidate.active = WorldKey(c.planet);
        candidate.worlds.insert_or_assign(candidate.active, std::move(c));
        ValidateCampaign(candidate);
        SaveArchive body(false);
        body.Value(candidate);
        SaveArchive extension(false);
        extension.TrafficRecords(candidate);
        SaveArchive footer(false);
        std::uint64_t extensionSize = extension.Bytes().size();
        footer.Value(extensionSize);
        auto payload = body.Bytes();
        payload.insert(payload.end(), extension.Bytes().begin(), extension.Bytes().end());
        payload.insert(payload.end(), footer.Bytes().begin(), footer.Bytes().end());
        SaveArchive party(false), partyFooter(false);
        party.PartyRecords(candidate);
        std::uint64_t partySize = party.Bytes().size();
        partyFooter.Value(partySize);
        payload.insert(payload.end(), party.Bytes().begin(), party.Bytes().end());
        payload.insert(payload.end(), partyFooter.Bytes().begin(), partyFooter.Bytes().end());
        SaveArchive security(false), securityFooter(false);
        security.SecurityRecords(candidate);
        std::uint64_t securitySize = security.Bytes().size();
        securityFooter.Value(securitySize);
        payload.insert(payload.end(), security.Bytes().begin(), security.Bytes().end());
        payload.insert(payload.end(), securityFooter.Bytes().begin(), securityFooter.Bytes().end());
        SaveArchive links(false), linkFooter(false);
        links.LinkRecords(candidate);
        std::uint64_t linkSize = links.Bytes().size();
        linkFooter.Value(linkSize);
        payload.insert(payload.end(), links.Bytes().begin(), links.Bytes().end());
        payload.insert(payload.end(), linkFooter.Bytes().begin(), linkFooter.Bytes().end());
        SaveArchive community(false), communityFooter(false);
        community.CommunityRecords(candidate);
        std::uint64_t communitySize = community.Bytes().size();
        communityFooter.Value(communitySize);
        payload.insert(payload.end(), community.Bytes().begin(), community.Bytes().end());
        payload.insert(payload.end(), communityFooter.Bytes().begin(), communityFooter.Bytes().end());
        SaveArchive tracking(false), trackingFooter(false);
        tracking.TrackingRecords(candidate);
        std::uint64_t trackingSize = tracking.Bytes().size();
        trackingFooter.Value(trackingSize);
        payload.insert(payload.end(), tracking.Bytes().begin(), tracking.Bytes().end());
        payload.insert(payload.end(), trackingFooter.Bytes().begin(), trackingFooter.Bytes().end());
        SaveArchive mobility(false), mobilityFooter(false);
        mobility.MobilityRecords(candidate);
        std::uint64_t mobilitySize = mobility.Bytes().size();
        mobilityFooter.Value(mobilitySize);
        payload.insert(payload.end(), mobility.Bytes().begin(), mobility.Bytes().end());
        payload.insert(payload.end(), mobilityFooter.Bytes().begin(), mobilityFooter.Bytes().end());
        Require(payload.size() <= kMaximumFile, "저장 데이터가 허용 크기를 초과했습니다.");
        SaveArchive header(false);
        unsigned char magic[8] = {'S', 'G', 'W', 'O', 'R', 'L', 'D', 0};
        header.Raw(magic, 8);
        unsigned schema = kSchema, layout = world::ContentLayout::Revision;
        std::uint64_t size = payload.size(), checksum = Checksum(payload);
        header.Fields(schema, layout, size, checksum);
        auto bytes = header.Bytes();
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        WriteAtomic(path, bytes);
        campaign = std::move(candidate);
        Refresh();
        message = g.state.relayPreparing ? "월드 저장 완료 / 진행 중인 중계 이동은 출발 위치로 복원됩니다."
                                         : "월드 저장 완료 / 현재 위치·현장·발견·진행 기록을 보관했습니다.";
        g.ShowNotice(message);
        return true;
    }
    catch (const std::exception &error)
    {
        message = std::string("저장 실패 / ") + error.what();
        Game::Get().ShowNotice(message);
        return false;
    }
}
bool SaveSystem::Load(const std::filesystem::path &path)
{
    if (Loading())
    {
        return false;
    }
    if (auto cities = Game::Get().SurfaceCities(); cities && cities->Observing())
    {
        message = "도시 물리 갱신을 마친 뒤 불러올 수 있습니다.";
        return false;
    }
    if (HospitalRecovery::Active())
    {
        message = "의료 이송을 마친 뒤 불러와 주세요.";
        return false;
    }
    try
    {
        auto load = std::make_unique<PendingLoad>();
        try
        {
            load->candidate = Decode(path);
        }
        catch (const std::exception &primary)
        {
            const auto reason = std::string(primary.what());
            try
            {
                load->candidate = Decode(Backup(path));
                load->backup = true;
            }
            catch (const std::exception &)
            {
                throw std::runtime_error(reason + " 이전 백업도 불러올 수 없습니다.");
            }
        }
        load->checkpoint = load->candidate.worlds.at(load->candidate.active);
        Prepare(*load);
        pending = std::move(load);
        FreezeInput();
        message = pending->backup ? "이전 백업 확인 / 원본은 유지하고 월드를 준비합니다. Esc 취소"
                                  : "저장 확인 완료 / 같은 시드의 월드와 도착 지형을 준비합니다. Esc 취소";
        return true;
    }
    catch (const std::exception &error)
    {
        message = std::string("불러오기 실패 / ") + error.what();
        Game::Get().ShowNotice(message);
        return false;
    }
}
void SaveSystem::ResetCampaign()
{
    Cancel();
    campaign = {};
    message.clear();
}
bool SaveSystem::SwitchPlanet(std::string_view id)
{
    if (auto reason = Restriction())
    {
        message = reason;
        Game::Get().ShowNotice(message);
        return false;
    }
    try
    {
        auto profile = world::PlanetProfiles::Find(id);
        Require(profile != nullptr, "알 수 없는 행성 프로필입니다.");
        auto &g = Game::Get();
        Require(!profile->SameWorld(g.surface->Planet()), "현재 탐사 중인 행성입니다.");
        Require(!g.IsSurfaceMine() && !g.state.isInsideHome && !g.state.isInObservatory &&
                    !g.state.isBikeMounted && !g.state.relayPreparing,
                "지상에서 바이크를 내리고 중계 이동을 마친 뒤 행성을 선택해 주세요.");
        auto source = SaveArchive::Capture();
        Validate(source);
        ValidateRegistry(source, *g.surface->Stream().Snapshot()->planet);
        auto load = std::make_unique<PendingLoad>();
        load->switching = true;
        load->candidate = campaign;
        load->candidate.worlds.insert_or_assign(WorldKey(source.planet), source);
        const auto key = WorldKey(*profile);
        const auto previous = load->candidate.worlds.find(key);
        if (previous != load->candidate.worlds.end())
        {
            load->checkpoint = previous->second;
        }
        else
        {
            Require(!profile->storyPlanet, "에르마 출발 기록이 없습니다. 탐사를 다시 시작해 주세요.");
            load->fresh = true;
            auto &c = load->checkpoint;
            c.planet = *profile;
            c.pose = c.origin = G::Frame(G::FromDegrees(0, 0));
            c.game.surfaceExploration = true;
            c.game.isInsideHome = false;
            c.game.relayNetwork.Bind(*profile);
        }
        CarryTraveler(source, load->checkpoint);
        load->candidate.active = key;
        Prepare(*load);
        pending = std::move(load);
        FreezeInput();
        message = profile->name + " 탐사 준비 / 현재 월드를 보관합니다. Esc 취소";
        return true;
    }
    catch (const std::exception &error)
    {
        message = std::string("행성 선택 실패 / ") + error.what();
        Game::Get().ShowNotice(message);
        return false;
    }
}
void SaveSystem::Cancel()
{
    if (!pending)
    {
        return;
    }
    pending.reset();
    message = "월드 준비를 취소했습니다. 현재 행성과 저장 파일은 유지합니다.";
}
void SaveSystem::Update()
{
    if (!pending)
    {
        return;
    }
    try
    {
        auto &load = *pending;
        auto &c = load.checkpoint;
        load.session->Update();
        const auto snapshot = load.session->Stream().Snapshot();
        if (!snapshot->statistics.error.empty())
        {
            throw std::runtime_error(c.planet.name + " 월드 준비 실패 / " + snapshot->statistics.error);
        }
        Require(std::chrono::steady_clock::now() - load.began < std::chrono::seconds(30),
                "월드 준비 시간이 초과됐습니다. 현재 게임은 유지합니다.");
        if (!load.session->Ready())
        {
            return;
        }
        if (!load.validated)
        {
            if (load.fresh)
            {
                const auto &landing = snapshot->planet->Region("landing_zone");
                Require(load.session->Relocate(landing.access.position, .32),
                        "행성 착륙 지점이 막혀 있습니다.");
                c.pose = load.session->Pose();
                c.origin = load.session->Origin();
                c.game.playerPosition = {};
                c.game.cameraPosition = {};
                c.digest = snapshot->planet->Digest();
                // A local test bike is parked on a checked pad, never copied from Erma's coordinates.
                for (unsigned step = 0; step < 8 && !c.bike; ++step)
                {
                    double angle = step * G::Pi / 4;
                    auto bike =
                        G::Advance(c.pose, 3 * std::cos(angle), 3 * std::sin(angle), c.planet.radiusMeters)
                            .origin;
                    if (load.session->LandingClear(bike, .65))
                    {
                        bike.altitudeMeters = load.session->Stream().Contact(bike).heightMeters;
                        c.bike = bike;
                        c.bikeHeading = c.pose.right;
                        c.game.bikeYaw = 0;
                    }
                }
                Require(c.bike.has_value(), "착륙 거점의 바이크 주차 지점을 확보하지 못했습니다.");
                c.game.relayNetwork.Observe(*snapshot->planet, load.session->Location());
            }
            Require(
                snapshot->planet->Digest() == c.digest,
                "현재 생성기/건물·도로 리소스로 저장된 배치를 재현할 수 없습니다. 원본 저장은 유지합니다.");
            MigrateParty(c, *snapshot->planet);
            PlanetSecurity::Migrate(c.security, c.civic, c.planet, c.origin);
            Validate(c);
            if (c.legacyLeftTraffic)
            {
                PlanetTraffic::MigrateLanes(c.transit, snapshot->planet);
                c.legacyLeftTraffic = false;
            }
            ValidateRegistry(c, *snapshot->planet);
            load.community = std::make_unique<PlanetCommunity>(c.community);
            load.community->Bind(snapshot->planet);
            c.community = load.community->State();
            load.traffic = std::make_unique<PlanetTraffic>(c.transit);
            load.traffic->Bind(snapshot->planet);
            c.transit = load.traffic->State();
            load.party = std::make_unique<PlanetParty>(c.party);
            load.party->Bind(snapshot->planet);
            load.validated = true;
        }
        if (!load.session->Stream().Detailed(c.pose.origin, 96))
        {
            return;
        }
        if (!load.restored)
        {
            Require(load.session->RestorePose(c.pose, c.origin, c.game.isBikeMounted),
                    "저장 위치의 지형/충돌 조건이 맞지 않습니다.");
            load.restored = true;
        }
        // Allocate before commit. Moving the prepared containers keeps owned level references stable.
        auto cities = c.planet.storyPlanet ? std::make_unique<PlanetCities>(c.cities) : nullptr;
        auto approaches = c.planet.storyPlanet ? FacilityApproaches::Prepare(c.game) : FacilityApproachPlan{};
        auto infrastructure =
            c.planet.storyPlanet ? CityInfrastructure::Prepare(c.game, approaches) : CityInfrastructurePlan{};
        load.candidate.worlds.insert_or_assign(load.candidate.active, c);
        ValidateCampaign(load.candidate);
        auto ready = std::move(pending);
        auto &record = ready->checkpoint;
        auto &g = Game::Get();
        const int width = g.state.viewportWidth, height = g.state.viewportHeight,
                  lastTick = g.state.lastUpdateMilliseconds;
        g.StopSurface();
        game::SceneManager::Reset();
        game::GameLevels::Reset();
        WeaponWheel::Close();
        g.state = std::move(record.game);
        FacilityApproaches::Publish(std::move(approaches));
        CityInfrastructure::Publish(std::move(infrastructure));
        CityResidents::Get().RefreshSidewalks();
        game::GameLevels::Mine().GetState() = std::move(record.mine);
        game::GameLevels::Chapter().GetState() = std::move(record.chapter);
        CivicJustice::State() = std::move(record.civic);
        PlanetSecurity::State() = std::move(record.security);
        Overworld::Flights() = std::move(record.flights);
        g.surface = std::move(ready->session);
        campaign = std::move(ready->candidate);
        g.surfaceCities = std::move(cities);
        g.surfaceTraffic = std::move(ready->traffic);
        g.surfaceParty = std::move(ready->party);
        g.surfaceCommunity = std::move(ready->community);
        g.surfaceBike = record.bike;
        g.surfaceBikeHeading = record.bikeHeading;
        g.surfaceMineReturn = record.mineReturn;
        // Preflight migrated old records before commit. Publish before any local cache can be captured.
        g.surfaceParty->Publish();
        g.surfaceParty->Capture();
        g.surfaceParty->Update(0);
        g.state.viewportWidth = width;
        g.state.viewportHeight = height;
        g.state.lastUpdateMilliseconds = lastTick;
        g.state.StopMotion();
        g.state.cameraPosition = g.state.playerPosition;
        g.state.surfaceCameraHeight =
            (!g.IsSurfaceMine() && !g.state.isInsideHome && !g.state.isInObservatory)
                ? float(g.surface->Local(g.surface->Pose().origin).z / 2)
                : 0;
        g.state.bikeSafePoseValid = false;
        if (g.surfaceBike &&
            G::Distance(g.surface->Planet().radiusMeters, *g.surfaceBike, g.surface->Pose().origin) < 1000)
        {
            const auto bike = g.surface->Local(*g.surfaceBike);
            g.state.bikePosition = {float(bike.x / 2), float(bike.y / 2)};
            g.state.bikeElevation = float(bike.z / 2);
        }
        else
        {
            g.state.bikePosition = {1000000, 1000000};
        }
        g.RestoreSurfaceBikeHeading();
        g.state.ui = {};
        g.state.isPaused = g.state.mapVisible = g.state.journalVisible = g.state.dialogueHistoryVisible =
            false;
        g.frameSchedule = {};
        if (g.surfaceCities)
        {
            g.surfaceCities->Update(g.state, *g.surface, 0);
        }
        game::GameLevels::Chapter().Surface().Refresh();
        PlanetSecurity::Project();
        message = ready->switching ? g.surface->Planet().name +
                                         " 탐사 시작 / 행성별 현장 기록을 유지합니다. 디스크 보관은 F5"
                  : ready->backup  ? "백업 월드 복원 완료 / 원본 파일은 변경하지 않았습니다."
                                   : "월드 복원 완료 / 현장·장비·기록을 이어서 진행합니다.";
        g.ShowNotice(message);
    }
    catch (const std::exception &error)
    {
        pending.reset();
        message = std::string("불러오기 실패 / ") + error.what();
        Game::Get().ShowNotice(message);
    }
}
} // namespace tutorial
