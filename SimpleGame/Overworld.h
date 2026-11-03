#pragma once
#include "Game.h"
#include "WorldGeneration.h"
#include <array>
#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace tutorial
{
// ============================================================================
// PlanetParty — one physical actor per companion; levels only borrow a local projection
// ============================================================================
enum class CompanionId : std::uint8_t
{
    Ria,
    Kain
};
enum class CompanionPlace : std::uint8_t
{
    Surface,
    Mine,
    Frontier
};
struct PlanetCompanion
{
    bool joined = false;
    CompanionPlace place = CompanionPlace::Surface;
    world::SurfacePoint position;
    world::Vector3d heading = {0, 1, 0};
    combat::NpcCombatState combat;
    bool walking = false;
};
struct PlanetPartyState
{
    std::string worldKey;
    std::array<PlanetCompanion, 2> members;
};
class PlanetParty
{
  public:
    explicit PlanetParty(PlanetPartyState state = {});
    void Bind(std::shared_ptr<const world::GeneratedPlanet> planet);
    const PlanetPartyState &State() const
    {
        return state;
    }
    bool Joined(CompanionId id) const;
    bool Owns(CompanionId id, CompanionPlace place) const;
    bool Damage(CompanionId id, int amount, bool playerOwned);
    void Defend(CompanionId id, float yaw);
    // Capture before changing scene ownership; publish only to the current owner's adapter.
    void Capture();
    void Publish();
    void Update(float seconds);
    InteractionTarget Nearby(float *distance = nullptr) const;
    bool Blocked(const world::SurfacePoint &point, double clearance, int except = -1) const;
    std::array<bool, 2> NearbyTravelers() const;
    void Arrive(const std::array<bool, 2> &travelers);
    static void Validate(const PlanetPartyState &state, const world::PlanetDefinition &planet);

  private:
    void Join();
    world::SurfacePoint MinePoint(Vector2 local) const;
    Vector2 MineLocal(const world::SurfacePoint &point) const;
    world::TangentFrame Frame(CompanionPlace place) const;
    void ChangeOwner(size_t index, CompanionPlace place, const world::SurfacePoint &position);
    bool Clear(const world::SurfacePoint &point, int except) const;
    PlanetPartyState state;
    std::shared_ptr<const world::GeneratedPlanet> planet;
    std::array<float, 2> routeCooldown = {};
};

struct CityInfo
{
    const char *id;
    const char *name;
    Vector2 center;
    const char *mood;
    const char *rumor;
    world::SettlementTier tier = world::SettlementTier::District;
};

// ============================================================================
// RoadNetwork — shared geometry for road surfaces, lanes, clearance and the map
// ============================================================================
struct RoadJunction
{
    Vector2 position;
    float radius = 3;
    std::vector<int> outgoing;
    float altitude = 0;
};
struct RoadSegment
{
    int from, to, lanes; // Total lanes in both directions: 2, 4 or 6.
    float speedLimit;
};
struct TrafficLane
{
    int road, from, to, slot;
    Vector2 start, end, direction;
    float length, speedLimit;
};
struct AirCorridor
{
    Vector2 start, control, end;
    float altitude; // World units, independent of the ground road graph.
};
enum class TrafficTurn
{
    Straight,
    Left,
    Right
};
class RoadNetwork
{
  public:
    static constexpr float kLaneWidth = 1.5f; // Transit kit: 3 m per lane, never stretched.
    static const RoadNetwork &Get();
    std::vector<RoadJunction> junctions;
    std::vector<RoadSegment> roads;
    std::vector<TrafficLane> lanes;
    std::vector<AirCorridor> airCorridors;
    bool Contains(Vector2 position, float margin = 0) const;
    bool ContainsFootprint(Vector2 position, float margin = 0) const;
    bool ContainsSurface(Vector2 position, float height, float margin = 0) const;
    Vector2 LanePosition(int lane, float distance) const;
    float RoadElevation(int road, float fraction) const;
    float DriveElevation(Vector2 position, float previousHeight) const;
    bool CanTurn(int lane, TrafficTurn turn) const;
    static Vector2 RightOf(Vector2 direction);
    bool IsSignalized(int junction) const;
    int SignalAxis(int lane, int junction = -1) const;

  private:
    RoadNetwork();
    struct Cell
    {
        std::vector<int> roads, junctions;
    };
    std::map<std::pair<int, int>, Cell> cells;
};

// ============================================================================
// TrafficSystem — local population, protected turns, crossings and collisions
// ============================================================================
enum class TrafficLight
{
    Red,
    Amber,
    Green
};
enum class SignalPhase
{
    MainThrough,
    MainAmber,
    MainClear,
    MainLeft,
    MainLeftAmber,
    MainLeftClear,
    SideThrough,
    SideAmber,
    SideClear,
    SideLeft,
    SideLeftAmber,
    SideLeftClear,
    Walk,
    WalkClear
};
struct JunctionSignal
{
    SignalPhase phase = SignalPhase::MainThrough;
    float elapsed = 0;
    unsigned int walkCycle = 0;
};
struct TrafficVehicle
{
    std::uint64_t id = 0;
    int lane = 0, nextLane = -1;
    float distance = 0, speed = 0, turnDistance = -1, turnLength = 1;
    TrafficTurn turn = TrafficTurn::Straight;
    Vector2 position = {}, previousPosition = {};
    float yaw = 0, previousYaw = 0;
    float stoppedSeconds = 0;
    bool yieldingRight = false;
    int reservedFrom = -1, reservedTo = -1;
    int health = 140;
    bool occupied = true;
    float wreckSeconds = 0, impactCooldown = 0, hitFlash = 0;
    std::uint64_t globalId = 0; // Nonzero only while borrowing a PlanetTraffic actor.
    bool departed = false;      // A same-frame ownership transfer, never saved as a second car.
};
class TrafficSystem
{
    friend class SaveArchive;
    friend class PlanetTraffic;

  public:
    static constexpr size_t kVehicleLimit = 112;
    static constexpr float kDespawnDistance = 155;
    void Reset();
    void SetCityFilter(int city);
    static bool CoreLane(int lane, int city);
    void Update(GameState &world, float deltaSeconds);
    // A hidden city retains routes/reservations; only temporary states and wreck lifetime advance.
    size_t AdvanceDormant(float deltaSeconds);
    const std::vector<TrafficVehicle> &Vehicles() const
    {
        return vehicles;
    }
    const std::vector<JunctionSignal> &Signals() const
    {
        return signals;
    }
    Vector2 RenderPosition(size_t index) const;
    float RenderYaw(size_t index) const;
    float RenderElevation(size_t index) const;
    bool DamageVehicle(std::uint64_t id, int damage, bool playerOwned = true);
    void StopForImpact(std::uint64_t id);
    TrafficLight Light(int lane, TrafficTurn turn) const;
    bool PedestrianGreen(int junction, int road = -1) const;
    unsigned int PedestrianGroups(int junction) const;
    int CrossingRoad(int junction, Vector2 position) const;
    bool HasRightTurnSignal(int lane) const;
    bool CanStartCrossing(int junction, float travelSeconds, int road = -1) const;
    float PhaseDuration(int junction) const;
    bool CrossingOccupied(const GameState &world, int junction) const;
    int NearbyCrossing(Vector2 position) const;
    static bool OutsideView(Vector2 position, Vector2 camera, float padding = 180, float altitude = 0);
    static bool SweptHit(Vector2 from, Vector2 to, float yaw, Vector2 pedestrian, float padding = .25f);

  private:
    void Step(GameState &world, float deltaSeconds);
    void Populate(const GameState &world);
    void ChooseExit(TrafficVehicle &vehicle);
    bool Allows(int lane, TrafficTurn turn) const;
    unsigned int Random();
    std::vector<TrafficVehicle> vehicles;
    std::vector<JunctionSignal> signals;
    unsigned int randomState = 0x71b39a5u;
    std::uint64_t nextId = 0;
    float accumulator = 0, spawnTimer = 0, impactCooldown = 0;
    int cityFilter = -1;
};

// ============================================================================
// AirTraffic — independent, continuous routes; model animation does not move Actors
// ============================================================================
struct AircraftActor
{
    std::uint64_t id = 0;
    int route = 0, direction = 1;
    models::AircraftType type = models::AircraftType::Courier;
    bool alternate = false;
    float parameter = 0, speed = 20, altitude = 12, yaw = 0, bank = 0;
    Vector2 position = {};
    int health = 180;
    bool occupied = true;
    float wreckSeconds = 0, fallSpeed = 0;
};
class AirTraffic
{
    friend class SaveArchive;

  public:
    static constexpr size_t kAircraftLimit = 24;
    void Reset();
    void Update(const GameState &world, float deltaSeconds);
    const std::vector<AircraftActor> &Aircraft() const
    {
        return aircraft;
    }
    bool DamageAircraft(std::uint64_t id, int damage, bool playerOwned = true);
    static bool Contains(const AircraftActor &craft, models::Point point);
    static Vector2 Position(int route, float parameter);
    static Vector2 Tangent(int route, float parameter);
    // Includes both the banked hull and the renderer's projected, blurred ground shadow.
    static bool OutsideVisibleFootprint(const AircraftActor &craft, Vector2 camera, float padding = 40,
                                        float groundElevation = 0);

  private:
    std::vector<AircraftActor> aircraft;
    unsigned int randomState = 0x85ab32u;
    std::uint64_t nextId = 0;
    float spawnTimer = 0;
};

// ============================================================================
// PlanetTraffic — bounded spherical traffic, independent of city-local coordinates and render chunks
// ============================================================================
struct PlanetTrafficActor
{
    std::uint64_t id = 0;
    std::string routeId;
    bool airborne = false, occupied = true, collisionReady = true;
    int direction = 1, health = 140;
    double distanceMeters = 0, speedMeters = 0, altitudeMeters = 0;
    world::SurfacePoint position;
    world::Vector3d heading = {0, 1, 0};
    float bank = 0, wreckSeconds = 0, fallSpeed = 0, farSeconds = 0;
    int city = -1;
    std::uint64_t cityVehicle = 0;
};
struct PlanetTrafficLink
{
    std::uint64_t actor = 0, vehicle = 0;
    int city = -1;
};
struct PlanetTrafficState
{
    std::string worldKey;
    std::uint64_t manifestDigest = 0, nextId = 1;
    unsigned randomState = 1;
    float spawnSeconds = 0;
    std::vector<PlanetTrafficActor> actors;
};
struct PlanetTrafficRoute
{
    std::string id;
    unsigned from = 0, to = 0;
    bool airborne = false;
    std::vector<world::SurfacePoint> points;
    std::vector<double> distances, capDots;
    std::vector<world::Vector3d> caps;
    double lengthMeters = 0;
    int portalCity = -1, lane = -1;
    bool entry = false; // City portals are directed; regional roads remain bidirectional.
};
class PlanetTraffic
{
  public:
    static constexpr size_t GroundLimit = 64, AirLimit = 24;
    static constexpr double DetailedMeters = 240, RetireMeters = 4000;
    static constexpr unsigned Revision = 1;
    static constexpr size_t CityLimit = 8;
    explicit PlanetTraffic(PlanetTrafficState state = {});
    void Bind(std::shared_ptr<const world::GeneratedPlanet> planet);
    static void MigrateLanes(PlanetTrafficState &state, std::shared_ptr<const world::GeneratedPlanet> planet);
    void Update(GameState &state, const world::SurfaceSession &session, float deltaSeconds, bool exterior);
    const PlanetTrafficState &State() const
    {
        return state;
    }
    const std::vector<PlanetTrafficRoute> &Routes() const
    {
        return *routes;
    }
    const PlanetTrafficRoute *Find(std::string_view id) const;
    static void Validate(const PlanetTrafficState &state, const world::PlanetDefinition &planet,
                         std::uint64_t digest);
    world::SurfacePoint Position(const PlanetTrafficRoute &route, double distance, int direction) const;
    bool Blocked(const world::SurfacePoint &point, double clearanceMeters) const;
    bool Damage(std::uint64_t id, int amount, bool playerOwned = false);
    bool Contains(const PlanetTrafficActor &actor, const world::SurfacePoint &point,
                  double padding = 0) const;
    bool Outside(const PlanetTrafficActor &actor, const GameState &world,
                 const world::SurfaceSession &session, float padding = 240) const;
    bool Depart(int city, TrafficVehicle &vehicle);
    void SyncCity(int city, const TrafficSystem &traffic, const world::SurfaceSession &session);
    void SyncDamage(const TrafficVehicle &vehicle);
    std::vector<PlanetTrafficLink> Links() const;

  private:
    void BindLayout(std::shared_ptr<const world::GeneratedPlanet> planet, bool leftTraffic);
    bool leftTraffic = false;
    unsigned Random();
    void Advance(PlanetTrafficActor &actor, float seconds, const GameState &world,
                 const world::SurfaceSession &session, bool exterior);
    void Populate(const GameState &world, const world::SurfaceSession &session);
    bool InsideCore(const world::SurfacePoint &point) const;
    bool Arrive(PlanetTrafficActor &actor, const PlanetTrafficRoute &route,
                const world::SurfaceSession &session);
    PlanetTrafficState state;
    std::shared_ptr<const world::GeneratedPlanet> planet;
    std::shared_ptr<const std::vector<PlanetTrafficRoute>> routes =
        std::make_shared<const std::vector<PlanetTrafficRoute>>();
    std::map<std::string, size_t, std::less<>> routeIndex;
    std::map<unsigned, std::vector<size_t>> groundConnections;
    std::array<std::string, 5> exits;
};

// ============================================================================
// PlanetCommunity — persistent procedural settlement hubs, residents and optional local work
// ============================================================================
struct CommunityResident
{
    std::string id;
    std::uint64_t appearance = 0;
    std::array<world::SurfacePoint, 2> path;
    world::SurfacePoint position;
    world::Vector3d heading = {0, 1, 0};
    double distanceMeters = 0;
    float speed = 0, fearSeconds = 0, hitFlash = 0, corpseSeconds = 0;
    int direction = 1, health = 100;
};
struct CommunitySite
{
    std::string regionId;
    world::SurfacePoint contact, target;
    unsigned task = 0; // 0 offered, 1 accepted, 2 measured, 3 paid. Never advances the main story.
    std::vector<CommunityResident> residents;
};
struct PlanetCommunityState
{
    std::string worldKey;
    std::vector<CommunitySite> sites;
};
class PlanetCommunity
{
  public:
    static constexpr size_t SiteLimit = 512, ResidentsPerSite = 4, ActiveLimit = 48;
    static constexpr double WakeMeters = 350, DetailedMeters = 160;
    explicit PlanetCommunity(PlanetCommunityState state = {});
    void Bind(std::shared_ptr<const world::GeneratedPlanet> planet);
    static void Validate(const PlanetCommunityState &state, const world::PlanetDefinition &planet);
    void Update(float deltaSeconds, bool exterior);
    const PlanetCommunityState &State() const
    {
        return state;
    }
    bool Blocked(const world::SurfacePoint &point, double clearance) const;
    bool Damage(const std::string &id, int damage, bool playerOwned);
    bool Hit(const world::SurfacePoint &point, int damage, bool playerOwned);
    InteractionTarget Nearby(float *legacyDistance = nullptr) const;
    const char *Label() const;
    void Interact();
    std::vector<std::string> Journal() const;
    static const char *TaskName(const CommunitySite &site);

  private:
    bool Populate(const world::GeneratedRegion &region);
    CommunityResident *Find(const std::string &id);
    struct Contact
    {
        size_t site = 0, resident = 0;
        int kind = 0;
        double distance = 0;
    };
    std::optional<Contact> Nearest() const;
    PlanetCommunityState state;
    std::shared_ptr<const world::GeneratedPlanet> planet;
    float prepareSeconds = 0;
};

// ============================================================================
// WorldScenery — imported props and their rotated physical footprints
// ============================================================================
struct SceneryProp
{
    const models::Model *model = nullptr;
    models::SceneryType type = models::SceneryType::Count; // Count identifies a parked aircraft.
    Vector2 position = {}, minimum = {}, maximum = {};
    float yaw = 0, elevation = 0;
    bool solid = true;
};
class WorldScenery
{
  public:
    void Generate(const GameState &world);
    std::uint64_t Revision() const;
    const std::vector<SceneryProp> &Props() const
    {
        return props;
    }
    bool IsBlocked(Vector2 position, float clearance = .2f) const;

  private:
    bool Place(const GameState &world, const models::Model &model, models::SceneryType type, Vector2 position,
               float yaw = 0, bool solid = true);
    std::vector<SceneryProp> props;
    std::map<std::pair<int, int>, std::vector<size_t>> solidCells;
    std::uint64_t revision = 0;
};

// Connected outdoor world. Bike travel never changes the active scene or map.
class Overworld
{
  public:
    static constexpr int kCityCount = 5;
    static constexpr int kPopulationPerCity = 20;
    static constexpr int ResidentPopulation(int city)
    {
        return kPopulationPerCity - (city == 0 ? 2 : 0); // Ria and Mara are separate landmarks.
    }
    static constexpr int ResidentPopulation()
    {
        int count = 0;
        for (int city = 0; city < kCityCount; ++city)
        {
            count += ResidentPopulation(city);
        }
        return count;
    }
    static constexpr int kKainResidentSlot = kPopulationPerCity - 3;
    static constexpr float kBikeCruiseSpeed = 7.5f;
    static constexpr float kBikeBoostSpeed = 12.8f;
    static constexpr float kBikeReverseSpeed = .65f;
    static constexpr Vector2 kMineEntrance = {128, 18};
    static const std::array<CityInfo, kCityCount> &Cities();
    static const std::vector<world::StoryAnchor> &StoryAnchors();
    // Lazy read-only plan. Streaming activates this in CQ-01C; legacy gameplay stays unchanged.
    static const world::GeneratedPlanet &PlanetPlan();
    static int CityAt(Vector2 position);
    static bool IsRoad(int x, int y);
    static TrafficSystem &Traffic();
    static AirTraffic &Flights();
    static WorldScenery &Scenery();
    static void Populate(GameState &state);
    static void UpdateBike(GameState &state, float deltaSeconds);
    static void ToggleBike(GameState &state);
    static models::Palette BikePalette();
};
// ============================================================================
// CitySimulation — distance policy shared by update, culling and monitoring; no catch-up debt
// ============================================================================
enum class CitySimulationBand
{
    Dormant,
    Retained,
    Detailed
};
struct CitySimulationStatus
{
    CitySimulationBand band = CitySimulationBand::Dormant;
    double distanceMeters = 0;
    std::uint64_t detailedTicks = 0;
    double passiveSeconds = 0;
};
class CitySimulation
{
  public:
    static constexpr double EnterMeters = 420, LeaveMeters = 500, RetainMeters = 900;
    static constexpr int PathBudget = 2;
    static CitySimulationBand Select(double distanceMeters, CitySimulationBand previous, bool exterior);
    static const char *Name(CitySimulationBand band);
    static void AdvanceDormant(Resident &resident, float deltaSeconds);
};
// ============================================================================
// PlanetCities — main-thread city actors in persistent local frames, with bounded distance tiers
// ============================================================================
class PlanetCities
{
    friend class SaveArchive;

  public:
    PlanetCities();
    static world::CityCorePlan Describe(const GameState &state);
    void Update(GameState &state, const world::SurfaceSession &session, float dt);
    int ActiveCity() const
    {
        return activeCity;
    }
    TrafficSystem *ActiveTraffic();
    int ObservedCity() const
    {
        return observedCity;
    }
    bool Observing() const
    {
        return observedCity >= 0;
    }
    bool PlayerObserved() const
    {
        return Observing() && observedCity == activeCity;
    }
    const CitySimulationStatus &Status(int city) const
    {
        return simulation.at(size_t(city));
    }
    int RemainingPathBudget() const
    {
        return remainingPathBudget;
    }
    // Only observation coordinates rotate; a completed player contact correction is mapped back.
    void Observe(GameState &state, const world::SurfaceSession &session, int city,
                 const std::function<void()> &operation);
    const TrafficSystem &Traffic(int city) const
    {
        return traffic.at(size_t(city));
    }
    TrafficSystem &Traffic(int city)
    {
        return traffic.at(size_t(city));
    }
    std::optional<world::SurfacePoint> Point(const world::SurfaceSession &session, int city,
                                             Vector2 legacy) const;
    std::optional<Vector2> Local(const world::SurfaceSession &session, int city, Vector2 legacy) const;
    float Yaw(const world::SurfaceSession &session, int city, float legacyYaw) const;
    InteractionTarget Nearby(const GameState &state, const world::SurfaceSession &session,
                             float *nearestDistance = nullptr) const;
    bool Occupied(const GameState &state, const world::SurfaceSession &session, Vector2 local,
                  int cityOverride = -1) const;
    bool OccupiedAt(const GameState &state, const world::SurfaceSession &session,
                    const world::SurfacePoint &point, int cityOverride = -1) const;

  private:
    int activeCity = -1;
    int observedCity = -1, remainingPathBudget = CitySimulation::PathBudget;
    std::array<CitySimulationStatus, Overworld::kCityCount> simulation;
    std::array<TrafficSystem, Overworld::kCityCount> traffic;
};
} // namespace tutorial
