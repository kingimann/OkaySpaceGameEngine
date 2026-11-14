#pragma once
// ---------------------------------------------------------------------------
// Material graph (phase 1): data model, checker, text file, CPU evaluator.
// Design and honest status: docs/foundations/material_graph.md.
//
//   MaterialGraph --Compile--> Program (checked IR) --> EvaluateProgram (CPU)
//                                                   \-> EmitFragment (GLSL/HLSL text,
//                                                       see MaterialGraphCodegen.hpp)
//
// Nothing here touches the renderers or the existing Material. GPU use is
// UNVERIFIED: only the CPU side is run by tests.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay::matgraph {

enum class PinType : uint8_t { Float, Vec2, Vec3, Vec4, Texture, Sampler };
int         PinComponents(PinType t);            ///< 1..4 for numeric pins, 0 otherwise
const char* PinTypeName(PinType t);              ///< "float", "vec2", ...
bool        IsNumeric(PinType t);
/// Can a value of type `from` feed a pin of type `to`? Same type, or Float into Vec2/3/4.
bool        CanConnect(PinType from, PinType to);

enum class NodeKind : uint8_t {
    Output,
    ConstFloat, ConstVec2, ConstVec3, ConstVec4,
    ParamFloat, ParamVec2, ParamVec3, ParamVec4,
    TextureObject, SamplerState, TextureSample,
    TexCoord, WorldPosition, WorldNormal, ViewVector, Time,
    Add, Multiply, Lerp, Clamp, Saturate, Power, OneMinus, Dot, Normalize,
    Fresnel, Panner, Noise,
    Count
};

/// What an unlinked input uses.
enum class DefKind : uint8_t { Required, Const, UV, WorldNormal, ViewVector, Time };

struct PinDef {
    const char* name;
    PinType     type;      ///< for a generic pin: Float (the narrowest allowed)
    bool        generic;   ///< takes Float..Vec4; the node's common type decides
    DefKind     def;
    float       c[4];      ///< default value when def == Const
};

struct NodeDef {
    NodeKind kind;
    const char* name;      ///< the word used in the file
    const char* category;
    std::vector<PinDef> in;
    std::vector<PinDef> out;   ///< out pins with generic == true have the common type
    int  valueCount;           ///< how many of Node::v[] the kind uses (0..4)
    bool usesText;             ///< parameter name or texture path
    bool normalizeLike;        ///< generic type must be Vec2 or wider (Normalize)
};

const NodeDef& GetNodeDef(NodeKind k);
bool NodeKindFromName(const std::string& name, NodeKind& out);
/// Index of the named input/output pin, or -1.
int  InPinIndex(NodeKind k, const std::string& pin);
int  OutPinIndex(NodeKind k, const std::string& pin);

/// Output node input order (also the order of Program::out).
enum OutputPin { kBaseColor = 0, kMetallic, kRoughness, kEmissive, kOpacity, kNormal, kOutputPinCount };

/// SamplerState::v[0] modes.
enum SamplerMode { kWrapLinear = 0, kClampLinear = 1, kWrapPoint = 2, kClampPoint = 3, kSamplerModeCount };

constexpr int kMaxNodes = 4096;
constexpr int kMaxLinks = 16384;
constexpr size_t kMaxFileBytes = 4u << 20;
constexpr size_t kMaxTextLen = 260;

struct Node {
    int         id = 0;
    NodeKind    kind = NodeKind::Output;
    float       x = 0, y = 0;             ///< canvas position (for the later editor)
    float       v[4] = {0, 0, 0, 0};
    std::string text;
};

struct Link { int fromNode = 0, fromPin = 0, toNode = 0, toPin = 0; };

enum class Severity : uint8_t { Info, Warning, Error };
struct Diagnostic {
    Severity    severity = Severity::Error;
    std::string code;        ///< short stable word, e.g. "cycle", "type", "missing-input"
    int         node = -1;   ///< node id or -1
    int         pin = -1;
    std::string message;
};

