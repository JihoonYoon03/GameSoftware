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
    void DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize, bool outlined = false);
    bool DrawUiSprite(const std::string &name, float x, float y, float width, float height,
                      float fraction = 1, bool nineSlice = false);
    size_t UiSpriteCount() const;

  private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
