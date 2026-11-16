// Materials per voxel, volume-exact digging and building, slumping, loose pieces and stroke undo for VoxelTerrain.
// Written for this engine from general ideas (a signed-distance field's solid fraction, flood fill, angle of repose);
// no outside code was used. See docs/terrain_water.md.
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/MeshRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_set>

namespace okay {

namespace {
inline float ClampF(float v, float c) { return v < -c ? -c : (v > c ? c : v); }
inline int FloorQ(float v) {
    if (!(v > -1.0e9f)) return -1000000000;
    if (v > 1.0e9f) return 1000000000;
    const int i = (int)v; return v < (float)i ? i - 1 : i;
}
inline int CeilQ(float v) {
    if (!(v > -1.0e9f)) return -1000000000;
    if (v > 1.0e9f) return 1000000000;
    const int i = (int)v; return v > (float)i ? i + 1 : i;
}
// Fraction of a voxel that lies under the surface for a signed distance d (world units, positive inside).
inline double Cover(float d, float vs) {
    const double c = 0.5 + (double)d / (double)vs;
    return c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
}
float Hash01(int a, int b, int c, unsigned seed) {
    unsigned h = seed * 747796405u + 2891336453u;
    h ^= (unsigned)a * 0x85EBCA6Bu; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
    h ^= (unsigned)b * 0x27D4EB2Fu; h = (h ^ (h >> 16)) * 0x165667B1u;
    h ^= (unsigned)c * 0x9E3779B1u; h ^= h >> 15;
    return (h & 0xFFFFu) / 65535.0f;
}
}

std::vector<VoxelTerrain::VoxelMaterial> VoxelTerrain::DefaultMaterials() {
    std::vector<VoxelMaterial> t(7);
    auto set = [&](int i, const char* n, Color c, float hard, float dens, bool loose, const char* drop, float per, Color part) {
        t[(std::size_t)i].name = n; t[(std::size_t)i].color = c; t[(std::size_t)i].hardness = hard;
        t[(std::size_t)i].density = dens; t[(std::size_t)i].loose = loose; t[(std::size_t)i].drop = drop;
        t[(std::size_t)i].dropPerM3 = per; t[(std::size_t)i].particle = part;
    };
    set(0, "Dirt",  Color::FromBytes(112, 86, 60),   1.0f, 1500.0f, true,  "dirt",  8.0f,  Color::FromBytes(130, 104, 78));
    set(1, "Grass", Color::FromBytes(96, 150, 80),   1.0f, 1300.0f, true,  "dirt",  8.0f,  Color::FromBytes(96, 150, 80));
    set(2, "Sand",  Color::FromBytes(205, 195, 145), 0.6f, 1600.0f, true,  "sand",  8.0f,  Color::FromBytes(205, 195, 145));
    set(3, "Clay",  Color::FromBytes(160, 110, 90),  1.6f, 1800.0f, false, "clay",  8.0f,  Color::FromBytes(160, 110, 90));
    set(4, "Stone", Color::FromBytes(122, 112, 102), 4.0f, 2600.0f, false, "stone", 6.0f,  Color::FromBytes(140, 140, 140));
    set(5, "Ore",   Color::FromBytes(180, 150, 70),  6.0f, 3200.0f, false, "ore",   4.0f,  Color::FromBytes(230, 190, 70));
    set(6, "Snow",  Color::FromBytes(236, 240, 248), 0.5f, 400.0f,  true,  "snow",  10.0f, Color::FromBytes(236, 240, 248));
    return t;
}

void VoxelTerrain::EnableMaterials(int fillId) {
    if (materials.empty()) materials = DefaultMaterials();
    if (fillId < 0 || fillId >= (int)materials.size()) fillId = 0;
    material.assign(density.size(), (unsigned char)fillId);
    ++matRev_; mcAllDirty_ = true;
}

int VoxelTerrain::MaterialIndex(const std::string& name) const {
    for (std::size_t i = 0; i < materials.size(); ++i) if (materials[i].name == name) return (int)i;
    return -1;
}

double VoxelTerrain::SolidVolume() const {
    double v = 0.0;
    for (float d : density) v += Cover(d, voxelSize);
    return v * (double)voxelSize * voxelSize * voxelSize;
}

void VoxelTerrain::GenerateMaterials(unsigned seed, const LayerRules& r) {
    if (materials.empty()) materials = DefaultMaterials();
    const int stoneId = std::max(0, MaterialIndex("Stone"));
    material.assign(density.size(), (unsigned char)stoneId);
    const int dirt = std::max(0, MaterialIndex("Dirt")), grass = std::max(0, MaterialIndex("Grass")), sand = std::max(0, MaterialIndex("Sand")),
              clay = std::max(0, MaterialIndex("Clay")), stone = std::max(0, MaterialIndex("Stone")), ore = std::max(0, MaterialIndex("Ore")),
              snow = std::max(0, MaterialIndex("Snow"));
    const float sandBelow = r.sandBelow >= 0.0f ? r.sandBelow : SizeY() * 0.12f;
    const float snowAbove = r.snowAbove >= 0.0f ? r.snowAbove : SizeY() * 0.85f;
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            int top = -1;
            for (int y = ny - 1; y >= 0; --y) if (density[Index(x, y, z)] > iso) { top = y; break; }
            if (top < 0) continue;
            // surface steepness from the horizontal density gradient at the top voxel
            const float gx = Get(x + 1, top, z) - Get(x - 1, top, z), gz = Get(x, top, z + 1) - Get(x, top, z - 1);
            const float gy = Get(x, top + 1, z) - Get(x, top - 1, z);
            const float len = std::sqrt(gx * gx + gy * gy + gz * gz);
            const float slope = len > 1e-6f ? 1.0f - std::fabs(gy) / len : 0.0f;
            for (int y = top; y >= 0; --y) {
                const int idx = Index(x, y, z);
                if (density[idx] <= iso) continue;
                const float depth = (top - y) * voxelSize;
                int id;
                if (depth < r.topsoil) id = top * voxelSize >= snowAbove ? snow : (top * voxelSize <= sandBelow ? sand : (slope >= r.stoneSlope ? stone : grass));
                else if (depth < r.dirtDepth && slope < r.stoneSlope) id = top * voxelSize <= sandBelow ? sand : dirt;
                else id = stone;
                const float n = Hash01(x / 3, y / 3, z / 3, seed);       // blobs a few voxels wide
                if (id == dirt && n < r.clayAmount) id = clay;
                else if (id == stone && Hash01(x / 2, y / 2, z / 2, seed ^ 0x77u) < r.oreAmount) id = ore;
                material[idx] = (unsigned char)id;
            }
        }
    ++matRev_; mcAllDirty_ = true;
}

