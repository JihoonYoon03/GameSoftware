#include "stdafx.h"
#include "Drawing.h"
#include "Scene.h"
#include "Hud.h"
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
        assert(std::fabs(muzzle.x - right.x - .22f) < .0001f);
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
    // Steering rotates around world up: the nose turns, without roll or pitch.
    for (float sign : {-1.f, 1.f})
    {
        Transform3D attitude;
        attitude.yaw = sign * kPi / 6;
        auto nose = attitude.Apply({1, 0, 0}), side = attitude.Apply({0, 1, 0});
        assert(std::fabs(nose.z) < .00001f && std::fabs(side.z) < .00001f);
        assert(std::fabs(std::atan2(nose.y, nose.x) - sign * kPi / 6) < .0001f);
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
void TestHairAndBodyShape()
{
    const auto &library = ModelLibrary::Get();
    for (size_t i = 0; i < library.hairs.size(); ++i)
    {
        const auto &hair = library.hairs[i];
        assert(ModelFactory::ValidateModel(hair));
        assert(ModelFactory::ValidateModel(library.coveredHairs[i]));
        for (size_t part = 0; part < hair.count; ++part)
        {
            assert(hair.parts[part].joint == Joint::Head);
        }
        for (size_t j = 0; j < i; ++j)
        {
            const auto &other = library.hairs[j];
            bool different = hair.count != other.count;
            for (size_t part = 0; part < (std::min)(hair.count, other.count) && !different; ++part)
            {
                different = Distance3D(hair.parts[part].points[0], other.parts[part].points[0]) > .001f;
            }
            assert(different);
        }
    }
    // Compare bare torso geometry and joint spacing: neither check depends on hair or palette.
    float waist[2] = {};
    for (int gender = 0; gender < 2; ++gender)
    {
        const auto &body = ModelLibrary::Character(CharacterType::Explorer, Gender(gender));
        for (size_t i = 0; i < body.count; ++i)
        {
            const auto &part = body.parts[i];
            if (part.joint != Joint::Root || !part.clothing)
            {
                continue;
            }
            for (size_t j = 0; j < part.count; ++j)
            {
                if (std::fabs(part.points[j].z - .755f) < .001f)
                {
                    waist[gender] = (std::max)(waist[gender], std::fabs(part.points[j].y));
                }
            }
        }
        auto pose = ModelRig::Pose(0, false, true, 0, false, Gender(gender));
        assert(Distance3D(pose.Apply({0, 0, -.27f}, Joint::RightForearm), {.36f, 0, 26.f / 44}) < .0001f);
    }
    assert(waist[1] < waist[0] * .8f);
    auto male = ModelRig::Pose(0, false, false, 0, false, Gender::Male);
    auto female = ModelRig::Pose(0, false, false, 0, false, Gender::Female);
    assert(std::fabs(female.joints[size_t(Joint::RightArm)].position.y) <
           std::fabs(male.joints[size_t(Joint::RightArm)].position.y));
    assert(std::fabs(female.joints[size_t(Joint::RightLeg)].position.y) >
           std::fabs(male.joints[size_t(Joint::RightLeg)].position.y));
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
                    float x = p.x + v.x, y = p.y + v.y;
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
    assert(travel.yawRadians < -.51f && travel.yawRadians >= -kPi / 6);
    world.pressedKeys['w'] = false;
    for (int i = 0; i < 80; ++i)
    {
        game::GameLevels::Travel().Update(.025f);
    }
    assert(std::fabs(travel.yawRadians) < .001f);
    game::GameLevels::Reset();
}
void TestScaleGenderAndDistrict()
{
    const auto &library = ModelLibrary::Get();
    constexpr Point dimensions[] = {{4.3f, 1.7f, 1.35f},
                                    {5.6f, 1.85f, 2.1f},
                                    {5.2f, 1.9f, 2.f},
                                    {4.6f, 1.75f, 1.55f},
                                    {2.5f, .95f, 1.05f}};
    for (size_t index = 0; index < library.vehicles.size(); ++index)
    {
        const auto &model = library.vehicles[index].body;
        Point lo = {100, 100, 100}, hi = {-100, -100, -100};
        for (size_t i = 0; i < model.count; ++i)
        {
            for (size_t j = 0; j < model.parts[i].count; ++j)
            {
                auto p = model.parts[i].points[j];
                lo.x = (std::min)(lo.x, p.x);
                lo.y = (std::min)(lo.y, p.y);
                lo.z = (std::min)(lo.z, p.z);
                hi.x = (std::max)(hi.x, p.x);
                hi.y = (std::max)(hi.y, p.y);
                hi.z = (std::max)(hi.z, p.z);
            }
        }
        Point size = {(hi.x - lo.x) * 2, (hi.y - lo.y) * 2, (hi.z - lo.z) * 2};
        assert(Distance3D(size, dimensions[index]) < .001f);
    }
    for (Gender gender : {Gender::Male, Gender::Female})
    {
        for (int role = 0; role < 5; ++role)
        {
            const auto &model = ModelLibrary::Character(CharacterType(role), gender);
            assert(ModelFactory::ValidateModel(model));
            auto pose = ModelRig::Pose(0, false, false, 0, false, gender);
            float top = -100, bottom = 100;
            for (size_t i = 0; i < model.count; ++i)
            {
                for (size_t j = 0; j < model.parts[i].count; ++j)
                {
                    const auto &part = model.parts[i];
                    if (part.joint == Joint::Weapon)
                    {
                        continue;
                    }
                    auto p = pose.Apply(part.points[j], part.joint);
                    top = (std::max)(top, p.z);
                    bottom = (std::min)(bottom, p.z);
                }
            }
            float height = (top - bottom) * WorldScale::characterScale * WorldScale::metersPerUnit;
            assert(height > 1.79f && height < 1.95f); // 180cm barefoot, plus role-specific headgear.
            if (role == 0)
            {
                assert(std::fabs(height - 1.8f) < .002f);
            }
            for (int frame = 0; frame < 32; ++frame)
            {
                auto moving = ModelRig::Pose(frame * .2f, true, true, 0, false, gender);
                float face = moving.Apply({0, 0, 0}, Joint::Head).z;
                for (size_t i = 0; i < model.count; ++i)
                {
                    if (model.parts[i].clothing && model.parts[i].joint != Joint::Head)
                    {
                        for (size_t j = 0; j < model.parts[i].count; ++j)
                        {
                            assert(moving.Apply(model.parts[i].points[j], model.parts[i].joint).z <
                                   face - .015f);
                        }
                    }
                }
            }
        }
    }
    assert(&ModelLibrary::Character(CharacterType::Explorer, Gender::Female) != &library.characters[0]);
    assert(ModelFactory::ValidateModel(library.bulkhead) && ModelFactory::ValidateModel(library.trafficGate));
    assert(CityHud::ContainsMinimapBuilding({-8, -5, 2, 3, 150}));
    assert(!CityHud::ContainsMinimapBuilding({kWorldMaxX - 1, 0, 2, 2, 150}));
    assert(!CityHud::ContainsMinimapBuilding({35, 25, 3, 3, 150}));
    // A traffic wrap is permitted only when the entire vehicle has left the extended terrain.
    for (int car = 0; car < 6; ++car)
    {
        for (int frame = 0; frame < 2500; ++frame)
        {
            auto p = CityLayout::TrafficPosition(car, frame * .05f),
                 q = CityLayout::TrafficPosition(car, (frame + 1) * .05f);
            if (std::fabs(q.x - p.x) > 1)
            {
                assert(std::fabs(p.x) > CityLayout::kTerrainMaxX + 2 &&
                       std::fabs(q.x) > CityLayout::kTerrainMaxX + 2);
            }
        }
        float crossing = (18 - (-20 + (car / 2) * 13.333333f)) / 2.4f;
        auto a = CityLayout::TrafficPosition(car, crossing - .001f),
             b = CityLayout::TrafficPosition(car, crossing + .001f);
        assert(std::fabs(b.x - a.x) < .01f);
    }
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
    for (float yaw : {-kPi / 6, 0.f, kPi / 6})
    {
        auto &travel = game::GameLevels::Travel().GetState();
        travel.yawRadians = yaw;
        Begin();
        game::SceneManager::Get(game::SceneKind::TravelBackground).Draw();
        renderqueue::Flush();
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        game::SceneManager::Get(game::SceneKind::JetTravel).Draw();
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
        auto &travel = game::GameLevels::Travel().GetState();
        travel.distance = float(240.0 / 4.5 + (frame ? .001 : -.001));
        travel.yawRadians = 0;
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
    world.cameraPosition = world.playerPosition = {-3, 2};
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
void BenchmarkQuality(const std::filesystem::path &output)
{
    std::ofstream report(output / "performance.json");
    report << "{\"gpu\":\"" << glGetString(GL_RENDERER)
           << "\",\"resolution\":[1280,800],\"optimized\":true,\"warmup\":24,\"frames\":120,\"scenes\":[";
    auto &world = Game::Get().GetState();
    for (int scenario = 0; scenario < 7; ++scenario)
    {
        Game::Get().Reset();
        world.noticeSecondsRemaining = 0;
        constexpr const char *names[] = {"city",
                                         "observatory",
                                         "bike",
                                         "mine_entry",
                                         "mine_ruins",
                                         "mine_90_enemies",
                                         "npc_firefight_90_enemies"};
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
    BenchmarkQuality(output);
    GameRenderer::Get().Shutdown();
    std::cout
        << "3D rig, five wardrobes, yaw steering, continuous scrolling, billboard effects, cartoon lighting, "
           "pixel presentation and scene checks passed.\n";
}
