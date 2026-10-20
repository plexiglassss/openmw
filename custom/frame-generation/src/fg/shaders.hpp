// GLSL 430 compute shaders for the frame-generation pipeline:
// luma pyramid -> coarse-to-fine block matching (optical flow) -> smoothing ->
// scene-change reduction -> bidirectional warp interpolation.
#pragma once

namespace fg::shaders
{
    // Full-res RGBA -> half-res luma (pyramid level 0).
    inline const char* kLumaInit = R"GLSL(
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0) uniform sampler2D srcTex;
layout(r16f, binding = 0) uniform writeonly image2D dstLuma;
uniform ivec2 dstSize;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, dstSize)))
        return;
    ivec2 srcSize = textureSize(srcTex, 0);
    ivec2 s = p * 2;
    vec3 c = texelFetch(srcTex, min(s, srcSize - 1), 0).rgb
           + texelFetch(srcTex, min(s + ivec2(1, 0), srcSize - 1), 0).rgb
           + texelFetch(srcTex, min(s + ivec2(0, 1), srcSize - 1), 0).rgb
           + texelFetch(srcTex, min(s + ivec2(1, 1), srcSize - 1), 0).rgb;
    float l = dot(c * 0.25, vec3(0.299, 0.587, 0.114));
    imageStore(dstLuma, p, vec4(l));
}
)GLSL";

    // Luma level N -> N+1 (2x2 box).
    inline const char* kLumaDown = R"GLSL(
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0) uniform sampler2D srcLuma;
uniform int srcLevel;
layout(r16f, binding = 0) uniform writeonly image2D dstLuma;
uniform ivec2 dstSize;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, dstSize)))
        return;
    ivec2 srcSize = textureSize(srcLuma, srcLevel);
    ivec2 s = p * 2;
    float l = texelFetch(srcLuma, min(s, srcSize - 1), srcLevel).r
            + texelFetch(srcLuma, min(s + ivec2(1, 0), srcSize - 1), srcLevel).r
            + texelFetch(srcLuma, min(s + ivec2(0, 1), srcSize - 1), srcLevel).r
            + texelFetch(srcLuma, min(s + ivec2(1, 1), srcSize - 1), srcLevel).r;
    imageStore(dstLuma, p, vec4(l * 0.25));
}
)GLSL";

    // Block matching at one pyramid level. One invocation = one 4x4 block.
    // Flow is stored in pixels of the current level; upsampled coarse flow
    // seeds the search. Positive flow = motion prev -> cur.
    inline const char* kMatch = R"GLSL(
#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0) uniform sampler2D lumaPrev;
layout(binding = 1) uniform sampler2D lumaCur;
layout(binding = 2) uniform sampler2D coarseFlow;
uniform int level;
uniform int hasCoarse;
uniform int radius;
layout(rg16f, binding = 0) uniform writeonly image2D flowOut;
uniform ivec2 flowSize;

float blockSAD(ivec2 base, ivec2 off, ivec2 levelSize)
{
    float sad = 0.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
        {
            ivec2 pc = clamp(base + ivec2(i, j), ivec2(0), levelSize - 1);
            ivec2 pp = clamp(pc - off, ivec2(0), levelSize - 1);
            sad += abs(texelFetch(lumaCur, pc, level).r - texelFetch(lumaPrev, pp, level).r);
        }
    return sad;
}

void main()
{
    ivec2 b = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(b, flowSize)))
        return;
    ivec2 levelSize = textureSize(lumaCur, level);
    ivec2 base = b * 4;

    vec2 pred = vec2(0.0);
    if (hasCoarse == 1)
        pred = texture(coarseFlow, (vec2(b) + 0.5) / vec2(flowSize)).xy * 2.0;
    ivec2 ipred = ivec2(round(pred));

    float bestCost = 1e30;
    ivec2 bestOff = ipred;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
        {
            ivec2 off = ipred + ivec2(dx, dy);
            float sad = blockSAD(base, off, levelSize) + 0.002 * float(abs(dx) + abs(dy));
            if (sad < bestCost)
            {
                bestCost = sad;
                bestOff = off;
            }
        }

    // Explicit zero-motion candidate with a small bonus: keeps UI and static
    // geometry rock solid instead of drifting with noisy matches.
    float sadZero = blockSAD(base, ivec2(0), levelSize);
    if (sadZero <= bestCost * 1.1)
        bestOff = ivec2(0);

    imageStore(flowOut, b, vec4(vec2(bestOff), 0.0, 0.0));
}
)GLSL";

    // 3x3 box smoothing of the finest flow field.
    inline const char* kSmooth = R"GLSL(
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0) uniform sampler2D flowIn;
layout(rg16f, binding = 0) uniform writeonly image2D flowOut;
uniform ivec2 flowSize;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, flowSize)))
        return;
    vec2 sum = vec2(0.0);
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i)
            sum += texelFetch(flowIn, clamp(p + ivec2(i, j), ivec2(0), flowSize - 1), 0).xy;
    imageStore(flowOut, p, vec4(sum / 9.0, 0.0, 0.0));
}
)GLSL";

    // Mean |luma delta| on the coarsest level -> SSBO. Single workgroup.
    inline const char* kReduce = R"GLSL(