// ---- IR --------------------------------------------------------------------
enum class Op : uint8_t {
    Const, Param, TextureObj, SamplerObj, Uv, WorldPos, Normal, View, Time,
    Splat, Rgb, Alpha,
    Add, Mul, Lerp, Clamp, Sat, Pow, OneMinus, Dot, Norm, Fresnel, Panner, Noise, Sample
};
const char* OpName(Op o);

struct IrOp {
    Op       op = Op::Const;
    PinType  type = PinType::Float;
    int      a = -1, b = -1, c = -1, d = -1;   ///< operand op indices
    float    v[4] = {0, 0, 0, 0};              ///< Const value, Param default
    std::string s;                             ///< Param name, TextureObj path
    int      imm = 0;                          ///< SamplerObj mode
    int      node = -1;                        ///< source node id (for messages)
};

/// A checked graph: straight-line ops in dependency order, only what reaches the output.
struct Program {
    std::vector<IrOp> ops;
    int out[kOutputPinCount] = {-1, -1, -1, -1, -1, -1};
};

struct MaterialGraph {
    std::vector<Node> nodes;
    std::vector<Link> links;

    /// Adds a node with the next free id and returns the id.
    int  AddNode(NodeKind k, float x = 0, float y = 0);
    int  AddConst(float a);
    int  AddConst(float a, float b, float c);
    int  AddParam(NodeKind paramKind, const std::string& name, float a = 0, float b = 0, float c = 0, float d = 0);
    Node*       Find(int id);
    const Node* Find(int id) const;
    /// Adds a link (no checking; Check reports problems).
    void Connect(int fromNode, int fromPin, int toNode, int toPin);
    /// A graph with just an Output node (id 1) and default inputs.
    static MaterialGraph MakeDefault();

    /// Checks everything and, when there are no errors, builds the IR into *prog (may be null).
    /// Returns true when there are no Error diagnostics.
    bool Compile(Program* prog, std::vector<Diagnostic>* diags) const;
    bool Check(std::vector<Diagnostic>* diags) const { return Compile(nullptr, diags); }

    // ---- .okaymat graph text ("okaymatgraph 1") ----
    std::string ToText() const;
    struct ParseResult { bool ok = false; int line = 0; std::string error; };
    static ParseResult FromText(const std::string& text, MaterialGraph& out);
    bool SaveToFile(const std::string& path) const;
    static ParseResult LoadFromFile(const std::string& path, MaterialGraph& out);
    /// True when the first line says this text is a graph (not a version-1 Material).
    static bool LooksLikeGraph(const std::string& text);
};

bool SameGraph(const MaterialGraph& a, const MaterialGraph& b);

// ---- CPU evaluator -----------------------------------------------------------
struct MgValue { int n = 1; float v[4] = {0, 0, 0, 0}; };

/// Samples a texture: path, sampler mode, uv in, RGBA out. Default: opaque white.
using TextureFn = std::function<void(const std::string& path, int samplerMode, float u, float v, float rgba[4])>;

struct EvalInput {
    float uv[2] = {0, 0};
    float worldPos[3] = {0, 0, 0};
    float normal[3] = {0, 0, 1};
    float view[3] = {0, 0, 1};
    float time = 0;
    std::map<std::string, std::array<float, 4>> params;   ///< overrides by name
    TextureFn texture;
};

struct MaterialOutputs {
    float baseColor[3], metallic, roughness, emissive[3], opacity, normal[3];
};

/// The shared noise function (value noise, result 0..1). The shader text uses the same formula.
float Noise2(float u, float v, float scale);
/// Hash used by Noise2, exposed for tests.
float Hash12(float x, float y);

/// Evaluates every op of the program. `values` (optional) receives one value per op.
MaterialOutputs EvaluateProgram(const Program& p, const EvalInput& in, std::vector<MgValue>* values = nullptr);

} // namespace okay::matgraph
