#pragma once
#include <memory>
#include <string>

struct RenderPoint
{
    float x, y;
};
enum class SurfaceMaterial
{
    Concrete,
    Asphalt,
    Metal,
    Glass
};
class RenderAssets;

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
    void BeginScene(int viewportWidth, int viewportHeight);
    void EndScene(float seconds);
    void DrawMaterial(const RenderPoint (&vertices)[4], SurfaceMaterial material);
    void DrawCharacter(float x, float footY, int frame, int direction);
    void DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize);
    void DrawSolidRect(float x, float y, float z, float size, float r, float g, float b, float a);

  private:
    void CreatePostProcessing();
    unsigned int CompileProgram(const char *vertex, const char *fragment);
    void SetPresentationViewport();
    int m_width, m_height, m_viewportWidth, m_viewportHeight;
    unsigned int m_sceneTexture = 0;
    unsigned int m_sceneFramebuffer = 0;
    unsigned int m_postProgram = 0;
    std::unique_ptr<RenderAssets> m_assets;
};
