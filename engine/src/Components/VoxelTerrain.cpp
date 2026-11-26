#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Core/Base64.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Occluder.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>

namespace okay {

// ===================== Noise (matches Terrain's value-noise feel) ===========
static float VN(int a, int b, int c, unsigned seed) {
    unsigned h = seed + 0x9E3779B9u;
    h ^= (unsigned)a * 0x85EBCA6Bu; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
    h ^= (unsigned)b * 0x27D4EB2Fu; h = (h ^ (h >> 16)) * 0x165667B1u;
    h ^= (unsigned)c * 0x9E3779B1u; h = (h ^ (h >> 15));
    return (h & 0xFFFFu) / 65535.0f;
}
static float Smooth3(float x, float y, float z, unsigned seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float xf = x - xi, yf = y - yi, zf = z - zi;
    auto f = [](float t) { return t * t * (3.0f - 2.0f * t); };
    float u = f(xf), v = f(yf), w = f(zf);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    float c000 = VN(xi, yi, zi, seed),       c100 = VN(xi + 1, yi, zi, seed);
    float c010 = VN(xi, yi + 1, zi, seed),   c110 = VN(xi + 1, yi + 1, zi, seed);
    float c001 = VN(xi, yi, zi + 1, seed),   c101 = VN(xi + 1, yi, zi + 1, seed);
    float c011 = VN(xi, yi + 1, zi + 1, seed), c111 = VN(xi + 1, yi + 1, zi + 1, seed);
    return L(L(L(c000, c100, u), L(c010, c110, u), v),
             L(L(c001, c101, u), L(c011, c111, u), v), w);
}
static float Fractal3(float x, float y, float z, int oct, unsigned seed) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int o = 0; o < oct; ++o) {
        sum += Smooth3(x * freq, y * freq, z * freq, seed + (unsigned)o * 1013u) * amp;
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return norm > 0 ? sum / norm : 0;
}
static float Fractal2(float x, float z, int oct, unsigned seed) {
    return Fractal3(x, 0.0f, z, oct, seed);   // y=0 slice reads as 2D noise
}

// ===================== Marching-cubes cases, worked out by our own code =======
// Written from the idea of the method only (Lorensen and Cline, SIGGRAPH 1987): the surface crosses every cube
// edge whose two corners lie on opposite sides of iso, and each cube's crossings are joined into polygons. No
// case table is typed in: CubeCases() works out all 256 corner patterns once, by walking the cube's six faces.
//
// Corner c of a cube sits at (c & 1, (c >> 1) & 1, (c >> 2) & 1). The 12 edges are the corner pairs that differ in
// one bit, lower corner first, so neighbouring cubes interpolate a shared edge the same way round.
//
// A pattern has bit c set when corner c is below iso (air). On each face, walking its corners counter-clockwise
// as seen from outside, every entry into an air run is joined to the exit from that run. A face with two air
// corners on a diagonal therefore cuts each air corner off on its own (the solid stays joined across the face).
// The rule reads only the face's four corners, so the two cubes sharing a face draw the same segments and the
// surface has no cracks. Each crossing edge is an entry on one of its faces and an exit on the other, so the
// segments close into loops; each loop is split into triangles (SplitLoop), wound so they face the air.
namespace {
struct CubeCase {
    std::uint16_t edges = 0;           // bit e: the surface crosses edge e
    std::uint8_t  triCount = 0;        // at most 10: 12 crossings, at least one loop
    std::uint8_t  tri[30] = {};        // three edge numbers per triangle
};
struct CubeCaseTable {
    std::uint8_t edgeA[12] = {}, edgeB[12] = {};
    CubeCase cases[256];
};

// Triangulates the loop positions first..last (in loop order) into `tris`, using as inner edges only pairs of points
// that share no cube face (faceOf: one bit per face an edge lies on). Tries each apex for the side (first, last).
bool SplitLoop(const int* loop, int first, int last, const unsigned* faceOf, int (*tris)[3], int& count) {
    if (last - first < 2) return true;
    const int mark = count;
    for (int apex = first + 1; apex < last; ++apex) {
        const bool leftOk = apex == first + 1 || !(faceOf[loop[first]] & faceOf[loop[apex]]);
        const bool rightOk = apex == last - 1 || !(faceOf[loop[apex]] & faceOf[loop[last]]);
        if (!leftOk || !rightOk) continue;
        count = mark;
        tris[count][0] = first; tris[count][1] = apex; tris[count][2] = last; ++count;
        if (SplitLoop(loop, first, apex, faceOf, tris, count) && SplitLoop(loop, apex, last, faceOf, tris, count)) return true;
    }
    count = mark;
    return false;
}

CubeCaseTable BuildCubeCases() {
    CubeCaseTable t;
    int edgeOf[8][8];
    unsigned faceOf[12] = {};
    int ne = 0;
    for (int c = 0; c < 8; ++c)
        for (int bit = 1; bit < 8; bit <<= 1)
            if (!(c & bit)) {
                t.edgeA[ne] = (std::uint8_t)c; t.edgeB[ne] = (std::uint8_t)(c | bit); edgeOf[c][c | bit] = edgeOf[c | bit][c] = ne;
                for (int axis = 0; axis < 3; ++axis)                     // the two faces the edge lies on
                    if ((1 << axis) != bit) faceOf[ne] |= 1u << (axis * 2 + ((c >> axis) & 1));
                ++ne;
            }
    // The six faces as rings of four corners, counter-clockwise seen from outside the cube.
    int ring[6][4];
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            const int du[4] = {0, 1, 1, 0}, dv[4] = {0, 0, 1, 1};
            for (int k = 0; k < 4; ++k) {
                const int corner = (side << axis) | (du[k] << u) | (dv[k] << v);
                ring[axis * 2 + side][side ? k : 3 - k] = corner;
            }
        }
    for (int pattern = 0; pattern < 256; ++pattern) {
        CubeCase& cc = t.cases[pattern];
        auto air = [&](int corner) { return ((pattern >> corner) & 1) != 0; };
        for (int e = 0; e < 12; ++e)
            if (air(t.edgeA[e]) != air(t.edgeB[e])) cc.edges |= (std::uint16_t)(1u << e);
        int next[12];
        for (int e = 0; e < 12; ++e) next[e] = -1;
        for (const auto& q : ring)
            for (int k = 0; k < 4; ++k) {
                if (air(q[k]) || !air(q[(k + 1) & 3])) continue;          // not an entry into an air run
                int m = (k + 1) & 3;
                while (air(q[(m + 1) & 3])) m = (m + 1) & 3;             // last air corner of the run
                next[edgeOf[q[k]][q[(k + 1) & 3]]] = edgeOf[q[m]][q[(m + 1) & 3]];
            }
        bool used[12] = {};
        for (int start = 0; start < 12; ++start) {
            if (next[start] < 0 || used[start]) continue;
            int loop[12], len = 0;
            for (int e = start; !used[e]; e = next[e]) { used[e] = true; loop[len++] = e; }
            // Split the loop into triangles whose inner edges never join two points of the same cube face: such an
            // edge would lie in the face, where the neighbouring cube draws only the face's own segments, and the two
            // cubes would put triangles on top of each other there. The walk goes round the air clockwise seen
            // from the air side, so each triangle is written the other way round (facing the air).
            int tris[10][3], count = 0;
            if (!SplitLoop(loop, 0, len - 1, faceOf, tris, count)) {
                count = 0;                                            // no such split: a plain fan (not seen in the 256)
                for (int i = 1; i + 1 < len; ++i) { tris[count][0] = 0; tris[count][1] = i; tris[count][2] = i + 1; ++count; }
            }
            for (int k = 0; k < count; ++k) {
                std::uint8_t* out = cc.tri + 3 * cc.triCount++;
                out[0] = (std::uint8_t)loop[tris[k][0]]; out[1] = (std::uint8_t)loop[tris[k][2]]; out[2] = (std::uint8_t)loop[tris[k][1]];
            }
        }
    }
    return t;
}

const CubeCaseTable& CubeCases() {
    static const CubeCaseTable table = BuildCubeCases();
    return table;
}
} // namespace


