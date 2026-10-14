#pragma once
#include <cstdint>

namespace combat
{

// ============================================================================
// Weapon — shared interface
// ============================================================================
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
// ============================================================================
// Firearm — magazine, reload, spread
// ============================================================================
struct FirearmSettings
{
    int magazineSize;
    int damage;
    float fireInterval;
    float reloadSeconds;
    float bulletSpeed;
    float range;
    float minimumSpread;
    float maximumSpread;
    float spreadPerShot;
    float spreadRecovery;
};

class Firearm : public Weapon
{
  public:
    explicit Firearm(FirearmSettings settings);
    void Update(float deltaSeconds) override;
    bool TryFire(float aimAngle, Shot &shot) override;
    bool Reload() override;
    void Reset() override;
    int GetRounds() const;
    int GetMagazineSize() const;
    float GetSpread() const;
    float GetReloadProgress() const;
    bool IsReloading() const;

  private:
    FirearmSettings settings;
    int rounds = 0;
    float cooldown = 0;
    float reloadRemaining = 0;
    float spread = 0;
    std::uint32_t randomState = 0x81b437u;
};
// ============================================================================
// Pistol — firearm specialization
// ============================================================================
class Pistol final : public Firearm
{
  public:
    Pistol();
    const char *GetName() const override;
};
} // namespace combat
