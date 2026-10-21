#include "stdafx.h"
#include "Hud.h"
#include "Drawing.h"
#include "Overworld.h"
#include "Level.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{

// ============================================================================
// CityHud
// ============================================================================
void CityHud::DrawMinimap()
{
    const auto &world = Game::Get().GetState();
    if (world.isInsideHome || world.isInObservatory)
    {
        return;
    }
    Drawing::DrawRectangle(1004, 20, 254, 208, {.02f, .055f, .08f, .94f});
    Drawing::DrawLabel(1016, 42, "도시 연결망 / 인접 도시 약 20초");
    auto project = [](Vector2 p) { return Vector2{1022 + p.x * .4f, 66 + p.y * .42f}; };
    const auto &cities = Overworld::Cities();
    constexpr int links[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 1}};
    for (const auto &link : links)
    {
        Drawing::DrawLine(project(cities[link[0]].center), project(cities[link[1]].center),
                          {.28f, .48f, .55f}, 2);
    }
    Drawing::DrawLine(project({128, 0}), project(Overworld::kMineEntrance), {.8f, .6f, .3f}, 2);
    for (const auto &city : cities)
    {
        auto p = project(city.center);
        Drawing::DrawEllipse(p.x, p.y, 4, 4, {.5f, .75f, .8f});
        Drawing::DrawLabel(p.x - 12, p.y + 17, city.name);
    }
    auto entrance = project(Overworld::kMineEntrance);
    Drawing::DrawEllipse(entrance.x, entrance.y, 3, 3, {1, .72f, .3f});
    Drawing::DrawLabel(entrance.x - 12, entrance.y + 20, "광산");
    auto bike = project(world.bikePosition);
    Drawing::DrawEllipse(bike.x, bike.y, 3, 3, {.95f, .65f, .25f});
    auto player = project(world.playerPosition);
    Drawing::DrawEllipse(player.x, player.y, 4, 4, {.25f, 1, .9f});
    Drawing::DrawLabel(1016, 215, "청록: 플레이어 / 주황: 바이크");
}

// ============================================================================
// CityHud
// ============================================================================
void CityHud::DrawInteractionPrompt()
{
    if (Game::Get().FindNearbyInteraction() != InteractionTarget::None)
    {
        Drawing::DrawRectangle(kCanvasWidth * .5f - 95, kCanvasHeight * .69f, 190, 30,
                               ColorRGBA(.02f, .08f, .11f, .9f));
        Drawing::DrawLabel(kCanvasWidth * .5f - 76, kCanvasHeight * .69f + 20, "[ E ]  상호작용",
                           ColorRGBA(.5f, 1, .94f), GLUT_BITMAP_HELVETICA_18);
    }
}

// ============================================================================
// Hud
// ============================================================================
void Hud::DrawNoticePanel()
{
    auto &world = Game::Get().GetState();
    if (world.noticeSecondsRemaining <= 0)
    {
        return;
    }
    constexpr float panelWidth = 940;
    constexpr float fontSize = 15;
    float x = (kCanvasWidth - panelWidth) / 2;
    Drawing::DrawRectangle(x, kCanvasHeight - 132.f, panelWidth, 80, ColorRGBA(.02f, .055f, .085f, .96f));
    Drawing::DrawRectangle(x, kCanvasHeight - 132.f, 3, 80, ColorRGBA(.32f, .75f, .78f));
    // Advance complete UTF-8 codepoints; never split Korean syllables at byte offsets.
    const std::string &text = world.noticeText;
    size_t start = 0;
    for (int line = 0; start < text.size() && line < 3; ++line)
    {
        size_t end = start;
        float advance = 0;
        while (end < text.size())
        {
            unsigned char lead = static_cast<unsigned char>(text[end]);
            size_t bytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            float glyphWidth = bytes == 1 ? fontSize * .55f : fontSize;
            if (advance + glyphWidth > panelWidth - 32)
            {
                break;
            }
            if (end + bytes > text.size())
            {
                break;
            }
            advance += glyphWidth;
            end += bytes;
        }
        if (end == start)
        {
            break;
        }
        Drawing::DrawLabel(x + 16, kCanvasHeight - 107.f + line * 23, text.substr(start, end - start),
                           ColorRGBA(.82f, .9f, .91f));
        start = end;
        while (start < text.size() && text[start] == ' ')
        {
            ++start;
        }
    }
}

