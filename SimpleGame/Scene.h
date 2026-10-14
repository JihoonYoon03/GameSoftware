#pragma once
#include "Scene/SceneGraph.h"
#include "Game.h"
#include "Level.h"
#include <functional>
#include <unordered_set>
namespace tutorial
{
struct ColorRGBA;
}

namespace game
{
using tutorial::ColorRGBA;
using tutorial::Vector2;

// ============================================================================
// Scene — persistent Actor placement
// ============================================================================
// Synchronizes legacy placement data into persistent Actor nodes.
// Missing placements are removed at EndPlacement; surviving nodes keep identity.
class Scene
{
  public:
    virtual ~Scene() = default;
    virtual void Draw()
    {
    }
    void BeginPlacement();
    Actor &Place(const std::string &name, const std::string &group, Bounds bounds, int layer, float depth,
                 Actor::DrawCallback callback);
    void EndPlacement();
    SceneGraph &GetGraph();
    static Bounds WorldBounds(float x, float y, float width, float depth, float height, float margin = 40);
    void PlaceWorldActor(const std::string &name, float x, float y, Bounds bounds, int layer, float depth,
                         std::function<void()> draw);
    void DrawActors();

  private:
    SceneGraph graph;
    std::unordered_set<std::string> placed;
    std::unordered_set<std::string> previous;
};

enum class SceneKind
{
    Interior,
    Exterior,
    Observatory,
    JetTravel,
    TravelBackground,
    Mine,
    Background,
    Hud,
    Count
};

// ============================================================================
// ScreenScene : Scene — projected screen-space placement
// ============================================================================
class ScreenScene : public Scene
{
  protected:
    static void PlaceScreenActor(Scene &scene, const std::string &name, Bounds bounds, int layer,
                                 std::function<void()> draw);
    static void DrawScreenScene(Scene &scene);
};

// ============================================================================
// InteriorScene : Scene
// ============================================================================
class InteriorScene final : public Scene
{
  public:
    void Draw() override;
};

// ============================================================================
// CityScene : Scene
// ============================================================================
class CityScene final : public Scene
{
  public:
    void Draw() override;

  private:
    static Vector2 GetTrafficPosition(int index);
    static models::Palette GetTrafficPalette(int index);
    void DrawStreetTiles(game::Scene &scene);
    void DrawBoundary(game::Scene &scene);
    void DrawStreetFurniture(game::Scene &scene);
    void DrawDepthSortedObjects(game::Scene &scene);
    void DrawUtilityDevices(game::Scene &scene);
    void DrawQuestMarkers(game::Scene &scene);
};

// ============================================================================
// ObservatoryScene : Scene
// ============================================================================
class ObservatoryScene final : public Scene
{
  public:
    void Draw() override;
};

// ============================================================================
// MineScene : Scene
// ============================================================================
class MineScene final : public Scene
{
  public:
    void Draw() override;
    void DrawDarkness();

  private:
    void PlacePerson(game::Scene &scene, const std::string &id, Vector2 position, ColorRGBA coat,
                     models::CharacterType type, bool walking, const std::string &label, float facingYaw,
                     models::Gender gender);
    void PlaceClue(game::Scene &scene, const char *id, Vector2 position, const char *label, ColorRGBA color,
                   MineQuestStage expected, bool glyph = false);
    std::uint64_t cachedRevision = 0;
};

// ============================================================================
// JetTravelScene : ScreenScene
// ============================================================================
struct SceneryTile
{
    int id;
    float x;
};
// Unwrapped distance and persistent world indices keep varied scenery continuous.
class TravelScenery
{
  public:
    static constexpr float kRoadScroll = kRoutePixelScale;
    static constexpr float kForegroundScroll = 18;
    static std::vector<SceneryTile> VisibleTiles(double distance, float pixelsPerUnit, float spacing,
                                                 float margin = 200);
    static unsigned int Variant(int id, int layer);
};
class JetTravelScene final : public ScreenScene
{
  public:
    void Draw() override;
};

// ============================================================================
// TravelBackgroundScene : ScreenScene
// ============================================================================
class TravelBackgroundScene final : public ScreenScene
{
  public:
    void Draw() override;
};

// ============================================================================
// BackgroundScene : ScreenScene
// ============================================================================
class BackgroundScene final : public ScreenScene
{
  public:
    void Draw() override;
};

// ============================================================================
// HudScene : ScreenScene
// ============================================================================
class HudScene final : public ScreenScene
{
  public:
    void Draw() override;
};

// ============================================================================
// SceneManager — scene ownership and active-scene selection
// ============================================================================
class SceneManager
{
  public:
    static Scene &Get(SceneKind kind);
    static SceneKind Current();
    static void Reset();
    static void Update(float deltaSeconds);
};

} // namespace game
