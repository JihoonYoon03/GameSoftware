#pragma once
#include "Game.h"
#include "Renderer.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include <functional>
#include <initializer_list>

namespace tutorial
{
struct ColorRGBA
{
    float r, g, b, a;
    ColorRGBA(float red, float green, float blue, float alpha = 1);
};

// ============================================================================
// Drawing — primitives, text, models, environment
// ============================================================================
class Drawing
{
  public:
    static void SetColor(ColorRGBA color);
    static void DrawPolygon(std::initializer_list<Vector2> vertices, ColorRGBA color);
    static void DrawLine(Vector2 start, Vector2 end, ColorRGBA color, float lineWidth = 1);
    static void DrawRectangle(float x, float y, float width, float height, ColorRGBA color);
    static void DrawLabel(float x, float y, const std::string &text,
                          ColorRGBA color = ColorRGBA(.8f, .88f, .9f), void *font = GLUT_BITMAP_HELVETICA_12);
    static void DrawEllipse(float x, float y, float radiusX, float radiusY, ColorRGBA color);
    static Vector2 WorldToScreen(float x, float y, float height = 0);
    static void DrawTile(float x, float y, float width, float depth, ColorRGBA color);
    static void DrawBox(float x, float y, float width, float depth, float height, ColorRGBA color);
    static void DrawGlow(Vector2 position, float size, ColorRGBA color);
    static void DrawPerson(Vector2 worldPosition, ColorRGBA coat, bool isPlayer = false,
                           models::CharacterType type = models::CharacterType::Explorer, bool walking = false,
                           float facingYaw = .78539816f, models::Gender gender = models::Gender::Male,
                           models::HairStyle hair = models::HairStyle::SidePart, bool npcArmed = false,
                           float npcFlash = 0, bool downed = false);
    static void DrawHair(Vector2 screen, models::Point scale, const models::Transform3D &rotation,
                         float depth, const models::CharacterPose &pose, models::CharacterType type,
                         models::Gender gender, models::HairStyle hair);
    static void DrawCharacterAppearance(Vector2 screenPosition, ColorRGBA coat, models::CharacterType type,
                                        int frame = 0, int direction = 0);

    static void DrawBackdrop();
    static void DrawBuilding(Building building);
    static void DrawInteractionMarker(Vector2 worldPosition, const std::string &label, ColorRGBA color);
    static void DrawEffectRectangle(Vector2 topLeft, float width, float height, SurfaceEffect effect);
    static void DrawMaterialQuad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, SurfaceMaterial material,
                                 ColorRGBA color);
    static void DrawMaterialTile(float x, float y, SurfaceMaterial material, ColorRGBA color);
    static void DrawGroundStrip(Vector2 start, Vector2 end, float width, ColorRGBA color,
                                float elevation = .025f,
                                SurfaceMaterial material = SurfaceMaterial::Concrete);
    static void DrawHologramStrip(Vector2 start, Vector2 end, float width, ColorRGBA color,
                                  float elevation = .16f);
    static void DrawSoftShadow(Vector2 center, float radiusX, float radiusY, float opacity);
    static void DrawVehicle(Vector2 center, float scale,
                            models::VehicleType type = models::VehicleType::Coupe,
                            const models::Palette &palette = {}, float throttle = .5f, float direction = 1);
    static void DrawWorldVehicle(Vector2 position, float altitude, float scale, models::VehicleType type,
                                 const models::Palette &palette, float throttle, float yaw);
    static void DrawWorldBike(Vector2 position, float yaw, float throttle, bool mounted);
    static void DrawWorldAircraft(Vector2 position, float altitude, models::AircraftType type, bool alternate,
                                  float yaw, float bank);
    static void DrawProp(const models::Model &model, Vector2 position, models::Point scale = {1, 1, 1},
                         const models::Palette &palette = {}, float yaw = 0, float elevation = 0);
    static void DrawModel3D(const models::Model &model, Vector2 screen, models::Point scale,
                            const models::Transform3D &transform, float depth, const models::Palette &palette,
                            const models::CharacterPose *pose = nullptr);
};
} // namespace tutorial
