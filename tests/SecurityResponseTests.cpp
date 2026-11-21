#include "stdafx.h"
#include "SecurityResponse.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    using tutorial::SecurityResponse;
    const float thresholds[] = {20, 180, 400, 650, 900};
    const float gates[] = {0, 25, 60, 105, 165};
    for (int tier = 1; tier <= 5; ++tier)
    {
        const auto profile = SecurityResponse::ForTier(tier);
        assert(SecurityResponse::WantedForHeat(thresholds[tier - 1] - .01f) == tier - 1);
        assert(SecurityResponse::WantedForHeat(thresholds[tier - 1]) == tier);
        assert(SecurityResponse::NextTier(tier - 1, 1000, gates[tier - 1]) == tier);
        if (tier > 1)
        {
            assert(SecurityResponse::NextTier(tier - 1, 1000, gates[tier - 1] - .01f) == tier - 1);
        }
        assert(SecurityResponse::NextTier(tier - 1, thresholds[tier - 1] - .01f, 1000) == tier - 1);
        assert(profile.responderLimit == tier * 2 && profile.health == 60 + 40 * tier);
        const auto maximum = SecurityResponse::ForTier(tier, std::numeric_limits<int>::max());
        assert(maximum.growthBand == 4 && maximum.health == profile.health + 32 &&
               maximum.damage == profile.damage + 32);
        assert(SecurityResponse::ForTier(tier, 21).health == maximum.health);
        assert(SecurityResponse::ForTier(tier, 5).growthBand == 0 &&
               SecurityResponse::ForTier(tier, 6).growthBand == 1);
        if (tier > 1)
        {
            const auto previous = SecurityResponse::ForTier(tier - 1);
            assert(profile.damage > previous.damage && profile.fireInterval < previous.fireInterval &&
                   profile.reinforcementSeconds < previous.reinforcementSeconds &&
                   profile.minimumSpread < previous.minimumSpread && profile.moveSpeed > previous.moveSpeed);
        }
    }
    assert(SecurityResponse::NextTier(0, 1000, 1000) == 1); // A single event cannot skip levels.
    assert(SecurityResponse::NextTier(5, 1000, 1000) == 5);
    const auto none = SecurityResponse::ForTier(-5, 999);
    assert(none.responderLimit == 0 && none.damage == 0 && none.health == 0);
    assert(SecurityResponse::ForTier(999).tier == 5);
    assert(SecurityResponse::ForTier(1, std::numeric_limits<int>::min()).health == 100);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    assert(SecurityResponse::WantedForHeat(nan) == 0 && SecurityResponse::NextTier(2, 1000, nan) == 2);
    assert(SecurityResponse::kIdentifiedEscapeSeconds == 20 &&
           SecurityResponse::kUnidentifiedEscapeSeconds == 20);
    std::cout << "Wanted tier boundaries, escalation gates, response pressure and bounded growth passed.\n";
}
