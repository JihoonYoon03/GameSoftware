#include "stdafx.h"
#include "WorldScene.h"
#include "../TutorialDrawing.h"
#include "../LevelOne.h"
#include <array>
#include <cmath>

namespace tutorial
{
namespace
{
std::array<game::Scene, static_cast<size_t>(WorldSceneKind::Count)> scenes;
}
game::Scene &GetWorldScene(WorldSceneKind kind)
{
    return scenes.at(static_cast<size_t>(kind));
}
void ResetWorldScenes()
{
    for (auto &scene : scenes)
    {
        scene.BeginPlacement();
        scene.EndPlacement();
        scene.GetGraph().Clear();
    }
}
void UpdateWorldScene(float deltaSeconds)
{
    WorldSceneKind kind = levelone::IsActive()
                              ? WorldSceneKind::Hunting
                              : (g_state.isInsideHome ? WorldSceneKind::Interior : WorldSceneKind::Exterior);
    GetWorldScene(kind).GetGraph().Update(deltaSeconds);
    GetWorldScene(WorldSceneKind::Background).GetGraph().Update(deltaSeconds);
    GetWorldScene(WorldSceneKind::Hud).GetGraph().Update(deltaSeconds);
}
game::Bounds WorldBounds(float x, float y, float width, float depth, float height, float margin)
{
    return {(x - y - depth) * 36 - margin, (x + y) * 18 - height - margin, (x + width - y) * 36 + margin,
            (x + width + y + depth) * 18 + margin};
}
void PlaceWorldActor(game::Scene &scene, const std::string &name, float x, float y, game::Bounds bounds,
                     int layer, float depth, std::function<void()> draw)
{
    // 8x8 world-cell groups reject whole regions before testing individual objects.
    std::string group = std::to_string(int(std::floor(x / 8))) + "/" + std::to_string(int(std::floor(y / 8)));
    scene.Place(name, group, bounds, layer, depth * 18, [draw = std::move(draw)](const game::Actor &actor) {
        // The bridge alone knows OpenGL; Actor and SceneGraph do not depend on the renderer.
        auto offset = actor.GetWorldPosition();
        glPushMatrix();
        glTranslatef(offset.x, offset.y, 0);
        draw();
        glPopMatrix();
    });
}
void DrawScene(game::Scene &scene)
{
    scene.EndPlacement();
    Vector2 origin = WorldToScreen(0, 0);
    auto &graph = scene.GetGraph();
    const auto &actors =
        graph.CollectVisible({-origin.x, -origin.y, kCanvasWidth - origin.x, kCanvasHeight - origin.y});
    for (const game::Actor *actor : actors)
    {
        actor->Draw();
    }
}
} // namespace tutorial
