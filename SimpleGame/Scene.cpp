#include "stdafx.h"
#include "Scene.h"
#include "Level.h"
#include "Drawing.h"
#include "Hud.h"
#include "Profiler.h"
#include "RenderQueue.h"
#include "RenderCache.h"
#include <array>
#include <cmath>
#include <utility>

namespace game
{
using namespace tutorial;
namespace
{
class WorldView
{
  public:
    WorldView()
    {
        renderqueue::Flush();
        const auto first = WorldCamera::FromViewport({0, 0}),
                   last = WorldCamera::FromViewport({float(kCanvasWidth), float(kCanvasHeight)});
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(first.x, last.x, last.y, first.y, -8192, 8192);
        glMatrixMode(GL_MODELVIEW);
    }
    ~WorldView()
    {
        renderqueue::Flush();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
};
} // namespace

// ============================================================================
// Scene — Actor placement
// ============================================================================
void Scene::BeginPlacement()
{
    placed.clear();
}
Actor &Scene::Place(const std::string &name, const std::string &group, Bounds bounds, int layer, float depth,
                    Actor::DrawCallback callback)
{
    const std::string groupName = "group/" + group;
    profiling::Count(profiling::Counter::ScenePlacements);
    Actor *parent = graph.Find(groupName);
    if (!parent)
    {
        parent = &graph.Add(std::make_unique<Actor>(groupName), graph.GetRoot());
    }
    Actor *actor = graph.Find(name);
    if (!actor)
    {
        actor = &graph.Add(std::make_unique<Actor>(name), *parent);
    }
    else if (actor->GetParent() != parent && actor->GetParent()->GetName().find("group/") == 0)
    {
        graph.Reparent(name, *parent);
    }
    actor->SetBounds(bounds);
    actor->SetDrawOrder(layer, depth);
    actor->SetDrawCallback(std::move(callback));
    placed.insert(name);
    return *actor;
}
void Scene::EndPlacement()
{
    for (const auto &name : previous)
    {
        if (placed.find(name) == placed.end())
        {
            graph.Remove(name);
        }
    }
    previous = placed;
}
SceneGraph &Scene::GetGraph()
{
    return graph;
}
Bounds Scene::WorldBounds(float x, float y, float width, float depth, float height, float margin)
{
    return {(x - y - depth) * 36 - margin, (x + y) * 18 - height - margin, (x + width - y) * 36 + margin,
            (x + width + y + depth) * 18 + margin};
}
void Scene::PlaceWorldActor(const std::string &name, float x, float y, Bounds bounds, int layer, float depth,
                            std::function<void()> draw)
{
    // 8x8 world-cell groups reject whole regions before testing individual objects.
    std::string group = std::to_string(int(std::floor(x / 8))) + "/" + std::to_string(int(std::floor(y / 8)));
    Place(name, group, bounds, layer, depth * 18, [draw = std::move(draw)](const game::Actor &actor) {
        // The bridge alone knows OpenGL; Actor and SceneGraph do not depend on the renderer.
        auto offset = actor.GetWorldPosition();
        renderqueue::PushMatrix();
        renderqueue::Translate(offset.x, offset.y, 0);
        draw();
        renderqueue::PopMatrix();
    });
}
void Scene::DrawActors()
{
    EndPlacement();
    Vector2 origin = Drawing::WorldToScreen(0, 0);
    auto &graph = GetGraph();
    const auto &actors = graph.CollectVisible(
        {WorldCamera::FromViewport({0, 0}).x - origin.x, WorldCamera::FromViewport({0, 0}).y - origin.y,
         WorldCamera::FromViewport({float(kCanvasWidth), float(kCanvasHeight)}).x - origin.x,
         WorldCamera::FromViewport({float(kCanvasWidth), float(kCanvasHeight)}).y - origin.y});
    auto &renderer = GameRenderer::Get().Backend();
    renderer.BeginModels();
    for (const game::Actor *actor : actors)
    {
        renderer.BindObject(&actor->GetRenderObjects());
        actor->Draw();
    }
    renderer.EndModels();
}

// ============================================================================
// InteriorScene
// ============================================================================
void InteriorScene::Draw()
{
    WorldView view;
    auto &scene = *this;
    {
        profiling::Scope placementTimer(profiling::Timer::ScenePlacement);
        scene.BeginPlacement();
        for (int x = -3; x < 3; ++x)
        {
            for (int y = -3; y < 3; ++y)
            {
                scene.PlaceWorldActor(
                    "tile/" + std::to_string(x) + "/" + std::to_string(y), float(x), float(y),
                    WorldBounds(float(x), float(y), 1, 1, 0), 0, 0, [this, x, y]() {
                        Drawing::DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal,
                                                  ColorRGBA(.23f, .3f, .34f));
                    });
            }
        }
        scene.PlaceWorldActor("wall/back", -3, -3, WorldBounds(-3, -3, 6, .12f, 95), 1, 0, [this]() {
            Drawing::DrawBox(-3, -3, 6, .12f, 95, ColorRGBA(.13f, .21f, .28f));
        });
        scene.PlaceWorldActor("wall/side", -3, -3, WorldBounds(-3, -3, .12f, 6, 95), 1, 1, [this]() {
            Drawing::DrawBox(-3, -3, .12f, 6, 95, ColorRGBA(.1f, .18f, .24f));
        });
        scene.PlaceWorldActor("window", -1, -2.85f, WorldBounds(-1, -2.85f, 0, 0, 80, 150), 1, 2, [this]() {
            Vector2 window = Drawing::WorldToScreen(-1, -2.85f, 68);
            Drawing::DrawLine(window, {window.x + 110, window.y + 55}, ColorRGBA(.25f, .72f, .82f), 18);
        });
        scene.PlaceWorldActor("bed", -2.6f, -2.6f, WorldBounds(-2.6f, -2.6f, 1.8f, 1.1f, 22), 1, 3, [this]() {
            Drawing::DrawBox(-2.6f, -2.6f, 1.05f, .8f, 12, ColorRGBA(.32f, .39f, .46f));
            Drawing::DrawBox(-2.5f, -2.5f, .3f, .7f, 16, ColorRGBA(.66f, .68f, .62f));
        });
        scene.PlaceWorldActor("terminal", 1.3f, -2.6f, WorldBounds(1, -2.6f, 1.6f, 1.4f, 65), 1, 4, [this]() {
            Drawing::DrawModel3D(models::ModelLibrary::Get().console, Drawing::WorldToScreen(1.6f, -2.4f),
                                 {.64f, .64f, .64f}, {}, (1.6f - 2.4f) * 36, {});
            Drawing::DrawGlow(Drawing::WorldToScreen(1, -1.2f), 20, ColorRGBA(.3f, .95f, .9f));
        });
        scene.PlaceWorldActor("table", -2.4f, 1.1f, WorldBounds(-2.4f, 1.1f, 1.2f, .6f, 55), 1, 5, [this]() {
            Drawing::DrawBox(-2.4f, 1.1f, 1.2f, .6f, 20, ColorRGBA(.35f, .28f, .25f));
            Drawing::DrawGlow(Drawing::WorldToScreen(-2, 1.4f, 35), 18, ColorRGBA(1, .63f, .28f));
        });
        scene.PlaceWorldActor("door", 1.9f, 1.9f, WorldBounds(1.9f, 1.9f, 1, 1, 0), 1, 6,
                              [this]() { Drawing::DrawTile(1.9f, 1.9f, 1, 1, ColorRGBA(.15f, .45f, .47f)); });
        scene.PlaceWorldActor(
            "player", Game::Get().GetState().playerPosition.x, Game::Get().GetState().playerPosition.y,
            WorldBounds(Game::Get().GetState().playerPosition.x, Game::Get().GetState().playerPosition.y, 0,
                        0, 90),
            2, 0, [this]() {
                Drawing::DrawPerson(Game::Get().GetState().playerPosition, ColorRGBA(.67f, .8f, .82f), true);
            });
        scene.PlaceWorldActor("terminal-label", 1, -1.2f, WorldBounds(1, -1.2f, 0, 0, 80, 80), 3, 0,
                              [this]() {
                                  Vector2 label = Drawing::WorldToScreen(1, -1.2f, 65);
                                  Drawing::DrawLabel(label.x - 30, label.y, "단말");
                              });
        scene.PlaceWorldActor("door-label", 2.4f, 2.4f, WorldBounds(2.4f, 2.4f, 0, 0, 0, 80), 3, 0, [this]() {
            Vector2 label = Drawing::WorldToScreen(2.4f, 2.4f);
            Drawing::DrawLabel(label.x - 20, label.y + 25, "출구");
        });
        scene.GetGraph().Reparent("terminal-label", *scene.GetGraph().Find("terminal"));
        scene.GetGraph().Reparent("door-label", *scene.GetGraph().Find("door"));
    }
    scene.DrawActors();
}

// ============================================================================
// CityScene
// ============================================================================
Vector2 CityScene::GetTrafficPosition(int index)
{
    return CityLayout::TrafficPosition(index, Game::Get().GetState().animationTimeSeconds);
}

models::Palette CityScene::GetTrafficPalette(int index)
{
    constexpr std::array<float, 4> bodyColors[] = {
        {.72f, .38f, .26f, 1}, {.28f, .49f, .65f, 1}, {.42f, .63f, .45f, 1}, {.58f, .43f, .65f, 1}};
    models::Palette palette;
    palette.body = bodyColors[(index / 4 + index) % 4];
    palette.accent = {.5f, .65f, .72f, 1};
    palette.emission =
        index % 2 == 0 ? std::array<float, 4>{.2f, .8f, 1, 1} : std::array<float, 4>{1, .55f, .22f, 1};
    return palette;
}

void CityScene::DrawStreetTiles(game::Scene &scene)
{
    constexpr int chunkSize = 8;
    for (int x0 = CityLayout::kTerrainMinX; x0 < CityLayout::kTerrainMaxX; x0 += chunkSize)
    {
        for (int y0 = CityLayout::kTerrainMinY; y0 < CityLayout::kTerrainMaxY; y0 += chunkSize)
        {
            std::string key = "terrain/city/" + std::to_string(x0) + "/" + std::to_string(y0);
            scene.PlaceWorldActor(
                key, float(x0), float(y0), WorldBounds(float(x0), float(y0), chunkSize, chunkSize, 0), 0, 0,
                [this, x0, y0, key, chunkSize]() {
                    rendercache::DrawWorld(key, [this, x0, y0, chunkSize]() {
                        for (int x = x0; x < x0 + chunkSize && x < CityLayout::kTerrainMaxX; ++x)
                        {
                            for (int y = y0; y < y0 + chunkSize && y < CityLayout::kTerrainMaxY; ++y)
                            {
                                bool road = CityLayout::IsRoadTile(x, y);
                                float shade = 0;
                                Drawing::DrawMaterialTile(
                                    float(x), float(y),
                                    road ? SurfaceMaterial::Asphalt : SurfaceMaterial::Concrete,
                                    road ? ColorRGBA(.13f, .19f, .23f)
                                         : ColorRGBA(.28f + shade, .34f + shade, .37f + shade));
                                if (road && x % 2 == 0 && y == 0)
                                {
                                    Drawing::DrawTile(x + .1f, y + .1f, .6f, .035f,
                                                      ColorRGBA(.37f, .59f, .63f));
                                }
                            }
                        }
                    });
                });
        }
    }
}

void CityScene::DrawBoundary(game::Scene &scene)
{
    auto side = [&](const std::string &name, float fixed, float first, float last, bool vertical,
                    std::array<std::pair<float, float>, 2> openings) {
        int panel = 0;
        auto place = [&](float start, float length, bool gate) {
            Vector2 p = vertical ? Vector2{fixed, start} : Vector2{start, fixed};
            std::string id = "boundary/" + name + "/" + (gate ? "gate/" : "wall/") + std::to_string(panel++);
            scene.PlaceWorldActor(id, p.x, p.y,
                                  WorldBounds(p.x - .2f, p.y - .2f, vertical ? .4f : length + .4f,
                                              vertical ? length + .4f : .4f, gate ? 94.f : 75.f),
                                  3, p.x + p.y, [=]() {
                                      models::Transform3D transform;
                                      transform.yaw = vertical ? kPi / 2 : 0;
                                      models::Palette palette;
                                      palette.body = {.37f, .48f, .55f, 1};
                                      palette.accent = {.2f, .32f, .4f, 1};
                                      // Fade only the panel hiding the nearby player, while retaining the
                                      // boundary silhouette.
                                      auto player = Game::Get().GetState().playerPosition;
                                      Vector2 middle = vertical ? Vector2{fixed, start + length * .5f}
                                                                : Vector2{start + length * .5f, fixed};
                                      if (!gate && Game::Distance(player, middle) < 1.4f)
                                      {
                                          palette.body[3] = palette.accent[3] = palette.emission[3] = .35f;
                                      }
                                      Drawing::DrawModel3D(gate ? models::ModelLibrary::Get().trafficGate
                                                                : models::ModelLibrary::Get().bulkhead,
                                                           Drawing::WorldToScreen(p.x, p.y),
                                                           {length, 1, gate ? 2.f : 1.6f}, transform,
                                                           (p.x + p.y) * 36, palette);
                                  });
        };
        float cursor = first;
        for (auto opening : openings)
        {
            while (cursor < opening.first - .001f)
            {
                float length = (std::min)(1.f, opening.first - cursor);
                place(cursor, length, false);
                cursor += length;
            }
            place(opening.first, opening.second - opening.first, true);
            cursor = opening.second;
        }
        while (cursor < last - .001f)
        {
            float length = (std::min)(1.f, last - cursor);
            place(cursor, length, false);
            cursor += length;
        }
    };
    for (auto edge : {std::pair<const char *, float>{"west", kWorldMinX}, {"east", kWorldMaxX}})
    {
        side(edge.first, edge.second, kWorldMinY, kWorldMaxY, true, {{{-1, 1}, {7, 8}}});
    }
    for (auto edge : {std::pair<const char *, float>{"north", kWorldMinY}, {"south", kWorldMaxY}})
    {
        side(edge.first, edge.second, kWorldMinX, kWorldMaxX, false, {{{-10, -9}, {1, 3}}});
    }
}

void CityScene::DrawStreetFurniture(game::Scene &scene)
{
    scene.PlaceWorldActor("beacon-platform", 5, -3.8f, WorldBounds(5, -3.8f, 2.6f, 1.7f, 0), 2, 0,
                          [this]() { Drawing::DrawTile(5, -3.8f, 2.6f, 1.7f, ColorRGBA(.2f, .28f, .34f)); });
    for (int i = -6; i <= 6; i += 3)
    {
        scene.PlaceWorldActor(
            "lamp/" + std::to_string(i), float(i), 3.7f, WorldBounds(float(i), 3.7f, .55f, .6f, 80), 2, 0,
            [this, i]() {
                Drawing::DrawBox((float)i, 3.7f, .55f, .6f, 12, ColorRGBA(.2f, .28f, .3f));
                Vector2 p = Drawing::WorldToScreen((float)i, 3.7f, 52);
                Drawing::DrawLine(Drawing::WorldToScreen((float)i, 3.7f), p, ColorRGBA(.25f, .4f, .47f), 2);
                Drawing::DrawGlow(
                    p, 18,
                    ColorRGBA(.35f, .85f, .85f,
                              !GameLevels::Chapter().IsAvailable() ||
                                      GameLevels::Chapter().GetState().generatorRestored
                                  ? 1.f
                                  : .45f + .2f * std::sin(Game::Get().GetState().animationTimeSeconds * 3)));
            });
    }
}

void CityScene::DrawDepthSortedObjects(game::Scene &scene)
{
    enum class SceneObjectKind
    {
        Building,
        Player,
        Lia,
        Mara,
        Resident,
        Vehicle,
        JetBike
    };
    struct SceneObject
    {
        float depth;
        SceneObjectKind kind;
        int index;
    };
    auto placeObject = [this, &scene](SceneObject item) {
        Vector2 position = Game::Get().GetState().playerPosition;
        game::Bounds bounds;
        if (item.kind == SceneObjectKind::Building)
        {
            const auto &building = Game::Get().GetState().buildings[item.index];
            position = {building.x, building.y};
            bounds = WorldBounds(position.x, position.y, building.width, building.depth,
                                 building.height * 1.14f + 40);
        }
        else
        {
            if (item.kind == SceneObjectKind::Vehicle)
            {
                position = GetTrafficPosition(item.index);
            }
            else if (item.kind == SceneObjectKind::JetBike)
            {
                position = kJetBikePosition;
            }
            else if (item.kind == SceneObjectKind::Resident)
            {
                position = Game::Get().GetState().residents[item.index].position;
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
        case SceneObjectKind::JetBike:
            name = "personal-jet-bike";
            break;
        }
        scene.PlaceWorldActor(name, position.x, position.y, bounds, 3, item.depth, [this, item]() {
            if (item.kind == SceneObjectKind::Vehicle)
            {
                Vector2 position = GetTrafficPosition(item.index);
                Drawing::DrawWorldVehicle(
                    position, 3.3f, 1.f, static_cast<models::VehicleType>(item.index % 4),
                    GetTrafficPalette(item.index), .78f, CityLayout::TrafficYaw(item.index));
            }
            if (item.kind == SceneObjectKind::JetBike)
            {
                Drawing::DrawWorldVehicle(kJetBikePosition, 4.4f, 1.f, models::VehicleType::JetBike,
                                          GameLevels::Travel().GetBikePalette(), .08f, 0);
            }
            if (item.kind == SceneObjectKind::Resident)
            {
                const auto &resident = Game::Get().GetState().residents[item.index];
                Drawing::DrawPerson(resident.position,
                                    ColorRGBA(resident.coat[0], resident.coat[1], resident.coat[2]), false,
                                    resident.type, resident.facing.walking, resident.facing.yaw,
                                    resident.gender, resident.hair);
                if (Game::Distance(Game::Get().GetState().playerPosition, resident.position) < 2.2f)
                {
                    Vector2 label = Drawing::WorldToScreen(resident.position.x, resident.position.y, 62);
                    Drawing::DrawLabel(label.x - 20, label.y, resident.name);
                }
            }
            if (item.kind == SceneObjectKind::Building)
            {
                Drawing::DrawBuilding(Game::Get().GetState().buildings[item.index]);
            }
            if (item.kind == SceneObjectKind::Player)
            {
                Drawing::DrawPerson(Game::Get().GetState().playerPosition, ColorRGBA(.68f, .8f, .83f), true);
            }
            if (item.kind == SceneObjectKind::Lia)
            {
                Drawing::DrawPerson(kLiaPosition, ColorRGBA(.89f, .52f, .28f), false,
                                    models::CharacterType::Researcher, false, .78539816f,
                                    models::Gender::Female, models::HairStyle::Ponytail);
            }
            if (item.kind == SceneObjectKind::Mara)
            {
                Drawing::DrawPerson(kMaraPosition, ColorRGBA(.48f, .4f, .64f), false,
                                    models::CharacterType::Medic, false, .78539816f, models::Gender::Female,
                                    models::HairStyle::Bun);
            }
        });
    };
    for (size_t i = 0; i < Game::Get().GetState().buildings.size(); i++)
    {
        placeObject({Game::Get().GetState().buildings[i].x + Game::Get().GetState().buildings[i].width +
                         Game::Get().GetState().buildings[i].y + Game::Get().GetState().buildings[i].depth,
                     SceneObjectKind::Building, (int)i});
    }
    placeObject({Game::Get().GetState().playerPosition.x + Game::Get().GetState().playerPosition.y,
                 SceneObjectKind::Player, 0});
    placeObject({kLiaPosition.x + kLiaPosition.y, SceneObjectKind::Lia, 0});
    placeObject({kMaraPosition.x + kMaraPosition.y, SceneObjectKind::Mara, 0});
    for (int i = 0; i < 6; ++i)
    {
        Vector2 position = GetTrafficPosition(i);
        placeObject({position.x + position.y, SceneObjectKind::Vehicle, i});
    }
    placeObject({kJetBikePosition.x + kJetBikePosition.y, SceneObjectKind::JetBike, 0});
    for (size_t i = 0; i < Game::Get().GetState().residents.size(); ++i)
    {
        placeObject(
            {Game::Get().GetState().residents[i].position.x + Game::Get().GetState().residents[i].position.y,
             SceneObjectKind::Resident, int(i)});
    }
}

void CityScene::DrawUtilityDevices(game::Scene &scene)
{
    scene.PlaceWorldActor(
        "city-receiver", kCityReceiverPosition.x, kCityReceiverPosition.y,
        WorldBounds(kCityReceiverPosition.x, kCityReceiverPosition.y, .5f, .5f, 60), 4, 0, [this]() {
            Drawing::DrawBox(kCityReceiverPosition.x, kCityReceiverPosition.y, .5f, .5f, 36,
                             ColorRGBA(.2f, .36f, .4f));
            Drawing::DrawGlow(Drawing::WorldToScreen(kCityReceiverPosition.x, kCityReceiverPosition.y, 40),
                              16, ColorRGBA(.35f, .9f, 1));
        });
    scene.PlaceWorldActor(
        "relay", kRelayPosition.x, kRelayPosition.y,
        WorldBounds(kRelayPosition.x, kRelayPosition.y, .35f, .35f, 50), 4, 0, [this]() {
            Drawing::DrawBox(kRelayPosition.x, kRelayPosition.y, .35f, .35f, 25, ColorRGBA(.25f, .35f, .4f));
            Drawing::DrawGlow(Drawing::WorldToScreen(kRelayPosition.x, kRelayPosition.y, 29), 14,
                              Game::Get().GetState().isPowerRestored ? ColorRGBA(.3f, 1, .65f)
                                                                     : ColorRGBA(1, .65f, .25f));
        });
    scene.PlaceWorldActor(
        "beacon", kBeaconPosition.x, kBeaconPosition.y,
        WorldBounds(kBeaconPosition.x, kBeaconPosition.y, .4f, .4f, 60), 4, 0, [this]() {
            Drawing::DrawBox(kBeaconPosition.x, kBeaconPosition.y, .4f, .4f, 30, ColorRGBA(.3f, .45f, .5f));
            Drawing::DrawGlow(Drawing::WorldToScreen(kBeaconPosition.x, kBeaconPosition.y, 34), 20,
                              ColorRGBA(.5f, .83f, 1));
        });
}

void CityScene::DrawQuestMarkers(game::Scene &scene)
{
    auto marker = [this, &scene](const std::string &name, Vector2 position, std::string label,
                                 ColorRGBA color) {
        scene.PlaceWorldActor(
            "marker/" + name, position.x, position.y, WorldBounds(position.x, position.y, 0, 0, 70, 230), 5,
            0, [this, position, label, color]() { Drawing::DrawInteractionMarker(position, label, color); });
    };
    auto &game = Game::Get();
    if (game.IsQuestInteraction(InteractionTarget::HomeEntrance))
    {
        marker("home", kHomeDoor, "집 단말 확인", {.6f, .78f, .81f});
    }
    if (game.IsQuestInteraction(InteractionTarget::Lia))
    {
        marker("lia", kLiaPosition, "리아 / E 대화", {1, .69f, .38f});
    }
    if (game.IsQuestInteraction(InteractionTarget::Mara))
    {
        marker("mara", kMaraPosition, "마라 / 선택 의뢰", {1, .77f, .4f});
    }
    if (game.IsQuestInteraction(InteractionTarget::PowerRelay))
    {
        marker("relay", kRelayPosition, "전력 복구 [E]", {1, .7f, .3f});
    }
    if (game.IsQuestInteraction(InteractionTarget::GateBeacon))
    {
        marker("beacon", kBeaconPosition, "전망대 승강기 [E]", {.4f, .93f, .96f});
    }
    if (game.IsQuestInteraction(InteractionTarget::CityReceiver))
    {
        marker("city-receiver", kCityReceiverPosition,
               GameLevels::Chapter().IsAvailable()
                   ? (GameLevels::Chapter().CompletedSites() == 3 &&
                              !GameLevels::Chapter().GetState().recordsCompared
                          ? "1장 / 기록 대조 [E]"
                          : "1장 / 현장 출발 [E]")
                   : "도시 수신망 [E]",
               {.4f, .9f, 1});
    }
    if (game.IsQuestInteraction(InteractionTarget::JetBike))
    {
        marker("jet-bike", kJetBikePosition, "제트 바이크 탑승 [E]", {1, .75f, .4f});
    }
}

void CityScene::Draw()
{
    WorldView view;
    auto &scene = *this;
    {
        profiling::Scope placementTimer(profiling::Timer::ScenePlacement);
        scene.BeginPlacement();
        DrawStreetTiles(scene);
        DrawStreetFurniture(scene);
        DrawBoundary(scene);
        DrawDepthSortedObjects(scene);
        DrawUtilityDevices(scene);
        DrawQuestMarkers(scene);
        if (GameLevels::Chapter().GetState().kainJoined)
        {
            auto p = kCityKainPosition;
            scene.PlaceWorldActor("chapter/kain", p.x, p.y, WorldBounds(p.x, p.y, 0, 0, 85, 100), 1,
                                  p.x + p.y, [p]() {
                                      Drawing::DrawPerson(p, {.38f, .53f, .66f}, false,
                                                          models::CharacterType::Guard, false, .785f);
                                      if (Game::Distance(p, Game::Get().GetState().playerPosition) < 3)
                                      {
                                          auto label = Drawing::WorldToScreen(p.x, p.y, 60);
                                          Drawing::DrawLabel(label.x - 16, label.y, "카인");
                                      }
                                  });
        }
        auto &graph = scene.GetGraph();
        for (size_t i = 0; i < Game::Get().GetState().buildings.size(); ++i)
        {
            graph.Reparent("shadow/building/" + std::to_string(i),
                           *graph.Find("building/" + std::to_string(i)));
        }
        for (int i = 0; i < 6; ++i)
        {
            graph.Reparent("shadow/vehicle/" + std::to_string(i),
                           *graph.Find("vehicle/" + std::to_string(i)));
        }
        graph.Reparent("marker/lia", *graph.Find("lia"));
        graph.Reparent("marker/mara", *graph.Find("mara"));
        graph.Reparent("marker/relay", *graph.Find("relay"));
        graph.Reparent("marker/beacon", *graph.Find("beacon"));
        graph.Reparent("shadow/personal-jet-bike", *graph.Find("personal-jet-bike"));
        graph.Reparent("marker/jet-bike", *graph.Find("personal-jet-bike"));
    }
    scene.DrawActors();
}

// ============================================================================
// ObservatoryScene
// ============================================================================
void ObservatoryScene::Draw()
{
    WorldView view;
    auto &scene = *this;
    {
        profiling::Scope timer(profiling::Timer::ScenePlacement);
        scene.BeginPlacement();
        scene.PlaceWorldActor("deck", -4, -4, WorldBounds(-4, -4, 8, 8, 0, 60), 0, 0, [this]() {
            rendercache::DrawWorld("terrain/observatory/deck", [this]() {
                Vector2 center = Drawing::WorldToScreen(0, 0);

                for (int x = -4; x < 4; ++x)
                {
                    for (int y = -4; y < 4; ++y)
                    {
                        Drawing::DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal,
                                                  ColorRGBA(.19f, .3f, .34f));
                    }
                }
            });
        });
        scene.PlaceWorldActor("deck-label", 0, 0, WorldBounds(0, 0, 0, 0, 0, 150), 1, 0, [this]() {
            Vector2 center = Drawing::WorldToScreen(0, 0);
            Drawing::DrawLabel(center.x - 74, center.y + 8, "관측 데크 / 180 m", ColorRGBA(.5f, .8f, .82f));
        });
        scene.PlaceWorldActor("panorama-railing", -4, -4, WorldBounds(-4, -4, 8, 8, 78, 70), 1, 0, [this]() {
            for (int i = 0; i < 9; ++i)
            {
                float x = -4.f + i;
                Vector2 first = Drawing::WorldToScreen(x, -4, 62);
                Vector2 second = Drawing::WorldToScreen(x + 1, -4, 62);
                if (i < 8)
                {
                    Drawing::DrawMaterialQuad(first, second, {second.x, second.y + 45},
                                              {first.x, first.y + 45}, SurfaceMaterial::Glass,
                                              ColorRGBA(.21f, .59f, .68f, .38f));
                    Drawing::DrawLine(first, second, ColorRGBA(.42f, .77f, .82f), 3);
                }
                Drawing::DrawLine(Drawing::WorldToScreen(x, -4), first, ColorRGBA(.32f, .48f, .55f), 4);
                Vector2 left = Drawing::WorldToScreen(-4, x, 62);
                Vector2 next = Drawing::WorldToScreen(-4, x + 1, 62);
                if (i < 8)
                {
                    Drawing::DrawLine(left, next, ColorRGBA(.42f, .77f, .82f), 3);
                }
                Drawing::DrawLine(Drawing::WorldToScreen(-4, x), left, ColorRGBA(.32f, .48f, .55f), 4);
            }
        });
        scene.PlaceWorldActor(
            "signal-console", kObservatoryConsole.x, kObservatoryConsole.y,
            WorldBounds(kObservatoryConsole.x, kObservatoryConsole.y, .8f, .65f, 95, 75), 2, 0, [this]() {
                Drawing::DrawModel3D(models::ModelLibrary::Get().console,
                                     Drawing::WorldToScreen(kObservatoryConsole.x, kObservatoryConsole.y),
                                     {.64f, .64f, .64f}, {},
                                     (kObservatoryConsole.x + kObservatoryConsole.y) * 36, {});
            });
        scene.PlaceWorldActor("telescope", 2.6f, -3.2f, WorldBounds(2.6f, -3.2f, 0, 0, 140, 90), 2, -2,
                              [this]() {
                                  models::Transform3D rotation;
                                  rotation.yaw = -kPi / 2;
                                  Drawing::DrawModel3D(models::ModelLibrary::Get().telescope,
                                                       Drawing::WorldToScreen(2.6f, -3.2f), {.8f, .8f, .8f},
                                                       rotation, (2.6f - 3.2f) * 36, {});
                              });
        scene.PlaceWorldActor(
            "stairs", kObservatoryStairs.x, kObservatoryStairs.y,
            WorldBounds(kObservatoryStairs.x, kObservatoryStairs.y, 1, 1, 0, 65), 2, 0, [this]() {
                Drawing::DrawTile(-3.3f, 2.3f, 1.2f, 1.2f, ColorRGBA(.13f, .34f, .4f));
                for (int i = 0; i < 5; ++i)
                {
                    float x = -3.3f + i * .22f;
                    Drawing::DrawLine(Drawing::WorldToScreen(x, 2.3f), Drawing::WorldToScreen(x, 3.4f),
                                      ColorRGBA(.45f, .7f, .76f), 3);
                }
            });
        scene.PlaceWorldActor(
            "lia", kObservatoryLiaPosition.x, kObservatoryLiaPosition.y,
            WorldBounds(kObservatoryLiaPosition.x, kObservatoryLiaPosition.y, 0, 0, 95), 3, -2.5f, [this]() {
                Drawing::DrawPerson(kObservatoryLiaPosition, ColorRGBA(.89f, .52f, .28f), false,
                                    models::CharacterType::Engineer, false, .78539816f,
                                    models::Gender::Female, models::HairStyle::Ponytail);
            });
        scene.PlaceWorldActor(
            "player", Game::Get().GetState().playerPosition.x, Game::Get().GetState().playerPosition.y,
            WorldBounds(Game::Get().GetState().playerPosition.x, Game::Get().GetState().playerPosition.y, 0,
                        0, 95),
            3, Game::Get().GetState().playerPosition.x + Game::Get().GetState().playerPosition.y, [this]() {
                Drawing::DrawPerson(Game::Get().GetState().playerPosition, ColorRGBA(.68f, .8f, .83f), true,
                                    models::CharacterType::Explorer);
            });
        scene.PlaceWorldActor(
            "signal-marker", kObservatoryConsole.x, kObservatoryConsole.y,
            WorldBounds(kObservatoryConsole.x, kObservatoryConsole.y, 0, 0, 100, 190), 4, 0, [this]() {
                if (!Game::Get().IsQuestInteraction(InteractionTarget::ObservatorySignal))
                {
                    return;
                }
                Drawing::DrawInteractionMarker(kObservatoryConsole,
                                               Game::Get().GetState().gateSignalHeard ? "관문 응답 기록"
                                                                                      : "신호 측정 [E]",
                                               ColorRGBA(.4f, .93f, .96f));
            });
        scene.PlaceWorldActor("exit-marker", kObservatoryStairs.x, kObservatoryStairs.y,
                              WorldBounds(kObservatoryStairs.x, kObservatoryStairs.y, 0, 0, 80, 160), 4, 0,
                              [this]() {
                                  if (!Game::Get().IsQuestInteraction(InteractionTarget::ObservatoryExit))
                                  {
                                      return;
                                  }
                                  Drawing::DrawInteractionMarker(kObservatoryStairs, "도시로 내려가기 [E]",
                                                                 ColorRGBA(1, .75f, .4f));
                              });
        scene.GetGraph().Reparent("signal-marker", *scene.GetGraph().Find("signal-console"));
        scene.GetGraph().Reparent("exit-marker", *scene.GetGraph().Find("stairs"));
    }
    scene.DrawActors();
}

// ============================================================================
// MineScene
// ============================================================================
void MineScene::PlacePerson(game::Scene &scene, const std::string &id, Vector2 position, ColorRGBA coat,
                            models::CharacterType type, bool walking, const std::string &label,
                            float facingYaw, models::Gender gender, models::HairStyle hair,
                            const NpcCombatState &combat)
{
    if (GameLevels::Mine().Lighting().At(position) < .025f)
    {
        return;
    }
    scene.PlaceWorldActor(
        id, position.x, position.y, WorldBounds(position.x, position.y, 0, 0, 85, 100), 1,
        position.x + position.y,
        [this, position, coat, type, walking, label, facingYaw, gender, hair, combat, id]() {
            bool downed = combat.health <= 0;
            Drawing::DrawPerson(position, coat, false, type, walking && !downed,
                                combat.engaged ? combat.aimYaw : facingYaw, gender, hair,
                                combat.engaged && !downed &&
                                    (type == models::CharacterType::Guard || id == "ria"),
                                combat.muzzleFlash, downed);
            if (Game::Distance(position, Game::Get().GetState().playerPosition) < 3 && !label.empty())
            {
                auto screen = Drawing::WorldToScreen(position.x, position.y, 66);
                Drawing::DrawLabel(screen.x - 25, screen.y, downed ? "E 응급 처치" : label, coat);
                if (combat.health < 100)
                {
                    Drawing::DrawRectangle(screen.x - 18, screen.y + 8, 36, 3, {.12f, .18f, .2f});
                    Drawing::DrawRectangle(screen.x - 18, screen.y + 8, 36.f * combat.health / 100, 3,
                                           {.25f, .85f, .65f});
                }
            }
        });
}

void MineScene::PlaceClue(game::Scene &scene, const char *id, Vector2 position, const char *label,
                          ColorRGBA color, MineQuestStage expected, bool glyph)
{
    scene.PlaceWorldActor(
        id, position.x, position.y, WorldBounds(position.x, position.y, 1, 1, 80, 180), 1,
        position.x + position.y, [this, position, label, color, glyph, expected]() {
            Drawing::DrawBox(position.x - .25f, position.y - .25f, .5f, .5f, glyph ? 65.f : 25.f,
                             glyph ? ColorRGBA(.25f, .23f, .28f) : ColorRGBA(.25f, .3f, .32f));
            auto center = Drawing::WorldToScreen(position.x, position.y, glyph ? 48.f : 30.f);
            Drawing::DrawGlow(center, 15, color);
            if (glyph)
            {
                Drawing::DrawPolygon({{center.x, center.y - 12},
                                      {center.x + 15, center.y},
                                      {center.x, center.y + 12},
                                      {center.x - 15, center.y}},
                                     color);
                Drawing::DrawEllipse(center.x, center.y, 3, 7, {.04f, .06f, .08f});
            }
            bool relevant = GameLevels::Mine().GetState().quest.GetStage() == expected;
            if (relevant && Game::Distance(position, Game::Get().GetState().playerPosition) < 4 &&
                GameLevels::Mine().Lighting().At(position) > .1f)
            {
                Drawing::DrawLabel(center.x - 55, center.y - 26, label, color);
            }
        });
}

void MineScene::Draw()
{
    WorldView view;
    auto &scene = *this;
    if (cachedRevision != GameLevels::Mine().GetState().mapRevision)
    {
        rendercache::InvalidateTerrain();
        GetGraph().Clear();
        cachedRevision = GameLevels::Mine().GetState().mapRevision;
    }
    scene.BeginPlacement();
    constexpr int chunkSize = 8;
    for (int y0 = 0; y0 < kMapSize; y0 += chunkSize)
    {
        for (int x0 = 0; x0 < kMapSize; x0 += chunkSize)
        {
            std::string key = "terrain/mine/" + std::to_string(x0) + "/" + std::to_string(y0);
            scene.PlaceWorldActor(
                key, float(x0), float(y0), WorldBounds(float(x0), float(y0), chunkSize, chunkSize, 0), 0, -1,
                [this, x0, y0, key, chunkSize]() {
                    rendercache::DrawWorld(key, [this, x0, y0, chunkSize]() {
                        for (int y = y0; y < y0 + chunkSize; ++y)
                        {
                            for (int x = x0; x < x0 + chunkSize; ++x)
                            {
                                if (GameLevels::Mine().GetState().tiles[y * kMapSize + x] != Tile::Wall)
                                {
                                    bool ruins = x >= 42 && y >= 42;
                                    Drawing::DrawMaterialTile(float(x), float(y), SurfaceMaterial::Concrete,
                                                              ruins ? ColorRGBA(.33f, .29f, .37f)
                                                                    : ColorRGBA(.28f, .25f, .22f));
                                    if ((y == 10 && x <= 24) || (x == 24 && y <= 29) ||
                                        (y == 29 && x >= 24 && x <= 48))
                                    {
                                        auto a = Drawing::WorldToScreen(x + .2f, y + .2f);
                                        auto b = Drawing::WorldToScreen(x + .8f, y + .8f);
                                        Drawing::DrawLine(a, b, {.42f, .38f, .28f}, 2);
                                    }
                                }
                            }
                        }
                    });
                });
        }
    }
    for (int y = 1; y < kMapSize - 1; ++y)
    {
        for (int x = 1; x < kMapSize - 1; ++x)
        {
            bool gate = GameLevels::Mine().Map().IsGateCell(x, y);
            if (GameLevels::Mine().GetState().tiles[y * kMapSize + x] != Tile::Wall && !gate)
            {
                continue;
            }
            if (gate && GameLevels::Mine().GetState().quest.IsPowerBypassed())
            {
                continue;
            }
            bool adjacent = false;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    adjacent =
                        adjacent ||
                        GameLevels::Mine().GetState().tiles[(y + dy) * kMapSize + x + dx] != Tile::Wall;
                }
            }
            if (adjacent)
            {
                scene.PlaceWorldActor(
                    "rock/" + std::to_string(y * kMapSize + x), float(x), float(y),
                    WorldBounds(float(x), float(y), 1, 1, 38), 1, float(x + y + 2), [this, x, y, gate]() {
                        if (gate)
                        {
                            Drawing::DrawBox(float(x), float(y), 1, 1, 48, {.38f, .22f, .16f});
                        }
                        else
                        {
                            // Stable per-cell variation, still sharing one instanced rock mesh.
                            unsigned int seed = unsigned(x * 73 + y * 151);
                            models::Transform3D rotation;
                            rotation.yaw = float(seed % 8) * .78539816f;
                            rotation.position = {
                                .5f - .5f * (std::cos(rotation.yaw) - std::sin(rotation.yaw)),
                                .5f - .5f * (std::sin(rotation.yaw) + std::cos(rotation.yaw)), 0};
                            Drawing::DrawModel3D(
                                models::ModelLibrary::Get().rock, Drawing::WorldToScreen(float(x), float(y)),
                                {1, 1, .86f + float(seed % 7) * .045f}, rotation, float(x + y) * 36,
                                {{.31f, .28f, .27f, 1}, {.3f, .4f, .42f, 1}, {1, 1, 1, 1}});
                        }
                    });
            }
        }
    }
    for (size_t i = 0; i < GameLevels::Mine().GetState().lights.size(); ++i)
    {
        const auto lamp = GameLevels::Mine().GetState().lights[i];
        if (lamp.needsPower && !GameLevels::Mine().GetState().quest.IsPowerBypassed())
        {
            continue;
        }
        scene.PlaceWorldActor("lamp/" + std::to_string(i), lamp.position.x, lamp.position.y,
                              WorldBounds(lamp.position.x, lamp.position.y, 0, 0, 45, 100), 1,
                              lamp.position.x + lamp.position.y, [this, lamp]() {
                                  auto center = Drawing::WorldToScreen(lamp.position.x, lamp.position.y, 16);
                                  Drawing::DrawRectangle(center.x - 4, center.y - 8, 8, 12, {.9f, .7f, .34f});
                                  Drawing::DrawGlow(center, 32, {.95f, .63f, .23f});
                              });
    }
    PlaceClue(scene, "power-bypass", kPowerBypass, "E 비상 전력 우회", {1, .7f, .3f},
              MineQuestStage::BypassPower);
    PlaceClue(scene, "ruin-signal", kRuinSignal, "E 구조 신호 조사", {.3f, .8f, 1},
              MineQuestStage::InvestigateSignal);
    PlaceClue(scene, "observer-glyph", kObserverGlyph, "E 관측자 문양", {.7f, .55f, 1},
              MineQuestStage::InvestigateGlyph, true);
    PlaceClue(scene, "relay-coordinates", kCoordinateTerminal, "E 중계 좌표 확보", {.4f, 1, .8f},
              MineQuestStage::CollectCoordinates);
    PlaceClue(scene, "mine-exit", GameLevels::Mine().GetState().entry, "E 헤일로 귀환", {.4f, .9f, .8f},
              MineQuestStage::ReturnToHalo);
    for (size_t i = 0; i < GameLevels::Mine().GetState().enemies.size(); ++i)
    {
        auto enemy = GameLevels::Mine().GetState().enemies[i];
        if (enemy.health <= 0 || GameLevels::Mine().Lighting().At(enemy.position) < .025f)
        {
            continue;
        }
        scene.PlaceWorldActor(
            "enemy/" + std::to_string(i), enemy.position.x, enemy.position.y,
            WorldBounds(enemy.position.x, enemy.position.y, 0, 0, 65), 1, enemy.position.x + enemy.position.y,
            [this, enemy]() {
                auto screen = Drawing::WorldToScreen(enemy.position.x, enemy.position.y);
                Drawing::DrawSoftShadow(screen, 22, 8, .6f);
                models::Transform3D rotation;
                rotation.yaw = enemy.facing.yaw;
                Drawing::DrawModel3D(models::ModelLibrary::Get().drone, screen, {1, 1, 1}, rotation,
                                     (enemy.position.x + enemy.position.y) * 36,
                                     {{.35f, .27f, .2f, 1}, {.75f, .35f, .2f, 1}, {1, .2f, .08f, 1}});
                if (Game::Distance(enemy.position, Game::Get().GetState().playerPosition) < 5)
                {
                    Drawing::DrawRectangle(screen.x - 18, screen.y - 39, 36, 3, {.15f, .05f, .04f});
                    Drawing::DrawRectangle(screen.x - 18, screen.y - 39,
                                           36.f * enemy.health / kEnemyMaxHealth, 3, {.9f, .32f, .2f});
                }
            });
    }
    for (const auto &drop : GameLevels::Mine().GetState().drops)
    {
        if (GameLevels::Mine().Lighting().At(drop.position) < .025f)
        {
            continue;
        }
        scene.PlaceWorldActor("drop/" + std::to_string(drop.actorId), drop.position.x, drop.position.y,
                              WorldBounds(drop.position.x, drop.position.y, 0, 0, 30), 1,
                              drop.position.x + drop.position.y, [this, drop]() {
                                  auto center = Drawing::WorldToScreen(drop.position.x, drop.position.y);
                                  Drawing::DrawModel3D(drop.kind == ItemKind::RecoveryKit
                                                           ? models::ModelLibrary::Get().recoveryKit
                                                           : models::ModelLibrary::Get().salvage,
                                                       center, {1, 1, 1}, {},
                                                       (drop.position.x + drop.position.y) * 36, {});
                              });
    }
    for (const auto &npc : GameLevels::Mine().GetState().survivors)
    {
        bool guard = GameLevels::Mine().GetState().rescueGroups[npc.group].combatants;
        PlacePerson(scene, "survivor/" + std::to_string(npc.id), npc.position,
                    guard ? ColorRGBA(.38f, .62f, .8f) : ColorRGBA(.9f, .68f, .28f),
                    guard ? models::CharacterType::Guard : models::CharacterType::Engineer,
                    npc.facing.walking,
                    npc.evacuating ? "헤일로로 철수 중"
                    : guard        ? "고립 전투원 / E 구조"
                                   : "광부 / E 구조",
                    npc.facing.yaw, npc.gender, npc.hair, npc.combat);
    }
    PlacePerson(scene, "ria", GameLevels::Mine().GetState().riaPosition, {.95f, .55f, .25f},
                models::CharacterType::Engineer, GameLevels::Mine().GetState().riaFacing.walking, "리아",
                GameLevels::Mine().GetState().riaFacing.yaw, models::Gender::Female,
                models::HairStyle::Ponytail, GameLevels::Mine().GetState().riaCombat);
    auto player = Game::Get().GetState().playerPosition;
    scene.PlaceWorldActor("player", player.x, player.y, WorldBounds(player.x, player.y, 0, 0, 85), 1,
                          player.x + player.y,
                          [this, player]() { Drawing::DrawPerson(player, {.7f, .84f, .9f}, true); });
    scene.DrawActors();
    // Projectile segments stop at the collision point and are masked with the rest of the world.
    for (const auto &tracer : GameLevels::Mine().GetState().tracers)
    {
        auto start =
            Drawing::WorldToScreen(tracer.start.x, tracer.start.y, models::WorldScale::aimHeightPixels);
        auto end = Drawing::WorldToScreen(tracer.end.x, tracer.end.y, models::WorldScale::aimHeightPixels);
        float fade = tracer.remaining / .12f;
        Drawing::DrawLine(
            start, end,
            tracer.npcOwned ? ColorRGBA(.2f, .8f, 1, fade * .45f) : ColorRGBA(1, .65f, .2f, fade * .45f), 7);
        Drawing::DrawLine(start, end, {1, .96f, .7f, fade}, 2);
        Drawing::DrawGlow(end, 10, {1, .68f, .3f, fade});
    }
}

