#include "stdafx.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include "Dependencies/glew/include/GL/glew.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    bool depth = false;
    bool depthWrite = false;
    GLuint texture = 0, program = 0;
    int effect = 0;
    float seconds = 0;
    bool operator==(const State &other) const
    {
        return depth == other.depth && depthWrite == other.depthWrite && texture == other.texture &&
               program == other.program && effect == other.effect && seconds == other.seconds;
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
float u = 0, v = 0, lineWidth = 1, solidU = .5f, solidV = .5f, opacity = 1;
GLuint solidTexture = 0, streamBuffer = 0;
size_t streamCapacity = 0, streamHead = 0;
GLenum mode = GL_TRIANGLES;
constexpr size_t kMaximumBatchVertices = 65536;
MeshPtr instanceMesh;
std::vector<Transform> instances;
GLuint instanceProgram = 0, instanceBuffer = 0, instanceVao = 0;
size_t instanceCapacity = 0;
GLint instanceMvpLoc = -1, instanceImageLoc = -1;
GLuint queueProgram = 0, queueVao = 0;
GLint queueMvpLoc = -1, queueImageLoc = -1, queueHasTexLoc = -1;
GLuint cachedEffectProgram = 0;
GLint cachedEffectMvpLoc = -1, cachedEffectKindLoc = -1, cachedEffectSecondsLoc = -1;
bool instanceInitialized = false;
void FlushInstances();

GLuint CompileSimpleProgram(const char *vertexBody, const char *fragmentBody)
{
    profiling::Scope timer(profiling::Timer::ShaderCompile);
    const char *version = GLEW_VERSION_4_5 ? "#version 450 core\n" : "#version 330 core\n";
    const std::string vsSrc = std::string(version) + vertexBody;
    const std::string fsSrc = std::string(version) + fragmentBody;
    const char *sources[] = {vsSrc.c_str(), fsSrc.c_str()};
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
    GLuint program = 0;
    if (valid)
    {
        program = glCreateProgram();
        glAttachShader(program, shaders[0]);
        glAttachShader(program, shaders[1]);
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            glDeleteProgram(program);
            program = 0;
        }
    }
    for (GLuint shader : shaders)
    {
        if (shader)
        {
            glDeleteShader(shader);
        }
    }
    return program;
}

void MultiplyMat4(const float *a, const float *b, float *out)
{
    for (int c = 0; c < 4; ++c)
    {
        const float b0 = b[c * 4 + 0], b1 = b[c * 4 + 1], b2 = b[c * 4 + 2], b3 = b[c * 4 + 3];
        out[c * 4 + 0] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
        out[c * 4 + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
        out[c * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
        out[c * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
    }
}

void ComputeMvp(const Transform &t, float *out)
{
    float proj[16], mv[16], mvp[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    MultiplyMat4(proj, mv, mvp);
    if (t.x == 0 && t.y == 0 && t.z == 0 && t.sx == 1 && t.sy == 1 && t.sz == 1)
    {
        for (int i = 0; i < 16; ++i)
        {
            out[i] = mvp[i];
        }
        return;
    }
    const float local[16] = {t.sx, 0, 0, 0, 0, t.sy, 0, 0, 0, 0, t.sz, 0, t.x, t.y, t.z, 1};
    MultiplyMat4(mvp, local, out);
}

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
    const char *kQueueVs = R"GLSL(
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 vertexUv;
uniform mat4 mvp;
out vec4 tint;
out vec2 uv;
void main() {
    gl_Position = mvp * vec4(vertexPosition, 1.0);
    tint = vertexColor;
    uv = vertexUv;
}
)GLSL";
    const char *kQueueFs = R"GLSL(
uniform sampler2D image;
uniform int hasTexture;
in vec4 tint;
in vec2 uv;
out vec4 fragColor;
void main() {
    fragColor = hasTexture != 0 ? texture(image, uv) * tint : tint;
}
)GLSL";
    queueProgram = CompileSimpleProgram(kQueueVs, kQueueFs);
    if (queueProgram)
    {
        queueMvpLoc = glGetUniformLocation(queueProgram, "mvp");
        queueImageLoc = glGetUniformLocation(queueProgram, "image");
        queueHasTexLoc = glGetUniformLocation(queueProgram, "hasTexture");
        glGenVertexArrays(1, &queueVao);
        glBindVertexArray(queueVao);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    const char *kInstVs = R"GLSL(
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 vertexUv;
layout(location = 3) in vec3 instancePosition;
layout(location = 4) in vec3 instanceScale;
uniform mat4 mvp;
out vec4 tint;
out vec2 uv;
void main() {
    gl_Position = mvp * vec4(vertexPosition * instanceScale + instancePosition, 1.0);
    tint = vertexColor;
    uv = vertexUv;
}
)GLSL";
    const char *kInstFs = R"GLSL(
uniform sampler2D image;
in vec4 tint;
in vec2 uv;
out vec4 fragColor;
void main() {
    fragColor = texture(image, uv) * tint;
}
)GLSL";
    instanceProgram = CompileSimpleProgram(kInstVs, kInstFs);
    if (instanceProgram)
    {
        instanceMvpLoc = glGetUniformLocation(instanceProgram, "mvp");
        instanceImageLoc = glGetUniformLocation(instanceProgram, "image");
        glGenBuffers(1, &instanceBuffer);
        glGenVertexArrays(1, &instanceVao);
        glBindVertexArray(instanceVao);
        for (GLuint a = 0; a < 5; ++a)
        {
            glEnableVertexAttribArray(a);
        }
        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Transform), nullptr);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Transform),
                              reinterpret_cast<void *>(offsetof(Transform, sx)));
        glVertexAttribDivisor(3, 1);
        glVertexAttribDivisor(4, 1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
}

