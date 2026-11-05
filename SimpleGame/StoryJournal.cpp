#include "stdafx.h"
#include "StoryJournal.h"
#include "Level.h"

namespace tutorial
{
namespace
{
constexpr unsigned kGeneratorRecord = 1, kReceiverRecord = 2, kTradeRecord = 4;
constexpr unsigned kLedgerRecord = 8, kDetailLedgerRecord = 16;
} // namespace
std::vector<InvestigationRecord> StoryJournal::Collect(const GameState &state, const game::MineState &mine,
                                                       const game::ChapterOneState &chapter)
{
    std::vector<InvestigationRecord> records;
    auto add = [&](const char *id, const char *title, const char *fact, const char *unresolved = "") {
        records.push_back({id, title, fact, unresolved});
    };
    const bool legacy = !state.surfaceExploration;
    const auto stage = mine.quest.GetStage();
    if (state.homeMessageRead || (legacy && state.mainQuestStage >= MainQuestStage::MeetLia))
    {
        add("home/request", "집 단말", "N-0 정기 측정 요청과 전망대 접근 코드를 받았다.");
    }
    if (state.gateSignalHeard)
    {
        add("observatory/voice", "전망대 음성", "내 목소리와 닮은 수신 음성을 저장했다. 녹음한 기억은 없다.",
            "발신자가 누구인지, 왜 내 목소리와 닮았는지는 확인하지 못했다.");
    }
    if (stage >= game::MineQuestStage::TravelToMine)
    {
        add("mine/route", "수신 경로", "전망대 기록과 광산 통신선의 중계 흔적이 겹친다.",
            "두 기록이 같은 발신원에서 왔는지는 모른다.");
    }
    if (mine.powerBypassed || (legacy && mine.quest.IsPowerBypassed()))
    {
        add("mine/power", "광산 전력", "비상 전력을 우회해 동쪽 격벽과 조명을 복구했다.");
    }
    if (mine.signalRecorded || (legacy && stage >= game::MineQuestStage::InvestigateGlyph))
    {
        add("mine/signal", "광산 구조 신호", "실종 탐사선 식별자의 신호를 보관했다.",
            "식별자만으로 현재 생존자가 있다고 단정할 수 없다.");
    }
    if (mine.glyphRecorded || (legacy && stage >= game::MineQuestStage::CollectCoordinates))
    {
        add("mine/glyph", "벽면 문양", "나를 닮은 형상을 발견하고 기록했다.",
            "누가 언제 새겼는지, 나와 어떤 관계인지는 모른다.");
    }
    if (mine.coordinatesRecorded || (legacy && mine.quest.AreCoordinatesFound()))
    {
        add("mine/coordinates", "광산 좌표", "신호가 거쳐 간 중계 좌표를 보관했다.");
    }
    if (stage == game::MineQuestStage::Complete)
    {
        add("mine/report", "광산 보고", "헤일로 수신망에 중계 좌표를 보고했다.");
    }
    if (state.isPowerRestored)
    {
        add("mara/power", "주거 구역 전력", "동쪽 전력 장치를 복구했다.");
    }
    if (state.isSideQuestComplete)
    {
        add("mara/report", "마라 보고", "전력 복구 사실을 마라에게 전했다.");
    }
    const bool generatorKnown = chapter.generatorInspected || (chapter.observedRecords & kGeneratorRecord);
    const bool receiverKnown =
        chapter.receiverInspected || chapter.convoyBriefed || (chapter.observedRecords & kReceiverRecord);
    const bool tradeKnown = chapter.tradeProofFound || (chapter.observedRecords & kTradeRecord);
    const bool ledgerKnown = chapter.ledgerFound || (chapter.observedRecords & kLedgerRecord);
    if (chapter.unlocked || generatorKnown || receiverKnown || tradeKnown || ledgerKnown)
    {
        add("frontier/facility", "설비 번호", "N-0/17은 관문 자체가 아닌 외곽 설비 번호다.");
    }
    if (chapter.generatorRestored)
    {
        add("frontier/generator", "개척지 발전기",
            "전력이 새던 외곽 선로를 분리했다. 끊어진 선로의 "
            "계기판은 여전히 같은 간격으로 움직인다.",
            "분리한 선로의 계기판이 움직이는 원인은 모른다.");
    }
    else if (generatorKnown)
    {
        add("frontier/generator", "개척지 발전기", "폐쇄된 외곽 선로로 주기적인 전력 유출을 확인했다.",
            "전력을 복구한 것은 아니다. 유출 원인은 아직 확인하지 못했다.");
    }
    if (receiverKnown)
    {
        add("frontier/radio", "운송대 교신", "초기 현장 보고의 인원은 다섯 명인데 무전 응답은 여섯 명이었다.",
            "추가 응답을 새 생존자로 확인한 것은 아니다.");
    }
    if (chapter.convoyEvacuated || chapter.convoyRescued)
    {
        add("frontier/evacuation", "운송대 대피", "운송대의 안전 지점 도착 사실을 보관했다.");
    }
    if (chapter.kainJoined)
    {
        add("frontier/kain", "카인", "카인이 인원을 재확인하고 동행 의사를 밝혔다.");
    }
    if (chapter.portableLightRepaired)
    {
        add("frontier/light", "휴대 조명", "접점을 수리해 조명이 켜지는 것을 확인했다.");
    }
    if (chapter.vehicleRepaired)
    {
        add("frontier/vehicle", "운송 차량", "구동계를 수리했다. 운송대가 차량을 회수할 수 있다.");
    }
    if (tradeKnown)
    {
        add("frontier/trade", "거래 기록", "익명의 구매자가 N-0/17 부품을 시세보다 비싸게 샀다.",
            "구매자의 신원은 확인하지 못했다.");
    }
    if (ledgerKnown)
    {
        add("frontier/ledger", "기본 장부", "발주처가 지워진 좌표·전력 주기·교신 시각을 확보했다.",
            "삭제된 발주처의 정체는 아직 모른다.");
    }
    if (chapter.detailLedgerFound || (chapter.observedRecords & kDetailLedgerRecord))
    {
        add("frontier/detail-ledger", "상세 장부", "구매자를 추적할 추가 단서를 보관했다.",
            "추적 단서를 확보한 것이 구매자의 신원을 확인했다는 뜻은 아니다.");
    }
    if (chapter.recordsCompared)
    {
        add("frontier/comparison", "기록 대조",
            "광산 좌표·발전기 전력 유출·운송대 교신 기록이 N-0/17에서 "
            "겹친다는 것을 확인했다.",
            "기록의 일치만으로 발신자나 외곽 설비의 전력원을 설명할 수 없다.");
        if (chapter.waterwayUnlocked)
        {
            add("frontier/waterway", "수로 기록", "보관 지도의 옛 수로 진입로 좌표를 확보했다.",
                "실제 통행 가능 여부는 아직 현장에서 확인하지 않았다.");
        }
    }
    return records;
}
} // namespace tutorial