// ===================== VoxelTerrain =========================================
namespace {

using Clock = std::chrono::steady_clock;
inline double MsSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// Grid coordinates from a float that may be far outside the grid (or not a number): clamped so the
// cast to int can never overflow. The same answer as (int)std::floor(v) for every ordinary value.
inline int FloorI(float v) {
    if (!(v > -1.0e9f)) return -1000000000;      // also catches NaN
    if (v > 1.0e9f) return 1000000000;
    const int i = (int)v;
    return v < (float)i ? i - 1 : i;
}
inline int CeilI(float v) {
    if (!(v > -1.0e9f)) return -1000000000;
    if (v > 1.0e9f) return 1000000000;
    const int i = (int)v;
    return v > (float)i ? i + 1 : i;
}
inline bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::size_t MeshBytes(const Mesh& m) {
    return m.vertices.size() * sizeof(Vec3) + m.normals.size() * sizeof(Vec3) + m.uvs.size() * sizeof(Vec2) +
           m.triangles.size() * sizeof(int) + m.triColors.size() * sizeof(Color);
}

// ---- the MeshRenderer settings a chunk object copies from the terrain's own renderer ------------------
// One list drives both the copy and the change check, so a field cannot be in one and forgotten in the other.
// (When MeshRenderer gets a new look-changing field, add it here: tests/test_voxel_chunks.cpp checks the list.)
#define OKAY_VOXEL_MATERIAL_FIELDS(X)                                                                         \
    X(enabled) X(color) X(shader) X(toonBands) X(gradientTop) X(gradientBottom)                              \
    X(rimStrength) X(rimPower) X(rimColor) X(castShadows) X(receiveShadows)                                  \
    X(groundShadow) X(groundShadowY) X(groundShadowStrength)                                                 \
    X(outline) X(outlineColor) X(outlineWidth) X(uvScroll) X(triplanar) X(wireframe) X(doubleSided)          \
    X(emissive) X(specular) X(shininess) X(unlit) X(texture) X(tiling) X(texOffset) X(texFilter)             \
    X(normalMap) X(normalStrength) X(reflectivity) X(specularMap) X(aoMap) X(aoStrength)                     \
    X(metallicRoughnessMap) X(emissiveMap) X(metallic) X(matcap)

inline std::uint64_t MixU(std::uint64_t h, std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); return h; }
inline std::uint64_t MixField(std::uint64_t h, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return MixU(h, u); }
inline std::uint64_t MixField(std::uint64_t h, bool b) { return MixU(h, b ? 1u : 0u); }
inline std::uint64_t MixField(std::uint64_t h, int i) { return MixU(h, (std::uint32_t)i); }
inline std::uint64_t MixField(std::uint64_t h, const Color& c) { return MixField(MixField(MixField(MixField(h, c.r), c.g), c.b), c.a); }
inline std::uint64_t MixField(std::uint64_t h, const Vec2& v) { return MixField(MixField(h, v.x), v.y); }
inline std::uint64_t MixField(std::uint64_t h, const std::string& s) { return MixU(h, std::hash<std::string>{}(s)); }
template <class E, class = typename std::enable_if<std::is_enum<E>::value>::type>
inline std::uint64_t MixField(std::uint64_t h, E e) { return MixU(h, (std::uint64_t)(int)e); }

std::uint64_t MaterialSig(const MeshRenderer& r) {
    std::uint64_t h = 1469598103934665603ull;
#define X(f) h = MixField(h, r.f);
    OKAY_VOXEL_MATERIAL_FIELDS(X)
#undef X
    return h;
}
void MirrorMaterial(const MeshRenderer& src, MeshRenderer& dst) {
#define X(f) dst.f = src.f;
    OKAY_VOXEL_MATERIAL_FIELDS(X)
#undef X
}

int g_forceOldRemesh = -1;

} // namespace

void VoxelTerrain::ForceOldRemesh(int mode) { g_forceOldRemesh = mode; }
bool VoxelTerrain::OldRemeshActive() {
    if (g_forceOldRemesh >= 0) return g_forceOldRemesh != 0;
    static const bool env = [] {
        const char* e = std::getenv("OKAY_VOXEL_OLD_REMESH");
        return e && *e && *e != '0';
    }();
    return env;
}

void VoxelTerrain::Resize(int X, int Y, int Z) {
    nx = X < 2 ? 2 : X; ny = Y < 2 ? 2 : Y; nz = Z < 2 ? 2 : Z;
    density.assign((std::size_t)nx * ny * nz, -1.0f);   // all air
    mcAllDirty_ = true;
    mcNx_ = -1;
    floatersSwept_ = false;
    editAny_ = false;
}

// Material values are kept within a bounded band (±~5 voxels) so the field reads
// as a clamped signed distance: only the sign and the near-surface gradient drive
// the mesh, and digging/adding with a finite strength reliably flips a voxel
// rather than fighting an ever-larger interior value deep in the rock.
float VoxelTerrain::Clamp() const { return voxelSize * 3.0f; }
static float ClampTo(float v, float c) { return v < -c ? -c : (v > c ? c : v); }
float VoxelTerrain::StepDensity(float cell, float amount, float fall, float c) { return ClampTo(cell - amount * fall, c); }

void VoxelTerrain::FillSlab(float frac) {
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    float topY = SizeY() * frac, c = Clamp();
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                density[Index(x, y, z)] = ClampTo(topY - y * voxelSize, c);   // solid below topY
    SealFloor();
    mcAllDirty_ = true;
    floatersSwept_ = false;
    editAny_ = false;
}

void VoxelTerrain::GenerateSlices(int zFrom, int zTo, float surfaceFrac, float amplitude, float caveAmount, unsigned seed) {
    if (caveAmount < 0) caveAmount = 0; if (caveAmount > 1) caveAmount = 1;
    const float baseY = SizeY() * surfaceFrac;
    const float fxz = 2.5f / std::max(1.0f, (float)(nx - 1));   // surface feature scale
    const float fc  = 3.5f / std::max(1.0f, (float)(nx - 1));   // cave feature scale
    for (int z = std::max(0, zFrom); z < std::min(nz, zTo); ++z)
        for (int x = 0; x < nx; ++x) {
            float n = Fractal2((float)x * fxz, (float)z * fxz, 5, seed);   // [0,1]
            float surf = baseY + (n - 0.5f) * 2.0f * amplitude;
            for (int y = 0; y < ny; ++y) {
                float wy = y * voxelSize;
                float d = surf - wy;                  // solid below the surface
                if (caveAmount > 0.0f && wy < surf - voxelSize * 1.5f) {
                    // Winding caverns: carve where a 3D field sits near a midline.
                    float cf = Fractal3((float)x * fc, (float)y * fc, (float)z * fc, 4, seed ^ 0x5AB3u);
                    float worm = std::fabs(cf - 0.5f);
                    if (worm < 0.07f * (0.4f + caveAmount)) d = -voxelSize;   // open air
                }
                density[Index(x, y, z)] = ClampTo(d, Clamp());
            }
        }
}

void VoxelTerrain::Generate(float surfaceFrac, float amplitude, float caveAmount, unsigned seed) {
    GenerateSlices(0, nz, surfaceFrac, amplitude, caveAmount, seed);
    SealFloor();
    mcAllDirty_ = true;
    floatersSwept_ = false;
    editAny_ = false;
}

// ---- dirty tracking ---------------------------------------------------------
// A density write at cell k changes the corners of cubes k-1 and k, and ALSO the normals of cubes k-2 .. k+1
// (a normal reads half a cell either side of its vertex, so a cube's vertices read cells c-1 .. c+2). The old
// code only marked k-1 .. k, so the cubes just outside an edit kept normals from before it.
void VoxelTerrain::TouchBox(int x0, int y0, int z0, int x1, int y1, int z1) {
    if (x0 > x1 || y0 > y1 || z0 > z1) return;
    ++editCount;
    if (!editAny_) {
        editMin_[0] = x0; editMin_[1] = y0; editMin_[2] = z0;
        editMax_[0] = x1; editMax_[1] = y1; editMax_[2] = z1;
        editAny_ = true;
    } else {
        if (x0 < editMin_[0]) editMin_[0] = x0;
        if (y0 < editMin_[1]) editMin_[1] = y0;
        if (z0 < editMin_[2]) editMin_[2] = z0;
        if (x1 > editMax_[0]) editMax_[0] = x1;
        if (y1 > editMax_[1]) editMax_[1] = y1;
        if (z1 > editMax_[2]) editMax_[2] = z1;
    }
    if (y0 < floorLayers) floorTouched_ = true;
    if (fluidOn && !fluid_.empty()) WakeFluidBox(x0, y0, z0, x1, y1, z1);
    MarkCubes(x0 - 2, y0 - 2, z0 - 2, x1 + 1, y1 + 1, z1 + 1);
}

