#include "ResourceAssets.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void TestMixamoRigAndSockets()
{
    using namespace models;
    static_assert(kMaxSkinBones == 72, "Skin palette must hold full 65-bone Mixamo rigs + extra sockets");
    assert(SkinMatrices{}.size() == size_t(kMaxSkinBones) * 16);

    // 1) Synthetic full 65-bone Mixamo skeleton hierarchy (standard mixamorig: prefix)
    AnimationRig mixamo;
    auto addBone = [&](const char *name, int parent) {
        const int idx = int(mixamo.names.size());
        mixamo.names.emplace_back(name);
        mixamo.parents.push_back(parent);
        return idx;
    };
    const int hips = addBone("mixamorig:Hips", -1);
    const int spine = addBone("mixamorig:Spine", hips);
    const int spine1 = addBone("mixamorig:Spine1", spine);
    const int spine2 = addBone("mixamorig:Spine2", spine1);
    const int neck = addBone("mixamorig:Neck", spine2);
    const int head = addBone("mixamorig:Head", neck);
    const int headTop = addBone("mixamorig:HeadTop_End", head);
    (void)headTop;
    const int lShoulder = addBone("mixamorig:LeftShoulder", spine2);
    const int lArm = addBone("mixamorig:LeftArm", lShoulder);
    const int lForeArm = addBone("mixamorig:LeftForeArm", lArm);
    const int lHand = addBone("mixamorig:LeftHand", lForeArm);
    for (const char *finger : {"Thumb", "Index", "Middle", "Ring", "Pinky"})
    {
        int prev = lHand;
        for (int seg = 1; seg <= 4; ++seg)
        {
            prev = addBone((std::string("mixamorig:LeftHand") + finger + std::to_string(seg)).c_str(), prev);
        }
    }
    const int rShoulder = addBone("mixamorig:RightShoulder", spine2);
    const int rArm = addBone("mixamorig:RightArm", rShoulder);
    const int rForeArm = addBone("mixamorig:RightForeArm", rArm);
    const int rHand = addBone("mixamorig:RightHand", rForeArm);
    int rIndexTip = rHand;
    for (const char *finger : {"Thumb", "Index", "Middle", "Ring", "Pinky"})
    {
        int prev = rHand;
        for (int seg = 1; seg <= 4; ++seg)
        {
            prev = addBone((std::string("mixamorig:RightHand") + finger + std::to_string(seg)).c_str(), prev);
            if (std::string(finger) == "Index" && seg == 4)
            {
                rIndexTip = prev;
            }
        }
    }
    const int lUpLeg = addBone("mixamorig:LeftUpLeg", hips);
    const int lLeg = addBone("mixamorig:LeftLeg", lUpLeg);
    const int lFoot = addBone("mixamorig:LeftFoot", lLeg);
    const int lToe = addBone("mixamorig:LeftToeBase", lFoot);
    const int rUpLeg = addBone("mixamorig:RightUpLeg", hips);
    const int rLeg = addBone("mixamorig:RightLeg", rUpLeg);
    const int rFoot = addBone("mixamorig:RightFoot", rLeg);
    const int rToe = addBone("mixamorig:RightToeBase", rFoot);
    assert(mixamo.names.size() == 63);
    assert(mixamo.names.size() < kMaxSkinBones);

    LayeredAnimation::ClassifyRig(mixamo);
    assert(mixamo.preset == RigPreset::Mixamo);
    assert(mixamo.SocketBone(RigSocket::Pelvis) == hips);
    assert(mixamo.spine == spine);
    assert(mixamo.SocketBone(RigSocket::Chest) == spine2);
    assert(mixamo.SocketBone(RigSocket::Back) == spine1);
    assert(mixamo.SocketBone(RigSocket::Head) == head);
    assert(mixamo.SocketBone(RigSocket::HandL) == lHand);
    assert(mixamo.SocketBone(RigSocket::HandR) == rHand);
    assert(mixamo.SocketBone(RigSocket::Muzzle) == rHand); // Falls back to HandR when no dedicated Muzzle bone exists.

    // Upper-body masks must spare pelvis and legs, ramp across Spine -> Spine1 -> Spine2, and cover arms/hands/fingers.
    for (int lower : {hips, lUpLeg, lLeg, lFoot, lToe, rUpLeg, rLeg, rFoot, rToe})
    {
        assert(mixamo.upperBodyMask[size_t(lower)] == 0.f);
        assert(mixamo.upperBodyFullMask[size_t(lower)] == 0.f);
    }
    assert(std::fabs(mixamo.upperBodyMask[size_t(spine)] - 0.35f) < 0.0001f);
    assert(std::fabs(mixamo.upperBodyMask[size_t(spine1)] - 0.75f) < 0.0001f);
    assert(std::fabs(mixamo.upperBodyFullMask[size_t(spine)] - 1.f) < 0.0001f);
    assert(std::fabs(mixamo.upperBodyFullMask[size_t(spine1)] - 1.f) < 0.0001f);
    for (int upper : {spine2, neck, head, lShoulder, lHand, rShoulder, rHand, rIndexTip})
    {
        assert(std::fabs(mixamo.upperBodyMask[size_t(upper)] - 1.f) < 0.0001f);
        assert(std::fabs(mixamo.upperBodyFullMask[size_t(upper)] - 1.f) < 0.0001f);
    }

    // 2) Legacy 23-bone HUMAN_7H character asset must keep LegacyHuman7H classification, exact legacy masks, and valid sockets.
    const auto explorerId = ResourceAssets::Find("character/Male/Explorer");
    const auto &explorer = ResourceAssets::Get(explorerId);
    assert(explorer.rig && explorer.rig->preset == RigPreset::LegacyHuman7H);
    const auto &legacy = *explorer.rig;
    assert(legacy.SocketBone(RigSocket::Pelvis) >= 0);
    assert(legacy.SocketBone(RigSocket::Chest) >= 0);
    assert(legacy.SocketBone(RigSocket::Back) >= 0);
    assert(legacy.SocketBone(RigSocket::Head) >= 0);
    assert(legacy.SocketBone(RigSocket::HandR) >= 0);
    assert(legacy.SocketBone(RigSocket::HandL) >= 0);
    assert(legacy.SocketBone(RigSocket::Muzzle) >= 0);
    for (size_t i = 0; i < 23; ++i)
    {
        const float expected = (i < 2 || i >= 17) ? 0.f : (i == 2 ? 0.35f : (i == 3 ? 0.75f : 1.f));
        assert(std::fabs(legacy.upperBodyMask[i] - expected) < 0.0001f);
    }
    CharacterPose pose{};
    for (unsigned s = 0; s < unsigned(RigSocket::Count); ++s)
    {
        const auto pos = ResourceAssets::Socket(explorerId, pose, RigSocket(s));
        assert(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z));
    }
    const auto headPos = ResourceAssets::Socket(explorerId, pose, RigSocket::Head);
    const auto chestPos = ResourceAssets::Socket(explorerId, pose, RigSocket::Chest);
    const auto pelvisPos = ResourceAssets::Socket(explorerId, pose, RigSocket::Pelvis);
    assert(headPos.z > chestPos.z && chestPos.z > pelvisPos.z);

    // 3) Synthetic 63-bone Mixamo clip must pass LayeredAnimation::Prepare() without hitting the old 32-bone cap.
    mixamo.bind.resize(mixamo.names.size());
    mixamo.inverseBind.resize(mixamo.names.size());
    for (size_t j = 0; j < mixamo.names.size(); ++j)
    {
        AnimationMatrix id{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, float(j) * 0.02f, 1};
        mixamo.bind[j] = id;
        mixamo.inverseBind[j] = AnimationMath::Inverse(id);
    }
    ResourceClip clip63;
    clip63.name = "Idle";
    clip63.frames = 2;
    clip63.duration = 0.5f;
    clip63.matrices.resize(size_t(clip63.frames) * mixamo.names.size() * 16, 0.f);
    for (unsigned f = 0; f < clip63.frames; ++f)
    {
        for (size_t j = 0; j < mixamo.names.size(); ++j)
        {
            for (size_t a = 0; a < 4; ++a)
            {
                clip63.matrices[(f * mixamo.names.size() + j) * 16 + a * 5] = 1.f;
            }
        }
    }
    LayeredAnimation::Prepare(clip63, mixamo);
    assert(clip63.localFrames.size() == size_t(clip63.frames) * mixamo.names.size());

    // 4) Real CQ-11 Mixamo modular asset (Field_Medic_Coat_LOD0) packed via mixamo_characters.json.
    assert(ResourceAssets::TryFind("modular/nonexistent_part") == 0);
    const auto coatId = ResourceAssets::TryFind("modular/outfit/Field_Medic_Coat");
    assert(coatId != 0);
    const auto &coat = ResourceAssets::Get(coatId);
    assert(coat.boneCount == 22);
    assert(coat.rig && coat.rig->preset == RigPreset::Mixamo);
    assert(coat.TriangleCount(LodLevel::Lod0) == 3197);
    assert(coat.TriangleCount(LodLevel::Lod1) < coat.TriangleCount(LodLevel::Lod0));
    assert(coat.TriangleCount(LodLevel::Lod2) < coat.TriangleCount(LodLevel::Lod1));
    assert(coat.TriangleCount(LodLevel::ShadowProxy) < coat.TriangleCount(LodLevel::Lod0));
    for (unsigned s = 0; s < unsigned(RigSocket::Count); ++s)
    {
        assert(coat.rig->SocketBone(RigSocket(s)) >= 0);
        const auto pos = ResourceAssets::Socket(coatId, pose, RigSocket(s));
        assert(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z));
    }
    const auto coatHead = ResourceAssets::Socket(coatId, pose, RigSocket::Head);
    const auto coatChest = ResourceAssets::Socket(coatId, pose, RigSocket::Chest);
    const auto coatPelvis = ResourceAssets::Socket(coatId, pose, RigSocket::Pelvis);
    assert(coatHead.z > coatChest.z && coatChest.z > coatPelvis.z);
    for (const auto &v : coat.packedVertices)
    {
        assert(v.surface == 1); // Fabric matte PBR surface (not metal fallback).
        assert(v.palette == 0); // Authored BaseColor texture preserved without tint override.
    }
    SkinMatrices coatSkin;
    ResourceAssets::Sample(coatId, &pose, coatSkin);
    const auto skinnedPt = ResourceAssets::SkinPoint(coat.packedVertices.front(), coatSkin);
    assert(std::isfinite(skinnedPt.x) && std::isfinite(skinnedPt.y) && std::isfinite(skinnedPt.z));
}
} // namespace

