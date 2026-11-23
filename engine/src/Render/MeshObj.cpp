// Mesh Wavefront OBJ reader and writer.
// Bodies of okay::Mesh members moved out of engine/include/okay/Render/Mesh.hpp (modularization plan M1); the
// declarations (with their default arguments) stay in the struct. The code is unchanged.
#include "okay/Render/Mesh.hpp"

namespace okay {

    Mesh Mesh::LoadOBJ(const std::string& path, bool* ok, std::string* outTexture) {
        Mesh m;
        // Read the whole file once and parse it with a pointer walk. The previous
        // getline + istringstream version spent 10-100x longer in stream tokenizing
        // (a multi-second freeze on real exported models).
        std::string data;
        {
#ifdef _WIN32
            std::FILE* fp = _wfopen(utf8::ToWide(path).c_str(), L"rb");   // a narrow fopen reads a UTF-8 path as ANSI there
#else
            std::FILE* fp = std::fopen(path.c_str(), "rb");
#endif
            if (!fp) { if (ok) *ok = false; return m; }
            std::fseek(fp, 0, SEEK_END); long sz = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
            // A folder opens on some systems and reports a huge size (LONG_MAX on Linux): refuse it, and anything over
            // 2 GB, instead of asking for that much memory (std::length_error ended the editor).
            if (sz < 0 || sz >= 0x7fffffffL) { std::fclose(fp); if (ok) *ok = false; return m; }
            if (sz > 0) {
                data.resize((std::size_t)sz);
                if (std::fread(&data[0], 1, (std::size_t)sz, fp) != (std::size_t)sz) {
                    std::fclose(fp); if (ok) *ok = false; return m;
                }
            }
            std::fclose(fp);
        }
        std::string dir;
        { std::size_t s = path.find_last_of("/\\"); if (s != std::string::npos) dir = path.substr(0, s + 1); }
        std::vector<Vec3> pos, nrm; std::vector<Vec2> uv;
        struct Corner { int p, t, n; };                        // 0-based pos / uv / normal (-1 = none)
        std::vector<std::vector<Corner>> faces;
        std::vector<Color> vcol;                               // per `v` colour (the `v x y z r g b` extension)
        bool anyVCol = false;
        std::vector<std::string> mtlNames;                     // materials named by `usemtl`, in first-use order
        std::vector<int> faceMtl;                              // per face: index into mtlNames (-1 = none)
        int curMtl = -1;
        std::string mtllib;
        bool badIndex = false;                                 // a face index no vertex can satisfy
        const char* p = data.c_str();
        const char* end = p + data.size();
        // Bounded number readers: skip spaces/tabs only — NEVER a newline, so a short
        // line can't consume the next line's numbers (strtof alone would).
        auto readF = [&](const char*& c) -> float {
            while (c < end && (*c == ' ' || *c == '\t' || *c == '\r')) ++c;
            if (c >= end || *c == '\n') return 0.0f;
            char* q = nullptr; float v = std::strtof(c, &q); c = q; return v;
        };
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
            if (p < end) {
                if (p[0] == 'v' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
                    p += 2; Vec3 v; v.x = readF(p); v.y = readF(p); v.z = readF(p); pos.push_back(v);
                    // What follows the position: nothing, a weight (`v x y z w`), or a colour (`v x y z r g b`, 0..1).
                    float extra[4]; int nExtra = 0;
                    for (; nExtra < 4; ++nExtra) {
                        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                        if (p >= end || *p == '\n' || *p == '#') break;
                        char* q = nullptr; extra[nExtra] = std::strtof(p, &q);
                        if (q == p) break;
                        p = q;
                    }
                    Color vc{1, 1, 1, 1};
                    if (nExtra >= 3) {
                        if (std::isfinite(extra[0] + extra[1] + extra[2])) { vc.r = extra[0]; vc.g = extra[1]; vc.b = extra[2]; }
                        anyVCol = true;
                    }
                    if (anyVCol && vcol.size() + 1 < pos.size()) vcol.resize(pos.size() - 1, Color{1, 1, 1, 1});
                    if (anyVCol) vcol.push_back(vc);
                } else if (p[0] == 'v' && p + 2 < end && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
                    p += 3; Vec2 t; t.x = readF(p); t.y = readF(p); uv.push_back(t);
                } else if (p[0] == 'v' && p + 2 < end && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
                    p += 3; Vec3 n; n.x = readF(p); n.y = readF(p); n.z = readF(p); nrm.push_back(n);
                } else if (p[0] == 'f' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
                    p += 2;
                    std::vector<Corner> face; face.reserve(4);
                    for (;;) {
                        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                        if (p >= end || *p == '\n') break;
                        char* q = nullptr;
                        long vi = std::strtol(p, &q, 10); if (q == p) break; p = q;
                        long ti = 0, ni = 0;
                        if (p < end && *p == '/') {                 // v/vt, v//vn or v/vt/vn
                            ++p;
                            if (p < end && *p == '/') { ++p; ni = std::strtol(p, &q, 10); p = q; }
                            else {
                                ti = std::strtol(p, &q, 10); p = q;
                                if (p < end && *p == '/') { ++p; ni = std::strtol(p, &q, 10); p = q; }
                            }
                        }
                        if (vi < 0) vi = (long)pos.size() + vi + 1;
                        if (ti < 0) ti = (long)uv.size() + ti + 1;
                        if (ni < 0) ni = (long)nrm.size() + ni + 1;
                        if (vi < 1 || vi > INT_MAX || ti < 0 || ti > INT_MAX || ni < 0 || ni > INT_MAX) badIndex = true;
                        face.push_back({(int)vi - 1, (int)ti - 1, (int)ni - 1});
                    }
                    if (face.size() >= 3) { faces.push_back(std::move(face)); faceMtl.push_back(curMtl); }
                } else if (end - p >= 7 && std::strncmp(p, "usemtl", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
                    p += 7;
                    while (p < end && (*p == ' ' || *p == '\t')) ++p;
                    const char* s0 = p;
                    while (p < end && *p != '\n' && *p != '\r') ++p;
                    std::string nm(s0, p);
                    while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\t')) nm.pop_back();
                    curMtl = -1;
                    for (int k = 0; k < (int)mtlNames.size() && curMtl < 0; ++k) if (mtlNames[k] == nm) curMtl = k;
                    if (curMtl < 0) { curMtl = (int)mtlNames.size(); mtlNames.push_back(nm); }
                } else if (end - p >= 7 && std::strncmp(p, "mtllib", 6) == 0 &&
                           (p[6] == ' ' || p[6] == '\t')) {
                    p += 7;
                    while (p < end && (*p == ' ' || *p == '\t')) ++p;
                    const char* s = p;
                    while (p < end && *p != '\n' && *p != '\r') ++p;
                    mtllib.assign(s, p);
                }
            }
            while (p < end && *p != '\n') ++p;                    // to end of line
            if (p < end) ++p;
        }
        // Reject a face that points past the vertex list (checked against the final
        // count, so files that list faces before their vertices still load).
        for (const auto& fc : faces)
            for (const Corner& c : fc)
                if (c.p < 0 || c.p >= (int)pos.size()) badIndex = true;
        if (badIndex) { if (ok) *ok = false; return Mesh{}; }
        if (anyVCol) vcol.resize(pos.size(), Color{1, 1, 1, 1});
        // Every triangle remembers its face, so polygons (quads, n-gons) and materials carry over.
        std::vector<int> triFace;
        int curFace = 0;
        auto tri = [&](int a, int b, int cc) { m.triangles.insert(m.triangles.end(), {a, b, cc}); triFace.push_back(curFace); };
        if (uv.empty() && nrm.empty()) {               // simplest case: 1:1 with v lines
            m.vertices = pos;
            if (anyVCol) m.vertexColors = vcol;
            for (std::size_t fi = 0; fi < faces.size(); ++fi) {
                curFace = (int)fi; auto& fc = faces[fi];
                for (std::size_t i = 2; i < fc.size(); ++i) tri(fc[0].p, fc[i - 1].p, fc[i].p);
            }
        } else {                                       // de-index per (pos,uv,normal) so attributes map right
            const bool haveUV = !uv.empty(), haveN = !nrm.empty();
            // Hash map keyed on the packed corner: the ordered std::map<tuple> this
            // replaces was the second big import cost (log-n rebalancing per corner).
            struct CKey { int p, t, n; bool operator==(const CKey& o) const { return p == o.p && t == o.t && n == o.n; } };
            struct CKeyHash {
                std::size_t operator()(const CKey& k) const {
                    std::size_t h = 1469598103934665603ull;
                    auto mix = [&](long v) { h ^= (std::size_t)(v + 1); h *= 1099511628211ull; }; // dup-allow: moved unchanged from Mesh.hpp (vertex-key mix, not a string hash)
                    mix(k.p); mix(k.t); mix(k.n); return h;
                }
            };
            std::unordered_map<CKey, int, CKeyHash> remap;
            remap.reserve(pos.size() * 2 + 16);
            auto corner = [&](const Corner& c) {
                CKey key{c.p, c.t, c.n};
                auto it = remap.find(key); if (it != remap.end()) return it->second;
                int ni = (int)m.vertices.size();
                m.vertices.push_back((c.p >= 0 && c.p < (int)pos.size()) ? pos[c.p] : Vec3{0, 0, 0});
                if (haveUV) m.uvs.push_back((c.t >= 0 && c.t < (int)uv.size()) ? uv[c.t] : Vec2{0, 0});
                if (haveN)  m.normals.push_back((c.n >= 0 && c.n < (int)nrm.size()) ? nrm[c.n] : Vec3{0, 1, 0});
                if (anyVCol) m.vertexColors.push_back(vcol[c.p]);
                remap[key] = ni; return ni;
            };
            std::vector<int> idx;
            for (std::size_t fi = 0; fi < faces.size(); ++fi) {
                curFace = (int)fi; auto& fc = faces[fi];
                idx.clear(); for (auto& c : fc) idx.push_back(corner(c));
                for (std::size_t i = 2; i < idx.size(); ++i) tri(idx[0], idx[i - 1], idx[i]);
            }
        }
        // Polygons: kept (as polygon ids) when the file has any face with more than three corners.
        bool anyPoly = false;
        for (const auto& fc : faces) if (fc.size() > 3) { anyPoly = true; break; }
        if (anyPoly) m.polyFace = triFace;
        NormalizeImportScale(m);   // cm/mm exports (Meshy AI etc.) land at a usable size
        // The .mtl is held to the same place as textures: the model's folder or an allowed root (TexturePath.hpp).
        const std::string mtlPath = mtllib.empty() ? std::string() : ResolveModelTexturePath(mtllib, dir);
        // Material colours: each `usemtl` material's diffuse colour (Kd, and d for opacity) becomes the face colour of
        // its faces. A material with a diffuse texture (map_Kd) stays white, so the texture is not tinted, and a file
        // whose faces name no coloured material gets no face colours at all (as before).
        if (!mtlPath.empty() && !mtlNames.empty()) {
            std::map<std::string, Color> kd; std::set<std::string> textured;
            std::ifstream mf(std::filesystem::u8path(mtlPath)); std::string ml, cur;
            while (mf && std::getline(mf, ml)) {
                std::istringstream ms(ml); std::string mt; ms >> mt;
                if (mt == "newmtl") { std::getline(ms, cur); std::size_t a = cur.find_first_not_of(" \t"); cur = a == std::string::npos ? std::string() : cur.substr(a);
                                      while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t' || cur.back() == '\r')) cur.pop_back(); }
                else if (mt == "Kd" && !cur.empty()) { Color c = kd.count(cur) ? kd[cur] : Color{1, 1, 1, 1}; float r, g, b;
                                      if (ms >> r >> g >> b && std::isfinite(r + g + b)) { c.r = r; c.g = g; c.b = b; kd[cur] = c; } }
                else if (mt == "d" && !cur.empty()) { float d; if (ms >> d && std::isfinite(d)) { Color c = kd.count(cur) ? kd[cur] : Color{1, 1, 1, 1}; c.a = std::clamp(d, 0.0f, 1.0f); kd[cur] = c; } }
                else if (mt == "map_Kd" && !cur.empty()) textured.insert(cur);
            }
            std::vector<Color> faceCol(faces.size(), Color{1, 1, 1, 1});
            bool any = false;
            for (std::size_t fi = 0; fi < faces.size(); ++fi) {
                const int k = fi < faceMtl.size() ? faceMtl[fi] : -1;
                if (k < 0) continue;
                auto it = kd.find(mtlNames[k]);
                if (it == kd.end() || textured.count(mtlNames[k])) continue;
                faceCol[fi] = it->second; any = true;
            }
            if (any) {
                m.triColors.clear();
                for (int f : triFace) m.triColors.push_back(faceCol[(std::size_t)f]);
            }
        }
        if (outTexture && !mtlPath.empty()) {                   // read .mtl for map_Kd
            std::ifstream mf(std::filesystem::u8path(mtlPath)); std::string ml;
            while (mf && std::getline(mf, ml)) {
                std::istringstream ms(ml); std::string mt; ms >> mt;
                if (mt == "map_Kd") { std::string tex; std::getline(ms, tex);
                    std::size_t a = tex.find_first_not_of(" \t");
                    if (a != std::string::npos) {
                        tex = tex.substr(a);
                        while (!tex.empty() && (tex.back() == ' ' || tex.back() == '\t' || tex.back() == '\r')) tex.pop_back();
                        // Inside the model's folder, or inside an allowed root (#753; okay/Render/TexturePath.hpp);
                        // otherwise no texture. (A relative path with a folder used to be kept as written, so it was
                        // looked up from the working folder, and an absolute or "../" path was kept as is.)
                        *outTexture = ResolveModelTexturePath(tex, dir);
                    }
                    break;
                }
            }
        }
        if (ok) *ok = true;
        return m;
    }

    bool Mesh::SaveOBJ(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;
        char buf[128];
        auto num = [&](float x) { std::snprintf(buf, sizeof buf, "%.9g", (double)(std::isfinite(x) ? x : 0.0f)); return std::string(buf); };
        const int nTri = TriangleCount();
        // Face colour per triangle: the face colours, else the slot colours.
        std::vector<Color> faceCol;
        if (HasFaceColors()) faceCol = triColors;
        else if (HasSlots() && !slotColors.empty()) {
            faceCol.resize((std::size_t)nTri, Color{1, 1, 1, 1});
            for (int t = 0; t < nTri; ++t)
                if (triSlot[t] >= 0 && triSlot[t] < (int)slotColors.size()) faceCol[t] = slotColors[triSlot[t]];
        }
        std::vector<int> matOf((std::size_t)nTri, -1);
        std::vector<Color> mats;
        for (int t = 0; t < (int)faceCol.size(); ++t) {
            const Color& c = faceCol[t];
            int m = -1;
            for (int k = 0; k < (int)mats.size() && m < 0; ++k)
                if (mats[k].r == c.r && mats[k].g == c.g && mats[k].b == c.b && mats[k].a == c.a) m = k;
            if (m < 0) { m = (int)mats.size(); mats.push_back(c); }
            matOf[t] = m;
        }
        std::string mtlName;
        if (!mats.empty()) {
            std::string mtlPath = path;
            const std::size_t slash = mtlPath.find_last_of("/\\"), dot = mtlPath.find_last_of('.');
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) mtlPath.erase(dot);
            mtlPath += ".mtl";
            mtlName = slash == std::string::npos ? mtlPath : mtlPath.substr(mtlPath.find_last_of("/\\") + 1);
            std::ofstream mf(mtlPath);
            if (!mf) return false;
            mf << "# OkaySpace materials (one per face colour)\n";
            for (int k = 0; k < (int)mats.size(); ++k) {
                mf << "newmtl color" << k << "\n";
                mf << "Kd " << num(mats[k].r) << " " << num(mats[k].g) << " " << num(mats[k].b) << "\n";
                if (mats[k].a < 1.0f) mf << "d " << num(mats[k].a) << "\n";
            }
            if (!mf) return false;
        }
        f << "# OkaySpace mesh\n";
        if (!mtlName.empty()) f << "mtllib " << mtlName << "\n";
        const bool vc = HasVertexColors();
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& v = vertices[i];
            f << "v " << num(v.x) << " " << num(v.y) << " " << num(v.z);
            if (vc) f << " " << num(vertexColors[i].r) << " " << num(vertexColors[i].g) << " " << num(vertexColors[i].b);
            f << "\n";
        }
        const bool cornerUV = HasCornerUVs();
        const bool vertUV = !cornerUV && HasUVs();
        if (cornerUV) for (const Vec2& t : cornerUVs) f << "vt " << num(t.x) << " " << num(t.y) << "\n";
        if (vertUV)   for (const Vec2& t : uvs) f << "vt " << num(t.x) << " " << num(t.y) << "\n";
        const bool vn = HasNormals();
        if (vn) for (const Vec3& n : normals) f << "vn " << num(n.x) << " " << num(n.y) << " " << num(n.z) << "\n";
        f << (vn ? "s 1\n" : "s off\n");
        // A corner: vertex v, reached through triangle corner c (for corner UVs).
        auto corner = [&](int v, int c) {
            std::string out = std::to_string(v + 1);
            if (cornerUV || vertUV || vn) {
                out += "/";
                if (cornerUV) out += std::to_string(c + 1);
                else if (vertUV) out += std::to_string(v + 1);
                if (vn) out += "/" + std::to_string(v + 1);
            }
            return out;
        };
        // Faces in order: a polygon is written once, where its first triangle is.
        const bool polys = (int)polyFace.size() == nTri && nTri > 0;
        std::map<int, std::vector<int>> trisOfPoly;
        if (polys) for (int t = 0; t < nTri; ++t) trisOfPoly[polyFace[t]].push_back(t);
        std::vector<char> done((std::size_t)nTri, 0);
        int curMat = -2;
        auto useMat = [&](int t) {
            const int m = matOf.empty() ? -1 : matOf[t];
            if (m != curMat && m >= 0) f << "usemtl color" << m << "\n";
            curMat = m;
        };
        for (int t = 0; t < nTri; ++t) {
            if (done[t]) continue;
            std::vector<std::pair<int, int>> loop;                      // (vertex, triangle corner) around the polygon
            if (polys) {
                const std::vector<int>& group = trisOfPoly[polyFace[t]];
                if (group.size() > 1 && PolygonOutline(group, matOf, loop)) {
                    for (int g : group) done[g] = 1;
                    useMat(t);
                    f << "f";
                    for (const auto& vc2 : loop) f << " " << corner(vc2.first, vc2.second);
                    f << "\n";
                    continue;
                }
            }
            done[t] = 1;
            useMat(t);
            f << "f";
            for (int k = 0; k < 3; ++k) f << " " << corner(triangles[t * 3 + k], t * 3 + k);
            f << "\n";
        }
        return static_cast<bool>(f);
    }

} // namespace okay
