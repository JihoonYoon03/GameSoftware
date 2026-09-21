#include "stdafx.h"
#include "WorldScene.h"
#include "../Tutorial.h"
#include "../TutorialDrawing.h"
#include <cmath>

namespace tutorial
{
void DrawInterior()
{
    auto &scene = GetWorldScene(WorldSceneKind::Interior);
    scene.BeginPlacement();
    for (int x = -3; x < 3; ++x)
    {
        for (int y = -3; y < 3; ++y)
        {
            PlaceWorldActor(scene, "tile/" + std::to_string(x) + "/" + std::to_string(y), float(x), float(y),
                            WorldBounds(float(x), float(y), 1, 1, 0), 0, 0, [x, y]() {
                                DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal,
                                                 ColorRGBA(.23f, .3f, .34f));
                            });
        }
    }
    PlaceWorldActor(scene, "wall/back", -3, -3, WorldBounds(-3, -3, 6, .12f, 95), 1, 0,
                    []() { DrawBox(-3, -3, 6, .12f, 95, ColorRGBA(.13f, .21f, .28f)); });
    PlaceWorldActor(scene, "wall/side", -3, -3, WorldBounds(-3, -3, .12f, 6, 95), 1, 1,
                    []() { DrawBox(-3, -3, .12f, 6, 95, ColorRGBA(.1f, .18f, .24f)); });
    PlaceWorldActor(scene, "window", -1, -2.85f, WorldBounds(-1, -2.85f, 0, 0, 80, 150), 1, 2, []() {
        Vector2 window = WorldToScreen(-1, -2.85f, 68);
        DrawLine(window, {window.x + 110, window.y + 55}, ColorRGBA(.25f, .72f, .82f), 18);
    });
    PlaceWorldActor(scene, "bed", -2.6f, -2.6f, WorldBounds(-2.6f, -2.6f, 1.8f, 1.1f, 22), 1, 3, []() {
        DrawBox(-2.6f, -2.6f, 1.8f, 1.1f, 16, ColorRGBA(.32f, .39f, .46f));
        DrawBox(-2.5f, -2.5f, .5f, .8f, 22, ColorRGBA(.66f, .68f, .62f));
    });
    PlaceWorldActor(scene, "terminal", 1.3f, -2.6f, WorldBounds(1, -2.6f, 1.6f, 1.4f, 65), 1, 4, []() {
        DrawBox(1.3f, -2.6f, 1.3f, .8f, 27, ColorRGBA(.26f, .34f, .39f));
        Vector2 terminalScreen = WorldToScreen(1.6f, -1.9f, 46);
        DrawRectangle(terminalScreen.x - 15, terminalScreen.y - 12, 30, 19, ColorRGBA(.15f, .65f, .72f));
        DrawGlow(WorldToScreen(1, -1.2f), 20, ColorRGBA(.3f, .95f, .9f));
    });
    PlaceWorldActor(scene, "table", -2.4f, 1.1f, WorldBounds(-2.4f, 1.1f, 1.2f, .6f, 55), 1, 5, []() {
        DrawBox(-2.4f, 1.1f, 1.2f, .6f, 20, ColorRGBA(.35f, .28f, .25f));
        DrawGlow(WorldToScreen(-2, 1.4f, 35), 18, ColorRGBA(1, .63f, .28f));
    });
    PlaceWorldActor(scene, "door", 1.9f, 1.9f, WorldBounds(1.9f, 1.9f, 1, 1, 0), 1, 6,
                    []() { DrawTile(1.9f, 1.9f, 1, 1, ColorRGBA(.15f, .45f, .47f)); });
    PlaceWorldActor(scene, "player", g_state.playerPosition.x, g_state.playerPosition.y,
                    WorldBounds(g_state.playerPosition.x, g_state.playerPosition.y, 0, 0, 90), 2, 0,
                    []() { DrawPerson(g_state.playerPosition, ColorRGBA(.67f, .8f, .82f), true); });
    PlaceWorldActor(scene, "terminal-label", 1, -1.2f, WorldBounds(1, -1.2f, 0, 0, 80, 80), 3, 0, []() {
        Vector2 label = WorldToScreen(1, -1.2f, 65);
        DrawLabel(label.x - 30, label.y, "단말");
    });
    PlaceWorldActor(scene, "door-label", 2.4f, 2.4f, WorldBounds(2.4f, 2.4f, 0, 0, 0, 80), 3, 0, []() {
        Vector2 label = WorldToScreen(2.4f, 2.4f);
        DrawLabel(label.x - 20, label.y + 25, "출구");
    });
    scene.GetGraph().Reparent("terminal-label", *scene.GetGraph().Find("terminal"));
    scene.GetGraph().Reparent("door-label", *scene.GetGraph().Find("door"));
    DrawScene(scene);
}