void VoxelTerrain::MarkCubes(int a0, int b0, int c0, int a1, int b1, int c1) {
    if (mcAllDirty_) return;
    const int cubesX = nx - 1, cubesY = ny - 1, cubesZ = nz - 1;
    const int ncx = std::max(1, (cubesX + kChunk - 1) / kChunk);
    const int ncy = std::max(1, (cubesY + kChunk - 1) / kChunk);
    const int ncz = std::max(1, (cubesZ + kChunk - 1) / kChunk);
    if (mcNx_ != nx || mcNy_ != ny || mcNz_ != nz || (int)mcDirty_.size() != ncx * ncy * ncz) {
        mcAllDirty_ = true;      // the chunk table does not describe this grid (yet): everything is meshed
        return;
    }
    if (a0 < 0) a0 = 0; if (b0 < 0) b0 = 0; if (c0 < 0) c0 = 0;
    if (a1 > cubesX - 1) a1 = cubesX - 1;
    if (b1 > cubesY - 1) b1 = cubesY - 1;
    if (c1 > cubesZ - 1) c1 = cubesZ - 1;
    if (a0 > a1 || b0 > b1 || c0 > c1) return;
    for (int cz = c0 / kChunk; cz <= c1 / kChunk; ++cz)
        for (int cy = b0 / kChunk; cy <= b1 / kChunk; ++cy)
            for (int cx = a0 / kChunk; cx <= a1 / kChunk; ++cx) {
                unsigned char& d = mcDirty_[(std::size_t)(cx + ncx * (cy + ncy * cz))];
                if (!d) { d = 1; ++mcDirtyCount_; }
            }
}

bool VoxelTerrain::Dig(const Vec3& local, float radius, float amount) {
    // A brush that cannot make sense must not touch the field. (Radius 0 divided by zero and wrote NaN into
    // the cell the brush sat on; a NaN strength or position poisoned the whole box.)
    if (!(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(amount) || !Finite3(local)) return false;
    float gx = (local.x + HalfX()) / voxelSize;
    float gy = local.y / voxelSize;
    float gz = (local.z + HalfZ()) / voxelSize;
    float gr = radius / voxelSize;
    int x0 = std::max(0, FloorI(gx - gr)), x1 = std::min(nx - 1, CeilI(gx + gr));
    int y0 = std::max(0, FloorI(gy - gr)), y1 = std::min(ny - 1, CeilI(gy + gr));
    int z0 = std::max(0, FloorI(gz - gr)), z1 = std::min(nz - 1, CeilI(gz + gr));
    int cx0 = nx, cy0 = ny, cz0 = nz, cx1 = -1, cy1 = -1, cz1 = -1;    // the box of cells that really changed
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                // The bottom rows are the floor of the volume. Carving them opens a
                // hole out of the world; filling (negative amount) is still allowed.
                if (floorLayers > 0 && amount > 0.0f && y < floorLayers) continue;
                float dx = x - gx, dy = y - gy, dz = z - gz;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > gr) continue;
                float t = 1.0f - d / gr;
                float fall = t * t * (3.0f - 2.0f * t);    // smoothstep sphere (softer edge)
                float& cell = density[Index(x, y, z)];
                float nv = StepDensity(cell, amount, fall, Clamp());   // subtract = carve air
                if (nv != cell) {
                    cell = nv; changed = true;
                    if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
                    if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
                    if (z < cz0) cz0 = z; if (z > cz1) cz1 = z;
                }
            }
    if (changed) TouchBox(cx0, cy0, cz0, cx1, cy1, cz1);
    return changed;
}

bool VoxelTerrain::Add(const Vec3& local, float radius, float amount) {
    return Dig(local, radius, -amount);   // adding is digging with negative amount
}

bool VoxelTerrain::SmoothAt(const Vec3& local, float radius, float amount) {
    if (!(amount > 0.0f) || !(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(amount) || !Finite3(local)) return false;
    if (amount > 1.0f) amount = 1.0f;
    float gx = (local.x + HalfX()) / voxelSize;
    float gy = local.y / voxelSize;
    float gz = (local.z + HalfZ()) / voxelSize;
    float gr = radius / voxelSize;
    int x0 = std::max(1, FloorI(gx - gr)), x1 = std::min(nx - 2, CeilI(gx + gr));
    int y0 = std::max(std::max(1, floorLayers), FloorI(gy - gr));
    int y1 = std::min(ny - 2, CeilI(gy + gr));
    int z0 = std::max(1, FloorI(gz - gr)), z1 = std::min(nz - 2, CeilI(gz + gr));
    if (x0 > x1 || y0 > y1 || z0 > z1) return false;
    // Read from a snapshot of the box (and one cell around it) so the relaxation is even. The old code
    // copied the whole field for every call: megabytes per frame on a big world.
    const int sx = x1 - x0 + 3, sy = y1 - y0 + 3, sz = z1 - z0 + 3;
    std::vector<float> snap((std::size_t)sx * sy * sz);
    for (int z = 0; z < sz; ++z)
        for (int y = 0; y < sy; ++y) {
            const float* row = &density[Index(x0 - 1, y0 - 1 + y, z0 - 1 + z)];
            std::copy(row, row + sx, snap.begin() + ((std::size_t)z * sy + y) * sx);
        }
    auto S = [&](int x, int y, int z) { return snap[((std::size_t)(z - z0 + 1) * sy + (y - y0 + 1)) * sx + (x - x0 + 1)]; };
    int cx0 = nx, cy0 = ny, cz0 = nz, cx1 = -1, cy1 = -1, cz1 = -1;
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x - gx, dy = y - gy, dz = z - gz;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > gr) continue;
                // 6-neighbour average (a light Laplacian relax).
                float avg = (S(x - 1, y, z) + S(x + 1, y, z) +
                             S(x, y - 1, z) + S(x, y + 1, z) +
                             S(x, y, z - 1) + S(x, y, z + 1)) * (1.0f / 6.0f);
                float w = (1.0f - d / gr) * amount;
                float& cell = density[Index(x, y, z)];
                float nv = ClampTo(cell + (avg - cell) * w, Clamp());
                if (nv != cell) {
                    cell = nv; changed = true;
                    if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
                    if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
                    if (z < cz0) cz0 = z; if (z > cz1) cz1 = z;
                }
            }
    if (changed) TouchBox(cx0, cy0, cz0, cx1, cy1, cz1);
    return changed;
}

