#pragma once
#include "ModelLibrary.h"

namespace tutorial
{
// Prototype tuning in local world units (2 metres/unit); no root motion or invulnerability.
class PlayerMobility
{
  public:
    static constexpr float SprintMultiplier = 1.45f;
    static constexpr float DodgeSpeed = 7.5f, DodgeSeconds = .22f;
    static constexpr float MaxStamina = 100.f, DodgeCost = 30.f, SprintCostPerSecond = 18.f;
    static constexpr float RegenPerSecond = 25.f, RegenDelaySeconds = .6f;
    void RequestDodge();
    // Return the requested displacement for a dash; normal movement still uses PlayerMotion.
    models::Point Update(float dt, models::Point input, float facingYaw, bool sprintHeld, bool aiming,
                         bool allowed, bool weaponBusy);
    void Cancel(); // Preserve the resource when menus or transitions cancel an action.
    void RestoreStamina(float amount, float delay = 0);
    void RotateDirection(float x, float y);
    bool Contact(float requestedDistance, float actualDistance);
    bool Dodging() const;
    bool Moving() const
    {
        return remaining > 0 || activeFrame;
    }
    bool Sprinting() const;
    float Stamina() const
    {
        return stamina;
    }
    float RegenDelay() const
    {
        return regenDelay;
    }
    models::Point Direction() const;

  private:
    void Recover(float dt);
    models::Point direction = {};
    float remaining = 0, stamina = MaxStamina, regenDelay = 0;
    bool pending = false, sprinting = false, activeFrame = false;
    bool sprintExhausted = false;
};
} // namespace tutorial
