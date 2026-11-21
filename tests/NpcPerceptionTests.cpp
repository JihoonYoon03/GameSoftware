#include "stdafx.h"
#include "NpcPerception.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
void TestVisionBoundaries()
{
    const ai::Vision vision{10, 1.04719755f, 1, 2};
    assert(ai::Perception::InSight({}, 0, {10, 0, 2}, vision));
    assert(!ai::Perception::InSight({}, 0, {10.01f, 0, 0}, vision));
    assert(!ai::Perception::InSight({}, 0, {0, 4, 0}, vision));
    assert(!ai::Perception::InSight({}, 0, {-4, 0, 0}, vision));
    assert(ai::Perception::InSight({}, 0, {-1, 0, 0}, vision));
    assert(!ai::Perception::InSight({}, 0, {1, 0, 2.1f}, vision));
    assert(ai::Perception::InSight({}, 3.14159265f, {-4, 0, 0}, vision));
    assert(!ai::Perception::InSight({}, 0, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, vision));
    // Geometry is deliberately separate from occlusion. A caller must not refresh memory for a wall hit.
    ai::ThreatMemory memory;
    const bool clearLine = false;
    memory.Observe(ai::Perception::InSight({}, 0, {2, 0, 0}, vision) && clearLine, 4);
    assert(!memory.Known());
}
void TestMemoryAndRoles()
{
    ai::ThreatMemory memory;
    assert(!memory.Initialized() && !memory.Known());
    memory.Observe(true, 4);
    assert(memory.Visible() && memory.Source() == ai::Stimulus::Sight);
    assert(ai::Perception::Decide(ai::Role::Predator, memory, 4) == ai::Decision::Engage);
    assert(ai::Perception::Decide(ai::Role::Civilian, memory, 4) == ai::Decision::Withdraw);
    assert(ai::Perception::Decide(ai::Role::Watcher, memory, 4) == ai::Decision::Observe);
    assert(ai::Perception::Decide(ai::Role::Watcher, memory, 1) == ai::Decision::Withdraw);
    memory.Advance(0);
    assert(memory.Visible());
    memory.Advance(.5f);
    memory.Observe(false, 4);
    assert(memory.Known() && !memory.Visible() && std::abs(memory.Remaining() - 3.5f) < .0001f);
    assert(ai::Perception::Decide(ai::Role::Officer, memory, 10) == ai::Decision::Investigate);
    memory.Advance(4);
    assert(!memory.Known() && memory.Initialized());
    assert(ai::Perception::Decide(ai::Role::Officer, memory, 10) == ai::Decision::Routine);
    memory.Remember(ai::Stimulus::Sound, 4);
    assert(!memory.Visible() && memory.Source() == ai::Stimulus::Sound);
    assert(ai::Perception::Decide(ai::Role::Watcher, memory, 10) == ai::Decision::Withdraw);
    assert(ai::Perception::Decide(ai::Role::Predator, memory, 10) == ai::Decision::Investigate);
    memory.Observe(true, 4);
    memory.Remember(ai::Stimulus::Report, 8);
    assert(!memory.Visible() && memory.Source() == ai::Stimulus::Report);
    memory.Advance(std::numeric_limits<float>::quiet_NaN());
    assert(std::isfinite(memory.Remaining()));
    memory.Forget();
    memory.Remember(ai::Stimulus::Report, 1000);
    assert(memory.Remaining() == 30);
    memory.Advance(31);
    assert(!memory.Known());
}
} // namespace
int main()
{
    TestVisionBoundaries();
    TestMemoryAndRoles();
    std::cout << "NPC vision, occlusion input, finite memory and role decisions passed.\n";
}
