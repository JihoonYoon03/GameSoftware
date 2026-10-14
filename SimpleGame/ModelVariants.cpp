#include "stdafx.h"
#include "ModelVariants.h"
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <algorithm>

namespace models
{
namespace
{
void Add(Model &model, PartKind kind, std::initializer_list<Point> points, std::array<float, 4> color,
         PaletteSlot slot = PaletteSlot::Body, float width = 1)
{
    if (model.count >= model.parts.size() || points.size() > 8)
    {
        throw std::length_error("Model part capacity exceeded");
    }
    auto &part = model.parts[model.count++];
    part.kind = kind;
    part.count = static_cast<std::uint32_t>(points.size());
    part.color = color;
    part.palette = slot;
    part.width = width;
    size_t i = 0;
    for (auto point : points)
    {
        part.points[i++] = point;
    }
}
void Box(Model &model, float x, float y, float w, float d, float bottom, float top)
{
    Add(model, PartKind::Polygon,
        {{x, y + d, bottom}, {x + w, y + d, bottom}, {x + w, y + d, top}, {x, y + d, top}},
        {.55f, .55f, .6f, 1});
    Add(model, PartKind::Polygon,
        {{x + w, y, bottom}, {x + w, y + d, bottom}, {x + w, y + d, top}, {x + w, y, top}},
        {.78f, .78f, .82f, 1});
    Add(model, PartKind::Polygon, {{x, y, top}, {x + w, y, top}, {x + w, y + d, top}, {x, y + d, top}},
        {1, 1, 1, 1});
}
void Dome(Model &model, float bottom, float height)
{
    size_t firstPart = model.count;
    // Triangles avoid concave GL_POLYGON fans; all values are normalized world coordinates.
    constexpr int kSegments = 12;
    for (int ring = 0; ring < 3; ++ring)
    {
        float a0 = ring * 1.5707963f / 3, a1 = (ring + 1) * 1.5707963f / 3;
        for (int i = 0; i < kSegments; ++i)
        {
            float t0 = i * 6.2831853f / kSegments, t1 = (i + 1) * 6.2831853f / kSegments;
            Point a = {.5f + .46f * std::cos(a0) * std::cos(t0), .5f + .46f * std::cos(a0) * std::sin(t0),
                       bottom + height * std::sin(a0)};
            Point b = {.5f + .46f * std::cos(a0) * std::cos(t1), .5f + .46f * std::cos(a0) * std::sin(t1),
                       a.z};
            Point c = {.5f + .46f * std::cos(a1) * std::cos(t1), .5f + .46f * std::cos(a1) * std::sin(t1),
                       bottom + height * std::sin(a1)};
            Point d = {.5f + .46f * std::cos(a1) * std::cos(t0), .5f + .46f * std::cos(a1) * std::sin(t0),
                       c.z};
            Add(model, PartKind::Polygon, {a, b, c}, {.58f, .7f, .82f, 1}, PaletteSlot::Accent);
            if (ring < 2)
            {
                Add(model, PartKind::Polygon, {a, c, d}, {.65f, .8f, .9f, 1}, PaletteSlot::Accent);
            }
        }
    }
    std::stable_sort(model.parts.begin() + firstPart, model.parts.begin() + model.count,
                     [](const Part &a, const Part &b) {
                         float da = 0, db = 0;
                         for (size_t i = 0; i < a.count; ++i)
                         {
                             da += a.points[i].x + a.points[i].y;
                         }
                         for (size_t i = 0; i < b.count; ++i)
                         {
                             db += b.points[i].x + b.points[i].y;
                         }
                         return da / a.count < db / b.count;
                     });
}
} // namespace
void GenerateVehicle(VehicleModel &result, VehicleType type)
{
    auto &model = result.body;
    model.count = 0;
    model.space = CoordinateSpace::Screen;
    const bool bike = type == VehicleType::JetBike;
    float length = bike ? 30.f : type == VehicleType::Hauler ? 58.f : 46.f;
    float halfWidth = bike ? 7.f : 15.f;
    // Local +X is always the nose; the nozzles sit at the negative-X tail.
    Add(model, PartKind::Polygon,
        {{-length, -halfWidth},
         {length - 8, -halfWidth},
         {length, 0},
         {length - 8, halfWidth},
         {-length, halfWidth}},
        {.85f, .92f, 1, 1});
    Add(model, PartKind::Polygon,
        {{-length, halfWidth},
         {length - 8, halfWidth},
         {length - 8, halfWidth + 10},
         {-length, halfWidth + 10}},
        {.38f, .48f, .58f, 1});
    Add(model, PartKind::Polygon,
        {{length - 8, halfWidth}, {length, 0}, {length, 10}, {length - 8, halfWidth + 10}},
        {.48f, .55f, .62f, 1});
    Add(model, PartKind::Polygon,
        {{-length, -halfWidth},
         {-length, halfWidth},
         {-length + 5, halfWidth + 10},
         {-length + 5, -halfWidth + 10}},
        {.45f, .55f, .65f, 1});
    if (type == VehicleType::Coupe || type == VehicleType::Patrol)
    {
        Add(model, PartKind::Polygon, {{-18, -10}, {-7, -32}, {17, -27}, {29, -9}}, {.8f, .9f, 1, 1});
        Add(model, PartKind::Glass, {{-6, -27}, {15, -23}, {23, -10}, {-7, -10}}, {.16f, .5f, .65f, 1},
            PaletteSlot::Fixed);
        if (type == VehicleType::Patrol)
        {
            Add(model, PartKind::Polygon, {{-20, 2}, {-38, 27}, {10, 22}, {28, 6}}, {.65f, .7f, .8f, 1});
            Add(model, PartKind::Line, {{-7, -31}, {13, -28}}, {1, 1, 1, 1}, PaletteSlot::Emission, 4);
        }
    }
    else if (type == VehicleType::Shuttle)
    {
        Add(model, PartKind::Polygon, {{-36, -10}, {-32, -36}, {31, -31}, {39, -6}}, {.8f, .9f, 1, 1});
        for (int i = 0; i < 4; ++i)
        {
            float x = -25.f + i * 14;
            Add(model, PartKind::Glass, {{x, -30}, {x + 10, -29}, {x + 11, -14}, {x, -15}},
                {.12f, .55f, .7f, 1}, PaletteSlot::Fixed);
        }
    }
    else if (type == VehicleType::Hauler)
    {
        Add(model, PartKind::Polygon, {{17, -11}, {20, -32}, {45, -28}, {49, -6}}, {.8f, .9f, 1, 1});
        Add(model, PartKind::Glass, {{28, -27}, {43, -25}, {46, -12}, {28, -14}}, {.12f, .5f, .65f, 1},
            PaletteSlot::Fixed);
        for (int i = 0; i < 3; ++i)
        {
            float x = -48.f + i * 19;
            Add(model, PartKind::Polygon, {{x, -6}, {x, -25}, {x + 16, -23}, {x + 16, 4}},
                {.68f, .72f, .8f, 1}, PaletteSlot::Accent);
        }
    }
    else
    {
        Add(model, PartKind::Line, {{-22, 7}, {20, 8}}, {.6f, .65f, .7f, 1}, PaletteSlot::Fixed, 4);
        Add(model, PartKind::Polygon, {{-11, -5}, {-6, -14}, {8, -13}, {14, -4}}, {.14f, .17f, .2f, 1},
            PaletteSlot::Fixed);
        Add(model, PartKind::Line, {{13, -4}, {23, -18}}, {.5f, .7f, .8f, 1}, PaletteSlot::Fixed, 3);
        Add(model, PartKind::Line, {{17, -18}, {27, -18}}, {.6f, .8f, .9f, 1}, PaletteSlot::Fixed, 3);
    }
    for (int i = 0; i < 2; ++i)
    {
        float y = i == 0 ? -halfWidth + 3 : halfWidth + 3;
        result.exhausts[i] = {-length - 5, y, 0};
        result.headlights[i] = {length - 8, y - 1, 0};
        Add(model, PartKind::Polygon,
            {{-length - 5, y - 3}, {-length + 8, y - 3}, {-length + 8, y + 3}, {-length - 5, y + 3}},
            {.1f, .14f, .18f, 1}, PaletteSlot::Fixed);
        Add(model, PartKind::Line, {{-length - 5, y - 2}, {-length - 5, y + 2}}, {1, 1, 1, 1},
            PaletteSlot::Emission, 3);
        Add(model, PartKind::Line, {{length - 8, y - 3}, {length - 8, y + 1}}, {1, .93f, .7f, 1},
            PaletteSlot::Fixed, 3);
    }
}
void GenerateCharacter(Model &result, CharacterType type)
{
    result.count = 0;
    result.space = CoordinateSpace::Screen;
    // Accessories overlay the existing animated sprite, preserving walking legs.
    if (type == CharacterType::Explorer)
    {
        Add(result, PartKind::Polygon, {{-12, -40}, {-14, -17}, {-5, -15}, {-4, -39}}, {.4f, .5f, .6f, 1},
            PaletteSlot::Accent);
        Add(result, PartKind::Line, {{-6, -32}, {6, -19}}, {.8f, .85f, .9f, 1}, PaletteSlot::Accent, 3);
    }
    else if (type == CharacterType::Engineer)
    {
        Add(result, PartKind::Polygon, {{-14, -36}, {-15, -15}, {-7, -12}, {-6, -34}}, {.55f, .62f, .7f, 1});
        Add(result, PartKind::Ellipse, {{0, -48}, {10, 5}}, {1, 1, 1, 1});
        Add(result, PartKind::Line, {{-11, -44}, {11, -44}}, {.45f, .5f, .6f, 1}, PaletteSlot::Accent, 3);
        Add(result, PartKind::Polygon, {{5, -21}, {13, -21}, {13, -13}, {5, -13}}, {.6f, .7f, .8f, 1},
            PaletteSlot::Accent);
    }
    else if (type == CharacterType::Guard)
    {
        Add(result, PartKind::Polygon, {{-15, -36}, {15, -36}, {11, -18}, {-11, -18}}, {.65f, .75f, .9f, 1});
        Add(result, PartKind::Ellipse, {{0, -46}, {10, 9}}, {.7f, .8f, .9f, 1});
        Add(result, PartKind::Line, {{-7, -46}, {7, -46}}, {1, 1, 1, 1}, PaletteSlot::Emission, 3);
        Add(result, PartKind::Line, {{8, -30}, {18, -17}}, {.18f, .22f, .28f, 1}, PaletteSlot::Fixed, 5);
    }
    else if (type == CharacterType::Medic)
    {
        Add(result, PartKind::Polygon, {{-10, -36}, {10, -36}, {13, -8}, {-13, -8}}, {1, 1, 1, 1});
        Add(result, PartKind::Ellipse, {{0, -47}, {10, 6}}, {1, 1, 1, 1});
        Add(result, PartKind::Line, {{0, -32}, {0, -21}}, {1, 1, 1, 1}, PaletteSlot::Accent, 3);
        Add(result, PartKind::Line, {{-4, -27}, {4, -27}}, {1, 1, 1, 1}, PaletteSlot::Accent, 3);
    }
    else
    {
        Add(result, PartKind::Polygon, {{-9, -36}, {9, -36}, {15, -6}, {-15, -6}}, {.8f, .85f, .95f, 1});
        Add(result, PartKind::Ellipse, {{0, -48}, {10, 7}}, {.75f, .8f, .9f, 1}, PaletteSlot::Accent);
        Add(result, PartKind::Polygon, {{7, -27}, {17, -27}, {17, -17}, {7, -17}}, {1, 1, 1, 1},
            PaletteSlot::Emission);
    }
}
void GenerateBuilding(Model &result, BuildingType type)
{
    result.count = 0;
    result.space = CoordinateSpace::World;
    if (type == BuildingType::Habitat)
    {
        Box(result, 0, 0, 1, 1, 0, .64f);
        Box(result, .08f, .08f, .84f, .84f, .64f, .78f);
        for (float z : {.24f, .46f})
        {
            Box(result, .16f, .78f, .68f, .22f, z, z + .06f);
        }
        Add(result, PartKind::Polygon, {{0, 0, .78f}, {1, 0, .78f}, {1, .5f, 1}, {0, .5f, 1}},
            {.78f, .8f, .9f, 1}, PaletteSlot::Accent);
        Add(result, PartKind::Polygon, {{0, .5f, 1}, {1, .5f, 1}, {1, 1, .78f}, {0, 1, .78f}},
            {.55f, .65f, .8f, 1}, PaletteSlot::Accent);
    }
    else if (type == BuildingType::Terraced)
    {
        Box(result, 0, 0, 1, 1, 0, .36f);
        Box(result, .05f, .05f, .7f, .7f, .36f, .7f);
        Box(result, .1f, .1f, .42f, .42f, .7f, 1);
        Add(result, PartKind::Line, {{.8f, .85f, .37f}, {.95f, .85f, .37f}}, {1, 1, 1, 1},
            PaletteSlot::Accent, 4);
    }
    else if (type == BuildingType::Workshop)
    {
        Box(result, 0, 0, 1, 1, 0, .45f);
        Add(result, PartKind::Polygon, {{0, 0, .45f}, {1, 0, .45f}, {1, .5f, .8f}, {0, .5f, .8f}},
            {.8f, .85f, .9f, 1}, PaletteSlot::Accent);
        Add(result, PartKind::Polygon, {{0, .5f, .8f}, {1, .5f, .8f}, {1, 1, .45f}, {0, 1, .45f}},
            {.5f, .65f, .75f, 1}, PaletteSlot::Accent);
        Box(result, .65f, .05f, .18f, .18f, .6f, 1);
        Add(result, PartKind::Polygon, {{.15f, 1, .02f}, {.7f, 1, .02f}, {.7f, 1, .35f}, {.15f, 1, .35f}},
            {.2f, .25f, .3f, 1}, PaletteSlot::Fixed);
    }
    else
    {
        Box(result, 0, 0, 1, 1, 0, type == BuildingType::Observatory ? .3f : .2f);
        Dome(result, type == BuildingType::Observatory ? .3f : .2f,
             type == BuildingType::Observatory ? .6f : .75f);
        if (type == BuildingType::Observatory)
        {
            Add(result, PartKind::Line, {{.5f, .5f, .7f}, {.82f, .2f, 1.12f}}, {.75f, .85f, .9f, 1},
                PaletteSlot::Fixed, 12);
            Add(result, PartKind::Line, {{.74f, .18f, 1.1f}, {.9f, .22f, 1.14f}}, {1, 1, 1, 1},
                PaletteSlot::Emission, 5);
            Box(result, .35f, .8f, .3f, .2f, 0, .28f);
        }
    }
}
bool ValidateModel(const Model &model)
{
    if (unsigned(model.space) > unsigned(CoordinateSpace::World))
    {
        return false;
    }
    if (model.count == 0 || model.count > model.parts.size())
    {
        return false;
    }
    for (size_t i = 0; i < model.count; ++i)
    {
        const auto &part = model.parts[i];
        if (part.count > 8 || part.count < 2 || unsigned(part.kind) > 3 || unsigned(part.palette) > 3 ||
            !std::isfinite(part.width) || part.width <= 0)
        {
            return false;
        }
        if ((part.kind == PartKind::Polygon && part.count < 3) ||
            (part.kind == PartKind::Glass && part.count != 4) ||
            ((part.kind == PartKind::Line || part.kind == PartKind::Ellipse) && part.count != 2))
        {
            return false;
        }
        for (float color : part.color)
        {
            if (!std::isfinite(color) || color < 0 || color > 1)
            {
                return false;
            }
        }
        for (size_t j = 0; j < part.count; ++j)
        {
            auto p = part.points[j];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            {
                return false;
            }
        }
        if (part.kind == PartKind::Ellipse && (part.points[1].x <= 0 || part.points[1].y <= 0))
        {
            return false;
        }
        if (part.kind == PartKind::Polygon || part.kind == PartKind::Glass)
        {
            // Batch fans require convex faces. Check the same quarter-view projection used to draw buildings.
            auto projected = [&](Point p) {
                return model.space == CoordinateSpace::World
                           ? Point{(p.x - p.y) * 72, (p.x + p.y) * 36 - p.z * 120, 0}
                           : p;
            };
            float sign = 0, twiceArea = 0;
            for (size_t j = 0; j < part.count; ++j)
            {
                auto a = projected(part.points[j]), b = projected(part.points[(j + 1) % part.count]),
                     c = projected(part.points[(j + 2) % part.count]);
                float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                twiceArea += a.x * b.y - b.x * a.y;
                if (std::fabs(cross) < .00001f)
                {
                    continue;
                }
                if (sign * cross < 0)
                {
                    return false;
                }
                sign = cross;
            }
            if (std::fabs(twiceArea) < .00001f)
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace models
