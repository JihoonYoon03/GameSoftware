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
