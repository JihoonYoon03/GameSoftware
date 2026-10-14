#include "stdafx.h"
#include "MineQuest.h"

namespace levelone
{
MineQuestStage MineQuest::GetStage() const
{
    return stage;
}
bool MineQuest::Advance(MineQuestStage expected)
{
    if (stage != expected || stage == MineQuestStage::Complete ||
        (expected == MineQuestStage::TravelToMine && !riaJoined))
    {
        return false;
    }
    stage = static_cast<MineQuestStage>(static_cast<int>(stage) + 1);
    return true;
}
bool MineQuest::BeginJourney()
{
    if (stage < MineQuestStage::TravelToMine)
    {
        return false;
    }
    riaJoined = true;
    return true;
}
bool MineQuest::IsRiaJoined() const
{
    return riaJoined;
}
bool MineQuest::IsPowerBypassed() const
{
    return stage >= MineQuestStage::InvestigateSignal;
}
bool MineQuest::AreCoordinatesFound() const
{
    return stage >= MineQuestStage::ReturnToHalo;
}
const char *MineQuest::GetObjective() const
{
    switch (stage)
    {
    case MineQuestStage::ShowRecord:
        return "리아에게 관문 신호 기록 보여 주기 [E]";
    case MineQuestStage::CheckCityRelay:
        return "도시 수신망 단말에서 중계 흔적 확인 [E]";
    case MineQuestStage::TravelToMine:
        return "리아와 제트 바이크로 광산 이동 [E]";
    case MineQuestStage::BypassPower:
        return "주 갱도 끝의 비상 배전반에서 전력 우회 [E]";
    case MineQuestStage::InvestigateSignal:
        return "격벽 너머 지하 유적의 구조 신호 조사 [E]";
    case MineQuestStage::InvestigateGlyph:
        return "신호 옆의 관측자 문양 조사 [E]";
    case MineQuestStage::CollectCoordinates:
        return "유적 안쪽 단말에서 중계 좌표 확보 [E]";
    case MineQuestStage::ReturnToHalo:
        return "광산 입구로 돌아가 헤일로에 보고 [E]";
    case MineQuestStage::Complete:
        return "낙하한 신호 완료 / 자유 탐사·구조·사냥 가능";
    }
    return "광산 조사";
}
} // namespace levelone
