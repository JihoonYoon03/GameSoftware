#include "Window.h"
#include "Drawing.h"
#include "Hud.h"
#include "RenderAssets.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iterator>

namespace
{
void CheckAtlasContract()
{
    namespace fs = std::filesystem;
    const fs::path source = "Resource/UI/Export";
    const auto catalog = UiAtlasCatalog::Load(source);
    assert(catalog.width == 2048 && catalog.height == 2048 && catalog.sprites.size() >= 145);
    assert(catalog.sprites.count("Components/activity_page_dot") &&
           catalog.sprites.count("Icons/activity_track") &&
           catalog.sprites.count("HUDIcons/activity_submit"));
    struct Scratch
    {
        fs::path path;
        ~Scratch()
        {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } scratch{
        fs::temp_directory_path() /
        ("gse-ui-atlas-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    assert(fs::create_directory(scratch.path));
    fs::create_directory(scratch.path / "Atlases");
    fs::copy_file(catalog.image, scratch.path / "Atlases/ui_complete.png");
    for (const auto *group : {"Icons", "HUDIcons", "Components"})
    {
        fs::create_directory(scratch.path / group);
    }
    // Catalog validation needs only file identities; original PNGs are never decoded/repacked here.
    for (const auto &entry : catalog.sprites)
    {
        std::ofstream(scratch.path / (entry.first + ".png"));
    }
    std::ifstream input(source / "Atlases/ui_complete.json", std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto write = [&](const std::string &text) {
        std::ofstream out(scratch.path / "Atlases/ui_complete.json", std::ios::binary | std::ios::trunc);
        out << text;
    };
    auto reject = [&](const std::string &text) {
        write(text);
        bool rejected = false;
        try
        {
            UiAtlasCatalog::Load(scratch.path);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        assert(rejected);
    };
    auto replace = [&](const std::string &from, const std::string &to) {
        auto text = original;
        const auto at = text.find(from);
        assert(at != std::string::npos);
        text.replace(at, from.size(), to);
        return text;
    };
    write(original);
    assert(UiAtlasCatalog::Load(scratch.path).sprites.size() == catalog.sprites.size());
    reject(replace("\"version\": 1", "\"version\": 2"));
    reject(replace("\"version\": 1", "\"version\": 1, \"version\": 1"));
    reject(replace("\"x\": 4", "\"x\": 9999999"));
    reject(original.substr(0, original.size() / 2));
    fs::remove(scratch.path / (catalog.sprites.begin()->first + ".png"));
    reject(original);
}
} // namespace

int main()
{
    using namespace tutorial;
    CheckAtlasContract(); // CPU/metadata path runs before any GLFW/GL initialization.
    assert(platform::GameKey(GLFW_KEY_W, 0) == 'w');
    assert(platform::GameKey(GLFW_KEY_W, GLFW_MOD_SHIFT) == 'w');
    assert(platform::GameKey(GLFW_KEY_ENTER, 0) == 13);
    assert(platform::GameKey(GLFW_KEY_ESCAPE, 0) == 27);
    assert(platform::GameKey(GLFW_KEY_SLASH, GLFW_MOD_SHIFT) == '?');
    assert(platform::GameKey(GLFW_KEY_EQUAL, GLFW_MOD_SHIFT) == '+');
    assert(platform::GameKey(GLFW_KEY_LEFT_BRACKET, 0) == '[');
    assert(platform::GameKey(GLFW_KEY_F9, 0) == 0);
    assert(platform::GameKey(GLFW_KEY_LEFT_SHIFT, 0) == platform::SprintKey);
    assert(platform::GameKey(GLFW_KEY_RIGHT_SHIFT, GLFW_MOD_SHIFT) == platform::SprintKey);
    assert(platform::FramebufferCursor(640, 400, 1280, 800, 2560, 1600) == std::make_pair(1280, 800));
    assert(platform::FramebufferCursor(-1, 30, 1280, 800, 1920, 1200) == std::make_pair(-2, 45));
    const int textWidth = RenderAssets::MeasureUtf8(u8"한글 GLFW 123", 15);
    assert(textWidth > 20);
    assert(RenderAssets::MeasureUtf8(u8"한글 GLFW 123", 19) > textWidth);
    const auto before = platform::ElapsedMilliseconds();
    assert(platform::Initialize(u8"한글 GLFW 테스트", 640, 400, false));
    assert(platform::ElapsedMilliseconds() >= before);
    assert(glfwGetWindowAttrib(platform::Window(), GLFW_CONTEXT_VERSION_MAJOR) >= 2);
    assert(!glfwGetWindowAttrib(platform::Window(), GLFW_OPENGL_FORWARD_COMPAT));
    Game::Get().Reset();
    Hud::CloseMenu();
    platform::InstallCallbacks();
    auto key = glfwSetKeyCallback(platform::Window(), nullptr);
    auto focus = glfwSetWindowFocusCallback(platform::Window(), nullptr);
    auto entry = glfwSetCursorEnterCallback(platform::Window(), nullptr);
    auto mouse = glfwSetMouseButtonCallback(platform::Window(), nullptr);
    auto &state = Game::Get().GetState();
    key(platform::Window(), GLFW_KEY_W, 0, GLFW_PRESS, 0);
    assert(state.pressedKeys['w']);
    key(platform::Window(), GLFW_KEY_W, 0, GLFW_RELEASE, GLFW_MOD_SHIFT);
    assert(!state.pressedKeys['w']);
    key(platform::Window(), GLFW_KEY_LEFT_SHIFT, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    key(platform::Window(), GLFW_KEY_RIGHT_SHIFT, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    key(platform::Window(), GLFW_KEY_LEFT_SHIFT, 0, GLFW_RELEASE, GLFW_MOD_SHIFT);
    assert(state.pressedKeys[platform::SprintKey]);
    key(platform::Window(), GLFW_KEY_RIGHT_SHIFT, 0, GLFW_RELEASE, 0);
    assert(!state.pressedKeys[platform::SprintKey]);
    key(platform::Window(), GLFW_KEY_ESCAPE, 0, GLFW_REPEAT, 0);
    assert(!state.isPaused);
    key(platform::Window(), GLFW_KEY_W, 0, GLFW_PRESS, 0);
    state.mouseAiming = state.mouseFiring = true;
    state.playerMotion.mobility.RequestDodge();
    state.playerMotion.mobility.Update(.016f, {1, 0, 0}, 0, false, false, true, false);
    const float stamina = state.playerMotion.mobility.Stamina();
    focus(platform::Window(), GLFW_FALSE);
    assert(!state.playerMotion.mobility.Dodging() && state.playerMotion.mobility.Stamina() == stamina);
    assert(!state.pressedKeys['w'] && !state.mouseAiming && !state.mouseFiring);
    state.mouseAiming = state.mouseFiring = true;
    entry(platform::Window(), GLFW_FALSE);
    assert(!state.mouseAiming && !state.mouseFiring);
    state.mouseAiming = true;
    mouse(platform::Window(), GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    assert(!state.mouseAiming);
    state.isInsideHome = false;
    state.mapVisible = true;
    const auto zoomBefore = state.mapViews[size_t(MapRegion::City)].zoom;
    Game::Get().MouseWheel(1, 320, 200);
    assert(state.mapViews[size_t(MapRegion::City)].zoom > zoomBefore);
    const auto zoomAfter = state.mapViews[size_t(MapRegion::City)].zoom;
    state.isPaused = true;
    Game::Get().MouseWheel(-1, 320, 200);
    assert(state.mapViews[size_t(MapRegion::City)].zoom == zoomAfter);
    state.isPaused = false;
    state.mapVisible = false;
    assert(RenderAssets::MeasureUtf8(u8"한글 GLFW 123", 15) == textWidth);
    platform::RequestClose();
    assert(glfwWindowShouldClose(platform::Window()));
    // Exercise the close path with a current context for renderer destruction.
    GameCallbacks::Shutdown();
    platform::Shutdown();
    assert(platform::Window() == nullptr);
    std::cout << "GLFW input, focus, DPI, Korean text metrics and shutdown checks passed.\n";
}
