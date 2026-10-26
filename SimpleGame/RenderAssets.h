#pragma once
#include "Renderer.h"
#include <memory>

// Generated materials and bounded Korean text texture cache.
class RenderAssets
{
  public:
    static int MeasureUtf8(const std::string &text, int fontSize);
    RenderAssets();
    ~RenderAssets();
    void DrawMaterial(const RenderPoint (&vertices)[4], SurfaceMaterial material);
    void DrawGlow(float x, float y, float size);
    void DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize);

  private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
