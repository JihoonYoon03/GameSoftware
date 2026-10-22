#include "stdafx.h"
#include "Game.h"
#include "Level.h"
#include "Scene.h"
#include "Drawing.h"
#include "ResourceAssets.h"
#include "Monitoring/Monitor.h"

#include "RenderQueue.h"
#include "RenderCache.h"
#include "Profiler.h"
#include "Dependencies/glew.h"
#include "Renderer.h"
#include "RenderAssets.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <cmath>
#include <array>
#include <cstring>
#include <sstream>
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
uniform int pixelMode;
varying vec2 uv;
vec3 bright(vec2 p) {
    vec3 c=texture2D(scene,clamp(p,vec2(0.001),vec2(0.999))).rgb;
    return c*smoothstep(0.72,0.98,max(c.r,max(c.g,c.b)));
}
void main() {
    vec2 sampleUv=pixelMode!=0?(floor(uv/texel)+0.5)*texel:uv;
    vec3 color=texture2D(scene,sampleUv).rgb;
    vec3 bloom=vec3(0.0);
    if(pixelMode==0) for(int i=1;i<=3;++i) {
        vec2 d=texel*float(i*2);
        float weight=float(4-i);
        bloom+=(bright(uv+vec2(d.x,0.0))+bright(uv-vec2(d.x,0.0)))*weight;
        bloom+=(bright(uv+vec2(0.0,d.y))+bright(uv-vec2(0.0,d.y)))*weight;
    }
    color+=bloom*0.012;
    color*=vec3(0.98,1.04,1.08);
    color=pow(vec3(1.0)-exp(-color*1.35),vec3(0.9));
    vec2 p=sampleUv*2.0-1.0;
    color*=1.0-0.16*dot(p,p);
    float noise=fract(sin(dot(uv+fract(clockTime*0.07),vec2(12.9898,78.233)))*43758.5453);
    gl_FragColor=vec4(color+(pixelMode!=0?0.0:(noise-0.5)*0.007),1.0);
}
)GLSL";
} // namespace
namespace rendering
{
struct Mesh::Data
{
    struct Vertex
    {
        float x, y, z, nx, ny, nz, r, g, b, a, palette, joint, surface;
        std::array<float, 4> joints, weights;
    };
    unsigned int buffer = 0;
    std::vector<Vertex> vertices;
    float roughness = .7f, metallic = .2f;
    bool ground = false;
    bool imported = false;
};
Mesh::Mesh() : data(new Data())
{
}
Mesh::~Mesh()
{
    if (data->buffer)
    {
        glDeleteBuffers(1, &data->buffer);
    }
}
} // namespace rendering
struct Renderer::MeshData
{
    struct Instance
    {
        float anchor[4], scale[4], rotation[4], body[4], accent[4], emission[4], finish[4], translation[4];
    };
    struct Batch
    {
        std::shared_ptr<rendering::Mesh> mesh;
        models::SkinMatrices bones;
        bool armed = false;
        std::vector<Instance> instances;
    };
    std::map<const models::Model *, std::shared_ptr<rendering::Mesh>> meshes;
    std::map<std::string, std::shared_ptr<rendering::Material>> materials;
    std::map<std::string, size_t> batchKeys;
    std::vector<Batch> batches;
    std::vector<std::function<void()>> overlays;
    std::vector<rendering::RenderObject> *owner = nullptr;
    size_t ownerSlot = 0;
    bool collecting = false;
    unsigned int program = 0, instancesBuffer = 0;
    unsigned int darkProgram = 0, maskProgram = 0, mapTexture = 0, maskTexture = 0, maskFbo = 0;
    int maskWidth = 0, maskHeight = 0;
    unsigned int shadowFbo = 0, shadowTexture = 0, shadowComposite = 0;
    int shadowWidth = 0, shadowHeight = 0;
    std::uint64_t mapRevision = 0;
    bool powered = false;
    rendering::FrameStatistics statistics;
    ~MeshData()
    {
        if (shadowFbo)
        {
            glDeleteFramebuffers(1, &shadowFbo);
        }
        if (shadowTexture)
        {
            glDeleteTextures(1, &shadowTexture);
        }
        if (shadowComposite)
        {
            glDeleteProgram(shadowComposite);
        }
        if (darkProgram)
        {
            glDeleteProgram(darkProgram);
        }
        if (maskProgram)
        {
            glDeleteProgram(maskProgram);
        }
        if (mapTexture)
        {
            glDeleteTextures(1, &mapTexture);
        }
        if (maskTexture)
        {
            glDeleteTextures(1, &maskTexture);
        }
        if (maskFbo)
        {
            glDeleteFramebuffers(1, &maskFbo);
        }
        meshes.clear();
        if (program)
        {
            glDeleteProgram(program);
        }
        if (instancesBuffer)
        {
            glDeleteBuffers(1, &instancesBuffer);
        }
    }
};
namespace
{
const char *kMeshVertex = R"GLSL(
#version 120
attribute vec3 vertexPosition,vertexNormal;
attribute vec4 vertexColor;
attribute vec3 vertexInfo;
attribute vec4 vertexJoints,vertexWeights;
attribute vec4 instanceAnchor,instanceScale,instanceRotation,instanceBody,instanceAccent,instanceEmission,instanceFinish,instanceTranslation;
uniform mat4 bones[32];
uniform int armed,shadowPass,imported;
uniform vec2 shadowJitter;
varying vec3 normal,localPosition;
varying vec4 tint,finish;
varying float surface;
mat3 rotation(vec3 a) {
 float c=cos(a.x),s=sin(a.x),p=cos(a.y),q=sin(a.y),r=cos(a.z),t=sin(a.z);
 return mat3(c,s,0.,-s,c,0.,0.,0.,1.)*mat3(p,0.,-q,0.,1.,0.,q,0.,p)*mat3(1.,0.,0.,0.,r,t,0.,-t,r);
}
void main() {
 int j=int(vertexInfo.y+0.5);
 mat3 rotate=rotation(instanceRotation.xyz);
 mat4 skin=bones[int(vertexJoints.x+.5)]*vertexWeights.x+bones[int(vertexJoints.y+.5)]*vertexWeights.y
          +bones[int(vertexJoints.z+.5)]*vertexWeights.z+bones[int(vertexJoints.w+.5)]*vertexWeights.w;
 vec3 p=rotate*((skin*vec4(vertexPosition,1.)).xyz*instanceScale.xyz)+instanceTranslation.xyz;
 normal=normalize(rotate*((mat3(skin)*vertexNormal)/instanceScale.xyz));
 localPosition=p;surface=vertexInfo.z;
 tint=vertexColor;
 if(vertexInfo.x>2.5)tint*=instanceEmission;
 else if(vertexInfo.x>1.5)tint*=instanceAccent;
 else if(vertexInfo.x>0.5)tint*=instanceBody;
 else tint.a*=instanceBody.a;
 finish=instanceFinish;
 if(vertexInfo.x>2.5)surface=4.;
 if(shadowPass!=0) { p.xy+=vec2(.38,.55)*p.z;p.z=0.; }
 vec2 screen=instanceAnchor.xy+vec2((p.x-p.y)*36.,(p.x+p.y)*18.-p.z*44.);
 float depth=instanceAnchor.z+(p.x+p.y)*36.+p.z*(1296./44.);
 if(shadowPass!=0) {screen+=shadowJitter;depth=-7000.;}
 gl_Position=gl_ModelViewProjectionMatrix*vec4(screen,depth,1.);
 if(((imported!=0?j==1:j==10) && armed==0)||(shadowPass!=0 && finish.w<.5))gl_Position=vec4(2.,2.,2.,1.);
}
)GLSL";
const char *kSoftShadow = R"GLSL(
#version 120
uniform sampler2D image;
uniform vec2 texel;
varying vec2 uv;
void main(){
 float a=texture2D(image,uv).a*.2;
 for(int i=1;i<=2;++i){vec2 d=texel*float(i);
 a+=(texture2D(image,uv+vec2(d.x,0.)).a+texture2D(image,uv-vec2(d.x,0.)).a+
     texture2D(image,uv+vec2(0.,d.y)).a+texture2D(image,uv-vec2(0.,d.y)).a)*.1;}
 gl_FragColor=vec4(.005,.01,.025,a);
}
)GLSL";
const char *kMeshFragment = R"GLSL(
#version 120
uniform int cartoon,shadowPass;
varying vec3 normal,localPosition;
varying vec4 tint,finish;
varying float surface;
void main() {
 if(shadowPass!=0) {gl_FragColor=vec4(0.,0.,0.,.30*tint.a);return;}
 vec3 n=normalize(normal),l=normalize(vec3(-.35,-.45,.82)),v=normalize(vec3(1.,1.,.818));
 float diffuse=max(0.,dot(n,l));
 float light=cartoon!=0?(diffuse<.22?.46:diffuse<.65?.72:1.):.48+.52*diffuse;
 float rough=finish.x,metal=finish.y;
 if(surface>0.5 && surface<1.5){rough=.94;metal=0.;}
 if(surface>1.5 && surface<2.5){rough=.35;metal=.75;}
 if(surface>2.5 && surface<3.5){rough=.12;metal=.15;}
 if(surface>4.5 && surface<5.5){rough=.96;metal=0.;}
 if(surface>5.5 && surface<6.5){rough=.9;metal=0.;}
 if(surface>6.5){rough=.75;metal=0.;}
 float spec=pow(max(0.,dot(n,normalize(l+v))),mix(90.,8.,rough))*(1.-rough)*.65;
 float rim=pow(1.-max(0.,dot(n,v)),3.)*.08;
 vec3 color=tint.rgb*light+mix(vec3(.7,.83,.9),tint.rgb,metal)*spec+vec3(.2,.4,.48)*rim;
 if(surface>3.5 && surface<4.5)color=tint.rgb*(1.+finish.z*.25);
 gl_FragColor=vec4(color,tint.a);
}
)GLSL";
models::Point Cross(models::Point a, models::Point b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
models::Point Unit(models::Point p)
{
    float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    return l > .000001f ? models::Point{p.x / l, p.y / l, p.z / l} : models::Point{0, 0, 1};
}
} // namespace
bool Renderer::SubmitModel(const models::Model &model, RenderPoint screen, models::Point scale,
                           const models::Transform3D &transform, float depth, const models::Palette &palette,
                           const models::CharacterPose *pose)
{
    if (!m_meshes->program)
    {
        return false;
    }
    auto offset = renderqueue::GetTranslation();
    screen.x += offset[0];
    screen.y += offset[1];
    depth += offset[2];
    auto &data = *m_meshes;
    auto &mesh = data.meshes[&model];
    if (!mesh)
    {
        mesh = std::make_shared<rendering::Mesh>();
        auto &geometry = *mesh->data;
        geometry.ground = model.ground;
        const auto &library = models::ModelLibrary::Get();
        geometry.imported = model.resourceId != 0;
        if (model.resourceId)
        {
            const auto &resource = models::ResourceAssets::Get(model.resourceId);
            geometry.ground = geometry.ground || resource.name.find("ground/") == 0;
            geometry.vertices.reserve(resource.vertices.size());
            for (const auto &v : resource.vertices)
            {
                geometry.vertices.push_back({v.x, v.y, v.z, v.nx, v.ny, v.nz, v.r, v.g, v.b, v.a, v.palette,
                                             v.visibility, v.surface, v.joints, v.weights});
            }
        }
        for (const auto &m : library.grounds)
        {
            if (&model == &m)
            {
                geometry.ground = true;
            }
        }
        for (const auto &m : library.characters)
        {
            if (&model == &m || &model == &library.femaleCharacters[&m - library.characters.data()])
            {
                geometry.roughness = .9f;
                geometry.metallic = 0;
            }
        }
        for (const auto &m : library.vehicles)
        {
            if (&model == &m.body)
            {
                geometry.roughness = .35f;
                geometry.metallic = .65f;
            }
        }
        auto vertex = [&](models::Point p, models::Point n, const models::Part &part) {
            auto surface = part.surface;
            if (surface == models::Surface::Default && part.clothing)
            {
                surface = models::Surface::Fabric;
            }
            if (part.kind == models::PartKind::Glass)
            {
                surface = models::Surface::Glass;
            }
            geometry.vertices.push_back({p.x,
                                         p.y,
                                         p.z,
                                         n.x,
                                         n.y,
                                         n.z,
                                         part.color[0],
                                         part.color[1],
                                         part.color[2],
                                         part.color[3],
                                         float(part.palette),
                                         float(part.joint),
                                         float(surface),
                                         {float(part.joint), 31, 31, 31},
                                         {1, 0, 0, 0}});
        };
        for (size_t i = 0; i < model.count; ++i)
        {
            const auto &part = model.parts[i];
            if (part.kind == models::PartKind::Line)
            {
                auto a = part.points[0], b = part.points[1];
                auto axis = Unit({b.x - a.x, b.y - a.y, b.z - a.z});
                auto u = Unit(
                    Cross(axis, std::fabs(axis.z) > .9f ? models::Point{1, 0, 0} : models::Point{0, 0, 1}));
                auto v = Cross(axis, u);
                float radius = part.width / 88;
                for (int k = 0; k < 8; ++k)
                {
                    float t = k * 6.2831853f / 8, t1 = (k + 1) * 6.2831853f / 8;
                    auto n = models::Point{u.x * std::cos(t) + v.x * std::sin(t),
                                           u.y * std::cos(t) + v.y * std::sin(t),
                                           u.z * std::cos(t) + v.z * std::sin(t)};
                    auto n1 = models::Point{u.x * std::cos(t1) + v.x * std::sin(t1),
                                            u.y * std::cos(t1) + v.y * std::sin(t1),
                                            u.z * std::cos(t1) + v.z * std::sin(t1)};
                    auto at = [&](models::Point p, models::Point n) {
                        return models::Point{p.x + n.x * radius, p.y + n.y * radius, p.z + n.z * radius};
                    };
                    vertex(at(a, n), n, part);
                    vertex(at(b, n), n, part);
                    vertex(at(b, n1), n1, part);
                    vertex(at(a, n), n, part);
                    vertex(at(b, n1), n1, part);
                    vertex(at(a, n1), n1, part);
                }
                continue;
            }
            for (size_t k = 1; k + 1 < part.count; ++k)
            {
                auto a = part.points[0], b = part.points[k], c = part.points[k + 1];
                auto normal =
                    Unit(Cross({b.x - a.x, b.y - a.y, b.z - a.z}, {c.x - a.x, c.y - a.y, c.z - a.z}));
                for (size_t j : {size_t(0), k, k + 1})
                {
                    auto n = part.normals[j];
                    if (n.x * n.x + n.y * n.y + n.z * n.z < .001f)
                    {
                        n = normal;
                    }
                    vertex(part.points[j], n, part);
                }
            }
        }
        glGenBuffers(1, &geometry.buffer);
        glBindBuffer(GL_ARRAY_BUFFER, geometry.buffer);
        glBufferData(GL_ARRAY_BUFFER, geometry.vertices.size() * sizeof(rendering::Mesh::Data::Vertex),
                     geometry.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        profiling::Count(profiling::Counter::MeshUploadBytes,
                         geometry.vertices.size() * sizeof(rendering::Mesh::Data::Vertex));
    }
    std::string materialKey(reinterpret_cast<const char *>(&palette), sizeof(palette));
    materialKey.append(reinterpret_cast<const char *>(&mesh->data->roughness), sizeof(float) * 2);
    materialKey.push_back(mesh->data->ground ? 1 : 0);
    auto &material = data.materials[materialKey];
    if (!material)
    {
        material = std::make_shared<rendering::Material>();
        material->palette = palette;
        material->roughness = mesh->data->roughness;
        material->metallic = mesh->data->metallic;
        material->castsShadow = !mesh->data->ground;
    }
    if (data.owner)
    {
        if (data.owner->size() <= data.ownerSlot)
        {
            data.owner->resize(data.ownerSlot + 1);
        }
        (*data.owner)[data.ownerSlot++] = {mesh, material};
    }
    models::SkinMatrices bones = {};
    for (size_t j = 0; j < 11; ++j)
    {
        models::Point o = {}, x = {1, 0, 0}, y = {0, 1, 0}, z = {0, 0, 1};
        if (pose)
        {
            o = pose->Apply(o, models::Joint(j));
            x = pose->Apply(x, models::Joint(j));
            y = pose->Apply(y, models::Joint(j));
            z = pose->Apply(z, models::Joint(j));
        }
        float matrix[] = {x.x - o.x, x.y - o.y, x.z - o.z, 0, y.x - o.x, y.y - o.y, y.z - o.z, 0,
                          z.x - o.x, z.y - o.y, z.z - o.z, 0, o.x,       o.y,       o.z,       1};
        std::copy(matrix, matrix + 16, bones.begin() + j * 16);
    }
    if (model.resourceId)
    {
        models::ResourceAssets::Sample(model.resourceId, pose, bones);
    }
    bool armed = pose && pose->armed;
    std::string key; // identity + pose; palette is per instance
    auto identity = reinterpret_cast<std::uintptr_t>(&model);
    key.assign(reinterpret_cast<char *>(&identity), sizeof(identity));
    key.append(reinterpret_cast<char *>(bones.data()), sizeof(bones));
    key.push_back(armed ? '1' : '0');
    auto found = data.batchKeys.find(key);
    if (found == data.batchKeys.end())
    {
        data.batchKeys[key] = data.batches.size();
        data.batches.push_back({mesh, bones, armed, {}});
        found = data.batchKeys.find(key);
    }
    MeshData::Instance instance = {
        {screen.x, screen.y, depth, 0},
        {scale.x, scale.y, scale.z, 0},
        {transform.yaw, transform.pitch, transform.roll, 0},
        {},
        {},
        {},
        {material->roughness, material->metallic, material->emission, material->castsShadow ? 1.f : 0.f},
        {transform.position.x, transform.position.y, transform.position.z, 0}};
    std::copy(palette.body.begin(), palette.body.end(), instance.body);
    std::copy(palette.accent.begin(), palette.accent.end(), instance.accent);
    std::copy(palette.emission.begin(), palette.emission.end(), instance.emission);
    data.batches[found->second].instances.push_back(instance);
    ++data.statistics.objects;
    data.statistics.triangles += unsigned(mesh->data->vertices.size() / 3);
    if (!data.collecting)
    {
        DrawModels();
    }
    return true;
}
void Renderer::BindObject(std::vector<rendering::RenderObject> *owner)
{
    auto &d = *m_meshes;
    if (d.owner)
    {
        d.owner->resize(d.ownerSlot);
    }
    d.owner = owner;
    d.ownerSlot = 0;
}
void Renderer::BeginModels()
{
    m_meshes->collecting = true;
}
bool Renderer::DeferOverlay(std::function<void()> draw)
{
    if (!m_meshes->collecting)
    {
        return false;
    }
    const auto offset = renderqueue::GetTranslation();
    m_meshes->overlays.push_back([offset, draw = std::move(draw)]() {
        renderqueue::PushMatrix();
        renderqueue::LoadIdentity();
        renderqueue::Translate(offset[0], offset[1], offset[2]);
        draw();
        renderqueue::PopMatrix();
    });
    return true;
}
void Renderer::EndModels()
{
    BindObject(nullptr);
    DrawModels();
    renderqueue::Flush();
    m_meshes->collecting = false;
    auto overlays = std::move(m_meshes->overlays);
    m_meshes->overlays.clear();
    for (auto &draw : overlays)
    {
        draw();
    }
    renderqueue::Flush();
}
const rendering::FrameStatistics &Renderer::GetFrameStatistics() const
{
    return m_meshes->statistics;
}
void Renderer::DrawModels()
{
    auto &data = *m_meshes;
    if (data.batches.empty())
    {
        return;
    }
    GLint originalFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &originalFbo);
    auto attributes = [](bool enabled) {
        for (unsigned int i = 0; i < 14; ++i)
        {
            if (enabled)
            {
                glEnableVertexAttribArray(i);
            }
            else
            {
                glDisableVertexAttribArray(i);
            }
            glVertexAttribDivisor(i, 0);
        }
    };
    auto shader = [&](int shadow) {
        glUseProgram(data.program);
        attributes(true);
        glDisable(GL_TEXTURE_2D);
        glUniform1i(glGetUniformLocation(data.program, "cartoon"), m_style.cartoon ? 1 : 0);
        glUniform1i(glGetUniformLocation(data.program, "shadowPass"), shadow);
        glUniform2f(glGetUniformLocation(data.program, "shadowJitter"), 0, 0);
    };
    if (!data.instancesBuffer)
    {
        glGenBuffers(1, &data.instancesBuffer);
    }
    auto draw = [&](int pass) {
        for (auto &batch : data.batches)
        {
            auto &mesh = *batch.mesh->data;
            if (mesh.ground != (pass == 0))
            {
                continue;
            }
            using V = rendering::Mesh::Data::Vertex;
            glBindBuffer(GL_ARRAY_BUFFER, mesh.buffer);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V),
                                  reinterpret_cast<void *>(offsetof(V, x)));
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(V),
                                  reinterpret_cast<void *>(offsetof(V, nx)));
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V),
                                  reinterpret_cast<void *>(offsetof(V, r)));
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(V),
                                  reinterpret_cast<void *>(offsetof(V, palette)));
            glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, sizeof(V),
                                  reinterpret_cast<void *>(offsetof(V, joints)));
            glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, sizeof(V),
                                  reinterpret_cast<void *>(offsetof(V, weights)));
            glBindBuffer(GL_ARRAY_BUFFER, data.instancesBuffer);
            glBufferData(GL_ARRAY_BUFFER, batch.instances.size() * sizeof(MeshData::Instance),
                         batch.instances.data(), GL_STREAM_DRAW);
            for (unsigned int a = 4; a < 12; ++a)
            {
                glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(MeshData::Instance),
                                      reinterpret_cast<void *>(size_t(a - 4) * 16));
                glVertexAttribDivisor(a, 1);
            }
            glUniformMatrix4fv(glGetUniformLocation(data.program, "bones[0]"), 32, GL_FALSE,
                               batch.bones.data());
            glUniform1i(glGetUniformLocation(data.program, "armed"), batch.armed ? 1 : 0);
            glUniform1i(glGetUniformLocation(data.program, "imported"), mesh.imported ? 1 : 0);
            glDrawArraysInstanced(GL_TRIANGLES, 0, GLsizei(mesh.vertices.size()),
                                  GLsizei(batch.instances.size()));
            profiling::Count(profiling::Counter::DrawCalls);
            profiling::Count(profiling::Counter::RenderedInstances, batch.instances.size());
            profiling::Count(profiling::Counter::SubmittedVertices,
                             mesh.vertices.size() * batch.instances.size());
            ++data.statistics.drawCalls;
            if (pass == 1)
            {
                ++data.statistics.shadowDraws;
            }
            if (batch.instances.size() > 1)
            {
                ++data.statistics.instancedDraws;
                profiling::Count(profiling::Counter::InstancedDrawCalls);
            }
        }
    };
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    shader(0);
    draw(0);
    // Restore divisors before fixed-function 2D draws (UV array aliases attribute 8).
    attributes(false);
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    renderqueue::Flush();
    int width = (std::min)(640, m_renderWidth), height = (std::min)(400, m_renderHeight);
    if (!data.shadowTexture)
    {
        glGenTextures(1, &data.shadowTexture);
    }
    if (!data.shadowFbo)
    {
        glGenFramebuffers(1, &data.shadowFbo);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, data.shadowTexture);
    if (data.shadowWidth != width || data.shadowHeight != height)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        data.shadowWidth = width;
        data.shadowHeight = height;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, data.shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, data.shadowTexture, 0);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    shader(1);
    draw(1);
    attributes(false);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(originalFbo));
    if (originalFbo)
    {
        glViewport(0, 0, m_renderWidth, m_renderHeight);
    }
    else
    {
        SetPresentationViewport();
    }
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(data.shadowComposite);
    glBindTexture(GL_TEXTURE_2D, data.shadowTexture);
    glUniform1i(glGetUniformLocation(data.shadowComposite, "image"), 0);
    glUniform2f(glGetUniformLocation(data.shadowComposite, "texel"), 1.f / width, 1.f / height);
    renderdebug::BeginPrimitive(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-1, -1);
    glTexCoord2f(1, 0);
    glVertex2f(1, -1);
    glTexCoord2f(1, 1);
    glVertex2f(1, 1);
    glTexCoord2f(0, 1);
    glVertex2f(-1, 1);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    shader(0);
    draw(2);
    attributes(false);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    data.batches.clear();
    data.batchKeys.clear();
}

