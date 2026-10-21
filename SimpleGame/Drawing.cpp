#include "stdafx.h"
#include "Drawing.h"
#include "Overworld.h"
#include "ResourceAssets.h"
#include "Level.h"
#pragma comment(lib, "opengl32.lib")
#include "RenderQueue.h"
#include "RenderCache.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{
ColorRGBA::ColorRGBA(float red, float green, float blue, float alpha) : r(red), g(green), b(blue), a(alpha)
{
}

// ============================================================================
// Drawing — basic geometry
// ============================================================================
void Drawing::SetColor(ColorRGBA color)
{
    renderqueue::Color(color.r, color.g, color.b, color.a);
}
void Drawing::DrawPolygon(std::initializer_list<Vector2> vertices, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_POLYGON);
    for (auto vertex : vertices)
    {
        renderqueue::Vertex(vertex.x, vertex.y);
    }
    renderqueue::End();
}
void Drawing::DrawLine(Vector2 start, Vector2 end, ColorRGBA color, float lineWidth)
{
    SetColor(color);
    renderqueue::LineWidth(lineWidth);
    renderqueue::Begin(GL_LINES);
    renderqueue::Vertex(start.x, start.y);
    renderqueue::Vertex(end.x, end.y);
    renderqueue::End();
}
void Drawing::DrawRectangle(float x, float y, float width, float height, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_QUADS);
    for (int i = 0; i < 4; ++i)
    {
        const auto &point = models::ModelLibrary::Get().cube[i];
        renderqueue::Vertex(x + point.x * width, y + point.y * height);
    }
    renderqueue::End();
}
void Drawing::DrawLabel(float x, float y, const std::string &text, ColorRGBA color, void *font)
{
    if (GameRenderer::Get().Backend().DeferOverlay([=]() { DrawLabel(x, y, text, color, font); }))
    {
        return;
    }
    SetColor(color);
    int size = font == GLUT_BITMAP_HELVETICA_18 ? 19 : 15;
    GameRenderer::Get().Backend().DrawUtf8Text(x, y, text, size);
}
void Drawing::DrawEllipse(float x, float y, float radiusX, float radiusY, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_TRIANGLE_FAN);
    renderqueue::Vertex(x, y);
    for (const auto &point : models::ModelLibrary::Get().circle)
    {
        renderqueue::Vertex(x + point.x * radiusX, y + point.y * radiusY);
    }
    renderqueue::End();
}
Vector2 Drawing::WorldToScreen(float x, float y, float height)
{
    auto &world = Game::Get().GetState();
    return {kCanvasWidth * .5f + (x - y - world.cameraPosition.x + world.cameraPosition.y) * 36,
            kCanvasHeight * .57f + (x + y - world.cameraPosition.x - world.cameraPosition.y) * 18 - height};
}
void Drawing::DrawTile(float x, float y, float width, float depth, ColorRGBA color)
{
    SetColor(color);
    renderqueue::Begin(GL_QUADS);
    for (int i = 0; i < 4; ++i)
    {
        const auto &point = models::ModelLibrary::Get().cube[i];
        Vector2 screen = WorldToScreen(x + point.x * width, y + point.y * depth);
        renderqueue::Vertex(screen.x, screen.y);
    }
    renderqueue::End();
}
void Drawing::DrawBox(float x, float y, float width, float depth, float height, ColorRGBA color)
{
    models::Palette palette;
    palette.body = {color.r, color.g, color.b, color.a};
    DrawModel3D(models::ModelLibrary::Get().box, WorldToScreen(x, y), {width, depth, height / 44}, {},
                (x + y) * 36, palette);
}
void Drawing::DrawGlow(Vector2 position, float size, ColorRGBA color)
{
    if (GameRenderer::Get().Backend().DeferOverlay([=]() { DrawGlow(position, size, color); }))
    {
        return;
    }
    SetColor(color);
    GameRenderer::Get().Backend().DrawGlow(position.x, position.y, size);
}
void Drawing::DrawMaterialQuad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, SurfaceMaterial material,
                               ColorRGBA color)
{
    const RenderPoint vertices[] = {{a.x, a.y}, {b.x, b.y}, {c.x, c.y}, {d.x, d.y}};
    SetColor(color);
    GameRenderer::Get().Backend().DrawMaterial(vertices, material);
}
void Drawing::DrawMaterialTile(float x, float y, SurfaceMaterial material, ColorRGBA color)
{
    models::Palette palette;
    palette.body = {color.r, color.g, color.b, color.a};
    DrawModel3D(models::ModelLibrary::Get().grounds[size_t(material)], WorldToScreen(x, y), {1, 1, 1}, {},
                (x + y) * 36, palette);
}
void Drawing::DrawSoftShadow(Vector2 center, float radiusX, float radiusY, float opacity)
{
    renderqueue::Begin(GL_TRIANGLE_FAN);
    renderqueue::Color(.005f, .01f, .025f, opacity);
    renderqueue::Vertex(center.x, center.y);
    renderqueue::Color(.005f, .01f, .025f, 0);
    for (const auto &point : models::ModelLibrary::Get().softCircle)
    {
        renderqueue::Vertex(center.x + point.x * radiusX, center.y + point.y * radiusY);
    }
    renderqueue::End();
}
void Drawing::DrawEffectRectangle(Vector2 topLeft, float width, float height, SurfaceEffect effect)
{
    auto &world = Game::Get().GetState();
    const RenderPoint points[] = {{topLeft.x, topLeft.y},
                                  {topLeft.x + width, topLeft.y},
                                  {topLeft.x + width, topLeft.y + height},
                                  {topLeft.x, topLeft.y + height}};
    GameRenderer::Get().Backend().DrawEffect(points, effect, world.animationTimeSeconds);
}

