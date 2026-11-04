#pragma once
#include "Game.h"

namespace tutorial
{
enum class MineDiscovery
{
    Power,
    Signal,
    Glyph,
    Coordinates,
    Count
};

// Read-only narrative selection. Progress, rewards and dialogue playback remain with their owners.
class StoryDialogue
{
  public:
    static std::vector<DialogueLine> Proposal(MissionId id, const GameState &state,
                                              const game::MineState &mine,
                                              const game::ChapterOneState &chapter,
                                              const char *channel = "제안 기록");
    static const char *DiscoveryKey(MineDiscovery discovery);
    // Inspect facts before committing the interaction, so first discoveries differ from archive reads.
    static std::vector<DialogueLine> ObserveMine(MineDiscovery discovery, const game::MineState &mine,
                                                 bool riaPresent);
};
} // namespace tutorial
