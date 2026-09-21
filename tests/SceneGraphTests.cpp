#include "stdafx.h"
#include "Actor/Actor.h"
#include "Scene/Scene.h"
#include <cassert>
#include <memory>

namespace
{
class CountingActor : public game::Actor
{
  public:
    CountingActor() : Actor("counter")
    {
    }
    void Update(float) override
    {
        ++ticks;
    }
    int ticks = 0;
};

void TestHierarchy()
{
    game::SceneGraph graph;
    auto &parent = graph.Add(std::make_unique<game::Actor>("parent"), graph.GetRoot());
    auto &child = graph.Add(std::make_unique<game::Actor>("child"), parent);
    child.SetBounds({0, 0, 10, 10});
    child.SetDrawCallback([](const game::Actor &) {});
    parent.SetPosition({20, 30});
    child.SetPosition({5, 5});
    assert(child.GetWorldPosition().x == 25);
    assert(graph.CollectVisible({24, 34, 26, 36}).size() == 1);
    assert(graph.CollectVisible({0, 0, 10, 10}).empty());
    assert(graph.GetStatistics().rejectedSubtrees > 0);
    parent.SetVisible(false);
    assert(graph.CollectVisible({0, 0, 100, 100}).empty());
    parent.SetVisible(true);
    assert(graph.CollectVisible({0, 0, 100, 100}).size() == 1);
    // Moving a parent must invalidate its descendants' cached projected bounds.
    parent.SetPosition({200, 300});
    assert(graph.CollectVisible({0, 0, 100, 100}).empty());
    assert(!graph.Reparent("parent", child));
    assert(graph.Reparent("child", graph.GetRoot()));
    assert(graph.CollectVisible({0, 0, 100, 100}).size() == 1);
    assert(graph.Remove("parent"));
    assert(graph.Find("child"));
    assert(graph.Remove("child"));
    assert(graph.CollectVisible({0, 0, 100, 100}).empty());
}

void TestUpdateAndLifetime()
{
    game::SceneGraph graph;
    auto owned = std::make_unique<CountingActor>();
    auto *counter = owned.get();
    auto &parent = graph.Add(std::make_unique<game::Actor>("parent"), graph.GetRoot());
    graph.Add(std::move(owned), parent);
    parent.SetVisible(false);
    graph.Update(.016f);
    assert(counter->ticks == 1);
    parent.SetEnabled(false);
    graph.Update(.016f);
    assert(counter->ticks == 1);
    graph.Remove("parent");
    assert(!graph.Find("counter"));
    graph.Clear();
    assert(graph.Find("root"));
}

void TestPlacementAndOrder()
{
    game::Scene scene;
    const game::Bounds bounds = {0, 0, 20, 20};
    const auto draw = [](const game::Actor &) {};
    scene.BeginPlacement();
    auto *first = &scene.Place("first", "region", bounds, 0, 2, draw);
    scene.Place("second", "region", bounds, 0, 1, draw);
    scene.Place("overlay", "region", bounds, 1, -100, draw);
    scene.EndPlacement();
    const auto &visible = scene.GetGraph().CollectVisible(bounds);
    assert(visible.size() == 3);
    assert(visible[0]->GetName() == "second");
    assert(visible[2]->GetName() == "overlay");
    scene.BeginPlacement();
    assert(&scene.Place("first", "other-region", bounds, 0, 2, draw) == first);
    scene.EndPlacement();
    assert(!scene.GetGraph().Find("second"));
    assert(scene.GetGraph().CollectVisible(bounds).size() == 1);
}
} // namespace

int main()
{
    TestHierarchy();
    TestUpdateAndLifetime();
    TestPlacementAndOrder();
}
