#include "stdafx.h"
#include "LevelOne.h"
#include <algorithm>
#include <cmath>

namespace levelone
{
namespace
{
bool LightPath(tutorial::Vector2 from, tutorial::Vector2 to)
{
    float distance = tutorial::Distance(from, to);
    int steps = (std::max)(1, int(std::ceil(distance / .22f)));
    // Allow the final rock face to receive light, but never illuminate through it.
    for (int i = 0; i < steps; ++i)
    {
        float t = float(i) / steps;
        if (!IsWalkable(int(std::floor(from.x + (to.x - from.x) * t)),
                        int(std::floor(from.y + (to.y - from.y) * t))))
        {
            return false;
        }
    }
    return true;
}
float RadialLight(tutorial::Vector2 from, tutorial::Vector2 to, float radius)
{
    float distance = tutorial::Distance(from, to);
    if (distance >= radius || !LightPath(from, to))
    {
        return 0;
    }
    return (std::min)(1.f, (1 - distance / radius) * 1.8f);
}
} // namespace

float LightAt(tutorial::Vector2 position)
{
    auto player = tutorial::g_state.playerPosition;
    float light = RadialLight(player, position, 2.8f) * .8f;
    float distance = tutorial::Distance(player, position);
    if (distance < 12)
    {
        float sx = (position.x - player.x) - (position.y - player.y);
        float sy = ((position.x - player.x) + (position.y - player.y)) * .5f;
        float length = std::sqrt(sx * sx + sy * sy);
        float angle = GetAimAngle();
        float alignment = length > .001f ? (sx * std::cos(angle) + sy * std::sin(angle)) / length : 1;
        float cone = tutorial::Clamp((alignment - .86f) / .1f, 0, 1);
        if (cone > 0 && LightPath(player, position))
        {
            light = (std::max)(light, cone * (std::min)(1.f, (1 - distance / 12) * 1.65f));
        }
    }
    for (const auto &lamp : g_level.lights)
    {
        if (!lamp.needsPower || g_level.quest.IsPowerBypassed())
        {
            light = (std::max)(light, RadialLight(lamp.position, position, lamp.radius));
        }
    }
    for (const auto &tracer : g_level.tracers)
    {
        float dx = tracer.end.x - tracer.start.x, dy = tracer.end.y - tracer.start.y;
        float squaredLength = dx * dx + dy * dy;
        float t =
            squaredLength > .0001f
                ? tutorial::Clamp(((position.x - tracer.start.x) * dx + (position.y - tracer.start.y) * dy) /
                                      squaredLength,
                                  0, 1)
                : 0;
        tutorial::Vector2 closest = {tracer.start.x + dx * t, tracer.start.y + dy * t};
        light = (std::max)(light, RadialLight(closest, position, 1.3f) * tracer.remaining / .12f);
    }
    if (g_level.muzzleFlash > 0)
    {
        light = (std::max)(light, RadialLight(player, position, 3.5f));
    }
    return tutorial::Clamp(light, 0, 1);
}

} // namespace levelone
