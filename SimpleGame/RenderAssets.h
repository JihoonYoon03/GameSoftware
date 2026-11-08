#pragma once
#include "Renderer.h"
#include <memory>
#include <vector>
#include <filesystem>
#include <map>

// CPU-only delivery contract; validation never creates a GL context or repacks source PNGs.
class UiAtlasCatalog
{
  public:
    struct Sprite
    {
        int x, y, width, height;
        std::string rgbaHash;
    };
    int width = 0, height = 0;
    std::filesystem::path image;
    std::map<std::string, Sprite> sprites;
    static UiAtlasCatalog Load(const std::filesystem::path &exportRoot);
};

// Generated materials and bounded Korean text texture cache.
class RenderAssets
{
  public:
    static int MeasureUtf8(const std::string &text, int fontSize);
    static std::vector<std::string> WrapUtf8(const std::string &text, float width, int fontSize);
    RenderAssets();
    ~RenderAssets();
    void DrawMaterial(const RenderPoint (&vertices)[4], SurfaceMaterial material);
    void DrawGlow(float x, float y, float size);
    void DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize, bool outlined = false);
    bool DrawUiSprite(const std::string &name, float x, float y, float width, float height,
                      float fraction = 1, bool nineSlice = false);
    size_t UiSpriteCount() const;
    bool DrawBackdropLayer(const std::string &name, bool cartoon, int renderWidth);

  private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
