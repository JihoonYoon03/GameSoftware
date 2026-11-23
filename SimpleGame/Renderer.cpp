#include "stdafx.h"
#include <windows.h>
#include "Drawing.h"
#include "Game.h"
#include "Level.h"
#include "Monitoring/Monitor.h"
#include "ResourceAssets.h"
#include "Scene.h"

#include "Dependencies/glew/include/GL/glew.h"
#include "Profiler.h"
#include "RenderAssets.h"
#include "RenderCache.h"
#include "RenderQueue.h"
#include "Renderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#pragma comment(lib, "opengl32.lib")

namespace
{
const char *kFullscreenVertexBody = R"GLSL(
out vec2 uv;
void main() {
    vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
    uv = pos * 0.5 + 0.5;
}
)GLSL";

const char *kPostFragmentBody = R"GLSL(
uniform sampler2D scene;
uniform vec2 texel;
uniform float clockTime;
uniform int pixelMode;
in vec2 uv;
out vec4 fragColor;
vec3 bright(vec2 p) {
    vec3 c = texture(scene, clamp(p, vec2(0.001), vec2(0.999))).rgb;
    return c * smoothstep(0.72, 0.98, max(c.r, max(c.g, c.b)));
}
void main() {
    vec2 sampleUv = pixelMode != 0 ? (floor(uv / texel) + 0.5) * texel : uv;
    vec3 color = texture(scene, sampleUv).rgb;
    vec3 bloom = vec3(0.0);
    if (pixelMode == 0) {
        for (int i = 1; i <= 3; ++i) {
            vec2 d = texel * float(i * 2);
            float weight = float(4 - i);
            bloom += (bright(uv + vec2(d.x, 0.0)) + bright(uv - vec2(d.x, 0.0))) * weight;
            bloom += (bright(uv + vec2(0.0, d.y)) + bright(uv - vec2(0.0, d.y))) * weight;
        }
    }
    color += bloom * 0.012;
    color *= vec3(0.98, 1.04, 1.08);
    color = pow(vec3(1.0) - exp(-color * 1.35), vec3(0.9));
    vec2 p = sampleUv * 2.0 - 1.0;
    color *= 1.0 - 0.16 * dot(p, p);
    float noise = fract(sin(dot(uv + fract(clockTime * 0.07), vec2(12.9898, 78.233))) * 43758.5453);
    fragColor = vec4(color + (pixelMode != 0 ? 0.0 : (noise - 0.5) * 0.007), 1.0);
}
)GLSL";

const char *kMeshVertexSsboBody = R"GLSL(
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 vertexInfo;
layout(location = 4) in vec4 instanceAnchor;
layout(location = 5) in vec4 instanceScale;
layout(location = 6) in vec4 instanceRotation;
layout(location = 7) in vec4 instanceBody;
layout(location = 8) in vec4 instanceAccent;
layout(location = 9) in vec4 instanceEmission;
layout(location = 10) in vec4 instanceFinish;
layout(location = 11) in vec4 instanceTranslation;
layout(location = 12) in vec4 vertexJoints;
layout(location = 13) in vec4 vertexWeights;
layout(std430, binding = 0) readonly buffer BonePalettes {
    mat4 boneMatrices[];
};
uniform mat4 mvp;
uniform int shadowPass;
uniform int imported;
uniform vec2 shadowJitter;
uniform float hologramTime;
out vec3 normal;
out vec3 localPosition;
out vec4 tint;
out vec4 finish;
out float surface;
out float coverage;
out float holographic;
mat3 rotation(vec3 a) {
    float c = cos(a.x), s = sin(a.x), p = cos(a.y), q = sin(a.y), r = cos(a.z), t = sin(a.z);
    return mat3(c, s, 0., -s, c, 0., 0., 0., 1.) *
           mat3(p, 0., -q, 0., 1., 0., q, 0., p) *
           mat3(1., 0., 0., 0., r, t, 0., -t, r);
}
void main() {
    int j = int(vertexInfo.y + 0.5);
    mat3 rotate = rotation(instanceRotation.xyz);
    vec3 pos = vertexPosition;
    vec3 nrm = vertexNormal;
    int boneBase = int(floor(instanceRotation.w + 0.5));
    if (boneBase >= 0) {
        mat4 skin = boneMatrices[boneBase + int(vertexJoints.x + 0.5)] * vertexWeights.x
                  + boneMatrices[boneBase + int(vertexJoints.y + 0.5)] * vertexWeights.y
                  + boneMatrices[boneBase + int(vertexJoints.z + 0.5)] * vertexWeights.z
                  + boneMatrices[boneBase + int(vertexJoints.w + 0.5)] * vertexWeights.w;
        pos = (skin * vec4(vertexPosition, 1.0)).xyz;
        nrm = mat3(skin) * vertexNormal;
    }
    vec3 p = rotate * (pos * instanceScale.xyz) + instanceTranslation.xyz;
    normal = normalize(rotate * (nrm / instanceScale.xyz));
    localPosition = p;
    surface = vertexInfo.z;
    tint = vertexColor;
    if (vertexInfo.x > 2.5) tint *= instanceEmission;
    else if (vertexInfo.x > 1.5) tint *= instanceAccent;
    else if (vertexInfo.x > 0.5) tint *= instanceBody;
    else tint.a *= instanceBody.a;
    finish = instanceFinish;
    coverage = instanceBody.a;
    holographic = instanceAnchor.w;
    if (holographic > 0.5) {
        tint.rgb = mix(tint.rgb, vec3(0.22, 0.72, 1.0), 0.6) * (1.15 + 0.08 * sin(hologramTime));
        tint.a *= 0.64;
        coverage = 0.64;
    }
    if (vertexInfo.x > 2.5) surface = 4.0;
    if (shadowPass != 0) {
        float ground = instanceTranslation.w;
        p.xy += vec2(0.38, 0.55) * max(0.0, p.z - ground);
        p.z = ground;
    }
    vec2 screen = instanceAnchor.xy + vec2((p.x - p.y) * 36.0, (p.x + p.y) * 18.0 - p.z * 44.0);
    float depth = instanceAnchor.z + (p.x + p.y) * 36.0 + p.z * (1296.0 / 44.0);
    if (shadowPass != 0) {
        screen += shadowJitter;
        depth = -7000.0;
    }
    gl_Position = mvp * vec4(screen, depth, 1.0);
    bool instArmed = instanceScale.w > 0.5;
    if (((imported != 0 ? j == 1 : j == 10) && !instArmed) || (shadowPass != 0 && finish.w < 0.5)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    }
}
)GLSL";