namespace
{
void DrawStreetTiles(game::Scene &scene)
{
    for (int x = -20; x < 20; x++)
    {
        for (int y = -14; y < 16; y++)
        {
            PlaceWorldActor(scene, "tile/" + std::to_string(x) + "/" + std::to_string(y), float(x), float(y),
                            WorldBounds(float(x), float(y), 1, 1, 0), 0, 0, [x, y]() {
                                bool road = (y >= -1 && y <= 0) || (x >= 1 && x <= 2) || y == 7 || x == -10;
                                float shade = (x + y + 30) % 2 * .009f;
                                DrawMaterialTile(float(x), float(y),
                                                 road ? SurfaceMaterial::Asphalt : SurfaceMaterial::Concrete,
                                                 road ? ColorRGBA(.13f, .19f, .23f)
                                                      : ColorRGBA(.28f + shade, .34f + shade, .37f + shade));
                                if (road && x % 2 == 0 && y == 0)
                                {
                                    DrawTile(x + .1f, y + .1f, .6f, .035f, ColorRGBA(.37f, .59f, .63f));
                                }
                            });
        }
    }
}

void DrawBuildingShadows(game::Scene &scene)
{
    // Shared directional light: layered silhouette offsets provide a soft penumbra.
    // Shadows are composited on the ground before depth-sorted objects.
    for (size_t i = 0; i < g_state.buildings.size(); ++i)
    {
        const auto building = g_state.buildings[i];
        PlaceWorldActor(
            scene, "shadow/building/" + std::to_string(i), building.x, building.y,
            WorldBounds(building.x, building.y, building.width, building.depth, 0, building.height + 80), 1,
            0, [building]() {
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
            });
    }
}

void DrawStreetFurniture(game::Scene &scene)
{
    PlaceWorldActor(scene, "beacon-platform", 5, -3.8f, WorldBounds(5, -3.8f, 2.6f, 1.7f, 0), 2, 0,
                    []() { DrawTile(5, -3.8f, 2.6f, 1.7f, ColorRGBA(.2f, .28f, .34f)); });
    for (int i = -6; i <= 6; i += 3)
    {
        PlaceWorldActor(scene, "lamp/" + std::to_string(i), float(i), 3.7f,
                        WorldBounds(float(i), 3.7f, .55f, .6f, 80), 2, 0, [i]() {
                            DrawBox((float)i, 3.7f, .55f, .6f, 12, ColorRGBA(.2f, .28f, .3f));
                            Vector2 p = WorldToScreen((float)i, 3.7f, 52);
                            DrawLine(WorldToScreen((float)i, 3.7f), p, ColorRGBA(.25f, .4f, .47f), 2);
                            DrawGlow(p, 18, ColorRGBA(.35f, .85f, .85f));
                        });
    }
}

void DrawDepthSortedObjects(game::Scene &scene)
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
    auto placeObject = [&scene](SceneObject item) {
        Vector2 position = g_state.playerPosition;
        game::Bounds bounds;
        if (item.kind == SceneObjectKind::Building)
        {
            const auto &building = g_state.buildings[item.index];
            position = {building.x, building.y};
            bounds =
                WorldBounds(position.x, position.y, building.width, building.depth, building.height + 40);
        }
        else
        {
            if (item.kind == SceneObjectKind::Vehicle)
            {
                position = {-12 + item.index * 5.f, -.5f};
            }
            else if (item.kind == SceneObjectKind::Resident)
            {
                position = g_state.residents[item.index].position;
            }
            else if (item.kind == SceneObjectKind::Lia)
            {
                position = kLiaPosition;
            }
            else if (item.kind == SceneObjectKind::Mara)
            {
                position = kMaraPosition;
            }
            bounds = WorldBounds(position.x, position.y, 0, 0, 90, 120);
        }
        std::string name;
        switch (item.kind)
        {
        case SceneObjectKind::Building:
            name = "building/" + std::to_string(item.index);
            break;
        case SceneObjectKind::Player:
            name = "player";
            break;
        case SceneObjectKind::Lia:
            name = "lia";
            break;
        case SceneObjectKind::Mara:
            name = "mara";
            break;
        case SceneObjectKind::Resident:
            name = "resident/" + std::to_string(item.index);
            break;
        case SceneObjectKind::Vehicle:
            name = "vehicle/" + std::to_string(item.index);
            break;
        }
        PlaceWorldActor(scene, name, position.x, position.y, bounds, 3, item.depth, [item]() {
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
        });
    };
    for (size_t i = 0; i < g_state.buildings.size(); i++)
    {
        placeObject({g_state.buildings[i].x + g_state.buildings[i].width + g_state.buildings[i].y +
                         g_state.buildings[i].depth,
                     SceneObjectKind::Building, (int)i});
    }
    placeObject({g_state.playerPosition.x + g_state.playerPosition.y, SceneObjectKind::Player, 0});
    placeObject({kLiaPosition.x + kLiaPosition.y, SceneObjectKind::Lia, 0});
    placeObject({kMaraPosition.x + kMaraPosition.y, SceneObjectKind::Mara, 0});
    for (int i = 0; i < 6; ++i)
    {
        placeObject({-12 + i * 5.f - .5f, SceneObjectKind::Vehicle, i});
    }
    for (size_t i = 0; i < g_state.residents.size(); ++i)
    {
        placeObject({g_state.residents[i].position.x + g_state.residents[i].position.y,
                     SceneObjectKind::Resident, int(i)});
    }
}

