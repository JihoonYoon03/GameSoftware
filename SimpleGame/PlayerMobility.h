#pragma once
#include "ModelLibrary.h"

namespace tutorial
{
// Prototype tuning in local world units (2 metres/unit); no root motion or invulnerability.
class PlayerMobility
{
  public:
    static constexpr float SprintMultiplier = 1.45f;
    static constexpr float DodgeSpeed = 7.5f, DodgeSeconds = .22f, RecoverySeconds = .85f;
    void RequestDodge();
    // Return the requested displacement for a dash; normal movement still uses PlayerMotion.
    models::Point Update(float dt, models::Point input, float facingYaw, bool sprintHeld, bool aiming,
                         bool allowed, bool weaponBusy);
    void Cancel(); // Keep the remaining cooldown when menus/transitions cancel an action.
    void RestoreCooldown(float seconds);
    void RotateDirection(float x, float y);
    bool Contact(float requestedDistance, float actualDistance);
    bool Dodging() const;
    bool Sprinting() const;
    float Cooldown() const;
    models::Point Direction() const;

  private:
    models::Point direction = {};
    float remaining = 0, cooldown = 0;
    bool pending = false, sprinting = false, activeFrame = false;
};
} // namespace tutorial
