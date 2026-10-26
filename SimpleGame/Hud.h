#pragma once
#include "Game.h"
namespace tutorial
{
struct Building;

// ============================================================================
// Hud — shared display lifecycle
// ============================================================================
class Hud
{
  public:
    virtual ~Hud() = default;
    virtual void Draw() = 0;
    static void Render();

  private:
    static void DrawNoticePanel();
    static void DrawDialogue();
    static void DrawPauseOverlay();
};

// ============================================================================
// MapHud — fixed 45-degree schematic maps, circular clipping and destination selection
// ============================================================================
class MapHud
{
  public:
    static MapRegion Region();
    static void DrawMinimap();
    static void DrawFullMap();
    static bool HandleClick(Vector2 canvasPoint, bool clear = false);
    // Shared by rendering and pointer input, so selection survives letterboxed windows.
    static Vector2 WorldToMap(Vector2 worldPoint, bool fullMap);
    static Vector2 MapToWorld(Vector2 canvasPoint, bool fullMap);
    static bool Contains(Vector2 canvasPoint, bool fullMap);
};

// ============================================================================
// CityHud : Hud
// ============================================================================
class CityHud final : public Hud
{
  public:
    void Draw() override;

  private:
    void DrawInteractionPrompt();
};

// ============================================================================
// MineHud : Hud
// ============================================================================
class MineHud final : public Hud
{
  public:
    void Draw() override;

  private:
    void DrawScanner();
};

// ============================================================================
// ChapterHud : Hud — objectives, field map and evidence journal
// ============================================================================
class ChapterHud final : public Hud
{
  public:
    void Draw() override;
    static void DrawJournal();
};

} // namespace tutorial
