#include "stdafx.h"

#include "Tutorial.h"
#include "TutorialDrawing.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{
void DrawBuilding(Building building)
{
    Vector2 buildingScreen =
                WorldToScreen(building.x + building.width * .5f, building.y + building.depth * .5f),
            playerScreen = WorldToScreen(g_state.playerPosition.x, g_state.playerPosition.y);
    float opacity =
        (building.x + building.y + building.width + building.depth >
             g_state.playerPosition.x + g_state.playerPosition.y &&
         std::fabs(buildingScreen.x - playerScreen.x) < (building.width + building.depth) * 22 &&
         playerScreen.y > buildingScreen.y - building.height - 25 && playerScreen.y < buildingScreen.y + 35)
            ? .28f
            : 1;
    DrawBox(building.x, building.y, building.width, building.depth, building.height,
            ColorRGBA(.3f, .37f, .44f, opacity));
    DrawBox(building.x + .2f, building.y + .2f, building.width - .4f, building.depth - .4f,
            building.height + 8, ColorRGBA(.34f, .42f, .47f, opacity));
    for (int floor = 18; floor < building.height - 10; floor += 22)
    {
        for (int column = 0; column < 3; column++)
        {
            Vector2 windowPosition = WorldToScreen(
                building.x + building.width, building.y + .25f + column * building.depth / 3, (float)floor);
            DrawMaterialQuad(windowPosition, {windowPosition.x - 10, windowPosition.y + 5},
                             {windowPosition.x - 10, windowPosition.y + 13},
                             {windowPosition.x, windowPosition.y + 8}, SurfaceMaterial::Glass,
                             ColorRGBA(.18f, .42f, .52f, opacity));
            DrawLine(windowPosition, {windowPosition.x - 9, windowPosition.y + 4.5f},
                     g_state.isPowerRestored ? ColorRGBA(.95f, .72f, .37f, opacity)
                                             : ColorRGBA(.2f, .65f, .73f, opacity),
                     3);
            windowPosition = WorldToScreen(building.x + .25f + column * building.width / 3,
                                           building.y + building.depth, (float)floor);
            DrawLine(windowPosition, {windowPosition.x + 9, windowPosition.y + 4.5f},
                     ColorRGBA(.35f, .6f, .72f, opacity * .7f), 3);
        }
    }
    Vector2 roof = WorldToScreen(building.x + .5f, building.y + .5f, building.height + 8);
    DrawLine(roof, {roof.x, roof.y - 22}, ColorRGBA(.35f, .55f, .64f, opacity));
    DrawGlow({roof.x, roof.y - 22}, 6, ColorRGBA(.9f, .37f, .3f, opacity));
}

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
        glLineWidth((float)j * 2);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 96; i++)
        {
            float angle = i * 2 * kPi / 96;
            glVertex2f(gateX + std::cos(angle) * 115, gateY + std::sin(angle) * 145);
        }
        glEnd();
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
        float x = std::fmod(g_state.animationTimeSeconds * 45 + i * 360.f, kCanvasWidth + 180.f) - 90,
              y = railY - x * 55 / kCanvasWidth - 12;
        DrawVehicle({x, y}, .65f);
    }
}

void DrawInterior()
{
    for (int x = -3; x < 3; x++)
    {
        for (int y = -3; y < 3; y++)
        {
            DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal, ColorRGBA(.23f, .3f, .34f));
        }
    }
    DrawBox(-3, -3, 6, .12f, 95, ColorRGBA(.13f, .21f, .28f));
    DrawBox(-3, -3, .12f, 6, 95, ColorRGBA(.1f, .18f, .24f));
    Vector2 window = WorldToScreen(-1, -2.85f, 68);
    DrawLine(window, {window.x + 110, window.y + 55}, ColorRGBA(.25f, .72f, .82f), 18);
    DrawBox(-2.6f, -2.6f, 1.8f, 1.1f, 16, ColorRGBA(.32f, .39f, .46f));
    DrawBox(-2.5f, -2.5f, .5f, .8f, 22, ColorRGBA(.66f, .68f, .62f));
    DrawBox(1.3f, -2.6f, 1.3f, .8f, 27, ColorRGBA(.26f, .34f, .39f));
    Vector2 terminalScreen = WorldToScreen(1.6f, -1.9f, 46);
    DrawRectangle(terminalScreen.x - 15, terminalScreen.y - 12, 30, 19, ColorRGBA(.15f, .65f, .72f));
    DrawGlow(WorldToScreen(1, -1.2f), 20, ColorRGBA(.3f, .95f, .9f));
    DrawBox(-2.4f, 1.1f, 1.2f, .6f, 20, ColorRGBA(.35f, .28f, .25f));
    DrawGlow(WorldToScreen(-2, 1.4f, 35), 18, ColorRGBA(1, .63f, .28f));
    DrawTile(1.9f, 1.9f, 1, 1, ColorRGBA(.15f, .45f, .47f));
    DrawPerson(g_state.playerPosition, ColorRGBA(.67f, .8f, .82f), true);
    Vector2 labelPosition = WorldToScreen(1, -1.2f, 65);
    DrawLabel(labelPosition.x - 30, labelPosition.y, "단말");
    labelPosition = WorldToScreen(2.4f, 2.4f);
    DrawLabel(labelPosition.x - 20, labelPosition.y + 25, "출구");
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

