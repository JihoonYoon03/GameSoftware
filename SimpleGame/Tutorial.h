#pragma once

// GLUT callback interface. Game state and OpenGL implementation stay internal.
namespace tutorial
{
void Reset();
void Draw();
void Resize(int width, int height);
void KeyDown(unsigned char key, int mouseX, int mouseY);
void KeyUp(unsigned char key, int mouseX, int mouseY);
void Tick(int timerValue);
} // namespace tutorial
