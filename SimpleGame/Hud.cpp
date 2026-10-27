#include "stdafx.h"
#include "Hud.h"
#include "Civic.h"
#include "Drawing.h"
#include "Level.h"
#include "Overworld.h"
#include "RenderQueue.h"
#include "TransitLayout.h"
#include "RenderAssets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
void Button(const UiControl &control, int index)
{
    const auto &world = Game::Get().GetState();
    const auto cursor = world.cursorPosition;
    bool hover = world.cursorInsideViewport && cursor.x >= control.position.x &&
                 cursor.y >= control.position.y && cursor.x <= control.position.x + control.size.x &&
                 cursor.y <= control.position.y + control.size.y;
    bool selected = index == world.ui.focus;
    if (control.action == UiAction::Choice)
    {
        selected = world.dialogue.selection == control.choice;
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
    renderqueue::Opacity(world.uiPreferences.background);
    Sprite("Components/button_" + state, control.position.x, control.position.y, control.size.x,
           control.size.y, control.enabled ? ColorRGBA(1, 1, 1) : ColorRGBA(.55f, .6f, .62f), 1, true);
    renderqueue::Opacity(world.uiPreferences.content);
    Icon(control.enabled ? "chevron" : "lock", control.position.x + 12, control.position.y + 14, 18);
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
        float height = (std::max)(
            46.f,
            20.f +
                24.f * Dialogue::Wrap(std::to_string(i + 1) + "  " + page.choices[i].text, 758, 17).size());
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
    return {{(kMapMinimum.x + kMapMaximum.x) * .5f, (kMapMinimum.y + kMapMaximum.y) * .5f},
            {(minimum.x + maximum.x) * .5f, (minimum.y + maximum.y) * .5f},
            scale,
            0};
}
void MapPolygon(const SchematicView &view, std::vector<Vector2> points, ColorRGBA color)
{
    // Clip all building/terrain geometry to a 64-sided circle. No camera render target or stencil pass.
    bool insideCircle = view.radius > 0;
    if (view.radius > 0)
    {
        Vector2 minimum = points.front(), maximum = points.front();
        float innerRadius = view.radius * std::cos(kPi / 64);
        for (auto point : points)
        {
            minimum.x = (std::min)(minimum.x, point.x);
            minimum.y = (std::min)(minimum.y, point.y);
            maximum.x = (std::max)(maximum.x, point.x);
            maximum.y = (std::max)(maximum.y, point.y);
            float dx = point.x - view.center.x, dy = point.y - view.center.y;
            insideCircle &= dx * dx + dy * dy <= innerRadius * innerRadius;
        }
        if (minimum.x > view.center.x + view.radius || maximum.x < view.center.x - view.radius ||
            minimum.y > view.center.y + view.radius || maximum.y < view.center.y - view.radius)
        {
            return;
        }
    }
    auto clip = [&](Vector2 normal, float bound) {
        if (points.empty())
        {
            return;
        }
        std::vector<Vector2> clipped;
        Vector2 previous = points.back();
        float previousDistance = previous.x * normal.x + previous.y * normal.y - bound;
        for (Vector2 point : points)
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
        points.swap(clipped);
    };
    if (view.radius > 0 && !insideCircle)
    {
        for (int i = 0; i < 64 && !points.empty(); ++i)
        {
            float angle = (i + .5f) * kPi * 2 / 64;
            Vector2 normal = {std::cos(angle), std::sin(angle)};
            clip(normal,
                 normal.x * view.center.x + normal.y * view.center.y + view.radius * std::cos(kPi / 64));
        }
    }
    else if (view.radius <= 0)
    {
        clip({-1, 0}, -kMapMinimum.x);
        clip({1, 0}, kMapMaximum.x);
        clip({0, -1}, -kMapMinimum.y);
        clip({0, 1}, kMapMaximum.y);
    }
    if (points.size() >= 3)
    {
        Drawing::SetColor(color);
        renderqueue::Begin(GL_TRIANGLE_FAN);
        for (auto point : points)
        {
            renderqueue::Vertex(point.x, point.y);
        }
        renderqueue::End();
    }
}
void MapRectangle(const SchematicView &view, Vector2 minimum, Vector2 maximum, ColorRGBA color)
{
    MapPolygon(view,
               {view.Project(minimum), view.Project({maximum.x, minimum.y}), view.Project(maximum),
                view.Project({minimum.x, maximum.y})},
               color);
}
void MapLine(const SchematicView &view, Vector2 from, Vector2 to, ColorRGBA color, float width)
{
    // A clipped quad keeps even wide road markings completely inside the circular rim.
    Vector2 a = view.Project(from), b = view.Project(to);
    float length = Game::Distance(a, b);
    if (length < .001f)
    {
        return;
    }
    Vector2 side = {-(b.y - a.y) * width / (2 * length), (b.x - a.x) * width / (2 * length)};
    MapPolygon(view,
               {{a.x + side.x, a.y + side.y},
                {b.x + side.x, b.y + side.y},
                {b.x - side.x, b.y - side.y},
                {a.x - side.x, a.y - side.y}},
               color);
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
void MapLabel(Vector2 anchor, const char *label, ColorRGBA color)
{
    float width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(label); *p;)
    {
        unsigned char lead = *p;
        int bytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        width += bytes == 1 ? 8 : 15;
        p += bytes;
    }
    constexpr Vector2 offsets[] = {{12, 5}, {12, 27}, {12, -17}, {12, 49}, {12, -39}};
    Vector2 position = {};
    for (auto offset : offsets)
    {
        position = {Game::Clamp(anchor.x + offset.x, kMapMinimum.x + 5, kMapMaximum.x - width - 5),
                    Game::Clamp(anchor.y + offset.y, kMapMinimum.y + 19, kMapMaximum.y - 5)};
        bool overlap = false;
        for (auto other : mapLabels)
        {
            overlap |= position.x < other.x + other.width + 4 && position.x + width + 4 > other.x &&
                       position.y > other.y - 20 && position.y - 20 < other.y;
        }
        if (!overlap)
        {
            break;
        }
    }
    mapLabels.push_back({position.x, position.y, width});
    if (std::fabs(position.y - anchor.y) > 10)
    {
        Drawing::DrawLine(anchor, {position.x - 3, position.y - 6}, {.45f, .6f, .5f, .7f});
    }
    Drawing::DrawLabel(position.x, position.y, label, color);
}
void MapPin(const SchematicView &view, Vector2 position, ColorRGBA color, const char *label, bool full,
            float size = 5)
{
    if (MapHud::Region() == MapRegion::Mine && !game::GameLevels::Mine().Map().IsExplored(position))
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
        MapPin(view, objective, {1, .88f, .5f}, "현재 목표", full, 5);
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
    if (game::GameLevels::Mine().GetState().loadout.IsPistolEquipped() &&
        (game::GameLevels::IsCombatActive() || CivicCombat::Available()))
    {
        float angle = game::GameLevels::Chapter().IsActive() ? game::GameLevels::Chapter().GetAimAngle()
                                                             : game::GameLevels::Mine().GetAimAngle();
        auto aim = game::MineLevel::GetAimDirection(angle);
        heading = std::atan2(aim.y, aim.x);
    }
    DirectionArrow(view.Project(PlayerMapPosition()), heading + kPi * .25f, full ? 8.f : 10.f,
                   {.25f, 1, .94f});
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
    return MapView(fullMap).Project(worldPoint);
}
Vector2 MapHud::MapToWorld(Vector2 canvasPoint, bool fullMap)
{
    return MapView(fullMap).Unproject(canvasPoint);
}
bool MapHud::Contains(Vector2 canvasPoint, bool fullMap)
{
    return MapView(fullMap).Inside(canvasPoint);
}
bool MapHud::HandleClick(Vector2 point, bool clear)
{
    auto &world = Game::Get().GetState();
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
    if (!Game::Get().GetState().mapVisible)
    {
        return;
    }
    Drawing::DrawRectangle(0, 0, kCanvasWidth, kCanvasHeight, {.01f, .025f, .03f, .5f});
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
    UiText(80, 741, "방향 고정 · 지도 열람 중 일시정지 · 자동 길찾기 경로는 제공하지 않음", 14,
           {.55f, .72f, .72f});
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
        label = "집 단말 / 메시지 확인";
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
    case InteractionTarget::JetBike:
        label = w.isBikeMounted ? "바이크 / 하차" : "바이크 / 탑승";
        break;
    case InteractionTarget::CityReceiver:
        label = "도시 수신망 / 조사·출발";
        break;
    case InteractionTarget::MineEntrance:
        label = "광산 / 입장";
        break;
    case InteractionTarget::ObservatorySignal:
        label = "신호 콘솔 / 기록 수신";
        break;
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
    default:
        break;
    }
    Hud::DrawInteraction(label);
}

