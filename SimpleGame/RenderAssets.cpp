#include "stdafx.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include "Dependencies/glew/include/GL/glew.h"
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
#include <fstream>
#include <set>
#include <cstdlib>
#include <bcrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace
{
void AssetRequire(bool condition, const std::string &message)
{
    if (!condition)
    {
        throw std::runtime_error("UI delivery invalid: " + message);
    }
}
std::vector<unsigned char> ReadAsset(const std::filesystem::path &path, size_t limit)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    AssetRequire(bool(file) && file.tellg() > 0 && file.tellg() <= std::streamoff(limit), path.u8string());
    std::vector<unsigned char> bytes(size_t(file.tellg()));
    file.seekg(0);
    AssetRequire(bool(file.read(reinterpret_cast<char *>(bytes.data()), bytes.size())), path.u8string());
    return bytes;
}
class AssetHash
{
  public:
    AssetHash()
    {
        AssetRequire(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
                     "SHA-256 provider");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
            AssetRequire(false, "SHA-256 initialization");
        }
    }
    ~AssetHash()
    {
        if (hash)
        {
            BCryptDestroyHash(hash);
        }
        if (algorithm)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    }
    AssetHash(const AssetHash &) = delete;
    AssetHash &operator=(const AssetHash &) = delete;
    void Add(const unsigned char *bytes, size_t count)
    {
        AssetRequire(count <= 64 * 1024 * 1024 &&
                         BCryptHashData(hash, const_cast<PUCHAR>(bytes), ULONG(count), 0) >= 0,
                     "SHA-256 input");
    }
    std::string Finish()
    {
        unsigned char digest[32];
        AssetRequire(BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0, "SHA-256 result");
        std::string result;
        for (auto byte : digest)
        {
            result += "0123456789abcdef"[byte >> 4];
            result += "0123456789abcdef"[byte & 15];
        }
        return result;
    }

  private:
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
};
std::string Hash(const std::vector<unsigned char> &bytes)
{
    AssetHash hash;
    hash.Add(bytes.data(), bytes.size());
    return hash.Finish();
}
bool Digest(const std::string &text)
{
    return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == std::string::npos;
}

// Bounded metadata reader. Duplicate keys, trailing data and malformed scalars fail before GL upload.
class AssetJson
{
  public:
    enum class Type
    {
        Null,
        Number,
        String,
        Boolean,
        Object,
        Array
    } type = Type::Null;
    std::string text;
    double number = 0;
    bool boolean = false;
    std::map<std::string, AssetJson> object;
    std::vector<AssetJson> array;
    const AssetJson &At(const std::string &key) const
    {
        auto it = object.find(key);
        AssetRequire(type == Type::Object && it != object.end(), "missing key " + key);
        return it->second;
    }
    const std::string &Text() const
    {
        AssetRequire(type == Type::String, "expected string");
        return text;
    }
    int Integer() const
    {
        AssetRequire(type == Type::Number && std::isfinite(number) && number == std::floor(number) &&
                         number >= -10000000 && number <= 10000000,
                     "expected bounded integer");
        return int(number);
    }
    static AssetJson Load(const std::filesystem::path &path)
    {
        auto bytes = ReadAsset(path, 1024 * 1024);
        std::string input(bytes.begin(), bytes.end());
        size_t offset = 0;
        auto value = Parse(input, offset, 0);
        Space(input, offset);
        AssetRequire(offset == input.size(), "trailing JSON data");
        return value;
    }