// ============================================================================
// FrontierScene : Scene
// ============================================================================
void FrontierScene::Draw()
{
    WorldView view;
    auto &level = GameLevels::Chapter();
    const auto &state = level.GetState();
    BeginPlacement();
    for (int y0 = -16; y0 < kFrontierHeight + 16; y0 += 8)
    {
        for (int x0 = -16; x0 < kFrontierWidth + 16; x0 += 8)
        {
            std::string key = "terrain/frontier/" + std::to_string(x0) + "/" + std::to_string(y0);
            PlaceWorldActor(key, float(x0), float(y0), WorldBounds(float(x0), float(y0), 8, 8, 0), 0, 0,
                            [x0, y0, key, &level]() {
                                rendercache::DrawWorld(key, [x0, y0, &level]() {
                                    const auto &s = level.GetState();
                                    for (int y = y0; y < y0 + 8; ++y)
                                    {
                                        for (int x = x0; x < x0 + 8; ++x)
                                        {
                                            auto tile = x < 24 ? FrontierTile::Grass : FrontierTile::Sand;
                                            if (x >= 0 && y >= 0 && x < kFrontierWidth && y < kFrontierHeight)
                                            {
                                                tile = s.tiles[y * kFrontierWidth + x];
                                            }
                                            ColorRGBA color =
                                                tile == FrontierTile::Grass   ? ColorRGBA(.24f, .37f, .26f)
                                                : tile == FrontierTile::Sand  ? ColorRGBA(.55f, .44f, .28f)
                                                : tile == FrontierTile::Canal ? ColorRGBA(.12f, .15f, .14f)
                                                : tile == FrontierTile::Road  ? ColorRGBA(.28f, .30f, .30f)
                                                                              : ColorRGBA(.31f, .34f, .35f);
                                            Drawing::DrawMaterialTile(float(x), float(y),
                                                                      tile == FrontierTile::Road
                                                                          ? SurfaceMaterial::Asphalt
                                                                          : SurfaceMaterial::Concrete,
                                                                      color);
                                        }
                                    }
                                });
                            });
        }
    }
    auto prop = [this](const std::string &id, Vector2 p, float height, std::function<void()> draw) {
        PlaceWorldActor(id, p.x, p.y, WorldBounds(p.x, p.y, 3, 3, height, 70), 1, p.x + p.y, std::move(draw));
    };
    for (size_t i = 0; i < state.structures.size(); ++i)
    {
        auto b = state.structures[i];
        PlaceWorldActor("facility/" + std::to_string(i), b.x, b.y,
                        WorldBounds(b.x, b.y, b.width, b.depth, b.height * 1.14f + 40), 1, b.x + b.y,
                        [b]() { Drawing::DrawBuilding(b); });
    }
    for (int i = 0; i < 75; ++i)
    {
        Vector2 p = {2.5f + float((i * 17) % 43), 3.5f + float((i * 13) % 37)};
        if (level.IsBlocked(p) || std::fabs(p.x - 5.5f) < 1.5f || std::fabs(p.x - 24.5f) < 1.5f ||
            std::fabs(p.y - 20.5f) < 1.5f)
        {
            continue;
        }
        prop("flora/" + std::to_string(i), p, 28, [p, i]() {
            if (p.x < 24)
            {
                Drawing::DrawBox(p.x, p.y, .06f, .06f, 15 + float(i % 5), {.2f, .46f, .34f});
                models::Transform3D leaf;
                leaf.yaw =
                    float(i % 7) * .8f + .07f * std::sin(Game::Get().GetState().animationTimeSeconds + p.y);
                leaf.position.z = .26f;
                Drawing::DrawModel3D(models::ModelLibrary::Get().box, Drawing::WorldToScreen(p.x - .12f, p.y),
                                     {.28f, .06f, .04f}, leaf, (p.x + p.y) * 36, {{.3f, .67f, .45f, 1}});
                Drawing::DrawGlow(Drawing::WorldToScreen(p.x, p.y, 17), 7, {.35f, .85f, .64f, .5f});
            }
            else
            {
                Drawing::DrawModel3D(models::ModelLibrary::Get().rock, Drawing::WorldToScreen(p.x, p.y),
                                     {.3f, .3f, .28f}, {}, (p.x + p.y) * 36, {{.65f, .47f, .28f, 1}});
            }
        });
    }
    for (int edge = 0; edge < 4; ++edge)
    {
        int length = edge < 2 ? kFrontierWidth : kFrontierHeight;
        for (int n = 1; n < length - 1; n += 2)
        {
            if (edge == 0 && n >= 3 && n <= 7)
            {
                continue;
            }
            Vector2 p = edge < 2 ? Vector2{float(n), edge == 0 ? 1.f : float(kFrontierHeight - 1)}
                                 : Vector2{edge == 2 ? 1.f : float(kFrontierWidth - 1), float(n)};
            prop("boundary/" + std::to_string(edge) + "/" + std::to_string(n), p, 45, [p, edge]() {
                Drawing::DrawBox(p.x, p.y, edge < 2 ? 2.f : .15f, edge < 2 ? .15f : 2.f, 35,
                                 {.23f, .29f, .29f});
                Drawing::DrawBox(p.x, p.y, .18f, .18f, 43, {.55f, .55f, .38f});
            });
        }
    }
    prop("return-gate", {4, 1}, 115, []() {
        Drawing::DrawModel3D(models::ModelLibrary::Get().trafficGate, Drawing::WorldToScreen(4, 1),
                             {1.5f, 1, 1}, {}, 5 * 36, {});
    });
    prop("generator", {10, 9.5f}, 55, [&state]() {
        Drawing::DrawBox(9.2f, 9.4f, 3, .65f, 24, {.20f, .3f, .34f});
        for (int i = 0; i < 7; ++i)
        {
            Drawing::DrawBox(9.35f + i * .38f, 9.35f, .12f, .75f, 38, {.38f, .48f, .5f});
            Drawing::DrawGlow(Drawing::WorldToScreen(9.4f + i * .38f, 9.7f, 28), 7,
                              state.generatorRestored ? ColorRGBA(.35f, .94f, .8f)
                                                      : ColorRGBA(.9f, .52f, .2f));
        }
        Drawing::DrawModel3D(models::ModelLibrary::Get().console, Drawing::WorldToScreen(10.5f, 10.5f),
                             {.7f, .7f, .7f}, {}, 21 * 36, {});
        Drawing::DrawBox(12.2f, 10.3f, .4f, .4f, 35, {.28f, .38f, .4f});
        // Even after isolation, the one remaining gauge repeats the recorded pulse.
        float pulse = .5f + .5f * std::sin(Game::Get().GetState().animationTimeSeconds * 2.4f);
        Drawing::DrawGlow(Drawing::WorldToScreen(12.4f, 10.5f, 31), 8, {1, .67f, .28f, .35f + pulse * .65f});
    });
    prop("workbench", {9.5f, 13.5f}, 36, [&state]() {
        Drawing::DrawBox(9.1f, 13.2f, .9f, .6f, 24, {.34f, .4f, .41f});
        Drawing::DrawBox(9.5f, 13.5f, .14f, .14f, 32, {.68f, .59f, .36f});
        if (state.portableLightRepaired)
        {
            Drawing::DrawGlow(Drawing::WorldToScreen(9.55f, 13.55f, 35), 18, {1, .82f, .5f});
        }
    });
    for (int i = 0; i < 2; ++i)
    {
        Vector2 p = {32.5f + i * 3.5f, 9.5f};
        prop("convoy/vehicle/" + std::to_string(i), p, 65, [p, i, &state]() {
            models::Palette palette;
            palette.body = state.vehicleRepaired ? std::array<float, 4>{.42f, .65f, .65f, 1}
                                                 : std::array<float, 4>{.57f, .43f, .27f, 1};
            Drawing::DrawWorldVehicle(p, 4, 1, i ? models::VehicleType::Shuttle : models::VehicleType::Hauler,
                                      palette, 0, 0);
        });
    }
    for (int i = 0; i < 3; ++i)
    {
        constexpr Vector2 markers[] = {{36.5f, 13.5f}, {36.5f, 18.5f}, {29.5f, 18.5f}};
        auto p = markers[i];
        prop("survey/" + std::to_string(i), p, 38, [p, i, &state]() {
            Drawing::DrawBox(p.x - .07f, p.y - .07f, .14f, .14f, 32, {.65f, .6f, .4f});
            Drawing::DrawGlow(Drawing::WorldToScreen(p.x, p.y, 34), 12,
                              state.surveyMarkers > i ? ColorRGBA(.3f, 1, .8f)
                                                      : ColorRGBA(1, .6f, .25f, .5f));
        });
    }
    for (int i = 0; i < 3; ++i)
    {
        Vector2 p = i == 0 ? Vector2{24.5f, 30.5f} : i == 1 ? Vector2{33.5f, 32.5f} : Vector2{35.5f, 33.5f};
        prop("relay/device/" + std::to_string(i), p, 45, [p, i]() {
            Drawing::DrawModel3D(i == 1 ? models::ModelLibrary::Get().console
                                        : models::ModelLibrary::Get().salvage,
                                 Drawing::WorldToScreen(p.x, p.y), {1, 1, 1}, {}, (p.x + p.y) * 36, {});
        });
    }
    prop("camp", {24, 20}, 50, [&state]() {
        Drawing::DrawBox(23.2f, 21.7f, 2.8f, .5f, 15, {.33f, .36f, .35f});
        if (state.portableLightRepaired)
        {
            Drawing::DrawBox(24.5f, 21.6f, .12f, .12f, 34, {.7f, .64f, .45f});
            Drawing::DrawGlow(Drawing::WorldToScreen(24.5f, 21.6f, 37), 26, {1, .82f, .5f});
        }
    });
    auto person = [this](std::string id, const FrontierPerson &npc, models::CharacterType role,
                         models::Gender gender, models::HairStyle hair, ColorRGBA color, std::string label,
                         bool armed) {
        auto p = npc.position;
        PlaceWorldActor(
            id, p.x, p.y, WorldBounds(p.x, p.y, 0, 0, 85, 110), 1, p.x + p.y,
            [npc, role, gender, hair, color, label, armed]() {
                Drawing::DrawPerson(npc.position, color, false, role, npc.facing.walking,
                                    npc.combat.engaged ? npc.combat.aimYaw : npc.facing.yaw, gender, hair,
                                    armed && npc.combat.engaged && npc.combat.health > 0,
                                    npc.combat.muzzleFlash, npc.combat.health <= 0);
                if (Game::Distance(npc.position, Game::Get().GetState().playerPosition) < 4 && !label.empty())
                {
                    auto screen = Drawing::WorldToScreen(npc.position.x, npc.position.y, 60);
                    Drawing::DrawLabel(screen.x - 20, screen.y, label, color);
                    if (label == "폐설비 점거자")
                    {
                        Drawing::DrawRectangle(screen.x - 15, screen.y + 7, 30, 3, {.2f, .08f, .05f});
                        Drawing::DrawRectangle(screen.x - 15, screen.y + 7, 30.f * npc.combat.health / 60, 3,
                                               {.95f, .4f, .2f});
                    }
                }
            });
    };
    person("companion/ria", state.ria, models::CharacterType::Engineer, models::Gender::Female,
           models::HairStyle::Ponytail, {.9f, .58f, .3f}, "리아", true);
    person("companion/kain", state.kain, models::CharacterType::Guard, models::Gender::Male,
           models::HairStyle::Buzz, {.38f, .53f, .66f}, "카인", state.kainJoined);
    for (size_t i = 0; i < state.survivors.size(); ++i)
    {
        person("convoy/person/" + std::to_string(i), state.survivors[i],
               i % 3 ? models::CharacterType::Engineer : models::CharacterType::Explorer,
               i % 2 ? models::Gender::Female : models::Gender::Male, models::HairStyle(i % 6),
               {.65f, .69f, .51f},
               i == 0 ? (state.convoyRescued ? "구조된 운송대 · 5명" : "운송대 생존자 · 5명") : "", false);
    }
    for (size_t i = 0; i < state.scrappers.size(); ++i)
    {
        const auto &npc = state.scrappers[i];
        if (state.negotiated && npc.arrived)
        {
            continue;
        }
        person("scrapper/" + std::to_string(i), npc,
               i ? models::CharacterType::Guard : models::CharacterType::Explorer,
               i == 1 ? models::Gender::Female : models::Gender::Male, models::HairStyle::Curls,
               {.7f, .36f, .25f},
               state.negotiated         ? "철수 중"
               : npc.combat.health <= 0 ? "제압됨"
                                        : "폐설비 점거자",
               true);
    }
    auto player = Game::Get().GetState().playerPosition;
    PlaceWorldActor("player", player.x, player.y, WorldBounds(player.x, player.y, 0, 0, 85, 110), 1,
                    player.x + player.y,
                    [player]() { Drawing::DrawPerson(player, {.67f, .8f, .82f}, true); });
    for (const auto &point : level.Interactions())
    {
        if (point.action == ChapterAction::ConvoySupply)
        {
            continue;
        }
        if (point.action == ChapterAction::Exit && Game::Distance(player, point.position) > 9)
        {
            continue;
        }
        PlaceWorldActor("objective/" + std::to_string(int(point.action)), point.position.x, point.position.y,
                        WorldBounds(point.position.x, point.position.y, 0, 0, 70, 230), 5, 0, [point]() {
                            Drawing::DrawInteractionMarker(point.position, point.label, {.9f, .78f, .4f});
                        });
    }
    DrawActors();
    for (const auto &tracer : state.tracers)
    {
        auto start =
            Drawing::WorldToScreen(tracer.start.x, tracer.start.y, models::WorldScale::aimHeightPixels);
        auto end = Drawing::WorldToScreen(tracer.end.x, tracer.end.y, models::WorldScale::aimHeightPixels);
        Drawing::DrawLine(start, end,
                          tracer.npcOwned ? ColorRGBA(.3f, .85f, 1, tracer.remaining / .12f)
                                          : ColorRGBA(1, .74f, .3f, tracer.remaining / .12f),
                          2);
    }
}

