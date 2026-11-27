#include "stdafx.h"
#include "Hud.h"
#include "Profiler.h"
#include "Save.h"
#include "Civic.h"
#include "Drawing.h"
#include "Level.h"
#include "Overworld.h"
#include "RenderQueue.h"
#include "RenderCache.h"
#include "TransitLayout.h"
#include "RenderAssets.h"
#include "StoryArc.h"
#include "StoryJournal.h"
#include "WorldEncounters.h"
#include "FieldMinigame.h"
#include "Window.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string_view>
#include <unordered_set>

namespace tutorial
{

// ============================================================================
// MapHud — shared fixed 45-degree schematic projection and CPU-clipped map geometry
// ============================================================================
namespace
{
constexpr Vector2 kMinimapCenter = {1144, 139};
constexpr float kMinimapRadius = 82;
constexpr float kMinimapRange = 34;
constexpr Vector2 kMapMinimum = {80, 126}, kMapMaximum = {898, 674};

const ColorRGBA kUiInk = {.87f, .94f, .94f, 1};
void UiText(float x, float y, const std::string &text, int size = 16, ColorRGBA color = {.87f, .94f, .94f},
            bool outline = false, bool right = false)
{
    if (right)
    {
        x -= RenderAssets::MeasureUtf8(text, size);
    }
    Drawing::SetColor(color);
    GameRenderer::Get().Backend().DrawUiText(x, y, text, size, outline);
}
void Sprite(const std::string &name, float x, float y, float w, float h, ColorRGBA color = {1, 1, 1},
            float fraction = 1, bool nine = false)
{
    Drawing::SetColor(color);
    GameRenderer::Get().Backend().DrawUiSprite(name, x, y, w, h, fraction, nine);
}
void Icon(const std::string &name, float x, float y, float size = 24, bool hud = false,
          ColorRGBA color = {1, 1, 1})
{
    float margin = hud ? size / 6 : 0;
    Sprite(std::string(hud ? "HUDIcons/" : "Icons/") + name, x - margin, y - margin, size + margin * 2,
           size + margin * 2, color);
}
void Panel(float x, float y, float w, float h)
{
    const auto &alpha = Game::Get().GetState().uiPreferences;
    renderqueue::Opacity(alpha.background);
    Sprite("Components/panel_fill", x, y, w, h, {1, 1, 1}, 1, true);
    renderqueue::Opacity(alpha.border);
    Sprite("Components/panel_border", x, y, w, h, {1, 1, 1}, 1, true);
    renderqueue::Opacity(alpha.content);
}
// CQ-12: Unified frosted HUD card with consistent translucent backdrop, crisp rim, and optional theme stripe.
void HudCard(float x, float y, float w, float h, ColorRGBA accent = {.36f, .86f, .82f}, bool leftStripe = true)
{
    const auto &alpha = Game::Get().GetState().uiPreferences;
    const float rx = std::round(x);
    const float ry = std::round(y);
    const float rw = std::round(w);
    const float rh = std::round(h);
    const float bgA = .74f * alpha.background;
    const float rimA = .42f * alpha.border;
    Drawing::DrawRectangle(rx, ry, rw, rh, {.028f, .058f, .076f, bgA});
    Drawing::DrawRectangle(rx, ry, rw, 1.f, {.38f, .66f, .68f, rimA});
    Drawing::DrawRectangle(rx, ry + rh - 1.f, rw, 1.f, {.24f, .44f, .48f, rimA * .85f});
    // Non-overlapping vertical rims prevent double-blended corner artifacts.
    if (rh > 2.f)
    {
        Drawing::DrawRectangle(rx, ry + 1.f, 1.f, rh - 2.f, {.30f, .54f, .58f, rimA * .85f});
        Drawing::DrawRectangle(rx + rw - 1.f, ry + 1.f, 1.f, rh - 2.f, {.30f, .54f, .58f, rimA * .85f});
    }
    if (leftStripe)
    {
        Drawing::DrawRectangle(rx + 1.f, ry + 4.f, 3.f, (std::max)(4.f, rh - 8.f),
                               {accent.r, accent.g, accent.b, .92f * alpha.border});
    }
    renderqueue::Opacity(alpha.content);
}
struct HudShift
{
    explicit HudShift(float dx)
    {
        renderqueue::PushMatrix();
        renderqueue::Translate(std::round(dx), 0, 0);
    }
    ~HudShift()
    {
        renderqueue::PopMatrix();
    }
};
Vector2 CenteredCursor()
{
    auto p = Game::Get().GetState().cursorPosition;
    p.x -= WorldCamera::CenterOffsetX();
    return p;
}
void DrawFullscreenDim(ColorRGBA color)
{
    const float cxOff = WorldCamera::CenterOffsetX();
    Drawing::DrawRectangle(-cxOff, 0, WorldCamera::LogicalWidth(), float(kCanvasHeight), color);
}
void Button(const UiControl &control, int index)
{
    const auto &world = Game::Get().GetState();
    const auto cursor = CenteredCursor();
    bool hover = world.cursorInsideViewport && cursor.x >= control.position.x &&
                 cursor.y >= control.position.y && cursor.x <= control.position.x + control.size.x &&
                 cursor.y <= control.position.y + control.size.y;
    bool selected = index == world.ui.focus;
    if (control.action == UiAction::Choice)
    {
        selected = world.dialogue.selection == control.choice;
    }
    if (control.action == UiAction::ActivityTab)
    {
        selected = control.choice == int(world.journalLocalActivities || !Game::Get().IsStoryWorld());
    }
    if (control.action == UiAction::TrackMission)
    {
        selected = ActivityJournal::Current().mission == MissionId(control.choice);
    }
    std::string state = !control.enabled            ? "disabled"
                        : world.ui.pressed == index ? "pressed"
                        : hover                     ? "hover"
                        : selected                  ? "selected"
                                                    : "default";
    // Disabled/warning PNGs contain symbols; use the stretchable base and separate symbols.
    if (!control.enabled)
    {
        state = "default";
    }
    const bool warningCancel =
        control.action == UiAction::CancelLoad || (world.mapVisible && world.relayPreparing && control.action == UiAction::Back);
    renderqueue::Opacity(world.uiPreferences.background);
    Sprite("Components/button_" + state, control.position.x, control.position.y, control.size.x,
           control.size.y,
           !control.enabled ? ColorRGBA(.55f, .6f, .62f)
           : warningCancel  ? ColorRGBA(1, .78f, .42f)
                            : ColorRGBA(1, 1, 1),
           1, true);
    renderqueue::Opacity(world.uiPreferences.content);
    const bool activity =
        control.action == UiAction::Mission || control.action == UiAction::TrackMission ||
        control.action == UiAction::ActivityTab || control.action == UiAction::ActivityPage ||
        control.action == UiAction::TrackCommunity || control.action == UiAction::ViewActivity ||
        control.action == UiAction::ActivityDetailPage;
    if (activity)
    {
        const bool journal = world.surfaceExploration && world.journalVisible;
        const int font = journal ? 16 : 14, lineHeight = font + 4;
        const auto pages =
            Hud::TextPages(control.label, control.size.x - 16, font,
                           size_t((std::max)(1, int((control.size.y - 8) / float(lineHeight)))));
        // UI time continues while the journal pauses simulation. Long names retain every line.
        const size_t page = size_t((std::max)(0, world.lastUpdateMilliseconds) / 6000) % pages.size();
        for (size_t row = 0; row < pages[page].size(); ++row)
        {
            const float x =
                journal ? control.position.x +
                              (control.size.x - RenderAssets::MeasureUtf8(pages[page][row], font)) * .5f
                        : control.position.x + 8;
            const float y = journal ? control.position.y +
                                          (control.size.y - float(pages[page].size() * lineHeight)) * .5f +
                                          float(font)
                                    : control.position.y + 18;
            UiText(x, y + float(row) * float(lineHeight), pages[page][row], font,
                   control.enabled ? ColorRGBA(.87f, .95f, .94f) : ColorRGBA(.64f, .71f, .75f));
        }
        if (pages.size() > 1)
        {
            for (size_t i = 0; i < pages.size(); ++i)
            {
                Drawing::DrawEllipse(control.position.x + 8 + i * 5, control.position.y + control.size.y - 4,
                                     1.5f, 1.5f,
                                     i == page ? ColorRGBA(.32f, 1, .82f) : ColorRGBA(.3f, .45f, .45f));
            }
        }
        return;
    }
    const char *icon = !control.enabled                                      ? "lock"
                       : (control.action == UiAction::SaveWorld ||
                          control.action == UiAction::Checkpoint)            ? "checkpoint"
                       : control.action == UiAction::LoadWorld               ? "activity_restore"
                       : (control.action == UiAction::Profiles ||
                          control.action == UiAction::SelectProfile ||
                          (world.mapVisible &&
                           (control.action == UiAction::Choice ||
                            control.action == UiAction::Continue)))          ? "relay"
                       : (control.action == UiAction::CancelLoad ||
                          control.action == UiAction::Back)                  ? "exit"
                                                                             : "chevron";
    Icon(icon, control.position.x + 12, control.position.y + 14, 18, false,
         warningCancel ? ColorRGBA(1, .78f, .42f) : ColorRGBA(1, 1, 1));
    auto rows = Dialogue::Wrap(control.label, control.size.x - 58, 17);
    for (size_t row = 0; row < rows.size(); ++row)
    {
        UiText(control.position.x + 40, control.position.y + 29 + row * 24, rows[row], 17,
               control.enabled ? ColorRGBA(.87f, .95f, .94f) : ColorRGBA(.54f, .62f, .65f));
    }
}
struct DialogueLayout
{
    float top, height, bodyTop;
    std::vector<std::string> header, body;
    std::vector<UiControl> controls;
};
DialogueLayout LayoutDialogue()
{
    const auto &w = Game::Get().GetState();
    const auto &page = w.dialogue.pages[w.dialogue.page];
    DialogueLayout d;
    d.header = Dialogue::Wrap(page.speaker + " / " + page.channel, 640, 18);
    d.body = Dialogue::Wrap(page.text, 816, 20);
    float choicesHeight = 0;
    for (size_t i = 0; i < page.choices.size(); ++i)
    {
        auto label = std::to_string(i + 1) + "  " + page.choices[i].text;
        choicesHeight += (std::max)(46.f, 20.f + 24.f * Dialogue::Wrap(label, 758, 17).size()) + 10;
    }
    d.height = (std::max)(274.f, 94.f + float(d.header.size() - 1) * 24 + float(d.body.size()) * 32 +
                                     choicesHeight + 62);
    d.top = 700 - d.height;
    d.bodyTop = d.top + 82 + float(d.header.size() - 1) * 24;
    float y = d.bodyTop + float(d.body.size()) * 32 + 8;
    for (size_t i = 0; i < page.choices.size(); ++i)
    {
        float height =
            (std::max)(46.f, 20.f + 24.f * Dialogue::Wrap(std::to_string(i + 1) + "  " + page.choices[i].text,
                                                          758, 17)
                                               .size());
        d.controls.push_back({UiAction::Choice,
                              {232, y},
                              {816, height},
                              std::to_string(i + 1) + "  " + page.choices[i].text,
                              !Dialogue::Danger(),
                              int(i)});
        y += height + 10;
    }
    bool canAdvance = page.choices.empty() || (w.dialogue.selection >= 0 && !Dialogue::Danger());
    d.controls.push_back({UiAction::Advance,
                          {840, 654},
                          {208, 34},
                          page.choices.empty() ? "E / Enter 다음" : "E / Enter 확정",
                          canAdvance});
    return d;
}
bool Inside(const UiControl &control, Vector2 p)
{
    return p.x >= control.position.x && p.y >= control.position.y &&
           p.x <= control.position.x + control.size.x && p.y <= control.position.y + control.size.y;
}
Vector2 PlayerMapPosition()
{
    const auto &world = Game::Get().GetState();
    if (!game::GameLevels::Active())
    {
        if (world.isInsideHome)
        {
            return kHomeDoor;
        }
        if (world.isInObservatory)
        {
            return kObservatoryBuildingPosition;
        }
    }
    return world.playerPosition;
}
struct SchematicView
{
    Vector2 center, worldCenter;
    float scale, radius;
    Vector2 Project(Vector2 p) const
    {
        constexpr float diagonal = .70710678118f;
        float x = p.x - worldCenter.x, y = p.y - worldCenter.y;
        return {center.x + (x - y) * diagonal * scale, center.y + (x + y) * diagonal * scale};
    }
    Vector2 Unproject(Vector2 p) const
    {
        constexpr float diagonal = .70710678118f;
        float x = (p.x - center.x) / scale, y = (p.y - center.y) / scale;
        return {worldCenter.x + (x + y) * diagonal, worldCenter.y + (y - x) * diagonal};
    }
    bool Inside(Vector2 p, float inset = 0) const
    {
        if (radius > 0)
        {
            return Game::Distance(p, center) <= radius - inset;
        }
        return p.x >= kMapMinimum.x + inset && p.x <= kMapMaximum.x - inset && p.y >= kMapMinimum.y + inset &&
               p.y <= kMapMaximum.y - inset;
    }
};
bool PlanetMapActive()
{
    const auto &w = Game::Get().GetState();
    return w.surfaceExploration && (!Game::Get().IsSurfaceMine() || (w.mapVisible && w.planetMapSurface));
}
SchematicView PlanetMapView(bool full)
{
    const auto &game = Game::Get();
    const auto &state = Game::Get().GetState();
    auto session = game.Surface();
    auto player = session ? session->Local(session->Pose().origin) : world::Vector3d{};
    return {full ? Vector2{488, 400} : kMinimapCenter,
            {float(player.x) + (full ? state.planetMapPan.x : 0),
             float(player.y) + (full ? state.planetMapPan.y : 0)},
            (full ? 244.f : kMinimapRadius) / (full ? state.planetMapRange : 160.f),
            full ? 244.f : kMinimapRadius};
}
SchematicView MapView(bool full)
{
    if (!full)
    {
        return {kMinimapCenter, PlayerMapPosition(), kMinimapRadius / kMinimapRange, kMinimapRadius};
    }
    Vector2 minimum = {0, 0}, maximum = {float(game::kMapSize), float(game::kMapSize)};
    if (MapHud::Region() == MapRegion::Frontier)
    {
        maximum = {float(game::kFrontierWidth), float(game::kFrontierHeight)};
    }
    else if (MapHud::Region() == MapRegion::City)
    {
        minimum = maximum = kHomeDoor;
        auto include = [&](Vector2 p) {
            minimum.x = (std::min)(minimum.x, p.x);
            minimum.y = (std::min)(minimum.y, p.y);
            maximum.x = (std::max)(maximum.x, p.x);
            maximum.y = (std::max)(maximum.y, p.y);
        };
        for (const auto &junction : RoadNetwork::Get().junctions)
        {
            include(junction.position);
        }
        for (const auto &city : Overworld::Cities())
        {
            include({city.center.x - 48, city.center.y - 48});
            include({city.center.x + 48, city.center.y + 48});
        }
        for (const auto &building : Game::Get().GetState().buildings)
        {
            include({building.x, building.y});
            include({building.x + building.width, building.y + building.depth});
        }
        include(Overworld::kMineEntrance);
        // Fit the complete road/city diagram rather than empty terrain beyond the settled region.
        include(PlayerMapPosition());
        const auto &destination = Game::Get().GetState().mapDestinations[size_t(MapRegion::City)];
        if (destination.active)
        {
            include(destination.position);
        }
    }
    float width = ((maximum.x - minimum.x) + (maximum.y - minimum.y)) * .70710678118f;
    float height = width;
    float scale = (std::min)((kMapMaximum.x - kMapMinimum.x - 30) / (width + 8),
                             (kMapMaximum.y - kMapMinimum.y - 30) / (height + 8));
    const auto &navigation = Game::Get().GetState().mapViews[size_t(MapHud::Region())];
    return {
        {(kMapMinimum.x + kMapMaximum.x) * .5f, (kMapMinimum.y + kMapMaximum.y) * .5f},
        {(minimum.x + maximum.x) * .5f + navigation.pan.x, (minimum.y + maximum.y) * .5f + navigation.pan.y},
        scale * navigation.zoom,
        0};
}
void MapPolygonData(const SchematicView &view, const Vector2 *inputPoints, size_t inputCount, ColorRGBA color)
{
    if (inputCount < 3)
    {
        return;
    }
    // Precompute the 64-sided circular clip normals once and reuse scratch buffers (zero heap allocs).
    static const struct CircleClipTable
    {
        std::array<Vector2, 64> normals;
        float cosHalfStep;
        CircleClipTable() : cosHalfStep(std::cos(kPi / 64.f))
        {
            for (int i = 0; i < 64; ++i)
            {
                const float angle = (i + .5f) * kPi * 2.f / 64.f;
                normals[size_t(i)] = {std::cos(angle), std::sin(angle)};
            }
        }
    } kClip;
    static std::vector<Vector2> poly;
    static std::vector<Vector2> clipped;
    bool insideCircle = view.radius > 0;
    if (view.radius > 0)
    {
        Vector2 minimum = inputPoints[0], maximum = inputPoints[0];
        const float innerRadiusSq = (view.radius * kClip.cosHalfStep) * (view.radius * kClip.cosHalfStep);
        for (size_t i = 0; i < inputCount; ++i)
        {
            const auto point = inputPoints[i];
            minimum.x = (std::min)(minimum.x, point.x);
            minimum.y = (std::min)(minimum.y, point.y);
            maximum.x = (std::max)(maximum.x, point.x);
            maximum.y = (std::max)(maximum.y, point.y);
            const float dx = point.x - view.center.x, dy = point.y - view.center.y;
            insideCircle &= dx * dx + dy * dy <= innerRadiusSq;
        }
        if (minimum.x > view.center.x + view.radius || maximum.x < view.center.x - view.radius ||
            minimum.y > view.center.y + view.radius || maximum.y < view.center.y - view.radius)
        {
            return;
        }
        if (insideCircle)
        {
            Drawing::SetColor(color);
            renderqueue::Begin(GL_TRIANGLE_FAN);
            for (size_t i = 0; i < inputCount; ++i)
            {
                renderqueue::Vertex(inputPoints[i].x, inputPoints[i].y);
            }
            renderqueue::End();
            return;
        }
    }
    poly.assign(inputPoints, inputPoints + inputCount);
    auto clip = [&](Vector2 normal, float bound) {
        if (poly.empty())
        {
            return;
        }
        clipped.clear();
        Vector2 previous = poly.back();
        float previousDistance = previous.x * normal.x + previous.y * normal.y - bound;
        for (Vector2 point : poly)
        {
            float distance = point.x * normal.x + point.y * normal.y - bound;
            if ((distance <= 0) != (previousDistance <= 0))
            {
                float t = previousDistance / (previousDistance - distance);
                clipped.push_back(
                    {previous.x + (point.x - previous.x) * t, previous.y + (point.y - previous.y) * t});
            }
            if (distance <= 0)
            {
                clipped.push_back(point);
            }
            previous = point;
            previousDistance = distance;
        }
        poly.swap(clipped);
    };
    if (view.radius > 0)
    {
        const float innerR = view.radius * kClip.cosHalfStep;
        for (int i = 0; i < 64 && !poly.empty(); ++i)
        {
            const Vector2 normal = kClip.normals[size_t(i)];
            const float bound = normal.x * view.center.x + normal.y * view.center.y + innerR;
            // Skip half-plane clip if all vertices are already strictly inside this half-plane.
            bool needsClip = false;
            for (const auto &pt : poly)
            {
                if (pt.x * normal.x + pt.y * normal.y > bound)
                {
                    needsClip = true;
                    break;
                }
            }
            if (needsClip)
            {
                clip(normal, bound);
            }
        }
    }
    else
    {
        clip({-1, 0}, -kMapMinimum.x);
        clip({1, 0}, kMapMaximum.x);
        clip({0, -1}, -kMapMinimum.y);
        clip({0, 1}, kMapMaximum.y);
    }
    if (poly.size() >= 3)
    {
        Drawing::SetColor(color);
        renderqueue::Begin(GL_TRIANGLE_FAN);
        for (auto point : poly)
        {
            renderqueue::Vertex(point.x, point.y);
        }
        renderqueue::End();
    }
}
void MapPolygon(const SchematicView &view, const std::vector<Vector2> &points, ColorRGBA color)
{
    MapPolygonData(view, points.data(), points.size(), color);
}
void MapRectangle(const SchematicView &view, Vector2 minimum, Vector2 maximum, ColorRGBA color)
{
    const Vector2 quad[4] = {view.Project(minimum), view.Project({maximum.x, minimum.y}),
                             view.Project(maximum), view.Project({minimum.x, maximum.y})};
    MapPolygonData(view, quad, 4, color);
}
void MapLine(const SchematicView &view, Vector2 from, Vector2 to, ColorRGBA color, float width)
{
    // A clipped quad keeps even wide road markings completely inside the circular rim.
    Vector2 a = view.Project(from), b = view.Project(to);
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq < 1.e-6f)
    {
        return;
    }
    if (view.radius > 0)
    {
        // Fast segment-to-circle distance broadphase before allocating or clipping.
        const float t = std::clamp(((view.center.x - a.x) * dx + (view.center.y - a.y) * dy) / lenSq, 0.f, 1.f);
        const float cx = a.x + dx * t - view.center.x;
        const float cy = a.y + dy * t - view.center.y;
        const float reach = view.radius + width * .5f + 1.f;
        if (cx * cx + cy * cy > reach * reach)
        {
            return;
        }
    }
    const float invLen = 1.f / std::sqrt(lenSq);
    Vector2 side = {-dy * width * .5f * invLen, dx * width * .5f * invLen};
    const Vector2 quad[4] = {{a.x + side.x, a.y + side.y},
                             {b.x + side.x, b.y + side.y},
                             {b.x - side.x, b.y - side.y},
                             {a.x - side.x, a.y - side.y}};
    MapPolygonData(view, quad, 4, color);
}
void MapDisc(const SchematicView &view, Vector2 position, float worldRadius, ColorRGBA color)
{
    std::vector<Vector2> points;
    for (int i = 0; i < 48; ++i)
    {
        float angle = i * kPi * 2 / 48;
        points.push_back(view.Project(
            {position.x + std::cos(angle) * worldRadius, position.y + std::sin(angle) * worldRadius}));
    }
    MapPolygon(view, std::move(points), color);
}
void DirectionArrow(Vector2 position, float angle, float size, ColorRGBA color, bool triangle = false)
{
    Vector2 direction = {std::cos(angle), std::sin(angle)}, side = {-direction.y, direction.x};
    auto point = [&](float forward, float across) {
        return Vector2{position.x + direction.x * forward + side.x * across,
                       position.y + direction.y * forward + side.y * across};
    };
    if (triangle)
    {
        Drawing::DrawPolygon({point(size + 2, 0), point(-size, size * .8f), point(-size, -size * .8f)},
                             {.01f, .055f, .07f, color.a});
        Drawing::DrawPolygon(
            {point(size, 0), point(-size * .75f, size * .55f), point(-size * .75f, -size * .55f)}, color);
        return;
    }
    Drawing::DrawPolygon(
        {point(size + 2, 0), point(-size, size * .8f), point(-size * .5f, 0), point(-size, -size * .8f)},
        {.01f, .055f, .07f, color.a});
    Drawing::DrawPolygon({point(size, 0), point(-size * .75f, size * .55f), point(-size * .3f, 0),
                          point(-size * .75f, -size * .55f)},
                         color);
}
struct MapLabelBounds
{
    float x, y, width;
};
std::vector<MapLabelBounds> mapLabels;
void MapLabel(Vector2 anchor, const char *label, ColorRGBA color, bool allowSuppress = true,
              Vector2 boundsMin = kMapMinimum, Vector2 boundsMax = kMapMaximum)
{
    if (!label || !*label)
    {
        return;
    }
    float width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(label); *p;)
    {
        unsigned char lead = *p;
        int bytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        width += bytes == 1 ? 8 : 15;
        p += bytes;
    }
    const Vector2 offsets[] = {{12, 5},         {12, 25},         {12, -15},
                               {-width - 12, 5}, {-width - 12, 25}, {-width - 12, -15},
                               {12, 45},        {12, -35}};
    Vector2 position = {};
    bool placed = false;
    for (auto offset : offsets)
    {
        const float maxX = (std::max)(boundsMin.x + 5.f, boundsMax.x - width - 5.f);
        const float maxY = (std::max)(boundsMin.y + 19.f, boundsMax.y - 5.f);
        position = {Game::Clamp(anchor.x + offset.x, boundsMin.x + 5.f, maxX),
                    Game::Clamp(anchor.y + offset.y, boundsMin.y + 19.f, maxY)};
        bool overlap = false;
        for (auto other : mapLabels)
        {
            overlap |= position.x < other.x + other.width + 6 && position.x + width + 6 > other.x &&
                       position.y > other.y - 21 && position.y - 21 < other.y;
        }
        if (!overlap)
        {
            placed = true;
            break;
        }
    }
    if (!placed && allowSuppress)
    {
        return;
    }
    mapLabels.push_back({position.x, position.y, width});
    if (std::fabs(position.y - anchor.y) > 10 || position.x < anchor.x - 4)
    {
        const float leaderX = position.x < anchor.x ? position.x + width + 3 : position.x - 3;
        Drawing::DrawLine(anchor, {leaderX, position.y - 6}, {.45f, .6f, .5f, .7f});
    }
    Drawing::DrawLabel(position.x, position.y, label, color);
}
void MapPin(const SchematicView &view, Vector2 position, ColorRGBA color, const char *label, bool full,
            float size = 5)
{
    if (!PlanetMapActive() && MapHud::Region() == MapRegion::Mine &&
        !game::GameLevels::Mine().Map().IsExplored(position))
    {
        return;
    }
    auto p = view.Project(position);
    if (!view.Inside(p, size + 3))
    {
        return;
    }
    Drawing::DrawPolygon(
        {{p.x, p.y - size - 2}, {p.x + size + 2, p.y}, {p.x, p.y + size + 2}, {p.x - size - 2, p.y}},
        {.025f, .07f, .08f});
    Drawing::DrawPolygon({{p.x, p.y - size}, {p.x + size, p.y}, {p.x, p.y + size}, {p.x - size, p.y}}, color);
    if (full && label)
    {
        MapLabel(p, label, color);
    }
}
void MapDot(const SchematicView &view, Vector2 position, ColorRGBA color, float radius = 4.f)
{
    auto p = view.Project(position);
    if (!view.Inside(p, radius + 2.f))
    {
        return;
    }
    auto oct = [&](float r, ColorRGBA c) {
        const float d = r * .70710678f;
        Drawing::DrawPolygon({{p.x + r, p.y},
                              {p.x + d, p.y + d},
                              {p.x, p.y + r},
                              {p.x - d, p.y + d},
                              {p.x - r, p.y},
                              {p.x - d, p.y - d},
                              {p.x, p.y - r},
                              {p.x + d, p.y - d}},
                             c);
    };
    oct(radius + 1.5f, {.025f, .07f, .08f, .95f});
    oct(radius, color);
}
void DrawMapTiles(const SchematicView &view)
{
    const bool mine = MapHud::Region() == MapRegion::Mine;
    const auto &mineTiles = game::GameLevels::Mine().GetState().tiles;
    const auto &frontierTiles = game::GameLevels::Chapter().GetState().tiles;
    int width = mine ? game::kMapSize : game::kFrontierWidth;
    int height = mine ? game::kMapSize : game::kFrontierHeight;
    auto tileAt = [&](int x, int y) {
        size_t index = size_t(y) * width + x;
        if (mine && !game::GameLevels::Mine().Map().IsExplored({x + .5f, y + .5f}))
        {
            return -1;
        }
        return mine ? (index < mineTiles.size() ? int(mineTiles[index]) : int(game::Tile::Wall))
                    : (index < frontierTiles.size() ? int(frontierTiles[index])
                                                    : int(game::FrontierTile::Grass));
    };
    int minX = 0, minY = 0, maxX = width, maxY = height;
    if (view.radius > 0)
    {
        float range = view.radius / view.scale + 1;
        minX = (std::max)(0, int(std::floor(view.worldCenter.x - range)));
        minY = (std::max)(0, int(std::floor(view.worldCenter.y - range)));
        maxX = (std::min)(width, int(std::ceil(view.worldCenter.x + range)));
        maxY = (std::min)(height, int(std::ceil(view.worldCenter.y + range)));
    }
    for (int y = minY; y < maxY; ++y)
    {
        // Merge equal tiles into row strips; the schematic never draws individual 3D actors.
        for (int x = minX; x < maxX;)
        {
            int kind = tileAt(x, y), end = x + 1;
            while (end < maxX && tileAt(end, y) == kind)
            {
                ++end;
            }
            if (kind < 0)
            {
                x = end;
                continue;
            }
            ColorRGBA color = mine ? (kind == int(game::Tile::Wall)    ? ColorRGBA(.09f, .15f, .17f)
                                      : kind == int(game::Tile::Water) ? ColorRGBA(.15f, .29f, .32f)
                                                                       : ColorRGBA(.35f, .39f, .36f))
                              : kind == int(game::FrontierTile::Grass) ? ColorRGBA(.24f, .37f, .27f)
                              : kind == int(game::FrontierTile::Sand)  ? ColorRGBA(.46f, .41f, .3f)
                              : kind == int(game::FrontierTile::Canal) ? ColorRGBA(.1f, .25f, .29f)
                              : kind == int(game::FrontierTile::Road)  ? ColorRGBA(.4f, .45f, .43f)
                                                                       : ColorRGBA(.33f, .36f, .33f);
            MapRectangle(view, {float(x), float(y)}, {float(end), float(y + 1)}, color);
            x = end;
        }
    }
}
void DrawMapBuildings(const SchematicView &view, const std::vector<Building> &buildings)
{
    for (const auto &b : buildings)
    {
        bool civic = b.type == models::BuildingType::Observatory;
        MapRectangle(view, {b.x - .1f, b.y - .1f}, {b.x + b.width + .1f, b.y + b.depth + .1f},
                     {.08f, .17f, .18f});
        MapRectangle(view, {b.x, b.y}, {b.x + b.width, b.y + b.depth},
                     civic ? ColorRGBA(.65f, .76f, .64f) : ColorRGBA(.49f, .58f, .52f));
    }
}
void DrawCitySchematic(const SchematicView &view, bool full)
{
    for (size_t i = 0; i < Overworld::Cities().size(); ++i)
    {
        const auto &city = Overworld::Cities()[i];
        MapDisc(view, city.center, 45,
                i == 1 || i == 3 ? ColorRGBA(.3f, .35f, .3f) : ColorRGBA(.23f, .34f, .25f));
    }
    for (const auto &access : CityPlan::Connections())
    {
        for (size_t i = 1; i < access.path.size(); ++i)
        {
            MapLine(view, access.path[i - 1], access.path[i], {.44f, .49f, .4f},
                    (std::max)(.65f, view.scale));
        }
    }
    const auto &network = RoadNetwork::Get();
    for (int level = 0; level < 2; ++level)
    {
        for (const auto &road : network.roads)
        {
            const auto &a = network.junctions[road.from], &b = network.junctions[road.to];
            bool raised = (std::max)(a.altitude, b.altitude) > .5f;
            if (raised != (level == 1))
            {
                continue;
            }
            float width = (std::max)(1.5f, road.lanes * RoadNetwork::kLaneWidth * view.scale);
            MapLine(view, a.position, b.position, {.1f, .2f, .2f}, width + 2);
            MapLine(view, a.position, b.position,
                    raised ? ColorRGBA(.28f, .67f, .68f) : ColorRGBA(.57f, .61f, .52f), width);
            if (width > 6)
            {
                MapLine(view, a.position, b.position, {.35f, .4f, .34f}, 1);
            }
        }
    }
    const auto &world = Game::Get().GetState();
    DrawMapBuildings(view, world.buildings);
    MapPin(view, kHomeDoor, {.74f, .85f, .8f}, nullptr, full, 3);
    MapPin(view, world.bikePosition, {1, .72f, .3f}, nullptr, full, 3);
    MapPin(view, Overworld::kMineEntrance, {.98f, .76f, .4f}, "광산 입구", full, 4);
    for (const auto &enc : encounters::WorldEncounters::Get().State().live)
    {
        if (!enc.surfaceSpace && enc.phase != encounters::Phase::Completed)
        {
            const bool active = enc.phase != encounters::Phase::Dormant;
            MapPin(view, enc.legacyAnchor, active ? ColorRGBA(1.f, .82f, .28f) : ColorRGBA(.98f, .58f, .26f),
                   full ? enc.title.c_str() : nullptr, full, active ? 5 : 4);
        }
    }
    Vector2 objective = kHomeDoor;
    if (world.mainQuestStage == MainQuestStage::MeetLia)
    {
        objective = kLiaPosition;
    }
    else if (world.mainQuestStage >= MainQuestStage::EnterObservatory &&
             world.mainQuestStage <= MainQuestStage::DescendObservatory)
    {
        objective = kObservatoryBuildingPosition;
    }
    else if (world.mainQuestStage >= MainQuestStage::MountJetBike)
    {
        auto stage = game::GameLevels::Mine().GetState().quest.GetStage();
        objective = stage == game::MineQuestStage::ShowRecord       ? kLiaPosition
                    : stage == game::MineQuestStage::CheckCityRelay ? kCityReceiverPosition
                    : stage == game::MineQuestStage::TravelToMine   ? Overworld::kMineEntrance
                                                                    : kCityReceiverPosition;
    }
    MapPin(view, objective, {1, .88f, .5f}, "현재 목표", full, 5);
    if (full)
    {
        for (const auto &city : Overworld::Cities())
        {
            auto p = view.Project({city.center.x, city.center.y - 53});
            MapLabel({p.x - 28, p.y - 5}, city.name, {.8f, .87f, .75f});
        }
    }
    for (const auto &hospital : CivicJustice::State().hospitals)
    {
        MapPin(view, hospital.position, {.35f, 1, .72f}, "의료소", full, 4);
    }
}
void DrawTrackedMarker(const SchematicView &view, Vector2 target, const std::string &label, bool full)
{
    if (!PlanetMapActive() && MapHud::Region() == MapRegion::Mine &&
        !game::GameLevels::Mine().Map().IsExplored(target))
    {
        return;
    }
    const ColorRGBA color{.32f, 1, .82f};
    const auto point = view.Project(target);
    if (view.Inside(point, 10))
    {
        Icon("activity_track", point.x - 9, point.y - 9, 18);
        if (full)
        {
            MapLabel(point, label.c_str(), color);
        }
    }
    else if (view.radius > 0)
    {
        const float distance = Game::Distance(point, view.center);
        if (distance <= .001f)
        {
            return;
        }
        const Vector2 edge{view.center.x + (point.x - view.center.x) * (view.radius - 13) / distance,
                           view.center.y + (point.y - view.center.y) * (view.radius - 13) / distance};
        DirectionArrow(edge, std::atan2(point.y - view.center.y, point.x - view.center.x), 7, color, true);
    }
}
void DrawFieldSchematic(const SchematicView &view, bool full)
{
    DrawMapTiles(view);
    if (MapHud::Region() == MapRegion::Mine)
    {
        const auto &mine = game::GameLevels::Mine().GetState();
        MapPin(view, mine.entry, {.8f, .87f, .73f}, "광산 밖으로", full, 4);
        auto stage = mine.quest.GetStage();
        Vector2 objective = stage == game::MineQuestStage::BypassPower          ? game::kPowerBypass
                            : stage == game::MineQuestStage::InvestigateSignal  ? game::kRuinSignal
                            : stage == game::MineQuestStage::InvestigateGlyph   ? game::kObserverGlyph
                            : stage == game::MineQuestStage::CollectCoordinates ? game::kCoordinateTerminal
                                                                                : mine.entry;
        if (Game::Get().GetState().surfaceExploration)
        {
            const auto goal = ActivityJournal::Current();
            if (goal.mineTarget)
            {
                DrawTrackedMarker(view, *goal.mineTarget, goal.title, full);
            }
        }
        else
        {
            MapPin(view, objective, {1, .88f, .5f}, "현재 목표", full, 5);
        }
        for (const auto &contact : mine.scannerContacts)
        {
            if (contact.group < mine.rescueGroups.size() && !mine.rescueGroups[contact.group].rescued &&
                contact.secondsRemaining > 0)
            {
                MapPin(view, contact.position, {.35f, 1, .72f}, "구조 신호", full, 4);
            }
        }
    }
    else
    {
        const auto &chapter = game::GameLevels::Chapter().GetState();
        DrawMapBuildings(view, chapter.structures);
        const Vector2 points[] = {game::kFrontierEntry, {10.5f, 10.5f}, {32.5f, 12.5f}, {33.5f, 32.5f}};
        const char *names[] = {"헤일로 귀환", "발전기", "운송대", "폐설비"};
        for (int i = 0; i < 4; ++i)
        {
            MapPin(view, points[i], {1, .82f, .45f}, names[i], full, 4);
        }
    }
}
void DrawDestination(const SchematicView &view, bool full)
{
    const auto &destination = Game::Get().GetState().mapDestinations[size_t(MapHud::Region())];
    if (!destination.active)
    {
        return;
    }
    if (MapHud::Region() == MapRegion::Mine &&
        !game::GameLevels::Mine().Map().IsExplored(destination.position))
    {
        return;
    }
    auto target = view.Project(destination.position);
    if (full || view.Inside(target, 12))
    {
        MapPin(view, destination.position, {.43f, .87f, 1}, full ? "설정한 목적지" : nullptr, full, 7);
        Drawing::DrawEllipse(target.x, target.y, 2, 2, {.02f, .12f, .17f});
    }
    else
    {
        float angle = std::atan2(target.y - view.center.y, target.x - view.center.x);
        float radius = view.radius - 11;
        DirectionArrow({view.center.x + std::cos(angle) * radius, view.center.y + std::sin(angle) * radius},
                       angle, 8, {.4f, .87f, 1});
    }
}
void DrawSecurityVision(const SchematicView &view)
{
    const auto &law = CivicJustice::State();
    if (law.wanted <= 0)
    {
        return;
    }
    for (const auto &officer : law.officers)
    {
        auto center = view.Project(officer.position);
        if (officer.health <= 0 || !view.Inside(center, 7))
        {
            continue;
        }
        std::vector<Vector2> cone = {center};
        constexpr int segments = 12;
        for (int i = 0; i <= segments; ++i)
        {
            float angle = officer.facing.yaw - CivicJustice::kSightHalfAngle +
                          2 * CivicJustice::kSightHalfAngle * i / segments;
            float range = CivicJustice::SightDistance(officer, angle);
            cone.push_back(view.Project({officer.position.x + std::cos(angle) * range,
                                         officer.position.y + std::sin(angle) * range}));
        }
        MapPolygon(view, std::move(cone),
                   law.identified ? ColorRGBA(1, .35f, .2f, .15f) : ColorRGBA(1, .75f, .3f, .14f));
        DirectionArrow(center, officer.facing.yaw + kPi * .25f, 5,
                       law.identified ? ColorRGBA(1, .4f, .24f) : ColorRGBA(1, .8f, .35f));
    }
}
void DrawSchematic(const SchematicView &view, bool full)
{
    mapLabels.clear();
    if (MapHud::Region() == MapRegion::City)
    {
        DrawCitySchematic(view, full);
        if (!full)
        {
            DrawSecurityVision(view);
        }
    }
    else
    {
        DrawFieldSchematic(view, full);
    }
    DrawDestination(view, full);
    const auto &world = Game::Get().GetState();
    float heading = world.isBikeMounted ? world.bikeYaw : world.facingYaw;
    DirectionArrow(view.Project(PlayerMapPosition()), heading + kPi * .25f, full ? 8.f : 10.f,
                   {.25f, 1, .94f});
}
std::vector<const world::RelayCandidate *> RegisteredRelays()
{
    std::vector<const world::RelayCandidate *> result;
    const auto *session = Game::Get().Surface();
    const auto &w = Game::Get().GetState();
    if (session && session->Ready())
    {
        for (const auto &relay : session->Stream().Snapshot()->planet->relays)
        {
            if (w.relayNetwork.Registered(relay.id))
            {
                result.push_back(&relay);
            }
        }
    }
    return result;
}
std::vector<UiControl> RelayControls()
{
    auto &w = Game::Get().GetState();
    auto entries = RegisteredRelays();
    int pages = (std::max)(1, int((entries.size() + 2) / 3));
    w.relayPage = std::clamp(w.relayPage, 0, pages - 1);
    std::vector<UiControl> controls;
    const auto *session = Game::Get().Surface();
    for (int row = 0; row < 3 && size_t(w.relayPage * 3 + row) < entries.size(); ++row)
    {
        int index = w.relayPage * 3 + row;
        const auto *relay = entries[size_t(index)];
        auto name = session->Stream().Snapshot()->planet->Region(relay->regionId).definition.name;
        auto suffix = relay->id.substr(relay->id.find_last_of('_') + 1);
        controls.push_back(
            {UiAction::Choice,
             {820, 232.f + row * 62},
             {344, 56},
             name + " · " + suffix + (w.selectedRelay == relay->id ? "\n선택됨" : "\n등록 중계환"),
             !w.relayPreparing,
             index});
    }
    controls.push_back(
        {UiAction::Previous, {820, 424}, {164, 44}, "이전 [", w.relayPage > 0 && !w.relayPreparing});
    controls.push_back(
        {UiAction::Next, {1000, 424}, {164, 44}, "다음 ]", w.relayPage + 1 < pages && !w.relayPreparing});
    controls.push_back({UiAction::Continue,
                        {820, 486},
                        {344, 44},
                        w.relayPreparing ? "도착 준비 중…" : "선택한 중계환으로 이동 · T",
                        !w.relayPreparing && RelayTravel::Restriction(w.selectedRelay) == nullptr});
    if (w.relayPreparing)
    {
        controls.push_back({UiAction::Back, {820, 544}, {344, 44}, "중계 이동 취소 · X / Esc"});
    }
    return controls;
}