void VoxelTerrain::LogStroke(int idx) {
    if (!strokeOpen_) return;
    if (strokeSeen_.size() != density.size()) strokeSeen_.assign(density.size(), 0);
    if (strokeSeen_[(std::size_t)idx]) return;
    strokeSeen_[(std::size_t)idx] = 1;
    strokeLog_.push_back({idx, density[(std::size_t)idx], material.empty() ? (unsigned char)0 : material[(std::size_t)idx]});
}

void VoxelTerrain::BeginStroke() {
    strokeOpen_ = true; strokeLog_.clear(); strokeSeen_.clear();
}

bool VoxelTerrain::UndoStroke() {
    if (strokeLog_.empty()) return false;
    strokeOpen_ = false;
    for (auto it = strokeLog_.rbegin(); it != strokeLog_.rend(); ++it) {
        density[(std::size_t)it->idx] = it->density;
        if (!material.empty()) material[(std::size_t)it->idx] = it->material;
        const int x = it->idx % nx, y = (it->idx / nx) % ny, z = it->idx / (nx * ny);
        TouchCell(x, y, z);
    }
    strokeLog_.clear(); strokeSeen_.clear();
    return true;
}

VoxelTerrain::EditResult VoxelTerrain::Edit(const Vec3& localIn, float radius, float amount, const EditOptions& o) {
    EditResult res;
    res.removedBy.assign(std::max<std::size_t>(1, materials.size()), 0.0);
    if (!(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(amount) || !std::isfinite(localIn.x) ||
        !std::isfinite(localIn.y) || !std::isfinite(localIn.z)) return res;
    float gx = (localIn.x + HalfX()) / voxelSize, gy = localIn.y / voxelSize, gz = (localIn.z + HalfZ()) / voxelSize;
    if (o.snap) { gx = std::floor(gx + 0.5f); gy = std::floor(gy + 0.5f); gz = std::floor(gz + 0.5f); }
    const float gr = radius / voxelSize;
    const int x0 = std::max(0, FloorQ(gx - gr)), x1 = std::min(nx - 1, CeilQ(gx + gr));
    const int y0 = std::max(0, FloorQ(gy - gr)), y1 = std::min(ny - 1, CeilQ(gy + gr));
    const int z0 = std::max(0, FloorQ(gz - gr)), z1 = std::min(nz - 1, CeilQ(gz + gr));
    const double vs3 = (double)voxelSize * voxelSize * voxelSize;
    const bool dig = amount > 0.0f, mats = UseMaterials();
    int cx0 = nx, cy0 = ny, cz0 = nz, cx1 = -1, cy1 = -1, cz1 = -1;
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                if (floorLayers > 0 && dig && y < floorLayers) continue;
                if (dig && o.protectBelowY >= 0.0f && y * voxelSize < o.protectBelowY) continue;
                if (dig && o.maxDepth > 0.0f && y * voxelSize < o.depthOriginY - o.maxDepth) continue;
                const float dx = x - gx, dy = y - gy, dz = z - gz;
                float d;
                switch (o.shape) {
                    case EditOptions::Shape::Cube:     d = std::max(std::fabs(dx), std::max(std::fabs(dy), std::fabs(dz))); break;
                    case EditOptions::Shape::Cylinder: d = std::max(std::sqrt(dx * dx + dz * dz), std::fabs(dy)); break;
                    case EditOptions::Shape::Plane:    d = std::max(std::sqrt(dx * dx + dz * dz), std::fabs(dy) * 3.0f); break;
                    default:                           d = std::sqrt(dx * dx + dy * dy + dz * dz); break;
                }
                if (d > gr) continue;
                const float t = 1.0f - d / gr;
                const float fall = t * t * (3.0f - 2.0f * t);
                const int idx = Index(x, y, z);
                float& cell = density[(std::size_t)idx];
                const int mid = mats ? (int)material[(std::size_t)idx] : 0;
                float eff = amount * fall;
                if (dig && cell > iso) {                         // only solid ground resists
                    const float hard = mats && mid < (int)materials.size() ? materials[(std::size_t)mid].hardness : 1.0f;
                    if (o.power < hard) continue;                // the tool is too weak for this material
                    eff /= std::max(0.05f, hard * o.hardnessScale);
                }
                const float nv = ClampF(cell - eff, Clamp());
                if (nv == cell) continue;
                LogStroke(idx);
                const double dv = (Cover(nv, voxelSize) - Cover(cell, voxelSize)) * vs3;
                if (dv < 0.0) { res.removed += -dv; res.removedBy[(std::size_t)std::min<int>(mid, (int)res.removedBy.size() - 1)] += -dv; }
                else res.added += dv;
                if (!dig && mats && o.addMaterial >= 0 && o.addMaterial < (int)materials.size() && cell <= iso && nv > cell)
                    material[(std::size_t)idx] = (unsigned char)o.addMaterial;
                cell = nv; res.changed = true;
                if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
                if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
                if (z < cz0) cz0 = z; if (z > cz1) cz1 = z;
            }
    if (res.changed) TouchBox(cx0, cy0, cz0, cx1, cy1, cz1);
    return res;
}

