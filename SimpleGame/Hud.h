#pragma once
#include "Game.h"
namespace tutorial
{
struct Building;
struct UiControl
{
    UiAction action;
    Vector2 position, size;
    std::string label;
    bool enabled = true;
    int choice = -1;
};

// ============================================================================
// Hud — shared display lifecycle
// ============================================================================
class Hud
{
  public:
    virtual ~Hud() = default;
    virtual void Draw() = 0;
    static void Render();
    static void OpenMenu(UiScreen screen);
    static void CloseMenu();
    static bool HandleKey(unsigned char key);
    static bool HandleSpecialKey(int key);
    static bool HandlePointer(int button, bool down);
    static void HandleMotion();
    static std::vector<UiControl> Controls();
    static void ExchangeSupply();
    static void DrawMedicalFade();

  private:
    static void DrawNoticePanel();
    static void DrawDialogue();
    static void DrawVitals();
    static void DrawWeaponWheel();
    static void DrawObjective(const std::string &objective, const std::string &context);
    static void DrawInteraction(const std::string &label);
    static void DrawMenu();
    static void Activate(const UiControl &control, int direction = 1);
    friend class CityHud;
    friend class MineHud;
    friend class ChapterHud;
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