// Link the production resource/animation core; no window or graphics context is needed.
int main()
{
    using namespace models;
    ResourcePoseSampler sampler;
    // Loading and sampling the delivered shared-bank pack must work before rendering.
    for (const char *gender : {"Male", "Female"})
    {
        for (const char *role : {"Explorer", "Engineer", "Guard", "Medic", "Researcher", "Miner",
                                 "HeavyCombat", "Pilot", "Civilian"})
        {
            const auto id = ResourceAssets::Find(std::string("character/") + gender + "/" + role);
            const auto &mesh = ResourceAssets::Get(id);
            assert(mesh.boneCount == 23 && !mesh.packedVertices.empty() && mesh.TriangleCount() > 100);
            assert(mesh.TriangleCount(LodLevel::Lod1) < mesh.TriangleCount(LodLevel::Lod0));
            assert(mesh.TriangleCount(LodLevel::Lod2) < mesh.TriangleCount(LodLevel::Lod1));
            assert(mesh.TriangleCount(LodLevel::ShadowProxy) < mesh.TriangleCount(LodLevel::Lod0));
            // Civic reactions plus the original clips and generated Ride exceed the old 32-clip cap.
            assert(mesh.Clips().size() > 32);
            for (const char *name : {"Idle", "Run", "Ride", "Hit_Front", "GetUp_Back"})
            {
                assert(std::any_of(mesh.Clips().begin(), mesh.Clips().end(),
                                   [name](const ResourceClip &clip) { return clip.name == name; }));
            }
            SkinMatrices matrices;
            ResourceAssets::Sample(id, nullptr, matrices);
            assert(std::all_of(matrices.begin(), matrices.end(), [](float v) { return std::isfinite(v); }));
            const auto point = ResourceAssets::SkinPoint(mesh.packedVertices.front(), matrices);
            assert(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
            const auto &rig = *mesh.rig;
            for (int clip : {rig.idle, rig.walk, rig.run, rig.aim, rig.fire, rig.melee, rig.carryIdle,
                             rig.carryWalk, rig.carryRun, rig.reload})
            {
                assert(clip >= 0 && !mesh.Clips()[size_t(clip)].localFrames.empty());
            }
            for (const auto &sample : rig.samples)
            {
                assert(!mesh.Clips()[size_t(sample.clipIndex)].localFrames.empty());
            }
            // Unused authored reactions remain available without duplicating their local frames.
            auto hit = std::find_if(mesh.Clips().begin(), mesh.Clips().end(),
                                    [](const auto &clip) { return clip.name == "Hit_Front"; });
            assert(hit != mesh.Clips().end() && !hit->matrices.empty() && hit->localFrames.empty());
            CharacterPose pose{};
            auto compare = [&]() {
                SkinMatrices reference, cached;
                ResourceAssets::Sample(id, &pose, reference);
                sampler.Sample(id, pose, cached);
                assert(reference == cached);
                sampler.Sample(id, pose, cached);
                assert(reference == cached);
            };
            compare();
            pose.walking = true;
            pose.animationPhase = 2.4f;
            pose.locomotionSpeed = .7f;
            compare();
            pose.armed = pose.blended = true;
            pose.moveLeft = -.6f;
            pose.moveForward = 1.2f;
            pose.locomotionPhase = .37f;
            pose.aimWeight = .8f;
            pose.fireSeconds = .12f;
            compare();
            pose.fireSeconds = 0;
            pose.reloadProgress = .4f;
            compare();
            pose.meleeSeconds = .32f;
            compare();
            pose.riding = true;
            compare();
            pose.riding = false;
            pose.armed = false;
            compare();
        }
    }
    bool rejected = false;
    try
    {
        ResourceAssets::Get(0);
    }
    catch (const std::out_of_range &)
    {
        rejected = true;
    }
    assert(rejected);
    TestMixamoRigAndSockets();
    std::cout << "Resource asset loading, Mixamo rig/socket and animation regression checks passed.\n";
}
