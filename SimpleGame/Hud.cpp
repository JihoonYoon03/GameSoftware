#include "stdafx.h"
#include "Hud.h"
#include "Drawing.h"
#include "Level.h"
#include "Overworld.h"
#include "RenderQueue.h"
#include "TransitLayout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{

// ============================================================================
// MapHud — shared north-up schematic projection and CPU-clipped map geometry
// ============================================================================
namespace
{
constexpr Vector2 kMinimapCenter = {1136, 144};
constexpr float kMinimapRadius = 102;
constexpr float kMinimapRange = 34;
constexpr Vector2 kMapMinimum = {84, 112}, kMapMaximum = {1196, 684};

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
        // North is -world Y. This never uses the isometric camera or player heading.
        return {center.x + (p.x - worldCenter.x) * scale, center.y + (p.y - worldCenter.y) * scale};
    }
    Vector2 Unproject(Vector2 p) const
    {
        return {worldCenter.x + (p.x - center.x) / scale, worldCenter.y + (p.y - center.y) / scale};
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
    float width = maximum.x - minimum.x, height = maximum.y - minimum.y;
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
    Vector2 first = view.Project(minimum), last = view.Project(maximum);
    if (view.radius > 0 && (last.x < view.center.x - view.radius || first.x > view.center.x + view.radius ||
                            last.y < view.center.y - view.radius || first.y > view.center.y + view.radius))
    {
        return;
    }
    MapPolygon(view, {first, {last.x, first.y}, last, {first.x, last.y}}, color);
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
}
void DrawFieldSchematic(const SchematicView &view, bool full)
{
    DrawMapTiles(view);
    if (MapHud::Region() == MapRegion::Mine)
    {
        const auto &mine = game::GameLevels::Mine().GetState();
        MapPin(view, mine.entry, {.8f, .87f, .73f}, "헤일로 귀환", full, 4);
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
void DrawSchematic(const SchematicView &view, bool full)
{
    mapLabels.clear();
    if (MapHud::Region() == MapRegion::City)
    {
        DrawCitySchematic(view, full);
    }
    else
    {
        DrawFieldSchematic(view, full);
    }
    DrawDestination(view, full);
    const auto &world = Game::Get().GetState();
    float heading = world.isBikeMounted ? world.bikeYaw : world.facingYaw;
    if (game::GameLevels::IsCombatActive())
    {
        float angle = game::GameLevels::Chapter().IsActive() ? game::GameLevels::Chapter().GetAimAngle()
                                                             : game::GameLevels::Mine().GetAimAngle();
        auto aim = game::MineLevel::GetAimDirection(angle);
        heading = std::atan2(aim.y, aim.x);
    }
    DirectionArrow(view.Project(PlayerMapPosition()), heading, full ? 8.f : 10.f, {.25f, 1, .94f});
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
    destination = {true, target};
    return true;
}
void MapHud::DrawMinimap()
{
    const auto view = MapView(false);
    Drawing::DrawEllipse(view.center.x, view.center.y, view.radius + 9, view.radius + 9,
                         {.015f, .06f, .075f, .97f});
    Drawing::DrawEllipse(view.center.x, view.center.y, view.radius + 3, view.radius + 3, {.46f, .66f, .61f});
    Drawing::DrawEllipse(view.center.x, view.center.y, view.radius, view.radius, {.18f, .28f, .22f});
    DrawSchematic(view, false);
    Drawing::DrawLabel(view.center.x - 5, view.center.y - view.radius + 17, "N", {.94f, .97f, .84f},
                       GLUT_BITMAP_HELVETICA_18);
    const auto &destination = Game::Get().GetState().mapDestinations[size_t(Region())];
    Drawing::DrawRectangle(view.center.x - 110, view.center.y + view.radius + 12, 220,
                           destination.active ? 49.f : 27.f, {.015f, .055f, .065f, .9f});
    Drawing::DrawLabel(view.center.x - 65, view.center.y + view.radius + 29, "M 전체 지도 · 북쪽 고정",
                       {.66f, .84f, .8f});
    if (destination.active)
    {
        char text[80];
        std::snprintf(text, sizeof(text), "목적지 %.0f m",
                      Game::Distance(PlayerMapPosition(), destination.position) *
                          models::WorldScale::metersPerUnit);
        Drawing::DrawLabel(view.center.x - 45, view.center.y + view.radius + 51, text, {.45f, .87f, 1});
    }
}
void MapHud::DrawFullMap()
{
    if (!Game::Get().GetState().mapVisible)
    {
        return;
    }
    Drawing::DrawRectangle(0, 0, float(kCanvasWidth), float(kCanvasHeight), {.005f, .025f, .035f, .97f});
    Drawing::DrawRectangle(64, 40, 1152, 720, {.025f, .08f, .09f, .99f});
    Drawing::DrawRectangle(kMapMinimum.x, kMapMinimum.y, kMapMaximum.x - kMapMinimum.x,
                           kMapMaximum.y - kMapMinimum.y, {.16f, .26f, .21f});
    const char *title = Region() == MapRegion::City   ? "헤일로 주변 / 전체 도시 약도"
                        : Region() == MapRegion::Mine ? "광산 / 구조 신호 조사 약도"
                                                      : "개척지 / 조사 구역 약도";
    Drawing::DrawLabel(86, 80, title, {.8f, .94f, .86f}, GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(848, 80, "N ↑  /  M 또는 Esc 닫기", {.58f, .79f, .75f});
    DrawSchematic(MapView(true), true);
    Drawing::DrawLabel(86, 717, "좌클릭 목적지 설정 / 우클릭 목적지 해제 / 지도를 보는 동안 일시정지",
                       {.7f, .87f, .81f});
    Drawing::DrawLabel(86, 743,
                       "청록: 내 위치 · 파랑: 설정한 목적지 · 노랑: 현재 목표 · 밝은 청록 도로: 고가 구간",
                       {.51f, .72f, .7f});
}

// ============================================================================
// CityHud
// ============================================================================
void CityHud::DrawInteractionPrompt()
{
    if (Game::Get().FindNearbyInteraction() != InteractionTarget::None)
    {
        Drawing::DrawRectangle(kCanvasWidth * .5f - 95, kCanvasHeight * .69f, 190, 30,
                               ColorRGBA(.02f, .08f, .11f, .9f));
        Drawing::DrawLabel(kCanvasWidth * .5f - 76, kCanvasHeight * .69f + 20, "[ E ]  상호작용",
                           ColorRGBA(.5f, 1, .94f), GLUT_BITMAP_HELVETICA_18);
    }
}

// ============================================================================
// Hud
// ============================================================================
void Hud::DrawNoticePanel()
{
    auto &world = Game::Get().GetState();
    if (world.noticeSecondsRemaining <= 0)
    {
        return;
    }
    constexpr float panelWidth = 940;
    constexpr float fontSize = 15;
    float x = (kCanvasWidth - panelWidth) / 2;
    Drawing::DrawRectangle(x, kCanvasHeight - 132.f, panelWidth, 80, ColorRGBA(.02f, .055f, .085f, .96f));
    Drawing::DrawRectangle(x, kCanvasHeight - 132.f, 3, 80, ColorRGBA(.32f, .75f, .78f));
    // Advance complete UTF-8 codepoints; never split Korean syllables at byte offsets.
    const std::string &text = world.noticeText;
    size_t start = 0;
    for (int line = 0; start < text.size() && line < 3; ++line)
    {
        size_t end = start;
        float advance = 0;
        while (end < text.size())
        {
            unsigned char lead = static_cast<unsigned char>(text[end]);
            size_t bytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            float glyphWidth = bytes == 1 ? fontSize * .55f : fontSize;
            if (advance + glyphWidth > panelWidth - 32)
            {
                break;
            }
            if (end + bytes > text.size())
            {
                break;
            }
            advance += glyphWidth;
            end += bytes;
        }
        if (end == start)
        {
            break;
        }
        Drawing::DrawLabel(x + 16, kCanvasHeight - 107.f + line * 23, text.substr(start, end - start),
                           ColorRGBA(.82f, .9f, .91f));
        start = end;
        while (start < text.size() && text[start] == ' ')
        {
            ++start;
        }
    }
}

// ============================================================================
// Hud
// ============================================================================
void Hud::DrawPauseOverlay()
{
    auto &world = Game::Get().GetState();
    if (world.isPaused)
    {
        Drawing::DrawRectangle(0, 0, (float)kCanvasWidth, (float)kCanvasHeight,
                               ColorRGBA(0, .02f, .04f, .8f));
        Drawing::DrawLabel(kCanvasWidth * .5f - 90, kCanvasHeight * .5f, "일시정지",
                           ColorRGBA(.7f, .95f, .95f), GLUT_BITMAP_HELVETICA_18);
        Drawing::DrawLabel(kCanvasWidth * .5f - 90, kCanvasHeight * .5f + 30,
                           game::GameLevels::Chapter().IsActive()
                               ? "Esc 계속 / R 헤일로 귀환(기록 유지) / Q 종료"
                               : "Esc 계속 / R 다시 시작 / Q 종료");
    }
}

// ============================================================================
// CityHud
// ============================================================================
void CityHud::Draw()
{
    auto &world = Game::Get().GetState();
    Drawing::DrawRectangle(22, 20, 650, 111, ColorRGBA(.025f, .06f, .09f, .92f));
    Drawing::DrawRectangle(22, 20, 3, 111, ColorRGBA(.27f, .86f, .83f));
    int cityIndex = Overworld::CityAt(world.playerPosition);
    const char *area = cityIndex >= 0 ? Overworld::Cities()[cityIndex].name : "도시 간 연결 도로";
    Drawing::DrawLabel(40, 45, world.isInObservatory ? "헤일로 / N-0 전망대" : area,
                       ColorRGBA(.67f, .9f, .91f), GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(40, 68,
                       world.isInsideHome                          ? "플레이어의 집 / 06:14"
                       : world.isInObservatory                     ? "상층 관측 데크 / 고도 180 m"
                       : game::GameLevels::Chapter().IsAvailable() ? "광산 보고 다음 날 / 오전 작업 시간"
                                                                   : "관문 인접 주거 구역 / 06:14",
                       ColorRGBA(.4f, .66f, .73f));
    Drawing::DrawLabel(
        40, 94,
        game::GameLevels::Chapter().IsAvailable() ? game::GameLevels::Chapter().Objective()
        : world.gateSignalHeard && world.hasDescendedObservatory
            ? game::GameLevels::Mine().GetState().quest.GetObjective()
        : world.mainQuestStage == MainQuestStage::ReadHomeTerminal   ? "01  집 단말 확인 [E]"
        : world.mainQuestStage == MainQuestStage::MeetLia            ? "02  밖에서 리아와 대화 [E]"
        : world.mainQuestStage == MainQuestStage::EnterObservatory   ? "03  전망대 승강기로 올라가기 [E]"
        : world.mainQuestStage == MainQuestStage::ReadGateSignal     ? "04  상층 콘솔에서 관문 신호 측정 [E]"
        : world.mainQuestStage == MainQuestStage::DescendObservatory ? "05  승강기로 도시에 내려가기 [E]"
        : world.mainQuestStage == MainQuestStage::MountJetBike       ? "06  개인용 제트 바이크 탑승 [E]"
                                                                     : "07  광산 이동 / 서막 완료",
        ColorRGBA(.91f, .89f, .75f));
    char timer[80];
    std::snprintf(timer, sizeof(timer), "%02d:%02d / 짧은 서막", (int)world.playTimeSeconds / 60,
                  (int)world.playTimeSeconds % 60);
    Drawing::DrawLabel(
        40, 116, game::GameLevels::Chapter().IsAvailable() ? "1장 / 빛나는 개척지 · J 조사 기록" : timer,
        ColorRGBA(.4f, .6f, .66f));
    MapHud::DrawMinimap();
    if (!world.isInsideHome && !world.isInObservatory)
    {
        char bike[160];
        std::snprintf(bike, sizeof(bike),
                      world.isBikeMounted ? "개인 바이크 / %.0f km/h · W 가속 S 제동 A/D 조향 · E 하차"
                                          : "도시 5곳 · 도시당 20명 / 주황 표시: 주차한 바이크 · E 탑승",
                      world.bikeSpeed * models::WorldScale::metersPerUnit * 3.6f);
        Drawing::DrawLabel(26, float(kCanvasHeight) - 46, bike, {.6f, .9f, .9f});
        const auto &player = game::GameLevels::Mine().GetState().player;
        Drawing::DrawLabel(26, float(kCanvasHeight) - 69,
                           "체력 " + std::to_string(player.health) + "/" + std::to_string(player.maxHealth),
                           {.9f, .78f, .67f});
        int crossing = Overworld::Traffic().NearbyCrossing(world.playerPosition);
        if (crossing >= 0)
        {
            bool walk = Overworld::Traffic().PedestrianGreen(
                crossing, Overworld::Traffic().CrossingRoad(crossing, world.playerPosition));
            Drawing::DrawRectangle(425, 143, 420, 32, {.02f, .055f, .08f, .92f});
            Drawing::DrawLabel(438, 165, walk ? "보행 신호 / 횡단 가능" : "보행 신호 / 대기 · 자동 전환",
                               walk ? ColorRGBA(.2f, 1, .8f) : ColorRGBA(1, .43f, .37f));
        }
    }
    Drawing::DrawRectangle(22, 143, 365, 64, ColorRGBA(.025f, .06f, .09f, .88f));
    Drawing::DrawLabel(40, 165, "선택 목표 / 남겨 둔 불빛", ColorRGBA(.96f, .72f, .4f));
    Drawing::DrawLabel(40, 188,
                       world.isSideQuestComplete   ? "완료: 마라의 집에 불빛이 돌아왔습니다."
                       : world.isPowerRestored     ? "광장의 마라에게 돌아가세요."
                       : world.isSideQuestAccepted ? "마라 동쪽의 전력 장치를 복구하세요."
                                                   : "주거 광장의 마라와 대화하세요.");
    Drawing::DrawLabel(
        26, (float)kCanvasHeight - 22,
        "WASD 이동·운전 / E 상호작용·승하차 / M 전체 지도·목적지 / 광산 입구 하차 후 E / Esc 휴식",
        ColorRGBA(.63f, .78f, .81f));
    DrawInteractionPrompt();
}

// ============================================================================
// MineHud
// ============================================================================
void MineHud::DrawScanner()
{
    const auto &mine = game::GameLevels::Mine().GetState();
    char message[100];
    if (mine.scannerSeconds > 0)
    {
        std::snprintf(message, sizeof(message), "구조 스캔 %.0f / %.0f m",
                      mine.scannerRadius * models::WorldScale::metersPerUnit,
                      game::kScannerRange * models::WorldScale::metersPerUnit);
        const auto view = MapView(false);
        for (int i = 0; i < 80; ++i)
        {
            float a = i * kPi * 2 / 80, b = (i + 1) * kPi * 2 / 80;
            MapLine(view,
                    {mine.scannerOrigin.x + std::cos(a) * mine.scannerRadius,
                     mine.scannerOrigin.y + std::sin(a) * mine.scannerRadius},
                    {mine.scannerOrigin.x + std::cos(b) * mine.scannerRadius,
                     mine.scannerOrigin.y + std::sin(b) * mine.scannerRadius},
                    {.4f, 1, .76f, .8f}, 1.5f);
        }
    }
    else if (mine.scannerCooldown > 0)
    {
        std::snprintf(message, sizeof(message), "F 스캐너 충전 %.1f초", mine.scannerCooldown);
    }
    else
    {
        std::snprintf(message, sizeof(message), "F 구조 신호 스캔");
    }
    Drawing::DrawRectangle(1026, 302, 220, 25, {.015f, .055f, .065f, .9f});
    Drawing::DrawLabel(1034, 319, message, {.47f, .88f, .77f});
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
        // Use the camera's isometric bearing for screen-edge arrows, independently of north-up maps.
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
    auto &world = Game::Get().GetState();
    const auto &stats = game::GameLevels::Mine().GetState().player;
    Drawing::DrawRectangle(22, 20, 650, 183, {.02f, .04f, .055f, .94f});
    Drawing::DrawLabel(40, 47, "낙하한 신호 / 광산 구조 신호 조사", {.85f, .85f, .68f},
                       GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(40, 77, game::GameLevels::Mine().GetState().quest.GetObjective(), {.6f, .95f, .87f});
    char text[180];
    std::snprintf(text, sizeof(text), "체력 %d / %d  ·  레벨 %d  ·  경험치 %d  ·  처치 %d", stats.health,
                  stats.maxHealth, stats.level, stats.experience, game::GameLevels::Mine().GetState().kills);
    Drawing::DrawLabel(40, 105, text);
    Drawing::DrawRectangle(40, 116, 440, 7, {.2f, .07f, .07f});
    Drawing::DrawRectangle(40, 116, 440.f * stats.health / stats.maxHealth, 7, {.8f, .28f, .2f});
    std::snprintf(text, sizeof(text), "%s  %d / %d  ·  %s",
                  game::GameLevels::Mine().GetState().pistol.GetName(),
                  game::GameLevels::Mine().GetState().pistol.GetRounds(),
                  game::GameLevels::Mine().GetState().pistol.GetMagazineSize(),
                  game::GameLevels::Mine().GetState().pistol.IsReloading() ? "재장전 중"
                                                                           : "R 재장전 / 예비 탄창 무제한");
    Drawing::DrawLabel(40, 148, text, {.95f, .8f, .47f});
    if (game::GameLevels::Mine().GetState().pistol.IsReloading())
    {
        Drawing::DrawRectangle(40, 156, 440 * game::GameLevels::Mine().GetState().pistol.GetReloadProgress(),
                               4, {.95f, .75f, .3f});
    }
    std::snprintf(text, sizeof(text), "구조 %d / 12명 · 회복 키트 %d · 부품 %d · 리아 동행",
                  game::GameLevels::Mine().GetState().rescuedPeople, stats.recoveryKits, stats.salvage);
    Drawing::DrawLabel(40, 185, text, {.55f, .75f, .75f});
    MapHud::DrawMinimap();
    DrawScanner();
    const auto cursor = world.cursorPosition;
    float gap = 6 + game::GameLevels::Mine().GetState().pistol.GetSpread() * 120;
    ColorRGBA crosshair = game::GameLevels::Mine().GetState().pistol.IsReloading() ? ColorRGBA(1, .65f, .3f)
                                                                                   : ColorRGBA(.75f, 1, .92f);
    Drawing::DrawLine({cursor.x - gap - 9, cursor.y}, {cursor.x - gap, cursor.y}, crosshair, 2);
    Drawing::DrawLine({cursor.x + gap, cursor.y}, {cursor.x + gap + 9, cursor.y}, crosshair, 2);
    Drawing::DrawLine({cursor.x, cursor.y - gap - 9}, {cursor.x, cursor.y - gap}, crosshair, 2);
    Drawing::DrawLine({cursor.x, cursor.y + gap}, {cursor.x, cursor.y + gap + 9}, crosshair, 2);
    Drawing::DrawEllipse(cursor.x, cursor.y, 1.5f, 1.5f, crosshair);
    Drawing::DrawLabel(26, kCanvasHeight - 22.f,
                       "WASD 이동 / 마우스 조준·좌클릭 사격 / R 재장전 / F 스캔 / E 조사·구조·회수 / Q 회복 "
                       "/ M 지도 / Esc 정지");
}

// ============================================================================
// ChapterHud : Hud
// ============================================================================
void ChapterHud::Draw()
{
    auto &level = game::GameLevels::Chapter();
    const auto &state = level.GetState();
    const auto &mine = game::GameLevels::Mine().GetState();
    const auto &world = Game::Get().GetState();
    Drawing::DrawRectangle(22, 20, 750, 190, {.025f, .06f, .075f, .94f});
    Drawing::DrawLabel(40, 48, "1장 / 빛나는 개척지", {.85f, .94f, .84f}, GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(40, 74, level.Objective(), {.9f, .85f, .6f});
    Drawing::DrawLabel(40, 100,
                       state.generatorRestored ? "[완료] 발전기 / 폐쇄 선로 분리·도시 전력 안정"
                                               : "[조사] 발전기 / 정전과 주기적 전력 유출",
                       {.6f, .85f, .75f});
    Drawing::DrawLabel(40, 124,
                       state.convoyRescued   ? "[완료] 운송대 5명 구조 / 카인 합류"
                       : state.escortStarted ? "[진행] 운송대 5명 안전 지점 이동 중"
                                             : "[조사] 운송대 / 카인과 점호·측량 우회로 확보",
                       {.6f, .85f, .75f});
    Drawing::DrawLabel(40, 148,
                       state.ledgerFound       ? "[완료] 폐설비 / 기본 장부 확보"
                       : state.tradeProofFound ? "[조사] 폐설비 / 거래 기록 제시 또는 전투"
                                               : "[조사] 폐설비 / 입구 거래 기록·점거자와 대화",
                       {.6f, .85f, .75f});
    char text[180];
    std::snprintf(text, sizeof(text), "체력 %d/%d · 권총 %d/%d · %s · 동행: 리아%s", mine.player.health,
                  mine.player.maxHealth, mine.pistol.GetRounds(), mine.pistol.GetMagazineSize(),
                  mine.pistol.IsReloading() ? "재장전 중" : "R 재장전", state.kainJoined ? " · 카인" : "");
    Drawing::DrawLabel(40, 184, text, {.82f, .85f, .86f});
    MapHud::DrawMinimap();
    game::ChapterInteraction point{};
    if (level.NearestInteraction(point))
    {
        Drawing::DrawRectangle(350, 548, 580, 34, {.02f, .07f, .09f, .92f});
        Drawing::DrawLabel(368, 571, point.label, {.9f, .95f, .8f}, GLUT_BITMAP_HELVETICA_18);
    }
    auto cursor = world.cursorPosition;
    float gap = 6 + mine.pistol.GetSpread() * 120;
    ColorRGBA cross = mine.pistol.IsReloading() ? ColorRGBA(1, .65f, .3f) : ColorRGBA(.7f, 1, .9f);
    Drawing::DrawLine({cursor.x - gap - 8, cursor.y}, {cursor.x - gap, cursor.y}, cross, 2);
    Drawing::DrawLine({cursor.x + gap, cursor.y}, {cursor.x + gap + 8, cursor.y}, cross, 2);
    Drawing::DrawLine({cursor.x, cursor.y - gap - 8}, {cursor.x, cursor.y - gap}, cross, 2);
    Drawing::DrawLine({cursor.x, cursor.y + gap}, {cursor.x, cursor.y + gap + 8}, cross, 2);
    Drawing::DrawLabel(
        26, kCanvasHeight - 22.f,
        "WASD 이동 / E 조사·대화 / 좌클릭 사격 / R 재장전 / Q 회복 / M 지도 / J 기록 / Esc 일시정지");
}
void ChapterHud::DrawJournal()
{
    auto &level = game::GameLevels::Chapter();
    const auto &s = level.GetState();
    if (!level.IsAvailable())
    {
        return;
    }
    if (s.comparisonStarted && (s.comparisonSeconds < 12 || s.journalVisible))
    {
        Drawing::DrawRectangle(408, 226, 565, 92, {.02f, .055f, .085f, .96f});
        Drawing::DrawLabel(426, 249, "원격 관측 / N-0 외곽 설비 순차 점등");
        int lamps = int(Game::Clamp(s.comparisonSeconds - 1, 0, 6) * 8 / 6);
        for (int i = 0; i < 8; ++i)
        {
            Drawing::DrawRectangle(430.f + i * 62, 266, 44, 12,
                                   i < lamps ? ColorRGBA(.5f, .95f, .9f) : ColorRGBA(.1f, .2f, .23f));
        }
        Drawing::DrawLabel(426, 304,
                           s.recordsCompared ? "2장 조사 경로 확보 / 카인의 외곽 수로 지도"
                                             : "관문 자체 가동 기록 없음 / 신호 동일 발신 여부 미확인");
    }
    if (!s.journalVisible)
    {
        return;
    }
    Drawing::DrawRectangle(110, 225, 1060, 350, {.02f, .045f, .065f, .97f});
    Drawing::DrawLabel(135, 256, "조사 기록 / J 닫기", {.7f, .95f, .9f}, GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(
        135, 290, "광산: 실종 탐사선의 구조 신호·관측자 문양·중계 좌표. 자기 음성과의 동일 원인은 미확인.");
    Drawing::DrawLabel(
        135, 326,
        s.generatorInspected
            ? "발전기: 폐쇄 선로로 주기적 전력 유출. 분리 후에도 정지한 계기판 바늘이 같은 주기로 움직임."
            : "발전기: 아직 조사 기록 없음.");
    Drawing::DrawLabel(
        135, 362,
        s.receiverInspected
            ? "운송대: 실제 5명, 교신 응답 6명. 끊어진 수신기의 과거 교신 반복. 여섯 번째 구조 대상은 미확정."
            : "운송대: 아직 교신 조사 기록 없음.");
    Drawing::DrawLabel(135, 398,
                       s.tradeProofFound || s.ledgerFound
                           ? "폐설비: 구매자가 특정 중계 부품에 높은 값을 제시. 부품 철거 뒤 새 전원 유입. "
                             "점거자는 설계자가 아님."
                           : "폐설비: 아직 거래 조사 기록 없음.");
    Drawing::DrawLabel(
        135, 434,
        s.ledgerFound
            ? "기본 장부: 발주처 삭제. 광산 좌표·유출 주기·교신 이상 시각은 동일 설비 N-0/17에 연결."
            : "기본 장부: 미확보.");
    Drawing::DrawLabel(
        135, 470,
        s.kainJoined ? "카인: 구조 후 동행 합류. N-0 외곽 근무 경험을 밝혔으나 세부 과거는 아직 말하지 않음."
                     : "카인: 운송대 현장 점호·안전 우회로 확보를 우선.");
    Drawing::DrawLabel(135, 506,
                       s.detailLedgerFound
                           ? "선택 기록: 상세 구매 장부 보관. 구매자 추적 단서 확보 / 발주처의 정체는 미확인."
                           : "선택 기록: 상세 구매 장부 미확보.");
    Drawing::DrawLabel(135, 548,
                       s.recordsCompared
                           ? "다음 조사선: N-0 외곽 수로. 다리는 사라졌지만 아래 수로가 남아 있을 가능성."
                           : "귀환 조건: 세 현장 조사 완료 후 헤일로 수신망에서 기록 대조.",
                       {.9f, .8f, .55f});
}

void Hud::Render()
{
    static ChapterHud chapter;
    static CityHud city;
    static MineHud mine;
    Hud &current = game::GameLevels::Chapter().IsActive() ? static_cast<Hud &>(chapter)
                   : game::GameLevels::Mine().IsActive()  ? static_cast<Hud &>(mine)
                                                          : static_cast<Hud &>(city);
    current.Draw();
    ChapterHud::DrawJournal();
    DrawNoticePanel();
    MapHud::DrawFullMap();
    DrawPauseOverlay();
}
} // namespace tutorial
