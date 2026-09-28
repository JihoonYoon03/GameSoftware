#include "stdafx.h"
#include "LevelOneNpc.h"
#include "TutorialDrawing.h"

namespace levelone
{
void DrawNpc(size_t index)
{
    const auto &npc = GetNpcs().at(index);
    const auto &profile = GetNpcProfile(index);
    tutorial::ColorRGBA coat(.65f, .68f, .72f);
    switch (profile.pattern)
    {
    case NpcPattern::Engineer:
        coat = {.85f, .65f, .25f};
        break;
    case NpcPattern::Guard:
        coat = {.35f, .55f, .85f};
        break;
    case NpcPattern::Medic:
        coat = {.8f, .95f, .86f};
        break;
    case NpcPattern::Courier:
        coat = {.35f, .8f, .55f};
        break;
    case NpcPattern::Researcher:
        coat = {.7f, .45f, .85f};
        break;
    case NpcPattern::Civilian:
        coat = {.72f, .5f, .42f};
        break;
    case NpcPattern::Trader:
        coat = {.9f, .75f, .5f};
        break;
    }
    tutorial::DrawPerson(npc.position, coat);
    if (tutorial::Distance(npc.position, tutorial::g_state.playerPosition) < 4)
    {
        auto screen = tutorial::WorldToScreen(npc.position.x, npc.position.y, 62);
        tutorial::DrawLabel(screen.x - 30, screen.y,
                            std::string(profile.name) + " / " + GetNpcActivityName(npc.activity), coat);
        if (tutorial::Distance(npc.position, tutorial::g_state.playerPosition) < 1.7f)
        {
            tutorial::DrawLabel(screen.x - 30, screen.y - 20, std::string("E ") + profile.personality);
        }
    }
}
} // namespace levelone
