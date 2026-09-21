#include "stdafx.h"
#include "Actor.h"
#include <algorithm>
#include <utility>

namespace game
{
bool Bounds::IsEmpty() const
{
    return right < left || bottom < top;
}
bool Bounds::Intersects(const Bounds &other) const
{
    return !IsEmpty() && !other.IsEmpty() && left <= other.right && right >= other.left &&
           top <= other.bottom && bottom >= other.top;
}
Bounds Bounds::Translated(Position offset) const
{
    if (IsEmpty())
    {
        return {};
    }
    return {left + offset.x, top + offset.y, right + offset.x, bottom + offset.y};
}
void Bounds::Include(const Bounds &other)
{
    if (other.IsEmpty())
    {
        return;
    }
    if (IsEmpty())
    {
        *this = other;
        return;
    }
    left = (std::min)(left, other.left);
    top = (std::min)(top, other.top);
    right = (std::max)(right, other.right);
    bottom = (std::max)(bottom, other.bottom);
}
Actor::Actor(std::string name) : name(std::move(name))
{
}
Actor::~Actor() = default;
const std::string &Actor::GetName() const
{
    return name;
}
Actor *Actor::GetParent() const
{
    return parent;
}
Position Actor::GetLocalPosition() const
{
    return localPosition;
}
Position Actor::GetWorldPosition() const
{
    Position result = localPosition;
    if (parent)
    {
        auto parentPosition = parent->GetWorldPosition();
        result.x += parentPosition.x;
        result.y += parentPosition.y;
    }
    return result;
}
void Actor::SetPosition(Position position)
{
    if (localPosition.x == position.x && localPosition.y == position.y)
    {
        return;
    }
    localPosition = position;
    MarkBoundsDirty();
}
void Actor::SetBounds(Bounds bounds)
{
    if (localBounds.left == bounds.left && localBounds.top == bounds.top &&
        localBounds.right == bounds.right && localBounds.bottom == bounds.bottom)
    {
        return;
    }
    localBounds = bounds;
    MarkBoundsDirty();
}
void Actor::SetVisible(bool value)
{
    if (visible == value)
    {
        return;
    }
    visible = value;
    MarkBoundsDirty();
}
void Actor::MarkBoundsDirty()
{
    boundsDirty = true;
    if (parent)
    {
        parent->MarkBoundsDirty();
    }
}
bool Actor::IsVisible() const
{
    return visible;
}
void Actor::SetEnabled(bool value)
{
    enabled = value;
}
void Actor::SetDrawOrder(int value, float sortDepth)
{
    layer = value;
    depth = sortDepth;
}
void Actor::SetDrawCallback(DrawCallback callback)
{
    drawCallback = std::move(callback);
}
void Actor::Update(float)
{
}
void Actor::Draw() const
{
    if (drawCallback)
    {
        drawCallback(*this);
    }
}
} // namespace game
