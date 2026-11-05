#include "stdafx.h"
#include "PlayerMobility.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
void PlayerMobility::RequestDodge()
{
    // Requests during recovery are discarded immediately, never queued until the cooldown expires.
    if (cooldown <= 0)
    {
        pending = true;
    }
    else
    {
        profiling::Count(profiling::Counter::DodgeRejected);
    }
}
models::Point PlayerMobility::Update(float dt, models::Point input, float facingYaw, bool sprintHeld,
                                     bool aiming, bool allowed, bool weaponBusy)
{
    if (!std::isfinite(dt) || dt <= 0)
    {
        return {};
    }
    dt = (std::min)(dt, .05f);
    cooldown = (std::max)(0.f, cooldown - dt);
    activeFrame = false;
    sprinting = false;
    if (!allowed)
    {
        Cancel();
        return {};
    }
    if (pending)
    {
        pending = false;
        if (cooldown <= 0 && std::isfinite(input.x) && std::isfinite(input.y) && std::isfinite(facingYaw))
        {
            const float length = std::hypot(input.x, input.y);
            direction = length > .001f ? models::Point{input.x / length, input.y / length, 0}
                                       : models::Point{std::cos(facingYaw), std::sin(facingYaw), 0};
            remaining = DodgeSeconds;
            cooldown = DodgeSeconds + RecoverySeconds - dt;
            profiling::Count(profiling::Counter::DodgeStarted);
        }
    }
    if (remaining > 0)
    {
        const float time = (std::min)(dt, remaining);
        remaining = (std::max)(0.f, remaining - dt);
        // Preserve attack lock for the entire final movement frame (including the last partial step).
        activeFrame = true;
        return {direction.x * DodgeSpeed * time, direction.y * DodgeSpeed * time, 0};
    }
    sprinting = sprintHeld && !aiming && !weaponBusy && std::hypot(input.x, input.y) > .001f;
    return {};
}
void PlayerMobility::Cancel()
{
    pending = sprinting = activeFrame = false;
    remaining = 0;
    direction = {};
}
void PlayerMobility::RestoreCooldown(float seconds)
{
    Cancel();
    cooldown = std::isfinite(seconds) ? std::clamp(seconds, 0.f, DodgeSeconds + RecoverySeconds) : 0;
}
void PlayerMobility::RotateDirection(float x, float y)
{
    const float length = std::hypot(x, y);
    if (std::isfinite(length) && length > .001f)
    {
        direction = {x / length, y / length, 0};
    }
}
bool PlayerMobility::Dodging() const
{
    return pending || remaining > 0 || activeFrame;
}
bool PlayerMobility::Contact(float requestedDistance, float actualDistance)
{
    if (Dodging() && requestedDistance > .001f && actualDistance < requestedDistance * .2f)
    {
        Cancel();
        profiling::Count(profiling::Counter::DodgeContacts);
        return true;
    }
    return false;
}
bool PlayerMobility::Sprinting() const
{
    return sprinting;
}
float PlayerMobility::Cooldown() const
{
    return cooldown;
}
models::Point PlayerMobility::Direction() const
{
    return direction;
}
} // namespace tutorial