bool VoxelTerrain::DigBox(const Vec3& a, const Vec3& b, float amount) {
    if (!Finite3(a) || !Finite3(b) || !std::isfinite(amount)) return false;
    Vec3 lo{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    Vec3 hi{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    int x0 = std::max(0, FloorI((lo.x + HalfX()) / voxelSize));
    int x1 = std::min(nx - 1, CeilI((hi.x + HalfX()) / voxelSize));
    int y0 = std::max(0, FloorI(lo.y / voxelSize));
    int y1 = std::min(ny - 1, CeilI(hi.y / voxelSize));
    int z0 = std::max(0, FloorI((lo.z + HalfZ()) / voxelSize));
    int z1 = std::min(nz - 1, CeilI((hi.z + HalfZ()) / voxelSize));
    int cx0 = nx, cy0 = ny, cz0 = nz, cx1 = -1, cy1 = -1, cz1 = -1;
    bool changed = false;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                if (floorLayers > 0 && amount > 0.0f && y < floorLayers) continue;
                float& cell = density[Index(x, y, z)];
                float nv = ClampTo(cell - amount, Clamp());
                if (nv != cell) {
                    cell = nv; changed = true;
                    if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
                    if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
                    if (z < cz0) cz0 = z; if (z > cz1) cz1 = z;
                }
            }
    if (changed) TouchBox(cx0, cy0, cz0, cx1, cy1, cz1);
    return changed;
}

// ---- floating pieces ------------------------------------------------------------------------------
namespace {
// Labels the 6-connected solid components (density > iso) that lie inside the region [rlo, rhi] and
// collects the cells of every one with fewer than `minVoxels` voxels. Seeds come from [slo, shi]. With
// `stopWhenBig` a walk ends as soon as its piece reaches minVoxels (it is attached, so it is not a crumb):
// the walk then never goes further than minVoxels cells from its seed, which is why a region that is
// minVoxels wider than the seed box can never cut a big piece short.
//   A cell is 0 (not seen), 1 (seen by a walk that found a crumb) or 2 (known to belong to a big piece:
//   the cells of a walk that stopped early, including the ones it had queued but not looked at yet). A walk
//   that runs into a 2 is part of that big piece too, however few cells it saw itself.
void FindFloaters(const std::vector<float>& dens, int nx, int ny, float iso,
                  const int rlo[3], const int rhi[3], const int slo[3], const int shi[3],
                  int minVoxels, bool stopWhenBig, std::vector<int>& outCells) {
    const std::size_t rx = (std::size_t)(rhi[0] - rlo[0] + 1), ry = (std::size_t)(rhi[1] - rlo[1] + 1),
                      rz = (std::size_t)(rhi[2] - rlo[2] + 1);
    std::vector<unsigned char> seen(rx * ry * rz, 0);
    auto vidx = [&](int x, int y, int z) {
        return (std::size_t)(x - rlo[0]) + rx * ((std::size_t)(y - rlo[1]) + ry * (std::size_t)(z - rlo[2]));
    };
    const std::size_t gsy = (std::size_t)nx, gsz = (std::size_t)nx * ny;
    auto gidx = [&](int x, int y, int z) { return (std::size_t)x + gsy * (std::size_t)y + gsz * (std::size_t)z; };
    auto pack = [](int x, int y, int z) { return (std::uint64_t)x | ((std::uint64_t)y << 21) | ((std::uint64_t)z << 42); };
    std::vector<std::uint64_t> stack;
    std::vector<int> comp;                 // global indices of the cells this walk has taken
    std::vector<std::size_t> compV;        // the same cells in the region's own numbering
    for (int sz = slo[2]; sz <= shi[2]; ++sz)
        for (int sy = slo[1]; sy <= shi[1]; ++sy)
            for (int sx = slo[0]; sx <= shi[0]; ++sx) {
                if (dens[gidx(sx, sy, sz)] <= iso) continue;
                const std::size_t v0 = vidx(sx, sy, sz);
                if (seen[v0]) continue;
                comp.clear(); compV.clear(); stack.clear();
                stack.push_back(pack(sx, sy, sz));
                seen[v0] = 1;
                bool attached = false;
                while (!stack.empty() && !attached) {
                    const std::uint64_t pk = stack.back(); stack.pop_back();
                    const int x = (int)(pk & 0x1FFFFF), y = (int)((pk >> 21) & 0x1FFFFF), z = (int)(pk >> 42);
                    comp.push_back((int)gidx(x, y, z));
                    if (stopWhenBig) compV.push_back(vidx(x, y, z));   // only the local walk needs to mark attached pieces
                    if (stopWhenBig && (int)comp.size() >= minVoxels) { attached = true; break; }
                    auto visit = [&](int xx, int yy, int zz) {
                        if (xx < rlo[0] || xx > rhi[0] || yy < rlo[1] || yy > rhi[1] || zz < rlo[2] || zz > rhi[2]) return;
                        if (dens[gidx(xx, yy, zz)] <= iso) return;
                        const std::size_t vi = vidx(xx, yy, zz);
                        if (seen[vi] == 2) { attached = true; return; }
                        if (seen[vi]) return;
                        seen[vi] = 1;
                        stack.push_back(pack(xx, yy, zz));
                    };
                    visit(x + 1, y, z); visit(x - 1, y, z); visit(x, y + 1, z);
                    visit(x, y - 1, z); visit(x, y, z + 1); visit(x, y, z - 1);
                }
                if (attached) {
                    for (std::size_t vi : compV) seen[vi] = 2;
                    for (std::uint64_t pk : stack)
                        seen[vidx((int)(pk & 0x1FFFFF), (int)((pk >> 21) & 0x1FFFFF), (int)(pk >> 42))] = 2;
                } else if ((int)comp.size() < minVoxels) {
                    outCells.insert(outCells.end(), comp.begin(), comp.end());
                }
            }
}
} // namespace

int VoxelTerrain::RemoveFloaters(int minVoxels) {
    if (minVoxels <= 1) return 0;
    const int rlo[3] = {0, 0, 0}, rhi[3] = {nx - 1, ny - 1, nz - 1};
    std::vector<int> cells;
    FindFloaters(density, nx, ny, iso, rlo, rhi, rlo, rhi, minVoxels, false, cells);
    const float air = iso - Clamp();          // a firmly-air density value
    int cleared = 0;
    for (int idx : cells) {
        const int x = idx % nx, y = (idx / nx) % ny, z = idx / (nx * ny);
        if (floorLayers > 0 && y < floorLayers) continue;   // leave the floor
        density[(std::size_t)idx] = air;
        ++cleared;
        TouchCell(x, y, z);                   // only the chunks around this voxel are meshed again
    }
    floatersSwept_ = true;
    editAny_ = false;
    return cleared;
}

int VoxelTerrain::RemoveFloatersEdited(int minVoxels) {
    if (minVoxels <= 1) return 0;
    if (!floatersSwept_) return RemoveFloaters(minVoxels);   // first time: the whole field, as it always was
    if (!editAny_) return 0;
    const int lim[3] = {nx - 1, ny - 1, nz - 1};
    int slo[3], shi[3], rlo[3], rhi[3];
    std::size_t volume = 1;
    for (int a = 0; a < 3; ++a) {
        slo[a] = std::max(0, editMin_[a] - 1);
        shi[a] = std::min(lim[a], editMax_[a] + 1);
        rlo[a] = std::max(0, slo[a] - minVoxels);
        rhi[a] = std::min(lim[a], shi[a] + minVoxels);
        volume *= (std::size_t)(rhi[a] - rlo[a] + 1);
    }
    if (volume > (std::size_t)Count() / 2 || volume > ((std::size_t)1 << 24)) return RemoveFloaters(minVoxels);
    std::vector<int> cells;
    FindFloaters(density, nx, ny, iso, rlo, rhi, slo, shi, minVoxels, true, cells);
    const float air = iso - Clamp();
    int cleared = 0;
    for (int idx : cells) {
        const int x = idx % nx, y = (idx / nx) % ny, z = idx / (nx * ny);
        if (floorLayers > 0 && y < floorLayers) continue;
        density[(std::size_t)idx] = air;
        ++cleared;
        TouchCell(x, y, z);
    }
    editAny_ = false;
    return cleared;
}

float VoxelTerrain::SampleDensity(const Vec3& local) const {
    float gx = (local.x + HalfX()) / voxelSize;
    float gy = local.y / voxelSize;
    float gz = (local.z + HalfZ()) / voxelSize;
    int x0 = (int)std::floor(gx), y0 = (int)std::floor(gy), z0 = (int)std::floor(gz);
    float fx = gx - x0, fy = gy - y0, fz = gz - z0;
    auto at = [&](int x, int y, int z) {
        if (x < 0) x = 0; if (y < 0) y = 0; if (z < 0) z = 0;
        if (x >= nx) x = nx - 1; if (y >= ny) y = ny - 1; if (z >= nz) z = nz - 1;
        return density[Index(x, y, z)];
    };
    float c000 = at(x0, y0, z0),     c100 = at(x0 + 1, y0, z0);
    float c010 = at(x0, y0 + 1, z0), c110 = at(x0 + 1, y0 + 1, z0);
    float c001 = at(x0, y0, z0 + 1), c101 = at(x0 + 1, y0, z0 + 1);
    float c011 = at(x0, y0 + 1, z0 + 1), c111 = at(x0 + 1, y0 + 1, z0 + 1);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    return L(L(L(c000, c100, fx), L(c010, c110, fx), fy),
             L(L(c001, c101, fx), L(c011, c111, fx), fy), fz);
}

Vec3 VoxelTerrain::SurfaceNormal(const Vec3& local) const {
    float e = voxelSize * 0.5f;
    float dx = SampleDensity({local.x + e, local.y, local.z}) - SampleDensity({local.x - e, local.y, local.z});
    float dy = SampleDensity({local.x, local.y + e, local.z}) - SampleDensity({local.x, local.y - e, local.z});
    float dz = SampleDensity({local.x, local.y, local.z + e}) - SampleDensity({local.x, local.y, local.z - e});
    // Density increases INTO solid, so the outward normal is -gradient.
    Vec3 n{-dx, -dy, -dz};
    float L = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return L > 1e-6f ? Vec3{n.x / L, n.y / L, n.z / L} : Vec3{0, 1, 0};
}

bool VoxelTerrain::SurfaceY(float lx, float lz, float& outY) const {
    // March from the top down; the first solid sample's top face is the surface.
    for (int y = ny - 1; y >= 0; --y) {
        float wy = y * voxelSize;
        if (SampleDensity(Vec3{lx, wy, lz}) > iso) {
            // Refine between this solid sample and the air one above it.
            float above = (y + 1 < ny) ? (y + 1) * voxelSize : wy + voxelSize;
            float dSolid = SampleDensity(Vec3{lx, wy, lz});
            float dAir   = SampleDensity(Vec3{lx, above, lz});
            float t = (dAir != dSolid) ? (iso - dSolid) / (dAir - dSolid) : 0.0f;
            if (t < 0) t = 0; if (t > 1) t = 1;
            outY = wy + (above - wy) * t;
            return true;
        }
    }
    return false;
}

// ---- marching cubes -----------------------------------------------------------------------------------
// The loop below does the same arithmetic as AppendCubesReference (the original, further down), operation
// for operation, so every vertex, normal and colour is bit for bit the same; it only skips work:
//  * a cube whose corners are all on one side of the surface is dropped before any position is computed,
//  * the density is read straight from the array when the sample cube lies inside the grid (no clamping),
//  * a vertex sits on one of 12 cube edges and up to five triangles share it: its normal (six trilinear
//    samples) is worked out once per edge instead of once per triangle corner.
void VoxelTerrain::AppendCubes(Mesh& m, int x0, int y0, int z0, int x1, int y1, int z1) const {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 > nx - 1) x1 = nx - 1;
    if (y1 > ny - 1) y1 = ny - 1;
    if (z1 > nz - 1) z1 = nz - 1;
    if (x0 >= x1 || y0 >= y1 || z0 >= z1) return;

    const float vs = voxelSize;
    const float halfX = HalfX(), halfZ = HalfZ();
    const float e = vs * 0.5f;
    const float* dens = density.data();
    const std::ptrdiff_t sy = nx, sz = (std::ptrdiff_t)nx * ny;
    const int gnx = nx, gny = ny, gnz = nz;
    const bool useMat = UseMaterials();

    // Trilinear density at a local-space point: SampleDensity's arithmetic exactly.
    auto sample = [&](float lx, float ly, float lz) -> float {
        const float gx = (lx + halfX) / vs;
        const float gy = ly / vs;
        const float gz = (lz + halfZ) / vs;
        const int ix = FloorI(gx), iy = FloorI(gy), iz = FloorI(gz);
        const float fx = gx - ix, fy = gy - iy, fz = gz - iz;
        float c000, c100, c010, c110, c001, c101, c011, c111;
        if (ix >= 0 && iy >= 0 && iz >= 0 && ix + 1 < gnx && iy + 1 < gny && iz + 1 < gnz) {
            const float* p = dens + (ix + (std::ptrdiff_t)iy * sy + (std::ptrdiff_t)iz * sz);
            c000 = p[0];  c100 = p[1];  c010 = p[sy];  c110 = p[sy + 1];
            c001 = p[sz]; c101 = p[sz + 1]; c011 = p[sz + sy]; c111 = p[sz + sy + 1];
        } else {
            auto at = [&](int x, int y, int z) {
                if (x < 0) x = 0; if (y < 0) y = 0; if (z < 0) z = 0;
                if (x >= gnx) x = gnx - 1; if (y >= gny) y = gny - 1; if (z >= gnz) z = gnz - 1;
                return dens[x + (std::ptrdiff_t)y * sy + (std::ptrdiff_t)z * sz];
            };
            c000 = at(ix, iy, iz);         c100 = at(ix + 1, iy, iz);
            c010 = at(ix, iy + 1, iz);     c110 = at(ix + 1, iy + 1, iz);
            c001 = at(ix, iy, iz + 1);     c101 = at(ix + 1, iy, iz + 1);
            c011 = at(ix, iy + 1, iz + 1); c111 = at(ix + 1, iy + 1, iz + 1);
        }
        auto L = [](float a, float b, float t) { return a + (b - a) * t; };
        return L(L(L(c000, c100, fx), L(c010, c110, fx), fy),
                 L(L(c001, c101, fx), L(c011, c111, fx), fy), fz);
    };
    // Gradient of the density field (central differences) -> outward normal.
    auto normalAt = [&](const Vec3& p) -> Vec3 {
        float dx = sample(p.x + e, p.y, p.z) - sample(p.x - e, p.y, p.z);
        float dy = sample(p.x, p.y + e, p.z) - sample(p.x, p.y - e, p.z);
        float dz = sample(p.x, p.y, p.z + e) - sample(p.x, p.y, p.z - e);
        Vec3 n{-dx, -dy, -dz};   // points toward decreasing density (out of solid)
        float L = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        return L > 1e-6f ? Vec3{n.x / L, n.y / L, n.z / L} : Vec3{0, 1, 0};
    };
    auto faceColor = [&](const Vec3& p, const Vec3& n) {
        float slope = 1.0f - std::fabs(n.y);
        if (p.y >= snowLevel) return snowColor;
        if (slope >= rockSlope) return rockColor;
        float t = slope / rockSlope; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return Color{grassColor.r + (soilColor.r - grassColor.r) * t,
                     grassColor.g + (soilColor.g - grassColor.g) * t,
                     grassColor.b + (soilColor.b - grassColor.b) * t, 1.0f};
    };

    const CubeCaseTable& cases = CubeCases();
    // Corner c of the cube at p is p[cornerOff[c]] (corner numbering: see CubeCases).
    const std::ptrdiff_t cornerOff[8] = {0, 1, sy, sy + 1, sz, sz + 1, sz + sy, sz + sy + 1};
    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y) {
            const float* row = dens + ((std::ptrdiff_t)y * sy + (std::ptrdiff_t)z * sz);
            for (int x = x0; x < x1; ++x) {
                const float* p = row + x;
                float val[8];
                for (int i = 0; i < 8; ++i) val[i] = p[cornerOff[i]];
                int pattern = 0;
                for (int i = 0; i < 8; ++i) if (val[i] < iso) pattern |= (1 << i);
                if (pattern == 0 || pattern == 255) continue;         // all corners on one side: no surface here
                const CubeCase& cc = cases.cases[pattern];
                const int edges = cc.edges;
                if (edges == 0) continue;
                Vec3 pos[8];
                for (int i = 0; i < 8; ++i)
                    pos[i] = Vec3{(x + (i & 1)) * vs - halfX, (y + ((i >> 1) & 1)) * vs, (z + ((i >> 2) & 1)) * vs - halfZ};
                Vec3 vert[12], nrm[12];
                unsigned nmask = 0;
                for (int ed = 0; ed < 12; ++ed) {
                    if (!(edges & (1 << ed))) continue;
                    const int a = cases.edgeA[ed], b = cases.edgeB[ed];
                    const float v1 = val[a], v2 = val[b];
                    const float t = (std::fabs(v2 - v1) > 1e-6f) ? (iso - v1) / (v2 - v1) : 0.5f;
                    vert[ed] = pos[a] + (pos[b] - pos[a]) * t;
                }
                for (int t = 0; t < 3 * cc.triCount; t += 3) {
                    const int ea = cc.tri[t], eb = cc.tri[t + 1], ec = cc.tri[t + 2];
                    Vec3 a = vert[ea], b = vert[eb], c = vert[ec];
                    auto nrmOf = [&](int ed) -> Vec3 {
                        if (!(nmask & (1u << ed))) { nrm[ed] = normalAt(vert[ed]); nmask |= 1u << ed; }
                        return nrm[ed];
                    };
                    Vec3 na = nrmOf(ea), nb = nrmOf(eb), nc = nrmOf(ec);
                    // Force a consistent OUTWARD winding (geometric normal agrees with
                    // the gradient normal). This lets the mesh render single-sided —
                    // backface culling then skips ~half the triangles, a big speedup —
                    // without the surface ever culling its visible side.
                    Vec3 g = Vec3::Cross(b - a, c - a);
                    Vec3 navg{na.x + nb.x + nc.x, na.y + nb.y + nc.y, na.z + nb.z + nc.z};
                    if (g.x * navg.x + g.y * navg.y + g.z * navg.z < 0.0f) {
                        std::swap(b, c); std::swap(nb, nc);
                    }
                    int base = (int)m.vertices.size();
                    m.vertices.push_back(a); m.vertices.push_back(b); m.vertices.push_back(c);
                    m.normals.push_back(na); m.normals.push_back(nb); m.normals.push_back(nc);
                    m.uvs.push_back({a.x * 0.25f, a.z * 0.25f});
                    m.uvs.push_back({b.x * 0.25f, b.z * 0.25f});
                    m.uvs.push_back({c.x * 0.25f, c.z * 0.25f});
                    m.triangles.push_back(base); m.triangles.push_back(base + 1); m.triangles.push_back(base + 2);
                    if (useMat) {
                        // the face takes the colour of the most solid corner of its cube
                        int bi = 0; for (int i = 1; i < 8; ++i) if (val[i] > val[bi]) bi = i;
                        const std::size_t mi = (std::size_t)((p - dens) + cornerOff[bi]);
                        const unsigned char id = material[mi];
                        m.triColors.push_back(id < materials.size() ? materials[id].color : color);
                    } else if (autoColor) {
                        Vec3 ctr{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
                        Vec3 fn{(na.x + nb.x + nc.x) / 3, (na.y + nb.y + nc.y) / 3, (na.z + nb.z + nc.z) / 3};
                        m.triColors.push_back(faceColor(ctr, fn));
                    }
                }
            }
        }
}

