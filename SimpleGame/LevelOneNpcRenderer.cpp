#include "stdafx.h"
#include "LevelOneNpc.h"
#include "TutorialDrawing.h"
#include <cmath>
#include <cstdio>

namespace levelone
{
void DrawNpc(size_t index)
{
    const auto &npc = GetNpcs().at(index);
    const auto &profile = GetNpcProfile(index);
    tutorial::ColorRGBA coat(.65f, .68f, .72f);
    auto type = models::CharacterType::Explorer;
    switch (profile.pattern)
    {
    case NpcPattern::Engineer:
        coat = {.85f, .65f, .25f};
        type = models::CharacterType::Engineer;
        break;
    case NpcPattern::Guard:
        coat = {.35f, .55f, .85f};
        type = models::CharacterType::Guard;
        break;
    case NpcPattern::Medic:
        coat = {.8f, .95f, .86f};
        type = models::CharacterType::Medic;
        break;
    case NpcPattern::Courier:
        coat = {.35f, .8f, .55f};
        break;
    case NpcPattern::Researcher:
        coat = {.7f, .45f, .85f};
        type = models::CharacterType::Researcher;
        break;
    case NpcPattern::Civilian:
        coat = {.72f, .5f, .42f};
        break;
    case NpcPattern::Trader:
        coat = {.9f, .75f, .5f};
        break;
    }
    float shade = .8f + float(npc.profileIndex % 4) * .07f;
    coat.r *= shade;
    coat.g *= shade;
    coat.b *= shade;
    bool walking = npc.routeIndex < npc.route.size() && npc.health > 0 && npc.talkSeconds <= 0;
    tutorial::DrawPerson(npc.position, coat, false, type, walking);
    auto origin = tutorial::WorldToScreen(npc.position.x, npc.position.y);
    if (npc.carryingSupplies)
    {
        tutorial::DrawRectangle(origin.x + 12, origin.y - 26, 13, 14, {.55f, .4f, .2f});
        tutorial::DrawLine({origin.x + 13, origin.y - 19}, {origin.x + 24, origin.y - 19}, {.9f, .75f, .3f},
                           2);
    }
    if (npc.actionFlash > 0)
    {
        auto target = tutorial::WorldToScreen(npc.actionTarget.x, npc.actionTarget.y, 20);
        auto hand = tutorial::Vector2{origin.x + 15, origin.y - 28};
        if (profile.pattern == NpcPattern::Guard || npc.profileIndex == 14 || npc.profileIndex == 19)
        {
            tutorial::DrawLine(hand, target, {.55f, .85f, 1, .8f}, 2);
            tutorial::DrawGlow(hand, 7, {.6f, .9f, 1});
            tutorial::DrawGlow(target, 6, {1, .45f, .2f});
        }
        else if (npc.activity == NpcActivity::Repairing)
        {
            tutorial::DrawLine(hand, target, {.5f, .8f, .9f}, 2);
            float flicker = std::sin(tutorial::g_state.animationTimeSeconds * 24 + float(index));
            tutorial::DrawLine(target, {target.x + 8 + flicker * 6, target.y - 9}, {1, .8f, .3f}, 2);
            tutorial::DrawGlow(target, 6, {1, .65f, .25f});
        }
        else if (npc.activity == NpcActivity::Healing)
        {
            tutorial::DrawGlow(target, 18, {.3f, 1, .6f});
            tutorial::DrawRectangle(target.x - 2, target.y - 14, 4, 12, {.8f, 1, .9f});
            tutorial::DrawRectangle(target.x - 6, target.y - 10, 12, 4, {.8f, 1, .9f});
        }
    }
    if (npc.health < npc.maxHealth)
    {
        tutorial::DrawRectangle(origin.x - 14, origin.y - 59, 28, 3, {.12f, .08f, .1f});
        tutorial::DrawRectangle(origin.x - 14, origin.y - 59, 28.f * npc.health / npc.maxHealth, 3,
                                {.35f, .95f, .55f});
    }
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
void DrawWorkstation(size_t index)
{
    using namespace tutorial;
    const auto &station = GetWorkstations().at(index);
    auto center = WorldToScreen(station.position.x, station.position.y);
    ColorRGBA accent = station.status == WorkstationStatus::Threatened    ? ColorRGBA(1, .4f, .25f)
                       : station.status == WorkstationStatus::Operational ? ColorRGBA(.25f, 1, .6f)
                                                                          : ColorRGBA(.35f, .75f, .9f);
    DrawSoftShadow(center, 32, 12, .55f);
    DrawBox(station.position.x - .5f, station.position.y - .5f, 1, 1, 18, {.22f, .28f, .33f});
    if (station.type == WorkstationType::CommunicationRelay)
    {
        DrawLine({center.x, center.y - 18}, {center.x, center.y - 78}, {.6f, .7f, .75f}, 5);
        DrawEllipse(center.x, center.y - 74, 22, 8, accent);
        DrawLine({center.x - 21, center.y - 73}, {center.x + 21, center.y - 73}, {.8f, .9f, 1}, 2);
    }
    else if (station.type == WorkstationType::EmergencyPower)
    {
        for (int i = 0; i < 3; ++i)
        {
            DrawRectangle(center.x - 23 + i * 16.f, center.y - 50, 13, 29, {.35f, .42f, .48f});
            DrawRectangle(center.x - 20 + i * 16.f, center.y - 45, 7, 18, accent);
        }
    }
    else if (station.type == WorkstationType::WaterPump)
    {
        DrawRectangle(center.x - 17, center.y - 51, 34, 29, {.32f, .42f, .48f});
        DrawEllipse(center.x, center.y - 52, 17, 7, {.45f, .58f, .65f});
        DrawEllipse(center.x, center.y - 24, 17, 7, {.2f, .32f, .4f});
        DrawLine({center.x + 16, center.y - 39}, {center.x + 29, center.y - 31}, accent, 6);
    }
    else
    {
        DrawRectangle(center.x - 24, center.y - 60, 6, 40, {.5f, .6f, .65f});
        DrawRectangle(center.x + 18, center.y - 60, 6, 40, {.5f, .6f, .65f});
        DrawRectangle(center.x - 24, center.y - 60, 48, 6, {.5f, .6f, .65f});
        DrawLine({center.x, center.y - 56}, {center.x, center.y - 32}, accent, 3);
        DrawRectangle(center.x - 7, center.y - 33, 14, 10, {.7f, .55f, .35f});
    }
    DrawGlow({center.x + 12, center.y - 20}, 8, accent);
    DrawRectangle(center.x - 26, center.y + 8, 52, 4, {.1f, .16f, .19f});
    DrawRectangle(center.x - 26, center.y + 8, 52 * station.repairProgress / 100, 4, accent);
    if (Distance(g_state.playerPosition, station.position) < 9)
    {
        DrawLabel(center.x - 50, center.y - 92, station.name, accent);
        char text[160];
        std::snprintf(text, sizeof(text), "%s / 수리 %.0f%% / 부품 %d",
                      GetWorkstationStatusName(station.status), station.repairProgress, station.supplies);
        DrawLabel(center.x - 80, center.y + 29, text, accent);
    }
}
} // namespace levelone
