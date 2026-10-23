#include "stdafx.h"
#include "ModelLibrary.h"
#include "ResourceAssets.h"
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
#include <map>
#include <utility>
#include <iterator>

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
             std::array<float, 4> color, PaletteSlot slot, Gender gender)
{
    const bool female = gender == Gender::Female;
    const float z[] = {hem, .755f, shoulders}, width[] = {hemWidth, female ? .108f : .158f, shoulderWidth};
    for (int ring = 0; ring < 2; ++ring)
    {
        Point v[8];
        for (int level = 0; level < 2; ++level)
        {
            int n = ring + level;
            float back = n == 1 ? (female ? -.102f : -.13f) : -.13f;
            float face = n == 1 ? (female ? .105f : .14f) : front;
            v[level * 4] = {back, -width[n], z[n]};
            v[level * 4 + 1] = {face, -width[n], z[n]};
            v[level * 4 + 2] = {face, width[n], z[n]};
            v[level * 4 + 3] = {back, width[n], z[n]};
        }
        for (int side = 0; side < 4; ++side)
        {
            int next = (side + 1) % 4;
            AddVariantPart(m, PartKind::Polygon, {v[side], v[next], v[next + 4], v[side + 4]}, color, slot);
            m.parts[m.count - 1].clothing = true;
        }
    }
    for (int n : {0, 2})
    {
        AddVariantPart(m, PartKind::Polygon,
                       {{-.13f, -width[n], z[n]},
                        {front, -width[n], z[n]},
                        {front, width[n], z[n]},
                        {-.13f, width[n], z[n]}},
                       color, slot);
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
CharacterPose ModelRig::Pose(float phase, bool walking, bool armed, float recoil, bool riding, Gender gender)
{
    CharacterPose p{};
    p.armed = armed;
    p.walking = walking;
    p.riding = riding;
    p.animationPhase = phase;
    p.recoil = recoil;
    auto &j = p.joints;
    const float shoulder = gender == Gender::Female ? .18f : .225f,
                hip = gender == Gender::Female ? .125f : .105f;
    float step = walking ? std::sin(phase) * .48f : 0;
    j[size_t(Joint::Root)].position.z = walking && !armed ? std::fabs(std::sin(phase)) * .025f : 0;
    j[size_t(Joint::Head)].position = {0, 0, 1.04f};
    j[size_t(Joint::RightArm)].position = {0, -shoulder, .94f};
    j[size_t(Joint::LeftArm)].position = {0, shoulder, .94f};
    j[size_t(Joint::RightForearm)].position = {0, 0, -.29f};
    j[size_t(Joint::LeftForearm)].position = {0, 0, -.29f};
    j[size_t(Joint::RightLeg)].position = {0, -hip, .57f};
    j[size_t(Joint::LeftLeg)].position = {0, hip, .57f};
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
    return ResourceAssets::Muzzle(pose);
}

void ModelFactory::GenerateVehicle(VehicleModel &result, VehicleType type)
{
    auto &m = result.body;
    m = Model{};
    m.space = CoordinateSpace::World;
    bool bike = type == VehicleType::JetBike;
    float length = bike ? .95f : type == VehicleType::Hauler ? 1.5f : 1.24f, w = bike ? .17f : .38f;
    MeshBox(m, {-length, -w, .17f}, {length * .82f, w, .45f}, {.85f, .92f, 1, 1});
    MeshBox(m, {-length * .9f, -w * .9f, .1f}, {length * .9f, w * .9f, .22f}, {.12f, .19f, .24f, 1},
            PaletteSlot::Fixed);
    if (bike)
    {
        MeshBox(m, {-.38f, -.16f, .44f}, {.13f, .16f, .55f}, {.1f, .15f, .19f, 1}, PaletteSlot::Fixed);
        MeshBox(m, {.16f, -.22f, .43f}, {.67f, .22f, .64f}, {1, 1, 1, 1}, PaletteSlot::Accent);
        Tube(m, {.37f, -.31f, .67f}, {.37f, .31f, .67f}, .027f, .027f, {.5f, .65f, .7f, 1},
             PaletteSlot::Fixed, 8);
        result.seat = {-.125f, 0, .55f};
    }
    else
    {
        size_t first = m.count;
        Ellipsoid(m, {-.04f, 0, .51f}, {.69f, w * .92f, type == VehicleType::Shuttle ? .39f : .28f},
                  {.10f, .29f, .37f, 1}, PaletteSlot::Fixed, Joint::Root, 16, 6);
        for (size_t i = first; i < m.count; ++i)
        {
            m.parts[i].surface = Surface::Glass;
        }
        // Armoured dorsal spine and flush sensor, replacing a conventional car roof.
        MeshBox(m, {-.58f, -.07f, .73f}, {.36f, .07f, .81f}, {1, 1, 1, 1}, PaletteSlot::Accent);
        if (type == VehicleType::Hauler)
        {
            MeshBox(m, {-1.45f, -.36f, .45f}, {-.64f, .36f, 1.05f}, {1, 1, 1, 1}, PaletteSlot::Accent);
            for (int i = 0; i < 4; ++i)
            {
                MeshBox(m, {-1.4f + i * .17f, -.37f, .52f}, {-1.37f + i * .17f, .37f, 1.08f},
                        {.19f, .28f, .34f, 1}, PaletteSlot::Fixed);
            }
        }
        if (type == VehicleType::Patrol)
        {
            MeshBox(m, {-.23f, -.24f, .81f}, {.07f, .24f, .87f}, {.15f, .65f, 1, 1}, PaletteSlot::Emission);
            Tube(m, {-.5f, 0, .79f}, {-.5f, 0, 1.02f}, .018f, .008f, {.4f, .65f, .75f, 1}, PaletteSlot::Fixed,
                 8);
        }
    }
    for (int side = 0; side < 2; ++side)
    {
        float y = (side ? 1.f : -1.f) * (bike ? .27f : .46f);
        // Independent magnetic drive pods and exposed luminous coil collars.
        Tube(m, {-length * .82f, y, .17f}, {length * .68f, y, .17f}, .12f, .10f, {.24f, .34f, .41f, 1},
             PaletteSlot::Fixed, 12);
        for (float x : {-length * .52f, length * .30f})
        {
            Tube(m, {x, y, .17f}, {x + .045f, y, .17f}, .125f, .125f, {1, 1, 1, 1}, PaletteSlot::Emission, 8);
        }
        result.exhausts[side] = {-length - .04f, y, .23f};
        result.headlights[side] = {length * .84f, y * .65f, .38f};
        auto nozzle = result.exhausts[side];
        Tube(m, {nozzle.x + .16f, y, .23f}, {nozzle.x - .04f, y, .23f}, .10f, .14f, {.32f, .45f, .52f, 1},
             PaletteSlot::Fixed, 12);
        Tube(m, {nozzle.x - .041f, y, .23f}, {nozzle.x - .046f, y, .23f}, .10f, .10f, {1, 1, 1, 1},
             PaletteSlot::Emission, 12);
        MeshBox(m, {-.38f, y - .055f, .32f}, {.40f, y + .055f, .37f}, {1, 1, 1, 1}, PaletteSlot::Accent);
        MeshBox(m, {-.32f, y - .058f, .371f}, {.29f, y + .058f, .38f}, {1, 1, 1, 1}, PaletteSlot::Emission);
        auto lamp = result.headlights[side];
        MeshBox(m, {lamp.x - .025f, lamp.y - .06f, lamp.z - .025f},
                {lamp.x + .025f, lamp.y + .06f, lamp.z + .025f}, {.75f, .95f, 1, 1}, PaletteSlot::Emission);
    }
    // Bake physical dimensions once, leaving instance scale at 1 in the walking world.
    constexpr Point sizesMeters[] = {{4.3f, 1.7f, 1.35f},
                                     {5.6f, 1.85f, 2.1f},
                                     {5.2f, 1.9f, 2.0f},
                                     {4.6f, 1.75f, 1.55f},
                                     {2.5f, .95f, 1.05f}};
    Point lo = {100, 100, 100}, hi = {-100, -100, -100};
    for (size_t i = 0; i < m.count; ++i)
    {
        for (size_t j = 0; j < m.parts[i].count; ++j)
        {
            auto p = m.parts[i].points[j];
            lo.x = (std::min)(lo.x, p.x);
            lo.y = (std::min)(lo.y, p.y);
            lo.z = (std::min)(lo.z, p.z);
            hi.x = (std::max)(hi.x, p.x);
            hi.y = (std::max)(hi.y, p.y);
            hi.z = (std::max)(hi.z, p.z);
        }
    }
    auto size = sizesMeters[size_t(type)];
    Point scale = {size.x / (2 * (hi.x - lo.x)), size.y / (2 * (hi.y - lo.y)), size.z / (2 * (hi.z - lo.z))};
    auto convert = [&](Point p) { return Point{p.x * scale.x, p.y * scale.y, (p.z - lo.z) * scale.z}; };
    for (size_t i = 0; i < m.count; ++i)
    {
        auto &part = m.parts[i];
        if (part.surface == Surface::Default)
        {
            part.surface = Surface::Metal;
        }
        for (size_t j = 0; j < part.count; ++j)
        {
            part.points[j] = convert(part.points[j]);
            auto n = part.normals[j];
            float x = n.x / scale.x, y = n.y / scale.y, z = n.z / scale.z,
                  len = std::sqrt(x * x + y * y + z * z);
            if (len > 0)
            {
                part.normals[j] = {x / len, y / len, z / len};
            }
        }
    }
    for (auto &p : result.exhausts)
    {
        p = convert(p);
    }
    for (auto &p : result.headlights)
    {
        p = convert(p);
    }
    result.seat = convert(result.seat);
}

void ModelFactory::GenerateCharacter(Model &m, CharacterType type, Gender gender)
{
    m.count = 0;
    m.space = CoordinateSpace::World;
    const bool medic = type == CharacterType::Medic, researcher = type == CharacterType::Researcher,
               engineer = type == CharacterType::Engineer, guard = type == CharacterType::Guard;
    PaletteSlot fabricSlot = medic ? PaletteSlot::Fixed : PaletteSlot::Body;
    std::array<float, 4> fabric =
        medic ? std::array<float, 4>{.93f, .96f, .94f, 1} : std::array<float, 4>{1, 1, 1, 1};
    // Outfit shells replace the torso surface. Each role has a different cut.
    const bool female = gender == Gender::Female;
    Garment(m, .55f, .98f, female ? (researcher ? .20f : .18f) : (researcher ? .19f : .155f),
            female ? (guard ? .18f : .16f) : (guard ? .225f : .205f), female ? .14f : .16f, fabric,
            fabricSlot, gender);
    const size_t torsoParts = m.count;
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
    MeshBox(m, {0, -.039f, -.035f}, {.22f, .039f, .035f}, {.35f, .39f, .43f, 1}, PaletteSlot::Fixed,
            Joint::Weapon);
    // Powered suit seams, sealed comms and joint-mounted equipment retain each role's wardrobe.
    ClothingBox(m, {.14f, -.10f, .69f}, {.19f, .10f, .93f}, {.18f, .27f, .33f, 1}, PaletteSlot::Fixed);
    Badge(m, .191f, 0, .83f, .15f, .022f, {1, 1, 1, 1}, PaletteSlot::Emission);
    if (medic)
    {
        Badge(m, .193f, 0, .81f, .045f, .15f, {.9f, .16f, .18f, 1});
        Badge(m, .194f, 0, .81f, .13f, .045f, {.9f, .16f, .18f, 1});
    }
    for (float side : {-1.f, 1.f})
    {
        ClothingBox(m, {-.07f, side * .15f - .025f, .08f}, {.03f, side * .15f + .025f, .16f},
                    {.17f, .27f, .34f, 1}, PaletteSlot::Fixed, Joint::Head);
        ClothingBox(m, {-.02f, side * .174f - .008f, .105f}, {.02f, side * .174f + .008f, .14f}, {1, 1, 1, 1},
                    PaletteSlot::Emission, Joint::Head);
        ClothingBox(m, {.052f, -.05f, -.19f}, {.075f, .05f, -.075f}, {.22f, .33f, .39f, 1},
                    PaletteSlot::Fixed, side < 0 ? Joint::RightForearm : Joint::LeftForearm);
    }
    // Compact coil pistol: segmented barrel shroud, luminous induction rails and capacitor.
    for (int cell = 0; cell < 3; ++cell)
    {
        MeshBox(m, {.045f + cell * .05f, -.048f, -.028f}, {.070f + cell * .05f, .048f, .043f},
                {.2f, .65f, .85f, 1}, PaletteSlot::Emission, Joint::Weapon);
    }
    MeshBox(m, {.02f, -.031f, .038f}, {.20f, .031f, .055f}, {.12f, .2f, .25f, 1}, PaletteSlot::Fixed,
            Joint::Weapon);
    if (gender == Gender::Female)
    {
        // Distinct fitted silhouette on the same skeleton; armour follows the body rather than overlaying
        // skin.
        for (size_t i = 0; i < m.count; ++i)
        {
            auto &part = m.parts[i];
            if (part.joint == Joint::Weapon)
            {
                continue;
            }
            for (size_t j = 0; j < part.count; ++j)
            {
                auto &p = part.points[j];
                if (part.joint == Joint::Root)
                {
                    if (i < torsoParts)
                    {
                        continue;
                    }
                    float width = p.z < .55f ? 1.04f : (.83f + (.98f - (std::min)(.98f, p.z)) * .40f);
                    p.y *= width;
                    p.x *= .94f;
                }
                else if (part.joint == Joint::Head)
                {
                    p.x *= .94f;
                    p.y *= .91f;
                    auto &n = part.normals[j];
                    n.x /= .94f;
                    n.y /= .91f;
                }
                else
                {
                    p.x *= .94f;
                    p.y *= .92f;
                }
            }
        }
    }
}
void GenerateHair(Model &m, HairStyle style, bool covered)
{
    m.count = 0;
    m.space = CoordinateSpace::World;
    const std::array<float, 4> color = {1, 1, 1, 1};
    auto lock = [&](Point center, Point radius, int segments = 10) {
        Ellipsoid(m, center, radius, color, PaletteSlot::Body, Joint::Head, segments, 4);
    };
    if (covered)
    {
        lock({-.09f, 0, .075f}, {.043f, .10f, .075f});
    }
    else
    {
        lock({-.02f, 0, .208f}, {.126f, .119f, style == HairStyle::Buzz ? .029f : .047f}, 12);
        if (style == HairStyle::SidePart)
        {
            lock({.025f, -.05f, .23f}, {.09f, .075f, .032f});
            lock({-.015f, .06f, .218f}, {.095f, .052f, .03f});
        }
        if (style == HairStyle::Curls)
        {
            for (int i = 0; i < 8; ++i)
            {
                float t = i * 6.2831853f / 8;
                lock({-.015f + .07f * std::cos(t), .075f * std::sin(t), .235f}, {.046f, .043f, .036f}, 8);
            }
        }
    }
    if (style == HairStyle::Bob)
    {
        for (float side : {-1.f, 1.f})
        {
            lock({-.035f, side * .111f, .079f}, {.085f, .034f, .106f});
        }
        lock({-.105f, 0, .063f}, {.045f, .103f, .105f});
    }
    if (style == HairStyle::Ponytail)
    {
        lock({-.14f, 0, covered ? .11f : .17f}, {.045f, .044f, .038f});
        lock({-.195f, 0, covered ? .03f : .085f}, {.057f, .043f, .095f});
        lock({-.22f, 0, covered ? -.055f : -.01f}, {.044f, .034f, .067f});
    }
    if (style == HairStyle::Bun)
    {
        lock({-.143f, 0, covered ? .10f : .19f}, {.073f, .07f, .055f});
    }
    if (covered && style == HairStyle::Curls)
    {
        for (float side : {-1.f, 1.f})
        {
            lock({-.09f, side * .08f, .11f}, {.045f, .035f, .045f}, 8);
        }
    }
    for (size_t i = 0; i < m.count; ++i)
    {
        m.parts[i].surface = Surface::Fabric;
    }
}

void ModelFactory::GenerateBuilding(Model &m, BuildingType type)
{
    m = Model{};
    m.space = CoordinateSpace::World;
    auto shell = [&](Point a, Point b, PaletteSlot slot = PaletteSlot::Body) {
        MeshBox(m, a, b, {1, 1, 1, 1}, slot);
    };
    auto band = [&](float edge, float start, float span, float z, float height) {
        AddVariantPart(m, PartKind::Glass,
                       {{edge + .003f, start, z},
                        {edge + .003f, start + span, z},
                        {edge + .003f, start + span, z + height},
                        {edge + .003f, start, z + height}},
                       {.09f, .32f, .4f, 1}, PaletteSlot::Fixed);
        AddVariantPart(m, PartKind::Glass,
                       {{start, edge + .003f, z},
                        {start, edge + .003f, z + height},
                        {start + span, edge + .003f, z + height},
                        {start + span, edge + .003f, z}},
                       {.09f, .32f, .4f, 1}, PaletteSlot::Fixed);
        MeshBox(m, {edge + .004f, start, z + height}, {edge + .009f, start + span, z + height + .009f},
                {1, 1, 1, 1}, PaletteSlot::Emission);
        MeshBox(m, {start, edge + .004f, z + height}, {start + span, edge + .009f, z + height + .009f},
                {1, 1, 1, 1}, PaletteSlot::Emission);
    };
    // Shared raised plinth and pressure-hull base, with no pitched roofs or chimneys.
    shell({0, 0, 0}, {1, 1, .06f}, PaletteSlot::Accent);
    if (type == BuildingType::Habitat)
    {
        shell({.02f, .02f, .06f}, {.98f, .98f, .78f});
        shell({0, 0, .78f}, {1, 1, .86f}, PaletteSlot::Accent);
        band(.98f, .10f, .77f, .39f, .07f);
        band(.98f, .10f, .77f, .66f, .07f);
        for (float x : {.05f, .89f})
        {
            shell({x, .04f, .08f}, {x + .06f, .95f, .92f}, PaletteSlot::Accent);
        }
        // Slanted solar/thermal array on the flat service deck.
        AddVariantPart(m, PartKind::Polygon,
                       {{.14f, .15f, .88f}, {.82f, .15f, .88f}, {.82f, .65f, 1.f}, {.14f, .65f, 1.f}},
                       {.10f, .24f, .31f, 1}, PaletteSlot::Fixed);
        for (float x : {.2f, .4f, .6f})
        {
            AddVariantPart(m, PartKind::Line, {{x, .15f, .884f}, {x, .65f, 1.004f}}, {.25f, .8f, .9f, 1},
                           PaletteSlot::Emission, .8f);
        }
    }
    else if (type == BuildingType::Terraced)
    {
        // Cantilevered habitat pods connected to an exposed service spine.
        shell({.08f, .08f, .06f}, {.30f, .36f, 1.06f}, PaletteSlot::Accent);
        for (int tier = 0; tier < 3; ++tier)
        {
            float inset = tier * .10f, z = .08f + tier * .29f, edge = 1 - inset;
            shell({inset, inset, z}, {edge, edge, z + .25f});
            shell({inset, inset, z + .25f}, {edge, edge, z + .28f}, PaletteSlot::Accent);
            band(edge, inset + .08f, .75f - inset * 2, z + .11f, .075f);
            for (float x : {inset + .08f, edge - .08f})
            {
                Tube(m, {x, inset + .04f, z}, {x + .035f, edge - .05f, z + .27f}, .012f, .012f,
                     {.5f, .65f, .7f, 1}, PaletteSlot::Fixed, 6);
            }
        }
        Tube(m, {.19f, .22f, 1.04f}, {.19f, .22f, 1.15f}, .03f, .018f, {1, 1, 1, 1}, PaletteSlot::Emission,
             8);
    }
    else if (type == BuildingType::Workshop)
    {
        shell({.04f, .04f, .06f}, {.96f, .96f, .68f});
        shell({0, 0, .68f}, {1, 1, .77f}, PaletteSlot::Accent);
        for (float x : {.05f, .88f})
        {
            shell({x, .02f, .06f}, {x + .07f, .98f, .84f}, PaletteSlot::Accent);
            Tube(m, {x + .035f, .2f, .1f}, {x + .035f, .65f, .85f}, .018f, .018f, {.4f, .65f, .7f, 1},
                 PaletteSlot::Fixed, 8);
        }
        Tube(m, {.25f, .3f, .79f}, {.73f, .3f, .79f}, .13f, .13f, {.28f, .4f, .45f, 1}, PaletteSlot::Fixed,
             16);
        Tube(m, {.3f, .3f, .79f}, {.32f, .3f, .79f}, .138f, .138f, {1, 1, 1, 1}, PaletteSlot::Emission, 12);
        band(.96f, .16f, .66f, .48f, .075f);
        // Recessed segmented hangar shutter instead of a domestic garage facade.
        for (int i = 0; i < 5; ++i)
        {
            MeshBox(m, {.22f, .963f, .07f + i * .065f}, {.78f, .975f, .13f + i * .065f},
                    {.18f, .27f, .33f, 1}, PaletteSlot::Fixed);
        }
    }
    else
    {
        shell({.02f, .02f, .06f}, {.98f, .98f, .19f});
        Dome(m, .19f, type == BuildingType::Observatory ? .65f : .70f);
        // Meridian ribs follow the curved shell rather than floating at its outside.
        for (int rib = 0; rib < 8; ++rib)
        {
            float longitude = rib * 6.2831853f / 8;
            for (int ring = 0; ring < 4; ++ring)
            {
                auto at = [&](int n) {
                    float t = n * 1.5707963f / 4;
                    float h = type == BuildingType::Observatory ? .65f : .70f;
                    return Point{.5f + .464f * std::cos(t) * std::cos(longitude),
                                 .5f + .464f * std::cos(t) * std::sin(longitude), .19f + h * std::sin(t)};
                };
                AddVariantPart(m, PartKind::Line, {at(ring), at(ring + 1)}, {.42f, .67f, .72f, 1},
                               rib % 2 ? PaletteSlot::Accent : PaletteSlot::Emission, .8f);
            }
        }
        if (type == BuildingType::Observatory)
        {
            Tube(m, {.5f, .5f, .82f}, {.5f, .5f, 1.04f}, .06f, .045f, {.65f, .75f, .8f, 1},
                 PaletteSlot::Fixed, 12);
            Tube(m, {.32f, .5f, 1.04f}, {.78f, .5f, 1.17f}, .05f, .11f, {.5f, .68f, .76f, 1},
                 PaletteSlot::Accent, 16);
            Tube(m, {.78f, .5f, 1.17f}, {.8f, .5f, 1.176f}, .095f, .095f, {1, 1, 1, 1}, PaletteSlot::Emission,
                 12);
        }
    }
    constexpr float heightMeters[] = {7.2f, 10.8f, 5.4f, 6.5f, 11.5f};
    float door = 2.15f / heightMeters[size_t(type)];
    // Pressure lock has the same 2.15m clearance across building types.
    shell({.36f, .78f, .01f}, {.64f, 1.f, door + .035f}, PaletteSlot::Accent);
    MeshBox(m, {.40f, 1.001f, .015f}, {.60f, 1.009f, door}, {.08f, .17f, .22f, 1}, PaletteSlot::Fixed);
    MeshBox(m, {.495f, 1.01f, .04f}, {.505f, 1.014f, door - .025f}, {1, 1, 1, 1}, PaletteSlot::Emission);
    for (size_t i = 0; i < m.count; ++i)
    {
        if (m.parts[i].surface == Surface::Default)
        {
            m.parts[i].surface = Surface::Metal;
        }
    }
}

bool ModelFactory::ValidateModel(const Model &model)
{
    if (model.resourceId)
    {
        const auto &mesh = ResourceAssets::Get(model.resourceId);
        return model.space == CoordinateSpace::World && !mesh.vertices.empty() &&
               mesh.vertices.size() % 3 == 0;
    }
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

void Generate(Library &library)
{
    library.cube = {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
    for (size_t i = 0; i < library.circle.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 48;
        library.circle[i] = {std::cos(angle), std::sin(angle), 0};
    }
    for (size_t i = 0; i < library.softCircle.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 64;
        library.softCircle[i] = {std::cos(angle), std::sin(angle), 0};
    }
    for (size_t i = 0; i < library.gateRing.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 96;
        library.gateRing[i] = {std::cos(angle), std::sin(angle), 0};
    }
    constexpr const char *vehicles[] = {"Coupe", "Shuttle", "Hauler", "Patrol", "JetBike"};
    for (size_t i = 0; i < library.vehicles.size(); ++i)
    {
        auto &vehicle = library.vehicles[i];
        ResourceAssets::Bind(vehicle.body, std::string("vehicle/") + vehicles[i]);
        const auto &resource = ResourceAssets::Get(vehicle.body.resourceId);
        std::copy_n(resource.sockets.begin(), 2, vehicle.exhausts.begin());
        std::copy_n(resource.sockets.begin() + 2, 2, vehicle.headlights.begin());
        vehicle.seat = resource.sockets[4];
    }
    constexpr const char *roles[] = {"Explorer", "Engineer",    "Guard", "Medic",   "Researcher",
                                     "Miner",    "HeavyCombat", "Pilot", "Civilian"};
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        ResourceAssets::Bind(library.characters[i], std::string("character/Male/") + roles[i]);
        ResourceAssets::Bind(library.femaleCharacters[i], std::string("character/Female/") + roles[i]);
    }
    constexpr const char *hairs[] = {"Buzz", "SidePart", "Curls", "Bob", "Ponytail", "Bun"};
    for (size_t i = 0; i < library.hairs.size(); ++i)
    {
        ResourceAssets::Bind(library.hairs[i], std::string("hair/") + hairs[i]);
        ResourceAssets::Bind(library.coveredHairs[i], std::string("hair/covered/") + hairs[i]);
    }
    constexpr const char *buildings[] = {"Habitat",       "Terraced",        "Workshop",   "Dome",
                                         "Observatory",   "SpireHelix",      "TwinBridge", "ArcologyTerrace",
                                         "TransitAtrium", "CourtyardHabitat"};
    static_assert(std::size(buildings) == size_t(BuildingType::Count));
    for (size_t i = 0; i < library.buildings.size(); ++i)
    {
        ResourceAssets::Bind(library.buildings[i], std::string("building/") + buildings[i]);
    }
    constexpr const char *creatures[] = {"RelaySentinel", "RelaySentinel_Variant",
                                         "RiftGrazer",    "RiftGrazer_Variant",
                                         "VeilWalker",    "VeilWalker_Variant"};
    for (size_t i = 0; i < library.creatures.size(); ++i)
    {
        ResourceAssets::Bind(library.creatures[i], std::string("creature/") + creatures[i]);
    }
    ResourceAssets::Bind(library.drone, "creature/RelaySentinel");
    const std::pair<Model *, const char *> props[] = {{&library.box, "box"},
                                                      {&library.salvage, "salvage"},
                                                      {&library.recoveryKit, "recoveryKit"},
                                                      {&library.rock, nullptr},
                                                      {&library.telescope, "telescope"},
                                                      {&library.console, "console"},
                                                      {&library.bulkhead, "bulkhead"},
                                                      {&library.trafficGate, "trafficGate"},
                                                      {&library.bed, "bed"},
                                                      {&library.chair, "chair"},
                                                      {&library.table, "table"},
                                                      {&library.lamp, "lamp"},
                                                      {&library.generator, "generator"},
                                                      {&library.breaker, "breaker"},
                                                      {&library.beacon, "beacon"},
                                                      {&library.relief, "relief"},
                                                      {&library.wall, "wall"},
                                                      {&library.door, "door"},
                                                      {&library.lantern, "lantern"},
                                                      {&library.support, "support"}};
    for (auto prop : props)
    {
        ResourceAssets::Bind(*prop.first, prop.second ? std::string("prop/") + prop.second : "nature/rock");
    }
    constexpr const char *grounds[] = {"concrete", "asphalt", "metal", "glass"};
    for (size_t i = 0; i < library.grounds.size(); ++i)
    {
        ResourceAssets::Bind(library.grounds[i], std::string("ground/") + grounds[i]);
    }
    ResourceAssets::Bind(library.mineGround, "ground/mine");
    ResourceAssets::Bind(library.outdoorGrounds[0], "ground/grass");
    ResourceAssets::Bind(library.outdoorGrounds[1], "ground/sand");
    ResourceAssets::Bind(library.tree, "nature/tree");
    ResourceAssets::Bind(library.fungus, "nature/fungus");
    ResourceAssets::Bind(library.crystal, "nature/crystal");
    ResourceAssets::Bind(library.reeds, "nature/reeds");
    constexpr const char *aircraft[] = {"Kite_Courier", "Manta_AirTaxi", "Mule_Skycrane", "Warden_Survey"};
    for (size_t i = 0; i < library.aircraft.size(); ++i)
    {
        ResourceAssets::Bind(library.aircraft[i],
                             std::string("aircraft/") + aircraft[i / 2] + (i % 2 ? "_Variant" : ""));
    }
    for (size_t i = 0; i < library.scenery.size(); ++i)
    {
        auto &model = library.scenery[i];
        auto type = static_cast<SceneryType>(i);
        ResourceAssets::Bind(model, std::string("scenery/") + ModelLibrary::SceneryName(type));
        model.ground = type <= SceneryType::SpeedTable || type == SceneryType::PedestrianHologram ||
                       type == SceneryType::LandingPad;
    }
    ResourceAssets::Bind(library.signalStates[0], "scenery/City_TrafficSignal_Amber");
    ResourceAssets::Bind(library.signalStates[1], "scenery/City_TrafficSignal_Green");
}

bool Valid(const Library &library)
{
    for (const auto &model : library.aircraft)
    {
        if (!ModelFactory::ValidateModel(model))
        {
            return false;
        }
    }
    for (const auto &model : library.scenery)
    {
        if (!ModelFactory::ValidateModel(model))
        {
            return false;
        }
    }
    for (const auto &model : library.signalStates)
    {
        if (!ModelFactory::ValidateModel(model))
        {
            return false;
        }
    }
    for (size_t i = 0; i < library.hairs.size(); ++i)
    {
        if (!ModelFactory::ValidateModel(library.hairs[i]) ||
            !ModelFactory::ValidateModel(library.coveredHairs[i]))
        {
            return false;
        }
    }

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
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        if (!ModelFactory::ValidateModel(library.characters[i]) ||
            !ModelFactory::ValidateModel(library.femaleCharacters[i]))
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
    for (const Model *model :
         {&library.box, &library.drone, &library.salvage, &library.recoveryKit, &library.rock,
          &library.telescope, &library.console, &library.bulkhead, &library.trafficGate})
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
    Generate(*library);
    if (!Valid(*library))
    {
        throw std::runtime_error("Resource model mappings failed validation");
    }
    return library;
}
} // namespace

const Library &ModelLibrary::Get()
{
    static const auto library = LoadOrGenerate();
    return *library;
}
const Model &ModelLibrary::Character(CharacterType type, Gender gender)
{
    const auto &library = Get();
    return gender == Gender::Female ? library.femaleCharacters.at(size_t(type))
                                    : library.characters.at(size_t(type));
}
const Model &ModelLibrary::Hair(HairStyle style, bool covered)
{
    return covered ? Get().coveredHairs.at(size_t(style)) : Get().hairs.at(size_t(style));
}
const Model &ModelLibrary::Aircraft(AircraftType type, bool alternate)
{
    return Get().aircraft.at(size_t(type) * 2 + (alternate ? 1 : 0));
}
const Model &ModelLibrary::Scenery(SceneryType type)
{
    return Get().scenery.at(size_t(type));
}
const Model &ModelLibrary::TrafficSignal(unsigned int state)
{
    return state == 0 ? Scenery(SceneryType::TrafficSignal) : Get().signalStates.at(state - 1);
}
const Model &ModelLibrary::Transit(const std::string &name)
{
    // Stable heap addresses are the mesh-cache identity; no large Model values on the stack.
    static std::map<std::string, std::unique_ptr<Model>> models;
    auto &model = models[name];
    if (!model)
    {
        model = std::make_unique<Model>();
        ResourceAssets::Bind(*model, "transit/" + name);
        // Elevated slabs, rails and piers cast real shadows; flat road paint/paving do not.
        model->ground = name.find("Deck") == std::string::npos && name.find("Ramp_2L") == std::string::npos &&
                        name.find("Pier_") == std::string::npos;
    }
    return *model;
}
const char *ModelLibrary::SceneryName(SceneryType type)
{
    constexpr const char *names[] = {"Road_2Lane_4m",         "Road_4Lane_4m",
                                     "Road_6Lane_4m",         "Road_Crossroad_12m",
                                     "Road_TJunction_12m",    "Road_Curve90_R9m",
                                     "City_Sidewalk_2x4m",    "Road_SpeedTable_4m",
                                     "Road_CrashBarrier_4m",  "City_Streetlight_4m",
                                     "City_TrafficSignal",    "City_PedestrianHologram_6m",
                                     "City_Bollard",          "City_Bench",
                                     "City_Planter",          "City_TransitShelter",
                                     "City_Wayfinding",       "City_ChargePost",
                                     "Utility_Transformer",   "Utility_CargoContainer_6m",
                                     "Utility_HVAC",          "Utility_PipeRack_4m",
                                     "Utility_CommRelay",     "Utility_SolarArray",
                                     "Utility_WaterTank",     "Airfield_LandingPad_16m",
                                     "Airfield_Beacon",       "Frontier_SurveyTripod",
                                     "Frontier_FieldShelter", "Frontier_MiningConveyor",
                                     "Ruin_ObserverMonolith", "Ruin_FracturedArch",
                                     "Nature_PrismBloom",     "Nature_SailTree"};
    static_assert(std::size(names) == size_t(SceneryType::Count));
    return names[size_t(type)];
}
} // namespace models