// ============================================================================
// ScreenScene
// ============================================================================
void ScreenScene::PlaceScreenActor(game::Scene &scene, const std::string &name, game::Bounds bounds,
                                   int layer, std::function<void()> draw)
{
    scene.Place(name, "travel", bounds, layer, 0, [draw = std::move(draw)](const game::Actor &) { draw(); });
}

void ScreenScene::DrawScreenScene(game::Scene &scene)
{
    scene.EndPlacement();
    auto &renderer = GameRenderer::Get().Backend();
    renderer.BeginModels();
    for (const auto *actor :
         scene.GetGraph().CollectVisible({0, 0, float(kCanvasWidth), float(kCanvasHeight)}))
    {
        renderer.BindObject(&actor->GetRenderObjects());
        actor->Draw();
    }
    renderer.EndModels();
}

// ============================================================================
// TravelBackgroundScene
// ============================================================================
std::vector<SceneryTile> TravelScenery::VisibleTiles(double distance, float pixelsPerUnit, float spacing,
                                                     float margin)
{
    std::vector<SceneryTile> result;
    if (!std::isfinite(distance) || !std::isfinite(pixelsPerUnit) || !std::isfinite(spacing) ||
        !std::isfinite(margin) || spacing <= 0 || margin < 0 || pixelsPerUnit < 0)
    {
        return result;
    }
    double offset = distance * pixelsPerUnit;
    int first = int(std::floor((offset - margin) / spacing));
    int last = int(std::ceil((offset + tutorial::kCanvasWidth + margin) / spacing));
    for (int id = first; id <= last; ++id)
    {
        result.push_back({id, float(id * double(spacing) - offset)});
    }
    return result;
}
unsigned int TravelScenery::Variant(int id, int layer)
{
    unsigned int hash =
        static_cast<unsigned int>(id) * 73856093u ^ static_cast<unsigned int>(layer) * 19349663u;
    hash ^= hash >> 13;
    hash *= 1274126177u;
    return hash ^ (hash >> 16);
}
void TravelBackgroundScene::Draw()
{
    using namespace tutorial;
    auto &scene = *this;
    scene.BeginPlacement();
    PlaceScreenActor(scene, "sky", {0, 0, float(kCanvasWidth), float(kCanvasHeight)}, 0, [this]() {
        Drawing::DrawRectangle(0, 0, float(kCanvasWidth), float(kCanvasHeight),
                               ColorRGBA(.018f, .035f, .067f));
        Drawing::DrawRectangle(0, 280, float(kCanvasWidth), 390, ColorRGBA(.065f, .13f, .17f));
    });
    PlaceScreenActor(scene, "gate", {900, 45, 1190, 380}, 1, [this]() {
        float drift = GameLevels::Travel().GetState().distance * .012f;
        Vector2 center = {1040 - drift, 205};
        Drawing::DrawEllipse(center.x, center.y, 105, 130, ColorRGBA(.09f, .3f, .34f, .7f));
        Drawing::DrawEllipse(center.x, center.y, 96, 121, ColorRGBA(.019f, .045f, .067f));
        Drawing::DrawGlow(center, 170, ColorRGBA(.2f, .7f, .8f, .13f));
        Drawing::DrawLabel(center.x - 55, center.y + 155, "관문 N-0 / 격리 회랑");
    });
    for (int layer = 0; layer < 3; ++layer)
    {
        constexpr float factors[] = {.5f, 1.9f, 4.5f};
        float spacing = 160 + layer * 40.f;
        for (const auto tile : TravelScenery::VisibleTiles(GameLevels::Travel().GetState().distance,
                                                           factors[layer], spacing, 280))
        {
            unsigned int variant = TravelScenery::Variant(tile.id, layer);
            float base = 355 + layer * 52.f, height = 58 + float(variant % 105);
            PlaceScreenActor(scene, "skyline/" + std::to_string(layer) + "/" + std::to_string(tile.id),
                             {tile.x - 120, base - height - 35, tile.x + spacing, base}, 2 + layer,
                             [tile, base, height, layer, variant]() {
                                 float shade = .055f + layer * .018f;
                                 models::Palette palette;
                                 palette.body = {shade, shade + .055f, shade + .08f, 1};
                                 Drawing::DrawModel3D(models::ModelLibrary::Get().buildings[variant % 4],
                                                      {tile.x, base}, {1.25f, 1.25f, height / 44}, {}, -6000,
                                                      palette);
                             });
        }
    }
    PlaceScreenActor(scene, "fog", {0, 350, float(kCanvasWidth), 640}, 5, [this]() {
        Drawing::DrawRectangle(0, 390, float(kCanvasWidth), 240, ColorRGBA(.12f, .27f, .3f, .12f));
    });
    DrawScreenScene(scene);
}

