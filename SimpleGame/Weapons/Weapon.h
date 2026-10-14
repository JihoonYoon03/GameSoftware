#pragma once

namespace combat
{
struct Shot
{
    float angle = 0;
    float speed = 0;
    float range = 0;
    int damage = 0;
};

class Weapon
{
  public:
    virtual ~Weapon() = default;
    virtual const char *GetName() const = 0;
    virtual void Update(float deltaSeconds) = 0;
    virtual bool TryFire(float aimAngle, Shot &shot) = 0;
    virtual bool Reload() = 0;
    virtual void Reset() = 0;
};
} // namespace combat