double VoxelTerrain::AddVolume(const Vec3& local, double volume, int materialId, float radius) {
    // Build up in steps. Air is stored far below the surface value, so the first strokes only raise the density and add no
    // volume yet; each step is therefore a large one, and the last one is scaled down to what is still missing.
    EditOptions o; o.addMaterial = materialId;
    double got = 0.0;
    const float step = Clamp() * 0.5f;
    int stall = 0;
    for (int i = 0; i < 160 && got < volume; ++i) {
        const double missing = volume - got;
        const float amt = (float)std::max(0.02, std::min(1.0, missing / std::max(volume, 1e-9))) * step;
        const EditResult r = Edit(local, radius, -amt, o);
        got += r.added;
        // the edge of a soft brush only creeps: when a stroke stops giving much, reach a little wider
        if (!r.changed || r.added < 0.02 * missing) {
            if (!r.changed || ++stall >= 4) { stall = 0; radius *= 1.2f; if (radius > 6.0f * voxelSize + 6.0f) break; }
        } else stall = 0;
    }
    return got;
}

int VoxelTerrain::Slump(int maxMoves) {
    if (!UseMaterials() || !editAny_ || maxMoves <= 0) return 0;
    const int x0 = std::max(1, editMin_[0] - 1), x1 = std::min(nx - 2, editMax_[0] + 1);
    const int y0 = std::max(std::max(1, floorLayers), editMin_[1] - 1), y1 = std::min(ny - 2, editMax_[1] + 2);
    const int z0 = std::max(1, editMin_[2] - 1), z1 = std::min(nz - 2, editMax_[2] + 1);
    int moved = 0;
    const float air = iso - Clamp(), solid = Clamp();
    auto isAir = [&](int x, int y, int z) { return density[(std::size_t)Index(x, y, z)] <= iso; };
    auto looseAt = [&](int idx) { const int m = material[(std::size_t)idx]; return m < (int)materials.size() && materials[(std::size_t)m].loose; };
    // bottom up, so a falling column settles in one pass
    for (int y = y0; y <= y1 && moved < maxMoves; ++y)
        for (int z = z0; z <= z1 && moved < maxMoves; ++z)
            for (int x = x0; x <= x1 && moved < maxMoves; ++x) {
                const int idx = Index(x, y, z);
                if (density[(std::size_t)idx] <= iso || !looseAt(idx)) continue;
                int tx = x, ty = y - 1, tz = z;
                if (!isAir(x, y - 1, z)) {
                    // resting on something: slide only if a diagonal-below cell is open and its side neighbour too
                    static const int dxs[4] = {1, -1, 0, 0}, dzs[4] = {0, 0, 1, -1};
                    bool slid = false;
                    for (int k = 0; k < 4 && !slid; ++k) {
                        const int ax = x + dxs[k], az = z + dzs[k];
                        if (ax < 1 || ax > nx - 2 || az < 1 || az > nz - 2) continue;
                        if (isAir(ax, y, az) && isAir(ax, y - 1, az)) { tx = ax; ty = y - 1; tz = az; slid = true; }
                    }
                    if (!slid) continue;
                }
                const int tidx = Index(tx, ty, tz);
                LogStroke(idx); LogStroke(tidx);
                // move the voxel (its material and the full solid value); the cell it leaves becomes air: volume is kept
                material[(std::size_t)tidx] = material[(std::size_t)idx];
                density[(std::size_t)tidx] = std::max(density[(std::size_t)idx], solid * 0.5f);
                density[(std::size_t)idx] = air;
                TouchCell(x, y, z); TouchCell(tx, ty, tz);
                ++moved;
            }
    return moved;
}