const char *kMeshVertexUniformBody = R"GLSL(
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 vertexInfo;
layout(location = 4) in vec4 instanceAnchor;
layout(location = 5) in vec4 instanceScale;
layout(location = 6) in vec4 instanceRotation;
layout(location = 7) in vec4 instanceBody;
layout(location = 8) in vec4 instanceAccent;
layout(location = 9) in vec4 instanceEmission;
layout(location = 10) in vec4 instanceFinish;
layout(location = 11) in vec4 instanceTranslation;
layout(location = 12) in vec4 vertexJoints;
layout(location = 13) in vec4 vertexWeights;
uniform mat4 mvp;
uniform mat4 bones[32];
uniform int useBones;
uniform int armed;
uniform int shadowPass;
uniform int imported;
uniform vec2 shadowJitter;
uniform float hologramTime;
out vec3 normal;
out vec3 localPosition;
out vec4 tint;
out vec4 finish;
out float surface;
out float coverage;
out float holographic;
mat3 rotation(vec3 a) {
    float c = cos(a.x), s = sin(a.x), p = cos(a.y), q = sin(a.y), r = cos(a.z), t = sin(a.z);
    return mat3(c, s, 0., -s, c, 0., 0., 0., 1.) *
           mat3(p, 0., -q, 0., 1., 0., q, 0., p) *
           mat3(1., 0., 0., 0., r, t, 0., -t, r);
}
void main() {
    int j = int(vertexInfo.y + 0.5);
    mat3 rotate = rotation(instanceRotation.xyz);
    vec3 pos = vertexPosition;
    vec3 nrm = vertexNormal;
    if (useBones != 0) {
        mat4 skin = bones[int(vertexJoints.x + 0.5)] * vertexWeights.x
                  + bones[int(vertexJoints.y + 0.5)] * vertexWeights.y
                  + bones[int(vertexJoints.z + 0.5)] * vertexWeights.z
                  + bones[int(vertexJoints.w + 0.5)] * vertexWeights.w;
        pos = (skin * vec4(vertexPosition, 1.0)).xyz;
        nrm = mat3(skin) * vertexNormal;
    }
    vec3 p = rotate * (pos * instanceScale.xyz) + instanceTranslation.xyz;
    normal = normalize(rotate * (nrm / instanceScale.xyz));
    localPosition = p;
    surface = vertexInfo.z;
    tint = vertexColor;
    if (vertexInfo.x > 2.5) tint *= instanceEmission;
    else if (vertexInfo.x > 1.5) tint *= instanceAccent;
    else if (vertexInfo.x > 0.5) tint *= instanceBody;
    else tint.a *= instanceBody.a;
    finish = instanceFinish;
    coverage = instanceBody.a;
    holographic = instanceAnchor.w;
    if (holographic > 0.5) {
        tint.rgb = mix(tint.rgb, vec3(0.22, 0.72, 1.0), 0.6) * (1.15 + 0.08 * sin(hologramTime));
        tint.a *= 0.64;
        coverage = 0.64;
    }
    if (vertexInfo.x > 2.5) surface = 4.0;
    if (shadowPass != 0) {
        float ground = instanceTranslation.w;
        p.xy += vec2(0.38, 0.55) * max(0.0, p.z - ground);
        p.z = ground;
    }
    vec2 screen = instanceAnchor.xy + vec2((p.x - p.y) * 36.0, (p.x + p.y) * 18.0 - p.z * 44.0);
    float depth = instanceAnchor.z + (p.x + p.y) * 36.0 + p.z * (1296.0 / 44.0);
    if (shadowPass != 0) {
        screen += shadowJitter;
        depth = -7000.0;
    }
    gl_Position = mvp * vec4(screen, depth, 1.0);
    if (((imported != 0 ? j == 1 : j == 10) && armed == 0) || (shadowPass != 0 && finish.w < 0.5)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    }
}
)GLSL";

const char *kSoftShadowFragmentBody = R"GLSL(
uniform sampler2D image;
uniform vec2 texel;
in vec2 uv;
out vec4 fragColor;
void main() {
    float a = texture(image, uv).a * 0.2;
    for (int i = 1; i <= 2; ++i) {
        vec2 d = texel * float(i);
        a += (texture(image, uv + vec2(d.x, 0.0)).a + texture(image, uv - vec2(d.x, 0.0)).a +
              texture(image, uv + vec2(0.0, d.y)).a + texture(image, uv - vec2(0.0, d.y)).a) * 0.1;
    }
    fragColor = vec4(0.005, 0.01, 0.025, a);
}
)GLSL";

const char *kMeshFragmentBody = R"GLSL(
uniform int cartoon;
uniform int shadowPass;
in vec3 normal;
in vec3 localPosition;
in vec4 tint;
in vec4 finish;
in float surface;
in float coverage;
in float holographic;
out vec4 fragColor;
void main() {
    if (shadowPass != 0) {
        fragColor = vec4(0.0, 0.0, 0.0, 0.30 * tint.a);
        return;
    }
    // Stable ordered coverage preserves depth for the player, regardless of batch order.
    // Faded glass also uses depth coverage: blending it while writing depth depended on batch order.
    float depthCoverage = coverage < 0.5 ? min(coverage, tint.a) : coverage;
    if (depthCoverage < 0.99) {
        vec2 cell = mod(floor(gl_FragCoord.xy), 4.0);
        float rank = mod(cell.x, 2.0) * 8.0 + mod(cell.y, 2.0) * 12.0;
        rank = mod(rank, 16.0) + mod(floor(cell.x / 2.0) * 2.0 + floor(cell.y / 2.0) * 3.0, 4.0);
        rank = mod(rank, 16.0);
        if ((rank + 0.5) / 16.0 > depthCoverage) discard;
    }
    if (holographic > 0.5) {
        fragColor = vec4(tint.rgb, 1.0);
        return;
    }
    vec3 n = normalize(normal);
    vec3 l = normalize(vec3(-0.35, -0.45, 0.82));
    vec3 v = normalize(vec3(1.0, 1.0, 0.818));
    float diffuse = max(0.0, dot(n, l));
    float light = cartoon != 0 ? (diffuse < 0.22 ? 0.46 : diffuse < 0.65 ? 0.72 : 1.0) : 0.48 + 0.52 * diffuse;
    float rough = finish.x;
    float metal = finish.y;
    if (surface > 0.5 && surface < 1.5) { rough = 0.94; metal = 0.0; }
    if (surface > 1.5 && surface < 2.5) { rough = 0.35; metal = 0.75; }
    if (surface > 2.5 && surface < 3.5) { rough = 0.12; metal = 0.15; }
    if (surface > 4.5 && surface < 5.5) { rough = 0.96; metal = 0.0; }
    if (surface > 5.5 && surface < 6.5) { rough = 0.9; metal = 0.0; }
    if (surface > 6.5) { rough = 0.75; metal = 0.0; }
    float spec = pow(max(0.0, dot(n, normalize(l + v))), mix(90.0, 8.0, rough)) * (1.0 - rough) * 0.65;
    float rim = pow(1.0 - max(0.0, dot(n, v)), 3.0) * 0.08;
    vec3 color = tint.rgb * light + mix(vec3(0.7, 0.83, 0.9), tint.rgb, metal) * spec + vec3(0.2, 0.4, 0.48) * rim;
    if (surface > 3.5 && surface < 4.5) color = tint.rgb * (1.0 + finish.z * 0.25);
    fragColor = vec4(color, coverage < 0.5 ? 1.0 : tint.a);
}
)GLSL";

