#include "stdafx.h"
#include "Drawing.h"
#include "Scene.h"
#include "Hud.h"
#include "Level.h"
#include "ResourceAssets.h"
#include "Overworld.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include <chrono>
#include "Monitoring/Monitor.h"
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

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
        assert(resource.boneCount == 17 && resource.clips.size() == 12);
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
    for (int i = 0; i < 60; ++i)
    {
        Overworld::UpdateBike(world, .025f);
    }
    assert(world.bikeSpeed > 12 && world.playerPosition.x > 90);
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
std::vector<unsigned char> Pixels()
{
    renderqueue::Flush();
    glFinish();
    assert(glGetError() == GL_NO_ERROR);
    std::vector<unsigned char> pixels(kCanvasWidth * kCanvasHeight * 4);
    glReadPixels(0, 0, kCanvasWidth, kCanvasHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    assert(glGetError() == GL_NO_ERROR);
    return pixels;
}
void Save(const std::filesystem::path &path)
{
    auto pixels = Pixels();
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
    info.biWidth = kCanvasWidth;
    info.biHeight = kCanvasHeight;
    info.biPlanes = 1;
    info.biBitCount = 32;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<char *>(&header), sizeof(header));
    output.write(reinterpret_cast<char *>(&info), sizeof(info));
    output.write(reinterpret_cast<char *>(pixels.data()), pixels.size());
    assert(output.good());
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
                if (currentScroll[i] != previousScroll[i] || currentScroll[i + 1] != previousScroll[i + 1] ||
                    currentScroll[i + 2] != previousScroll[i + 2])
                {
                    ++changed;
                }
            }
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
        assert(gates == 8 && walls > 90);
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
        level.Interact();
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
    capture("chapter-relay.bmp", {30, 32});
    interact(game::ChapterAction::TradeProof);
    interact(game::ChapterAction::Scrappers);
    interact(game::ChapterAction::Ledger);
    interact(game::ChapterAction::DetailLedger);
    s.journalVisible = true;
    capture("chapter-journal.bmp", {32, 32});
    s.journalVisible = false;
    level.Leave();
    level.InteractHub();
    assert(s.comparisonStarted);
    level.Update(8.1f);
    assert(s.recordsCompared);
    capture("chapter-comparison.bmp", kCityReceiverPosition, true);
    std::ofstream snapshot(output / "chapter-snapshot.json");
    snapshot << monitoring::WorldSnapshot::Build(1, "chapter-render");
    game.Reset();
}

void BenchmarkQuality(const std::filesystem::path &output)
{
    std::ofstream report(output / "performance.json");
    report << "{\"gpu\":\"" << glGetString(GL_RENDERER)
           << "\",\"resolution\":[1280,800],\"optimized\":true,\"warmup\":24,\"frames\":120,\"scenes\":[";
    auto &world = Game::Get().GetState();
    for (int scenario = 0; scenario < 10; ++scenario)
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
                                         "frontier_relay_combat"};
        if (scenario == 0)
        {
            world.isInsideHome = false;
            world.playerPosition = kLiaPosition;
            world.cameraPosition = kLiaPosition;
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
        if (scenario >= 7)
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
        bool npcShotsObserved = false;
        std::vector<double> samples;
        double draws = 0, instanced = 0, triangles = 0;
        for (int frame = 0; frame < 144; ++frame)
        {
            const auto previousDraws = profiling::ReadCounter(profiling::Counter::DrawCalls);
            auto start = std::chrono::steady_clock::now();
            profiling::BeginFrame(names[scenario]);
            world.animationTimeSeconds += 1.f / 60;
            auto npcShotsBefore = profiling::ReadCounter(profiling::Counter::NpcShots);
            if (auto *level = game::GameLevels::Active())
            {
                level->Update(1.f / 60);
            }
            npcShotsObserved |= profiling::ReadCounter(profiling::Counter::NpcShots) > npcShotsBefore;
            game::SceneManager::Update(1.f / 60);
            Begin();
            if (scenario < 3)
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
    TestModelsAndRig();
    TestHairAndBodyShape();
    TestCityAndTravel();
    TestScaleGenderAndDistrict();
    TestScrolling();
    OffscreenContext context;
    GameRenderer::Get().Initialize();
    TestDepth();
    TestEffectDepth();
    TestQuestAndResources();
    std::filesystem::path output = argc > 1 ? argv[1] : "x64/ModelChecks/renders";
    std::filesystem::create_directories(output);
    TestCityCoverage(output);
    CaptureScenes(output);
    TestPixelPresentation(output);
    QualityCaptures(output);
    CaptureDistrictAndBodies(output);
    CaptureHairAndCombat(output);
    CaptureChapter(output);
    BenchmarkQuality(output);
    GameRenderer::Get().Shutdown();
    std::cout << "Resource GLB meshes, 23-bone skins, nine wardrobes, direct driving, billboard effects, "
                 "cartoon lighting, "
                 "pixel presentation and scene checks passed.\n";
}
