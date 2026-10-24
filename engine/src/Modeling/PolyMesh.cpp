#include "okay/Modeling/PolyMesh.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace okay::modeling {

namespace {
std::pair<int, int> Key(int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }
constexpr float kPi = 3.14159265358979323846f;
} // namespace

bool IsOkayMeshPath(const std::string& path) {
    const std::string ext = ".okaymesh";
    if (path.size() < ext.size()) return false;
    for (std::size_t i = 0; i < ext.size(); ++i)
        if (std::tolower((unsigned char)path[path.size() - ext.size() + i]) != ext[i]) return false;
    return true;
}

std::vector<PolyMesh::Edge> PolyMesh::Edges() const {
    std::map<std::pair<int, int>, Edge> m;
    for (int f = 0; f < (int)faces.size(); ++f) {
        const auto& v = faces[f].verts;
        for (int i = 0; i < (int)v.size(); ++i) {
            const int a = v[i], b = v[(i + 1) % v.size()];
            if (a == b) continue;
            Edge& e = m[Key(a, b)];
            e.a = std::min(a, b); e.b = std::max(a, b);
            if (e.faceCount == 0) e.f0 = f; else if (e.faceCount == 1) e.f1 = f;
            ++e.faceCount;
        }
    }
    std::vector<Edge> out;
    out.reserve(m.size());
    for (auto& kv : m) out.push_back(kv.second);
    return out;
}

PolyMesh::Report PolyMesh::Check() const {
    Report r;
    std::vector<char> used(verts.size(), 0);
    for (const PolyFace& f : faces) {
        bool bad = false;
        for (int v : f.verts) { if (v < 0 || v >= (int)verts.size()) bad = true; else used[v] = 1; }
        if (bad) { ++r.badIndices; continue; }
        // Fewer than three distinct corners, or a corner repeated: not a real face.
        std::vector<int> s = f.verts;
        std::sort(s.begin(), s.end());
        if (f.verts.size() < 3 || std::unique(s.begin(), s.end()) != s.end()) ++r.degenerateFaces;
        else if (!f.uvs.empty() && f.uvs.size() != f.verts.size()) ++r.badIndices;
    }
    for (char u : used) if (!u) ++r.unusedVerts;
    if (r.badIndices) return r;
    for (const Edge& e : Edges()) {
        if (e.faceCount == 1) ++r.boundaryEdges;
        else if (e.faceCount > 2) ++r.nonManifoldEdges;
    }
    return r;
}

int PolyMesh::EulerCharacteristic() const {
    std::vector<char> used(verts.size(), 0);
    for (const PolyFace& f : faces) for (int v : f.verts) if (v >= 0 && v < (int)verts.size()) used[v] = 1;
    int v = 0;
    for (char u : used) v += u;
    return v - (int)Edges().size() + (int)faces.size();
}

int PolyMesh::CountFaces(int corners) const {
    int n = 0;
    for (const PolyFace& f : faces) if (f.Size() == corners) ++n;
    return n;
}

int PolyMesh::CountNgons() const {
    int n = 0;
    for (const PolyFace& f : faces) if (f.Size() > 4) ++n;
    return n;
}

