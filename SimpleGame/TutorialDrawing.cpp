#include "stdafx.h"
#include "RenderQueue.h"
#include "RenderCache.h"
#include "DrawCallCounter.h"

#include "TutorialDrawing.h"
#include "Tutorial.h"
#include "TutorialGraphics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{
#pragma comment(lib, "opengl32.lib")

ColorRGBA::ColorRGBA(float red, float green, float blue, float alpha) : r(red), g(green), b(blue), a(alpha)
{
}

void SetColor(ColorRGBA color)
{
    renderqueue::Color(color.r, color.g, color.b, color.a);
}

void DrawPolygon(std::initializer_list<Vector2> vertices, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_POLYGON);
    for (auto vertex : vertices)
    {
        renderqueue::Vertex(vertex.x, vertex.y);
    }
    renderqueue::End();
}

void DrawLine(Vector2 start, Vector2 end, ColorRGBA color, float lineWidth)
{
    SetColor(color);
    renderqueue::LineWidth(lineWidth);
    renderqueue::Begin(GL_LINES);
    renderqueue::Vertex(start.x, start.y);
    renderqueue::Vertex(end.x, end.y);
    renderqueue::End();
}

void DrawRectangle(float x, float y, float width, float height, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_QUADS);
    for (int i = 0; i < 4; ++i)
    {
        const auto &point = models::Get().cube[i];
        renderqueue::Vertex(x + point.x * width, y + point.y * height);
    }
    renderqueue::End();
}

void DrawLabel(float x, float y, const std::string &text, ColorRGBA color, void *font)
{
    SetColor(color);
    int size = font == GLUT_BITMAP_HELVETICA_18 ? 19 : 15;
    GetRenderer().DrawUtf8Text(x, y, text, size);
}

void DrawEllipse(float x, float y, float radiusX, float radiusY, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_TRIANGLE_FAN);
    renderqueue::Vertex(x, y);
    for (const auto &point : models::Get().circle)
    {
        renderqueue::Vertex(x + point.x * radiusX, y + point.y * radiusY);
    }
    renderqueue::End();
}

Vector2 WorldToScreen(float x, float y, float height)
{
    return {kCanvasWidth * .5f + (x - y - g_state.cameraPosition.x + g_state.cameraPosition.y) * 36,
            kCanvasHeight * .57f + (x + y - g_state.cameraPosition.x - g_state.cameraPosition.y) * 18 -
                height};
}

void DrawTile(float x, float y, float width, float depth, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_QUADS);
    for (int i = 0; i < 4; ++i)
    {
        const auto &point = models::Get().cube[i];
        Vector2 screen = WorldToScreen(x + point.x * width, y + point.y * depth);
        renderqueue::Vertex(screen.x, screen.y);
    }
    renderqueue::End();
}

void DrawBox(float x, float y, float width, float depth, float height, ColorRGBA color)
{
    // Buildings, furniture and level obstacles instance the same cached cube.
    Vector2 vertices[8];
    const auto &cube = models::Get().cube;
    for (size_t i = 0; i < cube.size(); ++i)
    {
        vertices[i] = WorldToScreen(x + cube[i].x * width, y + cube[i].y * depth, cube[i].z * height);
    }
    DrawMaterialQuad(vertices[7], vertices[6], vertices[2], vertices[3], SurfaceMaterial::Metal,
                     ColorRGBA(color.r * .58f, color.g * .58f, color.b * .65f, color.a));
    DrawMaterialQuad(vertices[5], vertices[6], vertices[2], vertices[1], SurfaceMaterial::Metal,
                     ColorRGBA(color.r * .78f, color.g * .78f, color.b * .85f, color.a));
    DrawMaterialQuad(vertices[4], vertices[5], vertices[6], vertices[7], SurfaceMaterial::Concrete, color);
    DrawLine(vertices[4], vertices[5], ColorRGBA(.35f, .65f, .7f, color.a));
    DrawLine(vertices[4], vertices[7], ColorRGBA(.25f, .45f, .5f, color.a));
}

void DrawGlow(Vector2 position, float size, ColorRGBA color)
{
    SetColor(color);
    GetRenderer().DrawGlow(position.x, position.y, size);
}

void DrawMaterialQuad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, SurfaceMaterial material, ColorRGBA color)
{
    const RenderPoint vertices[] = {{a.x, a.y}, {b.x, b.y}, {c.x, c.y}, {d.x, d.y}};
    SetColor(color);
    GetRenderer().DrawMaterial(vertices, material);
}
void DrawMaterialTile(float x, float y, SurfaceMaterial material, ColorRGBA color)
{
    DrawMaterialQuad(WorldToScreen(x, y), WorldToScreen(x + .98f, y), WorldToScreen(x + .98f, y + .98f),
                     WorldToScreen(x, y + .98f), material, color);
}
void DrawSoftShadow(Vector2 center, float radiusX, float radiusY, float opacity)
{
    renderqueue::Begin(GL_TRIANGLE_FAN);
    renderqueue::Color(.005f, .01f, .025f, opacity);
    renderqueue::Vertex(center.x, center.y);
    renderqueue::Color(.005f, .01f, .025f, 0);
    for (const auto &point : models::Get().softCircle)
    {
        renderqueue::Vertex(center.x + point.x * radiusX, center.y + point.y * radiusY);
    }
    renderqueue::End();
}

void DrawEffectRectangle(Vector2 topLeft, float width, float height, SurfaceEffect effect)
{
    const RenderPoint points[] = {{topLeft.x, topLeft.y},
                                  {topLeft.x + width, topLeft.y},
                                  {topLeft.x + width, topLeft.y + height},
                                  {topLeft.x, topLeft.y + height}};
    GetRenderer().DrawEffect(points, effect, g_state.animationTimeSeconds);
}

} // namespace tutorial