// ============================================================================
// Drawing — environment
// ============================================================================
void Drawing::DrawBackdrop()
{
    auto &world = Game::Get().GetState();
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
        for (const auto &point : models::ModelLibrary::Get().gateRing)
        {
            renderqueue::Vertex(gateX + point.x * 115, gateY + point.y * 145);
        }
        renderqueue::End();
    }
    for (int i = 0; i < 12; i++)
    {
        float angle = i * 2 * kPi / 12 + world.animationTimeSeconds * .015f;
        DrawRectangle(gateX + std::cos(angle) * 115 - 5, gateY + std::sin(angle) * 145 - 7, 10, 14,
                      ColorRGBA(.5f, .9f, .92f));
    }
    if (world.isComplete)
    {
        DrawLine({gateX - 65, gateY}, {gateX + 65, gateY}, ColorRGBA(.85f, .38f, .53f, .6f), 2);
        DrawEllipse(gateX, gateY, 8, 38, ColorRGBA(.9f, .65f, .8f, .6f));
    }
    for (int i = 0; i < 23; i++)
    {
        float x = i * kCanvasWidth / 22.f, towerHeight = 40.f + (i * 53 % 100);
        models::Palette distant;
        distant.body = {.09f, .14f, .19f, 1};
        DrawModel3D(models::ModelLibrary::Get().buildings[size_t(i) % 4], {x, kCanvasHeight * .39f},
                    {.7f, .7f, towerHeight / 44}, {}, -6000, distant);
    }
    DrawLabel(gateX - 63, gateY + 163, "N - 0 / 관문망", ColorRGBA(.3f, .6f, .69f));
    float railY = kCanvasHeight * .4f;
    DrawLine({0, railY}, {(float)kCanvasWidth, railY - 55}, ColorRGBA(.22f, .42f, .51f), 3);
    for (int i = 0; i < 4; i++)
    {
        float x = std::fmod(world.animationTimeSeconds * 115 + i * 360.f, kCanvasWidth + 300.f) - 150,
              y = railY - x * 55 / kCanvasWidth - 12;
        models::Palette palette;
        palette.body = {.38f + i * .09f, .54f, .7f - i * .08f, 1};
        palette.emission =
            i % 2 ? std::array<float, 4>{1, .55f, .22f, 1} : std::array<float, 4>{.25f, .8f, 1, 1};
        DrawVehicle({x, y}, .65f, models::VehicleType(i % 4), palette, .8f);
    }
}
void Drawing::DrawInteractionMarker(Vector2 worldPosition, const std::string &label, ColorRGBA color)
{
    if (GameRenderer::Get().Backend().DeferOverlay(
            [=]() { DrawInteractionMarker(worldPosition, label, color); }))
    {
        return;
    }
    auto &world = Game::Get().GetState();
    Vector2 screenPosition =
        WorldToScreen(worldPosition.x, worldPosition.y, 49 + std::sin(world.animationTimeSeconds * 2) * 3);
    DrawPolygon({{screenPosition.x, screenPosition.y + 7},
                 {screenPosition.x - 5, screenPosition.y},
                 {screenPosition.x, screenPosition.y - 7},
                 {screenPosition.x + 5, screenPosition.y}},
                color);
    DrawLabel(screenPosition.x + 9, screenPosition.y + 4, label, color);
}

