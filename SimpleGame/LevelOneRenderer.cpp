#include "stdafx.h"
#include "LevelOne.h"
#include "TutorialDrawing.h"
#include "TutorialGraphics.h"
#include <algorithm>
#include <cstdio>

namespace levelone
{
using namespace tutorial;
namespace
{
bool Visible(Vector2 screen)
{
    return screen.x > -100 && screen.x < kCanvasWidth + 100 && screen.y > -80 &&
           screen.y < kCanvasHeight + 80;
}

void DrawMapOverview()
{
    constexpr float left = kCanvasWidth - 210.f, top = 35, cellSize = 5;
    DrawRectangle(left - 12, top - 18, 186, 205, ColorRGBA(.025f, .055f, .075f, .95f));
    DrawLabel(left, top, "연결된 격리 구역", ColorRGBA(.6f, .9f, .9f));
    for (int y = 0; y < kMapSize; ++y)
    {
        for (int x = 0; x < kMapSize; ++x)
        {
            if (IsWalkable(x, y))
            {
                DrawRectangle(left + x * cellSize, top + 12 + y * cellSize, 4, 4,
                              ColorRGBA(.21f, .34f, .37f));
            }
        }
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
    DrawLabel(left, top + 185, "빨강: 적 / 보라: 귀환");
}
} // namespace

void DrawWorld()
{
    enum class ObjectKind
    {
        Wall,
        Enemy,
        Drop,
        Player,
        Flame
    };
    struct RenderObject
    {
        float depth;
        ObjectKind kind;
        int index;
    };
    std::vector<RenderObject> objects;
    for (int y = 0; y < kMapSize; ++y)
    {
        for (int x = 0; x < kMapSize; ++x)
        {
            Vector2 screen = WorldToScreen(float(x), float(y));
            if (!Visible(screen))
            {
                continue;
            }
            int cell = y * kMapSize + x;
            if (g_level.tiles[cell] == Tile::Wall)
            {
                objects.push_back({float(x + y + 2), ObjectKind::Wall, cell});
                continue;
            }
            DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal, ColorRGBA(.23f, .3f, .33f));
            if (g_level.tiles[cell] == Tile::Water)
            {
                Vector2 a = WorldToScreen(float(x), float(y)), b = WorldToScreen(x + 1.f, float(y));
                Vector2 c = WorldToScreen(x + 1.f, y + 1.f), d = WorldToScreen(float(x), y + 1.f);
                const RenderPoint vertices[] = {{a.x, a.y}, {b.x, b.y}, {c.x, c.y}, {d.x, d.y}};
                GetRenderer().DrawEffect(vertices, SurfaceEffect::Water, g_state.animationTimeSeconds);
            }
            if (cell % 71 == 0 && Distance({x + .5f, y + .5f}, g_level.entry) > 3)
            {
                objects.push_back({x + y + 1.f, ObjectKind::Flame, cell});
            }
        }
    }
    for (size_t i = 0; i < g_level.drops.size(); ++i)
    {
        objects.push_back(
            {g_level.drops[i].position.x + g_level.drops[i].position.y, ObjectKind::Drop, int(i)});
    }
    for (size_t i = 0; i < g_level.enemies.size(); ++i)
    {
        if (g_level.enemies[i].health > 0)
        {
            objects.push_back(
                {g_level.enemies[i].position.x + g_level.enemies[i].position.y, ObjectKind::Enemy, int(i)});
        }
    }
    objects.push_back({g_state.playerPosition.x + g_state.playerPosition.y, ObjectKind::Player, 0});
    std::stable_sort(objects.begin(), objects.end(),
                     [](const RenderObject &a, const RenderObject &b) { return a.depth < b.depth; });
    for (const RenderObject &object : objects)
    {
        if (object.kind == ObjectKind::Wall)
        {
            int x = object.index % kMapSize, y = object.index / kMapSize;
            DrawBox(float(x), float(y), .98f, .98f, 18, ColorRGBA(.18f, .23f, .28f));
        }
        else if (object.kind == ObjectKind::Enemy)
        {
            const Enemy &enemy = g_level.enemies[object.index];
            Vector2 screen = WorldToScreen(enemy.position.x, enemy.position.y);
            if (!Visible(screen))
            {
                continue;
            }
            DrawSoftShadow(screen, 23, 8, .5f);
            DrawCachedModel(models::Get().drone, screen);
            DrawRectangle(screen.x - 18, screen.y - 37, 36, 4, ColorRGBA(.12f, .07f, .08f));
            DrawRectangle(screen.x - 18, screen.y - 37, 36 * enemy.health / float(kEnemyMaxHealth), 4,
                          ColorRGBA(.95f, .28f, .24f));
        }
        else if (object.kind == ObjectKind::Drop)
        {
            const Drop &drop = g_level.drops[object.index];
            Vector2 screen = WorldToScreen(drop.position.x, drop.position.y);
            if (!Visible(screen))
            {
                continue;
            }
            DrawGlow(screen, 13, ColorRGBA(.3f, .9f, .7f));
            DrawCachedModel(drop.kind == ItemKind::RecoveryKit ? models::Get().recoveryKit
                                                               : models::Get().salvage,
                            screen);
        }
        else if (object.kind == ObjectKind::Player)
        {
            DrawPerson(g_state.playerPosition, ColorRGBA(.75f, .86f, .92f), true);
            if (g_level.attackFlash > 0)
            {
                Vector2 center = WorldToScreen(g_state.playerPosition.x, g_state.playerPosition.y);
                DrawEffectRectangle({center.x - 91, center.y - 45}, 182, 90, SurfaceEffect::Pulse);
            }
        }
        else
        {
            Vector2 center = WorldToScreen(object.index % kMapSize + .5f, object.index / kMapSize + .5f);
            DrawEffectRectangle({center.x - 12, center.y - 43}, 24, 44, SurfaceEffect::Flame);
        }
    }
    Vector2 entry = WorldToScreen(g_level.entry.x, g_level.entry.y);
    DrawEffectRectangle({entry.x - 24, entry.y - 12}, 48, 24, SurfaceEffect::Pulse);
    DrawLabel(entry.x - 35, entry.y - 30, "E 도시 귀환", ColorRGBA(.75f, .9f, 1));
}

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
    std::snprintf(text, sizeof(text), "지도 시드: %u", g_level.seed);
    DrawLabel(40, 215, text, ColorRGBA(.5f, .68f, .72f));
    DrawMapOverview();
    DrawLabel(26, kCanvasHeight - 22.f, "WASD 이동 / Space 공격 / E 줍기·귀환 / Q 회복 / Esc 일시정지");
}
} // namespace levelone
