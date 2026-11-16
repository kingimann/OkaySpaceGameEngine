# Material graph (shader graph foundation)

Status: **foundation, off by default.** The file format, the checks, a CPU evaluator, a preview sphere, GLSL and HLSL text and a small editor window exist. No renderer reads a material graph yet, so a graph does not change what a game draws. Feature map: [26.1](feature_map/20-35-planned.md#261-visual-shader-graphs). Try it: `docs/try_it.md` row `matgraph`.

## What existed before, and the gap

| Existing | What it is | Gap for a material graph |
|---|---|---|
| `.okaymat` (`Render/Material.hpp`) | A flat preset: colour, emissive, specular, texture, tiling, shader style, rim, outline | No nodes, no maths, no way to combine textures. The name is taken, so a graph is `.okaymatgraph` |
| MeshRenderer shader modes (Standard, Unlit, Toon, Gradient, Fresnel, Iridescent, Hologram, Posterize, Velvet) | Nine fixed styles, each written once per renderer (GLSL 120 in `GLRenderer`, HLSL in `D3D11Renderer` and `D3D12Renderer`, CPU in `SoftwareRenderer`) | A new look means editing four renderers |
| Visual Script editor (`editor/VisualScriptEditor.hpp`, `VisualScript/NodeGraph`) | Node canvas for gameplay logic; its pins carry script values (flow, numbers, objects) | Not reusable as it is for typed shader values. The Material Graph window has its own small canvas; sharing the canvas code is a later step |
| `Material` window in `editor/main.cpp` | Edits 8 fields of an `.okaymat`, with a hand-drawn preview ball | Not graph based |

Everything here is **new and additive**: `engine/include/okay/Render/MaterialGraph.hpp`, `engine/src/Render/MaterialGraph.cpp`, `editor/MaterialGraphWindow.{hpp,cpp}`, `tests/test_matgraph.cpp`, `examples/material_graph/`.

## The file (`.okaymatgraph`, version 1)

Plain text, one line per item. `#` starts a comment line. Numbers always use `.` as the decimal point.

```
okaymatgraph 1
node 2 TextureSample pos=0,0 path=proc:checker      # path runs to the end of the line (spaces allowed)
node 3 Vec3 v=0.9,0.55,0.2,0 pos=0,150              # v= is the constant of Float, Vec2 and Vec3 nodes (4 numbers)
node 1 Output pos=700,60
def 4.B 0.5,0,0,0                                   # an unwired input takes this instead of the pin's default
link 2.RGB 4.A                                      # <node>.<output pin> <node>.<input pin>
```

* An input takes at most one wire. Several wires may leave one output.
* The loader refuses (with `line N: ...`): a missing or wrong first line, a version other than 1, numbers that are not finite, unknown fields or lines, a wire or `def` that does not parse, more than 4096 nodes, 16384 wires or 1 MiB.
* A file that loads can still fail `Validate`: unknown node type, two nodes with one id, no Output or more than one, a wire to a pin or node that does not exist, two wires into one input, a value that does not fit the pin, a cycle. Nodes that do not reach the Output are a warning and are ignored.

### Types

Values are 1 to 4 numbers (Float, Vec2, Vec3, Vec4). A Float fits any pin (it is copied to every component). A wider value fits a narrower vector pin by dropping the tail (Vec4 into a Vec3 pin). A vector never fits a Float pin (use **Split**), and a short vector never fits a longer pin. The maths nodes (`Any` pins) take any width and are as wide as their widest input.

## The 28 nodes

| Group | Nodes |
|---|---|
| Input | Float, Vec2, Vec3 (also a colour), UV, Time, Normal, ViewDir |
| Texture | TextureSample (outputs RGBA, RGB, A), NormalMap (tangent space, Strength) |
| Math | Add, Subtract, Multiply, Divide, Lerp, Clamp, Saturate, Pow, Dot, Normalize, OneMinus, Sin, MakeVec3, Split |
| Effect | Fresnel, Noise |
| Colour | Desaturate, Contrast |
| Output | Output: BaseColor, Emission, Roughness, Metallic, Alpha, Normal (tangent space) |

The exact pins and defaults are in `NodeLibrary()` (`MaterialGraph.cpp`); each node has a one-sentence help text.

## CPU evaluator

```cpp
okay::matgraph::Graph g; okay::matgraph::Graph::LoadFromFile("a.okaymatgraph", g);
okay::matgraph::EvalContext ctx; ctx.uv = {0.3f, 0.7f}; ctx.normal = {0,0,1}; ctx.viewDir = {0,0,1}; ctx.time = 1.0f;
okay::matgraph::Result r;  std::string err;
if (okay::matgraph::Evaluate(g, ctx, r, &err)) { /* r.baseColor, r.emission, r.roughness, r.metallic, r.alpha, r.normal */ }
```

For many pixels call `Compile` once and `Run` per pixel. `ctx.sampler` reads textures (path, u, v gives RGBA); without one textures read as white and normal maps as flat. `RenderPreviewSphere(program, size, time, sampler)` returns an `Image`: a sphere lit by one light (Lambert plus a Blinn-Phong style highlight whose size follows Roughness, metallic tints the highlight and removes the diffuse part), gamma 2.2. The sphere's UV is longitude (u) and latitude (v, growing downwards).

Rules worth knowing: `Pow` treats a negative base as 0; `Divide` by 0 gives 0 on the CPU (a GPU gives infinity); `Normalize` of a zero vector is zero; `Fresnel` is `(1 - clamp(N.V, 0, 1))^Power` and wants a unit normal (unwired: the surface normal); `Noise` is value noise: a hash of the lattice corners, smoothstep blended, times `Scale` cells per UV unit; `Desaturate` uses weights 0.2126, 0.7152, 0.0722; `NormalMap` reads `rgb * 2 - 1`, scales x and y by Strength and normalizes.

## GLSL and HLSL text (`EmitShader`)

`EmitShader(graph, Target::Glsl120 | Target::Hlsl)` returns text, or an empty string with an error for an invalid graph. Shape:

```
// okaymatgraph -> GLSL 120
uniform sampler2D okay_tex_<node id>;   // <path>      (HLSL: Texture2D, plus one SamplerState okay_samp)
float okay_hash(...), okay_noise(...)                  // only when a Noise node is used
void okay_material(vec2 uv, vec3 n_surface, vec3 view, float time,
                   out vec3 o_base_color, out vec3 o_emission, out float o_roughness,
                   out float o_metallic, out float o_alpha, out vec3 o_normal)
{ one line per node: <type> n<id>_<pin> = <expression>; ... then the six outputs }
```

GLSL uses only version-120 features (`texture2D`, no integers, `mix`, `fract`); every number is written as a float literal. Floats are widened with `vecN(x)` (HLSL: `floatN(x, x, x)`). `tests/test_matgraph.cpp` holds a golden string for each target. **Not done:** compiling the text with a real GLSL or HLSL compiler (none here), and calling it from the GL or D3D11 renderer. A GPU's `sin` is less exact than the CPU's, so `Noise` can differ slightly between them; the pattern idea is the same.

## Editor

Window menu > **Material Graph**, File > Examples > **Material Graph (shader nodes)**, or double-click a `.okaymatgraph` in the Project panel. The window shows the preview sphere (with a Time slider or Animate), Save / Open / New, an **Add Node** picker, the checks, a read-only canvas (nodes at their saved positions with their wires) and a node list where values are edited and each input's wire is picked from a drop-down. **Not done:** dragging nodes and wires on the canvas, undo, a search palette, a preview in the Scene view. `proc:<name>` texture paths (for example `proc:checker`) work in the preview.

## What each file was written from

No code, shader text, comments or tests were taken from another project (CLAUDE.md rule 19a).

* `MaterialGraph.hpp/.cpp`: the node set and the file format are our own design. Maths: linear interpolation, smoothstep (`3t^2 - 2t^3`), value noise on a hashed integer lattice (the hash `fract(sin(x*91.3458 + y*47.8231) * 28461.1357)` uses constants chosen here), the `(1 - N.V)^p` Fresnel term, Rec.709 luma weights (ITU-R BT.709, public standard), Lambert and Blinn-Phong style lighting (general graphics textbooks), Kahn's algorithm for the order and cycle check. GLSL 1.20 and HLSL syntax from the language specifications.
* `MaterialGraphWindow.cpp`: our own design on the Dear ImGui draw list (already part of the editor).
* `test_matgraph.cpp`: expected values computed by hand (shown in the comments) and goldens read over by hand.

## Not done, honestly

Not wired into any renderer; not compiled by a GPU compiler; no D3D12 path; no node dragging in the window; the node set is a first 28 (missing: vertex colour, world position, step, smoothstep, abs, floor, fract, min, max, remap, hue shift, gradient, Voronoi, parallax, custom function); materials cannot point at a graph yet; the editor window was syntax-checked and its preview logic is the tested library, but the window itself was **not run** (no display here).
