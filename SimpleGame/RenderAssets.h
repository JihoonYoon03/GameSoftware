#pragma once
#include "Renderer.h"
#include <memory>

// Generated materials, character atlas and bounded Korean text texture cache.
class RenderAssets
{
  public:
    RenderAssets();
    ~RenderAssets();
    void DrawMaterial(const RenderPoint (&vertices)[4], SurfaceMaterial material);
    void DrawCharacter(float x, float footY, int frame, int direction);
    void DrawGlow(float x, float y, float size);
    void DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize);

  private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
