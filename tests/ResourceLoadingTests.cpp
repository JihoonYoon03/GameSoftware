#include "stdafx.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cassert>
#include <iostream>

int main()
{
    for (const auto &name : {"character/Male/Explorer", "character/Female/Engineer", "hair/Bob"})
    {
        const auto &mesh = models::ResourceAssets::Get(models::ResourceAssets::Find(name));
        assert(mesh.boneCount == 23 && mesh.Clips().size() > 32);
        for (const auto &required :
             {"Idle", "Walk", "Run", "Aim_Base", "Attack_Fire", "Ride", "Hit_Front", "Aim_Walk_Left",
              "Aim_Run_Back", "Pistol_Carry_Idle", "Pistol_Carry_Walk", "Pistol_Carry_Run", "Reload_Pistol"})
        {
            auto clip = std::find_if(mesh.Clips().begin(), mesh.Clips().end(),
                                     [&](const models::ResourceClip &c) { return c.name == required; });
            assert(clip != mesh.Clips().end() && clip->frames >= 2 && !clip->matrices.empty());
            if (clip->name == "Walk")
            {
                const size_t palette = mesh.boneCount * 16;
                bool animated = false;
                for (size_t i = palette; i < clip->matrices.size(); ++i)
                {
                    animated |= std::fabs(clip->matrices[i] - clip->matrices[i % palette]) > .001f;
                }
                assert(animated); // Loading names alone must not mask an empty/bind-pose animation.
            }
        }
    }
    const auto &male = models::ResourceAssets::Get(models::ResourceAssets::Find("character/Male/Explorer"));
    const auto &female =
        models::ResourceAssets::Get(models::ResourceAssets::Find("character/Female/Engineer"));
    assert(&male.Clips() == &female.Clips()); // Identical banks must share immutable CPU storage too.
    static_assert(sizeof(models::Model) < 128,
                  "Imported bindings must not retain unused procedural mesh arrays");
    std::cout << "Latest authored clips move, share animation storage and use compact model bindings.\n";
}
