// Our own MD5 (id Tech 4 / Doom 3: md5mesh + md5anim, text) reader: a joint hierarchy, a skinned mesh and one animation.
// Written from the public description of the format. Bounds-checked, capped, no recursion. See
// okay/Render/ModelData.hpp and docs/own_md5_reader.md.
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace md5 {

using namespace detail::txt;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxJoints    = 4096;
constexpr std::size_t kMaxMeshes    = 4096;
constexpr std::size_t kMaxVerts     = 10000000;   // over all meshes
constexpr std::size_t kMaxTris      = 20000000;
constexpr std::size_t kMaxWeights   = 40000000;
constexpr std::size_t kMaxFrames    = 100000;
constexpr std::size_t kMaxSamples   = 8000000;    // frames x joints
constexpr std::size_t kMaxFloats    = 48000000;   // frames x animated components
constexpr float       kDefaultFps   = 24.0f;

enum class T { End, Word, Str, LParen, RParen, LBrace, RBrace };
struct Tok { T kind = T::End; std::string text; std::size_t line = 1; };

// Tokens: words, "quoted strings", ( ) { }; // starts a comment that runs to the end of the line.
struct Scanner {
    const std::string* t = nullptr;
    std::size_t pos = 0, line = 1;
    explicit Scanner(const std::string& s) : t(&s) {}
    Tok Next() {
        const std::string& s = *t;
        for (;;) {
            while (pos < s.size() && IsWs(s[pos])) { if (s[pos] == '\n') ++line; ++pos; }
            if (pos + 1 < s.size() && s[pos] == '/' && s[pos + 1] == '/') { while (pos < s.size() && s[pos] != '\n' && s[pos] != '\r') ++pos; continue; }
            break;
        }
        Tok k; k.line = line;
        if (pos >= s.size()) return k;
        const char c = s[pos];
        if (c == '(') { ++pos; k.kind = T::LParen; return k; }
        if (c == ')') { ++pos; k.kind = T::RParen; return k; }
        if (c == '{') { ++pos; k.kind = T::LBrace; return k; }
        if (c == '}') { ++pos; k.kind = T::RBrace; return k; }
        if (c == '"') {
            ++pos;
            const std::size_t a = pos;
            while (pos < s.size() && s[pos] != '"' && s[pos] != '\n') ++pos;
            k.text.assign(s, a, pos - a);
            if (pos < s.size() && s[pos] == '"') ++pos;
            k.kind = T::Str;
            return k;
        }
        const std::size_t a = pos;
        while (pos < s.size() && !IsWs(s[pos]) && s[pos] != '(' && s[pos] != ')' && s[pos] != '{' && s[pos] != '}' && s[pos] != '"' &&
               !(s[pos] == '/' && pos + 1 < s.size() && s[pos + 1] == '/')) ++pos;
        k.text.assign(s, a, pos - a);
        k.kind = T::Word;
        return k;
    }
};

struct Joint { std::string name; int parent = -1; Vec3 pos{0, 0, 0}; Quat rot{0, 0, 0, 1}; };
struct Vert { float u = 0, v = 0; int firstW = 0, numW = 0; };
struct Weight { int joint = 0; float bias = 0; Vec3 pos{0, 0, 0}; };
struct MeshDesc { std::string shader; std::vector<Vert> verts; std::vector<std::array<int, 3>> tris; std::vector<Weight> weights; bool hadShader = false; };
struct AnimJoint { std::string name; int parent = -1; int flags = 0; int start = 0; Vec3 bpos{0, 0, 0}; float bq[3] = {0, 0, 0}; };

static Quat QuatFromXyz(float x, float y, float z) {
    const float t = 1.0f - x * x - y * y - z * z;
    const float w = t > 0.0f ? -std::sqrt(t) : 0.0f;
    return Quat{x, y, z, w}.Normalized();
}

struct Reader {
    std::string err;
    bool Fail(const std::string& m, std::size_t line = 0) {
        if (err.empty()) err = "MD5: " + m + (line ? " (line " + std::to_string(line) + ")" : "");
        return false;
    }

    // ---- md5mesh ----
    std::vector<Joint> joints;
    std::vector<MeshDesc> meshes;
    std::size_t totalVerts = 0, totalTris = 0, totalWeights = 0;
    int badTris = 0, emptyMeshes = 0, extraWeights = 0, zeroWeightVerts = 0;
    // ---- md5anim ----
    std::vector<AnimJoint> ajoints;
    std::vector<std::vector<float>> frames;
    float fps = kDefaultFps;
    std::size_t animComponents = 0;
    bool haveMesh = false, haveAnim = false;

    // Reads "(" a b c ")" into three finite floats.
    bool Vec(Scanner& sc, float* out, int n = 3) {
        Tok t = sc.Next();
        if (t.kind != T::LParen) return Fail("expected '('", t.line);
        for (int i = 0; i < n; ++i) {
            t = sc.Next();
            if (t.kind != T::Word || !ToFloat(t.text, out[i])) return Fail("bad number", t.line);
        }
        t = sc.Next();
        if (t.kind != T::RParen) return Fail("expected ')'", t.line);
        return true;
    }
    bool Int(Scanner& sc, long long& v) {
        const Tok t = sc.Next();
        if (t.kind != T::Word || !ToInt(t.text, v)) return Fail("expected a whole number", t.line);
        return true;
    }
    bool Version(Scanner& sc) {
        const Tok t = sc.Next();
        long long v = 0;
        if (t.kind != T::Word || !ToInt(t.text, v)) return Fail("bad MD5Version", t.line);
        if (v != 10) return Fail("unsupported MD5Version " + std::to_string(v) + " (only 10)", t.line);
        return true;
    }

    bool ParseMesh(const std::string& text) {
        Scanner sc(text);
        Tok t = sc.Next();
        if (t.kind != T::Word || t.text != "MD5Version") return Fail("not an md5mesh file (no MD5Version)", t.line);
        if (!Version(sc)) return false;
        long long numJoints = -1, numMeshes = -1;
        bool sawJoints = false;
        for (t = sc.Next(); t.kind != T::End; t = sc.Next()) {
            if (t.kind != T::Word) return Fail("unexpected token", t.line);
            if (t.text == "commandline") { const Tok s = sc.Next(); if (s.kind != T::Str && s.kind != T::Word) return Fail("bad commandline", s.line); continue; }
            if (t.text == "numJoints") {
                if (!Int(sc, numJoints)) return false;
                if (numJoints < 0 || (std::size_t)numJoints > kMaxJoints) return Fail("too many joints", t.line);
                continue;
            }
            if (t.text == "numMeshes") {
                if (!Int(sc, numMeshes)) return false;
                if (numMeshes < 0 || (std::size_t)numMeshes > kMaxMeshes) return Fail("too many meshes", t.line);
                continue;
            }
            if (t.text == "joints") {
                if (sawJoints) return Fail("a second joints block", t.line);
                sawJoints = true;
                if (numJoints < 0) return Fail("joints before numJoints", t.line);
                if (sc.Next().kind != T::LBrace) return Fail("expected '{' after joints", t.line);
                for (long long i = 0; i < numJoints; ++i) {
                    Joint j;
                    Tok n = sc.Next();
                    if (n.kind != T::Str) return Fail("expected a joint name", n.line);
                    j.name = n.text;
                    long long par = 0;
                    if (!Int(sc, par) || par < -1 || par >= numJoints) return Fail("bad joint parent", n.line);
                    j.parent = (int)par;
                    float p[3], q[3];
                    if (!Vec(sc, p) || !Vec(sc, q)) return false;
                    j.pos = Vec3{p[0], p[1], p[2]};
                    j.rot = QuatFromXyz(q[0], q[1], q[2]);
                    joints.push_back(std::move(j));
                }
                if (sc.Next().kind != T::RBrace) return Fail("expected '}' after the joints", t.line);
                continue;
            }
            if (t.text == "mesh") {
                if (sc.Next().kind != T::LBrace) return Fail("expected '{' after mesh", t.line);
                if (meshes.size() >= kMaxMeshes) return Fail("too many meshes", t.line);
                if (!sawJoints) return Fail("a mesh before the joints", t.line);
                if (!ParseOneMesh(sc, text)) return false;
                continue;
            }
            return Fail("unexpected '" + t.text.substr(0, 40) + "'", t.line);
        }
        if (!sawJoints) return Fail("no joints block");
        if (numMeshes >= 0 && (std::size_t)numMeshes != meshes.size()) numMeshesMismatch = (int)meshes.size();
        haveMesh = true;
        return true;
    }
    int numMeshesMismatch = -1;

    bool ParseOneMesh(Scanner& sc, const std::string& text) {
        MeshDesc m;
        long long nv = -1, nt = -1, nw = -1;
        const std::size_t left0 = text.size() - std::min(text.size(), sc.pos);
        for (Tok t = sc.Next();; t = sc.Next()) {
            if (t.kind == T::RBrace) break;
            if (t.kind != T::Word) return Fail("unexpected token in a mesh", t.line);
            if (t.text == "shader") { const Tok s = sc.Next(); if (s.kind != T::Str && s.kind != T::Word) return Fail("bad shader", s.line); m.shader = s.text; m.hadShader = true; continue; }
            if (t.text == "numverts") {
                if (!Int(sc, nv) || nv < 0) return Fail("bad numverts", t.line);
                if (totalVerts + (std::size_t)nv > kMaxVerts) return Fail("too many vertices", t.line);
                if ((std::size_t)nv * 10 > left0) return Fail("numverts is larger than the file", t.line);
                m.verts.assign((std::size_t)nv, Vert{});
                totalVerts += (std::size_t)nv;
                continue;
            }
            if (t.text == "numtris") {
                if (!Int(sc, nt) || nt < 0) return Fail("bad numtris", t.line);
                if (totalTris + (std::size_t)nt > kMaxTris) return Fail("too many triangles", t.line);
                if ((std::size_t)nt * 8 > left0) return Fail("numtris is larger than the file", t.line);
                m.tris.assign((std::size_t)nt, std::array<int, 3>{-1, -1, -1});
                totalTris += (std::size_t)nt;
                continue;
            }
            if (t.text == "numweights") {
                if (!Int(sc, nw) || nw < 0) return Fail("bad numweights", t.line);
                if (totalWeights + (std::size_t)nw > kMaxWeights) return Fail("too many weights", t.line);
                if ((std::size_t)nw * 12 > left0) return Fail("numweights is larger than the file", t.line);
                m.weights.assign((std::size_t)nw, Weight{});
                totalWeights += (std::size_t)nw;
                continue;
            }
            if (t.text == "vert") {
                long long i, ws, wc;
                float uv[2];
                if (nv < 0) return Fail("vert before numverts", t.line);
                if (!Int(sc, i) || i < 0 || i >= nv) return Fail("vertex index out of range", t.line);
                if (!Vec(sc, uv, 2) || !Int(sc, ws) || !Int(sc, wc)) return false;
                if (ws < 0 || wc < 0) return Fail("bad weight range", t.line);
                m.verts[(std::size_t)i] = Vert{uv[0], uv[1], (int)ws, (int)wc};
                continue;
            }
            if (t.text == "tri") {
                long long i, a, b, c;
                if (nt < 0) return Fail("tri before numtris", t.line);
                if (!Int(sc, i) || i < 0 || i >= nt) return Fail("triangle index out of range", t.line);
                if (!Int(sc, a) || !Int(sc, b) || !Int(sc, c)) return false;
                if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) { ++badTris; continue; }
                m.tris[(std::size_t)i] = {(int)a, (int)b, (int)c};
                continue;
            }
            if (t.text == "weight") {
                long long i, j;
                float bias, p[3];
                if (nw < 0) return Fail("weight before numweights", t.line);
                if (!Int(sc, i) || i < 0 || i >= nw) return Fail("weight index out of range", t.line);
                if (!Int(sc, j) || j < 0 || (std::size_t)j >= joints.size()) return Fail("weight joint out of range", t.line);
                const Tok b = sc.Next();
                if (b.kind != T::Word || !ToFloat(b.text, bias)) return Fail("bad weight bias", b.line);
                if (!Vec(sc, p)) return false;
                m.weights[(std::size_t)i] = Weight{(int)j, bias, Vec3{p[0], p[1], p[2]}};
                continue;
            }
            return Fail("unexpected '" + t.text.substr(0, 40) + "' in a mesh", t.line);
        }
        for (const Vert& v : m.verts) {
            if (v.firstW < 0 || v.numW < 0 || (std::size_t)v.firstW + (std::size_t)v.numW > m.weights.size()) return Fail("a vertex's weights are out of range");
        }
        meshes.push_back(std::move(m));
        return true;
    }

    bool ParseAnim(const std::string& text) {
        Scanner sc(text);
        Tok t = sc.Next();
        if (t.kind != T::Word || t.text != "MD5Version") return Fail("not an md5anim file (no MD5Version)", t.line);
        if (!Version(sc)) return false;
        long long numFrames = -1, numJoints = -1, comps = -1;
        bool sawHier = false, sawBase = false;
        std::vector<char> frameSeen;
        for (t = sc.Next(); t.kind != T::End; t = sc.Next()) {
            if (t.kind != T::Word) return Fail("unexpected token", t.line);
            if (t.text == "commandline") { const Tok s = sc.Next(); if (s.kind != T::Str && s.kind != T::Word) return Fail("bad commandline", s.line); continue; }
            if (t.text == "numFrames") { if (!Int(sc, numFrames) || numFrames < 0 || (std::size_t)numFrames > kMaxFrames) return Fail("bad or too many frames", t.line); continue; }
            if (t.text == "numJoints") { if (!Int(sc, numJoints) || numJoints < 0 || (std::size_t)numJoints > kMaxJoints) return Fail("bad or too many joints", t.line); continue; }
            if (t.text == "numAnimatedComponents") { if (!Int(sc, comps) || comps < 0 || (std::size_t)comps > kMaxJoints * 6) return Fail("bad numAnimatedComponents", t.line); continue; }
            if (t.text == "frameRate") {
                const Tok f = sc.Next();
                float r = 0;
                if (f.kind != T::Word || !ToFloat(f.text, r) || !(r > 0.0f) || r > 100000.0f) return Fail("bad frameRate", f.line);
                fps = r;
                continue;
            }
            if (t.text == "hierarchy") {
                if (sawHier) return Fail("a second hierarchy block", t.line);
                sawHier = true;
                if (numJoints < 0) return Fail("hierarchy before numJoints", t.line);
                if (sc.Next().kind != T::LBrace) return Fail("expected '{' after hierarchy", t.line);
                for (long long i = 0; i < numJoints; ++i) {
                    AnimJoint j;
                    const Tok n = sc.Next();
                    if (n.kind != T::Str) return Fail("expected a joint name", n.line);
                    j.name = n.text;
                    long long par, fl, st;
                    if (!Int(sc, par) || !Int(sc, fl) || !Int(sc, st)) return false;
                    if (par < -1 || par >= numJoints) return Fail("bad joint parent", n.line);
                    if (fl < 0 || fl > 63) return Fail("bad joint flags", n.line);
                    if (st < 0) return Fail("bad joint start index", n.line);
                    j.parent = (int)par; j.flags = (int)fl; j.start = (int)st;
                    ajoints.push_back(std::move(j));
                }
                if (sc.Next().kind != T::RBrace) return Fail("expected '}' after the hierarchy", t.line);
                continue;
            }
            if (t.text == "bounds") {   // not needed: skip the block
                if (sc.Next().kind != T::LBrace) return Fail("expected '{' after bounds", t.line);
                for (Tok b = sc.Next(); b.kind != T::RBrace; b = sc.Next()) if (b.kind == T::End || b.kind == T::LBrace) return Fail("bad bounds block", b.line);
                continue;
            }
            if (t.text == "baseframe") {
                if (sawBase) return Fail("a second baseframe block", t.line);
                sawBase = true;
                if (!sawHier) return Fail("baseframe before hierarchy", t.line);
                if (sc.Next().kind != T::LBrace) return Fail("expected '{' after baseframe", t.line);
                for (AnimJoint& j : ajoints) {
                    float p[3], q[3];
                    if (!Vec(sc, p) || !Vec(sc, q)) return false;
                    j.bpos = Vec3{p[0], p[1], p[2]}; j.bq[0] = q[0]; j.bq[1] = q[1]; j.bq[2] = q[2];
                }
                if (sc.Next().kind != T::RBrace) return Fail("expected '}' after the baseframe", t.line);
                continue;
            }
            if (t.text == "frame") {
                if (numFrames < 0 || comps < 0) return Fail("frame before numFrames / numAnimatedComponents", t.line);
                if (frames.empty()) {
                    if ((std::size_t)numFrames * (std::size_t)std::max<long long>(comps, 1) > kMaxFloats) return Fail("frames x components over the limit", t.line);
                    const std::size_t left = text.size() - std::min(text.size(), sc.pos);
                    if ((std::size_t)numFrames * (std::size_t)comps * 2 > left + (std::size_t)numFrames * 16) return Fail("the frame count is larger than the file", t.line);
                    frames.assign((std::size_t)numFrames, std::vector<float>());
                    frameSeen.assign((std::size_t)numFrames, 0);
                }
                long long i;
                if (!Int(sc, i) || i < 0 || i >= numFrames) return Fail("frame index out of range", t.line);
                if (sc.Next().kind != T::LBrace) return Fail("expected '{' after frame", t.line);
                std::vector<float>& fr = frames[(std::size_t)i];
                fr.clear();
                fr.reserve((std::size_t)comps);
                for (long long k = 0; k < comps; ++k) {
                    const Tok v = sc.Next();
                    float f;
                    if (v.kind != T::Word || !ToFloat(v.text, f)) return Fail("bad frame value (frame " + std::to_string(i) + ")", v.line);
                    fr.push_back(f);
                }
                if (sc.Next().kind != T::RBrace) return Fail("expected '}' after the frame values", t.line);
                frameSeen[(std::size_t)i] = 1;
                continue;
            }
            return Fail("unexpected '" + t.text.substr(0, 40) + "'", t.line);
        }
        if (!sawHier || ajoints.empty()) return Fail("the animation has no joints");
        if (!sawBase) return Fail("no baseframe block");
        if (numFrames < 0) return Fail("no numFrames");
        if (ajoints.size() * (std::size_t)numFrames > kMaxSamples) return Fail("frames x joints over the limit");
        animComponents = (std::size_t)std::max<long long>(comps, 0);
        for (const AnimJoint& j : ajoints) {
            int bits = 0; for (int b = 0; b < 6; ++b) if (j.flags & (1 << b)) ++bits;
            if ((std::size_t)j.start + (std::size_t)bits > animComponents) return Fail("a joint's animated components lie outside the frame data");
        }
        for (std::size_t f = 0; f < frames.size(); ++f) if (!frameSeen[f]) missingFrames++;
        haveAnim = true;
        return true;
    }
    int missingFrames = 0;

    // ---- output ----
    bool Build(ImportedModel& out, const std::string& baseDir) {
        ImportedNode root;
        root.name = "MD5";
        root.rotation = Quat::AngleAxis(-90.0f, Vec3{1, 0, 0});   // Z up to Y up: (x, y, z) -> (x, z, -y)
        out.nodes.push_back(root);

        std::vector<std::string> jointName;
        std::vector<int> nodeOfJoint;   // joint index -> node index
        std::unordered_map<std::string, int> nodeByName;
        int dupNames = 0, badParents = 0;
        std::vector<Mat4> invBind;

        auto addSkeleton = [&](const std::vector<std::string>& names, std::vector<int> par, const std::vector<Vec3>& lpos, const std::vector<Quat>& lrot) {
            const std::size_t n = names.size();
            std::vector<int> order;
            badParents += OrderParentsFirst(par, order);
            jointName.assign(n, std::string());
            nodeOfJoint.assign(n, 0);
            std::unordered_map<std::string, int> used;
            used["MD5"] = 1;
            for (int ji : order) {
                std::string nm = names[(std::size_t)ji].empty() ? "joint" + std::to_string(ji) : names[(std::size_t)ji];
                int& u = used[nm];
                if (u++) { nm += "_" + std::to_string(u - 1); ++dupNames; used[nm] = 1; }
                jointName[(std::size_t)ji] = nm;
                ImportedNode nd;
                nd.name = nm;
                nd.parent = par[(std::size_t)ji] < 0 ? 0 : nodeOfJoint[(std::size_t)par[(std::size_t)ji]];
                nd.position = lpos[(std::size_t)ji];
                nd.rotation = lrot[(std::size_t)ji];
                nodeOfJoint[(std::size_t)ji] = (int)out.nodes.size();
                nodeByName[nm] = (int)out.nodes.size();
                out.nodes.push_back(std::move(nd));
            }
        };

        if (haveMesh) {
            const std::size_t n = joints.size();
            std::vector<std::string> names(n);
            std::vector<int> par(n);
            std::vector<Vec3> lpos(n);
            std::vector<Quat> lrot(n);
            // md5mesh joints are in model space; the node's own transform is relative to its parent
            for (std::size_t i = 0; i < n; ++i) { names[i] = joints[i].name; par[i] = joints[i].parent; if (par[i] == (int)i) par[i] = -1; }
            // cut loops first so the local transforms are computed against the parent that will be used
            { std::vector<int> o; std::vector<int> pc = par; OrderParentsFirst(pc, o); par = pc; }
            for (std::size_t i = 0; i < n; ++i) {
                if (par[i] < 0) { lpos[i] = joints[i].pos; lrot[i] = joints[i].rot; continue; }
                const Joint& pj = joints[(std::size_t)par[i]];
                const Quat inv = pj.rot.Conjugate();
                lrot[i] = (inv * joints[i].rot).Normalized();
                lpos[i] = inv * (joints[i].pos - pj.pos);
            }
            addSkeleton(names, par, lpos, lrot);
            invBind.resize(n);
            for (std::size_t i = 0; i < n; ++i) invBind[i] = Mat4::TRS(joints[i].pos, joints[i].rot, Vec3{1, 1, 1}).Inverse();
        } else {
            const std::size_t n = ajoints.size();
            std::vector<std::string> names(n);
            std::vector<int> par(n);
            std::vector<Vec3> lpos(n);
            std::vector<Quat> lrot(n);
            for (std::size_t i = 0; i < n; ++i) { names[i] = ajoints[i].name; par[i] = ajoints[i].parent == (int)i ? -1 : ajoints[i].parent; lpos[i] = ajoints[i].bpos; lrot[i] = QuatFromXyz(ajoints[i].bq[0], ajoints[i].bq[1], ajoints[i].bq[2]); }
            addSkeleton(names, par, lpos, lrot);
        }

        // meshes: one part per mesh, vertices at the bind pose, up to four joint weights per vertex
        if (haveMesh) {
            std::vector<ImportedBone> allBones(joints.size());
            for (std::size_t i = 0; i < joints.size(); ++i) { allBones[i].name = jointName[i]; allBones[i].inverseBind = invBind[i]; }
            for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
                const MeshDesc& m = meshes[mi];
                if (m.verts.empty() || m.tris.empty()) { ++emptyMeshes; continue; }
                ImportedPart part;
                part.mesh.name = m.shader.empty() ? "mesh" + std::to_string(mi) : m.shader;
                part.node = 0;
                part.bones = allBones;
                for (const Vert& v : m.verts) {
                    Vec3 p{0, 0, 0};
                    float sum = 0;
                    for (int w = v.firstW; w < v.firstW + v.numW; ++w) sum += m.weights[(std::size_t)w].bias;
                    std::array<int, 4> ji{0, 0, 0, 0};
                    std::array<float, 4> jw{0, 0, 0, 0};
                    if (!(sum > 0.0f)) { ++zeroWeightVerts; }
                    else {
                        int cnt = 0;
                        for (int w = v.firstW; w < v.firstW + v.numW; ++w) {
                            const Weight& wt = m.weights[(std::size_t)w];
                            const Joint& jt = joints[(std::size_t)wt.joint];
                            p += (jt.pos + jt.rot * wt.pos) * (wt.bias / sum);
                            if (wt.bias <= 0.0f) continue;
                            // keep the four strongest joints (a joint listed twice adds up)
                            int slot = -1;
                            for (int q = 0; q < cnt; ++q) if (ji[(std::size_t)q] == wt.joint) slot = q;
                            if (slot >= 0) { jw[(std::size_t)slot] += wt.bias; continue; }
                            if (cnt < 4) { ji[(std::size_t)cnt] = wt.joint; jw[(std::size_t)cnt] = wt.bias; ++cnt; continue; }
                            int mn = 0; for (int q = 1; q < 4; ++q) if (jw[(std::size_t)q] < jw[(std::size_t)mn]) mn = q;
                            ++extraWeights;
                            if (wt.bias > jw[(std::size_t)mn]) { ji[(std::size_t)mn] = wt.joint; jw[(std::size_t)mn] = wt.bias; }
                        }
                        float s2 = 0; for (float w : jw) s2 += w;
                        if (s2 > 0) for (float& w : jw) w /= s2;
                    }
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::fabs(p.x) > 1e30f || std::fabs(p.y) > 1e30f || std::fabs(p.z) > 1e30f) return Fail("vertex positions overflow");
                    part.mesh.vertices.push_back(p);
                    part.mesh.uvs.push_back(Vec2{v.u, v.v});
                    part.jointIdx.push_back(ji);
                    part.jointWt.push_back(jw);
                }
                for (const auto& t : m.tris) {
                    if (t[0] < 0) continue;   // a slot the file never filled (or an out-of-range triangle)
                    if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) { ++degenerateTris; continue; }
                    // id Tech 4 winds clockwise: reverse to counter clockwise
                    part.mesh.triangles.push_back(t[2]); part.mesh.triangles.push_back(t[1]); part.mesh.triangles.push_back(t[0]);
                }
                if (part.mesh.triangles.empty()) { ++emptyMeshes; continue; }
                part.mesh.ComputeSmoothNormals();
                ImportedMaterial mat;
                mat.name = m.shader;
                const std::string lo = Lower(m.shader);
                const bool image = lo.size() > 4 && (lo.compare(lo.size() - 4, 4, ".tga") == 0 || lo.compare(lo.size() - 4, 4, ".png") == 0 || lo.compare(lo.size() - 4, 4, ".jpg") == 0 ||
                                                     lo.compare(lo.size() - 4, 4, ".bmp") == 0 || lo.compare(lo.size() - 4, 4, ".dds") == 0);
                if (image) mat.texture = (!baseDir.empty() && m.shader.find_first_of("/\\") == std::string::npos) ? baseDir + m.shader : m.shader;
                else if (!m.shader.empty()) ++shaderNames;
                part.material = (int)out.materials.size();
                out.materials.push_back(std::move(mat));
                out.parts.push_back(std::move(part));
            }
        }

        // animation
        if (haveAnim && !frames.empty()) {
            ImportedClip clip;
            clip.name = "animation";
            std::vector<ImportedChannel> ch(ajoints.size());
            std::vector<int> target(ajoints.size(), -1);
            int unmatched = 0;
            for (std::size_t j = 0; j < ajoints.size(); ++j) {
                auto it = nodeByName.find(ajoints[j].name);
                if (haveMesh) {
                    if (it == nodeByName.end()) { ++unmatched; continue; }
                    target[j] = it->second;
                    ch[j].node = out.nodes[(std::size_t)it->second].name;
                } else {
                    // anim only: the nodes were made from this hierarchy (names may have been made unique)
                    target[j] = nodeOfJoint[j];
                    ch[j].node = jointName[j];
                }
            }
            if (unmatched) animUnmatched = unmatched;
            if (haveMesh && ajoints.size() != joints.size()) animJointMismatch = (int)ajoints.size();
            for (std::size_t f = 0; f < frames.size(); ++f) {
                if (frames[f].size() != animComponents) continue;   // a frame the file never gave
                const float time = (float)((double)f / (double)fps);
                for (std::size_t j = 0; j < ajoints.size(); ++j) {
                    if (target[j] < 0) continue;
                    const AnimJoint& aj = ajoints[j];
                    const float* fp = frames[f].data() + aj.start;
                    Vec3 p = aj.bpos;
                    float q[3] = {aj.bq[0], aj.bq[1], aj.bq[2]};
                    int k = 0;
                    if (aj.flags & 1) p.x = fp[k++];
                    if (aj.flags & 2) p.y = fp[k++];
                    if (aj.flags & 4) p.z = fp[k++];
                    if (aj.flags & 8) q[0] = fp[k++];
                    if (aj.flags & 16) q[1] = fp[k++];
                    if (aj.flags & 32) q[2] = fp[k++];
                    if (std::fabs(p.x) > 1e30f || std::fabs(p.y) > 1e30f || std::fabs(p.z) > 1e30f) return Fail("animation values overflow");
                    ch[j].position.push_back({time, p});
                    ch[j].rotation.push_back({time, QuatFromXyz(q[0], q[1], q[2])});
                }
            }
            for (ImportedChannel& c : ch) if (!c.position.empty()) clip.channels.push_back(std::move(c));
            if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        }
        if (out.parts.empty() && out.clips.empty() && out.nodes.size() <= 1) return Fail("the file has no joints, mesh or animation");

        auto note = [&](const std::string& s) { out.notes.push_back("MD5: " + s); };
        note("the root node turns Z-up into Y-up; units are kept; triangle winding was reversed (id Tech 4 is clockwise); texture coordinates are kept as written (top-left origin, like the engine)");
        if (!out.parts.empty()) note("vertex normals are not in the file: smooth normals were generated");
        if (shaderNames) note(std::to_string(shaderNames) + " shader name(s) are material declarations, not image files: no texture is set (nothing is guessed)");
        if (emptyMeshes) note(std::to_string(emptyMeshes) + " mesh(es) without vertices or triangles skipped");
        if (badTris) note(std::to_string(badTris) + " triangle(s) with an out-of-range vertex index skipped");
        if (degenerateTris) note(std::to_string(degenerateTris) + " degenerate triangle(s) skipped");
        if (zeroWeightVerts) note(std::to_string(zeroWeightVerts) + " vertex(es) whose weights sum to zero sit at the origin");
        if (extraWeights) note(std::to_string(extraWeights) + " vertex weight(s) beyond four per vertex dropped (the four strongest are kept)");
        if (numMeshesMismatch >= 0) note("numMeshes does not match the number of mesh blocks (" + std::to_string(numMeshesMismatch) + " found)");
        if (badParents) note(std::to_string(badParents) + " joint(s) in a parent loop were cut loose to the root");
        if (dupNames) note(std::to_string(dupNames) + " joint name(s) were repeated and got a numeric suffix");
        if (animUnmatched) note(std::to_string(animUnmatched) + " animated joint(s) are not in the mesh: their keys were dropped");
        if (animJointMismatch >= 0) note("the animation has " + std::to_string(animJointMismatch) + " joints, the mesh " + std::to_string(joints.size()));
        if (missingFrames) note(std::to_string(missingFrames) + " frame(s) announced but not in the file were skipped");
        if (haveAnim && !out.clips.empty()) note("the animation plays at " + std::to_string((int)fps) + " frames per second (frameRate in the file)");
        return true;
    }
    int degenerateTris = 0, shaderNames = 0, animUnmatched = 0, animJointMismatch = -1;
};

} // namespace md5
} // namespace