// ============================================================================
// Part 3 (10-12): 3D Rotating Planet Globe, Wrist Hologram & Space System Map
// ============================================================================
constexpr Vector2 kGlobeCenter = {488.f, 400.f};
constexpr float kGlobeRadius = 244.f;
constexpr Vector2 kSpaceCenter = {442.f, 396.f};

world::SurfacePoint CurrentGlobeCenter(const world::SurfaceSession &session)
{
    const auto &w = Game::Get().GetState();
    return w.planetGlobeInitialized ? w.planetGlobeCenter : session.Pose().origin;
}

struct GlobeView
{
    Vector2 center = kGlobeCenter;
    float radius = kGlobeRadius;
    double planetRadius = 1200000.;
    double halfAngle = world::SurfaceGeometry::Pi * 0.5;
    double sinHalf = 1.0;
    world::Vector3d east = {0, 1, 0};
    world::Vector3d north = {0, 0, 1};
    world::Vector3d forward = {1, 0, 0};

    static GlobeView Build(const world::SurfaceSession &session)
    {
        const auto &w = Game::Get().GetState();
        GlobeView g;
        g.planetRadius = session.Planet().radiusMeters;
        const auto focus = CurrentGlobeCenter(session);
        g.forward = world::SurfaceGeometry::Unit(focus.normal);
        const world::Vector3d upRef =
            std::fabs(g.forward.z) < 0.995 ? world::Vector3d{0, 0, 1} : world::Vector3d{0, 1, 0};
        g.east = world::SurfaceGeometry::Unit(world::SurfaceGeometry::Cross(upRef, g.forward));
        g.north = world::SurfaceGeometry::Cross(g.forward, g.east);
        g.halfAngle = Game::Clamp(double(w.planetMapRange) / g.planetRadius, 64.0 / g.planetRadius,
                                  world::SurfaceGeometry::Pi * 0.5);
        g.sinHalf = std::sin(g.halfAngle);
        return g;
    }

    Vector2 Project(const world::SurfacePoint &pt, bool *frontHemisphere = nullptr) const
    {
        const double x = world::SurfaceGeometry::Dot(pt.normal, east);
        const double y = world::SurfaceGeometry::Dot(pt.normal, north);
        const double z = world::SurfaceGeometry::Dot(pt.normal, forward);
        if (frontHemisphere)
        {
            *frontHemisphere = z > 0.0;
        }
        if (z <= 0.0)
        {
            return {center.x + 9999.f, center.y + 9999.f};
        }
        return {center.x + float((x / sinHalf) * double(radius)),
                center.y - float((y / sinHalf) * double(radius))};
    }

    std::optional<world::SurfacePoint> Unproject(Vector2 canvas) const
    {
        const double dx = double(canvas.x - center.x) / double(radius);
        const double dy = -double(canvas.y - center.y) / double(radius);
        if (dx * dx + dy * dy > 1.0)
        {
            return std::nullopt;
        }
        const double u = dx * sinHalf;
        const double v = dy * sinHalf;
        const double r2 = u * u + v * v;
        if (r2 > 1.0)
        {
            return std::nullopt;
        }
        const double z = std::sqrt((std::max)(0.0, 1.0 - r2));
        const world::Vector3d dir = world::SurfaceGeometry::Unit(
            {east.x * u + north.x * v + forward.x * z, east.y * u + north.y * v + forward.y * z,
             east.z * u + north.z * v + forward.z * z});
        return world::SurfacePoint{dir, 0.0};
    }

    world::SurfacePoint RotateByPixels(Vector2 deltaPx) const
    {
        // Rotation speed scales naturally with current zoom angular half-span.
        const double dAng = (halfAngle / double(radius)) * 1.15;
        const double dx = -double(deltaPx.x) * dAng;
        const double dy = double(deltaPx.y) * dAng;
        const world::Vector3d dir = world::SurfaceGeometry::Unit(
            {forward.x + east.x * dx + north.x * dy, forward.y + east.y * dx + north.y * dy,
             forward.z + east.z * dx + north.z * dy});
        return world::SurfacePoint{dir, 0.0};
    }

    bool Contains(Vector2 canvas, float inset = 0.f) const
    {
        return Game::Distance(canvas, center) <= radius - inset;
    }

    float PixelsPerMeter() const
    {
        return float(double(radius) / (std::max)(1.0, planetRadius * sinHalf));
    }
};

struct SpaceBody
{
    const char *id;
    const char *name;
    const char *systemLabel;
    const char *summary;
    float radiusKm;
    float orbitAu;
    float orbitPx;
    float baseAngle;
    float bodyRadius;
    ColorRGBA baseColor;
    ColorRGBA rimColor;
    bool hasRing;
    bool playable;
};

const SpaceBody kSpaceBodies[] = {
    {"erma", "에르마 (Erma)", "에르마 주계열성계", "개척 거점 · 온대 해양과 도시 클러스터", 1200.f, 1.00f,
     96.f, 0.48f, 17.f, {.20f, .64f, .72f}, {.46f, .96f, .90f}, false, true},
    {"seira", "세이라 (Seira)", "외곽 탐사 성계", "저중력 위성형 · 암석 고원과 황토 협곡", 434.f, 1.58f,
     158.f, 2.38f, 12.f, {.76f, .58f, .36f}, {.98f, .82f, .54f}, false, true},
    {"orren", "오르엔 (Orren)", "외곽 탐사 성계", "고중력 대형 행성 · 고리대·빙하/사막", 1750.f, 2.28f,
     222.f, 4.18f, 21.f, {.46f, .38f, .74f}, {.78f, .62f, .98f}, true, true},
    {"nereus", "네레우스 (Nereus)", "외곽 도약 성계", "관문 N-0 잔류 신호가 지목한 미개척 성계", 0.f, 3.40f,
     282.f, 5.45f, 11.f, {.34f, .80f, 1.f}, {.66f, .98f, 1.f}, false, false}};

Vector2 ProjectSpaceOrbit(float orbitPx, float angle, float yaw, float pitch, float zoom)
{
    const float a = angle + yaw;
    const float sx = std::cos(a) * orbitPx * zoom;
    const float sy = std::sin(a) * orbitPx * zoom * std::sin(pitch);
    return {kSpaceCenter.x + sx, kSpaceCenter.y + sy};
}

UiControl SpaceModeToggleControl(bool spaceMode)
{
    return {UiAction::Continue, {992.f, 92.f}, {200.f, 40.f},
            spaceMode ? "행성 구체 [G]" : "우주 지도 [G]", true, 0};
}

UiControl ClearDestinationControl(bool hasDestination)
{
    return {UiAction::Back, {820.f, 92.f}, {162.f, 40.f}, "목적지 해제", hasDestination, 0};
}

Vector2 PlayerWristHudPoint()
{
    const auto &w = Game::Get().GetState();
    const auto *session = Game::Get().Surface();
    if (w.surfaceExploration && session && session->Ready() && !Game::Get().IsSurfaceMine() && !w.IsInsideAny())
    {
        const float elev = float(session->Local(session->Pose().origin).z / 2.0);
        const Vector2 base =
            Drawing::WorldToScreen(w.playerPosition.x, w.playerPosition.y, elev * WorldCamera::IsoZScale());
        const float s = models::WorldScale::characterScale;
        models::Transform3D rot;
        rot.yaw = w.facingYaw;
        const auto p = rot.Apply({0.24f * s, -0.16f * s, 0.72f * s});
        const Vector2 worldCanvas{base.x + (p.x - p.y) * 36.f,
                                  base.y + (p.x + p.y) * WorldCamera::IsoYScale() - p.z * WorldCamera::IsoZScale()};
        Vector2 hudPt = WorldCamera::ToViewport(worldCanvas);
        hudPt.x -= WorldCamera::CenterOffsetX();
        return {Game::Clamp(hudPt.x, 40.f, 1240.f), Game::Clamp(hudPt.y, 40.f, 770.f)};
    }
    return {150.f, 756.f};
}

void DrawWristHologramEmitterOverlay(float progress)
{
    const float t = Game::Clamp(progress, 0.02f, 1.f);
    const float ease = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    const Vector2 wrist = PlayerWristHudPoint();
    // Luminous wrist emitter ring + translucent cyan projection rays feeding into the floating map panel.
    const Vector2 anchorL{64.f, 724.f};
    const Vector2 anchorR{320.f, 724.f};
    Drawing::DrawPolygon({wrist, anchorL, anchorR}, {.22f, .92f, .98f, 0.14f * ease});
    Drawing::DrawLine(wrist, anchorL, {.44f, .98f, 1.f, 0.48f * ease}, 1.5f);
    Drawing::DrawLine(wrist, anchorR, {.44f, .98f, 1.f, 0.34f * ease}, 1.2f);
    Drawing::DrawGlow(wrist, 24.f * ease, {.30f, .96f, 1.f, 0.72f * ease});
    Drawing::DrawEllipse(wrist.x, wrist.y, 6.f * ease, 6.f * ease, {.78f, 1.f, 1.f, 0.92f * ease});
}

int NearestClusterIndex(const world::GeneratedPlanet &planet, const world::SurfacePoint &point)
{
    if (planet.clusters.empty())
    {
        return 0;
    }
    int bestIdx = 0;
    double bestDot = -2.0;
    for (size_t i = 0; i < planet.clusters.size(); ++i)
    {
        const double d = world::SurfaceGeometry::Dot(planet.clusters[i].normal, point.normal);
        if (d > bestDot)
        {
            bestDot = d;
            bestIdx = int(i);
        }
    }
    return bestIdx;
}

ColorRGBA BiomeGlobeColor(world::Biome biome, float elevationRatio)
{
    switch (biome)
    {
    case world::Biome::Ocean:
        return {.05f, .17f, .27f};
    case world::Biome::Coast:
        return {.10f, .30f, .36f};
    case world::Biome::Grassland:
        return {.17f, .39f, .27f};
    case world::Biome::Forest:
        return {.11f, .30f, .21f};
    case world::Biome::Desert:
        return {.48f, .39f, .24f};
    case world::Biome::Rocky:
        return {.32f, .33f, .35f};
    case world::Biome::Alpine:
        return {.44f + elevationRatio * .12f, .47f + elevationRatio * .12f, .50f + elevationRatio * .12f};
    case world::Biome::Ice:
        return {.66f, .78f, .84f};
    default:
        return {.16f, .30f, .31f};
    }
}

