#include "stdafx.h"
#include "Tutorial.h"
#include "TutorialDrawing.h"
#include "TutorialGraphics.h"
#include "LevelOne.h"
#include <memory>

namespace tutorial
{
namespace
{
std::unique_ptr<Renderer> renderer;
}
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
    renderer.reset();
}

void Draw()
{
    if (!renderer)
    {
        return;
    }
    renderer->BeginScene(g_state.viewportWidth, g_state.viewportHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kCanvasWidth, kCanvasHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawBackdrop();
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
    DrawHud();
    glutSwapBuffers();
}
} // namespace tutorial
