#pragma once
// ---------------------------------------------------------------------------
// OkayShader: the engine's own small surface-shader language (.okayshader files).
//
// A shader file declares a few properties and textures and one `surface { ... }`
// block. The block reads the surface inputs (uv, position, view, normal, vcolor,
// time) and writes the surface outputs (albedo, emission, alpha, normal,
// metallic, specular, shininess). The renderers keep their own lighting: the
// surface block only decides what goes INTO it, so every renderer lights a
// custom surface exactly like a Standard one.
//
//   shader "Lava";
//   property float speed = 0.3 range(0, 2);
//   property color hot = (1.0, 0.55, 0.1);
//   surface {
//       float n = noise(uv * 4.0 + vec2(0.0, time * speed));
//       albedo = mix(vec3(0.2, 0.02, 0.0), hot, n);
//       emission = hot * smoothstep(0.6, 1.0, n);
//   }
//
// One source becomes three things: GLSL (OpenGL renderer), HLSL (Direct3D 11
// renderer) and a register program that the software renderer runs per pixel.
// All three come from the same checked syntax tree, and the helper functions
// (noise, hash, mod, smoothstep, normalize) are written the same way in each,
// so the renderers agree.
//
// Written by us from the GLSL 1.20 / GLSL ES 3.00 and HLSL Shader Model 4
// language references (what the generated text must be) and general compiler
// techniques (a hand-written lexer, a recursive-descent parser with precedence
// levels, a type-checking pass, tree-walking code emitters and a linear
// register code for the CPU). No code from any other shading language,
// compiler or engine. The language is laid out so a visual shader graph can
// compile to it later: a graph is a list of local definitions and output
// assignments, which is exactly the body of a surface block.
// See docs/rendering/shaders.md.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {
namespace oshader {

enum class Type : std::uint8_t { Void, Bool, Float, Vec2, Vec3, Vec4, Texture };
const char* TypeName(Type t);
int Width(Type t);   ///< floats in a value of the type (bool = 1, texture = 0)

/// One problem in a source file, with its 1-based line and column.
struct Diagnostic {
    int line = 0, col = 0;
    std::string message;
    /// "file:line:col: error: message" (file may be empty).
    std::string ToString(const std::string& file = std::string()) const;
};

/// A declared property: a value the material sets (shown in the Inspector).
struct Property {
    std::string name;
    Type  type = Type::Float;
    bool  isColor = false;        ///< declared as `color` (a vec3 picked with a colour picker)
    float def[4] = {0, 0, 0, 0};  ///< default value
    bool  hasRange = false;
    float minV = 0.0f, maxV = 1.0f;
};

/// A declared texture: sampled with sample(name, uv). The material can name a
/// picture for it; otherwise `defaultPath` (may be empty: then it reads white).
struct TextureSlot {
    std::string name;
    std::string defaultPath;
};

struct CpuProgram;   // the software renderer's form (opaque)

/// The result of compiling one source text.
struct Compiled {
    bool ok = false;
    std::vector<Diagnostic> errors;   ///< empty when ok
    std::string name;                 ///< from `shader "Name";` (else empty)
    bool unlit = false;               ///< `mode unlit;`: no lighting, colour = albedo + emission
    bool usesDiscard = false;
    std::vector<Property> props;
    std::vector<TextureSlot> textures;
    /// GLSL 1.20-compatible text: the uniforms (okp_<name>, okt_<name>, uOkTime)
    /// and `OkaySurface okaySurface(...)`. Also valid GLSL ES 3.00 / 3.30 when
    /// texture2D is defined to texture (the renderer adds that line for those).
    std::string glsl;
    /// HLSL SM4 text: cbuffer OkayCustom : register(b4), textures t9.. and
    /// `OkaySurface okaySurface(...)`.
    std::string hlsl;
    std::shared_ptr<const CpuProgram> cpu;
    /// First error as "file:line:col: error: ..." (empty when ok).
    std::string FirstError(const std::string& file = std::string()) const;
};

/// Compile a source. Never throws; hostile input gives errors, not crashes.
Compiled Compile(const std::string& source);

/// The GPU texture unit / register the n-th declared texture uses.
constexpr int kFirstTextureUnit = 9;
constexpr int kMaxTextures = 4;
constexpr int kMaxProperties = 32;
/// HLSL constant buffer register of the property block.
constexpr int kHlslPropRegister = 4;

// ---- Running a shader on the CPU (software renderer) -------------------------

struct SurfaceIn {
    float uv[2] = {0, 0};
    float position[3] = {0, 0, 0};
    float view[3] = {0, 0, 1};      ///< unit vector from the surface to the eye
    float normal[3] = {0, 1, 0};    ///< unit world normal (after the normal map)
    float vcolor[3] = {1, 1, 1};
    float time = 0.0f;
    // Defaults the outputs start from (the material's own values).
    float albedo[3] = {1, 1, 1};
    float emission[3] = {0, 0, 0};
    float alpha = 1.0f;
    float metallic = 0.0f, specular = 0.0f, shininess = 16.0f;
};
struct SurfaceOut {
    float albedo[3], emission[3], alpha, normal[3], metallic, specular, shininess;
    bool discard = false;
};
/// Texture lookup for the CPU path: slot = declared texture index, rgba out.
using SampleFn = void (*)(const void* ctx, int slot, float u, float v, float rgba[4]);

/// Run the surface for one pixel. `props` holds 4 floats per declared property
/// (see ResolveProperties). Thread-safe: the program is read-only.
void RunCpu(const CpuProgram& prog, const SurfaceIn& in, const float* props,
            SampleFn sample, const void* sampleCtx, SurfaceOut& out);
/// The CPU program's size (instructions), for tests.
int CpuInstructionCount(const CpuProgram& prog);

// ---- Material property values ---------------------------------------------------
// A material stores its values as text: "speed=0.5;hot=1,0.5,0.1;tex:noise=a.png".
// Names it does not know are kept (a shader edit does not lose them) and ignored.

/// 4 floats per property, defaults first, then the values in `text`.
std::vector<float> ResolveProperties(const Compiled& c, const std::string& text);
/// The picture for texture slot i (the material's, else the default, else "").
std::string ResolveTexture(const Compiled& c, int slot, const std::string& text);
/// Read / write one value in the text (n = number of floats).
bool GetParam(const std::string& text, const std::string& name, float* out, int n);
std::string SetParam(const std::string& text, const std::string& name, const float* v, int n);
std::string GetTextParam(const std::string& text, const std::string& key);
std::string SetTextParam(const std::string& text, const std::string& key, const std::string& value);

// ---- Shader files ---------------------------------------------------------------

/// Compiled shader of a file, cached. The file is checked again for changes at
/// most every `recheckMs` milliseconds (hot reload); a changed file is compiled
/// again and gets a new `generation`. Errors are logged once per generation.
/// Never null: a missing file gives a Compiled with ok=false.
struct ShaderFile {
    std::shared_ptr<const Compiled> compiled;
    std::uint64_t generation = 0;
    std::string path;
};
ShaderFile LoadShaderFile(const std::string& path, int recheckMs = 250);
/// Forget every cached file (tests; the editor after a project switch).
void ClearShaderFileCache();

/// The colour drawn for a shader that does not compile (magenta).
constexpr float kErrorColor[3] = {1.0f, 0.0f, 1.0f};

} // namespace oshader
} // namespace okay
