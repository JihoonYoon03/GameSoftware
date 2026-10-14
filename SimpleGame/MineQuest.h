#pragma once

namespace levelone
{
enum class MineQuestStage
{
    ShowRecord,
    CheckCityRelay,
    TravelToMine,
    BypassPower,
    InvestigateSignal,
    InvestigateGlyph,
    CollectCoordinates,
    ReturnToHalo,
    Complete
};

class MineQuest
{
  public:
    MineQuestStage GetStage() const;
    bool Advance(MineQuestStage expected);
    bool BeginJourney();
    bool IsRiaJoined() const;
    bool IsPowerBypassed() const;
    bool AreCoordinatesFound() const;
    const char *GetObjective() const;

  private:
    MineQuestStage stage = MineQuestStage::ShowRecord;
    bool riaJoined = false;
};
} // namespace levelone
