# Math, geometry, noise and RNG

Header-only additions to the core math library. Everything is in `namespace okay`, is pulled in by
`#include <Okay.hpp>` (or `okay/Math/Math.hpp`, plus `okay/Core/Rng.hpp`), uses no OS headers and
no threads, and is covered by `tests/test_math_geometry.cpp` (ctest name `math_geometry`).

| Header | Contents |
|---|---|
| `Math/Mat3.hpp` | `Mat3`: 3x3 matrix and 2D homogeneous transform |
| `Math/Mat4.hpp` | `Mat4::Decompose` (added) |
| `Math/Quat.hpp` | `Quat::ToAngleAxis` (added) |
| `Math/Ray.hpp`, `Math/Bounds.hpp`, `Math/Geometry.hpp` | `Ray`, `Bounds` (= `AABB`), `Plane`, `Sphere`, `Capsule`, `OBB`, `ViewFrustum`, `Geom::*` |
| `Math/VecInt.hpp` | `Vec2i`, `Vec3i` |
| `Math/Noise.hpp` | `NoiseField` |
| `Math/Curves.hpp` | `Curves` |
| `Math/ColorSpace.hpp` | `ColorSpace`, `Hsv`, `Hsl` |
| `Core/Rng.hpp` | `Rng` (PCG32 with streams) |

## Mathf::Infinity

`Mathf::Infinity` is now a real IEEE infinity (it used to be the largest finite float), so
`x == Mathf::Infinity` and `std::isinf` behave as expected. `Mathf::MaxFloat` is the old value.
`SmoothDamp`'s default `maxSpeed` and the console renderer's depth clear still work unchanged.
Script-side `Mathf.Infinity` in OkayScript is a separate constant (`1e30`) and is unchanged.

## Mat3 and decomposition

```cpp
Mat3 r = Mat3::AngleAxis(90, {0,0,1});
Vec3 v = r * Vec3{1,0,0};                       // (0,1,0)
Mat3 n = model3.NormalMatrix();                 // inverse-transpose for normals

Mat3 t2d = Mat3::TRS2D({10,20}, 90.0f, {2,3});  // 2D translate * rotate * scale
Vec2 p = t2d.TransformPoint2({1,0});
t2d.Decompose2D(pos, deg, scale);

Vec3 t, s; Quat q;
bool affine = m.Decompose(t, q, s);             // TRS(t,q,s) reproduces m
float deg; Vec3 axis; q.ToAngleAxis(deg, axis); // inverse of Quat::AngleAxis
```

`Mat4::Decompose` rules:

- **Negative scale / mirror**: the reflection goes on `scale.x` (so `scale.x < 0`, `y` and `z`
  stay positive) and the rotation stays a proper rotation.
- **Shear**: cannot be expressed as TRS. Columns are Gram-Schmidt orthogonalised starting from
  the x axis, giving the nearest TRS; the x axis is reproduced exactly.
- **Zero-length axis** (flattened matrix): scale 0 on that axis and a completed orthonormal basis,
  so the quaternion is always unit length and finite.
- Returns `false` for a projective matrix (bottom row not `0 0 0 1`) or non-finite values; the
  outputs are still filled with safe values (no NaN).

`Quat::ToAngleAxis` returns the shortest representation (angle 0..180 for `w >= 0` input), accepts
non-unit input, and returns angle 0 with axis +X for the identity.

## Bounds, shapes and intersection tests

`Bounds` is the engine AABB (`using AABB = Bounds`). Changes:

- **Empty state**: a default-constructed `Bounds` is *empty* (negative extents). It contains
  nothing, intersects nothing, and the first `Encapsulate` makes it exactly that point or box
  instead of growing from the origin. `IsEmpty()`, `Bounds::Empty()`, `Clear()`.
  Constructing with `(center, size)`, `SetMinMax`, `FromMinMax` or assigning non-negative
  extents makes a real box. **Behaviour change:** a default `Bounds` used to be a point at the
  origin; code that wants that must now write `Bounds(Vec3{}, Vec3{})`.
