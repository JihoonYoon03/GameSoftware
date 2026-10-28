#pragma once
#include "Game.h"
#include <array>
#include <cstdint>
#include <map>

namespace tutorial
{
struct CityInfo
{
    const char *id;
    const char *name;
    Vector2 center;
    const char *mood;
    const char *rumor;
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
};
class TrafficSystem
{
  public:
    static constexpr size_t kVehicleLimit = 112;
    static constexpr float kDespawnDistance = 155;
    void Reset();
    void Update(GameState &world, float deltaSeconds);
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
    unsigned int Random();
    std::vector<TrafficVehicle> vehicles;
    std::vector<JunctionSignal> signals;
    unsigned int randomState = 0x71b39a5u;
    std::uint64_t nextId = 0;
    float accumulator = 0, spawnTimer = 0, impactCooldown = 0;
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
    static bool OutsideVisibleFootprint(const AircraftActor &craft, Vector2 camera, float padding = 40);

  private:
    std::vector<AircraftActor> aircraft;
    unsigned int randomState = 0x85ab32u;
    std::uint64_t nextId = 0;
    float spawnTimer = 0;
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
};

// Connected outdoor world. Bike travel never changes the active scene or map.
class Overworld
{
  public:
    static constexpr int kCityCount = 5;
    static constexpr int kPopulationPerCity = 20;
    static constexpr int kKainResidentSlot = kPopulationPerCity - 3;
    static constexpr float kBikeCruiseSpeed = 7.5f;
    static constexpr float kBikeBoostSpeed = 12.8f;
    static constexpr float kBikeReverseSpeed = .65f;
    static constexpr Vector2 kMineEntrance = {128, 18};
    static const std::array<CityInfo, kCityCount> &Cities();
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
} // namespace tutorial
