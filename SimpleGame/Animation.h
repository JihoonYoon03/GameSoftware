#pragma once
#include "ModelLibrary.h"
#include <array>
#include <string>
#include <vector>

namespace models
{
using AnimationMatrix = std::array<float, 16>;
struct AnimationRotation
{
    float x = 0, y = 0, z = 0, w = 1;
};
struct BonePose
{
    Point translation = {}, scale = {1, 1, 1};
    AnimationRotation rotation;
};
// ============================================================================
// AnimationMath — affine transforms and shortest-path rotation interpolation
// ============================================================================
class AnimationMath
{
  public:
    static AnimationMatrix Multiply(const AnimationMatrix &a, const AnimationMatrix &b);
    static AnimationMatrix Inverse(const AnimationMatrix &matrix);
    static AnimationMatrix Matrix(const BonePose &pose);
    static BonePose Pose(const AnimationMatrix &matrix);
    static AnimationRotation Slerp(AnimationRotation a, AnimationRotation b, float weight);
    static BonePose Blend(const BonePose &a, const BonePose &b, float weight);
};
struct AnimationSample
{
    std::string clip;
    float left = 0, forward = 0;
    int clipIndex = -1;
};
struct SolePoint
{
    unsigned int bone = 0;
    Point position = {};
};
struct AnimationRig
{
    std::vector<int> parents;
    std::vector<std::string> names;
    std::vector<AnimationMatrix> bind, inverseBind;
    std::vector<AnimationSample> samples;
    std::vector<std::array<unsigned int, 3>> triangles;
    std::vector<SolePoint> soles;
    int idle = -1, walk = -1, run = -1, aim = -1, fire = -1, melee = -1, muzzle = -1;
};
struct AnimationWeights
{
    std::array<unsigned int, 3> samples = {};
    std::array<float, 3> weights = {1, 0, 0};
};
// ============================================================================
// AnimationBlendSpace — velocity-space triangulation and boundary projection
// ============================================================================
class AnimationBlendSpace
{
  public:
    static AnimationWeights Evaluate(const AnimationRig &rig, float left, float forward);
};
struct ResourceMesh;
struct ResourceClip;
// ============================================================================
// LayeredAnimation — synchronized local-space sampling and per-bone overrides
// ============================================================================
class LayeredAnimation
{
  public:
    static void Prepare(ResourceClip &clip, const AnimationRig &rig);
    static void Sample(const ResourceMesh &mesh, const CharacterPose &pose, std::array<float, 512> &skin);
};
// ============================================================================
// CharacterAnimation — simulation-owned phase, smoothed velocity and aim transitions
// ============================================================================
class CharacterAnimation
{
  public:
    void Update(float deltaSeconds, Point worldVelocity, float facingYaw, bool aiming, bool riding);
    void Apply(CharacterPose &pose) const;
    void Fire();
    float Phase() const
    {
        return phase;
    }
    float AimWeight() const
    {
        return aimWeight;
    }

  private:
    Point velocity = {};
    float phase = 0, aimWeight = 0, left = 0, forward = 0, fireSeconds = 0;
};
} // namespace models
