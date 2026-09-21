#pragma once
#include "SceneGraph.h"
#include <unordered_set>

namespace game
{
// Synchronizes legacy placement data into persistent Actor nodes.
// Missing placements are removed at EndPlacement; surviving nodes keep identity.
class Scene
{
  public:
    void BeginPlacement();
    Actor &Place(const std::string &name, const std::string &group, Bounds bounds, int layer, float depth,
                 Actor::DrawCallback callback);
    void EndPlacement();
    SceneGraph &GetGraph();

  private:
    SceneGraph graph;
    std::unordered_set<std::string> placed;
    std::unordered_set<std::string> previous;
};
} // namespace game
