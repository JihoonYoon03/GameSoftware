#include "stdafx.h"
#include "Drawing.h"
#include "Civic.h"
#include "Hud.h"
#include "Level.h"
#include "Monitoring/Monitor.h"
#include "Overworld.h"
#include "Profiler.h"
#include "RenderQueue.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "Scene.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <windows.h>

// Hidden WGL surface: exercises the production mesh and FBO depth path without
// displaying a game window or changing the user's desktop.
namespace
{
using namespace tutorial;
using namespace models;
float Distance3D(Point a, Point b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
void TestModelsAndRig()
{
    const auto &library = ModelLibrary::Get();
    for (const auto &model : library.aircraft)
    {
        assert(model.resourceId && ModelFactory::ValidateModel(model));
        const auto &resource = ResourceAssets::Get(model.resourceId);
        assert(resource.boneCount == 17 && resource.Clips().size() == 12);
        assert(resource.sockets[0].x < 0 && resource.sockets[1].x < 0);
        assert(resource.sockets[0].y > 0 && resource.sockets[1].y < 0);
        assert(resource.sockets[5].x >= 0 && resource.sockets[5].x < resource.boneCount);
        assert(resource.sockets[5].y >= 0 && resource.sockets[5].y < resource.boneCount);
        auto pose = ModelRig::Pose(10, true);
        SkinMatrices parked, cruising;
        ResourceAssets::Sample(model.resourceId, nullptr, parked);
        ResourceAssets::Sample(model.resourceId, &pose, cruising);
        assert(parked != cruising);
    }
    for (const auto &model : library.scenery)
    {
        assert(model.resourceId && ModelFactory::ValidateModel(model));
    }
    assert(ModelLibrary::TrafficSignal(0).resourceId != ModelLibrary::TrafficSignal(2).resourceId);
    for (const auto &vehicle : library.vehicles)
    {
        assert(vehicle.body.resourceId && ModelFactory::ValidateModel(vehicle.body));
        assert(ResourceAssets::Get(vehicle.body.resourceId).boneCount == 16);
        assert(vehicle.exhausts[0].x < 0 && vehicle.headlights[0].x > 0);
        assert(vehicle.seat.z > 0);
    }
    for (const auto &model : library.characters)
    {
        assert(model.resourceId && ModelFactory::ValidateModel(model));
        const auto &resource = ResourceAssets::Get(model.resourceId);
        assert(resource.boneCount == 23 && resource.vertices.size() > 300);
        auto standing = ModelRig::Pose(0, false);
        auto moving = ModelRig::Pose(2, true);
        SkinMatrices first, second;
        ResourceAssets::Sample(model.resourceId, &standing, first);
        ResourceAssets::Sample(model.resourceId, &moving, second);
        assert(first != second);
        for (const auto &vertex : resource.vertices)
        {
            float sum = 0;
            for (size_t j = 0; j < 4; ++j)
            {
                assert(vertex.joints[j] >= 0 && vertex.joints[j] <= 31);
                sum += vertex.weights[j];
            }
            assert(std::fabs(sum - 1) < .0001f);
            const auto p = ResourceAssets::SkinPoint(vertex, first);
            assert(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        }
    }
    auto aim = ModelRig::Pose(0, false, true);
    auto muzzle = ModelRig::Muzzle(aim);
    assert(muzzle.x > .4f && muzzle.z > .8f);
    assert(std::fabs(muzzle.z * WorldScale::characterScale * 44 - WorldScale::aimHeightPixels) < .01f);
    // An unarmed strike must animate both body types and their attached hair, while walking
    // retains its pelvis/leg track. The previous armed-only gate silently sampled idle instead.
    for (Gender gender : {Gender::Male, Gender::Female})
    {
        for (bool walking : {false, true})
        {
            auto idle = ModelRig::Pose(2.1f, walking, false, 0, false, gender);
            auto strike = idle;
            strike.meleeSeconds = .325f;
            for (const Model *model : {&ModelLibrary::Character(CharacterType::Explorer, gender),
                                       &ModelLibrary::Hair(HairStyle::SidePart, false)})
            {
                SkinMatrices base, melee, early;
                ResourceAssets::Sample(model->resourceId, &idle, base);
                ResourceAssets::Sample(model->resourceId, &strike, melee);
                assert(base != melee && !strike.armed);
                assert(std::equal(base.begin() + 17 * 16, base.begin() + 23 * 16, melee.begin() + 17 * 16));
                strike.meleeSeconds = .64f;
                ResourceAssets::Sample(model->resourceId, &strike, early);
                assert(early != melee);
                strike.meleeSeconds = .325f;
            }
        }
    }
}
void TestHairAndBodyShape()
{
    const auto &library = ModelLibrary::Get();
    for (size_t i = 0; i < library.hairs.size(); ++i)
    {
        assert(library.hairs[i].resourceId && library.coveredHairs[i].resourceId);
        assert(ModelFactory::ValidateModel(library.hairs[i]));
    }
    for (size_t role = 0; role < library.characters.size(); ++role)
    {
        const auto &male = ResourceAssets::Get(library.characters[role].resourceId);
        const auto &female = ResourceAssets::Get(library.femaleCharacters[role].resourceId);
        assert(male.name != female.name && male.boneCount == female.boneCount);
        assert(male.vertices.size() != female.vertices.size() ||
               std::memcmp(male.vertices.data(), female.vertices.data(),
                           male.vertices.size() * sizeof(ResourceVertex)) != 0);
    }
}
void TestCityAndTravel()
{
    Game::Get().Reset();
    auto &world = Game::Get().GetState();
    assert(world.buildings.size() >= 15);
    bool observatory = false;
    for (const auto &b : world.buildings)
    {
        assert(!CityLayout::OverlapsRoad(b));
        observatory = observatory || b.type == BuildingType::Observatory;
    }
    assert(observatory);
    const auto &library = ModelLibrary::Get();
    const auto &network = RoadNetwork::Get();
    for (const auto &lane : network.lanes)
    {
        Vector2 p = {(lane.start.x + lane.end.x) * .5f, (lane.start.y + lane.end.y) * .5f};
        Transform3D transform;
        transform.yaw = std::atan2(lane.direction.y, lane.direction.x);
        auto nose = transform.Apply({1, 0, 0});
        assert(nose.x * lane.direction.x + nose.y * lane.direction.y > .999f);
        for (size_t car = 0; car < 4; ++car)
        {
            const auto &model = ResourceAssets::Get(library.vehicles[car].body.resourceId);
            for (size_t j = 0; j < model.vertices.size(); j += 9)
            {
                const auto &vertex = model.vertices[j];
                auto v = transform.Apply({vertex.x, vertex.y, vertex.z});
                float x = p.x + v.x, y = p.y + v.y;
                for (const auto &b : world.buildings)
                {
                    assert(!(x > b.x && x < b.x + b.width && y > b.y && y < b.y + b.depth));
                }
            }
        }
    }
    world.isInsideHome = false;
    world.bikePosition = {80, 0};
    world.bikeYaw = 0;
    Overworld::ToggleBike(world);
    assert(world.isBikeMounted);
    world.pressedKeys['w'] = true;
    for (int i = 0; i < 80; ++i)
    {
        Overworld::UpdateBike(world, .025f);
    }
    assert(world.bikeSpeed > 7 && world.bikeSpeed <= Overworld::kBikeCruiseSpeed &&
           world.playerPosition.x > 90);
    world.pressedKeys['w'] = false;
    for (int i = 0; i < 30; ++i)
    {
        Overworld::UpdateBike(world, .025f);
    }
    Overworld::ToggleBike(world);
    assert(!world.isBikeMounted && world.bikeSpeed == 0);
    game::GameLevels::Reset();
}

void TestScaleGenderAndDistrict()
{
    const auto &library = ModelLibrary::Get();
    for (const auto &building : library.buildings)
    {
        const auto &resource = ResourceAssets::Get(building.resourceId);
        assert(resource.minimum.x >= -.0001f && resource.minimum.y >= -.0001f);
        assert(resource.maximum.x <= 1.0001f && resource.maximum.y <= 1.0001f);
        assert(resource.physicalSize.x > 1 && resource.physicalSize.y > 1);
    }
    assert(library.bed.resourceId && library.console.resourceId && library.mineGround.resourceId);
    // Traffic now follows connected lanes; it has no wrapping render-time position function.
    assert(RoadNetwork::Get().lanes.size() > 100);
    assert(Overworld::Traffic().Vehicles().empty());
}

void TestScrolling()
{
    assert(RenderStyle::Shade(.1f, true) == RenderStyle::Shade(.2f, true));
    assert(RenderStyle::Shade(.3f, true) != RenderStyle::Shade(.9f, true));
    assert(RenderStyle::Shade(.3f, false) != RenderStyle::Shade(.4f, false));
}

struct OffscreenContext
{
    // A hidden own-DC surface lets the installed driver expose its real OpenGL
    // capabilities, including the same FBO depth attachment used by the game.
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    OffscreenContext()
    {
        WNDCLASSW type = {};
        type.style = CS_OWNDC;
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"GameRenderChecks";
        assert(RegisterClassW(&type));
        window = CreateWindowExW(0, type.lpszClassName, L"", WS_POPUP, 0, 0, kCanvasWidth, kCanvasHeight,
                                 nullptr, nullptr, type.hInstance, nullptr);
        assert(window);
        dc = GetDC(window);
        PIXELFORMATDESCRIPTOR format = {};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.cDepthBits = 24;
        int index = ChoosePixelFormat(dc, &format);
        assert(index && SetPixelFormat(dc, index, &format));
        context = wglCreateContext(dc);
        assert(context && wglMakeCurrent(dc, context));
        assert(glewInit() == GLEW_OK);
        while (glGetError() != GL_NO_ERROR)
        {
        }
        GLint bits = 0;
        glGetIntegerv(GL_DEPTH_BITS, &bits);
        assert(bits > 0);
        std::cout << "Offscreen renderer: " << glGetString(GL_RENDERER) << "; depth bits: " << bits
                  << std::endl;
    }
    ~OffscreenContext()
    {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(context);
        ReleaseDC(window, dc);
        DestroyWindow(window);
        UnregisterClassW(L"GameRenderChecks", GetModuleHandleW(nullptr));
    }
};

void Begin()
{
    renderqueue::BeginFrame();
    GameRenderer::Get().Backend().BeginScene(kCanvasWidth, kCanvasHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kCanvasWidth, kCanvasHeight, 0, -65536, 65536);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
std::vector<unsigned char> Pixels(int width = kCanvasWidth, int height = kCanvasHeight)
{
    renderqueue::Flush();
    GameRenderer::Get().Backend().ResolveScene();
    glFinish();
    assert(glGetError() == GL_NO_ERROR);
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    assert(glGetError() == GL_NO_ERROR);
    return pixels;
}
void Save(const std::filesystem::path &path, int width = kCanvasWidth, int height = kCanvasHeight)
{
    auto pixels = Pixels(width, height);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        std::swap(pixels[i], pixels[i + 2]);
    }
    BITMAPFILEHEADER header = {};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + DWORD(pixels.size());
    BITMAPINFOHEADER info = {};
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<char *>(&header), sizeof(header));
    output.write(reinterpret_cast<char *>(&info), sizeof(info));
    output.write(reinterpret_cast<char *>(pixels.data()), pixels.size());
    assert(output.good());
}
void TestAntialiasing()
{
    auto &renderer = GameRenderer::Get().Backend();
    renderer.SetStyle({});
    Begin();
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    assert(renderer.GetAntialiasingSamples() == 4 && samples == 4 && glIsEnabled(GL_MULTISAMPLE));
    renderer.EndScene(0);
    auto style = renderer.GetStyle();
    style.antialiasing = false;
    renderer.SetStyle(style);
    Begin();
    glGetIntegerv(GL_SAMPLES, &samples);
    assert(samples == 0 && renderer.GetAntialiasingSamples() == 0);
    renderer.EndScene(0);
    renderer.SetStyle({});
}
void TestDepth()
{
    auto draw = [](bool reverse) {
        Begin();
        GameRenderer::Get().Backend().BeginModels();
        for (int i = 0; i < 2; ++i)
        {
            int n = reverse ? 1 - i : i;
            Palette color;
            color.body = n ? std::array<float, 4>{.1f, .3f, 1, 1} : std::array<float, 4>{1, .2f, .1f, 1};
            Drawing::DrawModel3D(ModelLibrary::Get().box, {640, 440}, {2, 2, 2}, {}, n ? 300.f : 100.f,
                                 color);
        }
        GameRenderer::Get().Backend().EndModels();
        return Pixels();
    };
    assert(draw(false) == draw(true)); // Object submission order cannot override depth.
    auto overlay = [](bool translated) {
        Begin();
        auto &renderer = GameRenderer::Get().Backend();
        renderer.BeginModels();
        renderqueue::PushMatrix();
        if (translated)
        {
            renderqueue::Translate(140, 60, 0);
        }
        Drawing::DrawGlow(translated ? Vector2{400, 300} : Vector2{540, 360}, 50, {.2f, .8f, 1, 1});
        renderqueue::PopMatrix();
        renderer.EndModels();
        return Pixels();
    };
    assert(overlay(true) == overlay(false)); // Deferred effects retain their Actor's parent offset.
}
void TestEffectDepth()
{
    auto draw = [](int effect) {
        Begin();
        auto &renderer = GameRenderer::Get().Backend();
        renderer.BeginModels();
        Drawing::DrawModel3D(ModelLibrary::Get().box, {640, 500}, {4, 4, 3}, {}, 100, {});
        renderer.EndModels();
        if (effect)
        {
            float depth = effect < 0 ? -1000.f : 1000.f;
            const RenderPoint quad[] = {
                {625, 425, depth}, {655, 425, depth}, {655, 455, depth}, {625, 455, depth}};
            renderqueue::Depth(true, false);
            Drawing::SetColor({.2f, .8f, 1, 1});
            renderer.DrawEffect(quad, SurfaceEffect::JetExhaust, .7f);
            renderqueue::Flush();
            GLboolean writing = GL_TRUE;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &writing);
            assert(!writing); // Alpha effects must not occlude one another via depth writes.
            renderqueue::Depth(false);
        }
        return Pixels();
    };
    const auto solid = draw(0);
    assert(draw(-1) == solid); // A mesh in front completely hides the same billboard.
    assert(draw(1) != solid);  // A billboard in front remains visible.
    auto coveredVehicle = [](bool effects) {
        Begin();
        auto &renderer = GameRenderer::Get().Backend();
        renderer.BeginModels();
        if (effects)
        {
            Drawing::DrawVehicle({640, 440}, 1.5f, VehicleType::JetBike, {}, 1);
        }
        else
        {
            Transform3D rotation;
            rotation.yaw = -kPi / 4;
            Drawing::DrawModel3D(ModelLibrary::Get().vehicles[size_t(VehicleType::JetBike)].body, {640, 440},
                                 {1.5f, 1.5f, 1.5f}, rotation, 0, {});
        }
        Drawing::DrawModel3D(ModelLibrary::Get().box, {640, 650}, {12, 12, 14}, {}, 1000, {});
        renderer.EndModels();
        return Pixels();
    };
    assert(coveredVehicle(true) == coveredVehicle(false)); // Production flame, glow and sparks all hide.
}
void TestCityCoverage(const std::filesystem::path &output)
{
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    assert(game.IsPositionBlocked({kWorldMaxX + .01f, 0}));
    assert(game.IsPositionBlocked({kWorldMinX - .01f, 0}));
    assert(game.IsPositionBlocked({0, kWorldMaxY + .01f}));
    assert(game.IsPositionBlocked({0, kWorldMinY - .01f}));
    int index = 0;
    for (Vector2 camera : {Vector2{kWorldMinX, kWorldMinY},
                           {kWorldMinX, kWorldMaxY},
                           {kWorldMaxX, kWorldMinY},
                           {kWorldMaxX, kWorldMaxY},
                           {0, kWorldMinY},
                           {0, kWorldMaxY},
                           {kWorldMinX, 0},
                           {kWorldMaxX, 0}})
    {
        world.cameraPosition = world.playerPosition = camera;
        Begin();
        game::SceneManager::Get(game::SceneKind::Background).Draw();
        assert(!game::SceneManager::Get(game::SceneKind::Background).GetGraph().Find("background"));
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        renderqueue::Flush();
        GameRenderer::Get().Backend().ResolveScene();
        for (auto sample :
             {Vector2{1, 1}, {1278, 1}, {1, 798}, {1278, 798}, {640, 1}, {640, 798}, {1, 400}, {1278, 400}})
        {
            float depth = 1;
            glReadPixels(int(sample.x), int(sample.y), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            assert(depth < .999f); // Every viewport edge has actual world geometry, not a backdrop.
        }
        GameRenderer::Get().Backend().EndScene(0);
        if (index++ < 4)
        {
            Save(output / ("city-edge-" + std::to_string(index) + ".bmp"));
        }
    }
}
void CaptureScenes(const std::filesystem::path &output)
{
    auto &world = Game::Get().GetState();
    Game::Get().Reset();
    world.isInsideHome = false;
    world.playerPosition = kLiaPosition;
    world.cameraPosition = kLiaPosition;
    Begin();
    game::SceneManager::Get(game::SceneKind::Background).Draw();
    renderqueue::Flush();
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    game::SceneManager::Get(game::SceneKind::Exterior).Draw();
    GameRenderer::Get().Backend().EndScene(0);
    Save(output / "city.bmp");
    game::GameLevels::Mine().Enter(true);
    world.cameraPosition = world.playerPosition;
    world.cursorPosition = {920, 440};
    Begin();
    game::SceneManager::Get(game::SceneKind::Mine).Draw();
    GameRenderer::Get().Backend().EndScene(0);
    static_cast<game::MineScene &>(game::SceneManager::Get(game::SceneKind::Mine)).DrawDarkness();
    Save(output / "mine.bmp");
    game::GameLevels::Reset();
    auto &quest = game::GameLevels::Mine().GetState().quest;
    assert(quest.Advance(game::MineQuestStage::ShowRecord));
    assert(quest.Advance(game::MineQuestStage::CheckCityRelay));
    world.isBikeMounted = true;
    world.isInsideHome = false;
    world.bikePosition = world.playerPosition = world.cameraPosition = {128, 0};
    for (float yaw : {-kPi / 6, 0.f, kPi / 6})
    {
        world.bikeYaw = yaw;
        Begin();
        game::SceneManager::Get(game::SceneKind::Background).Draw();
        renderqueue::Flush();
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        GameRenderer::Get().Backend().EndScene(0);
        Save(output / (yaw < 0 ? "bike-yaw-up.bmp" : yaw > 0 ? "bike-yaw-down.bmp" : "bike-neutral.bmp"));
    }
    Begin();
    for (int i = 0; i < 8; ++i)
    {
        auto pose = ModelRig::Pose(1.2f, true, true);
        Transform3D rotation;
        rotation.yaw = i * kPi / 4;
        Palette palette;
        palette.body = {.7f, .84f, .9f, 1};
        Drawing::DrawModel3D(ModelLibrary::Get().characters[0],
                             {140.f + (i % 4) * 300, 310.f + (i / 4) * 360}, {3, 3, 3}, rotation, 0, palette,
                             &pose);
    }
    Save(output / "aim-directions.bmp");
    // The same palette makes garment cuts and role-specific equipment easy to review.
    Begin();
    for (int i = 0; i < 5; ++i)
    {
        auto pose = ModelRig::Pose(.8f, true);
        Transform3D rotation;
        rotation.yaw = -.25f;
        Palette palette;
        palette.body = {.6f, .7f, .74f, 1};
        palette.accent = {.95f, .59f, .24f, 1};
        Drawing::DrawModel3D(ModelLibrary::Get().characters[i], {140.f + i * 245, 530}, {3.2f, 3.2f, 3.2f},
                             rotation, 0, palette, &pose);
        constexpr const char *names[] = {"탐험복", "정비 작업복", "경비 방호복", "의무복", "연구원 코트"};
        Drawing::DrawLabel(90.f + i * 245, 585, names[i]);
    }
    GameRenderer::Get().Backend().EndScene(0);
    Save(output / "wardrobes.bmp");
    // Capture two frames immediately across a former modulo boundary.
    std::vector<unsigned char> previousScroll;
    for (int frame = 0; frame < 2; ++frame)
    {
        world.bikePosition = world.playerPosition =
            world.cameraPosition = {128.f + (frame ? .001f : -.001f), 0};
        world.bikeYaw = 0;
        Begin();
        game::SceneManager::Get(game::SceneKind::Background).Draw();
        renderqueue::Flush();
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        GameRenderer::Get().Backend().EndScene(0);
        auto currentScroll = Pixels();
        if (frame)
        {
            size_t changed = 0;
            for (size_t i = 0; i < currentScroll.size(); i += 4)
            {
                // MSAA changes fractional edge coverage by a few quantisation levels during
                // subpixel movement. Count visible changes, retaining the same 2% pop limit.
                if (std::abs(int(currentScroll[i]) - int(previousScroll[i])) > 3 ||
                    std::abs(int(currentScroll[i + 1]) - int(previousScroll[i + 1])) > 3 ||
                    std::abs(int(currentScroll[i + 2]) - int(previousScroll[i + 2])) > 3)
                {
                    ++changed;
                }
            }
            Save(output / "scroll-after.bmp");
            std::cout << "Scroll changed pixels: " << changed << std::endl;
            assert(changed < kCanvasWidth * kCanvasHeight / 50);
        }
        previousScroll = std::move(currentScroll);
        Save(output / (frame ? "scroll-after.bmp" : "scroll-before.bmp"));
    }
    std::ofstream catalog(output / "models.json");
    catalog << monitoring::ModelCatalog::Build();
}
void TestPixelPresentation(const std::filesystem::path &output)
{
    auto &renderer = GameRenderer::Get().Backend();
    auto &world = Game::Get().GetState();
    game::GameLevels::Reset();
    world.isBikeMounted = false;
    world.isInsideHome = false;
    world.playerPosition = kLiaPosition;
    world.cameraPosition = kLiaPosition;
    for (auto resolution : {WorldResolution::Half, WorldResolution::Quarter})
    {
        RenderStyle style;
        style.resolution = resolution;
        renderer.SetStyle(style);
        assert(renderer.GetRenderWidth() == kCanvasWidth / int(resolution));
        assert(renderer.GetRenderHeight() == kCanvasHeight / int(resolution));
        Begin();
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        renderer.EndScene(0);
        auto pixels = Pixels();
        int block = int(resolution);
        // Nearest enlargement has identical RGB values throughout each source texel.
        for (int y = 0; y < kCanvasHeight; y += block)
        {
            for (int x = 0; x < kCanvasWidth; x += block)
            {
                for (int dy = 0; dy < block; ++dy)
                {
                    for (int dx = 0; dx < block; ++dx)
                    {
                        for (int c = 0; c < 3; ++c)
                        {
                            assert(pixels[((y + dy) * kCanvasWidth + x + dx) * 4 + c] ==
                                   pixels[(y * kCanvasWidth + x) * 4 + c]);
                        }
                    }
                }
            }
        }
        Save(output / (resolution == WorldResolution::Half ? "city-640.bmp" : "city-320.bmp"));
        auto cursor = world.cursorPosition;
        Game::Get().MouseMotion(900, 400);
        assert(std::fabs(world.cursorPosition.x - 900) < .001 &&
               std::fabs(world.cursorPosition.y - 400) < .001);
        world.cursorPosition = cursor;
    }
    renderer.SetStyle({});
    assert(renderer.GetRenderWidth() == kCanvasWidth);
    Game::Get().SpecialKeyDown(GLUT_KEY_F7, 0, 0);
    assert(!renderer.GetStyle().cartoon);
    Game::Get().SpecialKeyDown(GLUT_KEY_F8, 0, 0);
    assert(renderer.GetStyle().resolution == WorldResolution::Half);
    renderer.SetStyle({});
}
void TestQuestAndResources()
{
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.gateSignalHeard = true;
    world.hasDescendedObservatory = true;
    world.mainQuestStage = MainQuestStage::MountJetBike;
    world.playerPosition = kLiaPosition;
    world.cameraPosition = kLiaPosition;
    auto &quest = game::GameLevels::Mine().GetState().quest;
    auto draw = [&]() {
        Begin();
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        GameRenderer::Get().Backend().EndScene(0);
    };
    auto &graph = game::SceneManager::Get(game::SceneKind::Exterior).GetGraph();
    assert(game.IsQuestInteraction(InteractionTarget::Lia));
    assert(!game.IsQuestInteraction(InteractionTarget::JetBike));
    draw();
    assert(graph.Find("marker/lia") && !graph.Find("marker/jet-bike") && !graph.Find("marker/city-receiver"));
    // Shared vehicle resources are independent of whether off-screen traffic has arrived yet.
    std::vector<rendering::RenderObject> carA, carB;
    auto &backend = GameRenderer::Get().Backend();
    backend.BeginModels();
    backend.BindObject(&carA);
    Drawing::DrawWorldVehicle({-1, 0}, 3.3f, 1, VehicleType::Coupe, {}, .2f, 0);
    backend.BindObject(&carB);
    Drawing::DrawWorldVehicle({1, 0}, 3.3f, 1, VehicleType::Coupe, {}, .2f, 0);
    backend.EndModels();
    assert(!carA.empty() && !carB.empty() && carA[0].mesh == carB[0].mesh);
    assert(carA[0].material && carB[0].material);
    auto statistics = GameRenderer::Get().Backend().GetFrameStatistics();
    assert(statistics.instancedDraws > 0 && statistics.shadowDraws > 0 && statistics.objects > 100);
    // Legacy text/effect UV arrays alias generic attributes. Leaked instance divisors
    // would silently erase those overlays after a model draw, despite GL_NO_ERROR.
    for (unsigned int attribute = 0; attribute < 12; ++attribute)
    {
        GLint divisor = -1;
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &divisor);
        assert(divisor == 0);
    }
    assert(quest.Advance(game::MineQuestStage::ShowRecord));
    draw();
    assert(graph.Find("marker/city-receiver") && !graph.Find("marker/jet-bike") && !graph.Find("marker/lia"));
    assert(quest.Advance(game::MineQuestStage::CheckCityRelay));
    draw();
    assert(graph.Find("marker/jet-bike") && !graph.Find("marker/city-receiver"));
}
void TestTrafficOcclusion(const std::filesystem::path &output)
{
    auto &world = Game::Get().GetState();
    Game::Get().Reset();
    world.isInsideHome = false;
    world.cameraPosition = world.playerPosition = {0, 0};
    auto &renderer = GameRenderer::Get().Backend();
    const auto &model = ModelLibrary::Aircraft(AircraftType::AirTaxi, false);
    const auto &mesh = ResourceAssets::Get(model.resourceId);
    const float altitude = 12;
    Point center = {(mesh.minimum.x + mesh.maximum.x) * .5f, (mesh.minimum.y + mesh.maximum.y) * .5f,
                    (mesh.minimum.z + mesh.maximum.z) * .5f};
    // Align the projected hull with the player's torso at its real flight altitude.
    float sum = ((altitude + center.z) * 44 - WorldScale::aimHeightPixels * .65f) / 18 - center.x - center.y;
    Vector2 position = {sum * .5f - (center.x - center.y) * .5f, sum * .5f + (center.x - center.y) * .5f};
    static const auto playerMarker = [] {
        auto marker = std::make_unique<Model>();
        marker->space = CoordinateSpace::World;
        marker->count = 1;
        marker->parts.resize(1);
        auto &part = marker->parts[0];
        part.count = 4;
        part.points[0] = {-.25f, .25f, 0};
        part.points[1] = {.25f, -.25f, 0};
        part.points[2] = {.25f, -.25f, .8f};
        part.points[3] = {-.25f, .25f, .8f};
        part.color = {1, .01f, .01f, 1};
        part.surface = Surface::Emissive;
        return marker;
    }();
    auto draw = [&](bool faded, bool reverse) {
        Begin();
        renderer.BeginModels();
        std::vector<rendering::RenderObject> aircraft;
        auto plane = [&]() {
            renderer.BindObject(&aircraft);
            if (faded)
            {
                Drawing::DrawWorldAircraft(position, altitude, AircraftType::AirTaxi, false, 0, 0);
            }
            else
            {
                Transform3D transform;
                transform.position.z = altitude;
                auto pose = ModelRig::Pose(world.animationTimeSeconds * 10, true);
                Drawing::DrawModel3D(model, Drawing::WorldToScreen(position.x, position.y), {1, 1, 1},
                                     transform, (position.x + position.y) * 36, {}, &pose);
            }
            renderer.BindObject(nullptr);
        };
        if (reverse)
        {
            plane();
        }
        Drawing::DrawModel3D(*playerMarker, Drawing::WorldToScreen(0, 0), {1, 1, 1}, {}, 0, {});
        if (!reverse)
        {
            plane();
        }
        renderer.EndModels();
        if (faded)
        {
            assert(aircraft.front().material->palette.body[3] < .5f);
        }
        else
        {
            assert(aircraft.front().material->palette.body[3] == 1);
        }
        auto pixels = Pixels();
        Save(output / (faded ? "aircraft-faded.bmp" : "aircraft-opaque.bmp"));
        int red = 0;
        for (int y = 320; y < 400; ++y)
        {
            for (int x = 615; x < 665; ++x)
            {
                size_t at = size_t(y * kCanvasWidth + x) * 4;
                red += pixels[at] > pixels[at + 1] * 2 && pixels[at] > pixels[at + 2] * 2;
            }
        }
        return red;
    };
    int opaque = draw(false, false), translucent = draw(true, false);
    std::cout << "Occlusion red pixels " << opaque << " -> " << translucent << std::endl;
    assert(translucent > opaque + 50);
    assert(draw(true, true) == translucent); // The player remains visible in either batching order.
    Save(output / "aircraft-player-occlusion.bmp");
    // A nearby ground car fades too; a car well away from the player keeps its material.
    for (float offset : {0.f, 20.f})
    {
        Begin();
        renderer.BeginModels();
        std::vector<rendering::RenderObject> car;
        renderer.BindObject(&car);
        Drawing::DrawWorldVehicle({offset, offset}, 3.3f, 1, VehicleType::Coupe, {}, 0, 0);
        renderer.EndModels();
        assert((car.front().material->palette.body[3] < .5f) == (offset == 0));
    }
}
void CaptureCrossingSignals(const std::filesystem::path &output)
{
    auto &world = Game::Get().GetState();
    const auto &net = RoadNetwork::Get();
    for (int lanes : {2, 4})
    {
        Game::Get().Reset();
        world.isInsideHome = false;
        int junction = -1, arm = -1;
        for (size_t j = 0; j < net.junctions.size(); ++j)
        {
            if (!net.IsSignalized(int(j)))
            {
                continue;
            }
            for (int out : net.junctions[j].outgoing)
            {
                if (net.roads[net.lanes[out].road].lanes == lanes)
                {
                    junction = int(j);
                    arm = out;
                    break;
                }
            }
            if (junction >= 0)
            {
                break;
            }
        }
        assert(junction >= 0);
        auto direction = net.lanes[arm].direction, normal = RoadNetwork::RightOf(direction);
        const auto &j = net.junctions[junction];
        world.cameraPosition = j.position;
        world.playerPosition = {j.position.x + direction.x * (j.radius + 1) + normal.x * (lanes * .75f + 1),
                                j.position.y + direction.y * (j.radius + 1) + normal.y * (lanes * .75f + 1)};
        for (int frame = 0; frame < 7200 && !Overworld::Traffic().PedestrianGreen(junction); ++frame)
        {
            Overworld::Traffic().Update(world, 1.f / 60);
        }
        assert(Overworld::Traffic().PedestrianGreen(junction));
        Begin();
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        GameRenderer::Get().Backend().EndScene(0);
        Save(output / ("crosswalk-" + std::to_string(lanes) + "lane.bmp"));
    }
}
void CaptureRoadDelivery(const std::filesystem::path &output)
{
    Game::Get().Reset();
    auto &world = Game::Get().GetState();
    world.isInsideHome = false;
    world.playerPosition = {-30, -10};
    constexpr Vector2 cameras[] = {{-32, -17}, {60, -17}, {48, -17}, {48, -85}};
    constexpr const char *names[] = {"roads-junction.bmp", "roads-transition.bmp", "roads-overpass.bmp",
                                     "roads-ramp.bmp"};
    for (size_t i = 0; i < std::size(cameras); ++i)
    {
        world.cameraPosition = cameras[i];
        Begin();
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        GameRenderer::Get().Backend().EndScene(0);
        assert(glGetError() == GL_NO_ERROR);
        Save(output / names[i]);
    }
}
void CaptureAircraftThrusters(const std::filesystem::path &output)
{
    Game::Get().Reset();
    auto &world = Game::Get().GetState();
    world.cameraPosition = {0, 0};
    world.playerPosition = {80, 80};
    world.animationTimeSeconds = .4f;
    Begin();
    auto &renderer = GameRenderer::Get().Backend();
    renderer.BeginModels();
    for (int i = 0; i < 4; ++i)
    {
        constexpr float altitude = 4;
        float screenX = 370.f + (i % 2) * 550, screenY = 230.f + (i / 2) * 350;
        float u = (screenX - 640) / 36, v = (screenY - 456 + altitude * 44) / 18;
        Vector2 position = {(u + v) * .5f, (v - u) * .5f};
        Drawing::DrawWorldAircraft(position, altitude, AircraftType(i), i % 2 == 0, 0, .08f);
    }
    renderer.EndModels();
    renderer.EndScene(.4f);
    Save(output / "aircraft-thrusters.bmp");
}
void QualityCaptures(const std::filesystem::path &output)
{
    auto &world = Game::Get().GetState();
    Game::Get().Reset();
    world.isInsideHome = false;
    world.isInObservatory = true;
    world.cameraPosition = {0, 0};
    world.playerPosition = {0, 0};
    Begin();
    game::SceneManager::Get(game::SceneKind::Observatory).Draw();
    GameRenderer::Get().Backend().EndScene(0);
    Save(output / "observatory.bmp");
    // Close-up of the supplied twin-lobed silhouette, plus explicit sparks.
    Begin();
    Drawing::SetColor({.12f, .8f, 1, 1});
    const RenderPoint flame[] = {{560, 140}, {560, 680}, {860, 680}, {860, 140}};
    GameRenderer::Get().Backend().DrawEffect(flame, SurfaceEffect::JetExhaust, .7f);
    renderqueue::Flush();
    GameRenderer::Get().Backend().EndScene(.7f);
    Save(output / "exhaust-profile.bmp");
    Game::Get().Reset();
    game::GameLevels::Mine().Enter(true);
    world.playerPosition = {24.5f, 27.5f};
    world.cameraPosition = world.playerPosition;
    world.cursorPosition = {1000, 360};
    Begin();
    game::SceneManager::Get(game::SceneKind::Mine).Draw();
    GameRenderer::Get().Backend().EndScene(0);
    game::SceneManager::Get(game::SceneKind::Mine);
    static_cast<game::MineScene &>(game::SceneManager::Get(game::SceneKind::Mine)).DrawDarkness();
    Save(output / "mine-wall-lighting.bmp");
}
void CaptureDistrictAndBodies(const std::filesystem::path &output)
{
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    for (Vector2 camera : {Vector2{16.5f, 0}, {0, 12.f}})
    {
        world.cameraPosition = world.playerPosition = camera;
        Begin();
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        auto &graph = game::SceneManager::Get(game::SceneKind::Exterior).GetGraph();
        int gates = 0, walls = 0;
        for (const auto *actor : graph.CollectVisible({-100000, -100000, 100000, 100000}))
        {
            const auto &name = actor->GetName();
            if (name.find("boundary/") != 0)
            {
                continue;
            }
            gates += name.find("/gate/") != std::string::npos;
            walls += name.find("/wall/") != std::string::npos;
        }
        // The connected overworld removed the old tutorial enclosure around Halo.
        assert(gates == 0 && walls == 0);
        GameRenderer::Get().Backend().EndScene(0);
        CityHud{}.Draw();
        Save(output / (camera.x > 0 ? "east-gate.bmp" : "south-bulkhead.bmp"));
    }
    Begin();
    auto &renderer = GameRenderer::Get().Backend();
    renderer.BeginModels();
    for (int gender = 0; gender < 2; ++gender)
    {
        for (int role = 0; role < 5; ++role)
        {
            auto pose = ModelRig::Pose(.8f, true, role == 0, 0, false, Gender(gender));
            Transform3D rotation;
            rotation.yaw = -.25f;
            Palette palette;
            palette.body = {.6f, .7f, .74f, 1};
            palette.accent = {.95f, .59f, .24f, 1};
            Drawing::DrawModel3D(ModelLibrary::Character(CharacterType(role), Gender(gender)),
                                 {140.f + role * 245, 320.f + gender * 340}, {3.2f, 3.2f, 3.2f}, rotation, 0,
                                 palette, &pose);
            Drawing::DrawLabel(78.f + role * 245, 360.f + gender * 340,
                               gender ? "여성형 장비복" : "남성형 장비복");
        }
    }
    renderer.EndModels();
    renderer.EndScene(0);
    Save(output / "body-variants.bmp");
    // The same scene actor selects body type from game data, rather than outfit or palette.
    game.Reset();
    world.isInsideHome = false;
    world.playerGender = Gender::Female;
    world.cameraPosition = world.playerPosition = kLiaPosition;
    Begin();
    game::SceneManager::Get(game::SceneKind::Exterior).Draw();
    renderer.EndScene(0);
    auto *actor = game::SceneManager::Get(game::SceneKind::Exterior).GetGraph().Find("player");
    assert(actor && !actor->GetRenderObjects().empty());
    auto female = actor->GetRenderObjects()[0].mesh;
    world.playerGender = Gender::Male;
    Begin();
    game::SceneManager::Get(game::SceneKind::Exterior).Draw();
    renderer.EndScene(0);
    assert(actor->GetRenderObjects()[0].mesh != female);
    CityHud{}.Draw();
    Save(output / "district-hud.bmp");
}

void CaptureHairAndCombat(const std::filesystem::path &output)
{
    Game::Get().Reset();
    auto &world = Game::Get().GetState();
    auto &renderer = GameRenderer::Get().Backend();
    const char *names[] = {"Buzz", "Side part", "Curls", "Bob", "Ponytail", "Bun"};
    for (bool rear : {false, true})
    {
        Begin();
        renderer.BeginModels();
        for (int gender = 0; gender < 2; ++gender)
        {
            for (int hair = 0; hair < int(HairStyle::Count); ++hair)
            {
                Vector2 screen = {108.f + hair * 212, 330.f + gender * 340};
                auto pose = ModelRig::Pose(0, false, false, 0, false, Gender(gender));
                Transform3D rotation;
                rotation.yaw = rear ? -2.5f : .35f;
                Palette palette;
                palette.body = {.5f, .65f, .7f, 1};
                Drawing::DrawModel3D(ModelLibrary::Character(CharacterType::Explorer, Gender(gender)), screen,
                                     {3.6f, 3.6f, 3.6f}, rotation, 0, palette, &pose);
                Drawing::DrawHair(screen, {3.6f, 3.6f, 3.6f}, rotation, 0, pose, CharacterType::Explorer,
                                  Gender(gender), HairStyle(hair));
                Drawing::DrawLabel(screen.x - 44, screen.y + 35, names[hair]);
            }
        }
        renderer.EndModels();
        renderer.EndScene(0);
        Save(output / (rear ? "hair-rear.bmp" : "hair-and-bodies.bmp"));
    }
    game::GameLevels::Mine().Enter(true);
    auto &mine = game::GameLevels::Mine().GetState();
    world.playerPosition = world.cameraPosition = {14.5f, 16.5f};
    std::fill(mine.tiles.begin(), mine.tiles.end(), game::Tile::Floor);
    ++mine.mapRevision;
    mine.survivors.clear();
    mine.rescueGroups[0].combatants = true;
    mine.survivors.push_back({100, 0, {16.5f, 15.5f}});
    mine.survivors.back().gender = Gender::Female;
    mine.survivors.back().combat.engaged = true;
    mine.survivors.back().combat.muzzleFlash = .05f;
    mine.riaPosition = {14.5f, 15.5f};
    mine.riaCombat.engaged = true;
    mine.riaCombat.muzzleFlash = .05f;
    mine.enemies = {{{19, 15.5f}}, {{18, 16.5f}}};
    for (auto &enemy : mine.enemies)
    {
        enemy.facing.yaw = 3.14f;
    }
    mine.tracers = {{{16.5f, 15.5f}, {18.5f, 15.5f}, .12f, true},
                    {{14.5f, 15.5f}, {16.5f, 15.5f}, .12f, true}};
    mine.lights = {{{16, 16}, 10, false}};
    Begin();
    game::SceneManager::Get(game::SceneKind::Mine).Draw();
    renderer.EndScene(0);
    static_cast<game::MineScene &>(game::SceneManager::Get(game::SceneKind::Mine)).DrawDarkness();
    Save(output / "npc-defence.bmp");
    game::GameLevels::Reset();
}
void CaptureMapNavigation(const std::filesystem::path &output)
{
    auto &game = Game::Get();
    auto &world = game.GetState();
    auto &renderer = GameRenderer::Get().Backend();
    game.Reset();
    world.isInsideHome = false;
    world.noticeSecondsRemaining = 0;
    world.cameraPosition = world.playerPosition = {0, 0};
    auto mapPixels = [&]() {
        Begin();
        renderer.EndScene(0);
        Drawing::DrawRectangle(0, 0, kCanvasWidth, kCanvasHeight, {.01f, .02f, .03f});
        MapHud::DrawMinimap();
        return Pixels();
    };
    auto baseline = mapPixels();
    world.facingYaw += 1.7f;
    world.cursorPosition = {world.cursorPosition.x - 400, world.cursorPosition.y + 250};
    auto rotated = mapPixels();
    auto pixelIndex = [](int x, int y) { return size_t((kCanvasHeight - y - 1) * kCanvasWidth + x) * 4; };
    auto mapCenter = MapHud::WorldToMap(world.playerPosition, false);
    float mapRadius = 0;
    while (MapHud::Contains({mapCenter.x + mapRadius, mapCenter.y}, false))
    {
        ++mapRadius;
    }
    --mapRadius;
    // Rotating the player cannot rotate any road/building outside the arrow itself.
    for (int y = 34; y < 253; ++y)
    {
        for (int x = 1025; x < 1247; ++x)
        {
            if ((x - mapCenter.x) * (x - mapCenter.x) + (y - mapCenter.y) * (y - mapCenter.y) > 18 * 18)
            {
                size_t index = pixelIndex(x, y);
                assert(std::equal(baseline.begin() + index, baseline.begin() + index + 4,
                                  rotated.begin() + index));
            }
        }
    }
    auto buildings = world.buildings;
    world.buildings = {{-40, -40, 80, 80, 100}};
    auto spanning = mapPixels();
    for (int y = 0; y < 260; ++y)
    {
        for (int x = 1015; x < 1256; ++x)
        {
            // A huge building crosses the entire rim; no fragment may escape the clipped map.
            if ((x - mapCenter.x) * (x - mapCenter.x) + (y - mapCenter.y) * (y - mapCenter.y) >
                (mapRadius + 2) * (mapRadius + 2))
            {
                size_t index = pixelIndex(x, y);
                assert(std::equal(rotated.begin() + index, rotated.begin() + index + 4,
                                  spanning.begin() + index));
            }
        }
    }
    world.buildings = std::move(buildings);
    world.mapVisible = true;
    assert(MapHud::HandleClick(MapHud::WorldToMap({200, 0}, true)));
    world.mapVisible = false;
    auto waypoint = mapPixels();
    int blueArrowPixels = 0;
    auto projected = MapHud::WorldToMap({200, 0}, false);
    float bearing = std::atan2(projected.y - mapCenter.y, projected.x - mapCenter.x);
    int tipX = int(mapCenter.x + std::cos(bearing) * (mapRadius - 11)),
        tipY = int(mapCenter.y + std::sin(bearing) * (mapRadius - 11));
    for (int y = tipY - 13; y < tipY + 14; ++y)
    {
        for (int x = tipX - 13; x < tipX + 14; ++x)
        {
            size_t i = pixelIndex(x, y);
            blueArrowPixels += waypoint[i] < 160 && waypoint[i + 1] > 180 && waypoint[i + 2] > 240;
        }
    }
    assert(blueArrowPixels > 20);
    auto captureScene = [&](const char *name) {
        Begin();
        game::SceneManager::Get(game::SceneManager::Current()).Draw();
        renderer.EndScene(world.animationTimeSeconds);
        if (game::GameLevels::Mine().IsActive())
        {
            static_cast<game::MineScene &>(game::SceneManager::Get(game::SceneKind::Mine)).DrawDarkness();
        }
        Hud::Render();
        Save(output / name);
    };
    captureScene("map-city-circle-destination.bmp");
    world.mapVisible = true;
    captureScene("map-city-full.bmp");
    world.mapVisible = false;
    game::GameLevels::Mine().Enter(true);
    auto &mine = game::GameLevels::Mine().GetState();
    assert(!mine.rescueGroups.empty());
    auto &mineMap = game::GameLevels::Mine().Map();
    auto route = mineMap.FindRoute(mine.entry, mine.rescueGroups[0].position);
    assert(!route.empty());
    size_t destination = route.size() > 8 ? route.size() - 8 : 0;
    for (size_t i = 0; i <= destination; ++i)
    {
        mineMap.Reveal(route[i]);
    }
    world.cameraPosition = world.playerPosition = route[destination];
    mine.enemies.clear();
    mine.lights.push_back({world.playerPosition, 16, false});
    game::GameLevels::Mine().Rescue().Scan();
    game::GameLevels::Mine().Rescue().Update(.8f);
    assert(!mine.scannerContacts.empty());
    captureScene("map-mine-scan-bearings.bmp");
    world.mapVisible = true;
    assert(!MapHud::HandleClick(MapHud::WorldToMap(game::kRuinSignal, true)));
    assert(MapHud::HandleClick(MapHud::WorldToMap(mine.entry, true)));
    captureScene("map-mine-full.bmp");
    world.mapVisible = false;
    auto aim = game::MineLevel::GetAimDirection(game::GameLevels::Mine().GetAimAngle());
    mine.enemies = {{{world.playerPosition.x + aim.x * .8f, world.playerPosition.y + aim.y * .8f}}};
    game::GameLevels::Mine().Combat().Melee();
    mine.meleeSeconds = .4f;
    captureScene("mine-melee.bmp");
    game::GameLevels::Chapter().Enter(true);
    world.noticeSecondsRemaining = 0;
    world.mapVisible = true;
    assert(MapHud::HandleClick(MapHud::WorldToMap({32.5f, 12.5f}, true)));
    captureScene("map-frontier-full.bmp");
    game.Reset();
}

void CaptureCivicSystems(const std::filesystem::path &output)
{
    auto &game = Game::Get();
    auto &world = game.GetState();
    auto &renderer = GameRenderer::Get().Backend();
    game.Reset();
    world.isInsideHome = false;
    world.playerPosition = world.cameraPosition = {2, 0};
    auto &law = CivicJustice::State();
    law.wanted = 5;
    law.heat = 1000;
    law.identified = true;
    law.infamy = 75;
    SecurityOfficer officer;
    officer.position = {4, 2};
    officer.lastKnown = world.playerPosition;
    officer.tier = 5;
    officer.health = 260;
    officer.facing.yaw = -2.2f;
    officer.muzzleFlash = .05f;
    law.officers.push_back(officer);
    law.tracers.push_back({officer.position, world.playerPosition, .12f, true});
    auto draw = [&](const char *name) {
        Begin();
        game::SceneManager::Get(game::SceneKind::Exterior).Draw();
        renderer.EndScene(world.animationTimeSeconds);
        game::SceneManager::Get(game::SceneKind::Hud).Draw();
        renderqueue::Flush();
        assert(glGetError() == GL_NO_ERROR);
        Save(output / name);
    };
    draw("civic-wanted.bmp");
    auto wanted = Pixels();
    auto mapDifference = [](const std::vector<unsigned char> &a, const std::vector<unsigned char> &b) {
        size_t changed = 0;
        for (int y = 60; y < 220; ++y)
        {
            for (int x = 1065; x < 1224; ++x)
            {
                size_t at = (size_t(kCanvasHeight - 1 - y) * kCanvasWidth + x) * 4;
                changed += a[at] != b[at] || a[at + 1] != b[at + 1] || a[at + 2] != b[at + 2];
            }
        }
        return changed;
    };
    law.wanted = 0;
    draw("civic-no-search-markers.bmp");
    assert(mapDifference(wanted, Pixels()) > 10); // Search markers only appear while wanted.
    law.wanted = 5;
    law.officers.front().facing.yaw += kPi;
    draw("civic-security-turned.bmp");
    assert(mapDifference(wanted, Pixels()) > 10); // The map cone follows actual heading.
    law.officers.front().facing.yaw -= kPi;
    law.identified = false;
    law.responseStarted = true;
    law.initialUnseenSeconds = 8;
    draw("civic-unidentified-search.bmp");
    assert(mapDifference(wanted, Pixels()) > 10); // Search and identified pursuit have different colors.
    law.identified = true;
    game::GameLevels::Mine().GetState().player.health = 0;
    HospitalRecovery::Begin();
    HospitalRecovery::Update(.84f);
    assert(HospitalRecovery::Opacity() > .95f);
    draw("civic-medical-fade.bmp");
    auto pixels = Pixels();
    size_t dark = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        dark += pixels[i] < 30 && pixels[i + 1] < 30 && pixels[i + 2] < 30;
    }
    assert(dark > size_t(kCanvasWidth * kCanvasHeight * .9f));
    HospitalRecovery::Update(1.6f);
    assert(!HospitalRecovery::Active());
    assert(game::GameLevels::Mine().GetState().player.health > 0 && law.wanted == 0 && law.infamy > 0);
    draw("civic-hospital.bmp");
    game.Reset();
}

