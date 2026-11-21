#include "stdafx.h"
#include "NpcCooperation.h"
#include <algorithm>
#include <cmath>

namespace ai
{
float Cooperation::TargetScore(float distance, unsigned claims, bool threatened)
{
    return distance + float(claims) * CooperationSettings::kClaimPenalty -
           (threatened ? CooperationSettings::kProtectionBonus : 0);
}
bool Cooperation::BlocksShot(models::Point from, models::Point target, models::Point friendly, float spread)
{
    const double x = target.x - from.x, y = target.y - from.y, z = target.z - from.z;
    const double length2 = x * x + y * y + z * z;
    const double fx = friendly.x - from.x, fy = friendly.y - from.y, fz = friendly.z - from.z;
    if (!std::isfinite(length2) || !std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz) ||
        !std::isfinite(spread) || spread < 0 || spread >= 1.5f || length2 < .000001)
    {
        return true;
    }
    const double fraction = (fx * x + fy * y + fz * z) / length2;
    if (fraction < 0 || fraction > 1)
    {
        return false;
    }
    const double dx = fx - x * fraction, dy = fy - y * fraction, dz = fz - z * fraction;
    const double clearance =
        CooperationSettings::kBodyRadius + std::sqrt(length2) * fraction * std::tan(spread);
    return dx * dx + dy * dy + dz * dz <= clearance * clearance;
}
std::optional<ThreatReport> Cooperation::NearestReport(models::Point receiver, std::uint64_t self,
                                                       const std::vector<ThreatReport> &sources)
{
    std::optional<ThreatReport> best;
    float nearest = CooperationSettings::kReportRange;
    for (const auto &source : sources)
    {
        if (source.reporterId == 0 || source.reporterId == self || !std::isfinite(source.remainingSeconds) ||
            source.remainingSeconds <= 0 || !std::isfinite(source.target.x) ||
            !std::isfinite(source.target.y) || !std::isfinite(source.target.z))
        {
            continue;
        }
        const float distance =
            std::hypot(std::hypot(source.origin.x - receiver.x, source.origin.y - receiver.y),
                       source.origin.z - receiver.z);
        if (distance < nearest || (distance == nearest && best && source.reporterId < best->reporterId))
        {
            nearest = distance;
            best = source;
        }
    }
    if (best)
    {
        best->remainingSeconds = (std::min)(best->remainingSeconds, CooperationSettings::kReportSeconds);
    }
    return best;
}
const char *Cooperation::TaskName(TeamTask task)
{
    switch (task)
    {
    case TeamTask::Defend:
        return "defend";
    case TeamTask::Protect:
        return "protect";
    case TeamTask::HoldFire:
        return "hold_fire";
    case TeamTask::Investigate:
        return "investigate_report";
    default:
        return "none";
    }
}
const char *Cooperation::TaskLabel(TeamTask task)
{
    switch (task)
    {
    case TeamTask::Defend:
        return "방어 표적 분담";
    case TeamTask::Protect:
        return "생존자 보호";
    case TeamTask::HoldFire:
        return "아군 사선 대기";
    case TeamTask::Investigate:
        return "동료 경고 지점 수색";
    default:
        return "일반 행동";
    }
}
} // namespace ai
