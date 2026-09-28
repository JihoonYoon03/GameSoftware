#include "stdafx.h"
#include "LevelOne.h"
#include "LevelOneNpc.h"
#include "RenderCache.h"
#include "TutorialDrawing.h"
#include "TutorialGraphics.h"
#include <algorithm>
#include <cstdio>

namespace levelone
{
using namespace tutorial;
namespace
{
void DrawMapOverview()
{
    constexpr float left = kCanvasWidth - 210.f, top = 35, cellSize = kMinimapSize / kMapSize;
    DrawRectangle(left - 12, top - 18, 186, 205, ColorRGBA(.025f, .055f, .075f, .95f));
    DrawLabel(left, top, "연결된 격리 구역", ColorRGBA(.6f, .9f, .9f));
    rendercache::DrawMinimapTerrain(left, top + 12);
    for (const auto &npc : GetNpcs())
    {
        DrawEllipse(left + npc.position.x * cellSize, top + 12 + npc.position.y * cellSize, 2, 2,
                    ColorRGBA(.95f, .85f, .25f));
    }
    for (const auto &enemy : g_level.enemies)
    {
        if (enemy.health > 0)
        {
            DrawEllipse(left + enemy.position.x * cellSize, top + 12 + enemy.position.y * cellSize, 2, 2,
                        ColorRGBA(1, .3f, .2f));
        }
    }
    DrawEllipse(left + g_level.entry.x * cellSize, top + 12 + g_level.entry.y * cellSize, 4, 4,
                ColorRGBA(.8f, .65f, 1));
    DrawEllipse(left + g_state.playerPosition.x * cellSize, top + 12 + g_state.playerPosition.y * cellSize, 3,
                3, ColorRGBA(.3f, 1, .85f));
    DrawLabel(left, top + 185, "적: 빨강 / 주민: 노랑 / 귀환: 보라");
}
} // namespace

void DrawHud()
{
    const auto &stats = g_level.player;
    DrawRectangle(22, 20, 390, 220, ColorRGBA(.02f, .05f, .08f, .95f));
    DrawLabel(40, 46, "레벨 1 / 관문 외곽 격리 구역", ColorRGBA(.65f, .93f, .9f), GLUT_BITMAP_HELVETICA_18);
    char text[200];
    std::snprintf(text, sizeof(text), "캐릭터 레벨 %d   체력 %d / %d", stats.level, stats.health,
                  stats.maxHealth);
    DrawLabel(40, 74, text);
    DrawRectangle(40, 85, 350, 8, ColorRGBA(.2f, .08f, .09f));
    DrawRectangle(40, 85, 350 * stats.health / float(stats.maxHealth), 8, ColorRGBA(.8f, .25f, .25f));
    int floor = ExperienceForLevel(stats.level),
        ceiling = ExperienceForLevel((std::min)(100, stats.level + 1));
    float progress = stats.level == 100 ? 1.f : float(stats.experience - floor) / (ceiling - floor);
    std::snprintf(text, sizeof(text), "누적 경험치 %d / %d   공격력 %d   방어력 %d", stats.experience,
                  ceiling, stats.attack, stats.defense);
    DrawLabel(40, 115, text);
    DrawRectangle(40, 125, 350, 7, ColorRGBA(.06f, .17f, .22f));
    DrawRectangle(40, 125, 350 * progress, 7, ColorRGBA(.25f, .8f, .9f));
    std::snprintf(text, sizeof(text), "회복 키트 %d   회수 부품 %d   처치 %d", stats.recoveryKits,
                  stats.salvage, g_level.kills);
    DrawLabel(40, 158, text);
    DrawLabel(40, 186,
              g_level.growthGoalReached ? "성장 목표 완료 / 계속 사냥할 수 있습니다."
                                        : "목표: 캐릭터 레벨 3 달성 및 스탯 성장 확인");
    std::snprintf(text, sizeof(text), "지도 %d×%d / 주민 %u / 적 정원 %u", kMapSize, kMapSize,
                  unsigned(GetNpcs().size()), unsigned(g_level.enemies.size()));
    DrawLabel(40, 215, text, ColorRGBA(.5f, .68f, .72f));
    DrawMapOverview();
    DrawLabel(26, kCanvasHeight - 22.f, "WASD 이동 / Space 공격 / E 대화·줍기·귀환 / Q 회복 / Esc 일시정지");
}
} // namespace levelone
