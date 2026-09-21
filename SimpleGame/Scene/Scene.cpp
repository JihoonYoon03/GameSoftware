#include "stdafx.h"
#include "Scene.h"
#include <utility>

namespace game
{
void Scene::BeginPlacement()
{
    placed.clear();
}
Actor &Scene::Place(const std::string &name, const std::string &group, Bounds bounds, int layer, float depth,
                    Actor::DrawCallback callback)
{
    const std::string groupName = "group/" + group;
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
} // namespace game