void DrawSpaceSystemMap(const world::SurfaceSession &session)
{
    const auto &w = Game::Get().GetState();
    const std::string currentPlanetId = session.Planet().id;
    const std::string selectedId = w.mapSelectedPlanetId.empty() ? currentPlanetId : w.mapSelectedPlanetId;

    UiText(96, 122, "우주 성계 항법도 · 에르마 섹터", 25, {.84f, .98f, .96f});
    UiText(96, 146, "좌클릭 드래그 궤도 회전 · 휠 확대/축소 · 좌클릭 행성 정보 · 우클릭 항로 지정", 13,
           {.70f, .86f, .88f});

    // Deep-space holographic viewport card.
    Drawing::DrawRectangle(92.f, 160.f, 700.f, 488.f, {.018f, .036f, .058f, .94f});
    Drawing::DrawLine({92.f, 160.f}, {792.f, 160.f}, {.30f, .74f, .82f, .72f}, 1.5f);
    Drawing::DrawLine({92.f, 648.f}, {792.f, 648.f}, {.30f, .74f, .82f, .72f}, 1.5f);
    Drawing::DrawLine({92.f, 160.f}, {92.f, 648.f}, {.30f, .74f, .82f, .52f}, 1.5f);
    Drawing::DrawLine({792.f, 160.f}, {792.f, 648.f}, {.30f, .74f, .82f, .52f}, 1.5f);

    // Subtle deterministic starfield.
    for (int i = 0; i < 64; ++i)
    {
        const float sx = 104.f + float((i * 173 + 37) % 676);
        const float sy = 172.f + float((i * 97 + 53) % 464);
        const float br = 0.28f + float(i % 5) * 0.14f;
        Drawing::DrawRectangle(sx, sy, (i % 7 == 0) ? 2.f : 1.5f, (i % 7 == 0) ? 2.f : 1.5f,
                               {.78f, .92f, 1.f, br});
    }

    // Central star (Sol-Erma).
    Drawing::DrawGlow(kSpaceCenter, 58.f * w.mapSpaceZoom, {1.f, .78f, .36f, .42f});
    Drawing::DrawGlow(kSpaceCenter, 28.f * w.mapSpaceZoom, {1.f, .94f, .72f, .82f});
    Drawing::DrawEllipse(kSpaceCenter.x, kSpaceCenter.y, 16.f * w.mapSpaceZoom, 16.f * w.mapSpaceZoom,
                         {1.f, .95f, .78f, 1.f});
    UiText(kSpaceCenter.x - 36.f, kSpaceCenter.y + 30.f * w.mapSpaceZoom, "에르마 주계열성", 12,
           {1.f, .88f, .62f}, true);

    // Draw orbital rings & planets.
    Vector2 currentPos = kSpaceCenter;
    Vector2 targetPos = kSpaceCenter;
    bool hasTargetPos = false;
    const SpaceBody *selectedBody = &kSpaceBodies[0];

    for (const auto &body : kSpaceBodies)
    {
        if (selectedId == body.id)
        {
            selectedBody = &body;
        }
        // Elliptical 3D-tilted orbit ring.
        const ColorRGBA orbitCol = body.playable ? ColorRGBA(.24f, .56f, .64f, .44f)
                                                 : ColorRGBA(.42f, .46f, .78f, .34f);
        Vector2 prev = ProjectSpaceOrbit(body.orbitPx, 0.f, w.mapSpaceYaw, w.mapSpacePitch, w.mapSpaceZoom);
        for (int seg = 1; seg <= 48; ++seg)
        {
            const float a = float(seg) * (kPi * 2.f / 48.f);
            const Vector2 curr = ProjectSpaceOrbit(body.orbitPx, a, w.mapSpaceYaw, w.mapSpacePitch, w.mapSpaceZoom);
            if (curr.x >= 96.f && curr.x <= 788.f && curr.y >= 164.f && curr.y <= 644.f &&
                prev.x >= 96.f && prev.x <= 788.f && prev.y >= 164.f && prev.y <= 644.f)
            {
                Drawing::DrawLine(prev, curr, orbitCol, body.id == selectedId ? 1.8f : 1.f);
            }
            prev = curr;
        }
    }

    for (const auto &body : kSpaceBodies)
    {
        const Vector2 pos =
            ProjectSpaceOrbit(body.orbitPx, body.baseAngle, w.mapSpaceYaw, w.mapSpacePitch, w.mapSpaceZoom);
        const bool isCurrent = currentPlanetId == body.id;
        const bool isSelected = selectedId == body.id;
        const bool isTarget = w.mapTargetPlanetId == body.id;
        if (isCurrent)
        {
            currentPos = pos;
        }
        if (isTarget)
        {
            targetPos = pos;
            hasTargetPos = true;
        }
        if (pos.x < 104.f || pos.x > 780.f || pos.y < 172.f || pos.y > 636.f)
        {
            continue;
        }
        const float r = body.bodyRadius * Game::Clamp(w.mapSpaceZoom, 0.65f, 1.45f);
        if (body.hasRing)
        {
            Drawing::DrawEllipse(pos.x, pos.y, r * 1.95f, r * 0.56f, {.74f, .66f, .92f, .32f});
        }
        Drawing::DrawGlow(pos, r * (isSelected ? 2.4f : 1.7f),
                          {body.rimColor.r, body.rimColor.g, body.rimColor.b, isSelected ? .55f : .30f});
        // 3D lit sphere disc + shadowed crescent + highlight.
        Drawing::DrawEllipse(pos.x, pos.y, r, r, body.baseColor);
        Drawing::DrawEllipse(pos.x + r * .18f, pos.y + r * .14f, r * .82f, r * .82f, {.02f, .04f, .07f, .44f});
        Drawing::DrawEllipse(pos.x - r * .28f, pos.y - r * .26f, r * .46f, r * .42f,
                             {body.rimColor.r, body.rimColor.g, body.rimColor.b, .65f});
        if (isSelected)
        {
            Drawing::DrawEllipse(pos.x, pos.y, r + 6.f, r + 6.f, {.40f, .98f, .92f, .24f});
        }
        if (isCurrent)
        {
            Drawing::DrawEllipse(pos.x, pos.y, r + 9.f, r + 9.f, {.36f, 1.f, .72f, .28f});
        }
        UiText(pos.x - 42.f, pos.y + r + 15.f, body.name, 13,
               isCurrent ? ColorRGBA(.46f, 1.f, .86f) : ColorRGBA(.86f, .94f, .96f), true);
        if (isCurrent)
        {
            UiText(pos.x - 32.f, pos.y - r - 8.f, "[현재 위치]", 11, {.46f, 1.f, .82f}, true);
        }
        else if (isTarget)
        {
            UiText(pos.x - 32.f, pos.y - r - 8.f, "[항로 목표]", 11, {1.f, .84f, .36f}, true);
        }
    }

    // Draw trajectory arc if a target planet is pinned.
    if (hasTargetPos && Game::Distance(currentPos, targetPos) > 10.f)
    {
        const Vector2 mid{(currentPos.x + targetPos.x) * .5f, (currentPos.y + targetPos.y) * .5f - 34.f};
        Vector2 prev = currentPos;
        for (int i = 1; i <= 20; ++i)
        {
            const float u = float(i) / 20.f;
            const float inv = 1.f - u;
            const Vector2 pt{inv * inv * currentPos.x + 2.f * inv * u * mid.x + u * u * targetPos.x,
                             inv * inv * currentPos.y + 2.f * inv * u * mid.y + u * u * targetPos.y};
            if (i % 2 == 1)
            {
                Drawing::DrawLine(prev, pt, {1.f, .84f, .34f, .90f}, 2.2f);
            }
            prev = pt;
        }
    }

    // Right-side Planet Selector & Telemetry Panel (x=812..1192).
    UiText(820, 174, "성계 천체 목록", 18, {.78f, .95f, .94f});
    for (size_t i = 0; i < std::size(kSpaceBodies); ++i)
    {
        const auto &b = kSpaceBodies[i];
        const float y = 192.f + float(i) * 52.f;
        const bool sel = selectedBody->id == b.id;
        const bool cur = currentPlanetId == b.id;
        UiControl card{UiAction::Continue, {820.f, y}, {370.f, 44.f},
                       std::string(b.name) + (cur ? "  · 현재" : b.playable ? "  · 착륙 가능" : "  · 신호 추적"),
                       true, int(i)};
        Button(card, sel ? 0 : -1);
    }

    HudCard(820.f, 412.f, 370.f, 186.f, selectedBody->rimColor, false);
    UiText(836, 440, selectedBody->name, 20, {.92f, .98f, 1.f});
    UiText(836, 464, selectedBody->systemLabel, 13, {.64f, .88f, .90f});
    char stats[128];
    if (selectedBody->radiusKm > 0.f)
    {
        std::snprintf(stats, sizeof(stats), "반경 %.0f km  ·  궤도 %.2f AU", selectedBody->radiusKm,
                      selectedBody->orbitAu);
    }
    else
    {
        std::snprintf(stats, sizeof(stats), "외곽 도약점  ·  거리 %.2f AU", selectedBody->orbitAu);
    }
    UiText(836, 490, stats, 14, {.84f, .92f, .90f});
    const auto descPages = Hud::TextPages(selectedBody->summary, 336.f, 14, 2);
    for (size_t i = 0; i < descPages[0].size(); ++i)
    {
        UiText(836, 518.f + float(i) * 20.f, descPages[0][i], 14, {.78f, .88f, .88f});
    }
    const bool canSwitch = selectedBody->playable && currentPlanetId != selectedBody->id;
    UiControl switchBtn{UiAction::Continue, {836.f, 554.f}, {338.f, 34.f},
                        currentPlanetId == selectedBody->id ? "현재 착륙 중인 행성입니다"
                        : selectedBody->playable            ? "이 행성으로 탐사 전환"
                                                            : "도약 관문 복구 후 항로 개방",
                        canSwitch, 99};
    Button(switchBtn, -1);

    UiText(820, 628, "G 행성 3D 구체 복귀 · M 지도 닫기", 14, {.70f, .84f, .84f});
    UiText(96, 672, "행성 간 상대 위치와 궤도 거리를 실시간으로 파악할 수 있습니다.", 14, {.74f, .86f, .86f});
    UiText(96, 696, "좌클릭 드래그 회전 · 휠 확대/축소 · 우클릭 항로 목표 지정", 14, {.68f, .82f, .84f});
}
} // namespace
MapRegion MapHud::Region()
{
    return game::GameLevels::Chapter().IsActive() ? MapRegion::Frontier
           : game::GameLevels::Mine().IsActive()  ? MapRegion::Mine
                                                  : MapRegion::City;
}
Vector2 MapHud::WorldToMap(Vector2 worldPoint, bool fullMap)
{
    if (PlanetMapActive() && fullMap)
    {
        if (const auto *session = Game::Get().Surface(); session && session->Ready())
        {
            if (const auto pt = world::SurfaceAtlas::Unproject(session->Origin(), {worldPoint.x, worldPoint.y, 0},
                                                               session->Planet().radiusMeters))
            {
                return GlobeView::Build(*session).Project(*pt);
            }
        }
    }
    return (PlanetMapActive() ? PlanetMapView(fullMap) : MapView(fullMap)).Project(worldPoint);
}
Vector2 MapHud::MapToWorld(Vector2 canvasPoint, bool fullMap)
{
    if (PlanetMapActive() && fullMap)
    {
        if (const auto *session = Game::Get().Surface(); session && session->Ready())
        {
            if (auto pt = GlobeView::Build(*session).Unproject(canvasPoint))
            {
                const auto local =
                    world::SurfaceAtlas::Project(session->Origin(), *pt, session->Planet().radiusMeters);
                return {float(local.x), float(local.y)};
            }
        }
    }
    return (PlanetMapActive() ? PlanetMapView(fullMap) : MapView(fullMap)).Unproject(canvasPoint);
}
Vector2 MapHud::PlanetToMap(const world::SurfacePoint &point, bool full)
{
    const auto *session = Game::Get().Surface();
    if (!session || !session->Ready())
    {
        return {};
    }
    if (full)
    {
        return GlobeView::Build(*session).Project(point);
    }
    auto local = world::SurfaceAtlas::Project(session->Origin(), point, session->Planet().radiusMeters);
    return PlanetMapView(false).Project({float(local.x), float(local.y)});
}
std::optional<world::SurfacePoint> MapHud::PlanetAt(Vector2 canvas)
{
    const auto *session = Game::Get().Surface();
    if (!PlanetMapActive() || !session || !session->Ready() || !Contains(canvas, true))
    {
        return std::nullopt;
    }
    return GlobeView::Build(*session).Unproject(canvas);
}
bool MapHud::Contains(Vector2 canvasPoint, bool fullMap)
{
    if (PlanetMapActive() && fullMap)
    {
        return Game::Distance(canvasPoint, kGlobeCenter) <= kGlobeRadius;
    }
    return (PlanetMapActive() ? PlanetMapView(fullMap) : MapView(fullMap)).Inside(canvasPoint);
}
void MapHud::OnMapOpened()
{
    auto &w = Game::Get().GetState();
    w.mapHologramProgress = 0.f;
    w.mapDragging = false;
    w.mapDraggedMoved = false;
    w.mapSpaceMode = false;
    if (const auto *session = Game::Get().Surface(); session && session->Ready())
    {
        w.planetGlobeCenter = session->Pose().origin;
        w.planetGlobeInitialized = true;
        if (w.mapSelectedPlanetId.empty())
        {
            w.mapSelectedPlanetId = session->Planet().id;
        }
    }
}
bool MapHud::Zoom(int steps, Vector2 pivot)
{
    auto &state = Game::Get().GetState();
    pivot.x -= WorldCamera::CenterOffsetX();
    if (PlanetMapActive() && (!Game::Get().Surface() || !Game::Get().Surface()->Ready()))
    {
        return false;
    }
    if (!state.mapVisible || state.relayPreparing || state.isPaused || state.ui.screen != UiScreen::None)
    {
        return false;
    }
    if (PlanetMapActive() && state.mapSpaceMode)
    {
        if (pivot.x < 92.f || pivot.x > 792.f || pivot.y < 160.f || pivot.y > 648.f)
        {
            return false;
        }
        const float mul = std::pow(1.22f, Game::Clamp(float(steps), -8.f, 8.f));
        state.mapSpaceZoom = Game::Clamp(state.mapSpaceZoom * mul, 0.55f, 1.85f);
        return true;
    }
    if (!Contains(pivot, true))
    {
        return false;
    }
    float multiplier = std::pow(1.35f, Game::Clamp(float(steps), -8, 8));
    if (PlanetMapActive())
    {
        const auto *session = Game::Get().Surface();
        if (!state.planetGlobeInitialized)
        {
            state.planetGlobeCenter = session->Pose().origin;
            state.planetGlobeInitialized = true;
        }
        const auto beforePt = GlobeView::Build(*session).Unproject(pivot);
        state.planetMapRange = Game::Clamp(
            state.planetMapRange / multiplier, 64,
            float(world::SurfaceAtlas::MaximumRange(session->Planet().radiusMeters)));
        if (beforePt)
        {
            for (int iter = 0; iter < 5; ++iter)
            {
                const auto afterGlobe = GlobeView::Build(*session);
                bool front = false;
                const auto afterPx = afterGlobe.Project(*beforePt, &front);
                if (!front)
                {
                    break;
                }
                const double errX = double(pivot.x - afterPx.x);
                const double errY = double(pivot.y - afterPx.y);
                if (errX * errX + errY * errY < 1e-10)
                {
                    break;
                }
                const double step = afterGlobe.sinHalf / double(afterGlobe.radius);
                const double dx = -errX * step;
                const double dy = errY * step;
                state.planetGlobeCenter = {world::SurfaceGeometry::Unit(
                                               {afterGlobe.forward.x + afterGlobe.east.x * dx + afterGlobe.north.x * dy,
                                                afterGlobe.forward.y + afterGlobe.east.y * dx + afterGlobe.north.y * dy,
                                                afterGlobe.forward.z + afterGlobe.east.z * dx + afterGlobe.north.z * dy}),
                                           0.0};
            }
        }
        return true;
    }
    auto before = MapToWorld(pivot, true);
    auto &navigation = state.mapViews[size_t(Region())];
    navigation.zoom = Game::Clamp(navigation.zoom * multiplier, .25f, 32);
    auto after = MapToWorld(pivot, true);
    auto &pan = state.mapViews[size_t(Region())].pan;
    pan.x += before.x - after.x;
    pan.y += before.y - after.y;
    Pan({}); // Also applies the finite navigation bounds after zooming at an edge.
    return true;
}
void MapHud::Pan(Vector2 delta)
{
    auto &state = Game::Get().GetState();
    if (!state.mapVisible || state.relayPreparing || state.isPaused || state.ui.screen != UiScreen::None)
    {
        return;
    }
    if (PlanetMapActive())
    {
        const auto *session = Game::Get().Surface();
        if (!session || !session->Ready())
        {
            return;
        }
        if (state.mapSpaceMode)
        {
            state.mapSpaceYaw += delta.x * 0.008f;
            state.mapSpacePitch = Game::Clamp(state.mapSpacePitch + delta.y * 0.005f, 0.22f, 1.35f);
            return;
        }
        if (!state.planetGlobeInitialized)
        {
            state.planetGlobeCenter = session->Pose().origin;
            state.planetGlobeInitialized = true;
        }
        if (std::fabs(delta.x) > 1e-4f || std::fabs(delta.y) > 1e-4f)
        {
            state.planetGlobeCenter = GlobeView::Build(*session).RotateByPixels({-delta.x, -delta.y});
        }
        return;
    }
    auto view = MapView(true);
    auto origin = view.Unproject(view.center);
    auto shifted = view.Unproject({view.center.x + delta.x, view.center.y + delta.y});
    auto &pan = state.mapViews[size_t(Region())].pan;
    float limit = Region() == MapRegion::City ? 2000.f : 256.f;
    pan.x = Game::Clamp(pan.x + shifted.x - origin.x, -limit, limit);
    pan.y = Game::Clamp(pan.y + shifted.y - origin.y, -limit, limit);
}
void MapHud::ResetView(bool playerOnly)
{
    auto &state = Game::Get().GetState();
    if (PlanetMapActive())
    {
        state.planetMapPan = {};
        if (const auto *session = Game::Get().Surface(); session && session->Ready())
        {
            state.planetGlobeCenter = session->Pose().origin;
            state.planetGlobeInitialized = true;
        }
        if (!playerOnly)
        {
            state.planetMapRange = 1000;
            state.mapSpaceYaw = 0.42f;
            state.mapSpacePitch = 0.56f;
            state.mapSpaceZoom = 1.f;
        }
    }
    else
    {
        auto &navigation = state.mapViews[size_t(Region())];
        navigation.pan = {};
        if (playerOnly)
        {
            auto center = MapView(true).worldCenter, player = PlayerMapPosition();
            navigation.pan = {player.x - center.x, player.y - center.y};
        }
        else
        {
            navigation.zoom = 1;
        }
    }
}
bool MapHud::FocusTrackedObjective()
{
    auto &g = Game::Get();
    auto &w = g.GetState();
    const auto goal = ActivityJournal::Current();
    if (!w.surfaceExploration || !g.Surface() || !g.Surface()->Ready() || goal.title.empty() ||
        Dialogue::Active() || w.dialogue.suspended || HospitalRecovery::Active() || w.relayPreparing)
    {
        return false;
    }
    if (g.IsSurfaceMine() && goal.mineTarget)
    {
        w.mapVisible = true;
        w.planetMapSurface = false;
        w.mapSpaceMode = false;
        w.mapHologramProgress = 0.f;
        auto &viewState = w.mapViews[size_t(MapRegion::Mine)];
        viewState.pan = {};
        const auto view = MapView(true);
        viewState.pan = {goal.mineTarget->x - view.worldCenter.x, goal.mineTarget->y - view.worldCenter.y};
    }
    else if (goal.surfaceTarget)
    {
        w.planetGlobeCenter = *goal.surfaceTarget;
        w.planetGlobeInitialized = true;
        w.planetMapPan = {};
        w.planetMapRange = (std::min)(w.planetMapRange, 1000.f);
        w.mapVisible = true;
        w.planetMapSurface = true;
        w.mapSpaceMode = false;
        w.mapHologramProgress = 0.f;
    }
    else
    {
        return false;
    }
    w.journalVisible = w.dialogueHistoryVisible = false;
    w.mouseFiring = w.mouseAiming = false;
    w.ui.focus = 0;
    w.ui.pressed = -1;
    w.StopMotion();
    return true;
}
bool MapHud::HandleMapKey(unsigned char key)
{
    auto &w = Game::Get().GetState();
    if (w.surfaceExploration && key == 9 && Game::Get().IsSurfaceMine())
    {
        w.planetMapSurface = !w.planetMapSurface;
        return true;
    }
    if (PlanetMapActive())
    {
        if (key == 'g')
        {
            w.mapSpaceMode = !w.mapSpaceMode;
            w.mapDragging = false;
            if (w.mapSpaceMode && w.mapSelectedPlanetId.empty() && Game::Get().Surface())
            {
                w.mapSelectedPlanetId = Game::Get().Surface()->Planet().id;
            }
            return true;
        }
        if (key == 'x' && !w.relayPreparing)
        {
            w.planetDestination.reset();
            w.selectedRelay.clear();
            w.relayStatus.clear();
            w.mapTargetPlanetId.clear();
            return true;
        }
        if (key == 'p' && Game::Get().Surface())
        {
            w.mapSpaceMode = false;
            w.planetMapRange =
                float(world::SurfaceAtlas::MaximumRange(Game::Get().Surface()->Planet().radiusMeters));
            w.planetMapPan = {};
            w.planetGlobeCenter = Game::Get().Surface()->Pose().origin;
            w.planetGlobeInitialized = true;
            return true;
        }
        if (key == 'l')
        {
            w.mapRegisteredRelaysOnly = !w.mapRegisteredRelaysOnly;
            return true;
        }
        if (key == 't')
        {
            RelayTravel::Begin(w.selectedRelay);
            return true;
        }
        if (key == '[' || key == ']')
        {
            w.relayPage = (std::max)(0, w.relayPage + (key == ']' ? 1 : -1));
            return true;
        }
    }
    if (key == '+' || key == '=' || key == '-' || key == '_')
    {
        const Vector2 pivot = PlanetMapActive()
                                  ? (w.mapSpaceMode ? kSpaceCenter : kGlobeCenter)
                                  : MapView(true).center;
        Zoom(key == '+' || key == '=' ? 1 : -1, {pivot.x + WorldCamera::CenterOffsetX(), pivot.y});
        return true;
    }
    if (key == '0' || key == 'c')
    {
        ResetView(key == 'c');
        return true;
    }
    return false;
}
void MapHud::HandleMotion(Vector2 canvasPoint)
{
    auto &w = Game::Get().GetState();
    if (!w.mapVisible || !w.mapDragging || !PlanetMapActive())
    {
        return;
    }
    const auto *session = Game::Get().Surface();
    if (!session || !session->Ready())
    {
        w.mapDragging = false;
        return;
    }
    Vector2 pt = canvasPoint;
    pt.x -= WorldCamera::CenterOffsetX();
    const Vector2 delta{pt.x - w.mapDragLast.x, pt.y - w.mapDragLast.y};
    w.mapDragLast = pt;
    if (Game::Distance(pt, w.mapDragStart) >= 3.f)
    {
        w.mapDraggedMoved = true;
    }
    if (w.mapSpaceMode)
    {
        w.mapSpaceYaw += delta.x * 0.008f;
        w.mapSpacePitch = Game::Clamp(w.mapSpacePitch + delta.y * 0.006f, 0.22f, 1.35f);
        return;
    }
    if (!w.planetGlobeInitialized)
    {
        w.planetGlobeCenter = session->Pose().origin;
        w.planetGlobeInitialized = true;
    }
    w.planetGlobeCenter = GlobeView::Build(*session).RotateByPixels(delta);
}
bool MapHud::HandlePointer(int button, bool down, Vector2 canvasPoint)
{
    auto &w = Game::Get().GetState();
    if (!w.mapVisible)
    {
        return false;
    }
    if (!PlanetMapActive())
    {
        if (down)
        {
            return HandleClick(canvasPoint, button == GLFW_MOUSE_BUTTON_RIGHT);
        }
        return false;
    }
    const auto *session = Game::Get().Surface();
    if (!session || !session->Ready())
    {
        return false;
    }
    Vector2 pt = canvasPoint;
    pt.x -= WorldCamera::CenterOffsetX();

    if (!down)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT && w.mapDragging)
        {
            const bool wasClick = !w.mapDraggedMoved;
            w.mapDragging = false;
            w.mapDraggedMoved = false;
            if (wasClick)
            {
                if (w.mapSpaceMode)
                {
                    for (const auto &body : kSpaceBodies)
                    {
                        const Vector2 bpos = ProjectSpaceOrbit(body.orbitPx, body.baseAngle, w.mapSpaceYaw,
                                                               w.mapSpacePitch, w.mapSpaceZoom);
                        if (Game::Distance(bpos, pt) <= body.bodyRadius + 14.f)
                        {
                            w.mapSelectedPlanetId = body.id;
                            return true;
                        }
                    }
                }
                else if (!w.relayPreparing && Contains(pt, true))
                {
                    const GlobeView globe = GlobeView::Build(*session);
                    const world::RelayCandidate *hit = nullptr;
                    float nearest = 14.f;
                    for (const auto &relay : session->Stream().Snapshot()->planet->relays)
                    {
                        if (w.relayNetwork.Discovered(relay.id) &&
                            (!w.mapRegisteredRelaysOnly || w.relayNetwork.Registered(relay.id)))
                        {
                            bool front = false;
                            const auto rp = globe.Project(relay.location.position, &front);
                            if (front)
                            {
                                const float dist = Game::Distance(rp, pt);
                                if (dist < nearest)
                                {
                                    hit = &relay;
                                    nearest = dist;
                                }
                            }
                        }
                    }
                    if (hit)
                    {
                        w.relayStatus.clear();
                        w.selectedRelay = hit->id;
                        w.planetDestination = hit->location.position;
                        return true;
                    }
                }
            }
            return true;
        }
        return false;
    }

    // Button PRESS on Planet 3D Globe or Space System Map.
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (Inside(SpaceModeToggleControl(w.mapSpaceMode), pt))
        {
            w.mapSpaceMode = !w.mapSpaceMode;
            w.mapDragging = false;
            if (w.mapSpaceMode && w.mapSelectedPlanetId.empty())
            {
                w.mapSelectedPlanetId = session->Planet().id;
            }
            return true;
        }
        if (w.mapSpaceMode)
        {
            const std::string selectedId =
                w.mapSelectedPlanetId.empty() ? session->Planet().id : w.mapSelectedPlanetId;
            for (size_t i = 0; i < std::size(kSpaceBodies); ++i)
            {
                const UiControl card{UiAction::Continue, {820.f, 192.f + float(i) * 52.f}, {370.f, 44.f}, ""};
                if (Inside(card, pt))
                {
                    w.mapSelectedPlanetId = kSpaceBodies[i].id;
                    return true;
                }
            }
            const UiControl switchBtn{UiAction::Continue, {836.f, 554.f}, {338.f, 34.f}, ""};
            if (Inside(switchBtn, pt))
            {
                for (const auto &body : kSpaceBodies)
                {
                    if (selectedId == body.id && body.playable && session->Planet().id != body.id)
                    {
                        if (SaveSystem::SwitchPlanet(body.id))
                        {
                            w.mapVisible = false;
                            w.mapSpaceMode = false;
                            w.mapDragging = false;
                        }
                        else if (!SaveSystem::Message().empty())
                        {
                            Game::Get().ShowNotice(SaveSystem::Message());
                        }
                        return true;
                    }
                }
                return true;
            }
            if (pt.x >= 92.f && pt.x <= 792.f && pt.y >= 160.f && pt.y <= 648.f)
            {
                w.mapDragging = true;
                w.mapDragStart = w.mapDragLast = pt;
                w.mapDraggedMoved = false;
                return true;
            }
            return false;
        }
        if (w.planetDestination && Inside(ClearDestinationControl(true), pt))
        {
            w.planetDestination.reset();
            w.selectedRelay.clear();
            w.relayStatus.clear();
            return true;
        }
        for (const auto &control : RelayControls())
        {
            if (Inside(control, pt))
            {
                if (!control.enabled)
                {
                    return true;
                }
                if (control.action == UiAction::Back)
                {
                    RelayTravel::Cancel();
                }
                else if (control.action == UiAction::Continue)
                {
                    RelayTravel::Begin(w.selectedRelay);
                }
                else if (control.action == UiAction::Next)
                {
                    ++w.relayPage;
                }
                else if (control.action == UiAction::Previous)
                {
                    --w.relayPage;
                }
                else
                {
                    auto entries = RegisteredRelays();
                    auto relay = entries.at(size_t(control.choice));
                    w.selectedRelay = relay->id;
                    w.relayStatus.clear();
                    w.planetDestination = relay->location.position;
                    w.planetGlobeCenter = relay->location.position;
                    w.planetGlobeInitialized = true;
                }
                return true;
            }
        }
        if (!w.relayPreparing && Contains(pt, true))
        {
            w.mapDragging = true;
            w.mapDragStart = w.mapDragLast = pt;
            w.mapDraggedMoved = false;
            return true;
        }
        return false;
    }

    // Right-Click sets destination on the 3D Planet Globe (or sets navigation target on Space Map)!
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (w.mapSpaceMode)
        {
            for (const auto &body : kSpaceBodies)
            {
                const Vector2 bpos = ProjectSpaceOrbit(body.orbitPx, body.baseAngle, w.mapSpaceYaw,
                                                       w.mapSpacePitch, w.mapSpaceZoom);
                if (Game::Distance(bpos, pt) <= body.bodyRadius + 14.f)
                {
                    if (w.mapTargetPlanetId == body.id)
                    {
                        w.mapTargetPlanetId.clear();
                    }
                    else
                    {
                        w.mapTargetPlanetId = body.id;
                        w.mapSelectedPlanetId = body.id;
                    }
                    return true;
                }
            }
            w.mapTargetPlanetId.clear();
            return true;
        }
        if (w.relayPreparing || !Contains(pt, true))
        {
            return false;
        }
        const GlobeView globe = GlobeView::Build(*session);
        if (w.planetDestination)
        {
            bool front = false;
            const auto existing = globe.Project(*w.planetDestination, &front);
            if (front && Game::Distance(existing, pt) <= 14.f)
            {
                w.planetDestination.reset();
                w.selectedRelay.clear();
                w.relayStatus.clear();
                return true;
            }
        }
        auto target = globe.Unproject(pt);
        if (!target)
        {
            return false;
        }
        auto location = *target;
        location.altitudeMeters = session->Stream().Snapshot()->planet->terrain.Height(location);
        w.planetDestination = location;
        w.selectedRelay.clear();
        w.relayStatus.clear();
        return true;
    }
    return false;
}
bool MapHud::HandleClick(Vector2 point, bool clear)
{
    auto &world = Game::Get().GetState();
    point.x -= world.mapVisible ? WorldCamera::CenterOffsetX() : WorldCamera::RightOffsetX();
    if (PlanetMapActive())
    {
        auto session = Game::Get().Surface();
        if (!world.mapVisible || !session || !session->Ready())
        {
            return false;
        }
        if (!clear)
        {
            for (const auto &control : RelayControls())
            {
                if (Inside(control, point))
                {
                    if (!control.enabled)
                    {
                        return true;
                    }
                    if (control.action == UiAction::Back)
                    {
                        RelayTravel::Cancel();
                    }
                    else if (control.action == UiAction::Continue)
                    {
                        RelayTravel::Begin(world.selectedRelay);
                    }
                    else if (control.action == UiAction::Next)
                    {
                        ++world.relayPage;
                    }
                    else if (control.action == UiAction::Previous)
                    {
                        --world.relayPage;
                    }
                    else
                    {
                        auto entries = RegisteredRelays();
                        auto relay = entries.at(size_t(control.choice));
                        world.selectedRelay = relay->id;
                        world.relayStatus.clear();
                        world.planetDestination = relay->location.position;
                    }
                    return true;
                }
            }
        }
        if (world.relayPreparing || !Contains(point, true))
        {
            return false;
        }
        if (clear)
        {
            world.planetDestination.reset();
            world.selectedRelay.clear();
            world.relayStatus.clear();
        }
        else
        {
            const world::RelayCandidate *hit = nullptr;
            float nearest = 10;
            for (const auto &relay : session->Stream().Snapshot()->planet->relays)
            {
                if (world.relayNetwork.Discovered(relay.id) &&
                    (!world.mapRegisteredRelaysOnly || world.relayNetwork.Registered(relay.id)))
                {
                    float distance = Game::Distance(PlanetToMap(relay.location.position), point);
                    if (distance < nearest)
                    {
                        hit = &relay;
                        nearest = distance;
                    }
                }
            }
            if (hit)
            {
                world.relayStatus.clear();
                world.selectedRelay = hit->id;
                world.planetDestination = hit->location.position;
                return true;
            }
            auto target = PlanetAt(point);
            if (!target)
            {
                return false;
            }
            auto location = *target;
            location.altitudeMeters = session->Stream().Snapshot()->planet->terrain.Height(location);
            world.planetDestination = location;
            world.selectedRelay.clear();
            world.relayStatus.clear();
        }
        return true;
    }
    if (!world.mapVisible || !Contains(point, true))
    {
        return false;
    }
    auto &destination = world.mapDestinations[size_t(Region())];
    if (clear)
    {
        destination.active = false;
        return true;
    }
    auto target = MapToWorld(point, true);
    Vector2 minimum = {0, 0}, maximum = {float(game::kMapSize), float(game::kMapSize)};
    if (Region() == MapRegion::City)
    {
        minimum = {kWorldMinX, kWorldMinY};
        maximum = {kWorldMaxX, kWorldMaxY};
    }
    else if (Region() == MapRegion::Frontier)
    {
        maximum = {float(game::kFrontierWidth), float(game::kFrontierHeight)};
    }
    if (target.x < minimum.x || target.x > maximum.x || target.y < minimum.y || target.y > maximum.y)
    {
        return false;
    }
    if (Region() == MapRegion::Mine && !game::GameLevels::Mine().Map().IsExplored(target))
    {
        return false;
    }
    destination = {true, target};
    return true;
}
void MapHud::DrawMinimap()
{
    HudShift shift(WorldCamera::RightOffsetX());
    const auto view = MapView(false);
    Drawing::DrawEllipse(view.center.x, view.center.y, view.radius, view.radius, {.025f, .07f, .08f, .92f});
    DrawSchematic(view, false);
    Sprite("Components/minimap_ring", view.center.x - 91, view.center.y - 91, 182, 182);
    UiText(1207, 69, "N ↗", 14, {.77f, .9f, .86f}, true);
    UiText(1096, 244, "M 전체 지도", 14, {.77f, .9f, .86f}, true);
    const auto &dest = Game::Get().GetState().mapDestinations[size_t(Region())];
    if (dest.active)
    {
        Icon("destination", 1085, 255, 16, true);
        char distance[64];
        std::snprintf(distance, sizeof(distance), "%.0f m",
                      Game::Distance(PlayerMapPosition(), dest.position) * models::WorldScale::metersPerUnit);
        UiText(1109, 270, distance, 15, {.62f, .88f, .96f}, true);
    }
}
void MapHud::DrawFullMap()
{
    if (PlanetMapActive())
    {
        return;
    }
    if (!Game::Get().GetState().mapVisible)
    {
        return;
    }
    HudShift shift(WorldCamera::CenterOffsetX());
    DrawFullscreenDim({.01f, .025f, .03f, .5f});
    Panel(56, 40, 1168, 720);
    Drawing::DrawRectangle(80, 126, 818, 548,
                           Region() == MapRegion::Mine ? ColorRGBA(.015f, .025f, .035f)
                                                       : ColorRGBA(.1f, .2f, .22f));
    Icon("record", 80, 68);
    UiText(116, 90, "지역 약도", 24);
    UiText(80, 113,
           Region() == MapRegion::City   ? "헤일로 주변 / 도시 연결망"
           : Region() == MapRegion::Mine ? "광산 / 탐험한 구간만 표시"
                                         : "1장 / 조사 구역",
           15);
    DrawSchematic(MapView(true), true);
    UiText(844, 151, "N ↗", 16, {.64f, .88f, .82f}, true, true);
    UiText(938, 149, "범례", 22);
    const char *icons[] = {"player", "destination", "objective", "gate", "medical", "bike", "unknown_signal"};
    const char *labels[] = {"나의 위치 / 방향", "설정한 목적지", "현재 목표",     "관문 / 지역 출입",
                            "의료소",           "주차된 바이크", "근사 구조 신호"};
    for (int i = 0; i < 7; ++i)
    {
        Icon(icons[i], 938, 177 + i * 42, 22);
        UiText(974, 196 + i * 42, labels[i], 16);
    }
    UiText(938, 522, "미탐험 구간과 미확인", 15);
    UiText(938, 548, "목표는 표시되지 않습니다.", 15);
    UiText(80, 714, "좌클릭 목적지 설정     우클릭 해제", 16);
    UiText(1190, 714, "M / Esc 닫기", 16, kUiInk, false, true);
    UiText(80, 741,
           Game::Get().IsSurfaceMine()
               ? "Tab 지상 지도 · 휠 확대·축소 · 방향키 이동 · C 내 위치 · 0 초기화"
               : "휠 / +/- 확대·축소 · 방향키 이동 · C 내 위치 · 0 초기화 · 지도 열람 중 정지",
           14, {.55f, .72f, .72f});
}

// ============================================================================
// CityHud
// ============================================================================
void CityHud::DrawInteractionPrompt()
{
    auto &g = Game::Get();
    auto &w = g.GetState();
    if (Dialogue::Active() || w.journalVisible || w.dialogueHistoryVisible || w.ui.screen != UiScreen::None)
    {
        return;
    }
    auto target = g.FindNearbyInteraction();
    if (target == InteractionTarget::None)
    {
        return;
    }
    const char *label = "가까운 대상 / 조사";
    switch (target)
    {
    case InteractionTarget::HomeTerminal:
        label = (w.surfaceExploration ? w.homeMessageRead
                                      : w.mainQuestStage > MainQuestStage::ReadHomeTerminal)
                    ? "집 단말 / 거점 현황 · 차량 호출"
                    : "집 단말 / 메시지 확인";
        break;
    case InteractionTarget::HomeBed:
        label = "침대 / 휴식 · 안전 저장";
        break;
    case InteractionTarget::HomeWorkbench:
        label = "작업대 / 장비 강화 · 보급 정비";
        break;
    case InteractionTarget::HomeStorage:
        label = "보관함 / 금고 · 외형 변경";
        break;
    case InteractionTarget::Lia:
        label = "리아 / 대화";
        break;
    case InteractionTarget::Mara:
        label = "마라 / 대화";
        break;
    case InteractionTarget::Kain:
        label = "카인 / 대화";
        break;
    case InteractionTarget::Resident:
        label = "주민 / 대화";
        break;
    case InteractionTarget::GroundVehicle:
        label = w.car.mounted ? "차량 / 하차" : "공용차 / 탑승";
        break;
    case InteractionTarget::Aircraft:
        label = w.aircraft.mounted ? (w.aircraft.grounded           ? "비행체 / 하차"
                                      : w.aircraft.landingRequested ? "비행체 / 착륙 취소"
                                                                    : "비행체 / 착륙")
                                   : "공용 비행체 / 탑승";
        break;
    case InteractionTarget::JetBike:
        label = w.isBikeMounted ? "바이크 / 하차" : "바이크 / 탑승";
        break;
    case InteractionTarget::CityReceiver:
        label = w.surfaceExploration ? "도시 수신망 / 조사 기록" : "도시 수신망 / 조사·출발";
        break;
    case InteractionTarget::MineEntrance:
        label = "광산 / 입장";
        break;
    case InteractionTarget::Relay:
        label = "국지 중계환 / 등록·이동 지도";
        break;
    case InteractionTarget::Community:
        label = g.SurfaceCommunity() ? g.SurfaceCommunity()->Label() : "지역 현장 / 상호작용";
        break;
    case InteractionTarget::FrontierSite: {
        game::ChapterInteraction point{};
        label = game::GameLevels::Chapter().NearestInteraction(point) ? point.label : "현장 / 조사";
        break;
    }
    case InteractionTarget::ObservatorySignal:
    {
        const auto &arc = story::StoryArc::Get().State();
        label = (arc.chapter4Complete && arc.epilogueShelterVisited && arc.epilogueClinicVisited &&
                 arc.epilogueWorkshopVisited)
                    ? (arc.nereusSignalReceived ? "신호 콘솔 / 네레우스 수신 기록 재생"
                                                : "신호 콘솔 / 17일 수신 기록 정리")
                    : (w.gateSignalHeard ? "신호 콘솔 / 보관 기록 확인" : "신호 콘솔 / 기록 수신");
        break;
    }
    case InteractionTarget::ObservatoryExit:
        label = "승강기 / 도시로 내려가기";
        break;
    case InteractionTarget::GateBeacon:
        label = "전망대 / 올라가기";
        break;
    case InteractionTarget::ApartmentExit:
        label = "현관 / 나가기";
        break;
    case InteractionTarget::HomeEntrance:
        label = "집 / 들어가기";
        break;
    case InteractionTarget::PowerRelay:
        label = "전력 장치 / 확인";
        break;
    case InteractionTarget::FacilityEntrance:
    {
        static std::string entranceLabel;
        entranceLabel = w.nearbyFacilityEntrance.label + " / 들어가기";
        label = entranceLabel.c_str();
        break;
    }
    case InteractionTarget::FacilityExit:
        label = "출입문 / 거리로 나가기";
        break;
    case InteractionTarget::FacilityNpc:
        switch (w.facilityInterior.kind)
        {
        case UrbanFacilityKind::Market:
            label = "보급 상인 / 대화 및 거래";
            break;
        case UrbanFacilityKind::Clinic:
            label = "의무관 / 대화 및 치료 · 보급";
            break;
        case UrbanFacilityKind::Workshop:
            label = "정비사 / 대화 및 정비 · 제작";
            break;
        case UrbanFacilityKind::CivicHall:
            label = "행정관 / 대화 및 치안 · 의뢰";
            break;
        default:
            label = "시설 담당자 / 대화";
            break;
        }
        break;
    case InteractionTarget::StoryOperationNode:
    {
        static std::string opLabel;
        opLabel = story::StoryArc::Get().NearbyInteractionLabel(w.playerPosition);
        label = opLabel.empty() ? "상호작용" : opLabel.c_str();
        break;
    }
    case InteractionTarget::StoryOperationPortal:
        label = "N-0 외곽 작전 구역 / 진입";
        break;
    case InteractionTarget::StorySafetyLine:
        label = "헤일로 동부 소실 구획 안전선 / 확인";
        break;
    case InteractionTarget::StoryShelterLamp:
        label = story::StoryArc::Get().State().shelterLampOn ? "대피소 휴대 조명 / 끄기" : "대피소 휴대 조명 / 켜기";
        break;
    case InteractionTarget::StoryEpilogueClinic:
        label = "임시 의료소 / 구조 탐사대원 3명 확인";
        break;
    case InteractionTarget::StoryEpilogueWorkshop:
        label = "정비소 · 복구팀 / 카인과 대화";
        break;
    case InteractionTarget::WorldEncounter:
    {
        static std::string encLabel;
        encLabel = encounters::WorldEncounters::Get().NearbyInteractionLabel();
        label = encLabel.empty() ? "현장 인카운터" : encLabel.c_str();
        break;
    }
    case InteractionTarget::FacilityServicePrimary:
        switch (w.facilityInterior.kind)
        {
        case UrbanFacilityKind::Market:
            label = "보급 카운터 / 회복 키트 구매 (40 Cr)";
            break;
        case UrbanFacilityKind::Clinic:
            label = "진료대 / 응급 치료 (30 Cr)";
            break;
        case UrbanFacilityKind::Workshop:
            label = "정비 콘솔 / 공용차·비행체 정비 (고철 1 / 35 Cr)";
            break;
        case UrbanFacilityKind::CivicHall:
            label = "행정 · 치안 창구 / 수배 정산";
            break;
        default:
            label = "시설 카운터 / 이용";
            break;
        }
        break;
    case InteractionTarget::FacilityServiceSecondary:
        switch (w.facilityInterior.kind)
        {
        case UrbanFacilityKind::Market:
            label = "고철 매입 단말 / 고철 판매 (+35 Cr)";
            break;
        case UrbanFacilityKind::Clinic:
            label = "구급 보급함 / 회복 키트 보급 (35 Cr)";
            break;
        case UrbanFacilityKind::Workshop:
            label = "개조 작업대 / 고철로 회복 키트 제작";
            break;
        case UrbanFacilityKind::CivicHall:
            label = "지역 의뢰 게시판 / 조사 기록 확인";
            break;
        default:
            label = "보조 단말 / 이용";
            break;
        }
        break;
    default:
        break;
    }
    Hud::DrawInteraction(label);
}

// ============================================================================
// Hud
// ============================================================================
std::vector<std::vector<std::string>> Hud::TextPages(const std::string &text, float width, int fontSize,
                                                     size_t linesPerPage)
{
    const auto rows = Dialogue::Wrap(text, width, fontSize);
    linesPerPage = (std::max)(size_t(1), linesPerPage);
    std::vector<std::vector<std::string>> pages;
    for (size_t start = 0; start < rows.size(); start += linesPerPage)
    {
        const auto end = (std::min)(rows.size(), start + linesPerPage);
        pages.emplace_back(rows.begin() + start, rows.begin() + end);
    }
    return pages;
}
void Hud::DrawNoticePanel()
{
    const auto &w = Game::Get().GetState();
    if (w.noticeSecondsRemaining <= 0 || w.weaponWheel.visible || Dialogue::Active() || w.journalVisible ||
        w.dialogueHistoryVisible || w.ui.screen != UiScreen::None)
    {
        return;
    }
    const bool isWarningNotice = w.noticeText.find("추락") != std::string::npos ||
                                 w.noticeText.find("파괴") != std::string::npos ||
                                 w.noticeText.find("피해") != std::string::npos ||
                                 w.noticeText.find("위험") != std::string::npos;
    const bool isSecurityNotice = w.noticeText.find("수배") != std::string::npos ||
                                  w.noticeText.find("투항") != std::string::npos ||
                                  w.noticeText.find("벌금") != std::string::npos ||
                                  w.noticeText.find("보안") != std::string::npos;
    const auto pages = TextPages(w.noticeText, 294, 16, 4);
    const float secondsPerPage = (std::max)(1.f, w.noticeDurationSeconds / pages.size());
    const auto page = (std::min)(pages.size() - 1,
                                 size_t((std::max)(0.f, w.noticeDurationSeconds - w.noticeSecondsRemaining) /
                                        secondsPerPage));
    const auto &rows = pages[page];
    const float cardH = 34.f + float(rows.size()) * 24.f + (pages.size() > 1 ? 22.f : 0.f);
    const ColorRGBA accent = isWarningNotice    ? ColorRGBA(1.f, .64f, .32f)
                             : isSecurityNotice ? ColorRGBA(1.f, .44f, .36f)
                                                : ColorRGBA(.36f, .90f, .80f);
    HudShift shift(WorldCamera::RightOffsetX());
    HudCard(902.f, 306.f, 358.f, cardH, accent);
    Icon(isWarningNotice ? "warning" : isSecurityNotice ? "security" : "check", 918, 322, 22, true,
         isWarningNotice ? ColorRGBA(1, .78f, .4f) : ColorRGBA(1, 1, 1));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        UiText(954, 342 + float(i) * 24, rows[i], 16, kUiInk, true);
    }
    if (pages.size() > 1)
    {
        UiText(954, 346 + float(rows.size()) * 24,
               std::to_string(page + 1) + " / " + std::to_string(pages.size()) + " · 자동 다음", 14,
               {.72f, .86f, .84f}, true);
    }
}

void Hud::DrawDialogue()
{
    const auto &w = Game::Get().GetState();
    const auto &d = w.dialogue;
    if (!d.active || w.journalVisible || w.dialogueHistoryVisible || w.ui.screen != UiScreen::None ||
        HospitalRecovery::Active())
    {
        return;
    }
    auto layout = LayoutDialogue();
    HudShift shift(WorldCamera::CenterOffsetX());
    Panel(208, layout.top, 864, layout.height);
    Icon(d.pages[d.page].channel == "무전" ? "radio" : "dialogue", 232, layout.top + 25, 24);
    for (size_t i = 0; i < layout.header.size(); ++i)
    {
        UiText(270, layout.top + 45 + i * 24, layout.header[i], 18, {.57f, .91f, .86f});
    }
    UiText(1048, layout.top + 43, std::to_string(d.page + 1) + " / " + std::to_string(d.pages.size()), 14,
           kUiInk, false, true);
    for (size_t i = 0; i < layout.body.size(); ++i)
    {
        UiText(232, layout.bodyTop + i * 32, layout.body[i], 20);
    }
    for (size_t i = 0; i < layout.controls.size(); ++i)
    {
        Button(layout.controls[i], int(i));
    }
    UiText(232, 677,
           Dialogue::Danger() ? "무전 · 전투 계속 / 안전한 곳에서 선택" : "H 대사 이력 · J 조사 기록", 14,
           {.58f, .75f, .75f});
}

void Hud::DrawInteraction(const std::string &label)
{
    const auto &w = Game::Get().GetState();
    if (Dialogue::Active() || w.mapVisible || w.journalVisible || w.dialogueHistoryVisible ||
        w.ui.screen != UiScreen::None)
    {
        return;
    }
    auto rows = Dialogue::Wrap(label, 286, 17);
    const float h = (std::max)(50.f, 24.f + float(rows.size()) * 22.f);
    HudShift shift(WorldCamera::CenterOffsetX());
    Panel(450, 514, 380, h);
    Drawing::DrawRectangle(456.f, 522.f, 3.f, (std::max)(8.f, h - 16.f), {.38f, .92f, .86f, .92f});
    for (size_t i = 0; i < rows.size(); ++i)
    {
        UiText(470, 544 + float(i) * 22, rows[i], 17);
    }
    const float keyY = 514.f + (h - 28.f) * .5f;
    Sprite("Components/keycap", 788, keyY, 28, 28);
    UiText(797, keyY + 20.f, "E", 17);
}
void Hud::DrawObjective(const std::string &objective, const std::string &context)
{
    const auto *site = ActivityJournal::TrackedCommunity();
    const bool submit = site && site->task == CommunityTaskProgress::Measured;
    const bool hold = submit && game::GameLevels::Mine().GetState().player.credits >
                                    game::PlayerStats::kMaximumCredits - CommunityTaskProgress::RewardCredits;
    const auto pages = TextPages(objective, 412, 18, 2);
    const auto page = size_t((std::max)(0.f, Game::Get().GetState().playTimeSeconds) / 5) % pages.size();
    const auto &rows = pages[page];
    const auto detail = context + (pages.size() > 1 ? " · " + std::to_string(page + 1) + "/" +
                                                          std::to_string(pages.size()) + " · J 전체 목표"
                                                    : "");
    const auto detailPages = TextPages(detail, 412, 14, 2);
    const size_t detailPage =
        size_t((std::max)(0, Game::Get().GetState().lastUpdateMilliseconds) / 6000) % detailPages.size();
    const float cardH = 38.f + float(rows.size()) * 26.f + float(detailPages[detailPage].size()) * 21.f;
    const ColorRGBA accent = hold ? ColorRGBA(1.f, .76f, .36f)
                             : submit ? ColorRGBA(.42f, .95f, .72f)
                                      : ColorRGBA(.36f, .88f, .88f);
    HudCard(20.f, 20.f, 472.f, cardH, accent);
    Icon(hold ? "activity_payment_hold" : submit ? "activity_submit" : "activity_track", 32, 36, 24, true);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        UiText(68, 57 + i * 26, rows[i], 18, kUiInk, true);
    }
    for (size_t i = 0; i < detailPages[detailPage].size(); ++i)
    {
        UiText(68, 58 + float(rows.size()) * 26 + float(i) * 21, detailPages[detailPage][i], 14,
               {.68f, .83f, .83f}, true);
    }
}

void Hud::DrawActivityObjective(const std::string &context)
{
    const auto goal = ActivityJournal::Current();
    if (goal.title.empty())
    {
        DrawObjective(goal.action, context);
        return;
    }
    const auto &w = Game::Get().GetState();
    const auto *site = ActivityJournal::TrackedCommunity();
    const bool submit = site && site->task == CommunityTaskProgress::Measured;
    const bool hold = submit && game::GameLevels::Mine().GetState().player.credits >
                                    game::PlayerStats::kMaximumCredits - CommunityTaskProgress::RewardCredits;
    const ColorRGBA accent = hold ? ColorRGBA(1.f, .76f, .36f)
                             : submit ? ColorRGBA(.42f, .95f, .72f)
                                      : ColorRGBA(.36f, .88f, .88f);
    HudCard(20.f, 20.f, 472.f, 108.f, accent);
    Icon(hold ? "activity_payment_hold" : submit ? "activity_submit" : "activity_track", 32, 36, 24, true);
    // Fixed baselines leave the existing city/mine progress HUD below untouched. Every long row cycles.
    const auto title = TextPages(goal.title, 412, 18, 1);
    const auto action = TextPages(goal.action, 412, 18, 1);
    const auto detail =
        TextPages(goal.location + " · J 기록 · V 경로 안내" + (hold ? " · 소지 한도 / 현장 정산 보류" : std::string()),
                  412, 14, 1);
    const size_t tick = size_t((std::max)(0, w.lastUpdateMilliseconds) / 5000);
    UiText(68, 57, title[tick % title.size()].front(), 18, {.78f, .96f, .94f}, true);
    UiText(68, 86, action[tick % action.size()].front(), 17, kUiInk, true);
    UiText(68, 113, detail[tick % detail.size()].front(), 14, {.68f, .83f, .83f}, true);
}