std::vector<VoxelTerrain::Piece> VoxelTerrain::ExtractFloaters(int maxPieces) {
    std::vector<Piece> out;
    if (!editAny_) return out;
    // flood from solid cells near the edit box; a component that never reaches the floor rows is a loose piece
    const int lim[3] = {nx - 1, ny - 1, nz - 1};
    int lo[3], hi[3];
    for (int a = 0; a < 3; ++a) { lo[a] = std::max(0, editMin_[a] - 1); hi[a] = std::min(lim[a], editMax_[a] + 1); }
    std::unordered_set<int> seen, ground;        // ground: cells known to be tied to the floor
    const int cap = 200000;                     // a piece larger than this is treated as attached (it is not worth a body)
    for (int z = lo[2]; z <= hi[2]; ++z)
        for (int y = lo[1]; y <= hi[1]; ++y)
            for (int x = lo[0]; x <= hi[0]; ++x) {
                const int s = Index(x, y, z);
                if (density[(std::size_t)s] <= iso || seen.count(s)) continue;
                std::vector<int> comp, stack{s};
                seen.insert(s);
                bool grounded = false;
                while (!stack.empty() && !grounded) {
                    const int idx = stack.back(); stack.pop_back();
                    comp.push_back(idx);
                    const int cx = idx % nx, cy = (idx / nx) % ny, cz = idx / (nx * ny);
                    if (cy < std::max(1, floorLayers) || cx == 0 || cz == 0 || cx == nx - 1 || cz == nz - 1 || (int)comp.size() > cap) { grounded = true; break; }
                    const int d[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
                    for (auto& dd : d) {
                        const int ax = cx + dd[0], ay = cy + dd[1], az = cz + dd[2];
                        if (!InBounds(ax, ay, az)) continue;
                        const int ni = Index(ax, ay, az);
                        if (density[(std::size_t)ni] <= iso) continue;
                        if (ground.count(ni)) { grounded = true; break; }
                        if (seen.count(ni)) continue;
                        seen.insert(ni); stack.push_back(ni);
                    }
                }
                if (grounded) {                      // everything this walk met (and queued) is tied to the floor
                    for (int i : comp) ground.insert(i);
                    for (int i : stack) ground.insert(i);
                    continue;
                }
                if ((int)out.size() >= maxPieces) continue;
                Piece p; p.cells = comp;
                p.dens.reserve(comp.size());
                for (int idx : comp) p.dens.push_back(density[(std::size_t)idx]);
                std::map<int, int> count;
                p.lo = {1e30f, 1e30f, 1e30f}; p.hi = {-1e30f, -1e30f, -1e30f};
                for (int idx : comp) {
                    const int cx = idx % nx, cy = (idx / nx) % ny, cz = idx / (nx * ny);
                    p.volume += Cover(density[(std::size_t)idx], voxelSize);
                    if (UseMaterials()) ++count[material[(std::size_t)idx]];
                    const Vec3 w = GridToLocal(cx, cy, cz);
                    p.lo = {std::min(p.lo.x, w.x), std::min(p.lo.y, w.y), std::min(p.lo.z, w.z)};
                    p.hi = {std::max(p.hi.x, w.x), std::max(p.hi.y, w.y), std::max(p.hi.z, w.z)};
                }
                p.volume *= (double)voxelSize * voxelSize * voxelSize;
                int best = 0, bc = -1; for (auto& kv : count) if (kv.second > bc) { bc = kv.second; best = kv.first; }
                p.material = best;
                out.push_back(std::move(p));
            }
    const float air = iso - Clamp();
    for (const Piece& p : out)
        for (int idx : p.cells) {
            LogStroke(idx);
            density[(std::size_t)idx] = air;
            TouchCell(idx % nx, (idx / nx) % ny, idx / (nx * ny));
        }
    return out;
}

Mesh VoxelTerrain::PieceMesh(const Piece& p) const {
    // a small field holding only the piece (with a margin so its surface closes), meshed like the terrain
    VoxelTerrain t;
    int lo[3] = {nx, ny, nz}, hi[3] = {-1, -1, -1};
    for (int idx : p.cells) {
        const int c[3] = {idx % nx, (idx / nx) % ny, idx / (nx * ny)};
        for (int a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], c[a]); hi[a] = std::max(hi[a], c[a]); }
    }
    Mesh empty; empty.name = "VoxelPiece";
    if (hi[0] < 0) return empty;
    t.Resize(hi[0] - lo[0] + 3, hi[1] - lo[1] + 3, hi[2] - lo[2] + 3);
    t.voxelSize = voxelSize; t.iso = iso; t.autoColor = false; t.floorLayers = 0;
    for (std::size_t k = 0; k < p.cells.size(); ++k) {
        const int idx = p.cells[k];
        const int x = idx % nx - lo[0] + 1, y = (idx / nx) % ny - lo[1] + 1, z = idx / (nx * ny) - lo[2] + 1;
        t.density[(std::size_t)t.Index(x, y, z)] = k < p.dens.size() ? p.dens[k] : density[(std::size_t)idx];
    }
    Mesh m = t.BuildMesh();
    m.name = "VoxelPiece";
    m.ComputeSmoothNormals();
    // shift to a mesh centred on the piece's box centre (the body's origin), like any prop
    const Vec3 centre{(p.lo.x + p.hi.x) * 0.5f, (p.lo.y + p.hi.y) * 0.5f, (p.lo.z + p.hi.z) * 0.5f};
    const Vec3 origin = GridToLocal(lo[0] - 1, lo[1] - 1, lo[2] - 1);
    for (Vec3& v : m.vertices) v = v + origin - centre;
    if (UseMaterials() && (int)p.material < (int)materials.size()) m.triColors.assign((std::size_t)m.TriangleCount(), materials[(std::size_t)p.material].color);
    return m;
}

std::string VoxelTerrain::EncodeMaterials() const {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((material.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < material.size(); i += 3) {
        const unsigned b0 = material[i], b1 = i + 1 < material.size() ? material[i + 1] : 0, b2 = i + 2 < material.size() ? material[i + 2] : 0;
        const unsigned n = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(b64[(n >> 18) & 63]); out.push_back(b64[(n >> 12) & 63]);
        out.push_back(i + 1 < material.size() ? b64[(n >> 6) & 63] : '='); out.push_back(i + 2 < material.size() ? b64[n & 63] : '=');
    }
    return out;
}

bool VoxelTerrain::DecodeMaterials(const std::string& s) {
    std::vector<unsigned char> bytes; unsigned acc = 0; int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52; else if (c == '+') v = 62; else if (c == '/') v = 63; else continue;
        acc = ((acc << 6) | (unsigned)v) & 0xFFFFFFu; bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back((unsigned char)((acc >> bits) & 0xFF)); }
    }
    if (bytes.size() != density.size()) return false;
    material = bytes; ++matRev_; mcAllDirty_ = true;
    return true;
}

} // namespace okay
