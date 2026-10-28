#pragma once
#include "../Actor/Actor.h"
#include <unordered_map>

namespace game
{
struct SceneStatistics
{
    std::size_t visitedNodes = 0;
    std::size_t rejectedSubtrees = 0;
    std::size_t visibleActors = 0;
};

// Owns all nodes. Structural changes are only allowed outside Update/CollectVisible traversal.
class SceneGraph
{
  public:
    SceneGraph();
    Actor &GetRoot();
    Actor *Find(const std::string &name) const;
    Actor &Add(std::unique_ptr<Actor> actor, Actor &parent);
    bool Remove(const std::string &name);
    bool Reparent(const std::string &name, Actor &parent);
    void Clear();
    void Update(float deltaSeconds);
    const std::vector<const Actor *> &CollectVisible(Bounds viewport);
    const SceneStatistics &GetStatistics() const;

  private:
    void Unregister(Actor &actor);
    void UpdateNode(Actor &actor, float deltaSeconds);
    void ResolveNode(Actor &actor, Position parentPosition);
    void CollectNode(const Actor &actor, Bounds viewport);
    Actor root;
    std::unordered_map<std::string, Actor *> actors;
    std::vector<const Actor *> visibleActors;
    SceneStatistics statistics;
    std::uint64_t nextOrder = 0;
    bool traversing = false;
};
} // namespace game
