#include "stdafx.h"
#include "Story.h"
#include "Level.h"
#include "Overworld.h"
#include <algorithm>

namespace tutorial
{
std::vector<DialogueLine> StoryDialogue::Proposal(MissionId id, const GameState &state,
                                                  const game::MineState &mine,
                                                  const game::ChapterOneState &chapter, const char *channel)
{
    if (unsigned(id) >= unsigned(MissionId::Count) || state.missions.Active(id) ||
        state.missions.Status(id) == MissionStatus::Completed)
    {
        return {};
    }
    const bool paused = state.missions.Status(id) == MissionStatus::Paused;
    std::vector<DialogueLine> lines;
    const char *speaker = id == MissionId::Mara ? "마라" : "리아";
    auto add = [&](const char *key, const char *text) { lines.push_back({key, speaker, channel, text, {}}); };
    auto addRecord = [&](const char *key, const char *text) {
        lines.push_back({key, "나", "조사 기록", text, {}});
    };
    if (paused)
    {
        add("resume", "조사를 이어가려는 거지? 그동안 확보한 기록부터 확인하자.");
        if (id == MissionId::Mara)
        {
            lines.back().text = "다시 들러 주셨네요. 전력 장치 이야기를 이어가도 될까요?";
        }
    }
    switch (id)
    {
    case MissionId::Signal:
        if (state.gateSignalHeard)
        {
            addRecord("signal/archive", "전망대의 음성을 보관했다. 내 목소리와 닮았지만 발신자는 "
                                        "확인하지 못했다. 이 기록으로 리아의 조사 제안을 검토할 수 있다.");
        }
        else
        {
            add("signal/request", "N-0 정기 측정 중에 전망대가 이상한 응답을 받았어. 점검 기록에는 없는 "
                                  "신호야. 전망대에 남은 수신 기록을 확인해 줄래?");
        }
        break;
    case MissionId::Mine:
        if (mine.coordinatesRecorded)
        {
            addRecord("mine/coordinates", "광산의 중계 좌표는 이미 보관했다. 전망대 기록과의 관계는 "
                                          "도시 수신망에서 대조해야 한다. 확보한 현장 기록은 유지된다.");
        }
        else if (mine.signalRecorded || mine.glyphRecorded || mine.powerBypassed)
        {
            addRecord("mine/visited", "먼저 살펴본 광산 기록이 남아 있다. 전망대 신호가 그곳을 거쳤는지 "
                                      "도시 수신망에서 대조하고, 아직 확인하지 못한 부분을 조사할 수 있다.");
        }
        else
        {
            add("mine/request", "전망대 기록에 광산 통신선과 겹치는 흔적이 있어. 같은 신호인지 "
                                "수신망에서 대조해 보자. 경로가 확인되면 광산을 조사할 수 있겠어?");
        }
        break;
    case MissionId::Frontier:
        if (chapter.generatorRestored && chapter.convoyRescued && chapter.ledgerFound)
        {
            addRecord("frontier/report", "발전기·운송대·폐설비의 필수 현장 기록이 모두 모였다. "
                                         "조사에 참여한 뒤 헤일로 수신망에서 세 기록을 대조할 수 있다.");
        }
        else if (chapter.observedRecords || chapter.generatorInspected || chapter.receiverInspected ||
                 chapter.generatorRestored || chapter.convoyEvacuated || chapter.ledgerFound)
        {
            addRecord("frontier/visited", "개척지에서 먼저 확인한 내용은 보관했다. 광산 좌표와 연결된 "
                                          "발전기·운송대·폐설비의 남은 일은 원하는 순서로 살펴볼 수 있다.");
        }
        else
        {
            add("frontier/request", "광산 좌표가 N-0 외곽 설비와 이어져 있어. 발전기·운송대·폐설비의 "
                                    "기록을 모으면 경로를 좁힐 수 있을 거야. 세 현장을 조사해 줄래?");
        }
        break;
    case MissionId::Mara:
        if (state.isPowerRestored)
        {
            addRecord("mara/restored", "동쪽 전력 장치는 이미 복구했다. 마라에게 수리한 내용을 "
                                       "직접 전할 수 있다.");
        }
        else
        {
            add("mara/request", "딸아이 방에 불이 안 들어와요. 동쪽 전력 장치를 살펴봐 주시겠어요? "
                                "밤에도 아이가 혼자 어둠 속에 있지 않았으면 해요.");
        }
        break;
    default:
        return {};
    }
    lines.push_back(
        {"offer",
         speaker,
         channel,
         id == MissionId::Mara ? "지금 이야기해 주실 수 있나요? 나중에 들르셔도 괜찮아요."
                               : "지금 이어갈까? 준비가 필요하면 나중에 다시 이야기하자.",
         {{"accept", paused ? "조사를 재개한다" : "제안을 수락한다"}, {"later", "지금은 하지 않는다"}}});
    return lines;
}

const char *StoryDialogue::DiscoveryKey(MineDiscovery discovery)
{
    switch (discovery)
    {
    case MineDiscovery::Power:
        return "power";
    case MineDiscovery::Signal:
        return "signal";
    case MineDiscovery::Glyph:
        return "glyph";
    case MineDiscovery::Coordinates:
        return "coordinates";
    default:
        return nullptr;
    }
}

std::vector<DialogueLine> StoryDialogue::ObserveMine(MineDiscovery discovery, const game::MineState &mine,
                                                     bool riaPresent)
{
    const char *speaker = nullptr, *text = nullptr;
    bool recorded = false;
    switch (discovery)
    {
    case MineDiscovery::Power:
        speaker = "배전반";
        recorded = mine.powerBypassed;
        text = recorded ? "비상 전력이 공급되고 있습니다. 동쪽 격벽은 열린 상태입니다."
                        : "비상 전력을 우회했습니다. 동쪽 격벽이 열리고 비상등이 켜집니다.";
        break;
    case MineDiscovery::Signal:
        speaker = "구조 신호";
        recorded = mine.signalRecorded;
        text = recorded ? "실종 탐사선 식별자와 수신 기록이 보관돼 있습니다. 끊긴 통신선 너머에서 "
                          "신호가 들어오는 이유는 아직 확인되지 않았습니다."
                        : "실종 탐사선 식별자가 검출됩니다. 통신선은 벽 앞에서 끊겼는데 신호는 "
                          "안쪽에서 나옵니다. 수신 기록을 보관했습니다.";
        break;
    case MineDiscovery::Glyph:
        speaker = "문양 조사 기록";
        recorded = mine.glyphRecorded;
        text = recorded ? "보관한 형상과 벽면 문양을 다시 대조했습니다. 당신을 닮았지만 제작 시기와 "
                          "의미는 여전히 알 수 없습니다."
                        : "벽면에 당신을 닮은 형상이 있습니다. 제작 시기와 의미는 알 수 없습니다. "
                          "문양 조사 기록을 보관했습니다.";
        break;
    case MineDiscovery::Coordinates:
        speaker = "중계 단말";
        recorded = mine.coordinatesRecorded;
        if (!recorded && (!mine.signalRecorded || !mine.glyphRecorded))
        {
            return {{"requires-records",
                     speaker,
                     "단말 기록",
                     "구조 신호와 벽면 문양 기록이 있어야 단말의 중계 좌표를 확인할 수 있습니다.",
                     {}}};
        }
        text = recorded ? "중계 좌표는 이미 보관돼 있습니다. 현장 기록은 재방문해도 유지됩니다."
                        : "신호가 거쳐 간 중계 좌표를 보관했습니다. 헤일로 수신망에서 기존 기록과 "
                          "대조할 수 있습니다.";
        break;
    default:
        return {};
    }
    if (recorded || !riaPresent)
    {
        return {{recorded ? "archive" : "discovery/solo", speaker, "현장 기록", text, {}}};
    }
    switch (discovery)
    {
    case MineDiscovery::Power:
        text = "전력이 들어왔어. 동쪽 격벽도 열렸고. 비상등이 켜진 길을 따라가 보자.";
        break;
    case MineDiscovery::Signal:
        text = "식별자는 오래전에 실종된 탐사선의 것이야. 그런데 통신선은 이 벽 앞에서 끊겨 있어. "
               "신호는 벽 안쪽에서 오고 있고.";
        break;
    case MineDiscovery::Glyph:
        text = "이 문양, 너랑 닮았네. 그래도 널 새긴 거라고 단정하진 말자. 기록해 둘게.";
        break;
    case MineDiscovery::Coordinates:
        text = "좌표는 보관했어. 여기서 신호를 보낸 사람까지 알 수는 없겠네. 헤일로 수신망의 "
               "기록과 대조해 보자.";
        break;
    default:
        return {};
    }
    return {{"discovery/ria", "리아", "무전", text, {}}};
}
bool StoryDialogue::CanFollowUp(CompanionId companion, const game::ChapterOneState &chapter,
                                bool speakerAvailable)
{
    if (!speakerAvailable)
    {
        return false;
    }
    switch (companion)
    {
    case CompanionId::Ria:
        return chapter.generatorRestored && chapter.portableLightRepaired && chapter.ria.combat.health > 0;
    case CompanionId::Kain:
        return chapter.convoyEvacuated && chapter.kainJoined && chapter.kain.combat.health > 0;
    default:
        return false;
    }
}

std::vector<DialogueLine> StoryDialogue::FrontierFollowUp(CompanionId companion,
                                                          const game::ChapterOneState &chapter,
                                                          bool speakerAvailable)
{
    if (!CanFollowUp(companion, chapter, speakerAvailable))
    {
        return {};
    }
    if (companion == CompanionId::Ria)
    {
        return {{"light",
                 "리아",
                 "현장",
                 "잠깐 쉬었다 갈까? 여기서는 해야 할 일만 보고 걷게 되네. 서로 얼굴은 보고 다니자.",
                 {}},
                {"answer",
                 "나",
                 "현장",
                 "휴대 조명은 고쳐 뒀어. 불 하나 켰을 뿐인데, 아까보다 덜 멀리 "
                 "온 것 같더라. 돌아갈 때 챙겨 가자.",
                 {}},
                {"rest",
                 "리아",
                 "현장",
                 "돌아가면 따뜻한 것부터 마시자. 기록은 도망가지 않을 테니까. "
                 "출발할 때 불러 줘.",
                 {}}};
    }
    const auto arrived = std::count_if(chapter.survivors.begin(), chapter.survivors.end(),
                                       [](const auto &person) { return person.arrived; });
    const auto injured = std::count_if(chapter.survivors.begin(), chapter.survivors.end(),
                                       [](const auto &person) { return person.combat.health <= 0; });
    std::string count = chapter.survivors.empty()
                            ? "현장 명부를 다시 확인해야겠습니다. 도착 인원을 지금 단정하지 않겠습니다."
                            : "현장 명부 " + std::to_string(chapter.survivors.size()) +
                                  "명 중 안전 지점 "
                                  "도착을 확인한 사람은 " +
                                  std::to_string(arrived) + "명입니다.";
    if (injured)
    {
        count += " 의료 지원이 필요한 사람은 " + std::to_string(injured) + "명입니다.";
    }
    return {{"roll-call/" + std::to_string(chapter.survivors.size()) + "/" + std::to_string(arrived) + "/" +
                 std::to_string(injured),
             "카인",
             "현장",
             count,
             {}},
            {"question",
             "나",
             "현장",
             chapter.receiverInspected || (chapter.observedRecords & 2)
                 ? "무전에서는 다른 응답도 들렸는데, 그건 어떻게 적어 둘까?"
                 : "조금 전에도 확인했는데, 또 세는 거야?",
             {}},
            {"answer",
             "카인",
             "현장",
             "직접 확인한 사람부터 적겠습니다. 목소리만으로 누가 더 있다고 "
             "단정할 수는 없으니까요. 돌아갈 때도 한 번 더 확인하죠.",
             {}}};
}

std::vector<DialogueLine> StoryDialogue::MaraFollowUp(const GameState &state,
                                                      const game::ChapterOneState &chapter)
{
    if (!state.isPowerRestored || !state.isSideQuestComplete)
    {
        return {};
    }
    std::vector<DialogueLine> lines = {{"window",
                                        "마라",
                                        "현장",
                                        "불이 돌아오니 아이도 자기 방에 책을 가져가려 해요. 문은 "
                                        "열어 둘 거라네요.",
                                        {}},
                                       {"answer", "나", "현장", "불이 돌아와서 다행이에요.", {}}};
    if (chapter.generatorRestored)
    {
        lines.push_back({"power",
                         "나",
                         "현장",
                         "개척지 발전기에서도 전력이 새던 선로를 분리했어요. "
                         "이상한 점은 기록해 뒀고요.",
                         {}});
        lines.push_back({"thanks",
                         "마라",
                         "현장",
                         "밖에서도 애쓰셨군요. 오늘은 차 한 잔 마실 시간은 "
                         "있으세요?",
                         {}});
    }
    else
    {
        lines.push_back({"tea", "마라", "현장", "언제든 들러요. 따뜻한 차를 준비해 둘게요.", {}});
    }
    return lines;
}

std::vector<DialogueLine> StoryDialogue::FrontierComparison(bool riaAvailable, bool kainAvailable)
{
    return {{"match",
             "수신망",
             "단말 기록",
             "광산 좌표·발전기 전력 유출·운송대 교신 시각이 설비 "
             "N-0/17의 기록과 일치합니다. 외곽 설비의 원격 화면을 "
             "연결합니다.",
             {}},
            {"observation", "나", "현장", "관문은 멈춰 있는데, 그 주변에는 차례로 불이 들어온다.", {}},
            {"ria",
             riaAvailable ? "리아" : "나",
             riaAvailable ? "무전" : "현장",
             riaAvailable ? "관문 가동 기록은 없는데 외곽 설비에는 전력이 공급되고 있어. 멈춘 관문 "
                            "주변에서 무언가 계속 움직이고 있었던 거야."
                          : "관문 가동 기록은 없는데 외곽 설비에는 전력이 공급되고 있다. 전력이 어디로 "
                            "흐르는지 확인해야겠다.",
             {}},
            {"route",
             kainAvailable ? "카인" : "수신망",
             kainAvailable ? "무전" : "단말 기록",
             kainAvailable ? "지도에 있던 다리는 사라졌습니다. 아래 수로의 옛 진입로는 기록해 뒀습니다."
                           : "교량 연결을 확인할 수 없습니다. 보관 지도의 수로 진입로 좌표를 표시합니다.",
             {}},
            {"next",
             kainAvailable ? "카인" : "나",
             kainAvailable ? "무전" : "현장",
             kainAvailable ? "먼저 그 길이 남아 있는지 확인합시다."
                           : "수로에 실제로 길이 남아 있는지는 "
                             "직접 확인해야겠다.",
             {}}};
}
std::vector<DialogueLine> StoryDialogue::MineStatus(const game::MineState &mine, StoryContact contact)
{
    const auto stage = mine.quest.GetStage();
    if (contact == StoryContact::Ria)
    {
        std::string text = mine.quest.GetRiaDialogue();
        const auto delimiter = text.find(" / ");
        if (delimiter != std::string::npos)
        {
            text.erase(0, delimiter + 3);
        }
        if (stage == game::MineQuestStage::ReturnToHalo)
        {
            text = "좌표는 보관했어. 헤일로 수신망에 직접 보고하고 다른 기록과 대조하자.";
        }
        return {{"ria", "리아", "현장", text, {}}};
    }
    if (contact != StoryContact::Receiver)
    {
        return {};
    }
    if (stage == game::MineQuestStage::Complete)
    {
        return {{"reported",
                 "수신망",
                 "단말 기록",
                 "광산 좌표 보고는 완료됐습니다. 보관된 조사 기록을 "
                 "다시 열람할 수 있습니다.",
                 {}}};
    }
    std::vector<DialogueLine> lines;
    if (mine.coordinatesRecorded || mine.quest.AreCoordinatesFound())
    {
        lines.push_back({"coordinates",
                         "수신망",
                         "단말 기록",
                         "확보한 중계 좌표가 보관돼 있습니다. "
                         "기록 보유와 정식 조사 보고는 별도로 처리합니다.",
                         {}});
    }
    lines.push_back({"objective",
                     "수신망",
                     "조사 안내",
                     std::string("현재 조사에서 남은 행동: ") + mine.quest.GetObjective(),
                     {}});
    return lines;
}

std::vector<DialogueLine> StoryDialogue::FrontierStatus(const game::ChapterOneState &chapter,
                                                        StoryContact contact)
{
    if (contact != StoryContact::Ria && contact != StoryContact::Receiver)
    {
        return {};
    }
    const char *speaker = contact == StoryContact::Ria ? "리아" : "수신망";
    const char *channel = contact == StoryContact::Ria ? "현장" : "단말 기록";
    if (chapter.recordsCompared)
    {
        return {{"compared",
                 speaker,
                 channel,
                 "세 현장 기록은 N-0/17에서 겹칩니다. 기록 대조는 끝났지만 신호의 발신자와 "
                 "외곽 설비의 전력원은 아직 확인하지 못했습니다.",
                 {}},
                {"route",
                 "조사 기록",
                 "보관 기록",
                 chapter.waterwayUnlocked
                     ? "옛 수로 진입로 좌표를 보관했습니다. 실제 통행 가능 여부를 확인한 것은 아닙니다."
                     : "후속 경로는 아직 확인하지 않았습니다.",
                 {}},
                {"available",
                 "안내",
                 "활동 안내",
                 "현재 이용 가능한 이야기 구간의 조사는 여기까지입니다. 기존 지역 탐사와 남은 선택 "
                 "의뢰를 계속할 수 있습니다.",
                 {}}};
    }
    if (chapter.generatorRestored && chapter.convoyRescued && chapter.ledgerFound)
    {
        return {{"report",
                 speaker,
                 channel,
                 "세 현장의 필수 기록이 모였습니다. 헤일로 수신망에서 광산 좌표와 대조할 수 있습니다. "
                 "기록을 모은 것만으로 대조가 끝난 것은 아닙니다.",
                 {}}};
    }
    return {{"remaining",
             speaker,
             channel,
             "확보한 기록과 현장 상태는 유지됩니다. 발전기·운송대·폐설비의 남은 일은 원하는 순서로 "
             "살펴볼 수 있습니다. 조사 기록에서 추적할 목표를 선택하세요.",
             {}}};
}

std::vector<DialogueLine> StoryDialogue::FrontierReturn(const game::ChapterOneState &chapter)
{
    if (!chapter.recordsCompared)
    {
        return {};
    }
    auto lines = FrontierStatus(chapter, StoryContact::Receiver);
    lines.push_back({"return",
                     "수신망",
                     "재방문 안내",
                     "빛나는 개척지를 다시 살펴보시겠습니까?",
                     {{"revisit", "기존 현장 재방문"}, {"later", "지금은 기록만 확인한다"}}});
    return lines;
}
} // namespace tutorial
