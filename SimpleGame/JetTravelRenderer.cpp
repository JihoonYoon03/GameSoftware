#include "stdafx.h"
#include "JetTravel.h"
#include "TutorialDrawing.h"
#include "RenderQueue.h"
#include "Scene/WorldScene.h"
#include <cmath>
#include <cstdio>

namespace jettravel
{
namespace
{
void PlaceScreenActor(game::Scene &scene, const std::string &name, game::Bounds bounds, int layer,
                      std::function<void()> draw)
{
    scene.Place(name, "travel", bounds, layer, 0, [draw = std::move(draw)](const game::Actor &) { draw(); });
}
void DrawScreenScene(game::Scene &scene)
{
    scene.EndPlacement();
    for (const auto *actor : scene.GetGraph().CollectVisible(
             {0, 0, float(tutorial::kCanvasWidth), float(tutorial::kCanvasHeight)}))
    {
        actor->Draw();
    }
}
} // namespace
void DrawBackground()
{
    using namespace tutorial;
    auto &scene = GetWorldScene(WorldSceneKind::TravelBackground);
    scene.BeginPlacement();
    PlaceScreenActor(scene, "sky", {0, 0, float(kCanvasWidth), float(kCanvasHeight)}, 0, []() {
        DrawRectangle(0, 0, float(kCanvasWidth), float(kCanvasHeight), ColorRGBA(.018f, .035f, .067f));
        DrawRectangle(0, 280, float(kCanvasWidth), 390, ColorRGBA(.065f, .13f, .17f));
    });
    PlaceScreenActor(scene, "gate", {900, 45, 1190, 380}, 1, []() {
        float drift = g_travel.distance * .012f;
        Vector2 center = {1040 - drift, 205};
        DrawEllipse(center.x, center.y, 105, 130, ColorRGBA(.09f, .3f, .34f, .7f));
        DrawEllipse(center.x, center.y, 96, 121, ColorRGBA(.019f, .045f, .067f));
        DrawGlow(center, 170, ColorRGBA(.2f, .7f, .8f, .13f));
        DrawLabel(center.x - 55, center.y + 155, "관문 N-0 / 격리 회랑");
    });
    for (int layer = 0; layer < 3; ++layer)
    {
        float factor = .12f + layer * .26f;
        float spacing = 160 + layer * 40.f;
        float scroll = std::fmod(g_travel.distance * factor * 2.3f, spacing);
        for (int i = -1; i < 10; ++i)
        {
            float x = i * spacing - scroll;
            float base = 355 + layer * 52.f;
            float height = 58 + (i + 3) * 47 % 105;
            PlaceScreenActor(scene, "skyline/" + std::to_string(layer) + "/" + std::to_string(i),
                             {x - 20, base - height - 35, x + spacing, base}, 2 + layer,
                             [x, base, height, layer, i]() {
                                 float shade = .055f + layer * .018f;
                                 DrawRectangle(x, base - height, 88 + layer * 12.f, height,
                                               ColorRGBA(shade, shade + .055f, shade + .08f));
                                 if ((i + layer) % 2 == 0)
                                 {
                                     DrawEllipse(x + 45, base - height, 45, 22,
                                                 ColorRGBA(shade + .02f, shade + .08f, shade + .11f));
                                 }
                                 DrawLine({x + 12, base - height + 28}, {x + 70, base - height + 28},
                                          ColorRGBA(.16f, .37f, .42f), 2);
                             });
        }
    }
    PlaceScreenActor(scene, "fog", {0, 350, float(kCanvasWidth), 640}, 5, []() {
        DrawRectangle(0, 390, float(kCanvasWidth), 240, ColorRGBA(.12f, .27f, .3f, .12f));
    });
    DrawScreenScene(scene);
}
void DrawWorld()
{
    using namespace tutorial;
    auto &scene = GetWorldScene(WorldSceneKind::JetTravel);
    scene.BeginPlacement();
    PlaceScreenActor(scene, "corridor", {0, 335, float(kCanvasWidth), 695}, 0, []() {
        DrawRectangle(0, 355, float(kCanvasWidth), 290, ColorRGBA(.045f, .075f, .09f, .86f));
        DrawLine({0, 359}, {float(kCanvasWidth), 359}, ColorRGBA(.3f, .68f, .73f), 4);
        DrawLine({0, 641}, {float(kCanvasWidth), 641}, ColorRGBA(.25f, .5f, .55f), 5);
        float shift = std::fmod(g_travel.distance * 2.3f, 170.f);
        for (int i = -1; i < 9; ++i)
        {
            float x = i * 170.f - shift;
            DrawLine({x, 445}, {x + 85, 445}, ColorRGBA(.22f, .4f, .44f, .45f), 2);
            DrawLine({x, 545}, {x + 85, 545}, ColorRGBA(.22f, .4f, .44f, .45f), 2);
        }
    });
    for (int i = 0; i < kObstacleCount; ++i)
    {
        const auto obstacle = g_travel.obstacles[i];
        float x = GetObstacleScreenX(obstacle);
        float y = 485 + obstacle.lane * 115;
        PlaceScreenActor(scene, "debris/" + std::to_string(i), {x - 38, y - 44, x + 45, y + 32}, 1, [x, y]() {
            DrawSoftShadow({x + 8, y + 27}, 40, 13, .45f);
            DrawPolygon({{x - 35, y - 8},
                         {x - 15, y - 32},
                         {x + 22, y - 28},
                         {x + 36, y + 8},
                         {x + 8, y + 21},
                         {x - 26, y + 15}},
                        ColorRGBA(.32f, .27f, .29f));
            DrawLine({x - 18, y - 20}, {x + 22, y + 7}, ColorRGBA(1, .35f, .25f), 4);
            DrawGlow({x, y}, 45, ColorRGBA(1, .22f, .12f, .3f));
        });
    }
    Vector2 bike = GetBikeScreenPosition();
    PlaceScreenActor(scene, "bike-shadow", {bike.x - 100, bike.y + 12, bike.x + 115, bike.y + 65}, 1,
                     [bike]() { DrawSoftShadow({bike.x + 15, bike.y + 40}, 85, 20, .5f); });
    PlaceScreenActor(scene, "bike", {bike.x - 210, bike.y - 75, bike.x + 140, bike.y + 80}, 2, [bike]() {
        float bob = std::sin(tutorial::g_state.animationTimeSeconds * 9) * 2;
        DrawVehicle({bike.x, bike.y + bob}, 1.5f, models::VehicleType::JetBike, GetBikePalette(),
                    g_travel.speed / 108.f, 1);
    });
    PlaceScreenActor(scene, "rider", {bike.x - 45, bike.y - 95, bike.x + 45, bike.y}, 3, [bike]() {
        renderqueue::PushMatrix();
        renderqueue::Translate(bike.x - 7, bike.y - 10, 0);
        renderqueue::Scale(.68f, .68f, 1);
        DrawCharacterAppearance({0, 0}, ColorRGBA(.55f, .68f, .72f), models::CharacterType::Explorer, 0, 2);
        renderqueue::PopMatrix();
    });
    scene.GetGraph().Reparent("bike-shadow", *scene.GetGraph().Find("bike"));
    scene.GetGraph().Reparent("rider", *scene.GetGraph().Find("bike"));
    PlaceScreenActor(scene, "speed-trails", {0, 250, float(kCanvasWidth), 710}, 4, []() {
        for (int i = 0; i < 22; ++i)
        {
            float x = float(i * 173 % kCanvasWidth) -
                      std::fmod(g_travel.distance * (4 + i % 3), float(kCanvasWidth));
            if (x < -100)
            {
                x += kCanvasWidth + 100;
            }
            float y = 312 + float(i * 71 % 365);
            float length = 18 + g_travel.speed * .9f;
            DrawLine({x, y}, {x + length, y}, ColorRGBA(.6f, .8f, .86f, .11f), 1);
        }
        float scroll = std::fmod(g_travel.distance * 5.5f, 310.f);
        for (int i = -1; i < 6; ++i)
        {
            float x = i * 310.f - scroll;
            DrawRectangle(x, 660, 20, 140, ColorRGBA(.04f, .11f, .15f, .8f));
            DrawLine({x, 672}, {x + 20, 672}, ColorRGBA(.7f, .56f, .28f), 4);
        }
    });
    DrawScreenScene(scene);
}
void DrawHud()
{
    using namespace tutorial;
    DrawRectangle(22, 20, 445, 108, ColorRGBA(.015f, .05f, .08f, .94f));
    DrawLabel(40, 47, "제트 바이크 / 격리 구역 진입 회랑", ColorRGBA(.55f, .93f, .94f),
              GLUT_BITMAP_HELVETICA_18);
    char progress[128];
    std::snprintf(progress, sizeof(progress), "이동 %.0f%% / 속도 %.0f km/h / 잔해 회피 %d",
                  g_travel.distance / kRouteLength * 100, g_travel.speed * 3.6f, g_travel.avoidedObstacles);
    DrawLabel(40, 74, progress);
    DrawRectangle(40, 88, 400, 7, ColorRGBA(.11f, .2f, .23f));
    DrawRectangle(40, 88, 400 * g_travel.distance / kRouteLength, 7, ColorRGBA(.3f, .85f, .88f));
    DrawLabel(40, 118, "부스트 잔량", ColorRGBA(.7f, .78f, .8f));
    DrawRectangle(142, 107, 290 * g_travel.boostCharge, 7, ColorRGBA(.95f, .64f, .26f));
    DrawLabel(26, float(kCanvasHeight) - 22,
              "W·S 위아래 조종 / D 가속 / A 감속 / Space 부스트 / Esc 일시정지");
}
} // namespace jettravel
