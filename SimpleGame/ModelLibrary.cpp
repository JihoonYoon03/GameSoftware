#include "stdafx.h"
#include "ModelLibrary.h"
#include "AssetCache.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include <vector>

namespace models
{

// ============================================================================
// ModelFactory
// ============================================================================
namespace
{
void AddVariantPart(Model &model, PartKind kind, std::initializer_list<Point> points,
                    std::array<float, 4> color, PaletteSlot slot = PaletteSlot::Body, float width = 1)
{
    if (model.count >= model.parts.size() || points.size() > 8)
    {
        throw std::length_error("Model part capacity exceeded");
    }
    auto &part = model.parts[model.count++];
    part = Part{};
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
void MeshBox(Model &, Point, Point, std::array<float, 4>, PaletteSlot, Joint);
void Box(Model &m, float x, float y, float w, float d, float bottom, float top)
{
    MeshBox(m, {x, y, bottom}, {x + w, y + d, top}, {1, 1, 1, 1}, PaletteSlot::Body, Joint::Root);
}
void Dome(Model &model, float bottom, float height)
{
    size_t firstPart = model.count;
    // Triangles avoid concave GL_POLYGON fans; all values are normalized world coordinates.
    constexpr int kSegments = 24;
    for (int ring = 0; ring < 5; ++ring)
    {
        float a0 = ring * 1.5707963f / 5, a1 = (ring + 1) * 1.5707963f / 5;
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
            AddVariantPart(model, PartKind::Polygon, {a, b, c}, {.65f, .8f, .9f, 1}, PaletteSlot::Accent);
            if (ring < 4)
            {
                AddVariantPart(model, PartKind::Polygon, {a, c, d}, {.65f, .8f, .9f, 1}, PaletteSlot::Accent);
            }
        }
    }
    for (size_t i = firstPart; i < model.count; ++i)
    {
        auto &part = model.parts[i];
        part.surface = Surface::Metal;
        for (size_t j = 0; j < part.count; ++j)
        {
            const auto p = part.points[j];
            Point n = {(p.x - .5f) / (.46f * .46f), (p.y - .5f) / (.46f * .46f),
                       (p.z - bottom) / (height * height)};
            float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            part.normals[j] = {n.x / length, n.y / length, n.z / length};
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

namespace
{
// Closed six-sided meshes; no camera-facing sprite planes or garment overlays.
void DetailFace(Model &m, std::vector<Point> points, std::array<float, 4> color, PaletteSlot slot,
                Joint joint, Point center)
{
    auto a = points[0], b = points[1], c = points[2];
    Point n = {(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
               (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
               (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
    if (n.x * (a.x - center.x) + n.y * (a.y - center.y) + n.z * (a.z - center.z) < 0)
    {
        std::reverse(points.begin(), points.end());
    }
    if (m.count >= m.parts.size())
    {
        throw std::length_error("Detailed mesh capacity exceeded");
    }
    auto &part = m.parts[m.count++];
    part = Part{};
    part.kind = PartKind::Polygon;
    part.count = unsigned(points.size());
    part.color = color;
    part.palette = slot;
    part.joint = joint;
    std::copy(points.begin(), points.end(), part.points.begin());
}
void MeshBox(Model &m, Point lo, Point hi, std::array<float, 4> color, PaletteSlot slot = PaletteSlot::Body,
             Joint joint = Joint::Root)
{
    Point center = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    float h[] = {(hi.x - lo.x) / 2, (hi.y - lo.y) / 2, (hi.z - lo.z) / 2};
    float minimum = (std::min)(h[0], (std::min)(h[1], h[2]));
    float radius = minimum > .035f ? (std::min)(.035f, minimum * .22f) : 0;
    auto point = [&](float x, float y, float z) { return Point{center.x + x, center.y + y, center.z + z}; };
    for (int axis = 0; axis < 3; ++axis)
    {
        for (float sign : {-1.f, 1.f})
        {
            int u = (axis + 1) % 3, v = (axis + 2) % 3;
            float a = h[u], b = h[v];
            const float ring[][2] = {{-a + radius, -b}, {a - radius, -b}, {a, -b + radius},
                                     {a, b - radius},   {a - radius, b},  {-a + radius, b},
                                     {-a, b - radius},  {-a, -b + radius}};
            std::vector<Point> vertices;
            for (int i = 0; i < 8; ++i)
            {
                if (radius == 0 && i % 2 == 1)
                {
                    continue;
                }
                float p[3] = {};
                p[axis] = sign * h[axis];
                p[u] = ring[i][0];
                p[v] = ring[i][1];
                vertices.push_back(point(p[0], p[1], p[2]));
            }
            DetailFace(m, vertices, color, slot, joint, center);
        }
    }
    if (radius == 0)
    {
        return;
    }
    for (int a = 0; a < 3; ++a)
    {
        for (int b = a + 1; b < 3; ++b)
        {
            for (float sa : {-1.f, 1.f})
            {
                for (float sb : {-1.f, 1.f})
                {
                    int c = 3 - a - b;
                    std::vector<Point> vertices;
                    for (int k = 0; k < 4; ++k)
                    {
                        float p[3] = {};
                        bool first = k == 0 || k == 3;
                        p[a] = sa * (h[a] - (first ? 0 : radius));
                        p[b] = sb * (h[b] - (first ? radius : 0));
                        p[c] = (k < 2 ? -1.f : 1.f) * (h[c] - radius);
                        vertices.push_back(point(p[0], p[1], p[2]));
                    }
                    DetailFace(m, vertices, color, slot, joint, center);
                }
            }
        }
    }
    for (float sx : {-1.f, 1.f})
    {
        for (float sy : {-1.f, 1.f})
        {
            for (float sz : {-1.f, 1.f})
            {
                std::vector<Point> vertices = {point(sx * h[0], sy * (h[1] - radius), sz * (h[2] - radius)),
                                               point(sx * (h[0] - radius), sy * h[1], sz * (h[2] - radius)),
                                               point(sx * (h[0] - radius), sy * (h[1] - radius), sz * h[2])};
                DetailFace(m, vertices, color, slot, joint, center);
            }
        }
    }
}
void Ellipsoid(Model &m, Point center, Point radius, std::array<float, 4> color, PaletteSlot slot,
               Joint joint = Joint::Root, int segments = 12, int rings = 5, bool stone = false)
{
    auto at = [&](int ring, int segment) {
        float latitude = -1.5707963f + ring * 3.14159265f / rings,
              longitude = segment * 6.2831853f / segments;
        float rough = stone ? (1 + .10f * std::sin(segment * 13.f + ring * 7.f)) : 1;
        return Point{center.x + radius.x * std::cos(latitude) * std::cos(longitude) * rough,
                     center.y + radius.y * std::cos(latitude) * std::sin(longitude) * rough,
                     center.z + radius.z * std::sin(latitude)};
    };
    for (int ring = 0; ring < rings; ++ring)
    {
        for (int segment = 0; segment < segments; ++segment)
        {
            std::vector<Point> v;
            if (ring == 0)
            {
                v = {at(0, segment), at(1, segment + 1), at(1, segment)};
            }
            else if (ring == rings - 1)
            {
                v = {at(ring, segment), at(ring, segment + 1), at(rings, segment)};
            }
            else
            {
                v = {at(ring, segment), at(ring, segment + 1), at(ring + 1, segment + 1),
                     at(ring + 1, segment)};
            }
            DetailFace(m, v, color, slot, joint, center);
            auto &part = m.parts[m.count - 1];
            if (stone)
            {
                part.surface = Surface::Stone;
                continue;
            }
            for (size_t k = 0; k < part.count; ++k)
            {
                auto p = part.points[k];
                Point n = {(p.x - center.x) / (radius.x * radius.x), (p.y - center.y) / (radius.y * radius.y),
                           (p.z - center.z) / (radius.z * radius.z)};
                float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                part.normals[k] = {n.x / len, n.y / len, n.z / len};
            }
        }
    }
}
void Tube(Model &m, Point a, Point b, float r0, float r1, std::array<float, 4> color, PaletteSlot slot,
          int segments = 12, Surface surface = Surface::Metal)
{
    Point axis = {b.x - a.x, b.y - a.y, b.z - a.z};
    float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    axis = {axis.x / length, axis.y / length, axis.z / length};
    Point u = std::fabs(axis.z) > .9f ? Point{0, 1, 0} : Point{-axis.y, axis.x, 0};
    float len = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    u = {u.x / len, u.y / len, u.z / len};
    Point v = {axis.y * u.z - axis.z * u.y, axis.z * u.x - axis.x * u.z, axis.x * u.y - axis.y * u.x};
    auto at = [&](Point p, float r, int i) {
        float t = i * 6.2831853f / segments;
        return Point{p.x + r * (u.x * std::cos(t) + v.x * std::sin(t)),
                     p.y + r * (u.y * std::cos(t) + v.y * std::sin(t)),
                     p.z + r * (u.z * std::cos(t) + v.z * std::sin(t))};
    };
    Point center = {(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2};
    for (int i = 0; i < segments; ++i)
    {
        DetailFace(m, {at(a, r0, i), at(a, r0, i + 1), at(b, r1, i + 1), at(b, r1, i)}, color, slot,
                   Joint::Root, center);
        m.parts[m.count - 1].surface = surface;
        DetailFace(m, {a, at(a, r0, i + 1), at(a, r0, i)}, color, slot, Joint::Root, center);
        m.parts[m.count - 1].surface = surface;
        DetailFace(m, {b, at(b, r1, i), at(b, r1, i + 1)}, color, slot, Joint::Root, center);
        m.parts[m.count - 1].surface = surface;
    }
}

void ClothingBox(Model &m, Point lo, Point hi, std::array<float, 4> color,
                 PaletteSlot slot = PaletteSlot::Body, Joint joint = Joint::Root)
{
    auto first = m.count;
    MeshBox(m, lo, hi, color, slot, joint);
    for (auto i = first; i < m.count; ++i)
    {
        m.parts[i].clothing = true;
    }
}
void Garment(Model &m, float hem, float shoulders, float hemWidth, float shoulderWidth, float front,
             std::array<float, 4> color, PaletteSlot slot = PaletteSlot::Body)
{
    const Point v[] = {{-.13f, -hemWidth, hem},
                       {front, -hemWidth, hem},
                       {front, hemWidth, hem},
                       {-.13f, hemWidth, hem},
                       {-.13f, -shoulderWidth, shoulders},
                       {front, -shoulderWidth, shoulders},
                       {front, shoulderWidth, shoulders},
                       {-.13f, shoulderWidth, shoulders}};
    const int faces[][4] = {{0, 3, 2, 1}, {0, 1, 5, 4}, {1, 2, 6, 5},
                            {2, 3, 7, 6}, {3, 0, 4, 7}, {4, 5, 6, 7}};
    for (const auto &f : faces)
    {
        AddVariantPart(m, PartKind::Polygon, {v[f[0]], v[f[1]], v[f[2]], v[f[3]]}, color, slot);
        m.parts[m.count - 1].clothing = true;
    }
}
void Badge(Model &m, float x, float y, float z, float w, float h, std::array<float, 4> color,
           PaletteSlot slot = PaletteSlot::Fixed)
{
    AddVariantPart(m, PartKind::Polygon,
                   {{x, y - w / 2, z - h / 2},
                    {x, y + w / 2, z - h / 2},
                    {x, y + w / 2, z + h / 2},
                    {x, y - w / 2, z + h / 2}},
                   color, slot);
    m.parts[m.count - 1].clothing = true;
}
constexpr Joint kParents[] = {Joint::Root, Joint::Root,    Joint::Root,        Joint::RightArm,
                              Joint::Root, Joint::LeftArm, Joint::Root,        Joint::RightLeg,
                              Joint::Root, Joint::LeftLeg, Joint::RightForearm};
} // namespace

// ============================================================================
// Transform3D / ModelRig — CPU skinning of rigid limb meshes
// ============================================================================
Point Transform3D::Apply(Point p) const
{
    const float cr = std::cos(roll), sr = std::sin(roll), cp = std::cos(pitch), sp = std::sin(pitch),
                cy = std::cos(yaw), sy = std::sin(yaw);
    p = {p.x, p.y * cr - p.z * sr, p.y * sr + p.z * cr};
    p = {p.x * cp + p.z * sp, p.y, -p.x * sp + p.z * cp};
    return {p.x * cy - p.y * sy + position.x, p.x * sy + p.y * cy + position.y, p.z + position.z};
}
Point CharacterPose::Apply(Point p, Joint joint) const
{
    if (joint == Joint::Weapon)
    {
        // The wrist stabilizes the barrel parallel to the aim plane. Its position
        // comes from the full shoulder -> elbow -> hand chain, including recoil.
        Point wrist = Apply({0, 0, -.27f}, Joint::RightForearm);
        return {p.x + wrist.x, p.y + wrist.y, p.z + wrist.z};
    }
    for (;;)
    {
        p = joints[size_t(joint)].Apply(p);
        if (joint == Joint::Root)
        {
            return p;
        }
        joint = kParents[size_t(joint)];
    }
}
CharacterPose ModelRig::Pose(float phase, bool walking, bool armed, float recoil, bool riding)
{
    CharacterPose p{};
    p.armed = armed;
    auto &j = p.joints;
    float step = walking ? std::sin(phase) * .48f : 0;
    j[size_t(Joint::Root)].position.z = walking && !armed ? std::fabs(std::sin(phase)) * .025f : 0;
    j[size_t(Joint::Head)].position = {0, 0, 1.04f};
    j[size_t(Joint::RightArm)].position = {0, -.20f, .94f};
    j[size_t(Joint::LeftArm)].position = {0, .20f, .94f};
    j[size_t(Joint::RightForearm)].position = {0, 0, -.29f};
    j[size_t(Joint::LeftForearm)].position = {0, 0, -.29f};
    j[size_t(Joint::RightLeg)].position = {0, -.105f, .57f};
    j[size_t(Joint::LeftLeg)].position = {0, .105f, .57f};
    j[size_t(Joint::RightShin)].position = {0, 0, -.26f};
    j[size_t(Joint::LeftShin)].position = {0, 0, -.26f};
    j[size_t(Joint::RightLeg)].pitch = step;
    j[size_t(Joint::LeftLeg)].pitch = -step;
    j[size_t(Joint::RightShin)].pitch = (std::max)(0.f, -step) * .6f;
    j[size_t(Joint::LeftShin)].pitch = (std::max)(0.f, step) * .6f;
    j[size_t(Joint::RightArm)].pitch = -step * .65f;
    j[size_t(Joint::LeftArm)].pitch = step * .65f;
    if (armed || riding)
    {
        // Two-bone IK: both hands meet the grip/handlebar while sleeves remain
        // on their own bones, outside the torso and below the face.
        float back = (std::max)(0.f, (std::min)(1.f, recoil)) * .045f;
        for (int side = 0; side < 2; ++side)
        {
            Joint arm = side ? Joint::LeftArm : Joint::RightArm;
            Joint fore = side ? Joint::LeftForearm : Joint::RightForearm;
            auto &a = j[size_t(arm)];
            auto &f = j[size_t(fore)];
            Point target = {riding ? .43f : .36f - back, riding ? (side ? .18f : -.18f) : 0.f,
                            riding ? .80f : 26.f / 44};
            float dx = target.x - a.position.x, dy = target.y - a.position.y, dz = target.z - a.position.z;
            float r = std::sqrt(dx * dx + dy * dy), d = std::sqrt(r * r + dz * dz);
            d = (std::min)(.559f, (std::max)(.03f, d));
            float theta = std::atan2(dz, r);
            float bend = std::acos(
                (std::max)(-1.f, (std::min)(1.f, (.29f * .29f + d * d - .27f * .27f) / (.58f * d))));
            float upper = theta - bend;
            float lower = std::atan2(dz - .29f * std::sin(upper), r - .29f * std::cos(upper));
            a.yaw = std::atan2(dy, dx);
            a.pitch = -1.5707963f - upper;
            f.pitch = upper - lower;
        }
    }
    if (riding)
    {
        j[size_t(Joint::Root)].position.z = -.40f;
        j[size_t(Joint::RightLeg)].pitch = -1.1f;
        j[size_t(Joint::LeftLeg)].pitch = -1.1f;
        j[size_t(Joint::RightShin)].pitch = 1.5f;
        j[size_t(Joint::LeftShin)].pitch = 1.5f;
    }
    return p;
}
Point ModelRig::Muzzle(const CharacterPose &pose)
{
    return pose.Apply({.34f, 0, 0}, Joint::Weapon);
}

void ModelFactory::GenerateVehicle(VehicleModel &result, VehicleType type)
{
    auto &m = result.body;
    m.count = 0;
    m.space = CoordinateSpace::World;
    bool bike = type == VehicleType::JetBike;
    float length = bike ? .95f : type == VehicleType::Hauler ? 1.50f : 1.24f;
    float w = bike ? .17f : .38f;
    MeshBox(m, {-length, -w, .15f}, {length * .82f, w, .43f}, {.85f, .9f, 1, 1});
    MeshBox(m, {-length * .93f, -w * .96f, .08f}, {length * .9f, w * .96f, .18f}, {.18f, .22f, .26f, 1},
            PaletteSlot::Fixed);
    if (bike)
    {
        MeshBox(m, {-.38f, -.16f, .43f}, {.13f, .16f, .55f}, {.12f, .15f, .19f, 1}, PaletteSlot::Fixed);
        MeshBox(m, {.16f, -.22f, .42f}, {.68f, .22f, .65f}, {1, 1, 1, 1}, PaletteSlot::Accent);
        MeshBox(m, {.36f, -.31f, .64f}, {.43f, .31f, .69f}, {.42f, .48f, .52f, 1}, PaletteSlot::Fixed);
    }
    else
    {
        float roof = type == VehicleType::Shuttle ? .96f : .78f;
        MeshBox(m, {-.65f, -w * .84f, .43f}, {.48f, w * .84f, roof}, {.65f, .76f, .86f, 1});
        // Solid tinted window insets, offset from the shell to avoid z fighting.
        MeshBox(m, {.481f, -w * .75f, .50f}, {.492f, w * .75f, roof - .06f}, {.09f, .27f, .34f, 1},
                PaletteSlot::Fixed);
        for (float side : {-1.f, 1.f})
        {
            MeshBox(m, {-.52f, side * w * .85f - .008f, .51f}, {.35f, side * w * .85f + .008f, roof - .06f},
                    {.08f, .24f, .31f, 1}, PaletteSlot::Fixed);
        }
        if (type == VehicleType::Hauler)
        {
            MeshBox(m, {-1.45f, -.37f, .44f}, {-.65f, .37f, 1.08f}, {1, 1, 1, 1}, PaletteSlot::Accent);
        }
        if (type == VehicleType::Patrol)
        {
            MeshBox(m, {-.2f, -.25f, roof}, {.06f, .25f, roof + .10f}, {1, .35f, .25f, 1},
                    PaletteSlot::Emission);
        }
    }
    for (int side = 0; side < 2; ++side)
    {
        float y = (side ? 1.f : -1.f) * (bike ? .27f : .36f);
        MeshBox(m, {-length * .85f, y - .10f, 0}, {length * .65f, y + .10f, .14f}, {.13f, .17f, .21f, 1},
                PaletteSlot::Fixed);
        result.exhausts[side] = {-length - .03f, y, .20f};
        result.headlights[side] = {length * .84f, y * .75f, .34f};
        MeshBox(m, {length * .82f, y * .75f - .055f, .29f}, {length * .88f, y * .75f + .055f, .39f},
                {1, .93f, .7f, 1}, PaletteSlot::Fixed);
        MeshBox(m, {-length - .01f, y - .06f, .17f}, {-length + .03f, y + .06f, .25f}, {1, .3f, .16f, 1},
                PaletteSlot::Fixed);
    }
    for (int side = 0; side < 2; ++side)
    {
        auto nozzle = result.exhausts[side];
        Tube(m, {nozzle.x + .18f, nozzle.y, nozzle.z}, {nozzle.x - .04f, nozzle.y, nozzle.z}, .105f, .14f,
             {.3f, .36f, .4f, 1}, PaletteSlot::Fixed);
        Tube(m, {nozzle.x - .041f, nozzle.y, nozzle.z}, {nozzle.x - .046f, nozzle.y, nozzle.z}, .10f, .10f,
             {1, 1, 1, 1}, PaletteSlot::Emission);
        for (int vent = 0; vent < 4; ++vent)
        {
            MeshBox(m, {-.35f + vent * .13f, (side ? 1.f : -1.f) * w - .008f, .25f},
                    {-.30f + vent * .13f, (side ? 1.f : -1.f) * w + .008f, .36f}, {.12f, .17f, .20f, 1},
                    PaletteSlot::Fixed);
        }
    }
    if (!bike)
    {
        MeshBox(m, {.55f, -w * .65f, .432f}, {.9f, w * .65f, .45f}, {.55f, .62f, .68f, 1},
                PaletteSlot::Accent);
        for (float side : {-1.f, 1.f})
        {
            MeshBox(m, {-.15f, side * w - .035f, .45f}, {.05f, side * w + .035f, .48f}, {.7f, .76f, .8f, 1},
                    PaletteSlot::Fixed);
        }
    }
}
void ModelFactory::GenerateCharacter(Model &m, CharacterType type)
{
    m.count = 0;
    m.space = CoordinateSpace::World;
    const bool medic = type == CharacterType::Medic, researcher = type == CharacterType::Researcher,
               engineer = type == CharacterType::Engineer, guard = type == CharacterType::Guard;
    PaletteSlot fabricSlot = medic ? PaletteSlot::Fixed : PaletteSlot::Body;
    std::array<float, 4> fabric =
        medic ? std::array<float, 4>{.93f, .96f, .94f, 1} : std::array<float, 4>{1, 1, 1, 1};
    // Outfit shells replace the torso surface. Each role has a different cut.
    Garment(m, .55f, .98f,
            guard        ? .15f
            : researcher ? .19f
                         : .15f,
            guard ? .19f : .16f, guard ? .16f : .13f, fabric, fabricSlot);
    if (type == CharacterType::Explorer)
    {
        ClothingBox(m, {-.26f, -.14f, .65f}, {-.135f, .14f, .91f}, {.45f, .51f, .48f, 1}, PaletteSlot::Fixed);
        Badge(m, .131f, -.09f, .81f, .045f, .29f, {1, 1, 1, 1}, PaletteSlot::Accent);
        Badge(m, .132f, .06f, .79f, .12f, .09f, {.55f, .62f, .68f, 1}, PaletteSlot::Fixed);
    }
    else if (engineer)
    {
        // Coverall bib, safety harness, tool belt and hard hat.
        ClothingBox(m, {-.145f, -.19f, .56f}, {.15f, .19f, .62f}, {.13f, .16f, .18f, 1}, PaletteSlot::Fixed);
        Badge(m, .131f, 0, .80f, .20f, .22f, {.42f, .48f, .53f, 1}, PaletteSlot::Fixed);
        for (float side : {-1.f, 1.f})
        {
            Badge(m, .132f, side * .11f, .82f, .035f, .28f, {1, 1, 1, 1}, PaletteSlot::Accent);
            ClothingBox(m, {-.11f, side * .20f - .045f, .50f}, {.08f, side * .20f + .045f, .64f},
                        {.24f, .28f, .30f, 1}, PaletteSlot::Fixed);
        }
    }
    else if (guard)
    {
        ClothingBox(m, {.161f, -.14f, .69f}, {.20f, .14f, .91f}, {.17f, .24f, .29f, 1}, PaletteSlot::Fixed);
        ClothingBox(m, {-.20f, -.14f, .66f}, {-.131f, .14f, .94f}, {.17f, .24f, .29f, 1}, PaletteSlot::Fixed);
        Badge(m, .201f, 0, .81f, .19f, .035f, {1, 1, 1, 1}, PaletteSlot::Accent);
    }
    else if (medic)
    {
        Badge(m, .131f, 0, .81f, .045f, .15f, {.85f, .16f, .18f, 1});
        Badge(m, .132f, 0, .81f, .13f, .045f, {.85f, .16f, .18f, 1});
        ClothingBox(m, {-.26f, -.16f, .51f}, {-.135f, .16f, .74f}, {.86f, .88f, .83f, 1}, PaletteSlot::Fixed);
    }
    else if (researcher)
    {
        // Long split coat, lapels, scarf and a shoulder satchel.
        Badge(m, .131f, -.09f, .86f, .055f, .20f, {.82f, .84f, .78f, 1});
        Badge(m, .132f, .09f, .86f, .055f, .20f, {.82f, .84f, .78f, 1});
        ClothingBox(m, {-.14f, -.10f, .94f}, {.14f, .10f, 1.00f}, {1, 1, 1, 1}, PaletteSlot::Accent);
        ClothingBox(m, {-.25f, .19f, .48f}, {-.10f, .30f, .68f}, {.30f, .22f, .17f, 1}, PaletteSlot::Fixed);
    }
    // Skin and hair are separate from all collars and sleeves.
    MeshBox(m, {-.055f, -.055f, .99f}, {.06f, .055f, 1.06f}, {.76f, .56f, .42f, 1}, PaletteSlot::Fixed);
    Ellipsoid(m, {0, 0, .11f}, {.116f, .112f, .11f}, {.81f, .62f, .48f, 1}, PaletteSlot::Fixed, Joint::Head);
    Ellipsoid(m, {-.018f, 0, .21f}, {.114f, .115f, .045f}, {.12f, .09f, .08f, 1}, PaletteSlot::Fixed,
              Joint::Head, 12, 4);
    Ellipsoid(m, {.116f, 0, .096f}, {.025f, .025f, .035f}, {.81f, .62f, .48f, 1}, PaletteSlot::Fixed,
              Joint::Head, 8, 3);
    for (float side : {-1.f, 1.f})
    {
        Ellipsoid(m, {.104f, side * .048f, .135f}, {.015f, .013f, .010f}, {.09f, .12f, .14f, 1},
                  PaletteSlot::Fixed, Joint::Head, 8, 3);
        Ellipsoid(m, {-.01f, side * .11f, .095f}, {.025f, .016f, .035f}, {.78f, .58f, .44f, 1},
                  PaletteSlot::Fixed, Joint::Head, 8, 3);
    }
    if (engineer)
    {
        ClothingBox(m, {-.14f, -.14f, .20f}, {.14f, .14f, .30f}, {1, .8f, .3f, 1}, PaletteSlot::Fixed,
                    Joint::Head);
        ClothingBox(m, {-.16f, -.16f, .19f}, {.16f, .16f, .215f}, {1, .8f, .3f, 1}, PaletteSlot::Fixed,
                    Joint::Head);
    }
    else if (guard)
    {
        ClothingBox(m, {-.14f, -.14f, .13f}, {.105f, .14f, .28f}, {.16f, .23f, .28f, 1}, PaletteSlot::Fixed,
                    Joint::Head);
        ClothingBox(m, {.126f, -.12f, .10f}, {.14f, .12f, .16f}, {.25f, .8f, .9f, 1}, PaletteSlot::Fixed,
                    Joint::Head);
    }
    else if (medic)
    {
        ClothingBox(m, {-.12f, -.12f, .20f}, {.12f, .12f, .28f}, fabric, PaletteSlot::Fixed, Joint::Head);
    }
    for (int side = 0; side < 2; ++side)
    {
        Joint arm = side ? Joint::LeftArm : Joint::RightArm,
              fore = side ? Joint::LeftForearm : Joint::RightForearm,
              leg = side ? Joint::LeftLeg : Joint::RightLeg, shin = side ? Joint::LeftShin : Joint::RightShin;
        ClothingBox(m, {-.065f, -.065f, -.285f}, {.065f, .065f, .01f}, fabric, fabricSlot, arm);
        ClothingBox(m, {-.055f, -.055f, -.22f}, {.055f, .055f, .005f}, fabric, fabricSlot, fore);
        MeshBox(m, {-.05f, -.05f, -.29f}, {.05f, .05f, -.22f}, {.8f, .61f, .46f, 1}, PaletteSlot::Fixed,
                fore);
        ClothingBox(m, {-.075f, -.075f, -.255f}, {.075f, .075f, 0},
                    engineer ? std::array<float, 4>{.65f, .70f, .73f, 1}
                             : std::array<float, 4>{.14f, .2f, .25f, 1},
                    engineer ? PaletteSlot::Body : PaletteSlot::Fixed, leg);
        ClothingBox(m, {-.068f, -.068f, -.235f}, {.068f, .068f, .005f},
                    engineer ? std::array<float, 4>{.65f, .70f, .73f, 1}
                             : std::array<float, 4>{.17f, .23f, .28f, 1},
                    engineer ? PaletteSlot::Body : PaletteSlot::Fixed, shin);
        ClothingBox(m, {-.075f, -.078f, -.31f}, {.16f, .078f, -.235f}, {.07f, .1f, .13f, 1},
                    PaletteSlot::Fixed, shin);
        if (medic || researcher)
        {
            ClothingBox(m, {-.16f, -.095f, researcher ? -.37f : -.25f}, {.14f, .095f, .05f}, fabric,
                        fabricSlot, leg);
        }
        if (guard)
        {
            ClothingBox(m, {-.085f, -.09f, -.10f}, {.085f, .09f, .035f}, {.24f, .32f, .38f, 1},
                        PaletteSlot::Fixed, arm);
            ClothingBox(m, {-.065f, -.065f, -.20f}, {.065f, .065f, -.05f}, {.20f, .27f, .32f, 1},
                        PaletteSlot::Fixed, fore);
        }
    }
    MeshBox(m, {-.035f, -.035f, -.10f}, {.045f, .035f, .015f}, {.17f, .19f, .21f, 1}, PaletteSlot::Fixed,
            Joint::Weapon);
    MeshBox(m, {0, -.039f, -.035f}, {.34f, .039f, .035f}, {.35f, .39f, .43f, 1}, PaletteSlot::Fixed,
            Joint::Weapon);
}
void ModelFactory::GenerateBuilding(Model &result, BuildingType type)
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
        AddVariantPart(result, PartKind::Polygon, {{0, 0, .78f}, {1, 0, .78f}, {1, .5f, 1}, {0, .5f, 1}},
                       {.78f, .8f, .9f, 1}, PaletteSlot::Accent);
        AddVariantPart(result, PartKind::Polygon, {{0, .5f, 1}, {1, .5f, 1}, {1, 1, .78f}, {0, 1, .78f}},
                       {.55f, .65f, .8f, 1}, PaletteSlot::Accent);
    }
    else if (type == BuildingType::Terraced)
    {
        Box(result, 0, 0, 1, 1, 0, .36f);
        Box(result, .05f, .05f, .7f, .7f, .36f, .7f);
        Box(result, .1f, .1f, .42f, .42f, .7f, 1);
        AddVariantPart(result, PartKind::Line, {{.8f, .85f, .37f}, {.95f, .85f, .37f}}, {1, 1, 1, 1},
                       PaletteSlot::Accent, 4);
    }
    else if (type == BuildingType::Workshop)
    {
        Box(result, 0, 0, 1, 1, 0, .45f);
        AddVariantPart(result, PartKind::Polygon, {{0, 0, .45f}, {1, 0, .45f}, {1, .5f, .8f}, {0, .5f, .8f}},
                       {.8f, .85f, .9f, 1}, PaletteSlot::Accent);
        AddVariantPart(result, PartKind::Polygon, {{0, .5f, .8f}, {1, .5f, .8f}, {1, 1, .45f}, {0, 1, .45f}},
                       {.5f, .65f, .75f, 1}, PaletteSlot::Accent);
        Box(result, .65f, .05f, .18f, .18f, .6f, 1);
        AddVariantPart(result, PartKind::Polygon,
                       {{.15f, 1, .02f}, {.7f, 1, .02f}, {.7f, 1, .35f}, {.15f, 1, .35f}},
                       {.2f, .25f, .3f, 1}, PaletteSlot::Fixed);
    }
    else
    {
        Box(result, 0, 0, 1, 1, 0, type == BuildingType::Observatory ? .3f : .2f);
        Dome(result, type == BuildingType::Observatory ? .3f : .2f,
             type == BuildingType::Observatory ? .6f : .75f);
        if (type == BuildingType::Observatory)
        {
            AddVariantPart(result, PartKind::Line, {{.5f, .5f, .7f}, {.82f, .2f, 1.12f}},
                           {.75f, .85f, .9f, 1}, PaletteSlot::Fixed, 12);
            AddVariantPart(result, PartKind::Line, {{.74f, .18f, 1.1f}, {.9f, .22f, 1.14f}}, {1, 1, 1, 1},
                           PaletteSlot::Emission, 5);
            Box(result, .35f, .8f, .3f, .2f, 0, .28f);
        }
    }
    // Each row belongs to an actual facade, including the recessed upper tiers.
    auto windows = [&](float edge, float start, float span, float z, float height) {
        for (int i = 0; i < 3; ++i)
        {
            float t = start + span * (i + .2f) / 3, width = span * .55f / 3;
            AddVariantPart(result, PartKind::Glass,
                           {{edge + .002f, t, z},
                            {edge + .002f, t + width, z},
                            {edge + .002f, t + width, z + height},
                            {edge + .002f, t, z + height}},
                           {.17f, .39f, .48f, 1}, PaletteSlot::Fixed);
            AddVariantPart(result, PartKind::Glass,
                           {{t, edge + .002f, z},
                            {t, edge + .002f, z + height},
                            {t + width, edge + .002f, z + height},
                            {t + width, edge + .002f, z}},
                           {.17f, .39f, .48f, 1}, PaletteSlot::Fixed);
        }
    };
    if (type == BuildingType::Habitat)
    {
        windows(1, .10f, .73f, .22f, .10f);
        windows(1, .10f, .73f, .43f, .10f);
    }
    else if (type == BuildingType::Terraced)
    {
        windows(1, .10f, .73f, .17f, .10f);
        windows(.75f, .12f, .55f, .48f, .10f);
        windows(.52f, .15f, .31f, .80f, .08f);
    }
    else if (type == BuildingType::Workshop)
    {
        windows(1, .10f, .73f, .28f, .10f);
    }
    else
    {
        windows(1, .10f, .73f, .06f, .08f);
    }
    if (type != BuildingType::Dome && type != BuildingType::Observatory)
    {
        float roof = type == BuildingType::Terraced ? 1.f : type == BuildingType::Habitat ? .82f : .52f;
        MeshBox(result, {.10f, .10f, roof}, {.30f, .29f, roof + .12f}, {.44f, .5f, .52f, 1},
                PaletteSlot::Fixed);
        for (int i = 0; i < 4; ++i)
        {
            MeshBox(result, {.105f, .12f + i * .035f, roof + .121f}, {.295f, .133f + i * .035f, roof + .135f},
                    {.15f, .22f, .25f, 1}, PaletteSlot::Fixed);
        }
    }
}
bool ModelFactory::ValidateModel(const Model &model)
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
        if (unsigned(part.joint) >= unsigned(Joint::Count) || part.count > 8 || part.count < 2 ||
            unsigned(part.kind) > 3 || unsigned(part.palette) > 3 || !std::isfinite(part.width) ||
            part.width <= 0 || unsigned(part.surface) > unsigned(Surface::Skin))
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
            auto n = part.normals[j];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(n.x) ||
                !std::isfinite(n.y) || !std::isfinite(n.z))
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

            Point normal = {};
            for (size_t j = 0; j < part.count; ++j)
            {
                auto a = part.points[j], b = part.points[(j + 1) % part.count];
                normal.x += (a.y - b.y) * (a.z + b.z);
                normal.y += (a.z - b.z) * (a.x + b.x);
                normal.z += (a.x - b.x) * (a.y + b.y);
            }
            float n = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
            if (n < 1.e-14f)
            {
                return false;
            }
            for (size_t j = 0; j < part.count; ++j)
            {
                auto a = part.points[j], b = part.points[(j + 1) % part.count],
                     c = part.points[(j + 2) % part.count];
                Point u = {b.x - a.x, b.y - a.y, b.z - a.z}, v = {c.x - b.x, c.y - b.y, c.z - b.z};
                Point cross = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
                if (cross.x * normal.x + cross.y * normal.y + cross.z * normal.z < -1.e-8f)
                {
                    return false;
                }
            }
        }
    }
    return true;
}
// ============================================================================
// ModelLibrary
// ============================================================================
namespace
{
constexpr std::uint32_t kModelVersion = 7;
static_assert(std::is_trivially_copyable<Library>::value, "Model cache needs a stable POD payload");

void Generate(Library &library)
{
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
    library.box.space = CoordinateSpace::World;
    MeshBox(library.box, {0, 0, 0}, {1, 1, 1}, {1, 1, 1, 1});
    library.drone.space = library.salvage.space = library.recoveryKit.space = CoordinateSpace::World;
    MeshBox(library.drone, {-.34f, -.30f, .24f}, {.34f, .30f, .53f}, {1, 1, 1, 1});
    MeshBox(library.drone, {.341f, -.11f, .32f}, {.38f, .11f, .43f}, {1, .25f, .12f, 1},
            PaletteSlot::Emission);
    for (float side : {-1.f, 1.f})
    {
        MeshBox(library.drone, {-.48f, side * .35f - .07f, .12f}, {.48f, side * .35f + .07f, .24f},
                {.35f, .4f, .45f, 1}, PaletteSlot::Accent);
    }
    MeshBox(library.salvage, {-.13f, -.13f, .02f}, {.13f, .13f, .25f}, {.3f, .87f, .94f, 1},
            PaletteSlot::Fixed);
    MeshBox(library.recoveryKit, {-.17f, -.12f, .02f}, {.17f, .12f, .25f}, {.2f, .65f, .4f, 1},
            PaletteSlot::Fixed);
    MeshBox(library.recoveryKit, {.171f, -.035f, .07f}, {.18f, .035f, .21f}, {.85f, 1, .9f, 1},
            PaletteSlot::Fixed);
    MeshBox(library.recoveryKit, {.171f, -.085f, .12f}, {.181f, .085f, .16f}, {.85f, 1, .9f, 1},
            PaletteSlot::Fixed);
    for (auto &ground : library.grounds)
    {
        ground.space = CoordinateSpace::World;
        AddVariantPart(ground, PartKind::Polygon,
                       {{0, 0, 0}, {1.001f, 0, 0}, {1.001f, 1.001f, 0}, {0, 1.001f, 0}}, {1, 1, 1, 1},
                       PaletteSlot::Body);
        ground.parts[0].surface = Surface::Stone;
    }
    library.grounds[1].parts[0].surface = Surface::Rubber; // Rough, non-metallic asphalt.
    library.grounds[2].parts[0].surface = Surface::Metal;
    library.grounds[3].parts[0].surface = Surface::Glass;
    library.rock.space = library.telescope.space = library.console.space = CoordinateSpace::World;
    Ellipsoid(library.rock, {.5f, .5f, .29f}, {.58f, .58f, .33f}, {1, 1, 1, 1}, PaletteSlot::Body,
              Joint::Root, 12, 5, true);
    auto &telescope = library.telescope;
    Tube(telescope, {0, 0, .06f}, {0, 0, .85f}, .10f, .08f, {.38f, .45f, .5f, 1}, PaletteSlot::Fixed);
    for (int i = 0; i < 3; ++i)
    {
        float angle = i * 6.2831853f / 3;
        Tube(telescope, {0, 0, .35f}, {std::cos(angle) * .4f, std::sin(angle) * .4f, .02f}, .045f, .065f,
             {.28f, .36f, .41f, 1}, PaletteSlot::Fixed, 8);
    }
    Tube(telescope, {-.38f, 0, .91f}, {.62f, 0, 1.25f}, .16f, .23f, {.75f, .8f, .83f, 1}, PaletteSlot::Body,
         16);
    Tube(telescope, {.62f, 0, 1.25f}, {.67f, 0, 1.267f}, .235f, .235f, {.12f, .34f, .43f, 1},
         PaletteSlot::Fixed, 16, Surface::Glass);
    MeshBox(telescope, {-.18f, -.22f, .66f}, {.18f, .22f, .83f}, {.26f, .34f, .4f, 1}, PaletteSlot::Accent);
    auto &console = library.console;
    MeshBox(console, {-.36f, -.25f, 0}, {.36f, .25f, .48f}, {.35f, .43f, .47f, 1});
    MeshBox(console, {-.4f, -.23f, .48f}, {.4f, .17f, .60f}, {.17f, .24f, .29f, 1}, PaletteSlot::Fixed);
    MeshBox(console, {-.38f, .12f, .60f}, {.38f, .18f, .98f}, {.22f, .3f, .35f, 1}, PaletteSlot::Fixed);
    AddVariantPart(console, PartKind::Glass,
                   {{-.32f, .181f, .67f}, {.32f, .181f, .67f}, {.32f, .181f, .92f}, {-.32f, .181f, .92f}},
                   {.2f, .75f, .82f, 1}, PaletteSlot::Emission);
    for (int i = 0; i < 5; ++i)
    {
        MeshBox(console, {-.30f + i * .13f, -.15f, .602f}, {-.24f + i * .13f, -.06f, .62f},
                {.5f, .75f, .78f, 1}, PaletteSlot::Fixed);
    }
    for (size_t i = 0; i < library.vehicles.size(); ++i)
    {
        ModelFactory::GenerateVehicle(library.vehicles[i], VehicleType(i));
    }
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        ModelFactory::GenerateCharacter(library.characters[i], CharacterType(i));
    }
    for (size_t i = 0; i < library.buildings.size(); ++i)
    {
        ModelFactory::GenerateBuilding(library.buildings[i], BuildingType(i));
    }
}

bool Valid(const Library &library)
{
    for (const auto &vehicle : library.vehicles)
    {
        if (!ModelFactory::ValidateModel(vehicle.body))
        {
            return false;
        }
        for (size_t i = 0; i < 2; ++i)
        {
            if (!std::isfinite(vehicle.exhausts[i].x) || !std::isfinite(vehicle.exhausts[i].y) ||
                !std::isfinite(vehicle.exhausts[i].z) || !std::isfinite(vehicle.headlights[i].x) ||
                !std::isfinite(vehicle.headlights[i].y) || !std::isfinite(vehicle.headlights[i].z) ||
                vehicle.exhausts[i].x >= 0 || vehicle.headlights[i].x <= 0)
            {
                return false;
            }
        }
    }
    for (const auto &character : library.characters)
    {
        if (!ModelFactory::ValidateModel(character))
        {
            return false;
        }
    }
    for (const auto &building : library.buildings)
    {
        if (!ModelFactory::ValidateModel(building))
        {
            return false;
        }
    }
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
    for (const Model *model : {&library.box, &library.drone, &library.salvage, &library.recoveryKit,
                               &library.rock, &library.telescope, &library.console})
    {
        if (!ModelFactory::ValidateModel(*model))
        {
            return false;
        }
    }
    for (const auto &ground : library.grounds)
    {
        if (!ModelFactory::ValidateModel(ground))
        {
            return false;
        }
    }
    return true;
}

std::unique_ptr<Library> LoadOrGenerate()
{
    auto library = std::make_unique<Library>();
    std::vector<unsigned char> bytes;
    if (assetcache::Load("models.bin", kModelVersion, sizeof(Library), bytes))
    {
        std::memcpy(library.get(), bytes.data(), sizeof(Library));
        if (Valid(*library))
        {
            return library;
        }
    }
    // Invalid cached bytes must not survive in unused vertices or palette slots.
    library = std::make_unique<Library>();
    Generate(*library);
    if (!Valid(*library))
    {
        throw std::runtime_error("Generated model geometry failed validation");
    }
    assetcache::Save("models.bin", kModelVersion, library.get(), sizeof(Library));
    return library;
}
} // namespace

const Library &ModelLibrary::Get()
{
    static const auto library = LoadOrGenerate();
    return *library;
}
} // namespace models
