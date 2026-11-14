#pragma once
// ---------------------------------------------------------------------------
// Material graph: GLSL and HLSL text from the checked IR, and a small own
// text checker for that output. Design: docs/foundations/material_graph.md.
//
// The output is a SNIPPET (macros, uniforms, helper functions and one function
// `okm_material`) to be inserted into a shader later. It is not a full shader.
// UNVERIFIED: never given to a real GLSL or HLSL compiler or run on a GPU.
// ---------------------------------------------------------------------------
#include "okay/Render/MaterialGraph.hpp"
#include <string>
#include <vector>

namespace okay::matgraph {

enum class ShaderLang { GLSL, HLSL };

struct EmitOptions {
    std::string functionName = "okm_material";
    int firstTextureSlot = 8;   ///< HLSL: Texture2D registers t8.. and SamplerState s8.. (a guess; reconcile in phase 4)
    int paramRegister = 5;      ///< HLSL: cbuffer OkmParams : register(b5) (a guess; reconcile in phase 4)
};

struct ParamBinding   { std::string name; PinType type; float def[4]; };   ///< uniform `okm_p_<name>`
struct TextureBinding { std::string path; int index; };                    ///< `okm_tex_<index>`
struct SamplerBinding { int mode; int index; };                            ///< `okm_smp_<index>`

struct EmittedShader {
    std::string text;      ///< the whole snippet
    std::string body;      ///< only the statements of the function body ("    type vN = expr;")
    std::vector<ParamBinding> params;
    std::vector<TextureBinding> textures;
    std::vector<SamplerBinding> samplers;
};

EmittedShader EmitFragment(const Program& p, ShaderLang lang, const EmitOptions& opt = EmitOptions());

/// Lexer-level check of generated text: brackets balance, every name is declared before use,
/// numbers are well formed, and no token of the other language leaked in.
/// Returns one message per problem; empty means it passed. This is NOT a compiler.
std::vector<std::string> CheckShaderText(const std::string& text, ShaderLang lang);

} // namespace okay::matgraph