// ============================================================================
// Hud
// ============================================================================
void Hud::DrawPauseOverlay()
{
    auto &world = Game::Get().GetState();
    if (world.isPaused)
    {
        Drawing::DrawRectangle(0, 0, (float)kCanvasWidth, (float)kCanvasHeight,
                               ColorRGBA(0, .02f, .04f, .8f));
        Drawing::DrawLabel(kCanvasWidth * .5f - 90, kCanvasHeight * .5f, "일시정지",
                           ColorRGBA(.7f, .95f, .95f), GLUT_BITMAP_HELVETICA_18);
        Drawing::DrawLabel(kCanvasWidth * .5f - 90, kCanvasHeight * .5f + 30,
                           game::GameLevels::Chapter().IsActive()
                               ? "Esc 계속 / R 헤일로 귀환(기록 유지) / Q 종료"
                               : "Esc 계속 / R 다시 시작 / Q 종료");
    }
}

// ============================================================================
// CityHud
// ============================================================================
void CityHud::Draw()
{
    auto &world = Game::Get().GetState();
    Drawing::DrawRectangle(22, 20, 650, 111, ColorRGBA(.025f, .06f, .09f, .92f));
    Drawing::DrawRectangle(22, 20, 3, 111, ColorRGBA(.27f, .86f, .83f));
    int cityIndex = Overworld::CityAt(world.playerPosition);
    const char *area = cityIndex >= 0 ? Overworld::Cities()[cityIndex].name : "도시 간 연결 도로";
    Drawing::DrawLabel(40, 45, world.isInObservatory ? "헤일로 / N-0 전망대" : area,
                       ColorRGBA(.67f, .9f, .91f), GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(40, 68,
                       world.isInsideHome                          ? "플레이어의 집 / 06:14"
                       : world.isInObservatory                     ? "상층 관측 데크 / 고도 180 m"
                       : game::GameLevels::Chapter().IsAvailable() ? "광산 보고 다음 날 / 오전 작업 시간"
                                                                   : "관문 인접 주거 구역 / 06:14",
                       ColorRGBA(.4f, .66f, .73f));
    Drawing::DrawLabel(
        40, 94,
        game::GameLevels::Chapter().IsAvailable() ? game::GameLevels::Chapter().Objective()
        : world.gateSignalHeard && world.hasDescendedObservatory
            ? game::GameLevels::Mine().GetState().quest.GetObjective()
        : world.mainQuestStage == MainQuestStage::ReadHomeTerminal   ? "01  집 단말 확인 [E]"
        : world.mainQuestStage == MainQuestStage::MeetLia            ? "02  밖에서 리아와 대화 [E]"
        : world.mainQuestStage == MainQuestStage::EnterObservatory   ? "03  전망대 승강기로 올라가기 [E]"
        : world.mainQuestStage == MainQuestStage::ReadGateSignal     ? "04  상층 콘솔에서 관문 신호 측정 [E]"
        : world.mainQuestStage == MainQuestStage::DescendObservatory ? "05  승강기로 도시에 내려가기 [E]"
        : world.mainQuestStage == MainQuestStage::MountJetBike       ? "06  개인용 제트 바이크 탑승 [E]"
                                                                     : "07  광산 이동 / 서막 완료",
        ColorRGBA(.91f, .89f, .75f));
    char timer[80];
    std::snprintf(timer, sizeof(timer), "%02d:%02d / 짧은 서막", (int)world.playTimeSeconds / 60,
                  (int)world.playTimeSeconds % 60);
    Drawing::DrawLabel(
        40, 116, game::GameLevels::Chapter().IsAvailable() ? "1장 / 빛나는 개척지 · J 조사 기록" : timer,
        ColorRGBA(.4f, .6f, .66f));
    DrawMinimap();
    if (!world.isInsideHome && !world.isInObservatory)
    {
        char bike[160];
        std::snprintf(bike, sizeof(bike),
                      world.isBikeMounted ? "개인 바이크 / %.0f km/h · W 가속 S 제동 A/D 조향 · E 하차"
                                          : "도시 5곳 · 도시당 20명 / 주황 표시: 주차한 바이크 · E 탑승",
                      world.bikeSpeed * models::WorldScale::metersPerUnit * 3.6f);
        Drawing::DrawLabel(26, float(kCanvasHeight) - 46, bike, {.6f, .9f, .9f});
    }
    Drawing::DrawRectangle(22, 143, 365, 64, ColorRGBA(.025f, .06f, .09f, .88f));
    Drawing::DrawLabel(40, 165, "선택 목표 / 남겨 둔 불빛", ColorRGBA(.96f, .72f, .4f));
    Drawing::DrawLabel(40, 188,
                       world.isSideQuestComplete   ? "완료: 마라의 집에 불빛이 돌아왔습니다."
                       : world.isPowerRestored     ? "광장의 마라에게 돌아가세요."
                       : world.isSideQuestAccepted ? "마라 동쪽의 전력 장치를 복구하세요."
                                                   : "주거 광장의 마라와 대화하세요.");
    Drawing::DrawLabel(26, (float)kCanvasHeight - 22,
                       "WASD 이동·운전 / E 상호작용·승하차 / 광산 입구에서 하차 후 E 입장 / Esc 휴식",
                       ColorRGBA(.63f, .78f, .81f));
    DrawInteractionPrompt();
}

// ============================================================================
// MineHud
// ============================================================================
void MineHud::DrawScanner()
{
    constexpr float centerX = 1136, centerY = 139, radius = 90;
    if (game::GameLevels::Mine().GetState().scannerSeconds <= 0)
    {
        char message[100];
        std::snprintf(message, sizeof(message),
                      game::GameLevels::Mine().GetState().scannerCooldown > 0 ? "F 스캐너 충전 %.1f초"
                                                                              : "F 구조 신호 스캔",
                      game::GameLevels::Mine().GetState().scannerCooldown);
        Drawing::DrawLabel(1040, 42, message, {.45f, .8f, .8f});
        return;
    }
    Drawing::DrawRectangle(1014, 22, 244, 266, {.015f, .055f, .065f, .96f});
    Drawing::DrawLabel(1032, 46, "구조 신호 / 대략적 방위");
    Drawing::DrawEllipse(centerX, centerY, radius, radius, {.035f, .12f, .14f});
    float progress = 1 - game::GameLevels::Mine().GetState().scannerSeconds / 2.8f;
    float sweep = (std::min)(1.f, progress * 1.6f);
    constexpr int segments = 48;
    for (int i = 0; i < segments; ++i)
    {
        float a = i * kPi * 2 / segments, b = (i + 1) * kPi * 2 / segments;
        Drawing::DrawLine({centerX + std::cos(a) * radius * sweep, centerY + std::sin(a) * radius * sweep},
                          {centerX + std::cos(b) * radius * sweep, centerY + std::sin(b) * radius * sweep},
                          {.3f, 1, .8f, 1 - progress}, 2);
    }
    for (const auto &contact : game::GameLevels::Mine().GetState().scannerContacts)
    {
        if (game::GameLevels::Mine().GetState().rescueGroups[contact.group].rescued ||
            contact.distance / 42 > sweep)
        {
            continue;
        }
        float r = contact.distance / 42 * radius;
        float x = centerX + std::cos(contact.angle) * r, y = centerY + std::sin(contact.angle) * r;
        Drawing::DrawGlow({x, y}, 15, {.45f, 1, .7f, 1 - progress});
        Drawing::DrawEllipse(x, y, 5, 5, {.5f, 1, .7f, 1 - progress});
    }
    Drawing::DrawEllipse(centerX, centerY, 3, 3, {.7f, 1, .9f});
    Drawing::DrawLabel(1032, 254, "범위 42 / 표시 오차 있음");
    Drawing::DrawLabel(1032, 277, "스캔 시 위치 기준 · 곧 소멸");
}

// ============================================================================
// MineHud
// ============================================================================
void MineHud::Draw()
{
    auto &world = Game::Get().GetState();
    const auto &stats = game::GameLevels::Mine().GetState().player;
    Drawing::DrawRectangle(22, 20, 650, 183, {.02f, .04f, .055f, .94f});
    Drawing::DrawLabel(40, 47, "낙하한 신호 / 광산 구조 신호 조사", {.85f, .85f, .68f},
                       GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(40, 77, game::GameLevels::Mine().GetState().quest.GetObjective(), {.6f, .95f, .87f});
    char text[180];
    std::snprintf(text, sizeof(text), "체력 %d / %d  ·  레벨 %d  ·  경험치 %d  ·  처치 %d", stats.health,
                  stats.maxHealth, stats.level, stats.experience, game::GameLevels::Mine().GetState().kills);
    Drawing::DrawLabel(40, 105, text);
    Drawing::DrawRectangle(40, 116, 440, 7, {.2f, .07f, .07f});
    Drawing::DrawRectangle(40, 116, 440.f * stats.health / stats.maxHealth, 7, {.8f, .28f, .2f});
    std::snprintf(text, sizeof(text), "%s  %d / %d  ·  %s",
                  game::GameLevels::Mine().GetState().pistol.GetName(),
                  game::GameLevels::Mine().GetState().pistol.GetRounds(),
                  game::GameLevels::Mine().GetState().pistol.GetMagazineSize(),
                  game::GameLevels::Mine().GetState().pistol.IsReloading() ? "재장전 중"
                                                                           : "R 재장전 / 예비 탄창 무제한");
    Drawing::DrawLabel(40, 148, text, {.95f, .8f, .47f});
    if (game::GameLevels::Mine().GetState().pistol.IsReloading())
    {
        Drawing::DrawRectangle(40, 156, 440 * game::GameLevels::Mine().GetState().pistol.GetReloadProgress(),
                               4, {.95f, .75f, .3f});
    }
    std::snprintf(text, sizeof(text), "구조 %d / 12명 · 회복 키트 %d · 부품 %d · 리아 동행",
                  game::GameLevels::Mine().GetState().rescuedPeople, stats.recoveryKits, stats.salvage);
    Drawing::DrawLabel(40, 185, text, {.55f, .75f, .75f});
    DrawScanner();
    const auto cursor = world.cursorPosition;
    float gap = 6 + game::GameLevels::Mine().GetState().pistol.GetSpread() * 120;
    ColorRGBA crosshair = game::GameLevels::Mine().GetState().pistol.IsReloading() ? ColorRGBA(1, .65f, .3f)
                                                                                   : ColorRGBA(.75f, 1, .92f);
    Drawing::DrawLine({cursor.x - gap - 9, cursor.y}, {cursor.x - gap, cursor.y}, crosshair, 2);
    Drawing::DrawLine({cursor.x + gap, cursor.y}, {cursor.x + gap + 9, cursor.y}, crosshair, 2);
    Drawing::DrawLine({cursor.x, cursor.y - gap - 9}, {cursor.x, cursor.y - gap}, crosshair, 2);
    Drawing::DrawLine({cursor.x, cursor.y + gap}, {cursor.x, cursor.y + gap + 9}, crosshair, 2);
    Drawing::DrawEllipse(cursor.x, cursor.y, 1.5f, 1.5f, crosshair);
    Drawing::DrawLabel(
        26, kCanvasHeight - 22.f,
        "WASD 이동 / 마우스 조준·좌클릭 사격 / R 재장전 / F 스캔 / E 조사·구조·회수 / Q 회복 / Esc 정지");
}

// ============================================================================
// ChapterHud : Hud
// ============================================================================
void ChapterHud::Draw()
{
    auto &level = game::GameLevels::Chapter();
    const auto &state = level.GetState();
    const auto &mine = game::GameLevels::Mine().GetState();
    const auto &world = Game::Get().GetState();
    Drawing::DrawRectangle(22, 20, 750, 190, {.025f, .06f, .075f, .94f});
    Drawing::DrawLabel(40, 48, "1장 / 빛나는 개척지", {.85f, .94f, .84f}, GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(40, 74, level.Objective(), {.9f, .85f, .6f});
    Drawing::DrawLabel(40, 100,
                       state.generatorRestored ? "[완료] 발전기 / 폐쇄 선로 분리·도시 전력 안정"
                                               : "[조사] 발전기 / 정전과 주기적 전력 유출",
                       {.6f, .85f, .75f});
    Drawing::DrawLabel(40, 124,
                       state.convoyRescued   ? "[완료] 운송대 5명 구조 / 카인 합류"
                       : state.escortStarted ? "[진행] 운송대 5명 안전 지점 이동 중"
                                             : "[조사] 운송대 / 카인과 점호·측량 우회로 확보",
                       {.6f, .85f, .75f});
    Drawing::DrawLabel(40, 148,
                       state.ledgerFound       ? "[완료] 폐설비 / 기본 장부 확보"
                       : state.tradeProofFound ? "[조사] 폐설비 / 거래 기록 제시 또는 전투"
                                               : "[조사] 폐설비 / 입구 거래 기록·점거자와 대화",
                       {.6f, .85f, .75f});
    char text[180];
    std::snprintf(text, sizeof(text), "체력 %d/%d · 권총 %d/%d · %s · 동행: 리아%s", mine.player.health,
                  mine.player.maxHealth, mine.pistol.GetRounds(), mine.pistol.GetMagazineSize(),
                  mine.pistol.IsReloading() ? "재장전 중" : "R 재장전", state.kainJoined ? " · 카인" : "");
    Drawing::DrawLabel(40, 184, text, {.82f, .85f, .86f});
    if (state.mapVisible)
    {
        constexpr float x0 = 1042, y0 = 56, scale = 4;
        Drawing::DrawRectangle(1022, 20, 236, 236, {.02f, .06f, .07f, .94f});
        Drawing::DrawLabel(1038, 43, "현장 지도 / M 표시 전환");
        for (int y = 0; y < game::kFrontierHeight; ++y)
        {
            for (int x = 0; x < game::kFrontierWidth; ++x)
            {
                auto tile = state.tiles[y * game::kFrontierWidth + x];
                ColorRGBA color = tile == game::FrontierTile::Grass   ? ColorRGBA(.16f, .3f, .22f)
                                  : tile == game::FrontierTile::Sand  ? ColorRGBA(.4f, .33f, .2f)
                                  : tile == game::FrontierTile::Canal ? ColorRGBA(.06f, .09f, .1f)
                                                                      : ColorRGBA(.29f, .34f, .35f);
                Drawing::DrawRectangle(x0 + x * scale, y0 + y * scale, scale, scale, color);
            }
        }
        const Vector2 points[] = {game::kFrontierEntry, {10.5f, 10.5f}, {32.5f, 12.5f}, {33.5f, 32.5f}};
        const char *names[] = {"귀환", "발전기", "운송대", "폐설비"};
        for (int i = 0; i < 4; ++i)
        {
            Vector2 point = {x0 + points[i].x * scale, y0 + points[i].y * scale};
            Drawing::DrawEllipse(point.x, point.y, 3, 3, {1, .77f, .35f});
            Drawing::DrawLabel(point.x + 5, point.y - 5, names[i]);
        }
        Drawing::DrawEllipse(x0 + world.playerPosition.x * scale, y0 + world.playerPosition.y * scale, 3, 3,
                             {.2f, 1, .95f});
    }
    game::ChapterInteraction point{};
    if (level.NearestInteraction(point))
    {
        Drawing::DrawRectangle(350, 548, 580, 34, {.02f, .07f, .09f, .92f});
        Drawing::DrawLabel(368, 571, point.label, {.9f, .95f, .8f}, GLUT_BITMAP_HELVETICA_18);
    }
    auto cursor = world.cursorPosition;
    float gap = 6 + mine.pistol.GetSpread() * 120;
    ColorRGBA cross = mine.pistol.IsReloading() ? ColorRGBA(1, .65f, .3f) : ColorRGBA(.7f, 1, .9f);
    Drawing::DrawLine({cursor.x - gap - 8, cursor.y}, {cursor.x - gap, cursor.y}, cross, 2);
    Drawing::DrawLine({cursor.x + gap, cursor.y}, {cursor.x + gap + 8, cursor.y}, cross, 2);
    Drawing::DrawLine({cursor.x, cursor.y - gap - 8}, {cursor.x, cursor.y - gap}, cross, 2);
    Drawing::DrawLine({cursor.x, cursor.y + gap}, {cursor.x, cursor.y + gap + 8}, cross, 2);
    Drawing::DrawLabel(
        26, kCanvasHeight - 22.f,
        "WASD 이동 / E 조사·대화 / 좌클릭 사격 / R 재장전 / Q 회복 / M 지도 / J 기록 / Esc 일시정지");
}
void ChapterHud::DrawJournal()
{
    auto &level = game::GameLevels::Chapter();
    const auto &s = level.GetState();
    if (!level.IsAvailable())
    {
        return;
    }
    if (s.comparisonStarted && (s.comparisonSeconds < 12 || s.journalVisible))
    {
        Drawing::DrawRectangle(408, 226, 565, 92, {.02f, .055f, .085f, .96f});
        Drawing::DrawLabel(426, 249, "원격 관측 / N-0 외곽 설비 순차 점등");
        int lamps = int(Game::Clamp(s.comparisonSeconds - 1, 0, 6) * 8 / 6);
        for (int i = 0; i < 8; ++i)
        {
            Drawing::DrawRectangle(430.f + i * 62, 266, 44, 12,
                                   i < lamps ? ColorRGBA(.5f, .95f, .9f) : ColorRGBA(.1f, .2f, .23f));
        }
        Drawing::DrawLabel(426, 304,
                           s.recordsCompared ? "2장 조사 경로 확보 / 카인의 외곽 수로 지도"
                                             : "관문 자체 가동 기록 없음 / 신호 동일 발신 여부 미확인");
    }
    if (!s.journalVisible)
    {
        return;
    }
    Drawing::DrawRectangle(110, 225, 1060, 350, {.02f, .045f, .065f, .97f});
    Drawing::DrawLabel(135, 256, "조사 기록 / J 닫기", {.7f, .95f, .9f}, GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(
        135, 290, "광산: 실종 탐사선의 구조 신호·관측자 문양·중계 좌표. 자기 음성과의 동일 원인은 미확인.");
    Drawing::DrawLabel(
        135, 326,
        s.generatorInspected
            ? "발전기: 폐쇄 선로로 주기적 전력 유출. 분리 후에도 정지한 계기판 바늘이 같은 주기로 움직임."
            : "발전기: 아직 조사 기록 없음.");
    Drawing::DrawLabel(
        135, 362,
        s.receiverInspected
            ? "운송대: 실제 5명, 교신 응답 6명. 끊어진 수신기의 과거 교신 반복. 여섯 번째 구조 대상은 미확정."
            : "운송대: 아직 교신 조사 기록 없음.");
    Drawing::DrawLabel(135, 398,
                       s.tradeProofFound || s.ledgerFound
                           ? "폐설비: 구매자가 특정 중계 부품에 높은 값을 제시. 부품 철거 뒤 새 전원 유입. "
                             "점거자는 설계자가 아님."
                           : "폐설비: 아직 거래 조사 기록 없음.");
    Drawing::DrawLabel(
        135, 434,
        s.ledgerFound
            ? "기본 장부: 발주처 삭제. 광산 좌표·유출 주기·교신 이상 시각은 동일 설비 N-0/17에 연결."
            : "기본 장부: 미확보.");
    Drawing::DrawLabel(
        135, 470,
        s.kainJoined ? "카인: 구조 후 동행 합류. N-0 외곽 근무 경험을 밝혔으나 세부 과거는 아직 말하지 않음."
                     : "카인: 운송대 현장 점호·안전 우회로 확보를 우선.");
    Drawing::DrawLabel(135, 506,
                       s.detailLedgerFound
                           ? "선택 기록: 상세 구매 장부 보관. 구매자 추적 단서 확보 / 발주처의 정체는 미확인."
                           : "선택 기록: 상세 구매 장부 미확보.");
    Drawing::DrawLabel(135, 548,
                       s.recordsCompared
                           ? "다음 조사선: N-0 외곽 수로. 다리는 사라졌지만 아래 수로가 남아 있을 가능성."
                           : "귀환 조건: 세 현장 조사 완료 후 헤일로 수신망에서 기록 대조.",
                       {.9f, .8f, .55f});
}

void Hud::Render()
{
    static ChapterHud chapter;
    static CityHud city;
    static MineHud mine;
    Hud &current = game::GameLevels::Chapter().IsActive() ? static_cast<Hud &>(chapter)
                   : game::GameLevels::Mine().IsActive()  ? static_cast<Hud &>(mine)
                                                          : static_cast<Hud &>(city);
    current.Draw();
    ChapterHud::DrawJournal();
    DrawNoticePanel();
    DrawPauseOverlay();
}
} // namespace tutorial
