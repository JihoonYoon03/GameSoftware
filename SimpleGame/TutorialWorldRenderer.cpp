#include "stdafx.h"
#include "RenderQueue.h"
#include "DrawCallCounter.h"

#include "Tutorial.h"
#include "TutorialDrawing.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{
void DrawBuilding(Building building)
{
    Vector2 buildingScreen =
                WorldToScreen(building.x + building.width * .5f, building.y + building.depth * .5f),
            playerScreen = WorldToScreen(g_state.playerPosition.x, g_state.playerPosition.y);
    float opacity =
        (building.x + building.y + building.width + building.depth >
             g_state.playerPosition.x + g_state.playerPosition.y &&
         std::fabs(buildingScreen.x - playerScreen.x) < (building.width + building.depth) * 22 &&
         playerScreen.y > buildingScreen.y - building.height - 25 && playerScreen.y < buildingScreen.y + 35)
            ? .28f
            : 1;
    DrawBox(building.x, building.y, building.width, building.depth, building.height,
            ColorRGBA(.3f, .37f, .44f, opacity));
    DrawBox(building.x + .2f, building.y + .2f, building.width - .4f, building.depth - .4f,
            building.height + 8, ColorRGBA(.34f, .42f, .47f, opacity));
    for (int floor = 18; floor < building.height - 10; floor += 22)
    {
        for (int column = 0; column < 3; column++)
        {
            Vector2 windowPosition = WorldToScreen(
                building.x + building.width, building.y + .25f + column * building.depth / 3, (float)floor);
            DrawMaterialQuad(windowPosition, {windowPosition.x - 10, windowPosition.y + 5},
                             {windowPosition.x - 10, windowPosition.y + 13},
                             {windowPosition.x, windowPosition.y + 8}, SurfaceMaterial::Glass,
                             ColorRGBA(.18f, .42f, .52f, opacity));
            DrawLine(windowPosition, {windowPosition.x - 9, windowPosition.y + 4.5f},
                     g_state.isPowerRestored ? ColorRGBA(.95f, .72f, .37f, opacity)
                                             : ColorRGBA(.2f, .65f, .73f, opacity),
                     3);
            windowPosition = WorldToScreen(building.x + .25f + column * building.width / 3,
                                           building.y + building.depth, (float)floor);
            DrawLine(windowPosition, {windowPosition.x + 9, windowPosition.y + 4.5f},
                     ColorRGBA(.35f, .6f, .72f, opacity * .7f), 3);
        }
    }
    Vector2 roof = WorldToScreen(building.x + .5f, building.y + .5f, building.height + 8);
    DrawLine(roof, {roof.x, roof.y - 22}, ColorRGBA(.35f, .55f, .64f, opacity));
    DrawGlow({roof.x, roof.y - 22}, 6, ColorRGBA(.9f, .37f, .3f, opacity));
}

void DrawBackdrop()
{
    DrawRectangle(0, 0, (float)kCanvasWidth, (float)kCanvasHeight, ColorRGBA(.025f, .045f, .085f));
    for (int i = 0; i < 65; i++)
    {
        DrawRectangle((float)((i * 193 + 31) % 1280) * kCanvasWidth / 1280, (float)((i * 71 + 11) % 230), 1,
                      1, ColorRGBA(.4f, .64f, .72f, .35f));
    }
    float gateX = kCanvasWidth * .76f, gateY = kCanvasHeight * .27f;
    for (int j = 8; j > 0; j--)
    {
        SetColor(ColorRGBA(.18f, .62f, .7f, j == 1 ? .85f : .025f));
        renderqueue::LineWidth((float)j * 2);
        renderqueue::Begin(GL_LINE_LOOP);
        for (const auto &point : models::Get().gateRing)
        {
            renderqueue::Vertex(gateX + point.x * 115, gateY + point.y * 145);
        }
        renderqueue::End();
    }
    for (int i = 0; i < 12; i++)
    {
        float angle = i * 2 * kPi / 12 + g_state.animationTimeSeconds * .015f;
        DrawRectangle(gateX + std::cos(angle) * 115 - 5, gateY + std::sin(angle) * 145 - 7, 10, 14,
                      ColorRGBA(.5f, .9f, .92f));
    }
    if (g_state.isComplete)
    {
        DrawLine({gateX - 65, gateY}, {gateX + 65, gateY}, ColorRGBA(.85f, .38f, .53f, .6f), 2);
        DrawEllipse(gateX, gateY, 8, 38, ColorRGBA(.9f, .65f, .8f, .6f));
    }
    for (int i = 0; i < 23; i++)
    {
        float x = i * kCanvasWidth / 22.f, towerHeight = 40.f + (i * 53 % 100);
        DrawRectangle(x, kCanvasHeight * .39f - towerHeight, kCanvasWidth / 28.f, towerHeight,
                      ColorRGBA(.045f, .085f, .13f));
    }
    DrawLabel(gateX - 63, gateY + 163, "N - 0 / 관문망", ColorRGBA(.3f, .6f, .69f));
    float railY = kCanvasHeight * .4f;
    DrawLine({0, railY}, {(float)kCanvasWidth, railY - 55}, ColorRGBA(.22f, .42f, .51f), 3);
    for (int i = 0; i < 4; i++)
    {
        float x = std::fmod(g_state.animationTimeSeconds * 45 + i * 360.f, kCanvasWidth + 180.f) - 90,
              y = railY - x * 55 / kCanvasWidth - 12;
        DrawVehicle({x, y}, .65f);
    }
}

void DrawInteractionMarker(Vector2 worldPosition, const std::string &label, ColorRGBA color)
{
    Vector2 screenPosition =
        WorldToScreen(worldPosition.x, worldPosition.y, 49 + std::sin(g_state.animationTimeSeconds * 2) * 3);
    DrawPolygon({{screenPosition.x, screenPosition.y + 7},
                 {screenPosition.x - 5, screenPosition.y},
                 {screenPosition.x, screenPosition.y - 7},
                 {screenPosition.x + 5, screenPosition.y}},
                color);
    DrawLabel(screenPosition.x + 9, screenPosition.y + 4, label, color);
}

} // namespace tutorial
