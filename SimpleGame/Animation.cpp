#include "stdafx.h"
#include "Animation.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace models
{
namespace
{
using PoseBuffer = std::array<BonePose, kMaxSkinBones>;
std::string CanonicalBoneName(const std::string &raw)
{
    std::string s;
    s.reserve(raw.size());
    for (unsigned char ch : raw)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            s.push_back(char(ch - 'A' + 'a'));
        }
        else
        {
            s.push_back(char(ch));
        }
    }
    if (s.compare(0, 10, "mixamorig:") == 0 || s.compare(0, 10, "mixamorig_") == 0)
    {
        s.erase(0, 10);
    }
    else if (s.compare(0, 9, "mixamorig") == 0 && s.size() > 9)
    {
        s.erase(0, 9);
    }
    return s;
}
float Dot(AnimationRotation a, AnimationRotation b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
AnimationRotation Normalized(AnimationRotation q)
{
    float length = std::sqrt(Dot(q, q));
    if (length < .000001f)
    {
        return {};
    }
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}
AnimationRotation Product(AnimationRotation a, AnimationRotation b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Point Lerp(Point a, Point b, float w)
{
    return {a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w};
}
float Smooth(float x)
{
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3 - 2 * x);
}
void Globals(const AnimationRig &rig, const PoseBuffer &local, std::array<AnimationMatrix, kMaxSkinBones> &global)
{
    for (size_t j = 0; j < rig.parents.size() && j < kMaxSkinBones; ++j)
    {
        auto m = AnimationMath::Matrix(local[j]);
        global[j] = rig.parents[j] < 0 ? m : AnimationMath::Multiply(global[size_t(rig.parents[j])], m);
    }
}
void ClipPose(const ResourceMesh &mesh, int index, float phase, bool loop, PoseBuffer &out)
{
    if (index < 0 || size_t(index) >= mesh.Clips().size())
    {
        return;
    }
    const auto &clip = mesh.Clips()[size_t(index)];
    if (clip.localFrames.empty())
    {
        return;
    }
    phase = loop ? phase - std::floor(phase) : std::clamp(phase, 0.f, 1.f);
    float frame = phase * (clip.frames - 1);
    auto a = (std::min)(clip.frames - 1, unsigned(frame)), b = (std::min)(clip.frames - 1, a + 1);
    for (unsigned j = 0; j < mesh.boneCount; ++j)
    {
        out[j] = AnimationMath::Blend(clip.localFrames[a * mesh.boneCount + j],
                                      clip.localFrames[b * mesh.boneCount + j], frame - a);
    }
}
void Mix(PoseBuffer &a, const PoseBuffer &b, float weight, size_t bones)
{
    for (size_t j = 0; j < bones; ++j)
    {
        a[j] = AnimationMath::Blend(a[j], b[j], weight);
    }
}
void UpperLayer(const ResourceMesh &mesh, PoseBuffer &base, const PoseBuffer &layer, float alpha,
                bool fullMask = false)
{
    if (alpha <= 0)
    {
        return;
    }
    const auto &rig = *mesh.rig;
    const size_t count = (std::min)(rig.parents.size(), size_t(kMaxSkinBones));
    std::array<AnimationRotation, kMaxSkinBones> baseWorldRot{}, layerWorldRot{}, resultWorldRot{};
    for (size_t j = 0; j < count; ++j)
    {
        const int parent = rig.parents[j];
        const auto bq = Normalized(base[j].rotation);
        const auto lq = Normalized(layer[j].rotation);
        baseWorldRot[j] = parent < 0 ? bq : Product(baseWorldRot[size_t(parent)], bq);
        layerWorldRot[j] = parent < 0 ? lq : Product(layerWorldRot[size_t(parent)], lq);
        const float mask = fullMask ? rig.upperBodyFullMask[j] : rig.upperBodyMask[j];
        const float w = std::clamp(alpha * mask, 0.f, 1.f);
        if (w <= 0)
        {
            resultWorldRot[j] = baseWorldRot[j];
            continue;
        }
        BonePose local = w >= 1.f ? layer[j] : AnimationMath::Blend(base[j], layer[j], w);
        // Component-space rotation keeps the barrel/arms stable over the moving pelvis.
        const auto desired = w >= 1.f ? layerWorldRot[j] : AnimationMath::Slerp(baseWorldRot[j], layerWorldRot[j], w);
        const auto parentRot = parent < 0 ? AnimationRotation{0, 0, 0, 1} : resultWorldRot[size_t(parent)];
        local.rotation = Product({-parentRot.x, -parentRot.y, -parentRot.z, parentRot.w}, desired);
        base[j] = local;
        resultWorldRot[j] = desired;
    }
}
} // namespace