- New: `Encapsulate(Bounds)`, `Expand(amount)`, `ClosestPoint`, `SqrDistance`,
  `Raycast(ray, distance, maxDistance)`, `Transform(Mat4)`.

```cpp
Bounds b;                                  // empty
for (auto& p : points) b.Encapsulate(p);   // exactly the box of the points

float t;
if (b.Raycast(Ray(origin, dir), t)) hit = origin + dir * t;

Plane ground = Plane::FromPointNormal({0,0,0}, {0,1,0});
Geom::Raycast(ray, Sphere({0,1,0}, 2), t);
Geom::Raycast(ray, OBB(center, halfExtents, rotation), t);
Geom::Raycast(ray, Capsule(a, b, radius), t);

ViewFrustum f = ViewFrustum::FromViewProjection(proj * view);
if (f.Intersects(bounds)) draw();
```

Conventions: `Ray` normalises its direction on construction and every routine normalises again,
so `t` is a true distance. A ray starting inside a solid shape hits at `t = 0`. `maxDistance`
limits the search. Overlap tests are inclusive (touching counts) and an empty `Bounds` never
overlaps anything.

| Overlap (`Geom::Overlap`) | Sphere | AABB | OBB | Capsule |
|---|---|---|---|---|
| **Sphere** | yes | yes | yes | yes |
| **AABB** | yes | yes (`Intersects`) | yes | - |
| **OBB** | yes | yes | yes (15-axis SAT) | - |
| **Capsule** | yes | - | - | yes |

Also: `Geom::ClosestPoint` (sphere, capsule, OBB, AABB, plane), `Geom::ClosestPointOnSegment`,
`Geom::SegmentSegment` (squared distance plus closest points), `Geom::Classify(plane, sphere)`.
`ViewFrustum` tests points, spheres, AABBs, OBBs and capsules; its box tests are conservative (they
never reject something visible, but may keep a box grazing a frustum corner). It is a separate type
from the renderer's `Frustum` in `Render/Culling.hpp`, which stays as it is.

Not included: capsule vs box, ray vs triangle, swept tests (those live in Physics).

## Vec2i / Vec3i

Exact `==`, lexicographic `<` (works in `std::map`, `std::set`, `std::sort`), `std::hash`
specialisations (work in `std::unordered_map`/`set`), arithmetic, `Manhattan`/`Chebyshev`
distances, `FloorDiv`/`FloorMod` (round toward negative infinity: negative tile coordinates map to
the right chunk), `FloorToInt`/`CeilToInt`/`RoundToInt` from `Vec2`/`Vec3`, `SqrMagnitude` in 64 bits.

```cpp
std::unordered_map<Vec2i, Chunk> chunks;
Vec2i tile = Vec2i::FloorToInt(worldPos.xy());
Vec2i chunk = tile.FloorDiv({16,16});   // (-1,-16) -> (-1,-1), not (0,-1)
Vec2i local = tile.FloorMod({16,16});
```

## Noise

One seedable library. `NoiseField(seed)` is a pure function of `(seed, coordinates)`, with no tables
or global state, so results are identical across platforms and safe to call from any thread.

```cpp
NoiseField n(1234);
float h  = n.Perlin2(x, z);                                   // [-1, 1]
float s  = n.Simplex3(x, y, z);                               // [-1, 1]
float v  = n.Value2(x, z);                                    // [-1, 1]
WorleyResult w = n.Worley2(x, z);                             // w.f1, w.f2 (cell units), w.id in [0,1)
FractalParams p; p.octaves = 5;
float f  = n.Fbm2(NoiseType::Simplex, x, z, p);               // [-1, 1]
float r  = n.Ridged2(NoiseType::Perlin, x, z, p);             // [0, 1]
float t  = n.Turbulence3(NoiseType::Value, x, y, z, p);       // [0, 1]
```

