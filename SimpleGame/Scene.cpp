#include "stdafx.h"
#include "Scene.h"
#include "Civic.h"
#include "CityFacilities.h"
#include "FacilityApproaches.h"
#include "CityInfrastructure.h"
#include "Drawing.h"
#include "Hud.h"
#include "Level.h"
#include "Overworld.h"
#include "Profiler.h"
#include "RenderCache.h"
#include "RenderQueue.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <set>
#include <chrono>
#include <map>

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
        glOrtho(first.x, last.x, last.y, first.y, -65536, 65536);
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
void DrawInfrastructureGuide(Vector2 standing, Vector2 equipment, const std::string &label)
{
    Drawing::DrawHologramStrip(standing, equipment, .18f, {.3f, .75f, .8f}, .06f);
    const auto screen = Drawing::WorldToScreen(standing.x, standing.y, 50);
    Drawing::DrawLabel(screen.x + 9, screen.y - 8, label, {.5f, .85f, .9f});
    Drawing::DrawLabel(screen.x + 9, screen.y + 12, "외부 점검 접근로", {.65f, .75f, .8f});
}
} // namespace

// ============================================================================
// Scene ? Actor placement
// ============================================================================
void Scene::BeginPlacement()
{
    if (++placementGeneration == 0)
    {
        for (auto &entry : placements)
        {
            entry.second = 0;
        }
        placementGeneration = 1;
    }
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
    placements[name] = placementGeneration;
    return *actor;
}
void Scene::EndPlacement()
{
    for (auto it = placements.begin(); it != placements.end();)
    {
        if (it->second != placementGeneration)
        {
            graph.Remove(it->first);
            it = placements.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
bool Scene::ReusePlacement(const std::string &name)
{
    auto found = placements.find(name);
    if (found == placements.end() || !graph.Find(name))
    {
        return false;
    }
    found->second = placementGeneration;
    profiling::Count(profiling::Counter::ScenePlacements);
    return true;
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
            for (int i = 0; i < 6; ++i)
            {
                Drawing::DrawProp(models::ModelLibrary::Get().wall, {-3.f + i, -3}, {1, .12f, 95.f / 44});
            }
        });
        scene.PlaceWorldActor("wall/side", -3, -3, WorldBounds(-3, -3, .12f, 6, 95), 1, 1, [this]() {
            for (int i = 0; i < 6; ++i)
            {
                Drawing::DrawProp(models::ModelLibrary::Get().wall, {-3, -3.f + i}, {1, .12f, 95.f / 44}, {},
                                  kPi / 2);
            }
        });
        scene.PlaceWorldActor("window", -1, -2.85f, WorldBounds(-1, -2.85f, 0, 0, 80, 150), 1, 2, [this]() {
            Vector2 window = Drawing::WorldToScreen(-1, -2.85f, 68);
            Drawing::DrawLine(window, {window.x + 110, window.y + 55}, ColorRGBA(.25f, .72f, .82f), 18);
        });
        scene.PlaceWorldActor("bed", -2.6f, -2.6f, WorldBounds(-2.6f, -2.6f, 1.8f, 1.1f, 22), 1, 3, [this]() {
            Drawing::DrawProp(models::ModelLibrary::Get().bed, {-2.05f, -2.05f});
        });
        scene.PlaceWorldActor("terminal", 1.3f, -2.6f, WorldBounds(1, -2.6f, 1.6f, 1.4f, 65), 1, 4, [this]() {
            Drawing::DrawModel3D(models::ModelLibrary::Get().console, Drawing::WorldToScreen(1.6f, -2.4f),
                                 {1, 1, 1}, {}, (1.6f - 2.4f) * 36, {});
            Drawing::DrawGlow(Drawing::WorldToScreen(1, -1.2f), 20, ColorRGBA(.3f, .95f, .9f));
        });
        scene.PlaceWorldActor("table", -2.4f, 1.1f, WorldBounds(-2.4f, 1.1f, 1.2f, .6f, 55), 1, 5, [this]() {
            Drawing::DrawProp(models::ModelLibrary::Get().table, {-1.8f, 1.4f});
            Drawing::DrawProp(models::ModelLibrary::Get().chair, {-1.8f, 2.15f});
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
    return Overworld::Traffic().RenderPosition(size_t(index));
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
    const auto camera = Game::Get().GetState().cameraPosition;
    int minX = (std::max)(CityLayout::kTerrainMinX, int(std::floor((camera.x - 28) / chunkSize)) * chunkSize);
    int minY = (std::max)(CityLayout::kTerrainMinY, int(std::floor((camera.y - 28) / chunkSize)) * chunkSize);
    int maxX = (std::min)(CityLayout::kTerrainMaxX, int(std::ceil(camera.x + 28)));
    int maxY = (std::min)(CityLayout::kTerrainMaxY, int(std::ceil(camera.y + 28)));
    for (int x0 = minX; x0 < maxX; x0 += chunkSize)
    {
        for (int y0 = minY; y0 < maxY; y0 += chunkSize)
        {
            std::string key = "terrain/city/" + std::to_string(x0) + "/" + std::to_string(y0);
            scene.PlaceWorldActor(
                key, float(x0), float(y0), WorldBounds(float(x0), float(y0), chunkSize, chunkSize, 0), 0, 0,
                [this, x0, y0, key, chunkSize]() {
                    rendercache::DrawWorld(key, [this, x0, y0, chunkSize]() {
                        for (const auto &patch : CityPlan::Terrain(x0, y0, chunkSize))
                        {
                            Drawing::DrawModel3D(CityPlan::Ground(patch.ground),
                                                 Drawing::WorldToScreen(patch.position.x, patch.position.y),
                                                 {patch.length + .012f, 1.012f, 1}, {},
                                                 (patch.position.x + patch.position.y) * 36, {});
                        }
                    });
                });
        }
    }
}

void CityScene::DrawTrafficInfrastructure(game::Scene &scene)
{
    const auto &net = RoadNetwork::Get();
    const auto camera = Game::Get().GetState().cameraPosition;
    auto add = [](Vector2 a, Vector2 b) { return Vector2{a.x + b.x, a.y + b.y}; };
    auto mul = [](Vector2 a, float b) { return Vector2{a.x * b, a.y * b}; };
    const auto &modules = TransitLayout::Get();
    for (size_t i = 0; i < modules.size(); ++i)
    {
        const auto &module = modules[i];
        if (Game::Distance(module.position, camera) > (module.air ? 85.f : 48.f) + module.radius)
        {
            continue;
        }
        const auto name = "transit/" + std::to_string(i);
        if (scene.ReusePlacement(name))
        {
            continue; // TransitLayout's published placements are immutable.
        }
        const float r = module.radius;
        const auto &surface =
            models::ResourceAssets::Get(models::ModelLibrary::Transit(module.asset).resourceId);
        const float top = module.altitude + surface.maximum.z;
        scene.PlaceWorldActor(
            name, module.position.x, module.position.y,
            WorldBounds(module.position.x - r, module.position.y - r, r * 2, r * 2, (top + 2) * 44, 24), 1, 0,
            [module]() {
                Drawing::DrawProp(models::ModelLibrary::Transit(module.asset), module.position, {1, 1, 1}, {},
                                  module.yaw, module.altitude);
            });
    }
    const auto &alleys = CityPlan::Walkways();
    for (size_t i = 0; i < alleys.size(); ++i)
    {
        const auto &m = alleys[i];
        if (Game::Distance(m.position, camera) > 48 + m.radius)
        {
            continue;
        }
        scene.PlaceWorldActor(
            "frontage/" + std::to_string(i), m.position.x, m.position.y,
            WorldBounds(m.position.x - m.radius, m.position.y - m.radius, m.radius * 2, m.radius * 2, 20), 1,
            0, [m]() {
                Drawing::DrawProp(models::ModelLibrary::Transit(m.asset), m.position, {1, 1, 1}, {}, m.yaw,
                                  m.altitude);
            });
    }
    for (const auto &group : {std::make_pair("facility/walkway/", &FacilityApproaches::Get().modules),
                              std::make_pair("infrastructure/walkway/", &CityInfrastructure::Get().modules)})
    {
        for (size_t i = 0; i < group.second->size(); ++i)
        {
            const auto &m = (*group.second)[i];
            if (Game::Distance(m.position, camera) > 48 + m.radius)
            {
                continue;
            }
            scene.PlaceWorldActor(
                std::string(group.first) + std::to_string(i), m.position.x, m.position.y,
                WorldBounds(m.position.x - m.radius, m.position.y - m.radius, m.radius * 2, m.radius * 2, 20),
                1, 0, [m]() {
                    Drawing::DrawProp(models::ModelLibrary::Transit(m.asset), m.position, {1, 1, 1}, {},
                                      m.yaw, m.altitude);
                });
        }
    }
    for (size_t junction = 0; junction < net.junctions.size(); ++junction)
    {
        const auto &j = net.junctions[junction];
        if (Game::Distance(j.position, camera) > 46)
        {
            continue;
        }
        auto p = j.position;
        scene.PlaceWorldActor(
            "junction/" + std::to_string(junction), p.x, p.y,
            WorldBounds(p.x - j.radius - 4, p.y - j.radius - 4, (j.radius + 4) * 2, (j.radius + 4) * 2, 150),
            1, 0, [=, &net]() {
                std::vector<int> arms;
                for (int lane : j.outgoing)
                {
                    if (net.lanes[lane].slot == 0)
                    {
                        arms.push_back(lane);
                    }
                }
                if (arms.size() < 3)
                {
                    return;
                }
                for (int arm : arms)
                {
                    const auto &out = net.lanes[arm];
                    auto d = out.direction;
                    Vector2 n = RoadNetwork::RightOf(d);
                    bool walk = Overworld::Traffic().PedestrianGreen(int(junction), out.road);
                    ColorRGBA pedestrian = walk ? ColorRGBA(.15f, 1, .78f) : ColorRGBA(.95f, .21f, .28f);
                    float half = net.roads[out.road].lanes * RoadNetwork::kLaneWidth * .5f;
                    auto across = add(p, mul(d, j.radius + 1.f));
                    // Rails sit on the two long edges of the native four-metre crosswalk.
                    // Its actual road width is shared with the mesh placement and navigation.
                    for (float edge : {-1.f, 1.f})
                    {
                        auto center = add(across, mul(d, .90f * edge));
                        Drawing::DrawHologramStrip(add(center, mul(n, -half + .08f)),
                                                   add(center, mul(n, half - .08f)), .10f, pedestrian, .085f);
                    }
                    for (int incoming = 0; incoming < int(net.lanes.size()); ++incoming)
                    {
                        const auto &lane = net.lanes[incoming];
                        if (lane.road != out.road || lane.to != int(junction))
                        {
                            continue;
                        }
                        std::vector<TrafficTurn> turns;
                        for (auto turn : {TrafficTurn::Straight, TrafficTurn::Left, TrafficTurn::Right})
                        {
                            if (net.CanTurn(incoming, turn))
                            {
                                turns.push_back(turn);
                            }
                        }
                        unsigned int signal = 0;
                        for (auto turn : turns)
                        {
                            signal = (std::max)(signal, unsigned(Overworld::Traffic().Light(incoming, turn)));
                        }
                        auto head = add(add(p, mul(d, j.radius + 2.2f)),
                                        mul(n, -(lane.slot + .5f) * RoadNetwork::kLaneWidth));
                        bool leftOnly =
                            Overworld::Traffic().Light(incoming, TrafficTurn::Left) == TrafficLight::Green &&
                            Overworld::Traffic().Light(incoming, TrafficTurn::Straight) !=
                                TrafficLight::Green;
                        // A protected left must not look like another straight-ahead green.
                        Drawing::DrawProp(models::ModelLibrary::TrafficSignal(leftOnly ? 0 : signal), head,
                                          {.9f, 1, 1}, {}, std::atan2(d.y, d.x) + kPi * .5f);
                        if (leftOnly)
                        {
                            const auto &resource = models::ResourceAssets::Get(
                                models::ModelLibrary::TrafficSignal(0).resourceId);
                            auto label = Drawing::WorldToScreen(head.x, head.y, resource.maximum.z * 44 + 12);
                            Drawing::DrawLabel(label.x - 20, label.y, "좌회전", {.2f, 1, .75f});
                        }
                    }
                    // Paint the stop line at the actual approach boundary, immediately before the crossing.
                    auto lineCenter = add(add(p, mul(d, j.radius + 2.2f)), mul(n, -half * .5f));
                    Drawing::DrawGroundStrip(add(lineCenter, mul(n, -half * .5f)),
                                             add(lineCenter, mul(n, half * .5f)), .09f, {.78f, .86f, .87f},
                                             .045f);
                    // Small glowing sidewalk pads keep the pedestrian light legible from either side.
                    for (float side : {-1.f, 1.f})
                    {
                        auto pad = add(across, mul(n, (half + .45f) * side));
                        Drawing::DrawHologramStrip(add(pad, mul(d, -.48f)), add(pad, mul(d, .48f)), .45f,
                                                   pedestrian, .18f);
                    }
                }
            });
    }
}

void CityScene::DrawStreetFurniture(game::Scene &scene)
{
    auto minimum = CityLayout::kPlazaMin, maximum = CityLayout::kPlazaMax;
    scene.PlaceWorldActor("quest-plaza", minimum.x, minimum.y,
                          WorldBounds(minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y, 0),
                          1, 0, [=]() {
                              float centerY = (minimum.y + maximum.y) * .5f;
                              Drawing::DrawGroundStrip({minimum.x, centerY}, {maximum.x, centerY},
                                                       maximum.y - minimum.y, {.25f, .33f, .36f}, .025f);
                          });
    auto bayMin = CityLayout::kBikeAccessMin, bayMax = CityLayout::kBikeAccessMax;
    scene.PlaceWorldActor(
        "bike-bay", kJetBikePosition.x, kJetBikePosition.y,
        WorldBounds(bayMin.x, bayMin.y, bayMax.x - bayMin.x, bayMax.y - bayMin.y, 0), 2, 0, [=]() {
            // A visible driveway connects the reserved bay to the six-lane boulevard.
            float roadEdgeY = kBikeRoadExit.y + 3 * RoadNetwork::kLaneWidth + .05f;
            Drawing::DrawGroundStrip({kJetBikePosition.x, CityLayout::kPlazaMin.y},
                                     {kJetBikePosition.x, roadEdgeY}, bayMax.x - bayMin.x, {.18f, .27f, .3f},
                                     .045f);
            for (float side : {-1.f, 1.f})
            {
                Drawing::DrawHologramStrip({kJetBikePosition.x + side * .8f, kJetBikePosition.y - 1},
                                           {kJetBikePosition.x + side * .8f, kJetBikePosition.y + 1}, .045f,
                                           {.3f, .78f, .87f}, .08f);
            }
        });
    auto beacon = kBeaconPosition;
    scene.PlaceWorldActor("beacon-platform", beacon.x, beacon.y,
                          WorldBounds(beacon.x - 1.3f, beacon.y - .6f, 2.6f, 1.2f, 0), 2, 0, [beacon]() {
                              Drawing::DrawGroundStrip({beacon.x - 1.3f, beacon.y},
                                                       {beacon.x + 1.3f, beacon.y}, 1.2f, {.2f, .28f, .34f},
                                                       .05f);
                          });
    scene.PlaceWorldActor("home-entry", kHomeDoor.x, kHomeDoor.y,
                          WorldBounds(kHomeDoor.x - 1, kHomeDoor.y - 1.4f, 2, 1.7f, 0), 2, 0, []() {
                              Drawing::DrawGroundStrip({kHomeDoor.x, kHomeDoor.y - 1.25f}, kHomeDoor, 1.4f,
                                                       {.22f, .36f, .39f}, .04f);
                          });
    for (int i = -6; i <= 6; i += 3)
    {
        if (i == 0)
        {
            continue; // The widened north/south carriageway must remain clear.
        }
        scene.PlaceWorldActor(
            "lamp/" + std::to_string(i), float(i), 3.7f, WorldBounds(float(i), 3.7f, .55f, .6f, 80), 2, 0,
            [this, i]() {
                Drawing::DrawProp(models::ModelLibrary::Scenery(models::SceneryType::Streetlight),
                                  {float(i), 3.7f});
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

void CityScene::DrawImportedScenery(game::Scene &scene)
{
    const auto &scenery = Overworld::Scenery();
    const bool unchanged = sceneryRevision == scenery.Revision();
    for (size_t i = 0; i < scenery.Props().size(); ++i)
    {
        const auto name = "scenery/" + std::to_string(i);
        if (unchanged && scene.ReusePlacement(name))
        {
            continue;
        }
        const auto &prop = scenery.Props()[i];
        const auto &resource = models::ResourceAssets::Get(prop.model->resourceId);
        scene.PlaceWorldActor(
            name, prop.position.x, prop.position.y,
            WorldBounds(prop.minimum.x, prop.minimum.y, prop.maximum.x - prop.minimum.x,
                        prop.maximum.y - prop.minimum.y, (resource.maximum.z + prop.elevation) * 44 + 8),
            prop.model->ground ? 1 : 3, prop.position.x + prop.position.y, [prop]() {
                Drawing::DrawProp(*prop.model, prop.position, {1, 1, 1}, {}, prop.yaw, prop.elevation);
            });
    }
    sceneryRevision = scenery.Revision();
    for (const auto &craft : Overworld::Flights().Aircraft())
    {
        const auto &model = models::ModelLibrary::Aircraft(craft.type, craft.alternate);
        const auto &resource = models::ResourceAssets::Get(model.resourceId);
        // Parent Actor bounds must retain a visible ground shadow even when the hull is above view.
        float radius = (std::max)(resource.physicalSize.x, resource.physicalSize.y) +
                       (craft.altitude + resource.maximum.z + 2) * .55f + 2;
        auto p = craft.position;
        scene.PlaceWorldActor("aircraft/" + std::to_string(craft.id), p.x, p.y,
                              WorldBounds(p.x - radius, p.y - radius, radius * 2, radius * 2,
                                          (craft.altitude + resource.maximum.z + 3) * 44 + 24),
                              3, p.x + p.y, [craft]() {
                                  Drawing::DrawWorldAircraft(
                                      craft.position, craft.altitude, craft.type, craft.alternate, craft.yaw,
                                      craft.bank, craft.health <= 0,
                                      VehicleWreck::Opacity(craft.wreckSeconds, craft.altitude <= .2f));
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
                position = Game::Get().GetState().bikePosition;
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
            float elevation = item.kind == SceneObjectKind::Vehicle
                                  ? Overworld::Traffic().RenderElevation(size_t(item.index))
                              : item.kind == SceneObjectKind::JetBike ? Game::Get().GetState().bikeElevation
                                                                      : 0;
            bounds = WorldBounds(position.x, position.y, 0, 0, 90 + elevation * 44, 120);
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
            name = "vehicle/" + std::to_string(Overworld::Traffic().Vehicles()[item.index].id);
            break;
        case SceneObjectKind::JetBike:
            name = "personal-jet-bike";
            break;
        }
        scene.PlaceWorldActor(name, position.x, position.y, bounds, 3, item.depth, [this, item]() {
            if (item.kind == SceneObjectKind::Vehicle)
            {
                Vector2 position = GetTrafficPosition(item.index);
                const auto &car = Overworld::Traffic().Vehicles()[item.index];
                const auto palette =
                    car.health <= 0 ? VehicleWreck::Appearance(VehicleWreck::Opacity(car.wreckSeconds, true))
                                    : GetTrafficPalette(int(car.id % 12));
                Drawing::DrawWorldVehicle(
                    position, 3.3f + Overworld::Traffic().RenderElevation(size_t(item.index)) * 44, 1.f,
                    static_cast<models::VehicleType>(car.id % 4), palette, car.speed / 10.f,
                    Overworld::Traffic().RenderYaw(size_t(item.index)), car.health <= 0);
                if (car.hitFlash > 0)
                {
                    Drawing::DrawGlow(Drawing::WorldToScreen(position.x, position.y, 32), 18,
                                      {1, .6f, .2f, car.hitFlash / .15f});
                }
            }
            if (item.kind == SceneObjectKind::JetBike)
            {
                const auto &world = Game::Get().GetState();
                Drawing::DrawWorldBike(world.bikePosition, world.bikeYaw,
                                       .08f + .92f * std::fabs(world.bikeSpeed) / Overworld::kBikeBoostSpeed,
                                       world.isBikeMounted);
            }
            if (item.kind == SceneObjectKind::Resident)
            {
                const auto &resident = Game::Get().GetState().residents[item.index];
                Drawing::DrawPerson(
                    resident.position, ColorRGBA(resident.coat[0], resident.coat[1], resident.coat[2]), false,
                    resident.type, resident.facing.walking, resident.facing.yaw, resident.gender,
                    resident.hair, false, 0,
                    resident.health <= 0 || resident.impact.response == BodyResponse::Knockdown,
                    resident.speed);
                if (Game::Distance(Game::Get().GetState().playerPosition, resident.position) < 2.2f)
                {
                    Vector2 label = Drawing::WorldToScreen(resident.position.x, resident.position.y, 62);
                    Drawing::DrawLabel(label.x - 20, label.y, resident.name);
                    Drawing::DrawLabel(label.x - 20, label.y + 16, CityResidents::ActivityLabel(resident),
                                       {.5f, .8f, .85f});
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
                                    models::CharacterType::Engineer, false, .78539816f,
                                    models::Gender::Female, models::HairStyle::Ponytail, false, 0,
                                    CivicJustice::State().landmarkHealth[0] <= 0);
            }
            if (item.kind == SceneObjectKind::Mara)
            {
                Drawing::DrawPerson(kMaraPosition, ColorRGBA(.48f, .4f, .64f), false,
                                    models::CharacterType::Medic, false, .78539816f, models::Gender::Female,
                                    models::HairStyle::Bun, false, 0,
                                    CivicJustice::State().landmarkHealth[1] <= 0);
            }
        });
    };
    for (size_t i = 0; i < Game::Get().GetState().buildings.size(); i++)
    {
        placeObject({Game::Get().GetState().buildings[i].x + Game::Get().GetState().buildings[i].width +
                         Game::Get().GetState().buildings[i].y + Game::Get().GetState().buildings[i].depth,
                     SceneObjectKind::Building, (int)i});
    }
    if (!Game::Get().GetState().isBikeMounted)
    {
        placeObject({Game::Get().GetState().playerPosition.x + Game::Get().GetState().playerPosition.y,
                     SceneObjectKind::Player, 0});
    }
    placeObject({kLiaPosition.x + kLiaPosition.y, SceneObjectKind::Lia, 0});
    placeObject({kMaraPosition.x + kMaraPosition.y, SceneObjectKind::Mara, 0});
    for (int i = 0; i < int(Overworld::Traffic().Vehicles().size()); ++i)
    {
        Vector2 position = GetTrafficPosition(i);
        placeObject({position.x + position.y, SceneObjectKind::Vehicle, i});
    }
    auto bike = Game::Get().GetState().bikePosition;
    placeObject({bike.x + bike.y, SceneObjectKind::JetBike, 0});
    for (size_t i = 0; i < Game::Get().GetState().residents.size(); ++i)
    {
        if (i == Overworld::kKainResidentSlot && GameLevels::Chapter().GetState().kainJoined)
        {
            continue;
        }
        placeObject(
            {Game::Get().GetState().residents[i].position.x + Game::Get().GetState().residents[i].position.y,
             SceneObjectKind::Resident, int(i)});
    }
}

void CityScene::DrawUtilityDevices(game::Scene &scene)
{
    auto entrance = Overworld::kMineEntrance;
    scene.PlaceWorldActor(
        "mine-entrance", entrance.x, entrance.y, WorldBounds(entrance.x - 3, entrance.y - 2, 6, 4, 140), 3,
        entrance.x + entrance.y, [entrance]() {
            Drawing::DrawProp(models::ModelLibrary::Get().trafficGate, {entrance.x - 3, entrance.y - 1},
                              {6, .3f, 2.5f});
            Drawing::DrawInteractionMarker(entrance, "광산 입구 / 하차 후 E 입장", {.4f, .9f, 1});
        });
    for (const auto &city : Overworld::Cities())
    {
        auto p = city.center;
        std::string label =
            std::string(city.name) + " / 주민 " +
            std::to_string(CivicJustice::LivingPopulation(int(&city - Overworld::Cities().data()))) + "명";
        scene.PlaceWorldActor(
            std::string("city-sign/") + city.id, p.x, p.y, WorldBounds(p.x, p.y, 0, 0, 100, 150), 4, 0,
            [p, label]() { Drawing::DrawInteractionMarker({p.x, p.y + 4}, label, {.5f, .8f, .85f}); });
    }
    scene.PlaceWorldActor(
        "city-receiver", kCityReceiverPosition.x, kCityReceiverPosition.y,
        WorldBounds(kCityReceiverPosition.x, kCityReceiverPosition.y, .5f, .5f, 60), 4, 0, [this]() {
            Drawing::DrawProp(models::ModelLibrary::Get().console, kCityReceiverPosition);
            Drawing::DrawGlow(Drawing::WorldToScreen(kCityReceiverPosition.x, kCityReceiverPosition.y, 40),
                              16, ColorRGBA(.35f, .9f, 1));
        });
    scene.PlaceWorldActor("relay", kRelayPosition.x, kRelayPosition.y,
                          WorldBounds(kRelayPosition.x, kRelayPosition.y, .35f, .35f, 50), 4, 0, [this]() {
                              Drawing::DrawProp(models::ModelLibrary::Get().breaker, kRelayPosition);
                              Drawing::DrawGlow(
                                  Drawing::WorldToScreen(kRelayPosition.x, kRelayPosition.y, 29), 14,
                                  Game::Get().GetState().isPowerRestored ? ColorRGBA(.3f, 1, .65f)
                                                                         : ColorRGBA(1, .65f, .25f));
                          });
    scene.PlaceWorldActor("beacon", kBeaconPosition.x, kBeaconPosition.y,
                          WorldBounds(kBeaconPosition.x, kBeaconPosition.y, .4f, .4f, 60), 4, 0, [this]() {
                              Drawing::DrawProp(models::ModelLibrary::Get().beacon, kBeaconPosition);
                              Drawing::DrawGlow(
                                  Drawing::WorldToScreen(kBeaconPosition.x, kBeaconPosition.y, 34), 20,
                                  ColorRGBA(.5f, .83f, 1));
                          });
    const CityFacility *nearest = nullptr;
    float distance = 5;
    for (const auto &facility : CityFacilities::All())
    {
        const auto d = Game::Distance(Game::Get().GetState().playerPosition, facility.exterior);
        if (d < distance)
        {
            nearest = &facility;
            distance = d;
        }
    }
    if (nearest)
    {
        const auto facility = *nearest;
        const auto p = facility.exterior;
        scene.PlaceWorldActor(
            "facility/" + std::string(facility.id), p.x, p.y, WorldBounds(p.x, p.y, 0, 0, 90, 150), 5, 0,
            [facility]() {
                if (facility.hasInterior)
                {
                    Drawing::DrawHologramStrip(CityFacilities::StandingPoint(facility), facility.exterior,
                                               .3f, {.3f, .8f, .85f}, .07f);
                }
                if (!Game::Get().IsQuestInteraction(facility.target))
                {
                    Drawing::DrawInteractionMarker(facility.exterior, facility.label, {.4f, .85f, .9f});
                }
            });
    }
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
    const CityInfrastructureSite *nearestInfrastructure = nullptr;
    size_t infrastructureRole = 0;
    float infrastructureDistance = 5;
    const auto &infrastructure = CityInfrastructure::Get();
    for (size_t i = 0; i < infrastructure.sites.size(); ++i)
    {
        const auto &site = infrastructure.sites[i];
        const float distance = Game::Distance(site.standing, game.GetState().playerPosition);
        if (site.access == InfrastructureAccess::Connected && distance < infrastructureDistance)
        {
            nearestInfrastructure = &site;
            infrastructureRole = i % 3;
            infrastructureDistance = distance;
        }
    }
    if (nearestInfrastructure)
    {
        const auto &site = *nearestInfrastructure;
        const auto standing = site.standing, equipment = site.equipment;
        const std::string label = CityInfrastructure::Profiles()[site.city].roles[infrastructureRole].label;
        scene.PlaceWorldActor(
            "infrastructure/guide/" + site.id, standing.x, standing.y,
            WorldBounds(standing.x, standing.y, 0, 0, 90, 240), 5, 0,
            [standing, equipment, label]() { DrawInfrastructureGuide(standing, equipment, label); });
    }
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
        if (!game.GetState().isBikeMounted)
        {
            marker("jet-bike", game.GetState().bikePosition, "제트 바이크 탑승 [E]", {1, .75f, .4f});
        }
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
        DrawTrafficInfrastructure(scene);
        DrawStreetFurniture(scene);
        DrawImportedScenery(scene);
        // Open arterial roads replace the closed tutorial boundary.
        DrawDepthSortedObjects(scene);
        DrawUtilityDevices(scene);
        DrawQuestMarkers(scene);
        if (GameLevels::Chapter().GetState().kainJoined)
        {
            auto p = kCityKainPosition;
            scene.PlaceWorldActor(
                "chapter/kain", p.x, p.y, WorldBounds(p.x, p.y, 0, 0, 85, 100), 1, p.x + p.y, [p]() {
                    Drawing::DrawPerson(p, {.38f, .53f, .66f}, false, models::CharacterType::Guard, false,
                                        .785f, models::Gender::Male, models::HairStyle::Buzz, false, 0,
                                        CivicJustice::State().landmarkHealth[2] <= 0);
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
        for (const auto &vehicle : Overworld::Traffic().Vehicles())
        {
            auto name = "vehicle/" + std::to_string(vehicle.id);
            graph.Reparent("shadow/" + name, *graph.Find(name));
        }
        graph.Reparent("marker/lia", *graph.Find("lia"));
        graph.Reparent("marker/mara", *graph.Find("mara"));
        graph.Reparent("marker/relay", *graph.Find("relay"));
        graph.Reparent("marker/beacon", *graph.Find("beacon"));
        graph.Reparent("shadow/personal-jet-bike", *graph.Find("personal-jet-bike"));
        graph.Reparent("marker/jet-bike", *graph.Find("personal-jet-bike"));
    }
    scene.DrawActors();
    CivicPresentation::DrawWorld();
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
                if (Game::Get().SurfaceParty() && Game::Get().SurfaceParty()->Joined(CompanionId::Ria))
                {
                    return;
                }
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
                                combat.muzzleFlash, downed, 2.1f,
                                combat.pistol.IsReloading() ? combat.pistol.GetReloadProgress() : -1);
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
        position.x + position.y, [this, id, position, label, color, glyph, expected]() {
            Drawing::DrawProp(glyph ? models::ModelLibrary::Scenery(models::SceneryType::ObserverMonolith)
                                    : models::ModelLibrary::Get().console,
                              position, glyph ? models::Point{.6f, .6f, .6f} : models::Point{1, 1, 1});
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
            if (Game::Get().GetState().surfaceExploration &&
                !Game::Get().GetState().missions.Active(MissionId::Mine))
            {
                relevant = std::string(id) == "mine-exit";
            }
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
    const auto camera = Game::Get().GetState().cameraPosition;
    int minX = (std::max)(0, int(std::floor((camera.x - 28) / chunkSize)) * chunkSize);
    int minY = (std::max)(0, int(std::floor((camera.y - 28) / chunkSize)) * chunkSize);
    int maxX = (std::min)(kMapSize, int(std::ceil(camera.x + 28)));
    int maxY = (std::min)(kMapSize, int(std::ceil(camera.y + 28)));
    for (int y0 = minY; y0 < maxY; y0 += chunkSize)
    {
        for (int x0 = minX; x0 < maxX; x0 += chunkSize)
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
                                    bool ruins = x >= 84 && y >= 84;
                                    Drawing::DrawProp(models::ModelLibrary::Get().mineGround,
                                                      {float(x), float(y)}, {1, 1, 1},
                                                      {ruins ? std::array<float, 4>{.8f, .72f, .9f, 1}
                                                             : std::array<float, 4>{1, 1, 1, 1}});
                                    if ((x * 13 + y * 7) % 31 == 0)
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
    for (int y = (std::max)(1, minY); y < (std::min)(kMapSize - 1, maxY); ++y)
    {
        for (int x = (std::max)(1, minX); x < (std::min)(kMapSize - 1, maxX); ++x)
        {
            bool gate = GameLevels::Mine().Map().IsGateCell(x, y);
            if (GameLevels::Mine().GetState().tiles[y * kMapSize + x] != Tile::Wall && !gate)
            {
                continue;
            }
            if (gate && GameLevels::Mine().PowerAvailable())
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
                            Drawing::DrawProp(models::ModelLibrary::Get().bulkhead, {float(x), float(y)},
                                              {1, 1, 48.f / 44});
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
        if (lamp.needsPower && !GameLevels::Mine().PowerAvailable())
        {
            continue;
        }
        scene.PlaceWorldActor("lamp/" + std::to_string(i), lamp.position.x, lamp.position.y,
                              WorldBounds(lamp.position.x, lamp.position.y, 0, 0, 45, 100), 1,
                              lamp.position.x + lamp.position.y, [this, lamp]() {
                                  auto center = Drawing::WorldToScreen(lamp.position.x, lamp.position.y, 16);
                                  Drawing::DrawProp(models::ModelLibrary::Get().lantern, lamp.position);
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
    PlaceClue(scene, "mine-exit", GameLevels::Mine().GetState().entry, "E 광산 밖으로", {.4f, .9f, .8f},
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
            [this, enemy, i]() {
                auto screen = Drawing::WorldToScreen(enemy.position.x, enemy.position.y);
                Drawing::DrawSoftShadow(screen, 22, 8, .6f);
                models::Transform3D rotation;
                rotation.yaw = enemy.facing.yaw;
                auto pose = models::ModelRig::Pose(Game::Get().GetState().animationTimeSeconds * 10,
                                                   enemy.facing.walking);
                const size_t variant = enemy.position.x < kMineGateX ? i % 2 : 2 + i % 4;
                Drawing::DrawModel3D(models::ModelLibrary::Get().creatures[variant], screen, {1, 1, 1},
                                     rotation, (enemy.position.x + enemy.position.y) * 36,
                                     {{.35f, .27f, .2f, 1}, {.75f, .35f, .2f, 1}, {1, .2f, .08f, 1}}, &pose);
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
                    guard ? models::CharacterType::HeavyCombat : models::CharacterType::Miner,
                    npc.facing.walking,
                    npc.evacuating ? "헤일로로 철수 중"
                    : guard        ? "고립 전투원 / E 구조"
                                   : "광부 / E 구조",
                    npc.facing.yaw, npc.gender, npc.hair, npc.combat);
    }
    if (Game::Get().HasCompanion(CompanionId::Ria, CompanionPlace::Mine))
    {
        PlacePerson(scene, "ria", GameLevels::Mine().GetState().riaPosition, {.95f, .55f, .25f},
                    models::CharacterType::Engineer, GameLevels::Mine().GetState().riaFacing.walking, "리아",
                    GameLevels::Mine().GetState().riaFacing.yaw, models::Gender::Female,
                    models::HairStyle::Ponytail, GameLevels::Mine().GetState().riaCombat);
    }
    if (Game::Get().HasCompanion(CompanionId::Kain, CompanionPlace::Mine))
    {
        const auto &npc = GameLevels::Mine().GetState().kain;
        PlacePerson(scene, "kain", npc.position, {.38f, .53f, .66f}, models::CharacterType::Guard,
                    npc.facing.walking, "카인", npc.facing.yaw, models::Gender::Male, models::HairStyle::Buzz,
                    npc.combat);
    }
    auto player = Game::Get().GetState().playerPosition;
    scene.PlaceWorldActor("player", player.x, player.y, WorldBounds(player.x, player.y, 0, 0, 85), 1,
                          player.x + player.y,
                          [this, player]() { Drawing::DrawPerson(player, {.7f, .84f, .9f}, true); });
    scene.DrawActors();
    // Projectile segments stop at the collision point and are masked with the rest of the world.
    const auto &scan = GameLevels::Mine().GetState();
    if (scan.scannerSeconds > 0)
    {
        Drawing::DrawScannerPulse(scan.scannerOrigin, scan.scannerRadius,
                                  (std::min)(1.f, scan.scannerSeconds / .3f));
    }
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
// MineScene ? darkness overlay
// ============================================================================
void MineScene::DrawDarkness()
{
    profiling::Scope timer(profiling::Timer::MineLighting);
    // Apply after bloom so concealed rooms cannot leak emissive surfaces or glow.
    if (GameRenderer::Get().Backend().DrawMineDarkness())
    {
        return;
    }

    // Keep the CPU visibility mask for devices without the GPU darkness path.
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
            Vector2 world = {(sx + sy) * .5f, (sy - sx) * .5f};
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
            PlaceWorldActor(
                key, float(x0), float(y0), WorldBounds(float(x0), float(y0), 8, 8, 0), 0, 0,
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
                                ColorRGBA color = tile == FrontierTile::Grass   ? ColorRGBA(.24f, .37f, .26f)
                                                  : tile == FrontierTile::Sand  ? ColorRGBA(.55f, .44f, .28f)
                                                  : tile == FrontierTile::Canal ? ColorRGBA(.12f, .15f, .14f)
                                                  : tile == FrontierTile::Road  ? ColorRGBA(.28f, .30f, .30f)
                                                                                : ColorRGBA(.31f, .34f, .35f);
                                if (tile == FrontierTile::Grass || tile == FrontierTile::Sand)
                                {
                                    const auto &ground =
                                        models::ModelLibrary::Get()
                                            .outdoorGrounds[tile == FrontierTile::Grass ? 0 : 1];
                                    models::Palette palette;
                                    palette.body = {color.r, color.g, color.b, color.a};
                                    Drawing::DrawProp(ground, {float(x), float(y)}, {1, 1, 1}, palette);
                                }
                                else
                                {
                                    Drawing::DrawMaterialTile(float(x), float(y),
                                                              tile == FrontierTile::Road
                                                                  ? SurfaceMaterial::Asphalt
                                                                  : SurfaceMaterial::Concrete,
                                                              color);
                                }
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
                const auto &plant = i % 3 == 0
                                        ? models::ModelLibrary::Scenery(models::SceneryType::PrismBloom)
                                        : models::ModelLibrary::Get().reeds;
                Drawing::DrawProp(plant, p, {.8f, .8f, .8f});
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
                Drawing::DrawProp(models::ModelLibrary::Get().bulkhead, p, {2, .15f, 35.f / 44}, {},
                                  edge < 2 ? 0.f : kPi / 2);
            });
        }
    }
    prop("return-gate", {4, 1}, 115, []() {
        Drawing::DrawModel3D(models::ModelLibrary::Get().trafficGate, Drawing::WorldToScreen(4, 1),
                             {1.5f, 1, 1}, {}, 5 * 36, {});
    });
    prop("generator", {10, 9.5f}, 55, [&state]() {
        for (int i = 0; i < 3; ++i)
        {
            Drawing::DrawProp(i == 0 ? models::ModelLibrary::Scenery(models::SceneryType::Transformer)
                                     : models::ModelLibrary::Get().generator,
                              {9.5f + i * .9f, 9.7f});
            Drawing::DrawGlow(Drawing::WorldToScreen(9.5f + i * .9f, 9.7f, 28), 7,
                              state.generatorRestored ? ColorRGBA(.35f, .94f, .8f)
                                                      : ColorRGBA(.9f, .52f, .2f));
        }
        Drawing::DrawModel3D(models::ModelLibrary::Get().console, Drawing::WorldToScreen(10.5f, 10.5f),
                             {.7f, .7f, .7f}, {}, 21 * 36, {});
        Drawing::DrawProp(models::ModelLibrary::Get().breaker, {12.4f, 10.5f});
        // Even after isolation, the one remaining gauge repeats the recorded pulse.
        float pulse = .5f + .5f * std::sin(Game::Get().GetState().animationTimeSeconds * 2.4f);
        Drawing::DrawGlow(Drawing::WorldToScreen(12.4f, 10.5f, 31), 8, {1, .67f, .28f, .35f + pulse * .65f});
    });
    prop("workbench", {9.5f, 13.5f}, 36, [&state]() {
        Drawing::DrawProp(models::ModelLibrary::Get().table, {9.5f, 13.5f});
        Drawing::DrawProp(models::ModelLibrary::Get().lantern, {9.5f, 13.5f}, {.7f, .7f, .7f});
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
            Drawing::DrawProp(models::ModelLibrary::Scenery(models::SceneryType::SurveyTripod), p);
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
        Drawing::DrawProp(models::ModelLibrary::Get().table, {24.5f, 21.7f});
        if (state.portableLightRepaired)
        {
            Drawing::DrawProp(models::ModelLibrary::Get().lantern, {24.5f, 21.6f});
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
                                    npc.combat.muzzleFlash, npc.combat.health <= 0, 2.1f,
                                    npc.combat.pistol.IsReloading() ? npc.combat.pistol.GetReloadProgress()
                                                                    : -1);
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
        PlaceWorldActor(
            (ChapterOneLevel::IsOptionalConversation(point.action) ? "conversation/" : "objective/") +
                std::to_string(int(point.action)),
            point.position.x, point.position.y,
            WorldBounds(point.position.x, point.position.y, 0, 0, 70, 230), 5, 0, [point]() {
                Drawing::DrawInteractionMarker(point.position, point.label,
                                               ChapterOneLevel::IsOptionalConversation(point.action)
                                                   ? ColorRGBA(.4f, .85f, .8f)
                                                   : ColorRGBA(.9f, .78f, .4f));
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
// BackgroundScene / HudScene ? screen-space compositions
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
    // HUD labels and panels must retain painter order; deferring labels with 3D meshes
    // would draw ordinary HUD text over dialogue/journal panels.
    EndPlacement();
    for (const auto *actor : GetGraph().CollectVisible({0, 0, float(kCanvasWidth), float(kCanvasHeight)}))
    {
        actor->Draw();
    }
}

// ============================================================================
// PlanetScene : Scene ? main-thread terrain model/GPU ownership
// ============================================================================

namespace
{
world::Vector3d SurfaceProjection(const world::SurfacePoint &point, const world::TangentFrame &frame,
                                  double radius)
{
    auto p = world::SurfaceGeometry::Cartesian(radius, point),
         o = world::SurfaceGeometry::Cartesian(radius, frame.origin);
    world::Vector3d delta{p.x - o.x, p.y - o.y, p.z - o.z};
    return {world::SurfaceGeometry::Dot(delta, frame.right),
            world::SurfaceGeometry::Dot(delta, frame.forward),
            world::SurfaceGeometry::Dot(delta, frame.origin.normal)};
}
std::array<float, 4> TerrainColor(world::Biome biome)
{
    switch (biome)
    {
    case world::Biome::Ocean:
        return {.065f, .24f, .32f, 1};
    case world::Biome::Coast:
        return {.52f, .51f, .35f, 1};
    case world::Biome::Forest:
        return {.20f, .36f, .29f, 1};
    case world::Biome::Desert:
        return {.53f, .43f, .30f, 1};
    case world::Biome::Rocky:
        return {.40f, .42f, .43f, 1};
    case world::Biome::Alpine:
        return {.52f, .57f, .58f, 1};
    case world::Biome::Ice:
        return {.74f, .86f, .88f, 1};
    default:
        return {.31f, .43f, .31f, 1};
    }
}
std::unique_ptr<models::Model> SurfaceModel(const world::SurfaceChunk &chunk, double radius, bool skirts)
{
    auto model = std::make_unique<models::Model>();
    model->ground = true;
    model->space = models::CoordinateSpace::World;
    auto triangle = [&](std::array<models::Point, 3> points, std::array<models::Point, 3> normals,
                        std::array<float, 4> color) {
        models::Part part;
        part.count = 3;
        part.color = color;
        part.surface = models::Surface::Default;
        for (unsigned i = 0; i < 3; ++i)
        {
            part.points[i] = points[i];
            part.normals[i] = normals[i];
        }
        model->parts.push_back(part);
    };
    std::vector<models::Point> positions, normals;
    for (const auto &vertex : chunk.vertices)
    {
        auto point = vertex.point;
        point.altitudeMeters = (std::max)(world::PlanetTerrain::SeaLevelMeters, point.altitudeMeters);
        auto v = SurfaceProjection(point, chunk.frame, radius);
        auto n =
            vertex.point.altitudeMeters < world::PlanetTerrain::SeaLevelMeters ? point.normal : vertex.normal;
        positions.push_back({float(v.x / 2), float(v.y / 2), float(v.z / 2)});
        normals.push_back({float(world::SurfaceGeometry::Dot(n, chunk.frame.right)),
                           float(world::SurfaceGeometry::Dot(n, chunk.frame.forward)),
                           float(world::SurfaceGeometry::Dot(n, chunk.frame.origin.normal))});
    }
    auto add = [&](unsigned a, unsigned b, unsigned c) {
        auto color = TerrainColor(chunk.vertices[a].biome);
        triangle({positions[a], positions[b], positions[c]}, {normals[a], normals[b], normals[c]}, color);
    };
    for (unsigned y = 0; y < chunk.cells; ++y)
    {
        for (unsigned x = 0; x < chunk.cells; ++x)
        {
            unsigned a = y * (chunk.cells + 1) + x, b = a + 1, c = a + chunk.cells + 1, d = c + 1;
            add(a, b, c);
            add(b, d, c);
        }
    }
    if (skirts)
    {
        for (unsigned edge = 0; edge < 4; ++edge)
        {
            for (unsigned i = 0; i < chunk.cells; ++i)
            {
                unsigned a = edge == 0   ? i
                             : edge == 1 ? i * (chunk.cells + 1) + chunk.cells
                             : edge == 2 ? chunk.cells * (chunk.cells + 1) + i
                                         : i * (chunk.cells + 1);
                unsigned b = a + (edge % 2 ? chunk.cells + 1 : 1);
                auto lowA = positions[a], lowB = positions[b];
                lowA.z -= 1;
                lowB.z -= 1;
                auto n = normals[a];
                auto color = TerrainColor(chunk.vertices[a].biome);
                triangle({positions[a], lowA, positions[b]}, {n, n, n}, color);
                triangle({positions[b], lowA, lowB}, {n, n, n}, color);
            }
        }
    }
    for (const auto &road : chunk.roads)
    {
        for (std::size_t i = 1; i < road.points.size(); ++i)
        {
            auto a = SurfaceProjection(road.points[i - 1], chunk.frame, radius),
                 b = SurfaceProjection(road.points[i], chunk.frame, radius);
            double length = std::hypot(b.x - a.x, b.y - a.y);
            if (length < .01)
            {
                continue;
            }
            auto strip = [&](double width, double offset, double lift, std::array<float, 4> color) {
                double nx = -(b.y - a.y) / length, ny = (b.x - a.x) / length;
                double left = offset + width * .5, right = offset - width * .5;
                models::Point p{float((a.x + nx * left) / 2), float((a.y + ny * left) / 2),
                                float((a.z + lift) / 2)},
                    q{float((b.x + nx * left) / 2), float((b.y + ny * left) / 2), float((b.z + lift) / 2)},
                    r{float((a.x + nx * right) / 2), float((a.y + ny * right) / 2), float((a.z + lift) / 2)},
                    s{float((b.x + nx * right) / 2), float((b.y + ny * right) / 2), float((b.z + lift) / 2)},
                    n{0, 0, 1};
                triangle({p, r, q}, {n, n, n}, color);
                triangle({q, r, s}, {n, n, n}, color);
            };
            if (road.sidewalkMeters > 0)
            {
                strip(road.widthMeters + road.sidewalkMeters * 2, 0, 0, {.38f, .43f, .43f, 1});
            }
            strip(road.widthMeters, 0, .015, {.16f, .2f, .23f, 1});
            if (road.sidewalkMeters > 0)
            {
                strip(.12, 0, .03, {.82f, .71f, .36f, 1});
                for (unsigned lane = 1; lane < road.lanes; ++lane)
                {
                    if (lane * 2 == road.lanes)
                    {
                        continue;
                    }
                    strip(.09, -road.widthMeters * .5 + lane * 3., .03, {.61f, .68f, .68f, 1});
                }
            }
        }
    }
    model->count = std::uint32_t(model->parts.size());
    return model;
}
void DrawSurfaceModel(const models::Model &model, const world::TangentFrame &frame,
                      const world::SurfaceSession &session, float lower = 0)
{
    const auto &origin = session.Origin();
    auto p = SurfaceProjection(frame.origin, origin, session.Planet().radiusMeters);
    auto x = world::SurfaceGeometry::TransformDirection({1, 0, 0}, frame, origin);
    auto y = world::SurfaceGeometry::TransformDirection({0, 1, 0}, frame, origin);
    auto z = world::SurfaceGeometry::TransformDirection({0, 0, 1}, frame, origin);
    models::Transform3D transform;
    transform.yaw = float(std::atan2(x.y, x.x));
    transform.pitch = float(std::asin(std::clamp(-x.z, -1., 1.)));
    transform.roll = float(std::atan2(y.z, z.z));
    transform.position.z = float(p.z / 2) + lower;
    float px = float(p.x / 2), py = float(p.y / 2);
    Drawing::DrawModel3D(model, Drawing::WorldToScreen(px, py), {1, 1, 1}, transform, (px + py) * 36, {});
}
const models::Model &StructureModel(world::ContentForm form, std::uint16_t variant,
                                    const std::string &assetName)
{
    using F = world::ContentForm;
    using B = models::BuildingType;
    using S = models::SceneryType;
    auto &library = models::ModelLibrary::Get();
    switch (form)
    {
    case F::Building:
        return library.buildings.at(variant);
    case F::Scenery:
        return models::ModelLibrary::Scenery(S(variant));
    case F::Console:
        return library.console;
    case F::Breaker:
        return library.breaker;
    case F::Beacon:
        return library.beacon;
    case F::TransitMesh:
        return models::ModelLibrary::Transit(assetName);
    case F::Aircraft:
        return models::ModelLibrary::Aircraft(models::AircraftType(variant / 2), variant % 2 != 0);
    case F::Generator:
        return library.generator;
    case F::Table:
        return library.table;
    case F::Lantern:
        return library.lantern;
    case F::Salvage:
        return library.salvage;
    case F::Reeds:
        return library.reeds;
    case F::Rock:
        return library.rock;
    case F::Vehicle:
        return library.vehicles.at(variant).body;
    case F::Habitat:
        return library.buildings[size_t(B::Habitat)];
    case F::Terrace:
        return library.buildings[size_t(B::Terraced)];
    case F::Workshop:
        return library.buildings[size_t(B::Workshop)];
    case F::Dome:
        return library.buildings[size_t(B::Dome)];
    case F::Spire:
        return library.buildings[size_t(B::SpireHelix)];
    case F::TwinBridge:
        return library.buildings[size_t(B::TwinBridge)];
    case F::Arcology:
        return library.buildings[size_t(B::ArcologyTerrace)];
    case F::Transit:
        return library.buildings[size_t(B::TransitAtrium)];
    case F::Courtyard:
        return library.buildings[size_t(B::CourtyardHabitat)];
    case F::Cargo:
        return models::ModelLibrary::Scenery(S::CargoContainer);
    case F::Relay:
        return models::ModelLibrary::Scenery(S::CommRelay);
    case F::Solar:
        return models::ModelLibrary::Scenery(S::SolarArray);
    case F::WaterTank:
        return models::ModelLibrary::Scenery(S::WaterTank);
    case F::Conveyor:
        return models::ModelLibrary::Scenery(S::MiningConveyor);
    case F::Monolith:
        return models::ModelLibrary::Scenery(S::ObserverMonolith);
    case F::Arch:
        return models::ModelLibrary::Scenery(S::FracturedArch);
    default:
        return models::ModelLibrary::Scenery(S::FieldShelter);
    }
}
void DrawSurfaceStructure(const world::SurfaceStructure &structure, const world::SurfaceSession &session)
{
    const auto &model = StructureModel(structure.form, structure.variant, structure.assetName);
    if (!model.resourceId)
    {
        return;
    }
    const auto &asset = models::ResourceAssets::Get(model.resourceId);
    if (asset.maximum.x <= asset.minimum.x || asset.maximum.y <= asset.minimum.y ||
        asset.maximum.z <= asset.minimum.z)
    {
        return;
    }
    // Prototype only: production parcels need native-sized buildings/modules
    // (Resource/RENDER_RESOURCE_REQUEST.md).
    models::Point scale = {float(structure.sizeMeters.x / 2 / (asset.maximum.x - asset.minimum.x)),
                           float(structure.sizeMeters.y / 2 / (asset.maximum.y - asset.minimum.y)),
                           float(structure.sizeMeters.z / 2 / (asset.maximum.z - asset.minimum.z))};
    const auto &origin = session.Origin();
    auto x = world::SurfaceGeometry::TransformDirection({std::cos(structure.yaw), std::sin(structure.yaw), 0},
                                                        structure.frame, origin);
    auto y = world::SurfaceGeometry::TransformDirection(
        {-std::sin(structure.yaw), std::cos(structure.yaw), 0}, structure.frame, origin);
    auto z = world::SurfaceGeometry::TransformDirection({0, 0, 1}, structure.frame, origin);
    auto p = SurfaceProjection(structure.frame.origin, origin, session.Planet().radiusMeters);
    models::Transform3D transform;
    transform.yaw = float(std::atan2(x.y, x.x));
    transform.pitch = float(std::asin(std::clamp(-x.z, -1., 1.)));
    transform.roll = float(std::atan2(y.z, z.z));
    // Resource pivots differ: centre the real mesh bounds on the same rectangle used by collision.
    const double baseLift = structure.BaseClearanceMeters();
    auto offset = transform.Apply({-(asset.minimum.x + asset.maximum.x) * .5f * scale.x,
                                   -(asset.minimum.y + asset.maximum.y) * .5f * scale.y,
                                   -asset.minimum.z * scale.z + float(baseLift / 2)});
    transform.position = {offset.x, offset.y, offset.z + float(p.z / 2)};
    float px = float(p.x / 2), py = float(p.y / 2);
    models::Palette palette;
    palette.body = {.28f + float(structure.appearance % 5) * .035f,
                    .38f + float((structure.appearance >> 4) % 4) * .025f, .44f, 1};
    palette.accent = {.35f, .63f, .68f, 1};
    palette.emission = {.25f, .78f, .87f, 1};
    if (structure.palette)
    {
        palette.body = structure.palette->body;
        palette.accent = structure.palette->accent;
        palette.emission = structure.palette->emission;
    }
    if (structure.regionId == "frontier" && structure.form == world::ContentForm::Vehicle &&
        GameLevels::Chapter().GetState().vehicleRepaired)
    {
        palette.body = {.42f, .65f, .65f, 1};
    }
    auto screen = Drawing::WorldToScreen(px, py, float(p.z / 2) * 44);
    auto player = Drawing::WorldToScreen(Game::Get().GetState().playerPosition.x,
                                         Game::Get().GetState().playerPosition.y,
                                         float(session.Local(session.Pose().origin).z / 2) * 44);
    float width = float((structure.sizeMeters.x + structure.sizeMeters.y) * 9);
    if (screen.y > player.y - 12 && player.y > screen.y - float(structure.sizeMeters.z * 22) - 20 &&
        std::fabs(screen.x - player.x) < width)
    {
        palette.body[3] = palette.accent[3] = palette.emission[3] = .28f;
    }
    if (structure.foundationMeters > .05)
    {
        auto foundation = transform;
        foundation.position = {0, 0, float(p.z / 2)};
        auto shift = foundation.Apply({-float(structure.sizeMeters.x / 4), -float(structure.sizeMeters.y / 4),
                                       -float(structure.foundationMeters / 2)});
        foundation.position = shift;
        Drawing::DrawModel3D(models::ModelLibrary::Get().box, Drawing::WorldToScreen(px, py),
                             {float(structure.sizeMeters.x / 2), float(structure.sizeMeters.y / 2),
                              float((structure.foundationMeters + structure.FoundationTopMeters()) / 2)},
                             foundation, (px + py) * 36, palette);
    }
    Drawing::DrawModel3D(model, Drawing::WorldToScreen(px, py), scale, transform, (px + py) * 36, palette);
}
void DrawPlanetTraffic(const world::SurfaceSession &session)
{
    const auto traffic = Game::Get().SurfaceTraffic();
    if (!traffic)
    {
        return;
    }
    const auto &state = Game::Get().GetState();
    for (const auto &actor : traffic->State().actors)
    {
        if (actor.city >= 0 || traffic->Outside(actor, state, session, 80))
        {
            continue;
        }
        auto ground = actor.position;
        ground.altitudeMeters -= actor.altitudeMeters;
        const auto local = session.Local(ground);
        const float height = float(local.z / 2);
        const Vector2 position{float(local.x / 2), float(local.y / 2)};
        const float yaw =
            float(std::atan2(world::SurfaceGeometry::Dot(actor.heading, session.Origin().forward),
                             world::SurfaceGeometry::Dot(actor.heading, session.Origin().right)));
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
        if (actor.airborne)
        {
            Drawing::DrawWorldAircraft(
                position, float(actor.altitudeMeters / 2),
                models::AircraftType((actor.id / 2) % size_t(models::AircraftType::Count)), actor.id % 2 != 0,
                yaw, actor.bank, actor.health <= 0,
                VehicleWreck::Opacity(actor.wreckSeconds, actor.altitudeMeters == 0));
        }
        else
        {
            models::Palette palette;
            palette.body = {.25f + float(actor.id % 4) * .1f, .4f, .55f, 1};
            if (actor.health <= 0)
            {
                palette = VehicleWreck::Appearance(VehicleWreck::Opacity(actor.wreckSeconds, true));
            }
            Drawing::DrawWorldVehicle(position, 3.3f, 1, models::VehicleType(actor.id % 4), palette,
                                      float(actor.speedMeters / 20), yaw, actor.health <= 0);
        }
        renderqueue::PopMatrix();
    }
}
void DrawPlanetCommunity(const world::SurfaceSession &session)
{
    const auto *community = Game::Get().SurfaceCommunity();
    if (!community)
    {
        return;
    }
    const auto &hero = session.Pose().origin;
    auto withinDrawRange = [&](const world::SurfacePoint &point) {
        return world::SurfaceGeometry::Distance(session.Planet().radiusMeters, hero, point) < 120;
    };
    static constexpr models::CharacterType types[] = {
        models::CharacterType::Engineer, models::CharacterType::Miner, models::CharacterType::Medic,
        models::CharacterType::Researcher};
    size_t drawn = 0;
    for (const auto &site : community->State().sites)
    {
        for (size_t i = 0; i < site.residents.size(); ++i)
        {
            const auto &person = site.residents[i];
            if (!withinDrawRange(person.position) || (person.health <= 0 && person.corpseSeconds >= 30) ||
                drawn >= PlanetCommunity::ActiveLimit)
            {
                continue;
            }
            ++drawn;
            const auto p = session.Local(person.position);
            const Vector2 position{float(p.x / 2), float(p.y / 2)};
            const float height = float(p.z / 2);
            const float yaw =
                float(std::atan2(world::SurfaceGeometry::Dot(person.heading, session.Origin().forward),
                                 world::SurfaceGeometry::Dot(person.heading, session.Origin().right)));
            renderqueue::PushMatrix();
            renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
            const ColorRGBA coat = person.hitFlash > 0
                                       ? ColorRGBA(1, .3f, .25f)
                                       : ColorRGBA(.3f + float(person.appearance % 5) * .08f, .5f, .6f);
            Drawing::DrawPerson(
                position, coat, false, types[i % 4], person.speed > .1f, yaw,
                models::Gender(person.appearance % 2),
                models::HairStyle((person.appearance >> 8) % size_t(models::HairStyle::Count)), false, 0,
                person.health <= 0, person.speed / 2);
            renderqueue::PopMatrix();
        }
        auto terminal = [&](const world::SurfacePoint &point, const char *label, bool mark) {
            if (!withinDrawRange(point))
            {
                return;
            }
            const auto p = session.Local(point);
            const Vector2 position{float(p.x / 2), float(p.y / 2)};
            const float height = float(p.z / 2);
            renderqueue::PushMatrix();
            renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
            models::Palette palette;
            palette.emission = {.25f, .85f, .9f, 1};
            Drawing::DrawProp(models::ModelLibrary::Scenery(models::SceneryType::Wayfinding), position,
                              {1, 1, 1}, palette);
            if (mark)
            {
                Drawing::DrawInteractionMarker(position, label, {1, .8f, .35f});
            }
            renderqueue::PopMatrix();
        };
        terminal(site.contact, site.task == 2 ? "현장 기록 제출" : "선택 의뢰",
                 site.task != 1 && site.task != 3);
        terminal(site.target, PlanetCommunity::TaskName(site), site.task == 1);
    }
}
void DrawSurfaceCities(const world::SurfaceSession &session)
{
    auto cities = Game::Get().SurfaceCities();
    if (!cities)
    {
        return;
    }
    const auto &state = Game::Get().GetState();
    auto place = [&](int city, Vector2 legacy) -> std::optional<world::Vector3d> {
        if (cities->Status(city).band != CitySimulationBand::Detailed)
        {
            return std::nullopt;
        }
        auto point = cities->Point(session, city, legacy);
        if (!point || world::SurfaceGeometry::Distance(session.Planet().radiusMeters, *point,
                                                       session.Pose().origin) > 100)
        {
            return std::nullopt;
        }
        return session.Local(*point);
    };
    auto person = [&](int city, Vector2 legacy, ColorRGBA coat, models::CharacterType type, bool walking,
                      float yaw, models::Gender gender, models::HairStyle hair, bool downed,
                      const std::string &name) {
        auto point = place(city, legacy);
        if (!point)
        {
            return;
        }
        Vector2 local{float(point->x / 2), float(point->y / 2)};
        float height = float(point->z / 2);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
        Drawing::DrawPerson(local, coat, false, type, walking, cities->Yaw(session, city, yaw), gender, hair,
                            false, 0, downed);
        if (Game::Distance(local, state.playerPosition) < 2.2f)
        {
            auto label = Drawing::WorldToScreen(local.x, local.y, 62);
            Drawing::DrawLabel(label.x - 20, label.y, name);
        }
        renderqueue::PopMatrix();
    };
    for (size_t i = 0; i < state.residents.size(); ++i)
    {
        const auto &r = state.residents[i];
        if (i == Overworld::kKainResidentSlot && GameLevels::Chapter().GetState().kainJoined)
        {
            continue;
        }
        person(r.cityIndex, r.position, {r.coat[0], r.coat[1], r.coat[2]}, r.type, r.facing.walking,
               r.facing.yaw, r.gender, r.hair, r.health <= 0, r.name);
    }
    if (!Game::Get().SurfaceParty() || !Game::Get().SurfaceParty()->Joined(CompanionId::Ria))
    {
        person(0, kLiaPosition, {.89f, .52f, .28f}, models::CharacterType::Engineer, false, .7854f,
               models::Gender::Female, models::HairStyle::Ponytail,
               CivicJustice::State().landmarkHealth[0] <= 0, "리아");
    }
    person(0, kMaraPosition, {.48f, .4f, .64f}, models::CharacterType::Medic, false, .7854f,
           models::Gender::Female, models::HairStyle::Bun, CivicJustice::State().landmarkHealth[1] <= 0,
           "마라");
    if (GameLevels::Chapter().GetState().kainJoined && !Game::Get().SurfaceParty())
    {
        person(0, kCityKainPosition, {.38f, .53f, .66f}, models::CharacterType::Guard, false, .7854f,
               models::Gender::Male, models::HairStyle::Buzz, CivicJustice::State().landmarkHealth[2] <= 0,
               "카인");
    }
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        if (cities->Status(city).band != CitySimulationBand::Detailed)
        {
            continue;
        }
        const auto &traffic = cities->Traffic(city);
        for (size_t i = 0; i < traffic.Vehicles().size(); ++i)
        {
            const auto &car = traffic.Vehicles()[i];
            auto point = place(city, traffic.RenderPosition(i));
            if (!point)
            {
                continue;
            }
            models::Palette palette;
            palette.body = {.25f + float(car.id % 4) * .1f, .4f, .55f, 1};
            if (car.health <= 0)
            {
                palette = VehicleWreck::Appearance(VehicleWreck::Opacity(car.wreckSeconds, true));
            }
            Drawing::DrawWorldVehicle({float(point->x / 2), float(point->y / 2)},
                                      float(point->z / 2) * 44 + 3.3f, 1, models::VehicleType(car.id % 4),
                                      palette, car.speed / 10,
                                      cities->Yaw(session, city, traffic.RenderYaw(i)), car.health <= 0);
        }
        const auto &net = RoadNetwork::Get();
        for (size_t junction = 0; junction < net.junctions.size(); ++junction)
        {
            const auto &j = net.junctions[junction];
            if (!net.IsSignalized(int(junction)) ||
                Game::Distance(j.position, Overworld::Cities()[size_t(city)].center) > 110)
            {
                continue;
            }
            auto point = place(city, j.position);
            if (!point)
            {
                continue;
            }
            auto strip = [&](Vector2 a, Vector2 b, float width, ColorRGBA color, bool glow) {
                auto first = cities->Local(session, city, a), last = cities->Local(session, city, b);
                if (!first || !last)
                {
                    return;
                }
                float height = float(point->z / 2);
                renderqueue::PushMatrix();
                renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
                if (glow)
                {
                    Drawing::DrawHologramStrip(*first, *last, width, color, .085f);
                }
                else
                {
                    Drawing::DrawGroundStrip(*first, *last, width, color, .07f);
                }
                renderqueue::PopMatrix();
            };
            auto add = [](Vector2 a, Vector2 b, float scale) {
                return Vector2{a.x + b.x * scale, a.y + b.y * scale};
            };
            for (int arm : j.outgoing)
            {
                const auto &out = net.lanes[size_t(arm)];
                if (out.slot != 0 || !TrafficSystem::CoreLane(arm, city))
                {
                    continue;
                }
                auto d = out.direction, normal = RoadNetwork::RightOf(d);
                float half = net.roads[size_t(out.road)].lanes * RoadNetwork::kLaneWidth * .5f;
                auto across = add(j.position, d, j.radius + 1);
                for (float offset = -half + .2f; offset < half; offset += .5f)
                {
                    auto stripe = add(across, normal, offset);
                    strip(add(stripe, d, -.8f), add(stripe, d, .8f), .22f, {.65f, .74f, .73f, 1}, false);
                }
                bool walk = traffic.PedestrianGreen(int(junction), out.road);
                ColorRGBA pedestrian = walk ? ColorRGBA(.15f, 1, .78f) : ColorRGBA(.95f, .21f, .28f);
                for (float edge : {-1.f, 1.f})
                {
                    auto center = add(across, d, .9f * edge);
                    strip(add(center, normal, -half + .08f), add(center, normal, half - .08f), .1f,
                          pedestrian, true);
                }
                for (size_t index = 0; index < net.lanes.size(); ++index)
                {
                    const auto &incoming = net.lanes[index];
                    if (incoming.road != out.road || incoming.to != int(junction) ||
                        !TrafficSystem::CoreLane(int(index), city))
                    {
                        continue;
                    }
                    bool left = traffic.Light(int(index), TrafficTurn::Left) == TrafficLight::Green &&
                                traffic.Light(int(index), TrafficTurn::Straight) != TrafficLight::Green;
                    auto light =
                        left ? TrafficLight::Green : traffic.Light(int(index), TrafficTurn::Straight);
                    ColorRGBA color = light == TrafficLight::Green   ? ColorRGBA(.2f, 1, .78f)
                                      : light == TrafficLight::Amber ? ColorRGBA(1, .7f, .15f)
                                                                     : ColorRGBA(1, .2f, .28f);
                    auto stop = add(add(j.position, d, j.radius + 2.2f), normal,
                                    -(incoming.slot + .5f) * RoadNetwork::kLaneWidth);
                    strip(add(stop, normal, -.55f), add(stop, normal, .55f), .15f, color, true);
                    if (left)
                    {
                        if (auto at = cities->Local(session, city, stop))
                        {
                            auto label = Drawing::WorldToScreen(at->x, at->y, float(point->z / 2) * 44 + 24);
                            Drawing::DrawLabel(label.x - 16, label.y, "좌회전", {.2f, 1, .75f});
                        }
                    }
                }
            }
        }
    }
    auto drawWalkway = [&](const TransitPlacement &module, int city) {
        const auto point = place(city, module.position);
        if (!point)
        {
            return;
        }
        const float height = float(point->z / 2);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
        Drawing::DrawProp(models::ModelLibrary::Transit(module.asset),
                          {float(point->x / 2), float(point->y / 2)}, {1, 1, 1}, {},
                          cities->Yaw(session, city, module.yaw), module.altitude);
        renderqueue::PopMatrix();
    };
    for (const auto &module : FacilityApproaches::Get().modules)
    {
        drawWalkway(module, 0);
    }
    const auto &infrastructure = CityInfrastructure::Get();
    for (size_t i = 0; i < infrastructure.modules.size(); ++i)
    {
        drawWalkway(infrastructure.modules[i], infrastructure.moduleCities[i]);
    }
    const CityInfrastructureSite *nearestInfrastructure = nullptr;
    size_t infrastructureRole = 0;
    float infrastructureDistance = 5;
    for (size_t i = 0; i < infrastructure.sites.size(); ++i)
    {
        const auto &site = infrastructure.sites[i];
        if (site.access != InfrastructureAccess::Connected)
        {
            continue;
        }
        const auto point = place(site.city, site.standing);
        if (!point)
        {
            continue;
        }
        const float distance =
            Game::Distance({float(point->x / 2), float(point->y / 2)}, state.playerPosition);
        if (distance < infrastructureDistance)
        {
            infrastructureDistance = distance;
            nearestInfrastructure = &site;
            infrastructureRole = i % 3;
        }
    }
    if (nearestInfrastructure)
    {
        const auto &site = *nearestInfrastructure;
        const auto point = place(site.city, site.standing), equipment = place(site.city, site.equipment);
        if (point && equipment)
        {
            const float height = float(point->z / 2);
            renderqueue::PushMatrix();
            renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
            DrawInfrastructureGuide(
                {float(point->x / 2), float(point->y / 2)},
                {float(equipment->x / 2), float(equipment->y / 2)},
                CityInfrastructure::Profiles()[site.city].roles[infrastructureRole].label);
            renderqueue::PopMatrix();
        }
    }
    const CityFacility *nearestFacility = nullptr;
    float facilityDistance = 5;
    for (const auto &facility : CityFacilities::All())
    {
        const auto p = place(0, facility.exterior);
        if (!p)
        {
            continue;
        }
        const float distance = Game::Distance({float(p->x / 2), float(p->y / 2)}, state.playerPosition);
        if (distance < facilityDistance)
        {
            nearestFacility = &facility;
            facilityDistance = distance;
        }
    }
    if (nearestFacility)
    {
        const auto &facility = *nearestFacility;
        const auto p = *place(0, facility.exterior);
        const Vector2 local{float(p.x / 2), float(p.y / 2)};
        const float height = float(p.z / 2);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * 44, height * (1296.f / 44));
        if (facility.hasInterior)
        {
            if (const auto pad = place(0, CityFacilities::StandingPoint(facility)))
            {
                Drawing::DrawHologramStrip({float(pad->x / 2), float(pad->y / 2)}, local, .3f,
                                           {.3f, .8f, .85f}, .07f);
            }
        }
        if (!Game::Get().IsQuestInteraction(facility.target))
        {
            Drawing::DrawInteractionMarker(local, facility.label, {.4f, .85f, .9f});
        }
        renderqueue::PopMatrix();
    }
    struct Marker
    {
        Vector2 position;
        InteractionTarget target;
        const char *label;
    };
    for (auto marker : {Marker{kHomeDoor, InteractionTarget::HomeEntrance, "집 단말"},
                        Marker{kLiaPosition, InteractionTarget::Lia, "리아"},
                        Marker{kMaraPosition, InteractionTarget::Mara, "마라 / 선택 의뢰"},
                        Marker{kRelayPosition, InteractionTarget::PowerRelay, "전력 장치"},
                        Marker{kBeaconPosition, InteractionTarget::GateBeacon, "전망대"},
                        Marker{kCityReceiverPosition, InteractionTarget::CityReceiver, "도시 수신망"}})
    {
        if (marker.target == InteractionTarget::Lia && Game::Get().SurfaceParty() &&
            Game::Get().SurfaceParty()->Joined(CompanionId::Ria))
        {
            continue;
        }
        if (!Game::Get().IsQuestInteraction(marker.target))
        {
            continue;
        }
        auto point = place(0, marker.position);
        if (!point)
        {
            continue;
        }
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -float(point->z / 2) * 44, float(point->z / 2) * (1296.f / 44));
        Drawing::DrawInteractionMarker({float(point->x / 2), float(point->y / 2)}, marker.label,
                                       {.4f, .9f, .85f});
        renderqueue::PopMatrix();
    }
}
void DrawPlanetParty(const world::SurfaceSession &session)
{
    const auto *party = Game::Get().SurfaceParty();
    if (!party)
    {
        return;
    }
    const auto &members = party->State().members;
    for (size_t i = 0; i < members.size(); ++i)
    {
        const auto &m = members[i];
        if (!m.joined || m.place != CompanionPlace::Surface ||
            world::SurfaceGeometry::Distance(session.Planet().radiusMeters, m.position,
                                             session.Pose().origin) > 260)
        {
            continue;
        }
        auto local = session.Local(m.position);
        Vector2 position{float(local.x / 2), float(local.y / 2)};
        float yaw = float(std::atan2(world::SurfaceGeometry::Dot(m.heading, session.Origin().forward),
                                     world::SurfaceGeometry::Dot(m.heading, session.Origin().right)));
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -float(local.z / 2) * 44, float(local.z / 2) * (1296.f / 44));
        Drawing::DrawPerson(position, i == 0 ? ColorRGBA(.9f, .58f, .3f) : ColorRGBA(.38f, .53f, .66f), false,
                            i == 0 ? models::CharacterType::Engineer : models::CharacterType::Guard,
                            m.walking, yaw, i == 0 ? models::Gender::Female : models::Gender::Male,
                            i == 0 ? models::HairStyle::Ponytail : models::HairStyle::Buzz,
                            m.combat.engaged && m.combat.health > 0, m.combat.muzzleFlash,
                            m.combat.health <= 0);
        if (Game::Distance(position, Game::Get().GetState().playerPosition) < 4)
        {
            auto at = Drawing::WorldToScreen(position.x, position.y, 62);
            Drawing::DrawLabel(at.x - 20, at.y, i == 0 ? "리아" : "카인");
            if (m.combat.health > 0 &&
                Game::Get().IsQuestInteraction(i == 0 ? InteractionTarget::Lia : InteractionTarget::Kain))
            {
                Drawing::DrawInteractionMarker(position, i == 0 ? "리아" : "카인", {.4f, .9f, .85f});
            }
        }
        renderqueue::PopMatrix();
    }
}
std::unique_ptr<models::Model> FrontierGround(const world::GeneratedPlanet &planet)
{
    const auto &region = planet.Region("frontier");
    const auto &state = GameLevels::Chapter().GetState();
    const auto &core = *planet.cityCores->Find("frontier");
    auto model = std::make_unique<models::Model>();
    model->ground = true;
    model->space = models::CoordinateSpace::World;
    auto outside = TerrainColor(region.biome);
    for (int y = 0; y < kFrontierHeight; ++y)
    {
        for (int x = 0; x < kFrontierWidth; ++x)
        {
            auto tile = state.tiles[size_t(y * kFrontierWidth + x)];
            std::array<float, 4> color =
                tile == FrontierTile::Grass   ? std::array<float, 4>{.24f, .37f, .26f, 1}
                : tile == FrontierTile::Sand  ? std::array<float, 4>{.55f, .44f, .28f, 1}
                : tile == FrontierTile::Canal ? std::array<float, 4>{.12f, .15f, .14f, 1}
                : tile == FrontierTile::Road  ? std::array<float, 4>{.28f, .30f, .30f, 1}
                                              : std::array<float, 4>{.31f, .34f, .35f, 1};
            float edge = std::clamp(
                float((std::min)({x, y, kFrontierWidth - 1 - x, kFrontierHeight - 1 - y})) / 4, 0.f, 1.f);
            if (tile == FrontierTile::Grass || tile == FrontierTile::Sand)
            {
                for (unsigned i = 0; i < 3; ++i)
                {
                    color[i] = outside[i] + (color[i] - outside[i]) * edge;
                }
            }
            for (auto indices : {std::array<unsigned, 3>{0, 1, 2}, std::array<unsigned, 3>{0, 2, 3}})
            {
                constexpr int corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                models::Part part;
                part.count = 3;
                part.color = color;
                part.surface = models::Surface::Default;
                for (unsigned i = 0; i < 3; ++i)
                {
                    auto p = world::SurfaceGeometry::FromLocal(
                        region.frame,
                        {(x + corners[indices[i]][0] - kFrontierEntry.x) * 2,
                         (y + corners[indices[i]][1] - kFrontierEntry.y) * 2, 0},
                        planet.terrain.Planet().radiusMeters);
                    p.altitudeMeters = core.elevationMeters + .03;
                    auto local = SurfaceProjection(p, region.frame, planet.terrain.Planet().radiusMeters);
                    part.points[i] = {float(local.x / 2), float(local.y / 2), float(local.z / 2)};
                    part.normals[i] = {
                        float(world::SurfaceGeometry::Dot(p.normal, region.frame.right)),
                        float(world::SurfaceGeometry::Dot(p.normal, region.frame.forward)),
                        float(world::SurfaceGeometry::Dot(p.normal, region.frame.origin.normal))};
                }
                model->parts.push_back(part);
            }
        }
    }
    model->count = std::uint32_t(model->parts.size());
    return model;
}
void DrawSurfaceFrontier(const world::SurfaceSession &session)
{
    auto &level = GameLevels::Chapter();
    const auto &state = level.GetState();
    const auto &surface = level.Surface();
    if (!state.generated ||
        world::SurfaceGeometry::Distance(
            session.Planet().radiusMeters, session.Pose().origin,
            session.Stream().Snapshot()->planet->Region("frontier").access.position) > 260)
    {
        return;
    }
    auto height = [&](Vector2 authored) { return float(session.Local(surface.Point(authored)).z / 2); };
    auto person = [&](const FrontierPerson &npc, models::CharacterType role, models::Gender gender,
                      models::HairStyle hair, ColorRGBA color, const char *label, bool armed) {
        auto point = surface.Project(npc.position);
        if (!point)
        {
            return;
        }
        float z = height(npc.position);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -z * 44, z * (1296.f / 44));
        Drawing::DrawPerson(*point, color, false, role, npc.facing.walking,
                            surface.Yaw(npc.combat.engaged ? npc.combat.aimYaw : npc.facing.yaw), gender,
                            hair, armed && npc.combat.engaged && npc.combat.health > 0,
                            npc.combat.muzzleFlash, npc.combat.health <= 0, 2.1f,
                            npc.combat.pistol.IsReloading() ? npc.combat.pistol.GetReloadProgress() : -1);
        if (Game::Distance(*point, Game::Get().GetState().playerPosition) < 4 && label[0])
        {
            auto at = Drawing::WorldToScreen(point->x, point->y, 60);
            Drawing::DrawLabel(at.x - 20, at.y, label, color);
        }
        renderqueue::PopMatrix();
    };
    if (Game::Get().HasCompanion(CompanionId::Ria, CompanionPlace::Frontier))
    {
        person(state.ria, models::CharacterType::Engineer, models::Gender::Female,
               models::HairStyle::Ponytail, {.9f, .58f, .3f}, "리아", true);
    }
    if (!state.kainJoined || Game::Get().HasCompanion(CompanionId::Kain, CompanionPlace::Frontier))
    {
        person(state.kain, models::CharacterType::Guard, models::Gender::Male, models::HairStyle::Buzz,
               {.38f, .53f, .66f}, "카인", state.kainJoined);
    }
    for (size_t i = 0; i < state.survivors.size(); ++i)
    {
        person(state.survivors[i], i % 3 ? models::CharacterType::Engineer : models::CharacterType::Explorer,
               i % 2 ? models::Gender::Female : models::Gender::Male, models::HairStyle(i % 6),
               {.65f, .69f, .51f}, i == 0 ? "운송대 생존자" : "", false);
    }
    for (size_t i = 0; i < state.scrappers.size(); ++i)
    {
        if (!(state.negotiated && state.scrappers[i].arrived))
        {
            person(state.scrappers[i], i ? models::CharacterType::Guard : models::CharacterType::Explorer,
                   i == 1 ? models::Gender::Female : models::Gender::Male, models::HairStyle::Curls,
                   {.7f, .36f, .25f}, state.negotiated ? "철수 중" : "폐설비 점거자", true);
        }
    }
    auto glow = [&](Vector2 authored, float z, float radius, ColorRGBA color) {
        if (auto p = surface.Project(authored))
        {
            Drawing::DrawGlow(Drawing::WorldToScreen(p->x, p->y, height(authored) * 44 + z), radius, color);
        }
    };
    for (int i = 0; i < 3; ++i)
    {
        glow({9.5f + i * .9f, 9.7f}, 28, 7,
             state.generatorRestored ? ColorRGBA(.35f, .94f, .8f) : ColorRGBA(.9f, .52f, .2f));
    }
    constexpr Vector2 markers[] = {{36.5f, 13.5f}, {36.5f, 18.5f}, {29.5f, 18.5f}};
    for (unsigned i = 0; i < 3; ++i)
    {
        glow(markers[i], 34, 12,
             state.surveyMarkers > int(i) ? ColorRGBA(.3f, 1, .8f) : ColorRGBA(1, .6f, .25f, .5f));
    }
    if (state.portableLightRepaired)
    {
        glow({9.5f, 13.5f}, 35, 18, {1, .82f, .5f});
        glow({24.5f, 21.6f}, 37, 26, {1, .82f, .5f});
    }
    for (const auto &clue : level.Interactions())
    {
        if (clue.action == ChapterAction::Exit || clue.action == ChapterAction::ConvoySupply)
        {
            continue;
        }
        if (auto p = surface.Project(clue.position))
        {
            float z = height(clue.position);
            renderqueue::PushMatrix();
            renderqueue::Translate(0, -z * 44, z * (1296.f / 44));
            Drawing::DrawInteractionMarker(*p, clue.label,
                                           ChapterOneLevel::IsOptionalConversation(clue.action)
                                               ? ColorRGBA(.4f, .85f, .8f)
                                               : ColorRGBA(.9f, .78f, .4f));
            renderqueue::PopMatrix();
        }
    }
    for (const auto &tracer : state.tracers)
    {
        auto first = surface.Project(tracer.start), last = surface.Project(tracer.end);
        if (!first || !last)
        {
            continue;
        }
        Drawing::DrawLine(
            Drawing::WorldToScreen(first->x, first->y,
                                   height(tracer.start) * 44 + models::WorldScale::aimHeightPixels),
            Drawing::WorldToScreen(last->x, last->y,
                                   height(tracer.end) * 44 + models::WorldScale::aimHeightPixels),
            tracer.npcOwned ? ColorRGBA(.3f, .85f, 1, tracer.remaining / .12f)
                            : ColorRGBA(1, .74f, .3f, tracer.remaining / .12f),
            2);
    }
}
} // namespace
struct PlanetScene::Data
{
    struct Entry
    {
        std::shared_ptr<const world::SurfaceChunk> chunk;
        std::unique_ptr<models::Model> model;
    };
    std::map<world::ChunkKey, Entry> meshes;
    std::unique_ptr<models::Model> fallback;
    std::unique_ptr<models::Model> frontierGround;
    world::TangentFrame fallbackFrame;
    std::uint64_t epoch = 0;
};
PlanetScene::PlanetScene() : data(std::make_unique<Data>())
{
}
PlanetScene::~PlanetScene() = default;
void PlanetScene::ResetResources()
{
    for (const auto &entry : data->meshes)
    {
        GameRenderer::Get().Backend().ReleaseModel(*entry.second.model);
    }
    data->meshes.clear();
    if (data->frontierGround)
    {
        GameRenderer::Get().Backend().ReleaseModel(*data->frontierGround);
        data->frontierGround.reset();
    }
    if (data->fallback)
    {
        GameRenderer::Get().Backend().ReleaseModel(*data->fallback);
        data->fallback.reset();
    }
    data->epoch = 0;
}
void PlanetScene::Draw()
{
    auto started = std::chrono::steady_clock::now();
    const auto session = Game::Get().Surface();
    if (!session || !session->Ready())
    {
        return;
    }
    WorldView view;
    auto snapshot = session->Stream().Snapshot();
    auto &d = *data;
    if (d.epoch != snapshot->statistics.epoch)
    {
        ResetResources();
        d.epoch = snapshot->statistics.epoch;
    }
    std::vector<std::shared_ptr<const world::SurfaceChunk>> visible;
    for (const auto &chunk : snapshot->chunks)
    {
        if (world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin,
                                             chunk->frame.origin) < chunk->radiusMeters + 96)
        {
            visible.push_back(chunk);
        }
    }
    std::sort(visible.begin(), visible.end(), [&](const auto &a, const auto &b) {
        return world::SurfaceGeometry::Dot(a->frame.origin.normal, session->Pose().origin.normal) >
               world::SurfaceGeometry::Dot(b->frame.origin.normal, session->Pose().origin.normal);
    });
    if (visible.size() > 32)
    {
        visible.resize(32);
    }
    std::set<world::ChunkKey> retained;
    for (const auto &chunk : visible)
    {
        retained.insert(chunk->key);
    }
    for (auto it = d.meshes.begin(); it != d.meshes.end();)
    {
        if (!retained.count(it->first))
        {
            GameRenderer::Get().Backend().ReleaseModel(*it->second.model);
            it = d.meshes.erase(it);
        }
        else
        {
            ++it;
        }
    }
    unsigned uploads = 0;
    // Small, bounded safety patch is always available while CPU chunks or GPU uploads catch up.
    if (!d.fallback || world::SurfaceGeometry::Distance(session->Planet().radiusMeters,
                                                        d.fallbackFrame.origin, session->Pose().origin) > 16)
    {
        world::SurfaceChunk coarse;
        coarse.frame = session->Pose();
        coarse.cells = 8;
        for (unsigned y = 0; y <= 8; ++y)
        {
            for (unsigned x = 0; x <= 8; ++x)
            {
                auto point =
                    world::SurfaceGeometry::Advance(coarse.frame, (double(x) - 4) * 16, (double(y) - 4) * 16,
                                                    session->Planet().radiusMeters)
                        .origin;
                point.altitudeMeters = snapshot->planet->terrain.Height(point);
                auto biome = snapshot->planet->terrain.Environment(point, point.altitudeMeters, 0).biome;
                coarse.vertices.push_back({point, point.normal, biome});
            }
        }
        if (d.fallback)
        {
            GameRenderer::Get().Backend().ReleaseModel(*d.fallback);
        }
        d.fallback = SurfaceModel(coarse, session->Planet().radiusMeters, false);
        d.fallbackFrame = coarse.frame;
        ++uploads;
    }
    for (const auto &chunk : visible)
    {
        if (uploads >= 2 || std::chrono::steady_clock::now() - started > std::chrono::milliseconds(2))
        {
            break;
        }
        if (d.meshes.count(chunk->key))
        {
            continue;
        }
        auto model = SurfaceModel(*chunk, session->Planet().radiusMeters, true);
        d.meshes.emplace(chunk->key, Data::Entry{chunk, std::move(model)});
        ++uploads;
    }
    auto &backend = GameRenderer::Get().Backend();
    backend.BeginModels();
    DrawSurfaceModel(*d.fallback, d.fallbackFrame, *session, -.08f);
    for (const auto &chunk : visible)
    {
        auto it = d.meshes.find(chunk->key);
        if (it != d.meshes.end())
        {
            DrawSurfaceModel(*it->second.model, it->second.chunk->frame, *session);
        }
    }
    const auto &w = Game::Get().GetState();
    std::map<std::string, const world::SurfaceStructure *> structures;
    for (const auto &chunk : visible)
    {
        for (const auto &structure : chunk->structures)
        {
            structures.emplace(structure.id, &structure);
        }
    }
    // Collision's bounded neighbourhood is also the object fallback during chunk publication.
    for (auto structure : session->NearbyStructures())
    {
        structures.emplace(structure->id, structure);
    }
    for (const auto &entry : structures)
    {
        const auto &structure = *entry.second;
        if (world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin,
                                             structure.frame.origin) <
            80 + std::hypot(structure.sizeMeters.x, structure.sizeMeters.y) * .5)
        {
            DrawSurfaceStructure(structure, *session);
        }
    }
    if (Game::Get().IsStoryWorld())
    {
        const auto &frontier = snapshot->planet->Region("frontier");
        if (snapshot->planet->cityCores && snapshot->planet->cityCores->Find("frontier") &&
            world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin,
                                             frontier.access.position) < 260)
        {
            if (!d.frontierGround)
            {
                d.frontierGround = FrontierGround(*snapshot->planet);
            }
            DrawSurfaceModel(*d.frontierGround, frontier.frame, *session);
            DrawSurfaceFrontier(*session);
        }
    }
    DrawSurfaceCities(*session);
    DrawPlanetCommunity(*session);
    DrawPlanetParty(*session);
    DrawPlanetTraffic(*session);
    CivicPresentation::DrawWorld();
    for (const auto &relay : snapshot->planet->relays)
    {
        if (world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin,
                                             relay.location.position) > 100)
        {
            continue;
        }
        auto local = session->Local(relay.location.position);
        Vector2 position{float(local.x / 2), float(local.y / 2)};
        bool registered = w.relayNetwork.Registered(relay.id);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -float(local.z / 2) * 44, float(local.z / 2) * (1296.f / 44));
        // Shared console and a billboard ring are a test representation, not a new stretched building.
        Drawing::DrawProp(models::ModelLibrary::Get().console, {position.x + 1.5f, position.y},
                          {.6f, .6f, .6f});
        auto center = Drawing::WorldToScreen(position.x, position.y, 10);
        auto color = registered ? ColorRGBA(.25f, 1, .85f, .75f) : ColorRGBA(.5f, .7f, .85f, .7f);
        Drawing::DrawGlow(center, 24, color);
        Drawing::DrawEllipse(center.x, center.y, 20, 10, color);
        if (world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin,
                                             relay.location.position) < 6)
        {
            Drawing::DrawLabel(center.x - 48, center.y - 34,
                               registered ? "국지 중계환 · E" : "중계환 등록 · E", color);
        }
        renderqueue::PopMatrix();
    }
    if (Game::Get().IsStoryWorld())
    {
        const auto &mineEntrance = snapshot->planet->Region("mq00_mine").access.position;
        if (world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin,
                                             mineEntrance) < 100)
        {
            auto local = session->Local(mineEntrance);
            renderqueue::PushMatrix();
            renderqueue::Translate(0, -float(local.z / 2) * 44, float(local.z / 2) * (1296.f / 44));
            Drawing::DrawInteractionMarker({float(local.x / 2), float(local.y / 2)},
                                           "오르딘 광산 / 하차 후 E 입장", {.4f, .9f, 1});
            renderqueue::PopMatrix();
        }
    }
    if (Game::Distance(w.bikePosition, w.playerPosition) < 80)
    {
        Drawing::DrawWorldBike(w.bikePosition, w.bikeYaw, std::fabs(w.bikeSpeed) / 12.8f, w.isBikeMounted);
    }
    if (!w.isBikeMounted)
    {
        float elevation = float(session->Local(session->Pose().origin).z / 2);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -elevation * 44, elevation * (1296.f / 44));
        Drawing::DrawPerson(w.playerPosition, {.55f, .68f, .72f}, true);
        renderqueue::PopMatrix();
    }
    backend.EndModels();
}

// ============================================================================
// SceneManager ? active-scene selection and resource retirement
// ============================================================================
Scene &SceneManager::Get(SceneKind kind)
{
    static auto scenes = []() {
        std::array<std::unique_ptr<Scene>, static_cast<size_t>(SceneKind::Count)> result;
        result[size_t(SceneKind::Interior)] = std::make_unique<InteriorScene>();
        result[size_t(SceneKind::Exterior)] = std::make_unique<CityScene>();
        result[size_t(SceneKind::Observatory)] = std::make_unique<ObservatoryScene>();
        result[size_t(SceneKind::Mine)] = std::make_unique<MineScene>();
        result[size_t(SceneKind::Frontier)] = std::make_unique<FrontierScene>();
        result[size_t(SceneKind::Planet)] = std::make_unique<PlanetScene>();
        result[size_t(SceneKind::Background)] = std::make_unique<BackgroundScene>();
        result[size_t(SceneKind::Hud)] = std::make_unique<HudScene>();
        return result;
    }();
    return *scenes.at(size_t(kind));
}
SceneKind SceneManager::Current()
{
    const auto &state = Game::Get().GetState();
    return Game::Get().IsSurfaceMine()        ? SceneKind::Mine
           : state.surfaceExploration         ? (state.isInsideHome      ? SceneKind::Interior
                                                 : state.isInObservatory ? SceneKind::Observatory
                                                                         : SceneKind::Planet)
           : GameLevels::Chapter().IsActive() ? SceneKind::Frontier
           : GameLevels::Mine().IsActive()    ? SceneKind::Mine
           : state.isInObservatory            ? SceneKind::Observatory
           : state.isInsideHome               ? SceneKind::Interior
                                              : SceneKind::Exterior;
}
void SceneManager::Reset()
{
    for (size_t i = 0; i < size_t(SceneKind::Count); ++i)
    {
        auto &scene = Get(SceneKind(i));
        scene.ResetResources();
        scene.BeginPlacement();
        scene.EndPlacement();
        scene.GetGraph().Clear();
    }
}
void SceneManager::Update(float deltaSeconds)
{
    profiling::Scope timer(profiling::Timer::SceneUpdate);
    Get(Current()).GetGraph().Update(deltaSeconds);
    Get(SceneKind::Background).GetGraph().Update(deltaSeconds);
    Get(SceneKind::Hud).GetGraph().Update(deltaSeconds);
}
} // namespace game