// The original marching-cubes loop, unchanged apart from its name and the case table (both loops read
// CubeCases()). It is the reference the tests hold AppendCubes to, and what OKAY_VOXEL_OLD_REMESH=1 runs.
void VoxelTerrain::AppendCubesReference(Mesh& m, int x0, int y0, int z0, int x1, int y1, int z1) const {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 > nx - 1) x1 = nx - 1;
    if (y1 > ny - 1) y1 = ny - 1;
    if (z1 > nz - 1) z1 = nz - 1;
    if (x0 >= x1 || y0 >= y1 || z0 >= z1) return;
    auto interp = [&](const Vec3& p1, const Vec3& p2, float v1, float v2) {
        float t = (std::fabs(v2 - v1) > 1e-6f) ? (iso - v1) / (v2 - v1) : 0.5f;
        return p1 + (p2 - p1) * t;
    };
    // Gradient of the density field (central differences) -> outward normal.
    auto normalAt = [&](const Vec3& local) {
        float e = voxelSize * 0.5f;
        float dx = SampleDensity({local.x + e, local.y, local.z}) - SampleDensity({local.x - e, local.y, local.z});
        float dy = SampleDensity({local.x, local.y + e, local.z}) - SampleDensity({local.x, local.y - e, local.z});
        float dz = SampleDensity({local.x, local.y, local.z + e}) - SampleDensity({local.x, local.y, local.z - e});
        Vec3 n{-dx, -dy, -dz};   // points toward decreasing density (out of solid)
        float L = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        return L > 1e-6f ? Vec3{n.x / L, n.y / L, n.z / L} : Vec3{0, 1, 0};
    };
    auto faceColor = [&](const Vec3& p, const Vec3& n) {
        float slope = 1.0f - std::fabs(n.y);
        if (p.y >= snowLevel) return snowColor;
        if (slope >= rockSlope) return rockColor;
        float t = slope / rockSlope; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return Color{grassColor.r + (soilColor.r - grassColor.r) * t,
                     grassColor.g + (soilColor.g - grassColor.g) * t,
                     grassColor.b + (soilColor.b - grassColor.b) * t, 1.0f};
    };

    const CubeCaseTable& cases = CubeCases();
    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                float val[8]; Vec3 pos[8];
                int pattern = 0;
                for (int i = 0; i < 8; ++i) {
                    int cx = x + (i & 1), cy = y + ((i >> 1) & 1), cz = z + ((i >> 2) & 1);
                    val[i] = density[Index(cx, cy, cz)];
                    pos[i] = GridToLocal(cx, cy, cz);
                    if (val[i] < iso) pattern |= (1 << i);
                }
                const CubeCase& cc = cases.cases[pattern];
                int edges = cc.edges;
                if (edges == 0) continue;
                Vec3 vert[12];
                for (int e = 0; e < 12; ++e) {
                    if (!(edges & (1 << e))) continue;
                    int a = cases.edgeA[e], b = cases.edgeB[e];
                    vert[e] = interp(pos[a], pos[b], val[a], val[b]);
                }
                for (int t = 0; t < 3 * cc.triCount; t += 3) {
                    Vec3 a = vert[cc.tri[t]];
                    Vec3 b = vert[cc.tri[t + 1]];
                    Vec3 c = vert[cc.tri[t + 2]];
                    Vec3 na = normalAt(a), nb = normalAt(b), nc = normalAt(c);
                    Vec3 g = Vec3::Cross(b - a, c - a);
                    Vec3 navg{na.x + nb.x + nc.x, na.y + nb.y + nc.y, na.z + nb.z + nc.z};
                    if (g.x * navg.x + g.y * navg.y + g.z * navg.z < 0.0f) {
                        std::swap(b, c); std::swap(nb, nc);
                    }
                    int base = (int)m.vertices.size();
                    m.vertices.push_back(a); m.vertices.push_back(b); m.vertices.push_back(c);
                    m.normals.push_back(na); m.normals.push_back(nb); m.normals.push_back(nc);
                    m.uvs.push_back({a.x * 0.25f, a.z * 0.25f});
                    m.uvs.push_back({b.x * 0.25f, b.z * 0.25f});
                    m.uvs.push_back({c.x * 0.25f, c.z * 0.25f});
                    m.triangles.push_back(base); m.triangles.push_back(base + 1); m.triangles.push_back(base + 2);
                    if (autoColor) {
                        Vec3 ctr{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
                        Vec3 fn{(na.x + nb.x + nc.x) / 3, (na.y + nb.y + nc.y) / 3, (na.z + nb.z + nc.z) / 3};
                        m.triColors.push_back(faceColor(ctr, fn));
                    }
                }
            }
}

