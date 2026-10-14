#include "stdafx.h"
#include "Hud.h"
#include "Drawing.h"
#include "Level.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{

// ============================================================================
// CityHud
// ============================================================================
bool CityHud::ContainsMinimapBuilding(const Building &b)
{
    if (b.x < kWorldMinX || b.y < kWorldMinY || b.x + b.width > kWorldMaxX || b.y + b.depth > kWorldMaxY)
    {
        return false;
    }
    for (Vector2 p :
         {Vector2{b.x, b.y}, {b.x + b.width, b.y}, {b.x, b.y + b.depth}, {b.x + b.width, b.y + b.depth}})
    {
        float x = kCanvasWidth - 125.f + (p.x - p.y) * 2.5f;
        float y = 105 + (p.x + p.y) * 1.25f;
        if (x < kCanvasWidth - 229.f || x > kCanvasWidth - 28.f || y < 54 || y > 164)
        {
            return false;
        }
    }
    return true;
}
void CityHud::DrawMinimap()
{
    auto &world = Game::Get().GetState();
    if (!world.isInsideHome && !world.isInObservatory && !world.isJetTravel)
    {
        float mapCenterX = kCanvasWidth - 125.f, mapCenterY = 105;
        Drawing::DrawRectangle(kCanvasWidth - 235.f, 20, 213, 150, ColorRGBA(.02f, .055f, .08f, .92f));
        Drawing::DrawLabel(kCanvasWidth - 220.f, 42, "구역 지도 / 청록색: 내 위치");
        for (const auto &building : world.buildings)
        {
            if (!ContainsMinimapBuilding(building))
            {
                continue;
            }
            auto project = [=](float x, float y) {
                return Vector2{mapCenterX + (x - y) * 2.5f, mapCenterY + (x + y) * 1.25f};
            };
            Drawing::DrawPolygon({project(building.x, building.y),
                                  project(building.x + building.width, building.y),
                                  project(building.x + building.width, building.y + building.depth),
                                  project(building.x, building.y + building.depth)},
                                 ColorRGBA(.24f, .34f, .4f));
        }
        const Vector2 points[] = {kHomeDoor,       kLiaPosition,     kMaraPosition,        kRelayPosition,
                                  kBeaconPosition, kJetBikePosition, kCityReceiverPosition};
        const InteractionTarget targets[] = {InteractionTarget::HomeEntrance, InteractionTarget::Lia,
                                             InteractionTarget::Mara,         InteractionTarget::PowerRelay,
                                             InteractionTarget::GateBeacon,   InteractionTarget::JetBike,
                                             InteractionTarget::CityReceiver};
        for (int i = 0; i < 7; i++)
        {
            if (!Game::Get().IsQuestInteraction(targets[i]))
            {
                continue;
            }
            float x = mapCenterX + (points[i].x - points[i].y) * 2.5f,
                  y = mapCenterY + (points[i].x + points[i].y) * 1.25f;
            Drawing::DrawEllipse(x, y, 3, 3, ColorRGBA(1, .68f, .35f));
            if (i == 4)
            {
                Drawing::DrawLabel(x + 5, y, "전망대");
            }
            if (i == 6)
            {
                Drawing::DrawLabel(x + 5, y, "수신망");
            }
            if (i == 5)
            {
                Drawing::DrawLabel(x + 5, y, "바이크");
            }
        }
        Drawing::DrawEllipse(mapCenterX + (world.playerPosition.x - world.playerPosition.y) * 2.5f,
                             mapCenterY + (world.playerPosition.x + world.playerPosition.y) * 1.25f, 4, 4,
                             ColorRGBA(.25f, 1, .9f));
    }
}