void ReadDialogue()
{
    auto &d = Game::Get().GetState().dialogue;
    int count = 0;
    while (Dialogue::Active())
    {
        assert(++count < 200);
        if (!d.pages[d.page].choices.empty())
        {
            assert(Dialogue::Select(0));
        }
        assert(Dialogue::Advance());
    }
}
void CaptureDialogue(const std::filesystem::path &output)
{
    auto &game = Game::Get();
    auto &world = game.GetState();
    auto &renderer = GameRenderer::Get().Backend();
    game.Reset();
    auto capture = [&](const char *name) {
        Begin();
        game::SceneManager::Get(game::SceneManager::Current()).Draw();
        renderer.EndScene(world.animationTimeSeconds);
        game::SceneManager::Get(game::SceneKind::Hud).Draw();
        renderqueue::Flush();
        assert(glGetError() == GL_NO_ERROR);
        Save(output / name);
    };
    world.journalVisible = true;
    capture("dialogue-prologue-journal.bmp");
    world.journalVisible = false;
    world.isInsideHome = false;
    world.isInObservatory = true;
    world.playerPosition = kObservatoryConsole;
    world.cameraPosition = {0, 0};
    game.InteractWithNearbyTarget();
    assert(Dialogue::Active());
    capture("dialogue-observatory-voice.bmp");
    Dialogue::Advance();
    capture("dialogue-observatory-player.bmp");
    Dialogue::Advance();
    capture("dialogue-observatory-ria.bmp");
    ReadDialogue();
    Dialogue::Start(
        "render/choices",
        {{"page",
          "리아",
          "현장",
          "문장을 천천히 읽어도 기록은 사라지지 않아. 조사 내용을 다시 확인하고 준비가 끝나면 출발하자.",
          {{"depart", "조사 기록을 확인했으니 현장으로 출발한다"},
           {"prepare", "아직 준비한다. 헤일로에서 장비와 보급품을 다시 확인한다"},
           {"review", "잠깐 기다린다. 지금까지 읽은 대사와 조사 기록을 다시 확인한다"}}}});
    Dialogue::Select(1);
    game.ShowNotice("보급품 획득 / 이 알림은 대화와 별도로 표시됩니다.");
    capture("dialogue-choices.bmp");
    world.dialogueHistoryVisible = true;
    capture("dialogue-history.bmp");
    game.Reset();
}

