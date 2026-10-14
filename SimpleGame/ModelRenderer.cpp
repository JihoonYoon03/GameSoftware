#include "stdafx.h"
#include "TutorialDrawing.h"
#include "TutorialGraphics.h"
#include "RenderCache.h"
#include "RenderQueue.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace tutorial
{
namespace
{
std::string PaletteKey(const models::Palette &palette)
{
    std::string result;
    for (const auto &color : {palette.body, palette.accent, palette.emission})
    {
        for (float value : color)
        {
            result += "/" + std::to_string(int(value * 255 + .5f));
        }
    }
    return result;
}
void DrawParts(const models::Model &model, const models::Palette &palette,
               const std::function<Vector2(models::Point)> &project)
{
    for (size_t i = 0; i < model.count; ++i)
    {
        const auto &part = model.parts[i];
        auto color = part.color;
        const auto *tint = part.palette == models::PaletteSlot::Body       ? &palette.body
                           : part.palette == models::PaletteSlot::Accent   ? &palette.accent
                           : part.palette == models::PaletteSlot::Emission ? &palette.emission
                                                                           : nullptr;
        for (size_t j = 0; j < 4; ++j)
        {
            color[j] *= tint ? (*tint)[j] : (j == 3 ? palette.body[3] : 1);
        }
        ColorRGBA rgba(color[0], color[1], color[2], color[3]);
        auto a = project(part.points[0]);
        if (part.kind == models::PartKind::Ellipse)
        {
            DrawEllipse(a.x, a.y, part.points[1].x, part.points[1].y, rgba);
        }
        else if (part.kind == models::PartKind::Line)
        {
            DrawLine(a, project(part.points[1]), rgba, part.width);
        }
        else if (part.kind == models::PartKind::Glass)
        {
            DrawMaterialQuad(a, project(part.points[1]), project(part.points[2]), project(part.points[3]),
                             SurfaceMaterial::Glass, rgba);
        }
        else
        {
            SetColor(rgba);
            renderqueue::Begin(GL_POLYGON);
            for (size_t j = 0; j < part.count; ++j)
            {
                auto point = project(part.points[j]);
                renderqueue::Vertex(point.x, point.y);
            }
            renderqueue::End();
        }
    }
}
} // namespace
void DrawCachedModel(const models::Model &model, Vector2 position, float scale,
                     const models::Palette &palette)
{
    renderqueue::PushMatrix();
    renderqueue::Translate(position.x, position.y, 0);
    renderqueue::Scale(scale, scale, 1);
    std::string key =
        "model/" + std::to_string(reinterpret_cast<std::uintptr_t>(&model)) + PaletteKey(palette);
    rendercache::Draw(
        key, [&]() { DrawParts(model, palette, [](models::Point p) { return Vector2{p.x, p.y}; }); });
    renderqueue::PopMatrix();
}
void DrawPerson(Vector2 position, ColorRGBA coat, bool isPlayer, models::CharacterType type, bool walking)
{
    Vector2 screen = WorldToScreen(position.x, position.y);
    DrawSoftShadow({screen.x + 5, screen.y + 1}, 14, 6, .48f);
    if (isPlayer)
    {
        DrawEllipse(screen.x, screen.y, 16, 6, ColorRGBA(.25f, .95f, .9f, .17f));
    }
    int frame = ((isPlayer && g_state.movementAmount > 0) || walking)
                    ? 1 + int(g_state.animationTimeSeconds * 10) % 7
                    : 0;
    DrawCharacterAppearance(screen, coat, type, frame, isPlayer ? g_state.facingDirection : 0);
}
void DrawCharacterAppearance(Vector2 screen, ColorRGBA coat, models::CharacterType type, int frame,
                             int direction)
{
    SetColor(coat);
    GetRenderer().DrawCharacter(screen.x, screen.y, frame, direction);
    models::Palette palette;
    palette.body = {coat.r, coat.g, coat.b, coat.a};
    palette.accent = {.65f, .8f, .85f, coat.a};
    int step = frame == 0 ? 0 : int(std::sin(frame * 6.2831853f / 8) * 4);
    float bob = (std::abs(step) / 2) * 53.f / 64;
    DrawCachedModel(models::Get().characters.at(size_t(type)), {screen.x, screen.y - bob}, 1, palette);
}
void DrawVehicle(Vector2 center, float scale, models::VehicleType type, const models::Palette &palette,
                 float throttle, float direction)
{
    const auto &vehicle = models::Get().vehicles.at(size_t(type));
    float length = 18 + Clamp(throttle, 0, 1) * 75;
    renderqueue::PushMatrix();
    renderqueue::Translate(center.x, center.y, 0);
    renderqueue::Scale(scale * (direction < 0 ? -1.f : 1.f), scale, 1);
    // UV x=0 is the nozzle, x=1 is the rearward tail; mirror the entire vehicle as one unit.
    for (const auto &socket : vehicle.exhausts)
    {
        SetColor({palette.emission[0], palette.emission[1], palette.emission[2], palette.emission[3]});
        const RenderPoint points[] = {{socket.x, socket.y - 12},
                                      {socket.x - length, socket.y - 12},
                                      {socket.x - length, socket.y + 12},
                                      {socket.x, socket.y + 12}};
        GetRenderer().DrawEffect(points, SurfaceEffect::JetExhaust, g_state.animationTimeSeconds);
        DrawGlow({socket.x, socket.y}, 8,
                 ColorRGBA(palette.emission[0], palette.emission[1], palette.emission[2], .55f));
    }
    DrawCachedModel(vehicle.body, {0, 0}, 1, palette);
    for (const auto &lamp : vehicle.headlights)
    {
        DrawGlow({lamp.x, lamp.y}, 5, ColorRGBA(1, .9f, .65f, .75f));
    }
    renderqueue::PopMatrix();
}
void DrawBuilding(Building building)
{
    auto center = WorldToScreen(building.x + building.width * .5f, building.y + building.depth * .5f);
    auto player = WorldToScreen(g_state.playerPosition.x, g_state.playerPosition.y);
    bool occludes = building.x + building.y + building.width + building.depth >
                        g_state.playerPosition.x + g_state.playerPosition.y &&
                    std::fabs(center.x - player.x) < (building.width + building.depth) * 22 &&
                    player.y > center.y - building.height * 1.2f - 25 && player.y < center.y + 35;
    auto palette = building.palette;
    if (occludes)
    {
        palette.body[3] = .28f;
        palette.accent[3] = .28f;
        palette.emission[3] = .28f;
    }
    const auto &model = models::Get().buildings.at(size_t(building.type));
    auto origin = WorldToScreen(building.x, building.y);
    renderqueue::PushMatrix();
    renderqueue::Translate(origin.x, origin.y, 0);
    std::string key = "building/" + std::to_string(unsigned(building.type)) + "/" +
                      std::to_string(building.width) + "/" + std::to_string(building.depth) + "/" +
                      std::to_string(building.height) + PaletteKey(palette);
    rendercache::Draw(key, [&]() {
        DrawParts(model, palette, [&](models::Point p) {
            return Vector2{(p.x * building.width - p.y * building.depth) * 36,
                           (p.x * building.width + p.y * building.depth) * 18 - p.z * building.height};
        });
    });
    renderqueue::PopMatrix();
    // Windows follow the independent palette; only powered light changes at runtime.
    auto light =
        WorldToScreen(building.x + building.width * .5f, building.y + building.depth, building.height * .22f);
    DrawLine(light, {light.x + 15, light.y + 7.5f},
             g_state.isPowerRestored ? ColorRGBA(1, .78f, .45f, palette.body[3])
                                     : ColorRGBA(.3f, .7f, .82f, palette.body[3]),
             3);
}
} // namespace tutorial
