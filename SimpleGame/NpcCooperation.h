#pragma once
#include "ModelLibrary.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace ai
{
enum class TeamTask
{
    None,
    Defend,
    Protect,
    HoldFire,
    Investigate
};
struct TeamState
{
    TeamTask task = TeamTask::None;
    std::uint64_t targetId = 0, reporterId = 0; // Owner-scene IDs; transient, never cross a handoff.
};
struct ThreatReport
{
    std::uint64_t reporterId = 0;
    models::Point origin{}, target{};
    float remainingSeconds = 0;
};
struct CooperationSettings
{
    static constexpr float kClaimPenalty = 2.5f, kProtectionBonus = 2;
    static constexpr float kProtectedRange = 3, kReportRange = 8, kReportSeconds = 2;
    static constexpr float kBodyRadius = .45f, kProjectedSpreadScale = 2;
    static constexpr float kBodyHeight = 1.2f, kFootClearance = .1f;
};
class Cooperation
{
  public:
    static float TargetScore(float distance, unsigned claims, bool threatensProtected);
    // Includes the current spread cone; check before TryFire, so a hold spends no ammunition.
    static bool BlocksShot(models::Point from, models::Point target, models::Point friendly,
                           float spread = 0);
    // Supply a snapshot of direct, current sightings only. Recipients cannot relay reports.
    static std::optional<ThreatReport> NearestReport(models::Point receiver, std::uint64_t self,
                                                     const std::vector<ThreatReport> &directSightings);
    static const char *TaskName(TeamTask task);
    static const char *TaskLabel(TeamTask task);
};
} // namespace ai