void CaptureDeliveredUi(const std::filesystem::path &output)
{
    auto &g = Game::Get();
    auto &w = g.GetState();
    auto &renderer = GameRenderer::Get().Backend();
    assert(renderer.UiSpriteCount() >= 112);
    auto draw = [&](const char *name) {
        Begin();
        game::SceneManager::Get(game::SceneManager::Current()).Draw();
        renderer.EndScene(w.animationTimeSeconds);
        if (game::GameLevels::Mine().IsActive())
        {
            static_cast<game::MineScene &>(game::SceneManager::Get(game::SceneKind::Mine)).DrawDarkness();
        }
        game::SceneManager::Get(game::SceneKind::Hud).Draw();
        renderqueue::Flush();
        assert(glGetError() == GL_NO_ERROR);
        Save(output / name);
    };
    g.Reset();
    Hud::OpenMenu(UiScreen::Title);
    draw("ui-title.bmp");
    Hud::OpenMenu(UiScreen::Stages);
    draw("ui-stages.bmp");
    Hud::OpenMenu(UiScreen::Pause);
    draw("ui-pause.bmp");
    Hud::OpenMenu(UiScreen::Options);
    draw("ui-options.bmp");
    Hud::OpenMenu(UiScreen::Summary);
    draw("ui-summary.bmp");
    Hud::OpenMenu(UiScreen::Help);
    draw("ui-help.bmp");
    Hud::OpenMenu(UiScreen::Confirm);
    draw("ui-confirm.bmp");
    Hud::CloseMenu();
    w.isInsideHome = false;
    w.playerPosition = w.cameraPosition = kCityReceiverPosition;
    auto &law = CivicJustice::State();
    law.wanted = 5;
    law.heat = 900;
    law.infamy = 80;
    auto &mine = game::GameLevels::Mine().GetState();
    mine.player.health = 18;
    draw("ui-city-unarmed.bmp");
    assert(WeaponWheel::Open());
    draw("ui-weapon-wheel-empty.bmp");
    WeaponWheel::Close();
    mine.loadout.GrantPistol();
    mine.loadout.Select(combat::WeaponSlot::Secondary);
    g.MouseMotion(640, 400);
    assert(WeaponWheel::Open());
    g.MouseMotion(780, 470);
    draw("ui-weapon-wheel-pistol.bmp");
    WeaponWheel::Close();
    combat::Shot shot{};
    mine.pistol.TryFire(0, shot);
    mine.pistol.Reload();
    draw("ui-city-combat.bmp");
    w.isBikeMounted = true;
    w.bikeSpeed = -.65f;
    draw("ui-bike-reverse.bmp");
    w.bikeSpeed = 12.8f;
    w.pressedKeys[' '] = true;
    draw("ui-bike-boost.bmp");
    g.Reset();
    auto &chapter = game::GameLevels::Chapter();
    chapter.Enter(true);
    chapter.GetState().convoyRescued = true;
    w.playerPosition = w.cameraPosition = {24.5f, 20.5f};
    mine.player.salvage = 10;
    Hud::OpenMenu(UiScreen::Supply);
    draw("ui-supply.bmp");
    Hud::CloseMenu();
    mine.player.health = 0;
    HospitalRecovery::Begin();
    HospitalRecovery::Update(.7f);
    draw("ui-medical-transfer.bmp");
    HospitalRecovery::Update(3);
    HospitalRecovery::Update(3);
    assert(w.ui.screen == UiScreen::Medical);
    draw("ui-medical-result.bmp");
    g.Reset();
    game::GameLevels::Mine().Enter(true);
    mine.enemies.clear();
    w.playerPosition = w.cameraPosition = mine.entry;
    game::GameLevels::Mine().Rescue().Scan();
    game::GameLevels::Mine().Rescue().Update(.6f);
    draw("ui-mine-scan.bmp");
    Dialogue::Start(
        "ui/radio",
        {{"radio", "리아", "무전", "빛을 따라 이동해. 구조 신호는 가까워지면 다시 스캔하자.", {}}});
    w.dialogue.blocking = false;
    draw("ui-mine-radio.bmp");
    ReadDialogue();
    auto style = renderer.GetStyle();
    style.resolution = WorldResolution::Quarter;
    renderer.SetStyle(style);
    draw("ui-pixel-world.bmp");
    style.resolution = WorldResolution::Native;
    renderer.SetStyle(style);
    Hud::OpenMenu(UiScreen::Options);
    w.uiPreferences.background = .8f;
    w.uiPreferences.border = .8f;
    w.uiPreferences.content = .85f;
    draw("ui-opacity-minimum.bmp");
    Hud::CloseMenu();
    game::GameLevels::Mine().Leave();
    w.playerPosition = w.cameraPosition = kCityReceiverPosition;
    Hud::OpenMenu(UiScreen::Summary);
    draw("ui-opacity-minimum-city.bmp");
    w.uiPreferences = {};
    g.Reset();
    w.isInsideHome = false;
    w.playerPosition = w.cameraPosition = kCityReceiverPosition;
    Dialogue::Start("ui/high-resolution",
                    {{"text",
                      "헤일로 N-0 외곽 설비 통신 기록 관리 담당자 · 현장 연결",
                      "수신 기록",
                      "긴 한글과 English 123 문구를 실제 화면에서 확인합니다. 화면 크기가 달라도 필수 대사는 "
                      "빠지지 않고, 선택지와 조작 안내가 서로 겹치지 않아야 합니다.",
                      {{"one", "현재 조사 기록을 보관하고 현장으로 출발한다"},
                       {"two", "아직 준비한다. 보급품과 지금까지 확인한 내용을 다시 점검한다"},
                       {"three", "헤일로에 남아 다른 조사 현장의 기록을 먼저 확인한다"}}}});
    HWND surface = WindowFromDC(wglGetCurrentDC());
    for (auto size : {std::pair<int, int>{1920, 1080}, {2560, 1440}})
    {
        assert(SetWindowPos(surface, nullptr, 0, 0, size.first, size.second,
                            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        g.Resize(size.first, size.second);
        renderqueue::BeginFrame();
        renderer.BeginScene(size.first, size.second);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, kCanvasWidth, kCanvasHeight, 0, -65536, 65536);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        game::SceneManager::Get(game::SceneManager::Current()).Draw();
        renderer.EndScene(0);
        Hud::Render();
        renderqueue::Flush();
        assert(glGetError() == GL_NO_ERROR);
        Save(output /
                 ("ui-dialogue-" + std::to_string(size.first) + "x" + std::to_string(size.second) + ".bmp"),
             size.first, size.second);
    }
    assert(SetWindowPos(surface, nullptr, 0, 0, kCanvasWidth, kCanvasHeight,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    g.Resize(kCanvasWidth, kCanvasHeight);
    g.Reset();
}

void CaptureChapter(const std::filesystem::path &output)
{
    auto &game = Game::Get();
    auto &world = game.GetState();
    auto &level = game::GameLevels::Chapter();
    auto &renderer = GameRenderer::Get().Backend();
    game.Reset();
    level.Enter(true);
    auto &s = level.GetState();
    auto interact = [&](game::ChapterAction action) {
        bool found = false;
        for (auto point : level.Interactions())
        {
            if (point.action == action)
            {
                world.playerPosition = point.position;
                found = true;
                break;
            }
        }
        assert(found);
        ReadDialogue();
        level.Interact();
        ReadDialogue();
    };
    auto capture = [&](const char *name, Vector2 camera, bool city = false) {
        world.cameraPosition = world.playerPosition = camera;
        s.ria.position = {camera.x + .8f, camera.y + .8f};
        Begin();
        game::SceneManager::Get(city ? game::SceneKind::Exterior : game::SceneKind::Frontier).Draw();
        renderer.EndScene(world.animationTimeSeconds);
        game::SceneManager::Get(game::SceneKind::Hud).Draw();
        renderqueue::Flush();
        assert(glGetError() == GL_NO_ERROR);
        const auto &stats = renderer.GetFrameStatistics();
        assert(stats.instancedDraws > 0 && stats.shadowDraws > 0);
        Save(output / name);
    };
    capture("chapter-generator.bmp", {11, 12});
    interact(game::ChapterAction::Generator);
    interact(game::ChapterAction::IsolateLine);
    interact(game::ChapterAction::PortableLight);
    capture("chapter-generator-restored.bmp", {11, 12});
    interact(game::ChapterAction::Kain);
    interact(game::ChapterAction::Receiver);
    capture("chapter-convoy.bmp", {33, 12.5f});
    interact(game::ChapterAction::SurveyOne);
    interact(game::ChapterAction::SurveyTwo);
    interact(game::ChapterAction::SurveyThree);
    interact(game::ChapterAction::Escort);
    for (int i = 0; i < 140; ++i)
    {
        level.Update(.05f);
    }
    capture("chapter-escort.bmp", {32, 19});
    for (int i = 0; i < 600 && !s.convoyRescued; ++i)
    {
        level.Update(.05f);
    }
    assert(s.convoyRescued && s.kainJoined);
    ReadDialogue();
    capture("chapter-relay.bmp", {30, 32});
    interact(game::ChapterAction::TradeProof);
    interact(game::ChapterAction::Scrappers);
    interact(game::ChapterAction::Ledger);
    interact(game::ChapterAction::DetailLedger);
    world.journalVisible = true;
    capture("chapter-journal.bmp", {32, 32});
    world.journalVisible = false;
    level.Leave();
    level.InteractHub();
    assert(s.comparisonStarted);
    level.Update(80.f);
    assert(!s.recordsCompared);
    capture("chapter-comparison-unread.bmp", kCityReceiverPosition, true);
    ReadDialogue();
    assert(s.recordsCompared);
    capture("chapter-comparison.bmp", kCityReceiverPosition, true);
    std::ofstream snapshot(output / "chapter-snapshot.json");
    snapshot << monitoring::WorldSnapshot::Build(1, "chapter-render");
    game.Reset();
}

void BenchmarkQuality(const std::filesystem::path &output)
{
    std::ofstream report(output / "performance.json");
#ifdef _DEBUG
    constexpr bool optimized = false;
#else
    constexpr bool optimized = true;
#endif
    report << "{\"gpu\":\"" << glGetString(GL_RENDERER)
           << "\",\"resolution\":[1280,800],\"optimized\":" << (optimized ? "true" : "false")
           << ",\"warmup\":24,\"frames\":120,\"scenes\":[";
    auto &world = Game::Get().GetState();
    for (int scenario = 0; scenario < 14; ++scenario)
    {
        Game::Get().Reset();
        world.noticeSecondsRemaining = 0;
        constexpr const char *names[] = {"city",
                                         "observatory",
                                         "bike",
                                         "mine_entry",
                                         "mine_ruins",
                                         "mine_90_enemies",
                                         "npc_firefight_90_enemies",
                                         "frontier_generator",
                                         "frontier_convoy",
                                         "frontier_relay_combat",
                                         "city_security_5stars",
                                         "roads_junction",
                                         "roads_overpass",
                                         "roads_ramp"};
        if (scenario == 0)
        {
            world.isInsideHome = false;
            world.playerPosition = kLiaPosition;
            world.cameraPosition = kLiaPosition;
        }
        if (scenario >= 11)
        {
            world.isInsideHome = false;
            constexpr Vector2 stations[] = {{-32, -17}, {48, -17}, {48, -85}};
            world.playerPosition = world.cameraPosition = stations[scenario - 11];
        }
        if (scenario == 10)
        {
            world.isInsideHome = false;
            world.playerPosition = world.cameraPosition = {2, 0};
            auto &law = CivicJustice::State();
            law.heat = 1000;
            law.wanted = 5;
            law.infamy = 100;
            law.playerGrace = 1000;
            for (int i = 0; i < 10; ++i)
            {
                SecurityOfficer officer;
                officer.id = law.nextOfficer++;
                officer.tier = 5;
                officer.health = 260;
                officer.position = {4.f + (i % 5) * 2.f, -2.f + (i / 5) * 3.f};
                officer.lastKnown = world.playerPosition;
                if (CivicJustice::Solid(officer.position, .35f))
                {
                    officer.position = {-18.f + i, 0};
                }
                law.officers.push_back(officer);
            }
        }
        if (scenario == 1)
        {
            world.isInsideHome = false;
            world.isInObservatory = true;
        }
        if (scenario == 2)
        {
            auto &q = game::GameLevels::Mine().GetState().quest;
            q.Advance(game::MineQuestStage::ShowRecord);
            q.Advance(game::MineQuestStage::CheckCityRelay);
            world.isBikeMounted = true;
            world.isInsideHome = false;
            world.bikePosition = world.playerPosition = world.cameraPosition = {128, 0};
        }
        if (scenario >= 3 && scenario < 7)
        {
            game::GameLevels::Mine().Enter(true);
            auto &mine = game::GameLevels::Mine().GetState();
            mine.invulnerability = 1000;
            if (scenario >= 4)
            {
                mine.quest.Advance(game::MineQuestStage::BypassPower);
                world.playerPosition = game::kRuinSignal;
                world.cameraPosition = world.playerPosition;
            }
            if (scenario == 5)
            {
                auto enemies = mine.enemies;
                mine.enemies.insert(mine.enemies.end(), enemies.begin(), enemies.end());
                mine.enemies.insert(mine.enemies.end(), enemies.begin(), enemies.end());
            }
        }
        if (scenario == 6)
        {
            auto &mine = game::GameLevels::Mine().GetState();
            for (int y = 36; y < 59; ++y)
            {
                for (int x = 36; x < 59; ++x)
                {
                    mine.tiles[y * game::kMapSize + x] = game::Tile::Floor;
                }
            }
            ++mine.mapRevision;
            mine.enemies.clear();
            for (int i = 0; i < 90; ++i)
            {
                float angle = i * 6.2831853f / 90;
                mine.enemies.push_back(
                    {{47.5f + std::cos(angle) * 6.5f, 46.5f + std::sin(angle) * 6.5f}, 500});
            }
            mine.riaPosition = {46.5f, 46.5f};
            for (size_t i = 0; i < mine.survivors.size(); ++i)
            {
                auto &npc = mine.survivors[i];
                npc.position = {45.5f + float(i % 4), 45.5f + float(i / 4)};
                mine.rescueGroups[npc.group].position = {47, 46.5f};
            }
            mine.lights.push_back({{47, 46.5f}, 10, false});
        }
        if (scenario >= 7 && scenario < 10)
        {
            auto &chapter = game::GameLevels::Chapter();
            chapter.Enter(true);
            world.cameraPosition = world.playerPosition = scenario == 7   ? Vector2{11, 12}
                                                          : scenario == 8 ? Vector2{33, 13}
                                                                          : Vector2{30, 32};
            auto &s = chapter.GetState();
            s.ria.position = {world.playerPosition.x + 1, world.playerPosition.y};
            s.invulnerability = 1000;
            if (scenario == 9)
            {
                s.hostile = true;
                s.kainJoined = true;
                s.kain.position = {30, 33};
            }
        }
        if (scenario == 0 || scenario == 2 || scenario >= 10)
        {
            for (int warm = 0; warm < 900; ++warm)
            {
                CivicCombat::Update(1.f / 60);
                CivicJustice::Update(1.f / 60);
                BodyCollision::Update(1.f / 60);
                Overworld::Traffic().Update(world, 1.f / 60);
                Overworld::Flights().Update(world, 1.f / 60);
                CityResidents::Get().Update(world, 1.f / 60);
            }
        }
        bool npcShotsObserved = false;
        std::vector<double> samples;
        double draws = 0, instanced = 0, triangles = 0;
        for (int frame = 0; frame < 144; ++frame)
        {
            const auto previousDraws = profiling::ReadCounter(profiling::Counter::DrawCalls);
            auto start = std::chrono::steady_clock::now();
            profiling::BeginFrame(names[scenario]);
            world.animationTimeSeconds += 1.f / 60;
            if (scenario == 0 || scenario == 2 || scenario >= 10)
            {
                CivicCombat::Update(1.f / 60);
                CivicJustice::Update(1.f / 60);
                BodyCollision::Update(1.f / 60);
                Overworld::Traffic().Update(world, 1.f / 60);
                Overworld::Flights().Update(world, 1.f / 60);
                CityResidents::Get().Update(world, 1.f / 60);
            }
            auto npcShotsBefore = profiling::ReadCounter(profiling::Counter::NpcShots);
            if (auto *level = game::GameLevels::Active())
            {
                level->Update(1.f / 60);
            }
            npcShotsObserved |= profiling::ReadCounter(profiling::Counter::NpcShots) > npcShotsBefore;
            game::SceneManager::Update(1.f / 60);
            Begin();
            if (scenario < 3 || scenario >= 10)
            {
                game::SceneManager::Get(game::SceneKind::Background).Draw();
                renderqueue::Flush();
                glDepthMask(GL_TRUE);
                glClear(GL_DEPTH_BUFFER_BIT);
            }
            game::SceneManager::Get(game::SceneManager::Current()).Draw();
            GameRenderer::Get().Backend().EndScene(world.animationTimeSeconds);
            if (scenario >= 3 && scenario < 7)
            {
                static_cast<game::MineScene &>(game::SceneManager::Get(game::SceneKind::Mine)).DrawDarkness();
            }
            game::SceneManager::Get(game::SceneKind::Hud).Draw();
            renderqueue::Flush();
            profiling::EndGpuFrame();
            glFinish();
            assert(glGetError() == GL_NO_ERROR);
            const auto frameDraws = profiling::ReadCounter(profiling::Counter::DrawCalls) - previousDraws;
            profiling::EndFrame();
            double milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (frame >= 24)
            {
                samples.push_back(milliseconds);
                draws += frameDraws;
                const auto &stats = GameRenderer::Get().Backend().GetFrameStatistics();
                instanced += stats.instancedDraws;
                triangles += stats.triangles;
            }
        }
        if (scenario == 6)
        {
            assert(npcShotsObserved);
        }
        std::sort(samples.begin(), samples.end());
        double sum = 0;
        for (double n : samples)
        {
            sum += n;
        }
        double average = sum / samples.size(), p95 = samples[size_t(samples.size() * .95)];
        report << (scenario ? "," : "") << "{\"name\":\"" << names[scenario]
               << "\",\"average_ms\":" << average << ",\"p95_ms\":" << p95 << ",\"max_ms\":" << samples.back()
               << ",\"draw_calls\":" << draws / samples.size()
               << ",\"instanced_draws\":" << instanced / samples.size()
               << ",\"triangles\":" << triangles / samples.size() << "}";
        std::cout << names[scenario] << ": average " << average << " ms; p95 " << p95 << " ms (" << 1000 / p95
                  << " fps)" << std::endl;
    }
    report << "]}";
}

} // namespace
int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--civic-only")
    {
        OffscreenContext context;
        GameRenderer::Get().Initialize();
        std::filesystem::path output = argc > 2 ? argv[2] : "x64/RevisionChecks/civic-renders";
        std::filesystem::create_directories(output);
        CaptureCivicSystems(output);
        GameRenderer::Get().Shutdown();
        std::cout << "Civic minimap and medical visual checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--dialogue-only")
    {
        OffscreenContext context;
        GameRenderer::Get().Initialize();
        std::filesystem::path output = argc > 2 ? argv[2] : "x64/RevisionChecks/dialogue-renders";
        std::filesystem::create_directories(output);
        CaptureDialogue(output);
        CaptureChapter(output);
        CaptureDeliveredUi(output);
        GameRenderer::Get().Shutdown();
        std::cout << "Dialogue visual checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--roads-only")
    {
        OffscreenContext context;
        GameRenderer::Get().Initialize();
        std::filesystem::path output = argc > 2 ? argv[2] : "x64/RevisionChecks/road-renders";
        std::filesystem::create_directories(output);
        CaptureRoadDelivery(output);
        GameRenderer::Get().Shutdown();
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--maps-only")
    {
        OffscreenContext context;
        GameRenderer::Get().Initialize();
        std::filesystem::path output = argc > 2 ? argv[2] : "x64/RevisionChecks/renders";
        std::filesystem::create_directories(output);
        CaptureMapNavigation(output);
        GameRenderer::Get().Shutdown();
        std::cout << "Map visual checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--benchmark")
    {
        OffscreenContext context;
        GameRenderer::Get().Initialize();
        std::filesystem::path output = argc > 2 ? argv[2] : "x64/RevisionChecks/renders";
        std::filesystem::create_directories(output);
        BenchmarkQuality(output);
        GameRenderer::Get().Shutdown();
        return 0;
    }
    TestModelsAndRig();
    TestHairAndBodyShape();
    TestCityAndTravel();
    TestScaleGenderAndDistrict();
    TestScrolling();
    OffscreenContext context;
    GameRenderer::Get().Initialize();
    std::cout << "Checking antialiasing, depth and effect occlusion.\n" << std::flush;
    TestAntialiasing();
    TestDepth();
    TestEffectDepth();
    std::cout << "Checking quest resources and city coverage.\n" << std::flush;
    TestQuestAndResources();
    std::filesystem::path output = argc > 1 ? argv[1] : "x64/ModelChecks/renders";
    std::filesystem::create_directories(output);
    TestCityCoverage(output);
    CaptureScenes(output);
    TestPixelPresentation(output);
    QualityCaptures(output);
    TestTrafficOcclusion(output);
    CaptureCrossingSignals(output);
    CaptureRoadDelivery(output);
    CaptureAircraftThrusters(output);
    CaptureDistrictAndBodies(output);
    CaptureHairAndCombat(output);
    CaptureDialogue(output);
    CaptureChapter(output);
    CaptureDeliveredUi(output);
    CaptureMapNavigation(output);
    CaptureCivicSystems(output);
    BenchmarkQuality(output);
    GameRenderer::Get().Shutdown();
    std::cout << "Resource GLB meshes, 23-bone skins, nine wardrobes, direct driving, billboard effects, "
                 "cartoon lighting, "
                 "pixel presentation and scene checks passed.\n";
}
