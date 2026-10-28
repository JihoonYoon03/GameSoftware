#include "stdafx.h"
#include "../Profiler.h"
#include "SceneGraph.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace game
{
SceneGraph::SceneGraph() : root("root")
{
    actors.emplace(root.GetName(), &root);
}
Actor &SceneGraph::GetRoot()
{
    return root;
}
Actor *SceneGraph::Find(const std::string &name) const
{
    auto found = actors.find(name);
    return found == actors.end() ? nullptr : found->second;
}
Actor &SceneGraph::Add(std::unique_ptr<Actor> actor, Actor &parent)
{
    if (traversing || !actor || Find(actor->name) || Find(parent.name) != &parent)
    {
        throw std::logic_error("Invalid scene attachment");
    }
    actor->parent = &parent;
    actor->order = nextOrder++;
    Actor &result = *actor;
    parent.children.push_back(std::move(actor));
    parent.MarkBoundsDirty();
    actors.emplace(result.name, &result);
    return result;
}
void SceneGraph::Unregister(Actor &actor)
{
    for (auto &child : actor.children)
    {
        Unregister(*child);
    }
    actors.erase(actor.name);
}
bool SceneGraph::Remove(const std::string &name)
{
    Actor *actor = Find(name);
    if (traversing || !actor || actor == &root)
    {
        return false;
    }
    auto &siblings = actor->parent->children;
    actor->parent->MarkBoundsDirty();
    auto found = std::find_if(siblings.begin(), siblings.end(),
                              [actor](const auto &value) { return value.get() == actor; });
    Unregister(*actor);
    siblings.erase(found);
    visibleActors.clear();
    return true;
}
bool SceneGraph::Reparent(const std::string &name, Actor &parent)
{
    Actor *actor = Find(name);
    if (traversing || !actor || actor == &root || Find(parent.name) != &parent)
    {
        return false;
    }
    for (Actor *ancestor = &parent; ancestor; ancestor = ancestor->parent)
    {
        if (ancestor == actor)
        {
            return false;
        }
    }
    if (actor->parent == &parent)
    {
        return true;
    }
    auto &siblings = actor->parent->children;
    auto found = std::find_if(siblings.begin(), siblings.end(),
                              [actor](const auto &value) { return value.get() == actor; });
    auto owned = std::move(*found);
    actor->parent->MarkBoundsDirty();
    siblings.erase(found);
    owned->parent = &parent;
    parent.children.push_back(std::move(owned));
    parent.MarkBoundsDirty();
    return true;
}
void SceneGraph::Clear()
{
    if (traversing)
    {
        throw std::logic_error("Cannot clear a traversing scene");
    }
    root.children.clear();
    root.SetPosition({});
    root.SetVisible(true);
    root.SetEnabled(true);
    actors.clear();
    actors.emplace(root.name, &root);
    visibleActors.clear();
    statistics = {};
    nextOrder = 0;
    root.MarkBoundsDirty();
}
void SceneGraph::UpdateNode(Actor &actor, float deltaSeconds)
{
    if (!actor.enabled)
    {
        return;
    }
    actor.Update(deltaSeconds);
    for (auto &child : actor.children)
    {
        UpdateNode(*child, deltaSeconds);
    }
}
void SceneGraph::Update(float deltaSeconds)
{
    traversing = true;
    try
    {
        UpdateNode(root, deltaSeconds);
    }
    catch (...)
    {
        traversing = false;
        throw;
    }
    traversing = false;
}
void SceneGraph::ResolveNode(Actor &actor, Position parentPosition)
{
    Position position = {parentPosition.x + actor.localPosition.x, parentPosition.y + actor.localPosition.y};
    if (!actor.boundsDirty && position.x == actor.worldPosition.x && position.y == actor.worldPosition.y)
    {
        return;
    }
    actor.worldPosition = position;
    profiling::Count(profiling::Counter::SceneBoundsRecomputed);
    actor.worldBounds = actor.localBounds.Translated(actor.worldPosition);
    actor.subtreeBounds = {};
    // Hidden parents hide their descendants, but do not stop their gameplay updates.
    if (!actor.visible)
    {
        return;
    }
    actor.subtreeBounds.Include(actor.worldBounds);
    for (auto &child : actor.children)
    {
        ResolveNode(*child, actor.worldPosition);
        actor.subtreeBounds.Include(child->subtreeBounds);
    }
    actor.boundsDirty = false;
}
void SceneGraph::CollectNode(const Actor &actor, Bounds viewport)
{
    ++statistics.visitedNodes;
    if (!actor.visible || !actor.subtreeBounds.Intersects(viewport))
    {
        ++statistics.rejectedSubtrees;
        return;
    }
    if (actor.drawCallback && actor.worldBounds.Intersects(viewport))
    {
        visibleActors.push_back(&actor);
    }
    for (const auto &child : actor.children)
    {
        CollectNode(*child, viewport);
    }
}
const std::vector<const Actor *> &SceneGraph::CollectVisible(Bounds viewport)
{
    profiling::Scope profileTimer(profiling::Timer::SceneCullSort);
    visibleActors.clear();
    statistics = {};
    ResolveNode(root, {});
    CollectNode(root, viewport);
    std::sort(visibleActors.begin(), visibleActors.end(), [](const Actor *a, const Actor *b) {
        if (a->layer != b->layer)
        {
            return a->layer < b->layer;
        }
        float firstDepth = a->depth + a->worldPosition.y;
        float secondDepth = b->depth + b->worldPosition.y;
        if (firstDepth != secondDepth)
        {
            return firstDepth < secondDepth;
        }
        return a->order < b->order;
    });
    statistics.visibleActors = visibleActors.size();
    profiling::Count(profiling::Counter::SceneVisited, statistics.visitedNodes);
    profiling::Count(profiling::Counter::SceneRejected, statistics.rejectedSubtrees);
    profiling::Count(profiling::Counter::SceneVisible, statistics.visibleActors);
    return visibleActors;
}
const SceneStatistics &SceneGraph::GetStatistics() const
{
    return statistics;
}
} // namespace game