void CityHud::Draw()
{
    const auto &w = Game::Get().GetState();
    std::string objective;
    auto &chapter = game::GameLevels::Chapter();
    if (story::StoryArc::Get().IsActTwoUnlocked())
    {
        objective = story::StoryArc::Get().CurrentObjectiveAction();
    }
    else if (chapter.IsAvailable())
    {
        objective = chapter.Objective();
    }
    else if (w.gateSignalHeard && w.hasDescendedObservatory)
    {
        objective = game::GameLevels::Mine().GetState().quest.GetObjective();
    }
    else
    {
        switch (w.mainQuestStage)
        {
        case MainQuestStage::ReadHomeTerminal:
            objective = "집 단말에서 리아의 메시지 확인";
            break;
        case MainQuestStage::MeetLia:
            objective = "집 밖에서 리아와 대화";
            break;
        case MainQuestStage::EnterObservatory:
            objective = "전망대 승강기로 올라가기";
            break;
        case MainQuestStage::ReadGateSignal:
            objective = "상층 콘솔에서 신호 측정";
            break;
        case MainQuestStage::DescendObservatory:
            objective = "승강기로 도시에 내려가기";
            break;
        default:
            objective = "헤일로 광장에서 리아와 조사 경로 확인";
            break;
        }
    }
    int city = Overworld::CityAt(w.playerPosition);
    std::string area = w.isInsideHome      ? "헤일로 / 집"
                       : w.isInObservatory ? "헤일로 / 전망대"
                       : city >= 0         ? Overworld::Cities()[city].name
                                           : "도시 연결 구역";
    if (w.surfaceExploration)
    {
        Hud::DrawActivityObjective(area + " · J 조사 기록");
    }
    else
    {
        Hud::DrawObjective(objective, area + " · J 조사 기록");
    }
    if (!w.surfaceExploration && w.isSideQuestAccepted && !w.isSideQuestComplete)
    {
        Icon("objective", 36, 139, 16, true);
        UiText(68, 153, w.isPowerRestored ? "마라에게 전력 복구 보고" : "선택 · 마라의 전력 장치 복구", 15,
               {.94f, .77f, .44f}, true);
    }
    if (w.isInsideHome)
    {
        const auto &res = w.residence;
        const float rowY =
            (!w.surfaceExploration && w.isSideQuestAccepted && !w.isSideQuestComplete) ? 182.f : 153.f;
        const std::string status = "거주지 거점 · 금고 " + std::to_string(res.stashCredits) + " Cr · 킷 " +
                                   std::to_string(res.stashRecoveryKits) + " · 고철 " +
                                   std::to_string(res.stashSalvage) + " · 방어 " +
                                   std::to_string(res.armorTier) + "/3 · 무기 " +
                                   std::to_string(res.weaponTier) + "/3";
        UiText(68, rowY, status, 14, {.65f, .9f, .81f}, true);
    }
    MapHud::DrawMinimap();
    DrawInteractionPrompt();
    if (!w.isInsideHome && !w.isInObservatory && !w.isBikeMounted)
    {
        int crossing = Overworld::Traffic().NearbyCrossing(w.playerPosition);
        if (crossing >= 0)
        {
            bool walk = Overworld::Traffic().PedestrianGreen(
                crossing, Overworld::Traffic().CrossingRoad(crossing, w.playerPosition));
            Icon(walk ? "walk" : "stop", 348, 711, 22);
            UiText(380, 732, walk ? "횡단 가능" : "보행 신호 대기", 16,
                   walk ? ColorRGBA(.59f, .95f, .8f) : ColorRGBA(1, .74f, .39f), true);
        }
    }
}

void MineHud::DrawScanner()
{
    const auto &mine = game::GameLevels::Mine().GetState();
    float progress = mine.scannerSeconds > 0    ? mine.scannerRadius / 42.f
                     : mine.scannerCooldown > 0 ? 1 - mine.scannerCooldown / 6.f
                                                : 1;
    progress = Game::Clamp(progress, 0, 1);
    {
        HudShift shift(WorldCamera::RightOffsetX());
        Drawing::DrawEllipse(976, 656, 24, 24, {.015f, .03f, .035f, .8f});
        for (int i = 0; i < 60; ++i)
        {
            float a = -kPi * .5f + i * kPi / 30, b = a + kPi / 30;
            ColorRGBA color = i < progress * 60 ? ColorRGBA(.63f, .85f, .97f) : ColorRGBA(.15f, .22f, .27f);
            Drawing::DrawLine({976 + std::cos(a) * 21, 656 + std::sin(a) * 21},
                              {976 + std::cos(b) * 21, 656 + std::sin(b) * 21}, {0, 0, 0, .9f}, 7);
            Drawing::DrawLine({976 + std::cos(a) * 21, 656 + std::sin(a) * 21},
                              {976 + std::cos(b) * 21, 656 + std::sin(b) * 21}, color, 4);
        }
        UiText(971, 663, "F", 17, kUiInk, true);
        std::string status = mine.scannerSeconds > 0    ? "구조 신호 스캔 중"
                             : mine.scannerCooldown > 0 ? "스캐너 충전 중"
                                                        : "구조 신호 스캔";
        UiText(1010, 652, status, 15, {.68f, .87f, .98f}, true);
        if (!mine.scannerContacts.empty())
        {
            UiText(1010, 682, "근사 신호 감지", 14, {.68f, .87f, .98f}, true);
        }
    }
    const auto &world = Game::Get().GetState();
    Vector2 player =
        WorldCamera::ToViewport(Drawing::WorldToScreen(world.playerPosition.x, world.playerPosition.y, 20));
    for (const auto &contact : mine.scannerContacts)
    {
        if (contact.group >= mine.rescueGroups.size() || mine.rescueGroups[contact.group].rescued ||
            contact.secondsRemaining <= 0)
        {
            continue;
        }
        // Use the camera's isometric bearing for screen-edge arrows, independently of the fixed schematic
        // rotation.
        Vector2 target =
            WorldCamera::ToViewport(Drawing::WorldToScreen(contact.position.x, contact.position.y, 20));
        Vector2 direction = {target.x - player.x, target.y - player.y};
        float length = Game::Distance(direction, {0, 0});
        if (length < .001f)
        {
            continue;
        }
        direction.x /= length;
        direction.y /= length;
        constexpr Vector2 center = {kCanvasWidth * .5f, kCanvasHeight * .5f};
        float rayX = std::fabs(direction.x) > .0001f ? (center.x - 35) / std::fabs(direction.x) : 1.e8f;
        float rayY = std::fabs(direction.y) > .0001f ? (center.y - 52) / std::fabs(direction.y) : 1.e8f;
        float ray = (std::min)(rayX, rayY);
        Vector2 position = {center.x + direction.x * ray, center.y + direction.y * ray};
        float alpha = (std::min)(1.f, contact.secondsRemaining / .7f);
        Drawing::DrawEllipse(position.x, position.y, 20, 20, {.015f, .08f, .075f, .9f * alpha});
        DirectionArrow(position, std::atan2(direction.y, direction.x), 13, {.4f, 1, .72f, alpha}, true);
        char distance[64];
        std::snprintf(distance, sizeof(distance), "구조 약 %.0f m",
                      Game::Distance(world.playerPosition, contact.position) *
                          models::WorldScale::metersPerUnit);
        float textX = Game::Clamp(position.x - 32, 12, kCanvasWidth - 104.f);
        float textY = position.y < 100 ? position.y + 36 : position.y - 24;
        Drawing::DrawLabel(textX, textY, distance, {.65f, 1, .8f, alpha});
    }
    for (const auto &ping : mine.resourcePings)
    {
        Vector2 target = WorldCamera::ToViewport(Drawing::WorldToScreen(ping.position.x, ping.position.y, 16));
        Vector2 direction = {target.x - player.x, target.y - player.y};
        float length = Game::Distance(direction, {0, 0});
        if (length < 120.f)
        {
            continue;
        }
        direction.x /= length;
        direction.y /= length;
        constexpr Vector2 center = {kCanvasWidth * .5f, kCanvasHeight * .5f};
        float rayX = std::fabs(direction.x) > .0001f ? (center.x - 42) / std::fabs(direction.x) : 1.e8f;
        float rayY = std::fabs(direction.y) > .0001f ? (center.y - 58) / std::fabs(direction.y) : 1.e8f;
        float ray = (std::min)(rayX, rayY);
        Vector2 position = {center.x + direction.x * ray, center.y + direction.y * ray};
        float alpha = (std::min)(1.f, ping.secondsRemaining / 1.2f);
        const ColorRGBA col = ping.isElite ? ColorRGBA(1.f, .44f, .28f, alpha)
                              : ping.kind == game::MineNodeKind::PrismCrystal ? ColorRGBA(.46f, .94f, 1.f, alpha)
                                                                              : ColorRGBA(1.f, .78f, .34f, alpha);
        DirectionArrow(position, std::atan2(direction.y, direction.x), 10, col, true);
        char label[64];
        std::snprintf(label, sizeof(label), "%s %.0f m",
                      ping.isElite                                    ? "정예"
                      : ping.kind == game::MineNodeKind::PrismCrystal ? "수정맥"
                      : ping.kind == game::MineNodeKind::SupplyCrate  ? "보급함"
                                                                      : "광맥",
                      Game::Distance(world.playerPosition, ping.position) * models::WorldScale::metersPerUnit);
        float textX = Game::Clamp(position.x - 28, 12, kCanvasWidth - 96.f);
        float textY = position.y < 100 ? position.y + 30 : position.y - 22;
        Drawing::DrawLabel(textX, textY, label, col);
    }
}

// ============================================================================
// MineHud
// ============================================================================
void MineHud::Draw()
{
    auto &level = game::GameLevels::Mine();
    const auto &s = level.GetState();
    if (Game::Get().GetState().surfaceExploration)
    {
        Hud::DrawActivityObjective("광산 · J 기록");
    }
    else
    {
        Hud::DrawObjective(s.quest.GetObjective(), "광산 / 구조 신호 조사 · J 기록");
    }
    MapHud::DrawMinimap();
    DrawScanner();
    // Mine hunting & farming status strip directly below the objective card.
    {
        std::string strip = "사냥 " + std::to_string(s.kills) + " · 광맥 " + std::to_string(s.veinsHarvested) +
                            "/" + std::to_string(s.resourceNodes.size()) + " · 정예 " +
                            std::to_string(s.elitesDefeated) + "/3";
        if (s.huntStreak >= 2)
        {
            strip += "  |  연속 사냥 x" + std::to_string(s.huntStreak);
        }
        HudCard(20.f, 134.f, 472.f, 28.f,
                s.huntStreak >= 3 ? ColorRGBA(1.f, .78f, .32f) : ColorRGBA(.42f, .88f, .80f), false);
        UiText(34.f, 153.f, strip, 13,
               s.huntStreak >= 3 ? ColorRGBA(1.f, .86f, .44f) : ColorRGBA(.74f, .92f, .88f), true);
    }
    if (!Dialogue::Active())
    {
        std::string hint;
        const std::string resLabel = level.NearbyResourceLabel();
        if (!resLabel.empty())
        {
            hint = resLabel;
        }
        if (Game::Get().HasCompanion(CompanionId::Ria, CompanionPlace::Mine) && level.IsNear(s.riaPosition))
        {
            hint = "리아 / 대화·응급 처치";
        }
        if (Game::Get().HasCompanion(CompanionId::Kain, CompanionPlace::Mine) &&
            level.IsNear(s.kain.position))
        {
            hint = "카인 / 대화·응급 처치";
        }
        for (const auto &group : s.rescueGroups)
        {
            if (!group.rescued && level.IsNear(group.position))
            {
                hint = "구조 대상 / 확인";
            }
        }
        for (auto p :
             {game::kPowerBypass, game::kRuinSignal, game::kObserverGlyph, game::kCoordinateTerminal})
        {
            if (level.IsNear(p))
            {
                hint = "조사 지점 / 확인";
            }
        }
        if (level.IsNear(s.entry))
        {
            hint = "광산 입구 / 나가기";
        }
        for (const auto &drop : s.drops)
        {
            if (level.IsNear(drop.position))
            {
                hint = "물자 / 회수";
            }
        }
        if (!hint.empty())
        {
            Hud::DrawInteraction(hint);
        }
    }
}

void ChapterHud::Draw()
{
    auto &level = game::GameLevels::Chapter();
    if (Game::Get().GetState().surfaceExploration)
    {
        Hud::DrawActivityObjective("빛나는 개척지 · J 기록");
    }
    else
    {
        Hud::DrawObjective(level.Objective(), "1장 / 빛나는 개척지 · J 조사 기록");
    }
    UiText(68, 162, "현장 조사 " + std::to_string(level.CompletedSites()) + " / 3", 15, {.67f, .83f, .8f},
           true);
    MapHud::DrawMinimap();
    game::ChapterInteraction point{};
    if (level.NearestInteraction(point))
    {
        Hud::DrawInteraction(point.action == game::ChapterAction::ConvoySupply ? "운송대 보급 / 교환"
                                                                               : point.label);
    }
}

namespace
{
struct ActivityRow
{
    int choice = -1;
    MissionId mission = MissionId::Count;
    std::string region, title, subtitle, status, icon;
    bool tracked = false, eligible = false, trackable = false, paymentHold = false;
};
bool LocalActivities()
{
    auto &g = Game::Get();
    return g.GetState().journalLocalActivities || !g.IsStoryWorld();
}
std::vector<ActivityRow> ActivityRows()
{
    auto &g = Game::Get();
    const auto &w = g.GetState();
    std::vector<ActivityRow> rows;
    if (!LocalActivities())
    {
        const auto tracked = ActivityJournal::Current().mission;
        for (int i = 0; i < int(MissionId::Count); ++i)
        {
            ActivityRow row;
            row.choice = i;
            row.mission = MissionId(i);
            row.title = QuestParticipation::Name(row.mission);
            const auto status = w.missions.Status(row.mission);
            row.eligible = g.MissionEligible(row.mission);
            row.trackable = status == MissionStatus::Active;
            row.tracked = row.trackable && tracked == row.mission;
            row.status = status == MissionStatus::Active      ? "진행 중"
                         : status == MissionStatus::Paused    ? "보류"
                         : status == MissionStatus::Completed ? "완료"
                         : row.eligible                       ? "미수락"
                                                              : "선행 조사 필요";
            row.icon = status == MissionStatus::Active      ? "activity_active"
                       : status == MissionStatus::Paused    ? "activity_paused"
                       : status == MissionStatus::Completed ? "activity_complete"
                       : row.eligible                       ? "activity_offer"
                                                            : "activity_prerequisite";
            rows.push_back(std::move(row));
        }
        return rows;
    }
    const auto entries = ActivityJournal::CommunityEntries();
    const int pages = (std::max)(1, int((entries.size() + ActivityJournal::LocalEntriesPerPage - 1) /
                                        ActivityJournal::LocalEntriesPerPage));
    const int page = std::clamp(w.journalActivityPage, 0, pages - 1);
    const auto *community = g.SurfaceCommunity();
    for (int i = page * ActivityJournal::LocalEntriesPerPage;
         community && i < int(entries.size()) && i < (page + 1) * ActivityJournal::LocalEntriesPerPage; ++i)
    {
        const auto index = entries[size_t(i)];
        const auto &site = community->State().sites[index];
        ActivityRow row;
        row.choice = int(index);
        row.region = site.regionId;
        row.title = g.Surface()->Stream().Snapshot()->planet->Region(site.regionId).definition.name;
        row.subtitle = PlanetCommunity::TaskName(site);
        row.trackable = row.eligible = true;
        row.tracked = ActivityJournal::TrackedCommunity() == &site;
        row.paymentHold = site.task == CommunityTaskProgress::Measured &&
                          game::GameLevels::Mine().GetState().player.credits >
                              game::PlayerStats::kMaximumCredits - CommunityTaskProgress::RewardCredits;
        row.status = row.paymentHold                                ? "정산 보류 · 소지 한도"
                     : site.task == CommunityTaskProgress::Measured ? "측정 기록 제출 대기"
                                                                    : "측정 중";
        row.icon = row.paymentHold                                ? "activity_payment_hold"
                   : site.task == CommunityTaskProgress::Measured ? "activity_submit"
                                                                  : "activity_measure";
        rows.push_back(std::move(row));
    }
    return rows;
}
size_t SelectedActivity(const std::vector<ActivityRow> &rows)
{
    const auto &ui = Game::Get().GetState().ui;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (LocalActivities() ? !ui.activityRegion.empty() && rows[i].region == ui.activityRegion
                              : rows[i].mission == ui.activityMission)
        {
            return i;
        }
    }
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].tracked)
        {
            return i;
        }
    }
    return 0;
}
struct ActivityDetail
{
    std::vector<std::string> title;
    std::vector<std::vector<std::string>> pages;
    float y = 310;
};
ActivityDetail ActivityBody(const std::vector<ActivityRow> &rows)
{
    ActivityDetail result;
    if (rows.empty())
    {
        result.title = {"수락한 지역 의뢰가 없습니다."};
        result.y = 338;
        result.pages = Hud::TextPages("도시·전초기지·광구·고대 유적·야생·무법 지대의 현장 접수 단말에서 의뢰를 확인할 수 있습니다.\n\n"
                                      "자유 탐험과 이야기 임무는 계속할 수 있습니다.",
                                      560, 20, 10);
        return result;
    }
    auto &g = Game::Get();
    const auto &w = g.GetState();
    const auto &row = rows[SelectedActivity(rows)];
    result.title = Dialogue::Wrap(row.title, 560, 24);
    const auto headingLines = (std::min)(size_t(3), result.title.size());
    // Very long names remain available in the paged body, rather than extending beyond the frame.
    const auto headingPages = Hud::TextPages(row.title, 560, 24, 3);
    result.title = headingPages[size_t((std::max)(0, w.lastUpdateMilliseconds) / 4000) % headingPages.size()];
    result.y = 310 + float(headingLines) * 28;
    std::string body = (headingPages.size() > 1 ? row.title + "\n" : "") + row.status +
                       (row.tracked ? " · HUD 추적 중" : "") + "\n\n";
    if (row.mission != MissionId::Count)
    {
        const auto status = w.missions.Status(row.mission);
        if (status == MissionStatus::Completed)
        {
            body += "임무를 완료했습니다. 현장 기록은 보존됩니다.\n";
        }
        else
        {
            body += std::string("다음 행동 / ") + g.MissionObjective(row.mission) + "\n";
            if (const auto *reason = g.MissionRestriction(row.mission))
            {
                body += std::string(reason) + "\n";
            }
            if (status == MissionStatus::NotAccepted)
            {
                body += "임무는 현장의 제안자에게 직접 방문해 수락합니다. [제안자 안내] 또는 월드에서 [N] 키로 3D 경로를 표시하세요.\n";
            }
            else if (status != MissionStatus::Active)
            {
                body += "보류 중인 임무는 재개하거나 [N] 경로 안내로 현장을 찾아갈 수 있습니다.\n";
            }
        }
        const auto records = StoryJournal::Collect(w, game::GameLevels::Mine().GetState(),
                                                   game::GameLevels::Chapter().GetState());
        for (const auto &record : records)
        {
            const bool belongs = row.mission == MissionId::Signal
                                     ? record.id.find("home/") == 0 || record.id.find("observatory/") == 0
                                 : row.mission == MissionId::Mine ? record.id.find("mine/") == 0
                                 : row.mission == MissionId::Mara ? record.id.find("mara/") == 0
                                                                  : record.id.find("frontier/") == 0;
            if (!belongs)
            {
                continue;
            }
            body += "\n" + record.title + "\n확인한 사실 / " + record.fact + "\n";
            if (!record.unresolved.empty())
            {
                body += "남은 의문 / " + record.unresolved + "\n";
            }
        }
    }
    else
    {
        const auto &site = g.SurfaceCommunity()->State().sites[size_t(row.choice)];
        body += row.subtitle + "\n\n다음 행동 / " +
                (site.task == CommunityTaskProgress::Accepted
                     ? std::string("현장 센서에서 측정 기록 확보 [E]")
                     : std::string("현장 접수 단말에 측정 기록 제출 [E]"));
        if (row.paymentHold)
        {
            body += "\n\n크레딧 소지 한도로 정산이 보류됩니다. 측정 기록과 기존 재화는 유지되며, 여유가 "
                    "생기면 같은 단말에서 다시 제출하세요.";
        }
        body += "\n\n수락·중단·기록 제출과 보상 수령은 현장에서 진행합니다. 지도 보기는 직접 지정한 목적지를 "
                "바꾸지 않습니다.";
    }
    const size_t lines = size_t((std::max)(1, int((602 - result.y) / 26)));
    result.pages = Hud::TextPages(body, 560, 20, lines);
    return result;
}
} // namespace

std::vector<UiControl> ActivityJournalUi::Controls()
{
    auto &g = Game::Get();
    const auto &w = g.GetState();
    const bool local = LocalActivities(), unlocked = !Dialogue::Active() && !w.dialogue.suspended;
    const auto rows = ActivityRows();
    std::vector<UiControl> controls = {
        {UiAction::ActivityTab, {96, 179}, {168, 38}, "이야기 임무", g.IsStoryWorld(), 0},
        {UiAction::ActivityTab, {276, 179}, {168, 38}, "지역 의뢰", true, 1}};
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto &row = rows[i];
        const float y = 232 + float(i) * (local ? 122 : 94);
        controls.push_back({local ? UiAction::TrackCommunity : UiAction::SelectActivity,
                            {96, y},
                            {472, local ? 76.f : 42.f},
                            local ? row.title + "\n" + row.subtitle : row.title,
                            true,
                            row.choice});
        if (!local)
        {
            const auto status = w.missions.Status(row.mission);
            controls.push_back({UiAction::Mission,
                                {284, y + 46},
                                {116, 32},
                                status == MissionStatus::Active      ? "진행 보류"
                                : status == MissionStatus::Paused    ? "재개"
                                : status == MissionStatus::Completed ? "완료"
                                : row.eligible                       ? "제안자 안내"
                                                                     : "수락 불가",
                                unlocked && row.eligible && status != MissionStatus::Completed,
                                row.choice});
            controls.push_back({UiAction::TrackMission,
                                {412, y + 46},
                                {134, 32},
                                row.tracked ? "추적 중" : "목표 추적",
                                unlocked && row.trackable,
                                row.choice});
        }
        else
        {
            controls.push_back(
                {UiAction::ViewActivity, {412, y + 78}, {134, 32}, "지도 보기", unlocked, row.choice});
        }
    }
    const auto entries = ActivityJournal::CommunityEntries();
    const int pages = local ? (std::max)(1, int((entries.size() + ActivityJournal::LocalEntriesPerPage - 1) /
                                                ActivityJournal::LocalEntriesPerPage))
                            : 1;
    const int page = local ? std::clamp(w.journalActivityPage, 0, pages - 1) : 0;
    controls.push_back({UiAction::ActivityPage, {392, 629}, {78, 34}, "이전", local && page > 0, -1});
    controls.push_back({UiAction::ActivityPage, {478, 629}, {78, 34}, "다음", local && page + 1 < pages, 1});
    const auto detail = ActivityBody(rows);
    const int bodyPage = std::clamp(w.ui.activityDetailPage, 0, int(detail.pages.size()) - 1);
    controls.push_back({UiAction::ActivityDetailPage, {944, 629}, {100, 34}, "이전", bodyPage > 0, -1});
    controls.push_back({UiAction::ActivityDetailPage,
                        {1056, 629},
                        {100, 34},
                        "다음",
                        bodyPage + 1 < int(detail.pages.size()),
                        1});
    controls.push_back({UiAction::ViewActivity,
                        {624, 629},
                        {134, 34},
                        "지도 보기",
                        unlocked && !rows.empty() && rows[SelectedActivity(rows)].trackable,
                        rows.empty() || !local ? -1 : rows[SelectedActivity(rows)].choice});
    controls.push_back({UiAction::Continue, {1032, 697}, {152, 36}, "닫기 · Esc"});
    return controls;
}
void ActivityJournalUi::Select(int choice)
{
    auto &w = Game::Get().GetState();
    const auto rows = ActivityRows();
    for (const auto &row : rows)
    {
        if (row.choice != choice)
        {
            continue;
        }
        w.ui.activityMission = row.mission;
        w.ui.activityRegion = row.region;
        w.ui.activityDetailPage = 0;
        return;
    }
}
void ActivityJournalUi::TurnPage(int direction)
{
    auto &ui = Game::Get().GetState().ui;
    const auto detail = ActivityBody(ActivityRows());
    ui.activityDetailPage = std::clamp(ui.activityDetailPage + direction, 0, int(detail.pages.size()) - 1);
}
void ActivityJournalUi::Draw()
{
    const auto &w = Game::Get().GetState();
    const bool local = LocalActivities();
    const auto rows = ActivityRows();
    const auto selected = SelectedActivity(rows);
    Panel(64, 40, 1152, 720);
    UiText(96, 80, "에르마 · 활동 기록", 16, {.65f, .9f, .81f});
    UiText(96, 120, "조사 기록", 28);
    UiText(96, 154, "현재 행성의 활동 · F5 저장 / F9 저장·불러오기", 16);
    UiText(624, 205, local ? "수락한 의뢰만 표시" : "참여와 추적은 별개", 16, {.65f, .9f, .81f});
    Drawing::DrawLine({590, 236}, {590, 658}, {.3f, .46f, .5f, .5f});
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto &row = rows[i];
        const float y = 232 + float(i) * (local ? 122 : 94), height = local ? 112.f : 86.f;
        renderqueue::Opacity(w.uiPreferences.background);
        Sprite("Components/panel_fill", 96, y, 472, height, {1, 1, 1}, 1, true);
        renderqueue::Opacity(w.uiPreferences.border);
        if (i == selected || row.tracked || row.paymentHold || w.ui.focus == 2 + int(i) * (local ? 2 : 3))
        {
            if (local)
            {
                Sprite(row.paymentHold ? "Components/activity_row_warning" : "Components/activity_row_focus",
                       96, y, 472, 112);
            }
            else
            {
                Sprite("Components/panel_border", 96, y, 472, height, {.65f, .9f, .81f}, 1, true);
                Drawing::DrawRectangle(96, y + 12, 3, height - 24, {.65f, .9f, .81f});
            }
        }
        renderqueue::Opacity(w.uiPreferences.content);
        const auto titles = Hud::TextPages(row.title, 424, local ? 18 : 22, local ? 2 : 1);
        const auto &title = titles[size_t((std::max)(0, w.lastUpdateMilliseconds) / 4000) % titles.size()];
        for (size_t line = 0; line < title.size(); ++line)
        {
            UiText(118, y + 26 + float(line) * 22, title[line], local ? 18 : 22);
        }
        if (local)
        {
            UiText(118, y + 70, row.subtitle, 16);
        }
        const float baseline = y + (local ? 98 : 70);
        Icon(row.icon, 118, baseline - 15, 16);
        UiText(144, baseline, row.status, 16,
               row.paymentHold ? ColorRGBA(1, .86f, .6f) : ColorRGBA(.65f, .9f, .81f));
        if (row.tracked)
        {
            Icon("activity_track", 538, y + 10, 18);
        }
    }
    const auto detail = ActivityBody(rows);
    UiText(624, 258,
           rows.empty()             ? "지역 의뢰"
           : rows[selected].tracked ? "현재 추적 중"
                                    : "선택한 기록",
           16, {.65f, .9f, .81f});
    for (size_t i = 0; i < detail.title.size(); ++i)
    {
        UiText(624, 294 + float(i) * 28, detail.title[i], 24);
    }
    const size_t bodyPage = size_t(std::clamp(w.ui.activityDetailPage, 0, int(detail.pages.size()) - 1));
    for (size_t i = 0; i < detail.pages[bodyPage].size(); ++i)
    {
        UiText(624, detail.y + float(i) * 26, detail.pages[bodyPage][i], 20);
    }
    const int count = local ? (std::max)(1, int((ActivityJournal::CommunityEntries().size() +
                                                 ActivityJournal::LocalEntriesPerPage - 1) /
                                                ActivityJournal::LocalEntriesPerPage))
                            : 1;
    UiText(96, 651,
           "목록 " + std::to_string(local ? std::clamp(w.journalActivityPage, 0, count - 1) + 1 : 1) + " / " +
               std::to_string(count),
           16);
    UiText(776, 651, "기록 " + std::to_string(bodyPage + 1) + " / " + std::to_string(detail.pages.size()),
           16);
    Sprite("Components/activity_page_dot", 1168, 643, 8, 8);
    UiText(96, 722,
           Dialogue::Danger() ? "교전 중 · 기록을 봐도 시간은 흐릅니다."
                              : "현장 기록 유지 · 제출과 보고는 현장에서 진행",
           16, Dialogue::Danger() ? ColorRGBA(1, .86f, .6f) : kUiInk);
    const auto controls = Controls();
    for (size_t i = 0; i < controls.size(); ++i)
    {
        if (controls[i].action != UiAction::SelectActivity && controls[i].action != UiAction::TrackCommunity)
        {
            Button(controls[i], int(i));
        }
    }
}

void ChapterHud::DrawJournal()
{
    const auto &world = Game::Get().GetState();
    if (HospitalRecovery::Active())
    {
        return;
    }
    if (!world.journalVisible && !world.dialogueHistoryVisible)
    {
        return;
    }
    HudShift shift(WorldCamera::CenterOffsetX());
    if (world.surfaceExploration && world.journalVisible)
    {
        ActivityJournalUi::Draw();
        return;
    }
    std::vector<std::string> rows;
    if (world.journalVisible)
    {
        for (const auto &entry : Dialogue::Journal())
        {
            auto wrapped = Dialogue::Wrap(entry, 714, 18);
            rows.insert(rows.end(), wrapped.begin(), wrapped.end());
            rows.push_back("");
        }
    }
    else
    {
        for (const auto &entry : world.dialogue.history)
        {
            auto header = Dialogue::Wrap(entry.speaker + " · " + entry.channel, 714, 18);
            rows.insert(rows.end(), header.begin(), header.end());
            auto wrapped = Dialogue::Wrap(entry.text, 714, 18);
            rows.insert(rows.end(), wrapped.begin(), wrapped.end());
            rows.push_back("");
        }
        if (rows.empty())
        {
            rows.push_back("아직 읽은 대사가 없습니다.");
        }
    }
    constexpr size_t perPage = 15;
    size_t pages = (std::max)(size_t(1), (rows.size() + perPage - 1) / perPage);
    size_t offset = (std::min)(world.dialogue.historyOffset, pages - 1);
    size_t page = world.dialogueHistoryVisible ? pages - 1 - offset : offset;
    Panel(64, 48, 1152, 704);
    Icon(world.journalVisible ? "record" : "history", 96, 104, 26);
    UiText(136, 132, world.journalVisible ? "조사 기록" : "대사 이력", 28);
    UiText(96, 164,
           world.surfaceExploration ? "F5 월드 저장 · F9 저장/불러오기"
                                    : "현재 실행 기록 · 테스트 직행은 저장 미지원",
           18);
    UiText(454, 218, world.journalVisible ? "확인한 사실과 다음 행동" : "읽은 대사 / 전달 기록", 18,
           {.61f, .88f, .83f});
    if (world.surfaceExploration && world.journalVisible)
    {
        UiText(96, 622,
               Dialogue::Danger() ? "교전 중 · 기록을 봐도 시간은 흐릅니다."
                                  : "임무를 보류해도 현장 기록은 유지됩니다.",
               14, Dialogue::Danger() ? ColorRGBA(1, .78f, .4f) : ColorRGBA(.61f, .88f, .83f));
    }
    else if (Dialogue::Danger())
    {
        Icon("warning", 96, 466, 22);
        UiText(130, 485, "교전 중", 17, {1, .78f, .4f});
        UiText(96, 519, "기록을 보는 동안에도", 15);
        UiText(96, 545, "시간이 계속 흐릅니다.", 15);
    }
    for (size_t i = 0; i < perPage && page * perPage + i < rows.size(); ++i)
    {
        UiText(454, 251 + float(i) * 25, rows[page * perPage + i], 18);
    }
    UiText(454, 650, std::to_string(page + 1) + " / " + std::to_string(pages), 14, {.59f, .75f, .76f});
    UiText(96, 650,
           world.dialogue.suspended ? "Enter 중단 대화 재개 · 안전한 곳에서 가능"
                                    : "PageUp / PageDown 페이지",
           14);
    auto controls = Hud::Controls();
    for (size_t i = 0; i < controls.size(); ++i)
    {
        Button(controls[i], int(i));
    }
}

