#include "stdafx.h"
#include "Story.h"
#include "Level.h"

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
} // namespace tutorial