Vec3 PolyMesh::FaceNormal(int face) const {
    Vec3 n{0, 0, 0};
    if (face < 0 || face >= (int)faces.size()) return n;
    const auto& v = faces[face].verts;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Vec3& a = verts[v[i]];
        const Vec3& b = verts[v[(i + 1) % v.size()]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const float m = n.Magnitude();
    return m > 1e-12f ? n * (1.0f / m) : Vec3{0, 0, 0};
}

std::vector<int> PolyMesh::TriangulateFace(int face) const {
    std::vector<int> tris;
    if (face < 0 || face >= (int)faces.size()) return tris;
    const int n = faces[face].Size();
    if (n < 3) return tris;
    if (n == 3) return {0, 1, 2};
    // Project onto the face plane and clip ears (works for concave faces too).
    const Vec3 N = FaceNormal(face);
    Vec3 u = std::fabs(N.x) < 0.9f ? Vec3::Cross(N, Vec3{1, 0, 0}) : Vec3::Cross(N, Vec3{0, 1, 0});
    u = u.Normalized();
    const Vec3 w = Vec3::Cross(N, u);
    std::vector<Vec2> p(n);
    for (int i = 0; i < n; ++i) {
        const Vec3& q = verts[faces[face].verts[i]];
        p[i] = Vec2{Vec3::Dot(q, u), Vec3::Dot(q, w)};
    }
    auto cross = [&](int a, int b, int c) {
        return (p[b].x - p[a].x) * (p[c].y - p[a].y) - (p[b].y - p[a].y) * (p[c].x - p[a].x);
    };
    auto inside = [&](int a, int b, int c, int x) {
        const float d1 = cross(a, b, x), d2 = cross(b, c, x), d3 = cross(c, a, x);
        return d1 >= -1e-9f && d2 >= -1e-9f && d3 >= -1e-9f;
    };
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = i;
    int guard = 0;
    while (idx.size() > 3 && guard++ < n * n) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (cross(a, b, c) <= 1e-12f) continue;   // reflex (or flat) corner: not an ear
            bool blocked = false;
            for (int x : idx) if (x != a && x != b && x != c && inside(a, b, c, x)) { blocked = true; break; }
            if (blocked) continue;
            tris.insert(tris.end(), {a, b, c});
            idx.erase(idx.begin() + (long)i);
            clipped = true;
            break;
        }
        if (!clipped) break;   // self-intersecting or degenerate: fan the rest
    }
    if (idx.size() == 3) tris.insert(tris.end(), {idx[0], idx[1], idx[2]});
    else for (std::size_t i = 1; i + 1 < idx.size(); ++i) tris.insert(tris.end(), {idx[0], idx[i], idx[i + 1]});
    return tris;
}

