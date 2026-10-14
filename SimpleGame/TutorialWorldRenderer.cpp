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
        float x = std::fmod(g_state.animationTimeSeconds * 115 + i * 360.f, kCanvasWidth + 300.f) - 150,
              y = railY - x * 55 / kCanvasWidth - 12;
        models::Palette palette;
        palette.body = {.38f + i * .09f, .54f, .7f - i * .08f, 1};
        palette.emission =
            i % 2 ? std::array<float, 4>{1, .55f, .22f, 1} : std::array<float, 4>{.25f, .8f, 1, 1};
        DrawVehicle({x, y}, .65f, models::VehicleType(i % 4), palette, .8f);
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