Mesh VoxelTerrain::BuildMesh() const {
    Mesh m;
    m.name = "VoxelTerrain";
    AppendCubes(m, 0, 0, 0, nx - 1, ny - 1, nz - 1);
    return m;
}

Mesh VoxelTerrain::BuildMeshReference() const {
    Mesh m;
    m.name = "VoxelTerrain";
    AppendCubesReference(m, 0, 0, 0, nx - 1, ny - 1, nz - 1);
    return m;
}

bool VoxelTerrain::SealFloor() {
    if (floorLayers <= 0 || nx <= 0 || ny <= 0 || nz <= 0) return false;
    const int yMax = floorLayers < ny ? floorLayers : ny;
    const float solid = Clamp();
    bool changed = false;
    for (int y = 0; y < yMax; ++y)
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x) {
                float& c = density[Index(x, y, z)];
                if (c <= iso) { c = solid; changed = true; }
            }
    return changed;
}

unsigned VoxelTerrain::MeshStamp() const {
    unsigned h = 2166136261u;
    auto mix = [&](float f) {
        unsigned u = 0;
        std::memcpy(&u, &f, sizeof(u));
        h = h * 16777619u ^ u;
    };
    mix(iso); mix(snowLevel); mix(rockSlope);
    mix(grassColor.r); mix(grassColor.g); mix(grassColor.b);
    mix(soilColor.r); mix(soilColor.g); mix(soilColor.b);
    mix(rockColor.r); mix(rockColor.g); mix(rockColor.b);
    mix(snowColor.r); mix(snowColor.g); mix(snowColor.b);
    h ^= autoColor ? 1u : 0u;
    h = h * 16777619u ^ matRev_;
    h ^= UseMaterials() ? 2u : 0u;
    return h;
}

// ---- chunk table and the hand-off to the renderer ------------------------------------------------
bool VoxelTerrain::RunningGame() const {
    const Scene* s = gameObject ? gameObject->scene() : nullptr;
    return s && s->runScripts;
}

// Components that read the terrain object's own mesh (a collider is fitted to it or cuts triangles from it, an
// occluder or LOD group draws from it). With chunk objects that mesh is empty, so Auto leaves such a terrain on the
// one combined mesh. (Chunked, asked for by name, does not look: the caller knows.)
bool VoxelTerrain::MeshReadByOthers() const {
    return gameObject && (gameObject->GetComponent<Collider3D>() || gameObject->GetComponent<LODGroup>() ||
                          gameObject->GetComponent<Occluder>());
}

void VoxelTerrain::CubeRange(int cx, int cy, int cz, int& x1, int& y1, int& z1) const {
    x1 = std::min(cx * kChunk + kChunk, std::max(0, nx - 1));
    y1 = std::min(cy * kChunk + kChunk, std::max(0, ny - 1));
    z1 = std::min(cz * kChunk + kChunk, std::max(0, nz - 1));
}

// (Re)build the chunk table when the grid, the voxel size or anything that changes the mesh itself
// (iso, colours) changed. Returns true when it did: every chunk is then meshed again.
bool VoxelTerrain::EnsureChunkTable() {
    const int cubesX = std::max(0, nx - 1), cubesY = std::max(0, ny - 1), cubesZ = std::max(0, nz - 1);
    const int ncx = std::max(1, (cubesX + kChunk - 1) / kChunk);
    const int ncy = std::max(1, (cubesY + kChunk - 1) / kChunk);
    const int ncz = std::max(1, (cubesZ + kChunk - 1) / kChunk);
    const unsigned stamp = MeshStamp();
    const int nChunks = ncx * ncy * ncz;
    if (mcNx_ == nx && mcNy_ == ny && mcNz_ == nz && mcVox_ == voxelSize && mcStamp_ == stamp &&
        (int)mcChunks_.size() == nChunks && (int)mcDirty_.size() == nChunks)
        return false;
    DestroyChunkObjects();
    mcChunks_.assign((std::size_t)nChunks, McChunk{});
    mcDirty_.assign((std::size_t)nChunks, 0);
    mcDirtyCount_ = 0;
    mcNx_ = nx; mcNy_ = ny; mcNz_ = nz; mcVox_ = voxelSize; mcStamp_ = stamp;
    ncx_ = ncx; ncy_ = ncy; ncz_ = ncz;
    mcAllDirty_ = true;
    combinedPtr_ = nullptr; combinedVerts_ = 0;
    return true;
}

