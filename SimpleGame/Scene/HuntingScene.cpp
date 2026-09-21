#include "stdafx.h"
#include "WorldScene.h"
#include "../LevelOne.h"
#include "../TutorialDrawing.h"
#include "../TutorialGraphics.h"

namespace levelone
{
using namespace tutorial;
void DrawWorld()
{
    auto &scene = GetWorldScene(WorldSceneKind::Hunting);
    scene.BeginPlacement();
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
    auto placeObject = [&scene](RenderObject object) {
        Vector2 position = {float(object.index % kMapSize), float(object.index / kMapSize)};
        if (object.kind == ObjectKind::Enemy)
        {
            position = g_level.enemies[object.index].position;
        }
        else if (object.kind == ObjectKind::Drop)
        {
            position = g_level.drops[object.index].position;
        }
        else if (object.kind == ObjectKind::Player)
        {
            position = g_state.playerPosition;
        }
        std::string name;
        switch (object.kind)
        {
        case ObjectKind::Wall:
            name = "wall/" + std::to_string(object.index);
            break;
        case ObjectKind::Enemy:
            name = "enemy/" + std::to_string(object.index);
            break;
        case ObjectKind::Drop:
            name = "drop/" + std::to_string(g_level.drops[object.index].actorId);
            break;
        case ObjectKind::Player:
            name = "player";
            break;
        case ObjectKind::Flame:
            name = "flame/" + std::to_string(object.index);
            break;
        }
        PlaceWorldActor(
            scene, name, position.x, position.y, WorldBounds(position.x, position.y, 1, 1, 80, 100), 1,
            object.depth, [object]() {
                if (object.kind == ObjectKind::Wall)
                {
                    int x = object.index % kMapSize, y = object.index / kMapSize;
                    DrawBox(float(x), float(y), .98f, .98f, 18, ColorRGBA(.18f, .23f, .28f));
                }
                else if (object.kind == ObjectKind::Enemy)
                {
                    const Enemy &enemy = g_level.enemies[object.index];
                    Vector2 screen = WorldToScreen(enemy.position.x, enemy.position.y);
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
                    Vector2 center =
                        WorldToScreen(object.index % kMapSize + .5f, object.index / kMapSize + .5f);
                    DrawEffectRectangle({center.x - 12, center.y - 43}, 24, 44, SurfaceEffect::Flame);
                }
            });
    };
    for (int y = 0; y < kMapSize; ++y)
    {
        for (int x = 0; x < kMapSize; ++x)
        {
            int cell = y * kMapSize + x;
            if (g_level.tiles[cell] == Tile::Wall)
            {
                placeObject({float(x + y + 2), ObjectKind::Wall, cell});
                continue;
            }
            PlaceWorldActor(
                scene, "tile/" + std::to_string(cell), float(x), float(y),
                WorldBounds(float(x), float(y), 1, 1, 0), 0, float(cell), [x, y, cell]() {
                    DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal, ColorRGBA(.23f, .3f, .33f));
                    if (g_level.tiles[cell] == Tile::Water)
                    {
                        Vector2 a = WorldToScreen(float(x), float(y)), b = WorldToScreen(x + 1.f, float(y));
                        Vector2 c = WorldToScreen(x + 1.f, y + 1.f), d = WorldToScreen(float(x), y + 1.f);
                        const RenderPoint vertices[] = {{a.x, a.y}, {b.x, b.y}, {c.x, c.y}, {d.x, d.y}};
                        GetRenderer().DrawEffect(vertices, SurfaceEffect::Water,
                                                 g_state.animationTimeSeconds);
                    }
                });
            if (cell % 71 == 0 && Distance({x + .5f, y + .5f}, g_level.entry) > 3)
            {
                placeObject({x + y + 1.f, ObjectKind::Flame, cell});
            }
        }
    }
    for (size_t i = 0; i < g_level.drops.size(); ++i)
    {
        placeObject({g_level.drops[i].position.x + g_level.drops[i].position.y, ObjectKind::Drop, int(i)});
    }
    for (size_t i = 0; i < g_level.enemies.size(); ++i)
    {
        if (g_level.enemies[i].health > 0)
        {
            placeObject(
                {g_level.enemies[i].position.x + g_level.enemies[i].position.y, ObjectKind::Enemy, int(i)});
        }
    }
    placeObject({g_state.playerPosition.x + g_state.playerPosition.y, ObjectKind::Player, 0});
    PlaceWorldActor(scene, "exit", g_level.entry.x, g_level.entry.y,
                    WorldBounds(g_level.entry.x, g_level.entry.y, 0, 0, 50, 110), 2, 0, []() {
                        Vector2 entry = WorldToScreen(g_level.entry.x, g_level.entry.y);
                        DrawEffectRectangle({entry.x - 24, entry.y - 12}, 48, 24, SurfaceEffect::Pulse);
                        DrawLabel(entry.x - 35, entry.y - 30, "E 도시 귀환", ColorRGBA(.75f, .9f, 1));
                    });
    DrawScene(scene);
}

} // namespace levelone
