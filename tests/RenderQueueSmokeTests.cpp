#include "stdafx.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "Profiler.h"
#include "RenderQueue.h"
#include <array>
#include <cassert>
#include <cstdio>

namespace
{
void Quad(float x, float y)
{
    renderqueue::Begin(GL_QUADS);
    renderqueue::Vertex(x, y);
    renderqueue::Vertex(x + 8, y);
    renderqueue::Vertex(x + 8, y + 8);
    renderqueue::Vertex(x, y + 8);
    renderqueue::End();
}
std::array<unsigned char, 4> Pixel(int x, int y)
{
    std::array<unsigned char, 4> result = {};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}
void Clear()
{
    renderqueue::Flush();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
} // namespace
int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_SINGLE);
    glutInitWindowSize(64, 64);
    int window = glutCreateWindow("Render queue smoke tests");
    glutHideWindow();
    const GLenum glewResult = glewInit();
    assert(glewResult == GLEW_OK);
    if (!GLEW_VERSION_3_0)
    {
        std::fprintf(stderr, "Offscreen smoke tests require OpenGL 3.0.\n");
        glutDestroyWindow(window);
        return 77;
    }
    // Some compatibility drivers leave an error during extension discovery.
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glViewport(0, 0, 64, 64);
    GLuint framebuffer = 0, target = 0;
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(framebufferStatus == GL_FRAMEBUFFER_COMPLETE);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLuint white = 0;
    const unsigned char texel[] = {255, 255, 255, 255};
    glGenTextures(1, &white);
    glBindTexture(GL_TEXTURE_2D, white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    renderqueue::SolidSample(white, .5f, .5f);
    renderqueue::BeginFrame();
    Clear();
    auto before = profiling::ReadCounter(profiling::Counter::DrawCalls);
    renderqueue::Color(1, 0, 0, 1);
    Quad(0, 0);
    Quad(16, 0);
    renderqueue::Flush();
    assert(profiling::ReadCounter(profiling::Counter::DrawCalls) - before == 1);
    assert(Pixel(4, 4)[0] > 250);
    assert(Pixel(12, 4)[0] == 0);
    assert(Pixel(20, 4)[0] > 250);

    // Preserve translucent primitive order inside a merged batch.
    Clear();
    renderqueue::Color(1, 0, 0, .5f);
    Quad(0, 0);
    renderqueue::Color(0, 0, 1, .5f);
    Quad(0, 0);
    renderqueue::Flush();
    auto blended = Pixel(4, 4);
    assert(blended[0] >= 60 && blended[0] <= 68);
    assert(blended[2] >= 124 && blended[2] <= 132);

    // GPU-cached geometry must follow each instance's transform, not capture-time camera state.
    Clear();
    auto mesh = renderqueue::Capture([]() {
        renderqueue::Color(1, 0, 0, 1);
        Quad(0, 0);
    });
    before = profiling::ReadCounter(profiling::Counter::DrawCalls);
    for (int x : {8, 32})
    {
        renderqueue::PushMatrix();
        renderqueue::Translate(float(x), 16, 0);
        renderqueue::DrawMesh(mesh);
        renderqueue::PopMatrix();
    }
    renderqueue::Flush();
    auto draws = profiling::ReadCounter(profiling::Counter::DrawCalls) - before;
    assert(draws >= 1 && draws <= 2); // Instancing or ordered fallback.
    assert(Pixel(12, 20)[0] > 250);
    assert(Pixel(36, 20)[0] > 250);
    assert(Pixel(24, 20)[0] == 0);
    assert(glGetError() == GL_NO_ERROR);
    mesh.reset();
    renderqueue::Shutdown();
    glDeleteTextures(1, &white);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &target);
    glutDestroyWindow(window);
}