const char *kDarkFragmentBody = R"GLSL(
uniform sampler2D occupancy;
uniform sampler2D depthImage;
uniform float viewZoom;
uniform float mapSize;
uniform vec2 origin;
uniform vec4 lights[24];
uniform vec4 scanner;
uniform int lightCount;
uniform vec2 aim;
in vec2 uv;
out vec4 fragColor;
float trace(vec2 from, vec2 to) {
    vec2 delta = to - from;
    vec2 cell = floor(from);
    vec2 end = floor(to);
    vec2 stepDir = sign(delta);
    vec2 inverse = 1.0 / max(abs(delta), vec2(0.00001));
    vec2 edge = mix(cell, cell + 1.0, stepDir * 0.5 + 0.5);
    vec2 crossing = abs(edge - from) * inverse;
    for (int i = 0; i < 48; ++i) {
        if (all(equal(cell, end))) return 1.0;
        if (any(lessThan(cell, vec2(0.0))) || any(greaterThanEqual(cell, vec2(mapSize)))) return 0.0;
        if (texture(occupancy, (cell + 0.5) / mapSize).r > 0.5) return 0.0;
        if (crossing.x < crossing.y) { cell.x += stepDir.x; crossing.x += inverse.x; }
        else { cell.y += stepDir.y; crossing.y += inverse.y; }
    }
    return 0.0;
}
void main() {
    vec2 screen = vec2(uv.x * 1280.0, (1.0 - uv.y) * 800.0);
    screen = vec2(640.0, 456.0) + (screen - vec2(640.0, 456.0)) / viewZoom;
    vec2 s = screen - origin;
    float d = texture(depthImage, uv).r;
    float height = d < 0.9999 ? max(0.0, ((0.5 - d) * 131072.0 - 2.0 * s.y) / (88.0 + 1296.0 / 44.0)) : 0.0;
    float sum = (s.y + 44.0 * height) / 18.0;
    float difference = s.x / 36.0;
    vec2 world = vec2(sum + difference, sum - difference) * 0.5;
    float light = 0.0;
    for (int i = 0; i < 24; ++i) {
        if (i >= lightCount) break;
        vec4 source = lights[i];
        float distance = length(world - source.xy);
        if (distance >= source.z) continue;
        float amount = min(1.0, (1.0 - distance / source.z) * 1.8) * source.w;
        if (i == 1) {
            vec2 delta = world - source.xy;
            vec2 direction = vec2(delta.x - delta.y, (delta.x + delta.y) * 0.5);
            float alignment = length(direction) > 0.001 ? dot(normalize(direction), aim) : 1.0;
            amount *= smoothstep(0.86, 0.96, alignment);
        }
        if (amount <= 0.001) continue;
        float visible = (trace(source.xy, world) +
                         trace(source.xy + vec2(0.025, -0.025), world) +
                         trace(source.xy + vec2(-0.025, 0.025), world)) / 3.0;
        light = max(light, amount * visible);
    }
    // The sensor illuminates only its thin floor band; walls and people retain the darkness mask.
    if (scanner.w > 0.0 && height < 0.10) {
        float band = abs(length(world - scanner.xy) - scanner.z) / max(0.05, scanner.z * 0.024);
        light = max(light, (1.0 - smoothstep(0.25, 1.0, band)) * scanner.w);
    }
    fragColor = vec4(0.0, 0.0, 0.0, 1.0 - clamp(light, 0.0, 1.0));
}
)GLSL";

const char *kMaskCompositeFragmentBody = R"GLSL(
uniform sampler2D image;
uniform vec2 texel;
uniform int soften;
in vec2 uv;
out vec4 fragColor;
void main() {
    vec4 value = texture(image, uv);
    if (soften != 0) {
        value *= 0.25;
        value += (texture(image, uv + vec2(texel.x, 0.0)) + texture(image, uv - vec2(texel.x, 0.0)) +
                  texture(image, uv + vec2(0.0, texel.y)) + texture(image, uv - vec2(0.0, texel.y))) * 0.125;
        value += (texture(image, uv + texel) + texture(image, uv - texel) +
                  texture(image, uv + vec2(texel.x, -texel.y)) + texture(image, uv + vec2(-texel.x, texel.y))) * 0.0625;
    }
    fragColor = value;
}
)GLSL";

const char *kEffectVertexBody = R"GLSL(
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 vertexUv;
uniform mat4 mvp;
out vec2 uv;
out vec4 tint;
void main() {
    gl_Position = mvp * vec4(vertexPosition, 1.0);
    uv = vertexUv;
    tint = vertexColor;
}
)GLSL";

const char *kEffectFragmentBody = R"GLSL(
uniform float seconds;
uniform int effect;
in vec2 uv;
in vec4 tint;
out vec4 fragColor;
void main() {
    if (effect == 0) {
        float wave = sin(uv.x * 25.0 + uv.y * 13.0 - seconds * 2.2) * 0.5 + 0.5;
        float ripple = sin(length(uv - 0.5) * 48.0 - seconds * 3.0) * 0.5 + 0.5;
        vec3 water = mix(vec3(0.025, 0.13, 0.19), vec3(0.12, 0.52, 0.59), pow(wave * ripple, 3.0));
        fragColor = vec4(water, 0.86);
    } else if (effect == 1) {
        float height = 1.0 - uv.y;
        float sway = sin(height * 12.0 - seconds * 6.0) * 0.09 * height;
        float width = (1.0 - height) * 0.35 + 0.035;
        float flame = 1.0 - smoothstep(width * 0.3, width, abs(uv.x - 0.5 + sway));
        flame *= smoothstep(0.0, 0.08, uv.y) * smoothstep(0.0, 0.1, height);
        vec3 color = mix(vec3(1.0, 0.22, 0.035), vec3(1.0, 0.9, 0.3), flame * (1.0 - height));
        fragColor = vec4(color, flame);
    } else if (effect == 3) {
        float t = uv.x, y = uv.y - 0.5;
        float envelope = smoothstep(0.0, 0.07, t) * pow(max(0.0, 1.0 - t), 1.35);
        float separation = 0.19 * pow(1.0 - t, 1.7);
        float width = 0.045 + 0.045 * sin(t * 3.14159);
        float sway = sin(t * 18.0 - seconds * 17.0) * 0.012 * t;
        float cores = exp(-pow((y - separation + sway) / width, 2.0)) +
                      exp(-pow((y + separation + sway) / width, 2.0));
        float halo = exp(-pow(y / (0.26 * pow(1.0 - t, 0.7) + 0.012), 2.0)) * envelope;
        float alpha = clamp(cores * envelope * 0.93 + halo * 0.37, 0.0, 1.0) * tint.a;
        vec3 outer = mix(vec3(0.035, 0.16, 0.68), tint.rgb, 0.40);
        vec3 inner = mix(tint.rgb, vec3(0.55, 1.0, 1.0), 0.60);
        fragColor = vec4(mix(outer, inner, clamp(cores * envelope, 0.0, 1.0)), alpha);
    } else {
        float radius = length((uv - 0.5) * 2.0);
        float ring = 1.0 - smoothstep(0.02, 0.11, abs(radius - 0.78));
        fragColor = vec4(0.35, 0.93, 1.0, ring * 0.8);
    }
}
)GLSL";