PolyMesh PolyMesh::FromMesh(const Mesh& mesh, const FromMeshOptions& opt) {
    PolyMesh pm;
    const int nv = (int)mesh.vertices.size();
    const bool hasUV = mesh.uvs.size() == mesh.vertices.size() && !mesh.uvs.empty();
    const bool hasCol = mesh.HasFaceColors();
    // Weld by position: corners keep their own UVs, so seams survive.
    std::vector<int> remap(nv);
    if (opt.weld) {
        std::map<std::tuple<long, long, long>, int> ids;
        for (int i = 0; i < nv; ++i) {
            const Vec3& v = mesh.vertices[i];
            auto key = std::make_tuple(std::lround(v.x * 1e5f), std::lround(v.y * 1e5f), std::lround(v.z * 1e5f));
            auto it = ids.find(key);
            if (it == ids.end()) { it = ids.emplace(key, (int)pm.verts.size()).first; pm.verts.push_back(v); }
            remap[i] = it->second;
        }
    } else {
        pm.verts = mesh.vertices;
        for (int i = 0; i < nv; ++i) remap[i] = i;
    }
    for (int t = 0; t < mesh.TriangleCount(); ++t) {
        PolyFace f;
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            const int src = mesh.triangles[t * 3 + k];
            if (src < 0 || src >= nv) { ok = false; break; }
            f.verts.push_back(remap[src]);
            if (hasUV) f.uvs.push_back(mesh.uvs[src]);
        }
        if (!ok || f.verts[0] == f.verts[1] || f.verts[1] == f.verts[2] || f.verts[0] == f.verts[2]) continue;
        if (hasCol) { f.hasColor = true; f.color = mesh.triColors[t]; }
        pm.faces.push_back(f);
    }
    if (!opt.mergeQuads) return pm;

    // Quads from coplanar triangle pairs: two triangles merge across an edge
    // that is the longest edge of both (the diagonal a triangulated quad was
    // split along), when they're flat together, look the same and make a
    // convex quad. Greedy and mutual, so a quad grid comes back exactly.
    const float cosTol = std::cos(opt.planarDegrees * kPi / 180.0f);
    const int nf = (int)pm.faces.size();
    std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
    for (int f = 0; f < nf; ++f)
        for (int i = 0; i < 3; ++i) edgeFaces[Key(pm.faces[f].verts[i], pm.faces[f].verts[(i + 1) % 3])].push_back(f);
    // Corner index i of the edge (i, i+1) that is clearly the longest, or -1 on a
    // tie (a fan's two equal spokes aren't a quad's diagonal).
    auto longest = [&](int f) {
        float l[3];
        for (int i = 0; i < 3; ++i) l[i] = (pm.verts[pm.faces[f].verts[i]] - pm.verts[pm.faces[f].verts[(i + 1) % 3]]).SqrMagnitude();
        const int best = l[0] >= l[1] ? (l[0] >= l[2] ? 0 : 2) : (l[1] >= l[2] ? 1 : 2);
        for (int i = 0; i < 3; ++i) if (i != best && l[i] >= l[best] * 0.999f) return -1;
        return best;
    };
    auto sameLook = [&](const PolyFace& a, const PolyFace& b) {
        if (a.material != b.material || a.hasColor != b.hasColor) return false;
        if (!a.hasColor) return true;
        return std::fabs(a.color.r - b.color.r) < 1e-4f && std::fabs(a.color.g - b.color.g) < 1e-4f &&
               std::fabs(a.color.b - b.color.b) < 1e-4f && std::fabs(a.color.a - b.color.a) < 1e-4f;
    };
    std::vector<char> merged(nf, 0);
    std::vector<PolyFace> out;
    out.reserve(nf);
    for (int f = 0; f < nf; ++f) {
        if (merged[f]) continue;
        const int i = longest(f);
        const PolyFace& A = pm.faces[f];
        if (i < 0) { out.push_back(A); merged[f] = 1; continue; }
        const int x = A.verts[i], y = A.verts[(i + 1) % 3], z = A.verts[(i + 2) % 3];
        int partner = -1;
        auto it = edgeFaces.find(Key(x, y));
        if (it != edgeFaces.end() && it->second.size() == 2) {
            const int g = it->second[0] == f ? it->second[1] : it->second[0];
            if (!merged[g] && g != f) {
                const PolyFace& B = pm.faces[g];
                const int j = longest(g);
                // B must share the same edge as its own longest, running the other way (consistent winding).
                if (j >= 0 && B.verts[j] == y && B.verts[(j + 1) % 3] == x && sameLook(A, B) &&
                    Vec3::Dot(pm.FaceNormal(f), pm.FaceNormal(g)) >= cosTol)
                    partner = g;
            }
        }
        if (partner < 0) { out.push_back(A); merged[f] = 1; continue; }
        const PolyFace& B = pm.faces[partner];
        const int j = longest(partner);
        const int o = B.verts[(j + 2) % 3];   // B's corner opposite the shared edge
        PolyFace q;
        q.verts = {x, o, y, z};
        if (!A.uvs.empty() && !B.uvs.empty()) {
            // The shared corners must agree on UV, or the pair straddles a seam: keep them apart.
            const Vec2 ax = A.uvs[i], ay = A.uvs[(i + 1) % 3], bx = B.uvs[(j + 1) % 3], by = B.uvs[j];
            if (std::fabs(ax.x - bx.x) > 1e-5f || std::fabs(ax.y - bx.y) > 1e-5f ||
                std::fabs(ay.x - by.x) > 1e-5f || std::fabs(ay.y - by.y) > 1e-5f) {
                out.push_back(A); merged[f] = 1; continue;
            }
            q.uvs = {A.uvs[i], B.uvs[(j + 2) % 3], A.uvs[(i + 1) % 3], A.uvs[(i + 2) % 3]};
        }
        q.material = A.material; q.hasColor = A.hasColor; q.color = A.color;
        // Convex? Every corner turns the same way as the face normal.
        const Vec3 N = pm.FaceNormal(f);
        bool convex = true;
        for (int k = 0; k < 4 && convex; ++k) {
            const Vec3& p0 = pm.verts[q.verts[k]];
            const Vec3& p1 = pm.verts[q.verts[(k + 1) % 4]];
            const Vec3& p2 = pm.verts[q.verts[(k + 2) % 4]];
            if (Vec3::Dot(Vec3::Cross(p1 - p0, p2 - p1), N) <= 1e-9f) convex = false;
        }
        if (!convex) { out.push_back(A); merged[f] = 1; continue; }
        out.push_back(q);
        merged[f] = merged[partner] = 1;
    }
    pm.faces = std::move(out);
    return pm;
}

