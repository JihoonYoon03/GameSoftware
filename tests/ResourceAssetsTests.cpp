#include "ResourceAssets.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Link with ResourceAssets.cpp and Profiler.cpp; no window or graphics context is needed.
int main()
{
    using namespace models;
    // Loading the delivered detailed pack (>128 MiB) must succeed before any rendering.
    for (const char *gender : {"Male", "Female"})
    {
        for (const char *role : {"Explorer", "Engineer", "Guard", "Medic", "Researcher", "Miner",
                                 "HeavyCombat", "Pilot", "Civilian"})
        {
            const auto id = ResourceAssets::Find(std::string("character/") + gender + "/" + role);
            const auto &mesh = ResourceAssets::Get(id);
            assert(mesh.boneCount == 23 && !mesh.vertices.empty());
            // Civic reactions plus the original clips and generated Ride exceed the old 32-clip cap.
            assert(mesh.clips.size() > 32);
            for (const char *name : {"Idle", "Run", "Ride", "Hit_Front", "GetUp_Back"})
            {
                assert(std::any_of(mesh.clips.begin(), mesh.clips.end(),
                                   [name](const ResourceClip &clip) { return clip.name == name; }));
            }
            SkinMatrices matrices;
            ResourceAssets::Sample(id, nullptr, matrices);
            assert(std::all_of(matrices.begin(), matrices.end(), [](float v) { return std::isfinite(v); }));
            const auto point = ResourceAssets::SkinPoint(mesh.vertices.front(), matrices);
            assert(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
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
