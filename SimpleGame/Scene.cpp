#include "stdafx.h"
#include "Scene.h"
#include "Civic.h"
#include "CityFacilities.h"
#include "FacilityApproaches.h"
#include "CityInfrastructure.h"
#include "Drawing.h"
#include "Hud.h"
#include "Level.h"
#include "StoryArc.h"
#include "WorldEncounters.h"
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
#include <string_view>
#include <unordered_set>
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
                   last = WorldCamera::FromViewport({WorldCamera::LogicalWidth(), float(kCanvasHeight)});
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
        rendering::SceneLighting light;
        light.keyDirection = {-.25f, -.35f, .90f};
        light.keyColor = {1.02f, .96f, .88f};
        light.keyIntensity = .94f;
        light.skyColor = {.54f, .68f, .80f};
        light.groundColor = {.46f, .42f, .38f};
        light.ambientIntensity = .86f;
        light.rimColor = {.42f, .78f, .90f};
        light.rimIntensity = .24f;
        light.exposure = 1.20f;
        light.shadowStrength = .26f;
        GameRenderer::Get().Backend().SetLighting(light);
        Drawing::AddPointLight({-.3f, -2.5f}, 1.4f, 5.0f, {.32f, .80f, .94f}, .85f);
        Drawing::AddPointLight({-2.05f, -1.85f}, .65f, 3.8f, {.45f, .80f, .95f}, .72f);
        Drawing::AddPointLight({1.6f, -2.2f}, .95f, 4.4f, {.30f, .95f, .90f}, .92f);
        Drawing::AddPointLight({-1.9f, 1.4f}, .95f, 4.6f, {1.f, .65f, .28f}, 1.05f);
        Drawing::AddPointLight({2.25f, -.15f}, .75f, 3.4f, {.95f, .74f, .34f}, .62f);
    }
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
            Drawing::DrawGlow(Drawing::WorldToScreen(-2.05f, -1.85f, 16), 16, ColorRGBA(.45f, .78f, .92f, .7f));
        });
        scene.PlaceWorldActor("terminal", 1.3f, -2.6f, WorldBounds(1, -2.6f, 1.6f, 1.4f, 65), 1, 4, [this]() {
            Drawing::DrawModel3D(models::ModelLibrary::Get().console, Drawing::WorldToScreen(1.6f, -2.4f),
                                 {1, 1, 1}, {}, (1.6f - 2.4f) * 36, {});
            Drawing::DrawGlow(Drawing::WorldToScreen(1, -1.2f), 20, ColorRGBA(.3f, .95f, .9f));
        });
        scene.PlaceWorldActor("table", -2.4f, 1.1f, WorldBounds(-2.4f, 1.1f, 1.2f, .6f, 55), 1, 5, [this]() {
            Drawing::DrawProp(models::ModelLibrary::Get().table, {-1.8f, 1.4f});
            Drawing::DrawProp(models::ModelLibrary::Get().chair, {-1.8f, 2.15f});
            Drawing::DrawProp(models::ModelLibrary::Get().recoveryKit, {-1.55f, 1.35f}, {.65f, .65f, .65f}, {},
                              .3f, .44f);
            Drawing::DrawProp(models::ModelLibrary::Get().salvage, {-2.08f, 1.42f}, {.55f, .55f, .55f}, {},
                              -.4f, .44f);
            Drawing::DrawGlow(Drawing::WorldToScreen(-2, 1.4f, 35), 18, ColorRGBA(1, .63f, .28f));
        });
        scene.PlaceWorldActor("storage", 1.85f, -.55f, WorldBounds(1.85f, -.55f, .9f, .9f, 45), 1, 5.5f, [this]() {
            models::Palette palette;
            palette.body = {.24f, .36f, .42f, 1};
            palette.accent = {.88f, .64f, .28f, 1};
            Drawing::DrawProp(models::ModelLibrary::Get().box, {2.25f, -.15f}, {.78f, .78f, .78f}, palette);
            Drawing::DrawGlow(Drawing::WorldToScreen(2.25f, -.15f, 34), 16, ColorRGBA(.95f, .74f, .32f, .8f));
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
        scene.PlaceWorldActor("bed-label", -2.05f, -2.05f, WorldBounds(-2.05f, -2.05f, 0, 0, 55, 80), 3, 0,
                              [this]() {
                                  Vector2 label = Drawing::WorldToScreen(-2.05f, -2.05f, 48);
                                  Drawing::DrawLabel(label.x - 42, label.y, "침대 · 휴식",
                                                     ColorRGBA(.75f, .92f, 1));
                              });
        scene.PlaceWorldActor("workbench-label", -1.8f, 1.4f, WorldBounds(-1.8f, 1.4f, 0, 0, 65, 80), 3, 0,
                              [this]() {
                                  Vector2 label = Drawing::WorldToScreen(-1.8f, 1.4f, 58);
                                  Drawing::DrawLabel(label.x - 48, label.y, "작업대 · 정비",
                                                     ColorRGBA(1, .84f, .52f));
                              });
        scene.PlaceWorldActor("storage-label", 2.25f, -.15f, WorldBounds(2.25f, -.15f, 0, 0, 60, 80), 3, 0,
                              [this]() {
                                  Vector2 label = Drawing::WorldToScreen(2.25f, -.15f, 54);
                                  Drawing::DrawLabel(label.x - 50, label.y, "보관함 · 옷장",
                                                     ColorRGBA(1, .86f, .55f));
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
        scene.GetGraph().Reparent("bed-label", *scene.GetGraph().Find("bed"));
        scene.GetGraph().Reparent("workbench-label", *scene.GetGraph().Find("table"));
        scene.GetGraph().Reparent("storage-label", *scene.GetGraph().Find("storage"));
        scene.GetGraph().Reparent("terminal-label", *scene.GetGraph().Find("terminal"));
        scene.GetGraph().Reparent("door-label", *scene.GetGraph().Find("door"));
    }
    scene.DrawActors();
}

// ============================================================================
// FacilityInteriorScene — CQ-04 C2 walkable Market / Clinic / Workshop / CivicHall
// ============================================================================
void FacilityInteriorScene::Draw()
{
    const auto &w = Game::Get().GetState();
    const auto kind = w.facilityInterior.kind;
    const ColorRGBA floorCol = kind == UrbanFacilityKind::Market    ? ColorRGBA(.24f, .31f, .29f)
                               : kind == UrbanFacilityKind::Clinic  ? ColorRGBA(.27f, .36f, .39f)
                               : kind == UrbanFacilityKind::Workshop ? ColorRGBA(.28f, .27f, .25f)
                                                                    : ColorRGBA(.22f, .28f, .36f);
    const ColorRGBA accentCol = kind == UrbanFacilityKind::Market    ? ColorRGBA(.28f, .92f, .64f)
                                : kind == UrbanFacilityKind::Clinic  ? ColorRGBA(.32f, .92f, .98f)
                                : kind == UrbanFacilityKind::Workshop ? ColorRGBA(1.f, .68f, .24f)
                                                                     : ColorRGBA(.62f, .78f, 1.f);
    const ColorRGBA clerkCoat = kind == UrbanFacilityKind::Market    ? ColorRGBA(.28f, .62f, .52f)
                                : kind == UrbanFacilityKind::Clinic  ? ColorRGBA(.82f, .92f, .95f)
                                : kind == UrbanFacilityKind::Workshop ? ColorRGBA(.78f, .52f, .24f)
                                                                     : ColorRGBA(.28f, .44f, .72f);
    const char *clerkRole = kind == UrbanFacilityKind::Market    ? "보급 상인"
                            : kind == UrbanFacilityKind::Clinic  ? "의무관"
                            : kind == UrbanFacilityKind::Workshop ? "정비사"
                                                                 : "행정관";
    const models::CharacterType clerkType = kind == UrbanFacilityKind::Clinic    ? models::CharacterType::Medic
                                            : kind == UrbanFacilityKind::Workshop ? models::CharacterType::Engineer
                                            : kind == UrbanFacilityKind::CivicHall ? models::CharacterType::Researcher
                                                                                   : models::CharacterType::Civilian;
    const models::Gender clerkGender =
        (kind == UrbanFacilityKind::Clinic || kind == UrbanFacilityKind::Market) ? models::Gender::Female
                                                                                 : models::Gender::Male;
    const models::HairStyle clerkHair = kind == UrbanFacilityKind::Clinic    ? models::HairStyle::Ponytail
                                        : kind == UrbanFacilityKind::Workshop ? models::HairStyle::Buzz
                                        : kind == UrbanFacilityKind::CivicHall ? models::HairStyle::SidePart
                                                                               : models::HairStyle::Bob;

    // First-person player-perspective view when conversing with the facility NPC across the counter.
    if (w.ui.screen == UiScreen::FacilityShop)
    {
        auto &backend = GameRenderer::Get().Backend();
        const float logicalW = WorldCamera::LogicalWidth();
        const float cxOff = WorldCamera::CenterOffsetX();
        const float t = w.animationTimeSeconds;
        const float react = Game::Clamp(w.facilityReactionSeconds / 1.4f, 0.f, 1.f);
        const float reactNod = std::sin(react * kPi);
        const float lightPulse = 0.94f + 0.06f * std::sin(t * 1.9f);

        rendering::SceneLighting light;
        light.keyDirection = {-.28f, .68f, .62f};
        light.keyColor = {1.04f * lightPulse, .97f * lightPulse, .90f * lightPulse};
        light.keyIntensity = 1.08f;
        light.skyColor = {.50f + accentCol.r * .25f, .58f + accentCol.g * .22f, .68f + accentCol.b * .22f};
        light.groundColor = {.38f, .34f, .32f};
        light.ambientIntensity = .64f;
        light.rimColor = {accentCol.r, accentCol.g, accentCol.b};
        light.rimIntensity = .58f;
        light.exposure = 1.24f;
        light.shadowStrength = .20f;
        backend.SetLighting(light);

        // 1st-person eye-level interior perspective backdrop filling the entire active aspect ratio.
        Drawing::DrawRectangle(0, 0, logicalW, kCanvasHeight,
                               {0.05f + accentCol.r * 0.04f, 0.07f + accentCol.g * 0.05f, 0.10f + accentCol.b * 0.06f,
                                1.f});
        Drawing::DrawRectangle(0, 0, logicalW, 114, {0.03f, 0.05f, 0.07f, 1.f});
        Drawing::DrawRectangle(cxOff + 64.f, 88, 468, 7,
                               {accentCol.r * .75f, accentCol.g * .82f, accentCol.b * .90f, 0.85f * lightPulse});
        Drawing::DrawGlow({cxOff + 298.f, 124}, 170, {accentCol.r, accentCol.g, accentCol.b, 0.25f * lightPulse});
        for (int i = 0; i < 4; ++i)
        {
            const float px = cxOff + 32.f + float(i) * 134.f;
            Drawing::DrawRectangle(px, 138, 118, 454, {0.08f, 0.11f, 0.15f, 0.78f});
            Drawing::DrawLine({px, 138}, {px + 118, 138},
                              {accentCol.r * .45f, accentCol.g * .52f, accentCol.b * .60f, 0.55f}, 1.5f);
            for (int shelf = 0; shelf < 3; ++shelf)
            {
                const float sy = 216.f + float(shelf) * 98.f;
                Drawing::DrawRectangle(px + 10, sy, 98, 6, {0.14f, 0.19f, 0.24f, 0.85f});
                Drawing::DrawRectangle(
                    px + 16, sy - 26, 28, 26,
                    {0.18f + accentCol.r * 0.12f, 0.24f + accentCol.g * 0.12f, 0.28f + accentCol.b * 0.12f, 0.75f});
                Drawing::DrawRectangle(px + 54, sy - 18, 34, 18, {0.12f, 0.17f, 0.22f, 0.75f});
            }
        }
        Drawing::DrawGlow({cxOff + 295.f, 375}, 260, {accentCol.r * 0.9f, accentCol.g * 0.9f, accentCol.b, 0.26f});
        renderqueue::Flush();

        // Render live-animated 3D NPC upper-body close-up on the LEFT side in eye-level 1st-person projection.
        backend.SetProjectionBasis(18.f, 44.f, true);
        backend.BeginModels();
        models::CharacterPose talkPose =
            models::ModelRig::Pose(t * 1.35f, false, false, 0, false, clerkGender);
        talkPose.motionForwardLean = 0.025f * std::sin(t * 1.4f) + 0.085f * reactNod;
        talkPose.motionSideLean = 0.045f * std::sin(t * 0.85f);
        if (w.facilityReactionSeconds > 0.f)
        {
            const bool isTrade = w.facilityReactionSeconds > 1.15f;
            talkPose.facilityClip = isTrade ? 2 : 1; // Shop_Trade_HandOver / Shop_Talk_Welcome
            talkPose.facilityPhase =
                isTrade ? std::clamp((1.4f - w.facilityReactionSeconds) / 1.133f, 0.f, 1.f)
                        : std::clamp((1.15f - w.facilityReactionSeconds) / 1.15f, 0.f, 1.f);
            talkPose.facilityWeight = 1.f;
        }
        else
        {
            talkPose.facilityClip =
                (kind == UrbanFacilityKind::Workshop || kind == UrbanFacilityKind::Clinic) ? 5 : 3;
            talkPose.facilityPhase = std::fmod(t / 3.2f, 1.f);
            talkPose.facilityWeight = 1.f;
        }
        models::Palette npcPal;
        npcPal.body = {clerkCoat.r, clerkCoat.g, clerkCoat.b, 1.f};
        npcPal.accent = {accentCol.r, accentCol.g, accentCol.b, 1.f};
        npcPal.emission = {accentCol.r, accentCol.g, accentCol.b, 1.f};
        const float portraitScale = models::WorldScale::characterScale * 15.8f;
        models::Transform3D rot;
        // Character local +X is forward; live breathing sway + attentive turn + greeting/trade nod.
        rot.yaw = kPi * 0.5f - 0.14f + std::sin(t * 1.15f) * 0.09f + std::sin(t * 0.47f) * 0.05f;
        rot.roll = std::sin(t * 0.85f) * 0.032f;
        rot.pitch = -0.075f * reactNod - 0.015f * std::sin(t * 2.1f);
        const float breatheY = std::sin(t * 2.1f) * 3.5f + reactNod * 6.0f;
        const float swayX = std::sin(t * 0.85f) * 5.0f;
        const Vector2 npcAnchor{cxOff + 295.f + swayX, 920.f + breatheY};
        int sharedSlot = -1;
        bool drewFieldMedic = false;
        if (clerkType == models::CharacterType::Medic)
        {
            constexpr const char *kFieldMedicSkinned[] = {
                "modular/body/Field_Medic_Head_Neck",    "modular/body/Field_Medic_Hands",
                "modular/outfit/Field_Medic_Coat",       "modular/outfit/Field_Medic_Cargo_Pants",
                "modular/outfit/Field_Medic_Boots",      "modular/part/Field_Medic_Bun_Hair",
                "modular/part/Field_Medic_Shoulder_Guards", "modular/part/Field_Medic_Knee_Guards"};
            if (models::ModelLibrary::ModularPart(kFieldMedicSkinned[0]))
            {
                const models::Palette neutralPal{};
                for (const char *partName : kFieldMedicSkinned)
                {
                    if (const auto *part = models::ModelLibrary::ModularPart(partName))
                    {
                        Drawing::DrawModel3D(*part, npcAnchor, {portraitScale, portraitScale, portraitScale}, rot,
                                             0.f, neutralPal, &talkPose, 0.f, 0.f, &sharedSlot);
                    }
                }
                drewFieldMedic = true;
            }
        }
        if (!drewFieldMedic)
        {
            Drawing::DrawModel3D(models::ModelLibrary::Character(clerkType, clerkGender), npcAnchor,
                                 {portraitScale, portraitScale, portraitScale}, rot, 0.f, npcPal, &talkPose, 0.f,
                                 0.f, &sharedSlot);
            Drawing::DrawHair(npcAnchor, {portraitScale, portraitScale, portraitScale}, rot, 0.f, talkPose,
                              clerkType, clerkGender, clerkHair, &sharedSlot);
        }
        backend.EndModels();
        backend.SetProjectionBasis(WorldCamera::IsoYScale(), WorldCamera::IsoZScale(), false);

        // Foreground service counter framing the NPC at upper-body / waist height across full viewport width.
        Drawing::DrawRectangle(0, 628, logicalW, 172, {0.06f, 0.08f, 0.11f, 0.97f});
        Drawing::DrawRectangle(0, 624, logicalW, 6,
                               {accentCol.r * .58f, accentCol.g * .68f, accentCol.b * .74f, 0.85f});
        Drawing::DrawLine({0, 631}, {logicalW, 631}, {1.f, 1.f, 1.f, 0.12f}, 1.5f);
        renderqueue::Flush();
        return;
    }

    WorldView view;
    auto &scene = *this;
    {
        rendering::SceneLighting light;
        light.keyDirection = {-.22f, -.30f, .92f};
        light.keyColor = kind == UrbanFacilityKind::Clinic     ? std::array<float, 3>{.96f, 1.04f, 1.08f}
                         : kind == UrbanFacilityKind::Workshop ? std::array<float, 3>{1.08f, .92f, .76f}
                         : kind == UrbanFacilityKind::Market   ? std::array<float, 3>{1.00f, 1.04f, .94f}
                                                               : std::array<float, 3>{.96f, 1.00f, 1.08f};
        light.keyIntensity = 1.00f;
        light.skyColor = {accentCol.r * .40f + .44f, accentCol.g * .40f + .48f, accentCol.b * .40f + .54f};
        light.groundColor = {.44f, .44f, .46f};
        light.ambientIntensity = .90f;
        light.rimColor = {accentCol.r, accentCol.g, accentCol.b};
        light.rimIntensity = .26f;
        light.exposure = 1.22f;
        light.shadowStrength = .26f;
        GameRenderer::Get().Backend().SetLighting(light);
        Drawing::AddPointLight({0.f, -2.1f}, 1.45f, 5.8f, accentCol, .95f);
        Drawing::AddPointLight({-1.6f, -1.4f}, .95f, 4.4f, {1.f, .96f, .88f}, .78f);
        Drawing::AddPointLight({1.8f, -1.4f}, .95f, 4.4f, accentCol, .82f);
    }
    {
        profiling::Scope placementTimer(profiling::Timer::ScenePlacement);
        scene.BeginPlacement();
        for (int x = -3; x < 4; ++x)
        {
            for (int y = -2; y < 3; ++y)
            {
                scene.PlaceWorldActor(
                    "ftile/" + std::to_string(x) + "/" + std::to_string(y), float(x), float(y),
                    WorldBounds(float(x), float(y), 1, 1, 0), 0, 0, [this, x, y, floorCol]() {
                        Drawing::DrawMaterialTile(float(x), float(y), SurfaceMaterial::Metal, floorCol);
                    });
            }
        }
        scene.PlaceWorldActor("fwall/back", -3, -2.5f, WorldBounds(-3, -2.5f, 7, .12f, 95), 1, 0, [this]() {
            for (int i = 0; i < 7; ++i)
            {
                Drawing::DrawProp(models::ModelLibrary::Get().wall, {-3.f + i, -2.5f}, {1, .12f, 95.f / 44});
            }
        });
        scene.PlaceWorldActor("fwall/side", -3.4f, -2.5f, WorldBounds(-3.4f, -2.5f, .12f, 5, 95), 1, 1, [this]() {
            for (int i = 0; i < 5; ++i)
            {
                Drawing::DrawProp(models::ModelLibrary::Get().wall, {-3.4f, -2.5f + i}, {1, .12f, 95.f / 44}, {},
                                  kPi / 2);
            }
        });
        scene.PlaceWorldActor("fbanner", -1.f, -2.4f, WorldBounds(-1.f, -2.4f, 0, 0, 80, 150), 1, 2,
                              [this, accentCol]() {
                                  Vector2 pt = Drawing::WorldToScreen(-1.f, -2.4f, 68);
                                  Drawing::DrawLine(pt, {pt.x + 120, pt.y + 60}, accentCol, 14);
                              });
        scene.PlaceWorldActor("fcounter", -2.2f, -1.9f, WorldBounds(-2.2f, -1.9f, 4.4f, .9f, 45), 1, 3,
                              [this, kind, accentCol]() {
                                  Drawing::DrawProp(models::ModelLibrary::Get().table, {-1.1f, -1.55f}, {1.6f, 1.f, 1.f});
                                  Drawing::DrawProp(models::ModelLibrary::Get().console, {1.25f, -1.65f}, {.95f, .95f, .95f});
                                  if (kind == UrbanFacilityKind::Market || kind == UrbanFacilityKind::Clinic)
                                  {
                                      Drawing::DrawProp(models::ModelLibrary::Get().recoveryKit, {-1.35f, -1.5f},
                                                        {.75f, .75f, .75f}, {}, .2f, .44f);
                                  }
                                  else
                                  {
                                      Drawing::DrawProp(models::ModelLibrary::Get().salvage, {-1.35f, -1.5f},
                                                        {.65f, .65f, .65f}, {}, -.3f, .44f);
                                  }
                                  Drawing::DrawGlow(Drawing::WorldToScreen(-1.05f, -1.25f, 34), 20, accentCol);
                                  Drawing::DrawGlow(Drawing::WorldToScreen(1.25f, -1.25f, 34), 18, accentCol);
                              });
        scene.PlaceWorldActor("fcrates", -2.9f, 1.2f, WorldBounds(-2.9f, 1.2f, .9f, .9f, 45), 1, 4, [this, kind]() {
            if (kind == UrbanFacilityKind::Clinic)
            {
                Drawing::DrawProp(models::ModelLibrary::Get().bed, {-2.6f, 1.2f});
            }
            else
            {
                Drawing::DrawProp(models::ModelLibrary::Get().box, {-2.75f, 1.35f}, {.85f, .85f, .85f});
            }
        });
        scene.PlaceWorldActor("fclerk", -.2f, -2.05f, WorldBounds(-.2f, -2.05f, 0, 0, 90), 1, 2.5f,
                              [this, &w, clerkCoat, clerkType, clerkGender, clerkHair, clerkRole, accentCol]() {
                                  models::CharacterPose idlePose = models::ModelRig::Pose(
                                      w.animationTimeSeconds, false, false, 0, false, clerkGender);
                                  idlePose.facilityClip = 0; // Shop_Idle_Counter_Loop
                                  idlePose.facilityPhase = std::fmod(w.animationTimeSeconds / 3.6f, 1.f);
                                  idlePose.facilityWeight = 1.f;
                                  const Vector2 clerkPos{-.2f, -2.05f};
                                  const Vector2 screen = Drawing::WorldToScreen(clerkPos.x, clerkPos.y);
                                  const float depth = (clerkPos.x + clerkPos.y) * 36.f;
                                  const float s = models::WorldScale::characterScale;
                                  models::Transform3D rot;
                                  rot.yaw = kPi * .5f;
                                  int sharedSlot = -1;
                                  bool drewFieldMedic = false;
                                  if (clerkType == models::CharacterType::Medic)
                                  {
                                      constexpr const char *kFieldMedicSkinned[] = {
                                          "modular/body/Field_Medic_Head_Neck",
                                          "modular/body/Field_Medic_Hands",
                                          "modular/outfit/Field_Medic_Coat",
                                          "modular/outfit/Field_Medic_Cargo_Pants",
                                          "modular/outfit/Field_Medic_Boots",
                                          "modular/part/Field_Medic_Bun_Hair",
                                          "modular/part/Field_Medic_Shoulder_Guards",
                                          "modular/part/Field_Medic_Knee_Guards"};
                                      if (models::ModelLibrary::ModularPart(kFieldMedicSkinned[0]))
                                      {
                                          const models::Palette neutralPal{};
                                          for (const char *partName : kFieldMedicSkinned)
                                          {
                                              if (const auto *part = models::ModelLibrary::ModularPart(partName))
                                              {
                                                  Drawing::DrawModel3D(*part, screen, {s, s, s}, rot, depth,
                                                                       neutralPal, &idlePose, 0.f, 0.f, &sharedSlot);
                                              }
                                          }
                                          drewFieldMedic = true;
                                      }
                                  }
                                  if (!drewFieldMedic)
                                  {
                                      models::Palette pal;
                                      pal.body = {clerkCoat.r, clerkCoat.g, clerkCoat.b, 1.f};
                                      Drawing::DrawModel3D(models::ModelLibrary::Character(clerkType, clerkGender),
                                                           screen, {s, s, s}, rot, depth, pal, &idlePose, 0.f, 0.f,
                                                           &sharedSlot);
                                      Drawing::DrawHair(screen, {s, s, s}, rot, depth, idlePose, clerkType,
                                                        clerkGender, clerkHair, &sharedSlot);
                                  }
                                  Vector2 pt = Drawing::WorldToScreen(-.2f, -2.05f, 84);
                                  Drawing::DrawLabel(pt.x - 42, pt.y, std::string(clerkRole) + " · E 대화", accentCol);
                              });
        scene.PlaceWorldActor("fdoor", 2.1f, 1.6f, WorldBounds(2.1f, 1.6f, 1, 1, 0), 1, 5, [this, accentCol]() {
            Drawing::DrawTile(2.1f, 1.6f, 1, 1, ColorRGBA(accentCol.r * .35f, accentCol.g * .45f, accentCol.b * .45f));
        });
        scene.PlaceWorldActor("player", w.playerPosition.x, w.playerPosition.y,
                              WorldBounds(w.playerPosition.x, w.playerPosition.y, 0, 0, 90), 2, 0, [this, &w]() {
                                  Drawing::DrawPerson(w.playerPosition, ColorRGBA(.67f, .8f, .82f), true);
                              });
        scene.PlaceWorldActor("fdoor-label", 2.5f, 1.9f, WorldBounds(2.5f, 1.9f, 0, 0, 0, 80), 3, 0, [this]() {
            Vector2 p = Drawing::WorldToScreen(2.5f, 1.9f);
            Drawing::DrawLabel(p.x - 26, p.y + 24, "거리 출구");
        });
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
                    const bool walk = Overworld::Traffic().PedestrianGreen(int(junction), out.road);
                    const bool flashing = Overworld::Traffic().PedestrianFlashing(int(junction), out.road);
                    const bool flashOn = !flashing || std::fmod(Game::Get().GetState().animationTimeSeconds * 2.5f, 1.f) < .5f;
                    ColorRGBA pedestrian = !walk     ? ColorRGBA(.95f, .21f, .28f)
                                           : flashOn ? ColorRGBA(.15f, 1, .78f)
                                                     : ColorRGBA(1, .74f, .18f);
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
                            Drawing::DrawLabel(label.x - 24, label.y, "◀ 좌회전", {.2f, 1, .75f});
                        }
                        else if (Overworld::Traffic().HasRightTurnSignal(incoming) &&
                                 Overworld::Traffic().Light(incoming, TrafficTurn::Straight) != TrafficLight::Green)
                        {
                            const auto &resource = models::ResourceAssets::Get(
                                models::ModelLibrary::TrafficSignal(0).resourceId);
                            auto label = Drawing::WorldToScreen(head.x, head.y, resource.maximum.z * 44 + 12);
                            Drawing::DrawLabel(label.x - 28, label.y, "우회전 정지", {1.f, .35f, .32f});
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
                    resident.speed, -1, &resident.facing);
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
    if (!story::StoryArc::Get().IsRiaMissing())
    {
        placeObject({kLiaPosition.x + kLiaPosition.y, SceneObjectKind::Lia, 0});
    }
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
        const auto &arc = story::StoryArc::Get().State();
        const char *maraLabel = arc.chapter4Complete    ? "마라 / 대피소 [E]"
                                : arc.sectorLossTriggered ? "마라 / 실종 접수 · 지원팀 기록 [E]"
                                                          : "마라 / 선택 의뢰";
        marker("mara", kMaraPosition, maraLabel, {1, .77f, .4f});
    }
    if (game.IsQuestInteraction(InteractionTarget::PowerRelay))
    {
        marker("relay", kRelayPosition, "전력 복구 [E]", {1, .7f, .3f});
    }
    if (game.IsQuestInteraction(InteractionTarget::GateBeacon))
    {
        marker("beacon", kBeaconPosition, "전망대 승강기 [E]", {.4f, .93f, .96f});
    }
    if (game.IsQuestInteraction(InteractionTarget::JetBike))
    {
        if (!game.GetState().isBikeMounted)
        {
            marker("jet-bike", game.GetState().bikePosition, "제트 바이크 탑승 [E]", {1, .75f, .4f});
        }
    }
    const auto &arc = story::StoryArc::Get().State();
    if (arc.sectorLossTriggered)
    {
        scene.PlaceWorldActor("halo-missing-sector", 25.f, 7.f, WorldBounds(25.f, 7.f, 6.f, 10.f, 60.f, 80.f), 1, 32.f,
                              []() {
                                  const Vector2 a = Drawing::WorldToScreen(25.f, 2.f);
                                  const Vector2 b = Drawing::WorldToScreen(25.f, 12.f);
                                  const Vector2 c = Drawing::WorldToScreen(31.f, 12.f);
                                  const Vector2 d = Drawing::WorldToScreen(31.f, 2.f);
                                  Drawing::DrawPolygon({a, b, c, d}, {.03f, .05f, .08f, .92f});
                                  Drawing::DrawLine(a, b, {.96f, .44f, .34f, .90f}, 2.5f);
                              });
        if (!arc.chapter4Complete)
        {
            marker("safety-line", {25.f, 7.f}, "소실 구획 안전선 확인 [E]", {.98f, .52f, .38f});
        }
    }
    if (arc.chapter4Complete)
    {
        if (!arc.epilogueClinicVisited)
        {
            marker("epilogue-clinic", {11.f, -2.f}, "임시 의료소 · 구조 탐사대 3명 [E]", {.38f, .94f, .76f});
        }
        if (!arc.epilogueWorkshopVisited)
        {
            marker("epilogue-workshop", {19.f, 2.f}, "정비소 · 카인 · 복구팀 [E]", {1.f, .78f, .36f});
        }
        if (GameLevels::Chapter().GetState().portableLightRepaired)
        {
            const Vector2 lampPos{kMaraPosition.x + 1.6f, kMaraPosition.y};
            marker("shelter-lamp", lampPos,
                   arc.shelterLampOn ? "대피소 휴대 조명 (켜짐) [E]" : "대피소 휴대 조명 (꺼짐) [E]",
                   arc.shelterLampOn ? ColorRGBA(1.f, .88f, .46f) : ColorRGBA(.56f, .64f, .70f));
        }
    }
}

void CityScene::Draw()
{
    WorldView view;
    auto &scene = *this;
    rendering::SceneLighting cityLight{};
    const auto &arc = story::StoryArc::Get().State();
    if (arc.cityPhase == story::CityPhase::Uneasy)
    {
        cityLight.keyColor = {.92f, .96f, 1.04f};
        cityLight.groundColor = {.38f, .42f, .48f};
    }
    else if (arc.cityPhase == story::CityPhase::Evacuation)
    {
        cityLight.keyColor = {1.06f, .76f, .64f};
        cityLight.skyColor = {.52f, .42f, .64f};
        cityLight.groundColor = {.36f, .28f, .36f};
    }
    else if (arc.cityPhase == story::CityPhase::Recovery)
    {
        cityLight.keyColor = {.94f, .95f, 1.00f};
        cityLight.skyColor = {.60f, .72f, .82f};
        cityLight.groundColor = {.44f, .48f, .52f};
    }
    GameRenderer::Get().Backend().SetLighting(cityLight);
    Drawing::AddPointLight(kObservatoryBuildingPosition, 1.8f, 6.5f, {.42f, .84f, 1.f}, .75f);
    if (arc.chapter4Complete && GameLevels::Chapter().GetState().portableLightRepaired && arc.shelterLampOn)
    {
        Drawing::AddPointLight({kMaraPosition.x + 1.6f, kMaraPosition.y}, 0.9f, 4.2f, {1.f, .86f, .46f}, 1.1f);
    }
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
        for (const auto &enc : encounters::WorldEncounters::Get().State().live)
        {
            if (enc.surfaceSpace || !enc.activeInRange || enc.phase == encounters::Phase::Completed)
            {
                continue;
            }
            const auto &we = encounters::WorldEncounters::Get();
            const Vector2 anchor = enc.legacyAnchor;
            scene.PlaceWorldActor("enc/" + enc.id, anchor.x, anchor.y,
                                  WorldBounds(anchor.x, anchor.y, 24.f, 24.f, 90.f, 140.f), 1, anchor.x + anchor.y,
                                  [&enc, &we]() {
                                      for (const auto &n : enc.nodes)
                                      {
                                          if (!n.revealed || n.completed)
                                          {
                                              continue;
                                          }
                                          const bool usable =
                                              (n.requiredStage == 0 &&
                                               (enc.phase == encounters::Phase::Dormant ||
                                                enc.phase == encounters::Phase::Stage4)) ||
                                              (n.requiredStage == int(enc.phase));
                                          if (!usable)
                                          {
                                              continue;
                                          }
                                          const Vector2 wp = we.ToLegacyWorld(enc, n.offset);
                                          Drawing::DrawGlow(Drawing::WorldToScreen(wp.x, wp.y), 24.f,
                                                            {n.glow.r, n.glow.g, n.glow.b, .35f});
                                          Drawing::DrawInteractionMarker(wp, n.label + " [E]", n.glow);
                                      }
                                      for (const auto &npc : enc.npcs)
                                      {
                                          if (!npc.visible)
                                          {
                                              continue;
                                          }
                                          const Vector2 wp = we.ToLegacyWorld(enc, npc.offset);
                                          Drawing::DrawPerson(wp, npc.coat, false, npc.type, npc.escorting, npc.yaw,
                                                              npc.gender, npc.hair);
                                          const Vector2 sp = Drawing::WorldToScreen(wp.x, wp.y, 58.f);
                                          Drawing::DrawLabel(sp.x - 24.f, sp.y, npc.name, {.92f, .96f, 1.f});
                                      }
                                      for (const auto &e : enc.enemies)
                                      {
                                          if (!e.active || e.health <= 0)
                                          {
                                              continue;
                                          }
                                          const Vector2 wp = we.ToLegacyWorld(enc, e.offset);
                                          const ColorRGBA col = e.hitFlash > .1f ? ColorRGBA(1.f, .94f, .78f)
                                                                : e.isBoss       ? ColorRGBA(.86f, .24f, .22f)
                                                                                 : ColorRGBA(.56f, .28f, .26f);
                                          Drawing::DrawPerson(wp, col, false,
                                                              e.isBoss ? models::CharacterType::HeavyCombat
                                                                       : models::CharacterType::Guard,
                                                              true, e.yaw, models::Gender::Male,
                                                              models::HairStyle::Buzz, !e.meleeOnly);
                                          const Vector2 sp = Drawing::WorldToScreen(wp.x, wp.y, 62.f);
                                          const float ratio =
                                              Game::Clamp(float(e.health) / float((std::max)(1, e.maxHealth)), 0.f, 1.f);
                                          Drawing::DrawRectangle(sp.x - 22.f, sp.y, 44.f, 5.f, {.05f, .08f, .10f, .85f});
                                          Drawing::DrawRectangle(sp.x - 21.f, sp.y + 1.f, 42.f * ratio, 3.f,
                                                                 e.isBoss ? ColorRGBA(1.f, .64f, .22f)
                                                                          : ColorRGBA(.96f, .32f, .30f));
                                      }
                                      for (const auto &tr : enc.tracers)
                                      {
                                          const Vector2 a =
                                              Drawing::WorldToScreen(enc.legacyAnchor.x + tr.from.x,
                                                                     enc.legacyAnchor.y + tr.from.y, 18.f);
                                          const Vector2 b =
                                              Drawing::WorldToScreen(enc.legacyAnchor.x + tr.to.x,
                                                                     enc.legacyAnchor.y + tr.to.y, 18.f);
                                          Drawing::DrawLine(a, b, {tr.color.r, tr.color.g, tr.color.b, tr.life * 6.f},
                                                            2.f);
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
        if (auto *liaNode = graph.Find("lia"))
        {
            if (graph.Find("marker/lia"))
            {
                graph.Reparent("marker/lia", *liaNode);
            }
        }
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
        rendering::SceneLighting light;
        light.keyDirection = {-.32f, -.40f, .85f};
        light.keyColor = {.98f, 1.03f, 1.08f};
        light.keyIntensity = 1.08f;
        light.skyColor = {.62f, .80f, .96f};
        light.groundColor = {.44f, .52f, .58f};
        light.ambientIntensity = .92f;
        light.rimColor = {.48f, .86f, 1.f};
        light.rimIntensity = .30f;
        light.exposure = 1.22f;
        GameRenderer::Get().Backend().SetLighting(light);
        Drawing::AddPointLight(kObservatoryConsole, 1.0f, 5.5f, {.30f, .92f, 1.f}, 1.05f);
    }
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
                if (story::StoryArc::Get().IsRiaMissing() ||
                    Game::Get().GetState().hasDescendedObservatory ||
                    (Game::Get().SurfaceParty() && Game::Get().SurfaceParty()->Joined(CompanionId::Ria)))
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
                            const NpcCombatState &combat, const MovementFacing &motion)
{
    if (GameLevels::Mine().Lighting().At(position) < .025f)
    {
        return;
    }
    scene.PlaceWorldActor(
        id, position.x, position.y, WorldBounds(position.x, position.y, 0, 0, 85, 100), 1,
        position.x + position.y,
        [this, position, coat, type, walking, label, facingYaw, gender, hair, combat, motion, id]() {
            bool downed = combat.health <= 0;
            Drawing::DrawPerson(
                position, coat, false, type, walking && !downed, combat.engaged ? combat.aimYaw : facingYaw,
                gender, hair,
                combat.engaged && !downed && (type == models::CharacterType::Guard || id == "ria"),
                combat.muzzleFlash, downed, 2.1f,
                combat.pistol.IsReloading() ? combat.pistol.GetReloadProgress() : -1, &motion,
                &combat.support);
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
    {
        rendering::SceneLighting light;
        light.keyDirection = {-.25f, -.30f, .92f};
        light.keyColor = {.88f, .94f, 1.00f};
        light.keyIntensity = .82f;
        light.skyColor = {.46f, .58f, .70f};
        light.groundColor = {.38f, .36f, .42f};
        light.ambientIntensity = .84f;
        light.rimColor = {.42f, .78f, .92f};
        light.rimIntensity = .26f;
        light.exposure = 1.24f;
        light.shadowStrength = .32f;
        GameRenderer::Get().Backend().SetLighting(light);
        const auto &w = Game::Get().GetState();
        Drawing::AddPointLight(w.playerPosition, 1.05f, 6.2f, {.76f, .92f, 1.f}, 1.10f);
        Drawing::AddPointLight(kPowerBypass, 1.1f, 5.2f, {1.f, .68f, .28f}, .95f);
        Drawing::AddPointLight(kRuinSignal, 1.2f, 6.5f, {.38f, .94f, 1.f}, 1.15f);
        Drawing::AddPointLight(kObserverGlyph, 1.3f, 6.8f, {.58f, .44f, 1.f}, 1.15f);
        Drawing::AddPointLight(kCoordinateTerminal, 1.1f, 5.5f, {.34f, .96f, .88f}, .95f);
    }
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
                    npc.facing.yaw, npc.gender, npc.hair, npc.combat, npc.facing);
    }
    if (Game::Get().HasCompanion(CompanionId::Ria, CompanionPlace::Mine))
    {
        PlacePerson(scene, "ria", GameLevels::Mine().GetState().riaPosition, {.95f, .55f, .25f},
                    models::CharacterType::Engineer, GameLevels::Mine().GetState().riaFacing.walking, "리아",
                    GameLevels::Mine().GetState().riaFacing.yaw, models::Gender::Female,
                    models::HairStyle::Ponytail, GameLevels::Mine().GetState().riaCombat,
                    GameLevels::Mine().GetState().riaFacing);
    }
    if (Game::Get().HasCompanion(CompanionId::Kain, CompanionPlace::Mine))
    {
        const auto &npc = GameLevels::Mine().GetState().kain;
        PlacePerson(scene, "kain", npc.position, {.38f, .53f, .66f}, models::CharacterType::Guard,
                    npc.facing.walking, "카인", npc.facing.yaw, models::Gender::Male, models::HairStyle::Buzz,
                    npc.combat, npc.facing);
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
    {
        rendering::SceneLighting light;
        light.keyDirection = {-.48f, -.36f, .76f};
        light.keyColor = {1.10f, .99f, .85f};
        light.keyIntensity = 1.16f;
        light.skyColor = {.64f, .80f, .92f};
        light.groundColor = {.58f, .50f, .38f};
        light.ambientIntensity = .92f;
        light.rimColor = {.92f, .78f, .56f};
        light.rimIntensity = .26f;
        light.exposure = 1.22f;
        GameRenderer::Get().Backend().SetLighting(light);
        Drawing::AddPointLight({10.5f, 10.5f}, 1.2f, 6.0f, {.34f, .92f, .96f}, .95f);
        Drawing::AddPointLight({32.5f, 12.5f}, 1.1f, 5.5f, {1.f, .74f, .34f}, .85f);
        Drawing::AddPointLight({33.5f, 32.5f}, 1.2f, 6.0f, {.52f, .76f, 1.f}, .90f);
    }
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
                                                                    : -1,
                                    &npc.facing, &npc.combat.support);
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
    // Helper: estimate local terrain slope (dz/dx, dz/dy in world units) near (wx, wy) from chunk terrain grid
    // so intersection pads & roundabouts conform smoothly to hillside slopes without clipping into roads.
    auto terrainSlopeAt = [&](double wx, double wy, double &dzdx, double &dzdy) {
        dzdx = 0.;
        dzdy = 0.;
        if (chunk.cells == 0 || positions.size() < size_t((chunk.cells + 1) * (chunk.cells + 1)))
        {
            return;
        }
        double bestDist2 = 1.e30;
        unsigned bestX = 0, bestY = 0;
        const unsigned stride = chunk.cells + 1;
        for (unsigned y = 0; y < chunk.cells; ++y)
        {
            for (unsigned x = 0; x < chunk.cells; ++x)
            {
                const auto &p = positions[y * stride + x];
                const double dx = double(p.x) - wx, dy = double(p.y) - wy;
                const double d2 = dx * dx + dy * dy;
                if (d2 < bestDist2)
                {
                    bestDist2 = d2;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        const auto &p00 = positions[bestY * stride + bestX];
        const auto &p10 = positions[bestY * stride + bestX + 1];
        const auto &p01 = positions[(bestY + 1) * stride + bestX];
        const double ux = double(p10.x - p00.x), uy = double(p10.y - p00.y), uz = double(p10.z - p00.z);
        const double vx = double(p01.x - p00.x), vy = double(p01.y - p00.y), vz = double(p01.z - p00.z);
        const double det = ux * vy - uy * vx;
        if (std::fabs(det) > 1.e-6)
        {
            dzdx = std::clamp((uz * vy - uy * vz) / det, -.35, .35);
            dzdy = std::clamp((ux * vz - uz * vx) / det, -.35, .35);
        }
    };
    for (const auto &road : chunk.roads)
    {
        if (road.points.size() < 2)
        {
            continue;
        }
        // Filter duplicate consecutive projected points and build continuous 3D miter normals so sloped
        // roads and curves share exact vertices with zero horizontal cap chevrons and zero V-gaps.
        std::vector<world::Vector3d> pts;
        pts.reserve(road.points.size());
        for (const auto &rp : road.points)
        {
            const auto proj = SurfaceProjection(rp, chunk.frame, radius);
            if (!pts.empty() && std::hypot(proj.x - pts.back().x, proj.y - pts.back().y) < .01)
            {
                continue;
            }
            pts.push_back(proj);
        }
        const std::size_t nPts = pts.size();
        if (nPts < 2)
        {
            continue;
        }
        std::vector<double> segNx(nPts - 1, 0.), segNy(nPts - 1, 1.);
        for (std::size_t i = 0; i + 1 < nPts; ++i)
        {
            const double dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y;
            const double len = std::hypot(dx, dy);
            segNx[i] = -dy / len;
            segNy[i] = dx / len;
        }
        std::vector<double> miterX(nPts, 0.), miterY(nPts, 1.), miterScale(nPts, 1.);
        miterX[0] = segNx[0];
        miterY[0] = segNy[0];
        miterX[nPts - 1] = segNx[nPts - 2];
        miterY[nPts - 1] = segNy[nPts - 2];
        for (std::size_t i = 1; i + 1 < nPts; ++i)
        {
            const double ax = segNx[i - 1] + segNx[i], ay = segNy[i - 1] + segNy[i];
            const double alen = std::hypot(ax, ay);
            if (alen > 1.e-4)
            {
                const double mx = ax / alen, my = ay / alen;
                const double denom = mx * segNx[i - 1] + my * segNy[i - 1];
                miterX[i] = mx;
                miterY[i] = my;
                miterScale[i] = (std::min)(1.35, 1. / (std::max)(.5, denom));
            }
            else
            {
                miterX[i] = segNx[i];
                miterY[i] = segNy[i];
                miterScale[i] = 1.;
            }
        }
        const bool isUnderpass =
            road.kind == world::StreetKind::Underpass || road.kind == world::StreetKind::UnderpassRamp;
        const bool isElevated =
            road.kind == world::StreetKind::Elevated || road.kind == world::StreetKind::ElevatedRamp;
        const bool isBridge = road.kind == world::StreetKind::Bridge;
        const std::array<float, 4> roadAsphalt =
            isUnderpass ? std::array<float, 4>{.12f, .15f, .19f, 1}
                        : std::array<float, 4>{.16f, .2f, .23f, 1};
        const std::array<float, 4> roadApron =
            road.sidewalkMeters >= 1.5 ? std::array<float, 4>{.38f, .43f, .43f, 1}
                                       : std::array<float, 4>{.31f, .35f, .36f, 1};
        for (std::size_t i = 1; i < nPts; ++i)
        {
            const auto &a = pts[i - 1];
            const auto &b = pts[i];
            const double t0 = double(i - 1) / double((std::max)(std::size_t(1), nPts - 1));
            const double t1 = double(i) / double((std::max)(std::size_t(1), nPts - 1));
            const double wStart = (road.kind == world::StreetKind::Taper && road.startWidthMeters > .5)
                                      ? (road.startWidthMeters + (road.widthMeters - road.startWidthMeters) * t0)
                                      : road.widthMeters;
            const double wEnd = (road.kind == world::StreetKind::Taper && road.startWidthMeters > .5)
                                    ? (road.startWidthMeters + (road.widthMeters - road.startWidthMeters) * t1)
                                    : road.widthMeters;
            auto taperedStrip = [&](double wA, double wB, double offA, double offB, double lift,
                                    std::array<float, 4> color) {
                const double lA = (offA + wA * .5) * miterScale[i - 1];
                const double rA = (offA - wA * .5) * miterScale[i - 1];
                const double lB = (offB + wB * .5) * miterScale[i];
                const double rB = (offB - wB * .5) * miterScale[i];
                const double nxA = miterX[i - 1], nyA = miterY[i - 1];
                const double nxB = miterX[i], nyB = miterY[i];
                models::Point p{float((a.x + nxA * lA) / 2), float((a.y + nyA * lA) / 2),
                                float((a.z + lift) / 2)},
                    q{float((b.x + nxB * lB) / 2), float((b.y + nyB * lB) / 2), float((b.z + lift) / 2)},
                    r{float((a.x + nxA * rA) / 2), float((a.y + nyA * rA) / 2), float((a.z + lift) / 2)},
                    s{float((b.x + nxB * rB) / 2), float((b.y + nyB * rB) / 2), float((b.z + lift) / 2)},
                    n{0, 0, 1};
                triangle({p, r, q}, {n, n, n}, color);
                triangle({q, r, s}, {n, n, n}, color);
            };
            auto strip = [&](double width, double offset, double lift, std::array<float, 4> color) {
                taperedStrip(width, width, offset, offset, lift, color);
            };
            if (road.kind == world::StreetKind::PedestrianPlaza)
            {
                taperedStrip(wStart, wEnd, 0, 0, .01, {.46f, .51f, .52f, 1});
                taperedStrip(wStart * .92, wEnd * .92, 0, 0, .022, {.54f, .59f, .60f, 1});
                continue;
            }
            if (road.sidewalkMeters > 0)
            {
                taperedStrip(wStart + road.sidewalkMeters * 2, wEnd + road.sidewalkMeters * 2, 0, 0, 0, roadApron);
            }
            taperedStrip(wStart, wEnd, 0, 0, .015, roadAsphalt);
            if (isElevated || isBridge || isUnderpass)
            {
                // Side guardrails / retaining walls + underpass cyan guide strip.
                const double halfA = wStart * .5 + .45, halfB = wEnd * .5 + .45;
                const std::array<float, 4> railColor =
                    isUnderpass ? std::array<float, 4>{.24f, .30f, .36f, 1}
                                : std::array<float, 4>{.48f, .54f, .58f, 1};
                taperedStrip(.45, .45, halfA, halfB, isUnderpass ? .55 : .35, railColor);
                taperedStrip(.45, .45, -halfA, -halfB, isUnderpass ? .55 : .35, railColor);
                if (isUnderpass)
                {
                    taperedStrip(.12, .12, halfA - .32, halfB - .32, .38, {.22f, .86f, .92f, 1});
                    taperedStrip(.12, .12, -halfA + .32, -halfB + .32, .38, {.22f, .86f, .92f, 1});
                }
            }
            // Draw standard centreline & lane markings on urban streets AND regional intercity highways.
            if (road.hasMedian && road.lanes >= 6)
            {
                strip(1.3, 0, .04, {.34f, .40f, .40f, 1});
                strip(.75, 0, .06, {.25f, .44f, .33f, 1});
            }
            else if (road.hasMedian && road.lanes >= 4)
            {
                strip(.12, -.22, .03, {.85f, .73f, .34f, 1});
                strip(.12, .22, .03, {.85f, .73f, .34f, 1});
            }
            else
            {
                strip(.12, 0, .03, {.82f, .71f, .36f, 1});
            }
            if (road.sidewalkMeters < 1.5 && wStart >= 6.)
            {
                // Crisp outer white edge lines for regional highways & bridges.
                taperedStrip(.10, .10, wStart * .5 - .35, wEnd * .5 - .35, .028, {.66f, .72f, .73f, 1});
                taperedStrip(.10, .10, -(wStart * .5 - .35), -(wEnd * .5 - .35), .028, {.66f, .72f, .73f, 1});
            }
            for (unsigned lane = 1; lane < road.lanes; ++lane)
            {
                if (lane * 2 == road.lanes)
                {
                    continue;
                }
                const double frac = double(lane) / double(road.lanes) - .5;
                taperedStrip(.09, .09, wStart * frac, wEnd * frac, .03, {.61f, .68f, .68f, 1});
            }
        }
    }
    // CQ-04B: Render explicit slope-conforming SurfaceIntersection pads, roundabouts, and box-culvert slabs.
    for (const auto &inter : chunk.intersections)
    {
        const auto c = SurfaceProjection(inter.center, chunk.frame, radius);
        // Sit slightly above road deck/markings and tilt along local hillside slope so zero clipping occurs.
        const double cx = c.x / 2., cy = c.y / 2., cz = (c.z + .108) / 2.;
        double dzdx = 0., dzdy = 0.;
        terrainSlopeAt(cx, cy, dzdx, dzdy);
        auto padZ = [&](double px, double py, double lift) {
            return float(cz + dzdx * (px - cx) + dzdy * (py - cy) + lift);
        };
        const models::Point n{0, 0, 1};
        auto disc = [&](double innerR, double outerR, double lift, unsigned segs, std::array<float, 4> col,
                        double rotYaw = 0.) {
            for (unsigned s = 0; s < segs; ++s)
            {
                const double a0 = rotYaw + double(s) * (6.28318530718 / segs);
                const double a1 = rotYaw + double(s + 1) * (6.28318530718 / segs);
                const double c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                const double ox0 = cx + c0 * outerR, oy0 = cy + s0 * outerR;
                const double ox1 = cx + c1 * outerR, oy1 = cy + s1 * outerR;
                models::Point o0{float(ox0), float(oy0), padZ(ox0, oy0, lift)};
                models::Point o1{float(ox1), float(oy1), padZ(ox1, oy1, lift)};
                if (innerR <= .01)
                {
                    models::Point centerPt{float(cx), float(cy), float(cz + lift)};
                    triangle({centerPt, o0, o1}, {n, n, n}, col);
                }
                else
                {
                    const double ix0 = cx + c0 * innerR, iy0 = cy + s0 * innerR;
                    const double ix1 = cx + c1 * innerR, iy1 = cy + s1 * innerR;
                    models::Point i0{float(ix0), float(iy0), padZ(ix0, iy0, lift)};
                    models::Point i1{float(ix1), float(iy1), padZ(ix1, iy1, lift)};
                    triangle({i0, o0, o1}, {n, n, n}, col);
                    triangle({i0, o1, i1}, {n, n, n}, col);
                }
            }
        };
        const double rWorld = inter.radiusMeters * .5;
        if (inter.control == world::IntersectionControl::Roundabout && inter.islandRadiusMeters > 1.)
        {
            const double islW = inter.islandRadiusMeters * .5;
            // Sidewalk apron + annular asphalt carriageway + raised central green island + yield ring.
            disc(islW - .3, rWorld + 1.2, 0., 20, {.38f, .43f, .43f, 1});
            disc(islW, rWorld, .01, 20, {.16f, .2f, .23f, 1});
            disc(islW + .15, islW + .3, .02, 20, {.82f, .71f, .36f, 1});
            disc(0., islW, .06, 16, {.36f, .42f, .42f, 1});
            disc(0., islW * .82, .08, 16, {.24f, .44f, .33f, 1});
        }
        else if (inter.control == world::IntersectionControl::GradeSeparated && inter.underpassBelow)
        {
            // Short box-culvert plaza roof slab over the -5.4m underpass (cutaway when player enters below).
            disc(0., rWorld * 1.18, .02, 8, {.46f, .51f, .53f, 1}, inter.mainAxisYaw + .392699);
            disc(0., rWorld * .94, .035, 8, {.54f, .59f, .60f, 1}, inter.mainAxisYaw + .392699);
        }
        else
        {
            // Clean axis-aligned octagonal asphalt pad covering the intersection core without X-crossing markings.
            const std::array<float, 4> apronCol =
                inter.maxLanes >= 4 ? std::array<float, 4>{.38f, .43f, .43f, 1}
                                    : std::array<float, 4>{.33f, .38f, .39f, 1};
            disc(0., rWorld * 1.12, 0., 8, apronCol, inter.mainAxisYaw + .392699);
            disc(0., rWorld * 1.02, .01, 8, {.16f, .2f, .23f, 1}, inter.mainAxisYaw + .392699);
            // Stop-line / crosswalk accent bands on cardinal approaches.
            if (inter.control == world::IntersectionControl::Signalized)
            {
                for (int arm = 0; arm < 4; ++arm)
                {
                    const double ang = inter.mainAxisYaw + arm * 1.5707963268;
                    const double ca = std::cos(ang), sa = std::sin(ang);
                    const double nx = -sa, ny = ca;
                    const double dist = rWorld * .88;
                    const double halfSpan = rWorld * .50;
                    const double x0 = cx + ca * (dist - .28) + nx * halfSpan, y0 = cy + sa * (dist - .28) + ny * halfSpan;
                    const double x1 = cx + ca * (dist + .28) + nx * halfSpan, y1 = cy + sa * (dist + .28) + ny * halfSpan;
                    const double x2 = cx + ca * (dist - .28) - nx * halfSpan, y2 = cy + sa * (dist - .28) - ny * halfSpan;
                    const double x3 = cx + ca * (dist + .28) - nx * halfSpan, y3 = cy + sa * (dist + .28) - ny * halfSpan;
                    models::Point p0{float(x0), float(y0), padZ(x0, y0, .022)};
                    models::Point p1{float(x1), float(y1), padZ(x1, y1, .022)};
                    models::Point p2{float(x2), float(y2), padZ(x2, y2, .022)};
                    models::Point p3{float(x3), float(y3), padZ(x3, y3, .022)};
                    triangle({p0, p2, p1}, {n, n, n}, {.68f, .75f, .76f, 1});
                    triangle({p1, p2, p3}, {n, n, n}, {.68f, .75f, .76f, 1});
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
    case F::MeasuredBuilding:
        return models::ModelLibrary::MeasuredBuilding(
            models::MeasuredBuildingType(variant % unsigned(models::MeasuredBuildingType::Count)));
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
struct ScreenFrustum
{
    Vector2 minPt, maxPt;
    static ScreenFrustum Current()
    {
        return {WorldCamera::FromViewport({0, 0}),
                WorldCamera::FromViewport({WorldCamera::LogicalWidth(), float(kCanvasHeight)})};
    }
    bool Visible(Vector2 screen, float padLeft, float padRight, float padUp, float padDown) const
    {
        return screen.x + padRight >= minPt.x && screen.x - padLeft <= maxPt.x &&
               screen.y + padDown >= minPt.y && screen.y - padUp <= maxPt.y;
    }
};
void DrawSurfaceStructure(const world::SurfaceStructure &structure, const world::SurfaceSession &session,
                          const ScreenFrustum &frustum, Vector2 playerScreen)
{
    const auto &origin = session.Origin();
    const auto p = SurfaceProjection(structure.frame.origin, origin, session.Planet().radiusMeters);
    const float px = float(p.x / 2), py = float(p.y / 2);
    const auto screen = Drawing::WorldToScreen(px, py, float(p.z / 2) * WorldCamera::IsoZScale());
    const float padH = float((structure.sizeMeters.x + structure.sizeMeters.y) * 11. + structure.sizeMeters.z * 10.) + 96.f;
    const float padUp = float(structure.sizeMeters.z * 24. + (structure.sizeMeters.x + structure.sizeMeters.y) * 6.) + 96.f;
    const float padDown =
        float((structure.sizeMeters.x + structure.sizeMeters.y) * 6. + structure.sizeMeters.z * 12. +
              structure.foundationMeters * 24.) +
        96.f;
    if (!frustum.Visible(screen, padH, padH, padUp, padDown))
    {
        return;
    }
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
    auto x = world::SurfaceGeometry::TransformDirection({std::cos(structure.yaw), std::sin(structure.yaw), 0},
                                                        structure.frame, origin);
    auto y = world::SurfaceGeometry::TransformDirection(
        {-std::sin(structure.yaw), std::cos(structure.yaw), 0}, structure.frame, origin);
    auto z = world::SurfaceGeometry::TransformDirection({0, 0, 1}, structure.frame, origin);
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
    float width = float((structure.sizeMeters.x + structure.sizeMeters.y) * 9);
    if (screen.y > playerScreen.y - 12 && playerScreen.y > screen.y - float(structure.sizeMeters.z * 22) - 20 &&
        std::fabs(screen.x - playerScreen.x) < width)
    {
        palette.body[3] = palette.accent[3] = palette.emission[3] = .28f;
    }
    const auto baseScreen = Drawing::WorldToScreen(px, py);
    if (structure.foundationMeters > .05)
    {
        auto foundation = transform;
        foundation.position = {0, 0, float(p.z / 2)};
        auto shift = foundation.Apply({-float(structure.sizeMeters.x / 4), -float(structure.sizeMeters.y / 4),
                                       -float(structure.foundationMeters / 2)});
        foundation.position = shift;
        Drawing::DrawModel3D(models::ModelLibrary::Get().box, baseScreen,
                             {float(structure.sizeMeters.x / 2), float(structure.sizeMeters.y / 2),
                              float((structure.foundationMeters + structure.FoundationTopMeters()) / 2)},
                             foundation, (px + py) * 36, palette, nullptr, float(p.z / 2));
    }
    Drawing::DrawModel3D(model, baseScreen, scale, transform, (px + py) * 36, palette, nullptr, float(p.z / 2));
}
void DrawPlanetTraffic(const world::SurfaceSession &session)
{
    const auto traffic = Game::Get().SurfaceTraffic();
    if (!traffic)
    {
        return;
    }
    const auto frustum = ScreenFrustum::Current();
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
        const auto scr = Drawing::WorldToScreen(
            position.x, position.y, (height + float(actor.altitudeMeters / 2)) * WorldCamera::IsoZScale());
        const float pad = actor.airborne ? 320.f : 220.f;
        if (!frustum.Visible(scr, pad, pad, pad, pad + float(actor.altitudeMeters * 22.)))
        {
            continue;
        }
        const float yaw =
            float(std::atan2(world::SurfaceGeometry::Dot(actor.heading, session.Origin().forward),
                             world::SurfaceGeometry::Dot(actor.heading, session.Origin().right)));
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
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
    const auto frustum = ScreenFrustum::Current();
    const auto &hero = session.Pose().origin;
    const double communityMaxDist = Game::Get().GetState().aircraft.mounted ? 260. : 120.;
    auto withinDrawRange = [&](const world::SurfacePoint &point) {
        return world::SurfaceGeometry::Distance(session.Planet().radiusMeters, hero, point) < communityMaxDist;
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
            if (!frustum.Visible(Drawing::WorldToScreen(position.x, position.y, height * WorldCamera::IsoZScale()), 160,
                                 160, 200, 140))
            {
                continue;
            }
            const float yaw =
                float(std::atan2(world::SurfaceGeometry::Dot(person.heading, session.Origin().forward),
                                 world::SurfaceGeometry::Dot(person.heading, session.Origin().right)));
            renderqueue::PushMatrix();
            renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
            const ColorRGBA coat = person.hitFlash > 0
                                       ? ColorRGBA(1, .3f, .25f)
                                       : person.hostile
                                             ? ColorRGBA(.86f, .24f, .16f)
                                             : ColorRGBA(.3f + float(person.appearance % 5) * .08f, .5f, .6f);
            Drawing::DrawPerson(
                position, coat, false, person.hostile ? models::CharacterType::HeavyCombat : types[i % 4],
                person.speed > .1f, yaw, models::Gender(person.appearance % 2),
                models::HairStyle((person.appearance >> 8) % size_t(models::HairStyle::Count)), person.hostile,
                0, person.health <= 0, person.speed / 2, -1, &person.motion);
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
            renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
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
        const auto *planetPtr = session.Stream().Snapshot() ? session.Stream().Snapshot()->planet.get() : nullptr;
        terminal(site.contact, site.task == 2 ? "현장 기록 제출" : "선택 의뢰",
                 site.task != 1 && site.task != 3);
        terminal(site.target, PlanetCommunity::ZoneTaskName(site, planetPtr), site.task == 1);
    }
    // CQ-04 C2: Render visible doorway markers and color-coded facility signs on all nearby enterable
    // urban facilities (within 46m) so players can identify Market / Clinic / Workshop / CivicHall at a glance.
    const auto snapshot = session.Stream().Snapshot();
    if (snapshot && snapshot->planet)
    {
        const double planetR = session.Planet().radiusMeters;
        size_t facilityBadges = 0;
        for (const auto *item : session.NearbyStructures())
        {
            if (!item || facilityBadges >= 18)
            {
                continue;
            }
            const auto kind = Game::EnterableUrbanFacility(*item, snapshot->planet.get());
            if (kind == UrbanFacilityKind::None)
            {
                continue;
            }
            const auto doorPt = Game::FacilityDoorPoint(*item, planetR);
            const double dist = world::SurfaceGeometry::Distance(planetR, hero, doorPt);
            if (dist > 46.)
            {
                continue;
            }
            const auto p = session.Local(doorPt);
            const Vector2 position{float(p.x / 2), float(p.y / 2)};
            const float height = float(p.z / 2);
            if (!frustum.Visible(Drawing::WorldToScreen(position.x, position.y, height * WorldCamera::IsoZScale()), 180,
                                 180, 220, 140))
            {
                continue;
            }
            const ColorRGBA badgeCol = kind == UrbanFacilityKind::Market    ? ColorRGBA(.28f, .94f, .64f)
                                       : kind == UrbanFacilityKind::Clinic  ? ColorRGBA(.32f, .94f, 1.f)
                                       : kind == UrbanFacilityKind::Workshop ? ColorRGBA(1.f, .70f, .24f)
                                                                            : ColorRGBA(.62f, .80f, 1.f);
            renderqueue::PushMatrix();
            renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
            Drawing::DrawGlow(Drawing::WorldToScreen(position.x, position.y, 12), 20, badgeCol);
            Drawing::DrawInteractionMarker(position,
                                           dist < 14. ? Game::FacilityKindName(kind) : Game::FacilityShortLabel(kind),
                                           badgeCol);
            renderqueue::PopMatrix();
            ++facilityBadges;
        }
    }
}
void DrawSurfaceCities(const world::SurfaceSession &session)
{
    auto cities = Game::Get().SurfaceCities();
    if (!cities)
    {
        return;
    }
    const auto frustum = ScreenFrustum::Current();
    const auto &state = Game::Get().GetState();
    const auto planSnap = session.Stream().Snapshot();
    const auto *planetPtr = planSnap ? planSnap->planet.get() : nullptr;
    auto place = [&](int city, Vector2 legacy) -> std::optional<world::Vector3d> {
        if (cities->Status(city).band != CitySimulationBand::Detailed)
        {
            return std::nullopt;
        }
        if (city > 0 && planetPtr && planetPtr->cityCores)
        {
            if (const auto *core = planetPtr->cityCores->Find(Overworld::Cities()[size_t(city)].id);
                core && core->lots.empty() && core->roads.empty())
            {
                return std::nullopt;
            }
        }
        const double cityDrawDist = state.aircraft.mounted ? 260. : 100.;
        auto point = cities->Point(session, city, legacy);
        if (!point || world::SurfaceGeometry::Distance(session.Planet().radiusMeters, *point,
                                                       session.Pose().origin) > cityDrawDist)
        {
            return std::nullopt;
        }
        return session.Local(*point);
    };
    auto person = [&](int city, Vector2 legacy, ColorRGBA coat, models::CharacterType type, bool walking,
                      float yaw, models::Gender gender, models::HairStyle hair, bool downed,
                      const std::string &name, const MovementFacing *motion = nullptr) {
        auto point = place(city, legacy);
        if (!point)
        {
            return;
        }
        Vector2 local{float(point->x / 2), float(point->y / 2)};
        float height = float(point->z / 2);
        if (!frustum.Visible(Drawing::WorldToScreen(local.x, local.y, height * WorldCamera::IsoZScale()), 160, 160,
                             200, 140))
        {
            return;
        }
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
        Drawing::DrawPerson(local, coat, false, type, walking, cities->Yaw(session, city, yaw), gender, hair,
                            false, 0, downed, 0, -1, motion);
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
               r.facing.yaw, r.gender, r.hair, r.health <= 0, r.name, &r.facing);
    }
    if (!story::StoryArc::Get().IsRiaMissing() &&
        (!Game::Get().SurfaceParty() || !Game::Get().SurfaceParty()->Joined(CompanionId::Ria)))
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
        if (planetPtr && planetPtr->cityCores)
        {
            if (const auto *core = planetPtr->cityCores->Find(Overworld::Cities()[size_t(city)].id);
                core && core->roads.empty())
            {
                continue;
            }
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
            const Vector2 carLocal{float(point->x / 2), float(point->y / 2)};
            if (!frustum.Visible(
                    Drawing::WorldToScreen(carLocal.x, carLocal.y, float(point->z / 2) * WorldCamera::IsoZScale()),
                    220, 220, 200, 180))
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
                                      float(point->z / 2) * WorldCamera::IsoZScale() + 3.3f, 1,
                                      models::VehicleType(car.id % 4), palette, car.speed / 10,
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
            if (!frustum.Visible(Drawing::WorldToScreen(float(point->x / 2), float(point->y / 2),
                                                        float(point->z / 2) * WorldCamera::IsoZScale()),
                                 380, 380, 260, 260))
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
                renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
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
                const bool walk = traffic.PedestrianGreen(int(junction), out.road);
                const bool flashing = traffic.PedestrianFlashing(int(junction), out.road);
                const bool flashOn =
                    !flashing || std::fmod(Game::Get().GetState().animationTimeSeconds * 2.5f, 1.f) < .5f;
                ColorRGBA pedestrian = !walk     ? ColorRGBA(.95f, .21f, .28f)
                                       : flashOn ? ColorRGBA(.15f, 1, .78f)
                                                 : ColorRGBA(1, .74f, .18f);
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
                            auto label = Drawing::WorldToScreen(at->x, at->y,
                                                                float(point->z / 2) * WorldCamera::IsoZScale() + 24);
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
        if (!frustum.Visible(
                Drawing::WorldToScreen(float(point->x / 2), float(point->y / 2), height * WorldCamera::IsoZScale()),
                260, 260, 220, 220))
        {
            return;
        }
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
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
            renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
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
        renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
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
    const auto &arcMarker = story::StoryArc::Get().State();
    const char *surfaceMaraLabel = arcMarker.chapter4Complete    ? "마라 / 대피소"
                                   : arcMarker.sectorLossTriggered ? "마라 / 실종 접수"
                                                                   : "마라 / 선택 의뢰";
    for (auto marker : {Marker{kHomeDoor, InteractionTarget::HomeEntrance, "집 단말"},
                        Marker{kLiaPosition, InteractionTarget::Lia, "리아"},
                        Marker{kMaraPosition, InteractionTarget::Mara, surfaceMaraLabel},
                        Marker{kRelayPosition, InteractionTarget::PowerRelay, "전력 장치"},
                        Marker{kBeaconPosition, InteractionTarget::GateBeacon, "전망대"}})
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
        renderqueue::Translate(0, -float(point->z / 2) * WorldCamera::IsoZScale(), float(point->z / 2) * (1296.f / 44));
        Drawing::DrawInteractionMarker({float(point->x / 2), float(point->y / 2)}, marker.label,
                                       {.4f, .9f, .85f});
        renderqueue::PopMatrix();
    }
}
void DrawPlanetParty(const world::SurfaceSession &session)
{
    const auto *party = Game::Get().SurfaceParty();
    if (!party || Game::Get().GetState().car.mounted)
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
        renderqueue::Translate(0, -float(local.z / 2) * WorldCamera::IsoZScale(), float(local.z / 2) * (1296.f / 44));
        Drawing::DrawPerson(position, i == 0 ? ColorRGBA(.9f, .58f, .3f) : ColorRGBA(.38f, .53f, .66f), false,
                            i == 0 ? models::CharacterType::Engineer : models::CharacterType::Guard,
                            m.walking, yaw, i == 0 ? models::Gender::Female : models::Gender::Male,
                            i == 0 ? models::HairStyle::Ponytail : models::HairStyle::Buzz,
                            m.combat.engaged && m.combat.health > 0, m.combat.muzzleFlash,
                            m.combat.health <= 0, 0, -1, &m.motion, &m.combat.support);
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
        renderqueue::Translate(0, -z * WorldCamera::IsoZScale(), z * (1296.f / 44));
        Drawing::DrawPerson(*point, color, false, role, npc.facing.walking,
                            surface.Yaw(npc.combat.engaged ? npc.combat.aimYaw : npc.facing.yaw), gender,
                            hair, armed && npc.combat.engaged && npc.combat.health > 0,
                            npc.combat.muzzleFlash, npc.combat.health <= 0, 2.1f,
                            npc.combat.pistol.IsReloading() ? npc.combat.pistol.GetReloadProgress() : -1,
                            &npc.facing, &npc.combat.support);
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
            renderqueue::Translate(0, -z * WorldCamera::IsoZScale(), z * (1296.f / 44));
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
    double fallbackRadius = 0;
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
    {
        rendering::SceneLighting light;
        const auto loc = Game::Get().PlanetLocation().value_or(session->Location());
        if (loc.regionId != "overworld" && snapshot->planet)
        {
            const auto kind = snapshot->planet->Region(loc.regionId).definition.kind;
            if (kind == world::RegionKind::AncientRuins)
            {
                light.keyDirection = {-.42f, -.38f, .78f};
                light.keyColor = {.94f, .98f, 1.08f};
                light.skyColor = {.56f, .66f, .92f};
                light.groundColor = {.42f, .40f, .52f};
                light.rimColor = {.58f, .78f, 1.f};
                light.rimIntensity = .34f;
            }
            else if (kind == world::RegionKind::Outlaw)
            {
                light.keyDirection = {-.50f, -.34f, .74f};
                light.keyColor = {1.12f, .94f, .80f};
                light.skyColor = {.68f, .68f, .76f};
                light.groundColor = {.56f, .44f, .36f};
                light.rimColor = {.96f, .64f, .40f};
                light.rimIntensity = .28f;
            }
            else if (kind == world::RegionKind::Mine)
            {
                light.keyDirection = {-.44f, -.40f, .78f};
                light.keyColor = {1.08f, .98f, .84f};
                light.skyColor = {.64f, .74f, .84f};
                light.groundColor = {.56f, .48f, .36f};
            }
        }
        const auto &arc = story::StoryArc::Get().State();
        if (Game::Get().SurfaceCities() && Game::Get().SurfaceCities()->ActiveCity() == 0)
        {
            if (arc.cityPhase == story::CityPhase::Uneasy)
            {
                light.keyColor = {.92f, .96f, 1.04f};
                light.groundColor = {.38f, .42f, .48f};
            }
            else if (arc.cityPhase == story::CityPhase::Evacuation)
            {
                light.keyColor = {1.06f, .76f, .64f};
                light.skyColor = {.52f, .42f, .64f};
                light.groundColor = {.36f, .28f, .36f};
            }
            else if (arc.cityPhase == story::CityPhase::Recovery)
            {
                light.keyColor = {.94f, .95f, 1.00f};
                light.skyColor = {.60f, .72f, .82f};
                light.groundColor = {.44f, .48f, .52f};
            }
        }
        GameRenderer::Get().Backend().SetLighting(light);
    }
    const auto terrainView = Game::Get().SurfaceTerrainView();
    auto &d = *data;
    if (d.epoch != snapshot->statistics.epoch)
    {
        ResetResources();
        d.epoch = snapshot->statistics.epoch;
    }
    std::vector<std::shared_ptr<const world::SurfaceChunk>> visible;
    for (const auto &chunk : snapshot->chunks)
    {
        if (world::SurfaceGeometry::Distance(session->Planet().radiusMeters, terrainView.center,
                                             chunk->frame.origin) <
            chunk->radiusMeters + terrainView.radiusMeters)
        {
            visible.push_back(chunk);
        }
    }
    std::sort(visible.begin(), visible.end(), [&](const auto &a, const auto &b) {
        return world::SurfaceGeometry::Dot(a->frame.origin.normal, terrainView.center.normal) >
               world::SurfaceGeometry::Dot(b->frame.origin.normal, terrainView.center.normal);
    });
    if (visible.size() > 32)
    {
        visible.resize(32);
    }
    for (auto it = d.meshes.begin(); it != d.meshes.end();)
    {
        const bool keep = std::any_of(visible.begin(), visible.end(),
                                      [&](const auto &chunk) { return chunk->key == it->first; });
        if (!keep)
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
    if (!d.fallback ||
        world::SurfaceGeometry::Distance(session->Planet().radiusMeters, d.fallbackFrame.origin,
                                         terrainView.center) > 16 ||
        std::abs(d.fallbackRadius - terrainView.radiusMeters) > 1)
    {
        world::SurfaceChunk coarse;
        coarse.frame = world::SurfaceGeometry::Frame(terrainView.center);
        coarse.cells = 16;
        const double step = terrainView.radiusMeters / 8;
        for (unsigned y = 0; y <= coarse.cells; ++y)
        {
            for (unsigned x = 0; x <= coarse.cells; ++x)
            {
                auto point =
                    world::SurfaceGeometry::Advance(coarse.frame, (double(x) - 8) * step,
                                                    (double(y) - 8) * step, session->Planet().radiusMeters)
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
        d.fallbackRadius = terrainView.radiusMeters;
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
    const auto frustum = ScreenFrustum::Current();
    const auto &w = Game::Get().GetState();
    const auto playerScreen =
        Drawing::WorldToScreen(w.playerPosition.x, w.playerPosition.y,
                               float(session->Local(session->Pose().origin).z / 2) * WorldCamera::IsoZScale());
    auto &backend = GameRenderer::Get().Backend();
    backend.BeginModels();
    DrawSurfaceModel(*d.fallback, d.fallbackFrame, *session, -.08f);
    for (const auto &chunk : visible)
    {
        auto it = d.meshes.find(chunk->key);
        if (it == d.meshes.end())
        {
            continue;
        }
        const auto cp = SurfaceProjection(chunk->frame.origin, session->Origin(), session->Planet().radiusMeters);
        const auto cscr =
            Drawing::WorldToScreen(float(cp.x / 2), float(cp.y / 2), float(cp.z / 2) * WorldCamera::IsoZScale());
        const float cpadH = float(chunk->radiusMeters) * 28.f + 260.f;
        const float cpadV = float(chunk->radiusMeters) * WorldCamera::IsoYScale() + 480.f;
        if (frustum.Visible(cscr, cpadH, cpadH, cpadV, cpadV))
        {
            DrawSurfaceModel(*it->second.model, it->second.chunk->frame, *session);
        }
    }
    // CQ-04B: Render live 4-aspect procedural city signals (concurrent walk, flashing walk, left arrow)
    // and grade-separated cutaway guides for active surface chunks.
    {
        const double planetRadius = session->Planet().radiusMeters;
        const int urgentAxis = (CivicJustice::State().wanted >= 2) ? 0 : -1;
        const bool flashPhase = std::fmod(w.animationTimeSeconds * 2.5f, 1.f) < .5f;
        for (const auto &chunk : visible)
        {
            for (const auto &inter : chunk->intersections)
            {
                if (inter.control != world::IntersectionControl::Signalized &&
                    inter.control != world::IntersectionControl::GradeSeparated)
                {
                    continue;
                }
                const auto p = SurfaceProjection(inter.center, session->Origin(), planetRadius);
                const float px = float(p.x / 2.), py = float(p.y / 2.), pz = float(p.z / 2.);
                const auto scr = Drawing::WorldToScreen(px, py, pz * WorldCamera::IsoZScale());
                if (!frustum.Visible(scr, 260, 260, 220, 220))
                {
                    continue;
                }
                const float rW = float(inter.radiusMeters * .5);
                if (inter.control == world::IntersectionControl::Signalized)
                {
                    const auto sig = IntersectionSignalController::Evaluate(
                        inter, double(w.animationTimeSeconds), true, true, true, urgentAxis);
                    auto lightCol = [](TrafficLight l) {
                        return l == TrafficLight::Green   ? ColorRGBA(.18f, 1.f, .76f)
                               : l == TrafficLight::Amber ? ColorRGBA(1.f, .72f, .16f)
                                                          : ColorRGBA(.96f, .22f, .28f);
                    };
                    renderqueue::PushMatrix();
                    renderqueue::Translate(0, -pz * WorldCamera::IsoZScale(), pz * (1296.f / 44.f));
                    for (int arm = 0; arm < 4; ++arm)
                    {
                        const bool isMain = (arm % 2 == 0);
                        const float ang = float(inter.mainAxisYaw) + arm * (kPi * .5f);
                        const Vector2 d{std::cos(ang), std::sin(ang)};
                        const Vector2 n{-d.y, d.x};
                        const Vector2 edgeCenter{px + d.x * (rW * .90f), py + d.y * (rW * .90f)};
                        const bool walk = isMain ? sig.mainWalkGreen : sig.sideWalkGreen;
                        const bool flash = isMain ? sig.mainWalkFlashing : sig.sideWalkFlashing;
                        const ColorRGBA walkCol = !walk            ? ColorRGBA(.95f, .21f, .28f)
                                                  : (!flash || flashPhase) ? ColorRGBA(.15f, 1.f, .78f)
                                                                           : ColorRGBA(1.f, .74f, .18f);
                        Drawing::DrawHologramStrip(
                            {edgeCenter.x - n.x * (rW * .42f), edgeCenter.y - n.y * (rW * .42f)},
                            {edgeCenter.x + n.x * (rW * .42f), edgeCenter.y + n.y * (rW * .42f)}, .12f, walkCol,
                            .09f);
                        const TrafficLight throughL = isMain ? sig.mainThrough : sig.sideThrough;
                        const TrafficLight leftL = isMain ? sig.mainLeft : sig.sideLeft;
                        const bool leftActive = leftL == TrafficLight::Green;
                        const ColorRGBA stopCol = leftActive ? ColorRGBA(.2f, 1.f, .76f) : lightCol(throughL);
                        const Vector2 stopPos{px + d.x * (rW * 1.05f) - n.x * (rW * .24f),
                                              py + d.y * (rW * 1.05f) - n.y * (rW * .24f)};
                        Drawing::DrawHologramStrip({stopPos.x - n.x * .55f, stopPos.y - n.y * .55f},
                                                   {stopPos.x + n.x * .55f, stopPos.y + n.y * .55f}, .16f, stopCol,
                                                   .12f);
                    }
                    renderqueue::PopMatrix();
                }
                else if (inter.control == world::IntersectionControl::GradeSeparated)
                {
                    // Quarter-view cutaway guide strips when player approaches under an overpass or underpass slab.
                    const double distToPlayer =
                        world::SurfaceGeometry::Distance(planetRadius, session->Pose().origin, inter.center);
                    if (distToPlayer < inter.radiusMeters + 18.)
                    {
                        renderqueue::PushMatrix();
                        renderqueue::Translate(0, -pz * WorldCamera::IsoZScale(), pz * (1296.f / 44.f));
                        const float ang = float(inter.mainAxisYaw);
                        const Vector2 d{std::cos(ang), std::sin(ang)};
                        const Vector2 n{-d.y, d.x};
                        const ColorRGBA guideCol = inter.underpassBelow ? ColorRGBA(.22f, .92f, 1.f, .85f)
                                                                        : ColorRGBA(.95f, .82f, .28f, .85f);
                        for (float side : {-1.f, 1.f})
                        {
                            const Vector2 a{px - d.x * rW + n.x * (rW * .55f * side),
                                            py - d.y * rW + n.y * (rW * .55f * side)};
                            const Vector2 b{px + d.x * rW + n.x * (rW * .55f * side),
                                            py + d.y * rW + n.y * (rW * .55f * side)};
                            Drawing::DrawHologramStrip(a, b, .14f, guideCol, .14f);
                        }
                        renderqueue::PopMatrix();
                    }
                }
            }
        }
    }
    {
        static std::unordered_set<std::string_view> drawnStructureIds;
        struct ClearOnExit
        {
            std::unordered_set<std::string_view> &set;
            ~ClearOnExit()
            {
                set.clear();
            }
        } clearDrawnStructureIds{drawnStructureIds};
        drawnStructureIds.clear();
        const double planetRadius = session->Planet().radiusMeters;
        auto submitStructure = [&](const world::SurfaceStructure &structure) {
            if (!drawnStructureIds.insert(structure.id).second)
            {
                return;
            }
            const double maxDist =
                terrainView.radiusMeters + std::hypot(structure.sizeMeters.x, structure.sizeMeters.y) * .5;
            const double limit = maxDist / planetRadius;
            if (limit < 2.)
            {
                const double dx = terrainView.center.normal.x - structure.frame.origin.normal.x;
                const double dy = terrainView.center.normal.y - structure.frame.origin.normal.y;
                const double dz = terrainView.center.normal.z - structure.frame.origin.normal.z;
                if (dx * dx + dy * dy + dz * dz > limit * limit)
                {
                    return;
                }
            }
            if (world::SurfaceGeometry::Distance(planetRadius, terrainView.center, structure.frame.origin) <
                maxDist)
            {
                DrawSurfaceStructure(structure, *session, frustum, playerScreen);
            }
        };
        for (const auto &chunk : visible)
        {
            for (const auto &structure : chunk->structures)
            {
                submitStructure(structure);
            }
        }
        // Collision's bounded neighbourhood is also the object fallback during chunk publication.
        for (auto structure : session->NearbyStructures())
        {
            submitStructure(*structure);
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
    {
        const auto &we = encounters::WorldEncounters::Get();
        const double groundH = session->Pose().origin.altitudeMeters;
        for (const auto &enc : we.State().live)
        {
            if (!enc.surfaceSpace || !enc.activeInRange || enc.phase == encounters::Phase::Completed)
            {
                continue;
            }
            auto toWorld = [&](Vector2 offMeters) {
                const auto sp = we.ToSurfaceWorld(enc, offMeters);
                const auto loc = session->Local(sp);
                return Vector2{float(loc.x * .5), float(loc.y * .5)};
            };
            const Vector2 basePos = toWorld({0.f, 0.f});
            Drawing::AddPointLight(basePos, 1.2f, 7.f, {1.f, .78f, .36f}, .95f);
            for (const auto &n : enc.nodes)
            {
                if (!n.revealed || n.completed)
                {
                    continue;
                }
                const bool usable =
                    (n.requiredStage == 0 &&
                     (enc.phase == encounters::Phase::Dormant || enc.phase == encounters::Phase::Stage4)) ||
                    (n.requiredStage == int(enc.phase));
                if (!usable)
                {
                    continue;
                }
                const Vector2 wp = toWorld(n.offset);
                Drawing::DrawBox(wp.x - .4f, wp.y - .4f, .8f, .8f, 1.3f,
                                 {n.glow.r * .45f, n.glow.g * .45f, n.glow.b * .45f});
                Drawing::DrawGlow(Drawing::WorldToScreen(wp.x, wp.y), 26.f, {n.glow.r, n.glow.g, n.glow.b, .38f});
                Drawing::DrawInteractionMarker(wp, n.label + " [E]", n.glow);
            }
            for (const auto &npc : enc.npcs)
            {
                if (!npc.visible)
                {
                    continue;
                }
                const Vector2 wp = toWorld(npc.offset);
                Drawing::DrawPerson(wp, npc.coat, false, npc.type, npc.escorting, npc.yaw, npc.gender, npc.hair);
                const Vector2 sp = Drawing::WorldToScreen(wp.x, wp.y, 58.f);
                Drawing::DrawLabel(sp.x - 24.f, sp.y, npc.name, {.92f, .96f, 1.f});
            }
            for (const auto &e : enc.enemies)
            {
                if (!e.active || e.health <= 0)
                {
                    continue;
                }
                const Vector2 wp = toWorld(e.offset);
                const ColorRGBA col = e.hitFlash > .1f ? ColorRGBA(1.f, .94f, .78f)
                                      : e.isBoss       ? ColorRGBA(.88f, .24f, .22f)
                                                       : ColorRGBA(.58f, .28f, .26f);
                Drawing::DrawPerson(wp, col, false,
                                    e.isBoss ? models::CharacterType::HeavyCombat : models::CharacterType::Guard, true,
                                    e.yaw, models::Gender::Male, models::HairStyle::Buzz, !e.meleeOnly);
                const Vector2 sp = Drawing::WorldToScreen(wp.x, wp.y, 62.f);
                const float ratio = Game::Clamp(float(e.health) / float((std::max)(1, e.maxHealth)), 0.f, 1.f);
                Drawing::DrawRectangle(sp.x - 22.f, sp.y, 44.f, 5.f, {.05f, .08f, .10f, .85f});
                Drawing::DrawRectangle(sp.x - 21.f, sp.y + 1.f, 42.f * ratio, 3.f,
                                       e.isBoss ? ColorRGBA(1.f, .64f, .22f) : ColorRGBA(.96f, .32f, .30f));
            }
            for (const auto &tr : enc.tracers)
            {
                const Vector2 a = toWorld(tr.from);
                const Vector2 b = toWorld(tr.to);
                Drawing::DrawLine(Drawing::WorldToScreen(a.x, a.y, 18.f), Drawing::WorldToScreen(b.x, b.y, 18.f),
                                  {tr.color.r, tr.color.g, tr.color.b, tr.life * 6.f}, 2.f);
            }
        }
        (void)groundH;
    }
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
        renderqueue::Translate(0, -float(local.z / 2) * WorldCamera::IsoZScale(), float(local.z / 2) * (1296.f / 44));
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
            renderqueue::Translate(0, -float(local.z / 2) * WorldCamera::IsoZScale(), float(local.z / 2) * (1296.f / 44));
            Drawing::DrawInteractionMarker({float(local.x / 2), float(local.y / 2)},
                                           "오르딘 광산 / 하차 후 E 입장", {.4f, .9f, 1});
            renderqueue::PopMatrix();
        }
        if (story::StoryArc::Get().IsActTwoUnlocked() && !story::StoryArc::Get().State().sectorLossTriggered)
        {
            const auto &n0Portal = snapshot->planet->Region("n0_outskirts").access.position;
            if (world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin,
                                                 n0Portal) < 140)
            {
                auto local = session->Local(n0Portal);
                const Vector2 p{float(local.x / 2), float(local.y / 2)};
                renderqueue::PushMatrix();
                renderqueue::Translate(0, -float(local.z / 2) * WorldCamera::IsoZScale(),
                                       float(local.z / 2) * (1296.f / 44));
                Drawing::AddPointLight(p, 1.4f, 6.5f, {.34f, .92f, 1.f}, 1.15f);
                Drawing::DrawInteractionMarker(p, "N-0 외곽 작전 구역 / E 진입", {.34f, .92f, 1.f});
                renderqueue::PopMatrix();
            }
        }
        const auto &arc = story::StoryArc::Get().State();
        if (const auto *cities = Game::Get().SurfaceCities(); cities && cities->ActiveCity() == 0)
        {
            auto haloLocal = [&](Vector2 legacyPt) -> std::optional<world::Vector3d> {
                auto pt = cities->Point(*session, 0, legacyPt);
                return pt ? std::optional<world::Vector3d>(session->Local(*pt)) : std::nullopt;
            };
            if (arc.sectorLossTriggered)
            {
                auto pA = haloLocal({25.f, 2.f}), pB = haloLocal({25.f, 12.f}),
                     pC = haloLocal({31.f, 12.f}), pD = haloLocal({31.f, 2.f});
                if (pA && pB && pC && pD)
                {
                    const float h = float(pA->z / 2);
                    renderqueue::PushMatrix();
                    renderqueue::Translate(0, -h * WorldCamera::IsoZScale(), h * (1296.f / 44));
                    const Vector2 a = Drawing::WorldToScreen(float(pA->x / 2), float(pA->y / 2));
                    const Vector2 b = Drawing::WorldToScreen(float(pB->x / 2), float(pB->y / 2));
                    const Vector2 c = Drawing::WorldToScreen(float(pC->x / 2), float(pC->y / 2));
                    const Vector2 d = Drawing::WorldToScreen(float(pD->x / 2), float(pD->y / 2));
                    Drawing::DrawPolygon({a, b, c, d}, {.03f, .05f, .08f, .92f});
                    Drawing::DrawLine(a, b, {.96f, .44f, .34f, .90f}, 2.5f);
                    renderqueue::PopMatrix();
                }
                if (!arc.chapter4Complete)
                {
                    if (auto sp = haloLocal({25.f, 7.f}))
                    {
                        const float h = float(sp->z / 2);
                        renderqueue::PushMatrix();
                        renderqueue::Translate(0, -h * WorldCamera::IsoZScale(), h * (1296.f / 44));
                        Drawing::DrawInteractionMarker({float(sp->x / 2), float(sp->y / 2)},
                                                       "소실 구획 안전선 확인 [E]", {.98f, .52f, .38f});
                        renderqueue::PopMatrix();
                    }
                }
            }
            if (arc.chapter4Complete)
            {
                if (auto cp = haloLocal({11.f, -2.f}))
                {
                    const float h = float(cp->z / 2);
                    const Vector2 base{float(cp->x / 2), float(cp->y / 2)};
                    renderqueue::PushMatrix();
                    renderqueue::Translate(0, -h * WorldCamera::IsoZScale(), h * (1296.f / 44));
                    Drawing::DrawPerson({base.x - .6f, base.y}, {.36f, .76f, .72f}, false,
                                        models::CharacterType::Researcher, false, .6f, models::Gender::Female,
                                        models::HairStyle::Ponytail);
                    Drawing::DrawPerson({base.x + .5f, base.y - .4f}, {.44f, .64f, .78f}, false,
                                        models::CharacterType::Engineer, false, 1.2f, models::Gender::Male,
                                        models::HairStyle::SidePart);
                    Drawing::DrawPerson({base.x + .3f, base.y + .6f}, {.58f, .62f, .46f}, false,
                                        models::CharacterType::Engineer, false, -.8f, models::Gender::Male,
                                        models::HairStyle::Buzz);
                    if (!arc.epilogueClinicVisited)
                    {
                        Drawing::DrawInteractionMarker(base, "임시 의료소 · 구조 탐사대 3명 [E]", {.38f, .94f, .76f});
                    }
                    renderqueue::PopMatrix();
                }
                if (auto wp = haloLocal({19.f, 2.f}))
                {
                    const float h = float(wp->z / 2);
                    const Vector2 base{float(wp->x / 2), float(wp->y / 2)};
                    renderqueue::PushMatrix();
                    renderqueue::Translate(0, -h * WorldCamera::IsoZScale(), h * (1296.f / 44));
                    Drawing::DrawPerson(base, {.38f, .53f, .66f}, false, models::CharacterType::Guard, false, .7854f,
                                        models::Gender::Male, models::HairStyle::Buzz);
                    if (!arc.epilogueWorkshopVisited)
                    {
                        Drawing::DrawInteractionMarker(base, "정비소 · 카인 · 복구팀 [E]", {1.f, .78f, .36f});
                    }
                    renderqueue::PopMatrix();
                }
                if (GameLevels::Chapter().GetState().portableLightRepaired)
                {
                    if (auto lp = haloLocal({kMaraPosition.x + 1.6f, kMaraPosition.y}))
                    {
                        const float h = float(lp->z / 2);
                        const Vector2 base{float(lp->x / 2), float(lp->y / 2)};
                        renderqueue::PushMatrix();
                        renderqueue::Translate(0, -h * WorldCamera::IsoZScale(), h * (1296.f / 44));
                        if (arc.shelterLampOn)
                        {
                            Drawing::AddPointLight(base, 0.9f, 4.5f, {1.f, .86f, .46f}, 1.15f);
                        }
                        Drawing::DrawInteractionMarker(
                            base, arc.shelterLampOn ? "대피소 휴대 조명 (켜짐) [E]" : "대피소 휴대 조명 (꺼짐) [E]",
                            arc.shelterLampOn ? ColorRGBA(1.f, .88f, .46f) : ColorRGBA(.56f, .64f, .70f));
                        renderqueue::PopMatrix();
                    }
                }
            }
        }
    }
    if (w.car.position && world::SurfaceGeometry::Distance(session->Planet().radiusMeters, *w.car.position,
                                                           session->Pose().origin) < 180)
    {
        const auto local = session->Local(*w.car.position);
        const Vector2 p{float(local.x / 2), float(local.y / 2)};
        models::Palette palette;
        palette.body = {.16f, .46f, .52f, 1};
        if (w.car.health <= 0)
        {
            palette = VehicleWreck::Appearance(VehicleWreck::Opacity(w.car.wreckSeconds, true));
        }
        const float height = float(local.z / 2);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
        Drawing::DrawWorldVehicle(
            p, 0, 1, models::VehicleType::Coupe, palette, std::abs(w.car.speed) / 16.5f,
            w.car.Yaw(session->Origin()), w.car.health <= 0, w.car.mounted,
            1.f - float((std::max)(0, w.car.health)) / float(GroundVehicle::MaxHealth));
        renderqueue::PopMatrix();
    }
    float aircraftAglMeters = 0.f;
    float aircraftYaw = 0.f;
    float aircraftBank = 0.f;
    Vector2 aircraftScreenCenter{WorldCamera::LogicalWidth() * .5f, kCanvasHeight * .57f};
    if (w.aircraft.position &&
        (w.aircraft.mounted ||
         world::SurfaceGeometry::Distance(session->Planet().radiusMeters, *w.aircraft.position,
                                          session->Pose().origin) < 300))
    {
        auto ground = *w.aircraft.position;
        ground.altitudeMeters =
            (std::max)(world::PlanetTerrain::SeaLevelMeters, session->Stream().Contact(ground).heightMeters);
        const auto local = session->Local(ground);
        const float height = float(local.z / 2);
        const float aglMeters = float((std::max)(0., w.aircraft.position->altitudeMeters - ground.altitudeMeters));
        const float aglWorld = aglMeters * .5f;
        const float ax = float(local.x / 2), ay = float(local.y / 2);
        aircraftYaw = w.aircraft.Yaw(session->Origin());
        aircraftBank = w.aircraft.bank;
        const float horizSpeed = float(std::hypot(w.aircraft.velocity.x, w.aircraft.velocity.y));
        const float vertSpeed = std::fabs(float(w.aircraft.velocity.z));
        const float aircraftThrottle =
            (!w.aircraft.mounted || (w.aircraft.grounded && horizSpeed < 1.f && vertSpeed < 0.5f))
                ? 0.f
                : Game::Clamp(horizSpeed / 60.f + vertSpeed / 22.f, 0.f, 1.f);
        if (w.aircraft.mounted)
        {
            aircraftAglMeters = aglMeters;
            aircraftScreenCenter = Drawing::WorldToScreen(ax, ay, (height + aglWorld) * WorldCamera::IsoZScale());
            const float blurAmount = Game::Clamp((aglMeters - 8.f) / 130.f, 0.f, 1.f);
            const float focusDepth = (ax + ay) * 36.f + (height + aglWorld * .55f) * (1296.f / 44.f);
            const float focusDepth01 = 0.5f - focusDepth / 131072.f;
            backend.SetAltitudeEffects(blurAmount, focusDepth01);
        }
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -height * WorldCamera::IsoZScale(), height * (1296.f / 44));
        Drawing::DrawWorldAircraft(
            {ax, ay}, aglWorld, models::AircraftType::Courier, false, aircraftYaw, aircraftBank,
            w.aircraft.health <= 0, VehicleWreck::Opacity(w.aircraft.wreckSeconds, w.aircraft.grounded),
            1.f - float((std::max)(0, w.aircraft.health)) / float(PlayerAircraft::MaxHealth), aircraftThrottle);
        if (!w.Mounted() && w.aircraft.health > 0 && w.aircraft.grounded)
        {
            Drawing::DrawInteractionMarker({ax, ay}, "공용 비행체 / E 탑승", {.4f, .9f, 1});
        }
        renderqueue::PopMatrix();
    }
    if (Game::Distance(w.bikePosition, w.playerPosition) < 80)
    {
        Drawing::DrawWorldBike(w.bikePosition, w.bikeYaw, std::fabs(w.bikeSpeed) / 12.8f, w.isBikeMounted);
    }
    if (!w.Mounted())
    {
        float elevation = float(session->Local(session->Pose().origin).z / 2);
        renderqueue::PushMatrix();
        renderqueue::Translate(0, -elevation * WorldCamera::IsoZScale(), elevation * (1296.f / 44));
        Drawing::DrawPerson(w.playerPosition, {.55f, .68f, .72f}, true);
        renderqueue::PopMatrix();
    }
    // Genshin-style [N] 3D world-space glowing path guidance ribbon from player to tracked objective / quest giver.
    if (w.guidanceSecondsRemaining > 0.f && snapshot && snapshot->planet)
    {
        if (auto target = ActivityJournal::GuidanceTarget())
        {
            using G = world::SurfaceGeometry;
            const double R = session->Planet().radiusMeters;
            const auto start = session->Pose().origin;
            const double dot = G::Dot(start.normal, target->normal);
            if (dot > -1.0 + 1e-12)
            {
                const double totalDist = G::Distance(R, start, *target);
                const float duration = (std::max)(1.f, w.guidanceDurationSeconds);
                const float elapsed = duration - w.guidanceSecondsRemaining;
                const float fadeIn = Game::Clamp(elapsed / .45f, 0.f, 1.f);
                const float fadeOut = Game::Clamp(w.guidanceSecondsRemaining / 1.1f, 0.f, 1.f);
                const float alphaEnv = fadeIn * fadeOut;
                const auto pFrame = session->Pose();
                if (totalDist > 2.5)
                {
                    const world::Vector3d tang = G::Unit({target->normal.x - start.normal.x * dot,
                                                          target->normal.y - start.normal.y * dot,
                                                          target->normal.z - start.normal.z * dot});
                    const double dirR = G::Dot(tang, pFrame.right);
                    const double dirF = G::Dot(tang, pFrame.forward);
                    const double rightR = -dirF;
                    const double rightF = dirR;
                    const double maxMeters = (std::min)(totalDist, 44.0);
                    const int segs = (std::max)(4, int(std::ceil(maxMeters / 2.2)));
                    const float headFrac = std::fmod(elapsed * .62f, 1.35f);
                    std::vector<Vector2> pts;
                    std::vector<float> zs;
                    pts.reserve(size_t(segs + 1));
                    zs.reserve(size_t(segs + 1));
                    for (int i = 0; i <= segs; ++i)
                    {
                        const float frac = float(i) / float(segs);
                        const double d = 1.6 + (maxMeters - 1.6) * double(frac);
                        const double swayEnv = std::sin(frac * kPi);
                        const double sway = std::sin(d * 0.34 - double(w.animationTimeSeconds) * 4.8) * 0.42 * swayEnv;
                        auto pt = G::Advance(pFrame, dirR * d + rightR * sway, dirF * d + rightF * sway, R).origin;
                        pt.altitudeMeters = (std::max)(world::PlanetTerrain::SeaLevelMeters,
                                                       session->Stream().Contact(*snapshot, pt).heightMeters);
                        const auto loc = session->Local(pt);
                        pts.push_back({float(loc.x * .5), float(loc.y * .5)});
                        zs.push_back(float(loc.z * .5));
                    }
                    for (int i = 0; i < segs; ++i)
                    {
                        const float frac = (float(i) + .5f) / float(segs);
                        const float headDist = std::fabs(frac - headFrac);
                        const float headBoost = Game::Clamp(1.f - headDist * 3.8f, 0.f, 1.f);
                        const float wave = .55f + .45f * std::sin(frac * 12.f - w.animationTimeSeconds * 7.5f);
                        const float edgeFade = Game::Clamp((1.f - frac) * 4.f, 0.f, 1.f);
                        const float a = alphaEnv * edgeFade * (.38f + .56f * (wave * .5f + headBoost * .5f));
                        const float avgZ = (zs[size_t(i)] + zs[size_t(i + 1)]) * .5f;
                        const ColorRGBA col{
                            .32f + .66f * headBoost,
                            .92f + .08f * headBoost,
                            .96f - .38f * headBoost,
                            a};
                        renderqueue::PushMatrix();
                        renderqueue::Translate(0, -avgZ * WorldCamera::IsoZScale(), avgZ * (1296.f / 44.f));
                        Drawing::DrawHologramStrip(pts[size_t(i)], pts[size_t(i + 1)], .20f + .14f * headBoost, col,
                                                   .24f);
                        renderqueue::PopMatrix();
                        if (headBoost > .65f || (i % 4 == 2))
                        {
                            const Vector2 mid{(pts[size_t(i)].x + pts[size_t(i + 1)].x) * .5f,
                                              (pts[size_t(i)].y + pts[size_t(i + 1)].y) * .5f};
                            const auto scr = Drawing::WorldToScreen(mid.x, mid.y, (avgZ + .3f) * WorldCamera::IsoZScale());
                            Drawing::DrawGlow(scr, 14.f + 16.f * headBoost, col);
                            if (headBoost > .82f)
                            {
                                Drawing::AddPointLight(mid, avgZ + .7f, 4.5f, col, 1.0f * alphaEnv);
                            }
                        }
                    }
                }
                if (totalDist < 120.0)
                {
                    auto tPt = *target;
                    tPt.altitudeMeters = (std::max)(world::PlanetTerrain::SeaLevelMeters,
                                                    session->Stream().Contact(*snapshot, tPt).heightMeters);
                    const auto tLoc = session->Local(tPt);
                    const Vector2 tp{float(tLoc.x * .5), float(tLoc.y * .5)};
                    const float tz = float(tLoc.z * .5);
                    const auto scrBase = Drawing::WorldToScreen(tp.x, tp.y, (tz + .4f) * WorldCamera::IsoZScale());
                    const auto scrTop = Drawing::WorldToScreen(tp.x, tp.y, (tz + 3.2f) * WorldCamera::IsoZScale());
                    Drawing::DrawLine(scrBase, scrTop, {1.f, .92f, .46f, .82f * alphaEnv}, 3.f);
                    Drawing::DrawGlow(scrBase, 26.f, {.42f, .96f, 1.f, .85f * alphaEnv});
                    Drawing::AddPointLight(tp, tz + 1.2f, 5.5f, {1.f, .9f, .48f}, 1.1f * alphaEnv);
                }
            }
        }
    }
    backend.EndModels();

    // Sparse, non-repeating high-altitude wind gusts & velocity-aligned wingtip trails (never shown when stopped).
    if (w.aircraft.mounted && w.aircraft.position && !w.aircraft.grounded)
    {
        const float speed = float(std::hypot(w.aircraft.velocity.x, w.aircraft.velocity.y));
        const float altFactor = Game::Clamp((aircraftAglMeters - 12.f) / 70.f, 0.f, 1.f);
        const float speedFactor = Game::Clamp((speed - 8.f) / 100.f, 0.f, 1.f);
        const float windStrength = Game::Clamp(altFactor * .62f + speedFactor * .58f, 0.f, 1.f);
        if (windStrength > .04f)
        {
            const float isoY = WorldCamera::IsoYScale();
            const float isoZ = WorldCamera::IsoZScale();
            Vector2 flow{-0.82f, -0.57f};
            if (speed > 4.f)
            {
                const float vx = float(-w.aircraft.velocity.x * .5);
                const float vy = float(-w.aircraft.velocity.y * .5);
                const float sx = (vx - vy) * 36.f;
                const float sy = (vx + vy) * isoY;
                const float sl = std::hypot(sx, sy);
                if (sl > .01f)
                {
                    flow = {sx / sl, sy / sl};
                }
            }
            const Vector2 perp{-flow.y, flow.x};
            const float invZoom = 1.f / (std::max)(.5f, WorldCamera::zoom);
            const float spanX = (WorldCamera::LogicalWidth() * .52f) * invZoom;
            const float spanY = 390.f * invZoom;
            const float clock = w.animationTimeSeconds;
            auto hash01 = [](float a, float b) {
                const float s = std::sin(a * 127.1f + b * 311.7f) * 43758.5453f;
                return s - std::floor(s);
            };
            const int streakCount = 4 + int(windStrength * 8.f);
            for (int i = 0; i < streakCount; ++i)
            {
                const float fi = float(i + 1);
                const float basePhase = hash01(fi, 1.37f);
                const float rate = (0.42f + hash01(fi, 4.91f) * 0.48f) * (0.65f + windStrength * 1.05f + speedFactor * 0.9f);
                const float t = clock * rate + basePhase;
                const float cycle = std::floor(t);
                const float phase = t - cycle;
                // Quiet gap between gusts so streaks feel intermittent and never form repeating lanes.
                if (phase > 0.54f)
                {
                    continue;
                }
                const float normPhase = phase / 0.54f;
                const float rLat = hash01(fi * 3.1f + cycle * 17.3f, 2.71f);
                const float rLen = hash01(fi * 5.7f + cycle * 23.9f, 8.13f);
                const float rAng = (hash01(fi * 7.3f + cycle * 11.1f, 5.47f) - 0.5f) * 0.14f;
                const float ca = std::cos(rAng), sa = std::sin(rAng);
                const Vector2 dir{flow.x * ca - flow.y * sa, flow.x * sa + flow.y * ca};
                const float along = (normPhase - 0.5f) * (spanX * 2.15f);
                const float lateral = (rLat - 0.5f) * (spanY * 2.0f);
                const float len = (36.f + windStrength * 74.f + speedFactor * 52.f) * (0.55f + 0.70f * rLen) * invZoom;
                const Vector2 head{aircraftScreenCenter.x + dir.x * along + perp.x * lateral,
                                   aircraftScreenCenter.y + dir.y * along + perp.y * lateral};
                const Vector2 tail{head.x - dir.x * len, head.y - dir.y * len};
                const float envelope = std::sin(normPhase * kPi);
                const float alpha = envelope * windStrength * (0.10f + 0.16f * rLen);
                Drawing::DrawLine(tail, head, ColorRGBA(.84f, .94f, 1.f, alpha), (0.9f + rLen * 1.1f) * invZoom);
            }
            // Wingtip contrails ONLY while cruising forward (speed > 18 m/s), anchored to rotated wingtips & -velocity.
            if (speed > 18.f)
            {
                const float trailFactor = Game::Clamp((speed - 18.f) / 48.f, 0.f, 1.f);
                const float ribbonAlpha = trailFactor * (0.14f + 0.20f * altFactor);
                const float ribbonLen = (48.f + trailFactor * 96.f) * invZoom;
                models::Transform3D wingXform;
                wingXform.yaw = aircraftYaw;
                wingXform.roll = aircraftBank;
                for (float side : {-1.f, 1.f})
                {
                    const auto tip = wingXform.Apply({-0.38f, side * 1.15f, 0.04f});
                    const Vector2 start{aircraftScreenCenter.x + (tip.x - tip.y) * 36.f,
                                        aircraftScreenCenter.y + (tip.x + tip.y) * isoY - tip.z * isoZ};
                    const Vector2 end{start.x + flow.x * ribbonLen, start.y + flow.y * ribbonLen};
                    Drawing::DrawLine(start, end, ColorRGBA(.80f, .95f, 1.f, ribbonAlpha), 1.6f * invZoom);
                }
            }
        }
    }
}

void StoryOperationScene::Draw()
{
    WorldView view;
    const auto &w = Game::Get().GetState();
    const auto &arc = story::StoryArc::Get().State();
    auto &backend = GameRenderer::Get().Backend();

    // Zone-specific atmospheric R5 lighting
    rendering::SceneLighting light{};
    ColorRGBA groundA{.20f, .24f, .26f, 1.f};
    ColorRGBA groundB{.17f, .21f, .24f, 1.f};
    ColorRGBA gridCol{.28f, .38f, .42f, .35f};
    switch (arc.activeZone)
    {
    case story::OperationZone::Waterway:
        light.keyColor = {.86f, .94f, 1.02f};
        light.skyColor = {.52f, .68f, .78f};
        groundA = {.18f, .24f, .26f, 1.f};
        groundB = {.15f, .20f, .23f, 1.f};
        gridCol = {.30f, .52f, .56f, .32f};
        break;
    case story::OperationZone::CutoffFacility:
        light.keyColor = {1.06f, .76f, .58f};
        light.skyColor = {.56f, .46f, .52f};
        groundA = {.19f, .20f, .23f, 1.f};
        groundB = {.16f, .17f, .20f, 1.f};
        gridCol = {.62f, .38f, .26f, .35f};
        break;
    case story::OperationZone::HaloEvacuation:
        light.keyColor = {1.04f, .72f, .62f};
        light.skyColor = {.54f, .42f, .66f};
        groundA = {.21f, .21f, .25f, 1.f};
        groundB = {.17f, .18f, .22f, 1.f};
        gridCol = {.56f, .34f, .40f, .34f};
        break;
    case story::OperationZone::OuterControl:
        light.keyColor = {.92f, 1.00f, 1.04f};
        light.skyColor = {.58f, .74f, .84f};
        groundA = {.20f, .25f, .28f, 1.f};
        groundB = {.18f, .22f, .25f, 1.f};
        gridCol = {.34f, .64f, .70f, .36f};
        break;
    case story::OperationZone::GateInterior:
        light.keyColor = {.78f, .86f, 1.12f};
        light.skyColor = {.42f, .48f, .78f};
        light.groundColor = {.24f, .26f, .42f};
        groundA = {.12f, .14f, .22f, 1.f};
        groundB = {.10f, .12f, .19f, 1.f};
        gridCol = {.34f, .54f, .88f, .38f};
        break;
    case story::OperationZone::GateCore:
        light.keyColor = {1.08f, .74f, .68f};
        light.skyColor = {.52f, .40f, .66f};
        light.groundColor = {.28f, .22f, .32f};
        groundA = {.13f, .13f, .19f, 1.f};
        groundB = {.10f, .11f, .16f, 1.f};
        gridCol = {.74f, .34f, .40f, .40f};
        break;
    default:
        break;
    }
    backend.SetLighting(light);
    for (const auto &n : arc.nodes)
    {
        if (n.visible)
        {
            Drawing::AddPointLight(n.position, 1.0f, 5.2f, n.completed ? ColorRGBA(.34f, .92f, .68f) : n.glow,
                                   n.completed ? .55f : 1.05f);
        }
    }

    auto toScreen = [](Vector2 p, float h = 0.f) { return Drawing::WorldToScreen(p.x, p.y, h); };

    // 1) Tactical isometric ground grid
    const int maxX = int(arc.worldSize.x);
    const int maxY = int(arc.worldSize.y);
    for (int y = 0; y < maxY; y += 2)
    {
        for (int x = 0; x < maxX; x += 2)
        {
            const Vector2 p0 = toScreen({float(x), float(y)});
            const Vector2 p1 = toScreen({float(x + 2), float(y)});
            const Vector2 p2 = toScreen({float(x + 2), float(y + 2)});
            const Vector2 p3 = toScreen({float(x), float(y + 2)});
            Drawing::DrawPolygon({p0, p1, p2, p3}, ((x + y) / 2) % 2 == 0 ? groundA : groundB);
        }
    }
    for (int x = 0; x <= maxX; x += 4)
    {
        Drawing::DrawLine(toScreen({float(x), 0.f}), toScreen({float(x), float(maxY)}), gridCol, 1.f);
    }
    for (int y = 0; y <= maxY; y += 4)
    {
        Drawing::DrawLine(toScreen({0.f, float(y)}), toScreen({float(maxX), float(y)}), gridCol, 1.f);
    }

    // Green Return Line beacons in GateCore (always legible during combat!)
    if (arc.activeZone == story::OperationZone::GateCore)
    {
        const ColorRGBA lineCol = arc.coreStopped ? ColorRGBA(.34f, 1.f, .64f, .95f) : ColorRGBA(.24f, .86f, .54f, .65f);
        Drawing::DrawLine(toScreen({7.5f, 22.f}), toScreen({29.f, 22.f}), lineCol, 3.2f);
        for (float x = 10.f; x <= 28.f; x += 4.5f)
        {
            Drawing::DrawGlow(toScreen({x, 22.f}), 18.f, lineCol);
        }
    }

    // 2) Anomalies (distortion shimmer rings / Observer Monoliths)
    for (const auto &a : arc.anomalies)
    {
        const Vector2 sp = toScreen(a.position);
        if (!a.monolith)
        {
            const ColorRGBA col =
                a.stabilized ? ColorRGBA(.32f, .92f, .76f, .35f) : ColorRGBA(.58f, .38f, 1.f, .52f);
            Drawing::DrawGlow(sp, a.radius * 18.f, col);
        }
    }

    // Scanner & GateCore Phase Pulse rings
    if (arc.scannerPulseRadius > 0.f)
    {
        const int segs = 36;
        for (int i = 0; i < segs; ++i)
        {
            const float a0 = float(i) * (2.f * kPi / segs);
            const float a1 = float(i + 1) * (2.f * kPi / segs);
            Drawing::DrawLine(
                toScreen({arc.scannerOrigin.x + std::cos(a0) * arc.scannerPulseRadius,
                          arc.scannerOrigin.y + std::sin(a0) * arc.scannerPulseRadius}),
                toScreen({arc.scannerOrigin.x + std::cos(a1) * arc.scannerPulseRadius,
                          arc.scannerOrigin.y + std::sin(a1) * arc.scannerPulseRadius}),
                {.34f, .96f, 1.f, .85f}, 2.2f);
        }
    }
    if (arc.pulseWaveRadius > 0.f)
    {
        const int segs = 40;
        for (int i = 0; i < segs; ++i)
        {
            const float a0 = float(i) * (2.f * kPi / segs);
            const float a1 = float(i + 1) * (2.f * kPi / segs);
            Drawing::DrawLine(
                toScreen({34.f + std::cos(a0) * arc.pulseWaveRadius, 22.f + std::sin(a0) * arc.pulseWaveRadius}),
                toScreen({34.f + std::cos(a1) * arc.pulseWaveRadius, 22.f + std::sin(a1) * arc.pulseWaveRadius}),
                {1.f, .34f, .32f, .92f}, 3.2f);
        }
    }

    // 3) Interactive node floor glows
    for (const auto &n : arc.nodes)
    {
        if (!n.visible)
        {
            continue;
        }
        const Vector2 sp = toScreen(n.position);
        const ColorRGBA col = n.completed ? ColorRGBA(.38f, .86f, .64f, .75f) : n.glow;
        Drawing::DrawGlow(sp, n.completed ? 18.f : 32.f, {col.r, col.g, col.b, .34f});
    }

    // 4) 3D Obstacles, Monoliths, Enemies, NPCs, Player
    renderqueue::Flush();
    backend.BeginModels();
    for (const auto &obs : arc.obstacles)
    {
        const float sx = obs.max.x - obs.min.x;
        const float sy = obs.max.y - obs.min.y;
        Drawing::DrawBox(obs.min.x, obs.min.y, sx, sy, obs.height, obs.color);
    }
    for (const auto &a : arc.anomalies)
    {
        if (a.monolith)
        {
            models::Transform3D tr;
            tr.yaw = a.yaw;
            models::Palette pal;
            pal.body = {.22f, .26f, .38f, 1.f};
            pal.emission = {.48f, .76f, 1.f, 1.f};
            Drawing::DrawModel3D(models::ModelLibrary::Get().relief, toScreen(a.position), {1.35f, 1.35f, 1.35f}, tr,
                                 0.f, pal);
        }
    }
    for (const auto &e : arc.enemies)
    {
        if (!e.active || e.health <= 0)
        {
            continue;
        }
        if (e.kind == story::OpEnemyKind::CoreCoupling)
        {
            const bool unlocked = arc.controlPointsDisabled[0] && arc.controlPointsDisabled[1];
            const ColorRGBA body = e.hitFlash > .1f ? ColorRGBA(1.f, .92f, .72f)
                                   : unlocked       ? ColorRGBA(.96f, .40f, .28f)
                                                    : ColorRGBA(.42f, .46f, .54f);
            Drawing::DrawBox(e.position.x - .62f, e.position.y - .62f, 1.24f, 1.24f, 2.4f, body);
        }
        else
        {
            const ColorRGBA col = e.hitFlash > .1f                             ? ColorRGBA(1.f, .94f, .80f)
                                  : e.kind == story::OpEnemyKind::VoidSentinel ? ColorRGBA(.48f, .36f, .78f)
                                  : e.kind == story::OpEnemyKind::RuinStalker  ? ColorRGBA(.68f, .32f, .28f)
                                                                               : ColorRGBA(.30f, .46f, .64f);
            const models::CharacterType ct = e.kind == story::OpEnemyKind::ObservatoryGuard
                                                 ? models::CharacterType::HeavyCombat
                                                 : models::CharacterType::Guard;
            Drawing::DrawPerson(e.position, col, false, ct, true, e.yaw, models::Gender::Male,
                                models::HairStyle::Buzz, true, 0.f, false);
        }
    }
    for (const auto &npc : arc.npcs)
    {
        if (!npc.visible)
        {
            continue;
        }
        Drawing::DrawPerson(npc.position, npc.coat, false, npc.type, npc.escorting || npc.isCompanion, npc.yaw,
                            npc.gender, npc.hair, npc.isCompanion, 0.f, false);
    }
    Drawing::DrawPerson(w.playerPosition, {.32f, .62f, .76f}, true, models::CharacterType::Explorer,
                        w.movementSpeed > .1f, w.facingYaw, w.playerGender, w.playerHair);
    backend.EndModels();

    // Floating interaction markers rendered cleanly above 3D obstacles
    for (const auto &n : arc.nodes)
    {
        if (!n.visible)
        {
            continue;
        }
        const ColorRGBA col = n.completed ? ColorRGBA(.38f, .86f, .64f, .75f) : n.glow;
        Drawing::DrawInteractionMarker(n.position, n.completed ? (n.label + " (완료)") : (n.label + " [E]"), col);
    }

    // 5) Enemy HP bars, NPC nameplates, bullets & tracers
    for (const auto &e : arc.enemies)
    {
        if (!e.active || e.health <= 0)
        {
            continue;
        }
        const Vector2 sp = toScreen(e.position);
        const float ratio = Game::Clamp(float(e.health) / float((std::max)(1, e.maxHealth)), 0.f, 1.f);
        Drawing::DrawRectangle(sp.x - 22.f, sp.y - 56.f, 44.f, 5.f, {.05f, .08f, .10f, .85f});
        Drawing::DrawRectangle(sp.x - 21.f, sp.y - 55.f, 42.f * ratio, 3.f,
                               e.kind == story::OpEnemyKind::CoreCoupling ? ColorRGBA(1.f, .62f, .26f)
                                                                          : ColorRGBA(.96f, .34f, .32f));
    }
    for (const auto &npc : arc.npcs)
    {
        if (!npc.visible)
        {
            continue;
        }
        const Vector2 sp = toScreen(npc.position);
        Drawing::DrawLabel(sp.x - 24.f, sp.y - 58.f, npc.name, {.88f, .96f, 1.f, .92f});
    }
    for (const auto &b : arc.bullets)
    {
        const Vector2 sp = toScreen(b.position);
        Drawing::DrawEllipse(sp.x, sp.y - 18.f, 4.f, 4.f,
                             b.playerOwned ? ColorRGBA(1.f, .92f, .52f) : ColorRGBA(1.f, .36f, .32f));
    }
    for (const auto &tr : arc.tracers)
    {
        Vector2 a = toScreen(tr.from);
        Vector2 b = toScreen(tr.to);
        a.y -= 18.f;
        b.y -= 18.f;
        Drawing::DrawLine(a, b, {tr.color.r, tr.color.g, tr.color.b, tr.life * 7.f}, 2.f);
    }
}

// ============================================================================
// SceneManager — active-scene selection and resource retirement
// ============================================================================
Scene &SceneManager::Get(SceneKind kind)
{
    static auto scenes = []() {
        std::array<std::unique_ptr<Scene>, static_cast<size_t>(SceneKind::Count)> result;
        result[size_t(SceneKind::Interior)] = std::make_unique<InteriorScene>();
        result[size_t(SceneKind::Exterior)] = std::make_unique<CityScene>();
        result[size_t(SceneKind::Observatory)] = std::make_unique<ObservatoryScene>();
        result[size_t(SceneKind::FacilityInterior)] = std::make_unique<FacilityInteriorScene>();
        result[size_t(SceneKind::Mine)] = std::make_unique<MineScene>();
        result[size_t(SceneKind::Frontier)] = std::make_unique<FrontierScene>();
        result[size_t(SceneKind::Planet)] = std::make_unique<PlanetScene>();
        result[size_t(SceneKind::StoryOperation)] = std::make_unique<StoryOperationScene>();
        result[size_t(SceneKind::Background)] = std::make_unique<BackgroundScene>();
        result[size_t(SceneKind::Hud)] = std::make_unique<HudScene>();
        return result;
    }();
    return *scenes.at(size_t(kind));
}
SceneKind SceneManager::Current()
{
    switch (Game::Get().Mode())
    {
    case WorldMode::StoryOperation:
        return SceneKind::StoryOperation;
    case WorldMode::SurfaceMine:
    case WorldMode::LegacyMine:
        return SceneKind::Mine;
    case WorldMode::SurfaceInterior:
    case WorldMode::LegacyInterior:
        return SceneKind::Interior;
    case WorldMode::SurfaceObservatory:
    case WorldMode::LegacyObservatory:
        return SceneKind::Observatory;
    case WorldMode::SurfaceFacility:
        return SceneKind::FacilityInterior;
    case WorldMode::SurfaceExterior:
        return SceneKind::Planet;
    case WorldMode::LegacyFrontier:
        return SceneKind::Frontier;
    case WorldMode::LegacyCity:
    default:
        return SceneKind::Exterior;
    }
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
