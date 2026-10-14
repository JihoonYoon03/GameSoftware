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
struct Part
{
    PartKind kind = PartKind::Polygon;
    std::uint32_t count = 0;
    std::array<Point, 8> points = {};
    std::array<float, 4> color = {};
    float width = 1;
    PaletteSlot palette = PaletteSlot::Fixed;
};
struct Model
{
    CoordinateSpace space = CoordinateSpace::Screen;
    std::uint32_t count = 0;
    std::array<Part, 96> parts = {};
};
struct VehicleModel
{
    Model body;
    std::array<Point, 2> exhausts = {};
    std::array<Point, 2> headlights = {};
};
struct Library
{
    std::array<Point, 49> circle = {};
    std::array<Point, 65> softCircle = {};
    std::array<Point, 97> gateRing = {};
    std::array<Point, 8> cube = {};
    Model vehicle;
    Model drone;
    Model salvage;
    Model recoveryKit;
    std::array<VehicleModel, static_cast<size_t>(VehicleType::Count)> vehicles;
    std::array<Model, static_cast<size_t>(CharacterType::Count)> characters;
    std::array<Model, static_cast<size_t>(BuildingType::Count)> buildings;
};

// Immutable heap-owned model data: loaded once, generated in place on cache miss/version change.
const Library &Get();
} // namespace models
