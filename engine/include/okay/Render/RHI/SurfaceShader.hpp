#pragma once
// ---------------------------------------------------------------------------
// OkayShader -> RHI: one .okayshader surface becomes one RHI shader for every backend.
// The OkayShader compiler already turns a surface into GLSL, HLSL and a CPU register
// program (OkayShader.hpp); this wraps them in a mesh vertex stage and a fragment
// main, so the OpenGL backend runs the GLSL and the Software backend runs the CPU
// program, from the same source.
//
// Round 1: unlit surfaces (`mode unlit;`), colour = albedo + emission, alpha = alpha.
// Lit surfaces need the lighting on the RHI first (docs/rendering/rhi.md, round 3).
//
// Vertex layout (locations): 0 position vec3, 1 normal vec3, 2 uv vec2, 3 colour vec3.
// Uniform block (SurfaceUniforms below): uMVP, uModel, uOkTime, uEye, then each property
// in its own 16 bytes, the order of Compiled::props (the same 16-byte slots as the HLSL
// cbuffer OkayShader emits, so the block can feed Direct3D unchanged later).
// Texture slot i is the surface's i-th declared texture (okt_<name>).
// ---------------------------------------------------------------------------
#include "okay/Render/RHI/RHI.hpp"
#include "okay/Render/OkayShader.hpp"

#include <vector>

namespace okay {
namespace rhi {

constexpr std::uint32_t kSurfaceMvpOffset = 0, kSurfaceModelOffset = 64, kSurfaceTimeOffset = 128,
                        kSurfaceEyeOffset = 144, kSurfacePropsOffset = 160;

/// Build the RHI shader description of an unlit surface. False (and `error` set) when the
/// surface did not compile or is lit.
bool ShaderFromSurface(const oshader::Compiled& surface, ShaderDesc& out, std::string* error = nullptr);

/// The uniform block bytes for a draw: matrices column-major (okay::Mat4::m), `props`
/// as ResolveProperties gives them (4 floats per property).
std::vector<std::uint8_t> SurfaceUniforms(const float mvp[16], const float model[16], float time, const float eye[3],
                                          const std::vector<float>& props);

} // namespace rhi
} // namespace okay
