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
#include <unordered_map>
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
    struct TextKey
    {
        int fontSize = 0;
        bool outlined = false;
        std::string text;
        bool operator==(const TextKey &other) const
        {
            return fontSize == other.fontSize && outlined == other.outlined && text == other.text;
        }
    };
    struct TextKeyHash
    {
        size_t operator()(const TextKey &k) const noexcept
        {
            size_t h = std::hash<std::string>{}(k.text);
            h ^= size_t(k.fontSize) * 2654435761u + (k.outlined ? 0x9e3779b9u : 0u) + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct TextEntry
    {
        GLuint texture = 0;
        bool inAtlas = false;
        int width = 0, height = 0;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        unsigned long long lastUse = 0;
    };
    struct Sprite
    {
        int x, y, width, height;
    };
    static constexpr int kTextAtlasWidth = 4096;
    static constexpr int kTextAtlasHeight = 2048;
    GLuint uiAtlas = 0;
    GLuint pbrBaseArray = 0, pbrNormalArray = 0, pbrOrmArray = 0;
    GLuint pbrDetailBaseArray = 0, pbrDetailNormalArray = 0, pbrDetailOrmArray = 0;
    GLuint damageMaskTex = 0;
    GLuint effectMasksTex = 0, waterFlowTex = 0;
    PbrTextureBudget pbrBudget;
    int uiWidth = 2048, uiHeight = 0;
    std::unordered_map<std::string, Sprite> sprites;
    std::filesystem::path exportRoot;
    std::unordered_map<std::string, GLuint> backdrop;
    AssetJson backdropMetadata;
    GLuint atlas = 0;
    GLuint textAtlas = 0;
    int textAtlasX = 2, textAtlasY = 2, textShelfHeight = 0;
    float textRasterScale = 1.f;
    HDC textDc = nullptr;
    std::unordered_map<int, HFONT> textFonts;
    std::unordered_map<TextKey, TextEntry, TextKeyHash> texts;
    unsigned long long clock = 0;
    ~Data()
    {
        glDeleteTextures(1, &atlas);
        glDeleteTextures(1, &uiAtlas);
        if (pbrBaseArray)
        {
            glDeleteTextures(1, &pbrBaseArray);
        }
        if (pbrNormalArray)
        {
            glDeleteTextures(1, &pbrNormalArray);
        }
        if (pbrOrmArray)
        {
            glDeleteTextures(1, &pbrOrmArray);
        }
        if (pbrDetailBaseArray)
        {
            glDeleteTextures(1, &pbrDetailBaseArray);
        }
        if (pbrDetailNormalArray)
        {
            glDeleteTextures(1, &pbrDetailNormalArray);
        }
        if (pbrDetailOrmArray)
        {
            glDeleteTextures(1, &pbrDetailOrmArray);
        }
        if (damageMaskTex)
        {
            glDeleteTextures(1, &damageMaskTex);
        }
        if (effectMasksTex)
        {
            glDeleteTextures(1, &effectMasksTex);
        }
        if (waterFlowTex)
        {
            glDeleteTextures(1, &waterFlowTex);
        }
        if (textAtlas)
        {
            glDeleteTextures(1, &textAtlas);
        }
        for (const auto &entry : backdrop)
        {
            glDeleteTextures(1, &entry.second);
        }
        for (const auto &entry : texts)
        {
            if (!entry.second.inAtlas && entry.second.texture)
            {
                glDeleteTextures(1, &entry.second.texture);
            }
        }
        for (const auto &entry : textFonts)
        {
            DeleteObject(entry.second);
        }
        if (textDc)
        {
            DeleteDC(textDc);
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
        // R4 / GFX-01 / GFX-08: Load immutable shared PBR texture arrays (1024^2 pilot x 4 + 512^2 skin/world x 9)
        // and vehicle damage mask (256^2) under an explicit VRAM residency budget.
        // Locate Resource/Export/Textures independently of staged Resource/UI/Export so build-tree runs find PBR assets.
        fs::path resourceRoot = uiRoot.parent_path().parent_path();
        {
            auto searchDir = fs::path(module).parent_path();
            for (int level = 0; level < 7 && !searchDir.empty(); ++level)
            {
                const auto candidateRoot = searchDir / L"Resource";
                std::error_code ec;
                if (fs::is_directory(candidateRoot / L"Export" / L"Textures" / L"Pilot", ec))
                {
                    resourceRoot = candidateRoot;
                    break;
                }
                const auto parent = searchDir.parent_path();
                if (parent == searchDir)
                {
                    break;
                }
                searchDir = parent;
            }
        }
        const fs::path texturesDir = resourceRoot / "Export" / "Textures";
        const fs::path pilotDir = texturesDir / "Pilot";
        const fs::path materialPilotDir = resourceRoot / "Export" / "MaterialPilot" / "Textures";
        const fs::path damagePath = texturesDir / "SharedDamage" / "Vehicle_DamageMask.png";
        float anisotropy = 1.f;
        if (GLEW_EXT_texture_filter_anisotropic)
        {
            GLfloat maxAniso = 1.f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            anisotropy = std::clamp(float(maxAniso), 1.f, 8.f);
        }
        m_data->pbrBudget.anisotropy = anisotropy;
        auto mipLevels = [](int size) {
            int levels = 1;
            for (int s = size; s > 1; s >>= 1)
            {
                ++levels;
            }
            return levels;
        };
        auto arrayMipBytes = [&](int size, int layers) {
            size_t total = 0;
            const int levels = mipLevels(size);
            for (int level = 0, w = size; level < levels; ++level, w = (std::max)(1, w >> 1))
            {
                total += size_t(w) * size_t(w) * 4u * size_t(layers);
            }
            return total;
        };
        auto uploadArray = [&](const std::vector<fs::path> &prefixes, const char *suffix, int size, bool srgb,
                               bool &usedImmutable, size_t &bytesOut) -> GLuint {
            const int layers = int(prefixes.size());
            if (layers <= 0)
            {
                return 0;
            }
            const size_t estBytes = arrayMipBytes(size, layers);
            if (m_data->pbrBudget.residentBytes + estBytes > m_data->pbrBudget.budgetBytes)
            {
                return 0;
            }
            std::error_code ec;
            for (int layer = 0; layer < layers; ++layer)
            {
                fs::path p = prefixes[size_t(layer)];
                p += suffix;
                if (!fs::is_regular_file(p, ec))
                {
                    return 0;
                }
            }
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
            if (anisotropy > 1.f)
            {
                glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
            }
            const GLenum internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            const int levels = mipLevels(size);
            if (glTexStorage3D != nullptr)
            {
                glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, internalFormat, size, size, layers);
                usedImmutable = true;
            }
            else
            {
                glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, size, size, layers, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, nullptr);
            }
            for (int layer = 0; layer < layers; ++layer)
            {
                fs::path p = prefixes[size_t(layer)];
                p += suffix;
                auto px = DecodePng(factory.Get(), p, size, size);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                px.data());
            }
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            bytesOut = estBytes;
            profiling::Count(profiling::Counter::TextureUploadBytes, estBytes);
            return tex;
        };
        try
        {
            if (maximumTextureSize >= 1024 && GLEW_VERSION_3_0)
            {
                const std::vector<fs::path> pilotPrefixes = {
                    pilotDir / "Pilot_CastPaving", pilotDir / "Pilot_TechnicalWeave",
                    pilotDir / "Pilot_BrushedSteel", pilotDir / "Pilot_PaintedAlloy"};
                bool imm0 = false, imm1 = false, imm2 = false;
                size_t b0 = 0, b1 = 0, b2 = 0;
                GLuint baseArr = uploadArray(pilotPrefixes, "_BaseColor.png", 1024, false, imm0, b0);
                GLuint nrmArr = baseArr ? uploadArray(pilotPrefixes, "_Normal.png", 1024, false, imm1, b1) : 0;
                GLuint ormArr = nrmArr ? uploadArray(pilotPrefixes, "_ORM.png", 1024, false, imm2, b2) : 0;
                if (baseArr && nrmArr && ormArr)
                {
                    m_data->pbrBaseArray = baseArr;
                    m_data->pbrNormalArray = nrmArr;
                    m_data->pbrOrmArray = ormArr;
                    m_data->pbrBudget.pilotLayers = unsigned(pilotPrefixes.size());
                    m_data->pbrBudget.residentBytes += b0 + b1 + b2;
                    m_data->pbrBudget.immutableStorage = imm0 && imm1 && imm2;
                }
                else
                {
                    if (baseArr) glDeleteTextures(1, &baseArr);
                    if (nrmArr) glDeleteTextures(1, &nrmArr);
                    if (ormArr) glDeleteTextures(1, &ormArr);
                }
            }
            if (maximumTextureSize >= 512 && GLEW_VERSION_3_0)
            {
                // Layer 0: SkinMicrograin; 1..8: world/building/road/mine/ruin/terrain PBR sets (all 512^2).
                const std::vector<fs::path> detailPrefixes = {
                    materialPilotDir / "SkinMicrograin", texturesDir / "Habitat_Concrete",
                    texturesDir / "Habitat_Panels",      texturesDir / "Road_Basalt",
                    texturesDir / "Rock_Mine",           texturesDir / "Ruin_Obsidian",
                    texturesDir / "Terrain_AlienGrass",  texturesDir / "Terrain_Dunes",
                    texturesDir / "Terrain_Regolith"};
                bool imm0 = false, imm1 = false, imm2 = false;
                size_t b0 = 0, b1 = 0, b2 = 0;
                GLuint baseArr = uploadArray(detailPrefixes, "_BaseColor.png", 512, false, imm0, b0);
                GLuint nrmArr = baseArr ? uploadArray(detailPrefixes, "_Normal.png", 512, false, imm1, b1) : 0;
                GLuint ormArr = nrmArr ? uploadArray(detailPrefixes, "_ORM.png", 512, false, imm2, b2) : 0;
                if (baseArr && nrmArr && ormArr)
                {
                    m_data->pbrDetailBaseArray = baseArr;
                    m_data->pbrDetailNormalArray = nrmArr;
                    m_data->pbrDetailOrmArray = ormArr;
                    m_data->pbrBudget.detailLayers = unsigned(detailPrefixes.size());
                    m_data->pbrBudget.residentBytes += b0 + b1 + b2;
                    m_data->pbrBudget.immutableStorage =
                        m_data->pbrBudget.immutableStorage && imm0 && imm1 && imm2;
                }
                else
                {
                    if (baseArr) glDeleteTextures(1, &baseArr);
                    if (nrmArr) glDeleteTextures(1, &nrmArr);
                    if (ormArr) glDeleteTextures(1, &ormArr);
                }
            }
            std::error_code ec;
            if (fs::is_regular_file(damagePath, ec))
            {
                auto px = DecodePng(factory.Get(), damagePath, 256, 256);
                GLuint tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                if (glTexStorage2D != nullptr)
                {
                    glTexStorage2D(GL_TEXTURE_2D, mipLevels(256), GL_RGBA8, 256, 256);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                }
                else
                {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                }
                glGenerateMipmap(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, 0);
                const size_t dmgBytes = arrayMipBytes(256, 1);
                m_data->pbrBudget.residentBytes += dmgBytes;
                profiling::Count(profiling::Counter::TextureUploadBytes, dmgBytes);
                m_data->damageMaskTex = tex;
            }
            auto loadRepeat2d = [&](const fs::path &texPath, int size) -> GLuint {
                std::error_code tec;
                if (!fs::is_regular_file(texPath, tec))
                {
                    return 0;
                }
                auto px = DecodePng(factory.Get(), texPath, size, size);
                GLuint tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                if (glTexStorage2D != nullptr)
                {
                    glTexStorage2D(GL_TEXTURE_2D, mipLevels(size), GL_RGBA8, size, size);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                }
                else
                {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                }
                glGenerateMipmap(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, 0);
                const size_t bytes = arrayMipBytes(size, 1);
                m_data->pbrBudget.residentBytes += bytes;
                profiling::Count(profiling::Counter::TextureUploadBytes, bytes);
                return tex;
            };
            m_data->effectMasksTex = loadRepeat2d(texturesDir / "SharedEffects" / "Frontier_Effects_Masks.png", 512);
            m_data->waterFlowTex = loadRepeat2d(texturesDir / "SharedEffects" / "Frontier_Water_Flow.png", 512);
        }
        catch (const std::exception &)
        {
            // Keep graceful vertex-color fallback if optional pilot/damage textures are unavailable.
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
void RenderAssets::SetTextRasterScale(float scale)
{
    const float clamped = (std::max)(1.f, (std::min)(3.f, std::round(scale * 2.f) * .5f));
    if (std::fabs(clamped - m_data->textRasterScale) < .01f)
    {
        return;
    }
    renderqueue::Flush();
    for (const auto &entry : m_data->texts)
    {
        if (!entry.second.inAtlas && entry.second.texture)
        {
            glDeleteTextures(1, &entry.second.texture);
        }
    }
    m_data->texts.clear();
    m_data->textAtlasX = 2;
    m_data->textAtlasY = 2;
    m_data->textShelfHeight = 0;
    m_data->textRasterScale = clamped;
}
void RenderAssets::DrawUtf8Text(float x, float baselineY, const std::string &utf8, int fontSize,
                                bool outlined)
{
    if (utf8.empty())
    {
        return;
    }
    const float rasterScale = m_data->textRasterScale;
    const int pixelFontSize = (std::max)(8, int(std::lround(fontSize * rasterScale)));
    const Data::TextKey key{fontSize, outlined, utf8};
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
        if (!m_data->textDc)
        {
            m_data->textDc = CreateCompatibleDC(nullptr);
        }
        HDC dc = m_data->textDc;
        if (!dc)
        {
            return;
        }
        HFONT &font = m_data->textFonts[pixelFontSize];
        if (!font)
        {
            font = CreateFontW(-pixelFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                               L"Malgun Gothic");
            if (!font)
            {
                return;
            }
        }
        HGDIOBJ previousFont = SelectObject(dc, font);
        SIZE extent = {};
        GetTextExtentPoint32W(dc, text.data(), count, &extent);
        const int outlineRadius = (std::max)(2, int(std::lround(2.f * rasterScale)));
        const int padding = outlined ? (outlineRadius + 1) : (std::max)(1, int(std::lround(rasterScale)));
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
            const int r2 = outlineRadius * outlineRadius + 1;
            auto mask = pixels;
            for (int yy = 0; yy < height; ++yy)
            {
                for (int xx = 0; xx < width; ++xx)
                {
                    int alpha = 0;
                    for (int oy = -outlineRadius; oy <= outlineRadius; ++oy)
                    {
                        for (int ox = -outlineRadius; ox <= outlineRadius; ++ox)
                        {
                            if (ox * ox + oy * oy <= r2 && xx + ox >= 0 && xx + ox < width && yy + oy >= 0 &&
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
        SelectObject(dc, previousBitmap);
        SelectObject(dc, previousFont);
        DeleteObject(bitmap);

        Data::TextEntry entry;
        entry.width = width;
        entry.height = height;
        const bool fitsAtlas = width + 4 <= Data::kTextAtlasWidth && height + 4 <= Data::kTextAtlasHeight;
        if (fitsAtlas)
        {
            if (!m_data->textAtlas)
            {
                glGenTextures(1, &m_data->textAtlas);
                glBindTexture(GL_TEXTURE_2D, m_data->textAtlas);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                std::vector<unsigned char> empty(size_t(Data::kTextAtlasWidth) * Data::kTextAtlasHeight * 4, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Data::kTextAtlasWidth, Data::kTextAtlasHeight, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, empty.data());
                glBindTexture(GL_TEXTURE_2D, 0);
                profiling::Count(profiling::Counter::TextureUploadBytes, empty.size());
            }
            if (m_data->textAtlasX + width + 2 > Data::kTextAtlasWidth)
            {
                m_data->textAtlasX = 2;
                m_data->textAtlasY += m_data->textShelfHeight + 2;
                m_data->textShelfHeight = 0;
            }
            if (m_data->textAtlasY + height + 2 > Data::kTextAtlasHeight)
            {
                // Flush queued quads referencing the old atlas layout, then reset the shelf.
                renderqueue::Flush();
                for (auto it = m_data->texts.begin(); it != m_data->texts.end();)
                {
                    if (it->second.inAtlas)
                    {
                        profiling::Count(profiling::Counter::TextCacheEvictions);
                        it = m_data->texts.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                m_data->textAtlasX = 2;
                m_data->textAtlasY = 2;
                m_data->textShelfHeight = 0;
            }
            const int ax = m_data->textAtlasX;
            const int ay = m_data->textAtlasY;
            m_data->textAtlasX += width + 2;
            m_data->textShelfHeight = (std::max)(m_data->textShelfHeight, height);
            {
                profiling::Scope uploadTimer(profiling::Timer::TextureUpload);
                glBindTexture(GL_TEXTURE_2D, m_data->textAtlas);
                glTexSubImage2D(GL_TEXTURE_2D, 0, ax, ay, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                                pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);
                profiling::Count(profiling::Counter::TextureUploadBytes, pixels.size());
            }
            entry.texture = m_data->textAtlas;
            entry.inAtlas = true;
            entry.u0 = float(ax) / float(Data::kTextAtlasWidth);
            entry.v0 = float(ay) / float(Data::kTextAtlasHeight);
            entry.u1 = float(ax + width) / float(Data::kTextAtlasWidth);
            entry.v1 = float(ay + height) / float(Data::kTextAtlasHeight);
        }
        else
        {
            if (m_data->texts.size() >= 512)
            {
                auto oldest = std::min_element(
                    m_data->texts.begin(), m_data->texts.end(),
                    [](const auto &a, const auto &b) { return a.second.lastUse < b.second.lastUse; });
                renderqueue::Flush();
                profiling::Count(profiling::Counter::TextCacheEvictions);
                if (!oldest->second.inAtlas && oldest->second.texture)
                {
                    glDeleteTextures(1, &oldest->second.texture);
                }
                m_data->texts.erase(oldest);
            }
            entry.texture = Upload(width, height, pixels);
            entry.inAtlas = false;
        }
        found = m_data->texts.emplace(key, entry).first;
    }
    else
    {
        profiling::Count(profiling::Counter::TextCacheHits);
    }
    found->second.lastUse = ++m_data->clock;
    const auto &entry = found->second;
    const float invScale = 1.f / rasterScale;
    Quad(entry.texture, x - (outlined ? 2.f : 0.f), baselineY - fontSize - (outlined ? 2.f : 0.f),
         float(entry.width) * invScale, float(entry.height) * invScale, entry.u0, entry.v0, entry.u1, entry.v1);
}

size_t RenderAssets::UiSpriteCount() const
{
    return m_data->sprites.size();
}
unsigned int RenderAssets::PbrBaseArray() const
{
    return m_data->pbrBaseArray;
}
unsigned int RenderAssets::PbrNormalArray() const
{
    return m_data->pbrNormalArray;
}
unsigned int RenderAssets::PbrOrmArray() const
{
    return m_data->pbrOrmArray;
}
unsigned int RenderAssets::PbrDetailBaseArray() const
{
    return m_data->pbrDetailBaseArray;
}
unsigned int RenderAssets::PbrDetailNormalArray() const
{
    return m_data->pbrDetailNormalArray;
}
unsigned int RenderAssets::PbrDetailOrmArray() const
{
    return m_data->pbrDetailOrmArray;
}
unsigned int RenderAssets::DamageMaskTexture() const
{
    return m_data->damageMaskTex;
}
unsigned int RenderAssets::EffectMasksTexture() const
{
    return m_data->effectMasksTex;
}
unsigned int RenderAssets::WaterFlowTexture() const
{
    return m_data->waterFlowTex;
}
bool RenderAssets::HasPbrTextures() const
{
    return m_data->pbrBaseArray != 0 && m_data->pbrNormalArray != 0 && m_data->pbrOrmArray != 0;
}
bool RenderAssets::HasPbrDetailTextures() const
{
    return m_data->pbrDetailBaseArray != 0 && m_data->pbrDetailNormalArray != 0 &&
           m_data->pbrDetailOrmArray != 0;
}
bool RenderAssets::HasDamageMask() const
{
    return m_data->damageMaskTex != 0;
}
bool RenderAssets::HasEffectTextures() const
{
    return m_data->effectMasksTex != 0 && m_data->waterFlowTex != 0;
}
const PbrTextureBudget &RenderAssets::PbrBudget() const
{
    return m_data->pbrBudget;
}
bool RenderAssets::DrawBackdropLayer(const std::string &name, bool cartoon, int renderWidth, float logicalWidth)
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
    Quad(found->second, 0, 0, logicalWidth > 0.f ? logicalWidth : 1280.f, 800.f);
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