void VoxelTerrain::CombineInto(MeshRenderer& mr) {
    Mesh combined;
    combined.name = "VoxelTerrain";
    std::size_t nv = 0, nt = 0, nc = 0;
    for (const McChunk& c : mcChunks_) {
        nv += c.mesh.vertices.size();
        nt += c.mesh.triangles.size();
        nc += c.mesh.triColors.size();
    }
    combined.vertices.reserve(nv);
    combined.normals.reserve(nv);
    combined.uvs.reserve(nv);
    combined.triangles.reserve(nt);
    if (nc) combined.triColors.reserve(nc);
    for (const McChunk& c : mcChunks_) {
        const int base = (int)combined.vertices.size();
        combined.vertices.insert(combined.vertices.end(), c.mesh.vertices.begin(), c.mesh.vertices.end());
        combined.normals.insert(combined.normals.end(), c.mesh.normals.begin(), c.mesh.normals.end());
        combined.uvs.insert(combined.uvs.end(), c.mesh.uvs.begin(), c.mesh.uvs.end());
        for (int idx : c.mesh.triangles) combined.triangles.push_back(base + idx);
        if (!c.mesh.triColors.empty())
            combined.triColors.insert(combined.triColors.end(), c.mesh.triColors.begin(), c.mesh.triColors.end());
    }
    lastStats.bytesHandedOff += MeshBytes(combined);
    // A fresh Mesh (new buffers) so the renderer uploads this frame. An in-place
    // edit of the old vertex vector can sit unseen until a sampled hash notices.
    mr.mesh = std::move(combined);
    combinedPtr_ = mr.mesh.vertices.data();
    combinedVerts_ = mr.mesh.vertices.size();
}

GameObject* VoxelTerrain::CreateChunkObject(int cx, int cy, int cz) {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return nullptr;
    GameObject* g = sc->CreateGameObject("Voxel Chunk " + std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(cz));
    if (!g) return nullptr;
    g->transient = true;                      // runtime only: the scene file never gets these
    g->layer = gameObject->layer;
    if (transform && g->transform) g->transform->SetParent(transform, /*worldPositionStays=*/false);
    g->AddComponent<MeshRenderer>();
    return g;
}

void VoxelTerrain::HandOffChunk(int ci, int cx, int cy, int cz, Mesh&& fresh, const MeshRenderer& parent) {
    McChunk& c = mcChunks_[(std::size_t)ci];
    GameObject* g = c.obj ? GameObject::FromInstanceId(c.obj) : nullptr;
    const bool has = !fresh.vertices.empty();
    if (!g) {
        c.obj = 0;
        if (!has) { c.lastVerts = 0; return; }       // a chunk that never had a surface gets no object
        g = CreateChunkObject(cx, cy, cz);
        if (!g) return;
        c.obj = g->InstanceId();
        MirrorMaterial(parent, *g->GetComponent<MeshRenderer>());
    }
    MeshRenderer* cm = g->GetComponent<MeshRenderer>();
    if (!cm) { cm = g->AddComponent<MeshRenderer>(); MirrorMaterial(parent, *cm); }
    c.lastVerts = (int)fresh.vertices.size();
    ++lastStats.chunksHandedOff;
    lastStats.bytesHandedOff += MeshBytes(fresh);
    // Brand-new buffers (the old ones are still alive while these are allocated), so the renderer's
    // pointer check sees a different mesh and re-uploads THIS chunk, and only this chunk.
    cm->mesh = std::move(fresh);
}

void VoxelTerrain::EnterChunked(MeshRenderer& parent) {
    chunked_ = true;
    if (!mcAllDirty_) {
        int ci = 0;
        for (int cz = 0; cz < ncz_; ++cz)
            for (int cy = 0; cy < ncy_; ++cy)
                for (int cx = 0; cx < ncx_; ++cx, ++ci) {
                    McChunk& c = mcChunks_[(std::size_t)ci];
                    if (c.mesh.vertices.empty()) continue;
                    if (mcDirty_[(std::size_t)ci]) { c.mesh = Mesh{}; continue; }   // replaced right after this
                    Mesh keep = std::move(c.mesh);
                    c.mesh = Mesh{};
                    HandOffChunk(ci, cx, cy, cz, std::move(keep), parent);
                }
    } else {
        for (McChunk& c : mcChunks_) c.mesh = Mesh{};
    }
    parent.mesh = Mesh{};          // the chunk objects draw the surface now; no second copy
    parent.mesh.name = "VoxelTerrain";   // (a saved scene still names it; the terrain rebuilds the surface on load)
    combinedPtr_ = nullptr; combinedVerts_ = 0;
    matSig_ = 0;                   // copy the material to every chunk on the next sync
}

void VoxelTerrain::LeaveChunked() {
    DestroyChunkObjects();
    chunked_ = false;
    mcAllDirty_ = true;            // the combined mesh is built again from the field
    combinedPtr_ = nullptr; combinedVerts_ = 0;
}

void VoxelTerrain::SyncChunkMaterials(const MeshRenderer& parent) {
    for (McChunk& c : mcChunks_) {
        if (!c.obj) continue;
        GameObject* g = GameObject::FromInstanceId(c.obj);
        if (!g) continue;
        if (MeshRenderer* cm = g->GetComponent<MeshRenderer>()) MirrorMaterial(parent, *cm);
    }
    matSig_ = MaterialSig(parent);
}

void VoxelTerrain::DestroyChunkObjects() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    for (McChunk& c : mcChunks_) {
        if (!c.obj) continue;
        GameObject* g = GameObject::FromInstanceId(c.obj);
        if (g) {
            g->active = false;           // gone from this frame's picture; the scene frees it at the end of the frame
            if (sc) sc->Destroy(g);
        }
        c.obj = 0;
    }
}

long VoxelTerrain::DrawnTriangles() const {
    // cached for a few frames: summing the chunk meshes is a walk over every chunk object
    if (trisCacheTick_ != lastStats.chunksRemeshed + (int)editCount * 7 + (int)chunked_ || trisCache_ < 0) {
        long n = 0;
        if (gameObject) if (auto* mr = gameObject->GetComponent<MeshRenderer>()) n += mr->mesh.TriangleCount();
        for (const McChunk& c : mcChunks_)
            if (c.obj) if (GameObject* g = GameObject::FromInstanceId(c.obj)) if (auto* mr = g->GetComponent<MeshRenderer>()) n += mr->mesh.TriangleCount();
        trisCache_ = n; trisCacheTick_ = lastStats.chunksRemeshed + (int)editCount * 7 + (int)chunked_;
    }
    return trisCache_;
}

int VoxelTerrain::ChunkObjectCount() const {
    int n = 0;
    for (const McChunk& c : mcChunks_)
        if (c.obj && GameObject::FromInstanceId(c.obj)) ++n;
    return n;
}

int VoxelTerrain::CatchFallers() {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc || !gameObject->transform) return 0;
    Transform* tt = gameObject->transform;
    int n = 0;
    for (Rigidbody3D* rb : sc->FindObjectsOfType<Rigidbody3D>()) {
        if (!rb->gameObject || !rb->gameObject->transform || rb->gameObject == gameObject) continue;
        const Vec3 l = tt->InverseTransformPoint(rb->gameObject->transform->Position());
        if (!std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z) || l.y >= -catchMargin * voxelSize) continue;
        ++n; ++caughtCount;
        if (onFall) { onFall(*rb->gameObject); continue; }
        Vec3 in{std::max(-HalfX(), std::min(HalfX(), l.x)), 0.0f, std::max(-HalfZ(), std::min(HalfZ(), l.z))};
        float sy = 0.0f;
        in.y = (SurfaceY(in.x, in.z, sy) ? sy : SizeY() * 0.5f) + 2.0f * voxelSize;
        rb->gameObject->transform->SetPosition(tt->TransformPoint(in));
        rb->velocity = Vec3::Zero;
    }
    return n;
}