// ============================================================================
// Hud — live values, menus and shared pointer/keyboard hit regions
// ============================================================================
void Hud::DrawVitals()
{
    const auto &w = Game::Get().GetState();
    auto &mine = game::GameLevels::Mine().GetState();
    const auto &chapter = game::GameLevels::Chapter().GetState();
    if (Dialogue::Active() && Dialogue::LocksWorld())
    {
        return;
    }
    const auto &stats = mine.player;
    const bool critical = stats.health <= stats.maxHealth * .25f;
    // CQ-12: Unified bottom-left Vitals + Quick Supply card (x=20..308, y=678..786), clear of bottom controls.
    HudCard(20.f, 678.f, 288.f, 108.f,
            critical ? ColorRGBA(1.f, .44f, .36f) : ColorRGBA(.46f, .90f, .82f));
    const std::string supplyLine = "G 키트 " + std::to_string(stats.recoveryKits) + " · 고철 " +
                                   std::to_string(stats.salvage) + " · " + std::to_string(stats.credits) + " Cr";
    UiText(34, 699, supplyLine, 13, {.72f, .90f, .86f}, true);
    Icon("health", 32, 736, 24, true);
    UiText(66, 735, std::to_string(stats.health), 28, critical ? ColorRGBA(1, .56f, .44f) : kUiInk, true);
    float maximumX = 76.f + RenderAssets::MeasureUtf8(std::to_string(stats.health), 28);
    UiText(maximumX, 734, "/ " + std::to_string(stats.maxHealth), 18, kUiInk, true);
    Sprite("Components/hud_health_track", 64, 746, 224, 16);
    Sprite("Components/hud_health_fill", 66, 748, 220, 12,
           critical ? ColorRGBA(1, .52f, .43f) : ColorRGBA(.65f, .92f, .86f),
           Game::Clamp(float(stats.health) / (std::max)(1, stats.maxHealth), 0, 1));
    if (!w.Mounted() && Game::Get().HasWalkingControls())
    {
        const auto &mobility = w.playerMotion.mobility;
        const bool low = mobility.Stamina() < PlayerMobility::DodgeCost;
        const ColorRGBA ink = low ? ColorRGBA(1, .88f, .65f) : ColorRGBA(.65f, .9f, .81f);
        Icon("stamina", 32, 762, 16, true, ink);
        Drawing::DrawRectangle(66, 772, 220, 6, {.12f, .20f, .24f});
        Drawing::DrawRectangle(66, 772, 220 * mobility.Stamina() / PlayerMobility::MaxStamina, 6, ink);
        const float dodgeMarkX = 66.f + 220.f * (PlayerMobility::DodgeCost / PlayerMobility::MaxStamina);
        Drawing::DrawRectangle(dodgeMarkX, 770, 2, 10, {.9f, .96f, .94f, .8f});
        if (mobility.Dodging() || low)
        {
            UiText(318, 748, mobility.Dodging() ? "회피 중 · 공격 제한" : "스태미너 부족", 13, ink, true);
        }
    }
    bool combat = game::GameLevels::IsCombatActive() || CivicCombat::Available();
    if (critical)
    {
        HudCard(20.f, 642.f, 288.f, 30.f, {1.f, .44f, .34f});
        UiText(34, 663,
               game::GameLevels::IsCombatActive()
                   ? (stats.recoveryKits > 0 ? "위험 · G 회복 키트 사용" : "위험 · 회복 키트 없음")
                   : "위험 · 의료 지원 필요",
               15, {1, .61f, .46f}, true);
    }
    if (w.Mounted())
    {
        HudShift shift(WorldCamera::RightOffsetX());
        const float cardTop = w.aircraft.mounted ? 642.f : w.car.mounted ? 676.f : 698.f;
        const float cardHeight = w.aircraft.mounted ? 148.f : w.car.mounted ? 114.f : 82.f;
        HudCard(988.f, cardTop, 272.f, cardHeight, {.42f, .90f, .94f}, false);
        Drawing::DrawRectangle(1257.f, cardTop + 4.f, 3.f, cardHeight - 8.f, {.42f, .90f, .94f, .9f});
        const float driveSpeed = w.aircraft.mounted
                                     ? float(std::hypot(w.aircraft.velocity.x, w.aircraft.velocity.y) / 2)
                                 : w.car.mounted ? w.car.speed
                                                 : w.bikeSpeed;
        float speed = std::fabs(driveSpeed) * models::WorldScale::metersPerUnit * 3.6f;
        char value[48];
        std::snprintf(value, sizeof(value), speed < 10 ? "%.1f" : "%.0f", speed);
        if (w.aircraft.mounted)
        {
            const auto *session = Game::Get().Surface();
            const int altitude = session && w.aircraft.position
                                     ? int(w.aircraft.position->altitudeMeters -
                                           session->Stream().Contact(*w.aircraft.position).heightMeters)
                                     : 0;
            const float hpRatio = Game::Clamp(float(w.aircraft.health) / float(PlayerAircraft::MaxHealth), 0, 1);
            const ColorRGBA hpInk = hpRatio < .3f ? ColorRGBA(1, .88f, .65f) : ColorRGBA(.65f, .9f, .81f);
            const bool cruising = !w.aircraft.grounded && !w.aircraft.landingRequested &&
                                  !w.aircraft.landingAvailable && altitude >= 14;
            const char *flightState = w.aircraft.grounded           ? "착륙"
                                      : w.aircraft.landingRequested ? "착륙 중"
                                      : w.aircraft.landingAvailable ? "착륙 가능"
                                      : cruising                    ? "순항"
                                                                    : "착륙 불가";
            if (!cruising)
            {
                Icon("landing", 1054, 654, 20, true,
                     w.aircraft.landingAvailable || w.aircraft.grounded ? ColorRGBA(.65f, .9f, .81f)
                                                                        : ColorRGBA(1, .88f, .65f));
            }
            UiText(1248, 671,
                   "고도 " + std::to_string((std::max)(0, altitude)) + " m · " + flightState, 16, kUiInk, true,
                   true);
            UiText(1248, 700,
                   "내구도 " + std::to_string(w.aircraft.health) + " / " +
                       std::to_string(PlayerAircraft::MaxHealth),
                   15, hpInk, true, true);
            Drawing::DrawRectangle(1060, 709, 188, 5, {.12f, .20f, .24f});
            Drawing::DrawRectangle(1060, 709, 188 * hpRatio, 5, hpInk);
            Icon("flight", 1022, 730, 22, true);
            UiText(1248, 784,
                   w.aircraft.grounded           ? "E 하차"
                   : w.aircraft.landingRequested ? "E 착륙 취소"
                                                 : "E 착륙",
                   14, {.78f, .9f, .88f}, true, true);
        }
        else if (w.car.mounted)
        {
            const float hpRatio = Game::Clamp(float(w.car.health) / float(GroundVehicle::MaxHealth), 0, 1);
            const ColorRGBA hpInk = hpRatio < .3f ? ColorRGBA(1, .88f, .65f) : ColorRGBA(.65f, .9f, .81f);
            UiText(1248, 672, "방탄 차체 · 험지 주행 · 동승", 14, {.65f, .9f, .88f}, true, true);
            UiText(1248, 700,
                   "장갑 " + std::to_string(w.car.health) + " / " + std::to_string(GroundVehicle::MaxHealth),
                   15, hpInk, true, true);
            Drawing::DrawRectangle(1060, 709, 188, 5, {.12f, .20f, .24f});
            Drawing::DrawRectangle(1060, 709, 188 * hpRatio, 5, hpInk);
            Icon("ground_car", 1022, 730, 22, true);
            UiText(1248, 784, "Space 부스트 · E 하차", 14, {.78f, .9f, .88f}, true, true);
        }
        else
        {
            Icon("bike", 1044, 724, 25, true);
        }
        UiText(1172, 749, value, 42, kUiInk, true, true);
        UiText(1248, 747, "km/h", 18, kUiInk, true, true);
        if (driveSpeed < -.01f)
        {
            Icon("reverse", 1090, 650, 20, true);
            UiText(1248, 670, "후진", 17, kUiInk, true, true);
        }
        else if ((w.isBikeMounted && w.pressedKeys[' '] && w.bikeSpeed > 0) ||
                 (w.car.mounted && w.pressedKeys[' '] && w.car.speed > 0))
        {
            Icon("boost", 1090, 650, 20, true);
            UiText(1248, 670, w.car.mounted ? "오버드라이브" : "부스트", 17, {.55f, .95f, .88f}, true, true);
        }
    }
    else if (combat && mine.loadout.IsFirearmEquipped())
    {
        const auto &gun = Game::Get().ActiveFirearm();
        {
            HudShift shift(WorldCamera::RightOffsetX());
            HudCard(992.f, 654.f, 268.f, 132.f, {.42f, .88f, .94f}, false);
            Drawing::DrawRectangle(1257.f, 658.f, 3.f, 124.f, {.42f, .88f, .94f, .9f});
            Icon(mine.loadout.IsPrimaryEquipped() ? "carbine" : "pistol", 1040, 722, 28, true);
            Icon("infinite", 1064, 754, 15, true);
            UiText(1008, 703, gun.GetName(), 15, {.72f, .92f, .96f}, true);
            if (w.playerMotion.mobility.FocusActive())
            {
                UiText(1248, 676, "전술 집중", 15, {.42f, 1.f, .86f}, true, true);
            }
            char count[32];
            std::snprintf(count, sizeof(count), "%02d", gun.GetRounds());
            UiText(1182, 746, count, 38, gun.GetRounds() ? kUiInk : ColorRGBA(1, .78f, .4f), true, true);
            UiText(1248, 745, "/ " + std::to_string(gun.GetMagazineSize()), 22, kUiInk, true, true);
            if (gun.IsReloading())
            {
                Sprite("Components/hud_reload_track", 1088, 755, 156, 14);
                Sprite("Components/hud_reload_fill", 1090, 757, 152, 10, {1, .78f, .4f}, gun.GetReloadProgress());
                UiText(1248, 705, "재장전 중", 17, {1, .77f, .4f}, true, true);
            }
            else
            {
                float stride = (std::min)(13.f, 152.f / (std::max)(1, gun.GetMagazineSize()));
                for (int i = 0; i < gun.GetMagazineSize(); ++i)
                {
                    Sprite(i < gun.GetRounds() ? "Components/hud_ammo_pip" : "Components/hud_ammo_pip_empty",
                           1090 + i * stride, 757, (std::min)(9.f, stride - 2), 10);
                }
                if (!gun.GetRounds())
                {
                    UiText(1248, 705, "R 재장전", 17, {1, .77f, .4f}, true, true);
                }
            }
        }
        if (w.cursorInsideViewport && !w.weaponWheel.visible && Game::Get().IsAiming())
        {
            auto c = w.cursorPosition;
            float gap = 6 + gun.GetSpread() * 120;
            const bool blocked = gun.IsReloading();
            ColorRGBA ink = blocked ? ColorRGBA(1, .75f, .4f) : ColorRGBA(.9f, 1, .98f);
            for (int i = 0; i < 4; ++i)
            {
                float angle = i * kPi / 2;
                Vector2 begin = {c.x + std::cos(angle) * gap, c.y + std::sin(angle) * gap};
                Vector2 end = {c.x + std::cos(angle) * (gap + 8), c.y + std::sin(angle) * (gap + 8)};
                Drawing::DrawLine(begin, end, {0, 0, 0}, 5);
                Drawing::DrawLine(begin, end, ink, 2);
            }
            Drawing::DrawEllipse(c.x, c.y, 2.5f, 2.5f, {0, 0, 0});
            Drawing::DrawEllipse(c.x, c.y, 1, 1, ink);
            if (blocked)
            {
                Drawing::DrawLine({c.x - 3, c.y - 3}, {c.x + 3, c.y + 3}, ink, 2);
                Drawing::DrawLine({c.x + 3, c.y - 3}, {c.x - 3, c.y + 3}, ink, 2);
            }
        }
    }
    if (!w.Mounted() && combat && !mine.loadout.IsFirearmEquipped())
    {
        HudShift shift(WorldCamera::RightOffsetX());
        HudCard(992.f, 704.f, 268.f, 78.f, {.42f, .80f, .82f}, false);
        Drawing::DrawRectangle(1257.f, 708.f, 3.f, 70.f, {.42f, .80f, .82f, .85f});
        Icon("unarmed", 1020, 723, 28, true);
        UiText(1246, 736, "맨손", 25, kUiInk, true, true);
        UiText(1246, 764, "좌클릭 근접 · Q 홀드 무기 선택", 15, kUiInk, true, true);
    }
    if (combat && !Game::Get().IsAiming() && w.cursorInsideViewport && !w.weaponWheel.visible)
    {
        auto p = w.cursorPosition;
        Sprite(mine.loadout.IsFirearmEquipped() ? "Components/cursor_hip" : "Components/cursor_unarmed",
               p.x - 16, p.y - 16, 32, 32);
        if (mine.loadout.IsFirearmEquipped() && !w.playerMotion.mobility.FocusActive())
        {
            HudShift shift(WorldCamera::RightOffsetX());
            Icon("mouse_right", 1008, 661, 18, true);
            UiText(1248, 676, "우클릭 홀드 · 조준", 14, {.78f, .90f, .90f}, true, true);
        }
    }
    const auto &law = CivicJustice::State();
    if (CivicJustice::IsExterior() && law.wanted > 0)
    {
        HudShift shift(WorldCamera::CenterOffsetX());
        const bool airborneInterceptor =
            law.wanted >= 5 && w.aircraft.mounted && !w.aircraft.grounded;
        const bool canSurrender =
            w.surfaceExploration && law.wanted == 1 && law.heat < 60 && !w.Mounted() &&
            std::any_of(law.officers.begin(), law.officers.end(),
                        [](const auto &o) { return o.health > 0 && !o.departing; });
        const bool hasSubLine = canSurrender || (law.responseStarted && !law.identified);
        const float cardH = hasSubLine ? 120.f : 98.f;
        const ColorRGBA alertCol =
            airborneInterceptor ? ColorRGBA(1.f, .40f, .34f) : ColorRGBA(1.f, .74f, .34f);
        HudCard(508.f, 20.f, 276.f, cardH, alertCol);
        Icon("security", 522, 36, 21, true, {1, .78f, .4f});
        for (int i = 0; i < 5; ++i)
        {
            UiText(554 + i * 35, 58, i < law.wanted ? "★" : "☆", 25, {1, .78f, .4f}, true);
        }
        Sprite("Components/hud_heat_track", 552, 69, 169, 10);
        Sprite("Components/hud_heat_fill", 554, 71, 165, 6, {1, .78f, .4f},
               Game::Clamp(law.heat / CivicJustice::kMaximumHeat, 0, 1));
        UiText(524, 104,
               airborneInterceptor ? "대공 요격 경계 · 보안망 추적 중"
               : law.identified    ? "보안망 추적 중"
                                   : "수색 중 · 미식별",
               15, airborneInterceptor ? ColorRGBA(1, .56f, .44f) : ColorRGBA(1, .78f, .4f), true);
        if (canSurrender)
        {
            UiText(524, 126, "비무장 투항 가능 · 맨손 정지", 14, {.65f, .9f, .81f}, true);
        }
        else if (law.responseStarted && !law.identified)
        {
            int remaining = int(std::ceil(
                (std::max)(0.f, CivicJustice::kUnidentifiedEscapeSeconds - law.initialUnseenSeconds)));
            UiText(524, 126, "미발각 해제까지 " + std::to_string(remaining) + "초", 14, {.87f, .88f, .76f},
                   true);
        }
    }
    if (game::GameLevels::Mine().IsActive())
    {
        const bool ria = Game::Get().HasCompanion(CompanionId::Ria, CompanionPlace::Mine);
        const bool kain = Game::Get().HasCompanion(CompanionId::Kain, CompanionPlace::Mine);
        Icon("companion", 32, 115, 18, true);
        std::string companions = ria ? (mine.riaCombat.health <= 0 ? "리아 회복 중"
                                        : mine.riaCombat.engaged   ? "리아 엄호 중"
                                                                   : "리아 동행")
                                     : "";
        if (kain)
        {
            if (!companions.empty())
            {
                companions += " / ";
            }
            companions += mine.kain.combat.health <= 0 ? "카인 회복 중"
                          : mine.kain.combat.engaged   ? "카인 엄호 중"
                                                       : "카인 동행";
        }
        UiText(62, 132, companions.empty() ? "오르딘 광산" : companions, 15, {.83f, .86f, .71f}, true);
        Icon("rescue", 32, 148, 18, true);
        UiText(62, 165, "광산 구조 " + std::to_string(mine.rescuedPeople) + " / 12명", 15, kUiInk, true);
    }
    if (game::GameLevels::Chapter().IsActive())
    {
        Icon(chapter.ria.combat.health > 0 ? "companion" : "injured", 32, 139, 18, true);
        std::string names = Game::Get().HasCompanion(CompanionId::Ria, CompanionPlace::Frontier)
                                ? (chapter.ria.combat.health > 0 ? "리아 · 동행" : "리아 · 회복 중")
                                : "빛나는 개척지";
        if (Game::Get().HasCompanion(CompanionId::Kain, CompanionPlace::Frontier))
        {
            names += chapter.kain.combat.health > 0 ? " / 카인 동행" : " / 카인 회복 중";
        }
        UiText(62, 156, names, 15, kUiInk, true);
        if (chapter.escortStarted)
        {
            size_t arrived = std::count_if(chapter.survivors.begin(), chapter.survivors.end(),
                                           [](const game::FrontierPerson &p) { return p.arrived; });
            UiText(62, 183,
                   chapter.convoyRescued ? "운송대 5명 · 인수 완료"
                                         : "운송대 안전 지점 도착 " + std::to_string(arrived) + " / 5",
                   15, kUiInk, true);
        }
    }
}
void Hud::OpenMenu(UiScreen screen)
{
    if (SaveSystem::Loading())
    {
        return;
    }
    SaveSystem::Refresh();
    auto &w = Game::Get().GetState();
    if (HospitalRecovery::Active())
    {
        return;
    }
    WeaponWheel::Close();
    w.ui.screen = screen;
    w.ui.focus = 0;
    w.ui.pressed = -1;
    w.isPaused = screen != UiScreen::None;
    w.mapVisible = w.journalVisible = w.dialogueHistoryVisible = false;
    w.mouseFiring = w.mouseAiming = false;
    w.StopMotion();
    std::fill(std::begin(w.pressedKeys), std::end(w.pressedKeys), false);
}
void Hud::CloseMenu()
{
    OpenMenu(UiScreen::None);
}
std::vector<UiControl> Hud::Controls()
{
    if (SaveSystem::Loading())
    {
        return {{UiAction::CancelLoad, {440, 336}, {400, 46}, "불러오기 취소 · Esc"}};
    }
    const auto &w = Game::Get().GetState();
    auto screen = w.ui.screen;
    if (screen == UiScreen::None && w.isPaused)
    {
        screen = UiScreen::Pause;
    }
    std::vector<UiControl> controls;
    auto row = [&](UiAction action, const std::string &label, bool enabled = true) {
        const bool title = screen == UiScreen::Title, pause = screen == UiScreen::Pause;
        const Vector2 origin = title ? Vector2{96, 366} : pause ? Vector2{408, 178} : Vector2{440, 220};
        const Vector2 size = title ? Vector2{430, 52} : pause ? Vector2{464, 52} : Vector2{400, 46};
        controls.push_back({action,
                            {origin.x, origin.y + float(controls.size()) * (pause ? 64 : 58)},
                            size,
                            label,
                            enabled});
    };
    switch (screen)
    {
    case UiScreen::Title:
        row(UiAction::Start, "시작하기");
        row(UiAction::Stages, "스테이지 목록 · 테스트");
        row(UiAction::Options, "옵션");
        row(UiAction::Help, "조작 안내");
        row(UiAction::Quit, "종료");
        row(UiAction::Checkpoint, "월드 불러오기");
        break;
    case UiScreen::Pause:
        row(UiAction::Continue, "계속하기");
        row(UiAction::Stages, "스테이지 목록 · 테스트");
        row(UiAction::Options, "옵션");
        row(UiAction::Summary, "보유 · 성장 정보");
        row(UiAction::Help, "조작 안내");
        row(UiAction::Restart, w.surfaceExploration                     ? "행성 탐사 재시작 · 진행 초기화"
                               : game::GameLevels::Chapter().IsActive() ? "헤일로 귀환 · 기록 유지"
                               : game::GameLevels::Mine().IsActive()    ? "광산 재시작"
                                                                        : "새 게임 · 진행 초기화");
        row(UiAction::Quit, "종료");
        row(UiAction::Checkpoint, "월드 저장 · 불러오기");
        row(UiAction::Profiles, "행성 선택 · 탐사 테스트", SaveSystem::Restriction() == nullptr);
        break;
    case UiScreen::PlanetProfiles:
        for (size_t i = 0; i < world::PlanetProfiles::All().size(); ++i)
        {
            const auto &p = world::PlanetProfiles::All()[i];
            bool current = Game::Get().Surface() && p.SameWorld(Game::Get().Surface()->Planet());
            controls.push_back({UiAction::SelectProfile,
                                {440, 220 + float(i) * 70},
                                {400, 60},
                                p.name + " · 반경 " + std::to_string(int(p.radiusMeters / 1000)) + " km" +
                                    (current ? " · 현재" : ""),
                                !current,
                                int(i)});
        }
        controls.push_back({UiAction::Back, {440, 440}, {400, 46}, "뒤로"});
        break;
    case UiScreen::Checkpoint:
        row(UiAction::SaveWorld, "현재 월드 저장 · F5", SaveSystem::Restriction() == nullptr);
        row(UiAction::LoadWorld, "저장된 월드 불러오기", SaveSystem::Available());
        row(UiAction::Back, "뒤로");
        break;
    case UiScreen::Stages:
        for (int i = 0; i < int(TestStage::Count); ++i)
        {
            const int col = i < 6 ? 0 : 1;
            const int rowIdx = i < 6 ? i : (i - 6);
            controls.push_back({UiAction::EnterStage,
                                {96.f + col * 340.f, 211.f + rowIdx * 56.f},
                                {324.f, 48.f},
                                Game::StageName(TestStage(i)),
                                true,
                                i});
        }
        controls.push_back({UiAction::Back, {96, 678}, {170, 52}, "뒤로"});
        break;
    case UiScreen::Options:
        row(UiAction::Background, "배경 불투명도");
        row(UiAction::Border, "테두리 불투명도");
        row(UiAction::Content, "내용 불투명도");
        row(UiAction::Cartoon, "카툰 명암");
        row(UiAction::Resolution, "월드 렌더 스타일");
        row(UiAction::Antialiasing, "안티앨리어싱");
        row(UiAction::DisplayResolution, "화면 해상도");
        row(UiAction::DisplayFullscreen, "전체화면");
        row(UiAction::Back, "뒤로");
        for (size_t i = 0; i < controls.size(); ++i)
        {
            if (i < 3)
            {
                controls[i].position = {96, 212 + float(i) * 104};
                controls[i].size = {548.f, 88.f};
            }
            else if (i < 8)
            {
                controls[i].position = {688, 202 + float(i - 3) * 82};
                controls[i].size = {480.f, 70.f};
            }
            else
            {
                controls[i].position = {96, 672};
                controls[i].size = {224.f, 48.f};
            }
        }
        break;
    case UiScreen::FacilityShop: {
        const auto services = Game::Get().FacilityServices();
        for (size_t i = 0; i < services.size(); ++i)
        {
            controls.push_back({UiAction::FacilityService,
                                {594.f, 286.f + float(i) * 106.f},
                                {602.f, 92.f},
                                "",
                                services[i].enabled,
                                int(i)});
        }
        controls.push_back({UiAction::Continue, {594.f, 654.f}, {602.f, 48.f}, "대화 종료"});
        break;
    }
    case UiScreen::Wardrobe: {
        const auto &w = Game::Get().GetState();
        // Left panel: Base appearance controls (x = 46..378)
        controls.push_back({UiAction::WardrobeModularMode,
                            {46.f, 152.f},
                            {332.f, 48.f},
                            w.playerModularMedic ? "모드: 필드 메딕 모듈러" : "모드: 표준 개척자 슈트",
                            true,
                            0});
        controls.push_back({UiAction::WardrobeGender,
                            {46.f, 212.f},
                            {332.f, 46.f},
                            std::string("체형: ") + (w.playerGender == models::Gender::Male ? "남성" : "여성"),
                            !w.playerModularMedic,
                            0});
        static constexpr const char *kRoleNames[9] = {
            "개척자", "엔지니어", "경비대원", "의무관", "연구원", "광부", "중장갑 전투원", "파일럿", "시민"};
        controls.push_back({UiAction::WardrobeOutfit,
                            {46.f, 270.f},
                            {332.f, 46.f},
                            std::string("의상 프리셋: ") + kRoleNames[size_t(w.playerOutfit) % 9],
                            !w.playerModularMedic,
                            1});
        static constexpr const char *kHairNames[6] = {"버즈컷", "가르마", "웨이브", "단발", "포니테일", "번 헤어"};
        controls.push_back({UiAction::WardrobeHair,
                            {46.f, 328.f},
                            {332.f, 46.f},
                            std::string("헤어스타일: ") + kHairNames[size_t(w.playerHair) % 6],
                            !w.playerModularMedic,
                            1});
        // 6 Coat color swatches (2 rows x 3 columns)
        for (int c = 0; c < 6; ++c)
        {
            controls.push_back({UiAction::WardrobeCoatColor,
                                {46.f + float(c % 3) * 112.f, 412.f + float(c / 3) * 52.f},
                                {104.f, 42.f},
                                "",
                                !w.playerModularMedic,
                                c});
        }
        // Turntable controls on bottom-left
        controls.push_back({UiAction::WardrobeRotate, {46.f, 540.f}, {104.f, 44.f}, "◀ 좌", true, -1});
        controls.push_back({UiAction::WardrobeAutoRotate,
                            {160.f, 540.f},
                            {104.f, 44.f},
                            w.ui.wardrobeAutoRotate ? "자동ON" : "자동OFF",
                            true,
                            0});
        controls.push_back({UiAction::WardrobeRotate, {274.f, 540.f}, {104.f, 44.f}, "우 ▶", true, 1});
        controls.push_back({UiAction::OpenHomeStorage, {46.f, 608.f}, {332.f, 46.f}, "물자 보관함으로 전환", true, 0});
        controls.push_back({UiAction::Continue, {46.f, 666.f}, {332.f, 48.f}, "외형 적용 · 닫기 (Esc)", true, 0});

        // Right panel: Per-part modular attachment toggles (Mixamo 11 parts) or Standard quick-role grid
        if (w.playerModularMedic)
        {
            static constexpr const char *kPartNames[11] = {
                "메딕 코트", "카고 팬츠", "전술 부츠", "번 헤어", "어깨 가드", "무릎 가드",
                "왼팔 스캐너", "오른팔 가드", "의료 파우치", "구조 배낭", "탄창 랙"};
            for (int bit = 0; bit < 11; ++bit)
            {
                const bool on = (w.playerModularPartsMask & (1u << unsigned(bit))) != 0;
                const int col = bit % 2;
                const int r = bit / 2;
                controls.push_back({UiAction::WardrobeModularPart,
                                    {902.f + float(col) * 168.f, 154.f + float(r) * 74.f},
                                    {160.f, 62.f},
                                    std::string(kPartNames[bit]) + (on ? "\n장착 중" : "\n해제됨"),
                                    true,
                                    bit});
            }
        }
        else
        {
            for (int role = 0; role < 9; ++role)
            {
                const int col = role % 2;
                const int r = role / 2;
                controls.push_back({UiAction::WardrobeOutfit,
                                    {902.f + float(col) * 168.f, 154.f + float(r) * 74.f},
                                    {160.f, 62.f},
                                    kRoleNames[role],
                                    true,
                                    100 + role});
            }
            controls.push_back({UiAction::WardrobeModularMode,
                                {902.f, 540.f},
                                {328.f, 66.f},
                                "필드 메딕 모듈러 (13파츠)\n착용 및 파트별 커스터마이즈",
                                true,
                                1});
        }
        break;
    }
    case UiScreen::HomeStorage: {
        const auto &p = game::GameLevels::Mine().GetState().player;
        const auto &r = Game::Get().GetState().residence;
        // Card 1: Credits Safe (x = 96..436)
        controls.push_back({UiAction::StorageAction,
                            {116.f, 534.f},
                            {146.f, 44.f},
                            "+200 예치",
                            p.credits > 0 && r.stashCredits < ResidenceState::kMaxStashCredits,
                            0});
        controls.push_back({UiAction::StorageAction,
                            {270.f, 534.f},
                            {146.f, 44.f},
                            "-200 인출",
                            r.stashCredits > 0,
                            1});
        // Card 2: Recovery Kits (x = 470..810)
        controls.push_back({UiAction::StorageAction,
                            {490.f, 534.f},
                            {146.f, 44.f},
                            "1개 보관",
                            p.recoveryKits > 0 && r.stashRecoveryKits < ResidenceState::kMaxStashKits,
                            2});
        controls.push_back({UiAction::StorageAction,
                            {644.f, 534.f},
                            {146.f, 44.f},
                            "1개 인출",
                            r.stashRecoveryKits > 0 && p.recoveryKits < ResidenceState::kMaxCarryKits,
                            3});
        // Card 3: Salvage Parts (x = 844..1184)
        controls.push_back({UiAction::StorageAction,
                            {864.f, 534.f},
                            {146.f, 44.f},
                            "전량 보관",
                            p.salvage > 0 && r.stashSalvage < ResidenceState::kMaxStashSalvage,
                            4});
        controls.push_back({UiAction::StorageAction,
                            {1018.f, 534.f},
                            {146.f, 44.f},
                            "전량 인출",
                            r.stashSalvage > 0,
                            5});
        // Top-right switch to 3D Wardrobe & bottom close
        controls.push_back({UiAction::OpenWardrobe, {884.f, 82.f}, {300.f, 52.f}, "3D 외관 옷장 열기", true, 0});
        controls.push_back({UiAction::Continue, {470.f, 676.f}, {340.f, 48.f}, "보관함 닫기 (Esc)", true, 0});
        break;
    }
    case UiScreen::Summary:
    case UiScreen::Help:
        controls.push_back({UiAction::Back, {96, 672}, {224, 48}, "뒤로"});
        break;
    case UiScreen::Supply: {
        const auto &stats = game::GameLevels::Mine().GetState().player;
        controls.push_back(
            {UiAction::Exchange,
             {336, 494},
             {608, 46},
             "교환하기",
             stats.salvage >= CivicJustice::ShopPrice(2) && stats.recoveryKits < 999 && !Dialogue::Danger()});
        controls.push_back({UiAction::Continue, {336, 552}, {608, 46}, "닫기"});
        break;
    }
    case UiScreen::Confirm:
        controls.push_back({w.ui.confirmation,
                            {336, 476},
                            {296, 46},
                            w.ui.confirmation == UiAction::Quit ? "종료한다" : "실행한다"});
        controls.push_back({UiAction::Back, {648, 476}, {296, 46}, "취소"});
        break;
    case UiScreen::Medical:
        controls.push_back({UiAction::Continue, {336, 552}, {608, 46}, "계속하기"});
        break;
    default:
        if (w.journalVisible || w.dialogueHistoryVisible)
        {
            if (w.surfaceExploration && w.journalVisible)
            {
                return ActivityJournalUi::Controls();
            }
            controls.push_back({UiAction::Journal, {96, 212}, {296, 48}, "조사 기록"});
            controls.push_back({UiAction::History, {96, 274}, {296, 48}, "대사 이력"});
            controls.push_back({UiAction::Previous, {454, 676}, {176, 44}, "이전 페이지"});
            controls.push_back({UiAction::Next, {648, 676}, {176, 44}, "다음 페이지"});
            controls.push_back({UiAction::Continue, {848, 676}, {320, 44}, "닫기"});
        }
        else if (Dialogue::Active())
        {
            controls = LayoutDialogue().controls;
        }
        break;
    }
    return controls;
}
void Hud::Activate(const UiControl &control, int direction)
{
    if (!control.enabled)
    {
        return;
    }
    auto &g = Game::Get();
    auto &w = g.GetState();
    auto &prefs = w.uiPreferences;
    auto adjust = [&](float &value, float low) { value = Game::Clamp(value + .01f * direction, low, 1); };
    switch (control.action)
    {
    case UiAction::Continue:
        w.journalVisible = w.dialogueHistoryVisible = false;
        CloseMenu();
        break;
    case UiAction::Start:
        g.StartNewGame();
        break;
    case UiAction::Checkpoint: {
        const auto parent = w.ui.screen == UiScreen::Title ? UiScreen::Title : UiScreen::Pause;
        OpenMenu(UiScreen::Checkpoint);
        w.ui.returnTo = parent;
        break;
    }
    case UiAction::Profiles:
        OpenMenu(UiScreen::PlanetProfiles);
        w.ui.returnTo = UiScreen::Pause;
        break;
    case UiAction::SelectProfile:
        if (control.choice >= 0 && control.choice < int(world::PlanetProfiles::All().size()))
        {
            SaveSystem::SwitchPlanet(world::PlanetProfiles::All()[size_t(control.choice)].id);
        }
        break;
    case UiAction::SaveWorld:
        SaveSystem::Save();
        break;
    case UiAction::LoadWorld:
        SaveSystem::Load();
        break;
    case UiAction::CancelLoad:
        SaveSystem::Cancel();
        break;
    case UiAction::ActivityTab:
        w.journalLocalActivities = control.choice == 1;
        w.journalActivityPage = 0;
        w.ui.activityMission = MissionId::Count;
        w.ui.activityRegion.clear();
        w.ui.activityDetailPage = 0;
        w.ui.focus = 0;
        break;
    case UiAction::ActivityPage: {
        const int pages = (std::max)(1, int((ActivityJournal::CommunityEntries().size() +
                                             ActivityJournal::LocalEntriesPerPage - 1) /
                                            ActivityJournal::LocalEntriesPerPage));
        const int page = std::clamp(w.journalActivityPage, 0, pages - 1);
        w.journalActivityPage = std::clamp(page + control.choice, 0, pages - 1);
        w.ui.activityRegion.clear();
        w.ui.activityDetailPage = 0;
        w.ui.focus = 0;
        break;
    }
    case UiAction::TrackCommunity:
    case UiAction::ViewActivity: {
        if (control.choice >= 0)
        {
            ActivityJournalUi::Select(control.choice);
        }
        bool tracked = control.choice < 0;
        if (tracked && w.surfaceExploration && w.journalVisible && !LocalActivities() &&
            w.ui.activityMission != MissionId::Count)
        {
            tracked = ActivityJournal::TrackMission(w.ui.activityMission);
        }
        if (control.choice >= 0)
        {
            const auto *community = g.SurfaceCommunity();
            if (community && size_t(control.choice) < community->State().sites.size())
            {
                tracked = ActivityJournal::TrackCommunity(
                    community->State().sites[size_t(control.choice)].regionId);
            }
        }
        if (tracked)
        {
            if (control.action == UiAction::ViewActivity)
            {
                MapHud::FocusTrackedObjective();
            }
            else
            {
                g.ShowNotice("목표 추적 / " + ActivityJournal::Current().title);
            }
        }
        break;
    }
    case UiAction::TrackMission:
        ActivityJournalUi::Select(control.choice);
        if (ActivityJournal::TrackMission(MissionId(control.choice)))
        {
            ActivityJournal::TriggerGuidance();
            g.ShowNotice(std::string("목표 추적 / ") + QuestParticipation::Name(MissionId(control.choice)) +
                         " · 3D 경로 안내 [N]");
        }
        break;
    case UiAction::SelectActivity:
        ActivityJournalUi::Select(control.choice);
        if (LocalActivities())
        {
            const auto *community = g.SurfaceCommunity();
            if (community && control.choice >= 0 && size_t(control.choice) < community->State().sites.size())
            {
                ActivityJournal::TrackCommunity(community->State().sites[size_t(control.choice)].regionId);
            }
        }
        break;
    case UiAction::ActivityDetailPage:
        ActivityJournalUi::TurnPage(control.choice);
        break;
    case UiAction::Mission:
        ActivityJournalUi::Select(control.choice);
        g.ManageMission(MissionId(control.choice));
        break;
    case UiAction::EnterStage:
        g.EnterTestStage(TestStage(control.choice));
        break;
    case UiAction::Stages:
    case UiAction::Options:
    case UiAction::Summary:
    case UiAction::Help: {
        auto parent = w.ui.screen == UiScreen::Title ? UiScreen::Title : UiScreen::Pause;
        OpenMenu(control.action == UiAction::Stages    ? UiScreen::Stages
                 : control.action == UiAction::Options ? UiScreen::Options
                 : control.action == UiAction::Summary ? UiScreen::Summary
                                                       : UiScreen::Help);
        w.ui.returnTo = parent;
        break;
    }
    case UiAction::Back: {
        auto parent = w.ui.returnTo;
        OpenMenu(parent);
        break;
    }
    case UiAction::Restart:
    case UiAction::Quit:
        if (w.ui.screen == UiScreen::Confirm)
        {
            if (control.action == UiAction::Quit)
            {
                platform::RequestClose();
                break;
            }
            CloseMenu();
            if (game::GameLevels::Mine().IsActive() && !w.surfaceExploration)
            {
                game::GameLevels::Mine().Restart();
            }
            else
            {
                g.StartNewGame();
            }
        }
        else
        {
            auto parent = w.ui.screen;
            OpenMenu(UiScreen::Confirm);
            w.ui.returnTo = parent;
            w.ui.confirmation = control.action;
        }
        break;
    case UiAction::Background:
        adjust(prefs.background, .8f);
        break;
    case UiAction::Border:
        adjust(prefs.border, .8f);
        break;
    case UiAction::Content:
        adjust(prefs.content, .85f);
        break;
    case UiAction::Cartoon:
    case UiAction::Resolution:
    case UiAction::Antialiasing: {
        auto &r = GameRenderer::Get().Backend();
        auto style = r.GetStyle();
        if (control.action == UiAction::Cartoon)
        {
            style.cartoon = !style.cartoon;
        }
        if (control.action == UiAction::Antialiasing)
        {
            style.antialiasing = !style.antialiasing;
        }
        if (control.action == UiAction::Resolution)
        {
            if (direction < 0)
            {
                style.resolution = style.resolution == WorldResolution::Native ? WorldResolution::Quarter
                                   : style.resolution == WorldResolution::Quarter ? WorldResolution::Half
                                                                                  : WorldResolution::Native;
            }
            else
            {
                style.resolution = style.resolution == WorldResolution::Native ? WorldResolution::Half
                                   : style.resolution == WorldResolution::Half ? WorldResolution::Quarter
                                                                               : WorldResolution::Native;
            }
        }
        r.SetStyle(style);
        break;
    }
    case UiAction::DisplayResolution: {
        const int count = int(platform::kResolutionOptions.size());
        const int step = direction < 0 ? -1 : 1;
        w.displayPreferences.resolutionIndex = (w.displayPreferences.resolutionIndex + step + count) % count;
        Game::Get().ApplyDisplayPreferences(true);
        break;
    }
    case UiAction::DisplayFullscreen:
        w.displayPreferences.fullscreen = !w.displayPreferences.fullscreen;
        Game::Get().ApplyDisplayPreferences(true);
        break;
    case UiAction::FacilityService:
        Game::Get().ExecuteFacilityService(control.choice);
        break;
    case UiAction::OpenWardrobe:
        OpenMenu(UiScreen::Wardrobe);
        break;
    case UiAction::OpenHomeStorage:
        OpenMenu(UiScreen::HomeStorage);
        break;
    case UiAction::WardrobeGender:
        Game::Get().ResidenceCycleGender();
        break;
    case UiAction::WardrobeOutfit: {
        if (control.choice >= 100)
        {
            w.playerOutfit = models::CharacterType((control.choice - 100) % int(models::CharacterType::Count));
            w.playerModularMedic = false;
        }
        else
        {
            const int count = int(models::CharacterType::Count);
            const int step = direction < 0 ? -1 : 1;
            w.playerOutfit = models::CharacterType((int(w.playerOutfit) + step + count) % count);
        }
        break;
    }
    case UiAction::WardrobeHair:
        Game::Get().ResidenceCycleHair();
        break;
    case UiAction::WardrobeCoatColor:
        w.playerCoatColor = ((control.choice % 6) + 6) % 6;
        break;
    case UiAction::WardrobeModularMode:
        w.playerModularMedic = control.choice > 0 ? true : !w.playerModularMedic;
        break;
    case UiAction::WardrobeModularPart:
        if (control.choice >= 0 && control.choice < 11)
        {
            w.playerModularPartsMask ^= (1u << unsigned(control.choice));
        }
        break;
    case UiAction::WardrobeRotate:
        w.ui.wardrobeAutoRotate = false;
        w.ui.wardrobeYawOffset += float(control.choice) * 0.45f;
        break;
    case UiAction::WardrobeAutoRotate:
        w.ui.wardrobeAutoRotate = !w.ui.wardrobeAutoRotate;
        break;
    case UiAction::StorageAction:
        switch (control.choice)
        {
        case 0:
            Game::Get().ResidenceDepositCredits();
            break;
        case 1:
            Game::Get().ResidenceWithdrawCredits();
            break;
        case 2:
            Game::Get().ResidenceDepositKit();
            break;
        case 3:
            Game::Get().ResidenceWithdrawKit();
            break;
        case 4:
            Game::Get().ResidenceDepositSalvage();
            break;
        case 5:
            Game::Get().ResidenceWithdrawSalvage();
            break;
        default:
            break;
        }
        break;
    case UiAction::Exchange:
        ExchangeSupply();
        break;
    case UiAction::Choice:
        Dialogue::Select(size_t(control.choice));
        break;
    case UiAction::Advance:
        Dialogue::Advance();
        break;
    case UiAction::Journal:
        w.journalVisible = true;
        w.dialogueHistoryVisible = false;
        w.dialogue.historyOffset = 0;
        break;
    case UiAction::History:
        w.journalVisible = false;
        w.dialogueHistoryVisible = true;
        w.dialogue.historyOffset = 0;
        break;
    case UiAction::Previous:
        if (w.dialogue.historyOffset)
        {
            --w.dialogue.historyOffset;
        }
        break;
    case UiAction::Next:
        w.dialogue.historyOffset = (std::min)(w.dialogue.historyOffset + 1, size_t(4096));
        break;
    }
}
bool Hud::HandleKey(unsigned char key)
{
    auto &w = Game::Get().GetState();
    const bool activity = w.surfaceExploration && w.journalVisible;
    if (activity && (key == 27 || key == 'j' || key == 'h' || (key == 13 && w.dialogue.suspended)))
    {
        return false;
    }
    if (activity)
    {
        w.pressedKeys[key] = false;
    } // Navigation must not also move the player during combat.
    if (w.ui.screen == UiScreen::None && !w.isPaused && !activity)
    {
        if (key == 27 && !w.mapVisible && !w.journalVisible && !w.dialogueHistoryVisible)
        {
            OpenMenu(UiScreen::Pause);
            return true;
        }
        if (!Dialogue::Active() && !w.mapVisible && !w.journalVisible && !w.dialogueHistoryVisible &&
            (key == 'i' || key == '?'))
        {
            OpenMenu(key == 'i' ? UiScreen::Summary : UiScreen::Help);
            w.ui.returnTo = UiScreen::Pause;
            return true;
        }
        return false;
    }
    if (key == 27)
    {
        if (w.ui.screen == UiScreen::Title)
        {
            return true;
        }
        if (w.ui.screen == UiScreen::Pause || w.ui.screen == UiScreen::Medical ||
            w.ui.screen == UiScreen::Supply || w.ui.screen == UiScreen::FacilityShop ||
            w.ui.screen == UiScreen::Wardrobe || w.ui.screen == UiScreen::HomeStorage ||
            w.ui.screen == UiScreen::None)
        {
            CloseMenu();
        }
        else
        {
            OpenMenu(w.ui.returnTo);
        }
        return true;
    }
    auto controls = Controls();
    if (controls.empty())
    {
        return true;
    }
    if (key == 'w')
    {
        w.ui.focus = (w.ui.focus + int(controls.size()) - 1) % int(controls.size());
    }
    if (key == 's' || key == 9)
    {
        w.ui.focus = (w.ui.focus + 1) % int(controls.size());
    }
    w.ui.focus = (std::min)(w.ui.focus, int(controls.size()) - 1);
    if (key == 13 || key == 'e')
    {
        Activate(controls[size_t(w.ui.focus)]);
    }
    if (w.ui.screen == UiScreen::FacilityShop && key >= '1' && key <= '9')
    {
        const size_t idx = size_t(key - '1');
        if (idx < controls.size())
        {
            w.ui.focus = int(idx);
            Activate(controls[idx]);
        }
    }
    if (w.ui.screen == UiScreen::Options && (key == 'a' || key == 'd'))
    {
        Activate(controls[size_t(w.ui.focus)], key == 'a' ? -1 : 1);
    }
    if (w.ui.screen == UiScreen::Wardrobe && (key == 'a' || key == 'd'))
    {
        w.ui.wardrobeAutoRotate = false;
        w.ui.wardrobeYawOffset += (key == 'a' ? -0.35f : 0.35f);
    }
    if (!activity && (w.ui.screen == UiScreen::Pause || w.ui.screen == UiScreen::None) && key == 'r')
    {
        Activate({UiAction::Restart, {}, {}, ""});
    }
    return true;
}
bool Hud::HandleSpecialKey(int key)
{
    const auto &w = Game::Get().GetState();
    const bool activity = w.surfaceExploration && w.journalVisible;
    if (activity && (key == GLFW_KEY_PAGE_UP || key == GLFW_KEY_PAGE_DOWN))
    {
        ActivityJournalUi::TurnPage(key == GLFW_KEY_PAGE_UP ? -1 : 1);
        return true;
    }
    if (w.ui.screen == UiScreen::None && !w.isPaused && !activity)
    {
        return false;
    }
    if (key == GLFW_KEY_UP)
    {
        return HandleKey('w');
    }
    if (key == GLFW_KEY_DOWN)
    {
        return HandleKey('s');
    }
    if (key == GLFW_KEY_LEFT)
    {
        return HandleKey('a');
    }
    if (key == GLFW_KEY_RIGHT)
    {
        return HandleKey('d');
    }
    return key != GLFW_KEY_F7 && key != GLFW_KEY_F8;
}
void Hud::HandleMotion()
{
    auto &w = Game::Get().GetState();
    if (w.ui.screen == UiScreen::Wardrobe && w.ui.wardrobeDragging && w.cursorInsideViewport)
    {
        const auto cursor = CenteredCursor();
        w.ui.wardrobeAutoRotate = false;
        w.ui.wardrobeYawOffset = w.ui.wardrobeDragStartYaw + (cursor.x - w.ui.wardrobeDragStartX) * 0.012f;
        return;
    }
    if (w.ui.screen != UiScreen::Options || w.ui.pressed < 0 || !w.cursorInsideViewport)
    {
        return;
    }
    auto controls = Controls();
    if (size_t(w.ui.pressed) >= controls.size())
    {
        return;
    }
    auto action = controls[size_t(w.ui.pressed)].action;
    if (action != UiAction::Background && action != UiAction::Border && action != UiAction::Content)
    {
        return;
    }
    float minimum = action == UiAction::Content ? .85f : .8f;
    const auto &control = controls[size_t(w.ui.pressed)];
    const auto cursor = CenteredCursor();
    float ratio = Game::Clamp((cursor.x - control.position.x - 16) / (control.size.x - 32), 0, 1);
    float value = std::round((minimum + ratio * (1 - minimum)) * 100) / 100;
    if (action == UiAction::Background)
    {
        w.uiPreferences.background = value;
    }
    if (action == UiAction::Border)
    {
        w.uiPreferences.border = value;
    }
    if (action == UiAction::Content)
    {
        w.uiPreferences.content = value;
    }
}
bool Hud::HandlePointer(int button, bool down)
{
    auto &w = Game::Get().GetState();
    auto controls = Controls();
    bool modal = SaveSystem::Loading() || w.ui.screen != UiScreen::None || w.isPaused || w.journalVisible ||
                 w.dialogueHistoryVisible;
    if (!modal && !Dialogue::Active())
    {
        return false;
    }
    int hit = -1;
    if (w.cursorInsideViewport)
    {
        const auto cursor = CenteredCursor();
        for (size_t i = 0; i < controls.size(); ++i)
        {
            if (Inside(controls[i], cursor))
            {
                hit = int(i);
                break;
            }
        }
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
    {
        return modal;
    }
    if (down)
    {
        w.ui.pressed = hit;
        if (hit >= 0)
        {
            w.ui.focus = hit;
            HandleMotion();
        }
        else if (w.ui.screen == UiScreen::Wardrobe && w.cursorInsideViewport)
        {
            const auto cursor = CenteredCursor();
            if (cursor.x >= 400.f && cursor.x <= 880.f)
            {
                w.ui.wardrobeDragging = true;
                w.ui.wardrobeDragStartX = cursor.x;
                w.ui.wardrobeDragStartYaw = w.ui.wardrobeYawOffset;
            }
        }
    }
    else
    {
        w.ui.wardrobeDragging = false;
        int pressed = w.ui.pressed;
        w.ui.pressed = -1;
        if (hit >= 0 && hit == pressed)
        {
            auto action = controls[size_t(hit)].action;
            bool slider =
                w.ui.screen == UiScreen::Options &&
                (action == UiAction::Background || action == UiAction::Border || action == UiAction::Content);
            if (!slider)
            {
                Activate(controls[size_t(hit)]);
            }
        }
    }
    return modal || hit >= 0;
}
void Hud::ExchangeSupply()
{
    auto &level = game::GameLevels::Chapter();
    const auto &w = Game::Get().GetState();
    auto player =
        w.surfaceExploration && level.IsActive() ? level.Surface().Local(w.playerPosition) : w.playerPosition;
    if (!level.IsActive() || !level.GetState().convoyRescued ||
        Game::Distance(player, {24.5f, 20.5f}) >= 1.5f || Dialogue::Danger())
    {
        return;
    }
    auto &stats = game::GameLevels::Mine().GetState().player;
    int price = CivicJustice::ShopPrice(2);
    if (stats.salvage < price || stats.recoveryKits >= 999)
    {
        return;
    }
    stats.salvage -= price;
    ++stats.recoveryKits;
    Game::Get().ShowNotice("보급 교환 / 부품 -" + std::to_string(price) + " · 회복 키트 +1");
}
void Hud::DrawMedicalFade()
{
    if (!HospitalRecovery::Active())
    {
        return;
    }
    HudShift shift(WorldCamera::CenterOffsetX());
    DrawFullscreenDim({0, 0, 0, HospitalRecovery::Opacity()});
    if (HospitalRecovery::Opacity() > .75f)
    {
        Icon("medical", 602, 328, 64);
        UiText(640 - RenderAssets::MeasureUtf8("의료 이송 중", 26) * .5f, 435, "의료 이송 중", 26);
        UiText(478, 474, "생체 신호 안정화 · 입력할 수 없습니다", 17);
    }
}
void Hud::DrawMenu()
{
    const auto &w = Game::Get().GetState();
    UiScreen screen = SaveSystem::Loading() ? UiScreen::Checkpoint : w.ui.screen;
    if (screen == UiScreen::None && w.isPaused)
    {
        screen = UiScreen::Pause;
    }
    if (screen == UiScreen::None || HospitalRecovery::Active())
    {
        return;
    }
    HudShift shift(WorldCamera::CenterOffsetX());
    if (screen == UiScreen::FacilityShop)
    {
        // Keep the LEFT 1st-person 3D NPC upper-body close-up bright and unobstructed; draw the service list on the RIGHT.
        const auto kind = w.facilityInterior.kind;
        const ColorRGBA accent = kind == UrbanFacilityKind::Market    ? ColorRGBA(.28f, .92f, .64f)
                                 : kind == UrbanFacilityKind::Clinic  ? ColorRGBA(.32f, .92f, .98f)
                                 : kind == UrbanFacilityKind::Workshop ? ColorRGBA(1.f, .70f, .26f)
                                                                      : ColorRGBA(.64f, .80f, 1.f);
        const char *npcName = kind == UrbanFacilityKind::Market    ? "보급 상인"
                              : kind == UrbanFacilityKind::Clinic  ? "의료소 의무관"
                              : kind == UrbanFacilityKind::Workshop ? "정비소 정비사"
                                                                   : "행정소 담당관";
        const char *greeting =
            kind == UrbanFacilityKind::Market
                ? "어서 오세요. 필요한 보급품이나 탄약, 매각할 고철이 있으신가요?"
            : kind == UrbanFacilityKind::Clinic
                ? "상태를 확인해 드릴까요? 응급 치료와 구급 키트 보급이 가능합니다."
            : kind == UrbanFacilityKind::Workshop
                ? "차량이나 비행체 정비가 필요하신가요? 고철로 키트 제작도 됩니다."
                : "행정 창구입니다. 경범죄 벌금 정산이나 지역 의뢰 기록을 확인하실 수 있습니다.";

        // NPC nameplate card across the bottom-left counter edge
        HudCard(48.f, 654.f, 470.f, 66.f, accent, true);
        UiText(68.f, 682.f, npcName, 20, {.90f, .97f, .96f}, true);
        UiText(68.f, 706.f, w.facilityInterior.regionName + " · " + w.facilityInterior.label, 14, accent, true);

        // Right-side header panel (leaves item showcase windows unobstructed for live 3D item props!)
        Panel(566.f, 52.f, 658.f, 224.f);
        UiText(594.f, 90.f, w.facilityInterior.regionName + " · " + w.facilityInterior.label, 14, accent);
        UiText(594.f, 124.f, std::string(npcName) + " · 시각 거래 카탈로그", 24, {.88f, .96f, .94f});
        HudCard(594.f, 140.f, 602.f, 60.f, accent, false);
        const auto quoteLines = Dialogue::Wrap(greeting, 570.f, 15);
        for (size_t i = 0; i < quoteLines.size() && i < 2; ++i)
        {
            UiText(610.f, 165.f + float(i) * 22.f, quoteLines[i], 15, {.86f, .93f, .94f});
        }
        const auto &pStats = game::GameLevels::Mine().GetState().player;
        const std::string wallet = "크레딧 " + std::to_string(pStats.credits) + " Cr  ·  키트 " +
                                   std::to_string(pStats.recoveryKits) + "  ·  고철 " +
                                   std::to_string(pStats.salvage) + "  ·  체력 " +
                                   std::to_string(pStats.health) + "/" + std::to_string(pStats.maxHealth);
        HudCard(594.f, 212.f, 602.f, 38.f, {.42f, .78f, .76f}, false);
        UiText(612.f, 237.f, wallet, 15, {.78f, .94f, .90f});

        const auto services = Game::Get().FacilityServices();
        auto controls = Controls();
        const auto cursor = CenteredCursor();
        for (size_t i = 0; i < controls.size(); ++i)
        {
            if (controls[i].action == UiAction::FacilityService && size_t(controls[i].choice) < services.size())
            {
                const auto &svc = services[size_t(controls[i].choice)];
                const float x = controls[i].position.x;
                const float y = controls[i].position.y;
                const float h = controls[i].size.y;
                const bool hovered = w.cursorInsideViewport && Inside(controls[i], cursor);
                const bool focused = (w.ui.focus == int(i)) || hovered;
                const ColorRGBA frameCol = !svc.enabled ? ColorRGBA{.30f, .36f, .40f, .75f}
                                           : focused    ? ColorRGBA{accent.r, accent.g, accent.b, 1.f}
                                                        : ColorRGBA{.34f, .62f, .66f, .85f};
                // Left 80x92 glass showcase window (transparent when 3D prop is rendered behind it!)
                if (svc.propKind == 0)
                {
                    Drawing::DrawRectangle(x, y, 80.f, h, {.05f, .10f, .14f, .90f});
                    Icon(svc.icon, x + 22.f, y + 28.f, 36.f, false, svc.enabled ? accent : ColorRGBA{.5f, .54f, .58f});
                }
                else
                {
                    Drawing::DrawRectangle(x, y, 80.f, h, {.04f, .09f, .13f, .24f});
                }
                // Right 522x92 item info deck
                Drawing::DrawRectangle(x + 80.f, y, 522.f, h,
                                       focused ? ColorRGBA{.09f, .16f, .22f, .94f}
                                               : ColorRGBA{.05f, .10f, .14f, .92f});
                // Crisp outer border around the combined visual card
                Drawing::DrawRectangle(x, y, 602.f, 2.f, frameCol);
                Drawing::DrawRectangle(x, y + h - 2.f, 602.f, 2.f, frameCol);
                Drawing::DrawRectangle(x, y, 2.f, h, frameCol);
                Drawing::DrawRectangle(x + 600.f, y, 2.f, h, frameCol);
                Drawing::DrawRectangle(x + 80.f, y + 6.f, 2.f, h - 12.f, {.30f, .56f, .60f, .55f});

                // Keycap + Category + Item Title + Action Description + Price Pill
                UiText(x + 94.f, y + 22.f, "[" + std::to_string(i + 1) + "] " + svc.category, 12,
                       svc.enabled ? accent : ColorRGBA{.50f, .56f, .60f});
                UiText(x + 94.f, y + 48.f, svc.title, 20,
                       svc.enabled ? ColorRGBA{.94f, .98f, .99f} : ColorRGBA{.56f, .62f, .66f});
                UiText(x + 94.f, y + 74.f, svc.subtitle, 14,
                       svc.enabled ? ColorRGBA{.70f, .84f, .86f} : ColorRGBA{.48f, .54f, .58f});
                // Price badge pill on right
                Drawing::DrawRectangle(x + 484.f, y + 26.f, 102.f, 38.f,
                                       svc.enabled ? ColorRGBA{.10f, .22f, .24f, .95f}
                                                   : ColorRGBA{.10f, .12f, .14f, .85f});
                Drawing::DrawRectangle(x + 484.f, y + 26.f, 102.f, 2.f, frameCol);
                UiText(x + 576.f, y + 51.f, svc.priceLabel, 17,
                       svc.enabled ? accent : ColorRGBA{.55f, .58f, .60f}, false, true);
            }
            else
            {
                Button(controls[i], int(i));
            }
        }
        if (w.noticeSecondsRemaining > 0 && !w.noticeText.empty())
        {
            const auto noticeLines = Dialogue::Wrap(w.noticeText, 570.f, 15);
            for (size_t i = 0; i < noticeLines.size() && i < 2; ++i)
            {
                UiText(594.f, 616.f + float(i) * 20.f, noticeLines[i], 15, {.58f, .95f, .84f});
            }
        }
        UiText(594.f, 722.f, "W/S 선택 · Enter/클릭/숫자키 구매·이용 · Esc 대화 종료", 13, {.58f, .74f, .76f});
        return;
    }
    if (screen == UiScreen::Wardrobe)
    {
        // Keep the CENTER 3D character turntable (x = 398..882) 100% unobstructed and bright!
        Panel(30.f, 76.f, 364.f, 656.f);
        Panel(886.f, 76.f, 364.f, 656.f);
        HudCard(424.f, 24.f, 432.f, 46.f, {.38f, .92f, .88f}, false);
        UiText(640.f - RenderAssets::MeasureUtf8("외관 옷장 · 3D 실시간 파트 커스터마이즈", 18) * .5f, 54.f,
               "외관 옷장 · 3D 실시간 파트 커스터마이즈", 18, {.92f, .98f, .98f}, true);

        UiText(48.f, 114.f, "기본 외형 · 색상", 20, {.48f, .94f, .88f});
        UiText(48.f, 138.f, "항목 선택 시 중앙 3D 모델에 즉시 반영", 13, {.68f, .82f, .86f});
        UiText(48.f, 400.f, "코트 색상 팔레트", 14, {.78f, .90f, .92f});

        UiText(904.f, 114.f, w.playerModularMedic ? "모듈러 파츠 탈부착 (11종)" : "빠른 의상 프리셋", 20,
               {.48f, .94f, .88f});
        UiText(904.f, 138.f,
               w.playerModularMedic ? "클릭해 각 장비를 실시간 착용/해제" : "역할별 표준 복장 또는 Mixamo 모듈러 선택",
               13, {.68f, .82f, .86f});

        auto controls = Controls();
        for (size_t i = 0; i < controls.size(); ++i)
        {
            const auto &ctrl = controls[i];
            if (ctrl.action == UiAction::WardrobeCoatColor)
            {
                const ColorRGBA swatch = Drawing::PlayerCoatColor(ctrl.choice);
                const bool active = !w.playerModularMedic && w.playerCoatColor == ctrl.choice;
                const ColorRGBA border = active ? ColorRGBA{1.f, .94f, .52f, 1.f} : ColorRGBA{.32f, .48f, .54f, .85f};
                Drawing::DrawRectangle(ctrl.position.x, ctrl.position.y, ctrl.size.x, ctrl.size.y,
                                       w.playerModularMedic ? ColorRGBA{swatch.r * .4f, swatch.g * .4f, swatch.b * .4f, .6f}
                                                            : swatch);
                Drawing::DrawRectangle(ctrl.position.x, ctrl.position.y, ctrl.size.x, 3.f, border);
                Drawing::DrawRectangle(ctrl.position.x, ctrl.position.y + ctrl.size.y - 3.f, ctrl.size.x, 3.f, border);
                Drawing::DrawRectangle(ctrl.position.x, ctrl.position.y, 3.f, ctrl.size.y, border);
                Drawing::DrawRectangle(ctrl.position.x + ctrl.size.x - 3.f, ctrl.position.y, 3.f, ctrl.size.y, border);
                if (active)
                {
                    UiText(ctrl.position.x + 28.f, ctrl.position.y + 27.f, "적용 중", 14, {1.f, 1.f, 1.f}, true);
                }
            }
            else
            {
                Button(ctrl, int(i));
            }
        }
        HudCard(424.f, 722.f, 432.f, 38.f, {.36f, .84f, .82f}, false);
        UiText(446.f, 747.f, "←/→ · A/D · 중앙 드래그로 360° 회전  |  Esc 적용 후 닫기", 13,
               {.78f, .92f, .92f}, true);
        return;
    }
    if (screen == UiScreen::HomeStorage)
    {
        const auto &p = game::GameLevels::Mine().GetState().player;
        const auto &r = w.residence;
        Panel(72.f, 52.f, 1136.f, 110.f);
        UiText(104.f, 92.f, "헤일로 거주지 · 시각 물자 보관함", 15, {.48f, .94f, .88f});
        UiText(104.f, 130.f, "보관함 · 안전 금고", 28, {.92f, .98f, .98f});
        UiText(410.f, 128.f,
               "장비 강화 현황: 방호복 +" + std::to_string(r.armorTier) + "/3 · 무기 +" +
                   std::to_string(r.weaponTier) + "/3",
               16, {.74f, .88f, .90f});

        struct StorageCardDesc
        {
            float x;
            const char *title;
            const char *icon;
            ColorRGBA accent;
            std::string stockLine;
            std::string descLine;
            float fillRatio;
        };
        const StorageCardDesc cards[3] = {
            {96.f,
             "크레딧 안전 금고",
             "credit",
             {1.f, .78f, .32f},
             "휴대 " + std::to_string(p.credits) + " Cr  /  금고 " + std::to_string(r.stashCredits) + " Cr",
             "금고에 예치된 크레딧은 의료 이송 시 차감되지 않습니다.",
             Game::Clamp(float(r.stashCredits) / 2000.f, 0.f, 1.f)},
            {470.f,
             "응급 회복 키트",
             "heal",
             {.36f, .96f, .78f},
             "휴대 " + std::to_string(p.recoveryKits) + "/" + std::to_string(ResidenceState::kMaxCarryKits) +
                 "  /  보관 " + std::to_string(r.stashRecoveryKits) + "/" +
                 std::to_string(ResidenceState::kMaxStashKits),
             "현장에서 G키로 즉시 체력 +45를 회복하는 필수 생존 키트.",
             Game::Clamp(float(r.stashRecoveryKits) / float(ResidenceState::kMaxStashKits), 0.f, 1.f)},
            {844.f,
             "고철 · 정비 부품",
             "parts",
             {.46f, .84f, 1.f},
             "휴대 " + std::to_string(p.salvage) + "개  /  보관 " + std::to_string(r.stashSalvage) + "개",
             "작업대 장비 강화, 키트 제작, 차량·비행체 수리에 사용됩니다.",
             Game::Clamp(float(r.stashSalvage) / 50.f, 0.f, 1.f)},
        };
        for (const auto &c : cards)
        {
            const float x = c.x;
            const float y = 186.f;
            const float wCard = 340.f;
            // Upper 186..380: transparent glass showcase frame revealing the live spinning 3D prop!
            Drawing::DrawRectangle(x, y, wCard, 194.f, {.05f, .09f, .13f, .22f});
            Drawing::DrawRectangle(x, y, wCard, 3.f, c.accent);
            Drawing::DrawRectangle(x, y, 2.f, 418.f, {.30f, .54f, .60f, .85f});
            Drawing::DrawRectangle(x + wCard - 2.f, y, 2.f, 418.f, {.30f, .54f, .60f, .85f});
            Drawing::DrawRectangle(x, y + 416.f, wCard, 2.f, {.30f, .54f, .60f, .85f});
            // Lower 380..604: solid info & deposit/withdraw deck
            Drawing::DrawRectangle(x + 2.f, y + 194.f, wCard - 4.f, 222.f, {.05f, .09f, .13f, .94f});
            Drawing::DrawRectangle(x + 12.f, y + 194.f, wCard - 24.f, 2.f, {c.accent.r, c.accent.g, c.accent.b, .48f});
            Icon(c.icon, x + 18.f, y + 210.f, 28.f, false, c.accent);
            UiText(x + 56.f, y + 232.f, c.title, 20, {.94f, .98f, .99f});
            UiText(x + 20.f, y + 266.f, c.stockLine, 15, c.accent);
            // Visual stash fill bar
            Drawing::DrawRectangle(x + 20.f, y + 278.f, wCard - 40.f, 8.f, {.03f, .05f, .07f, .95f});
            Drawing::DrawRectangle(x + 20.f, y + 278.f, (wCard - 40.f) * c.fillRatio, 8.f, c.accent);
            const auto wrapped = Dialogue::Wrap(c.descLine, wCard - 40.f, 14);
            for (size_t line = 0; line < wrapped.size() && line < 2; ++line)
            {
                UiText(x + 20.f, y + 310.f + float(line) * 20.f, wrapped[line], 14, {.72f, .84f, .88f});
            }
        }

        auto controls = Controls();
        for (size_t i = 0; i < controls.size(); ++i)
        {
            Button(controls[i], int(i));
        }
        if (w.noticeSecondsRemaining > 0 && !w.noticeText.empty())
        {
            UiText(640.f - RenderAssets::MeasureUtf8(w.noticeText, 15) * .5f, 646.f, w.noticeText, 15,
                   {.58f, .96f, .86f}, true);
        }
        return;
    }
    DrawFullscreenDim({.01f, .025f, .03f, .55f});
    auto &stats = game::GameLevels::Mine().GetState().player;
    bool narrow = screen == UiScreen::Title || screen == UiScreen::Pause;
    if (screen == UiScreen::Title)
    {
        Drawing::DrawLine({96, 120}, {162, 120}, {.65f, .9f, .81f}, 3);
    }
    else if (screen == UiScreen::Pause)
    {
        Panel(376, 56, 528, 688);
    }
    else if (screen == UiScreen::Stages)
    {
        Panel(64, 48, 1152, 704);
    }
    else if (screen == UiScreen::Help || screen == UiScreen::Options || screen == UiScreen::Summary)
    {
        Panel(64, 48, 1152, 704);
    }
    else if (screen == UiScreen::Checkpoint || screen == UiScreen::PlanetProfiles)
    {
        Panel(220, 90, 840, 620);
    }
    else
    {
        Panel(312, 90, 656, 600);
    }
    float x = screen == UiScreen::Title || screen == UiScreen::Stages                                  ? 96.f
              : screen == UiScreen::Help || screen == UiScreen::Options || screen == UiScreen::Summary ? 96.f
              : screen == UiScreen::Checkpoint || screen == UiScreen::PlanetProfiles                   ? 294.f
              : narrow ? 408.f
                       : 336.f;
    if (screen == UiScreen::Checkpoint)
    {
        Icon("checkpoint", 254, 134, 28, false, {.65f, .9f, .81f});
    }
    else if (screen == UiScreen::PlanetProfiles)
    {
        Icon("relay", 254, 134, 28, false, {.65f, .9f, .81f});
    }
    const char *title = screen == UiScreen::Title            ? "침묵하는 관문"
                        : screen == UiScreen::Pause          ? "일시정지"
                        : screen == UiScreen::Stages         ? "스테이지 목록 · 테스트 진입"
                        : screen == UiScreen::Options        ? "옵션"
                        : screen == UiScreen::Summary        ? "보유 · 성장 정보"
                        : screen == UiScreen::Help           ? "조작 안내"
                        : screen == UiScreen::Supply         ? "운송대 보급 교환"
                        : screen == UiScreen::Medical        ? "의료소 복귀"
                        : screen == UiScreen::PlanetProfiles ? "행성 선택 · 탐사 테스트"
                        : screen == UiScreen::Checkpoint     ? "월드 저장 · 불러오기"
                                                             : "진행 확인";
    UiText(x,
           screen == UiScreen::Title   ? 253
           : screen == UiScreen::Pause ? 144
           : screen == UiScreen::Stages || screen == UiScreen::Help || screen == UiScreen::Options ||
                   screen == UiScreen::Summary
               ? 132
               : 159,
           title,
           screen == UiScreen::Title   ? 54
           : screen == UiScreen::Pause ? 32
                                       : 28,
           {.83f, .95f, .91f});
    auto controls = Controls();
    for (size_t i = 0; i < controls.size(); ++i)
    {
        Button(controls[i], int(i));
    }
    if (screen == UiScreen::PlanetProfiles)
    {
        const std::string text = SaveSystem::Message().empty()
                                     ? "지상에서 하차한 뒤 선택하세요. 현재 행성의 탐험 기록은 유지됩니다."
                                     : SaveSystem::Message();
        const auto pages = TextPages(text, 740, 18, 3);
        const size_t page = size_t((std::max)(0, w.lastUpdateMilliseconds) / 5000) % pages.size();
        for (size_t i = 0; i < pages[page].size(); ++i)
        {
            UiText(254, 520 + float(i) * 26, pages[page][i], 18, {.67f, .9f, .85f});
        }
        UiText(254, 636, "행성 간 비행·스토리 이동은 이후 단계에서 연결합니다.", 16);
    }
    if (screen == UiScreen::Checkpoint)
    {
        const auto reason = SaveSystem::Restriction();
        if (SaveSystem::Loading())
        {
            UiText(254, 430, "저장된 월드를 준비하고 있습니다. Esc로 취소할 수 있습니다.", 18, {.65f, .9f, .81f});
        }
        else
        {
            if (reason)
            {
                const auto rPages = TextPages(reason, 740, 17, 2);
                const size_t rPage = size_t((std::max)(0, w.lastUpdateMilliseconds) / 5000) % rPages.size();
                for (size_t i = 0; i < rPages[rPage].size(); ++i)
                {
                    UiText(254, 418 + float(i) * 24, rPages[rPage][i], 17, {1, .81f, .5f});
                }
            }
            const auto text = !SaveSystem::Message().empty() ? SaveSystem::Message()
                              : reason
                                  ? std::string(reason)
                                  : "현재 월드를 수동으로 저장합니다. 불러오면 저장 이후의 진행을 되돌립니다.";
            const auto pages = TextPages(text, 740, 18, 3);
            const size_t page = size_t((std::max)(0, w.lastUpdateMilliseconds) / 5000) % pages.size();
            for (size_t i = 0; i < pages[page].size(); ++i)
            {
                UiText(254, 486 + float(i) * 26, pages[page][i], 18, {.67f, .9f, .85f});
            }
        }
        const auto pathPages = TextPages("저장 위치: " + SaveSystem::DefaultPath().u8string(), 740, 14, 3);
        const size_t pathPage = size_t((std::max)(0, w.lastUpdateMilliseconds) / 5000) % pathPages.size();
        for (size_t i = 0; i < pathPages[pathPage].size(); ++i)
        {
            UiText(254, 606 + float(i) * 19, pathPages[pathPage][i], 14, {.55f, .7f, .72f});
        }
        if (pathPages.size() > 1)
        {
            UiText(254, 668,
                   "경로 페이지 " + std::to_string(pathPage + 1) + " / " + std::to_string(pathPages.size()),
                   13, {.55f, .7f, .72f});
        }
    }
    if (screen == UiScreen::Options)
    {
        UiText(96, 86, "화면 설정", 14, {.65f, .9f, .81f});
        UiText(96, 164, "정보의 크기와 대비를 해치지 않는 범위에서 조절합니다.", 18);
        const auto &p = w.uiPreferences;
        for (int i = 0; i < 3; ++i)
        {
            float alpha = i == 0 ? p.background : i == 1 ? p.border : p.content;
            UiText(624, 241 + i * 104, std::to_string(int(std::round(alpha * 100))) + "%", 17, kUiInk, false,
                   true);
            Sprite("Components/gauge_track", 112, 270 + i * 104, 516, 5);
            Sprite("Components/gauge_scan", 112, 270 + i * 104, 516, 5, {1, 1, 1},
                   (alpha - (i == 2 ? .85f : .8f)) / (i == 2 ? .15f : .2f));
        }
        const auto &style = GameRenderer::Get().Backend().GetStyle();
        UiText(1148, 232, style.cartoon ? "켜짐" : "꺼짐", 17, kUiInk, false, true);
        UiText(1148, 314,
               style.resolution == WorldResolution::Native ? "고해상도"
               : style.resolution == WorldResolution::Half ? "1/2 픽셀"
                                                           : "1/4 픽셀",
               17, kUiInk, false, true);
        int samples = GameRenderer::Get().Backend().GetAntialiasingSamples();
        UiText(1148, 396,
               !style.antialiasing                           ? "꺼짐"
               : style.resolution != WorldResolution::Native ? "픽셀 모드 미적용"
               : samples <= 1                                ? "미지원"
                                                             : std::to_string(samples) + "× MSAA",
               17, kUiInk, false, true);
        const int resIdx =
            std::clamp(w.displayPreferences.resolutionIndex, 0, int(platform::kResolutionOptions.size()) - 1);
        UiText(1148, 478, platform::kResolutionOptions[size_t(resIdx)].label, 16, {.65f, .94f, .86f}, false, true);
        UiText(1148, 560, w.displayPreferences.fullscreen ? "켜짐 (Alt+Enter)" : "꺼짐 (창 모드)", 16,
               w.displayPreferences.fullscreen ? ColorRGBA{.65f, .94f, .86f} : kUiInk, false, true);
        UiText(336, 641, "선택 후 A/D 또는 ←/→ 조절 · Enter 전환 · Alt+Enter 전체화면", 15);
        UiText(336, 665, "최대 4K 네이티브 렌더 지원 · 디스플레이 설정 자동 저장", 14, {.59f, .75f, .75f});
    }
    if (screen == UiScreen::Title || screen == UiScreen::Pause)
    {
        if (screen == UiScreen::Pause && w.noticeText == SaveSystem::Message() &&
            w.noticeSecondsRemaining > 0)
        {
            DrawNoticePanel();
        }
        const bool titleScreen = screen == UiScreen::Title;
        UiText(x, titleScreen ? 166 : 98, titleScreen ? "에르마 · 헤일로" : "플레이를 잠시 멈췄습니다", 15,
               {.59f, .79f, .77f});
        if (titleScreen)
        {
            UiText(96, 299, "신호가 닿은 곳에서, 이야기가 시작된다.", 20);
        }
        UiText(x, titleScreen ? 742 : 704, "F5 수동 저장 · F9 저장/불러오기", 15, {.95f, .77f, .4f});
        if (!titleScreen)
        {
            UiText(x, 730, "W/S · 방향 키 선택 / Enter 확인", 14, {.59f, .75f, .75f});
        }
    }
    if (screen == UiScreen::Stages)
    {
        UiText(96, 86, "오픈월드 챕터 · 구간 테스트", 14, {.65f, .9f, .81f});
        UiText(96, 164, "모든 챕터와 구간이 에르마 오픈월드 행성 위에서 즉시 시작됩니다.", 18);
        UiText(96, 636, "선택 시 현재 진행이 초기화되며 이전 필수 스토리가 자동 반영됩니다.", 18,
               {1, .88f, .65f});
        UiText(1176, 711, "방향 키 이동 · Enter 선택 · Esc 뒤로", 14, kUiInk, false, true);
        Drawing::DrawLine({780, 211}, {780, 567}, {.18f, .28f, .32f});
        int stage = (std::min)(int(TestStage::Count) - 1, (std::max)(0, w.ui.focus));
        const char *descriptions[] = {
            "에르마 헤일로 거주지에서 서장을 시작합니다. 문을 나서면 행성 전역으로 이어집니다.",
            "헤일로 전망대 관측 데크에서 시작합니다. 하강하면 헤일로 오픈월드 광장으로 이어집니다.",
            "헤일로 오픈월드 광장에서 바이크에 탑승한 채 오르딘 광산으로 바로 출발합니다.",
            "오르딘 광산 표면 입구에서 시작합니다. 권총이 해금되며 리아가 동행합니다.",
            "오르딘 광산 내부에서 시작합니다. 출구로 나오면 오픈월드 광산 입구로 복귀합니다.",
            "1장 빛나는 개척지 오픈월드 구역에 직접 진입합니다. 세 현장을 자유 순서로 조사합니다.",
            "2장 외곽 수로 조사(S20~S25): N-0 외곽 진입 · 왜곡 표식 · 폐측량소 · 옛 야영지 · 봉쇄 중계실.",
            "3장 관문이 열리는 밤(S30~S36): 외곽 차단 설비 · 헤일로 07구역 대피 · 외부 제어소 · 관문 내부.",
            "4장 닫히는 문(S40~S44): 핵심 결합부 폐쇄 후 에르마 오픈월드 복구 헤일로로 귀환합니다.",
            "에필로그 빈 채널(S50~S52): 오픈월드 복구 헤일로에서 마라·의료소·카인과 17일 후 신호를 확인합니다.",
            "에르마 행성의 도시·광산·개척지·랜덤 인카운터·중계환을 자유롭게 탐사합니다."};
        Icon("stage", 824, 222, 36);
        auto heading = Dialogue::Wrap(Game::StageName(TestStage(stage)), 328, 24);
        for (size_t i = 0; i < heading.size(); ++i)
        {
            UiText(824, 306 + float(i) * 34, heading[i], 24);
        }
        auto description = Dialogue::Wrap(descriptions[stage], 328, 18);
        for (size_t i = 0; i < description.size(); ++i)
        {
            UiText(824, 326 + float(heading.size()) * 34 + float(i) * 28, description[i], 18);
        }
    }
    if (screen == UiScreen::Summary)
    {
        UiText(96, 86, "플레이어", 14, {.65f, .9f, .81f});
        UiText(96, 164, "현재 플레이어의 보유량과 성장 상태입니다.", 18);
        Icon("level", 96, 221, 30);
        UiText(150, 252, "레벨 " + std::to_string(stats.level), 25);
        int floor = game::MineLevel::ExperienceForLevel(stats.level),
            next = game::MineLevel::ExperienceForLevel((std::min)(100, stats.level + 1));
        UiText(96, 297, "경험치 " + std::to_string(stats.experience) + " / " + std::to_string(next), 18);
        Sprite("Components/gauge_track", 96, 314, 1072, 8);
        Sprite("Components/gauge_experience", 96, 314, 1072, 8, {1, 1, 1},
               Game::Clamp(stats.level >= 100 ? 1.f
                                              : float(stats.experience - floor) / (std::max)(1, next - floor),
                           0, 1));
        const std::string rows[] = {
            std::to_string(stats.credits), std::to_string(stats.salvage), std::to_string(stats.recoveryKits),
            std::to_string(stats.attack) + " / " + std::to_string(stats.defense),
            std::to_string(int(CivicJustice::State().infamy)) + " / 거래 할증 +" +
                std::to_string(int(std::round((CivicJustice::PriceMultiplier() - 1) * 100))) + "%"};
        const char *icons[] = {"credit", "parts", "heal", "defense", "reputation"};
        const char *labels[] = {"크레딧", "거래 부품", "회복 키트", "공격 / 방어", "악명 / 거래 할증"};
        for (int i = 0; i < 5; ++i)
        {
            Icon(icons[i], 96, 358 + i * 52, 22);
            UiText(136, 379 + i * 52, labels[i], 18);
            UiText(1152, 379 + i * 52, rows[i], 20, kUiInk, false, true);
        }
    }
    if (screen == UiScreen::Help)
    {
        UiText(96, 86, "필드 가이드", 14, {.65f, .9f, .81f});
        UiText(96, 164, "도보·전투·탐험에서 사용하는 조작입니다.", 18);
        const char *keys[] = {
            "WASD / Shift", "우클릭 홀드", "좌클릭 / V",     "Q 홀드 + 마우스",      "R / G", "E",
            "F / M",        "J / H",       "E·Enter / 숫자", "바이크 W·S·AD / Space"};
        const char *rows[] = {"이동 · 이동 방향 회전 / 누른 채 달리기",
                              "권총 장착 시 커서 방향 조준",
                              "장착 무기 공격 / 현재 임무 수령·수행 지점 경로 안내",
                              "무기 선택 · Q 해제로 확정",
                              "재장전 / 광산·1장 회복 키트",
                              "조사·대화·승하차·출입",
                              "광산 스캔 / 지도 (클릭 지정·우클릭 해제)",
                              "조사 기록 / 대사 이력 · PageUp/Down 페이지",
                              "대화 진행 / 선택지 선택 후 확정",
                              "도보 Space 회피 / 바이크 가속·제동/후진·조향·부스트"};
        for (int i = 0; i < 10; ++i)
        {
            float column = i % 2 ? 660.f : 96.f, y = 219.f + (i / 2) * 84;
            UiText(column, y, keys[i], 20, {.65f, .9f, .81f});
            auto wrapped = Dialogue::Wrap(rows[i], 508, 18);
            for (size_t row = 0; row < wrapped.size(); ++row)
            {
                UiText(column, y + 29 + float(row) * 24, wrapped[row], 18);
            }
        }
        auto footer = Dialogue::Wrap(
            "비행체: W/S 전후진 · A/D 선회 · Space/C 상승·하강 · Shift+W 고속 순항 · X 제동 · E 착륙/하차  |  "
            "I 보유 정보 · ? 도움말 · Esc 일시정지 · F5 저장 · F9 불러오기",
            1072, 15);
        for (size_t line = 0; line < footer.size(); ++line)
        {
            UiText(96, 616 + float(line) * 22, footer[line], 15);
        }
    }
    if (screen == UiScreen::Supply)
    {
        Icon("parts", 365, 236, 40);
        Icon("heal", 835, 236, 40);
        UiText(594, 269, "→", 32);
        int price = CivicJustice::ShopPrice(2);
        UiText(336, 326, "부품 " + std::to_string(price) + "개 → 회복 키트 1개", 24);
        UiText(336, 369,
               "보유 부품 " + std::to_string(stats.salvage) + " / 회복 키트 " +
                   std::to_string(stats.recoveryKits),
               18);
        UiText(336, 410,
               "평판 거래 할증 +" +
                   std::to_string(int(std::round((CivicJustice::PriceMultiplier() - 1) * 100))) + "%",
               17, {1, .78f, .4f});
        UiText(336, 451,
               stats.recoveryKits >= 999 ? "회복 키트 보유 상한에 도달했습니다"
               : stats.salvage < price   ? "교환할 부품이 부족합니다"
                                         : "교환할 수 있습니다",
               17);
        if (w.noticeSecondsRemaining > 0 && w.noticeText.rfind("보급 교환 /", 0) == 0)
        {
            auto result = Dialogue::Wrap(w.noticeText, 600, 16);
            for (size_t i = 0; i < result.size(); ++i)
            {
                UiText(336, 625 + i * 24, result[i], 16, {.57f, .91f, .84f});
            }
        }
    }
    if (screen == UiScreen::Confirm)
    {
        auto rows = Dialogue::Wrap(w.ui.confirmation == UiAction::Quit
                                       ? "게임을 종료할까요? 마지막 수동 저장 이후의 진행은 사라집니다."
                                   : game::GameLevels::Chapter().IsActive()
                                       ? "조사 기록을 유지하고 헤일로로 귀환할까요?"
                                   : game::GameLevels::Mine().IsActive()
                                       ? "광산 현장을 다시 생성할까요? 성장·보유품·조사 진행은 유지됩니다."
                                       : "새 게임을 시작할까요? 현재 세션의 진행은 초기화됩니다.",
                                   592, 20);
        Icon("warning", 336, 202, 32);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            UiText(336, 281 + i * 32, rows[i], 20);
        }
    }
    if (screen == UiScreen::Medical)
    {
        const auto &law = CivicJustice::State();
        Icon("medical", 604, 194, 56);
        std::string city = law.hospitals.empty()
                               ? "의료소"
                               : Overworld::Cities()[law.hospitals[size_t(law.hospital)].city].name;
        UiText(336, 302, city + " 의료소에서 회복했습니다", 24);
        UiText(336, 355,
               "레벨 -" + std::to_string(law.lostLevels) + " · 크레딧 -" + std::to_string(law.lostCredits) +
                   " · 부품 -" + std::to_string(law.lostSalvage),
               18, {1, .78f, .4f});
        UiText(336, 404, "조사 기록은 유지되며, 도시 간 악명은 남습니다.", 18);
        if (w.dialogue.suspended)
        {
            UiText(336, 451, "중단한 대화가 있습니다.", 18);
            UiText(336, 483, "계속한 뒤 J 기록에서 Enter로 재개할 수 있습니다.", 16);
        }
    }
}