namespace
{
const char *kDarkFragment = R"GLSL(
#version 120
uniform sampler2D occupancy,depthImage;
uniform float viewZoom, mapSize;
uniform vec2 origin;
uniform vec4 lights[24];
uniform int lightCount;
uniform vec2 aim;
varying vec2 uv;
float trace(vec2 from,vec2 to) {
 vec2 delta=to-from,cell=floor(from),end=floor(to),step=sign(delta);
 vec2 inverse=1./max(abs(delta),vec2(.00001));
 vec2 edge=mix(cell,cell+1.,step*.5+.5);
 vec2 crossing=abs(edge-from)*inverse;
 for(int i=0;i<48;++i) {
  if(all(equal(cell,end)))return 1.;
  if(any(lessThan(cell,vec2(0.)))||any(greaterThanEqual(cell,vec2(mapSize))))return 0.;
  if(texture2D(occupancy,(cell+.5)/mapSize).r>.5)return 0.;
  if(crossing.x<crossing.y){cell.x+=step.x;crossing.x+=inverse.x;}
  else {cell.y+=step.y;crossing.y+=inverse.y;}
 }
 return 0.;
}
void main() {
 vec2 screen=vec2(uv.x*1280.,(1.-uv.y)*800.);
 screen=vec2(640.,456.)+(screen-vec2(640.,456.))/viewZoom;
 vec2 s=screen-origin;
 float d=texture2D(depthImage,uv).r;
 float height=d<.9999?max(0.,((.5-d)*131072.-2.*s.y)/(88.+1296./44.)):0.;
 float sum=(s.y+44.*height)/18.,difference=s.x/36.;
 vec2 world=vec2(sum+difference,sum-difference)*.5;
 float light=0.;
 for(int i=0;i<24;++i) {
  if(i>=lightCount)break;
  vec4 source=lights[i];float distance=length(world-source.xy);
  if(distance>=source.z)continue;
  float amount=min(1.,(1.-distance/source.z)*1.8)*source.w;
  if(i==1) {
   vec2 delta=world-source.xy;
   vec2 direction=vec2(delta.x-delta.y,(delta.x+delta.y)*.5);
   float alignment=length(direction)>.001?dot(normalize(direction),aim):1.;
   amount*=smoothstep(.86,.96,alignment);
  }
  if(amount<=.001)continue;
  float visible=(trace(source.xy,world)+trace(source.xy+vec2(.025,-.025),world)+trace(source.xy+vec2(-.025,.025),world))/3.;
  light=max(light,amount*visible);
 }
 gl_FragColor=vec4(0.,0.,0.,1.-clamp(light,0.,1.));
}
)GLSL";
const char *kMaskComposite = R"GLSL(
#version 120
uniform sampler2D image;
uniform vec2 texel;
uniform int soften;
varying vec2 uv;
void main(){
 vec4 value=texture2D(image,uv);
 if(soften!=0){
  value*=.25;
  value+=(texture2D(image,uv+vec2(texel.x,0.))+texture2D(image,uv-vec2(texel.x,0.))+
          texture2D(image,uv+vec2(0.,texel.y))+texture2D(image,uv-vec2(0.,texel.y)))*.125;
  value+=(texture2D(image,uv+texel)+texture2D(image,uv-texel)+
          texture2D(image,uv+vec2(texel.x,-texel.y))+texture2D(image,uv+vec2(-texel.x,texel.y)))*.0625;
 }
 gl_FragColor=value;
}
)GLSL";
void FullScreenQuad()
{
    renderdebug::BeginPrimitive(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-1, -1);
    glTexCoord2f(1, 0);
    glVertex2f(1, -1);
    glTexCoord2f(1, 1);
    glVertex2f(1, 1);
    glTexCoord2f(0, 1);
    glVertex2f(-1, 1);
    glEnd();
}
} // namespace
bool Renderer::DrawMineDarkness()
{
    auto &mine = game::GameLevels::Mine();
    auto &state = mine.GetState();
    if (!HasPostProcessing() || !m_meshes->darkProgram || state.tiles.empty())
    {
        return false;
    }
    auto &d = *m_meshes;
    renderqueue::Flush();
    int width = (std::min)(1280, m_renderWidth), height = (std::min)(800, m_renderHeight);
    if (!d.maskTexture)
    {
        glGenTextures(1, &d.maskTexture);
    }
    if (!d.maskFbo)
    {
        glGenFramebuffers(1, &d.maskFbo);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, d.maskTexture);
    if (d.maskWidth != width || d.maskHeight != height)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        d.maskWidth = width;
        d.maskHeight = height;
    }
    GLint filter = m_style.resolution == WorldResolution::Native ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, d.maskFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, d.maskTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    if (!d.mapTexture)
    {
        glGenTextures(1, &d.mapTexture);
    }
    glBindTexture(GL_TEXTURE_2D, d.mapTexture);
    bool powered = state.quest.IsPowerBypassed();
    if (d.mapRevision != state.mapRevision || d.powered != powered)
    {
        std::vector<unsigned char> cells(game::kMapSize * game::kMapSize);
        for (int y = 0; y < game::kMapSize; ++y)
        {
            for (int x = 0; x < game::kMapSize; ++x)
            {
                cells[y * game::kMapSize + x] = mine.Map().IsWalkable(x, y) ? 0 : 255;
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, game::kMapSize, game::kMapSize, 0, GL_LUMINANCE,
                     GL_UNSIGNED_BYTE, cells.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        d.mapRevision = state.mapRevision;
        d.powered = powered;
    }
    auto &world = tutorial::Game::Get().GetState();
    auto origin = tutorial::Drawing::WorldToScreen(0, 0);
    std::vector<std::array<float, 4>> lights = {{world.playerPosition.x, world.playerPosition.y, 2.8f, .8f},
                                                {world.playerPosition.x, world.playerPosition.y, 12, 1}};
    for (const auto &lamp : state.lights)
    {
        if (!lamp.needsPower || powered)
        {
            lights.push_back({lamp.position.x, lamp.position.y, lamp.radius, 1});
        }
    }
    if (state.muzzleFlash > 0)
    {
        lights.push_back({world.playerPosition.x, world.playerPosition.y, 3.5f, 1});
    }
    if (state.riaCombat.muzzleFlash > 0)
    {
        lights.push_back({state.riaPosition.x, state.riaPosition.y, 3, 1});
    }
    for (const auto &npc : state.survivors)
    {
        if (npc.combat.muzzleFlash > 0 && lights.size() < 24)
        {
            lights.push_back({npc.position.x, npc.position.y, 3, 1});
        }
    }
    for (const auto &tracer : state.tracers)
    {
        if (lights.size() < 24)
        {
            lights.push_back({tracer.end.x, tracer.end.y, 1.3f, tracer.remaining / .12f});
        }
    }
    if (lights.size() > 24)
    {
        lights.resize(24);
    }
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glUseProgram(d.darkProgram);
    glUniform1f(glGetUniformLocation(d.darkProgram, "viewZoom"), tutorial::WorldCamera::zoom);
    glUniform1f(glGetUniformLocation(d.darkProgram, "mapSize"), float(game::kMapSize));
    glUniform1i(glGetUniformLocation(d.darkProgram, "occupancy"), 0);
    glUniform1i(glGetUniformLocation(d.darkProgram, "depthImage"), 1);
    glUniform2f(glGetUniformLocation(d.darkProgram, "origin"), origin.x, origin.y);
    float angle = mine.GetAimAngle();
    glUniform2f(glGetUniformLocation(d.darkProgram, "aim"), std::cos(angle), std::sin(angle));
    glUniform1i(glGetUniformLocation(d.darkProgram, "lightCount"), GLint(lights.size()));
    glUniform4fv(glGetUniformLocation(d.darkProgram, "lights[0]"), GLsizei(lights.size()),
                 lights.front().data());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_sceneDepth);
    FullScreenQuad();
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SetPresentationViewport();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(d.maskProgram);
    glBindTexture(GL_TEXTURE_2D, d.maskTexture);
    glUniform1i(glGetUniformLocation(d.maskProgram, "image"), 0);
    glUniform2f(glGetUniformLocation(d.maskProgram, "texel"), 1.f / width, 1.f / height);
    glUniform1i(glGetUniformLocation(d.maskProgram, "soften"),
                m_style.resolution == WorldResolution::Native ? 1 : 0);
    FullScreenQuad();
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height), m_viewportWidth(width), m_viewportHeight(height),
      m_renderWidth(width), m_renderHeight(height), m_meshes(new MeshData()), m_assets(new RenderAssets())
{
    if (GLEW_VERSION_3_3)
    {
        m_meshes->program = CompileProgram(kMeshVertex, kMeshFragment);
        m_meshes->darkProgram = CompileProgram(kVertex, kDarkFragment);
        m_meshes->maskProgram = CompileProgram(kVertex, kMaskComposite);
        m_meshes->shadowComposite = CompileProgram(kVertex, kSoftShadow);
    }
    if (GLEW_VERSION_3_0)
    {
        CreatePostProcessing();
    }
    if (GLEW_VERSION_2_0)
    {
        const char *vertex = R"GLSL(
#version 120
varying vec2 uv;
varying vec4 tint;
void main() { gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex; uv=gl_MultiTexCoord0.xy; tint=gl_Color; }
)GLSL";
        const char *fragment = R"GLSL(
#version 120
uniform float seconds;
uniform int effect;
varying vec2 uv;
varying vec4 tint;
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
    } else if(effect==3) {
        float t=uv.x,y=uv.y-.5;
        float envelope=smoothstep(0.,.07,t)*pow(max(0.,1.-t),1.35);
        float separation=.19*pow(1.-t,1.7);
        float width=.045+.045*sin(t*3.14159);
        float sway=sin(t*18.-seconds*17.)*.012*t;
        float cores=exp(-pow((y-separation+sway)/width,2.))+exp(-pow((y+separation+sway)/width,2.));
        float halo=exp(-pow(y/(.26*pow(1.-t,.7)+.012),2.))*envelope;
        float alpha=clamp(cores*envelope*.93+halo*.37,0.,1.)*tint.a;
        vec3 outer=mix(vec3(.035,.16,.68),tint.rgb,.40);
        vec3 inner=mix(tint.rgb,vec3(.55,1.,1.),.60);
        gl_FragColor=vec4(mix(outer,inner,clamp(cores*envelope,0.,1.)),alpha);
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
    if (m_sceneDepth)
    {
        glDeleteTextures(1, &m_sceneDepth);
    }
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
        const char *attributes[] = {
            "vertexPosition", "vertexNormal",        "vertexColor",  "vertexInfo",     "instanceAnchor",
            "instanceScale",  "instanceRotation",    "instanceBody", "instanceAccent", "instanceEmission",
            "instanceFinish", "instanceTranslation", "vertexJoints", "vertexWeights"};
        for (GLuint a = 0; a < 14; ++a)
        {
            glBindAttribLocation(program, a, attributes[a]);
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
// ============================================================================
// RenderStyle / Renderer — world resolution is independent of logical UI/input
// ============================================================================
float RenderStyle::Shade(float diffuse, bool cartoon)
{
    diffuse = (std::max)(0.f, (std::min)(1.f, diffuse));
    return cartoon ? (diffuse < .22f ? .46f : diffuse < .65f ? .72f : 1.f) : .48f + .52f * diffuse;
}
const RenderStyle &Renderer::GetStyle() const
{
    return m_style;
}
int Renderer::GetRenderWidth() const
{
    return m_renderWidth;
}
int Renderer::GetRenderHeight() const
{
    return m_renderHeight;
}
void Renderer::SetStyle(RenderStyle style)
{
    if (style.cartoon != m_style.cartoon)
    {
        rendercache::Clear();
    }
    int divisor = int(style.resolution);
    if (divisor != 1 && divisor != 2 && divisor != 4)
    {
        style.resolution = WorldResolution::Native;
    }
    if (!HasPostProcessing())
    {
        style.resolution = WorldResolution::Native;
    }
    bool resize = style.resolution != m_style.resolution;
    m_style = style;
    if (resize)
    {
        renderqueue::Flush();
        m_renderWidth = m_width / int(style.resolution);
        m_renderHeight = m_height / int(style.resolution);
        CreatePostProcessing();
        if (!HasPostProcessing())
        {
            m_style.resolution = WorldResolution::Native;
            m_renderWidth = m_width;
            m_renderHeight = m_height;
        }
    }
}
void Renderer::CreatePostProcessing()
{
    if (!m_postProgram)
    {
        m_postProgram = CompileProgram(kVertex, kFragment);
    }
    if (!m_postProgram)
    {
        return;
    }
    if (!m_sceneTexture)
    {
        glGenTextures(1, &m_sceneTexture);
    }
    glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_renderWidth, m_renderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    GLint filter = m_style.resolution == WorldResolution::Native ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!m_sceneFramebuffer)
    {
        glGenFramebuffers(1, &m_sceneFramebuffer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneTexture, 0);
    if (!m_sceneDepth)
    {
        glGenTextures(1, &m_sceneDepth);
    }
    glBindTexture(GL_TEXTURE_2D, m_sceneDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_renderWidth, m_renderHeight, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_sceneDepth, 0);
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
    m_meshes->statistics = {};
    m_viewportWidth = width;
    m_viewportHeight = height;
    if (HasPostProcessing())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFramebuffer);
        glViewport(0, 0, m_renderWidth, m_renderHeight);
    }
    else
    {
        SetPresentationViewport();
    }
    glClearColor(.02f, .04f, .07f, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(m_postProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    glUniform1i(glGetUniformLocation(m_postProgram, "scene"), 0);
    glUniform2f(glGetUniformLocation(m_postProgram, "texel"), 1.f / m_renderWidth, 1.f / m_renderHeight);
    glUniform1f(glGetUniformLocation(m_postProgram, "clockTime"), seconds);
    glUniform1i(glGetUniformLocation(m_postProgram, "pixelMode"),
                m_style.resolution == WorldResolution::Native ? 0 : 1);
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
        if (effect != SurfaceEffect::JetExhaust)
        {
            renderqueue::Color(.12f, .4f, .5f, .4f);
        }
    }
    renderqueue::Begin(GL_QUADS);
    renderqueue::TexCoord(0, 0);
    renderqueue::Vertex(vertices[0].x, vertices[0].y, vertices[0].z);
    renderqueue::TexCoord(1, 0);
    renderqueue::Vertex(vertices[1].x, vertices[1].y, vertices[1].z);
    renderqueue::TexCoord(1, 1);
    renderqueue::Vertex(vertices[2].x, vertices[2].y, vertices[2].z);
    renderqueue::TexCoord(0, 1);
    renderqueue::Vertex(vertices[3].x, vertices[3].y, vertices[3].z);
    renderqueue::End();
    if (m_effectProgram)
    {
        renderqueue::Effect(0, 0, 0);
    }
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

namespace tutorial
{
GameRenderer &GameRenderer::Get()
{
    static GameRenderer owner;
    return owner;
}
Renderer &GameRenderer::Backend()
{
    return *renderer;
}

// ============================================================================
// GameRenderer::Initialize
// ============================================================================
void GameRenderer::Initialize()
{
    profiling::Initialize();
    profiling::Scope timer(profiling::Timer::ResourceInitialize);
    models::ModelLibrary::Get();
    renderer.reset(new Renderer(kCanvasWidth, kCanvasHeight));
}

void GameRenderer::Shutdown()
{
    monitoring::GameMonitor::Get().Shutdown();
    game::SceneManager::Reset();
    rendercache::Clear();
    renderqueue::Shutdown();
    renderer.reset();
    profiling::Shutdown();
}

void GameRenderer::Draw()
{
    if (!renderer)
    {
        return;
    }
    profiling::BeginFrame(game::GameLevels::Mine().IsActive()    ? "mine"
                          : Game::Get().GetState().isBikeMounted ? "overworld_bike"
                          : Game::Get().GetState().isInObservatory
                              ? "observatory"
                              : (Game::Get().GetState().isInsideHome ? "interior" : "exterior"));
    renderqueue::BeginFrame();
    renderer->BeginScene(Game::Get().GetState().viewportWidth, Game::Get().GetState().viewportHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kCanvasWidth, kCanvasHeight, 0, -65536, 65536);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    {
        profiling::Scope timer(profiling::Timer::Background);
        profiling::GpuScope gpuTimer(profiling::Timer::Background);
        if (game::GameLevels::Mine().IsActive())
        {
            Drawing::DrawRectangle(0, 0, float(kCanvasWidth), float(kCanvasHeight), ColorRGBA(0, 0, 0));
        }
        else
        {
            game::SceneManager::Get(game::SceneKind::Background).Draw();
        }
        renderqueue::Flush();
    }
    {
        profiling::Scope timer(profiling::Timer::World);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        profiling::GpuScope gpuTimer(profiling::Timer::World);
        game::SceneManager::Get(game::SceneManager::Current()).Draw();
        renderqueue::Flush();
    }
    renderer->EndScene(Game::Get().GetState().animationTimeSeconds);
    {
        profiling::Scope timer(profiling::Timer::Hud);
        profiling::GpuScope gpuTimer(profiling::Timer::Hud);
        if (game::GameLevels::Mine().IsActive())
        {
            static_cast<game::MineScene &>(game::SceneManager::Get(game::SceneKind::Mine)).DrawDarkness();
        }
        game::SceneManager::Get(game::SceneKind::Hud).Draw();
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
