#include "stdafx.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include "Dependencies/glew.h"
#include <windows.h>
#include "RenderAssets.h"
#include "AssetCache.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>
#include <iterator>
#include <filesystem>
#include <tuple>
#include <list>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#pragma comment(lib, "gdi32.lib")

namespace
{
// CPU layout caches are bounded and independent of GL/context lifetime. They are used
// only on the game thread, including CPU-only dialogue tests.
template <typename Key, typename Value> class LayoutCache
{
  public:
    const Value *Find(const Key &key)
    {
        auto found = entries.find(key);
        if (found == entries.end())
        {
            return nullptr;
        }
        recent.splice(recent.end(), recent, found->second.position);
        return &found->second.value;
    }
    void Insert(const Key &key, Value value)
    {
        constexpr size_t kLimit = 256;
        if (entries.size() == kLimit)
        {
            entries.erase(recent.front());
            recent.pop_front();
        }
        recent.push_back(key);
        entries.emplace(key, Entry{std::move(value), std::prev(recent.end())});
    }

  private:
    struct Entry
    {
        Value value;
        typename std::list<Key>::iterator position;
    };
    std::list<Key> recent;
    std::map<Key, Entry> entries;
};

GLuint Upload(int width, int height, const std::vector<unsigned char> &pixels)
{
    profiling::Scope timer(profiling::Timer::TextureUpload);
    profiling::Count(profiling::Counter::TextureUploadBytes, pixels.size());
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void Quad(GLuint texture, float x, float y, float width, float height, float u0 = 0, float v0 = 0,
          float u1 = 1, float v1 = 1)
{
    renderqueue::Texture(texture);
    renderqueue::Begin(GL_QUADS);
    renderqueue::TexCoord(u0, v0);
    renderqueue::Vertex(x, y);
    renderqueue::TexCoord(u1, v0);
    renderqueue::Vertex(x + width, y);
    renderqueue::TexCoord(u1, v1);
    renderqueue::Vertex(x + width, y + height);
    renderqueue::TexCoord(u0, v1);
    renderqueue::Vertex(x, y + height);
    renderqueue::End();
    renderqueue::Texture(0);
}

std::vector<unsigned char> CreateMaterialPixels(int type)
{
    constexpr int size = 64;
    std::vector<unsigned char> pixels;
    const std::string cacheName = "material-" + std::to_string(type) + ".rgba";
    if (assetcache::Load(cacheName, 2, size * size * 4, pixels))
    {
        return pixels;
    }
    pixels.resize(size * size * 4);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            unsigned int hash = unsigned(x * 73856093u) ^ unsigned(y * 19349663u);
            int grain = int((hash ^ (hash >> 13)) % 29) - 14;
            int brightness = 215 + grain;
            if (type == 1)
            {
                brightness = 175 + grain * 2; // Granular asphalt.
            }
            if (type == 2) // Brushed alloy without repeating tile borders.
            {
                brightness = 210 + grain / 3 + (y % 3) * 5;
                if ((x == 5 || x == 58) && (y == 5 || y == 58))
                {
                    brightness = 255;
                }
            }
            if (type == 3)
            {
                brightness = 160 + x + int(18 * std::sin((x + y) * .13f));
            }
            brightness = (std::max)(0, (std::min)(255, brightness));
            size_t offset = (y * size + x) * 4;
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = (unsigned char)brightness;
            pixels[offset + 3] = 255;
        }
    }
    assetcache::Save(cacheName, 2, pixels.data(), pixels.size());
    return pixels;
}

} // namespace

struct RenderAssets::Data
{
    struct TextTexture
    {
        GLuint texture;
        int width, height;
        unsigned long long lastUse;
    };
    struct Sprite
    {
        int x, y, width, height;
    };
    GLuint uiAtlas = 0;
    int uiWidth = 2048, uiHeight = 0;
    std::map<std::string, Sprite> sprites;
    GLuint atlas = 0;
    std::map<std::tuple<int, bool, std::string>, TextTexture> texts;
    unsigned long long clock = 0;
    ~Data()
    {
        glDeleteTextures(1, &atlas);
        glDeleteTextures(1, &uiAtlas);
        for (const auto &entry : texts)
        {
            glDeleteTextures(1, &entry.second.texture);
        }
    }
};

