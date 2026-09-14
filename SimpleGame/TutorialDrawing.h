#pragma once
#include "Renderer.h"
#include "ModelLibrary.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "TutorialState.h"
#include <initializer_list>

namespace tutorial
{
struct ColorRGBA
{
    float r, g, b, a;
    ColorRGBA(float red, float green, float blue, float alpha = 1);
};
void SetColor(ColorRGBA color);
void DrawPolygon(std::initializer_list<Vector2> vertices, ColorRGBA color);
void DrawLine(Vector2 start, Vector2 end, ColorRGBA color, float lineWidth = 1);
void DrawRectangle(float x, float y, float width, float height, ColorRGBA color);
void DrawLabel(float x, float y, const std::string &text, ColorRGBA color = ColorRGBA(.8f, .88f, .9f),
               void *font = GLUT_BITMAP_HELVETICA_12);
void DrawEllipse(float x, float y, float radiusX, float radiusY, ColorRGBA color);
Vector2 WorldToScreen(float x, float y, float height = 0);
void DrawTile(float x, float y, float width, float depth, ColorRGBA color);
void DrawBox(float x, float y, float width, float depth, float height, ColorRGBA color);
void DrawGlow(Vector2 position, float size, ColorRGBA color);
void DrawPerson(Vector2 worldPosition, ColorRGBA coat, bool isPlayer = false);

void DrawBackdrop();
void DrawCachedModel(const models::Model &model, Vector2 position, float scale = 1);
void DrawEffectRectangle(Vector2 topLeft, float width, float height, SurfaceEffect effect);
void DrawMaterialQuad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, SurfaceMaterial material, ColorRGBA color);
void DrawMaterialTile(float x, float y, SurfaceMaterial material, ColorRGBA color);
void DrawSoftShadow(Vector2 center, float radiusX, float radiusY, float opacity);
void DrawVehicle(Vector2 center, float scale);
void DrawInterior();
void DrawExterior();
void DrawHud();
} // namespace tutorial
