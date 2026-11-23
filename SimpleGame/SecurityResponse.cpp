#include "stdafx.h"
#include "SecurityResponse.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace tutorial
{
namespace
{
const std::array<SecurityResponseProfile, 6> profiles = {{
    {},
    {1, 2, 100, 12, 0, 20, 0, 5, 2.45f, 9.5f, 1.4f, .071f, "초동 대응"},
    {2, 4, 140, 16, 0, 180, 25, 4.5f, 2.7f, 11, 1.2f, .062f, "증원 대응"},
    {3, 6, 180, 20, 0, 400, 60, 4, 2.95f, 12.5f, 1, .053f, "중무장 대응"},
    {4, 8, 220, 24, 0, 650, 105, 3.5f, 3.2f, 14, .8f, .044f, "집중 대응"},
    {5, 10, 260, 28, 0, 900, 165, 3, 3.45f, 15.5f, .6f, .035f, "최대 대응"},
}};
} // namespace
SecurityResponseProfile SecurityResponse::ForTier(int tier, int playerLevel)
{
    auto profile = profiles[size_t(std::clamp(tier, 0, kMaximumTier))];
    if (profile.tier)
    {
        const int maximumLevel = 1 + kGrowthBandLevels * kMaximumGrowthBand;
        profile.growthBand = (std::clamp(playerLevel, 1, maximumLevel) - 1) / kGrowthBandLevels;
        profile.health += profile.growthBand * kHealthPerGrowthBand;
        profile.damage += profile.growthBand * kDamagePerGrowthBand;
    }
    return profile;
}
SecurityRole SecurityResponse::RoleForUnit(int tier, unsigned long long officerId)
{
    tier = std::clamp(tier, 1, kMaximumTier);
    if (tier == 1)
    {
        return SecurityRole::Patrol;
    }
    if (tier == 2)
    {
        return (officerId % 2 == 0) ? SecurityRole::Tactical : SecurityRole::Patrol;
    }
    if (tier == 3)
    {
        return (officerId % 3 == 0) ? SecurityRole::Heavy : SecurityRole::Tactical;
    }
    if (tier == 4)
    {
        const auto slot = officerId % 4;
        return slot == 0 ? SecurityRole::Heavy
               : slot == 1 ? SecurityRole::Marksman
                           : SecurityRole::Tactical;
    }
    const auto slot = officerId % 4;
    return slot == 0 ? SecurityRole::Interceptor
           : slot == 1 ? SecurityRole::Heavy
           : slot == 2 ? SecurityRole::Marksman
                       : SecurityRole::Tactical;
}
const char *SecurityResponse::RoleLabel(SecurityRole role)
{
    switch (role)
    {
    case SecurityRole::Patrol:
        return "순찰";
    case SecurityRole::Tactical:
        return "기동";
    case SecurityRole::Heavy:
        return "중무장";
    case SecurityRole::Marksman:
        return "저격";
    case SecurityRole::Interceptor:
        return "대공요격";
    default:
        return "보안";
    }
}
float SecurityResponse::PreferredRange(SecurityRole role)
{
    switch (role)
    {
    case SecurityRole::Heavy:
        return 3.6f;
    case SecurityRole::Marksman:
        return 11.5f;
    case SecurityRole::Interceptor:
        return 9.5f;
    case SecurityRole::Tactical:
        return 6.5f;
    case SecurityRole::Patrol:
    default:
        return 5.0f;
    }
}
float SecurityResponse::HealthMultiplier(SecurityRole role)
{
    return role == SecurityRole::Heavy ? 1.45f : (role == SecurityRole::Interceptor ? 1.25f : 1.f);
}
float SecurityResponse::SurfaceEscapeSeconds(int tier, bool identified)
{
    static constexpr float kUnidentified[] = {0.f, 16.f, 20.f, 24.f, 30.f, 38.f};
    static constexpr float kIdentified[] = {0.f, 18.f, 22.f, 28.f, 36.f, 44.f};
    const size_t idx = size_t(std::clamp(tier, 0, kMaximumTier));
    return identified ? kIdentified[idx] : kUnidentified[idx];
}
int SecurityResponse::WantedForHeat(float heat)
{
    if (!std::isfinite(heat))
    {
        return 0;
    }
    int tier = 0;
    for (size_t i = 1; i < profiles.size(); ++i)
    {
        tier += heat >= profiles[i].heatThreshold;
    }
    return tier;
}
int SecurityResponse::NextTier(int current, float heat, float pursuitSeconds)
{
    current = std::clamp(current, 0, kMaximumTier);
    const int next = (std::min)(current + 1, kMaximumTier);
    return std::isfinite(pursuitSeconds) && WantedForHeat(heat) >= next &&
                   pursuitSeconds >= profiles[size_t(next)].minimumPursuitSeconds
               ? next
               : current;
}
} // namespace tutorial
