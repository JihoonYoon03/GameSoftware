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

#pragma comment(lib, "gdi32.lib")

namespace
{
GLuint Upload(int width, int height, const std::vector<unsigned char> &pixels, bool repeat = false)
{
    profiling::Scope timer(profiling::Timer::TextureUpload);
    profiling::Count(profiling::Counter::TextureUploadBytes, pixels.size());
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
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

GLuint CreateMaterial(int type)
{
    constexpr int size = 64;
    std::vector<unsigned char> pixels;
    const std::string cacheName = "material-" + std::to_string(type) + ".rgba";
    if (assetcache::Load(cacheName, 2, size * size * 4, pixels))
    {
        return Upload(size, size, pixels, true);
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
    return Upload(size, size, pixels, true);
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
    GLuint materials[4] = {};
    GLuint atlas = 0;
    std::map<std::pair<int, std::string>, TextTexture> texts;
    unsigned long long clock = 0;
};

RenderAssets::RenderAssets() : m_data(new Data())
{
    for (int i = 0; i < 4; ++i)
    {
        m_data->materials[i] = CreateMaterial(i);
    }
    // Four materials, a white cell, and a radial glow share a padded atlas.
    constexpr int cell = 66, atlasWidth = cell * 6;
    std::vector<unsigned char> atlas(atlasWidth * cell * 4, 255);
    for (int type = 0; type < 6; ++type)
    {
        std::vector<unsigned char> pixels(64 * 64 * 4, 255);
        if (type < 4)
        {
            glBindTexture(GL_TEXTURE_2D, m_data->materials[type]);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
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
    glDeleteTextures(4, m_data->materials);
    std::fill(std::begin(m_data->materials), std::end(m_data->materials), 0);
    m_data->atlas = Upload(atlasWidth, cell, atlas);
    renderqueue::SolidSample(m_data->atlas, (4 * cell + 32.f) / atlasWidth, .5f);
}
RenderAssets::~RenderAssets()
{
    renderqueue::Flush();
    glDeleteTextures(1, &m_data->atlas);
    glDeleteTextures(4, m_data->materials);
    for (const auto &entry : m_data->texts)
    {
        glDeleteTextures(1, &entry.second.texture);
    }
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
    return GetTextExtentPoint32W(metrics->dc, text.data(), count, &extent) ? int(extent.cx) : 0;
}
void RenderAssets::DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize)
{
    if (utf8.empty())
    {
        return;
    }
    const auto key = std::make_pair(fontSize, utf8);
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
        int width = (std::max)(1, int(extent.cx) + 4), height = (std::max)(1, int(extent.cy) + 4);
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
        TextOutW(dc, 1, 1, text.data(), count);
        GdiFlush();
        const auto *source = static_cast<unsigned char *>(memory);
        std::vector<unsigned char> pixels(width * height * 4);
        for (int i = 0; i < width * height; ++i)
        {
            pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
            pixels[i * 4 + 3] = (std::max)(source[i * 4], (std::max)(source[i * 4 + 1], source[i * 4 + 2]));
        }
        GLuint texture = Upload(width, height, pixels);
        SelectObject(dc, previousBitmap);
        SelectObject(dc, previousFont);
        DeleteObject(bitmap);
        DeleteObject(font);
        DeleteDC(dc);
        // Bound transient timer/NPC label textures. Evict the least recently drawn entry.
        if (m_data->texts.size() >= 128)
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
    Quad(texture.texture, x, baselineY - fontSize, (float)texture.width, (float)texture.height);
}
