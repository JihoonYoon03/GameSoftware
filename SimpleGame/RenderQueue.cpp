#include "stdafx.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include "Dependencies/glew.h"
#include <array>
#include <vector>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace renderqueue
{
namespace
{
struct VertexData
{
    float x, y, z, r, g, b, a, u, v;
};
struct Transform
{
    float x = 0, y = 0, z = 0, sx = 1, sy = 1, sz = 1;
};
struct State
{
    GLuint texture = 0, program = 0;
    int effect = 0;
    float seconds = 0;
    bool operator==(const State &other) const
    {
        return texture == other.texture && program == other.program && effect == other.effect &&
               seconds == other.seconds;
    }
};
struct Range
{
    State state;
    size_t first = 0, count = 0;
};
struct Geometry
{
    std::vector<VertexData> vertices;
    std::vector<Range> ranges;
    std::uint64_t primitiveCount = 0;
};
Geometry pending;
Geometry *capture = nullptr;
std::vector<VertexData> primitive;
std::vector<Transform> transforms(1);
State state;
std::array<float, 4> color = {1, 1, 1, 1};
float u = 0, v = 0, lineWidth = 1, solidU = .5f, solidV = .5f;
GLuint solidTexture = 0, streamBuffer = 0;
GLenum mode = GL_TRIANGLES;
constexpr size_t kMaximumBatchVertices = 65536;
MeshPtr instanceMesh;
std::vector<Transform> instances;
GLuint instanceProgram = 0, instanceBuffer = 0;
bool instanceInitialized = false;
void FlushInstances();

void InitializeInstances()
{
    if (instanceInitialized)
    {
        return;
    }
    instanceInitialized = true;
    if (!GLEW_VERSION_3_3)
    {
        return;
    }
    profiling::Scope timer(profiling::Timer::ShaderCompile);
    const char *sources[] = {
        "#version 120\nattribute vec3 instancePosition; attribute vec3 instanceScale;"
        "attribute vec3 vertexPosition; attribute vec4 vertexColor; attribute vec2 vertexUv;"
        "varying vec4 tint; varying vec2 uv; void main(){"
        "gl_Position=gl_ModelViewProjectionMatrix*vec4(vertexPosition*instanceScale+instancePosition,1.0);"
        "tint=vertexColor; uv=vertexUv;}",
        "#version 120\nuniform sampler2D image; varying vec4 tint; varying vec2 uv;"
        "void main(){gl_FragColor=texture2D(image,uv)*tint;}"};
    GLuint shaders[] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
    bool valid = shaders[0] && shaders[1];
    for (int i = 0; i < 2 && valid; ++i)
    {
        glShaderSource(shaders[i], 1, &sources[i], nullptr);
        glCompileShader(shaders[i]);
        GLint compiled = 0;
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &compiled);
        valid = compiled != 0;
    }
    if (valid)
    {
        instanceProgram = glCreateProgram();
        glAttachShader(instanceProgram, shaders[0]);
        glAttachShader(instanceProgram, shaders[1]);
        glBindAttribLocation(instanceProgram, 0, "vertexPosition");
        glBindAttribLocation(instanceProgram, 1, "vertexColor");
        glBindAttribLocation(instanceProgram, 2, "vertexUv");
        glBindAttribLocation(instanceProgram, 3, "instancePosition");
        glBindAttribLocation(instanceProgram, 4, "instanceScale");
        glLinkProgram(instanceProgram);
        GLint linked = 0;
        glGetProgramiv(instanceProgram, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            glDeleteProgram(instanceProgram);
            instanceProgram = 0;
        }
    }
    for (GLuint shader : shaders)
    {
        if (shader)
        {
            glDeleteShader(shader);
        }
    }
    if (instanceProgram)
    {
        glGenBuffers(1, &instanceBuffer);
    }
}

void ApplyState(const State &value)
{
    if (GLEW_VERSION_2_0)
    {
        glUseProgram(value.program);
    }
    if (GLEW_VERSION_1_3)
    {
        glActiveTexture(GL_TEXTURE0);
    }
    if (value.texture)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, value.texture);
    }
    else
    {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (value.program)
    {
        // Uniform lookups are bounded by submitted batches, not individual effects.
        glUniform1i(glGetUniformLocation(value.program, "effect"), value.effect);
        glUniform1f(glGetUniformLocation(value.program, "seconds"), value.seconds);
    }
}
void DrawGeometry(const Geometry &geometry, GLuint buffer, const Transform &transform)
{
    if (geometry.vertices.empty())
    {
        return;
    }
    glPushMatrix();
    glTranslatef(transform.x, transform.y, transform.z);
    glScalef(transform.sx, transform.sy, transform.sz);
    if (GLEW_VERSION_1_5)
    {
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
    }
    const unsigned char *base =
        buffer ? nullptr : reinterpret_cast<const unsigned char *>(geometry.vertices.data());
    auto address = [base, buffer](size_t offset) -> const void * {
        return buffer ? reinterpret_cast<const void *>(offset) : base + offset;
    };
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (GLEW_VERSION_1_3)
    {
        glClientActiveTexture(GL_TEXTURE0);
    }
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(VertexData), address(offsetof(VertexData, x)));
    glColorPointer(4, GL_FLOAT, sizeof(VertexData), address(offsetof(VertexData, r)));
    glTexCoordPointer(2, GL_FLOAT, sizeof(VertexData), address(offsetof(VertexData, u)));
    for (const auto &range : geometry.ranges)
    {
        ApplyState(range.state);
        glDrawArrays(GL_TRIANGLES, GLint(range.first), GLsizei(range.count));
        profiling::Count(profiling::Counter::DrawCalls);
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (GLEW_VERSION_1_5)
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (GLEW_VERSION_2_0)
    {
        glUseProgram(0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}
State ResolvedState()
{
    State result = state;
    if (!result.texture && !result.program)
    {
        result.texture = solidTexture;
    }
    return result;
}
void Append(VertexData a, VertexData b, VertexData c)
{
    if (!capture && pending.vertices.size() + 3 > kMaximumBatchVertices)
    {
        profiling::Count(profiling::Counter::BatchCapacityBreaks);
        Flush();
    }
    auto &geometry = capture ? *capture : pending;
    State resolved = ResolvedState();
    if (geometry.ranges.empty() || !(geometry.ranges.back().state == resolved))
    {
        if (!geometry.ranges.empty())
        {
            profiling::Count(profiling::Counter::BatchStateBreaks);
        }
        geometry.ranges.push_back({resolved, geometry.vertices.size(), 0});
    }
    geometry.vertices.insert(geometry.vertices.end(), {a, b, c});
    geometry.ranges.back().count += 3;
    if (!capture)
    {
        profiling::Count(profiling::Counter::SubmittedVertices, 3);
    }
}
void AppendLine(VertexData a, VertexData b)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < .0001f)
    {
        return;
    }
    float nx = -dy / length * lineWidth * .5f, ny = dx / length * lineWidth * .5f;
    auto c = b, d = a;
    a.x += nx;
    a.y += ny;
    b.x += nx;
    b.y += ny;
    c.x -= nx;
    c.y -= ny;
    d.x -= nx;
    d.y -= ny;
    Append(a, b, c);
    Append(a, c, d);
}
} // namespace
class Mesh
{
  public:
    Geometry geometry;
    GLuint buffer = 0;
    ~Mesh()
    {
        if (buffer)
        {
            glDeleteBuffers(1, &buffer);
        }
    }
};
namespace
{
void FlushInstances()
{
    if (!instanceMesh)
    {
        return;
    }
    profiling::Scope timer(profiling::Timer::QueueFlush);
    const auto &geometry = instanceMesh->geometry;
    bool compatible = instanceProgram && instanceMesh->buffer && instances.size() > 1;
    for (const auto &range : geometry.ranges)
    {
        compatible = compatible && range.state.program == 0 && range.state.texture != 0;
    }
    // Multi-range meshes must retain object-major painter order. Only a single range may instance.
    compatible = compatible && geometry.ranges.size() == 1;
    if (compatible)
    {
        glBindBuffer(GL_ARRAY_BUFFER, instanceMesh->buffer);
        for (GLuint attribute = 0; attribute < 5; ++attribute)
        {
            glEnableVertexAttribArray(attribute);
        }
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, x)));
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, r)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, u)));
        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(Transform), instances.data(), GL_STREAM_DRAW);
        profiling::Count(profiling::Counter::StreamUploadBytes, instances.size() * sizeof(Transform));
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Transform), nullptr);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Transform),
                              reinterpret_cast<void *>(offsetof(Transform, sx)));
        glVertexAttribDivisor(3, 1);
        glVertexAttribDivisor(4, 1);
        glUseProgram(instanceProgram);
        glUniform1i(glGetUniformLocation(instanceProgram, "image"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, geometry.ranges[0].state.texture);
        glDrawArraysInstanced(GL_TRIANGLES, 0, GLsizei(geometry.vertices.size()), GLsizei(instances.size()));
        profiling::Count(profiling::Counter::DrawCalls);
        profiling::Count(profiling::Counter::InstancedDrawCalls);
        glVertexAttribDivisor(3, 0);
        glVertexAttribDivisor(4, 0);
        for (GLuint attribute = 0; attribute < 5; ++attribute)
        {
            glDisableVertexAttribArray(attribute);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    else
    {
        for (const auto &transform : instances)
        {
            DrawGeometry(geometry, instanceMesh->buffer, transform);
        }
    }
    profiling::Count(profiling::Counter::RenderedInstances, instances.size());
    instances.clear();
    instanceMesh.reset();
}
} // namespace
void BeginFrame()
{
    InitializeInstances();
    pending.vertices.clear();
    pending.ranges.clear();
    transforms.assign(1, {});
    state = {};
    color = {1, 1, 1, 1};
}
void Shutdown()
{
    Flush();
    if (instanceProgram)
    {
        glDeleteProgram(instanceProgram);
        instanceProgram = 0;
        glDeleteBuffers(1, &instanceBuffer);
        instanceBuffer = 0;
    }
    instanceInitialized = false;
    if (streamBuffer)
    {
        glDeleteBuffers(1, &streamBuffer);
        streamBuffer = 0;
    }
    solidTexture = 0;
}
void Flush()
{
    if (capture)
    {
        return;
    }
    FlushInstances();
    if (pending.vertices.empty())
    {
        return;
    }
    profiling::Scope timer(profiling::Timer::QueueFlush);
    if (GLEW_VERSION_1_5)
    {
        if (!streamBuffer)
        {
            glGenBuffers(1, &streamBuffer);
        }
        glBindBuffer(GL_ARRAY_BUFFER, streamBuffer);
        const size_t bytes = pending.vertices.size() * sizeof(VertexData);
        glBufferData(GL_ARRAY_BUFFER, bytes, pending.vertices.data(), GL_STREAM_DRAW);
        profiling::Count(profiling::Counter::StreamUploadBytes, bytes);
    }
    DrawGeometry(pending, streamBuffer, {});
    pending.vertices.clear();
    pending.ranges.clear();
}
void Begin(unsigned int value)
{
    if (!capture)
    {
        FlushInstances();
    }
    mode = value;
    primitive.clear();
    if (capture)
    {
        ++capture->primitiveCount;
    }
    else
    {
        profiling::Count(profiling::Counter::SubmittedPrimitives);
    }
}
void Vertex(float x, float y, float z)
{
    const auto &t = transforms.back();
    bool solid = !state.texture && !state.program;
    primitive.push_back({x * t.sx + t.x, y * t.sy + t.y, z * t.sz + t.z, color[0], color[1], color[2],
                         color[3], solid ? solidU : u, solid ? solidV : v});
}
void End()
{
    if (mode == GL_LINES || mode == GL_LINE_LOOP)
    {
        size_t step = mode == GL_LINES ? 2 : 1;
        for (size_t i = 0; i + 1 < primitive.size(); i += step)
        {
            AppendLine(primitive[i], primitive[i + 1]);
        }
        if (mode == GL_LINE_LOOP && primitive.size() > 1)
        {
            AppendLine(primitive.back(), primitive.front());
        }
    }
    else if (mode == GL_QUADS)
    {
        for (size_t i = 0; i + 3 < primitive.size(); i += 4)
        {
            Append(primitive[i], primitive[i + 1], primitive[i + 2]);
            Append(primitive[i], primitive[i + 2], primitive[i + 3]);
        }
    }
    else if (mode == GL_TRIANGLES)
    {
        for (size_t i = 0; i + 2 < primitive.size(); i += 3)
        {
            Append(primitive[i], primitive[i + 1], primitive[i + 2]);
        }
    }
    else if (mode == GL_POLYGON || mode == GL_TRIANGLE_FAN)
    {
        // Existing polygon assets are convex, matching the old GL_POLYGON contract.
        for (size_t i = 1; i + 1 < primitive.size(); ++i)
        {
            Append(primitive[0], primitive[i], primitive[i + 1]);
        }
    }
    else
    {
        throw std::logic_error("Unsupported batch primitive");
    }
}
void Color(float r, float g, float b, float a)
{
    color = {r, g, b, a};
}
void TexCoord(float first, float second)
{
    u = first;
    v = second;
}
void Texture(unsigned int value)
{
    state.texture = value;
}
void SolidSample(unsigned int texture, float first, float second)
{
    solidTexture = texture;
    solidU = first;
    solidV = second;
}
void Effect(unsigned int program, int kind, float seconds)
{
    state.program = program;
    state.effect = program ? kind : 0;
    state.seconds = program ? seconds : 0;
}
void LineWidth(float width)
{
    lineWidth = width;
}
void PushMatrix()
{
    transforms.push_back(transforms.back());
}
void PopMatrix()
{
    if (transforms.size() <= 1)
    {
        throw std::logic_error("Unbalanced batch transform stack");
    }
    transforms.pop_back();
}
void Translate(float x, float y, float z)
{
    auto &t = transforms.back();
    t.x += x * t.sx;
    t.y += y * t.sy;
    t.z += z * t.sz;
}
void Scale(float x, float y, float z)
{
    auto &t = transforms.back();
    t.sx *= x;
    t.sy *= y;
    t.sz *= z;
}
void LoadIdentity()
{
    transforms.back() = {};
}
MeshPtr Capture(const std::function<void()> &draw)
{
    if (capture)
    {
        throw std::logic_error("Nested mesh capture");
    }
    profiling::Scope timer(profiling::Timer::MeshBuild);
    auto mesh = std::make_shared<Mesh>();
    auto savedTransforms = transforms;
    State savedState = state;
    auto savedColor = color;
    transforms.assign(1, {});
    state = {};
    capture = &mesh->geometry;
    try
    {
        draw();
    }
    catch (...)
    {
        capture = nullptr;
        transforms = std::move(savedTransforms);
        state = savedState;
        color = savedColor;
        throw;
    }
    capture = nullptr;
    transforms = std::move(savedTransforms);
    state = savedState;
    color = savedColor;
    if (GLEW_VERSION_1_5 && !mesh->geometry.vertices.empty())
    {
        glGenBuffers(1, &mesh->buffer);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->buffer);
        size_t bytes = mesh->geometry.vertices.size() * sizeof(VertexData);
        glBufferData(GL_ARRAY_BUFFER, bytes, mesh->geometry.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        profiling::Count(profiling::Counter::MeshUploadBytes, bytes);
    }
    return mesh;
}
void DrawMesh(const MeshPtr &mesh)
{
    if (!mesh)
    {
        return;
    }
    if (capture)
    {
        throw std::logic_error("Cannot draw a cached mesh during mesh capture");
    }
    profiling::Count(profiling::Counter::SubmittedPrimitives, mesh->geometry.primitiveCount);
    profiling::Count(profiling::Counter::SubmittedVertices, mesh->geometry.vertices.size());
    if (instanceMesh == mesh && instances.size() < 1024)
    {
        instances.push_back(transforms.back());
        return;
    }
    if (!pending.vertices.empty() || instanceMesh)
    {
        profiling::Count(profiling::Counter::BatchBarrierBreaks);
    }
    Flush();
    instanceMesh = mesh;
    instances.push_back(transforms.back());
}
} // namespace renderqueue