namespace {
// Reports this terrain's per-frame cost to the overlay (FrameOverlay::Lines) when the frame ends.
struct VoxelFrameTimer {
    VoxelTerrain* t; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    explicit VoxelFrameTimer(VoxelTerrain* v) : t(v) {}
    ~VoxelFrameTimer() {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        VoxelOverlayStats& o = FrameOverlay::Voxel();
        const double now = FrameOverlay::NowSeconds();
        if (now - o.seenAt > 1.0) { o.terrains = 0; o.triangles = 0; o.updateMs = ms; }
        o.updateMs = o.updateMs * 0.9 + ms * 0.1;
        o.seenAt = now;
        o.terrains = std::max(o.terrains, 1);
        o.chunkObjects = t->ChunkObjectCount();
        o.chunksTotal = t->lastStats.chunksTotal;
        o.triangles = t->DrawnTriangles();
        o.lastMeshMs = t->lastStats.totalMs; o.lastChunksRemeshed = t->lastStats.chunksRemeshed;
        o.oldRemesh = VoxelTerrain::OldRemeshActive();
    }
};
}

void VoxelTerrain::Update(float dt) {
    VoxelFrameTimer frameTimer(this);
    if (fluidOn || fluidObj_) UpdateFluid(dt);
    if (footprints && ((++footTick_) % 3) == 0) UpdateFootprints();
    if (genPhase_ != 0 && StepGenerate(generateBudgetMs)) Apply();
    if (catchFalling && ((++catchTick_) % 6) == 0) CatchFallers();
    if (!chunked_ || !gameObject) return;
    MeshRenderer* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) return;
    if (MaterialSig(*mr) != matSig_) SyncChunkMaterials(*mr);   // e.g. the Inspector changed the terrain's shader
}

void VoxelTerrain::OnDestroy() { DestroyChunkObjects(); if (fluidObj_) { if (Scene* sc = gameObject ? gameObject->scene() : nullptr) sc->Destroy(fluidObj_); fluidObj_ = nullptr; } }
void VoxelTerrain::ReleaseUnstarted() { DestroyChunkObjects(); }   // a terrain removed before it started (edit mode, tests)

void VoxelTerrain::Apply() {
    if (!gameObject) return;
    const auto tStart = Clock::now();
    lastStats = MeshStats{};
    MeshRenderer* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    const bool oldPath = OldRemeshActive();

    // The protected floor is only looked at when something could have changed it.
    if (oldPath || mcAllDirty_ || floorTouched_ || floorLayers != sealedLayers_) {
        if (SealFloor()) mcAllDirty_ = true;
        sealedLayers_ = floorLayers;
        floorTouched_ = false;
    }

    // Marching cubes is the expensive part of a dig. The volume is split into chunks and only the chunks
    // the edits reached are meshed again. Each cube belongs to one chunk, and a normal reads the full
    // field, so the surface matches a full rebuild along the borders.
    EnsureChunkTable();
    lastStats.chunksTotal = (int)mcChunks_.size();

    // The terrain's own renderer keeps what Apply always set: colour, triplanar texture, single sided.
    mr->color = color;
    // Single-sided: the winding is forced outward in AppendCubes, so backface culling
    // safely skips the hidden ~half of the triangles (a real frame-rate win on big
    // terrains) while caves still render correctly from the inside.
    mr->doubleSided = false;
    // Triplanar ground texture (tinted by the per-face auto-colours). Triplanar
    // keeps cave walls / overhangs / floors from smearing.
    mr->texture = texture;
    if (!texture.empty()) {
        mr->tiling = {textureTiling, textureTiling};
        mr->triplanar = true;
    } else {
        mr->triplanar = false;
    }

    // How the surface goes to the renderer. See RenderMode.
    bool wantChunked = false;
    if (!oldPath) {
        if (renderMode == RenderMode::Chunked) wantChunked = true;
        else if (renderMode == RenderMode::Auto)
            wantChunked = chunked_ || (builtOnce_ && !mcAllDirty_ && mcDirtyCount_ > 0 && RunningGame() && !MeshReadByOthers());
    }
    if (wantChunked && !gameObject->scene()) wantChunked = false;   // chunk objects need a scene to live in
    if (wantChunked && !chunked_) EnterChunked(*mr);
    else if (!wantChunked && chunked_) LeaveChunked();

    double remeshMs = 0.0, handoffMs = 0.0;
    bool anyRemeshed = false;
    int ci = 0;
    for (int cz = 0; cz < ncz_; ++cz)
        for (int cy = 0; cy < ncy_; ++cy)
            for (int cx = 0; cx < ncx_; ++cx, ++ci) {
                if (!(mcAllDirty_ || mcDirty_[(std::size_t)ci])) continue;
                int x1, y1, z1;
                CubeRange(cx, cy, cz, x1, y1, z1);
                Mesh fresh;
                fresh.name = chunked_ ? "VoxelChunk" : "VoxelTerrain";
                McChunk& ch = mcChunks_[(std::size_t)ci];
                {
                    OKAY_PROFILE("Voxel.Mesh");
                    const auto t0 = Clock::now();
                    const std::size_t hint = chunked_ ? (std::size_t)ch.lastVerts : ch.mesh.vertices.size();
                    if (hint) {
                        fresh.vertices.reserve(hint + hint / 4); fresh.normals.reserve(hint + hint / 4);
                        fresh.uvs.reserve(hint + hint / 4); fresh.triangles.reserve(hint + hint / 4);
                        if (autoColor || UseMaterials()) fresh.triColors.reserve((hint + hint / 4) / 3);
                    }
                    if (oldPath) AppendCubesReference(fresh, cx * kChunk, cy * kChunk, cz * kChunk, x1, y1, z1);
                    else         AppendCubes(fresh, cx * kChunk, cy * kChunk, cz * kChunk, x1, y1, z1);
                    remeshMs += MsSince(t0);
                }
                ++lastStats.chunksRemeshed;
                lastStats.vertices += (int)fresh.vertices.size();
                lastStats.triangles += fresh.TriangleCount();
                anyRemeshed = true;
                {
                    OKAY_PROFILE("Voxel.Upload");
                    const auto t1 = Clock::now();
                    if (chunked_) HandOffChunk(ci, cx, cy, cz, std::move(fresh), *mr);
                    else { ch.lastVerts = (int)fresh.vertices.size(); ch.mesh = std::move(fresh); }
                    handoffMs += MsSince(t1);
                }
            }

    if (!chunked_) {
        // Single mesh: copy the chunks together again when anything changed (or the mesh was replaced).
        const bool stale = oldPath || anyRemeshed || combinedPtr_ != (const void*)mr->mesh.vertices.data() ||
                           combinedVerts_ != mr->mesh.vertices.size();
        if (stale) {
            OKAY_PROFILE("Voxel.Upload");
            const auto t1 = Clock::now();
            CombineInto(*mr);
            handoffMs += MsSince(t1);
        }
    } else {
        if (MaterialSig(*mr) != matSig_) SyncChunkMaterials(*mr);
    }

    std::fill(mcDirty_.begin(), mcDirty_.end(), (unsigned char)0);
    mcDirtyCount_ = 0;
    mcAllDirty_ = false;
    builtOnce_ = true;
    lastStats.chunked = chunked_;
    lastStats.remeshMs = remeshMs;
    lastStats.handoffMs = handoffMs;
    lastStats.totalMs = MsSince(tStart);
}

// ===================== Compact density (de)serialization ====================
std::string VoxelTerrain::EncodeDensity() const {
    // Quantize each density to a signed byte over [-clamp, clamp] (only values
    // near the iso surface drive the mesh, so clamping the far field is lossless
    // for the surface). Then base64 the bytes.
    const float clamp = Clamp() + 1e-3f;
    std::vector<unsigned char> bytes; bytes.reserve(density.size());
    for (float d : density) {
        float v = d / clamp; if (v < -1) v = -1; if (v > 1) v = 1;
        int q = (int)std::lround(v * 127.0f);
        bytes.push_back((unsigned char)(q & 0xFF));
    }
    return base64::Encode(bytes.data(), bytes.size());   // standard alphabet, '=' padded (okay/Core/Base64.hpp)
}

bool VoxelTerrain::DecodeDensity(const std::string& b64) {
    const float clamp = Clamp() + 1e-3f;
    std::vector<unsigned char> bytes;
    base64::DecodeLenient(b64, bytes);   // skips '=', line breaks and anything outside the alphabet, as before
    if (bytes.size() != density.size()) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        signed char q = (signed char)bytes[i];
        density[i] = (q / 127.0f) * clamp;
    }
    mcAllDirty_ = true;
    floatersSwept_ = false;
    editAny_ = false;
    return true;
}

} // namespace okay
