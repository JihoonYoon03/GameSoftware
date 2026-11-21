#include "stdafx.h"
#include "NpcSupport.h"
#include <algorithm>
#include <cmath>

namespace ai
{
float Support::Duration(SupportTask task)
{
    switch (task)
    {
    case SupportTask::Aid:
        return kAidSeconds;
    case SupportTask::Inspect:
        return kInspectSeconds;
    case SupportTask::Supply:
        return kSupplySeconds;
    default:
        return 0;
    }
}
bool Support::Advance(SupportState &state, float dt, bool reached, bool safe)
{
    if (!std::isfinite(dt) || dt <= 0)
    {
        return false;
    }
    state.blocked = !safe;
    state.performing = safe && reached && state.task != SupportTask::None;
    if (!state.performing)
    {
        state.progressSeconds = 0;
        return false;
    }
    const float duration = Duration(state.task);
    state.progressSeconds = (std::min)(duration, state.progressSeconds + (std::min)(dt, .05f));
    return duration > 0 && state.progressSeconds >= duration;
}
bool Support::Busy(const SupportState &state)
{
    return state.performing && state.task != SupportTask::None && state.task != SupportTask::Cover;
}
const char *Support::TaskName(SupportTask task)
{
    switch (task)
    {
    case SupportTask::Aid:
        return "aid";
    case SupportTask::Cover:
        return "cover_worker";
    case SupportTask::Inspect:
        return "inspect_equipment";
    case SupportTask::Supply:
        return "supply_route";
    default:
        return "none";
    }
}
const char *Support::TaskLabel(SupportTask task)
{
    switch (task)
    {
    case SupportTask::Aid:
        return "부상자 응급 처치";
    case SupportTask::Cover:
        return "작업자 주변 엄호";
    case SupportTask::Inspect:
        return "설비 점검";
    case SupportTask::Supply:
        return "점검 부품 운반";
    default:
        return "동행 / 일반 행동";
    }
}
} // namespace ai