`Worley*` take a `jitter` (0 = regular lattice, 1 = random points). The engine contains several
older private noise implementations (`Render/ProcTexture.hpp`, `Render/Mesh.hpp` `ValueNoise`,
`Components/Terrain.cpp`, `Components/Light.hpp`, `Components/VirtualCamera.hpp`,
`VisualScript/NodesWave4.hpp` `VsPerlin2`). They were left as they are: each uses its own hash and
interpolation, so swapping them would change generated textures, terrain and shake patterns and
would invalidate saved seeds. New code should use `NoiseField`.

## Curves

Template helpers over `float`, `Vec2` or `Vec3`:

```cpp
Curves::QuadBezier(p0, p1, p2, t);            Curves::QuadBezierTangent(p0, p1, p2, t);
Curves::CubicBezier(p0, p1, p2, p3, t);       Curves::CubicBezierTangent(...);
Curves::Hermite(p0, m0, p1, m1, t);           Curves::HermiteTangent(...);
Curves::CatmullRom(p0, p1, p2, p3, t, 0.5f);  Curves::CatmullRomTangent(...);   // passes through p1..p2

float len = Curves::CubicBezierLength(a, b, c, d);
Curves::ArcLengthTable tab; tab.Build([&](float t){ return Curves::CubicBezier(a,b,c,d,t); });
float t = tab.ParamAtLength(distance);        // constant-speed travel
```

Tangents are the exact first derivative with respect to `t` (not normalised). `t` is not clamped
(the curve extrapolates). Arc length is a polyline approximation that converges from below.

## Colour spaces

`ColorSpace` works on `Color` (components 0..1, treated as sRGB-encoded unless a name says linear;
alpha is never gamma-converted):

```cpp
Color lin = ColorSpace::ToLinear(c);     Color srgb = ColorSpace::ToSrgb(lin);
Color mid = ColorSpace::LerpLinear(a, b, 0.5f);          // blend in linear light
Hsv hsv = ColorSpace::ToHsv(c);          Color c2 = ColorSpace::FromHsv(210, 0.5f, 0.8f);
Hsl hsl = ColorSpace::ToHsl(c);          Color c3 = ColorSpace::FromHsl(hsl);
Color parsed; bool ok = ColorSpace::ParseHex("#FF8000CC", parsed);
std::string hex = ColorSpace::ToHex(c, /*withAlpha*/ true);
```

Hue is in degrees and wraps (negative and >360 are fine). `ParseHex` accepts `#RGB`, `#RGBA`,
`#RRGGBB`, `#RRGGBBAA` (the `#` or a `0x` prefix is optional, case-insensitive, surrounding
whitespace ignored) and leaves the output untouched on failure. There was no existing
`Color::Parse` in the engine.

## Rng: deterministic streams

`Rng` is PCG32 (identical output on every platform and compiler). It is separate from
`okay::Random` (xorshift128+), which is unchanged so existing seeds keep their sequences.

```cpp
Rng world(seed);
Rng loot  = world.Fork("loot");          // independent stream, derived from seed+stream id,
Rng spawn = world.Fork("spawn");         // NOT from the current position: forking never consumes
                                         // `world`, and adding draws to one system never shifts another
loot.Range(0, 10);                       // [0, 10): max exclusive, unbiased (rejection sampling)
loot.Value(); loot.Chance(0.25f); loot.Gaussian(0, 1);
loot.Shuffle(items);                     // Fisher-Yates
auto* pick = loot.Choose(items);         // nullptr when empty
int i = loot.WeightedIndex({1, 0, 3});   // -1 when no positive weight

std::string saved = loot.GetState();     // "pcg32:<state>:<inc>:<seed>:<stream>" (hex)
bool ok = loot.SetState(saved);          // false and unchanged on malformed text
```

`Rng` does no locking; give each thread its own (`Fork` makes that cheap).

## Platform notes

The new headers avoid `<mutex>`, `<thread>`, `<condition_variable>` and Windows macro names
(`near`, `far`, `min`, `max`, ...), and were syntax-checked with the MinGW cross compiler.