// ============================================================================
// Hud
// ============================================================================
void Hud::DrawNoticePanel()
{
    const auto &w = Game::Get().GetState();
    if (w.noticeSecondsRemaining <= 0 || Dialogue::Active() || w.journalVisible || w.dialogueHistoryVisible ||
        w.ui.screen != UiScreen::None)
    {
        return;
    }
    Icon("check", 918, 322, 22, true);
    auto rows = Dialogue::Wrap(w.noticeText, 294, 16);
    for (size_t i = 0; i < (std::min)(size_t(2), rows.size()); ++i)
    {
        UiText(954, 342 + float(i) * 24, rows[i] + (i == 1 && rows.size() > 2 ? "…" : ""), 16, kUiInk, true);
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
    float h = (std::max)(48.f, 24.f + float(rows.size()) * 22);
    Panel(450, 514, 380, h);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        UiText(468, 545 + float(i) * 22, rows[i], 17);
    }
    Sprite("Components/keycap", 788, 526, 28, 28);
    UiText(797, 546, "E", 17);
}
void Hud::DrawObjective(const std::string &objective, const std::string &context)
{
    Icon("objective", 32, 36, 24, true);
    auto rows = Dialogue::Wrap(objective, 420, 18);
    for (size_t i = 0; i < (std::min)(size_t(2), rows.size()); ++i)
    {
        UiText(68, 57 + i * 26, rows[i], 18, kUiInk, true);
    }
    UiText(68, 58 + float((std::min)(size_t(2), rows.size())) * 26, context, 14, {.68f, .83f, .83f}, true);
}

