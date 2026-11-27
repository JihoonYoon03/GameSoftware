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

struct PbrTextureBudget
{
    size_t residentBytes = 0;
    size_t budgetBytes = 128u * 1024u * 1024u;
    unsigned int pilotLayers = 0;
    unsigned int detailLayers = 0;
    bool immutableStorage = false;
    float anisotropy = 1.f;
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
    void SetTextRasterScale(float scale);
    void DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize, bool outlined = false);
    bool DrawUiSprite(const std::string &name, float x, float y, float width, float height,
                      float fraction = 1, bool nineSlice = false);
    size_t UiSpriteCount() const;
    bool DrawBackdropLayer(const std::string &name, bool cartoon, int renderWidth, float logicalWidth = 1280.f);
    unsigned int PbrBaseArray() const;
    unsigned int PbrNormalArray() const;
    unsigned int PbrOrmArray() const;
    unsigned int PbrDetailBaseArray() const;
    unsigned int PbrDetailNormalArray() const;
    unsigned int PbrDetailOrmArray() const;
    unsigned int AssetAlbedoArray() const;
    unsigned int AssetNormalArray() const;
    int AssetAlbedoLayer(const std::string &assetName) const;
    unsigned int DamageMaskTexture() const;
    unsigned int EffectMasksTexture() const;
    unsigned int WaterFlowTexture() const;
    unsigned int PlanetAlbedoTexture(const std::string &planetId) const;
    unsigned int PlanetCloudTexture(const std::string &planetId) const;
    unsigned int PlanetRingTexture(const std::string &planetId) const;
    unsigned int SpaceNebulaTexture() const;
    bool HasPbrTextures() const;
    bool HasPbrDetailTextures() const;
    bool HasAssetAlbedoTextures() const;
    bool HasDamageMask() const;
    bool HasEffectTextures() const;
    const PbrTextureBudget &PbrBudget() const;

  private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
