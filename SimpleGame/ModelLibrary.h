#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

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
enum class CharacterType : std::uint32_t
{
    Explorer,
    Engineer,
    Guard,
    Medic,
    Researcher,
    Count
};
enum class Gender : std::uint32_t
{
    Male,
    Female
};
// ============================================================================
// WorldScale — physical scale shared by models, animation and gameplay
// ============================================================================
struct WorldScale
{
    static constexpr float metersPerUnit = 2;
    static constexpr float pixelsPerMeter = 22;
    static constexpr float humanHeightMeters = 1.8f;
    static constexpr float characterScale = humanHeightMeters / (metersPerUnit * 1.295f);
    static constexpr float aimHeightPixels = 26 * characterScale;
};
enum class BuildingType : std::uint32_t
{
    Habitat,
    Terraced,
    Workshop,
    Dome,
    Observatory,
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
    Point Apply(Point point, Joint joint) const;
};
// ============================================================================
// ModelRig — hierarchical limb animation and hand-mounted weapon
// ============================================================================
class ModelRig
{
  public:
    static CharacterPose Pose(float phase, bool walking, bool armed = false, float recoil = 0,
                              bool riding = false);
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
    CoordinateSpace space = CoordinateSpace::Screen;
    std::uint32_t count = 0;
    std::array<Part, 1024> parts = {};
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
    std::array<Model, 4> grounds;
    std::array<VehicleModel, static_cast<size_t>(VehicleType::Count)> vehicles;
    std::array<Model, static_cast<size_t>(CharacterType::Count)> characters;
    std::array<Model, static_cast<size_t>(CharacterType::Count)> femaleCharacters;
    std::array<Model, static_cast<size_t>(BuildingType::Count)> buildings;
};

// Immutable heap-owned model data: loaded once, generated in place on cache miss/version change.
class ModelLibrary
{
  public:
    static const Library &Get();
    static const Model &Character(CharacterType type, Gender gender);
};

// ============================================================================
// ModelFactory — vehicle, character, building variants
// ============================================================================
class ModelFactory
{
  public:
    static void GenerateVehicle(VehicleModel &result, VehicleType type);
    static void GenerateCharacter(Model &result, CharacterType type, Gender gender = Gender::Male);
    static void GenerateBuilding(Model &result, BuildingType type);
    static bool ValidateModel(const Model &model);
};
} // namespace models
