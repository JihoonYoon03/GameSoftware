#include "stdafx.h"

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
    glColor4f(color.r, color.g, color.b, color.a);
}

void DrawPolygon(std::initializer_list<Vector2> vertices, ColorRGBA color)
{
    SetColor(color);
    glBegin(GL_POLYGON);
    for (auto vertex : vertices)
    {
        glVertex2f(vertex.x, vertex.y);
    }
    glEnd();
}

void DrawLine(Vector2 start, Vector2 end, ColorRGBA color, float lineWidth)
{
    SetColor(color);
    glLineWidth(lineWidth);
    glBegin(GL_LINES);
    glVertex2f(start.x, start.y);
    glVertex2f(end.x, end.y);
    glEnd();
}

void DrawRectangle(float x, float y, float width, float height, ColorRGBA color)
{
    DrawPolygon({{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}, color);
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
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 48; i++)
    {
        float angle = i * 2 * kPi / 48;
        glVertex2f(x + std::cos(angle) * radiusX, y + std::sin(angle) * radiusY);
    }
    glEnd();
}

Vector2 WorldToScreen(float x, float y, float height)
{
    return {kCanvasWidth * .5f + (x - y - g_state.cameraPosition.x + g_state.cameraPosition.y) * 36,
            kCanvasHeight * .57f + (x + y - g_state.cameraPosition.x - g_state.cameraPosition.y) * 18 -
                height};
}

void DrawTile(float x, float y, float width, float depth, ColorRGBA color)
{
    DrawPolygon({WorldToScreen(x, y), WorldToScreen(x + width, y), WorldToScreen(x + width, y + depth),
                 WorldToScreen(x, y + depth)},
                color);
}

void DrawBox(float x, float y, float width, float depth, float height, ColorRGBA color)
{
    Vector2 back = WorldToScreen(x, y, height), right = WorldToScreen(x + width, y, height);
    Vector2 front = WorldToScreen(x + width, y + depth, height), left = WorldToScreen(x, y + depth, height);
    Vector2 baseFront = WorldToScreen(x + width, y + depth), baseLeft = WorldToScreen(x, y + depth),
            baseRight = WorldToScreen(x + width, y);
    DrawMaterialQuad(left, front, baseFront, baseLeft, SurfaceMaterial::Metal,
                     ColorRGBA(color.r * .58f, color.g * .58f, color.b * .65f, color.a));
    DrawMaterialQuad(right, front, baseFront, baseRight, SurfaceMaterial::Metal,
                     ColorRGBA(color.r * .78f, color.g * .78f, color.b * .85f, color.a));
    DrawMaterialQuad(back, right, front, left, SurfaceMaterial::Concrete, color);
    DrawLine(back, right, ColorRGBA(.35f, .65f, .7f, color.a));
    DrawLine(back, left, ColorRGBA(.25f, .45f, .5f, color.a));
}

void DrawGlow(Vector2 position, float size, ColorRGBA color)
{
    for (int i = 5; i > 0; i--)
    {
        DrawEllipse(position.x, position.y, size * i * .42f, size * i * .18f,
                    ColorRGBA(color.r, color.g, color.b, .02f * (6 - i)));
    }
    DrawEllipse(position.x, position.y, 3, 2, color);
}

void DrawPerson(Vector2 position, ColorRGBA coat, bool isPlayer)
{
    Vector2 screen = WorldToScreen(position.x, position.y);
    DrawSoftShadow({screen.x + 5, screen.y + 1}, 14, 6, .48f);
    if (isPlayer)
    {
        DrawEllipse(screen.x, screen.y, 16, 6, ColorRGBA(.25f, .95f, .9f, .17f));
    }
    int frame = isPlayer && g_state.movementAmount > 0 ? 1 + int(g_state.animationTimeSeconds * 10) % 7 : 0;
    int direction = isPlayer ? g_state.facingDirection : 0;
    SetColor(coat);
    GetRenderer().DrawCharacter(screen.x, screen.y, frame, direction);
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
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(.005f, .01f, .025f, opacity);
    glVertex2f(center.x, center.y);
    for (int i = 0; i <= 64; ++i)
    {
        float angle = i * 2 * kPi / 64;
        glColor4f(.005f, .01f, .025f, 0);
        glVertex2f(center.x + std::cos(angle) * radiusX, center.y + std::sin(angle) * radiusY);
    }
    glEnd();
}
void DrawVehicle(Vector2 center, float scale)
{
    glPushMatrix();
    glTranslatef(center.x, center.y, 0);
    glScalef(scale, scale, 1);
    // Thick hull, near side, raised cabin and paired thruster nacelles.
    DrawPolygon({{-46, -4}, {-14, -22}, {44, -2}, {21, 15}, {-28, 8}}, ColorRGBA(.32f, .45f, .53f));
    DrawPolygon({{-28, 8}, {21, 15}, {44, -2}, {44, 12}, {21, 29}, {-28, 21}}, ColorRGBA(.1f, .19f, .27f));
    DrawPolygon({{-46, -4}, {-28, 8}, {-28, 21}, {-46, 9}}, ColorRGBA(.17f, .28f, .35f));
    DrawPolygon({{-20, -10}, {-5, -28}, {21, -20}, {28, -5}, {11, 5}}, ColorRGBA(.35f, .48f, .55f));
    DrawMaterialQuad({-17, -10}, {-4, -25}, {19, -18}, {24, -6}, SurfaceMaterial::Glass,
                     ColorRGBA(.13f, .53f, .66f));
    DrawLine({-4, -25}, {-1, -7}, ColorRGBA(.5f, .66f, .7f), 2);
    DrawLine({-26, 11}, {17, 18}, ColorRGBA(.5f, .62f, .65f), 2);
    for (int side = -1; side <= 1; side += 2)
    {
        float x = side == 1 ? 25.f : -31.f, y = side == 1 ? 22.f : 10.f;
        DrawEllipse(x, y, 11, 7, ColorRGBA(.045f, .09f, .13f));
        DrawEllipse(x, y, 7, 4, ColorRGBA(.15f, .5f, .6f));
        DrawGlow({x, y + 3}, 10, ColorRGBA(.3f, .85f, 1));
    }
    DrawLine({28, 8}, {36, 2}, ColorRGBA(1, .89f, .58f), 3);
    DrawLine({-43, 6}, {-35, 11}, ColorRGBA(.9f, .22f, .18f), 3);
    glPopMatrix();
}

} // namespace tutorial
