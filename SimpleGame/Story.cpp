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
        add("resume", "아까 하던 얘기, 이제 이어서 할래? 손에 쥔 것부터 다시 펼쳐 보자.");
        if (id == MissionId::Mara)
        {
            lines.back().text = "다시 들러 주셨군요. 아까 말씀드린 불빛 이야기… 아직 괜찮으실까요?";
        }
    }
    switch (id)
    {
    case MissionId::Signal:
        if (state.gateSignalHeard)
        {
            addRecord("signal/archive",
                      "전망대에서 받아 둔 음성이 귓가에 맴돈다. 분명 내 목소리인데, 입 밖으로 낸 기억이 없다. "
                      "리아에게 이걸 들려주고 같이 파헤쳐 볼지 정해야 한다.");
        }
        else
        {
            add("signal/request",
                "야, 아침부터 골치 아픈 게 잡혔어. N-0 정기 측정 돌렸는데 전망대 수신기에 정체 모를 "
                "응답이 꽂혔거든. 점검표엔 없는 주파수야. 같이 올라가서 들어 줄래?");
        }
        break;
    case MissionId::Mine:
        if (mine.coordinatesRecorded)
        {
            addRecord("mine/coordinates",
                      "광산 지하에서 빼 온 중계 좌표가 아직 단말에 남아 있다. 전망대 목소리와 정말 이어지는 "
                      "건지, 도시 수신망에 걸어 보면 답이 나올 것이다.");
        }
        else if (mine.signalRecorded || mine.glyphRecorded || mine.powerBypassed)
        {
            addRecord("mine/visited",
                      "광산 안쪽에서 건져 온 흔적이 몇 개 있다. 전망대 신호가 거길 스쳐 간 건지 도시 "
                      "수신망에서 대조해 보고, 못 본 구역을 마저 뒤져야 한다.");
        }
        else
        {
            add("mine/request",
                "전망대 잡음 밑바닥에 낡은 광산 통신선 주소가 겹쳐 있어. 장비가 헛것을 잡은 건지, 진짜 "
                "누가 밑에서 부른 건지 확인해야겠어. 수신망부터 찍고 광산으로 가 볼래?");
        }
        break;
    case MissionId::Frontier:
        if (chapter.generatorRestored && chapter.convoyRescued && chapter.ledgerFound)
        {
            addRecord("frontier/report",
                      "발전기, 운송대, 폐설비—세 곳에서 주워 온 조각이 다 모였다. 헤일로 수신망에 한꺼번에 "
                      "올려 보면 이 소동의 밑그림이 드러날 것이다.");
        }
        else if (chapter.observedRecords || chapter.generatorInspected || chapter.receiverInspected ||
                 chapter.generatorRestored || chapter.convoyEvacuated || chapter.ledgerFound)
        {
            addRecord("frontier/visited",
                      "개척지 쪽에서 먼저 눈에 띈 단서들을 적어 두었다. 광산 좌표와 맞물린 발전기·운송대·"
                      "폐설비 가운데 남은 곳을 마저 돌아보면 된다.");
        }
        else
        {
            add("frontier/request",
                "광산에서 뽑아 온 좌표가 N-0 외곽 설비랑 딱 맞물려. 마침 그쪽 발전기 정전에 운송대 "
                "실종, 폐설비 약탈까지 한꺼번에 터졌거든. 우연이라고 넘기기엔 너무 찝찝해. 같이 돌아볼래?");
        }
        break;
    case MissionId::Mara:
        if (state.isPowerRestored)
        {
            addRecord("mara/restored",
                      "동쪽 전력 장치는 이미 손봐 두었다. 창문에 불이 들어왔을 테니 마라에게 가서 알려 주면 "
                      "안심할 것이다.");
        }
        else
        {
            add("mara/request",
                "실례지만 잠깐만요… 어젯밤부터 딸아이 방 전등이 아예 나가 버렸어요. 동쪽 전력 장치가 "
                "말썽이라는데, 밤마다 어둠 속에 혼자 두려니 마음이 쓰여서요. 한 번만 봐 주실 수 있을까요?");
        }
        break;
    default:
        return {};
    }
    lines.push_back(
        {"offer",
         speaker,
         channel,
         id == MissionId::Mara ? "바쁘시면 나중에 들르셔도 괜찮아요. 오늘은 시간이 되실까요?"
                               : "지금 바로 움직일까? 정비부터 하고 싶으면 천천히 와도 돼.",
         {{"accept", paused ? "조사를 이어간다" : "같이 확인한다"}, {"later", "조금 있다가 한다"}}});
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
        speaker = "비상 배전반";
        recorded = mine.powerBypassed;
        text = recorded ? "우회한 비상 전력이 웅웅거리며 흐르고 있다. 동쪽 격벽은 이미 활짝 열려 있다."
                        : "레버를 내리자 먼지 낀 코일이 튀며 비상등이 줄줄이 켜진다. 묵직한 굉음과 함께 "
                          "동쪽 격벽이 열렸다.";
        break;
    case MineDiscovery::Signal:
        speaker = "구조 신호 수신기";
        recorded = mine.signalRecorded;
        text = recorded ? "오래전 실종된 탐사선의 식별 코드가 아직 잡혀 있다. 전선은 벽 앞에서 뚝 끊겼는데, "
                          "신호는 여전히 돌벽 안쪽에서 흘러나온다."
                        : "잡음 사이로 낡은 탐사선의 구조 식별자가 떠오른다. 이상하다—인간이 깔아 둔 통신선은 "
                          "이 벽 앞에서 끝났는데, 신호는 벽 너머에서 울리고 있다.";
        break;
    case MineDiscovery::Glyph:
        speaker = "벽면 문양";
        recorded = mine.glyphRecorded;
        text = recorded ? "손전등을 다시 비춰 봐도 기분이 묘하다. 내 윤곽을 닮은 형상이지만, 누가 언제 "
                          "돌벽에 새긴 건지는 끝내 알 수 없다."
                        : "먼지를 닦아 내자 돌벽에 새겨진 사람 형상이 드러난다. 소름이 돋을 만큼 나와 닮았다. "
                          "누가, 언제 이런 걸 새겨 둔 걸까.";
        break;
    case MineDiscovery::Coordinates:
        speaker = "중계 단말";
        recorded = mine.coordinatesRecorded;
        if (!recorded && (!mine.signalRecorded || !mine.glyphRecorded))
        {
            return {{"requires-records",
                     speaker,
                     "단말 기록",
                     "단말이 암호화된 중계 좌표를 토해 내고 있다. 방금 들린 구조 신호와 벽면 문양을 먼저 "
                     "살펴봐야 이 좌표가 뭘 가리키는지 맞출 수 있다.",
                     {}}};
        }
        text = recorded ? "빼내 둔 중계 좌표가 단말에 저장되어 있다. 헤일로 수신망에 올려 대조해 보면 "
                          "신호가 어디로 흘렀는지 알 수 있다."
                        : "단말 깊은 곳에서 짧은 중계 좌표 하나를 건져 올렸다. 이 숫자가 가리키는 곳이 어딘지, "
                          "헤일로 수신망의 옛 지도와 맞춰 봐야 한다.";
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
        text = "오, 살아났다! 동쪽 격벽도 열렸어. 비상등 켜진 쪽으로 가 보자—발밑 조심하고.";
        break;
    case MineDiscovery::Signal:
        text = "잠깐… 이 식별자, 몇 년 전에 실종된 탐사선 거잖아. 근데 통신선은 이 벽 앞에서 끊겼어. "
               "신호가 콘크리트 너머에서 새어 나온다고?";
        break;
    case MineDiscovery::Glyph:
        text = "…어이, 이거 너랑 너무 닮았는데? 농담 아니야. 일단 사진 찍어 둘게—널 그려 놓은 거라고 "
               "지레짐작하진 말고. 아직은.";
        break;
    case MineDiscovery::Coordinates:
        text = "좌표는 뽑았어. 여기서 누가 보낸 건지까지 알아내긴 무리겠네. 헤일로로 돌아가서 도시 "
               "수신망 목록이랑 겹쳐 보자.";
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
                 "야, 잠깐만 숨 좀 돌리자. 밖으로 나오면 자꾸 계기판만 쳐다보게 돼. 얼굴은 보고 다녀야지, 안 그래?",
                 {}},
                {"answer",
                 "나",
                 "현장",
                 "아까 고친 휴대 조명, 생각보다 밝더라. 불 하나 켜졌을 뿐인데 여기까지 덜 멀리 온 기분이야.",
                 {}},
                {"rest",
                 "리아",
                 "현장",
                 "그치? 새 장비보다 손때 묻은 걸 살려 쓰는 게 훨씬 든든해. 헤일로 돌아가면 따뜻한 차부터 한 잔 하자. "
                 "출발할 때 불러 줘.",
                 {}}};
    }
    const auto arrived = std::count_if(chapter.survivors.begin(), chapter.survivors.end(),
                                       [](const auto &person) { return person.arrived; });
    const auto injured = std::count_if(chapter.survivors.begin(), chapter.survivors.end(),
                                       [](const auto &person) { return person.combat.health <= 0; });
    std::string count = chapter.survivors.empty()
                            ? "명부를 다시 훑어봐야겠습니다. 눈으로 확인하기 전엔 숫자를 적지 않습니다."
                            : "명부 " + std::to_string(chapter.survivors.size()) +
                                  "명 가운데 제 눈으로 안전 지점 도착을 확인한 분은 " +
                                  std::to_string(arrived) + "명입니다.";
    if (injured)
    {
        count += " 그중 " + std::to_string(injured) + "명은 당장 치료가 필요합니다.";
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
                 ? "무전 너머로 들렸던 여섯 번째 목소리는… 명부에 안 적을 거야?"
                 : "방금 전에도 셌잖아. 또 확인하는 거야?",
             {}},
            {"answer",
             "카인",
             "현장",
             "목소리만 믿고 숫자를 늘렸다가… 낭패를 본 적이 있습니다. 얼굴을 본 사람부터 적겠습니다. "
             "돌아갈 때도 한 번 더 셀 겁니다.",
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
                                        "전등이 켜지자마자 애가 그림책을 방으로 들고 가더라고요. "
                                        "그래도 방문은 조금 열어 둘 거래요—아직은 거실 소리가 들려야 안심이 되나 봐요.",
                                        {}},
                                       {"answer", "나", "현장", "불이 돌아와서 다행이에요. 밤에 덜 무섭겠네요.", {}}};
    if (chapter.generatorRestored)
    {
        lines.push_back({"power",
                         "나",
                         "현장",
                         "개척지 발전기에서도 전력이 새던 낡은 선로를 끊어 뒀어요. "
                         "당분간 주거 구역 정전은 덜할 거예요.",
                         {}});
        lines.push_back({"thanks",
                         "마라",
                         "현장",
                         "도시 밖까지 다니시느라 손이 마를 날이 없으시네요. "
                         "오늘은 쫓기지 말고 따뜻한 차 한 잔 하고 가세요.",
                         {}});
    }
    else
    {
        lines.push_back({"tea", "마라", "현장", "언제든 지나가다 들르세요. 주전자에 물은 늘 올려 두니까요.", {}});
    }
    return lines;
}