Mesh PolyMesh::ToMesh(std::vector<int>* triFace) const {
    Mesh m;
    bool anyUV = false, anyColor = false;
    for (const PolyFace& f : faces) {
        if (!f.uvs.empty() && f.uvs.size() == f.verts.size()) anyUV = true;
        if (f.hasColor) anyColor = true;
    }
    // One mesh vertex per (polygon vertex, UV): shared unless a seam splits it.
    std::map<std::tuple<int, long, long>, int> slot;
    auto vertexFor = [&](int pv, const Vec2& uv) {
        const auto key = anyUV ? std::make_tuple(pv, std::lround(uv.x * 1e6f), std::lround(uv.y * 1e6f)) : std::make_tuple(pv, 0L, 0L);
        auto it = slot.find(key);
        if (it != slot.end()) return it->second;
        const int id = (int)m.vertices.size();
        m.vertices.push_back(verts[pv]);
        if (anyUV) m.uvs.push_back(uv);
        slot.emplace(key, id);
        return id;
    };
    // Mesh vertex i is polygon vertex i (with the UV of its first corner), so a
    // welded mesh keeps its vertex numbering through FromMesh / ToMesh; extra
    // copies split off by UV seams come after.
    {
        std::vector<char> seen(verts.size(), 0);
        std::vector<Vec2> firstUV(verts.size(), Vec2{0, 0});
        for (const PolyFace& f : faces)
            for (std::size_t c = 0; c < f.verts.size(); ++c) {
                const int v = f.verts[c];
                if (v < 0 || v >= (int)verts.size() || seen[v]) continue;
                seen[v] = 1;
                if (f.uvs.size() == f.verts.size()) firstUV[v] = f.uvs[c];
            }
        for (int v = 0; v < (int)verts.size(); ++v) vertexFor(v, firstUV[v]);
    }
    if (triFace) triFace->clear();
    for (int fi = 0; fi < (int)faces.size(); ++fi) {
        const PolyFace& f = faces[fi];
        bool ok = f.verts.size() >= 3;
        for (int v : f.verts) if (v < 0 || v >= (int)verts.size()) ok = false;
        if (!ok) continue;
        const bool fUV = f.uvs.size() == f.verts.size();
        const std::vector<int> tri = TriangulateFace(fi);
        for (std::size_t k = 0; k + 2 < tri.size(); k += 3) {
            for (int c = 0; c < 3; ++c) {
                const int corner = tri[k + c];
                m.triangles.push_back(vertexFor(f.verts[corner], fUV ? f.uvs[corner] : Vec2{0, 0}));
            }
            if (anyColor) m.triColors.push_back(f.hasColor ? f.color : Color{1, 1, 1, 1});
            if (triFace) triFace->push_back(fi);
        }
    }
    return m;
}

std::string PolyMesh::Serialize() const {
    std::ostringstream o;
    o.precision(9);
    o << "okaymesh 1\n";
    o << "verts " << verts.size() << "\n";
    for (const Vec3& v : verts) o << "v " << v.x << " " << v.y << " " << v.z << "\n";
    o << "faces " << faces.size() << "\n";
    for (const PolyFace& f : faces) {
        o << "f " << f.verts.size();
        for (int v : f.verts) o << " " << v;
        if (f.uvs.size() == f.verts.size() && !f.uvs.empty()) {
            o << " uv";
            for (const Vec2& t : f.uvs) o << " " << t.x << " " << t.y;
        }
        if (f.hasColor) o << " color " << f.color.r << " " << f.color.g << " " << f.color.b << " " << f.color.a;
        if (f.material != 0) o << " mat " << f.material;
        o << "\n";
    }
    return o.str();
}