void CityHud::Draw()
{
    const auto &w = Game::Get().GetState();
    std::string objective;
    auto &chapter = game::GameLevels::Chapter();
    if (chapter.IsAvailable())
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
            objective = "광장 수신망에서 조사 경로 확인";
            break;
        }
    }
    int city = Overworld::CityAt(w.playerPosition);
    std::string area = w.isInsideHome      ? "헤일로 / 집"
                       : w.isInObservatory ? "헤일로 / 전망대"
                       : city >= 0         ? Overworld::Cities()[city].name
                                           : "도시 연결 구역";
    Hud::DrawObjective(objective, area + " · J 조사 기록");
    if (w.isSideQuestAccepted && !w.isSideQuestComplete)
    {
        Icon("objective", 36, 139, 16, true);
        UiText(68, 153, w.isPowerRestored ? "마라에게 전력 복구 보고" : "선택 · 마라의 전력 장치 복구", 15,
               {.94f, .77f, .44f}, true);
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
}

// ============================================================================
// MineHud
// ============================================================================
void MineHud::Draw()
{
    auto &level = game::GameLevels::Mine();
    const auto &s = level.GetState();
    Hud::DrawObjective(s.quest.GetObjective(), "광산 / 구조 신호 조사 · J 기록");
    MapHud::DrawMinimap();
    DrawScanner();
    if (!Dialogue::Active())
    {
        std::string hint;
        if (level.IsNear(s.riaPosition))
        {
            hint = "리아 / 대화·응급 처치";
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
    Hud::DrawObjective(level.Objective(), "1장 / 빛나는 개척지 · J 조사 기록");
    UiText(68, 113, "현장 조사 " + std::to_string(level.CompletedSites()) + " / 3", 15, {.67f, .83f, .8f},
           true);
    MapHud::DrawMinimap();
    game::ChapterInteraction point{};
    if (level.NearestInteraction(point))
    {
        Hud::DrawInteraction(point.action == game::ChapterAction::ConvoySupply ? "운송대 보급 / 교환"
                                                                               : point.label);
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
    std::vector<std::string> rows;
    if (world.journalVisible)
    {
        for (const auto &entry : Dialogue::Journal())
        {
            auto wrapped = Dialogue::Wrap(entry, 526, 18);
            rows.insert(rows.end(), wrapped.begin(), wrapped.end());
            rows.push_back("");
        }
    }
    else
    {
        for (const auto &entry : world.dialogue.history)
        {
            auto header = Dialogue::Wrap(entry.speaker + " · " + entry.channel, 526, 18);
            rows.insert(rows.end(), header.begin(), header.end());
            auto wrapped = Dialogue::Wrap(entry.text, 526, 18);
            rows.insert(rows.end(), wrapped.begin(), wrapped.end());
            rows.push_back("");
        }
        if (rows.empty())
        {
            rows.push_back("아직 읽은 대사가 없습니다.");
        }
    }
    constexpr size_t perPage = 17;
    size_t pages = (std::max)(size_t(1), (rows.size() + perPage - 1) / perPage);
    size_t offset = (std::min)(world.dialogue.historyOffset, pages - 1);
    size_t page = world.dialogueHistoryVisible ? pages - 1 - offset : offset;
    Panel(176, 72, 928, 656);
    Icon(world.journalVisible ? "record" : "history", 200, 100, 26);
    UiText(240, 125, world.journalVisible ? "조사 기록" : "대사 이력", 24);
    UiText(510, 161, world.journalVisible ? "확인한 사실과 다음 행동" : "읽은 대사 / 전달 기록", 18,
           {.61f, .88f, .83f});
    UiText(200, 326, "현재 실행의 기록", 17, {.95f, .78f, .4f});
    UiText(200, 356, "게임 종료 후 복원하는", 15);
    UiText(200, 380, "저장 기능은 아직 없습니다.", 15);
    if (Dialogue::Danger())
    {
        Icon("warning", 200, 426, 22);
        UiText(234, 445, "교전 중", 17, {1, .78f, .4f});
        UiText(200, 479, "기록을 보는 동안에도", 15);
        UiText(200, 503, "시간이 계속 흐릅니다.", 15);
    }
    for (size_t i = 0; i < perPage && page * perPage + i < rows.size(); ++i)
    {
        UiText(510, 194 + float(i) * 25, rows[page * perPage + i], 18);
    }
    UiText(510, 650, std::to_string(page + 1) + " / " + std::to_string(pages), 14, {.59f, .75f, .76f});
    UiText(200, 701,
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
    Icon("health", 32, 736, 24, true);
    UiText(66, 735, std::to_string(stats.health), 28, critical ? ColorRGBA(1, .56f, .44f) : kUiInk, true);
    float maximumX = 76.f + RenderAssets::MeasureUtf8(std::to_string(stats.health), 28);
    UiText(maximumX, 734, "/ " + std::to_string(stats.maxHealth), 18, kUiInk, true);
    Sprite("Components/hud_health_track", 64, 746, 224, 16);
    Sprite("Components/hud_health_fill", 66, 748, 220, 12,
           critical ? ColorRGBA(1, .52f, .43f) : ColorRGBA(.65f, .92f, .86f),
           Game::Clamp(float(stats.health) / (std::max)(1, stats.maxHealth), 0, 1));
    bool combat = game::GameLevels::IsCombatActive() || CivicCombat::Available();
    if (critical)
    {
        UiText(32, 690,
               game::GameLevels::IsCombatActive()
                   ? (stats.recoveryKits > 0 ? "위험 · G 회복" : "위험 · 회복 키트 없음")
                   : "위험 · 의료 지원 필요",
               16, {1, .61f, .46f}, true);
    }
    if (w.isBikeMounted)
    {
        float speed = std::fabs(w.bikeSpeed) * models::WorldScale::metersPerUnit * 3.6f;
        char value[48];
        std::snprintf(value, sizeof(value), speed < 10 ? "%.1f" : "%.0f", speed);
        Icon("bike", 1044, 724, 25, true);
        UiText(1176, 749, value, 44, kUiInk, true, true);
        UiText(1248, 748, "km/h", 18, kUiInk, true, true);
        if (w.bikeSpeed < -.01f)
        {
            Icon("reverse", 1090, 675, 20, true);
            UiText(1248, 695, "후진", 17, kUiInk, true, true);
        }
        else if (w.pressedKeys[' '] && w.bikeSpeed > 0)
        {
            Icon("boost", 1090, 675, 20, true);
            UiText(1248, 695, "부스트", 17, {.55f, .95f, .88f}, true, true);
        }
    }
    else if (combat && mine.loadout.IsPistolEquipped())
    {
        const auto &gun = mine.pistol;
        Icon("pistol", 1040, 723, 28, true);
        Icon("infinite", 1064, 754, 15, true);
        char count[32];
        std::snprintf(count, sizeof(count), "%02d", gun.GetRounds());
        UiText(1182, 746, count, 38, gun.GetRounds() ? kUiInk : ColorRGBA(1, .78f, .4f), true, true);
        UiText(1248, 745, "/ " + std::to_string(gun.GetMagazineSize()), 22, kUiInk, true, true);
        if (gun.IsReloading())
        {
            Sprite("Components/hud_reload_track", 1088, 755, 156, 14);
            Sprite("Components/hud_reload_fill", 1090, 757, 152, 10, {1, .78f, .4f}, gun.GetReloadProgress());
            UiText(1248, 705, "재장전", 17, {1, .77f, .4f}, true, true);
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
        if (w.cursorInsideViewport && !w.weaponWheel.visible)
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
    if (!w.isBikeMounted && combat && !mine.loadout.IsPistolEquipped())
    {
        UiText(1248, 736, "맨손", 25, kUiInk, true, true);
        UiText(1248, 764, "좌클릭 근접 · Q 홀드 무기 선택", 15, kUiInk, true, true);
        if (w.cursorInsideViewport && !w.weaponWheel.visible)
        {
            auto p = w.cursorPosition;
            Drawing::DrawEllipse(p.x, p.y, 5, 5, {.02f, .05f, .06f, .8f});
            Drawing::DrawEllipse(p.x, p.y, 2, 2, {.75f, .96f, .9f});
        }
    }
    const auto &law = CivicJustice::State();
    if (CivicJustice::IsExterior() && law.wanted > 0)
    {
        for (int i = 0; i < 5; ++i)
        {
            UiText(558 + i * 35, 60, i < law.wanted ? "★" : "☆", 25, {1, .78f, .4f}, true);
        }
        Sprite("Components/hud_heat_track", 556, 71, 169, 10);
        Sprite("Components/hud_heat_fill", 558, 73, 165, 6, {1, .78f, .4f},
               Game::Clamp(law.heat / CivicJustice::kMaximumHeat, 0, 1));
        UiText(570, 106, law.identified ? "보안망 추적 중" : "수색 중 · 미식별", 15, {1, .78f, .4f}, true);
        if (law.responseStarted && !law.identified)
        {
            int remaining = int(std::ceil(
                (std::max)(0.f, CivicJustice::kUnidentifiedEscapeSeconds - law.initialUnseenSeconds)));
            UiText(570, 128, "미발각 해제까지 " + std::to_string(remaining) + "초", 14, {.87f, .88f, .76f},
                   true);
        }
    }
    if (game::GameLevels::Mine().IsActive() && mine.quest.IsRiaJoined())
    {
        Icon(mine.riaCombat.health > 0 ? "companion" : "injured", 32, 115, 18, true);
        UiText(62, 132,
               mine.riaCombat.health <= 0 ? "리아 · 회복 중"
               : mine.riaCombat.engaged   ? "리아 · 엄호 중"
                                          : "리아 · 동행",
               15, {.83f, .86f, .71f}, true);
        Icon("rescue", 32, 148, 18, true);
        UiText(62, 165, "광산 구조 " + std::to_string(mine.rescuedPeople) + " / 12명", 15, kUiInk, true);
    }
    if (game::GameLevels::Chapter().IsActive())
    {
        Icon(chapter.ria.combat.health > 0 ? "companion" : "injured", 32, 139, 18, true);
        std::string names = chapter.ria.combat.health > 0 ? "리아 · 동행" : "리아 · 회복 중";
        if (chapter.kainJoined)
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
    w.mouseFiring = false;
    w.movementAmount = w.movementSpeed = 0;
    std::fill(std::begin(w.pressedKeys), std::end(w.pressedKeys), false);
}
void Hud::CloseMenu()
{
    OpenMenu(UiScreen::None);
}
std::vector<UiControl> Hud::Controls()
{
    const auto &w = Game::Get().GetState();
    auto screen = w.ui.screen;
    if (screen == UiScreen::None && w.isPaused)
    {
        screen = UiScreen::Pause;
    }
    std::vector<UiControl> controls;
    auto row = [&](UiAction action, const std::string &label, bool enabled = true) {
        controls.push_back({action, {440, 220 + float(controls.size()) * 58}, {400, 46}, label, enabled});
    };
    switch (screen)
    {
    case UiScreen::Title:
        row(UiAction::Start, "시작하기");
        row(UiAction::Stages, "스테이지 목록 · 테스트");
        row(UiAction::Options, "옵션");
        row(UiAction::Help, "조작 안내");
        row(UiAction::Quit, "종료");
        break;
    case UiScreen::Pause:
        row(UiAction::Continue, "계속하기");
        row(UiAction::Stages, "스테이지 목록 · 테스트");
        row(UiAction::Options, "옵션");
        row(UiAction::Summary, "보유 · 성장 정보");
        row(UiAction::Help, "조작 안내");
        row(UiAction::Restart, game::GameLevels::Chapter().IsActive() ? "헤일로 귀환 · 기록 유지"
                               : game::GameLevels::Mine().IsActive()  ? "광산 재시작"
                                                                      : "새 게임 · 진행 초기화");
        row(UiAction::Quit, "종료");
        break;
    case UiScreen::Stages:
        for (int i = 0; i < int(TestStage::Count); ++i)
        {
            controls.push_back({UiAction::EnterStage,
                                {336, 222 + i * 55.f},
                                {608, 44},
                                Game::StageName(TestStage(i)),
                                true,
                                i});
        }
        controls.push_back({UiAction::Back, {336, 566}, {608, 44}, "뒤로"});
        break;
    case UiScreen::Options:
        row(UiAction::Background, "배경 불투명도");
        row(UiAction::Border, "테두리 불투명도");
        row(UiAction::Content, "내용 불투명도");
        row(UiAction::Cartoon, "카툰 명암");
        row(UiAction::Resolution, "월드 렌더 해상도");
        row(UiAction::Antialiasing, "안티앨리어싱");
        row(UiAction::Back, "뒤로");
        for (size_t i = 0; i < controls.size(); ++i)
        {
            controls[i].position = {336, 190 + float(i) * 58};
            controls[i].size = {608, 46};
        }
        break;
    case UiScreen::Summary:
    case UiScreen::Help:
        controls.push_back({UiAction::Back, {640, 594}, {280, 46}, "뒤로"});
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
            controls.push_back({UiAction::Journal, {200, 170}, {270, 46}, "조사 기록"});
            controls.push_back({UiAction::History, {200, 228}, {270, 46}, "대사 이력"});
            controls.push_back({UiAction::Previous, {510, 668}, {155, 38}, "이전 페이지"});
            controls.push_back({UiAction::Next, {680, 668}, {155, 38}, "다음 페이지"});
            controls.push_back({UiAction::Continue, {850, 668}, {226, 38}, "닫기"});
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
        g.Reset();
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
                glutLeaveMainLoop();
                break;
            }
            CloseMenu();
            if (game::GameLevels::Chapter().IsActive())
            {
                game::GameLevels::Chapter().Leave();
            }
            else if (game::GameLevels::Mine().IsActive())
            {
                game::GameLevels::Mine().Restart();
            }
            else
            {
                g.Reset();
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
            style.resolution = style.resolution == WorldResolution::Native ? WorldResolution::Half
                               : style.resolution == WorldResolution::Half ? WorldResolution::Quarter
                                                                           : WorldResolution::Native;
        }
        r.SetStyle(style);
        break;
    }
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
    if (w.ui.screen == UiScreen::None && !w.isPaused)
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
            w.ui.screen == UiScreen::Supply || w.ui.screen == UiScreen::None)
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
    if (w.ui.screen == UiScreen::Options && (key == 'a' || key == 'd'))
    {
        Activate(controls[size_t(w.ui.focus)], key == 'a' ? -1 : 1);
    }
    if ((w.ui.screen == UiScreen::Pause || w.ui.screen == UiScreen::None) && key == 'r')
    {
        Activate({UiAction::Restart, {}, {}, ""});
    }
    return true;
}
bool Hud::HandleSpecialKey(int key)
{
    const auto &w = Game::Get().GetState();
    if (w.ui.screen == UiScreen::None && !w.isPaused)
    {
        return false;
    }
    if (key == GLUT_KEY_UP)
    {
        return HandleKey('w');
    }
    if (key == GLUT_KEY_DOWN)
    {
        return HandleKey('s');
    }
    if (key == GLUT_KEY_LEFT)
    {
        return HandleKey('a');
    }
    if (key == GLUT_KEY_RIGHT)
    {
        return HandleKey('d');
    }
    return key != GLUT_KEY_F7 && key != GLUT_KEY_F8;
}
void Hud::HandleMotion()
{
    auto &w = Game::Get().GetState();
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
    float ratio = Game::Clamp((w.cursorPosition.x - 620) / 228, 0, 1);
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
    bool modal = w.ui.screen != UiScreen::None || w.isPaused || w.journalVisible || w.dialogueHistoryVisible;
    if (!modal && !Dialogue::Active())
    {
        return false;
    }
    int hit = -1;
    if (w.cursorInsideViewport)
    {
        for (size_t i = 0; i < controls.size(); ++i)
        {
            if (Inside(controls[i], w.cursorPosition))
            {
                hit = int(i);
                break;
            }
        }
    }
    if (button != GLUT_LEFT_BUTTON)
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
    }
    else
    {
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
    if (!level.IsActive() || !level.GetState().convoyRescued ||
        Game::Distance(w.playerPosition, {24.5f, 20.5f}) >= 1.5f || Dialogue::Danger())
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
    Drawing::DrawRectangle(0, 0, kCanvasWidth, kCanvasHeight, {0, 0, 0, HospitalRecovery::Opacity()});
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
    UiScreen screen = w.ui.screen;
    if (screen == UiScreen::None && w.isPaused)
    {
        screen = UiScreen::Pause;
    }
    if (screen == UiScreen::None || HospitalRecovery::Active())
    {
        return;
    }
    Drawing::DrawRectangle(0, 0, kCanvasWidth, kCanvasHeight, {.01f, .025f, .03f, .55f});
    auto &stats = game::GameLevels::Mine().GetState().player;
    bool narrow = screen == UiScreen::Title || screen == UiScreen::Pause;
    if (narrow)
    {
        Panel(416, 112, 448, 576);
    }
    else
    {
        Panel(312, 90, 656, 600);
    }
    float x = narrow ? 440.f : 336.f;
    const char *title = screen == UiScreen::Title     ? "침묵하는 관문"
                        : screen == UiScreen::Pause   ? "일시정지"
                        : screen == UiScreen::Stages  ? "스테이지 목록 · 테스트 진입"
                        : screen == UiScreen::Options ? "옵션"
                        : screen == UiScreen::Summary ? "보유 · 성장 정보"
                        : screen == UiScreen::Help    ? "조작 안내"
                        : screen == UiScreen::Supply  ? "운송대 보급 교환"
                        : screen == UiScreen::Medical ? "의료소 복귀"
                                                      : "진행 확인";
    UiText(x, 159, title, 28, {.83f, .95f, .91f});
    auto controls = Controls();
    for (size_t i = 0; i < controls.size(); ++i)
    {
        Button(controls[i], int(i));
    }
    if (screen == UiScreen::Options)
    {
        const auto &p = w.uiPreferences;
        for (int i = 0; i < 3; ++i)
        {
            float alpha = i == 0 ? p.background : i == 1 ? p.border : p.content;
            UiText(920, 219 + i * 58, std::to_string(int(std::round(alpha * 100))) + "%", 17, kUiInk, false,
                   true);
            Sprite("Components/gauge_track", 620, 223 + i * 58, 228, 5);
            Sprite("Components/gauge_scan", 620, 223 + i * 58, 228, 5, {1, 1, 1},
                   (alpha - (i == 2 ? .85f : .8f)) / (i == 2 ? .15f : .2f));
        }
        const auto &style = GameRenderer::Get().Backend().GetStyle();
        UiText(920, 393, style.cartoon ? "켜짐" : "꺼짐", 17, kUiInk, false, true);
        UiText(920, 451,
               style.resolution == WorldResolution::Native ? "기본"
               : style.resolution == WorldResolution::Half ? "1/2"
                                                           : "1/4",
               17, kUiInk, false, true);
        int samples = GameRenderer::Get().Backend().GetAntialiasingSamples();
        UiText(920, 509,
               !style.antialiasing                           ? "꺼짐"
               : style.resolution != WorldResolution::Native ? "픽셀 모드에서 미적용"
               : samples <= 1                                ? "지원되지 않음"
                                                             : std::to_string(samples) + "×",
               17, kUiInk, false, true);
        UiText(336, 637, "선택 후 A/D 또는 ←/→ 조절 · Enter 전환", 15);
        UiText(336, 661, "UI는 네이티브 해상도 유지 · 설정은 새 게임에도 유지", 14, {.59f, .75f, .75f});
    }
    if (screen == UiScreen::Title || screen == UiScreen::Pause)
    {
        UiText(440, 184, screen == UiScreen::Title ? "에르마 / 개척지 기록" : "플레이를 잠시 멈췄습니다", 15,
               {.59f, .79f, .77f});
        UiText(440, 638, "현재 진행은 실행 세션에만 유지됩니다.", 15, {.95f, .77f, .4f});
        UiText(440, 666, "W/S · 방향 키 선택 / Enter 확인", 14, {.59f, .75f, .75f});
    }
    if (screen == UiScreen::Stages)
    {
        UiText(336, 198, "선택 시 현재 진행 초기화 · 이전 필수 스토리 완료 처리", 16, {1, .78f, .4f});
        UiText(336, 648, "선택한 구간의 시작 지점으로 바로 이동합니다.", 16);
    }
    if (screen == UiScreen::Summary)
    {
        Icon("level", 336, 191, 30);
        UiText(386, 219, "레벨 " + std::to_string(stats.level), 25);
        int floor = game::MineLevel::ExperienceForLevel(stats.level),
            next = game::MineLevel::ExperienceForLevel((std::min)(100, stats.level + 1));
        UiText(336, 262, "경험치 " + std::to_string(stats.experience) + " / " + std::to_string(next), 18);
        Sprite("Components/gauge_track", 336, 280, 608, 8);
        Sprite("Components/gauge_experience", 336, 280, 608, 8, {1, 1, 1},
               Game::Clamp(stats.level >= 100 ? 1.f
                                              : float(stats.experience - floor) / (std::max)(1, next - floor),
                           0, 1));
        const std::string rows[] = {
            "크레딧  " + std::to_string(stats.credits), "거래 부품  " + std::to_string(stats.salvage),
            "회복 키트  " + std::to_string(stats.recoveryKits),
            "공격  " + std::to_string(stats.attack) + " / 방어  " + std::to_string(stats.defense),
            "악명  " + std::to_string(int(CivicJustice::State().infamy)) + " / 거래 할증 +" +
                std::to_string(int(std::round((CivicJustice::PriceMultiplier() - 1) * 100))) + "%"};
        const char *icons[] = {"credit", "parts", "heal", "defense", "reputation"};
        for (int i = 0; i < 5; ++i)
        {
            Icon(icons[i], 336, 310 + i * 48, 22);
            UiText(374, 331 + i * 48, rows[i], 18);
        }
    }
    if (screen == UiScreen::Help)
    {
        const char *rows[] = {"WASD 이동 · 마우스 조준 · 좌클릭 장착 무기 공격",
                              "Q 홀드 + 마우스 무기 선택 · V 근접 · R 재장전",
                              "E 조사·대화·승하차 · G 회복 (광산·1장)",
                              "바이크 W 가속 / S 제동·후진 / AD 조향 / Space 부스트",
                              "F 광산 스캔 · M 지도 · 좌클릭 목적지 / 우클릭 해제",
                              "대화 E/Enter 다음 · 숫자 선택 후 E/Enter 확정",
                              "J 조사 기록 · H 대사 이력 · PageUp/Down 기록 페이지",
                              "I 보유 정보 · ? 조작 안내 · Esc 일시정지",
                              "예비 탄창 무제한 · 저장/불러오기는 아직 지원하지 않음"};
        for (int i = 0; i < 9; ++i)
        {
            UiText(336, 215 + i * 39, rows[i], 16);
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
                                       ? "게임을 종료할까요? 현재 세션의 진행은 파일로 저장되지 않습니다."
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

void Hud::Render()
{
    const auto &w = Game::Get().GetState();
    renderqueue::Opacity(w.uiPreferences.content);
    if (w.ui.screen == UiScreen::None && !w.mapVisible && !w.journalVisible && !w.dialogueHistoryVisible &&
        !HospitalRecovery::Active())
    {
        static CityHud city;
        static MineHud mine;
        static ChapterHud chapter;
        if (game::GameLevels::Chapter().IsActive())
        {
            chapter.Draw();
        }
        else if (game::GameLevels::Mine().IsActive())
        {
            mine.Draw();
        }
        else
        {
            city.Draw();
        }
        DrawVitals();
        DrawNoticePanel();
        DrawDialogue();
    }
    MapHud::DrawFullMap();
    ChapterHud::DrawJournal();
    DrawMenu();
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
    constexpr Vector2 center = {640, 390};
    constexpr float inner = 86, outer = 224;
    Drawing::DrawRectangle(0, 0, kCanvasWidth, kCanvasHeight, {.01f, .025f, .035f, .56f});
    struct Sector
    {
        combat::WeaponSlot slot;
        float angle;
        Vector2 label;
        const char *title;
    };
    const Sector sectors[] = {{combat::WeaponSlot::Primary, -kPi / 2, {640, 232}, "주 무장"},
                              {combat::WeaponSlot::Secondary, kPi / 6, {782, 454}, "보조 무장"},
                              {combat::WeaponSlot::Unarmed, 5 * kPi / 6, {498, 454}, "맨손"}};
    for (const auto &sector : sectors)
    {
        const bool available = loadout.IsAvailable(sector.slot);
        const bool hover = wheel.hovered == sector.slot;
        ColorRGBA fill =
            hover ? (available ? ColorRGBA(.10f, .46f, .46f, .94f) : ColorRGBA(.26f, .24f, .23f, .96f))
                  : ColorRGBA(.035f, .085f, .11f, .95f);
        auto point = [&](float a, float r) {
            return Vector2{center.x + std::cos(a) * r, center.y + std::sin(a) * r};
        };
        for (int i = 0; i < 48; ++i)
        {
            float a = sector.angle - kPi / 3 + .015f + (2 * kPi / 3 - .03f) * i / 48;
            float b = sector.angle - kPi / 3 + .015f + (2 * kPi / 3 - .03f) * (i + 1) / 48;
            Drawing::DrawPolygon({point(a, inner), point(a, outer), point(b, outer), point(b, inner)}, fill);
            Drawing::DrawLine(point(a, outer), point(b, outer),
                              hover ? ColorRGBA(.4f, .95f, .87f) : ColorRGBA(.2f, .36f, .4f),
                              hover ? 3.f : 1.f);
        }
        auto label = sector.label;
        ColorRGBA ink = available ? kUiInk : ColorRGBA(.53f, .6f, .62f);
        auto centeredText = [&](const std::string &text, float y, int size, ColorRGBA color) {
            UiText(label.x - RenderAssets::MeasureUtf8(text, size) * .5f, y, text, size, color, true);
        };
        centeredText(sector.title, label.y, 20, ink);
        if (sector.slot == combat::WeaponSlot::Secondary && loadout.HasPistol())
        {
            Icon("pistol", label.x - 18, label.y + 10, 36, false, ink);
            centeredText("개척자 권총", label.y + 69, 16, ink);
        }
        else
        {
            if (sector.slot == combat::WeaponSlot::Unarmed)
            {
                // A closed hand, rather than a weapon icon, for the unarmed slot.
                Drawing::DrawRectangle(label.x - 13, label.y + 24, 26, 20, ink);
                Drawing::DrawRectangle(label.x - 8, label.y + 44, 16, 6, ink);
                for (int finger = 0; finger < 4; ++finger)
                {
                    Drawing::DrawEllipse(label.x - 9 + finger * 6, label.y + 23, 4, 5, ink);
                }
                Drawing::DrawLine({label.x - 10, label.y + 32}, {label.x + 6, label.y + 32}, fill, 2);
                Drawing::DrawLine({label.x + 6, label.y + 32}, {label.x + 9, label.y + 39}, fill, 2);
            }
            else
            {
                Icon("empty", label.x - 16, label.y + 12, 32, false, ink);
            }
            centeredText(available ? "근접 공격" : "비어 있음", label.y + 69, 16, ink);
        }
    }
    const char *name = wheel.hovered == combat::WeaponSlot::Unarmed ? "맨손"
                       : loadout.IsAvailable(wheel.hovered)         ? "권총"
                                                                    : "빈 슬롯";
    UiText(center.x - RenderAssets::MeasureUtf8(name, 26) * .5f, center.y, name, 26, kUiInk, true);
    const char *status = loadout.IsAvailable(wheel.hovered) ? "Q 놓아서 선택" : "장착할 무기 없음";
    UiText(center.x - RenderAssets::MeasureUtf8(status, 14) * .5f, center.y + 29, status, 14, kUiInk);
    float length = std::hypot(wheel.offset.x, wheel.offset.y);
    if (length >= 24)
    {
        float r = (std::min)(length, outer - 10);
        Drawing::DrawEllipse(center.x + wheel.offset.x / length * r, center.y + wheel.offset.y / length * r,
                             5, 5, {.58f, 1, .93f});
    }
    UiText(480, 660, "Q 홀드 + 마우스 이동 · Esc 취소", 18, kUiInk, true);
}
} // namespace tutorial
