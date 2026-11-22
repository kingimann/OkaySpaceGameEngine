# OkayShader: custom surface shaders

> **What this is:** The record of our own shader language (`.okayshader` files: a surface block and
> an optional vertex block) and the Custom shader mode on the Mesh Renderer: what the language is,
> how each renderer runs it (colour pass and shadow maps), what was tested, and what is not done.
>
> **Status:** Internal record. Newest facts are in the code: `engine/include/okay/Render/OkayShader.hpp`,
> `engine/src/Render/OkayShader.cpp`.
>
> **Last checked:** 2026-11-22.

## What it is

A shader file holds properties, textures, an optional `vertex { ... }` block and a `surface { ... }`
block (at least one of the two). The vertex block moves each vertex (world position and normal)
before the triangle is drawn: a waving flag, a wobble. The surface block reads the surface inputs
and writes the surface outputs. Neither does lighting: every renderer lights a custom surface with
its own Standard lighting (lights, shadows, ambient, fog, reflections, filmic tonemap), so a custom
shader looks the same everywhere and keeps working when the lighting changes. Both blocks also run
in the shadow-map passes: a moved vertex moves its shadow, and a texel the surface discards (or
whose alpha is below one half) casts no shadow, so a dissolving object casts a dissolving shadow.

```
shader "Lava";                                  // optional display name
mode unlit;                                     // optional: lit (default) or unlit
property float speed = 0.25 range(0, 2);        // a slider in the Inspector
property color hot = (1.0, 0.45, 0.05);         // a colour picker
property vec2 scale = vec2(3.0, 3.0);           // float, vec2, vec3, vec4 or color
texture detail = "proc:noise";                  // a picture, with an optional default path
vertex {                                        // optional: move the surface
    position += normal * (0.03 * sin(time * 2.0 + local.y * 8.0));
}
surface {
    float n = noise(uv * scale + vec2(0.0, time * speed));
    albedo = mix(vec3(0.12, 0.03, 0.02), hot, n) * sample(detail, uv).rgb;
    emission = hot * smoothstep(0.6, 1.0, n);
    if (n < 0.05) { discard; }
}
```

A person reaches it in the editor: Mesh Renderer, Shader = **Custom**, then Shader File. The
Inspector then shows the shader's name (or its first error with line and column; "moves vertices"
when it has a vertex block), one control per property (a slider for a ranged float, a colour
picker for `color`, drag fields otherwise), a path field per texture, and Reset Properties. The
**Material window** (an `.okaymat` opened from the Project panel) has the same Shader selector and,
for Custom, the same fields; Save writes them into the `.okaymat` and Apply to Selection copies
them onto the selected Mesh Renderer. The example scene is `examples/shaders/shader_demo.okayscene`
(shipped in the Windows zip by `scripts/build-windows-dist.sh`), with `lava.okayshader`,
`dissolve.okayshader`, `rim_glow.okayshader`, `flag_wave.okayshader` (a Grid flag on a pole) and
`wobble.okayshader` (a jelly ball) next to it; the dissolve ball casts a dissolving shadow.

## The language

**Types:** `float`, `vec2`, `vec3`, `vec4`, `bool` (locals only). Every number is a float. `color`
is a property type that is a `vec3` shown with a colour picker.

**Inputs** (read-only): `uv` (vec2, the mesh UV times the material tiling), `position` (vec3, world),
`view` (vec3, unit vector from the surface to the eye), `vcolor` (vec3, vertex colour, white without
one), `time` (float, seconds, `Time::ElapsedTime`).

**Outputs** (read and write; they start at the material's own values): `albedo` (vec3: material
colour times texture times vertex colour), `emission` (vec3), `alpha` (float), `normal` (vec3,
world, after the normal map; renormalised after the block), `metallic`, `specular`, `shininess`
(floats, the existing specular model).

