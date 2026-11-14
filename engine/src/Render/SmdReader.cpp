// Our own SMD (Valve "Studiomdl Data", text) reader: a skeleton, a skinned triangle mesh and a skeletal animation.
// Written from the public description of the format (the Valve Developer Community's SMD page) and of Valve's published
// math library for the Euler angle convention. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and
// docs/own_smd_reader.md.
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace smd {

using namespace detail::txt;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxBones     = 4096;
constexpr std::size_t kMaxFrames    = 100000;
constexpr std::size_t kMaxKeys      = 20000000;   // bone entries over all frames
constexpr std::size_t kMaxTriangles = 5000000;
constexpr std::size_t kMaxMaterials = 4096;
constexpr float       kFps          = 30.0f;      // SMD stores frame numbers, not seconds

struct Bone { std::string name; int parent = -1; bool defined = false; };
struct Pose { float px = 0, py = 0, pz = 0, rx = 0, ry = 0, rz = 0; };
struct Corner {
    int bone = 0;
    float p[3], n[3], uv[2];
    int nlinks = 0;
    int linkBone[4] = {0, 0, 0, 0};
    float linkWt[4] = {0, 0, 0, 0};
};

// Valve's RadianEuler -> quaternion (mathlib AngleQuaternion): roll is X, pitch is Y, yaw is Z; rotation = Rz * Ry * Rx.
static Quat FromEuler(float rx, float ry, float rz) {
    const float sr = std::sin(rx * 0.5f), cr = std::cos(rx * 0.5f);
    const float sp = std::sin(ry * 0.5f), cp = std::cos(ry * 0.5f);
    const float sy = std::sin(rz * 0.5f), cy = std::cos(rz * 0.5f);
    return Quat{sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy}.Normalized();
}

struct Reader {
    std::string err;
    std::string baseDir;
    bool Fail(const std::string& m, std::size_t line = 0) {
        if (err.empty()) err = "SMD: " + m + (line ? " (line " + std::to_string(line) + ")" : "");
        return false;
    }

    std::vector<Bone> bones;
    std::map<int, std::map<int, Pose>> frames;    // time -> bone id -> pose
    std::vector<Corner> corners;                  // 3 per triangle
    std::vector<int> triMat;                      // material index per triangle
    std::vector<std::string> matNames;
    std::unordered_map<std::string, int> matIndex;
    int nonmonotonic = 0, unknownSections = 0, vertexAnim = 0, badBoneRefs = 0, extraLinks = 0, zeroNormals = 0, dupFrames = 0, droppedBoneEntries = 0;
    int dupBoneIds = 0;
    std::unordered_map<int, int> boneOfId;        // SMD bone id -> index in `bones`

    static std::string StripComment(const std::string& s) {
        const std::size_t p = s.find("//");
        return p == std::string::npos ? s : s.substr(0, p);
    }

    bool Parse(const std::string& text) {
        Lines lines(text);
        std::string raw;
        enum Sec { None, Nodes, Skeleton, Triangles, Skip } sec = None;
        int curTime = -1;
        bool haveTime = false;
        int triState = 0;            // 0 = expecting a material line, 1..3 = vertex lines
        std::string curMat;
        std::size_t triCount = 0, keyCount = 0;
        bool sawVersion = false, sawAnything = false;
        while (lines.Next(raw)) {
            const std::size_t ln = lines.number;
            const std::string line = StripComment(raw);
            std::vector<std::string> tk = Split(line);
            if (tk.empty()) continue;
            const std::string kw = Lower(tk[0]);
            if (sec == None || (sec != Triangles && (kw == "end" || kw == "nodes" || kw == "skeleton" || kw == "triangles" || kw == "version" || kw == "vertexanimation") && tk.size() <= 2)) {
                // section structure
                if (kw == "version") { if (tk.size() >= 2) { long long v; if (!ToInt(tk[1], v) || v != 1) return Fail("unsupported version " + tk[1], ln); } sawVersion = true; sec = None; continue; }
                if (kw == "nodes") { sec = Nodes; sawAnything = true; continue; }
                if (kw == "skeleton") { sec = Skeleton; haveTime = false; sawAnything = true; continue; }
                if (kw == "triangles") { sec = Triangles; triState = 0; sawAnything = true; continue; }
                if (kw == "vertexanimation") { sec = Skip; ++vertexAnim; continue; }
                if (kw == "end") { sec = None; continue; }
                if (sec == None) { ++unknownSections; sec = Skip; continue; }
            }
            if (sec == Skip) { if (kw == "end") sec = None; continue; }
            if (sec == Nodes) {
                if (kw == "end") { sec = None; continue; }
                long long id = 0, par = 0;
                if (tk.size() < 3 || !ToInt(tk[0], id) || !ToInt(tk[tk.size() - 1], par)) return Fail("bad bone line", ln);
                if (bones.size() >= kMaxBones) return Fail("too many bones", ln);
                if (boneOfId.count((int)id)) { ++dupBoneIds; continue; }
                Bone b;
                b.name = tk[1];
                b.parent = (int)par;   // an SMD id for now; mapped to an index below
                boneOfId[(int)id] = (int)bones.size();
                idOf.push_back((int)id);
                bones.push_back(std::move(b));
                continue;
            }
            if (sec == Skeleton) {
                if (kw == "end") { sec = None; continue; }
                if (kw == "time") {
                    long long t = 0;
                    if (tk.size() < 2 || !ToInt(tk[1], t)) return Fail("bad time line", ln);
                    if (haveTime && t <= curTime) ++nonmonotonic;
                    curTime = (int)t; haveTime = true;
                    if (frames.count(curTime) == 0 && frames.size() >= kMaxFrames) return Fail("too many frames", ln);
                    if (frames.count(curTime)) ++dupFrames;
                    frames[curTime];
                    continue;
                }
                if (!haveTime) return Fail("a bone pose before any 'time' line", ln);
                long long id = 0;
                Pose p;
                float v[6];
                if (tk.size() < 7 || !ToInt(tk[0], id)) return Fail("bad bone pose line", ln);
                for (int k = 0; k < 6; ++k) if (!ToFloat(tk[1 + (std::size_t)k], v[k])) return Fail("bad number in a bone pose", ln);
                p.px = v[0]; p.py = v[1]; p.pz = v[2]; p.rx = v[3]; p.ry = v[4]; p.rz = v[5];
                if (++keyCount > kMaxKeys) return Fail("too many bone poses", ln);
                if (!boneOfId.count((int)id)) { ++droppedBoneEntries; continue; }
                frames[curTime][(int)id] = p;
                continue;
            }
            if (sec == Triangles) {
                if (kw == "end" && triState == 0 && tk.size() == 1) { sec = None; continue; }
                if (triState == 0) {
                    // the material is the whole line (it may contain spaces); quotes are not used here
                    std::string m = line;
                    while (!m.empty() && IsWs(m.back())) m.pop_back();
                    std::size_t a = 0; while (a < m.size() && IsWs(m[a])) ++a;
                    curMat = m.substr(a);
                    triState = 1;
                    continue;
                }
                // vertex: parent px py pz nx ny nz u v [links n (bone weight)*]
                Corner c;
                long long b = 0;
                if (tk.size() < 9 || !ToInt(tk[0], b)) return Fail("bad vertex line", ln);
                float v[8];
                for (int k = 0; k < 8; ++k) if (!ToFloat(tk[1 + (std::size_t)k], v[k])) return Fail("bad number in a vertex", ln);
                c.bone = (int)b;
                c.p[0] = v[0]; c.p[1] = v[1]; c.p[2] = v[2]; c.n[0] = v[3]; c.n[1] = v[4]; c.n[2] = v[5]; c.uv[0] = v[6]; c.uv[1] = v[7];
                if (tk.size() > 9) {
                    long long nl = 0;
                    if (!ToInt(tk[9], nl) || nl < 0 || nl > 64 || tk.size() < 10 + 2 * (std::size_t)nl) return Fail("bad bone link list", ln);
                    int kept = 0;
                    for (long long k = 0; k < nl; ++k) {
                        long long lb = 0; float w = 0;
                        if (!ToInt(tk[10 + 2 * (std::size_t)k], lb) || !ToFloat(tk[11 + 2 * (std::size_t)k], w)) return Fail("bad bone link", ln);
                        if (w <= 0) continue;
                        if (kept < 4) { c.linkBone[kept] = (int)lb; c.linkWt[kept] = w; ++kept; }
                        else {
                            ++extraLinks;
                            int mn = 0; for (int q = 1; q < 4; ++q) if (c.linkWt[q] < c.linkWt[mn]) mn = q;   // keep the 4 strongest
                            if (w > c.linkWt[mn]) { c.linkBone[mn] = (int)lb; c.linkWt[mn] = w; }
                        }
                    }
                    c.nlinks = kept;
                }
                if (triState == 1) {
                    if (triCount >= kMaxTriangles) return Fail("too many triangles", ln);
                    auto it = matIndex.find(curMat);
                    int mi;
                    if (it == matIndex.end()) {
                        if (matNames.size() >= kMaxMaterials) return Fail("too many materials", ln);
                        mi = (int)matNames.size(); matNames.push_back(curMat); matIndex[curMat] = mi;
                    } else mi = it->second;
                    triMat.push_back(mi);
                    ++triCount;
                }
                corners.push_back(c);
                triState = triState == 3 ? 0 : triState + 1;
                continue;
            }
        }
        (void)sawVersion;
        if (triState != 0) return Fail("the file ends inside a triangle");
        if (!sawAnything) return Fail("no nodes, skeleton or triangles found: not an SMD file");
        return true;
    }

    std::vector<int> idOf;   // bone index -> SMD id

    bool Build(ImportedModel& out) {
        const std::size_t nb = bones.size();
        // Parents as bone indices; ids that do not exist, or loops, make the bone a root.
        std::vector<int> par(nb, -1);
        int badParents = 0;
        for (std::size_t i = 0; i < nb; ++i) {
            auto it = boneOfId.find(bones[i].parent);
            if (bones[i].parent >= 0 && it != boneOfId.end() && it->second != (int)i) par[i] = it->second;
            else if (bones[i].parent >= 0) ++badParents;
        }
        // order: parents before children; a parent loop is cut at the bone that closes it
        std::vector<int> order;
        badParents += OrderParentsFirst(par, order);
        // unique node names
        std::vector<std::string> nodeName(nb);
        {
            std::unordered_map<std::string, int> used;
            used["SMD"] = 1;
            int renamed = 0;
            for (std::size_t i = 0; i < nb; ++i) {
                std::string n = bones[i].name.empty() ? "bone" + std::to_string(i) : bones[i].name;
                int& u = used[n];
                if (u++) { n += "_" + std::to_string(u - 1); ++renamed; used[n] = 1; }
                nodeName[i] = n;
            }
            dupNames = renamed;
        }
        ImportedNode root;
        root.name = "SMD";
        {   // Z up to Y up: (x, y, z) -> (x, z, -y)
            root.rotation = Quat::AngleAxis(-90.0f, Vec3{1, 0, 0});
        }
        out.nodes.push_back(root);
        std::vector<int> nodeOf(nb, 0);
        // bind pose: the first (smallest time) frame
        const std::map<int, Pose>* bind = frames.empty() ? nullptr : &frames.begin()->second;
        std::vector<Mat4> world(nb);
        for (int bi : order) {
            ImportedNode n;
            n.name = nodeName[(std::size_t)bi];
            n.parent = par[(std::size_t)bi] < 0 ? 0 : nodeOf[(std::size_t)par[(std::size_t)bi]];
            Pose p;
            bool has = false;
            if (bind) { auto it = bind->find(idOf[(std::size_t)bi]); if (it != bind->end()) { p = it->second; has = true; } }
            if (!has) ++noBindPose;
            n.position = Vec3{p.px, p.py, p.pz};
            n.rotation = FromEuler(p.rx, p.ry, p.rz);
            const Mat4 l = Mat4::TRS(n.position, n.rotation, Vec3{1, 1, 1});
            world[(std::size_t)bi] = par[(std::size_t)bi] < 0 ? l : world[(std::size_t)par[(std::size_t)bi]] * l;
            nodeOf[(std::size_t)bi] = (int)out.nodes.size();
            out.nodes.push_back(std::move(n));
        }
        for (const Mat4& w : world) for (int k = 0; k < 16; ++k) if (!std::isfinite(w.m[k]) || std::fabs(w.m[k]) > 1e30f) return Fail("bone transforms overflow");

        // mesh: one part per material, vertices welded on position, normal, uv and skin
        const std::size_t ntri = triMat.size();
        if (ntri > 0) {
            std::vector<std::string> texOf(matNames.size());
            for (std::size_t m = 0; m < matNames.size(); ++m) {
                ImportedMaterial im;
                im.name = matNames[m];
                im.texture = matNames[m];
                if (!baseDir.empty() && im.texture.find_first_of("/\\") == std::string::npos && !im.texture.empty()) im.texture = baseDir + im.texture;
                out.materials.push_back(im);
            }
            std::vector<ImportedBone> allBones;
            const bool skinned = nb > 0;
            if (skinned) {
                allBones.resize(nb);
                for (std::size_t i = 0; i < nb; ++i) { allBones[i].name = nodeName[i]; allBones[i].inverseBind = world[i].Inverse(); }
            }
            struct PartAcc { ImportedPart part; std::unordered_map<std::string, int> weld; bool badNormal = false; };
            std::vector<PartAcc> parts(matNames.size());
            for (std::size_t t = 0; t < ntri; ++t) {
                PartAcc& pa = parts[(std::size_t)triMat[t]];
                for (int k = 0; k < 3; ++k) {
                    const Corner& c = corners[t * 3 + (std::size_t)k];
                    // skin weights: the listed links, the rest of the weight on the vertex's own bone
                    std::array<int, 4> ji{0, 0, 0, 0};
                    std::array<float, 4> jw{0, 0, 0, 0};
                    if (skinned) {
                        int cnt = 0; float sum = 0;
                        auto add = [&](int smdBone, float w) {
                            auto it = boneOfId.find(smdBone);
                            if (it == boneOfId.end()) { ++badBoneRefs; return; }
                            for (int q = 0; q < cnt; ++q) if (ji[(std::size_t)q] == it->second) { jw[(std::size_t)q] += w; sum += w; return; }
                            if (cnt < 4) { ji[(std::size_t)cnt] = it->second; jw[(std::size_t)cnt] = w; ++cnt; sum += w; }
                        };
                        for (int q = 0; q < c.nlinks; ++q) add(c.linkBone[q], c.linkWt[q]);
                        if (c.nlinks == 0) add(c.bone, 1.0f);
                        else if (sum < 0.999f) add(c.bone, 1.0f - sum);
                        if (cnt == 0) { ji[0] = 0; jw[0] = 1; sum = 1; }
                        float s2 = 0; for (float w : jw) s2 += w;
                        if (s2 > 0) for (float& w : jw) w /= s2;
                    }
                    Vec3 nrm{c.n[0], c.n[1], c.n[2]};
                    const float nm = nrm.Magnitude();
                    if (!(nm > 1e-6f) || !std::isfinite(nm)) { pa.badNormal = true; ++zeroNormals; nrm = Vec3{0, 0, 1}; } else nrm = nrm * (1.0f / nm);
                    // key
                    std::uint32_t key[16];
                    const float f[8] = {c.p[0], c.p[1], c.p[2], nrm.x, nrm.y, nrm.z, c.uv[0], c.uv[1]};
                    for (int q = 0; q < 8; ++q) std::memcpy(&key[q], &f[q], 4);
                    for (int q = 0; q < 4; ++q) { key[8 + q] = (std::uint32_t)ji[(std::size_t)q]; std::memcpy(&key[12 + q], &jw[(std::size_t)q], 4); }
                    const std::string ks((const char*)key, sizeof key);
                    auto it = pa.weld.find(ks);
                    int vi;
                    if (it == pa.weld.end()) {
                        vi = (int)pa.part.mesh.vertices.size();
                        pa.part.mesh.vertices.push_back(Vec3{c.p[0], c.p[1], c.p[2]});
                        pa.part.mesh.normals.push_back(nrm);
                        pa.part.mesh.uvs.push_back(Vec2{c.uv[0], 1.0f - c.uv[1]});
                        if (skinned) { pa.part.jointIdx.push_back(ji); pa.part.jointWt.push_back(jw); }
                        pa.weld.emplace(ks, vi);
                    } else vi = it->second;
                    pa.part.mesh.triangles.push_back(vi);
                }
            }
            for (std::size_t m = 0; m < parts.size(); ++m) {
                PartAcc& pa = parts[m];
                if (pa.part.mesh.triangles.empty()) continue;
                pa.part.mesh.name = matNames[m];
                if (pa.badNormal) pa.part.mesh.ComputeSmoothNormals();
                pa.part.node = 0;
                pa.part.material = (int)m;
                if (skinned) pa.part.bones = allBones;
                out.parts.push_back(std::move(pa.part));
            }
        }

        // animation: more than one frame -> one clip
        if (frames.size() > 1 && nb > 0) {
            ImportedClip clip;
            clip.name = "animation";
            const int t0 = frames.begin()->first;
            std::vector<ImportedChannel> ch(nb);
            for (std::size_t i = 0; i < nb; ++i) ch[i].node = nodeName[i];
            for (const auto& fr : frames) {
                const float time = (float)(fr.first - t0) / kFps;
                for (const auto& be : fr.second) {
                    const std::size_t bi = (std::size_t)boneOfId[be.first];
                    ch[bi].position.push_back({time, Vec3{be.second.px, be.second.py, be.second.pz}});
                    ch[bi].rotation.push_back({time, FromEuler(be.second.rx, be.second.ry, be.second.rz)});
                }
            }
            for (ImportedChannel& c : ch) if (!c.position.empty()) clip.channels.push_back(std::move(c));
            if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        }
        if (out.parts.empty() && out.clips.empty() && nb == 0) return Fail("the file has no mesh, no skeleton and no animation");

        auto note = [&](const std::string& s) { out.notes.push_back("SMD: " + s); };
        if (!out.clips.empty()) note("frame numbers are turned into seconds at an assumed " + std::to_string((int)kFps) + " frames per second");
        if (vertexAnim) note("a vertexanimation section (flex / morph frames) is not read");
        if (unknownSections) note(std::to_string(unknownSections) + " unknown section(s) skipped");
        if (badParents) note(std::to_string(badParents) + " bone(s) with a missing or looping parent were attached to the root");
        if (dupBoneIds) note(std::to_string(dupBoneIds) + " bone line(s) repeating an earlier bone id ignored");
        if (dupNames) note(std::to_string(dupNames) + " bone name(s) were repeated and got a numeric suffix");
        if (noBindPose) note(std::to_string(noBindPose) + " bone(s) without a pose in the first frame sit at the origin");
        if (nonmonotonic) note(std::to_string(nonmonotonic) + " 'time' line(s) not in increasing order: frames were sorted");
        if (dupFrames) note(std::to_string(dupFrames) + " frame(s) given twice: their entries were merged, the later pose of a bone wins");
        if (droppedBoneEntries) note(std::to_string(droppedBoneEntries) + " pose(s) for bone ids that are not in the nodes list ignored");
        if (badBoneRefs) note(std::to_string(badBoneRefs) + " vertex bone reference(s) to missing bones ignored");
        if (extraLinks) note(std::to_string(extraLinks) + " vertex bone link(s) beyond four per vertex dropped (the four strongest are kept)");
        if (zeroNormals) note(std::to_string(zeroNormals) + " vertex normal(s) were zero or invalid: smooth normals generated for those parts");
        if (!out.parts.empty()) {
            note("the root node turns Z-up into Y-up; triangle winding and units are kept as written; v coordinates are flipped (v = 1 - v)");
        }
        if (nb == 0 && !out.parts.empty()) note("no skeleton in the file: the mesh is rigid");
        return true;
    }
    int dupNames = 0, noBindPose = 0;
};

} // namespace smd
} // namespace

bool ReadSmdBuffer(const std::string& textIn, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    smd::Reader r;
    r.baseDir = baseDir;
    std::string text = textIn, why;
    bool conv = false;
    bool ok = detail::txt::NormalizeEncoding(text, conv, why, smd::kMaxFileBytes);
    if (!ok) r.err = "SMD: " + why;
    ok = ok && r.Parse(text) && r.Build(out);
    if (!ok) {
        const std::string m = r.err.empty() ? "SMD: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool ReadSmdFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, smd::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadSmdBuffer(bytes, dir, out, err);
}

} // namespace okay
