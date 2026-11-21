#pragma once

namespace tutorial
{
struct SecurityResponseProfile
{
    int tier = 0, responderLimit = 0, health = 0, damage = 0, growthBand = 0;
    float heatThreshold = 0, minimumPursuitSeconds = 0, reinforcementSeconds = 0;
    float moveSpeed = 0, fireRange = 0, fireInterval = 0, minimumSpread = 0;
    const char *label = "수배 없음";
};
// Pure balance policy. Dispatch adapters own coordinates, sight, collision and timers.
class SecurityResponse
{
  public:
    static constexpr int kMaximumTier = 5, kOfficerSlots = 16, kSpawnRouteBudget = 2;
    static constexpr int kGrowthBandLevels = 5, kMaximumGrowthBand = 4;
    static constexpr int kHealthPerGrowthBand = 8, kDamagePerGrowthBand = 8;
    static constexpr float kUnidentifiedEscapeSeconds = 20, kIdentifiedEscapeSeconds = 20;
    static SecurityResponseProfile ForTier(int tier, int playerLevel = 1);
    static int WantedForHeat(float heat);
    // Call only for a new qualifying crime; elapsed time alone never escalates a pursuit.
    static int NextTier(int current, float heat, float pursuitSeconds);
};
} // namespace tutorial
