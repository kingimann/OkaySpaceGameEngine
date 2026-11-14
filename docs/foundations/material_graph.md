# Material graph: foundation design

Status: phase 1 (engine data and text generation) is written and unit tested on the CPU. Nothing in this page has run on a GPU. Everything GPU-side is UNVERIFIED.

## Goal

Let a person build a surface look by joining nodes (like Unreal's Material Editor), save it as a file, and have every renderer draw it the same way. This page is the plan and the contract. The first code is the data, the checker, the file format, a CPU evaluator and the shader text generator. There is no editor window and no renderer wiring yet.

## What Unreal has, and what we will and will not copy

| Unreal feature | Us |
|---|---|
| Node graph with typed pins, one result node with fixed inputs (Base Color, Metallic, Roughness, Emissive, Opacity, Normal) | Copy. Same six inputs. |
| Float, float2, float3, float4 and texture pins, with scalar-to-vector widening | Copy. A float may feed a vector pin (it is widened). Nothing narrows (no silent drop of components). |
| Parameters you can change per object without recompiling | Copy the idea. A parameter node has a name and a default. The host sets the value. |
| Material functions, layers, instances | Later. Not in phase 1. |
| Shading models, blend modes, two-sided, tessellation, world position offset, custom HLSL node | Will not copy now. The existing `Material` and `MeshRenderer` fields keep deciding these. A custom code node is an escape hatch we do not want (it cannot run on the CPU evaluator). |
| Compile to HLSL only, with a platform translator | We write both GLSL and HLSL text ourselves from one checked intermediate form (IR). No third-party shader compiler. |
| Live preview in the editor | Later phase (needs the editor window). |

## Data model

Code: `engine/include/okay/Render/MaterialGraph.hpp`, `engine/src/Render/MaterialGraph*.cpp`. Namespace `okay::matgraph`.

- **Pin types:** `Float`, `Vec2`, `Vec3`, `Vec4`, `Texture`, `Sampler`.
- **Node:** an id, a kind, a canvas position (for the later editor), four numbers (`v[4]`, used by constants, parameter defaults and the sampler mode) and a text (the parameter name or the texture path).
- **Link:** from (node, output pin number) to (node, input pin number). An input takes at most one link. An output may feed many.
- **Pins and defaults** come from one table (`GetNodeDef`). An input with no link either uses a default (a constant, the mesh UV, the world normal, the view vector, or the time) or is "required" and is an error if left open.
- **Connection rule:** same type, or `Float` into `Vec2/3/4` (widened). Texture and Sampler only join their own type.
- **Maths nodes are generic.** Add, Multiply, Lerp, Clamp, Saturate, Power, OneMinus take any of Float..Vec4. The result type is the widest connected input. Every connected input must be that type or Float.

## Phase 1 node set

| Group | Nodes |
|---|---|
| Constants | ConstFloat, ConstVec2, ConstVec3, ConstVec4 |
| Parameters | ParamFloat, ParamVec2, ParamVec3, ParamVec4 |
| Textures | TextureObject (a path), SamplerState (wrap or clamp, linear or point), TextureSample (RGB, A, RGBA outputs) |
| Surface inputs | TexCoord (UV), WorldPosition, WorldNormal, ViewVector, Time |
| Maths | Add, Multiply, Lerp, Clamp, Saturate, Power, OneMinus, Dot, Normalize |
| Effects | Fresnel, Panner, Noise (value noise, 0..1) |
| Result | Output (BaseColor, Metallic, Roughness, Emissive, Opacity, Normal). Exactly one per graph. |

Output `Normal` is a tangent-space normal, default `(0, 0, 1)`. Defaults for the others: base colour 0.8 grey, metallic 0, roughness 0.5, emissive black, opacity 1.

## Pipeline

```
MaterialGraph  --Check/Compile-->  checked IR (Program)  -+-> CPU evaluator   (tests, software renderer later)
 (.okaymat)     types, cycles,      straight-line ops     +-> GLSL text  (#version 120 style, like GLRenderer)
                missing inputs,                           +-> HLSL text  (like D3D11Renderer)
                one Output
```

- The IR is a list of operations in order. Each op makes one value. Dead nodes (not reaching the Output) are dropped.
- Both text generators print the same lines. Only the type names and a small block of macros at the top differ (`vec3`/`float3`, `mix`/`lerp`). Every operation is a macro call such as `okm_mul(v3, v4)`, so the body is the same in both languages.
- The result is one function `okm_material(uv, worldPos, normal, view, time)` returning a struct with the six outputs, plus a list of parameters and textures the host must bind. It is a snippet for later insertion into the existing shaders; it is not a complete shader.
- Noise uses a hash with no `sin` (so it does not depend on the GPU's sine precision), but a fract of a large product still differs a little between CPUs and GPUs. UNVERIFIED across GPUs.

## How it plugs into the existing Material

Nothing changes for current materials. `Material` (`.okaymat` version 1, first line `okaymat 1`) and the `MeshRenderer` fields stay as they are and keep being the default.

- A graph file is also a `.okaymat`, but its first line is `okaymatgraph 1`. A loader looks at the first line to decide which kind it has. The old reader never sees a graph file unless somebody opens it by hand.
- Later (phase 3) `Material` gets one optional field, a path to a graph. Empty means today's behaviour, exactly. The renderer picks the graph path only when the field is set.
- A graph material supplies the surface inputs (colour, metallic, roughness, emissive, opacity, normal). Lighting, shadows, fog and post stay in the renderers.

## Phases

| Phase | What | Needs | Effort (honest, for one person) |
|---|---|---|---|
| 1 (done here) | Data model, checker, `.okaymat` graph text, CPU evaluator, GLSL and HLSL text, tests | Linux only | Done |
| 2 | Software renderer calls the evaluator per pixel for graph materials (reference look) | Linux, pixel tests | 3 to 5 days |
| 3 | `Material` gets the optional graph path; scene save and load; asset database knows the file | Linux | 2 to 3 days |
| 4 | Splice the generated function into the GL and D3D11 mesh shaders; bind parameters and textures; keep the old path when no graph | A Windows machine with a GPU, plus a GL machine, to look at frames | 1 to 2 weeks, mostly debugging on real hardware |
| 5 | Editor window: node canvas (reuse the logic-graph canvas), palette, pins, live preview, errors from the checker shown on nodes | Editor build; the preview needs phase 2 or 4 | 2 to 3 weeks |
| 6 | More nodes (mask/append, texture coordinates tools, parallax, world-space UV, custom functions, instances), D3D12 | Phase 4 | Open ended |

## Not done and not verified

- No editor window. No renderer wiring. The software renderer does not call the evaluator yet.
- The GLSL and HLSL text has been checked only by our own small text checker (balanced brackets, declared names, language-correct type and function names) and by a CPU interpreter that runs the generated lines. It has never been given to a real GLSL or HLSL compiler.
- Constant buffer layout, register slots and texture binding numbers in the HLSL are options with guessed defaults. They must be reconciled with `D3D11Renderer.cpp` in phase 4.
