#include "stdafx.h"
#include "LevelOne.h"
#include "TutorialDrawing.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include <vector>

namespace levelone
{
void DrawDarkness()
{
    using namespace tutorial;
    profiling::Scope timer(profiling::Timer::MineLighting);
    // A smooth vertex mask works with the existing compatibility renderer and its fallback.
    // Applied AFTER bloom: concealed rooms cannot leak bright sprites or post-process glow.
    constexpr int step = 16;
    constexpr int columns = kCanvasWidth / step + 1;
    constexpr int rows = kCanvasHeight / step + 1;
    std::vector<float> alpha(columns * rows);
    auto origin = WorldToScreen(0, 0);
    for (int y = 0; y < rows; ++y)
    {
        for (int x = 0; x < columns; ++x)
        {
            float sx = (x * step - origin.x) / 36;
            float sy = (y * step - origin.y) / 18;
            tutorial::Vector2 world = {(sx + sy) * .5f, (sy - sx) * .5f};
            alpha[y * columns + x] = 1 - LightAt(world);
        }
    }
    renderqueue::Texture(0);
    renderqueue::Effect(0, 0, 0);
    renderqueue::Begin(GL_QUADS);
    for (int y = 0; y < rows - 1; ++y)
    {
        for (int x = 0; x < columns - 1; ++x)
        {
            const int corners[4][2] = {{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}};
            for (const auto &corner : corners)
            {
                renderqueue::Color(0, 0, 0, alpha[corner[1] * columns + corner[0]]);
                renderqueue::Vertex(float(corner[0] * step), float(corner[1] * step));
            }
        }
    }
    renderqueue::End();
}
} // namespace levelone
