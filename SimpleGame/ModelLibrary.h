#pragma once
#include <array>
#include <cstdint>

namespace models
{
struct Point
{
    float x, y, z;
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
};
struct Model
{
    std::uint32_t count = 0;
    std::array<Part, 24> parts = {};
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
};

// Immutable model data: loaded once per process, generated only on cache miss/version change.
const Library &Get();
} // namespace models