namespace
{
void DrawStreetTiles()
{
    for (int x = -20; x < 20; x++)
    {
        for (int y = -14; y < 16; y++)
        {
            Vector2 tileScreen = WorldToScreen(float(x), float(y));
            if (tileScreen.x < -80 || tileScreen.x > kCanvasWidth + 80 || tileScreen.y < -40 ||
                tileScreen.y > kCanvasHeight + 80)
            {
                continue;
            }
            bool road = (y >= -1 && y <= 0) || (x >= 1 && x <= 2) || y == 7 || x == -10;
            float shade = (x + y + 30) % 2 * .009f;
            DrawMaterialTile(float(x), float(y), road ? SurfaceMaterial::Asphalt : SurfaceMaterial::Concrete,
                             road ? ColorRGBA(.13f, .19f, .23f)
                                  : ColorRGBA(.28f + shade, .34f + shade, .37f + shade));
            if (road && x % 2 == 0 && y == 0)
            {
                DrawTile(x + .1f, y + .1f, .6f, .035f, ColorRGBA(.37f, .59f, .63f));
            }
        }
    }
}

void DrawBuildingShadows()
{
    // Shared directional light: layered silhouette offsets provide a soft penumbra.
    // Shadows are composited on the ground before depth-sorted objects.
    for (const auto &building : g_state.buildings)
    {
        Vector2 back = WorldToScreen(building.x, building.y);
        Vector2 right = WorldToScreen(building.x + building.width, building.y);
        Vector2 front = WorldToScreen(building.x + building.width, building.y + building.depth);
        Vector2 left = WorldToScreen(building.x, building.y + building.depth);
        float length = building.height * .36f;
        for (int layer = 0; layer < 6; ++layer)
        {
            float spread = layer * 1.3f;
            DrawPolygon({back,
                         right,
                         {right.x + length + spread, right.y + length * .4f},
                         {front.x + length + spread, front.y + length * .4f + spread},
                         {left.x + length, left.y + length * .4f + spread},
                         left},
                        ColorRGBA(.005f, .01f, .025f, .035f));
        }
        DrawSoftShadow({front.x, front.y}, building.width * 25, building.depth * 12, .4f);
    }
}

void DrawStreetFurniture()
{
    DrawTile(5, -3.8f, 2.6f, 1.7f, ColorRGBA(.2f, .28f, .34f));
    for (int i = -6; i <= 6; i += 3)
    {
        DrawBox((float)i, 3.7f, .55f, .6f, 12, ColorRGBA(.2f, .28f, .3f));
        Vector2 p = WorldToScreen((float)i, 3.7f, 52);
        DrawLine(WorldToScreen((float)i, 3.7f), p, ColorRGBA(.25f, .4f, .47f), 2);
        DrawGlow(p, 18, ColorRGBA(.35f, .85f, .85f));
    }
}

void DrawDepthSortedObjects()
{
    enum class SceneObjectKind
    {
        Building,
        Player,
        Lia,
        Mara,
        Resident,
        Vehicle
    };
    struct SceneObject
    {
        float depth;
        SceneObjectKind kind;
        int index;
    };
    std::vector<SceneObject> items;
    for (size_t i = 0; i < g_state.buildings.size(); i++)
    {
        items.push_back({g_state.buildings[i].x + g_state.buildings[i].width + g_state.buildings[i].y +
                             g_state.buildings[i].depth,
                         SceneObjectKind::Building, (int)i});
    }
    items.push_back({g_state.playerPosition.x + g_state.playerPosition.y, SceneObjectKind::Player, 0});
    items.push_back({kLiaPosition.x + kLiaPosition.y, SceneObjectKind::Lia, 0});
    items.push_back({kMaraPosition.x + kMaraPosition.y, SceneObjectKind::Mara, 0});
    for (int i = 0; i < 6; ++i)
    {
        items.push_back({-12 + i * 5.f - .5f, SceneObjectKind::Vehicle, i});
    }
    for (size_t i = 0; i < g_state.residents.size(); ++i)
    {
        items.push_back({g_state.residents[i].position.x + g_state.residents[i].position.y,
                         SceneObjectKind::Resident, int(i)});
    }
    std::sort(items.begin(), items.end(),
              [](const SceneObject &a, const SceneObject &b) { return a.depth < b.depth; });
    for (auto item : items)
    {
        if (item.kind == SceneObjectKind::Vehicle)
        {
            Vector2 body = WorldToScreen(-12 + item.index * 5.f, -.5f,
                                         20 + std::sin(g_state.animationTimeSeconds + item.index) * 1.5f);
            DrawVehicle(body, .8f);
        }
        if (item.kind == SceneObjectKind::Resident)
        {
            const auto &resident = g_state.residents[item.index];
            DrawPerson(resident.position, ColorRGBA(.55f + (item.index % 3) * .15f, .65f, .75f));
            if (Distance(g_state.playerPosition, resident.position) < 2.2f)
            {
                Vector2 label = WorldToScreen(resident.position.x, resident.position.y, 62);
                DrawLabel(label.x - 20, label.y, resident.name);
            }
        }
        if (item.kind == SceneObjectKind::Building)
        {
            DrawBuilding(g_state.buildings[item.index]);
        }
        if (item.kind == SceneObjectKind::Player)
        {
            DrawPerson(g_state.playerPosition, ColorRGBA(.68f, .8f, .83f), true);
        }
        if (item.kind == SceneObjectKind::Lia)
        {
            DrawPerson(kLiaPosition, ColorRGBA(.89f, .52f, .28f));
        }
        if (item.kind == SceneObjectKind::Mara)
        {
            DrawPerson(kMaraPosition, ColorRGBA(.48f, .4f, .64f));
        }
    }
}

void DrawUtilityDevices()
{
    DrawBox(kRelayPosition.x, kRelayPosition.y, .35f, .35f, 25, ColorRGBA(.25f, .35f, .4f));
    DrawGlow(WorldToScreen(kRelayPosition.x, kRelayPosition.y, 29), 14,
             g_state.isPowerRestored ? ColorRGBA(.3f, 1, .65f) : ColorRGBA(1, .65f, .25f));
    DrawBox(kBeaconPosition.x, kBeaconPosition.y, .4f, .4f, 30, ColorRGBA(.3f, .45f, .5f));
    DrawGlow(WorldToScreen(kBeaconPosition.x, kBeaconPosition.y, 34), 20, ColorRGBA(.5f, .83f, 1));
}

void DrawVehicleShadows()
{
    for (int i = 0; i < 6; ++i)
    {
        float worldX = -12 + i * 5.f;
        Vector2 ground = WorldToScreen(worldX, -.5f);
        DrawSoftShadow({ground.x + 8, ground.y + 5}, 48, 16, .55f);
    }
}

void DrawQuestMarkers()
{
    DrawInteractionMarker(kHomeDoor, "집", ColorRGBA(.6f, .78f, .81f));
    DrawInteractionMarker(kLiaPosition, "리아", ColorRGBA(1, .69f, .38f));
    DrawInteractionMarker(kMaraPosition,
                          g_state.isSideQuestComplete ? "마라 / 고마워요" : "마라 / 서브퀘스트",
                          ColorRGBA(1, .77f, .4f));
    if (g_state.isSideQuestAccepted && !g_state.isPowerRestored)
    {
        DrawInteractionMarker(kRelayPosition, "전력 복구", ColorRGBA(1, .7f, .3f));
    }
    if (g_state.mainQuestStage >= MainQuestStage::ReadGateSignal)
    {
        DrawInteractionMarker(kBeaconPosition, "관문 신호", ColorRGBA(.4f, .93f, .96f));
    }
}
} // namespace

void DrawExterior()
{
    DrawStreetTiles();
    DrawBuildingShadows();
    DrawVehicleShadows();
    DrawStreetFurniture();
    DrawDepthSortedObjects();
    DrawUtilityDevices();
    DrawQuestMarkers();
}
} // namespace tutorial
