#include "stdafx.h"
#include "ModelLibrary.h"
#include "AssetCache.h"
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace models
{
namespace
{
constexpr std::uint32_t kModelVersion = 2;
static_assert(std::is_trivially_copyable<Library>::value, "Model cache needs a stable POD payload");

void Add(Model &model, PartKind kind, std::initializer_list<Point> points, std::array<float, 4> color,
         float width = 1)
{
    Part &part = model.parts[model.count++];
    part.kind = kind;
    part.count = std::uint32_t(points.size());
    std::size_t index = 0;
    for (Point point : points)
    {
        part.points[index++] = point;
    }
    part.color = color;
    part.width = width;
}

Library Generate()
{
    Library library = {};
    for (std::size_t i = 0; i < library.circle.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 48;
        library.circle[i] = {std::cos(angle), std::sin(angle), 0};
    }
    for (std::size_t i = 0; i < library.softCircle.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 64;
        library.softCircle[i] = {std::cos(angle), std::sin(angle), 0};
    }
    library.cube = {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
    for (std::size_t i = 0; i < library.gateRing.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 96;
        library.gateRing[i] = {std::cos(angle), std::sin(angle), 0};
    }
    Model &vehicle = library.vehicle;
    Add(vehicle, PartKind::Polygon, {{-46, -4}, {-14, -22}, {44, -2}, {21, 15}, {-28, 8}},
        {.32f, .45f, .53f, 1});
    Add(vehicle, PartKind::Polygon, {{-28, 8}, {21, 15}, {44, -2}, {44, 12}, {21, 29}, {-28, 21}},
        {.1f, .19f, .27f, 1});
    Add(vehicle, PartKind::Polygon, {{-46, -4}, {-28, 8}, {-28, 21}, {-46, 9}}, {.17f, .28f, .35f, 1});
    Add(vehicle, PartKind::Polygon, {{-20, -10}, {-5, -28}, {21, -20}, {28, -5}, {11, 5}},
        {.35f, .48f, .55f, 1});
    Add(vehicle, PartKind::Glass, {{-17, -10}, {-4, -25}, {19, -18}, {24, -6}}, {.13f, .53f, .66f, 1});
    Add(vehicle, PartKind::Line, {{-4, -25}, {-1, -7}}, {.5f, .66f, .7f, 1}, 2);
    Add(vehicle, PartKind::Line, {{-26, 11}, {17, 18}}, {.5f, .62f, .65f, 1}, 2);
    for (int side = 0; side < 2; ++side)
    {
        float x = side == 0 ? -31.f : 25.f, y = side == 0 ? 10.f : 22.f;
        Add(vehicle, PartKind::Ellipse, {{x, y}, {11, 7}}, {.045f, .09f, .13f, 1});
        Add(vehicle, PartKind::Ellipse, {{x, y}, {7, 4}}, {.15f, .5f, .6f, 1});
    }
    Add(vehicle, PartKind::Line, {{28, 8}, {36, 2}}, {1, .89f, .58f, 1}, 3);
    Add(vehicle, PartKind::Line, {{-43, 6}, {-35, 11}}, {.9f, .22f, .18f, 1}, 3);
    Add(library.drone, PartKind::Ellipse, {{0, -12}, {19, 12}}, {.25f, .19f, .29f, 1});
    Add(library.drone, PartKind::Polygon, {{-22, -14}, {-7, -28}, {16, -22}, {22, -10}, {0, -3}},
        {.49f, .32f, .4f, 1});
    Add(library.drone, PartKind::Ellipse, {{3, -17}, {8, 5}}, {1, .25f, .17f, 1});
    Add(library.drone, PartKind::Line, {{-14, -5}, {-23, 4}}, {.4f, .54f, .6f, 1}, 3);
    Add(library.drone, PartKind::Line, {{13, -5}, {22, 4}}, {.4f, .54f, .6f, 1}, 3);
    Add(library.salvage, PartKind::Polygon, {{0, -15}, {9, -7}, {0, 0}, {-9, -7}}, {.3f, .87f, .94f, 1});
    Add(library.salvage, PartKind::Line, {{0, -12}, {0, -3}}, {.85f, 1, 1, 1}, 2);
    Add(library.recoveryKit, PartKind::Polygon, {{-8, -15}, {8, -15}, {8, 0}, {-8, 0}}, {.2f, .65f, .4f, 1});
    Add(library.recoveryKit, PartKind::Line, {{0, -12}, {0, -3}}, {.85f, 1, .9f, 1}, 3);
    Add(library.recoveryKit, PartKind::Line, {{-4, -7}, {4, -7}}, {.85f, 1, .9f, 1}, 3);
    return library;
}

bool Valid(const Library &library)
{
    auto finitePoints = [](const auto &points) {
        for (const Point &point : points)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            {
                return false;
            }
        }
        return true;
    };
    if (!finitePoints(library.circle) || !finitePoints(library.softCircle) || !finitePoints(library.cube) ||
        !finitePoints(library.gateRing))
    {
        return false;
    }
    for (const Model *model : {&library.vehicle, &library.drone, &library.salvage, &library.recoveryKit})
    {
        if (model->count == 0 || model->count > model->parts.size())
        {
            return false;
        }
        for (std::uint32_t i = 0; i < model->count; ++i)
        {
            const Part &part = model->parts[i];
            if (part.count > part.points.size() || static_cast<unsigned>(part.kind) > 3)
            {
                return false;
            }
            if (!std::isfinite(part.width) || part.width <= 0)
            {
                return false;
            }
            for (float component : part.color)
            {
                if (!std::isfinite(component))
                {
                    return false;
                }
            }
            if ((part.kind == PartKind::Polygon && part.count < 3) ||
                (part.kind == PartKind::Glass && part.count != 4) ||
                ((part.kind == PartKind::Line || part.kind == PartKind::Ellipse) && part.count != 2))
            {
                return false;
            }
            for (const Point &point : part.points)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

Library LoadOrGenerate()
{
    Library library = {};
    std::vector<unsigned char> bytes;
    if (assetcache::Load("models.bin", kModelVersion, sizeof(Library), bytes))
    {
        std::memcpy(&library, bytes.data(), sizeof(library));
        if (Valid(library))
        {
            return library;
        }
    }
    library = Generate();
    assetcache::Save("models.bin", kModelVersion, &library, sizeof(library));
    return library;
}
} // namespace

const Library &Get()
{
    static const Library library = LoadOrGenerate();
    return library;
}
} // namespace models
