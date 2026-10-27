#pragma once
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include "ModelLibrary.h"
#include "Actor/Actor.h"

namespace rendering
{
struct Material
{
    models::Palette palette;
    float roughness = .7f;
    float metallic = .2f;
    float emission = 1;
    bool castsShadow = true;
};
class Mesh
{
  public:
    Mesh();
    ~Mesh();
    Mesh(const Mesh &) = delete;
    struct Data;
    std::unique_ptr<Data> data;
};
struct FrameStatistics
{
    unsigned int objects = 0, triangles = 0, drawCalls = 0, instancedDraws = 0, shadowDraws = 0;
};
} // namespace rendering

struct RenderPoint
{
    float x, y;
    float z = 0;
};
enum class SurfaceMaterial
{
    Concrete,
    Asphalt,
    Metal,
    Glass
};
class RenderAssets;

// ============================================================================
// RenderStyle — cartoon lighting and optional low-resolution world presentation
// ============================================================================
enum class WorldResolution
{
    Native = 1,
    Half = 2,
    Quarter = 4
};
struct RenderStyle
{
    bool cartoon = true;
    bool antialiasing = true;
    WorldResolution resolution = WorldResolution::Native;
    static float Shade(float diffuse, bool cartoon);
};
enum class SurfaceEffect
{
    Water,
    Flame,
    Pulse,
    JetExhaust
};

// Owns GPU resources; lifetime must stay inside the current GL context.
class Renderer
{
  public:
    Renderer(int width, int height);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    bool IsInitialized() const;
    bool HasPostProcessing() const;
    const RenderStyle &GetStyle() const;
    void SetStyle(RenderStyle style);
    int GetRenderWidth() const;
    int GetRenderHeight() const;
    void BeginScene(int viewportWidth, int viewportHeight);
    void EndScene(float seconds);
    void ResolveScene();
    int GetAntialiasingSamples() const;
    void DrawMaterial(const RenderPoint (&vertices)[4], SurfaceMaterial material);
    void DrawGlow(float x, float y, float size);
    void DrawEffect(const RenderPoint (&vertices)[4], SurfaceEffect effect, float seconds);
    void DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize);
    void DrawUiText(float x, float baselineY, const std::string &text, int size, bool outlined = false);
    bool DrawUiSprite(const std::string &name, float x, float y, float width, float height,
                      float fraction = 1, bool nineSlice = false);
    size_t UiSpriteCount() const;
    void DrawSolidRect(float x, float y, float z, float size, float r, float g, float b, float a);
    bool SubmitModel(const models::Model &model, RenderPoint screen, models::Point scale,
                     const models::Transform3D &transform, float depth, const models::Palette &palette,
                     const models::CharacterPose *pose);
    void BeginModels();
    void EndModels();
    void BindObject(std::vector<rendering::RenderObject> *objects);
    bool DeferOverlay(std::function<void()> draw);
    bool DrawMineDarkness();
    const rendering::FrameStatistics &GetFrameStatistics() const;

  private:
    void CreatePostProcessing();
    unsigned int CompileProgram(const char *vertex, const char *fragment);
    void SetPresentationViewport();
    struct MeshData;
    std::unique_ptr<MeshData> m_meshes;
    void DrawModels();
    int m_width, m_height, m_viewportWidth, m_viewportHeight;
    int m_renderWidth, m_renderHeight;
    RenderStyle m_style;
    unsigned int m_sceneTexture = 0;
    unsigned int m_sceneFramebuffer = 0;
    unsigned int m_sceneDepth = 0;
    unsigned int m_msaaFramebuffer = 0, m_msaaColor = 0, m_msaaDepth = 0;
    int m_msaaSamples = 0;
    unsigned int m_postProgram = 0;
    unsigned int m_effectProgram = 0;
    std::unique_ptr<RenderAssets> m_assets;
};

namespace tutorial
{

// ============================================================================
// GameRenderer — frame passes and backend lifetime
// ============================================================================
class GameRenderer
{
  public:
    static GameRenderer &Get();
    Renderer &Backend();
    void Initialize();
    void Shutdown();
    void Draw();

  private:
    std::unique_ptr<Renderer> renderer;
};
} // namespace tutorial