void PlanetHud::Draw()
{
    auto session = Game::Get().Surface();
    if (!session)
    {
        return;
    }
    auto snapshot = session->Stream().Snapshot();
    const auto mode = Game::Get().Mode();
    if (!session->Ready() && mode == WorldMode::SurfaceExterior)
    {
        HudShift shift(WorldCamera::CenterOffsetX());
        Panel(390, 320, 500, 138);
        UiText(420, 365,
               snapshot->statistics.error.empty() ? session->Planet().name + " 탐사 준비"
                                                  : "탐사 지형을 불러오지 못했습니다.",
               22);
        UiText(420, 405,
               snapshot->statistics.error.empty() ? "주변 지형을 준비하고 있습니다."
                                                  : "Esc 메뉴에서 다시 진입해 주세요.",
               17);
        return;
    }
    // CQ-12: Centered bottom control strip (x=328..952, y=758..786), strictly non-overlapping with Vitals & Weapon HUD.
    auto drawBottomControls = [](const char *text) {
        HudShift shift(WorldCamera::CenterOffsetX());
        const float textW = RenderAssets::MeasureUtf8(text, 13);
        const float boxW = (std::min)(636.f, textW + 28.f);
        const float boxX = 640.f - boxW * .5f;
        HudCard(boxX, 758.f, boxW, 28.f, {.36f, .82f, .80f}, false);
        UiText(640.f - textW * .5f, 777.f, text, 13, {.78f, .90f, .89f}, true);
    };
    const auto &state = Game::Get().GetState();
    if (mode == WorldMode::SurfaceFacility)
    {
        const std::string area = state.facilityInterior.regionName + " · " + state.facilityInterior.label;
        Hud::DrawActivityObjective(area + " · E 카운터/출입문 · J 조사 기록");
        const auto &player = game::GameLevels::Mine().GetState().player;
        const std::string info = "소지 " + std::to_string(player.credits) + " Cr · 회복 키트 " +
                                 std::to_string(player.recoveryKits) + " · 고철 " +
                                 std::to_string(player.salvage);
        HudCard(20.f, 134.f, 340.f, 26.f, {.42f, .90f, .78f}, false);
        UiText(34, 152, info, 13, {.68f, .92f, .84f}, true);
        CityHud::DrawInteractionPrompt();
        drawBottomControls("WASD 이동 · E 담당자 대화/퇴장 · +/- 줌 · J 기록 · Esc 메뉴");
        return;
    }
    if (mode == WorldMode::SurfaceInterior || mode == WorldMode::SurfaceObservatory)
    {
        const std::string area =
            mode == WorldMode::SurfaceInterior ? "헤일로 · 거주지" : "헤일로 · 전망대";
        Hud::DrawActivityObjective(area + " · E 대화·조사 / M 지도");
        CityHud::DrawInteractionPrompt();
        drawBottomControls("WASD 이동 · E 상호작용 · J 조사 기록 · Esc 메뉴");
        return;
    }
    auto location = Game::Get().PlanetLocation().value_or(session->Location());
    std::string area = session->Planet().name + " 야외";
    if (location.regionId != "overworld")
    {
        area = snapshot->planet->Region(location.regionId).definition.name;
    }
    Hud::DrawActivityObjective(area + " · E 대화·조사 / M 지도");
    DrawMap();
    CityHud::DrawInteractionPrompt();
    auto ll = world::SurfaceGeometry::ToDegrees(location.position);
    char coordinates[160];
    std::snprintf(coordinates, sizeof(coordinates), "%s · %.4f° / %.4f° · %.0fm", area.c_str(), ll[0], ll[1],
                  location.position.altitudeMeters - world::PlanetTerrain::SeaLevelMeters);
    const float coordW = (std::min)(472.f, RenderAssets::MeasureUtf8(coordinates, 13) + 28.f);
    HudCard(20.f, 134.f, coordW, 26.f, {.36f, .78f, .78f}, false);
    UiText(34, 152, coordinates, 13, {.72f, .88f, .86f}, true);
    const char *controls =
        state.aircraft.mounted ? "W/S 이동 · A/D 조향 · Shift 순항 · Space/C 고도 · X 제동 · E 착륙/하차"
        : state.car.mounted    ? "W 가속 · S 제동/후진 · A/D 조향 · E 하차 · M 지도"
        : state.isBikeMounted  ? "W 가속 · S 제동/후진 · A/D 조향 · Space 부스트 · E 하차 · M 지도"
                               : "WASD 이동 · Shift 달리기 · Space 회피 · V 경로 안내 · R 재장전 · G 회복 · E 상호작용 · M 지도";
    drawBottomControls(controls);
}
void PlanetHud::DrawMap(bool full)
{
    profiling::Scope mapTimer(profiling::Timer::PlanetMap);
    auto session = Game::Get().Surface();
    if (!session || !session->Ready())
    {
        return;
    }
    const auto &w = Game::Get().GetState();
    HudShift shift(full ? WorldCamera::CenterOffsetX() : WorldCamera::RightOffsetX());
    auto snapshot = session->Stream().Snapshot();
    float radius = full ? kGlobeRadius : kMinimapRadius, range = full ? w.planetMapRange : 160.f;
    Vector2 center = full ? kGlobeCenter : kMinimapCenter;
    if (full)
    {
        const float t = Game::Clamp(w.mapHologramProgress, 0.02f, 1.f);
        const float ease = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
        DrawFullscreenDim({.012f, .030f, .042f, 0.66f * ease});
        Panel(64, 82, 1152, 642);
        DrawWristHologramEmitterOverlay(w.mapHologramProgress);
        Button(SpaceModeToggleControl(w.mapSpaceMode), -1);
        if (w.mapSpaceMode)
        {
            DrawSpaceSystemMap(*session);
            return;
        }
        if (w.planetDestination)
        {
            Button(ClearDestinationControl(true), -1);
        }
        UiText(96, 124,
               range > 200000 ? "3D 행성 구체 · 전역 궤도도"
               : range > 4000 ? "3D 행성 구체 · 지역 탐사도"
                              : "3D 행성 구체 · 블록 정밀도",
               24, {.86f, .98f, .96f});
        char scale[80];
        if (range >= 1000)
        {
            std::snprintf(scale, sizeof(scale), "표시 반경 %.2f km", range / 1000);
        }
        else
        {
            std::snprintf(scale, sizeof(scale), "표시 반경 %.0f m", range);
        }
        UiText(820, 196, scale, 18);
        UiText(96, 150, "좌클릭 드래그 3D 구체 회전(줌 연동) · 우클릭 목적지 지정", 13, {.74f, .90f, .90f});
        UiText(96, 170, "휠 확대·축소 · C 내 위치 · P 전체 구체 · G 우주 지도", 13, {.70f, .84f, .84f});
        UiText(820, 626, "현장 E 등록 · 주차 후 이동", 15);
        UiText(820, 659, "차량은 출발지에 남습니다.", 15);
    }
    if (full)
    {
        // Volumetric 3D planetary atmosphere halo + holographic bezel ring.
        Drawing::DrawGlow(center, radius + 24.f, {.16f, .76f, .88f, .24f});
        Drawing::DrawEllipse(center.x, center.y, radius + 8.f, radius + 8.f, {.18f, .64f, .78f, .22f});
        Drawing::DrawEllipse(center.x, center.y, radius + 3.f, radius + 3.f, {.38f, .82f, .86f, .90f});
        Drawing::DrawEllipse(center.x, center.y, radius, radius, {.028f, .078f, .115f, 1.f});
    }
    else
    {
        Drawing::DrawEllipse(center.x, center.y, radius + 3, radius + 3, {.35f, .62f, .61f});
        Drawing::DrawEllipse(center.x, center.y, radius, radius, {.075f, .14f, .14f});
    }
    auto local = session->Local(session->Pose().origin);
    auto view = PlanetMapView(full);
    const GlobeView globe = GlobeView::Build(*session);
    const world::SurfacePoint focus =
        full ? CurrentGlobeCenter(*session) : session->PointAt(view.worldCenter.x, view.worldCenter.y);
    mapLabels.clear();
    const double planetRadius = session->Planet().radiusMeters;
    const auto &originFrame = session->Origin();
    const float pxPerMeter = full ? globe.PixelsPerMeter() : view.scale;
    auto project = [&](const world::SurfacePoint &point) {
        auto p = world::SurfaceAtlas::Project(originFrame, point, planetRadius);
        return Vector2{float(p.x), float(p.y)};
    };
    auto toScreen = [&](const world::SurfacePoint &point, bool *frontOut = nullptr) -> Vector2 {
        if (full)
        {
            return globe.Project(point, frontOut);
        }
        if (frontOut)
        {
            *frontOut = true;
        }
        return view.Project(project(point));
    };
    auto insideDisc = [&](Vector2 p, float inset = 0.f) {
        return Game::Distance(p, center) <= radius - inset;
    };
    auto drawScreenLine = [&](Vector2 a, Vector2 b, ColorRGBA color, float width) {
        // Clip line segment analytically to the circular map disc.
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float fx = a.x - center.x, fy = a.y - center.y;
        const float r = (std::max)(1.f, radius - 1.f);
        const float qa = dx * dx + dy * dy;
        if (qa < 1e-6f)
        {
            return;
        }
        const float qb = 2.f * (fx * dx + fy * dy);
        const float qc = fx * fx + fy * fy - r * r;
        const float disc = qb * qb - 4.f * qa * qc;
        if (disc < 0.f)
        {
            return;
        }
        const float s = std::sqrt(disc);
        const float t0 = (std::max)(0.f, (-qb - s) / (2.f * qa));
        const float t1 = (std::min)(1.f, (-qb + s) / (2.f * qa));
        if (t1 <= t0)
        {
            return;
        }
        Drawing::DrawLine({a.x + dx * t0, a.y + dy * t0}, {a.x + dx * t1, a.y + dy * t1}, color, width);
    };
    auto drawSurfacePin = [&](const world::SurfacePoint &pt, ColorRGBA color, const char *label,
                              bool showLabel, float pinRadius) {
        bool front = false;
        const Vector2 sp = toScreen(pt, &front);
        if (!front || !insideDisc(sp, pinRadius + 3.f))
        {
            return;
        }
        Drawing::DrawEllipse(sp.x, sp.y, pinRadius + 1.5f, pinRadius + 1.5f, {.02f, .05f, .07f, .86f});
        Drawing::DrawEllipse(sp.x, sp.y, pinRadius, pinRadius, color);
        if (showLabel && label && *label && sp.y < center.y + radius - 20.f && sp.y > center.y - radius + 18.f)
        {
            MapLabel(sp, label, color, false);
        }
    };
    auto drawSurfaceDot = [&](const world::SurfacePoint &pt, ColorRGBA color, float dotRadius) {
        bool front = false;
        const Vector2 sp = toScreen(pt, &front);
        if (!front || !insideDisc(sp, dotRadius + 1.f))
        {
            return;
        }
        Drawing::DrawEllipse(sp.x, sp.y, dotRadius, dotRadius, color);
    };
    auto chordFar = [&](const world::SurfacePoint &a, const world::SurfacePoint &b, double maxMeters) {
        const double limit = maxMeters / planetRadius;
        if (limit >= 2.)
        {
            return false;
        }
        const double dx = a.normal.x - b.normal.x;
        const double dy = a.normal.y - b.normal.y;
        const double dz = a.normal.z - b.normal.z;
        return dx * dx + dy * dy + dz * dz > limit * limit;
    };
    struct StaticMapCacheKey
    {
        const world::GeneratedPlanet *planet = nullptr;
        std::uint64_t epoch = 0, revision = 0;
        std::size_t visitedCount = 0, collisionCells = 0;
        double originX = 0, originY = 0, originZ = 0;
        double focusX = 0, focusY = 0, focusZ = 0;
        float range = 0;
        Vector2 worldCenter = {};
        std::uint64_t version = 0;
        bool valid = false;
    };
    static StaticMapCacheKey staticCache[2];
    auto &cache = staticCache[full ? 1 : 0];
    const double moveThreshold = full ? (std::max)(.5f, range * .0015f) : .5f;
    const bool originMoved = std::fabs(originFrame.origin.normal.x - cache.originX) > 1.e-11 ||
                             std::fabs(originFrame.origin.normal.y - cache.originY) > 1.e-11 ||
                             std::fabs(originFrame.origin.normal.z - cache.originZ) > 1.e-11;
    const double focusDelta =
        std::hypot(std::hypot(focus.normal.x - cache.focusX, focus.normal.y - cache.focusY),
                   focus.normal.z - cache.focusZ) *
        planetRadius;
    const bool needsRebuild = !cache.valid || cache.planet != snapshot->planet.get() ||
                              cache.epoch != snapshot->statistics.epoch ||
                              cache.revision != snapshot->statistics.revision ||
                              cache.visitedCount != w.relayNetwork.VisitedRegions().size() ||
                              cache.collisionCells != session->CollisionCells() ||
                              std::fabs(cache.range - range) > .01f || originMoved ||
                              (full ? focusDelta > moveThreshold
                                    : Game::Distance(view.worldCenter, cache.worldCenter) > moveThreshold);
    if (needsRebuild)
    {
        cache.planet = snapshot->planet.get();
        cache.epoch = snapshot->statistics.epoch;
        cache.revision = snapshot->statistics.revision;
        cache.visitedCount = w.relayNetwork.VisitedRegions().size();
        cache.collisionCells = session->CollisionCells();
        cache.originX = originFrame.origin.normal.x;
        cache.originY = originFrame.origin.normal.y;
        cache.originZ = originFrame.origin.normal.z;
        cache.focusX = focus.normal.x;
        cache.focusY = focus.normal.y;
        cache.focusZ = focus.normal.z;
        cache.range = range;
        cache.worldCenter = view.worldCenter;
        cache.valid = true;
        ++cache.version;
        profiling::Count(profiling::Counter::MinimapRebuilds);
    }
    const SchematicView clipView{center, {}, 1.f, radius};
    auto drawStaticMap = [&]() {
        if (full)
        {
            // Render smooth 3D lit biome surface across the spherical planet globe cap (cached in DrawSlot).
            const int gridN = range > 12000.f ? 64 : 24;
            const double invN = 2.0 / double(gridN);
            const double sunX = 0.50, sunY = 0.36, sunZ = 0.787;
            for (int iy = 0; iy < gridN; ++iy)
            {
                const double dy0 = -1.0 + double(iy) * invN;
                const double dy1 = dy0 + invN;
                const double dym = (dy0 + dy1) * 0.5;
                for (int ix = 0; ix < gridN; ++ix)
                {
                    const double dx0 = -1.0 + double(ix) * invN;
                    const double dx1 = dx0 + invN;
                    const double dxm = (dx0 + dx1) * 0.5;
                    const double r2m = dxm * dxm + dym * dym;
                    if (r2m > 1.04)
                    {
                        continue;
                    }
                    const double u = dxm * globe.sinHalf;
                    const double v = dym * globe.sinHalf;
                    const double ur2 = u * u + v * v;
                    if (ur2 > 1.0)
                    {
                        continue;
                    }
                    const double z = std::sqrt((std::max)(0.0, 1.0 - ur2));
                    const world::Vector3d normal = world::SurfaceGeometry::Unit(
                        {globe.east.x * u + globe.north.x * v + globe.forward.x * z,
                         globe.east.y * u + globe.north.y * v + globe.forward.y * z,
                         globe.east.z * u + globe.north.z * v + globe.forward.z * z});
                    const auto sample = snapshot->planet->terrain.Sample({normal, 0.0});
                    const float elevRatio = Game::Clamp(float((sample.elevationMeters - 200.0) / 1800.0), 0.f, 1.f);
                    ColorRGBA col = BiomeGlobeColor(sample.biome, elevRatio);
                    // Apply directional sunlight + spherical limb darkening + Fresnel atmosphere rim.
                    const double ndotl = (std::max)(0.0, u * sunX + v * sunY + z * sunZ);
                    const float shade = float(0.32 + 0.78 * ndotl);
                    const float limb = float(0.45 + 0.55 * std::pow(z, 0.38));
                    const float rim = float(std::pow((std::max)(0.0, 1.0 - z), 2.6)) * 0.55f;
                    col.r = Game::Clamp(col.r * shade * limb + rim * 0.18f, 0.f, 1.f);
                    col.g = Game::Clamp(col.g * shade * limb + rim * 0.76f, 0.f, 1.f);
                    col.b = Game::Clamp(col.b * shade * limb + rim * 0.88f, 0.f, 1.f);
                    col.a = 1.f;
                    const Vector2 q[4] = {
                        {center.x + float(dx0 * radius), center.y - float(dy0 * radius)},
                        {center.x + float(dx1 * radius), center.y - float(dy0 * radius)},
                        {center.x + float(dx1 * radius), center.y - float(dy1 * radius)},
                        {center.x + float(dx0 * radius), center.y - float(dy1 * radius)}};
                    MapPolygonData(clipView, q, 4, col);
                }
            }
            // Curved 3D latitude & longitude graticule arcs on the front hemisphere.
            if (range > 45000.f)
            {
                const int latStep = range > 240000.f ? 15 : 5;
                const int lonStep = range > 240000.f ? 30 : 10;
                const ColorRGBA gridCol{.34f, .72f, .78f, range > 240000.f ? .24f : .16f};
                for (int latitude = -75; latitude <= 75; latitude += latStep)
                {
                    for (int longitude = -180; longitude < 180; longitude += 6)
                    {
                        bool f0 = false, f1 = false;
                        const auto p0 = globe.Project(world::SurfaceGeometry::FromDegrees(latitude, longitude), &f0);
                        const auto p1 =
                            globe.Project(world::SurfaceGeometry::FromDegrees(latitude, longitude + 6), &f1);
                        if (f0 && f1)
                        {
                            drawScreenLine(p0, p1, gridCol, 1.f);
                        }
                    }
                }
                for (int longitude = -180; longitude < 180; longitude += lonStep)
                {
                    for (int latitude = -84; latitude < 84; latitude += 6)
                    {
                        bool f0 = false, f1 = false;
                        const auto p0 = globe.Project(world::SurfaceGeometry::FromDegrees(latitude, longitude), &f0);
                        const auto p1 =
                            globe.Project(world::SurfaceGeometry::FromDegrees(latitude + 6, longitude), &f1);
                        if (f0 && f1)
                        {
                            drawScreenLine(p0, p1, gridCol, 1.f);
                        }
                    }
                }
            }
        }
        auto line = [&](const world::SurfacePoint &a, const world::SurfacePoint &b, ColorRGBA color,
                        float width) {
            bool f0 = false, f1 = false;
            const auto p0 = toScreen(a, &f0);
            const auto p1 = toScreen(b, &f1);
            if (!f0 || !f1)
            {
                return;
            }
            if (!full && Game::Distance(project(a), project(b)) > planetRadius * .4 &&
                world::SurfaceGeometry::Distance(planetRadius, a, b) < planetRadius * .2)
            {
                return;
            }
            drawScreenLine(p0, p1, color, width);
        };
        static std::vector<Vector2> regionPoly;
        for (const auto &r : snapshot->planet->regions)
        {
            if (r.center.space != world::SpaceKind::Surface ||
                chordFar(r.center.position, focus, r.boundingRadiusMeters + range * 1.5) ||
                world::SurfaceGeometry::Distance(planetRadius, r.center.position, focus) >
                    r.boundingRadiusMeters + range * 1.5)
            {
                continue;
            }
            regionPoly.clear();
            regionPoly.reserve(r.footprint.size());
            bool allFront = true;
            for (auto vertex : r.footprint)
            {
                bool f = false;
                regionPoly.push_back(
                    toScreen(world::SurfaceGeometry::FromLocal(r.frame, vertex, planetRadius), &f));
                allFront = allFront && f;
            }
            if (!allFront)
            {
                continue;
            }
            ColorRGBA zoneColor(.13f, .24f, .26f, .78f);
            if (w.relayNetwork.RegionVisited(r.definition.id))
            {
                switch (r.definition.kind)
                {
                case world::RegionKind::Settlement:
                    zoneColor = {.20f, .38f, .40f, .86f};
                    break;
                case world::RegionKind::Outpost:
                    zoneColor = {.18f, .38f, .32f, .86f};
                    break;
                case world::RegionKind::Mine:
                    zoneColor = {.38f, .30f, .18f, .86f};
                    break;
                case world::RegionKind::Outlaw:
                    zoneColor = {.40f, .20f, .18f, .86f};
                    break;
                case world::RegionKind::AncientRuins:
                    zoneColor = {.32f, .24f, .40f, .86f};
                    break;
                case world::RegionKind::Wilderness:
                default:
                    zoneColor = {.22f, .34f, .24f, .82f};
                    break;
                }
            }
            MapPolygonData(clipView, regionPoly.data(), regionPoly.size(), zoneColor);
        }
        static std::weak_ptr<const world::GeneratedPlanet> indexedPlanet;
        static std::unique_ptr<world::RouteSpatialIndex> routeIndex;
        static std::vector<std::size_t> nearbyRoutes;
        if (indexedPlanet.lock() != snapshot->planet || !routeIndex)
        {
            routeIndex = std::make_unique<world::RouteSpatialIndex>(*snapshot->planet);
            indexedPlanet = snapshot->planet;
        }
        struct MapSettlementClip
        {
            world::Vector3d normal;
            double cosInner;
        };
        std::vector<MapSettlementClip> mapClips;
        const bool rev5Map = snapshot->planet->layoutRevision >= 5;
        if (rev5Map && range < 10000)
        {
            for (const auto &region : snapshot->planet->regions)
            {
                if (region.center.space != world::SpaceKind::Surface)
                {
                    continue;
                }
                if (region.definition.kind == world::RegionKind::Settlement)
                {
                    double clipRadius = (std::max)(160., region.boundingRadiusMeters * .66);
                    if (snapshot->planet->cityCores)
                    {
                        if (const auto *core = snapshot->planet->cityCores->Find(region.definition.id);
                            core && (!core->lots.empty() || !core->roads.empty()))
                        {
                            clipRadius = (std::max)(clipRadius, core->radiusMeters + 100.);
                        }
                    }
                    if (!chordFar(region.center.position, focus, clipRadius + range + 60.))
                    {
                        mapClips.push_back({region.center.position.normal,
                                            std::cos((std::max)(10., clipRadius - 6.) / planetRadius)});
                    }
                }
                else if (region.definition.kind == world::RegionKind::Outlaw ||
                         region.definition.kind == world::RegionKind::AncientRuins ||
                         region.definition.kind == world::RegionKind::Wilderness)
                {
                    constexpr double kCampClearMeters = 26.;
                    if (!chordFar(region.access.position, focus, kCampClearMeters + range + 60.))
                    {
                        mapClips.push_back(
                            {region.access.position.normal, std::cos(kCampClearMeters / planetRadius)});
                    }
                }
            }
        }
        auto insideClippedCity = [&](const world::SurfacePoint &a, const world::SurfacePoint &b) {
            if (mapClips.empty())
            {
                return false;
            }
            const auto midN = world::SurfaceGeometry::Unit(
                {a.normal.x + b.normal.x, a.normal.y + b.normal.y, a.normal.z + b.normal.z});
            for (const auto &clip : mapClips)
            {
                if (world::SurfaceGeometry::Dot(midN, clip.normal) > clip.cosInner)
                {
                    return true;
                }
            }
            return false;
        };
        routeIndex->QueryInto(focus, range * 1.5, nearbyRoutes);
        profiling::Count(profiling::Counter::PlanetMapRoutesRejected,
                         snapshot->planet->connections.size() - nearbyRoutes.size());
        for (const auto route : nearbyRoutes)
        {
            const auto &edge = snapshot->planet->connections[route];
            if (edge.mode != world::RouteMode::Road && (!full || range < 10000))
            {
                continue;
            }
            if (rev5Map && edge.mode == world::RouteMode::Road &&
                edge.from < snapshot->planet->nodes.size() && edge.to < snapshot->planet->nodes.size() &&
                snapshot->planet->nodes[edge.from].regionId == snapshot->planet->nodes[edge.to].regionId)
            {
                const auto &reg = snapshot->planet->Region(snapshot->planet->nodes[edge.from].regionId);
                if (reg.definition.kind == world::RegionKind::Settlement &&
                    reg.center.space == world::SpaceKind::Surface)
                {
                    continue;
                }
            }
            const size_t stride = range > 200000  ? (std::max)(size_t(1), (edge.points.size() + 5) / 6)
                                  : range > 10000 ? (std::max)(size_t(1), (edge.points.size() + 23) / 24)
                                                  : 1;
            const ColorRGBA roadColor =
                edge.mode == world::RouteMode::Road ? ColorRGBA(.62f, .78f, .72f) : ColorRGBA(.36f, .64f, .78f, .55f);
            const float roadWidth = full ? 1.5f : 3.f;
            for (std::size_t i = stride; i < edge.points.size() + stride - 1; i += stride)
            {
                profiling::Count(profiling::Counter::PlanetMapSegmentsVisited);
                const auto &a = edge.points[i - stride];
                const auto &b = edge.points[(std::min)(i, edge.points.size() - 1)];
                if (edge.mode == world::RouteMode::Road && insideClippedCity(a, b))
                {
                    continue;
                }
                const double length = stride == 1 ? routeIndex->SegmentLength(route, i - 1)
                                                  : world::SurfaceGeometry::Distance(planetRadius, a, b);
                if (chordFar(a, focus, range * 1.5 + length) ||
                    world::SurfaceGeometry::Distance(planetRadius, a, focus) > range * 1.5 + length)
                {
                    continue;
                }
                line(a, b, roadColor, roadWidth);
            }
        }
        static std::unordered_set<std::string_view> drawnStructures;
        struct ClearOnExit
        {
            std::unordered_set<std::string_view> &set;
            ~ClearOnExit()
            {
                set.clear();
            }
        } clearDrawnStructures{drawnStructures};
        drawnStructures.clear();
        auto structure = [&](const world::SurfaceStructure &item) {
            const double diag = std::hypot(item.sizeMeters.x, item.sizeMeters.y);
            if (chordFar(item.frame.origin, focus, range + diag) ||
                world::SurfaceGeometry::Distance(planetRadius, item.frame.origin, focus) > range + diag)
            {
                return;
            }
            if (!drawnStructures.insert(item.id).second)
            {
                return;
            }
            const double cy = std::cos(item.yaw), sy = std::sin(item.yaw);
            const double hx = item.sizeMeters.x * .5, hy = item.sizeMeters.y * .5;
            Vector2 quad[4];
            bool allFront = true;
            static constexpr double kSigns[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int c = 0; c < 4; ++c)
            {
                const double x = kSigns[c][0] * hx, y = kSigns[c][1] * hy;
                const auto pt = world::SurfaceGeometry::FromLocal(
                    item.frame, {x * cy - y * sy, x * sy + y * cy, 0}, planetRadius);
                bool f = false;
                quad[c] = toScreen(pt, &f);
                allFront = allFront && f;
            }
            if (allFront)
            {
                MapPolygonData(clipView, quad, 4,
                               item.use == world::ContentUse::Housing ? ColorRGBA(.46f, .60f, .62f)
                                                                      : ColorRGBA(.58f, .70f, .50f));
            }
        };
        if (range < 10000)
        {
            for (const auto &chunk : snapshot->chunks)
            {
                if (chordFar(chunk->frame.origin, focus, chunk->radiusMeters + range + 40.))
                {
                    continue;
                }
                for (const auto &road : chunk->roads)
                {
                    if (road.points.size() < 2)
                    {
                        continue;
                    }
                    if (road.kind == world::StreetKind::PedestrianPlaza)
                    {
                        if (!full)
                        {
                            const float wPx = float(road.widthMeters) * pxPerMeter;
                            line(road.points.front(), road.points.back(), {.48f, .55f, .54f, .85f}, wPx);
                        }
                        continue;
                    }
                    if (road.sidewalkMeters <= 0)
                    {
                        continue;
                    }
                    const ColorRGBA col = road.kind == world::StreetKind::RingRoad
                                              ? ColorRGBA(.50f, .62f, .60f)
                                          : (road.kind == world::StreetKind::Elevated ||
                                             road.kind == world::StreetKind::ElevatedRamp)
                                              ? ColorRGBA(.58f, .68f, .72f)
                                          : (road.kind == world::StreetKind::Underpass ||
                                             road.kind == world::StreetKind::UnderpassRamp)
                                              ? ColorRGBA(.28f, .44f, .50f)
                                              : ColorRGBA(.39f, .47f, .46f);
                    const float wPx = full ? 1.2f : (std::max)(1.5f, float(road.widthMeters) * pxPerMeter);
                    for (std::size_t i = 1; i < road.points.size(); ++i)
                    {
                        line(road.points[i - 1], road.points[i], col, wPx);
                    }
                }
                for (const auto &inter : chunk->intersections)
                {
                    if (chordFar(inter.center, focus, range + inter.radiusMeters))
                    {
                        continue;
                    }
                    bool front = false;
                    const Vector2 sp = toScreen(inter.center, &front);
                    const float rPx = (std::max)(1.8f, float(inter.radiusMeters) * pxPerMeter);
                    if (!front || Game::Distance(sp, center) + rPx > radius)
                    {
                        continue;
                    }
                    if (inter.control == world::IntersectionControl::Roundabout && inter.islandRadiusMeters > 1.)
                    {
                        Drawing::DrawEllipse(sp.x, sp.y, rPx, rPx, {.44f, .54f, .53f, 1.f});
                        const float islPx = (std::max)(1.f, float(inter.islandRadiusMeters) * pxPerMeter);
                        Drawing::DrawEllipse(sp.x, sp.y, islPx, islPx, {.22f, .42f, .33f, 1.f});
                    }
                    else
                    {
                        Drawing::DrawEllipse(sp.x, sp.y, rPx * .95f, rPx * .95f, {.39f, .47f, .46f, 1.f});
                    }
                }
                for (const auto &item : chunk->structures)
                {
                    structure(item);
                }
            }
            for (auto item : session->NearbyStructures())
            {
                structure(*item);
            }
        }
    };
    rendercache::DrawSlot(full ? "planet_fullmap_static" : "planet_minimap_static", cache.version,
                          drawStaticMap);
    for (const auto &region : snapshot->planet->regions)
    {
        if (world::SurfaceGeometry::Distance(planetRadius, region.access.position, focus) > range)
        {
            continue;
        }
        const bool visited = w.relayNetwork.RegionVisited(region.definition.id);
        const bool showName = full && (visited || range <= 120000.f);
        drawSurfacePin(region.access.position,
                       region.center.space == world::SpaceKind::Subsurface ? ColorRGBA(.65f, .81f, 1)
                                                                           : ColorRGBA(.73f, .92f, .80f),
                       visited ? region.definition.name.c_str() : "미확인 지역", showName, full ? 4.f : 3.f);
    }
    for (const auto &relay : snapshot->planet->relays)
    {
        if (!w.relayNetwork.Discovered(relay.id) ||
            (w.mapRegisteredRelaysOnly && !w.relayNetwork.Registered(relay.id)))
        {
            continue;
        }
        if (world::SurfaceGeometry::Distance(planetRadius, relay.location.position, focus) > range)
        {
            continue;
        }
        bool front = false;
        const auto projected = toScreen(relay.location.position, &front);
        if (full && front && insideDisc(projected, 10.f))
        {
            const bool registered = w.relayNetwork.Registered(relay.id);
            const bool selected = w.selectedRelay == relay.id;
            const float iconSize = selected ? 20.f : 16.f;
            Icon("relay", projected.x - iconSize * .5f, projected.y - iconSize * .5f, iconSize, false,
                 registered ? ColorRGBA(.35f, 1, .9f) : ColorRGBA(.65f, .69f, .75f));
            MapLabel(projected, registered ? "등록 중계환" : "발견 · E 등록",
                     registered ? ColorRGBA(.35f, 1, .9f) : ColorRGBA(.65f, .69f, .75f), true);
        }
        else if (front)
        {
            drawSurfacePin(relay.location.position,
                           w.relayNetwork.Registered(relay.id) ? ColorRGBA(.35f, 1, .9f)
                                                               : ColorRGBA(.65f, .69f, .75f),
                           nullptr, false, w.selectedRelay == relay.id ? 7.f : 4.f);
        }
    }
    if (range <= 900.f)
    {
        const auto *planetPtr = snapshot->planet.get();
        const size_t maxFacilities = full ? 48 : 24;
        size_t drawnFacilities = 0;
        std::unordered_set<std::string_view> drawnFacilityIds;
        auto drawFacilityDot = [&](const world::SurfaceStructure &item) {
            if (drawnFacilities >= maxFacilities || !drawnFacilityIds.insert(item.id).second)
            {
                return;
            }
            const auto kind = Game::EnterableUrbanFacility(item, planetPtr);
            if (kind == UrbanFacilityKind::None)
            {
                return;
            }
            const auto doorPt = Game::FacilityDoorPoint(item, planetRadius);
            if (world::SurfaceGeometry::Distance(planetRadius, doorPt, focus) > range + 12.)
            {
                return;
            }
            const ColorRGBA col = kind == UrbanFacilityKind::Market     ? ColorRGBA(.28f, .94f, .64f)
                                  : kind == UrbanFacilityKind::Clinic   ? ColorRGBA(.32f, .94f, 1.f)
                                  : kind == UrbanFacilityKind::Workshop ? ColorRGBA(1.f, .70f, .24f)
                                                                        : ColorRGBA(.62f, .80f, 1.f);
            drawSurfaceDot(doorPt, col, full ? 4.5f : 3.5f);
            ++drawnFacilities;
        };
        for (const auto &chunk : snapshot->chunks)
        {
            if (drawnFacilities >= maxFacilities)
            {
                break;
            }
            if (chordFar(chunk->frame.origin, focus, chunk->radiusMeters + range + 40.))
            {
                continue;
            }
            for (const auto &item : chunk->structures)
            {
                drawFacilityDot(item);
            }
        }
        for (const auto *item : session->NearbyStructures())
        {
            if (drawnFacilities >= maxFacilities)
            {
                break;
            }
            if (item)
            {
                drawFacilityDot(*item);
            }
        }
    }
    if (const auto *community = Game::Get().SurfaceCommunity(); community && full)
    {
        const auto *trackedSite = ActivityJournal::TrackedCommunity();
        const auto *planetPtr = snapshot->planet.get();
        for (const auto &site : community->State().sites)
        {
            if (trackedSite == &site)
            {
                continue;
            }
            if (CommunityTaskProgress::IsActive(site))
            {
                const auto &target = site.task == CommunityTaskProgress::Accepted ? site.target : site.contact;
                drawSurfacePin(target, {1, .8f, .35f},
                               site.task == CommunityTaskProgress::Accepted
                                   ? PlanetCommunity::ZoneTaskName(site, planetPtr)
                                   : "현장 기록 제출",
                               full, 5.f);
            }
            else if (site.task == CommunityTaskProgress::Offered && range < 6000)
            {
                drawSurfacePin(site.contact, {.58f, .88f, .96f}, "현장 의뢰", full, 4.f);
            }
        }
    }
    // Item 12: Filter dormant orange random subquests to the player's current region cluster only.
    const int playerClusterIdx = NearestClusterIndex(*snapshot->planet, session->Pose().origin);
    const double clusterLimitMeters = (std::min)(24000.0, planetRadius * 0.045);
    for (const auto &enc : encounters::WorldEncounters::Get().State().live)
    {
        if (!enc.surfaceSpace || enc.phase == encounters::Phase::Completed)
        {
            continue;
        }
        const bool active = enc.phase != encounters::Phase::Dormant;
        if (!active)
        {
            const int encClusterIdx = NearestClusterIndex(*snapshot->planet, enc.surfaceAnchor);
            if (encClusterIdx != playerClusterIdx)
            {
                continue;
            }
            if (!snapshot->planet->clusters.empty() &&
                world::SurfaceGeometry::Distance(planetRadius, enc.surfaceAnchor,
                                                 snapshot->planet->clusters[size_t(playerClusterIdx)]) >
                    clusterLimitMeters)
            {
                continue;
            }
        }
        if (world::SurfaceGeometry::Distance(planetRadius, enc.surfaceAnchor, focus) <= range * 1.25)
        {
            const bool showEncLabel = full && (active || range <= 35000.f);
            drawSurfacePin(enc.surfaceAnchor,
                           active ? ColorRGBA(1.f, .86f, .30f) : ColorRGBA(1.f, .56f, .24f),
                           showEncLabel ? enc.title.c_str() : nullptr, showEncLabel, active ? 6.f : 4.f);
        }
    }
    auto &chapter = game::GameLevels::Chapter();
    if (full && chapter.IsActive() && range < 4000 && Game::Get().FocusedMission() == MissionId::Frontier)
    {
        for (const auto &point : chapter.Interactions())
        {
            if (point.action != game::ChapterAction::Exit &&
                point.action != game::ChapterAction::ConvoySupply &&
                !game::ChapterOneLevel::IsOptionalConversation(point.action))
            {
                drawSurfacePin(chapter.Surface().Point(point.position), {1, .79f, .36f}, point.label, full, 4.f);
            }
        }
    }
    const auto goal = ActivityJournal::Current();
    if (goal.surfaceTarget && !goal.title.empty())
    {
        if (full)
        {
            bool front = false;
            const auto sp = globe.Project(*goal.surfaceTarget, &front);
            if (front && insideDisc(sp, 10.f))
            {
                Drawing::DrawGlow(sp, 14.f, {1.f, .82f, .28f, .52f});
                Icon("destination", sp.x - 9.f, sp.y - 9.f, 18.f, false, {1.f, .88f, .34f});
                MapLabel(sp, goal.title.c_str(), {1.f, .90f, .42f}, true);
            }
            else
            {
                const double x = world::SurfaceGeometry::Dot(goal.surfaceTarget->normal, globe.east);
                const double y = world::SurfaceGeometry::Dot(goal.surfaceTarget->normal, globe.north);
                const float angle = float(std::atan2(-y, x));
                const Vector2 edge{center.x + std::cos(angle) * (radius - 12.f),
                                   center.y + std::sin(angle) * (radius - 12.f)};
                DirectionArrow(edge, angle, 8.f, {1.f, .86f, .34f}, true);
            }
        }
        else if (world::SurfaceGeometry::Dot(session->Origin().origin.normal, goal.surfaceTarget->normal) >
                 -1 + 1.e-12)
        {
            DrawTrackedMarker(view, project(*goal.surfaceTarget), goal.title, false);
        }
    }
    if (auto bike = Game::Get().SurfaceBike();
        bike && world::SurfaceGeometry::Distance(planetRadius, *bike, focus) < range + 10)
    {
        drawSurfaceDot(*bike, {1, .76f, .4f}, full ? 4.5f : 4.f);
    }
    if (w.aircraft.position &&
        world::SurfaceGeometry::Distance(planetRadius, *w.aircraft.position, focus) < range + 10)
    {
        drawSurfacePin(*w.aircraft.position, {.4f, .9f, 1},
                       w.aircraft.health > 0 ? "공용 비행체" : "비행체 잔해", full, 4.f);
    }
    if (const auto *party = Game::Get().SurfaceParty())
    {
        for (size_t i = 0; i < party->State().members.size(); ++i)
        {
            const auto &member = party->State().members[i];
            if (member.joined && member.place != CompanionPlace::Mine &&
                world::SurfaceGeometry::Distance(planetRadius, member.position, focus) < range)
            {
                drawSurfacePin(member.position,
                               member.combat.health > 0 ? ColorRGBA(.4f, .92f, .83f) : ColorRGBA(1, .4f, .3f),
                               i == 0 ? "리아" : "카인", full, 4.f);
            }
        }
    }
    if (CivicJustice::State().wanted > 0 && CivicJustice::IsExterior() && range < 4000)
    {
        for (const auto &officer : CivicJustice::State().officers)
        {
            if (officer.health <= 0)
            {
                continue;
            }
            const auto officerPt = session->PointAt(officer.position.x * 2, officer.position.y * 2);
            bool front = false;
            const auto at = toScreen(officerPt, &front);
            if (!front || !insideDisc(at, 7.f))
            {
                continue;
            }
            std::vector<Vector2> cone{at};
            for (int i = 0; i <= 8; ++i)
            {
                float angle = officer.facing.yaw - CivicJustice::kSightHalfAngle +
                              2 * CivicJustice::kSightHalfAngle * i / 8;
                const auto rimPt = session->PointAt((officer.position.x + std::cos(angle) * CivicJustice::kSightRange) * 2,
                                                    (officer.position.y + std::sin(angle) * CivicJustice::kSightRange) * 2);
                cone.push_back(toScreen(rimPt));
            }
            MapPolygonData(clipView, cone.data(), cone.size(), {1, .5f, .2f, .15f});
            const auto aheadPt = session->PointAt((officer.position.x + std::cos(officer.facing.yaw)) * 2,
                                                  (officer.position.y + std::sin(officer.facing.yaw)) * 2);
            const auto ahead = toScreen(aheadPt);
            DirectionArrow(at, std::atan2(ahead.y - at.y, ahead.x - at.x), 5, {1, .65f, .3f});
        }
    }
    if (w.planetDestination)
    {
        bool front = false;
        const auto point = toScreen(*w.planetDestination, &front);
        if (front && insideDisc(point, 10.f))
        {
            if (full)
            {
                Icon("destination", point.x - 9, point.y - 9, 18, false, {1, .78f, .3f});
                MapLabel(point, "목적지", {1, .78f, .3f}, false);
            }
            else
            {
                drawSurfacePin(*w.planetDestination, {1, .78f, .3f}, "목적지", false, 6.f);
            }
        }
        else if (full)
        {
            const double x = world::SurfaceGeometry::Dot(w.planetDestination->normal, globe.east);
            const double y = world::SurfaceGeometry::Dot(w.planetDestination->normal, globe.north);
            const float angle = float(std::atan2(-y, x));
            const Vector2 edge{center.x + std::cos(angle) * (radius - 12.f),
                               center.y + std::sin(angle) * (radius - 12.f)};
            DirectionArrow(edge, angle, 8.f, {1, .78f, .3f}, true);
        }
        else if (world::SurfaceGeometry::Dot(session->Origin().origin.normal, w.planetDestination->normal) >
                 -1 + 1.e-12)
        {
            const auto playerPt = view.Project({float(local.x), float(local.y)});
            const float distance = Game::Distance(point, view.center);
            if (distance > .001f)
            {
                Vector2 edge{view.center.x + (point.x - view.center.x) * (radius - 12) / distance,
                             view.center.y + (point.y - view.center.y) * (radius - 12) / distance};
                DirectionArrow(edge, std::atan2(point.y - playerPt.y, point.x - playerPt.x), 8, {1, .78f, .3f},
                               true);
            }
        }
        if (full)
        {
            char destination[80];
            std::snprintf(destination, sizeof(destination), "목적지 거리 %.2f km",
                          world::SurfaceGeometry::Distance(planetRadius, *w.planetDestination,
                                                           session->Pose().origin) /
                              1000);
            UiText(96, 202, destination, 15, {1.f, .86f, .44f});
        }
    }
    bool playerFront = false;
    const auto player = toScreen(session->Pose().origin, &playerFront);
    if (playerFront && insideDisc(player, 14.f))
    {
        float playerAngle = (w.isBikeMounted ? w.bikeYaw : w.facingYaw) + kPi * .25f;
        if (full)
        {
            const float yaw = w.car.mounted      ? w.car.Yaw(session->Origin())
                              : w.isBikeMounted  ? w.bikeYaw
                                                 : w.facingYaw;
            const auto aheadPt =
                session->PointAt(local.x + std::cos(yaw) * 4.0, local.y + std::sin(yaw) * 4.0);
            const auto ahead = globe.Project(aheadPt);
            playerAngle = std::atan2(ahead.y - player.y, ahead.x - player.x);
        }
        DirectionArrow(player, playerAngle, full ? 12 : 8, {.3f, .95f, .88f});
    }
    if (full)
    {
        auto controls = RelayControls();
        for (size_t i = 0; i < controls.size(); ++i)
        {
            Button(controls[i], int(i));
        }
        const char *reason = RelayTravel::Restriction(w.selectedRelay);
        const std::string statusText = w.relayPreparing         ? "목적지 지형 준비 중 · X 취소"
                                       : !w.relayStatus.empty() ? w.relayStatus
                                       : reason                 ? reason
                                                                : "등록 목적지 준비 완료";
        const auto pages = Hud::TextPages(statusText, 330, 15, 2);
        const size_t page = size_t((std::max)(0, w.lastUpdateMilliseconds) / 5000) % pages.size();
        for (size_t i = 0; i < pages[page].size(); ++i)
        {
            UiText(820, (w.relayPreparing ? 608.f : 586.f) + float(i) * 22, pages[page][i], 15,
                   {.76f, .87f, .82f});
        }
        Drawing::DrawLine({96, 652}, {790, 652}, {.32f, .52f, .54f, .45f}, 1.f);
        Icon("relay", 96, 659, 13, false, {.35f, 1, .9f});
        UiText(114, 671, "중계환", 13, {.72f, .92f, .89f});
        Icon("destination", 168, 659, 13, false, {1, .78f, .3f});
        UiText(186, 671, "목적지", 13, {.94f, .86f, .66f});
        auto legendDot = [](float x, float y, ColorRGBA c, const char *name) {
            Drawing::DrawEllipse(x, y, 4.f, 4.f, c);
            UiText(x + 9.f, y + 5.f, name, 13, {.82f, .90f, .90f});
        };
        legendDot(248.f, 666.f, {.28f, .94f, .64f}, "상점");
        legendDot(302.f, 666.f, {.32f, .94f, 1.f}, "의료");
        legendDot(356.f, 666.f, {1.f, .70f, .24f}, "정비");
        legendDot(410.f, 666.f, {.62f, .80f, 1.f}, "행정");
        legendDot(464.f, 666.f, {1.f, .56f, .24f}, "클러스터 의뢰");
        UiText(96, 692,
               w.mapRegisteredRelaysOnly
                   ? "L 필터: 등록 중계환만 · 주황 서브퀘스트는 현재 클러스터만 표시"
                   : "L 필터: 발견 중계환 · 주황 서브퀘스트는 현재 구역 클러스터만 표시",
               13, {.74f, .86f, .85f});
        UiText(96, 712,
               Game::Get().IsSurfaceMine() ? "Tab 지하/지상 전환 · G 우주 지도 · M 닫기"
                                           : "좌클릭 드래그 회전 · 우클릭 목적지 지정 · G 우주 지도 · M 닫기",
               13, {.70f, .82f, .82f});
        const float meters = range * .5f;
        Drawing::DrawLine({576, 707}, {690, 707}, {.7f, .88f, .87f}, 2);
        char distance[64];
        std::snprintf(distance, sizeof(distance), meters >= 1000 ? "%.1f km" : "%.0f m",
                      meters >= 1000 ? meters / 1000 : meters);
        UiText(700, 712, distance, 14, {.80f, .92f, .90f});
    }
    else
    {
        const Vector2 northPos{center.x - radius * .72f, center.y - radius * .72f};
        Drawing::DrawEllipse(northPos.x, northPos.y, 9.f, 9.f, {.03f, .07f, .09f, .92f});
        UiText(northPos.x - 4.5f, northPos.y + 4.5f, "N", 12, {.52f, .96f, .90f}, true);
        HudCard(1042.f, 218.f, 218.f, 26.f, {.36f, .84f, .82f}, false);
        auto miniKey = [](float x, ColorRGBA c, const char *name) {
            Drawing::DrawEllipse(x, 231.f, 3.5f, 3.5f, c);
            UiText(x + 7.f, 236.f, name, 12, {.78f, .90f, .88f}, true);
        };
        miniKey(1054.f, {.28f, .94f, .64f}, "상점");
        miniKey(1106.f, {.32f, .94f, 1.f}, "의료");
        miniKey(1158.f, {1.f, .70f, .24f}, "정비");
        miniKey(1210.f, {.62f, .80f, 1.f}, "행정");
    }
}