void ApplyLegacyState(const State &value)
{
    if (value.depth)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(value.depthWrite ? GL_TRUE : GL_FALSE);
    glDepthFunc(GL_LEQUAL);
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
        if (value.program != cachedEffectProgram)
        {
            cachedEffectProgram = value.program;
            cachedEffectMvpLoc = glGetUniformLocation(value.program, "mvp");
            cachedEffectKindLoc = glGetUniformLocation(value.program, "effect");
            cachedEffectSecondsLoc = glGetUniformLocation(value.program, "seconds");
        }
        if (cachedEffectKindLoc >= 0)
        {
            glUniform1i(cachedEffectKindLoc, value.effect);
        }
        if (cachedEffectSecondsLoc >= 0)
        {
            glUniform1f(cachedEffectSecondsLoc, value.seconds);
        }
    }
}

void DrawGeometry(const Geometry &geometry, GLuint buffer, const Transform &transform, size_t baseVertex = 0)
{
    if (geometry.vertices.empty())
    {
        return;
    }
    if (queueProgram && queueVao && buffer)
    {
        float mvp[16];
        ComputeMvp(transform, mvp);
        glBindVertexArray(queueVao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, x)));
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, r)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, u)));
        glActiveTexture(GL_TEXTURE0);
        glDepthFunc(GL_LEQUAL);
        GLuint activeProgram = 0;
        GLuint activeTexture = ~0u;
        int activeHasTex = -1;
        for (const auto &range : geometry.ranges)
        {
            const auto &st = range.state;
            if (st.depth)
            {
                glEnable(GL_DEPTH_TEST);
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(st.depthWrite ? GL_TRUE : GL_FALSE);
            const GLuint targetProgram = st.program ? st.program : queueProgram;
            if (targetProgram != activeProgram)
            {
                activeProgram = targetProgram;
                glUseProgram(activeProgram);
                if (activeProgram == queueProgram)
                {
                    glUniformMatrix4fv(queueMvpLoc, 1, GL_FALSE, mvp);
                    glUniform1i(queueImageLoc, 0);
                    activeHasTex = -1;
                }
                else
                {
                    if (activeProgram != cachedEffectProgram)
                    {
                        cachedEffectProgram = activeProgram;
                        cachedEffectMvpLoc = glGetUniformLocation(activeProgram, "mvp");
                        cachedEffectKindLoc = glGetUniformLocation(activeProgram, "effect");
                        cachedEffectSecondsLoc = glGetUniformLocation(activeProgram, "seconds");
                    }
                    if (cachedEffectMvpLoc >= 0)
                    {
                        glUniformMatrix4fv(cachedEffectMvpLoc, 1, GL_FALSE, mvp);
                    }
                }
            }
            if (st.program)
            {
                if (cachedEffectKindLoc >= 0)
                {
                    glUniform1i(cachedEffectKindLoc, st.effect);
                }
                if (cachedEffectSecondsLoc >= 0)
                {
                    glUniform1f(cachedEffectSecondsLoc, st.seconds);
                }
            }
            else
            {
                const int hasTex = st.texture ? 1 : 0;
                if (hasTex != activeHasTex)
                {
                    glUniform1i(queueHasTexLoc, hasTex);
                    activeHasTex = hasTex;
                }
                if (st.texture != activeTexture)
                {
                    glBindTexture(GL_TEXTURE_2D, st.texture);
                    activeTexture = st.texture;
                }
            }
            glDrawArrays(GL_TRIANGLES, GLint(baseVertex + range.first), GLsizei(range.count));
            profiling::Count(profiling::Counter::DrawCalls);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
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
        ApplyLegacyState(range.state);
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
    geometry.vertices.push_back(a);
    geometry.vertices.push_back(b);
    geometry.vertices.push_back(c);
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
    bool compatible = instanceProgram && instanceVao && instanceMesh->buffer && instances.size() > 1;
    for (const auto &range : geometry.ranges)
    {
        compatible = compatible && range.state.program == 0 && range.state.texture != 0;
    }
    // Multi-range meshes must retain object-major painter order. Only a single range may instance.
    compatible = compatible && geometry.ranges.size() == 1;
    if (compatible)
    {
        const auto &st = geometry.ranges[0].state;
        if (st.depth)
        {
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(st.depthWrite ? GL_TRUE : GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        float mvp[16];
        ComputeMvp({}, mvp);
        glBindVertexArray(instanceVao);
        glBindBuffer(GL_ARRAY_BUFFER, instanceMesh->buffer);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, x)));
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, r)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexData),
                              reinterpret_cast<void *>(offsetof(VertexData, u)));
        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        const size_t bytes = instances.size() * sizeof(Transform);
        if (bytes > instanceCapacity)
        {
            instanceCapacity = (std::max)(bytes, instanceCapacity * 2);
        }
        glBufferData(GL_ARRAY_BUFFER, instanceCapacity, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instances.data());
        profiling::Count(profiling::Counter::StreamUploadBytes, bytes);
        glUseProgram(instanceProgram);
        glUniformMatrix4fv(instanceMvpLoc, 1, GL_FALSE, mvp);
        glUniform1i(instanceImageLoc, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, st.texture);
        glDrawArraysInstanced(GL_TRIANGLES, 0, GLsizei(geometry.vertices.size()), GLsizei(instances.size()));
        profiling::Count(profiling::Counter::DrawCalls);
        profiling::Count(profiling::Counter::InstancedDrawCalls);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
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
    opacity = 1;
}
void Shutdown()
{
    Flush();
    if (instanceVao)
    {
        glDeleteVertexArrays(1, &instanceVao);
        instanceVao = 0;
    }
    if (instanceProgram)
    {
        glDeleteProgram(instanceProgram);
        instanceProgram = 0;
    }
    if (instanceBuffer)
    {
        glDeleteBuffers(1, &instanceBuffer);
        instanceBuffer = 0;
        instanceCapacity = 0;
    }
    if (queueVao)
    {
        glDeleteVertexArrays(1, &queueVao);
        queueVao = 0;
    }
    if (queueProgram)
    {
        glDeleteProgram(queueProgram);
        queueProgram = 0;
    }
    cachedEffectProgram = 0;
    cachedEffectMvpLoc = cachedEffectKindLoc = cachedEffectSecondsLoc = -1;
    instanceInitialized = false;
    if (streamBuffer)
    {
        glDeleteBuffers(1, &streamBuffer);
        streamBuffer = 0;
        streamCapacity = 0;
        streamHead = 0;
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
    size_t baseVertex = 0;
    if (GLEW_VERSION_1_5)
    {
        if (!streamBuffer)
        {
            glGenBuffers(1, &streamBuffer);
        }
        glBindBuffer(GL_ARRAY_BUFFER, streamBuffer);
        const size_t bytes = pending.vertices.size() * sizeof(VertexData);
        constexpr size_t kMinRingBytes = 4 * 1024 * 1024;
        if (bytes > streamCapacity || streamCapacity == 0)
        {
            streamCapacity = (std::max)(kMinRingBytes, (std::max)(bytes, streamCapacity * 2));
            glBufferData(GL_ARRAY_BUFFER, streamCapacity, nullptr, GL_STREAM_DRAW);
            streamHead = 0;
        }
        else if (streamHead + bytes > streamCapacity)
        {
            // Orphan once per ring wrap instead of per tiny flush.
            glBufferData(GL_ARRAY_BUFFER, streamCapacity, nullptr, GL_STREAM_DRAW);
            streamHead = 0;
        }
        if (GLEW_VERSION_3_0 && glMapBufferRange != nullptr)
        {
            void *dst = glMapBufferRange(GL_ARRAY_BUFFER, GLintptr(streamHead), GLsizeiptr(bytes),
                                         GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            if (dst)
            {
                std::memcpy(dst, pending.vertices.data(), bytes);
                glUnmapBuffer(GL_ARRAY_BUFFER);
            }
            else
            {
                glBufferSubData(GL_ARRAY_BUFFER, GLintptr(streamHead), GLsizeiptr(bytes),
                                pending.vertices.data());
            }
        }
        else
        {
            glBufferSubData(GL_ARRAY_BUFFER, GLintptr(streamHead), GLsizeiptr(bytes), pending.vertices.data());
        }
        baseVertex = streamHead / sizeof(VertexData);
        streamHead += bytes;
        profiling::Count(profiling::Counter::StreamUploadBytes, bytes);
    }
    DrawGeometry(pending, streamBuffer, {}, baseVertex);
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
                         color[3] * opacity, solid ? solidU : u, solid ? solidV : v});
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
void Opacity(float alpha)
{
    opacity = alpha;
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
void Depth(bool enabled, bool write)
{
    state.depth = enabled;
    state.depthWrite = enabled && write;
}
void PushMatrix()
{
    transforms.push_back(transforms.back());
}
std::array<float, 3> GetTranslation()
{
    const auto &t = transforms.back();
    return {t.x, t.y, t.z};
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
