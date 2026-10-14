#pragma once
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
    static void DrawPauseOverlay();
};

// ============================================================================
// CityHud : Hud
// ============================================================================
class CityHud final : public Hud
{
  public:
    void Draw() override;
    static bool ContainsMinimapBuilding(const Building &building);

  private:
    void DrawMinimap();
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
// JetTravelHud : Hud
// ============================================================================
class JetTravelHud final : public Hud
{
  public:
    void Draw() override;
};
} // namespace tutorial
