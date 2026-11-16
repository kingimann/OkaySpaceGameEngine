#pragma once
// Material graph: a node graph that describes a surface (base colour, emission, roughness, metallic, alpha, normal)
// out of small maths nodes. This is the data model, the text file (.okaymatgraph), the checks, a CPU evaluator that
// needs no GPU, a software preview sphere, and a compiler that writes GLSL (version 120 style, like our GL renderer)
// and HLSL text. Nothing here is called by the live renderers yet (see docs/material_graph.md).
//
// Written from general shader maths only (linear interpolation, smoothstep, value noise on a hashed lattice, the
// (1 - N.V)^p Fresnel term, Rec.709 luma weights, Lambert + Blinn-Phong style lighting for the preview). No code was
// taken from any other project. Not to be confused with `.okaymat` (Render/Material.hpp), the flat preset file.
#include "okay/Graphics/Image.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay {
namespace matgraph {

/// Size of a value: Float is 1 component, Vec2 2, Vec3 3, Vec4 4. `Any` (inputs and outputs of the maths nodes) means
/// "as wide as the widest connected input"; a single float is widened to match (splat).
enum class PinType { Any = 0, Float = 1, Vec2 = 2, Vec3 = 3, Vec4 = 4 };

struct PinDef {
    const char* name;
    PinType type;
    float def[4];   // value used when nothing is connected (inputs only)
    int implicit = 0;   // inputs only: when unwired, 1 = use the surface UV, 2 = use the surface normal (instead of def)
};

struct NodeDef {
    const char* type;       // the name in the file, e.g. "Multiply"
    const char* category;   // for a palette: Input, Texture, Math, Colour, Effect, Output
    const char* help;       // one plain sentence
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
    bool hasValue;          // constant nodes: Float, Vec2, Vec3 keep their number in Node::value
    bool hasPath;           // texture nodes keep an image path in Node::path
};

/// All node types, in palette order (28 of them).
const std::vector<NodeDef>& NodeLibrary();
/// nullptr when the name is not a node type.
const NodeDef* FindNodeDef(const std::string& type);

struct Node {
    int id = 0;
    std::string type;
    float value[4] = {0, 0, 0, 0};
    std::string path;
    float posX = 0, posY = 0;                                 // where the editor draws it; no effect on the look
    std::map<std::string, std::array<float, 4>> defaults;     // per-input override of the pin default
};

/// One wire: output pin `fromPin` of `fromNode` into input pin `toPin` of `toNode`. An input takes at most one wire.
struct Link { int fromNode = 0; std::string fromPin; int toNode = 0; std::string toPin; };

struct Issue {
    bool error = true;   // false = warning (does not stop evaluation)
    int node = 0;        // 0 = about the whole graph
    std::string message;
};

struct Graph {
    std::vector<Node> nodes;
    std::vector<Link> links;

    int NextId() const;
    Node* Find(int id);
    const Node* Find(int id) const;
    /// Adds a node of a known type with its pins at their defaults; returns its id, or 0 for an unknown type.
    int Add(const std::string& type, float posX = 0, float posY = 0);
    int AddValue(const std::string& type, float x, float y = 0, float z = 0, float w = 0);
    /// Adds a wire; replaces an existing wire into the same input. Does not check types (Validate does).
    void Connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin);
    void SetDefault(int node, const std::string& pin, float x, float y = 0, float z = 0, float w = 0);
    int OutputNode() const;   // id of the Output node, 0 when there is none

    std::string ToText() const;
    /// False with `err` set ("line N: ...") when the text is not a material graph. A graph that parses can still fail Validate.
    static bool FromText(const std::string& text, Graph& out, std::string* err = nullptr);
    bool SaveToFile(const std::string& path) const;
    static bool LoadFromFile(const std::string& path, Graph& out, std::string* err = nullptr);
};

/// Limits the loader enforces so a hostile file cannot use all memory.
constexpr int kMaxNodes = 4096;
constexpr int kMaxLinks = 16384;
constexpr std::size_t kMaxFileBytes = 1u << 20;

/// Problems: unknown node type, duplicate id, missing or extra Output node, unknown pin, two wires into one input,
/// a type that does not fit (a wide value into a Float pin), a cycle. Warnings: nodes that do not reach the Output.
std::vector<Issue> Validate(const Graph& g);
bool HasErrors(const std::vector<Issue>& issues);

/// A value of 1 to 4 floats.
struct Value { float v[4] = {0, 0, 0, 0}; int n = 1; };

/// Looks up a texture on the CPU: path and uv in, RGBA out. Empty function: textures read as white, normal maps as flat.
using TextureSampler = std::function<void(const std::string& path, float u, float v, float rgba[4])>;

struct EvalContext {
    Vec2 uv{0, 0};
    Vec3 normal{0, 0, 1};     // surface normal, the same space as viewDir
    Vec3 viewDir{0, 0, 1};    // from the surface towards the eye
    float time = 0;           // seconds
    TextureSampler sampler;
};

struct Result {
    Vec3 baseColor{0.8f, 0.8f, 0.8f};
    Vec3 emission{0, 0, 0};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float alpha = 1.0f;
    Vec3 normal{0, 0, 1};     // tangent space: (0,0,1) is "no change"
};

/// A checked, ordered copy of a graph: compile once, run for every pixel.
class Program {
public:
    bool valid() const { return m_valid; }
    struct Step {
        int node = -1; std::string type; float value[4] = {0, 0, 0, 0}; std::string path;
        int nIn = 0, width = 1;          // width: how wide the Any pins of this node are
        int src[8] = {}; int srcPin[8] = {};   // src: step index, or -1 = use def, -2 = surface UV, -3 = surface normal
        float def[8][4] = {};
        int outN[4] = {1, 1, 1, 1};
    };
    std::vector<Step> steps;
    int outputStep = -1;
    std::vector<Issue> issues;
private:
    friend bool Compile(const Graph&, Program&);
    bool m_valid = false;
};

/// Validates and orders the graph. Returns false (and fills program.issues) when Validate finds an error.
bool Compile(const Graph& g, Program& program);
Result Run(const Program& p, const EvalContext& ctx);
/// Compile + Run in one call; false with `err` when the graph has an error.
bool Evaluate(const Graph& g, const EvalContext& ctx, Result& out, std::string* err = nullptr);

/// A lit sphere, `size` x `size` pixels, drawn by the CPU only. Surface normal and view direction come from the
/// sphere; the light is fixed up and to the left. Pixels outside the sphere are transparent.
Image RenderPreviewSphere(const Program& p, int size, float time = 0, const TextureSampler& sampler = TextureSampler());

enum class Target { Glsl120, Hlsl };
/// Text of one function `okay_material(...)` plus the helpers and texture declarations it needs. Empty with `err`
/// set when the graph has an error. See docs/material_graph.md for the exact shape.
std::string EmitShader(const Graph& g, Target target, std::string* err = nullptr);

/// A small example graph (brick-like noise tint, fresnel rim, texture-less) used by the editor's "New" and by the docs.
Graph ExampleGraph();

}  // namespace matgraph
}  // namespace okay