// ============================================================================
// AnimationMath
// ============================================================================
AnimationMatrix AnimationMath::Multiply(const AnimationMatrix &a, const AnimationMatrix &b)
{
    // Unrolled column-major 4x4 multiply for affine animation transforms.
    AnimationMatrix m;
    for (size_t c = 0; c < 4; ++c)
    {
        const float b0 = b[c * 4 + 0], b1 = b[c * 4 + 1], b2 = b[c * 4 + 2], b3 = b[c * 4 + 3];
        m[c * 4 + 0] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
        m[c * 4 + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
        m[c * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
        m[c * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
    }
    return m;
}
AnimationMatrix AnimationMath::Inverse(const AnimationMatrix &m)
{
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < .0000001f)
    {
        throw std::runtime_error("Singular animation transform");
    }
    AnimationMatrix out = {(e * i - f * h) / det,
                           (f * g - d * i) / det,
                           (d * h - e * g) / det,
                           0,
                           (c * h - b * i) / det,
                           (a * i - c * g) / det,
                           (b * g - a * h) / det,
                           0,
                           (b * f - c * e) / det,
                           (c * d - a * f) / det,
                           (a * e - b * d) / det,
                           0,
                           0,
                           0,
                           0,
                           1};
    for (size_t r = 0; r < 3; ++r)
    {
        out[12 + r] = -(out[r] * m[12] + out[4 + r] * m[13] + out[8 + r] * m[14]);
    }
    return out;
}
AnimationMatrix AnimationMath::Matrix(const BonePose &p)
{
    auto q = Normalized(p.rotation);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return {(1 - 2 * (y * y + z * z)) * p.scale.x,
            2 * (x * y + z * w) * p.scale.x,
            2 * (x * z - y * w) * p.scale.x,
            0,
            2 * (x * y - z * w) * p.scale.y,
            (1 - 2 * (x * x + z * z)) * p.scale.y,
            2 * (y * z + x * w) * p.scale.y,
            0,
            2 * (x * z + y * w) * p.scale.z,
            2 * (y * z - x * w) * p.scale.z,
            (1 - 2 * (x * x + y * y)) * p.scale.z,
            0,
            p.translation.x,
            p.translation.y,
            p.translation.z,
            1};
}
BonePose AnimationMath::Pose(const AnimationMatrix &matrix)
{
    auto m = matrix;
    BonePose p;
    p.translation = {m[12], m[13], m[14]};
    p.scale = {std::hypot(m[0], m[1], m[2]), std::hypot(m[4], m[5], m[6]), std::hypot(m[8], m[9], m[10])};
    for (size_t c = 0; c < 3; ++c)
    {
        float s = c == 0 ? p.scale.x : c == 1 ? p.scale.y : p.scale.z;
        if (s < .000001f)
        {
            throw std::runtime_error("Zero animation bone scale");
        }
        for (size_t r = 0; r < 3; ++r)
        {
            m[c * 4 + r] /= s;
        }
    }
    AnimationRotation q;
    float trace = m[0] + m[5] + m[10];
    if (trace > 0)
    {
        float s = std::sqrt(trace + 1) * 2;
        q = {(m[6] - m[9]) / s, (m[8] - m[2]) / s, (m[1] - m[4]) / s, .25f * s};
    }
    else if (m[0] > m[5] && m[0] > m[10])
    {
        float s = std::sqrt(1 + m[0] - m[5] - m[10]) * 2;
        q = {.25f * s, (m[4] + m[1]) / s, (m[8] + m[2]) / s, (m[6] - m[9]) / s};
    }
    else if (m[5] > m[10])
    {
        float s = std::sqrt(1 + m[5] - m[0] - m[10]) * 2;
        q = {(m[4] + m[1]) / s, .25f * s, (m[9] + m[6]) / s, (m[8] - m[2]) / s};
    }
    else
    {
        float s = std::sqrt(1 + m[10] - m[0] - m[5]) * 2;
        q = {(m[8] + m[2]) / s, (m[9] + m[6]) / s, .25f * s, (m[1] - m[4]) / s};
    }
    p.rotation = Normalized(q);
    return p;
}
AnimationRotation AnimationMath::Slerp(AnimationRotation a, AnimationRotation b, float weight)
{
    a = Normalized(a);
    b = Normalized(b);
    float cosine = Dot(a, b);
    if (cosine < 0)
    {
        b = {-b.x, -b.y, -b.z, -b.w};
        cosine = -cosine;
    }
    float x = 1 - weight, y = weight;
    if (cosine < .9995f)
    {
        float theta = std::acos(std::clamp(cosine, -1.f, 1.f)), denominator = std::sin(theta);
        x = std::sin((1 - weight) * theta) / denominator;
        y = std::sin(weight * theta) / denominator;
    }
    return Normalized({a.x * x + b.x * y, a.y * x + b.y * y, a.z * x + b.z * y, a.w * x + b.w * y});
}
BonePose AnimationMath::Blend(const BonePose &a, const BonePose &b, float weight)
{
    if (weight <= 0)
    {
        return a;
    }
    if (weight >= 1)
    {
        return b;
    }
    return {Lerp(a.translation, b.translation, weight), Lerp(a.scale, b.scale, weight),
            Slerp(a.rotation, b.rotation, weight)};
}

// ============================================================================
// AnimationBlendSpace
// ============================================================================
AnimationWeights AnimationBlendSpace::Evaluate(const AnimationRig &rig, float left, float forward)
{
    AnimationWeights best;
    float bestDistance = 1.e30f;
    for (const auto &triangle : rig.triangles)
    {
        const auto &a = rig.samples[triangle[0]], &b = rig.samples[triangle[1]],
                   &c = rig.samples[triangle[2]];
        float den = (b.forward - c.forward) * (a.left - c.left) + (c.left - b.left) * (a.forward - c.forward);
        if (std::fabs(den) < .000001f)
        {
            continue;
        }
        float x =
            ((b.forward - c.forward) * (left - c.left) + (c.left - b.left) * (forward - c.forward)) / den;
        float y =
            ((c.forward - a.forward) * (left - c.left) + (a.left - c.left) * (forward - c.forward)) / den;
        float z = 1 - x - y;
        if (x >= -.00001f && y >= -.00001f && z >= -.00001f)
        {
            x = (std::max)(0.f, x);
            y = (std::max)(0.f, y);
            z = (std::max)(0.f, z);
            float sum = x + y + z;
            return {triangle, {x / sum, y / sum, z / sum}};
        }
        for (size_t edge = 0; edge < 3; ++edge)
        {
            unsigned u = triangle[edge], v = triangle[(edge + 1) % 3];
            const auto &p = rig.samples[u], &q = rig.samples[v];
            float dx = q.left - p.left, dy = q.forward - p.forward, length = dx * dx + dy * dy;
            float t = length > .000001f
                          ? std::clamp(((left - p.left) * dx + (forward - p.forward) * dy) / length, 0.f, 1.f)
                          : 0;
            float ex = left - p.left - dx * t, ey = forward - p.forward - dy * t,
                  distance = ex * ex + ey * ey;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = {{u, v, u}, {1 - t, t, 0}};
            }
        }
    }
    return best;
}

// ============================================================================
// LayeredAnimation
// ============================================================================
void LayeredAnimation::ClassifyRig(AnimationRig &rig)
{
    const size_t count = (std::min)(rig.names.size(), size_t(kMaxSkinBones));
    rig.upperBodyMask.fill(0.f);
    rig.upperBodyFullMask.fill(0.f);
    bool hasMixamoPrefix = false;
    bool hasHips = false, hasLeftUpLeg = false, hasRightUpLeg = false;
    int spine0 = -1, spine1 = -1, spine2 = -1;
    std::vector<std::string> canon(count);
    for (size_t j = 0; j < count; ++j)
    {
        const auto &raw = rig.names[j];
        if (raw.size() >= 9 && (raw.compare(0, 9, "mixamorig") == 0 || raw.compare(0, 9, "Mixamorig") == 0))
        {
            hasMixamoPrefix = true;
        }
        canon[j] = CanonicalBoneName(raw);
        const auto &c = canon[j];
        if (c == "hips")
        {
            hasHips = true;
            rig.pelvis = int(j);
        }
        else if (c == "pelvis" && rig.pelvis < 0)
        {
            rig.pelvis = int(j);
        }
        if (c == "leftupleg" || c == "thigh.l")
        {
            hasLeftUpLeg = true;
        }
        if (c == "rightupleg" || c == "thigh.r")
        {
            hasRightUpLeg = true;
        }
        if (c == "spine" && spine0 < 0)
        {
            spine0 = int(j);
        }
        else if (c == "spine1" && spine1 < 0)
        {
            spine1 = int(j);
        }
        else if ((c == "spine2" || c == "chest") && spine2 < 0)
        {
            spine2 = int(j);
        }
        if (c == "head" && rig.head < 0)
        {
            rig.head = int(j);
        }
        if ((c == "hand.r" || c == "righthand") && rig.handR < 0)
        {
            rig.handR = int(j);
        }
        if ((c == "hand.l" || c == "lefthand") && rig.handL < 0)
        {
            rig.handL = int(j);
        }
        if (c == "muzzle" && rig.muzzle < 0)
        {
            rig.muzzle = int(j);
        }
    }
    rig.spine = spine0 >= 0 ? spine0 : (count > 2 ? 2 : -1);
    rig.chest = spine2 >= 0 ? spine2 : (spine1 >= 0 ? spine1 : rig.spine);
    rig.back = spine1 >= 0 ? spine1 : (spine2 >= 0 ? spine2 : rig.spine);
    if (hasMixamoPrefix || (hasHips && hasLeftUpLeg && hasRightUpLeg))
    {
        rig.preset = RigPreset::Mixamo;
    }
    else if (count == 23)
    {
        rig.preset = RigPreset::LegacyHuman7H;
    }
    else
    {
        rig.preset = RigPreset::Custom;
    }
    if (rig.preset == RigPreset::LegacyHuman7H)
    {
        for (size_t j = 0; j < count; ++j)
        {
            if (j < 2 || j >= 17)
            {
                rig.upperBodyMask[j] = 0.f;
                rig.upperBodyFullMask[j] = 0.f;
            }
            else
            {
                rig.upperBodyMask[j] = j == 2 ? .35f : j == 3 ? .75f : 1.f;
                rig.upperBodyFullMask[j] = 1.f;
            }
        }
        return;
    }
    auto hasAncestor = [&](size_t bone, int target) {
        if (target < 0)
        {
            return false;
        }
        int cur = int(bone);
        for (size_t depth = 0; cur >= 0 && cur < int(count) && depth <= count; ++depth)
        {
            if (cur == target)
            {
                return true;
            }
            cur = cur < int(rig.parents.size()) ? rig.parents[size_t(cur)] : -1;
        }
        return false;
    };
    for (size_t j = 0; j < count; ++j)
    {
        if (!hasAncestor(j, spine0))
        {
            rig.upperBodyMask[j] = 0.f;
            rig.upperBodyFullMask[j] = 0.f;
        }
        else if (int(j) == spine0)
        {
            rig.upperBodyMask[j] = .35f;
            rig.upperBodyFullMask[j] = 1.f;
        }
        else if (int(j) == spine1)
        {
            rig.upperBodyMask[j] = .75f;
            rig.upperBodyFullMask[j] = 1.f;
        }
        else
        {
            rig.upperBodyMask[j] = 1.f;
            rig.upperBodyFullMask[j] = 1.f;
        }
    }
}
void LayeredAnimation::BindDodgeClip(AnimationRig &rig, const std::string &name, int index)
{
    if (name.compare(0, 6, "Dodge_") != 0)
    {
        return;
    }
    constexpr const char *directions[] = {"Forward", "ForwardLeft", "Left",  "BackLeft",
                                          "Back",    "BackRight",   "Right", "ForwardRight"};
    for (size_t direction = 0; direction < std::size(directions); ++direction)
    {
        const auto prefix = std::string("Dodge_") + directions[direction];
        for (size_t foot = 0; foot < 2; ++foot)
        {
            if (name == prefix + (foot == 0 ? "_L" : "_R"))
            {
                rig.dodgeClips[direction * 2 + foot] = index;
                return;
            }
        }
    }
}
void LayeredAnimation::BindSupportClip(AnimationRig &rig, const std::string &name, int index)
{
    constexpr const char *kNames[8] = {"Support_Treat_Enter",   "Support_Treat_Loop",   "Support_Treat_Exit",
                                       "Support_Inspect_Enter", "Support_Inspect_Loop", "Support_Inspect_Exit",
                                       "Support_Carry_Idle",    "Support_Carry_Walk"};
    for (size_t i = 0; i < std::size(kNames); ++i)
    {
        if (name == kNames[i])
        {
            rig.supportClips[i] = index;
            return;
        }
    }
}
void LayeredAnimation::BindTransitionClip(AnimationRig &rig, const std::string &name, int index)
{
    constexpr const char *kNames[12] = {"Run_Turn180_Left",         "Run_Turn180_Right",
                                        "Turn90_Left",              "Turn90_Right",
                                        "Aim_Strafe_Start_Forward", "Aim_Strafe_Start_Back",
                                        "Aim_Strafe_Start_Left",    "Aim_Strafe_Start_Right",
                                        "Aim_Strafe_Stop_Forward",  "Aim_Strafe_Stop_Back",
                                        "Aim_Strafe_Stop_Left",     "Aim_Strafe_Stop_Right"};
    for (size_t i = 0; i < std::size(kNames); ++i)
    {
        if (name == kNames[i])
        {
            rig.transitionClips[i] = index;
            return;
        }
    }
}
void LayeredAnimation::PrepareGraph(std::vector<ResourceClip> &clips, const AnimationRig &rig)
{
    std::vector<bool> required(clips.size(), false);
    auto require = [&](int index) {
        if (index >= 0 && size_t(index) < required.size())
        {
            required[size_t(index)] = true;
        }
    };
    for (int index : {rig.idle, rig.walk, rig.run, rig.aim, rig.fire, rig.melee, rig.carryIdle, rig.carryWalk,
                      rig.carryRun, rig.reload})
    {
        require(index);
    }
    for (const auto &sample : rig.samples)
    {
        require(sample.clipIndex);
    }
    for (int index : rig.dodgeClips)
    {
        require(index);
    }
    for (int index : rig.supportClips)
    {
        require(index);
    }
    for (int index : rig.transitionClips)
    {
        require(index);
    }
    for (size_t index = 0; index < clips.size(); ++index)
    {
        if (required[index])
        {
            Prepare(clips[index], rig);
        }
    }
}

void LayeredAnimation::Prepare(ResourceClip &clip, const AnimationRig &rig)
{
    size_t bones = (std::min)(rig.parents.size(), size_t(kMaxSkinBones));
    clip.localFrames.resize(size_t(clip.frames) * bones);
    for (unsigned frame = 0; frame < clip.frames; ++frame)
    {
        std::array<AnimationMatrix, kMaxSkinBones> world{};
        for (size_t j = 0; j < bones; ++j)
        {
            AnimationMatrix skin;
            std::copy_n(clip.matrices.data() + (frame * bones + j) * 16, 16, skin.begin());
            world[j] = AnimationMath::Multiply(skin, rig.bind[j]);
            auto local = rig.parents[j] < 0
                             ? world[j]
                             : AnimationMath::Multiply(AnimationMath::Inverse(world[size_t(rig.parents[j])]),
                                                       world[j]);
            clip.localFrames[frame * bones + j] = AnimationMath::Pose(local);
        }
    }
}
void LayeredAnimation::Sample(const ResourceMesh &mesh, const CharacterPose &pose, SkinMatrices &skin)
{
    const auto &rig = *mesh.rig;
    float speed = pose.blended   ? std::hypot(pose.moveLeft, pose.moveForward)
                  : pose.walking ? pose.locomotionSpeed * WorldScale::metersPerUnit
                                 : 0;
    float phase = pose.blended ? pose.locomotionPhase : pose.animationPhase / 10 * 1.5f;
    float aim = pose.blended ? pose.aimWeight : pose.armed ? 1.f : 0;
    PoseBuffer base{}, other{}, aimPose{};
    if (speed <= 1.08f)
    {
        const float w = std::clamp(speed / 1.08f, 0.f, 1.f);
        if (w <= 0.f)
        {
            ClipPose(mesh, rig.idle, pose.animationPhase / 10 / 2, true, base);
        }
        else if (w >= 1.f)
        {
            ClipPose(mesh, rig.walk, phase, true, base);
        }
        else
        {
            ClipPose(mesh, rig.idle, pose.animationPhase / 10 / 2, true, base);
            ClipPose(mesh, rig.walk, phase, true, other);
            Mix(base, other, w, mesh.boneCount);
        }
    }
    else
    {
        const float w = std::clamp((speed - 1.08f) / (2.46f - 1.08f), 0.f, 1.f);
        if (w >= 1.f)
        {
            ClipPose(mesh, rig.run, phase, true, base);
        }
        else
        {
            ClipPose(mesh, rig.walk, phase, true, base);
            ClipPose(mesh, rig.run, phase, true, other);
            Mix(base, other, w, mesh.boneCount);
        }
    }
    if (aim > 0 && !rig.samples.empty())
    {
        auto weights = AnimationBlendSpace::Evaluate(rig, pose.blended ? pose.moveLeft : 0,
                                                     pose.blended ? pose.moveForward : speed);
        float total = 0;
        for (size_t i = 0; i < 3; ++i)
        {
            float w = weights.weights[i];
            if (w <= 0)
            {
                continue;
            }
            ClipPose(mesh, rig.samples[weights.samples[i]].clipIndex, phase, true, other);
            if (total == 0)
            {
                aimPose = other;
            }
            else
            {
                Mix(aimPose, other, w / (total + w), mesh.boneCount);
            }
            total += w;
        }
        Mix(base, aimPose, aim, mesh.boneCount);
    }
    const int dodge = pose.dodgeSample >= 0 && size_t(pose.dodgeSample) < rig.dodgeClips.size()
                          ? rig.dodgeClips[size_t(pose.dodgeSample)]
                          : -1;
    const bool dodgeLayer = dodge >= 0 && pose.dodgeWeight > 0;
    if (pose.dodgeEntryWeight > 0 && pose.dodgeEntrySample >= 0 &&
        size_t(pose.dodgeEntrySample) < rig.dodgeClips.size())
    {
        const int entry = rig.dodgeClips[size_t(pose.dodgeEntrySample)];
        if (entry >= 0)
        {
            ClipPose(mesh, entry, pose.dodgeEntryTime / mesh.Clips()[size_t(entry)].duration, false, other);
            Mix(base, other, pose.dodgeEntryWeight, mesh.boneCount);
        }
    }
    if (dodgeLayer)
    {
        ClipPose(mesh, dodge, pose.dodgeTime / mesh.Clips()[size_t(dodge)].duration, false, other);
        Mix(base, other, pose.dodgeWeight, mesh.boneCount);
    }
    const int transition =
        !dodgeLayer && pose.transitionClip >= 0 && size_t(pose.transitionClip) < rig.transitionClips.size()
            ? rig.transitionClips[size_t(pose.transitionClip)]
            : -1;
    const float transWeight = transition >= 0 ? std::clamp(pose.transitionWeight, 0.f, 1.f) : 0.f;
    if (transWeight > 0.f)
    {
        ClipPose(mesh, transition, std::clamp(pose.transitionPhase, 0.f, 1.f), false, other);
        Mix(base, other, transWeight, mesh.boneCount);
    }
    const int support =
        !dodgeLayer && pose.supportClip >= 0 && size_t(pose.supportClip) < rig.supportClips.size()
            ? rig.supportClips[size_t(pose.supportClip)]
            : -1;
    const float supportWeight = support >= 0 ? std::clamp(pose.supportWeight, 0.f, 1.f) : 0.f;
    const bool supportFullBody = supportWeight > 0.f && pose.supportClip <= 2;
    const bool supportUpperBody = supportWeight > 0.f && pose.supportClip >= 3;
    const bool suppressWeaponHands = supportWeight > 0.5f;
    if (supportFullBody)
    {
        const bool loop = pose.supportClip == 1;
        ClipPose(mesh, support, pose.supportPhase, loop, other);
        Mix(base, other, supportWeight, mesh.boneCount);
    }
    if (!suppressWeaponHands && pose.armed && rig.carryIdle >= 0 && rig.carryWalk >= 0 && rig.carryRun >= 0)
    {
        PoseBuffer carry{}, moving{};
        if (aim < 1.f)
        {
            if (speed <= 1.08f)
            {
                const float w = std::clamp(speed / 1.08f, 0.f, 1.f);
                if (w <= 0.f)
                {
                    ClipPose(mesh, rig.carryIdle, pose.animationPhase / 10 / 2, true, carry);
                }
                else if (w >= 1.f)
                {
                    ClipPose(mesh, rig.carryWalk, phase, true, carry);
                }
                else
                {
                    ClipPose(mesh, rig.carryIdle, pose.animationPhase / 10 / 2, true, carry);
                    ClipPose(mesh, rig.carryWalk, phase, true, moving);
                    Mix(carry, moving, w, mesh.boneCount);
                }
            }
            else
            {
                const float w = std::clamp((speed - 1.08f) / (2.46f - 1.08f), 0.f, 1.f);
                if (w >= 1.f)
                {
                    ClipPose(mesh, rig.carryRun, phase, true, carry);
                }
                else
                {
                    ClipPose(mesh, rig.carryWalk, phase, true, carry);
                    ClipPose(mesh, rig.carryRun, phase, true, moving);
                    Mix(carry, moving, w, mesh.boneCount);
                }
            }
            if (aim > 0.f)
            {
                ClipPose(mesh, rig.aim, 0, false, other);
                Mix(carry, other, aim, mesh.boneCount);
            }
        }
        else
        {
            ClipPose(mesh, rig.aim, 0, false, carry);
        }
        UpperLayer(mesh, base, carry, 1, true);
    }
    else if (!suppressWeaponHands && aim > 0)
    {
        ClipPose(mesh, rig.aim, 0, false, other);
        UpperLayer(mesh, base, other, aim);
    }
    if (supportUpperBody)
    {
        const bool loop = pose.supportClip == 4 || pose.supportClip >= 6;
        ClipPose(mesh, support, pose.supportPhase, loop, other);
        UpperLayer(mesh, base, other, supportWeight, true);
    }
    float fire = pose.blended ? pose.fireSeconds : pose.recoil * .4f;
    if (fire > 0)
    {
        ClipPose(mesh, rig.fire, 1 - std::clamp(fire / .4f, 0.f, 1.f), false, other);
        UpperLayer(mesh, base, other, Smooth(fire / .1f));
    }
    if (pose.armed && rig.reload >= 0 && pose.reloadProgress >= 0)
    {
        float progress = std::clamp(pose.reloadProgress, 0.f, 1.f);
        ClipPose(mesh, rig.reload, progress, false, other);
        float alpha = Smooth(progress * 1.4f / .15f) * Smooth((1 - progress) * 1.4f / .15f);
        UpperLayer(mesh, base, other, alpha, true);
    }
    if (pose.meleeSeconds > 0 && !dodgeLayer)
    {
        float t = 1 - std::clamp(pose.meleeSeconds / .65f, 0.f, 1.f);
        ClipPose(mesh, rig.melee, t, false, other);
        UpperLayer(mesh, base, other, Smooth(t / .08f) * Smooth((1 - t) / .18f));
    }
    // Small spine counterbalance while start/stop/pivot clips are being authored.
    // Keep root/legs and precise aimed upper-body poses intact.
    const float leanWeight = pose.blended ? (1 - aim) * (1 - (dodgeLayer ? pose.dodgeWeight : 0)) *
                                                (1 - transWeight) * (1 - supportWeight)
                                          : 0;
    if (leanWeight > 0.f && rig.spine >= 0 && size_t(rig.spine) < mesh.boneCount &&
        (pose.motionForwardLean != 0.f || pose.motionSideLean != 0.f))
    {
        AnimationRotation pitch = {0, std::sin(pose.motionForwardLean * leanWeight * .5f), 0,
                                   std::cos(pose.motionForwardLean * leanWeight * .5f)};
        AnimationRotation roll = {std::sin(pose.motionSideLean * leanWeight * .5f), 0, 0,
                                  std::cos(pose.motionSideLean * leanWeight * .5f)};
        base[size_t(rig.spine)].rotation = Product(base[size_t(rig.spine)].rotation, Product(pitch, roll));
    }
    std::array<AnimationMatrix, kMaxSkinBones> world{};
    Globals(rig, base, world);
    const size_t boneLimit = (std::min)(size_t(mesh.boneCount), size_t(kMaxSkinBones));
    for (size_t j = 0; j < boneLimit; ++j)
    {
        auto m = AnimationMath::Multiply(world[j], rig.inverseBind[j]);
        std::copy(m.begin(), m.end(), skin.begin() + j * 16);
    }
    // Correct only the visual pelvis hierarchy. Root and collision coordinates remain untouched.
    if (pose.blended && aim > 0 && !rig.soles.empty())
    {
        float minimum = 1.e9f;
        for (const auto &sole : rig.soles)
        {
            if (sole.bone >= boneLimit)
            {
                continue;
            }
            const float *m = skin.data() + sole.bone * 16;
            minimum = (std::min)(minimum, m[2] * sole.position.x + m[6] * sole.position.y +
                                              m[10] * sole.position.z + m[14]);
        }
        float lift = std::clamp(.0007f - minimum, 0.f, .03f);
        const size_t liftStart = rig.preset == RigPreset::Mixamo ? 0u : 1u;
        for (size_t j = liftStart; j < boneLimit; ++j)
        {
            skin[j * 16 + 14] += lift;
        }
    }
}

// ============================================================================
// CharacterAnimation
// ============================================================================
namespace
{
constexpr float kDodgeClipSeconds = .4f, kDodgeBlendIn = .05f, kDodgeBlendOut = .1f;
}
void CharacterAnimation::StartDodge(Point direction, float facingYaw)
{
    const float cy = std::cos(facingYaw), sy = std::sin(facingYaw);
    const float angle = std::atan2(-sy * direction.x + cy * direction.y, cy * direction.x + sy * direction.y);
    constexpr float kOctant = .7853981634f;
    const int octant = (int(std::round(angle / kOctant)) + 8) % 8;
    const int foot = phase >= .25f && phase < .75f ? 1 : 0;
    // Stamina may allow another dash before the previous clip finishes its recovery.
    dodgeEntrySample = dodgeSample;
    dodgeEntryTime = dodgeTime;
    dodgeEntryWeight = DodgeWeight();
    dodgeSample = octant * 2 + foot;
    dodgeTime = 0;
    dodgeFade = -1;
    dodgeEntryLeft = left;
    dodgeEntryForward = forward;
    dodgeEntryPhase = phase;
}
float CharacterAnimation::DodgeWeight() const
{
    if (dodgeSample < 0)
    {
        return 0;
    }
    if (dodgeFade >= 0)
    {
        return dodgeInterruptWeight * Smooth(dodgeFade / kDodgeBlendOut);
    }
    return Smooth((std::min)(dodgeTime / kDodgeBlendIn, (kDodgeClipSeconds - dodgeTime) / kDodgeBlendOut));
}
void CharacterAnimation::InterruptDodge()
{
    if (dodgeSample >= 0 && dodgeFade < 0)
    {
        dodgeInterruptWeight = DodgeWeight();
        dodgeFade = kDodgeBlendOut;
    }
}
void CharacterAnimation::TriggerTurn(bool run180, bool rightTurn)
{
    if (dodgeSample >= 0)
    {
        return;
    }
    transitionClip = (run180 ? 0 : 2) + (rightTurn ? 1 : 0);
    transitionTime = 0;
    transitionDuration = run180 ? .5333f : .50f;
}
float CharacterAnimation::TransitionWeight() const
{
    if (transitionClip < 0 || transitionDuration <= 0)
    {
        return 0;
    }
    return Smooth((std::min)(transitionTime / .08f, (transitionDuration - transitionTime) / .10f));
}
void CharacterAnimation::Update(float dt, Point worldVelocity, float facingYaw, bool aiming, bool riding)
{
    dt = std::clamp(dt, 0.f, .05f);
    float phaseDelta = dt;
    if (riding)
    {
        velocity = {};
        left = forward = 0;
        aimWeight = 0;
        fireSeconds = 0;
        dodgeSample = -1;
        transitionClip = -1;
        wasAimMoving = false;
        return;
    }
    if (dodgeSample >= 0)
    {
        transitionClip = -1;
        if (dodgeFade >= 0)
        {
            dodgeFade = (std::max)(0.f, dodgeFade - dt);
            if (dodgeFade == 0)
            {
                dodgeSample = -1;
            }
        }
        else
        {
            const float previousTime = dodgeTime;
            dodgeTime += dt;
            if (previousTime < kDodgeClipSeconds - kDodgeBlendOut &&
                dodgeTime >= kDodgeClipSeconds - kDodgeBlendOut)
            {
                phase = dodgeSample % 2 == 0 ? .5f : 0.f;
            }
            if (dodgeTime >= kDodgeClipSeconds)
            {
                phaseDelta = dodgeTime - kDodgeClipSeconds;
                dodgeSample = -1;
            }
            else
            {
                phaseDelta = 0;
            }
        }
    }
    float weight = 1 - std::exp(-16 * dt);
    velocity = Lerp(velocity, worldVelocity, weight);
    float cy = std::cos(facingYaw), sy = std::sin(facingYaw);
    left = (-sy * velocity.x + cy * velocity.y) * WorldScale::metersPerUnit;
    forward = (cy * velocity.x + sy * velocity.y) * WorldScale::metersPerUnit;
    float speed = std::hypot(left, forward);
    // Directional transitions: advance active clip or trigger Aim_Strafe_Start/Stop on aim movement edges.
    if (transitionClip >= 0)
    {
        transitionTime += dt;
        if (transitionTime >= transitionDuration)
        {
            if (transitionClip <= 3)
            {
                phase = (transitionClip % 2 == 0) ? .5f : 0.f;
            }
            transitionClip = -1;
        }
    }
    if (aiming && dodgeSample < 0)
    {
        const bool movingNow = speed >= .55f;
        const bool stoppedNow = speed < .25f;
        if (movingNow)
        {
            lastAimDir = std::fabs(forward) >= std::fabs(left) ? (forward >= 0 ? 0 : 1) : (left >= 0 ? 2 : 3);
        }
        if (!wasAimMoving && movingNow)
        {
            transitionClip = 4 + lastAimDir;
            transitionTime = 0;
            transitionDuration = .40f;
            wasAimMoving = true;
        }
        else if (wasAimMoving && stoppedNow)
        {
            transitionClip = 8 + lastAimDir;
            transitionTime = 0;
            transitionDuration = .4667f;
            wasAimMoving = false;
        }
    }
    else
    {
        wasAimMoving = false;
        if (transitionClip >= 4)
        {
            transitionClip = -1;
        }
    }
    if (phaseDelta > 0 && std::hypot(worldVelocity.x, worldVelocity.y) > .001f)
    {
        float runWeight = std::clamp((speed - 1.08f) / (2.46f - 1.08f), 0.f, 1.f);
        float reference = 1.08f + (2.46f - 1.08f) * runWeight;
        float frequency = (1 + .5f * runWeight) * std::clamp(speed / reference, .1f, 2.f);
        phase = std::fmod(phase + phaseDelta * frequency, 1.f);
    }
    if (speed < .001f)
    {
        left = forward = 0;
    }
    aimWeight += (float(aiming) - aimWeight) * (1 - std::exp(-14 * dt));
    if (std::fabs(float(aiming) - aimWeight) < .0001f)
    {
        aimWeight = float(aiming);
    }
    fireSeconds = (std::max)(0.f, fireSeconds - dt);
}
void CharacterAnimation::Apply(CharacterPose &pose) const
{
    pose.blended = true;
    pose.locomotionPhase = phase;
    pose.moveLeft = left;
    pose.moveForward = forward;
    pose.aimWeight = aimWeight;
    pose.fireSeconds = (std::max)(fireSeconds, pose.recoil * .4f);
    pose.dodgeSample = dodgeSample;
    pose.dodgeTime = dodgeTime;
    pose.dodgeWeight = DodgeWeight();
    pose.transitionClip = transitionClip;
    pose.transitionPhase = transitionDuration > 0 ? std::clamp(transitionTime / transitionDuration, 0.f, 1.f) : 0;
    pose.transitionWeight = TransitionWeight();
    if (dodgeSample >= 0 && dodgeTime < kDodgeBlendIn)
    {
        pose.locomotionPhase = dodgeEntryPhase;
        pose.moveLeft = dodgeEntryLeft;
        pose.moveForward = dodgeEntryForward;
        pose.dodgeEntrySample = dodgeEntrySample;
        pose.dodgeEntryTime = dodgeEntryTime;
        pose.dodgeEntryWeight =
            dodgeEntryWeight * (dodgeFade >= 0 ? Smooth(dodgeFade / kDodgeBlendOut) : 1.f);
    }
}
void CharacterAnimation::Fire()
{
    fireSeconds = .4f;
}
} // namespace models
