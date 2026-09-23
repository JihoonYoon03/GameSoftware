#pragma once
#include <cstdint>
#include <functional>
#include <memory>

namespace renderqueue
{
class Mesh;
using MeshPtr = std::shared_ptr<Mesh>;
void BeginFrame();
void Shutdown();
void Flush();
void Begin(unsigned int mode);
void End();
void Vertex(float x, float y, float z = 0);
void Color(float r, float g, float b, float a);
void TexCoord(float u, float v);
void Texture(unsigned int texture);
void SolidSample(unsigned int texture, float u, float v);
void Effect(unsigned int program, int kind, float seconds);
void LineWidth(float width);
void PushMatrix();
void PopMatrix();
void Translate(float x, float y, float z);
void Scale(float x, float y, float z);
void LoadIdentity();
MeshPtr Capture(const std::function<void()> &draw);
void DrawMesh(const MeshPtr &mesh);
} // namespace renderqueue