// ============================================================================
// JetTravelScene
// ============================================================================
void JetTravelScene::Draw()
{
    using namespace tutorial;
    auto &scene = *this;
    scene.BeginPlacement();
    PlaceScreenActor(scene, "corridor", {0, 335, float(kCanvasWidth), 695}, 0, [this]() {
        models::Palette road;
        road.body = {.10f, .16f, .19f, 1};
        models::Transform3D rotation;
        rotation.yaw = -kPi / 4;
        rotation.position = rotation.Apply({-35, -5.5f, -.45f});
        Drawing::DrawModel3D(models::ModelLibrary::Get().grounds[1], {640, 485}, {70, 11, .08f}, rotation, 0,
                             road);
        Drawing::DrawLine({0, 359}, {float(kCanvasWidth), 359}, ColorRGBA(.3f, .68f, .73f), 4);
        Drawing::DrawLine({0, 641}, {float(kCanvasWidth), 641}, ColorRGBA(.25f, .5f, .55f), 5);
        for (const auto tile : TravelScenery::VisibleTiles(GameLevels::Travel().GetState().distance,
                                                           TravelScenery::kRoadScroll, 170))
        {
            float x = tile.x;
            Drawing::DrawLine({x, 445}, {x + 85, 445}, {.35f, .56f, .59f, .8f}, 3);
            Drawing::DrawLine({x, 545}, {x + 85, 545}, {.35f, .56f, .59f, .8f}, 3);
            Drawing::DrawLine({x, 373}, {x + 16, 373}, {.65f, .72f, .57f, .7f}, 4);
            Drawing::DrawLine({x, 627}, {x + 16, 627}, {.65f, .72f, .57f, .7f}, 4);
        }
        // Frequent expansion joints make the road surface's motion legible.
        for (const auto tile : TravelScenery::VisibleTiles(GameLevels::Travel().GetState().distance,
                                                           TravelScenery::kRoadScroll, 85))
        {
            Drawing::DrawLine({tile.x, 365}, {tile.x, 635}, {.35f, .46f, .49f, .16f});
        }
    });
    for (int i = 0; i < kObstacleCount; ++i)
    {
        const auto obstacle = GameLevels::Travel().GetState().obstacles[i];
        float x = GameLevels::Travel().GetObstacleScreenX(obstacle);
        float y = 485 + obstacle.lane * 115;
        PlaceScreenActor(scene, "debris/" + std::to_string(i), {x - 38, y - 44, x + 45, y + 32}, 1,
                         [this, x, y]() {
                             Drawing::DrawSoftShadow({x + 8, y + 27}, 40, 13, .45f);
                             models::Palette debris;
                             debris.body = {.38f, .29f, .27f, 1};
                             models::Transform3D rotation;
                             rotation.yaw = -kPi / 4;
                             Drawing::DrawModel3D(models::ModelLibrary::Get().box, {x, y}, {1.1f, .9f, .8f},
                                                  rotation, (y - 485) * 2, debris);
                             Drawing::DrawGlow({x, y}, 45, ColorRGBA(1, .22f, .12f, .3f));
                         });
    }
    Vector2 bike = GameLevels::Travel().GetBikeScreenPosition();
    PlaceScreenActor(scene, "bike-shadow", {bike.x - 100, bike.y + 12, bike.x + 115, bike.y + 65}, 1,
                     [this, bike]() { Drawing::DrawSoftShadow({bike.x + 15, bike.y + 40}, 85, 20, .5f); });
    PlaceScreenActor(
        scene, "bike", {bike.x - 210, bike.y - 75, bike.x + 140, bike.y + 80}, 2, [this, bike]() {
            float bob = std::sin(Game::Get().GetState().animationTimeSeconds * 9) * 2;
            Drawing::DrawTravelBike({bike.x, bike.y + bob}, GameLevels::Travel().GetState().yawRadians,
                                    GameLevels::Travel().GetState().speed / 108.f);
        });
    scene.GetGraph().Reparent("bike-shadow", *scene.GetGraph().Find("bike"));
    PlaceScreenActor(scene, "speed-trails", {0, 250, float(kCanvasWidth), 710}, 4, [this]() {
        float distance = GameLevels::Travel().GetState().distance;
        float speed = GameLevels::Travel().GetState().speed;
        for (int row = 0; row < 9; ++row)
        {
            float y = 312 + row * 44.f;
            for (const auto tile : TravelScenery::VisibleTiles(distance, 14 + float(row % 3) * 2, 390, 240))
            {
                unsigned int v = TravelScenery::Variant(tile.id, row);
                float x = tile.x + (v % 73), length = 30 + speed * .7f;
                Drawing::DrawLine({x, y}, {x + length, y}, {.6f, .8f, .86f, .10f}, 1);
            }
        }
        for (const auto tile :
             TravelScenery::VisibleTiles(distance, TravelScenery::kForegroundScroll, 310, 120))
        {
            float x = tile.x;
            Drawing::DrawRectangle(x, 660, 20, 140, {.04f, .11f, .15f, .9f});
            Drawing::DrawLine({x, 672}, {x + 20, 672}, {.7f, .56f, .28f}, 4);
        }
    });
    DrawScreenScene(scene);
}