void DrawUtilityDevices(game::Scene &scene)
{
    PlaceWorldActor(
        scene, "relay", kRelayPosition.x, kRelayPosition.y,
        WorldBounds(kRelayPosition.x, kRelayPosition.y, .35f, .35f, 50), 4, 0, []() {
            DrawBox(kRelayPosition.x, kRelayPosition.y, .35f, .35f, 25, ColorRGBA(.25f, .35f, .4f));
            DrawGlow(WorldToScreen(kRelayPosition.x, kRelayPosition.y, 29), 14,
                     g_state.isPowerRestored ? ColorRGBA(.3f, 1, .65f) : ColorRGBA(1, .65f, .25f));
        });
    PlaceWorldActor(
        scene, "beacon", kBeaconPosition.x, kBeaconPosition.y,
        WorldBounds(kBeaconPosition.x, kBeaconPosition.y, .4f, .4f, 60), 4, 0, []() {
            DrawBox(kBeaconPosition.x, kBeaconPosition.y, .4f, .4f, 30, ColorRGBA(.3f, .45f, .5f));
            DrawGlow(WorldToScreen(kBeaconPosition.x, kBeaconPosition.y, 34), 20, ColorRGBA(.5f, .83f, 1));
        });
}

void DrawVehicleShadows(game::Scene &scene)
{
    for (int i = 0; i < 6; ++i)
    {
        float worldX = -12 + i * 5.f;
        PlaceWorldActor(scene, "shadow/vehicle/" + std::to_string(i), worldX, -.5f,
                        WorldBounds(worldX, -.5f, 0, 0, 0, 75), 1, 0, [worldX]() {
                            Vector2 ground = WorldToScreen(worldX, -.5f);
                            DrawSoftShadow({ground.x + 8, ground.y + 5}, 48, 16, .55f);
                        });
    }
}

void DrawQuestMarkers(game::Scene &scene)
{
    auto marker = [&scene](const std::string &name, Vector2 position, std::string label, ColorRGBA color) {
        PlaceWorldActor(scene, "marker/" + name, position.x, position.y,
                        WorldBounds(position.x, position.y, 0, 0, 70, 230), 5, 0,
                        [position, label, color]() { DrawInteractionMarker(position, label, color); });
    };
    marker("home", kHomeDoor, "집", ColorRGBA(.6f, .78f, .81f));
    marker("lia", kLiaPosition, "리아", ColorRGBA(1, .69f, .38f));
    marker("mara", kMaraPosition, g_state.isSideQuestComplete ? "마라 / 고마워요" : "마라 / 서브퀘스트",
           ColorRGBA(1, .77f, .4f));
    if (g_state.isSideQuestAccepted && !g_state.isPowerRestored)
    {
        marker("relay", kRelayPosition, "전력 복구", ColorRGBA(1, .7f, .3f));
    }
    if (g_state.mainQuestStage >= MainQuestStage::ReadGateSignal)
    {
        marker("beacon", kBeaconPosition, "관문 신호", ColorRGBA(.4f, .93f, .96f));
    }
}
} // namespace

void DrawExterior()
{
    auto &scene = GetWorldScene(WorldSceneKind::Exterior);
    scene.BeginPlacement();
    DrawStreetTiles(scene);
    DrawBuildingShadows(scene);
    DrawVehicleShadows(scene);
    DrawStreetFurniture(scene);
    DrawDepthSortedObjects(scene);
    DrawUtilityDevices(scene);
    DrawQuestMarkers(scene);
    auto &graph = scene.GetGraph();
    for (size_t i = 0; i < g_state.buildings.size(); ++i)
    {
        graph.Reparent("shadow/building/" + std::to_string(i), *graph.Find("building/" + std::to_string(i)));
    }
    for (int i = 0; i < 6; ++i)
    {
        graph.Reparent("shadow/vehicle/" + std::to_string(i), *graph.Find("vehicle/" + std::to_string(i)));
    }
    graph.Reparent("marker/lia", *graph.Find("lia"));
    graph.Reparent("marker/mara", *graph.Find("mara"));
    graph.Reparent("marker/relay", *graph.Find("relay"));
    graph.Reparent("marker/beacon", *graph.Find("beacon"));
    DrawScene(scene);
}
} // namespace tutorial