bool IsIdentitySkin(const models::SkinMatrices &m)
{
    for (size_t j = 0; j < 32; ++j)
    {
        for (size_t r = 0; r < 4; ++r)
        {
            for (size_t c = 0; c < 4; ++c)
            {
                const float expected = r == c ? 1.f : 0.f;
                if (std::fabs(m[j * 16 + c * 4 + r] - expected) > 1.e-5f)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

void Multiply4x4(const float *a, const float *b, float *out)
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

namespace rendering
{
struct VertexBuffer
{
    GLuint id = 0;
    ~VertexBuffer()
    {
        if (id)
        {
            glDeleteBuffers(1, &id);
        }
    }
};
struct Mesh::Data
{
    using Vertex = models::ResourceVertex;
    std::shared_ptr<VertexBuffer> buffer;
    size_t vertexCount = 0;
    float roughness = .7f, metallic = .2f;
    bool ground = false;
    bool imported = false;
    bool staticResource = false;
    bool identityBindPose = true;
    bool holographic = false;
    models::SkinMatrices bindPose = {};
};
Mesh::Mesh() : data(new Data())
{
}
Mesh::~Mesh() = default;
} // namespace rendering

struct Renderer::MeshData
{
    struct Instance
    {
        // scale.w carries armed flag (1/0); rotation.w carries SSBO bone base index (-1 if unskinned).
        float anchor[4], scale[4], rotation[4], body[4], accent[4], emission[4], finish[4], translation[4];
    };
    struct Batch
    {
        std::shared_ptr<rendering::Mesh> mesh;
        models::SkinMatrices bones;
        bool armed = false;
        bool posed = false;
        int bindPoseSlot = -1;
        std::vector<Instance> instances;
        size_t instanceOffset = 0;
        size_t baseInstance = 0;
    };
    struct MaterialKey
    {
        models::Palette palette;
        float roughness = 0, metallic = 0;
        unsigned char ground = 0;
        bool operator==(const MaterialKey &o) const
        {
            return ground == o.ground && roughness == o.roughness && metallic == o.metallic &&
                   palette.body == o.palette.body && palette.accent == o.palette.accent &&
                   palette.emission == o.palette.emission;
        }
    };
    struct MaterialKeyHash
    {
        size_t operator()(const MaterialKey &k) const noexcept
        {
            // Bit-exact POD hash with zero heap allocation.
            const auto *bytes = reinterpret_cast<const unsigned char *>(&k);
            size_t h = 1469598103934665603ULL;
            for (size_t i = 0; i < sizeof(k); ++i)
            {
                h ^= bytes[i];
                h *= 1099511628211ULL;
            }
            return h;
        }
    };
    std::unordered_map<const models::Model *, std::shared_ptr<rendering::Mesh>> meshes;
    // Pure v3 resource bindings share one immutable GPU VBO across wrappers (RENDER_RESOURCE_REQUEST.md).
    std::unordered_map<unsigned int, std::weak_ptr<rendering::VertexBuffer>> resourceBuffers;
    struct MaterialEntry
    {
        std::shared_ptr<rendering::Material> value;
        std::uint64_t lastUse = 0;
    };
    std::unordered_map<MaterialKey, MaterialEntry, MaterialKeyHash> materials;
    std::uint64_t materialClock = 0;
    // Uniform-path batch lookup (fallback for < GL 4.3).
    std::unordered_multimap<size_t, size_t> batchKeys;
    // SSBO-path batch lookup: 1 batch per unique Model pointer regardless of pose/armed state!
    std::unordered_map<const models::Model *, size_t> ssboBatches;
    std::vector<Batch> batches;
    size_t batchCount = 0;
    models::ResourcePoseSampler poses;

    // Frame bone palette stream for GL 4.3+/4.5 SSBO skinning.
    std::vector<float> boneStream;
    GLuint boneSsbo = 0;
    size_t boneSsboCapacity = 0;
    bool useSsbo = false;
    bool useBaseInstance = false;

    struct ModelUniforms
    {
        GLint mvp = -1, cartoon = -1, shadowPass = -1, shadowJitter = -1, hologramTime = -1;
        GLint bones = -1, useBones = -1, armed = -1, imported = -1;
        void Initialize(GLuint program)
        {
            mvp = glGetUniformLocation(program, "mvp");
            cartoon = glGetUniformLocation(program, "cartoon");
            shadowPass = glGetUniformLocation(program, "shadowPass");
            shadowJitter = glGetUniformLocation(program, "shadowJitter");
            hologramTime = glGetUniformLocation(program, "hologramTime");
            bones = glGetUniformLocation(program, "bones[0]");
            useBones = glGetUniformLocation(program, "useBones");
            armed = glGetUniformLocation(program, "armed");
            imported = glGetUniformLocation(program, "imported");
        }
    } uniforms;
    struct PostUniforms
    {
        GLint scene = -1, texel = -1, clockTime = -1, pixelMode = -1;
        void Initialize(GLuint program)
        {
            scene = glGetUniformLocation(program, "scene");
            texel = glGetUniformLocation(program, "texel");
            clockTime = glGetUniformLocation(program, "clockTime");
            pixelMode = glGetUniformLocation(program, "pixelMode");
        }
    } postUniforms;
    struct ShadowUniforms
    {
        GLint image = -1, texel = -1;
        void Initialize(GLuint program)
        {
            image = glGetUniformLocation(program, "image");
            texel = glGetUniformLocation(program, "texel");
        }
    } shadowUniforms;
    struct DarkUniforms
    {
        GLint viewZoom = -1, mapSize = -1, occupancy = -1, depthImage = -1;
        GLint origin = -1, scanner = -1, aim = -1, lightCount = -1, lights = -1;
        void Initialize(GLuint program)
        {
            viewZoom = glGetUniformLocation(program, "viewZoom");
            mapSize = glGetUniformLocation(program, "mapSize");
            occupancy = glGetUniformLocation(program, "occupancy");
            depthImage = glGetUniformLocation(program, "depthImage");
            origin = glGetUniformLocation(program, "origin");
            scanner = glGetUniformLocation(program, "scanner");
            aim = glGetUniformLocation(program, "aim");
            lightCount = glGetUniformLocation(program, "lightCount");
            lights = glGetUniformLocation(program, "lights[0]");
        }
    } darkUniforms;
    struct MaskUniforms
    {
        GLint image = -1, texel = -1, soften = -1;
        void Initialize(GLuint program)
        {
            image = glGetUniformLocation(program, "image");
            texel = glGetUniformLocation(program, "texel");
            soften = glGetUniformLocation(program, "soften");
        }
    } maskUniforms;

    std::vector<Instance> instanceStream;
    size_t instanceBufferCapacity = 0;
    std::vector<std::function<void()>> overlays;
    std::vector<rendering::RenderObject> *owner = nullptr;
    size_t ownerSlot = 0;
    bool collecting = false;
    GLuint program = 0, instancesBuffer = 0, modelVao = 0, fullscreenVao = 0;
    GLuint darkProgram = 0, maskProgram = 0, mapTexture = 0, maskTexture = 0, maskFbo = 0;
    int maskWidth = 0, maskHeight = 0;
    GLint maskFilter = 0;
    GLuint shadowFbo = 0, shadowTexture = 0, shadowComposite = 0;
    int shadowWidth = 0, shadowHeight = 0;
    GLuint activeFbo = 0;
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
        if (fullscreenVao)
        {
            glDeleteVertexArrays(1, &fullscreenVao);
        }
        if (modelVao)
        {
            glDeleteVertexArrays(1, &modelVao);
        }
        if (boneSsbo)
        {
            glDeleteBuffers(1, &boneSsbo);
        }
        meshes.clear();
        resourceBuffers.clear();
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

bool Renderer::SubmitModel(const models::Model &model, RenderPoint screen, models::Point scale,
                           const models::Transform3D &transform, float depth, const models::Palette &palette,
                           const models::CharacterPose *pose, float shadowGroundZ)
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
        std::vector<rendering::Mesh::Data::Vertex> vertices;
        geometry.ground = model.ground;
        const auto &library = models::ModelLibrary::Get();
        geometry.imported = model.resourceId != 0;
        for (size_t joint = 0; joint < 32; ++joint)
        {
            for (size_t axis = 0; axis < 4; ++axis)
            {
                geometry.bindPose[joint * 16 + axis * 5] = 1;
            }
        }
        if (model.resourceId)
        {
            const auto &resource = models::ResourceAssets::Get(model.resourceId);
            geometry.staticResource = resource.boneCount == 0;
            if (!geometry.staticResource)
            {
                models::ResourceAssets::Sample(model.resourceId, nullptr, geometry.bindPose);
                geometry.identityBindPose = IsIdentitySkin(geometry.bindPose);
            }
            else
            {
                geometry.identityBindPose = true;
            }
            geometry.ground = geometry.ground || resource.name.find("ground/") == 0;
            geometry.holographic = resource.name.find("Transit_AirGuide_") != std::string::npos;
            if (model.count)
            {
                vertices = resource.vertices;
            }
        }
        else
        {
            geometry.staticResource = false;
            geometry.identityBindPose = true;
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
            vertices.push_back({p.x,
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
        const auto &upload = model.resourceId && !model.count
                                 ? models::ResourceAssets::Get(model.resourceId).vertices
                                 : vertices;
        geometry.vertexCount = upload.size();
        const bool sharedGeometry = model.resourceId && !model.count;
        if (sharedGeometry)
        {
            geometry.buffer = data.resourceBuffers[model.resourceId].lock();
        }
        if (!geometry.buffer)
        {
            geometry.buffer = std::make_shared<rendering::VertexBuffer>();
            glGenBuffers(1, &geometry.buffer->id);
            glBindBuffer(GL_ARRAY_BUFFER, geometry.buffer->id);
            glBufferData(GL_ARRAY_BUFFER, upload.size() * sizeof(rendering::Mesh::Data::Vertex),
                         upload.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            profiling::Count(profiling::Counter::MeshUploadBytes,
                             upload.size() * sizeof(rendering::Mesh::Data::Vertex));
            if (sharedGeometry)
            {
                data.resourceBuffers[model.resourceId] = geometry.buffer;
            }
        }
    }

    const float roughness = mesh->data->roughness;
    const float metallic = mesh->data->metallic;
    const bool castsShadow = !mesh->data->ground;
    if (data.owner)
    {
        const MeshData::MaterialKey materialKey{palette, roughness, metallic,
                                                static_cast<unsigned char>(mesh->data->ground ? 1 : 0)};
        constexpr size_t kMaterialCacheLimit = 512;
        auto foundMat = data.materials.find(materialKey);
        if (foundMat == data.materials.end())
        {
            if (data.materials.size() >= kMaterialCacheLimit)
            {
                auto oldest = std::min_element(
                    data.materials.begin(), data.materials.end(),
                    [](const auto &a, const auto &b) { return a.second.lastUse < b.second.lastUse; });
                data.materials.erase(oldest);
            }
            auto mat = std::make_shared<rendering::Material>();
            mat->palette = palette;
            mat->roughness = roughness;
            mat->metallic = metallic;
            mat->castsShadow = castsShadow;
            foundMat = data.materials.emplace(materialKey, MeshData::MaterialEntry{std::move(mat), 0}).first;
        }
        foundMat->second.lastUse = ++data.materialClock;
        if (data.owner->size() <= data.ownerSlot)
        {
            data.owner->resize(data.ownerSlot + 1);
        }
        (*data.owner)[data.ownerSlot++] = {mesh, foundMat->second.value};
    }

    const bool posed = pose && !mesh->data->staticResource;
    const bool armed = pose && pose->armed;
    size_t batchIndex = data.batchCount;
    float boneBaseAttr = -1.f;

    if (data.useSsbo)
    {
        auto foundBatch = data.ssboBatches.find(&model);
        if (foundBatch != data.ssboBatches.end())
        {
            batchIndex = foundBatch->second;
        }
        else
        {
            if (data.batchCount == data.batches.size())
            {
                data.batches.emplace_back();
            }
            batchIndex = data.batchCount++;
            auto &batch = data.batches[batchIndex];
            batch.mesh = mesh;
            batch.bindPoseSlot = -1;
            batch.instances.clear();
            data.ssboBatches.emplace(&model, batchIndex);
        }
        if (posed)
        {
            models::SkinMatrices bones;
            if (!model.resourceId)
            {
                bones = mesh->data->bindPose;
                for (size_t j = 0; j < 11; ++j)
                {
                    models::Point o = {}, x = {1, 0, 0}, y = {0, 1, 0}, z = {0, 0, 1};
                    o = pose->Apply(o, models::Joint(j));
                    x = pose->Apply(x, models::Joint(j));
                    y = pose->Apply(y, models::Joint(j));
                    z = pose->Apply(z, models::Joint(j));
                    float matrix[] = {x.x - o.x, x.y - o.y, x.z - o.z, 0, y.x - o.x, y.y - o.y, y.z - o.z, 0,
                                      z.x - o.x, z.y - o.y, z.z - o.z, 0, o.x,       o.y,       o.z,       1};
                    std::copy(matrix, matrix + 16, bones.begin() + j * 16);
                }
            }
            else
            {
                data.poses.Sample(model.resourceId, *pose, bones);
            }
            const size_t baseMatrix = data.boneStream.size() / 16;
            data.boneStream.insert(data.boneStream.end(), bones.begin(), bones.end());
            boneBaseAttr = float(baseMatrix);
        }
        else if (!mesh->data->identityBindPose)
        {
            auto &batch = data.batches[batchIndex];
            if (batch.bindPoseSlot < 0)
            {
                batch.bindPoseSlot = int(data.boneStream.size() / 16);
                data.boneStream.insert(data.boneStream.end(), mesh->data->bindPose.begin(),
                                       mesh->data->bindPose.end());
            }
            boneBaseAttr = float(batch.bindPoseSlot);
        }
    }
    else
    {
        models::SkinMatrices bones;
        const bool needsBones = posed || !mesh->data->identityBindPose;
        if (needsBones)
        {
            if (posed && model.resourceId)
            {
                data.poses.Sample(model.resourceId, *pose, bones);
            }
            else
            {
                bones = mesh->data->bindPose;
                for (size_t j = 0; posed && !model.resourceId && j < 11; ++j)
                {
                    models::Point o = {}, x = {1, 0, 0}, y = {0, 1, 0}, z = {0, 0, 1};
                    o = pose->Apply(o, models::Joint(j));
                    x = pose->Apply(x, models::Joint(j));
                    y = pose->Apply(y, models::Joint(j));
                    z = pose->Apply(z, models::Joint(j));
                    float matrix[] = {x.x - o.x, x.y - o.y, x.z - o.z, 0, y.x - o.x, y.y - o.y, y.z - o.z, 0,
                                      z.x - o.x, z.y - o.y, z.z - o.z, 0, o.x,       o.y,       o.z,       1};
                    std::copy(matrix, matrix + 16, bones.begin() + j * 16);
                }
            }
        }
        size_t key = std::hash<const models::Model *>{}(&model);
        if (needsBones)
        {
            key ^= std::hash<std::string_view>{}(
                std::string_view(reinterpret_cast<const char *>(bones.data()), sizeof(bones)));
        }
        const auto candidates = data.batchKeys.equal_range(key);
        for (auto it = candidates.first; it != candidates.second; ++it)
        {
            const auto &batch = data.batches[it->second];
            if (batch.mesh == mesh && batch.armed == armed && batch.posed == needsBones &&
                (!needsBones || std::memcmp(batch.bones.data(), bones.data(), sizeof(bones)) == 0))
            {
                batchIndex = it->second;
                break;
            }
        }
        if (batchIndex == data.batchCount)
        {
            if (data.batchCount == data.batches.size())
            {
                data.batches.emplace_back();
            }
            auto &batch = data.batches[data.batchCount++];
            batch.mesh = mesh;
            if (needsBones)
            {
                batch.bones = bones;
            }
            batch.armed = armed;
            batch.posed = needsBones;
            batch.instances.clear();
            data.batchKeys.emplace(key, batchIndex);
        }
    }

    MeshData::Instance instance = {
        {screen.x, screen.y, depth, mesh->data->holographic ? 1.f : 0.f},
        {scale.x, scale.y, scale.z, armed ? 1.f : 0.f},
        {transform.yaw, transform.pitch, transform.roll, boneBaseAttr},
        {},
        {},
        {},
        {roughness, metallic, 1.f, castsShadow ? 1.f : 0.f},
        // The fourth component carries the receiver plane without enlarging the instance stream.
        {transform.position.x, transform.position.y, transform.position.z, shadowGroundZ}};
    std::copy(palette.body.begin(), palette.body.end(), instance.body);
    std::copy(palette.accent.begin(), palette.accent.end(), instance.accent);
    std::copy(palette.emission.begin(), palette.emission.end(), instance.emission);
    data.batches[batchIndex].instances.push_back(instance);
    ++data.statistics.objects;
    data.statistics.triangles += unsigned(mesh->data->vertexCount / 3);
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
void Renderer::ReleaseModel(const models::Model &model)
{
    m_meshes->meshes.erase(&model);
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
    for (auto &draw : m_meshes->overlays)
    {
        draw();
    }
    m_meshes->overlays.clear();
    renderqueue::Flush();
}
const rendering::FrameStatistics &Renderer::GetFrameStatistics() const
{
    return m_meshes->statistics;
}

void Renderer::DrawModels()
{
    auto &data = *m_meshes;
    if (!data.batchCount)
    {
        return;
    }
    const GLuint originalFbo = data.activeFbo;
    float proj[16], mv[16], mvp[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    Multiply4x4(proj, mv, mvp);
    const float holoTime = float(std::fmod(double(GetTickCount64()) * .0018, 6.283185307179586));

    if (!data.instancesBuffer)
    {
        glGenBuffers(1, &data.instancesBuffer);
    }
    // One orphaned upload is reused by ground, shadow and surface passes.
    data.instanceStream.clear();
    for (size_t index = 0; index < data.batchCount; ++index)
    {
        auto &batch = data.batches[index];
        batch.baseInstance = data.instanceStream.size();
        batch.instanceOffset = batch.baseInstance * sizeof(MeshData::Instance);
        data.instanceStream.insert(data.instanceStream.end(), batch.instances.begin(), batch.instances.end());
    }
    const size_t instanceBytes = data.instanceStream.size() * sizeof(MeshData::Instance);
    if (instanceBytes > data.instanceBufferCapacity)
    {
        data.instanceBufferCapacity = (std::max)(instanceBytes, data.instanceBufferCapacity * 2);
    }
    glBindBuffer(GL_ARRAY_BUFFER, data.instancesBuffer);
    glBufferData(GL_ARRAY_BUFFER, data.instanceBufferCapacity, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instanceBytes, data.instanceStream.data());
    profiling::Count(profiling::Counter::StreamUploadBytes, instanceBytes);

    if (data.useSsbo && !data.boneStream.empty())
    {
        if (!data.boneSsbo)
        {
            glGenBuffers(1, &data.boneSsbo);
        }
        const size_t boneBytes = data.boneStream.size() * sizeof(float);
        if (boneBytes > data.boneSsboCapacity)
        {
            data.boneSsboCapacity = (std::max)(boneBytes, data.boneSsboCapacity * 2);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, data.boneSsbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, data.boneSsboCapacity, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, boneBytes, data.boneStream.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, data.boneSsbo);
        profiling::Count(profiling::Counter::StreamUploadBytes, boneBytes);
    }

    auto bindModelPass = [&](int shadow) {
        glUseProgram(data.program);
        glBindVertexArray(data.modelVao);
        glUniformMatrix4fv(data.uniforms.mvp, 1, GL_FALSE, mvp);
        glUniform1i(data.uniforms.cartoon, m_style.cartoon ? 1 : 0);
        glUniform1i(data.uniforms.shadowPass, shadow);
        glUniform2f(data.uniforms.shadowJitter, 0, 0);
        glUniform1f(data.uniforms.hologramTime, holoTime);
        if (data.useBaseInstance)
        {
            glBindBuffer(GL_ARRAY_BUFFER, data.instancesBuffer);
            for (unsigned int a = 4; a < 12; ++a)
            {
                glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(MeshData::Instance),
                                      reinterpret_cast<void *>(size_t(a - 4) * 16));
            }
        }
    };

    auto drawPass = [&](int pass) {
        GLuint boundMeshBuffer = 0;
        int boundImported = -1;
        for (size_t index = 0; index < data.batchCount; ++index)
        {
            auto &batch = data.batches[index];
            auto &mesh = *batch.mesh->data;
            if (mesh.ground != (pass == 0))
            {
                continue;
            }
            const GLuint meshBuf = mesh.buffer ? mesh.buffer->id : 0;
            if (meshBuf != boundMeshBuffer)
            {
                boundMeshBuffer = meshBuf;
                using V = rendering::Mesh::Data::Vertex;
                glBindBuffer(GL_ARRAY_BUFFER, meshBuf);
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
            }
            if (!data.useBaseInstance)
            {
                glBindBuffer(GL_ARRAY_BUFFER, data.instancesBuffer);
                for (unsigned int a = 4; a < 12; ++a)
                {
                    glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(MeshData::Instance),
                                          reinterpret_cast<void *>(batch.instanceOffset + size_t(a - 4) * 16));
                }
            }
            const int imp = mesh.imported ? 1 : 0;
            if (imp != boundImported)
            {
                glUniform1i(data.uniforms.imported, imp);
                boundImported = imp;
            }
            if (!data.useSsbo)
            {
                glUniform1i(data.uniforms.useBones, batch.posed ? 1 : 0);
                if (batch.posed)
                {
                    glUniformMatrix4fv(data.uniforms.bones, 32, GL_FALSE, batch.bones.data());
                }
                glUniform1i(data.uniforms.armed, batch.armed ? 1 : 0);
            }
            if (data.useBaseInstance)
            {
                glDrawArraysInstancedBaseInstance(GL_TRIANGLES, 0, GLsizei(mesh.vertexCount),
                                                  GLsizei(batch.instances.size()),
                                                  GLuint(batch.baseInstance));
            }
            else
            {
                glDrawArraysInstanced(GL_TRIANGLES, 0, GLsizei(mesh.vertexCount),
                                      GLsizei(batch.instances.size()));
            }
            profiling::Count(profiling::Counter::DrawCalls);
            profiling::Count(profiling::Counter::RenderedInstances, batch.instances.size());
            profiling::Count(profiling::Counter::SubmittedVertices,
                             mesh.vertexCount * batch.instances.size());
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
    bindModelPass(0);
    drawPass(0);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    renderqueue::Flush();

    const int width = (std::min)(640, m_renderWidth), height = (std::min)(400, m_renderHeight);
    if (!data.shadowTexture)
    {
        glGenTextures(1, &data.shadowTexture);
    }
    if (!data.shadowFbo)
    {
        glGenFramebuffers(1, &data.shadowFbo);
    }
    if (data.shadowWidth != width || data.shadowHeight != height)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, data.shadowTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, data.shadowFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, data.shadowTexture, 0);
        data.shadowWidth = width;
        data.shadowHeight = height;
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, data.shadowFbo);
    }
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    bindModelPass(1);
    drawPass(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, originalFbo);
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
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, data.shadowTexture);
    glUniform1i(data.shadowUniforms.image, 0);
    glUniform2f(data.shadowUniforms.texel, 1.f / width, 1.f / height);
    glBindVertexArray(data.fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    profiling::Count(profiling::Counter::DrawCalls);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    bindModelPass(0);
    drawPass(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    for (size_t index = 0; index < data.batchCount; ++index)
    {
        data.batches[index].instances.clear();
        data.batches[index].mesh.reset();
        data.batches[index].bindPoseSlot = -1;
    }
    data.batchCount = 0;
    data.batchKeys.clear();
    data.ssboBatches.clear();
    data.boneStream.clear();
}

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
    const int width = (std::min)(1280, m_renderWidth), height = (std::min)(800, m_renderHeight);
    if (!d.maskTexture)
    {
        glGenTextures(1, &d.maskTexture);
    }
    if (!d.maskFbo)
    {
        glGenFramebuffers(1, &d.maskFbo);
    }
    const GLint filter = m_style.resolution == WorldResolution::Native ? GL_LINEAR : GL_NEAREST;
    if (d.maskWidth != width || d.maskHeight != height)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, d.maskTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
        d.maskWidth = width;
        d.maskHeight = height;
        d.maskFilter = filter;
    }
    else
    {
        if (d.maskFilter != filter)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, d.maskTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            d.maskFilter = filter;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, d.maskFbo);
    }
    if (!d.mapTexture)
    {
        glGenTextures(1, &d.mapTexture);
    }
    glActiveTexture(GL_TEXTURE0);
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
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, game::kMapSize, game::kMapSize, 0, GL_RED, GL_UNSIGNED_BYTE,
                     cells.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        d.mapRevision = state.mapRevision;
        d.powered = powered;
    }
    auto &world = tutorial::Game::Get().GetState();
    auto origin = tutorial::Drawing::WorldToScreen(0, 0);
    std::array<float, 4> lights[24];
    int lightCount = 0;
    auto pushLight = [&](float x, float y, float r, float w) {
        if (lightCount < 24)
        {
            lights[lightCount++] = {x, y, r, w};
        }
    };
    pushLight(world.playerPosition.x, world.playerPosition.y, 2.8f, .8f);
    pushLight(world.playerPosition.x, world.playerPosition.y, 12.f, 1.f);
    for (const auto &lamp : state.lights)
    {
        if (!lamp.needsPower || powered)
        {
            pushLight(lamp.position.x, lamp.position.y, lamp.radius, 1.f);
        }
    }
    if (state.muzzleFlash > 0)
    {
        pushLight(world.playerPosition.x, world.playerPosition.y, 3.5f, 1.f);
    }
    if (state.riaCombat.muzzleFlash > 0)
    {
        pushLight(state.riaPosition.x, state.riaPosition.y, 3.f, 1.f);
    }
    for (const auto &npc : state.survivors)
    {
        if (npc.combat.muzzleFlash > 0)
        {
            pushLight(npc.position.x, npc.position.y, 3.f, 1.f);
        }
    }
    for (const auto &tracer : state.tracers)
    {
        pushLight(tracer.end.x, tracer.end.y, 1.3f, tracer.remaining / .12f);
    }
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glUseProgram(d.darkProgram);
    glUniform1f(d.darkUniforms.viewZoom, tutorial::WorldCamera::zoom);
    glUniform1f(d.darkUniforms.mapSize, float(game::kMapSize));
    glUniform1i(d.darkUniforms.occupancy, 0);
    glUniform1i(d.darkUniforms.depthImage, 1);
    glUniform2f(d.darkUniforms.origin, origin.x, origin.y);
    glUniform4f(d.darkUniforms.scanner, state.scannerOrigin.x, state.scannerOrigin.y, state.scannerRadius,
                state.scannerSeconds > 0 ? (std::min)(1.f, state.scannerSeconds / .3f) : 0);
    float angle = mine.Lighting().FlashlightAngle();
    glUniform2f(d.darkUniforms.aim, std::cos(angle), std::sin(angle));
    glUniform1i(d.darkUniforms.lightCount, lightCount);
    glUniform4fv(d.darkUniforms.lights, lightCount, lights[0].data());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_sceneDepth);
    glBindVertexArray(d.fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    profiling::Count(profiling::Counter::DrawCalls);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SetPresentationViewport();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(d.maskProgram);
    glBindTexture(GL_TEXTURE_2D, d.maskTexture);
    glUniform1i(d.maskUniforms.image, 0);
    glUniform2f(d.maskUniforms.texel, 1.f / width, 1.f / height);
    glUniform1i(d.maskUniforms.soften, m_style.resolution == WorldResolution::Native ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    profiling::Count(profiling::Counter::DrawCalls);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height), m_viewportWidth(width), m_viewportHeight(height),
      m_renderWidth(width), m_renderHeight(height), m_meshes(new MeshData()), m_assets(new RenderAssets())
{
    const char *glslVersion = GLEW_VERSION_4_5 ? "#version 450 core\n" : "#version 330 core\n";
    auto compileCore = [&](const char *vsBody, const char *fsBody) -> GLuint {
        const std::string vs = std::string(glslVersion) + vsBody;
        const std::string fs = std::string(glslVersion) + fsBody;
        return CompileProgram(vs.c_str(), fs.c_str());
    };
    if (GLEW_VERSION_3_3)
    {
        glGenVertexArrays(1, &m_meshes->fullscreenVao);
        glGenVertexArrays(1, &m_meshes->modelVao);
        glBindVertexArray(m_meshes->modelVao);
        for (GLuint a = 0; a < 14; ++a)
        {
            glEnableVertexAttribArray(a);
            glVertexAttribDivisor(a, (a >= 4 && a < 12) ? 1 : 0);
        }
        glBindVertexArray(0);

        m_meshes->useBaseInstance = GLEW_VERSION_4_2 && glDrawArraysInstancedBaseInstance != nullptr;
        if (GLEW_VERSION_4_3 && glBindBufferBase != nullptr)
        {
            m_meshes->program = compileCore(kMeshVertexSsboBody, kMeshFragmentBody);
            m_meshes->useSsbo = m_meshes->program != 0;
        }
        if (!m_meshes->program)
        {
            m_meshes->useSsbo = false;
            m_meshes->program = compileCore(kMeshVertexUniformBody, kMeshFragmentBody);
        }
        if (m_meshes->program)
        {
            m_meshes->uniforms.Initialize(m_meshes->program);
        }
        m_meshes->darkProgram = compileCore(kFullscreenVertexBody, kDarkFragmentBody);
        if (m_meshes->darkProgram)
        {
            m_meshes->darkUniforms.Initialize(m_meshes->darkProgram);
        }
        m_meshes->maskProgram = compileCore(kFullscreenVertexBody, kMaskCompositeFragmentBody);
        if (m_meshes->maskProgram)
        {
            m_meshes->maskUniforms.Initialize(m_meshes->maskProgram);
        }
        m_meshes->shadowComposite = compileCore(kFullscreenVertexBody, kSoftShadowFragmentBody);
        if (m_meshes->shadowComposite)
        {
            m_meshes->shadowUniforms.Initialize(m_meshes->shadowComposite);
        }
        m_effectProgram = compileCore(kEffectVertexBody, kEffectFragmentBody);
    }
    if (GLEW_VERSION_3_0)
    {
        CreatePostProcessing();
    }
    if (!m_effectProgram && GLEW_VERSION_2_0)
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
    if (m_msaaFramebuffer)
    {
        glDeleteFramebuffers(1, &m_msaaFramebuffer);
        glDeleteRenderbuffers(1, &m_msaaColor);
        glDeleteRenderbuffers(1, &m_msaaDepth);
    }
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
    bool resize = style.resolution != m_style.resolution || style.antialiasing != m_style.antialiasing;
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
        const char *glslVersion = GLEW_VERSION_4_5 ? "#version 450 core\n" : "#version 330 core\n";
        const std::string vs = std::string(glslVersion) + kFullscreenVertexBody;
        const std::string fs = std::string(glslVersion) + kPostFragmentBody;
        m_postProgram = CompileProgram(vs.c_str(), fs.c_str());
        if (m_postProgram)
        {
            m_meshes->postUniforms.Initialize(m_postProgram);
        }
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
    m_msaaSamples = 0;
    if (m_sceneFramebuffer && m_style.antialiasing && m_style.resolution == WorldResolution::Native &&
        GLEW_VERSION_3_0)
    {
        GLint supported = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &supported);
        int samples = (std::min)(4, supported);
        if (samples >= 2)
        {
            if (!m_msaaFramebuffer)
            {
                glGenFramebuffers(1, &m_msaaFramebuffer);
                glGenRenderbuffers(1, &m_msaaColor);
                glGenRenderbuffers(1, &m_msaaDepth);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFramebuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColor);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, m_renderWidth,
                                             m_renderHeight);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColor);
            glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepth);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, m_renderWidth,
                                             m_renderHeight);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepth);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
            {
                m_msaaSamples = samples;
            }
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_meshes->activeFbo = 0;
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
        const GLuint fbo = m_msaaSamples > 0 ? m_msaaFramebuffer : m_sceneFramebuffer;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        m_meshes->activeFbo = fbo;
        glViewport(0, 0, m_renderWidth, m_renderHeight);
    }
    else
    {
        m_meshes->activeFbo = 0;
        SetPresentationViewport();
    }
    if (m_msaaSamples > 0)
    {
        glEnable(GL_MULTISAMPLE); // Enabled before any world geometry is submitted.
    }
    else
    {
        glDisable(GL_MULTISAMPLE);
    }
    glClearColor(.02f, .04f, .07f, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
int Renderer::GetAntialiasingSamples() const
{
    return m_msaaSamples;
}
void Renderer::ResolveScene()
{
    if (m_msaaSamples <= 0 || m_meshes->activeFbo != m_msaaFramebuffer)
    {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneFramebuffer);
    glBlitFramebuffer(0, 0, m_renderWidth, m_renderHeight, 0, 0, m_renderWidth, m_renderHeight,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_msaaFramebuffer);
    // Colour captures and the mine's depth-based darkness read the resolved single-sample surface.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFramebuffer);
}
void Renderer::EndScene(float seconds)
{
    renderqueue::Flush();
    ResolveScene();
    profiling::Scope timer(profiling::Timer::PostProcess);
    profiling::GpuScope gpuTimer(profiling::Timer::PostProcess);
    if (!HasPostProcessing())
    {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_meshes->activeFbo = 0;
    SetPresentationViewport();
    glClearColor(.01f, .015f, .025f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(m_postProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    glUniform1i(m_meshes->postUniforms.scene, 0);
    glUniform2f(m_meshes->postUniforms.texel, 1.f / m_renderWidth, 1.f / m_renderHeight);
    glUniform1f(m_meshes->postUniforms.clockTime, seconds);
    glUniform1i(m_meshes->postUniforms.pixelMode, m_style.resolution == WorldResolution::Native ? 0 : 1);
    glBindVertexArray(m_meshes->fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    profiling::Count(profiling::Counter::DrawCalls);
    profiling::Count(profiling::Counter::SubmittedVertices, 3);
    glBindVertexArray(0);
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
void Renderer::DrawUiText(float x, float y, const std::string &text, int size, bool outlined)
{
    m_assets->DrawUtf8Text(x, y, text, size, outlined);
}
bool Renderer::DrawUiSprite(const std::string &name, float x, float y, float width, float height,
                            float fraction, bool nineSlice)
{
    return m_assets->DrawUiSprite(name, x, y, width, height, fraction, nineSlice);
}
size_t Renderer::UiSpriteCount() const
{
    return m_assets->UiSpriteCount();
}
bool Renderer::DrawBackdropLayer(const std::string &name)
{
    return m_assets->DrawBackdropLayer(name, GetStyle().cartoon, GetRenderWidth());
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
    Game::Get().StopSurface();
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
    const auto &state = Game::Get().GetState();
    const char *scene = game::GameLevels::Mine().IsActive()      ? "mine"
                        : state.isInsideHome                     ? "interior"
                        : state.isInObservatory                  ? "observatory"
                        : game::GameLevels::Chapter().IsActive() ? "frontier"
                        : state.surfaceExploration               ? "planet_surface"
                        : state.isBikeMounted                    ? "overworld_bike"
                                                                 : "exterior";
    const auto &style = renderer->GetStyle();
    profiling::BeginFrame(scene,
                          {state.viewportWidth, state.viewportHeight, state.isPaused, state.mapVisible,
                           state.ui.screen != UiScreen::None, state.isBikeMounted, int(style.resolution),
                           renderer->GetAntialiasingSamples(), style.cartoon, style.antialiasing});
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
        else if (Game::Get().GetState().surfaceExploration)
        {
            Drawing::DrawRectangle(0, 0, float(kCanvasWidth), float(kCanvasHeight), {.045f, .085f, .11f});
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
        platform::SwapBuffers();
    }
    profiling::EndFrame();
}
} // namespace tutorial
