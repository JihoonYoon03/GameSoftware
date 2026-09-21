#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace game
{
struct Position
{
    float x = 0;
    float y = 0;
};

// Bounds in camera-independent projected space. Empty bounds contain no geometry.
struct Bounds
{
    float left = 0;
    float top = 0;
    float right = -1;
    float bottom = -1;
    bool IsEmpty() const;
    bool Intersects(const Bounds &other) const;
    Bounds Translated(Position offset) const;
    void Include(const Bounds &other);
};

class SceneGraph;
class Actor
{
  public:
    using DrawCallback = std::function<void(const Actor &)>;
    explicit Actor(std::string name);
    virtual ~Actor();
    Actor(const Actor &) = delete;
    Actor &operator=(const Actor &) = delete;

    const std::string &GetName() const;
    Actor *GetParent() const;
    Position GetLocalPosition() const;
    Position GetWorldPosition() const;
    void SetPosition(Position position);
    void SetBounds(Bounds bounds);
    void SetVisible(bool visible);
    bool IsVisible() const;
    void SetEnabled(bool enabled);
    void SetDrawOrder(int layer, float depth);
    void SetDrawCallback(DrawCallback callback);
    virtual void Update(float deltaSeconds);
    void Draw() const;

  private:
    friend class SceneGraph;
    void MarkBoundsDirty();
    std::string name;
    Actor *parent = nullptr;
    std::vector<std::unique_ptr<Actor>> children;
    Position localPosition;
    Position worldPosition;
    Bounds localBounds;
    Bounds worldBounds;
    Bounds subtreeBounds;
    bool visible = true;
    bool enabled = true;
    bool boundsDirty = true;
    int layer = 0;
    float depth = 0;
    std::uint64_t order = 0;
    DrawCallback drawCallback;
};
} // namespace game
