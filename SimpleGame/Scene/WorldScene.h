#pragma once
#include "Scene.h"
#include <functional>

namespace tutorial
{
enum class WorldSceneKind
{
    Interior,
    Exterior,
    Hunting,
    Background,
    Hud,
    Count
};
game::Scene &GetWorldScene(WorldSceneKind kind);
void ResetWorldScenes();
void UpdateWorldScene(float deltaSeconds);
// Bounds are projected without the camera, so camera movement never moves scene nodes.
game::Bounds WorldBounds(float x, float y, float width, float depth, float height, float margin = 40);
void PlaceWorldActor(game::Scene &scene, const std::string &name, float x, float y, game::Bounds bounds,
                     int layer, float depth, std::function<void()> draw);
void DrawScene(game::Scene &scene);
} // namespace tutorial
