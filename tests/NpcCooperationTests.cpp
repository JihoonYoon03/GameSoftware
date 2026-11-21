#include "stdafx.h"
#include "NpcCooperation.h"
#include "NpcPerception.h"
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    using ai::Cooperation;
    const models::Point from{0, 0, 0}, target{10, 0, 0};
    assert(Cooperation::BlocksShot(from, target, {5, 0, 0}));
    assert(!Cooperation::BlocksShot(from, target, {-2, 0, 0}));
    assert(!Cooperation::BlocksShot(from, target, {12, 0, 0}));
    assert(!Cooperation::BlocksShot(from, target, {5, 0, 3}));
    assert(!Cooperation::BlocksShot(from, target, {5, .6f, 0}));
    assert(Cooperation::BlocksShot(from, target, {5, .6f, 0}, .1f));
    assert(Cooperation::BlocksShot(from, target, {5, 0, 0}, std::numeric_limits<float>::quiet_NaN()));
    assert(Cooperation::TargetScore(4, 1, false) > Cooperation::TargetScore(5, 0, false));
    assert(Cooperation::TargetScore(5, 0, true) < Cooperation::TargetScore(4, 0, false));

    std::vector<ai::ThreatReport> reports = {{3, {2, 0, 0}, {7, 0, 0}, 6}, {1, {-2, 0, 0}, {8, 0, 0}, 4}};
    auto report = Cooperation::NearestReport({}, 2, reports);
    assert(report && report->reporterId == 1 && report->remainingSeconds == 2);
    assert(!Cooperation::NearestReport({100, 0, 0}, 2, reports));
    ai::ThreatMemory receiver;
    receiver.Remember(ai::Stimulus::Report, report->remainingSeconds);
    assert(receiver.Known() && !receiver.Visible()); // A radio/pack warning is never a sighting.
    std::vector<ai::ThreatReport> secondHop;
    if (receiver.Visible())
    {
        secondHop.push_back(*report);
    }
    assert(!Cooperation::NearestReport({}, 4, secondHop));
    receiver.Advance(2.1f);
    assert(!receiver.Known());
    reports = {{1, {}, {}, .25f}};
    assert(!Cooperation::NearestReport({}, 1, reports));
    assert(Cooperation::NearestReport({}, 2, reports)->remainingSeconds == .25f);
    reports.front().target.x = std::numeric_limits<float>::quiet_NaN();
    assert(!Cooperation::NearestReport({}, 2, reports));
    std::cout << "Cooperation target distribution, shot corridor, bounded reports and no echo passed.\n";
}
