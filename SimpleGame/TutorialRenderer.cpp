#include "stdafx.h"
#include "Tutorial.h"
#include "DrawCallCounter.h"
#include "Profiler.h"
#include "RenderQueue.h"
#include "RenderCache.h"
#include "TutorialDrawing.h"
#include "TutorialGraphics.h"
#include "LevelOne.h"
#include "Scene/WorldScene.h"
#include <memory>

namespace tutorial
{
namespace
{
std::unique_ptr<Renderer> renderer;
void DrawScreenLayer(WorldSceneKind kind, const char *name, void (*draw)())
{
    auto &scene = GetWorldScene(kind);
    scene.BeginPlacement();
    scene.Place(name, "screen", {0, 0, float(kCanvasWidth), float(kCanvasHeight)}, 0, 0,
                [draw](const game::Actor &) { draw(); });
    scene.EndPlacement();
    for (const auto *actor :
         scene.GetGraph().CollectVisible({0, 0, float(kCanvasWidth), float(kCanvasHeight)}))
    {
        actor->Draw();
    }
}
} // namespace
Renderer &GetRenderer()
{
    return *renderer;
}
void InitializeGraphics()
{
    profiling::Initialize();
    profiling::Scope timer(profiling::Timer::ResourceInitialize);
    models::Get();
    renderer.reset(new Renderer(kCanvasWidth, kCanvasHeight));
}
void ShutdownGraphics()
{
    ResetWorldScenes();
    rendercache::Clear();
    renderqueue::Shutdown();
    renderer.reset();
    profiling::Shutdown();
}

void Draw()
{
    if (!renderer)
    {
        return;
    }
    profiling::BeginFrame(levelone::IsActive() ? "hunting"
                                               : (g_state.isInsideHome ? "interior" : "exterior"));
    renderqueue::BeginFrame();
    renderer->BeginScene(g_state.viewportWidth, g_state.viewportHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kCanvasWidth, kCanvasHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    {
        profiling::Scope timer(profiling::Timer::Background);
        profiling::GpuScope gpuTimer(profiling::Timer::Background);
        DrawScreenLayer(WorldSceneKind::Background, "background", DrawBackdrop);
        renderqueue::Flush();
    }
    {
        profiling::Scope timer(profiling::Timer::World);
        profiling::GpuScope gpuTimer(profiling::Timer::World);
        if (levelone::IsActive())
        {
            levelone::DrawWorld();
        }
        else if (g_state.isInsideHome)
        {
            DrawInterior();
        }
        else
        {
            DrawExterior();
        }
        renderqueue::Flush();
    }
    renderer->EndScene(g_state.animationTimeSeconds);
    {
        profiling::Scope timer(profiling::Timer::Hud);
        profiling::GpuScope gpuTimer(profiling::Timer::Hud);
        DrawScreenLayer(WorldSceneKind::Hud, "hud", DrawHud);
        renderqueue::Flush();
    }
    profiling::EndGpuFrame();
    {
        profiling::Scope timer(profiling::Timer::Present);
        glutSwapBuffers();
    }
    profiling::EndFrame();
}
} // namespace tutorial