  private:
    static void Space(const std::string &s, size_t &p)
    {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
        {
            ++p;
        }
    }
    static std::string String(const std::string &s, size_t &p)
    {
        AssetRequire(p < s.size() && s[p++] == '"', "JSON string");
        std::string value;
        while (p < s.size())
        {
            auto c = static_cast<unsigned char>(s[p++]);
            if (c == '"')
            {
                return value;
            }
            AssetRequire(c >= 32 && value.size() < 8192, "JSON string bounds");
            if (c == '\\')
            {
                AssetRequire(p < s.size(), "JSON escape");
                c = s[p++];
                if (c == 'n')
                {
                    c = '\n';
                }
                else if (c == 'r')
                {
                    c = '\r';
                }
                else if (c == 't')
                {
                    c = '\t';
                }
                else if (c == 'b')
                {
                    c = '\b';
                }
                else if (c == 'f')
                {
                    c = '\f';
                }
                else
                {
                    AssetRequire(c == '"' || c == '\\' || c == '/', "unsupported metadata escape");
                }
            }
            value += char(c);
        }
        AssetRequire(false, "unterminated JSON string");
        return {};
    }
    static AssetJson Parse(const std::string &s, size_t &p, unsigned depth)
    {
        Space(s, p);
        AssetRequire(depth < 16 && p < s.size(), "JSON nesting/input bounds");
        AssetJson v;
        const char first = s[p];
        if (first == '"')
        {
            v.type = Type::String;
            v.text = String(s, p);
            return v;
        }
        if (first == '{' || first == '[')
        {
            v.type = first == '{' ? Type::Object : Type::Array;
            const char close = first == '{' ? '}' : ']';
            ++p;
            Space(s, p);
            if (p < s.size() && s[p] == close)
            {
                ++p;
                return v;
            }
            for (size_t entries = 0;; ++entries)
            {
                AssetRequire(entries < 4096, "JSON collection bounds");
                if (first == '{')
                {
                    Space(s, p);
                    auto key = String(s, p);
                    Space(s, p);
                    AssetRequire(p < s.size() && s[p++] == ':', "JSON object separator");
                    AssetRequire(v.object.emplace(key, Parse(s, p, depth + 1)).second,
                                 "duplicate JSON key " + key);
                }
                else
                {
                    v.array.push_back(Parse(s, p, depth + 1));
                }
                Space(s, p);
                AssetRequire(p < s.size(), "unterminated JSON collection");
                if (s[p] == close)
                {
                    ++p;
                    return v;
                }
                AssetRequire(s[p++] == ',', "JSON collection separator");
            }
        }
        if (s.compare(p, 4, "true") == 0)
        {
            p += 4;
            v.type = Type::Boolean;
            v.boolean = true;
            return v;
        }
        if (s.compare(p, 5, "false") == 0)
        {
            p += 5;
            v.type = Type::Boolean;
            return v;
        }
        if (s.compare(p, 4, "null") == 0)
        {
            p += 4;
            return v;
        }
        const size_t start = p;
        if (s[p] == '-')
        {
            ++p;
        }
        AssetRequire(p < s.size() && s[p] >= '0' && s[p] <= '9', "JSON number");
        if (s[p] == '0')
        {
            ++p;
        }
        else
        {
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
            {
                ++p;
            }
        }
        if (p < s.size() && s[p] == '.')
        {
            const auto digits = ++p;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
            {
                ++p;
            }
            AssetRequire(p > digits, "JSON fraction");
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
        {
            ++p;
            if (p < s.size() && (s[p] == '-' || s[p] == '+'))
            {
                ++p;
            }
            const auto digits = p;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
            {
                ++p;
            }
            AssetRequire(p > digits, "JSON exponent");
        }
        v.type = Type::Number;
        v.number = std::strtod(s.substr(start, p - start).c_str(), nullptr);
        AssetRequire(std::isfinite(v.number), "JSON finite number");
        return v;
    }
};

std::vector<unsigned char> DecodePng(IWICImagingFactory *factory, const std::filesystem::path &path,
                                     int width, int height)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT w = 0, h = 0;
    AssetRequire(
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) &&
            w == unsigned(width) && h == unsigned(height) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)),
        "PNG size/format " + path.u8string());
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    AssetRequire(SUCCEEDED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data())),
                 "PNG decoding " + path.u8string());
    return pixels;
}
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