bool PolyMesh::Parse(const std::string& text, PolyMesh& out, std::string* error) {
    auto fail = [&](int line, const std::string& why) {
        if (error) *error = "line " + std::to_string(line) + ": " + why;
        return false;
    };
    PolyMesh pm;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    std::size_t wantVerts = 0, wantFaces = 0;
    bool header = false;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag[0] == '#') continue;
        if (!header) {
            int version = 0;
            if (tag != "okaymesh" || !(ls >> version)) return fail(lineNo, "not a .okaymesh file (it should start with \"okaymesh 1\")");
            if (version != 1) return fail(lineNo, "version " + std::to_string(version) + " is newer than this editor reads (1)");
            header = true;
            continue;
        }
        if (tag == "verts") { ls >> wantVerts; pm.verts.reserve(wantVerts); }
        else if (tag == "faces") { ls >> wantFaces; pm.faces.reserve(wantFaces); }
        else if (tag == "v") {
            Vec3 v;
            if (!(ls >> v.x >> v.y >> v.z)) return fail(lineNo, "a vertex needs x y z");
            pm.verts.push_back(v);
        } else if (tag == "f") {
            PolyFace f;
            int n = 0;
            if (!(ls >> n) || n < 3) return fail(lineNo, "a face needs a corner count of at least 3");
            for (int i = 0; i < n; ++i) {
                int v = -1;
                if (!(ls >> v)) return fail(lineNo, "the face lists fewer corners than it says");
                f.verts.push_back(v);
            }
            std::string key;
            while (ls >> key) {
                if (key == "uv") {
                    for (int i = 0; i < n; ++i) {
                        Vec2 t;
                        if (!(ls >> t.x >> t.y)) return fail(lineNo, "uv needs two numbers per corner");
                        f.uvs.push_back(t);
                    }
                } else if (key == "color") {
                    if (!(ls >> f.color.r >> f.color.g >> f.color.b >> f.color.a)) return fail(lineNo, "color needs r g b a");
                    f.hasColor = true;
                } else if (key == "mat") {
                    if (!(ls >> f.material)) return fail(lineNo, "mat needs a number");
                } else return fail(lineNo, "unknown face field '" + key + "'");
            }
            pm.faces.push_back(f);
        } else return fail(lineNo, "unknown record '" + tag + "'");
    }
    if (!header) return fail(lineNo, "empty file");
    if (pm.verts.size() != wantVerts) return fail(lineNo, "expected " + std::to_string(wantVerts) + " vertices, found " + std::to_string(pm.verts.size()));
    if (pm.faces.size() != wantFaces) return fail(lineNo, "expected " + std::to_string(wantFaces) + " faces, found " + std::to_string(pm.faces.size()));
    for (std::size_t i = 0; i < pm.faces.size(); ++i)
        for (int v : pm.faces[i].verts)
            if (v < 0 || v >= (int)pm.verts.size())
                return fail(lineNo, "face " + std::to_string(i) + " uses vertex " + std::to_string(v) + ", which doesn't exist");
    out = std::move(pm);
    return true;
}

bool PolyMesh::Save(const std::string& path, std::string* error) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { if (error) *error = "can't write " + path; return false; }
    f << Serialize();
    if (!f.good()) { if (error) *error = "writing " + path + " failed"; return false; }
    return true;
}

bool PolyMesh::Load(const std::string& path, PolyMesh& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "can't open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    if (!Parse(ss.str(), out, &err)) { if (error) *error = path + ": " + err; return false; }
    return true;
}

} // namespace okay::modeling
