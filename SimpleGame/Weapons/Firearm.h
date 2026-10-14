#pragma once
#include "Weapon.h"
#include <cstdint>

namespace combat
{
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
} // namespace combat
