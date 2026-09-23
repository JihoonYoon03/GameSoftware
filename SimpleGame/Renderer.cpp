#include "stdafx.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include "DrawCallCounter.h"
#include "Dependencies/glew.h"
#include "Renderer.h"
#include "RenderAssets.h"
#include <algorithm>
#include <cstdio>
#pragma comment(lib, "opengl32.lib")

namespace
{
const char *kVertex = R"GLSL(
#version 120
varying vec2 uv;
void main() { gl_Position=gl_Vertex; uv=gl_MultiTexCoord0.xy; }
)GLSL";
const char *kFragment = R"GLSL(
#version 120
uniform sampler2D scene;
uniform vec2 texel;
uniform float clockTime;
varying vec2 uv;
vec3 bright(vec2 p) {
    vec3 c=texture2D(scene,clamp(p,vec2(0.001),vec2(0.999))).rgb;
    return c*smoothstep(0.42,0.9,max(c.r,max(c.g,c.b)));
}
void main() {
    vec3 color=texture2D(scene,uv).rgb;
    vec3 bloom=vec3(0.0);
    for(int i=1;i<=3;++i) {
        vec2 d=texel*float(i*i*2);
        bloom+=bright(uv+vec2(d.x,0.0))+bright(uv-vec2(d.x,0.0));
        bloom+=bright(uv+vec2(0.0,d.y))+bright(uv-vec2(0.0,d.y));
    }
    color+=bloom*0.045;
    color*=vec3(0.98,1.04,1.08);
    color=pow(vec3(1.0)-exp(-color*1.35),vec3(0.9));
    vec2 p=uv*2.0-1.0;
    color*=1.0-0.16*dot(p,p);
    float noise=fract(sin(dot(uv+fract(clockTime*0.07),vec2(12.9898,78.233)))*43758.5453);
    gl_FragColor=vec4(color+(noise-0.5)*0.007,1.0);
}
)GLSL";
} // namespace
Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height), m_viewportWidth(width), m_viewportHeight(height),
      m_assets(new RenderAssets())
{
    if (GLEW_VERSION_3_0)
    {
        CreatePostProcessing();
    }
    if (GLEW_VERSION_2_0)
    {
        const char *vertex = R"GLSL(
#version 120
varying vec2 uv;
void main() { gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex; uv=gl_MultiTexCoord0.xy; }
)GLSL";
        const char *fragment = R"GLSL(
#version 120
uniform float seconds;
uniform int effect;
varying vec2 uv;
void main() {
    if(effect==0) {
        float wave=sin(uv.x*25.0+uv.y*13.0-seconds*2.2)*0.5+0.5;
        float ripple=sin(length(uv-0.5)*48.0-seconds*3.0)*0.5+0.5;
        vec3 water=mix(vec3(0.025,0.13,0.19),vec3(0.12,0.52,0.59),pow(wave*ripple,3.0));
        gl_FragColor=vec4(water,0.86);
    } else if(effect==1) {
        float height=1.0-uv.y;
        float sway=sin(height*12.0-seconds*6.0)*0.09*height;
        float width=(1.0-height)*0.35+0.035;
        float flame=1.0-smoothstep(width*0.3,width,abs(uv.x-0.5+sway));
        flame*=smoothstep(0.0,0.08,uv.y)*smoothstep(0.0,0.1,height);
        vec3 color=mix(vec3(1.0,0.22,0.035),vec3(1.0,0.9,0.3),flame*(1.0-height));
        gl_FragColor=vec4(color,flame);
    } else {
        float radius=length((uv-0.5)*2.0);
        float ring=1.0-smoothstep(0.02,0.11,abs(radius-0.78));
        gl_FragColor=vec4(0.35,0.93,1.0,ring*0.8);
    }
}
)GLSL";
        m_effectProgram = CompileProgram(vertex, fragment);
    }
}
Renderer::~Renderer()
{
    if (m_effectProgram)
    {
        glDeleteProgram(m_effectProgram);
    }
    if (m_postProgram)
    {
        glDeleteProgram(m_postProgram);
    }
    if (m_sceneFramebuffer)
    {
        glDeleteFramebuffers(1, &m_sceneFramebuffer);
    }
    if (m_sceneTexture)
    {
        glDeleteTextures(1, &m_sceneTexture);
    }
}
bool Renderer::IsInitialized() const
{
    return m_assets != nullptr;
}
bool Renderer::HasPostProcessing() const
{
    return m_sceneFramebuffer && m_postProgram;
}
unsigned int Renderer::CompileProgram(const char *vertex, const char *fragment)
{
    profiling::Scope timer(profiling::Timer::ShaderCompile);
    GLuint shaders[] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
    const char *sources[] = {vertex, fragment};
    bool valid = true;
    for (int i = 0; i < 2; ++i)
    {
        if (!shaders[i])
        {
            valid = false;
            continue;
        }
        glShaderSource(shaders[i], 1, &sources[i], nullptr);
        glCompileShader(shaders[i]);
        GLint compiled = 0;
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            char log[2048] = {};
            glGetShaderInfoLog(shaders[i], sizeof(log), nullptr, log);
            std::fprintf(stderr, "후처리 셰이더 오류: %s\n", log);
            valid = false;
        }
    }
    GLuint program = valid ? glCreateProgram() : 0;
    if (program)
    {
        for (GLuint shader : shaders)
        {
            glAttachShader(program, shader);
        }
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char log[2048] = {};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::fprintf(stderr, "후처리 연결 오류: %s\n", log);
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
void Renderer::CreatePostProcessing()
{
    m_postProgram = CompileProgram(kVertex, kFragment);
    if (!m_postProgram)
    {
        return;
    }
    glGenTextures(1, &m_sceneTexture);
    glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m_sceneFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glDeleteFramebuffers(1, &m_sceneFramebuffer);
        m_sceneFramebuffer = 0;
        std::fprintf(stderr, "후처리 버퍼를 사용할 수 없어 기본 렌더링으로 전환합니다.\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
void Renderer::SetPresentationViewport()
{
    float scale = (std::min)(m_viewportWidth / float(m_width), m_viewportHeight / float(m_height));
    int width = int(m_width * scale), height = int(m_height * scale);
    glViewport((m_viewportWidth - width) / 2, (m_viewportHeight - height) / 2, width, height);
}
void Renderer::BeginScene(int width, int height)
{
    m_viewportWidth = width;
    m_viewportHeight = height;
    if (HasPostProcessing())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFramebuffer);
        glViewport(0, 0, m_width, m_height);
    }
    else
    {
        SetPresentationViewport();
    }
    glClearColor(.02f, .04f, .07f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
void Renderer::EndScene(float seconds)
{
    renderqueue::Flush();
    profiling::Scope timer(profiling::Timer::PostProcess);
    profiling::GpuScope gpuTimer(profiling::Timer::PostProcess);
    if (!HasPostProcessing())
    {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SetPresentationViewport();
    glClearColor(.01f, .015f, .025f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glUseProgram(m_postProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    glUniform1i(glGetUniformLocation(m_postProgram, "scene"), 0);
    glUniform2f(glGetUniformLocation(m_postProgram, "texel"), 1.f / m_width, 1.f / m_height);
    glUniform1f(glGetUniformLocation(m_postProgram, "clockTime"), seconds);
    renderdebug::BeginPrimitive(GL_QUADS);
    profiling::Count(profiling::Counter::SubmittedVertices, 4);
    glTexCoord2f(0, 0);
    glVertex2f(-1, -1);
    glTexCoord2f(1, 0);
    glVertex2f(1, -1);
    glTexCoord2f(1, 1);
    glVertex2f(1, 1);
    glTexCoord2f(0, 1);
    glVertex2f(-1, 1);
    glEnd();
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_BLEND);
}
void Renderer::DrawMaterial(const RenderPoint (&vertices)[4], SurfaceMaterial material)
{
    m_assets->DrawMaterial(vertices, material);
}
void Renderer::DrawGlow(float x, float y, float size)
{
    m_assets->DrawGlow(x, y, size);
}

void Renderer::DrawEffect(const RenderPoint (&vertices)[4], SurfaceEffect effect, float seconds)
{
    if (m_effectProgram)
    {
        renderqueue::Effect(m_effectProgram, static_cast<int>(effect), seconds);
    }
    else
    {
        renderqueue::Color(.12f, .4f, .5f, .4f); // Static fallback; animation belongs to the shader.
    }
    renderqueue::Begin(GL_QUADS);
    renderqueue::TexCoord(0, 0);
    renderqueue::Vertex(vertices[0].x, vertices[0].y);
    renderqueue::TexCoord(1, 0);
    renderqueue::Vertex(vertices[1].x, vertices[1].y);
    renderqueue::TexCoord(1, 1);
    renderqueue::Vertex(vertices[2].x, vertices[2].y);
    renderqueue::TexCoord(0, 1);
    renderqueue::Vertex(vertices[3].x, vertices[3].y);
    renderqueue::End();
    if (m_effectProgram)
    {
        renderqueue::Effect(0, 0, 0);
    }
}
void Renderer::DrawCharacter(float x, float y, int frame, int direction)
{
    m_assets->DrawCharacter(x, y, frame, direction);
}
void Renderer::DrawUtf8Text(float x, float y, const std::string &text, int size)
{
    m_assets->DrawUtf8Text(x, y, text, size);
}
void Renderer::DrawSolidRect(float x, float y, float z, float size, float r, float g, float b, float a)
{
    // Preserve the old centered pixel-coordinate API.
    renderqueue::PushMatrix();
    renderqueue::LoadIdentity();
    renderqueue::Translate(m_width * .5f + x, m_height * .5f - y, 0);
    renderqueue::Color(r, g, b, a);
    renderqueue::Begin(GL_QUADS);
    renderqueue::Vertex(-size / 2, -size / 2, z);
    renderqueue::Vertex(size / 2, -size / 2, z);
    renderqueue::Vertex(size / 2, size / 2, z);
    renderqueue::Vertex(-size / 2, size / 2, z);
    renderqueue::End();
    renderqueue::PopMatrix();
}