**The vertex block** (optional, at most one): inputs (read-only) `uv` (vec2, the mesh UV times the
tiling), `local` (vec3, the mesh's own vertex position), `vcolor` (vec3), `time` (float); outputs
(read and write) `position` (vec3, world, starts at the vertex's world position) and `normal` (vec3,
world, unit length at the start; renormalised after the block). Properties and every function except
`sample` can be used; `discard` and `sample` are errors there ("only allowed in the surface block"),
and the surface block's names (`albedo`, `view`, ...) are unknown inside it, as `local` is in the
surface block. A file with only a vertex block draws the material as it is (a pass-through surface).
The moved normal is the one the surface block and the lighting see. The renderers hand the block the
same inputs; a mesh without UVs gets the box-projected UVs every renderer uses, and a mesh without
normals gets its face normals (OpenGL / Direct3D, one per triangle) or their average per vertex
(software renderer).

**Statements:** `float x = e;` (declaration, zero when there is no `= e`), `x = e;`, `x += e;`
`-= *= /=`, swizzle writes (`albedo.r = 1.0;`, no component twice), `if (bool) ... else ...`,
`for i in 0..8 { }` (whole-number bounds, at most 64 iterations, nested loops at most 4096 together;
`i` is a read-only float), `discard;`, blocks. A name cannot be declared twice while it is visible
(no shadowing).

**Expressions:** `?:`, `||`, `&&`, `== !=` (two floats or two bools), `< > <= >=` (floats),
`+ - * /` (same type, or a float with a vector), unary `-` and `!`, calls, swizzles (`xyzw` or
`rgba`, not mixed, 1 to 4 letters), constructors `vec2(...)` `vec3(...)` `vec4(...)` (exactly the
right number of values, or one float for all).

**Functions:** `sin cos tan asin acos atan exp log exp2 log2 sqrt abs sign floor ceil fract saturate
radians degrees normalize` (one value, any float type), `atan2 pow reflect` (two of the same type),
`min max mod` (second may be a float), `step` (first may be a float), `clamp` (limits both float or
both the same type), `mix` (amount may be a float), `smoothstep` (edges both float or both the same
type), `length distance dot` (to float), `cross` (vec3), `luminance` (vec3 to float),
`noise(vec2|vec3)` (smooth value noise, 0 to 1), `hash(vec2|vec3)` (one random value per whole cell,
0 to 1), `sample(texture, vec2)` (vec4).

Fixed meanings so the renderers agree: `mod(a, b) = a - b * floor(a / b)` (also in HLSL, whose own
`fmod` differs), `smoothstep` is our own `t*t*(3-2t)` on the clamped ratio, `normalize` of a zero
vector is zero (not NaN). `noise` and `hash` use only whole-number arithmetic below 2^24 inside the
lattice hash (a polynomial `(29x+7)x mod 251` applied per axis), which a float computes exactly, so
the CPU and the GPU pick the same lattice values.

**Material values** are stored on the Mesh Renderer (`MeshRenderer::shaderParams`, also in
`.okaymat` and scene files) as text: `speed=0.5;hot=1,0.5,0.1;tex:detail=rock.png`. A name the shader
does not declare is kept and ignored, so editing a shader does not lose values. Garbage keeps the
default.

**Errors** read `file:line:col: error: message`, for example
`lava.okayshader:2:12: error: unknown name 'foo'`. Syntax errors stop at the first; type errors are
collected (up to 16). A shader that does not compile draws the magenta error material and logs the
first error once per change of the file (`[shader] ... (drawn in magenta until fixed)`).

**Limits** (hostile files give errors, never crashes; they apply to each block): 1 MB of source, 64-character names, 96 levels
of nesting, 32 properties, 4 textures, 1024 CPU registers (floats) and 1024 constants per shader,
200000 tokens.

## How each renderer runs it

| Renderer | How | State |
|---|---|---|
| Software | `oshader::RunCpu`: the checked tree compiled to a linear register program (forward jumps, counted loops); run per pixel in `Raster::TrianglePhong` before lighting. Shaders and pictures are resolved before the band threads start. Vertex block: `oshader::RunCpuVertex` per vertex on the main thread (`customsw::DisplacedMesh`, cached per object, source mesh, time, transform and values); the colour pass and both shadow-map passes draw the moved copy. Shadow cut-outs: `customsw::ShadowCut` runs the surface per shadow-map texel (sun cascades and spot / point / area tiles). | Works; tested |
| OpenGL | The main mesh fragment shader compiled again with `#define OKAY_SURFACE` and the generated GLSL spliced in before `main()`: one program per shader file. Per-object uniform locations are swapped in for the draw; the frame's uniforms (lights, shadows, fog, sky, samplers) are copied from the main program once per frame. Pictures on units 9-12. Vertex block: the main vertex shader with `#define OKAY_VERTEX` and `okayVertex()`, projected with `uOkVP` (the camera's view-projection). Shadow maps: a second program per shader file (that vertex shader, and a fragment shader that only runs the surface and discards cut-away texels) draws the caster when the shader moves vertices or cuts. | Works on Mesa llvmpipe; tested against the software renderer |
| Direct3D 11 | `PSMain` compiled again with `#define OKAY_SURFACE` and the generated HLSL; properties and time in `cbuffer OkayCustom : register(b4)`, pictures on t9-t12. Vertex block: `VSMain` compiled again with `OKAY_VERTEX`, the view-projection in `cbuffer OkayVertex : register(b5)`. Shadow maps: that vertex shader (or the plain `VSMain`) with `PSCustomDepth`. The variant texts are built by `D3D11CustomPixelSource` / `D3D11CustomVertexSource` / `D3D11CustomDepthSource` (`D3D11Hlsl.hpp`), shared with the tests. A mesh skinned on the GPU keeps its skinning shader (no vertex block). | **Unverified**: the C++ is syntax-checked with the MinGW Direct3D headers; the HLSL variants of the example shaders were compiled by glslang's HLSL front end on Linux (see Tests), never by Microsoft's `D3DCompile`, and nothing ran on Windows |
| Direct3D 12 | `kHLSL12` (its own copy of the mesh shader) has the same `OKAY_SURFACE` / `OKAY_VERTEX` splice points; per shader file `PSMain` (and with a vertex block `VSMain`) is compiled again (SM 5.1) into an opaque and a blended pipeline state, rebuilt on hot reload. Root parameters 2 / 3 / 4 are b4 (time, properties), b5 (the vertex block's view-projection) and a t9-t12 table, written per draw into the upload ring and the shader-visible heap. Not in the shadow map (Direct3D 12 casts the plain, unmoved shadow). | **Unverified**: C++ syntax-checked with the MinGW headers; the spliced HLSL of the example shaders passes glslang's HLSL front end; never compiled by `D3DCompile` or run |
| Xbox (UWP) | No run-time HLSL compiler there: a Custom object draws magenta and logs why. | Not done |

The generated code is GLSL 1.20 (the version the OpenGL renderer's shaders use; it is also valid GLSL
ES 3.00 / 3.30 with `texture2D` defined to `texture`) and HLSL Shader Model 4. Every user name is
renamed (`l_name`, `okp_name`, `okt_name`), so a GLSL or HLSL keyword used as a name is harmless.

**Hot reload:** `oshader::LoadShaderFile` checks a file's time and size at most every 250 ms; a change
compiles again and gets a new generation, and each renderer rebuilds its program for that file. This
works in the editor and in a running game alike. A broken edit draws magenta until the next good save.

**Transparency:** a shader that assigns `alpha`, or a material whose colour alpha is below 1, is drawn
blended like other see-through materials.

**Shadows:** in the shadow-map passes the vertex block runs as in the colour pass, and a surface that
uses `discard` or assigns `alpha` runs per texel: a texel it discards, or whose `alpha` is below
`oshader::kShadowAlphaCut` (one half), casts no shadow. There is no eye there, so `view` is the
surface normal and `albedo` starts at the material colour (times the vertex colour) without the
material picture. A material whose colour alpha is below 1 still casts no shadow on the GPU
renderers and a whole one on the software renderer (as before; not changed here).

**Not covered by a custom surface:** the outline shell, the screen-space ambient occlusion depth pass
(it draws the unmoved mesh), visibility culling (it uses the unmoved mesh's bounds, so a vertex block
that moves the mesh far outside them can be culled at the screen edge), and the built-in shader
styles (a Custom object never also gets Toon bands, Gradient and so on).

**Direct3D 12, what is still missing:** the shadow-map pass (it streams positions only, with one
depth pipeline; a custom caster would need its own depth pipeline from the vertex variant and a
`PSCustomDepth`), and any run on Windows. Round 1 also left the Toon band count and rim strength of
every Direct3D 12 draw inside a comment (never set); fixed in round 2.

## What it was written from

All code is ours. The lexer, the recursive-descent parser (one function per precedence level), the
checking pass over a node pool, the tree-walking emitters and the register program were written from
general compiler techniques; the generated text follows the GLSL 1.20 and GLSL ES 3.00 specifications
and the HLSL Shader Model 4 reference. No code, grammar or tests were taken from another shading
language or compiler (Unity ShaderLab, Godot's shading language, glslang, SPIRV-Cross, Slang and so on).
The noise is ordinary lattice value noise with our own polynomial hash, written and checked by tests.

## Designed for a shader graph later (26.1)

The visual shader graph is not built. The language is shaped so a graph can compile to it: a graph is
a list of local definitions (one per node, using the same built-in functions) followed by assignments
to the outputs, which is exactly a surface block without control flow; properties and textures map to
graph inputs. The graph editor would then reuse every backend and the CPU program unchanged.

## Tests

Round 2 (2026-11-22, vertex block, shadows, Material window):

- `okayshader`: the vertex block (inputs, outputs, read-only inputs, `discard` / `sample` / surface names
  refused there with line:col, one vertex block, a vertex-only file, `RunCpuVertex` maths), the Direct3D 11
  variant texts of every example shader; `OKAY_HLSL_DUMP=<folder>` writes them out.
- `okayshader_render` (software): a half-discarded cube and a half alpha-cut cube cast half a shadow (the
  cut half's ground is as bright as open ground), a vertex block moves the ball up, grows it and sways it
  with time, and moves its shadow.
- `okayshader_gl` (software vs OpenGL, Mesa llvmpipe under Xvfb): two vertex-block cases, the cut shadow on
  both renderers, the waving flag (same moved silhouette), plus the earlier cases.
- Offline HLSL: the `.ps.hlsl`, `.vs.hlsl` and `.depth.hlsl` variants of the example shaders compiled with
  `glslangValidator -D` (glslang's HLSL front end, to SPIR-V) on Linux. That is a real HLSL parser and type
  checker, but not Microsoft's compiler, so a `D3DCompile` difference is still possible. Wine is installed
  here but aborts at start-up in this sandbox (`free(): invalid pointer` before any DLL loads), so Wine's
  `d3dcompiler_47.dll` could not be used.

Round 1:

- `okayshader` (`tests/test_okayshader.cpp`, 8029 checks): line:col errors for two dozen mistakes, hostile inputs
  (5000 nested brackets, 3000 nested ifs, a 100000-character name, NUL bytes, too many locals or
  properties), a 6000-mutant fuzz of valid shaders (about 0.1 s), the GLSL and HLSL text, the CPU
  program's maths (mod, smoothstep, loops, if/else, ?:, swizzle writes, discard, zero normalize, noise
  and hash range and continuity, textures through the sampler), material property text, file hot
  reload, the three example shaders. Also run clean under AddressSanitizer and UndefinedBehaviorSanitizer.
- `okayshader_render` (`tests/test_okayshader_render.cpp`): fields through scene files and `.okaymat`,
  a pass-through custom surface draws the same pixels as Standard (max difference 0), unlit output,
  inputs, the magenta error material (broken, missing and empty file), discard, hot reload, the example
  scene's shaders.
- `okayshader_gl` (`tests/test_okayshader_gl.cpp`, under Xvfb with Mesa llvmpipe): the software and
  OpenGL renderers draw the same surfaces; on 2026-11-22 the share of pixels within 12/255 and the mean
  difference were: unlit uv 96% / 0.8, unlit noise 83% / 2.6, unlit maths 96% / 0.7, unlit properties
  96% / 1.1, unlit discard 91% / 2.3, lava 96% / 2.0, rim glow 93% / 6.9, lit pass-through 63% / 10.5,
  dissolve 38% / 19.1. The lit cases differ because the two renderers' own Standard lighting differs
  (OpenGL lights a little brighter); a pass-through custom surface gives exactly Standard's pixels on
  each renderer. Dissolve differs most because the far inside of the ball, seen through the holes, is
  lit very differently by the two. A GLSL compile failure would show as magenta and fail the test; the
  example scene draws with no magenta pixel on OpenGL.

## Differences between renderers to know

- The GPU renderers never cull back faces; the software renderer does. For a Custom shader that uses
  `discard`, the software renderer now also draws the back faces (with their outward normal), so the
  holes show the inside as on OpenGL.
- The software renderer shades a Custom object per pixel even when per-pixel lighting is off.
- A Double-sided mesh seen from its back: the software renderer turns the normal towards the eye, the GPU
  renderers keep the mesh's own normal, so the two light it differently (found with the flag; not changed).
  Software shadows are softer (its own filter), so a dissolve shadow's holes show sharper on OpenGL.
