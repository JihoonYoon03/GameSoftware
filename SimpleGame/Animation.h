#pragma once
#include "ModelLibrary.h"
#include <array>
#include <string>
#include <vector>

namespace models
{
constexpr unsigned int kMaxSkinBones = 72;
using SkinMatrices = std::array<float, kMaxSkinBones * 16>;
enum class RigPreset : std::uint8_t
{
    LegacyHuman7H,
    Mixamo,
    Custom
};
enum class RigSocket : std::uint8_t
{
    Head,
    Chest,
    Back,
    Pelvis,
    HandR,
    HandL,
    ForearmR,
    ForearmL,
    Muzzle,
    Count
};
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
    RigPreset preset = RigPreset::LegacyHuman7H;
    int idle = -1, walk = -1, run = -1, aim = -1, fire = -1, melee = -1, muzzle = -1, handR = -1;
    int head = -1, chest = -1, back = -1, pelvis = -1, spine = -1, handL = -1, forearmR = -1, forearmL = -1;
    std::array<float, kMaxSkinBones> upperBodyMask = {};
    std::array<float, kMaxSkinBones> upperBodyFullMask = {};
    int carryIdle = -1, carryWalk = -1, carryRun = -1, reload = -1;
    int SocketBone(RigSocket socket) const
    {
        switch (socket)
        {
        case RigSocket::Head:
            return head;
        case RigSocket::Chest:
            return chest;
        case RigSocket::Back:
            return back;
        case RigSocket::Pelvis:
            return pelvis;
        case RigSocket::HandR:
            return handR;
        case RigSocket::HandL:
            return handL;
        case RigSocket::ForearmR:
            return forearmR >= 0 ? forearmR : handR;
        case RigSocket::ForearmL:
            return forearmL >= 0 ? forearmL : handL;
        case RigSocket::Muzzle:
            return muzzle >= 0 ? muzzle : handR;
        default:
            return -1;
        }
    }
    std::array<int, 16> dodgeClips = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // CQ-07C2 shared support bank (8 clips): Treat(Enter/Loop/Exit), Inspect(Enter/Loop/Exit), Carry(Idle/Walk).
    std::array<int, 8> supportClips = {-1, -1, -1, -1, -1, -1, -1, -1};
    // Directional transition bank (12 clips): Run_Turn180(L/R), Turn90(L/R), Aim_Strafe_Start(F/B/L/R), Stop(F/B/L/R).
    std::array<int, 12> transitionClips = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // CQ-11B shared facility/talk bank (7 clips): CounterIdle, Welcome, HandOver, Explain, Farewell, Inspect, Listen.
    std::array<int, 7> facilityClips = {-1, -1, -1, -1, -1, -1, -1};
    // CQ-06B/C shared vehicle boarding bank (12 clips): Coupe(Enter/Exit L/R, DriverIdle), Courier(Enter/Exit L, PilotIdle/Ascent/BankL/R/Land).
    std::array<int, 12> boardingClips = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
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
    static void ClassifyRig(AnimationRig &rig);
    static void BindDodgeClip(AnimationRig &rig, const std::string &name, int index);
    static void BindSupportClip(AnimationRig &rig, const std::string &name, int index);
    static void BindTransitionClip(AnimationRig &rig, const std::string &name, int index);
    static void BindFacilityClip(AnimationRig &rig, const std::string &name, int index);
    static void BindBoardingClip(AnimationRig &rig, const std::string &name, int index);
    static void Prepare(ResourceClip &clip, const AnimationRig &rig);
    // Prepare only graph-referenced clips; keep every authored matrix clip available.
    static void PrepareGraph(std::vector<ResourceClip> &clips, const AnimationRig &rig);
    static void Sample(const ResourceMesh &mesh, const CharacterPose &pose, SkinMatrices &skin);
};
// ============================================================================
// CharacterAnimation — simulation-owned phase, smoothed velocity and aim transitions
// ============================================================================
class CharacterAnimation
{
  public:
    void Update(float deltaSeconds, Point worldVelocity, float facingYaw, bool aiming, bool riding);
    void Apply(CharacterPose &pose) const;
    void StartDodge(Point direction, float facingYaw);
    void InterruptDodge();
    void TriggerTurn(bool run180, bool rightTurn);
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
    float DodgeWeight() const;
    float TransitionWeight() const;
    Point velocity = {};
    float phase = 0, aimWeight = 0, left = 0, forward = 0, fireSeconds = 0;
    int dodgeSample = -1;
    float dodgeTime = 0, dodgeFade = -1, dodgeInterruptWeight = 0;
    float dodgeEntryLeft = 0, dodgeEntryForward = 0, dodgeEntryPhase = 0;
    int dodgeEntrySample = -1;
    float dodgeEntryTime = 0, dodgeEntryWeight = 0;
    int transitionClip = -1;
    float transitionTime = 0, transitionDuration = 0;
    bool wasAimMoving = false;
    int lastAimDir = 0;
};
} // namespace models