RenderAssets::RenderAssets() : m_data(new Data())
{
    namespace fs = std::filesystem;
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr, module, 32768);
    fs::path uiRoot;
    auto directory = fs::path(module).parent_path();
    for (int level = 0; level < 7 && !directory.empty(); ++level)
    {
        auto candidate = directory / L"Resource" / L"UI" / L"Export";
        if (fs::is_directory(candidate))
        {
            uiRoot = candidate;
            break;
        }
        auto parent = directory.parent_path();
        if (parent == directory)
        {
            break;
        }
        directory = parent;
    }
    if (uiRoot.empty())
    {
        throw std::runtime_error("UI assets missing: deploy Resource/UI/Export beside the game.");
    }
    struct ComScope
    {
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~ComScope()
        {
            if (SUCCEEDED(initialized))
            {
                CoUninitialize();
            }
        }
    } apartment;
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(factory.GetAddressOf()))))
        {
            throw std::runtime_error("UI PNG decoder initialization failed.");
        }
        // Decode straight into the final padded atlas, without retaining every PNG's pixels.
        std::vector<unsigned char> pixels;
        m_data->uiHeight = 1;
        GLint maximumTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
        int x = 2, y = 2, row = 0;
        for (const auto &group : {"Icons", "HUDIcons", "Components"})
        {
            std::vector<fs::path> paths;
            for (const auto &entry : fs::directory_iterator(uiRoot / group))
            {
                if (entry.path().extension() == L".png")
                {
                    paths.push_back(entry.path());
                }
            }
            std::sort(paths.begin(), paths.end());
            for (const auto &path : paths)
            {
                ComPtr<IWICBitmapDecoder> decoder;
                ComPtr<IWICBitmapFrameDecode> frame;
                ComPtr<IWICFormatConverter> converter;
                UINT w = 0, h = 0;
                if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnLoad, &decoder)) ||
                    FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || !w || !h ||
                    w > 2044 || h > 2044 || FAILED(factory->CreateFormatConverter(&converter)) ||
                    FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                                 WICBitmapDitherTypeNone, nullptr, 0,
                                                 WICBitmapPaletteTypeCustom)))
                {
                    throw std::runtime_error("Invalid UI PNG: " + path.filename().string());
                }
                if (x + int(w) + 2 > m_data->uiWidth)
                {
                    x = 2;
                    y += row + 4;
                    row = 0;
                }
                while (m_data->uiHeight < y + int(h) + 2)
                {
                    m_data->uiHeight *= 2;
                }
                if (m_data->uiWidth > maximumTextureSize || m_data->uiHeight > maximumTextureSize)
                {
                    throw std::runtime_error("UI atlas exceeds the graphics device texture limit.");
                }
                pixels.resize(size_t(m_data->uiWidth) * m_data->uiHeight * 4, 0);
                const UINT stride = UINT(m_data->uiWidth) * 4;
                const UINT bytes = (h - 1) * stride + w * 4;
                auto *destination = pixels.data() + (size_t(y) * m_data->uiWidth + x) * 4;
                if (FAILED(converter->CopyPixels(nullptr, stride, bytes, destination)))
                {
                    throw std::runtime_error("UI PNG pixel conversion failed.");
                }
                m_data->sprites.emplace(std::string(group) + "/" + path.stem().string(),
                                        Data::Sprite{x, y, int(w), int(h)});
                x += int(w) + 4;
                row = (std::max)(row, int(h));
            }
        }
        while (m_data->uiHeight < y + row + 2)
        {
            m_data->uiHeight *= 2;
        }
        pixels.resize(size_t(m_data->uiWidth) * m_data->uiHeight * 4, 0);
        m_data->uiAtlas = Upload(m_data->uiWidth, m_data->uiHeight, pixels);
    }
    // Four materials, a white cell, and a radial glow share a padded atlas.
    constexpr int cell = 66, atlasWidth = cell * 6;
    std::vector<unsigned char> atlas(atlasWidth * cell * 4, 255);
    for (int type = 0; type < 6; ++type)
    {
        std::vector<unsigned char> pixels(64 * 64 * 4, 255);
        if (type < 4)
        {
            pixels = CreateMaterialPixels(type);
        }
        if (type == 5)
        {
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 64; ++x)
                {
                    float dx = (x - 31.5f) / 31.5f, dy = (y - 31.5f) / 31.5f;
                    float radius = std::sqrt(dx * dx + dy * dy);
                    float alpha = 0;
                    for (int ring = 5; ring > 0; --ring)
                    {
                        float edge = ring / 5.f;
                        float coverage = (std::max)(0.f, (std::min)(1.f, (edge - radius) * 32));
                        alpha = 1 - (1 - alpha) * (1 - coverage * .02f * (6 - ring));
                    }
                    alpha = (std::max)(alpha, std::exp(-radius * radius * 180));
                    pixels[(y * 64 + x) * 4 + 3] = static_cast<unsigned char>(alpha * 255);
                }
            }
        }
        for (int y = 0; y < cell; ++y)
        {
            for (int x = 0; x < cell; ++x)
            {
                int sourceX = (std::max)(0, (std::min)(63, x - 1));
                int sourceY = (std::max)(0, (std::min)(63, y - 1));
                std::memcpy(&atlas[(y * atlasWidth + type * cell + x) * 4],
                            &pixels[(sourceY * 64 + sourceX) * 4], 4);
            }
        }
    }
    m_data->atlas = Upload(atlasWidth, cell, atlas);
    renderqueue::SolidSample(m_data->atlas, (4 * cell + 32.f) / atlasWidth, .5f);
}
RenderAssets::~RenderAssets()
{
    renderqueue::Flush();
}
void RenderAssets::DrawMaterial(const RenderPoint (&points)[4], SurfaceMaterial material)
{
    int index = static_cast<int>(material);
    float u0 = (index * 66 + 1.5f) / 396.f, u1 = (index * 66 + 64.5f) / 396.f;
    float v0 = 1.5f / 66, v1 = 64.5f / 66;
    renderqueue::Texture(m_data->atlas);
    renderqueue::Begin(GL_QUADS);
    renderqueue::TexCoord(u0, v0);
    renderqueue::Vertex(points[0].x, points[0].y);
    renderqueue::TexCoord(u1, v0);
    renderqueue::Vertex(points[1].x, points[1].y);
    renderqueue::TexCoord(u1, v1);
    renderqueue::Vertex(points[2].x, points[2].y);
    renderqueue::TexCoord(u0, v1);
    renderqueue::Vertex(points[3].x, points[3].y);
    renderqueue::End();
    renderqueue::Texture(0);
}
void RenderAssets::DrawGlow(float x, float y, float size)
{
    Quad(m_data->atlas, x - size * 2.1f, y - size * .9f, size * 4.2f, size * 1.8f, (5 * 66 + 1.5f) / 396,
         1.5f / 66, (5 * 66 + 64.5f) / 396, 64.5f / 66);
}

