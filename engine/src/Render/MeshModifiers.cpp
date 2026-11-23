// Mesh modifiers and editing operations.
// Bodies of okay::Mesh members moved out of engine/include/okay/Render/Mesh.hpp (modularization plan M1); the
// declarations (with their default arguments) stay in the struct. The code is unchanged.
#include "okay/Render/Mesh.hpp"

namespace okay {

    void Mesh::SculptBrush(const Vec3& center, const Vec3& dir, float radius, float strength, int mode, bool symmetryX) {
        if (!(radius > 1e-6f) || vertices.empty()) return;
        if (!std::isfinite(radius + strength + center.x + center.y + center.z + dir.x + dir.y + dir.z)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        const std::size_t n = vertices.size();
        const std::vector<Vec3> snap = vertices;
        // Copies of one point (split for UVs or at a pole) are one group and move together.
        // Copies made with sin / cos can differ in the last bits, so points closer than a
        // tiny tolerance (1e-5 of the mesh size) count as one: a grid of cells that size,
        // each point checked against the groups in its own and the 26 cells around it.
        std::vector<int> group(n);
        std::vector<Vec3> gpos;
        int groups = 0;
        {
            Vec3 lo = snap[0], hi = snap[0];
            for (const Vec3& p : snap) {
                lo.x = std::fmin(lo.x, p.x); lo.y = std::fmin(lo.y, p.y); lo.z = std::fmin(lo.z, p.z);
                hi.x = std::fmax(hi.x, p.x); hi.y = std::fmax(hi.y, p.y); hi.z = std::fmax(hi.z, p.z);
            }
            const float extent = std::fmax(std::fmax(hi.x - lo.x, hi.y - lo.y), std::fmax(hi.z - lo.z, 1e-3f));
            const float tol = extent * 1e-5f, inv = 1.0f / tol;
            if (!std::isfinite(extent)) return;   // a NaN or infinite vertex: leave the mesh alone
            using Cell = std::tuple<long long, long long, long long>;
            std::map<Cell, std::vector<int>> cells;   // cell -> groups whose first point is in it
            for (std::size_t i = 0; i < n; ++i) {
                const Vec3 p = snap[i];
                const long long cx = (long long)std::floor((p.x - lo.x) * inv), cy = (long long)std::floor((p.y - lo.y) * inv),
                                cz = (long long)std::floor((p.z - lo.z) * inv);
                int found = -1;
                for (long long dx = -1; dx <= 1 && found < 0; ++dx)
                    for (long long dy = -1; dy <= 1 && found < 0; ++dy)
                        for (long long dz = -1; dz <= 1 && found < 0; ++dz) {
                            auto it = cells.find(Cell{cx + dx, cy + dy, cz + dz});
                            if (it == cells.end()) continue;
                            for (int g : it->second)
                                if ((gpos[(std::size_t)g] - p).Magnitude() <= tol) { found = g; break; }
                        }
                if (found < 0) { found = groups++; gpos.push_back(p); cells[Cell{cx, cy, cz}].push_back(found); }
                group[i] = found;
            }
        }
        // Area-weighted normal of each group (INFLATE, FLATTEN).
        std::vector<Vec3> gnorm;
        if (mode == 1 || mode == 3) {
            gnorm.assign((std::size_t)groups, Vec3{0, 0, 0});
            for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
                const int a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
                if (a < 0 || b < 0 || c < 0 || a >= (int)n || b >= (int)n || c >= (int)n) continue;
                const Vec3 face = Vec3::Cross(snap[b] - snap[a], snap[c] - snap[a]);
                gnorm[(std::size_t)group[a]] += face; gnorm[(std::size_t)group[b]] += face; gnorm[(std::size_t)group[c]] += face;
            }
            for (Vec3& v : gnorm) if (v.SqrMagnitude() > 1e-20f) v = v.Normalized();
        }
        // Average neighbour position of each group (SMOOTH).
        std::vector<Vec3> gavg;
        if (mode == 2) {
            std::vector<std::vector<int>> nb((std::size_t)groups);
            for (std::size_t t = 0; t + 2 < triangles.size(); t += 3)
                for (int k = 0; k < 3; ++k) {
                    const int a = triangles[t + k], b = triangles[t + (k + 1) % 3];
                    if (a < 0 || b < 0 || a >= (int)n || b >= (int)n) continue;
                    const int ga = group[a], gb = group[b];
                    if (ga == gb) continue;
                    nb[(std::size_t)ga].push_back(gb); nb[(std::size_t)gb].push_back(ga);
                }
            gavg = gpos;
            for (int g = 0; g < groups; ++g) {
                std::vector<int>& l = nb[(std::size_t)g];
                std::sort(l.begin(), l.end());
                l.erase(std::unique(l.begin(), l.end()), l.end());
                if (l.empty()) continue;
                Vec3 sum{0, 0, 0};
                for (int h : l) sum += gpos[(std::size_t)h];
                gavg[(std::size_t)g] = sum * (1.0f / (float)l.size());
            }
        }
        // One stroke's displacement of every group.
        auto stroke = [&](const Vec3& c, const Vec3& d, std::vector<Vec3>& disp) {
            disp.assign((std::size_t)groups, Vec3{0, 0, 0});
            Vec3 planeC{0, 0, 0}, planeN{0, 1, 0};
            if (mode == 3) {   // the best-fit plane of the region: its centroid and averaged normal
                Vec3 nsum{0, 0, 0}; int cnt = 0;
                for (int g = 0; g < groups; ++g)
                    if ((gpos[(std::size_t)g] - c).Magnitude() < radius) { planeC += gpos[(std::size_t)g]; nsum += gnorm[(std::size_t)g]; ++cnt; }
                if (cnt == 0) return;
                planeC = planeC * (1.0f / (float)cnt);
                const float m = nsum.Magnitude();
                if (m > 1e-6f) planeN = nsum * (1.0f / m);
            }
            for (int g = 0; g < groups; ++g) {
                const Vec3 p = gpos[(std::size_t)g];
                const float w = BrushFalloff((p - c).Magnitude(), radius);
                if (w <= 0.0f) continue;
                const float k = std::fmin(strength * w, 1.0f);
                Vec3& out = disp[(std::size_t)g];
                if (mode == 1) out = gnorm[(std::size_t)g] * (strength * w);                    // INFLATE
                else if (mode == 2) out = (gavg[(std::size_t)g] - p) * k;                       // SMOOTH
                else if (mode == 3) out = planeN * (-Vec3::Dot(p - planeC, planeN) * k);       // FLATTEN
                else if (mode == 4) out = (c - p) * k;                                          // PINCH
                else out = d * (strength * w);                                                  // GRAB
            }
        };
        std::vector<Vec3> dispA;
        stroke(center, dir, dispA);
        if (!symmetryX) {
            for (std::size_t i = 0; i < n; ++i) vertices[i] = snap[i] + dispA[(std::size_t)group[i]];
        } else {
            std::vector<Vec3> dispB;
            stroke(Vec3{-center.x, center.y, center.z}, Vec3{-dir.x, dir.y, dir.z}, dispB);
            const bool aIsPlus = center.x >= 0.0f;   // stroke A works on the side its centre is on
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t g = (std::size_t)group[i];
                const float x = snap[i].x;
                Vec3 dv;
                if (x == 0.0f) dv = (dispA[g] + dispB[g]) * 0.5f;
                else dv = ((x > 0.0f) == aIsPlus) ? dispA[g] : dispB[g];
                vertices[i] = snap[i] + dv;
            }
        }
        name = "";
        ComputeSmoothNormals();
    }

    int Mesh::Decimate(float cellSize) {
        AttrGuard attrGuard_(*this);
        if (!(cellSize > 1e-6f) || vertices.empty()) return TriangleCount();
        std::map<std::tuple<int, int, int>, int> cell;
        std::vector<Vec3> sum; std::vector<int> cnt; std::vector<int> remap(vertices.size());
        auto key = [&](const Vec3& v) {
            return std::make_tuple((int)std::floor(v.x / cellSize),
                                   (int)std::floor(v.y / cellSize),
                                   (int)std::floor(v.z / cellSize));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = cell.find(k);
            int id;
            if (it == cell.end()) { id = (int)sum.size(); cell[k] = id; sum.push_back(vertices[i]); cnt.push_back(1); }
            else { id = it->second; sum[id] += vertices[i]; ++cnt[id]; }
            remap[i] = id;
        }
        std::vector<Vec3> nv(sum.size());
        for (std::size_t i = 0; i < sum.size(); ++i) nv[i] = sum[i] * (1.0f / (float)cnt[i]);
        std::vector<int> nt; nt.reserve(triangles.size());
        std::vector<Color> nc; const bool hadColors = HasFaceColors();
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = remap[triangles[t]], b = remap[triangles[t + 1]], c = remap[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;            // collapsed → drop
            nt.push_back(a); nt.push_back(b); nt.push_back(c);
            if (hadColors) nc.push_back(triColors[t / 3]);
        }
        vertices = std::move(nv); triangles = std::move(nt);
        triColors = std::move(nc); uvs.clear();
        name = "";
        ComputeSmoothNormals();
        return TriangleCount();
    }

    bool Mesh::PointInside(const Vec3& p) const {
        const Vec3 d{1.0f, 0.0009124f, 0.0007215f};              // near +X, off every edge
        auto dot = [](const Vec3& u, const Vec3& w) { return u.x * w.x + u.y * w.y + u.z * w.z; };
        int crossings = 0;
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            const Vec3& a = vertices[triangles[i]];
            const Vec3& b = vertices[triangles[i + 1]];
            const Vec3& c = vertices[triangles[i + 2]];
            Vec3 e1 = b - a, e2 = c - a;
            Vec3 h = Vec3::Cross(d, e2);
            float det = dot(e1, h);
            if (det > -1e-9f && det < 1e-9f) continue;           // ray parallel to face
            float invDet = 1.0f / det;
            Vec3 s = p - a;
            float u = dot(s, h) * invDet;
            if (u < 0.0f || u > 1.0f) continue;
            Vec3 q = Vec3::Cross(s, e1);
            float v = dot(d, q) * invDet;
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = dot(e2, q) * invDet;
            if (t > 1e-7f) ++crossings;                          // crossing ahead along the ray
        }
        return (crossings & 1) != 0;
    }

    Mesh Mesh::Boolean(const Mesh& a, const Mesh& b, BoolOp op, float voxel, int maxDim) {
        if (a.vertices.empty() || b.vertices.empty() || voxel <= 1e-5f) return Mesh{};
        Vec3 alo, ahi, blo, bhi; a.Bounds(alo, ahi); b.Bounds(blo, bhi);
        // Union of both AABBs (covers every cell either solid could occupy), padded.
        Vec3 lo{std::min(alo.x, blo.x), std::min(alo.y, blo.y), std::min(alo.z, blo.z)};
        Vec3 hi{std::max(ahi.x, bhi.x), std::max(ahi.y, bhi.y), std::max(ahi.z, bhi.z)};
        lo = lo - Vec3{voxel, voxel, voxel};
        hi = hi + Vec3{voxel, voxel, voxel};
        int nx, ny, nz; float v;
        GridDims(lo, hi, voxel, maxDim, nx, ny, nz, v);
        const std::size_t n = (std::size_t)nx * ny * nz;
        std::vector<unsigned char> oa(n, 0), ob(n, 0);
        SampleInto(a, oa, nx, ny, nz, lo, v);
        SampleInto(b, ob, nx, ny, nz, lo, v);
        std::vector<unsigned char> occ(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            bool ia = oa[i] != 0, ib = ob[i] != 0;
            occ[i] = (op == BoolOp::Union ? (ia || ib)
                    : op == BoolOp::Intersect ? (ia && ib)
                    : (ia && !ib)) ? 1 : 0;                      // Difference
        }
        return EmitOccupancy(occ, nx, ny, nz, lo, v);
    }

    Mesh Mesh::ConvexHull(const std::vector<Vec3>& ptsIn) {
        Mesh out;
        // De-duplicate coincident points.
        std::vector<Vec3> p;
        p.reserve(ptsIn.size());
        for (const Vec3& q : ptsIn) {
            bool dup = false;
            for (const Vec3& e : p) if ((e - q).Magnitude() < 1e-6f) { dup = true; break; }
            if (!dup) p.push_back(q);
        }
        if (p.size() < 4) return out;
        auto dot = [](const Vec3& u, const Vec3& w) { return u.x * w.x + u.y * w.y + u.z * w.z; };

        // Seed tetrahedron: spread-out, non-degenerate 4 points.
        int i0 = 0, i1 = 1;
        float best = -1.0f;
        for (std::size_t i = 0; i < p.size(); ++i)
            for (std::size_t j = i + 1; j < p.size(); ++j) {
                float d = (p[i] - p[j]).Magnitude();
                if (d > best) { best = d; i0 = (int)i; i1 = (int)j; }
            }
        int i2 = -1; best = -1.0f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            Vec3 c = Vec3::Cross(p[i1] - p[i0], p[i] - p[i0]);
            float d = c.Magnitude();
            if (d > best) { best = d; i2 = (int)i; }
        }
        if (i2 < 0 || best < 1e-9f) return out;                 // all collinear
        Vec3 nrm = Vec3::Cross(p[i1] - p[i0], p[i2] - p[i0]);
        int i3 = -1; best = 1e-9f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            float d = std::fabs(dot(nrm, p[i] - p[i0]));
            if (d > best) { best = d; i3 = (int)i; }
        }
        if (i3 < 0) return out;                                  // all coplanar

        Vec3 interior = (p[i0] + p[i1] + p[i2] + p[i3]) * 0.25f; // always inside the hull
        struct Face { int a, b, c; };
        std::vector<Face> faces;
        auto faceNormal = [&](const Face& f) { return Vec3::Cross(p[f.b] - p[f.a], p[f.c] - p[f.a]); };
        auto addFace = [&](int a, int b, int c) {
            Face f{a, b, c};
            // Wind so the normal points away from the interior point.
            if (dot(faceNormal(f), p[a] - interior) < 0.0f) std::swap(f.b, f.c);
            faces.push_back(f);
        };
        addFace(i0, i1, i2); addFace(i0, i1, i3); addFace(i0, i2, i3); addFace(i1, i2, i3);

        for (int q = 0; q < (int)p.size(); ++q) {
            if (q == i0 || q == i1 || q == i2 || q == i3) continue;
            // Faces the new point can "see" (it's outside their plane).
            std::vector<char> vis(faces.size(), 0);
            bool any = false;
            for (std::size_t f = 0; f < faces.size(); ++f) {
                Vec3 n = faceNormal(faces[f]);
                if (dot(n, p[q] - p[faces[f].a]) > 1e-9f) { vis[f] = 1; any = true; }
            }
            if (!any) continue;                                 // inside the current hull
            // Horizon = directed edges of visible faces whose reverse isn't also visible.
            std::map<std::pair<int, int>, int> dir;             // directed edge -> count among visible
            auto add = [&](int a, int b) { dir[{a, b}]++; };
            for (std::size_t f = 0; f < faces.size(); ++f)
                if (vis[f]) { add(faces[f].a, faces[f].b); add(faces[f].b, faces[f].c); add(faces[f].c, faces[f].a); }
            std::vector<std::pair<int, int>> horizon;
            for (auto& kv : dir)
                if (dir.find({kv.first.second, kv.first.first}) == dir.end())
                    horizon.push_back(kv.first);
            // Drop visible faces, add a fan from the horizon to q.
            std::vector<Face> keep;
            for (std::size_t f = 0; f < faces.size(); ++f) if (!vis[f]) keep.push_back(faces[f]);
            faces.swap(keep);
            for (auto& e : horizon) addFace(e.first, e.second, q);
        }

        // Emit unique vertices used by the hull faces.
        std::map<int, int> remap;
        for (const Face& f : faces)
            for (int idx : {f.a, f.b, f.c})
                if (remap.find(idx) == remap.end()) { remap[idx] = (int)out.vertices.size(); out.vertices.push_back(p[idx]); }
        for (const Face& f : faces) {
            out.triangles.push_back(remap[f.a]); out.triangles.push_back(remap[f.b]); out.triangles.push_back(remap[f.c]);
        }
        out.ComputeSmoothNormals();
        return out;
    }

    void Mesh::Bisect(const Vec3& planeP, const Vec3& planeN, bool cap, bool keepPositive) {
        Vec3 N = planeN; float nl = N.Magnitude();
        if (nl < 1e-9f) return;
        N = N * (1.0f / nl);
        const float sign = keepPositive ? 1.0f : -1.0f;
        auto sd = [&](const Vec3& v) { return sign * ((v.x - planeP.x) * N.x + (v.y - planeP.y) * N.y + (v.z - planeP.z) * N.z); };
        std::vector<Vec3> nv; std::vector<int> nt;
        std::vector<Vec3> cut;                                   // intersection points (for the cap)
        auto emit = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
            int base = (int)nv.size(); nv.push_back(a); nv.push_back(b); nv.push_back(c);
            nt.push_back(base); nt.push_back(base + 1); nt.push_back(base + 2);
        };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            Vec3 v[3] = {vertices[triangles[t]], vertices[triangles[t + 1]], vertices[triangles[t + 2]]};
            float d[3] = {sd(v[0]), sd(v[1]), sd(v[2])};
            // Clip the triangle to the kept half-space (Sutherland–Hodgman).
            std::vector<Vec3> poly;
            for (int i = 0; i < 3; ++i) {
                int j = (i + 1) % 3;
                if (d[i] >= 0.0f) poly.push_back(v[i]);
                if ((d[i] >= 0.0f) != (d[j] >= 0.0f)) {          // edge crosses the plane
                    float tt = d[i] / (d[i] - d[j]);
                    Vec3 x = v[i] + (v[j] - v[i]) * tt;
                    poly.push_back(x);
                    cut.push_back(x);
                }
            }
            for (std::size_t k = 1; k + 1 < poly.size(); ++k) emit(poly[0], poly[k], poly[k + 1]);
        }
        // Cap the cross-section: fan-triangulate the intersection ring in the plane.
        if (cap && cut.size() >= 3) {
            Vec3 ctr{0, 0, 0}; for (const Vec3& c : cut) ctr += c; ctr = ctr * (1.0f / (float)cut.size());
            // Plane basis for angular sort.
            Vec3 up = std::fabs(N.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3 e0 = Vec3::Cross(up, N); e0 = e0 * (1.0f / std::max(1e-6f, e0.Magnitude()));
            Vec3 e1 = Vec3::Cross(N, e0);
            auto ang = [&](const Vec3& c) { Vec3 dd = c - ctr; return std::atan2(dd.x * e1.x + dd.y * e1.y + dd.z * e1.z,
                                                                                 dd.x * e0.x + dd.y * e0.y + dd.z * e0.z); };
            std::sort(cut.begin(), cut.end(), [&](const Vec3& a, const Vec3& b) { return ang(a) < ang(b); });
            for (std::size_t k = 0; k < cut.size(); ++k) {
                const Vec3& a = cut[k]; const Vec3& b = cut[(k + 1) % cut.size()];
                // Wind the cap so its normal faces out of the kept half (-N side is the new surface).
                Vec3 fn = Vec3::Cross(a - ctr, b - ctr);
                int base = (int)nv.size();
                if ((fn.x * N.x + fn.y * N.y + fn.z * N.z) * sign > 0.0f) { nv.push_back(ctr); nv.push_back(b); nv.push_back(a); }
                else                                                       { nv.push_back(ctr); nv.push_back(a); nv.push_back(b); }
                nt.push_back(base); nt.push_back(base + 1); nt.push_back(base + 2);
            }
        }
        vertices = std::move(nv); triangles = std::move(nt);
        triColors.clear(); uvs.clear();
        name = "";
        WeldVertices();
        ComputeSmoothNormals();
    }

    void Mesh::ShrinkFatten(float dist) {
        if (vertices.empty() || !std::isfinite(dist)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        ComputeSmoothNormals();
        Vec3 ctr{0, 0, 0};
        for (const Vec3& v : vertices) ctr += v;
        ctr = ctr * (1.0f / (float)vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[i];
            Vec3 out = vertices[i] - ctr;
            if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) n = n * -1.0f;  // face outward
            vertices[i] += n * dist;
        }
        name = "";
        ComputeSmoothNormals();
    }

    void Mesh::Wireframe(float thickness) {
        if (triangles.empty() || !(thickness > 1e-6f) || !std::isfinite(thickness)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        Mesh w = *this; w.WeldVertices();
        std::set<std::pair<int, int>> edges;
        for (std::size_t t = 0; t + 2 < w.triangles.size(); t += 3) {
            int a = w.triangles[t], b = w.triangles[t + 1], c = w.triangles[t + 2];
            auto add = [&](int i, int j) { edges.insert({std::min(i, j), std::max(i, j)}); };
            add(a, b); add(b, c); add(c, a);
        }
        Mesh out;
        float r = thickness * 0.5f;
        for (const auto& e : edges) out.AddBeam(w.vertices[e.first], w.vertices[e.second], r);
        *this = std::move(out);
        name = "";
        ComputeSmoothNormals();
    }

    void Mesh::Displace(float amount, float frequency, int seed) {
        if (!std::isfinite(amount + frequency)) return;   // a NaN or infinite amount would turn every vertex into NaN
        amount = std::clamp(amount, -1e6f, 1e6f); frequency = std::clamp(frequency, -1e4f, 1e4f);   // (and absurd ones overflow the noise)
        if (vertices.empty()) return;
        ComputeSmoothNormals();
        Vec3 ctr{0, 0, 0};
        for (const Vec3& v : vertices) ctr += v;
        ctr = ctr * (1.0f / (float)vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[i], out = vertices[i] - ctr;
            if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) n = n * -1.0f;
            float d = ValueNoise(vertices[i] * frequency, seed);
            vertices[i] += n * (d * amount);
        }
        name = "";
        ComputeSmoothNormals();
    }

    void Mesh::CastToCylinder(float radius, int axis, float amount) {
        if (vertices.empty() || !std::isfinite(radius) || !std::isfinite(amount)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        for (Vec3& v : vertices) {
            float a, b;
            if (axis == 0)      { a = v.y; b = v.z; }
            else if (axis == 2) { a = v.x; b = v.y; }
            else                { a = v.x; b = v.z; }
            float r = std::sqrt(a * a + b * b);
            if (r < 1e-6f) continue;
            float f = 1.0f + (radius / r - 1.0f) * amount;   // lerp(1, radius/r, amount)
            a *= f; b *= f;
            if (axis == 0)      { v.y = a; v.z = b; }
            else if (axis == 2) { v.x = a; v.y = b; }
            else                { v.x = a; v.z = b; }
        }
        name = "";
        ComputeSmoothNormals();
    }

    int Mesh::FillHoles() {
        AttrGuard attrGuard_(*this);
        if (triangles.size() < 3 || vertices.empty()) return 0;
        // Representative index per coincident-vertex group.
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(vertices.size());
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 4096.0f),
                                   (int)std::lround(v.y * 4096.0f),
                                   (int)std::lround(v.z * 4096.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::set<std::pair<int, int>> edges;            // directed, on representatives
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = rep[triangles[t]], b = rep[triangles[t + 1]], c = rep[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;
            edges.insert({a, b}); edges.insert({b, c}); edges.insert({c, a});
        }
        // Boundary edges: no reverse partner. A hole rim chains head-to-tail.
        std::map<int, int> next;
        for (const auto& e : edges)
            if (!edges.count({e.second, e.first}) && !next.count(e.first))
                next[e.first] = e.second;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        const bool hadFC = HasFaceColors(), hadSlots = HasSlots();
        int holes = 0;
        std::set<int> used;
        for (const auto& start : next) {
            if (used.count(start.first)) continue;
            std::vector<int> loop;                      // walk the rim
            int v = start.first;
            while (next.count(v) && !used.count(v)) {
                used.insert(v); loop.push_back(v);
                v = next[v];
            }
            if (loop.size() < 3 || v != start.first) continue;   // open chain — not a loop
            Vec3 c{0, 0, 0};
            for (int i : loop) c += vertices[i];
            c = c * (1.0f / (float)loop.size());
            int ci = (int)vertices.size();
            vertices.push_back(c);
            if (hadUV) uvs.push_back({0.5f, 0.5f});
            // Rim edges wind with the surrounding faces; (b, a, centre) faces outward.
            for (std::size_t i = 0; i < loop.size(); ++i) {
                int a = loop[i], b = loop[(i + 1) % loop.size()];
                triangles.insert(triangles.end(), {b, a, ci});
            }
            ++holes;
        }
        if (holes > 0) {
            // The caps are new faces: they get white and slot 0, and every old face keeps its
            // paint and slot (audit: filling a hole used to wipe the whole mesh's face colours).
            if (hadFC) triColors.resize((std::size_t)TriangleCount(), Color{1, 1, 1, 1}); else triColors.clear();
            if (hadSlots) triSlot.resize((std::size_t)TriangleCount(), 0);
            name = "";
            RefreshNormals();
        }
        return holes;
    }

    Mesh::RepairReport Mesh::Repair(float weldDist, bool fillHoles) {
        AttrGuard attrGuard_(*this);
        RepairReport r;
        const TopologyReport before = Topology();
        r.openEdgesBefore = before.boundaryEdges; r.nonManifoldBefore = before.nonManifoldEdges;
        r.mergedVerts = WeldVertices(weldDist);
        r.flippedFaces = OrientFacesOutward();          // consistent first, so hole rims chain up
        if (fillHoles) {
            r.holesFilled = FillHoles();
            if (r.holesFilled > 0) r.flippedFaces += OrientFacesOutward();   // the caps too
        }
        const TopologyReport after = Topology();
        r.openEdgesAfter = after.boundaryEdges; r.nonManifoldAfter = after.nonManifoldEdges;
        return r;
    }

    bool Mesh::BridgeLoops() {
        if (triangles.size() < 3 || vertices.empty()) return false;
        // Boundary loops, coincident-vertex aware (same approach as FillHoles).
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(vertices.size());
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 4096.0f),
                                   (int)std::lround(v.y * 4096.0f),
                                   (int)std::lround(v.z * 4096.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::set<std::pair<int, int>> edges;
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = rep[triangles[t]], b = rep[triangles[t + 1]], c = rep[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;
            edges.insert({a, b}); edges.insert({b, c}); edges.insert({c, a});
        }
        std::map<int, int> next;
        for (const auto& e : edges)
            if (!edges.count({e.second, e.first}) && !next.count(e.first))
                next[e.first] = e.second;
        std::vector<std::vector<int>> loops;
        std::set<int> used;
        for (const auto& start : next) {
            if (used.count(start.first)) continue;
            std::vector<int> loop;
            int v = start.first;
            while (next.count(v) && !used.count(v)) { used.insert(v); loop.push_back(v); v = next[v]; }
            if (loop.size() >= 3 && v == start.first) loops.push_back(std::move(loop));
        }
        if (loops.size() != 2) return false;
        std::vector<int>& A = loops[0];
        std::vector<int>  B = loops[1];
        // Two rims of one surface wind opposite each other when facing; walking A
        // forward pairs with walking B REVERSED. Then rotate B so its start is the
        // vertex nearest A's start.
        std::reverse(B.begin(), B.end());
        {
            float best = 1e30f; std::size_t bi = 0;
            for (std::size_t i = 0; i < B.size(); ++i) {
                float d = (vertices[B[i]] - vertices[A[0]]).SqrMagnitude();
                if (d < best) { best = d; bi = i; }
            }
            std::rotate(B.begin(), B.begin() + bi, B.end());
        }
        const int nA = (int)A.size(), nB = (int)B.size();
        const int steps = std::max(nA, nB);
        auto ai = [&](int i) { return A[((long long)i * nA / steps) % nA]; };
        auto bi = [&](int i) { return B[((long long)i * nB / steps) % nB]; };
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        (void)hadUV;                     // bridge reuses existing vertices only
        for (int i = 0; i < steps; ++i) {
            int a0 = ai(i), a1 = ai(i + 1);
            int b0 = bi(i), b1 = bi(i + 1);
            // The surface already contains the rim's directed edges, so the wall
            // must supply their REVERSES: (a1->a0) and, on the reversed-B rim,
            // (b0->b1). Quad (a1, a0, b0, b1) split into two triangles does both.
            if (a0 != a1) triangles.insert(triangles.end(), {a1, a0, b0});
            if (b0 != b1) triangles.insert(triangles.end(), {a1, b0, b1});
        }
        triColors.clear();
        name = "";
        RefreshNormals();
        return true;
    }

    void Mesh::CollapseEdges(const std::vector<std::pair<int, int>>& es) {
        AttrGuard attrGuard_(*this);
        if (es.empty()) return;
        for (const auto& e : es) {
            if (e.first < 0 || e.first >= (int)vertices.size() ||
                e.second < 0 || e.second >= (int)vertices.size()) continue;
            Vec3 mid = (vertices[e.first] + vertices[e.second]) * 0.5f;
            vertices[e.first] = mid;
            vertices[e.second] = mid;
        }
        WeldVertices();                  // merges the pairs + drops collapsed tris
        name = "";
        RefreshNormals();
    }

    void Mesh::SplitEdges(const std::vector<std::pair<int, int>>& es) { // dup-allow: Mesh member named Split*, not a string helper
        AttrGuard attrGuard_(*this);
        if (es.empty()) return;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        for (const auto& e : es) {
            int a = e.first, b = e.second;
            if (a < 0 || a >= (int)vertices.size() || b < 0 || b >= (int)vertices.size() || a == b)
                continue;
            int mid = (int)vertices.size();
            vertices.push_back((vertices[a] + vertices[b]) * 0.5f);
            if (hadUV) uvs.push_back({(uvs[a].x + uvs[b].x) * 0.5f, (uvs[a].y + uvs[b].y) * 0.5f});
            std::vector<int> out;
            out.reserve(triangles.size() + 6);
            for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
                int v[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
                int hit = -1;                            // which cyclic edge is (a,b)?
                for (int k = 0; k < 3; ++k) {
                    int p = v[k], q = v[(k + 1) % 3];
                    if ((p == a && q == b) || (p == b && q == a)) { hit = k; break; }
                }
                if (hit < 0) { out.insert(out.end(), {v[0], v[1], v[2]}); continue; }
                int p = v[hit], q = v[(hit + 1) % 3], r = v[(hit + 2) % 3];
                out.insert(out.end(), {p, mid, r,  mid, q, r});   // same orientation
            }
            triangles = std::move(out);
        }
        triColors.clear();
        name = "";
        RefreshNormals();
    }

    std::vector<int> Mesh::DuplicateFaces(const std::vector<int>& faces) {
        AttrGuard attrGuard_(*this);
        std::vector<int> newFaces;
        if (faces.empty()) return newFaces;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        const bool hadColors = HasFaceColors();
        std::map<int, int> remap;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            newFaces.push_back((int)(triangles.size() / 3));
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                auto it = remap.find(v);
                int nv;
                if (it == remap.end()) {
                    nv = (int)vertices.size(); remap[v] = nv;
                    vertices.push_back(vertices[v]);
                    if (hadUV) uvs.push_back(uvs[v]);
                } else nv = it->second;
                triangles.push_back(nv);
            }
            if (hadColors && f < (int)triColors.size()) triColors.push_back(triColors[f]);
        }
        name = "";
        RefreshNormals();
        return newFaces;
    }

    std::vector<int> Mesh::CoincidentReps() const {
        // Hashed (not an ordered map): this runs on every smooth-shading pass and on every guided regenerate, on up to a few
        // hundred thousand vertices. The representative is still the first vertex in each 1/4096 cell.
        struct Cell { int x, y, z; bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; } };
        struct CellHash { std::size_t operator()(const Cell& c) const { return (std::size_t)((unsigned)c.x * 73856093u) ^ (std::size_t)((unsigned)c.y * 19349663u) ^ (std::size_t)((unsigned)c.z * 83492791u); } };
        std::unordered_map<Cell, int, CellHash> grid;
        grid.reserve(vertices.size() * 2 + 1);
        std::vector<int> rep(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& v = vertices[i];
            const Cell k{(int)std::lround(v.x * 4096.0f), (int)std::lround(v.y * 4096.0f), (int)std::lround(v.z * 4096.0f)};
            auto it = grid.find(k);
            if (it == grid.end()) { grid.emplace(k, (int)i); rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        return rep;
    }

    std::vector<int> Mesh::GrownVertexSelection(const std::vector<int>& sel) const {
        if (sel.empty() || vertices.empty()) return sel;
        std::vector<int> rep = CoincidentReps();
        std::set<int> inSel;                              // rep space
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) inSel.insert(rep[v]);
        std::set<int> grown = inSel;
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k && inSel.count(r[k])) grown.insert(r[j]);
        }
        std::vector<int> out;
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (grown.count(rep[i])) out.push_back((int)i);
        return out;
    }

    std::vector<int> Mesh::ShrunkVertexSelection(const std::vector<int>& sel) const {
        if (sel.empty() || vertices.empty()) return sel;
        std::vector<int> rep = CoincidentReps();
        std::set<int> inSel;
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) inSel.insert(rep[v]);
        std::set<int> rim;                                // selected verts touching outside
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k && inSel.count(r[k]) && !inSel.count(r[j])) rim.insert(r[k]);
        }
        std::vector<int> out;
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (inSel.count(rep[i]) && !rim.count(rep[i])) out.push_back((int)i);
        return out;
    }

    std::vector<int> Mesh::LinkedVertices(int seed) const {
        std::vector<int> out;
        if (seed < 0 || seed >= (int)vertices.size()) return out;
        std::vector<int> rep = CoincidentReps();
        std::map<int, std::vector<int>> adj;              // rep-space adjacency
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k) adj[r[k]].push_back(r[j]);
        }
        std::set<int> seen;
        std::vector<int> stack = {rep[seed]};
        seen.insert(rep[seed]);
        while (!stack.empty()) {
            int v = stack.back(); stack.pop_back();
            auto it = adj.find(v);
            if (it == adj.end()) continue;
            for (int n : it->second)
                if (!seen.count(n)) { seen.insert(n); stack.push_back(n); }
        }
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (seen.count(rep[i])) out.push_back((int)i);
        return out;
    }

    std::vector<std::pair<int, int>> Mesh::EdgeLoop(int va, int vb, float minDot) const {
        std::vector<std::pair<int, int>> out;
        if (va < 0 || vb < 0 || va >= (int)vertices.size() || vb >= (int)vertices.size())
            return out;
        std::vector<int> rep = CoincidentReps();
        std::map<int, std::vector<int>> adj;              // quad-wireframe adjacency
        for (const auto& e : VisibleEdges()) {
            int a = rep[e.first], b = rep[e.second];
            if (a == b) continue;
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
        auto dir = [&](int a, int b) { return (vertices[b] - vertices[a]).Normalized(); };
        std::set<std::pair<int, int>> seen;
        auto addEdge = [&](int a, int b) {
            auto k = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            if (seen.count(k)) return false;
            seen.insert(k);
            out.push_back({a, b});
            return true;
        };
        int A = rep[va], B = rep[vb];
        if (!addEdge(A, B)) return out;
        for (int pass = 0; pass < 2; ++pass) {            // walk both directions
            int prev = pass == 0 ? A : B;
            int cur  = pass == 0 ? B : A;
            for (int guard = 0; guard < 4096; ++guard) {
                Vec3 d = dir(prev, cur);
                float best = minDot; int nxt = -1;
                auto it = adj.find(cur);
                if (it == adj.end()) break;
                for (int n : it->second) {
                    if (n == prev) continue;
                    float dot = Vec3::Dot(d, dir(cur, n));
                    if (dot > best) { best = dot; nxt = n; }
                }
                if (nxt < 0) break;
                if (!addEdge(cur, nxt)) break;            // loop closed
                prev = cur; cur = nxt;
            }
        }
        return out;
    }

    void Mesh::PaintFaces(const Vec3& center, float radius, const Color& c, float blend) {
        if (!(radius > 1e-6f) || triangles.empty() || !(blend > 0.0f)) return;
        if (!std::isfinite(radius + blend + center.x + center.y + center.z + c.r + c.g + c.b + c.a)) return;   // NaN would spread into the colours
        EnsureFaceColors();
        const int tc = TriangleCount();
        const bool poly = (int)polyFace.size() == tc;
        std::map<int, std::vector<int>> polyVerts;   // polygon id -> its corners
        if (poly)
            for (int f = 0; f < tc; ++f)
                for (int k = 0; k < 3; ++k) polyVerts[polyFace[(std::size_t)f]].push_back(triangles[(std::size_t)f * 3 + k]);
        std::map<int, Vec3> polyCentre;
        for (auto& pv : polyVerts) {
            std::vector<int>& l = pv.second;
            std::sort(l.begin(), l.end());
            l.erase(std::unique(l.begin(), l.end()), l.end());
            Vec3 sum{0, 0, 0}; int cnt = 0;
            for (int v : l) if (v >= 0 && v < (int)vertices.size()) { sum += vertices[(std::size_t)v]; ++cnt; }
            polyCentre[pv.first] = cnt ? sum * (1.0f / (float)cnt) : Vec3{0, 0, 0};
        }
        for (int f = 0; f < tc; ++f) {
            const Vec3 fc = poly ? polyCentre[polyFace[(std::size_t)f]] : FaceCenter(f);
            float w = BrushFalloff((fc - center).Magnitude(), radius) * blend;
            if (w <= 0.0f) continue;
            if (w > 1.0f) w = 1.0f;
            Color& o = triColors[(std::size_t)f];
            o.r += (c.r - o.r) * w;
            o.g += (c.g - o.g) * w;
            o.b += (c.b - o.b) * w;
            o.a += (c.a - o.a) * w;
        }
    }

    void Mesh::MottleFaceColors(float amt, int seed) {
        if (!HasFaceColors() || amt <= 0.0f) return;
        for (int f = 0; f < (int)triColors.size(); ++f) {
            unsigned x = ((unsigned)f * 73856093u) ^ ((unsigned)seed * 19349663u);
            x = (x ^ (x >> 13)) * 1274126177u;
            float k = 1.0f + amt * (((float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f);
            Color& c = triColors[f];
            c.r = std::fmin(c.r * k, 1.0f);
            c.g = std::fmin(c.g * k, 1.0f);
            c.b = std::fmin(c.b * k, 1.0f);
        }
    }

    std::vector<std::pair<int, int>> Mesh::ExtrudeEdges(const std::vector<std::pair<int, int>>& es, float dist) {
        std::vector<std::pair<int, int>> newEdges;
        if (es.empty() || std::fabs(dist) < 1e-8f) return newEdges;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        for (const auto& e : es) {
            int a = e.first, b = e.second;
            if (a < 0 || a >= (int)vertices.size() || b < 0 || b >= (int)vertices.size() || a == b)
                continue;
            // Find a triangle owning this edge to get its direction + normal.
            int p = -1, q = -1; Vec3 n{0, 1, 0}; bool found = false;
            for (std::size_t t = 0; t + 2 < triangles.size() && !found; t += 3) {
                int v[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
                for (int k = 0; k < 3; ++k) {
                    int i0 = v[k], i1 = v[(k + 1) % 3];
                    if ((i0 == a && i1 == b) || (i0 == b && i1 == a)) {
                        p = i0; q = i1; n = FaceNormal((int)(t / 3)); found = true; break;
                    }
                }
            }
            if (!found) continue;
            int p2 = (int)vertices.size();
            vertices.push_back(vertices[p] + n * dist);
            if (hadUV) uvs.push_back(uvs[p]);
            int q2 = (int)vertices.size();
            vertices.push_back(vertices[q] + n * dist);
            if (hadUV) uvs.push_back(uvs[q]);
            // The face already has directed edge p->q; the wall supplies q->p.
            triangles.insert(triangles.end(), {q, p, p2,  q, p2, q2});
            newEdges.push_back({p2, q2});
        }
        if (!newEdges.empty()) {
            triColors.clear();
            name = "";
            RefreshNormals();
        }
        return newEdges;
    }

    void Mesh::SnapToGrid(float step, const std::vector<int>& verts) {
        if (!std::isfinite(step)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (step <= 1e-6f || vertices.empty()) return;
        auto snap = [&](int v) {
            vertices[v] = {std::lround(vertices[v].x / step) * step,
                           std::lround(vertices[v].y / step) * step,
                           std::lround(vertices[v].z / step) * step};
        };
        if (verts.empty())
            for (int i = 0; i < (int)vertices.size(); ++i) snap(i);
        else
            for (int v : verts)
                if (v >= 0 && v < (int)vertices.size()) snap(v);
        name = "";
        RefreshNormals();
    }

    void Mesh::SplitByPlane(int axis, float coord, float eps) { // dup-allow: Mesh member named Split*, not a string helper
        if (axis < 0 || axis > 2 || triangles.size() < 3) return;
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 2);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 2);
        auto sd = [&](int v) { return (&vertices[v].x)[axis] - coord; };
        auto lerpV = [&](int a, int b) {
            float sa = sd(a), sb = sd(b), t = sa / (sa - sb);
            int idx = (int)vertices.size();
            vertices.push_back(vertices[a] + (vertices[b] - vertices[a]) * t);
            if (uvs.size() == vertices.size() - 1)
                uvs.clear();   // (keep it simple: slicing drops per-vertex UVs)
            return idx;
        };
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            float s[3] = {sd(a), sd(b), sd(c)};
            float mn = std::fmin(s[0], std::fmin(s[1], s[2]));
            float mx = std::fmax(s[0], std::fmax(s[1], s[2]));
            if (!(mn < -eps && mx > eps)) {                     // no genuine crossing
                out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue;
            }
            int v[3] = {a, b, c};
            int lone = (s[0] * s[1] >= 0.0f) ? 2 : ((s[0] * s[2] >= 0.0f) ? 1 : 0);
            int L = v[lone], M = v[(lone + 1) % 3], N = v[(lone + 2) % 3];
            int iLM = lerpV(L, M), iNL = lerpV(N, L);
            out.insert(out.end(), {L, iLM, iNL});               // lone-side triangle
            out.insert(out.end(), {M, N, iNL,  M, iNL, iLM});   // other-side quad
            if (hadColors) { outC.push_back(fc); outC.push_back(fc); outC.push_back(fc); }
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    void Mesh::BevelVertices(const std::vector<int>& sel, float amount) {
        AttrGuard attrGuard_(*this);
        if (!std::isfinite(amount)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (sel.empty() || amount <= 1e-6f || triangles.empty()) return;
        std::set<int> S;
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) S.insert(v);
        if (S.empty()) return;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        std::vector<Vec3> vn = Normals();                // cap orientation
        std::map<std::pair<int, int>, int> splitIdx;     // (corner, neighbour) -> new vert
        std::map<int, std::vector<int>> ringOf;          // corner -> its new ring verts
        auto splitPoint = [&](int v, int o) {
            auto k = std::make_pair(v, o);
            auto it = splitIdx.find(k);
            if (it != splitIdx.end()) return it->second;
            Vec3 d = vertices[o] - vertices[v];
            float len = d.Magnitude();
            float t = len > 1e-8f ? std::min(amount, len * 0.45f) / len : 0.0f;
            int idx = (int)vertices.size();
            vertices.push_back(vertices[v] + d * t);
            if (hadUV) uvs.push_back(uvs[v]);
            splitIdx[k] = idx;
            ringOf[v].push_back(idx);
            return idx;
        };
        std::vector<int> out;
        out.reserve(triangles.size() * 2);
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
            int poly[6]; int pn = 0;
            // Walk the perimeter a->b->c; a chamfered corner contributes its two
            // edge split points in walk order (arrive-edge first, leave-edge second).
            auto corner = [&](int v, int prev, int nxt) {
                if (!S.count(v)) { poly[pn++] = v; return; }
                poly[pn++] = splitPoint(v, prev);
                poly[pn++] = splitPoint(v, nxt);
            };
            corner(a, c, b); corner(b, a, c); corner(c, b, a);
            for (int i = 1; i + 1 < pn; ++i)
                out.insert(out.end(), {poly[0], poly[i], poly[i + 1]});
        }
        triangles = std::move(out);
        // Cap each chamfered corner with a facet over its ring, ordered by angle
        // around the old vertex normal so the fan faces outward.
        for (auto& kv : ringOf) {
            std::vector<int>& ring = kv.second;
            if (ring.size() < 3) continue;
            Vec3 nrm = vn[kv.first];
            if (nrm.Magnitude() < 1e-6f) nrm = {0, 1, 0};
            Vec3 u = Vec3::Cross(nrm, std::fabs(nrm.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
            { float mu = u.Magnitude(); u = mu > 1e-8f ? u * (1.0f / mu) : Vec3{1, 0, 0}; }
            Vec3 w = Vec3::Cross(nrm, u);
            Vec3 c{0, 0, 0};
            for (int i : ring) c += vertices[i];
            c = c * (1.0f / (float)ring.size());
            std::sort(ring.begin(), ring.end(), [&](int i, int j) {
                Vec3 di = vertices[i] - c, dj = vertices[j] - c;
                return std::atan2(Vec3::Dot(di, w), Vec3::Dot(di, u))
                     < std::atan2(Vec3::Dot(dj, w), Vec3::Dot(dj, u));
            });
            for (std::size_t i = 1; i + 1 < ring.size(); ++i)
                triangles.insert(triangles.end(), {ring[0], (int)ring[i], (int)ring[i + 1]});
        }
        triColors.clear();                               // face records changed shape
        name = "";
        RefreshNormals();
    }

    void Mesh::JitterVertices(const std::vector<int>& verts, float amount, int seed) {
        if (amount == 0.0f || !std::isfinite(amount) || vertices.empty()) return;   // a typed NaN or infinite value would turn every vertex into NaN
        auto h = [&](int i, int c) {
            unsigned x = ((unsigned)i * 73856093u) ^ ((unsigned)c * 19349663u) ^ ((unsigned)seed * 83492791u);
            x = (x ^ (x >> 13)) * 1274126177u;
            return ((float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f;
        };
        auto apply = [&](int v) {
            vertices[v] += Vec3{h(v, 1), h(v, 2), h(v, 3)} * amount;
        };
        if (verts.empty())
            for (int i = 0; i < (int)vertices.size(); ++i) apply(i);
        else
            for (int v : verts)
                if (v >= 0 && v < (int)vertices.size()) apply(v);
        name = "";
        RefreshNormals();
    }

    float Mesh::ValueNoise(const Vec3& p, int seed) {
        int xi = (int)std::floor(p.x), yi = (int)std::floor(p.y), zi = (int)std::floor(p.z);
        float fx = p.x - xi, fy = p.y - yi, fz = p.z - zi;
        auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
        fx = sm(fx); fy = sm(fy); fz = sm(fz);
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        float c00 = lerp(Hash3(xi, yi, zi, seed),       Hash3(xi + 1, yi, zi, seed), fx);
        float c10 = lerp(Hash3(xi, yi + 1, zi, seed),   Hash3(xi + 1, yi + 1, zi, seed), fx);
        float c01 = lerp(Hash3(xi, yi, zi + 1, seed),   Hash3(xi + 1, yi, zi + 1, seed), fx);
        float c11 = lerp(Hash3(xi, yi + 1, zi + 1, seed), Hash3(xi + 1, yi + 1, zi + 1, seed), fx);
        return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
    }

    void Mesh::AddBeam(const Vec3& p0, const Vec3& p1, float r) {
        Vec3 d = p1 - p0; float len = d.Magnitude();
        if (len < 1e-6f) return;
        d = d * (1.0f / len);
        Vec3 up = std::fabs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 u = Vec3::Cross(d, up); u = u * (1.0f / std::max(1e-6f, u.Magnitude()));
        Vec3 w = Vec3::Cross(d, u);
        auto corner = [&](const Vec3& base, float su, float sw) { return base + u * (su * r) + w * (sw * r); };
        int b = (int)vertices.size();
        // 0..3 around p0, 4..7 around p1 (same order).
        vertices.push_back(corner(p0, +1, +1)); vertices.push_back(corner(p0, +1, -1));
        vertices.push_back(corner(p0, -1, -1)); vertices.push_back(corner(p0, -1, +1));
        vertices.push_back(corner(p1, +1, +1)); vertices.push_back(corner(p1, +1, -1));
        vertices.push_back(corner(p1, -1, -1)); vertices.push_back(corner(p1, -1, +1));
        auto quad = [&](int a, int b2, int c, int dd) {
            triangles.insert(triangles.end(), {b + a, b + b2, b + c, b + a, b + c, b + dd});
        };
        quad(0, 1, 5, 4); quad(1, 2, 6, 5); quad(2, 3, 7, 6); quad(3, 0, 4, 7);  // sides
        quad(3, 2, 1, 0); quad(4, 5, 6, 7);                                       // caps
    }

    void Mesh::GridDims(const Vec3& lo, const Vec3& hi, float voxel, int maxDim, int& nx, int& ny, int& nz, float& outV) {
        outV = voxel;
        auto dim = [&](float span) {
            int d = (int)std::ceil(span / outV);
            return d < 1 ? 1 : d;
        };
        nx = dim(hi.x - lo.x); ny = dim(hi.y - lo.y); nz = dim(hi.z - lo.z);
        int m = std::max(nx, std::max(ny, nz));
        if (maxDim > 0 && m > maxDim) {
            outV *= (float)m / (float)maxDim;                   // grow cells to fit the cap
            nx = dim(hi.x - lo.x); ny = dim(hi.y - lo.y); nz = dim(hi.z - lo.z);
        }
    }

    Mesh Mesh::EmitOccupancy(const std::vector<unsigned char>& occ, int nx, int ny, int nz, const Vec3& origin, float v) {
        Mesh out;
        auto solid = [&](int x, int y, int z) {
            if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return false;
            return occ[(std::size_t)(z * ny + y) * nx + x] != 0;
        };
        auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 outward) {
            Vec3 nrm = Vec3::Cross(b - a, c - a);
            float dot = nrm.x * outward.x + nrm.y * outward.y + nrm.z * outward.z;
            int base = (int)out.vertices.size();
            if (dot >= 0.0f) { out.vertices.push_back(a); out.vertices.push_back(b);
                               out.vertices.push_back(c); out.vertices.push_back(d); }
            else             { out.vertices.push_back(a); out.vertices.push_back(d);
                               out.vertices.push_back(c); out.vertices.push_back(b); }
            out.triangles.insert(out.triangles.end(),
                                 {base, base + 1, base + 2, base, base + 2, base + 3});
        };
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    if (!solid(x, y, z)) continue;
                    float x0 = origin.x + x * v, y0 = origin.y + y * v, z0 = origin.z + z * v;
                    float x1 = x0 + v, y1 = y0 + v, z1 = z0 + v;
                    if (!solid(x + 1, y, z)) face({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0});
                    if (!solid(x - 1, y, z)) face({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {-1, 0, 0});
                    if (!solid(x, y + 1, z)) face({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, {0, 1, 0});
                    if (!solid(x, y - 1, z)) face({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});
                    if (!solid(x, y, z + 1)) face({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});
                    if (!solid(x, y, z - 1)) face({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1});
                }
        out.WeldVertices();
        out.ComputeSmoothNormals();
        return out;
    }

} // namespace okay