// ============================================================================
// CityHud
// ============================================================================
void CityHud::DrawInteractionPrompt()
{
    if (Game::Get().IsQuestInteraction(Game::Get().FindNearbyInteraction()))
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
                           "Esc 계속 / R 다시 시작 / Q 종료");
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
    Drawing::DrawLabel(40, 45, world.isInObservatory ? "헤일로 / N-0 전망대" : "헤일로 / 주거 구역 07",
                       ColorRGBA(.67f, .9f, .91f), GLUT_BITMAP_HELVETICA_18);
    Drawing::DrawLabel(40, 68,
                       world.isInsideHome      ? "플레이어의 집 / 06:14"
                       : world.isInObservatory ? "상층 관측 데크 / 고도 180 m"
                                               : "관문 인접 주거 구역 / 06:14",
                       ColorRGBA(.4f, .66f, .73f));
    Drawing::DrawLabel(
        40, 94,
        world.gateSignalHeard && world.hasDescendedObservatory
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
    Drawing::DrawLabel(40, 116, timer, ColorRGBA(.4f, .6f, .66f));
    DrawMinimap();
    Drawing::DrawRectangle(22, 143, 365, 64, ColorRGBA(.025f, .06f, .09f, .88f));
    Drawing::DrawLabel(40, 165, "선택 목표 / 남겨 둔 불빛", ColorRGBA(.96f, .72f, .4f));
    Drawing::DrawLabel(40, 188,
                       world.isSideQuestComplete   ? "완료: 마라의 집에 불빛이 돌아왔습니다."
                       : world.isPowerRestored     ? "광장의 마라에게 돌아가세요."
                       : world.isSideQuestAccepted ? "마라 동쪽의 전력 장치를 복구하세요."
                                                   : "주거 광장의 마라와 대화하세요.");
    Drawing::DrawLabel(26, (float)kCanvasHeight - 22,
                       "WASD 이동 / E 상호작용 / 1 디버그 광산 / Esc 일시정지 / 정지 중 R 다시 시작",
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
// JetTravelHud
// ============================================================================
void JetTravelHud::Draw()
{
    using namespace tutorial;
    Drawing::DrawRectangle(22, 20, 445, 108, ColorRGBA(.015f, .05f, .08f, .94f));
    Drawing::DrawLabel(40, 47, "제트 바이크 / 광산 진입 회랑", ColorRGBA(.55f, .93f, .94f),
                       GLUT_BITMAP_HELVETICA_18);
    char progress[128];
    std::snprintf(progress, sizeof(progress), "이동 %.0f%% / 속도 %.0f km/h / 잔해 회피 %d",
                  game::GameLevels::Travel().GetState().distance / game::kRouteLength * 100,
                  game::GameLevels::Travel().GetState().speed * 3.6f,
                  game::GameLevels::Travel().GetState().avoidedObstacles);
    Drawing::DrawLabel(40, 74, progress);
    Drawing::DrawRectangle(40, 88, 400, 7, ColorRGBA(.11f, .2f, .23f));
    Drawing::DrawRectangle(40, 88, 400 * game::GameLevels::Travel().GetState().distance / game::kRouteLength,
                           7, ColorRGBA(.3f, .85f, .88f));
    Drawing::DrawLabel(40, 118, "부스트 잔량", ColorRGBA(.7f, .78f, .8f));
    Drawing::DrawRectangle(142, 107, 290 * game::GameLevels::Travel().GetState().boostCharge, 7,
                           ColorRGBA(.95f, .64f, .26f));
    Drawing::DrawLabel(26, float(kCanvasHeight) - 22,
                       "W·S 위아래 조종 / D 가속 / A 감속 / Space 부스트 / Esc 일시정지");
}

void Hud::Render()
{
    static CityHud city;
    static MineHud mine;
    static JetTravelHud travel;
    Hud &current = game::GameLevels::Mine().IsActive()     ? static_cast<Hud &>(mine)
                   : game::GameLevels::Travel().IsActive() ? static_cast<Hud &>(travel)
                                                           : static_cast<Hud &>(city);
    current.Draw();
    DrawNoticePanel();
    DrawPauseOverlay();
}
} // namespace tutorial
