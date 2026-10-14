#include "stdafx.h"
#include "Drawing.h"
#include "Scene.h"
#include "Level.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include <chrono>
#include "Monitoring/Monitor.h"
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cmath>
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
    for (const auto &v : library.vehicles)
    {
        assert(v.body.space == CoordinateSpace::World && ModelFactory::ValidateModel(v.body));
        float minZ = 100, maxZ = -100;
        for (size_t i = 0; i < v.body.count; ++i)
        {
            for (size_t k = 0; k < v.body.parts[i].count; ++k)
            {
                minZ = (std::min)(minZ, v.body.parts[i].points[k].z);
                maxZ = (std::max)(maxZ, v.body.parts[i].points[k].z);
            }
        }
        assert(maxZ - minZ > .4f);
    }
    for (const auto &m : library.buildings)
    {
        assert(ModelFactory::ValidateModel(m));
    }
    for (const auto &m : library.characters)
    {
        assert(m.space == CoordinateSpace::World && ModelFactory::ValidateModel(m));
    }
    for (const auto *m : {&library.box, &library.drone, &library.salvage, &library.recoveryKit, &library.rock,
                          &library.telescope, &library.console})
    {
        assert(ModelFactory::ValidateModel(*m));
    }
    for (int i = 0; i < 64; ++i)
    {
        auto pose = ModelRig::Pose(i * .2f, true, true);
        auto right = pose.Apply({0, 0, -.27f}, Joint::RightForearm);
        auto left = pose.Apply({0, 0, -.27f}, Joint::LeftForearm);
        assert(Distance3D(right, {.36f, 0, 26.f / 44}) < .0001f);
        assert(Distance3D(right, left) < .0001f);
        auto muzzle = ModelRig::Muzzle(pose);
        assert(std::fabs(muzzle.x - right.x - .34f) < .0001f);
        auto recoil = ModelRig::Pose(i * .2f, true, true, 1);
        assert(ModelRig::Muzzle(recoil).x < muzzle.x - .04f);
        // No torso/sleeve vertex reaches the face, for any walking/aiming frame.
        for (bool armed : {false, true})
        {
            auto p = ModelRig::Pose(i * .2f, true, armed);
            float faceBottom = p.Apply({0, 0, 0}, Joint::Head).z;
            for (const auto &m : library.characters)
            {
                for (size_t j = 0; j < m.count; ++j)
                {
                    const auto &part = m.parts[j];
                    if (!part.clothing || part.joint == Joint::Head)
                    {
                        continue;
                    }
                    for (size_t k = 0; k < part.count; ++k)
                    {
                        assert(p.Apply(part.points[k], part.joint).z < faceBottom - .015f);
                    }
                }
            }
        }
    }
    // Banking tilts the sides around the forward axis; the nose stays level.
    for (float sign : {-1.f, 1.f})
    {
        Transform3D attitude;
        attitude.roll = sign * kPi / 6;
        auto nose = attitude.Apply({1, 0, 0}), side = attitude.Apply({0, 1, 0});
        assert(Distance3D(nose, {1, 0, 0}) < .00001f);
        assert(std::fabs(std::atan2(side.z, side.y) - sign * kPi / 6) < .0001f);
    }
    // A shared palette must still yield five structurally different wardrobes.
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        for (size_t j = i + 1; j < library.characters.size(); ++j)
        {
            const auto &a = library.characters[i], &b = library.characters[j];
            bool different = a.count != b.count;
            for (size_t k = 0; k < (std::min)(a.count, b.count) && !different; ++k)
            {
                different = a.parts[k].palette != b.parts[k].palette ||
                            Distance3D(a.parts[k].points[0], b.parts[k].points[0]) > .001f;
            }
            assert(different);
        }
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
    for (int t = 0; t < 500; ++t)
    {
        for (int car = 0; car < 6; ++car)
        {
            auto p = CityLayout::TrafficPosition(car, t * .05f);
            auto next = CityLayout::TrafficPosition(car, t * .05f + .001f);
            Transform3D transform;
            transform.yaw = CityLayout::TrafficYaw(car);
            auto nose = transform.Apply({1, 0, 0});
            assert(std::fabs(p.y - next.y) < .00001f);
            if (std::fabs(next.x - p.x) < 1)
            {
                assert((next.x - p.x) * nose.x > 0);
            }
            // Check every vehicle vertex against every physical building footprint.
            const auto &model = library.vehicles[size_t(car % 4)].body;
            for (size_t j = 0; j < model.count; ++j)
            {
                for (size_t k = 0; k < model.parts[j].count; ++k)
                {
                    auto v = transform.Apply(model.parts[j].points[k]);
                    float x = p.x + v.x * .8f, y = p.y + v.y * .8f;
                    assert(y > -1 && y < 1);
                    for (const auto &b : world.buildings)
                    {
                        assert(!(x > b.x && x < b.x + b.width && y > b.y && y < b.y + b.depth));
                    }
                }
            }
        }
    }
    auto &quest = game::GameLevels::Mine().GetState().quest;
    assert(quest.Advance(game::MineQuestStage::ShowRecord));
    assert(quest.Advance(game::MineQuestStage::CheckCityRelay));
    game::GameLevels::Travel().Enter();
    auto &travel = game::GameLevels::Travel().GetState();
    for (int i = 0; i < 48; ++i)
    {
        world.pressedKeys['w'] = true;
        travel.lateralOffset = 0; // Exercise sustained steering before reaching the road edge.
        game::GameLevels::Travel().Update(.025f);
    }
    assert(travel.rollRadians > .51f && travel.rollRadians <= kPi / 6);
    world.pressedKeys['w'] = false;
    for (int i = 0; i < 80; ++i)
    {
        game::GameLevels::Travel().Update(.025f);
    }
    assert(std::fabs(travel.rollRadians) < .001f);
    game::GameLevels::Reset();
}