#version 430 core
layout(local_size_x = 256) in;
layout(binding = 0) uniform sampler2D lumaPrev;
layout(binding = 1) uniform sampler2D lumaCur;
uniform int level;
layout(std430, binding = 0) buffer Stats { float sceneDiff; };
shared float partial[256];
void main()
{
    ivec2 size = textureSize(lumaCur, level);
    uint tid = gl_LocalInvocationIndex;
    int total = size.x * size.y;
    float sum = 0.0;
    for (int i = int(tid); i < total; i += 256)
    {
        ivec2 p = ivec2(i % size.x, i / size.x);
        sum += abs(texelFetch(lumaCur, p, level).r - texelFetch(lumaPrev, p, level).r);
    }
    partial[tid] = sum;
    barrier();
    for (uint s = 128u; s > 0u; s >>= 1u)
    {
        if (tid < s)
            partial[tid] += partial[tid + s];
        barrier();
    }
    if (tid == 0u)
        sceneDiff = partial[0] / float(total);
}
)GLSL";

    // Fullscreen-triangle present. A draw (not a blit) so presenting into a
    // multisampled default framebuffer (antialiasing > 0) stays legal.
    inline const char* kPresentVS = R"GLSL(
#version 430 core
out vec2 uv;
void main()
{
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

    inline const char* kPresentFS = R"GLSL(
#version 430 core
layout(binding = 0) uniform sampler2D tex;
in vec2 uv;
out vec4 color;
void main()
{
    color = vec4(texture(tex, uv).rgb, 1.0);
}
)GLSL";

    // Bidirectional warp at t = 0.5 with occlusion fallback and scene-cut guard.
    inline const char* kInterp = R"GLSL(
#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0) uniform sampler2D curTex;
layout(binding = 1) uniform sampler2D prevTex;
layout(binding = 2) uniform sampler2D flowTex;
layout(rgba8, binding = 0) uniform writeonly image2D dst;
uniform ivec2 fullSize;
uniform float sceneChangeThreshold;
layout(std430, binding = 0) buffer Stats { float sceneDiff; };

float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, fullSize)))
        return;
    vec2 uv = (vec2(p) + 0.5) / vec2(fullSize);
    vec3 cur0 = texture(curTex, uv).rgb;
    vec3 outc;

    if (sceneDiff > sceneChangeThreshold)
    {
        outc = cur0; // scene cut: duplicating beats hallucinating
    }
    else
    {
        // flow stored in half-res pixels -> full-res pixels -> uv
        vec2 f = texture(flowTex, uv).xy * 2.0;
        vec2 d = f / vec2(fullSize);
        vec3 a = texture(curTex, uv + 0.5 * d).rgb;
        vec3 b = texture(prevTex, uv - 0.5 * d).rgb;
        vec3 prev0 = texture(prevTex, uv).rgb;
        float errM = abs(lum(a) - lum(b));
        float err0 = abs(lum(cur0) - lum(prev0));
        float rgbDelta = max(max(abs(cur0.r - prev0.r), abs(cur0.g - prev0.g)),
                             abs(cur0.b - prev0.b));
        float motionPx = length(f);

        // Abrupt changes in a nominally static region are usually UI: save/load
        // overlays, menu text, progress indicators, or HUD elements appearing or
        // disappearing. The old zero-motion path blended old+new 50/50, which is
        // perceived as a one-frame flash. Snap to the current real pixel instead.
        if (motionPx < 1.0 && (err0 > 0.10 || rgbDelta > 0.18))
        {
            outc = cur0;
        }
        else if (err0 <= errM + 0.02)
        {
            outc = mix(cur0, prev0, 0.5); // stable static region / UI
        }
        else
        {
            vec3 warped = mix(a, b, 0.5);
            float conf = 1.0 - smoothstep(0.08, 0.30, errM);
            outc = mix(cur0, warped, conf); // low confidence -> lean on real frame
        }
    }

    imageStore(dst, p, vec4(outc, 1.0));
}
)GLSL";
}
