#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace models
{
struct Point
{
    float x, y, z;
};
enum class VehicleType : std::uint32_t
{
    Coupe,
    Shuttle,
    Hauler,
    Patrol,
    JetBike,
    Count
};
enum class AircraftType : std::uint32_t
{
    Courier,
    AirTaxi,
    Skycrane,
    Survey,
    Count
};
enum class SceneryType : std::uint32_t
{
    Road2Lane,
    Road4Lane,
    Road6Lane,
    Crossroad,
    TJunction,
    Curve90,
    Sidewalk,
    SpeedTable,
    CrashBarrier,
    Streetlight,
    TrafficSignal,
    PedestrianHologram,
    Bollard,
    Bench,
    Planter,
    TransitShelter,
    Wayfinding,
    ChargePost,
    Transformer,
    CargoContainer,
    HVAC,
    PipeRack,
    CommRelay,
    SolarArray,
    WaterTank,
    LandingPad,
    AirfieldBeacon,
    SurveyTripod,
    FieldShelter,
    MiningConveyor,
    ObserverMonolith,
    FracturedArch,
    PrismBloom,
    SailTree,
    Count
};
enum class CharacterType : std::uint32_t
{
    Explorer,
    Engineer,
    Guard,
    Medic,
    Researcher,
    Miner,
    HeavyCombat,
    Pilot,
    Civilian,
    Count
};
enum class Gender : std::uint32_t
{
    Male,
    Female
};
enum class HairStyle : std::uint32_t
{
    Buzz,
    SidePart,
    Curls,
    Bob,
    Ponytail,
    Bun,
    Count
};
// ============================================================================
// WorldScale — physical scale shared by models, animation and gameplay
// ============================================================================
struct WorldScale
{
    static constexpr float metersPerUnit = 2;
    static constexpr float pixelsPerMeter = 22;
    static constexpr float humanHeightMeters = 1.8f;
    static constexpr float characterScale = humanHeightMeters / (metersPerUnit * 1.26f);
    // Neutral Aim_Base muzzle height in the current HUMAN_7H delivery.
    static constexpr float aimHeightPixels = 1.00275f * 44 * characterScale;
};
enum class BuildingType : std::uint32_t
{
    Habitat,
    Terraced,
    Workshop,
    Dome,
    Observatory,
    SpireHelix,
    TwinBridge,
    ArcologyTerrace,
    TransitAtrium,
    CourtyardHabitat,
    Count
};
enum class PaletteSlot : std::uint32_t
{
    Fixed,
    Body,
    Accent,
    Emission
};
enum class CoordinateSpace : std::uint32_t
{
    Screen,
    World
};
struct Palette
{
    std::array<float, 4> body = {1, 1, 1, 1};
    std::array<float, 4> accent = {.35f, .75f, .85f, 1};
    std::array<float, 4> emission = {.25f, .8f, 1, 1};
};
enum class PartKind : std::uint32_t
{
    Polygon,
    Ellipse,
    Line,
    Glass
};
enum class Surface : std::uint32_t
{
    Default,
    Fabric,
    Metal,
    Glass,
    Emissive,
    Rubber,
    Stone,
    Skin
};
// Local +X is forward, +Y is left, +Z is up. Every joint is parent-relative.
enum class Joint : std::uint32_t
{
    Root,
    Head,
    RightArm,
    RightForearm,
    LeftArm,
    LeftForearm,
    RightLeg,
    RightShin,
    LeftLeg,
    LeftShin,
    Weapon,
    Count
};
struct Transform3D
{
    Point position = {};
    float yaw = 0, pitch = 0, roll = 0;
    Point Apply(Point point) const;
};
struct CharacterPose
{
    std::array<Transform3D, static_cast<size_t>(Joint::Count)> joints;
    bool armed = false;
    bool walking = false, riding = false;
    float animationPhase = 0, recoil = 0, locomotionSpeed = 2.1f;
    float meleeSeconds = 0;
    // Explicit graph inputs; legacy NPC callers retain their armed/forward locomotion defaults.
    bool blended = false;
    Gender gender = Gender::Male;
    float locomotionPhase = 0, moveLeft = 0, moveForward = 0, aimWeight = 0, fireSeconds = 0;
    Point Apply(Point point, Joint joint) const;
};
// ============================================================================
// ModelRig — hierarchical limb animation and hand-mounted weapon
// ============================================================================
class ModelRig
{
  public:
    static CharacterPose Pose(float phase, bool walking, bool armed = false, float recoil = 0,
                              bool riding = false, Gender gender = Gender::Male);
    static Point Muzzle(const CharacterPose &pose);
};
struct Part
{
    PartKind kind = PartKind::Polygon;
    std::uint32_t count = 0;
    std::array<Point, 8> points = {};
    std::array<float, 4> color = {};
    float width = 1;
    PaletteSlot palette = PaletteSlot::Fixed;
    Joint joint = Joint::Root;
    bool clothing = false;
    Surface surface = Surface::Default;
    std::array<Point, 8> normals = {};
};
struct Model
{
    std::uint32_t resourceId = 0;
    bool ground = false;
    CoordinateSpace space = CoordinateSpace::Screen;
    std::uint32_t count = 0;
    std::vector<Part> parts;
};
struct VehicleModel
{
    Model body;
    std::array<Point, 2> exhausts = {};
    std::array<Point, 2> headlights = {};
    Point seat = {};
};
struct Library
{
    std::array<Point, 49> circle = {};
    std::array<Point, 65> softCircle = {};
    std::array<Point, 97> gateRing = {};
    std::array<Point, 8> cube = {};
    Model drone;
    Model salvage;
    Model recoveryKit;
    Model box;
    Model rock;
    Model telescope;
    Model console;
    Model bulkhead;
    Model trafficGate;
    Model bed, chair, table, lamp, generator, breaker, beacon, relief, wall, door, lantern, support;
    Model tree, fungus, crystal, reeds;
    std::array<Model, 6> creatures;
    std::array<Model, 2> outdoorGrounds;
    Model mineGround;
    std::array<Model, 4> grounds;
    std::array<VehicleModel, static_cast<size_t>(VehicleType::Count)> vehicles;
    std::array<Model, static_cast<size_t>(AircraftType::Count) * 2> aircraft;
    std::array<Model, static_cast<size_t>(SceneryType::Count)> scenery;
    std::array<Model, 2> signalStates;
    std::array<Model, static_cast<size_t>(CharacterType::Count)> characters;
    std::array<Model, static_cast<size_t>(CharacterType::Count)> femaleCharacters;
    std::array<Model, static_cast<size_t>(HairStyle::Count)> hairs;
    std::array<Model, static_cast<size_t>(HairStyle::Count)> coveredHairs;
    std::array<Model, static_cast<size_t>(BuildingType::Count)> buildings;
};

// Heap-owned immutable bindings. ResourceAssets owns the converted GLB geometry and animation cache.
class ModelLibrary
{
  public:
    static const Library &Get();
    static const Model &Character(CharacterType type, Gender gender);
    static const Model &Hair(HairStyle style, bool covered);
    static const Model &Aircraft(AircraftType type, bool alternate = false);
    static const Model &Scenery(SceneryType type);
    static const char *SceneryName(SceneryType type);
    static const Model &TrafficSignal(unsigned int state);
    static const Model &Transit(const std::string &name);
};

// ============================================================================
// ModelFactory — imported meshes and small procedural effect geometry validation
// ============================================================================
class ModelFactory
{
  public:
    static bool ValidateModel(const Model &model);
};
} // namespace models