int RenderAssets::MeasureUtf8(const std::string &utf8, int fontSize)
{
    static LayoutCache<std::pair<int, std::string>, int> widths;
    const auto key = std::make_pair(fontSize, utf8);
    const bool cacheable = utf8.size() <= 4096;
    if (cacheable)
    {
        if (const auto *cached = widths.Find(key))
        {
            return *cached;
        }
    }
    struct Metrics
    {
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font;
        HGDIOBJ previous;
        explicit Metrics(int size)
        {
            font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                               L"Malgun Gothic");
            previous = dc && font ? SelectObject(dc, font) : nullptr;
        }
        ~Metrics()
        {
            if (previous)
            {
                SelectObject(dc, previous);
            }
            if (font)
            {
                DeleteObject(font);
            }
            if (dc)
            {
                DeleteDC(dc);
            }
        }
    };
    static std::map<int, std::unique_ptr<Metrics>> fonts;
    auto &metrics = fonts[fontSize];
    if (!metrics)
    {
        metrics = std::make_unique<Metrics>(fontSize);
    }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()), nullptr, 0);
    if (!count || !metrics->dc || !metrics->font)
    {
        return 0;
    }
    std::wstring text(count, L' ');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()), &text[0], count);
    SIZE extent{};
    if (!GetTextExtentPoint32W(metrics->dc, text.data(), count, &extent))
    {
        return 0;
    }
    if (cacheable)
    {
        widths.Insert(key, int(extent.cx));
    }
    return int(extent.cx);
}
std::vector<std::string> RenderAssets::WrapUtf8(const std::string &text, float width, int fontSize)
{
    static LayoutCache<std::tuple<int, float, std::string>, std::vector<std::string>> layouts;
    const bool cacheable = text.size() <= 4096 && std::isfinite(width) && width > 0;
    const auto key = std::make_tuple(fontSize, width, text);
    if (cacheable)
    {
        if (const auto *cached = layouts.Find(key))
        {
            return *cached;
        }
    }
    std::vector<std::string> result;
    for (size_t start = 0; start < text.size();)
    {
        size_t end = start, lastSpace = start;
        while (end < text.size() && text[end] != '\n')
        {
            unsigned char lead = static_cast<unsigned char>(text[end]);
            size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
            if (end + bytes > text.size())
            {
                break;
            }
            if (RenderAssets::MeasureUtf8(text.substr(start, end + bytes - start), fontSize) > width &&
                end > start)
            {
                break;
            }
            if (text[end] == ' ')
            {
                lastSpace = end;
            }
            end += bytes;
        }
        if (end == start && end < text.size() && text[end] != '\n')
        {
            ++end;
        }
        if (end < text.size() && text[end] != '\n' && lastSpace > start)
        {
            end = lastSpace;
        }
        result.push_back(text.substr(start, end - start));
        start = end;
        if (start < text.size() && text[start] == '\n')
        {
            ++start;
        }
        while (start < text.size() && text[start] == ' ')
        {
            ++start;
        }
    }
    if (result.empty())
    {
        result.push_back("");
    }
    if (cacheable)
    {
        layouts.Insert(key, result);
    }
    return result;
}
void RenderAssets::DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize,
                                bool outlined)
{
    if (utf8.empty())
    {
        return;
    }
    const auto key = std::make_tuple(fontSize, outlined, utf8);
    auto found = m_data->texts.find(key);
    if (found == m_data->texts.end())
    {
        profiling::Count(profiling::Counter::TextCacheMisses);
        profiling::Scope timer(profiling::Timer::TextRasterize);
        int count =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), nullptr, 0);
        if (count <= 0)
        {
            return;
        }
        std::wstring text(count, L' ');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &text[0], count);
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc)
        {
            return;
        }
        HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                                 L"Malgun Gothic");
        if (!font)
        {
            DeleteDC(dc);
            return;
        }
        HGDIOBJ previousFont = SelectObject(dc, font);
        SIZE extent = {};
        GetTextExtentPoint32W(dc, text.data(), count, &extent);
        int padding = outlined ? 3 : 1;
        int width = (std::max)(1, int(extent.cx) + padding * 2 + 2),
            height = (std::max)(1, int(extent.cy) + padding * 2 + 2);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *memory = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &memory, nullptr, 0);
        if (!bitmap)
        {
            SelectObject(dc, previousFont);
            DeleteObject(font);
            DeleteDC(dc);
            return;
        }
        HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
        std::memset(memory, 0, width * height * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, padding, padding, text.data(), count);
        GdiFlush();
        const auto *source = static_cast<unsigned char *>(memory);
        std::vector<unsigned char> pixels(width * height * 4);
        for (int i = 0; i < width * height; ++i)
        {
            pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
            pixels[i * 4 + 3] = (std::max)(source[i * 4], (std::max)(source[i * 4 + 1], source[i * 4 + 2]));
        }
        if (outlined)
        {
            auto mask = pixels;
            for (int yy = 0; yy < height; ++yy)
            {
                for (int xx = 0; xx < width; ++xx)
                {
                    int alpha = 0;
                    for (int oy = -2; oy <= 2; ++oy)
                    {
                        for (int ox = -2; ox <= 2; ++ox)
                        {
                            if (ox * ox + oy * oy <= 5 && xx + ox >= 0 && xx + ox < width && yy + oy >= 0 &&
                                yy + oy < height)
                            {
                                alpha = (std::max)(alpha, int(mask[((yy + oy) * width + xx + ox) * 4 + 3]));
                            }
                        }
                    }
                    size_t i = size_t(yy * width + xx) * 4;
                    auto inside = mask[i + 3];
                    pixels[i] = pixels[i + 1] = pixels[i + 2] =
                        alpha ? static_cast<unsigned char>(255 * int(inside) / alpha) : 0;
                    pixels[i + 3] = static_cast<unsigned char>(alpha);
                }
            }
        }
        GLuint texture = Upload(width, height, pixels);
        SelectObject(dc, previousBitmap);
        SelectObject(dc, previousFont);
        DeleteObject(bitmap);
        DeleteObject(font);
        DeleteDC(dc);
        // Bound transient timer/NPC label textures. Evict the least recently drawn entry.
        if (m_data->texts.size() >= 512)
        {
            auto oldest = std::min_element(
                m_data->texts.begin(), m_data->texts.end(),
                [](const auto &a, const auto &b) { return a.second.lastUse < b.second.lastUse; });
            renderqueue::Flush();
            profiling::Count(profiling::Counter::TextCacheEvictions);
            glDeleteTextures(1, &oldest->second.texture);
            m_data->texts.erase(oldest);
        }
        found = m_data->texts.emplace(key, Data::TextTexture{texture, width, height, 0}).first;
    }
    else
    {
        profiling::Count(profiling::Counter::TextCacheHits);
    }
    found->second.lastUse = ++m_data->clock;
    const auto &texture = found->second;
    Quad(texture.texture, x - (outlined ? 2 : 0), baselineY - fontSize - (outlined ? 2 : 0),
         (float)texture.width, (float)texture.height);
}