// ============================================================================
// MineScene — darkness overlay
// ============================================================================
void MineScene::DrawDarkness()
{
    using namespace tutorial;
    profiling::Scope timer(profiling::Timer::MineLighting);
    // A smooth vertex mask works with the existing compatibility renderer and its fallback.
    // Applied AFTER bloom: concealed rooms cannot leak bright sprites or post-process glow.
    if (GameRenderer::Get().Backend().DrawMineDarkness())
    {
        return;
    }
    constexpr int step = 16;
    constexpr int columns = kCanvasWidth / step + 1;
    constexpr int rows = kCanvasHeight / step + 1;
    std::vector<float> alpha(columns * rows);
    auto origin = Drawing::WorldToScreen(0, 0);
    for (int y = 0; y < rows; ++y)
    {
        for (int x = 0; x < columns; ++x)
        {
            auto screen = WorldCamera::FromViewport({float(x * step), float(y * step)});
            float sx = (screen.x - origin.x) / 36;
            float sy = (screen.y - origin.y) / 18;
            tutorial::Vector2 world = {(sx + sy) * .5f, (sy - sx) * .5f};
            alpha[y * columns + x] = 1 - GameLevels::Mine().Lighting().At(world);
        }
    }
    renderqueue::Texture(0);
    renderqueue::Effect(0, 0, 0);
    renderqueue::Begin(GL_QUADS);
    for (int y = 0; y < rows - 1; ++y)
    {
        for (int x = 0; x < columns - 1; ++x)
        {
            const int corners[4][2] = {{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}};
            for (const auto &corner : corners)
            {
                renderqueue::Color(0, 0, 0, alpha[corner[1] * columns + corner[0]]);
                renderqueue::Vertex(float(corner[0] * step), float(corner[1] * step));
            }
        }
    }
    renderqueue::End();
}

