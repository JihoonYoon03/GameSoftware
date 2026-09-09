#include "stdafx.h"

#include "Tutorial.h"
#include "TutorialDrawing.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{
namespace
{
void DrawMinimap()
{
    if (!g_state.isInsideHome)
    {
        float mapCenterX = kCanvasWidth - 125.f, mapCenterY = 105;
        DrawRectangle(kCanvasWidth - 235.f, 20, 213, 150, ColorRGBA(.02f, .055f, .08f, .92f));
        DrawLabel(kCanvasWidth - 220.f, 42, "구역 지도 / 청록색: 내 위치");
        for (auto building : g_state.buildings)
        {
            float x = mapCenterX + (building.x - building.y) * 2.5f,
                  y = mapCenterY + (building.x + building.y) * 1.25f;
            DrawRectangle(x, y, 9, 6, ColorRGBA(.24f, .34f, .4f));
        }
        const Vector2 points[] = {kHomeDoor, kLiaPosition, kMaraPosition, kRelayPosition, kBeaconPosition};
        for (int i = 0; i < 5; i++)
        {
            float x = mapCenterX + (points[i].x - points[i].y) * 2.5f,
                  y = mapCenterY + (points[i].x + points[i].y) * 1.25f;
            DrawEllipse(x, y, 3, 3, ColorRGBA(1, .68f, .35f));
            if (i == 4)
            {
                DrawLabel(x + 5, y, "관문");
            }
        }
        DrawEllipse(mapCenterX + (g_state.playerPosition.x - g_state.playerPosition.y) * 2.5f,
                    mapCenterY + (g_state.playerPosition.x + g_state.playerPosition.y) * 1.25f, 4, 4,
                    ColorRGBA(.25f, 1, .9f));
    }
}

void DrawInteractionPrompt()
{
    if (FindNearbyInteraction() != InteractionTarget::None)
    {
        DrawRectangle(kCanvasWidth * .5f - 95, kCanvasHeight * .69f, 190, 30,
                      ColorRGBA(.02f, .08f, .11f, .9f));
        DrawLabel(kCanvasWidth * .5f - 76, kCanvasHeight * .69f + 20, "[ E ]  상호작용",
                  ColorRGBA(.5f, 1, .94f), GLUT_BITMAP_HELVETICA_18);
    }
}

void DrawNoticePanel()
{
    if (g_state.noticeSecondsRemaining <= 0)
    {
        return;
    }
    constexpr float panelWidth = 940;
    constexpr float fontSize = 15;
    float x = (kCanvasWidth - panelWidth) / 2;
    DrawRectangle(x, kCanvasHeight - 132.f, panelWidth, 80, ColorRGBA(.02f, .055f, .085f, .96f));
    DrawRectangle(x, kCanvasHeight - 132.f, 3, 80, ColorRGBA(.32f, .75f, .78f));
    // Advance complete UTF-8 codepoints; never split Korean syllables at byte offsets.
    const std::string &text = g_state.noticeText;
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
        DrawLabel(x + 16, kCanvasHeight - 107.f + line * 23, text.substr(start, end - start),
                  ColorRGBA(.82f, .9f, .91f));
        start = end;
        while (start < text.size() && text[start] == ' ')
        {
            ++start;
        }
    }
}

void DrawCompletionPanel()
{
    if (g_state.isComplete && g_state.noticeSecondsRemaining > 0)
    {
        DrawRectangle(kCanvasWidth * .5f - 250, 215, 500, 135, ColorRGBA(.025f, .045f, .08f, .95f));
        DrawLabel(kCanvasWidth * .5f - 224, 247, "튜토리얼 완료", ColorRGBA(.5f, .93f, .93f),
                  GLUT_BITMAP_HELVETICA_18);
        DrawLabel(kCanvasWidth * .5f - 224, 277, "관문이 당신의 목소리로 응답했습니다.");
        DrawLabel(kCanvasWidth * .5f - 224, 300,
                  g_state.isSideQuestComplete ? "남겨 둔 불빛: 완료"
                                              : "선택 목표: 마라의 전력 복구를 도와주세요.");
        DrawLabel(kCanvasWidth * .5f - 224, 329, "계속 탐험하거나 Esc → R로 다시 시작하세요.");
    }
}

void DrawPauseOverlay()
{
    if (g_state.isPaused)
    {
        DrawRectangle(0, 0, (float)kCanvasWidth, (float)kCanvasHeight, ColorRGBA(0, .02f, .04f, .8f));
        DrawLabel(kCanvasWidth * .5f - 90, kCanvasHeight * .5f, "일시정지", ColorRGBA(.7f, .95f, .95f),
                  GLUT_BITMAP_HELVETICA_18);
        DrawLabel(kCanvasWidth * .5f - 90, kCanvasHeight * .5f + 30, "Esc 계속 / R 다시 시작 / Q 종료");
    }
}
} // namespace

void DrawHud()
{
    DrawRectangle(22, 20, 365, 111, ColorRGBA(.025f, .06f, .09f, .92f));
    DrawRectangle(22, 20, 3, 111, ColorRGBA(.27f, .86f, .83f));
    DrawLabel(40, 45, "헤일로 / 주거 구역 07", ColorRGBA(.67f, .9f, .91f), GLUT_BITMAP_HELVETICA_18);
    DrawLabel(40, 68, g_state.isInsideHome ? "플레이어의 집 / 06:14" : "관문 인접 주거 구역 / 06:14",
              ColorRGBA(.4f, .66f, .73f));
    DrawLabel(40, 94,
              g_state.mainQuestStage == MainQuestStage::ReadHomeTerminal ? "01  집 단말 확인 [E]"
              : g_state.mainQuestStage == MainQuestStage::MeetLia        ? "02  밖에서 리아와 대화 [E]"
              : g_state.mainQuestStage == MainQuestStage::ReadGateSignal ? "03  전망대에서 관문 신호 측정"
                                                                         : "04  있을 수 없는 응답",
              ColorRGBA(.91f, .89f, .75f));
    char timer[80];
    std::snprintf(timer, sizeof(timer), "%02d:%02d / 짧은 서막", (int)g_state.playTimeSeconds / 60,
                  (int)g_state.playTimeSeconds % 60);
    DrawLabel(40, 116, timer, ColorRGBA(.4f, .6f, .66f));
    DrawMinimap();
    DrawRectangle(22, 143, 365, 64, ColorRGBA(.025f, .06f, .09f, .88f));
    DrawLabel(40, 165, "선택 목표 / 남겨 둔 불빛", ColorRGBA(.96f, .72f, .4f));
    DrawLabel(40, 188,
              g_state.isSideQuestComplete   ? "완료: 마라의 집에 불빛이 돌아왔습니다."
              : g_state.isPowerRestored     ? "광장의 마라에게 돌아가세요."
              : g_state.isSideQuestAccepted ? "마라 동쪽의 전력 장치를 복구하세요."
                                            : "주거 광장의 마라와 대화하세요.");
    DrawLabel(26, (float)kCanvasHeight - 22, "WASD 이동 / E 상호작용 / Esc 일시정지 / 정지 중 R 다시 시작",
              ColorRGBA(.63f, .78f, .81f));
    DrawInteractionPrompt();
    DrawNoticePanel();
    DrawCompletionPanel();
    DrawPauseOverlay();
}
} // namespace tutorial
