#include "stdafx.h"
#include "Animation.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace models
{
namespace
{
using PoseBuffer = std::array<BonePose, 32>;
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
void Globals(const AnimationRig &rig, const PoseBuffer &local, std::array<AnimationMatrix, 32> &global)
{
    for (size_t j = 0; j < rig.parents.size(); ++j)
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
    std::array<AnimationMatrix, 32> baseWorld{}, layerWorld{}, resultWorld{};
    Globals(rig, base, baseWorld);
    Globals(rig, layer, layerWorld);
    BonePose pelvisOffset;
    pelvisOffset.translation = {baseWorld[1][12] - layerWorld[1][12], baseWorld[1][13] - layerWorld[1][13],
                                baseWorld[1][14] - layerWorld[1][14]};
    // Follow pelvis translation, while the aim layer keeps its component-space orientation.
    auto correction = AnimationMath::Matrix(pelvisOffset);
    for (size_t j = 0; j < rig.parents.size(); ++j)
    {
        // HUMAN_7H spine through muzzle: branch mask leaves root, pelvis and six leg bones intact.
        float mask = j < 2 || j >= 17 ? 0.f : fullMask ? 1.f : j == 2 ? .35f : j == 3 ? .75f : 1.f;
        float w = std::clamp(alpha * mask, 0.f, 1.f);
        BonePose local = AnimationMath::Blend(base[j], layer[j], w);
        if (w > 0)
        {
            // Component-space rotation keeps the barrel/arms stable over the moving pelvis.
            auto from = AnimationMath::Pose(baseWorld[j]).rotation;
            auto to = AnimationMath::Pose(AnimationMath::Multiply(correction, layerWorld[j])).rotation;
            auto desired = AnimationMath::Slerp(from, to, w);
            auto parent = AnimationMath::Pose(resultWorld[size_t(rig.parents[j])]).rotation;
            local.rotation = Product({-parent.x, -parent.y, -parent.z, parent.w}, desired);
        }
        base[j] = local;
        auto m = AnimationMath::Matrix(local);
        resultWorld[j] =
            rig.parents[j] < 0 ? m : AnimationMath::Multiply(resultWorld[size_t(rig.parents[j])], m);
    }
}
} // namespace

// ============================================================================
// AnimationMath
// ============================================================================
AnimationMatrix AnimationMath::Multiply(const AnimationMatrix &a, const AnimationMatrix &b)
{
    AnimationMatrix m{};
    for (size_t c = 0; c < 4; ++c)
    {
        for (size_t r = 0; r < 4; ++r)
        {
            for (size_t k = 0; k < 4; ++k)
            {
                m[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
            }
        }
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
void LayeredAnimation::Prepare(ResourceClip &clip, const AnimationRig &rig)
{
    size_t bones = rig.parents.size();
    clip.localFrames.resize(size_t(clip.frames) * bones);
    for (unsigned frame = 0; frame < clip.frames; ++frame)
    {
        std::array<AnimationMatrix, 32> world{};
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
void LayeredAnimation::Sample(const ResourceMesh &mesh, const CharacterPose &pose,
                              std::array<float, 512> &skin)
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
        ClipPose(mesh, rig.idle, pose.animationPhase / 10 / 2, true, base);
        ClipPose(mesh, rig.walk, phase, true, other);
        Mix(base, other, std::clamp(speed / 1.08f, 0.f, 1.f), mesh.boneCount);
    }
    else
    {
        ClipPose(mesh, rig.walk, phase, true, base);
        ClipPose(mesh, rig.run, phase, true, other);
        Mix(base, other, std::clamp((speed - 1.08f) / (2.46f - 1.08f), 0.f, 1.f), mesh.boneCount);
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
    if (pose.armed && rig.carryIdle >= 0 && rig.carryWalk >= 0 && rig.carryRun >= 0)
    {
        PoseBuffer carry{}, moving{};
        if (speed <= 1.08f)
        {
            ClipPose(mesh, rig.carryIdle, pose.animationPhase / 10 / 2, true, carry);
            ClipPose(mesh, rig.carryWalk, phase, true, moving);
            Mix(carry, moving, std::clamp(speed / 1.08f, 0.f, 1.f), mesh.boneCount);
        }
        else
        {
            ClipPose(mesh, rig.carryWalk, phase, true, carry);
            ClipPose(mesh, rig.carryRun, phase, true, moving);
            Mix(carry, moving, std::clamp((speed - 1.08f) / (2.46f - 1.08f), 0.f, 1.f), mesh.boneCount);
        }
        ClipPose(mesh, rig.aim, 0, false, other);
        Mix(carry, other, aim, mesh.boneCount);
        UpperLayer(mesh, base, carry, 1, true);
    }
    else if (aim > 0)
    {
        ClipPose(mesh, rig.aim, 0, false, other);
        UpperLayer(mesh, base, other, aim);
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
    if (pose.meleeSeconds > 0)
    {
        float t = 1 - std::clamp(pose.meleeSeconds / .65f, 0.f, 1.f);
        ClipPose(mesh, rig.melee, t, false, other);
        UpperLayer(mesh, base, other, Smooth(t / .08f) * Smooth((1 - t) / .18f));
    }
    std::array<AnimationMatrix, 32> world{};
    Globals(rig, base, world);
    for (size_t j = 0; j < mesh.boneCount; ++j)
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
            const float *m = skin.data() + sole.bone * 16;
            minimum = (std::min)(minimum, m[2] * sole.position.x + m[6] * sole.position.y +
                                              m[10] * sole.position.z + m[14]);
        }
        float lift = std::clamp(.0007f - minimum, 0.f, .03f);
        for (size_t j = 1; j < mesh.boneCount; ++j)
        {
            skin[j * 16 + 14] += lift;
        }
    }
}

// ============================================================================
// CharacterAnimation
// ============================================================================
void CharacterAnimation::Update(float dt, Point worldVelocity, float facingYaw, bool aiming, bool riding)
{
    dt = std::clamp(dt, 0.f, .05f);
    if (riding)
    {
        velocity = {};
        left = forward = 0;
        aimWeight = 0;
        fireSeconds = 0;
        return;
    }
    float weight = 1 - std::exp(-16 * dt);
    velocity = Lerp(velocity, worldVelocity, weight);
    float cy = std::cos(facingYaw), sy = std::sin(facingYaw);
    left = (-sy * velocity.x + cy * velocity.y) * WorldScale::metersPerUnit;
    forward = (cy * velocity.x + sy * velocity.y) * WorldScale::metersPerUnit;
    float speed = std::hypot(left, forward);
    if (std::hypot(worldVelocity.x, worldVelocity.y) > .001f)
    {
        float runWeight = std::clamp((speed - 1.08f) / (2.46f - 1.08f), 0.f, 1.f);
        float reference = 1.08f + (2.46f - 1.08f) * runWeight;
        float frequency = (1 + .5f * runWeight) * std::clamp(speed / reference, .1f, 2.f);
        phase = std::fmod(phase + dt * frequency, 1.f);
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
}
void CharacterAnimation::Fire()
{
    fireSeconds = .4f;
}
} // namespace models