size_t RenderAssets::UiSpriteCount() const
{
    return m_data->sprites.size();
}
bool RenderAssets::DrawUiSprite(const std::string &name, float x, float y, float width, float height,
                                float fraction, bool nineSlice)
{
    auto found = m_data->sprites.find(name);
    if (found == m_data->sprites.end() || width <= 0 || height <= 0)
    {
        return false;
    }
    const auto &s = found->second;
    auto draw = [&](float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh) {
        Quad(m_data->uiAtlas, dx, dy, dw, dh, (s.x + sx) / m_data->uiWidth, (s.y + sy) / m_data->uiHeight,
             (s.x + sx + sw) / m_data->uiWidth, (s.y + sy + sh) / m_data->uiHeight);
    };
    if (!nineSlice)
    {
        fraction = (std::max)(0.f, (std::min)(1.f, fraction));
        if (fraction > 0)
        {
            draw(x, y, width * fraction, height, .5f, .5f, (s.width - 1.f) * fraction, s.height - 1.f);
        }
        return true;
    }
    float corner = (std::min)(12.f, (std::min)(width, height) * .5f);
    const float dx[] = {x, x + corner, x + width - corner, x + width};
    const float dy[] = {y, y + corner, y + height - corner, y + height};
    const float sx[] = {.5f, 12.f, s.width - 12.f, s.width - .5f};
    const float sy[] = {.5f, 12.f, s.height - 12.f, s.height - .5f};
    for (int yy = 0; yy < 3; ++yy)
    {
        for (int xx = 0; xx < 3; ++xx)
        {
            draw(dx[xx], dy[yy], dx[xx + 1] - dx[xx], dy[yy + 1] - dy[yy], sx[xx], sy[yy],
                 sx[xx + 1] - sx[xx], sy[yy + 1] - sy[yy]);
        }
    }
    return true;
}
