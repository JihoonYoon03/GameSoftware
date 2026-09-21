#include "stdafx.h"
#include "Tutorial.h"
#include "DrawCallCounter.h"
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
    models::Get();
    renderer.reset(new Renderer(kCanvasWidth, kCanvasHeight));
}
void ShutdownGraphics()
{
    ResetWorldScenes();
    renderer.reset();
}

void Draw()
{
    if (!renderer)
    {
        return;
    }
    renderdebug::BeginFrame();
    renderer->BeginScene(g_state.viewportWidth, g_state.viewportHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kCanvasWidth, kCanvasHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawScreenLayer(WorldSceneKind::Background, "background", DrawBackdrop);
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
    renderer->EndScene(g_state.animationTimeSeconds);
    DrawScreenLayer(WorldSceneKind::Hud, "hud", DrawHud);
    glutSwapBuffers();
    renderdebug::EndFrame();
}
} // namespace tutorial