UiAtlasCatalog UiAtlasCatalog::Load(const std::filesystem::path &exportRoot)
{
    const auto metadata = AssetJson::Load(exportRoot / "Atlases/ui_complete.json");
    AssetRequire(metadata.At("schema").Text() == "erma.ui.atlas" && metadata.At("version").Integer() == 1,
                 "atlas schema/version");
    const auto &format = metadata.At("format");
    const auto &sampling = metadata.At("sampling");
    AssetRequire(format.At("channels").Text() == "RGBA8" && format.At("alpha").Text() == "straight" &&
                     format.At("color_space").Text() == "sRGB" && format.At("origin").Text() == "top-left" &&
                     format.At("row_direction").Text() == "down" &&
                     sampling.At("filter").Text() == "linear" &&
                     sampling.At("wrap").Text() == "clamp_to_edge" &&
                     sampling.At("uv").Text() == "pixel centers; .5 inset" &&
                     sampling.At("mipmaps").type == AssetJson::Type::Boolean &&
                     !sampling.At("mipmaps").boolean && metadata.At("nine_slice_inset").Integer() == 12,
                 "atlas format/sampling");
    UiAtlasCatalog result;
    const auto &atlas = metadata.At("atlas");
    result.width = atlas.At("width").Integer();
    result.height = atlas.At("height").Integer();
    const auto filename = atlas.At("file").Text();
    AssetRequire(filename == "ui_complete.png" && result.width > 0 && result.width <= 8192 &&
                     result.height > 0 && result.height <= 8192 &&
                     size_t(result.width) * result.height <= 16 * 1024 * 1024,
                 "atlas dimensions/name");
    result.image = exportRoot / "Atlases" / filename;
    AssetRequire(Digest(atlas.At("sha256").Text()) &&
                     Hash(ReadAsset(result.image, 64 * 1024 * 1024)) == atlas.At("sha256").Text(),
                 "atlas PNG hash");
    const int padding = metadata.At("padding").Integer();
    const auto &sprites = metadata.At("sprites");
    AssetRequire(padding >= 4 && padding <= 16 && sprites.type == AssetJson::Type::Object &&
                     sprites.object.size() >= 127 && sprites.object.size() <= 512 &&
                     int(sprites.object.size()) == metadata.At("source_count").Integer() &&
                     metadata.At("legacy_count").Integer() == 127,
                 "atlas inventory/padding");
    std::set<std::string> sourceIds;
    for (const auto *group : {"Icons", "HUDIcons", "Components"})
    {
        for (const auto &entry : std::filesystem::directory_iterator(exportRoot / group))
        {
            if (entry.is_regular_file() && entry.path().extension() == L".png")
            {
                sourceIds.insert(std::string(group) + "/" + entry.path().stem().u8string());
            }
        }
    }
    AssetRequire(sourceIds.size() == sprites.object.size(), "missing/outdated atlas source inventory");
    for (const auto &entry : sprites.object)
    {
        const auto &v = entry.second;
        Sprite sprite{v.At("x").Integer(), v.At("y").Integer(), v.At("width").Integer(),
                      v.At("height").Integer(), v.At("rgba_sha256").Text()};
        AssetRequire(
            sourceIds.count(entry.first) && v.At("source").Text() == "Export/" + entry.first + ".png" &&
                Digest(v.At("source_sha256").Text()) && Digest(sprite.rgbaHash) && sprite.x >= padding &&
                sprite.y >= padding && sprite.width > 0 && sprite.height > 0 &&
                sprite.width <= result.width - padding && sprite.height <= result.height - padding &&
                sprite.x <= result.width - padding - sprite.width &&
                sprite.y <= result.height - padding - sprite.height,
            "atlas rectangle " + entry.first);
        for (const auto &other : result.sprites)
        {
            const auto &b = other.second;
            AssetRequire(sprite.x + sprite.width + padding <= b.x || b.x + b.width + padding <= sprite.x ||
                             sprite.y + sprite.height + padding <= b.y ||
                             b.y + b.height + padding <= sprite.y,
                         "overlapping atlas sprites");
        }
        result.sprites.emplace(entry.first, std::move(sprite));
    }
    return result;
}

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
    std::filesystem::path exportRoot;
    std::map<std::string, GLuint> backdrop;
    AssetJson backdropMetadata;
    GLuint atlas = 0;
    std::map<std::tuple<int, bool, std::string>, TextTexture> texts;
    unsigned long long clock = 0;
    ~Data()
    {
        glDeleteTextures(1, &atlas);
        glDeleteTextures(1, &uiAtlas);
        for (const auto &entry : backdrop)
        {
            glDeleteTextures(1, &entry.second);
        }
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
    m_data->exportRoot = uiRoot;
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
        const auto catalog = UiAtlasCatalog::Load(uiRoot);
        GLint maximumTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
        AssetRequire(catalog.width <= maximumTextureSize && catalog.height <= maximumTextureSize,
                     "atlas exceeds graphics device texture limit");
        auto pixels = DecodePng(factory.Get(), catalog.image, catalog.width, catalog.height);
        m_data->uiWidth = catalog.width;
        m_data->uiHeight = catalog.height;
        for (const auto &entry : catalog.sprites)
        {
            const auto &s = entry.second;
            AssetHash hash;
            for (int row = 0; row < s.height; ++row)
            {
                hash.Add(pixels.data() + (size_t(s.y + row) * catalog.width + s.x) * 4, size_t(s.width) * 4);
            }
            AssetRequire(hash.Finish() == s.rgbaHash, "atlas sprite pixels " + entry.first);
            m_data->sprites.emplace(entry.first, Data::Sprite{s.x, s.y, s.width, s.height});
        }
        m_data->uiAtlas = Upload(m_data->uiWidth, m_data->uiHeight, pixels);
        m_data->backdropMetadata = AssetJson::Load(uiRoot / "Backdrop/backdrop_manifest.json");
        AssetRequire(m_data->backdropMetadata.At("schema").Text() == "erma.backdrop.layers" &&
                         m_data->backdropMetadata.At("version").Integer() == 1,
                     "backdrop schema/version");
        const auto &layers = m_data->backdropMetadata.At("layers");
        AssetRequire(layers.type == AssetJson::Type::Array && layers.array.size() == 10,
                     "backdrop inventory");
        std::set<std::tuple<std::string, std::string, int>> layerKeys;
        for (const auto &layer : layers.array)
        {
            const auto name = layer.At("name").Text(), variant = layer.At("variant").Text();
            const auto file = layer.At("file").Text();
            const int width = layer.At("width").Integer();
            AssetRequire(
                (name == "stars" || name == "gate_rings" || name == "rail" || name == "distant_skyline") &&
                    (variant == "smooth" || (name == "distant_skyline" && variant == "_cartoon")) &&
                    (width == 1280 || width == 2560) && layer.At("height").Integer() == width * 800 / 1280 &&
                    layer.At("alpha").Text() == "straight" &&
                    layerKeys.emplace(name, variant, width).second &&
                    fs::path(file).filename().u8string() == file && fs::path(file).extension() == L".png",
                "backdrop layer contract");
            AssetRequire(Digest(layer.At("sha256").Text()) &&
                             Hash(ReadAsset(uiRoot / "Backdrop" / file, 64 * 1024 * 1024)) ==
                                 layer.At("sha256").Text(),
                         "backdrop PNG hash " + file);
        }
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
bool RenderAssets::DrawBackdropLayer(const std::string &name, bool cartoon, int renderWidth)
{
    const std::string variant = name == "distant_skyline" && cartoon ? "_cartoon" : "smooth";
    // Use the supplied 2x layer only when the world render target can benefit from it.
    const int width = renderWidth > 1280 ? 2560 : 1280;
    const std::string key = name + variant + std::to_string(width);
    auto found = m_data->backdrop.find(key);
    if (found == m_data->backdrop.end())
    {
        const auto &layers = m_data->backdropMetadata.At("layers");
        AssetRequire(layers.type == AssetJson::Type::Array && layers.array.size() == 10,
                     "backdrop inventory");
        const AssetJson *selected = nullptr;
        for (const auto &layer : layers.array)
        {
            if (layer.At("name").Text() == name && layer.At("variant").Text() == variant &&
                layer.At("width").Integer() == width)
            {
                AssetRequire(!selected, "duplicate backdrop layer");
                selected = &layer;
            }
        }
        AssetRequire(selected != nullptr, "missing backdrop layer " + name);
        const auto file = selected->At("file").Text();
        AssetRequire(std::filesystem::path(file).filename().u8string() == file &&
                         std::filesystem::path(file).extension() == L".png" &&
                         selected->At("height").Integer() == width * 800 / 1280 &&
                         selected->At("alpha").Text() == "straight",
                     "backdrop dimensions/path");
        const auto path = m_data->exportRoot / "Backdrop" / file;
        AssetRequire(Digest(selected->At("sha256").Text()) &&
                         Hash(ReadAsset(path, 64 * 1024 * 1024)) == selected->At("sha256").Text(),
                     "backdrop hash " + file);
        GLint maximum = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        AssetRequire(width <= maximum, "backdrop graphics device texture limit");
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // COM must also be released when decoding throws.
        struct Apartment
        {
            HRESULT result;
            ~Apartment()
            {
                if (SUCCEEDED(result))
                {
                    CoUninitialize();
                }
            }
        } cleanup{apartment};
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        AssetRequire(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(factory.GetAddressOf()))),
                     "backdrop PNG decoder");
        auto pixels = DecodePng(factory.Get(), path, width, width * 800 / 1280);
        found = m_data->backdrop.emplace(key, Upload(width, width * 800 / 1280, pixels)).first;
    }
    renderqueue::Depth(false);
    renderqueue::Color(1, 1, 1, 1);
    Quad(found->second, 0, 0, 1280, 800);
    return true;
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
