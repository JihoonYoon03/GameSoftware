#include "stdafx.h"
#include "NpcSupport.h"
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    using ai::Support;
    using ai::SupportTask;
    ai::SupportState job;
    job.task = SupportTask::Aid;
    for (int i = 0; i < 20; ++i)
    {
        assert(!Support::Advance(job, .05f, true, true));
    }
    assert(Support::Busy(job) && job.progressSeconds > .9f);
    assert(!Support::Advance(job, .05f, true, false));
    assert(job.blocked && !Support::Busy(job) && job.progressSeconds == 0);
    Support::Advance(job, .05f, true, true);
    const float progress = job.progressSeconds;
    assert(!Support::Advance(job, 0, true, true));
    assert(!Support::Advance(job, std::numeric_limits<float>::quiet_NaN(), true, true));
    assert(job.progressSeconds == progress);
    assert(!Support::Advance(job, 100, false, true));
    assert(job.progressSeconds == 0 && !job.performing);
    Support::Advance(job, 100, true, true);
    assert(job.progressSeconds <= .05f); // A stalled frame cannot finish a timed action.
    bool complete = false;
    for (int i = 0; i < 61; ++i)
    {
        complete = Support::Advance(job, .05f, true, true);
    }
    assert(complete && job.progressSeconds == Support::kAidSeconds);
    job = {};
    job.task = SupportTask::Cover;
    assert(!Support::Advance(job, .05f, true, true));
    assert(job.performing && !Support::Busy(job)); // Covering actors may still shoot.
    job.task = SupportTask::Inspect;
    Support::Advance(job, .05f, true, true);
    assert(Support::Busy(job) && Support::Duration(job.task) == 4);
    job.task = SupportTask::Supply;
    assert(Support::Duration(job.task) == 1);
    job = {};
    assert(!Support::Advance(job, .05f, true, true) && !job.performing);
    std::cout << "Timed support, danger interruption, frozen time and covering fire contracts passed.\n";
}