std::vector<DialogueLine> StoryDialogue::FrontierComparison(bool riaAvailable, bool kainAvailable)
{
    return {{"match",
             "헤일로 수신망",
             "단말 기록",
             "광산의 중계 좌표, 발전기의 전력 유출 주기, 운송대의 이상 교신 시각이 전부 하나의 설비 번호—N-0/17에서 "
             "겹쳐진다. 원격 감시 화면이 지지직거리며 켜진다.",
             {}},
            {"observation",
             "나",
             "현장",
             "화면 속 N-0 관문은 죽은 듯 멈춰 있다. 그런데 그 둘레를 감싼 외곽 설비에 불빛이 하나씩, 차례로 켜지고 있다.",
             {}},
            {"ria",
             riaAvailable ? "리아" : "나",
             riaAvailable ? "무전" : "현장",
             riaAvailable ? "세상에… 도시 당국은 관문 가동 기록이 없다고 했잖아. 문은 안 열렸을지 몰라도, "
                            "문 바깥에서 누군가 계속 전기를 끌어다 뭔가를 돌리고 있었던 거야."
                          : "관문 가동 기록은 비어 있는데 외곽 설비에는 전력이 흘러 들어가고 있다. "
                            "누군가 멈춘 관문 주변에서 계속 움직이고 있었다는 뜻이다.",
             {}},
            {"route",
             kainAvailable ? "카인" : "수신망",
             kainAvailable ? "무전" : "단말 기록",
             kainAvailable ? "예전 탐사대가 건너던 다리는 지도에서만 남아 있습니다. 하지만 다리 아래로 "
                             "지나던 옛 수로는 남아 있을 겁니다. 진입 좌표를 넘겨 두었습니다."
                           : "교량 구간 연결이 끊어졌습니다. 구 지도에서 관문 외곽으로 이어지는 옛 수로 진입로 좌표를 "
                             "불러왔습니다.",
             {}},
            {"next",
             kainAvailable ? "카인" : "나",
             kainAvailable ? "무전" : "현장",
             kainAvailable ? "먼저 그 수로가 아직 남아 있는지부터 눈으로 확인합시다. 준비되시면 말씀해 주십시오."
                           : "옛 수로에 정말 길이 남아 있는지, 직접 가서 확인해 볼 차례다.",
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
            text = "좌표는 챙겼어. 헤일로 수신망으로 돌아가서 예전 시설 목록이랑 겹쳐 보자.";
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
                 "헤일로 수신망",
                 "단말 기록",
                 "광산 중계 좌표는 이미 시설 목록과 대조를 마쳤다. 보관해 둔 기록은 언제든 다시 열어 볼 수 있다.",
                 {}}};
    }
    std::vector<DialogueLine> lines;
    if (mine.coordinatesRecorded || mine.quest.AreCoordinatesFound())
    {
        lines.push_back({"coordinates",
                         "헤일로 수신망",
                         "단말 기록",
                         "광산에서 빼 온 중계 좌표가 단말에 대기 중이다. 수신망에 정식으로 올리면 어느 시설과 "
                         "이어지는지 대조할 수 있다.",
                         {}});
    }
    lines.push_back({"objective",
                     "헤일로 수신망",
                     "조사 안내",
                     std::string("지금 남은 단서 추적: ") + mine.quest.GetObjective(),
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
    const char *speaker = contact == StoryContact::Ria ? "리아" : "헤일로 수신망";
    const char *channel = contact == StoryContact::Ria ? "현장" : "단말 기록";
    if (chapter.recordsCompared)
    {
        return {{"compared",
                 speaker,
                 channel,
                 contact == StoryContact::Ria
                     ? "세 현장이 전부 N-0/17 하나로 모였어. 누가 거기서 전기를 끌어다 썼는지, 신호는 대체 "
                       "누가 보낸 건지… 아직 껍질만 벗긴 기분이야."
                     : "세 현장 기록이 N-0/17에서 겹치는 것을 확인했다. 하지만 발신자의 정체와 외곽 설비의 "
                       "전력원은 여전히 베일에 싸여 있다.",
                 {}},
                {"route",
                 "조사 기록",
                 "보관 기록",
                 chapter.waterwayUnlocked
                     ? "카인이 표시해 둔 옛 수로 진입로 좌표가 단말에 들어와 있다. 길이 무너지지 않았는지는 "
                       "가 봐야 안다."
                     : "외곽으로 이어지는 우회 경로는 아직 찾지 못했다.",
                 {}},
                {"available",
                 speaker,
                 channel,
                 contact == StoryContact::Ria
                     ? "수로로 출발하기 전에 정비할 거 있으면 마저 챙겨. 광장 수신망에서 언제든 출발할 수 있어."
                     : "헤일로 수신망에서 옛 수로 조사로 출발하거나, 개척지의 남은 일을 마저 살펴볼 수 있다.",
                 {}}};
    }
    if (chapter.generatorRestored && chapter.convoyRescued && chapter.ledgerFound)
    {
        return {{"report",
                 speaker,
                 channel,
                 contact == StoryContact::Ria
                     ? "발전기, 운송대, 폐설비까지 다 돌았네! 이제 헤일로 수신망으로 가서 광산 좌표랑 "
                       "한꺼번에 맞춰 보자."
                     : "세 현장의 단서가 모두 모였다. 헤일로 수신망에서 광산 좌표와 대조하면 전체 연결이 "
                       "드러날 것이다.",
                 {}}};
    }
    return {{"remaining",
             speaker,
             channel,
             contact == StoryContact::Ria
                 ? "지금까지 챙긴 기록은 잘 담아 뒀어. 발전기, 운송대, 폐설비 중에 남은 곳부터 편한 순서로 "
                   "돌아보자."
                 : "확보한 단서는 그대로 남아 있다. 발전기·운송대·폐설비 가운데 아직 확인하지 못한 현장을 "
                   "마저 조사해야 한다.",
             {}}};
}

std::vector<DialogueLine> StoryDialogue::FrontierReturn(const game::ChapterOneState &chapter)
{
    if (!chapter.recordsCompared)
    {
        return {};
    }
    auto lines = FrontierStatus(chapter, StoryContact::Receiver);
    std::vector<DialogueChoice> choices = {{"revisit", "개척지 현장을 다시 둘러본다"},
                                           {"later", "지금은 기록만 확인한다"}};
    if (chapter.waterwayUnlocked)
    {
        choices.push_back({"waterway", "2장 · 외곽 수로로 출발한다"});
    }
    lines.push_back({"return",
                     "헤일로 수신망",
                     "경로 선택",
                     chapter.waterwayUnlocked
                         ? "N-0 외곽으로 이어지는 옛 수로 좌표가 열려 있다. 어떻게 움직일까?"
                         : "개척지 현장을 다시 살펴볼까?",
                     std::move(choices)});
    return lines;
}
} // namespace tutorial