bool ReadMd5Buffer(const std::string& meshText, const std::string& animText, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    md5::Reader r;
    bool ok = true;
    std::string mesh = meshText, anim = animText, why;
    bool conv = false;
    if (mesh.empty() && anim.empty()) { r.err = "MD5: empty file"; ok = false; }
    if (ok && !mesh.empty() && !detail::txt::NormalizeEncoding(mesh, conv, why, md5::kMaxFileBytes)) { r.err = "MD5: " + why; ok = false; }
    if (ok && !anim.empty() && !detail::txt::NormalizeEncoding(anim, conv, why, md5::kMaxFileBytes)) { r.err = "MD5: " + why; ok = false; }
    if (ok && !mesh.empty()) ok = r.ParseMesh(mesh);
    std::string animProblem;
    if (ok && !anim.empty()) {
        md5::Reader a;
        if (a.ParseAnim(anim)) { r.ajoints = std::move(a.ajoints); r.frames = std::move(a.frames); r.fps = a.fps; r.animComponents = a.animComponents; r.haveAnim = true; r.missingFrames = a.missingFrames; }
        else if (mesh.empty()) { r.err = a.err; ok = false; }
        else animProblem = a.err;   // the mesh is still good: say what was wrong with the animation
    }
    ok = ok && r.Build(out, baseDir);
    if (!ok) {
        const std::string m = r.err.empty() ? "MD5: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    if (!animProblem.empty()) out.notes.push_back("MD5: the animation was not read: " + animProblem);
    out.backend = "own";
    return true;
}

bool ReadMd5File(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string dir, base = path;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    const std::size_t dot = path.find_last_of('.');
    std::string ext;
    if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) { ext = detail::txt::Lower(path.substr(dot)); base = path.substr(0, dot); }
    std::string a, b;
    if (ext == ".md5anim") {
        if (!detail::txt::ReadFileCapped(path, md5::kMaxFileBytes, b, err)) return false;
        return ReadMd5Buffer(std::string(), b, dir, out, err);
    }
    if (!detail::txt::ReadFileCapped(path, md5::kMaxFileBytes, a, err)) return false;
    // the animation next to it, same name: optional
    std::string tmp, e2;
    const char* exts[] = {".md5anim", ".MD5ANIM"};
    for (const char* x : exts) { if (detail::txt::ReadFileCapped(base + x, md5::kMaxFileBytes, tmp, &e2)) { b = std::move(tmp); break; } }
    return ReadMd5Buffer(a, b, dir, out, err);
}

} // namespace okay
