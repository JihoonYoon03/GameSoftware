#include "stdafx.h"
#include "Scene.h"
#include "Level.h"
#include "Overworld.h"
#include "Drawing.h"
#include "ResourceAssets.h"
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
                        for (int x = x0; x < x0 + chunkSize && x < CityLayout::kTerrainMaxX; ++x)
                        {
                            for (int y = y0; y < y0 + chunkSize && y < CityLayout::kTerrainMaxY; ++y)
                            {
                                bool city = Overworld::CityAt({float(x), float(y)}) >= 0;
                                float shade = 0;
                                Drawing::DrawMaterialTile(
                                    float(x), float(y), SurfaceMaterial::Concrete,
                                    city ? ColorRGBA(.28f + shade, .34f + shade, .37f + shade)
                                         : ColorRGBA(.17f, .2f, .22f));
                            }
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
    for (size_t roadIndex = 0; roadIndex < net.roads.size(); ++roadIndex)
    {
        const auto &road = net.roads[roadIndex];
        auto a = net.junctions[road.from].position, b = net.junctions[road.to].position;
        float length = Game::Distance(a, b), width = road.lanes * RoadNetwork::kLaneWidth;
        Vector2 d = {(b.x - a.x) / length, (b.y - a.y) / length};
        float alongCamera = (camera.x - a.x) * d.x + (camera.y - a.y) * d.y;
        int first = (std::max)(0, int(std::floor((alongCamera - 38) / 12)));
        int last = (std::min)(int(std::ceil(length / 12)), int(std::ceil((alongCamera + 38) / 12)));
        for (int chunk = first; chunk < last; ++chunk)
        {
            float begin = chunk * 12.f, end = (std::min)(length, begin + 12);
            auto start = add(a, mul(d, begin)), finish = add(a, mul(d, end));
            auto center = add(a, mul(d, (begin + end) * .5f));
            if (Game::Distance(center, camera) > 48)
            {
                continue;
            }
            float x = (std::min)(start.x, finish.x) - width, y = (std::min)(start.y, finish.y) - width;
            scene.PlaceWorldActor(
                "road/" + std::to_string(roadIndex) + "/" + std::to_string(chunk), center.x, center.y,
                WorldBounds(x, y, std::fabs(finish.x - start.x) + width * 2,
                            std::fabs(finish.y - start.y) + width * 2, 0),
                1, 0, [=, &net]() {
                    Drawing::DrawGroundStrip(start, finish, width + 1.4f, {.32f, .39f, .41f}, .04f);
                    Drawing::DrawGroundStrip(start, finish, width, {.13f, .19f, .23f}, .075f,
                                             SurfaceMaterial::Asphalt);
                    float markStart = (std::max)(begin, net.junctions[road.from].radius + 3.6f);
                    float markEnd = (std::min)(end, length - net.junctions[road.to].radius - 3.6f);
                    if (markEnd <= markStart)
                    {
                        return;
                    }
                    auto type = road.lanes == 6   ? models::SceneryType::Road6Lane
                                : road.lanes == 4 ? models::SceneryType::Road4Lane
                                                  : models::SceneryType::Road2Lane;
                    for (float t = std::ceil(markStart / 2) * 2; t + 2 <= markEnd + .001f; t += 2)
                    {
                        Drawing::DrawProp(models::ModelLibrary::Scenery(type), add(a, mul(d, t + 1)),
                                          {1, RoadNetwork::kLaneWidth / 1.5f, 1}, {}, std::atan2(d.y, d.x),
                                          .1f);
                    }
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
            WorldBounds(p.x - j.radius, p.y - j.radius, j.radius * 2, j.radius * 2, 150), 1, 0, [=, &net]() {
                // Round the apron with shared, narrow ground strips, rather than stepped tiles.
                for (int slice = 0; slice < 16; ++slice)
                {
                    float y = (-1 + (slice + .5f) / 8.f) * j.radius;
                    float half = std::sqrt((std::max)(0.f, j.radius * j.radius - y * y));
                    Drawing::DrawGroundStrip({p.x - half, p.y + y}, {p.x + half, p.y + y},
                                             j.radius / 8 + .02f, {.13f, .19f, .23f}, .09f,
                                             SurfaceMaterial::Asphalt);
                }
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
                bool walk = Overworld::Traffic().PedestrianGreen(int(junction));
                ColorRGBA pedestrian = walk ? ColorRGBA(.15f, 1, .78f) : ColorRGBA(.95f, .21f, .28f);
                for (int arm : arms)
                {
                    const auto &out = net.lanes[arm];
                    auto d = out.direction;
                    Vector2 n = {-d.y, d.x};
                    float half = net.roads[out.road].lanes * RoadNetwork::kLaneWidth * .5f;
                    auto across = add(p, mul(d, j.radius - .45f));
                    models::Palette pedestrianPalette;
                    pedestrianPalette.emission = {pedestrian.r, pedestrian.g, pedestrian.b, 1};
                    Drawing::DrawProp(models::ModelLibrary::Scenery(models::SceneryType::PedestrianHologram),
                                      across, {half * 2 / 2.948f, .5f, 1}, pedestrianPalette,
                                      std::atan2(n.y, n.x), .10f);
                    // Ground arrows distinguish protected left turns from straight/right lanes.
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
                        auto base = add(add(p, mul(d, j.radius + 2.1f)),
                                        mul(n, (lane.slot + .5f) * RoadNetwork::kLaneWidth));
                        unsigned int signal = 0;
                        for (auto turn : turns)
                        {
                            signal = (std::max)(signal, unsigned(Overworld::Traffic().Light(incoming, turn)));
                        }
                        auto head = add(add(p, mul(d, j.radius - 1)),
                                        mul(n, (lane.slot + .5f) * RoadNetwork::kLaneWidth));
                        Drawing::DrawProp(models::ModelLibrary::TrafficSignal(signal), head, {.9f, 1, 1}, {},
                                          std::atan2(d.y, d.x) + kPi * .5f);
                        for (size_t t = 0; t < turns.size(); ++t)
                        {
                            auto light = Overworld::Traffic().Light(incoming, turns[t]);
                            ColorRGBA color = light == TrafficLight::Green   ? ColorRGBA(.18f, 1, .8f)
                                              : light == TrafficLight::Amber ? ColorRGBA(1, .65f, .12f)
                                                                             : ColorRGBA(.95f, .17f, .24f);
                            auto origin = add(base, mul(d, float(t) * .65f));
                            Vector2 heading = turns[t] == TrafficTurn::Straight
                                                  ? mul(d, -1)
                                                  : mul(n, turns[t] == TrafficTurn::Left ? -1.f : 1.f);
                            auto tip = add(origin, mul(heading, .48f));
                            Vector2 wing = {-heading.y, heading.x};
                            Drawing::DrawHologramStrip(add(origin, mul(heading, -.25f)), tip, .07f, color,
                                                       .17f);
                            Drawing::DrawHologramStrip(
                                tip, add(add(tip, mul(heading, -.23f)), mul(wing, .18f)), .07f, color, .17f);
                            Drawing::DrawHologramStrip(
                                tip, add(add(tip, mul(heading, -.23f)), mul(wing, -.18f)), .07f, color, .17f);
                        }
                    }
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
    for (size_t route = 0; route < net.airCorridors.size(); ++route)
    {
        auto corridor = net.airCorridors[route];
        auto point = [=](float t) {
            float s = 1 - t;
            return add(add(mul(corridor.start, s * s), mul(corridor.control, 2 * s * t)),
                       mul(corridor.end, t * t));
        };
        for (int segment = 0; segment < 160; segment += 2)
        {
            auto a = point(segment / 160.f), b = point((segment + 1) / 160.f);
            if (Game::Distance(a, camera) > 65)
            {
                continue;
            }
            scene.PlaceWorldActor(
                "air-corridor/" + std::to_string(route) + "/" + std::to_string(segment), a.x, a.y,
                WorldBounds(a.x - 4, a.y - 4, 8, 8, corridor.altitude * 44), 2, a.x + a.y, [=]() {
                    Drawing::DrawHologramStrip(a, b, .08f, {.28f, .66f, .92f, .46f}, corridor.altitude);
                    if (segment % 20 == 0)
                    {
                        auto label = Drawing::WorldToScreen(a.x, a.y, corridor.altitude * 44 + 10);
                        Drawing::DrawLabel(label.x, label.y, "항공 회랑 " + std::to_string(route + 1),
                                           {.4f, .65f, .8f, .65f});
                    }
                });
        }
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
    for (size_t i = 0; i < Overworld::Scenery().Props().size(); ++i)
    {
        const auto &prop = Overworld::Scenery().Props()[i];
        const auto &resource = models::ResourceAssets::Get(prop.model->resourceId);
        scene.PlaceWorldActor(
            "scenery/" + std::to_string(i), prop.position.x, prop.position.y,
            WorldBounds(prop.minimum.x, prop.minimum.y, prop.maximum.x - prop.minimum.x,
                        prop.maximum.y - prop.minimum.y, (resource.maximum.z + prop.elevation) * 44 + 8),
            prop.model->ground ? 1 : 3, prop.position.x + prop.position.y, [prop]() {
                models::Transform3D transform;
                transform.yaw = prop.yaw;
                transform.position.z = prop.elevation;
                Drawing::DrawModel3D(*prop.model, Drawing::WorldToScreen(prop.position.x, prop.position.y),
                                     {1, 1, 1}, transform, (prop.position.x + prop.position.y) * 36, {});
            });
    }
    for (const auto &craft : Overworld::Flights().Aircraft())
    {
        const auto &model = models::ModelLibrary::Aircraft(craft.type, craft.alternate);
        const auto &resource = models::ResourceAssets::Get(model.resourceId);
        float radius = (std::max)(resource.physicalSize.x, resource.physicalSize.y) * .75f + .5f;
        auto p = craft.position;
        scene.PlaceWorldActor("aircraft/" + std::to_string(craft.id), p.x, p.y,
                              WorldBounds(p.x - radius, p.y - radius, radius * 2, radius * 2,
                                          (craft.altitude + resource.maximum.z) * 44 + 24),
                              3, p.x + p.y, [craft]() {
                                  Drawing::DrawWorldAircraft(craft.position, craft.altitude, craft.type,
                                                             craft.alternate, craft.yaw, craft.bank);
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
                Drawing::DrawWorldVehicle(
                    position, 3.3f, 1.f,
                    static_cast<models::VehicleType>(Overworld::Traffic().Vehicles()[item.index].id % 4),
                    GetTrafficPalette(int(Overworld::Traffic().Vehicles()[item.index].id % 12)),
                    Overworld::Traffic().Vehicles()[item.index].speed / 10.f,
                    Overworld::Traffic().RenderYaw(size_t(item.index)));
            }
            if (item.kind == SceneObjectKind::JetBike)
            {
                const auto &world = Game::Get().GetState();
                Drawing::DrawWorldBike(world.bikePosition, world.bikeYaw,
                                       .08f + .92f * world.bikeSpeed / Overworld::kBikeCruiseSpeed,
                                       world.isBikeMounted);
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
                                    models::CharacterType::Engineer, false, .78539816f,
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
        std::string label = std::string(city.name) + " / 주민 20명";
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
        if (lamp.needsPower && !GameLevels::Mine().GetState().quest.IsPowerBypassed())
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
// MineScene — darkness overlay
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
    Get(SceneKind::Background).GetGraph().Update(deltaSeconds);
    Get(SceneKind::Hud).GetGraph().Update(deltaSeconds);
}
} // namespace game