// ============================================================================
// Drawing — model instances
// ============================================================================
// ============================================================================
// Drawing — orthographic 3D mesh pass (same camera basis for every model)
// ============================================================================
void Drawing::DrawModel3D(const models::Model &model, Vector2 screen, models::Point scale,
                          const models::Transform3D &transform, float depth, const models::Palette &palette,
                          const models::CharacterPose *pose)
{
    if (GameRenderer::Get().Backend().SubmitModel(model, {screen.x, screen.y}, scale, transform, depth,
                                                  palette, pose))
    {
        return;
    }
    if (model.resourceId)
    {
        const auto &resource = models::ResourceAssets::Get(model.resourceId);
        models::SkinMatrices matrices;
        models::ResourceAssets::Sample(model.resourceId, pose, matrices);
        renderqueue::Depth(true);
        renderqueue::Begin(GL_TRIANGLES);
        for (size_t triangle = 0; triangle < resource.vertices.size(); triangle += 3)
        {
            if (resource.vertices[triangle].visibility == 1 && (!pose || !pose->armed))
            {
                continue;
            }
            models::Point positions[3];
            for (size_t j = 0; j < 3; ++j)
            {
                auto p = models::ResourceAssets::SkinPoint(resource.vertices[triangle + j], matrices);
                positions[j] = transform.Apply({p.x * scale.x, p.y * scale.y, p.z * scale.z});
            }
            auto a = positions[0], b = positions[1], c = positions[2];
            models::Point n = {(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                               (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                               (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
            float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            float light = length > 1.e-8f
                              ? RenderStyle::Shade((-.35f * n.x - .45f * n.y + .82f * n.z) / length,
                                                   GameRenderer::Get().Backend().GetStyle().cartoon)
                              : 1;
            for (size_t j = 0; j < 3; ++j)
            {
                const auto &v = resource.vertices[triangle + j];
                const auto &tint = v.palette == 1   ? palette.body
                                   : v.palette == 2 ? palette.accent
                                                    : palette.emission;
                float shade = v.palette == 3 ? 1 : light;
                renderqueue::Color(v.r * (v.palette ? tint[0] : 1) * shade,
                                   v.g * (v.palette ? tint[1] : 1) * shade,
                                   v.b * (v.palette ? tint[2] : 1) * shade, v.a * palette.body[3]);
                const auto &p = positions[j];
                renderqueue::Vertex(screen.x + (p.x - p.y) * 36, screen.y + (p.x + p.y) * 18 - p.z * 44,
                                    depth + (p.x + p.y) * 36 + p.z * (1296.f / 44));
            }
        }
        renderqueue::End();
        renderqueue::Depth(false);
        return;
    }
    renderqueue::Depth(true);
    auto transformed = [&](models::Point p, models::Joint joint) {
        if (pose)
        {
            p = pose->Apply(p, joint);
        }
        p = {p.x * scale.x, p.y * scale.y, p.z * scale.z};
        return transform.Apply(p);
    };
    for (size_t i = 0; i < model.count; ++i)
    {
        const auto &part = model.parts[i];
        if (part.joint == models::Joint::Weapon && (!pose || !pose->armed))
        {
            continue;
        }
        models::Point v[8];
        for (size_t k = 0; k < part.count; ++k)
        {
            v[k] = transformed(part.points[k], part.joint);
        }
        auto color = part.color;
        const auto &tint = part.palette == models::PaletteSlot::Body     ? palette.body
                           : part.palette == models::PaletteSlot::Accent ? palette.accent
                                                                         : palette.emission;
        for (int k = 0; k < 4; ++k)
        {
            color[k] *= part.palette == models::PaletteSlot::Fixed ? (k == 3 ? palette.body[3] : 1) : tint[k];
        }
        float light = 1;
        if (part.count >= 3 && part.palette != models::PaletteSlot::Emission)
        {
            auto a = v[0], b = v[1], c = v[2];
            models::Point u = {b.x - a.x, b.y - a.y, b.z - a.z}, w = {c.x - a.x, c.y - a.y, c.z - a.z};
            models::Point n = {u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x};
            float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (length > .000001f)
            {
                light = RenderStyle::Shade((-.35f * n.x - .45f * n.y + .82f * n.z) / length,
                                           GameRenderer::Get().Backend().GetStyle().cartoon);
            }
        }
        SetColor({color[0] * light, color[1] * light, color[2] * light, color[3]});
        renderqueue::LineWidth(part.width);
        renderqueue::Begin(part.kind == models::PartKind::Line ? GL_LINES : GL_POLYGON);
        for (size_t k = 0; k < part.count; ++k)
        {
            const auto &p = v[k];
            // Orthographic camera: yaw 45 degrees, elevation approximately 30 degrees.
            // The third coordinate is camera depth, used by the GPU z buffer.
            renderqueue::Vertex(screen.x + (p.x - p.y) * 36, screen.y + (p.x + p.y) * 18 - p.z * 44,
                                depth + (p.x + p.y) * 36 + p.z * (1296.f / 44));
        }
        renderqueue::End();
    }
    renderqueue::Depth(false);
}
void Drawing::DrawPerson(Vector2 position, ColorRGBA coat, bool isPlayer, models::CharacterType type,
                         bool walking, float facingYaw, models::Gender gender, models::HairStyle hair,
                         bool npcArmed, float npcFlash, bool downed)
{
    auto &world = Game::Get().GetState();
    Vector2 screen = WorldToScreen(position.x, position.y);

    if (isPlayer)
    {
        DrawEllipse(screen.x, screen.y, 16, 6, {.25f, .95f, .9f, .17f});
    }
    bool armed = (isPlayer && game::GameLevels::IsCombatActive()) || npcArmed;
    float yaw = isPlayer ? world.facingYaw : facingYaw;
    if (isPlayer)
    {
        gender = world.playerGender;
        hair = world.playerHair;
    }
    if (armed && isPlayer)
    {
        auto dir = game::GameLevels::Mine().GetAimDirection(game::GameLevels::AimAngle());
        yaw = std::atan2(dir.y, dir.x);
    }
    float flash = isPlayer ? game::GameLevels::MuzzleFlash() : npcFlash;
    auto pose = models::ModelRig::Pose(world.animationTimeSeconds * 10,
                                       (isPlayer && world.movementAmount > 0) || walking, armed,
                                       armed ? flash / .055f : 0, false, gender);
    pose.locomotionSpeed = isPlayer ? world.movementSpeed : 2.1f;
    models::Palette palette;
    palette.body = {coat.r, coat.g, coat.b, coat.a};
    models::Transform3D rotation;
    rotation.yaw = yaw;
    if (downed)
    {
        rotation.pitch = -1.35f;
        rotation.position.z = .12f;
    }
    DrawModel3D(models::ModelLibrary::Character(type, gender), screen,
                {models::WorldScale::characterScale, models::WorldScale::characterScale,
                 models::WorldScale::characterScale},
                rotation, (position.x + position.y) * 36, palette, &pose);
    DrawHair(screen,
             {models::WorldScale::characterScale, models::WorldScale::characterScale,
              models::WorldScale::characterScale},
             rotation, (position.x + position.y) * 36, pose, type, gender, hair);
    if (armed && flash > 0)
    {
        auto muzzle = models::ModelRig::Muzzle(pose);
        float bodyScale = models::WorldScale::characterScale;
        muzzle = rotation.Apply({muzzle.x * bodyScale, muzzle.y * bodyScale, muzzle.z * bodyScale});
        DrawGlow(WorldToScreen(position.x + muzzle.x, position.y + muzzle.y, muzzle.z * 44), 19,
                 {1, .85f, .4f});
    }
}
void Drawing::DrawHair(Vector2 screen, models::Point scale, const models::Transform3D &rotation, float depth,
                       const models::CharacterPose &pose, models::CharacterType type, models::Gender gender,
                       models::HairStyle hair)
{
    if (type == models::CharacterType::HeavyCombat)
    {
        return; // Sealed helmet.
    }
    bool covered = type == models::CharacterType::Miner;
    models::Palette palette;
    static constexpr float colors[][3] = {{.14f, .10f, .08f}, {.22f, .13f, .085f}, {.11f, .08f, .065f},
                                          {.34f, .18f, .10f}, {.13f, .09f, .075f}, {.23f, .16f, .12f}};
    const auto &color = colors[size_t(hair)];
    palette.body = {color[0], color[1], color[2], 1};
    if (gender == models::Gender::Female && !models::ModelLibrary::Hair(hair, covered).resourceId)
    {
        scale.x *= .94f;
        scale.y *= .91f;
    }
    DrawModel3D(models::ModelLibrary::Hair(hair, covered), screen, scale, rotation, depth, palette, &pose);
}
void Drawing::DrawCharacterAppearance(Vector2 screen, ColorRGBA coat, models::CharacterType type, int frame,
                                      int direction)
{
    auto pose = models::ModelRig::Pose(frame * 6.2831853f / 8, frame != 0);
    models::Palette palette;
    palette.body = {coat.r, coat.g, coat.b, coat.a};
    models::Transform3D rotation;
    constexpr float yaw[] = {.78539816f, 2.3561945f, -.78539816f, -2.3561945f};
    rotation.yaw = yaw[(std::max)(0, (std::min)(3, direction))];
    DrawModel3D(models::ModelLibrary::Get().characters.at(size_t(type)), screen, {1, 1, 1}, rotation, 0,
                palette, &pose);
    DrawHair(screen, {1, 1, 1}, rotation, 0, pose, type, models::Gender::Male, models::HairStyle::SidePart);
}
namespace
{
void VehicleEffects(const models::VehicleModel &vehicle, Vector2 screen, float scale,
                    const models::Transform3D &rotation, const models::Palette &palette, float throttle,
                    float baseDepth)
{
    auto project = [&](models::Point p) {
        p = rotation.Apply({p.x * scale, p.y * scale, p.z * scale});
        return Vector2{screen.x + (p.x - p.y) * 36, screen.y + (p.x + p.y) * 18 - p.z * 44};
    };
    if (GameRenderer::Get().Backend().DeferOverlay([=, &vehicle]() {
            VehicleEffects(vehicle, screen, scale, rotation, palette, throttle, baseDepth);
        }))
    {
        return;
    }
    // Project sockets from the model, then expand camera-facing 2D quads.
    // Flame width never collapses when the vehicle banks or turns.
    // Alpha billboards test against the opaque scene, without writing depth themselves.
    renderqueue::Depth(true, false);
    auto depthAt = [&](models::Point p) {
        p = rotation.Apply({p.x * scale, p.y * scale, p.z * scale});
        return baseDepth + (p.x + p.y) * 36 + p.z * (1296.f / 44);
    };
    auto glowAt = [&](Vector2 point, float size, ColorRGBA color, float depth) {
        renderqueue::PushMatrix();
        renderqueue::Translate(0, 0, depth);
        Drawing::DrawGlow(point, size, color);
        renderqueue::PopMatrix();
    };
    ColorRGBA emission(palette.emission[0], palette.emission[1], palette.emission[2], .86f);
    float power = Game::Clamp(throttle, 0, 1);
    for (auto socket : vehicle.exhausts)
    {
        Vector2 nozzle = project(socket);
        const float nozzleDepth = depthAt(socket);
        auto back = socket;
        back.x -= 1;
        auto projectedBack = project(back);
        float dx = projectedBack.x - nozzle.x, dy = projectedBack.y - nozzle.y;
        float magnitude = std::sqrt(dx * dx + dy * dy);
        if (magnitude < .001f)
        {
            dx = -1;
            dy = 0;
            magnitude = 1;
        }
        const float depthPerPixel = (depthAt(back) - nozzleDepth) / magnitude;
        dx /= magnitude;
        dy /= magnitude;
        float length = (22 + power * 72) * scale, halfWidth = (6 + power * 5) * scale;
        Vector2 tail = {nozzle.x + dx * length, nozzle.y + dy * length};
        Vector2 edge = {-dy * halfWidth, dx * halfWidth};
        const float tailDepth = nozzleDepth + depthPerPixel * length;
        const RenderPoint quad[] = {{nozzle.x + edge.x, nozzle.y + edge.y, nozzleDepth},
                                    {tail.x + edge.x, tail.y + edge.y, tailDepth},
                                    {tail.x - edge.x, tail.y - edge.y, tailDepth},
                                    {nozzle.x - edge.x, nozzle.y - edge.y, nozzleDepth}};
        Drawing::SetColor(emission);
        GameRenderer::Get().Backend().DrawEffect(quad, SurfaceEffect::JetExhaust,
                                                 Game::Get().GetState().animationTimeSeconds);
        glowAt(nozzle, (6 + power * 4) * scale, emission, nozzleDepth);
        // Sparks extend beyond the flame quad, but share its world depth.
        float clock = Game::Get().GetState().animationTimeSeconds;
        for (int i = 0; i < 10; ++i)
        {
            float life = std::fmod(clock * (1.6f + (i % 3) * .24f) + i * .103f, 1.f);
            float side = i % 2 ? -1.f : 1.f, spread = side * life * life * (18 + power * 17) * scale;
            Vector2 p = {nozzle.x + dx * life * length * 1.5f - dy * spread,
                         nozzle.y + dy * life * length * 1.5f + dx * spread};
            float opacity = (1 - life) * .95f;
            ColorRGBA spark(.64f, .97f, 1, opacity);
            float depth = nozzleDepth + depthPerPixel * life * length * 1.5f;
            renderqueue::PushMatrix();
            renderqueue::Translate(0, 0, depth);
            Drawing::DrawLine(p, {p.x - dx * (3 + life * 6) * scale, p.y - dy * (3 + life * 6) * scale},
                              spark, 1.6f * scale);
            renderqueue::PopMatrix();
            glowAt(p, 1.5f * scale, {emission.r, emission.g, emission.b, opacity * .6f}, depth);
        }
    }
    for (auto lamp : vehicle.headlights)
    {
        glowAt(project(lamp), 4 * scale, {1, .9f, .65f, .65f}, depthAt(lamp));
    }
    renderqueue::Depth(false);
}
} // namespace
void Drawing::DrawVehicle(Vector2 center, float scale, models::VehicleType type,
                          const models::Palette &palette, float throttle, float direction)
{
    const auto &vehicle = models::ModelLibrary::Get().vehicles.at(size_t(type));
    models::Transform3D rotation;
    rotation.yaw = -kPi / 4 + (direction < 0 ? kPi : 0);
    VehicleEffects(vehicle, center, scale, rotation, palette, throttle, 0);
    DrawModel3D(vehicle.body, center, {scale, scale, scale}, rotation, 0, palette);
}
void Drawing::DrawWorldVehicle(Vector2 position, float altitude, float scale, models::VehicleType type,
                               const models::Palette &palette, float throttle, float yaw)
{
    const auto &vehicle = models::ModelLibrary::Get().vehicles.at(size_t(type));
    auto screen = WorldToScreen(position.x, position.y, altitude);
    models::Transform3D rotation;
    rotation.yaw = yaw;
    auto pose = models::ModelRig::Pose(Game::Get().GetState().animationTimeSeconds * 10, throttle > .2f);
    VehicleEffects(vehicle, screen, scale, rotation, palette, throttle,
                   (position.x + position.y) * 36 + altitude * (1296.f / 1936));
    DrawModel3D(vehicle.body, screen, {scale, scale, scale}, rotation,
                (position.x + position.y) * 36 + altitude * (1296.f / 1936), palette, &pose);
}
void Drawing::DrawProp(const models::Model &model, Vector2 position, models::Point scale,
                       const models::Palette &palette, float yaw)
{
    models::Transform3D transform;
    transform.yaw = yaw;
    DrawModel3D(model, WorldToScreen(position.x, position.y), scale, transform,
                (position.x + position.y) * 36, palette);
}
void Drawing::DrawWorldBike(Vector2 position, float yaw, float throttle, bool mounted)
{
    constexpr float altitude = 4.4f;
    DrawWorldVehicle(position, altitude, 1, models::VehicleType::JetBike, Overworld::BikePalette(), throttle,
                     yaw);
    if (!mounted)
    {
        return;
    }
    const auto &vehicle = models::ModelLibrary::Get().vehicles[size_t(models::VehicleType::JetBike)];
    const auto &world = Game::Get().GetState();
    auto screen = WorldToScreen(position.x, position.y, altitude);
    float depth = (position.x + position.y) * 36 + altitude * (1296.f / 1936);
    constexpr float bodyScale = models::WorldScale::characterScale;
    models::Transform3D rotation;
    rotation.yaw = yaw;
    rotation.position = rotation.Apply({vehicle.seat.x, vehicle.seat.y, vehicle.seat.z - .17f * bodyScale});
    auto pose = models::ModelRig::Pose(0, false, false, 0, true, world.playerGender);
    models::Palette clothing;
    clothing.body = {.55f, .68f, .72f, 1};
    DrawModel3D(models::ModelLibrary::Character(models::CharacterType::Explorer, world.playerGender), screen,
                {bodyScale, bodyScale, bodyScale}, rotation, depth, clothing, &pose);
    DrawHair(screen, {bodyScale, bodyScale, bodyScale}, rotation, depth, pose,
             models::CharacterType::Explorer, world.playerGender, world.playerHair);
}
void Drawing::DrawBuilding(Building building)
{
    auto &world = Game::Get().GetState();
    auto center = WorldToScreen(building.x + building.width * .5f, building.y + building.depth * .5f);
    auto player = WorldToScreen(world.playerPosition.x, world.playerPosition.y);
    auto palette = building.palette;
    if (building.x + building.y + building.width + building.depth >
            world.playerPosition.x + world.playerPosition.y &&
        std::fabs(center.x - player.x) < (building.width + building.depth) * 22 &&
        player.y > center.y - building.height * 1.2f - 25 && player.y < center.y + 35)
    {
        palette.body[3] = palette.accent[3] = palette.emission[3] = .28f;
    }
    DrawModel3D(models::ModelLibrary::Get().buildings.at(size_t(building.type)),
                WorldToScreen(building.x, building.y), {building.width, building.depth, building.height / 44},
                {}, (building.x + building.y) * 36, palette);
}
} // namespace tutorial
