#include "stdafx.h"
#include "WorldScene.h"
#include "../TutorialDrawing.h"
#include "../RenderCache.h"
#include "../Profiler.h"
#include <cmath>

namespace tutorial
{
void DrawObservatoryInterior()
{
    auto &scene = GetWorldScene(WorldSceneKind::Observatory);
    {
        profiling::Scope timer(profiling::Timer::ScenePlacement);
        scene.BeginPlacement();
        PlaceWorldActor(scene, "deck", -4, -4, WorldBounds(-4, -4, 8, 8, 0, 60), 0, 0, []() {
            rendercache::DrawWorld("terrain/observatory/deck", []() {
                Vector2 center = WorldToScreen(0, 0);
                DrawEllipse(center.x, center.y, 305, 152, ColorRGBA(.18f, .3f, .34f));
                for (int x = -4; x < 4; ++x)
                {
                    for (int y = -4; y < 4; ++y)
                    {
                        DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal,
                                         ColorRGBA(.19f, .3f, .34f));
                    }
                }
                DrawEllipse(center.x, center.y, 90, 45, ColorRGBA(.14f, .27f, .32f));
                DrawEllipse(center.x, center.y, 77, 37, ColorRGBA(.24f, .42f, .47f));
            });
        });
        PlaceWorldActor(scene, "deck-label", 0, 0, WorldBounds(0, 0, 0, 0, 0, 150), 1, 0, []() {
            Vector2 center = WorldToScreen(0, 0);
            DrawLabel(center.x - 74, center.y + 8, "관측 데크 / 180 m", ColorRGBA(.5f, .8f, .82f));
        });
        PlaceWorldActor(scene, "panorama-railing", -4, -4, WorldBounds(-4, -4, 8, 8, 78, 70), 1, 0, []() {
            for (int i = 0; i < 9; ++i)
            {
                float x = -4 + i;
                Vector2 first = WorldToScreen(x, -4, 62);
                Vector2 second = WorldToScreen(x + 1, -4, 62);
                if (i < 8)
                {
                    DrawMaterialQuad(first, second, {second.x, second.y + 45}, {first.x, first.y + 45},
                                     SurfaceMaterial::Glass, ColorRGBA(.21f, .59f, .68f, .38f));
                    DrawLine(first, second, ColorRGBA(.42f, .77f, .82f), 3);
                }
                DrawLine(WorldToScreen(x, -4), first, ColorRGBA(.32f, .48f, .55f), 4);
                Vector2 left = WorldToScreen(-4, x, 62);
                Vector2 next = WorldToScreen(-4, x + 1, 62);
                if (i < 8)
                {
                    DrawLine(left, next, ColorRGBA(.42f, .77f, .82f), 3);
                }
                DrawLine(WorldToScreen(-4, x), left, ColorRGBA(.32f, .48f, .55f), 4);
            }
        });
        PlaceWorldActor(
            scene, "signal-console", kObservatoryConsole.x, kObservatoryConsole.y,
            WorldBounds(kObservatoryConsole.x, kObservatoryConsole.y, .8f, .65f, 95, 75), 2, 0, []() {
                DrawBox(1.9f, -2.4f, .9f, .6f, 36, ColorRGBA(.26f, .4f, .47f));
                Vector2 screen = WorldToScreen(2.2f, -2.1f, 55);
                DrawRectangle(screen.x - 23, screen.y - 19, 46, 28, ColorRGBA(.09f, .37f, .44f));
                for (int i = 0; i < 8; ++i)
                {
                    float pulse = std::sin(g_state.animationTimeSeconds * 4 + i * .7f) * 6;
                    DrawLine({screen.x - 20 + i * 5.f, screen.y - 6},
                             {screen.x - 20 + i * 5.f, screen.y - 6 + pulse}, ColorRGBA(.4f, .95f, .9f), 2);
                }
                DrawGlow(screen, 42, ColorRGBA(.25f, .9f, .95f, .35f));
            });
        PlaceWorldActor(scene, "telescope", 2.6f, -3.2f, WorldBounds(2.6f, -3.2f, 0, 0, 140, 90), 2, -2,
                        []() {
                            Vector2 foot = WorldToScreen(2.6f, -3.2f);
                            DrawLine(foot, {foot.x, foot.y - 58}, ColorRGBA(.45f, .56f, .59f), 9);
                            DrawLine({foot.x - 18, foot.y - 51}, {foot.x + 46, foot.y - 93},
                                     ColorRGBA(.57f, .68f, .71f), 22);
                            DrawEllipse(foot.x + 46, foot.y - 93, 11, 18, ColorRGBA(.1f, .38f, .47f));
                        });
        PlaceWorldActor(scene, "stairs", kObservatoryStairs.x, kObservatoryStairs.y,
                        WorldBounds(kObservatoryStairs.x, kObservatoryStairs.y, 1, 1, 0, 65), 2, 0, []() {
                            DrawTile(-3.3f, 2.3f, 1.2f, 1.2f, ColorRGBA(.13f, .34f, .4f));
                            for (int i = 0; i < 5; ++i)
                            {
                                float x = -3.3f + i * .22f;
                                DrawLine(WorldToScreen(x, 2.3f), WorldToScreen(x, 3.4f),
                                         ColorRGBA(.45f, .7f, .76f), 3);
                            }
                        });
        PlaceWorldActor(scene, "lia", kObservatoryLiaPosition.x, kObservatoryLiaPosition.y,
                        WorldBounds(kObservatoryLiaPosition.x, kObservatoryLiaPosition.y, 0, 0, 95), 3, -2.5f,
                        []() {
                            DrawPerson(kObservatoryLiaPosition, ColorRGBA(.89f, .52f, .28f), false,
                                       models::CharacterType::Engineer);
                        });
        PlaceWorldActor(scene, "player", g_state.playerPosition.x, g_state.playerPosition.y,
                        WorldBounds(g_state.playerPosition.x, g_state.playerPosition.y, 0, 0, 95), 3,
                        g_state.playerPosition.x + g_state.playerPosition.y, []() {
                            DrawPerson(g_state.playerPosition, ColorRGBA(.68f, .8f, .83f), true,
                                       models::CharacterType::Explorer);
                        });
        PlaceWorldActor(
            scene, "signal-marker", kObservatoryConsole.x, kObservatoryConsole.y,
            WorldBounds(kObservatoryConsole.x, kObservatoryConsole.y, 0, 0, 100, 190), 4, 0, []() {
                DrawInteractionMarker(kObservatoryConsole,
                                      g_state.gateSignalHeard ? "관문 응답 기록" : "신호 측정 [E]",
                                      ColorRGBA(.4f, .93f, .96f));
            });
        PlaceWorldActor(scene, "exit-marker", kObservatoryStairs.x, kObservatoryStairs.y,
                        WorldBounds(kObservatoryStairs.x, kObservatoryStairs.y, 0, 0, 80, 160), 4, 0, []() {
                            DrawInteractionMarker(kObservatoryStairs, "도시로 내려가기 [E]",
                                                  ColorRGBA(1, .75f, .4f));
                        });
        scene.GetGraph().Reparent("signal-marker", *scene.GetGraph().Find("signal-console"));
        scene.GetGraph().Reparent("exit-marker", *scene.GetGraph().Find("stairs"));
    }
    DrawScene(scene);
}
} // namespace tutorial