void TestScrolling()
{
    using game::TravelScenery;
    assert(TravelScenery::kRoadScroll >= 12 && TravelScenery::kForegroundScroll > TravelScenery::kRoadScroll);
    for (float rate : {.5f, 1.9f, 4.5f, TravelScenery::kRoadScroll, TravelScenery::kForegroundScroll})
    {
        for (int boundary = 1; boundary < 80; ++boundary)
        {
            double distance = boundary * 200.0 / rate;
            auto before = TravelScenery::VisibleTiles(distance - .001, rate, 200, 280);
            auto after = TravelScenery::VisibleTiles(distance + .001, rate, 200, 280);
            assert(!before.empty() && before.front().x <= -280 && before.back().x >= kCanvasWidth + 280);
            for (const auto &a : before)
            {
                for (const auto &b : after)
                {
                    if (a.id != b.id)
                    {
                        continue;
                    }
                    assert(std::fabs((b.x - a.x) + .002 * rate) < .001);
                    assert(TravelScenery::Variant(a.id, 2) == TravelScenery::Variant(b.id, 2));
                }
            }
        }
    }
    assert(TravelScenery::VisibleTiles(0, 1, 0).empty());
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
    glOrtho(0, kCanvasWidth, kCanvasHeight, 0, -8192, 8192);
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
void CaptureScenes(const std::filesystem::path &output)
{
    auto &world = Game::Get().GetState();
    Game::Get().Reset();
    world.isInsideHome = false;
    world.playerPosition = {-3, 2};
    world.cameraPosition = {-3, 2};
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
    game::GameLevels::Travel().Enter();
    for (float roll : {-kPi / 6, 0.f, kPi / 6})
    {
        auto &travel = game::GameLevels::Travel().GetState();
        travel.rollRadians = roll;
        Begin();
        game::SceneManager::Get(game::SceneKind::TravelBackground).Draw();
        renderqueue::Flush();
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        game::SceneManager::Get(game::SceneKind::JetTravel).Draw();
        GameRenderer::Get().Backend().EndScene(0);
        Save(output / (roll < 0   ? "bike-bank-left.bmp"
                       : roll > 0 ? "bike-bank-right.bmp"
                                  : "bike-neutral.bmp"));
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
        auto &travel = game::GameLevels::Travel().GetState();
        travel.distance = float(240.0 / 4.5 + (frame ? .001 : -.001));
        travel.rollRadians = 0;
        Begin();
        game::SceneManager::Get(game::SceneKind::TravelBackground).Draw();
        renderqueue::Flush();
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        game::SceneManager::Get(game::SceneKind::JetTravel).Draw();
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
    world.isJetTravel = false;
    world.isInsideHome = false;
    world.playerPosition = {-3, 2};
    world.cameraPosition = {-3, 2};
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
    world.playerPosition = {-3, 2};
    world.cameraPosition = {-3, 2};
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
    const auto *car0 = graph.Find("vehicle/0"), *car4 = graph.Find("vehicle/4");
    assert(car0 && car4 && !car0->GetRenderObjects().empty() && !car4->GetRenderObjects().empty());
    assert(car0->GetRenderObjects()[0].mesh == car4->GetRenderObjects()[0].mesh);
    assert(car0->GetRenderObjects()[0].material && car4->GetRenderObjects()[0].material);
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
void BenchmarkQuality(const std::filesystem::path &output)
{
    std::ofstream report(output / "performance.json");
    report << "{\"gpu\":\"" << glGetString(GL_RENDERER)
           << "\",\"resolution\":[1280,800],\"optimized\":true,\"warmup\":24,\"frames\":120,\"scenes\":[";
    auto &world = Game::Get().GetState();
    for (int scenario = 0; scenario < 6; ++scenario)
    {
        Game::Get().Reset();
        world.noticeSecondsRemaining = 0;
        constexpr const char *names[] = {"city",       "observatory", "bike",
                                         "mine_entry", "mine_ruins",  "mine_90_enemies"};
        if (scenario == 0)
        {
            world.isInsideHome = false;
            world.playerPosition = {-3, 2};
            world.cameraPosition = {-3, 2};
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
            game::GameLevels::Travel().Enter();
        }
        if (scenario >= 3)
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
        std::vector<double> samples;
        double draws = 0, instanced = 0, triangles = 0;
        for (int frame = 0; frame < 144; ++frame)
        {
            const auto previousDraws = profiling::ReadCounter(profiling::Counter::DrawCalls);
            auto start = std::chrono::steady_clock::now();
            profiling::BeginFrame(names[scenario]);
            world.animationTimeSeconds += 1.f / 60;
            if (auto *level = game::GameLevels::Active())
            {
                level->Update(1.f / 60);
            }
            game::SceneManager::Update(1.f / 60);
            Begin();
            if (scenario < 3)
            {
                game::SceneManager::Get(scenario == 2 ? game::SceneKind::TravelBackground
                                                      : game::SceneKind::Background)
                    .Draw();
                renderqueue::Flush();
                glDepthMask(GL_TRUE);
                glClear(GL_DEPTH_BUFFER_BIT);
            }
            game::SceneManager::Get(game::SceneManager::Current()).Draw();
            GameRenderer::Get().Backend().EndScene(world.animationTimeSeconds);
            if (scenario >= 3)
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
    TestCityAndTravel();
    TestScrolling();
    OffscreenContext context;
    GameRenderer::Get().Initialize();
    TestDepth();
    TestQuestAndResources();
    std::filesystem::path output = argc > 1 ? argv[1] : "x64/ModelChecks/renders";
    std::filesystem::create_directories(output);
    CaptureScenes(output);
    TestPixelPresentation(output);
    QualityCaptures(output);
    BenchmarkQuality(output);
    GameRenderer::Get().Shutdown();
    std::cout
        << "3D rig, five wardrobes, banking, continuous scrolling, billboard effects, cartoon lighting, "
           "pixel presentation and scene checks passed.\n";
}
