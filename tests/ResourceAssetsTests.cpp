#include "ResourceAssets.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
            assert(mesh.boneCount == 23 && !mesh.vertices.empty());
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
            const auto point = ResourceAssets::SkinPoint(mesh.vertices.front(), matrices);
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
    std::cout << "Resource asset loading and animation regression checks passed.\n";
}