// ============================================================================
// BackgroundScene / HudScene — screen-space compositions
// ============================================================================

void BackgroundScene::Draw()
{
    BeginPlacement();
    if (SceneManager::Current() == SceneKind::Exterior)
    {
        // The city now fills the view with world geometry, including outside its walkable bounds.
        EndPlacement();
        return;
    }
    PlaceScreenActor(*this, "background", {0, 0, float(kCanvasWidth), float(kCanvasHeight)}, 0,
                     []() { Drawing::DrawBackdrop(); });
    DrawScreenScene(*this);
}
void HudScene::Draw()
{
    BeginPlacement();
    PlaceScreenActor(*this, "hud", {0, 0, float(kCanvasWidth), float(kCanvasHeight)}, 0,
                     []() { Hud::Render(); });
    DrawScreenScene(*this);
}

// ============================================================================
// SceneManager
// ============================================================================

Scene &SceneManager::Get(SceneKind kind)
{
    static auto scenes = []() {
        std::array<std::unique_ptr<Scene>, static_cast<size_t>(SceneKind::Count)> result;
        result[size_t(SceneKind::Interior)] = std::make_unique<InteriorScene>();
        result[size_t(SceneKind::Exterior)] = std::make_unique<CityScene>();
        result[size_t(SceneKind::Observatory)] = std::make_unique<ObservatoryScene>();
        result[size_t(SceneKind::JetTravel)] = std::make_unique<JetTravelScene>();
        result[size_t(SceneKind::TravelBackground)] = std::make_unique<TravelBackgroundScene>();
        result[size_t(SceneKind::Mine)] = std::make_unique<MineScene>();
        result[size_t(SceneKind::Frontier)] = std::make_unique<FrontierScene>();
        result[size_t(SceneKind::Background)] = std::make_unique<BackgroundScene>();
        result[size_t(SceneKind::Hud)] = std::make_unique<HudScene>();
        return result;
    }();
    return *scenes.at(size_t(kind));
}
SceneKind SceneManager::Current()
{
    const auto &state = Game::Get().GetState();
    return GameLevels::Chapter().IsActive() ? SceneKind::Frontier
           : GameLevels::Mine().IsActive()  ? SceneKind::Mine
           : state.isJetTravel              ? SceneKind::JetTravel
           : state.isInObservatory          ? SceneKind::Observatory
           : state.isInsideHome             ? SceneKind::Interior
                                            : SceneKind::Exterior;
}
void SceneManager::Reset()
{
    for (size_t i = 0; i < size_t(SceneKind::Count); ++i)
    {
        auto &scene = Get(SceneKind(i));
        scene.BeginPlacement();
        scene.EndPlacement();
        scene.GetGraph().Clear();
    }
}
void SceneManager::Update(float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::SceneUpdate);
    Get(Current()).GetGraph().Update(deltaSeconds);
    if (GameLevels::Travel().IsActive())
    {
        Get(SceneKind::TravelBackground).GetGraph().Update(deltaSeconds);
    }
    Get(SceneKind::Background).GetGraph().Update(deltaSeconds);
    Get(SceneKind::Hud).GetGraph().Update(deltaSeconds);
}
} // namespace game
