#include "stdafx.h"
#include "NpcPerception.h"
#include <algorithm>
#include <cmath>

namespace ai
{
namespace
{
constexpr float kMaxMemorySeconds = 30, kWatcherStandOffDistance = 2.5f;
constexpr float kPi = 3.14159266f;
} // namespace
void ThreatMemory::Advance(float dt)
{
    if (!std::isfinite(dt) || dt <= 0)
    {
        return;
    }
    remaining = (std::max)(0.f, remaining - dt);
    if (remaining == 0)
    {
        visible = false;
        source = Stimulus::None;
    }
}
void ThreatMemory::Observe(bool seen, float duration)
{
    visible = false;
    if (seen && std::isfinite(duration) && duration > 0)
    {
        Remember(Stimulus::Sight, duration);
        visible = true;
    }
}
void ThreatMemory::Remember(Stimulus stimulus, float duration)
{
    initialized = true;
    if (stimulus == Stimulus::None || !std::isfinite(duration) || duration <= 0)
    {
        return;
    }
    remaining = (std::max)(remaining, (std::min)(duration, kMaxMemorySeconds));
    source = stimulus;
    if (stimulus != Stimulus::Sight)
    {
        visible = false;
    }
}
void ThreatMemory::Forget()
{
    remaining = 0;
    visible = false;
    source = Stimulus::None;
    initialized = true;
}
bool ThreatMemory::Known() const
{
    return remaining > 0;
}
bool ThreatMemory::Visible() const
{
    return visible && Known();
}
bool ThreatMemory::Initialized() const
{
    return initialized;
}
float ThreatMemory::Remaining() const
{
    return remaining;
}
Stimulus ThreatMemory::Source() const
{
    return source;
}
const char *ThreatMemory::StateName() const
{
    return Visible() ? "visible" : Known() ? "remembered" : "unaware";
}
bool Perception::InSight(models::Point origin, float yaw, models::Point target, Vision vision)
{
    const float x = target.x - origin.x, y = target.y - origin.y, z = target.z - origin.z;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw) ||
        !std::isfinite(vision.range) || vision.range <= 0 || !std::isfinite(vision.halfAngle) ||
        vision.halfAngle < 0 || vision.halfAngle > kPi || !std::isfinite(vision.nearRange) ||
        vision.nearRange < 0 || !std::isfinite(vision.heightTolerance) || vision.heightTolerance < 0)
    {
        return false;
    }
    const float distance = std::hypot(x, y);
    return distance <= vision.range && std::abs(z) <= vision.heightTolerance &&
           (distance <= vision.nearRange ||
            x * std::cos(yaw) + y * std::sin(yaw) >= distance * std::cos(vision.halfAngle));
}
Decision Perception::Decide(Role role, const ThreatMemory &memory, float distance)
{
    if (!memory.Known())
    {
        return Decision::Routine;
    }
    if (role == Role::Officer || role == Role::Predator)
    {
        return memory.Visible() ? Decision::Engage : Decision::Investigate;
    }
    if (role == Role::Watcher && memory.Source() == Stimulus::Sight && distance >= kWatcherStandOffDistance)
    {
        return Decision::Observe;
    }
    return Decision::Withdraw;
}
const char *Perception::DecisionName(Decision decision)
{
    switch (decision)
    {
    case Decision::Withdraw:
        return "withdraw";
    case Decision::Observe:
        return "observe";
    case Decision::Investigate:
        return "investigate";
    case Decision::Engage:
        return "engage";
    default:
        return "routine";
    }
}
const char *Perception::StimulusName(Stimulus stimulus)
{
    switch (stimulus)
    {
    case Stimulus::Sight:
        return "sight";
    case Stimulus::Sound:
        return "sound";
    case Stimulus::Report:
        return "report";
    default:
        return "none";
    }
}
} // namespace ai
