#pragma once
#include "Game.h"

namespace tutorial
{
struct InvestigationRecord
{
    std::string id, title, fact, unresolved;
};

// Derived evidence, never a second quest state or a persisted cache of narrative facts.
class StoryJournal
{
  public:
    static std::vector<InvestigationRecord> Collect(const GameState &state, const game::MineState &mine,
                                                    const game::ChapterOneState &chapter);
};
} // namespace tutorial
