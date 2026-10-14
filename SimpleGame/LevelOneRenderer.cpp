#include "stdafx.h"
#include "LevelOne.h"
#include "TutorialDrawing.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace levelone
{
using namespace tutorial;
namespace
{
void DrawScanner()
{
    constexpr float centerX = 1136, centerY = 139, radius = 90;
    if (g_level.scannerSeconds <= 0)
    {
        char message[100];
        std::snprintf(message, sizeof(message),
                      g_level.scannerCooldown > 0 ? "F 스캐너 충전 %.1f초" : "F 구조 신호 스캔",
                      g_level.scannerCooldown);
        DrawLabel(1040, 42, message, {.45f, .8f, .8f});
        return;
    }
    DrawRectangle(1014, 22, 244, 266, {.015f, .055f, .065f, .96f});
    DrawLabel(1032, 46, "구조 신호 / 대략적 방위");
    DrawEllipse(centerX, centerY, radius, radius, {.035f, .12f, .14f});
    float progress = 1 - g_level.scannerSeconds / 2.8f;
    float sweep = (std::min)(1.f, progress * 1.6f);
    constexpr int segments = 48;
    for (int i = 0; i < segments; ++i)
    {
        float a = i * kPi * 2 / segments, b = (i + 1) * kPi * 2 / segments;
        DrawLine({centerX + std::cos(a) * radius * sweep, centerY + std::sin(a) * radius * sweep},
                 {centerX + std::cos(b) * radius * sweep, centerY + std::sin(b) * radius * sweep},
                 {.3f, 1, .8f, 1 - progress}, 2);
    }
    for (const auto &contact : g_level.scannerContacts)
    {
        if (g_level.rescueGroups[contact.group].rescued || contact.distance / 42 > sweep)
        {
            continue;
        }
        float r = contact.distance / 42 * radius;
        float x = centerX + std::cos(contact.angle) * r, y = centerY + std::sin(contact.angle) * r;
        DrawGlow({x, y}, 15, {.45f, 1, .7f, 1 - progress});
        DrawEllipse(x, y, 5, 5, {.5f, 1, .7f, 1 - progress});
    }
    DrawEllipse(centerX, centerY, 3, 3, {.7f, 1, .9f});
    DrawLabel(1032, 254, "범위 42 / 표시 오차 있음");
    DrawLabel(1032, 277, "스캔 시 위치 기준 · 곧 소멸");
}
} // namespace
void DrawHud()
{
    const auto &stats = g_level.player;
    DrawRectangle(22, 20, 650, 183, {.02f, .04f, .055f, .94f});
    DrawLabel(40, 47, "낙하한 신호 / 광산 구조 신호 조사", {.85f, .85f, .68f}, GLUT_BITMAP_HELVETICA_18);
    DrawLabel(40, 77, g_level.quest.GetObjective(), {.6f, .95f, .87f});
    char text[180];
    std::snprintf(text, sizeof(text), "체력 %d / %d  ·  레벨 %d  ·  경험치 %d  ·  처치 %d", stats.health,
                  stats.maxHealth, stats.level, stats.experience, g_level.kills);
    DrawLabel(40, 105, text);
    DrawRectangle(40, 116, 440, 7, {.2f, .07f, .07f});
    DrawRectangle(40, 116, 440.f * stats.health / stats.maxHealth, 7, {.8f, .28f, .2f});
    std::snprintf(text, sizeof(text), "%s  %d / %d  ·  %s", g_level.pistol.GetName(),
                  g_level.pistol.GetRounds(), g_level.pistol.GetMagazineSize(),
                  g_level.pistol.IsReloading() ? "재장전 중" : "R 재장전 / 예비 탄창 무제한");
    DrawLabel(40, 148, text, {.95f, .8f, .47f});
    if (g_level.pistol.IsReloading())
    {
        DrawRectangle(40, 156, 440 * g_level.pistol.GetReloadProgress(), 4, {.95f, .75f, .3f});
    }
    std::snprintf(text, sizeof(text), "구조 %d / 12명 · 회복 키트 %d · 부품 %d · 리아 동행",
                  g_level.rescuedPeople, stats.recoveryKits, stats.salvage);
    DrawLabel(40, 185, text, {.55f, .75f, .75f});
    DrawScanner();
    const auto cursor = g_state.cursorPosition;
    float gap = 6 + g_level.pistol.GetSpread() * 120;
    ColorRGBA crosshair = g_level.pistol.IsReloading() ? ColorRGBA(1, .65f, .3f) : ColorRGBA(.75f, 1, .92f);
    DrawLine({cursor.x - gap - 9, cursor.y}, {cursor.x - gap, cursor.y}, crosshair, 2);
    DrawLine({cursor.x + gap, cursor.y}, {cursor.x + gap + 9, cursor.y}, crosshair, 2);
    DrawLine({cursor.x, cursor.y - gap - 9}, {cursor.x, cursor.y - gap}, crosshair, 2);
    DrawLine({cursor.x, cursor.y + gap}, {cursor.x, cursor.y + gap + 9}, crosshair, 2);
    DrawEllipse(cursor.x, cursor.y, 1.5f, 1.5f, crosshair);
    DrawLabel(
        26, kCanvasHeight - 22.f,
        "WASD 이동 / 마우스 조준·좌클릭 사격 / R 재장전 / F 스캔 / E 조사·구조·회수 / Q 회복 / Esc 정지");
}
} // namespace levelone
