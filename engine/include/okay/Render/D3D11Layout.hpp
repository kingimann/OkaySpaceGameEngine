#pragma once
// The constant-buffer structs the Direct3D 11 renderer uploads, one per cbuffer in D3D11Hlsl.hpp (b0 CB, b2 SH, b3 PC).
// Kept in a header with no Direct3D include so tests/test_d3d11_layout.cpp can compare their sizes with the HLSL cbuffers
// (16-byte packing rules) on Linux. Moved here unchanged from D3D11Renderer.cpp except for the 2026-11 lighting additions.
namespace okay { namespace d3d11layout {

constexpr int kCookieRows = 8;   // light masks held at once: an atlas 64 px wide and 64 x kCookieRows tall (t8)

// Constant-buffer layout mirrored on the C++ side (must match the cbuffer, 16-byte
// rules: each float3 is followed by a float to fill its 16-byte row).
struct CB {
    float mvp[16];
    float model[16];
    float color[3];     float specular;
    float emissive[3];  float shininess;
    float lightDir[3];  float unlit;
    float lightColor[3]; float useTex;
    float ambient[3];   float hasNormal;
    float eye[3];       float normalStrength;
    float tiling[2];    float shadowAlpha; float alpha;
    float rimColor[3];  float shaderMode;
    float gradTop[3];   float toonBands;
    float gradBot[3];   float rimStr;
    float pbr[4];              // metallic, reflectivity, hasSpecMap, hasAo
    float pbr2[4];             // aoStrength, envOn, texOffset.x, texOffset.y
    float sky[12];             // 3 float4: top, horizon, bottom (xyz)
    float lightCount[4];       // .x = number of scene lights
    float lights[384];         // 96 float4: 16 lights x 6 rows
    float fog[4];              // x=on y=start z=end w=receive shadows
    float fogCol[4];           // xyz = fog colour, w = filmic pipeline flag (linear + ACES + gamma)
    float fog2[4];             // mode, density, height, falloff
    float post[4];             // x = leave the colour linear for the post chain
};
// Sky + post-chain constants (cbuffer PC, b3); see the HLSL for each slot.
struct PostCB {
    float invVP[16];
    float eye[3], sunSize;
    float sunDir[3], sunHalo;
    float zenith[3], sunDisc;
    float horizon[3], haze;
    float ground[3], night;
    float sunColor[3], starDensity;
    float cloud[4];
    float cloudOff[2], time, lin;
    float p0[4];
    float p1[4];
    float p2[4];
    float lift[3], aspect;
    float gamma[3], thresh;
    float gain[3], radius;
    float texel[2], dir[2];
    float p7[4];
    float top[3], mode;
    float hor[3], horizonPos;
    float bot[3], starsOn;
    float sunPos[2], sunRad, sunOn;
    float viewSize[4];
};
// Per-frame shadow + ambient constants (cbuffer SH, b2).
struct ShadowCB {
    float casVP[16 * 4];
    float casSphere[4 * 4];    // xyz centre, w radius
    float casInfo[4 * 4];      // x texel world size
    float shadow[4];           // on, cascade count, cascade size, softness
    float shadow2[4];          // sun index, sun strength, sun bias, local tile size
    float ambSky[4];
    float ambGround[4];
    float localVP[16 * 16];
    float lshadow[4 * 16];     // first tile (-1 none), strength, bias, texel at 1m
    float sunExtra[4];         // sun Soft Edges, sun shadow fade distance, cookie atlas rows, unused
};


static_assert(sizeof(CB) % 16 == 0, "cbuffer CB size must be a multiple of 16 bytes");
static_assert(sizeof(PostCB) % 16 == 0, "cbuffer PC size must be a multiple of 16 bytes");
static_assert(sizeof(ShadowCB) % 16 == 0, "cbuffer SH size must be a multiple of 16 bytes");

}} // namespace okay::d3d11layout
