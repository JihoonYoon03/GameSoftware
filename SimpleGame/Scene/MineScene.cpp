#include "stdafx.h"
#include "WorldScene.h"
#include "../LevelOne.h"
#include "../RenderCache.h"
#include "../TutorialDrawing.h"
#include <cmath>

namespace levelone
{
using namespace tutorial;
namespace
{
void PlacePerson(game::Scene &scene, const std::string &id, Vector2 position, ColorRGBA coat,
                 models::CharacterType type, bool walking, const std::string &label)
{
    if (LightAt(position) < .025f)
    {
        return;
    }
    PlaceWorldActor(scene, id, position.x, position.y, WorldBounds(position.x, position.y, 0, 0, 85, 100), 1,
                    position.x + position.y, [position, coat, type, walking, label]() {
                        DrawPerson(position, coat, false, type, walking);
                        if (Distance(position, g_state.playerPosition) < 3 && !label.empty())
                        {
                            auto screen = WorldToScreen(position.x, position.y, 66);
                            DrawLabel(screen.x - 25, screen.y, label, coat);
                        }
                    });
}
void PlaceClue(game::Scene &scene, const char *id, Vector2 position, const char *label, ColorRGBA color,
               bool glyph = false)
{
    PlaceWorldActor(scene, id, position.x, position.y, WorldBounds(position.x, position.y, 1, 1, 80, 180), 1,
                    position.x + position.y, [position, label, color, glyph]() {
                        DrawBox(position.x - .25f, position.y - .25f, .5f, .5f, glyph ? 65.f : 25.f,
                                glyph ? ColorRGBA(.25f, .23f, .28f) : ColorRGBA(.25f, .3f, .32f));
                        auto center = WorldToScreen(position.x, position.y, glyph ? 48.f : 30.f);
                        DrawGlow(center, 15, color);
                        if (glyph)
                        {
                            DrawPolygon({{center.x, center.y - 12},
                                         {center.x + 15, center.y},
                                         {center.x, center.y + 12},
                                         {center.x - 15, center.y}},
                                        color);
                            DrawEllipse(center.x, center.y, 3, 7, {.04f, .06f, .08f});
                        }
                        if (Distance(position, g_state.playerPosition) < 4 && LightAt(position) > .1f)
                        {
                            DrawLabel(center.x - 55, center.y - 26, label, color);
                        }
                    });
}
} // namespace
void DrawWorld()
{
    auto &scene = GetWorldScene(WorldSceneKind::Mine);
    scene.BeginPlacement();
    constexpr int chunkSize = 8;
    for (int y0 = 0; y0 < kMapSize; y0 += chunkSize)
    {
        for (int x0 = 0; x0 < kMapSize; x0 += chunkSize)
        {
            std::string key = "terrain/mine/" + std::to_string(x0) + "/" + std::to_string(y0);
            PlaceWorldActor(
                scene, key, float(x0), float(y0), WorldBounds(float(x0), float(y0), chunkSize, chunkSize, 0),
                0, -1, [x0, y0, key, chunkSize]() {
                    rendercache::DrawWorld(key, [x0, y0, chunkSize]() {
                        for (int y = y0; y < y0 + chunkSize; ++y)
                        {
                            for (int x = x0; x < x0 + chunkSize; ++x)
                            {
                                if (g_level.tiles[y * kMapSize + x] != Tile::Wall)
                                {
                                    bool ruins = x >= 42 && y >= 42;
                                    DrawMaterialTile(float(x), float(y), SurfaceMaterial::Concrete,
                                                     ruins ? ColorRGBA(.33f, .29f, .37f)
                                                           : ColorRGBA(.28f, .25f, .22f));
                                    if ((y == 10 && x <= 24) || (x == 24 && y <= 29) ||
                                        (y == 29 && x >= 24 && x <= 48))
                                    {
                                        auto a = WorldToScreen(x + .2f, y + .2f);
                                        auto b = WorldToScreen(x + .8f, y + .8f);
                                        DrawLine(a, b, {.42f, .38f, .28f}, 2);
                                    }
                                }
                            }
                        }
                    });
                });
        }
    }
    for (int y = 1; y < kMapSize - 1; ++y)
    {
        for (int x = 1; x < kMapSize - 1; ++x)
        {
            bool gate = IsGateCell(x, y);
            if (g_level.tiles[y * kMapSize + x] != Tile::Wall && !gate)
            {
                continue;
            }
            if (gate && g_level.quest.IsPowerBypassed())
            {
                continue;
            }
            bool adjacent = false;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    adjacent = adjacent || g_level.tiles[(y + dy) * kMapSize + x + dx] != Tile::Wall;
                }
            }
            if (adjacent)
            {
                PlaceWorldActor(scene, "rock/" + std::to_string(y * kMapSize + x), float(x), float(y),
                                WorldBounds(float(x), float(y), 1, 1, 38), 1, float(x + y + 2),
                                [x, y, gate]() {
                                    DrawBox(float(x), float(y), .98f, .98f, gate ? 48.f : 28.f,
                                            gate ? ColorRGBA(.38f, .22f, .16f) : ColorRGBA(.24f, .22f, .23f));
                                });
            }
        }
    }
    for (size_t i = 0; i < g_level.lights.size(); ++i)
    {
        const auto lamp = g_level.lights[i];
        if (lamp.needsPower && !g_level.quest.IsPowerBypassed())
        {
            continue;
        }
        PlaceWorldActor(scene, "lamp/" + std::to_string(i), lamp.position.x, lamp.position.y,
                        WorldBounds(lamp.position.x, lamp.position.y, 0, 0, 45, 100), 1,
                        lamp.position.x + lamp.position.y, [lamp]() {
                            auto center = WorldToScreen(lamp.position.x, lamp.position.y, 16);
                            DrawRectangle(center.x - 4, center.y - 8, 8, 12, {.9f, .7f, .34f});
                            DrawGlow(center, 32, {.95f, .63f, .23f});
                        });
    }
    PlaceClue(scene, "power-bypass", kPowerBypass, "E 비상 전력 우회", {1, .7f, .3f});
    PlaceClue(scene, "ruin-signal", kRuinSignal, "E 구조 신호 조사", {.3f, .8f, 1});
    PlaceClue(scene, "observer-glyph", kObserverGlyph, "E 관측자 문양", {.7f, .55f, 1}, true);
    PlaceClue(scene, "relay-coordinates", kCoordinateTerminal, "E 중계 좌표 확보", {.4f, 1, .8f});
    PlaceClue(scene, "mine-exit", g_level.entry, "E 헤일로 귀환", {.4f, .9f, .8f});
    for (size_t i = 0; i < g_level.enemies.size(); ++i)
    {
        auto enemy = g_level.enemies[i];
        if (enemy.health <= 0 || LightAt(enemy.position) < .025f)
        {
            continue;
        }
        PlaceWorldActor(scene, "enemy/" + std::to_string(i), enemy.position.x, enemy.position.y,
                        WorldBounds(enemy.position.x, enemy.position.y, 0, 0, 65), 1,
                        enemy.position.x + enemy.position.y, [enemy]() {
                            auto screen = WorldToScreen(enemy.position.x, enemy.position.y);
                            DrawSoftShadow(screen, 22, 8, .6f);
                            DrawCachedModel(models::Get().drone, screen, 1,
                                            {{.35f, .27f, .2f, 1}, {.75f, .35f, .2f, 1}, {1, .2f, .08f, 1}});
                            if (Distance(enemy.position, g_state.playerPosition) < 5)
                            {
                                DrawRectangle(screen.x - 18, screen.y - 39, 36, 3, {.15f, .05f, .04f});
                                DrawRectangle(screen.x - 18, screen.y - 39,
                                              36.f * enemy.health / kEnemyMaxHealth, 3, {.9f, .32f, .2f});
                            }
                        });
    }
    for (const auto &drop : g_level.drops)
    {
        if (LightAt(drop.position) < .025f)
        {
            continue;
        }
        PlaceWorldActor(scene, "drop/" + std::to_string(drop.actorId), drop.position.x, drop.position.y,
                        WorldBounds(drop.position.x, drop.position.y, 0, 0, 30), 1,
                        drop.position.x + drop.position.y, [drop]() {
                            auto center = WorldToScreen(drop.position.x, drop.position.y);
                            DrawCachedModel(drop.kind == ItemKind::RecoveryKit ? models::Get().recoveryKit
                                                                               : models::Get().salvage,
                                            center);
                        });
    }
    for (const auto &npc : g_level.survivors)
    {
        bool guard = g_level.rescueGroups[npc.group].combatants;
        PlacePerson(scene, "survivor/" + std::to_string(npc.id), npc.position,
                    guard ? ColorRGBA(.38f, .62f, .8f) : ColorRGBA(.9f, .68f, .28f),
                    guard ? models::CharacterType::Guard : models::CharacterType::Engineer,
                    npc.evacuating && npc.routeIndex < npc.route.size(),
                    npc.evacuating ? "헤일로로 철수 중"
                    : guard        ? "고립 전투원 / E 구조"
                                   : "광부 / E 구조");
    }
    PlacePerson(scene, "ria", g_level.riaPosition, {.95f, .55f, .25f}, models::CharacterType::Engineer,
                g_level.riaMovement > 0, "리아");
    auto player = g_state.playerPosition;
    PlaceWorldActor(scene, "player", player.x, player.y, WorldBounds(player.x, player.y, 0, 0, 85), 1,
                    player.x + player.y, [player]() {
                        DrawPerson(player, {.7f, .84f, .9f}, true);
                        float angle = GetAimAngle();
                        auto hand = WorldToScreen(player.x, player.y, 26);
                        Vector2 muzzle = {hand.x + std::cos(angle) * 24, hand.y + std::sin(angle) * 24};
                        DrawLine(hand, muzzle, {.38f, .43f, .48f}, 6);
                        DrawLine(hand, muzzle, {.7f, .74f, .76f}, 2);
                        if (g_level.muzzleFlash > 0)
                        {
                            DrawGlow(muzzle, 19, {1, .85f, .4f});
                        }
                    });
    DrawScene(scene);
    // Projectile segments stop at the collision point and are masked with the rest of the world.
    for (const auto &tracer : g_level.tracers)
    {
        auto start = WorldToScreen(tracer.start.x, tracer.start.y, 26);
        auto end = WorldToScreen(tracer.end.x, tracer.end.y, 26);
        float fade = tracer.remaining / .12f;
        DrawLine(start, end, {1, .65f, .2f, fade * .45f}, 7);
        DrawLine(start, end, {1, .96f, .7f, fade}, 2);
        DrawGlow(end, 10, {1, .68f, .3f, fade});
    }
}
} // namespace levelone
