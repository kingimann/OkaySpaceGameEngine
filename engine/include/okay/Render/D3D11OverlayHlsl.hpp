#pragma once
// The full-screen overlay shaders of the Xbox UWP build (the software-painted 2D / UI layer drawn over the 3D frame) and the HLSL text the offline
// shader bundle is built from: the renderer's text (D3D11Hlsl.hpp, unchanged, the desktop still compiles only that) followed by these two entry points.
// One triangle from SV_VertexID (no vertex buffer); the texture is PREMULTIPLIED alpha, so the pass blends One / InvSrcAlpha. Pure text, no Direct3D.
// Written from the HLSL documentation (SV_VertexID, Texture2D.Sample). UNVERIFIED on a GPU / console.
#include "okay/Render/D3D11Hlsl.hpp"
#include <string>

namespace okay {

inline const char* D3D11OverlayHlslSource() {
    return "Texture2D uUi : register(t0);\n"
           "SamplerState uUiSamp : register(s0);\n"
           "struct OverlayVSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
           "OverlayVSOut VSOverlay(uint id : SV_VertexID) {\n"
           "  float2 p = float2((id << 1) & 2, id & 2);\n"
           "  OverlayVSOut o; o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); o.uv = p; return o;\n"
           "}\n"
           "float4 PSOverlay(OverlayVSOut i) : SV_TARGET { return uUi.Sample(uUiSamp, i.uv); }\n";
}

/// What scripts/xbox-compile-shaders.ps1 compiles (every entry point of ShaderBundle.hpp's table) and what the manifest's source hash covers.
inline const std::string& D3D11BundleSourceText() {
    static const std::string t = std::string(D3D11HlslSource()) + "\n" + D3D11OverlayHlslSource();
    return t;
}

} // namespace okay
