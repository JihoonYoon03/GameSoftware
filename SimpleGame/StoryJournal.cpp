#include "stdafx.h"
#include "StoryJournal.h"
#include "StoryArc.h"
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
        add("home/request", "집 단말 메시지",
            "리아가 N-0 정기 측정 중 이상한 응답이 잡혔다며 전망대 접근 코드를 보내왔다.");
    }
    if (state.gateSignalHeard)
    {
        add("observatory/voice", "전망대의 목소리",
            "관문 방향에서 들어온 수신 기록을 저장했다. 숨소리까지 내 목소리였지만, 녹음한 기억은 전혀 없다.",
            "누가 내 목소리로 그 말을 보냈는지, 왜 하필 지금 들려왔는지 아직 알 수 없다.");
    }
    if (stage >= game::MineQuestStage::TravelToMine)
    {
        add("mine/route", "겹친 통신선",
            "전망대 신호의 흔적이 인근 폐광의 낡은 통신선 주소와 겹쳐 있었다.",
            "전망대 목소리와 광산 구조 신호가 같은 발신원에서 온 것인지는 아직 단정할 수 없다.");
    }
    if (mine.powerBypassed || (legacy && mine.quest.IsPowerBypassed()))
    {
        add("mine/power", "광산 비상 전력",
            "주 갱도 끝 배전반을 우회해 비상등과 동쪽 격벽을 열었다.");
    }
    if (mine.signalRecorded || (legacy && stage >= game::MineQuestStage::InvestigateGlyph))
    {
        add("mine/signal", "벽 너머의 구조 신호",
            "오래전 실종된 탐사선의 식별 코드가 검출됐다. 전선은 벽 앞에서 끊겼는데 신호는 돌벽 안쪽에서 새어 나온다.",
            "식별자만으로는 아직 생존자가 남아 있다고 장담할 수 없다.");
    }
    if (mine.glyphRecorded || (legacy && stage >= game::MineQuestStage::CollectCoordinates))
    {
        add("mine/glyph", "돌벽의 관측자 문양",
            "지하 유적 벽면에서 소름이 돋을 만큼 나와 닮은 사람 형상을 발견해 기록했다.",
            "누가 언제 이런 문양을 새겼는지, 왜 나와 닮았는지는 알 수 없다.");
    }
    if (mine.coordinatesRecorded || (legacy && mine.quest.AreCoordinatesFound()))
    {
        add("mine/coordinates", "중계 좌표",
            "유적 안쪽 단말에서 신호가 거쳐 간 짧은 중계 좌표를 확보했다.");
    }
    if (stage == game::MineQuestStage::Complete)
    {
        add("mine/report", "헤일로 보고",
            "헤일로 수신망에 광산 좌표를 보고했다. 좌표가 멈춘 관문 N-0의 외곽 설비 목록과 일치했다.");
    }
    if (state.isPowerRestored)
    {
        add("mara/power", "남겨 둔 불빛",
            "주거 구역 동쪽 전력 장치를 손봐 마라네 집 창문에 다시 불이 들어오게 했다.");
    }
    if (state.isSideQuestComplete)
    {
        add("mara/report", "마라의 안도",
            "불이 돌아온 소식을 전했다. 아이가 자기 방에 다시 그림책을 들고 들어갔다고 한다.");
    }
    const bool generatorKnown = chapter.generatorInspected || (chapter.observedRecords & kGeneratorRecord);
    const bool receiverKnown =
        chapter.receiverInspected || chapter.convoyBriefed || (chapter.observedRecords & kReceiverRecord);
    const bool tradeKnown = chapter.tradeProofFound || (chapter.observedRecords & kTradeRecord);
    const bool ledgerKnown = chapter.ledgerFound || (chapter.observedRecords & kLedgerRecord);
    if (chapter.unlocked || generatorKnown || receiverKnown || tradeKnown || ledgerKnown)
    {
        add("frontier/facility", "설비 N-0/17",
            "광산 좌표가 가리킨 N-0/17은 관문 본체가 아니라 그 둘레에 붙은 외곽 설비 번호다.");
    }
    if (chapter.generatorRestored)
    {
        add("frontier/generator", "멈추지 않는 바늘",
            "전력이 새던 폐쇄 선로를 끊어 도시 전력을 되살렸다. 그런데 끊긴 선로의 계기판 바늘이 아직도 같은 간격으로 뛰고 있다.",
            "전기를 끊었는데도 바늘이 계속 움직이는 이유는 설명되지 않는다.");
    }
    else if (generatorKnown)
    {
        add("frontier/generator", "사라지는 전력",
            "개척지 발전기에서 폐쇄된 N-0 외곽 선로로 일정한 간격마다 전력이 빠져나가고 있었다.",
            "누가 그 전기를 끌어다 쓰는지 아직 확인하지 못했다.");
    }
    if (receiverKnown)
    {
        add("frontier/radio", "여섯 번째 목소리",
            "수로에 갇힌 운송대 인원은 다섯 명뿐인데, 끊어진 차량 수신기에서 여섯 번째 응답이 흘러나왔다.",
            "그 목소리가 앞선 교신의 되감기인지, 다른 누군가의 응답인지는 알 수 없다.");
    }
    if (chapter.convoyEvacuated || chapter.convoyRescued)
    {
        add("frontier/evacuation", "운송대 대피",
            "측량 표식을 따라 다섯 명의 운송대원을 안전 야영지까지 무사히 옮겼다.");
    }
    if (chapter.kainJoined)
    {
        add("frontier/kain", "전 경비원 카인",
            "N-0 외곽에서 근무했다는 카인이 인원을 끝까지 확인한 뒤 우리와 동행하기로 했다.");
    }
    if (chapter.portableLightRepaired)
    {
        add("frontier/light", "리아의 휴대 조명",
            "리아와 함께 낡은 휴대 조명의 접점을 고쳤다. 리아는 다음 야영 때 꼭 챙겨 가자고 했다.");
    }
    if (chapter.vehicleRepaired)
    {
        add("frontier/vehicle", "살아난 운송 차량",
            "수로에 묶여 있던 운송 차량의 구동계를 고쳐 두었다. 이제 운송대가 차를 몰고 돌아갈 수 있다.");
    }
    if (tradeKnown)
    {
        add("frontier/trade", "수상한 매입 기록",
            "정체를 숨긴 구매자가 N-0/17 외곽 부품을 시세보다 훨씬 비싼 값에 사들였다.",
            "돈을 댄 구매자가 누구인지는 아직 드러나지 않았다.");
    }
    if (ledgerKnown)
    {
        add("frontier/ledger", "지워진 발주 장부",
            "발주처 이름만 말끔히 지워진 장부에서 N-0/17 좌표, 전력 유출 주기, 교신 시각을 찾아냈다.",
            "누가 이름을 지우고 이 설비를 다시 돌렸는지는 아직 모른다.");
    }
    if (chapter.detailLedgerFound || (chapter.observedRecords & kDetailLedgerRecord))
    {
        add("frontier/detail-ledger", "상세 장부의 서명",
            "구매자를 추적할 수 있는 추가 단서와 서명 흔적을 확보했다.",
            "이 서명의 주인이 누구인지는 아직 밝혀지지 않았다.");
    }
    if (chapter.recordsCompared)
    {
        add("frontier/comparison", "하나로 모인 조각",
            "광산 좌표, 발전기 전력 유출, 운송대 교신 시각이 모두 N-0/17에서 겹쳤다. 멈춘 관문 둘레에 차례로 불빛이 들어오고 있다.",
            "관문은 열리지 않았는데 왜 외곽 설비가 깨어나고 있는지 아직 알 수 없다.");
        if (chapter.waterwayUnlocked)
        {
            add("frontier/waterway", "옛 수로의 진입로",
                "지도 속 다리는 사라졌지만, 카인이 기억하는 아래 수로 진입로 좌표를 확보했다.",
                "그 수로가 아직 무너지지 않고 남아 있는지는 직접 가 봐야 안다.");
        }
    }
    const auto &arc = story::StoryArc::Get().State();
    if (arc.returnBeaconPlaced)
    {
        add("act2/beacon", "수로 교각의 귀환 표식",
            "지도에서 지워진 다리 아래 강바닥에 귀환 표식을 고정했다. 왜곡된 표지 사이에서도 돌아올 선 하나는 잡았다.",
            "표식이 비추는 거리가 언제까지 같은 길로 이어질지는 장담할 수 없다.");
    }
    if (arc.kainRetreatRevealed)
    {
        add("act2/kain", "옛 야영지의 점호 기록",
            "카인은 과거 실종 탐사대의 경비원이었고 철수 명령에 동료들을 남겨 두고 빠져나왔다고 고백했다. 삭아 내린 컵의 물에는 아직 김이 올라오고 있었다.",
            "벽 너머로 사라진 목소리가 살아 있던 사람이었는지, 남은 메아리였는지 카인 자신도 확신하지 못한다.");
    }
    if (arc.veilHypothesisFound || arc.observatoryDocsFound)
    {
        add("act2/veil", "봉쇄 중계실과 장막 가설",
            "고대 도식은 목적지가 아니라 바깥을 가려 두는 '장막'의 테두리를 그리고 있었다. 심연 관측국 최종 승인본에서는 '확대 가동 즉시 중단 권고'가 삭제되어 있었다.",
            "장막 너머의 현상이 정확히 무엇인지, 관측국이 왜 경고를 지우면서까지 출력을 밀어붙였는지는 아직 풀리지 않았다.");
    }
    if (arc.haloEvacuationComplete)
    {
        add("act3/evac", "헤일로 07구역 대피",
            "외곽 차단이 끝난 뒤에도 헤일로 07구역에 구획 균열이 번졌다. 대피로와 측면 통로 주민을 대피시키고 예비 전원을 연결했다.",
            "차단 설비에서 들린 중단 요청이 과거의 기록인지, 막 벌어지려던 경고였는지 알 수 없다.");
    }
    if (arc.missingExpeditionConfirmedAlive)
    {
        add("act3/survivors", "관문 N-0 내부의 생존자 3명",
            "윤서진·강도현·박민재 세 명을 직접 만나 새 표식, 현장 문답, 외부 계측기 공백의 세 가지로 생존을 확인했다.",
            "왜 같은 무전이 채널마다 '멈춰'와 '기다려'로 갈려 들렸는지, 나머지 실종자들은 어디에 있는지 아직 모른다.");
    }
    if (arc.sectorLossTriggered)
    {
        add("act4/sector-loss", "닫힌 핵심과 사라진 구획",
            "탐사대원 3명을 인계하고 핵심 결합부 4곳을 폐쇄해 N-0 확대를 멈췄다. 직후 외부 제어소와 헤일로 동부 구획이 잘려 나갔고, 리아는 '여긴, 아직 빛이 있어'라는 마지막 수신을 남긴 채 실종됐다.",
            "사라진 구획과 리아가 어디로 밀려났는지, 되돌릴 길이 있는지는 아무도 답하지 못한다.");
    }
    if (arc.nereusSignalReceived)
    {
        add("epilogue/nereus", "붕괴 후 17일 · 네레우스 수신",
            "다른 항성계 관문들에서도 장막 약화가 보고되는 가운데, 미확인 해양 행성 네레우스 방향에서 리아의 음성과 일치하는 새 신호가 수신됐다.",
            "송신 시각은 확인되지 않았다. 지금 살아 보낸 신호인지, 어딘가에 고여 있던 메아리인지 직접 확인해야 한다.");
    }
    return records;
}
} // namespace tutorial