void Hud::Render()
{
    const auto &w = Game::Get().GetState();
    renderqueue::Opacity(w.uiPreferences.content);
    if (w.ui.screen == UiScreen::None && !w.weaponWheel.visible && !w.mapVisible && !w.journalVisible &&
        !w.dialogueHistoryVisible && !HospitalRecovery::Active())
    {
        static CityHud city;
        static MineHud mine;
        static ChapterHud chapter;
        static PlanetHud planet;
        switch (Game::Get().Mode())
        {
        case WorldMode::StoryOperation:
        {
            const auto &arc = story::StoryArc::Get();
            DrawObjective(arc.CurrentObjectiveAction(), arc.ActiveZoneTitle() + " · F 스캔 · V 경로 안내 · Q 무기");
            const std::string prompt = arc.NearbyInteractionLabel(w.playerPosition);
            if (!prompt.empty())
            {
                DrawInteraction(prompt);
            }
            break;
        }
        case WorldMode::SurfaceExterior:
        case WorldMode::SurfaceInterior:
        case WorldMode::SurfaceObservatory:
        case WorldMode::SurfaceFacility:
            planet.Draw();
            break;
        case WorldMode::LegacyFrontier:
            chapter.Draw();
            break;
        case WorldMode::SurfaceMine:
        case WorldMode::LegacyMine:
            mine.Draw();
            break;
        case WorldMode::LegacyInterior:
        case WorldMode::LegacyObservatory:
        case WorldMode::LegacyCity:
        default:
            city.Draw();
            break;
        }
        DrawVitals();
        DrawNoticePanel();
        if (const auto *enc = encounters::WorldEncounters::Get().ActiveEncounter())
        {
            HudCard(20.f, 168.f, 472.f, 70.f, {1.f, .78f, .34f, .92f});
            UiText(36.f, 194.f, encounters::WorldEncounters::Get().CurrentObjectiveTitle(), 16, {1.f, .86f, .44f});
            UiText(36.f, 222.f, encounters::WorldEncounters::Get().CurrentObjectiveAction(), 15, kUiInk);
            (void)enc;
        }
        if (story::StoryArc::Get().State().epilogueTransitionSeconds > 0.f)
        {
            const float alpha = Game::Clamp(story::StoryArc::Get().State().epilogueTransitionSeconds / 1.2f, 0.f, 1.f);
            HudShift centered(WorldCamera::CenterOffsetX());
            Drawing::DrawRectangle(340.f, 170.f, 600.f, 128.f, {.03f, .06f, .08f, .92f * alpha});
            Drawing::DrawRectangle(340.f, 170.f, 600.f, 3.f, {.36f, .96f, .86f, alpha});
            UiText(370.f, 212.f, "붕괴 후 17일 · 빈 작업대", 24, {.86f, .96f, .96f, alpha});
            UiText(370.f, 248.f, "수신기 표시등 점등 — 발신지: 네레우스 / 송신 시각 미확인", 16,
                   {.42f, .96f, .84f, alpha});
        }
        DrawDialogue();
    }
    if (PlanetMapActive() && w.mapVisible)
    {
        PlanetHud::DrawMap(true);
    }
    else
    {
        MapHud::DrawFullMap();
    }
    ChapterHud::DrawJournal();
    DrawMenu();
    FieldMinigame::Draw();
    DrawWeaponWheel();
    renderqueue::Opacity(1);
    DrawMedicalFade();
}
void Hud::DrawWeaponWheel()
{
    const auto &w = Game::Get().GetState();
    const auto &wheel = w.weaponWheel;
    if (!wheel.visible)
    {
        return;
    }
    const auto &loadout = game::GameLevels::Mine().GetState().loadout;
    HudShift shift(WorldCamera::CenterOffsetX());
    constexpr Vector2 center = {640, 390};
    constexpr float inner = 87, outer = 224;
    DrawFullscreenDim({.01f, .025f, .035f, .75f});
    UiText(624, 79, "장비", 14, {.65f, .9f, .81f});
    UiText(640 - RenderAssets::MeasureUtf8("무기 선택", 32) * .5f, 125, "무기 선택", 32);
    UiText(640 - RenderAssets::MeasureUtf8("가리킨 뒤 Q를 놓으면 장착됩니다.", 18) * .5f, 157,
           "가리킨 뒤 Q를 놓으면 장착됩니다.", 18);
    struct Sector
    {
        combat::WeaponSlot slot;
        float angle;
        Vector2 label;
        const char *title;
    };
    const Sector sectors[] = {{combat::WeaponSlot::Primary, -kPi / 2, {640, 236}, "주 무장"},
                              {combat::WeaponSlot::Secondary, kPi / 6, {780, 466}, "보조 무장"},
                              {combat::WeaponSlot::Unarmed, 5 * kPi / 6, {500, 466}, "맨손"}};
    for (const auto &sector : sectors)
    {
        const bool available = loadout.IsAvailable(sector.slot);
        const bool hover = wheel.hovered == sector.slot;
        ColorRGBA fill =
            hover ? (available ? ColorRGBA(.14f, .30f, .31f, .94f) : ColorRGBA(.16f, .19f, .20f, .96f))
                  : ColorRGBA(.067f, .15f, .19f, .95f);
        auto point = [&](float a, float r) {
            return Vector2{center.x + std::cos(a) * r, center.y + std::sin(a) * r};
        };
        for (int i = 0; i < 48; ++i)
        {
            float a = sector.angle - kPi / 3 + .05236f + (2 * kPi / 3 - .10472f) * i / 48;
            float b = sector.angle - kPi / 3 + .05236f + (2 * kPi / 3 - .10472f) * (i + 1) / 48;
            Drawing::DrawPolygon({point(a, inner), point(a, outer), point(b, outer), point(b, inner)}, fill);
            Drawing::DrawLine(point(a, outer), point(b, outer),
                              hover ? ColorRGBA(.4f, .95f, .87f) : ColorRGBA(.2f, .36f, .4f),
                              hover ? 3.f : 1.f);
        }
        auto label = sector.label;
        ColorRGBA ink = available ? kUiInk : ColorRGBA(.53f, .6f, .62f);
        auto centeredText = [&](const std::string &text, float y, int size, ColorRGBA color) {
            UiText(label.x - RenderAssets::MeasureUtf8(text, size) * .5f, y, text, size, color, false);
        };
        centeredText(sector.title, label.y + 23, 18, ink);
        if (sector.slot == combat::WeaponSlot::Primary && loadout.HasPrimary())
        {
            Icon("carbine", label.x - 18, label.y - 37, 36, false, ink);
        }
        else if (sector.slot == combat::WeaponSlot::Secondary && loadout.HasPistol())
        {
            Icon("pistol", label.x - 18, label.y - 37, 36, false, ink);
        }
        else
        {
            if (sector.slot == combat::WeaponSlot::Unarmed)
            {
                Icon("unarmed", label.x - 18, label.y - 37, 36, false, ink);
            }
            else
            {
                Icon("slot_empty", label.x - 18, label.y - 37, 36, false, ink);
            }
        }
        const bool equipped = loadout.Selected() == sector.slot;
        centeredText(equipped                                       ? "현재 장착"
                     : sector.slot == combat::WeaponSlot::Unarmed   ? "근접 공격"
                     : available                                    ? (sector.slot == combat::WeaponSlot::Primary
                                                                           ? Game::Get().PrimaryWeapon().GetName()
                                                                           : "권총")
                     : sector.slot == combat::WeaponSlot::Secondary ? "미보유"
                                                                    : "비어 있음",
                     label.y + 49, 16, equipped ? ColorRGBA(.65f, .9f, .81f) : ink);
        if (equipped)
        {
            Icon("check", label.x + 42, label.y - 43, 18, false, {.65f, .9f, .81f});
        }
    }
    const std::string name = wheel.hovered == combat::WeaponSlot::Unarmed
                                 ? "맨손"
                             : loadout.IsAvailable(wheel.hovered)
                                 ? (wheel.hovered == combat::WeaponSlot::Primary
                                        ? Game::Get().PrimaryWeapon().GetName()
                                        : "권총")
                                 : "빈 슬롯";
    UiText(center.x - RenderAssets::MeasureUtf8(name, 26) * .5f, center.y, name, 26, kUiInk, false);
    const char *status = loadout.IsAvailable(wheel.hovered) ? "Q 놓아서 선택" : "현재 장착 유지";
    UiText(center.x - RenderAssets::MeasureUtf8(status, 14) * .5f, center.y + 29, status, 14, kUiInk);
    float length = std::hypot(wheel.offset.x, wheel.offset.y);
    if (length >= 24)
    {
        float r = (std::min)(length, outer - 10);
        Drawing::DrawEllipse(center.x + wheel.offset.x / length * r, center.y + wheel.offset.y / length * r,
                             5, 5, {.58f, 1, .93f});
    }
    const std::string instructions = "Q 홀드 + 마우스 이동 · Q 해제 장착 · Esc 취소";
    UiText(640 - RenderAssets::MeasureUtf8(instructions, 18) * .5f, 696, instructions, 18, kUiInk, false);
}
} // namespace tutorial
