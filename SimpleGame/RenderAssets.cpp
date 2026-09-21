#include "stdafx.h"
#include "DrawCallCounter.h"
#include "Dependencies/glew.h"
#include <windows.h>
#include "RenderAssets.h"
#include "AssetCache.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#pragma comment(lib, "gdi32.lib")

namespace
{
GLuint Upload(int width, int height, const std::vector<unsigned char> &pixels, bool repeat = false)
{
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
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    renderdebug::BeginPrimitive(GL_QUADS);
    glTexCoord2f(u0, v0);
    glVertex2f(x, y);
    glTexCoord2f(u1, v0);
    glVertex2f(x + width, y);
    glTexCoord2f(u1, v1);
    glVertex2f(x + width, y + height);
    glTexCoord2f(u0, v1);
    glVertex2f(x, y + height);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

GLuint CreateMaterial(int type)
{
    constexpr int size = 64;
    std::vector<unsigned char> pixels;
    const std::string cacheName = "material-" + std::to_string(type) + ".rgba";
    if (assetcache::Load(cacheName, 1, size * size * 4, pixels))
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
            if (type == 0 && (x < 2 || y < 2))
            {
                brightness = 115; // Concrete panel joints.
            }
            if (type == 1)
            {
                brightness = 175 + grain * 2; // Granular asphalt.
            }
            if (type == 2) // Brushed alloy, panel seams and corner rivets.
            {
                brightness = 210 + grain / 3 + (y % 3) * 5;
                if (x < 2 || y < 2)
                {
                    brightness = 90;
                }
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
    assetcache::Save(cacheName, 1, pixels.data(), pixels.size());
    return Upload(size, size, pixels, true);
}

GLuint CreateCharacterAtlas()
{
    constexpr int frameWidth = 48, frameHeight = 64, columns = 8, rows = 4;
    constexpr int width = frameWidth * columns, height = frameHeight * rows;
    std::vector<unsigned char> pixels;
    if (assetcache::Load("characters.rgba", 1, width * height * 4, pixels))
    {
        return Upload(width, height, pixels);
    }
    pixels.resize(width * height * 4, 0);
    for (int direction = 0; direction < rows; ++direction)
    {
        for (int frame = 0; frame < columns; ++frame)
        {
            const int step = frame == 0 ? 0 : int(std::sin(frame * 6.2831853f / 8) * 4);
            const int bob = frame == 0 ? 0 : std::abs(step) / 2;
            auto rectangle = [&](int x, int y, int w, int h, int r, int g, int b) {
                for (int py = y; py < y + h; ++py)
                {
                    for (int px = x; px < x + w; ++px)
                    {
                        if (px < 0 || px >= frameWidth || py < 0 || py >= frameHeight)
                        {
                            continue;
                        }
                        size_t i = ((direction * frameHeight + py) * width + frame * frameWidth + px) * 4;
                        pixels[i] = (unsigned char)r;
                        pixels[i + 1] = (unsigned char)g;
                        pixels[i + 2] = (unsigned char)b;
                        pixels[i + 3] = 255;
                    }
                }
            };
            // Boots, separated legs, articulated arms, jacket panels, head and visor.
            rectangle(15, 43, 7, 13 + step, 65, 77, 95);
            rectangle(26, 43, 7, 13 - step, 48, 60, 79);
            rectangle(13, 55 + step, 10, 4, 35, 45, 59);
            rectangle(26, 55 - step, 10, 4, 35, 45, 59);
            rectangle(9, 28 - bob - step / 2, 6, 17, 170, 180, 188);
            rectangle(34, 28 - bob + step / 2, 6, 17, 145, 157, 170);
            rectangle(9, 44 - bob - step / 2, 6, 4, 195, 175, 159);
            rectangle(34, 44 - bob + step / 2, 6, 4, 195, 175, 159);
            rectangle(15, 25 - bob, 19, 22, 225, 235, 240);
            rectangle(16, 27 - bob, 7, 14, 185, 199, 211);
            rectangle(25, 27 - bob, 8, 14, 151, 171, 189);
            rectangle(23, 26 - bob, 2, 20, 60, 80, 97);
            rectangle(16, 43 - bob, 18, 4, 55, 68, 84);
            rectangle(17, 34 - bob, 4, 5, 70, 98, 113);
            rectangle(27, 30 - bob, 4, 2, 110, 255, 255);
            rectangle(18, 12 - bob, 13, 12, 207, 181, 162);
            rectangle(17, 9 - bob, 15, 6, 44, 55, 72);
            rectangle(18, 23 - bob, 12, 4, 79, 95, 109);
            if (direction != 3)
            {
                int visorX = direction == 1 ? 16 : direction == 2 ? 24 : 18;
                rectangle(visorX, 16 - bob, 10, 4, 61, 200, 223);
                rectangle(visorX + 1, 16 - bob, 7, 1, 210, 255, 255);
            }
            else
            {
                rectangle(18, 15 - bob, 13, 8, 44, 55, 72);
            }
        }
    }
    assetcache::Save("characters.rgba", 1, pixels.data(), pixels.size());
    return Upload(width, height, pixels);
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
    GLuint characters = 0;
    std::map<std::pair<int, std::string>, TextTexture> texts;
    unsigned long long clock = 0;
};

RenderAssets::RenderAssets() : m_data(new Data())
{
    for (int i = 0; i < 4; ++i)
    {
        m_data->materials[i] = CreateMaterial(i);
    }
    m_data->characters = CreateCharacterAtlas();
}
RenderAssets::~RenderAssets()
{
    glDeleteTextures(4, m_data->materials);
    glDeleteTextures(1, &m_data->characters);
    for (const auto &entry : m_data->texts)
    {
        glDeleteTextures(1, &entry.second.texture);
    }
}
void RenderAssets::DrawMaterial(const RenderPoint (&points)[4], SurfaceMaterial material)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_data->materials[static_cast<int>(material)]);
    renderdebug::BeginPrimitive(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(points[0].x, points[0].y);
    glTexCoord2f(1, 0);
    glVertex2f(points[1].x, points[1].y);
    glTexCoord2f(1, 1);
    glVertex2f(points[2].x, points[2].y);
    glTexCoord2f(0, 1);
    glVertex2f(points[3].x, points[3].y);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}
void RenderAssets::DrawCharacter(float x, float footY, int frame, int direction)
{
    frame = (std::max)(0, (std::min)(7, frame));
    direction = (std::max)(0, (std::min)(3, direction));
    Quad(m_data->characters, x - 20, footY - 50, 40, 53, frame / 8.f, direction / 4.f, (frame + 1) / 8.f,
         (direction + 1) / 4.f);
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
            glDeleteTextures(1, &oldest->second.texture);
            m_data->texts.erase(oldest);
        }
        found = m_data->texts.emplace(key, Data::TextTexture{texture, width, height, 0}).first;
    }
    found->second.lastUse = ++m_data->clock;
    const auto &texture = found->second;
    Quad(texture.texture, x, baselineY - fontSize, (float)texture.width, (float)texture.height);
}
