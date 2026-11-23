// The Blender (.blend) reader's build stage: nodes, parts, modifiers, animation and the notes (a pure move out of BlendReader.cpp).
#include "BlendReaderInternal.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "BlendModifiers.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {
namespace blnd {
// ---- helpers for parts -----------------------------------------------------------------------
static Vec3 Cross3(const Vec3& a, const Vec3& b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Triangles (local corner indices, 3 per triangle) of one polygon.
static void TriangulatePoly(const std::vector<Vec3>& pos, const int* lv, int n, std::vector<int>& tri, int& fanned, long long& budget) {
    tri.clear();
    if (n == 3) { tri = {0, 1, 2}; return; }
    // Newell normal
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < n; ++i) {
        const Vec3& a = pos[(std::size_t)lv[i]]; const Vec3& b = pos[(std::size_t)lv[(i + 1) % n]];
        nx += ((double)a.y - b.y) * ((double)a.z + b.z); ny += ((double)a.z - b.z) * ((double)a.x + b.x); nz += ((double)a.x - b.x) * ((double)a.y + b.y);
    }
    if (n == 4) {
        // split along the diagonal that keeps both triangles facing the polygon's normal
        auto side = [&](int i, int j, int k) {
            const Vec3 e1 = pos[(std::size_t)lv[j]] - pos[(std::size_t)lv[i]], e2 = pos[(std::size_t)lv[k]] - pos[(std::size_t)lv[i]];
            const Vec3 c = Cross3(e1, e2);
            return (double)c.x * nx + (double)c.y * ny + (double)c.z * nz;
        };
        const bool ok02 = side(0, 1, 2) > 0 && side(0, 2, 3) > 0;
        const bool ok13 = side(1, 2, 3) > 0 && side(1, 3, 0) > 0;
        if (!ok02 && ok13) tri = {1, 2, 3, 1, 3, 0}; else tri = {0, 1, 2, 0, 2, 3};
        return;
    }
    if (n > kMaxNgon || budget <= 0) { for (int i = 1; i + 1 < n; ++i) { tri.push_back(0); tri.push_back(i); tri.push_back(i + 1); } ++fanned; return; }
    const double ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
    const int drop = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
    auto P = [&](int i, double& u, double& v) {
        const Vec3& p = pos[(std::size_t)lv[i]];
        if (drop == 0) { u = p.y; v = p.z; } else if (drop == 1) { u = p.z; v = p.x; } else { u = p.x; v = p.y; }
    };
    const double nd = drop == 0 ? nx : (drop == 1 ? ny : nz);
    const double orient = nd >= 0 ? 1.0 : -1.0;
    std::vector<int> idx((std::size_t)n);
    for (int i = 0; i < n; ++i) idx[(std::size_t)i] = i;
    int guard = 0;
    while (idx.size() > 3 && guard++ < n * n + 8) {
        bool clipped = false;
        const int m = (int)idx.size();
        for (int i = 0; i < m && !clipped; ++i) {
            const int a = idx[(std::size_t)((i + m - 1) % m)], b = idx[(std::size_t)i], c = idx[(std::size_t)((i + 1) % m)];
            double au, av, bu, bv, cu, cv;
            P(a, au, av); P(b, bu, bv); P(c, cu, cv);
            const double cr = ((bu - au) * (cv - bv) - (bv - av) * (cu - bu)) * orient;
            if (!(cr > 1e-18)) continue;   // reflex or degenerate corner
            bool inside = false;
            budget -= m;
            for (int j = 0; j < m && !inside; ++j) {
                const int q = idx[(std::size_t)j];
                if (q == a || q == b || q == c) continue;
                double qu, qv; P(q, qu, qv);
                const double d1 = ((bu - au) * (qv - av) - (bv - av) * (qu - au)) * orient;
                const double d2 = ((cu - bu) * (qv - bv) - (cv - bv) * (qu - bu)) * orient;
                const double d3 = ((au - cu) * (qv - cv) - (av - cv) * (qu - cu)) * orient;
                if (d1 >= 0 && d2 >= 0 && d3 >= 0) inside = true;
            }
            if (inside) continue;
            tri.push_back(a); tri.push_back(b); tri.push_back(c);
            idx.erase(idx.begin() + i);
            clipped = true;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) { tri.push_back(idx[0]); tri.push_back(idx[1]); tri.push_back(idx[2]); }
    else if (idx.size() > 3) { for (std::size_t i = 1; i + 1 < idx.size(); ++i) { tri.push_back(idx[0]); tri.push_back(idx[i]); tri.push_back(idx[i + 1]); } ++fanned; }
}

static void ComputeCornerNormals(MeshData& md) {
    if (md.nDone) return;
    md.nDone = true;
    const std::size_t nl = md.loopVert.size(), nf = md.faceStart.size();
    md.cornerN.assign(nl, Vec3{0, 0, 1});
    std::vector<Vec3> fn(nf), vn(md.pos.size(), Vec3{0, 0, 0});
    for (std::size_t f = 0; f < nf; ++f) {
        const int n = md.faceCount[f], s = md.faceStart[f];
        double nx = 0, ny = 0, nz = 0;
        for (int i = 0; i < n; ++i) {
            const Vec3& a = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + i)]]; const Vec3& b = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + (i + 1) % n)]];
            nx += ((double)a.y - b.y) * ((double)a.z + b.z); ny += ((double)a.z - b.z) * ((double)a.x + b.x); nz += ((double)a.x - b.x) * ((double)a.y + b.y);
        }
        const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
        fn[f] = l > 1e-30 ? Vec3{(float)(nx / l), (float)(ny / l), (float)(nz / l)} : Vec3{0, 0, 1};
        if (!md.faceSmooth[f]) continue;
        for (int i = 0; i < n; ++i) {
            const Vec3& c = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + i)]];
            const Vec3 e1 = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + (i + n - 1) % n)]] - c, e2 = md.pos[(std::size_t)md.loopVert[(std::size_t)(s + (i + 1) % n)]] - c;
            const float l1 = e1.Magnitude(), l2 = e2.Magnitude();
            float ang = 0.0f;
            if (l1 > 1e-20f && l2 > 1e-20f) ang = std::acos(std::max(-1.0f, std::min(1.0f, Vec3::Dot(e1, e2) / (l1 * l2))));
            vn[(std::size_t)md.loopVert[(std::size_t)(s + i)]] += fn[f] * ang;
        }
    }
    for (std::size_t f = 0; f < nf; ++f)
        for (int i = 0; i < md.faceCount[f]; ++i) {
            const std::size_t li = (std::size_t)(md.faceStart[f] + i);
            Vec3 n = fn[f];
            if (md.faceSmooth[f]) { const Vec3& v = vn[(std::size_t)md.loopVert[li]]; const float l = v.Magnitude(); if (l > 1e-12f) n = v * (1.0f / l); }
            md.cornerN[li] = n;
        }
}

static std::uint32_t FBits(float v) { std::uint32_t u; std::memcpy(&u, &v, 4); return u; }

// ---- Run -------------------------------------------------------------------------------------
bool Reader::Run(ImportedModel& model) {
    out = &model;
    if (!f.ParseBlocks()) return Fail(f.err);
    if (f.structs.empty()) return Fail("Blend: empty SDNA");

    // objects
    for (const BlockInfo& b : f.blocks) {
        if (std::memcmp(b.code, "OB", 3) != 0) continue;
        if (objs.size() >= kMaxObjects) { ++cntTruncList; break; }
        Ref ob = At(b.old, "Object");
        if (!ob) { ++cntBadObj; continue; }
        ObjRec o; o.ob = ob; o.addr = b.old; o.name = IdName(ob); o.type = ob.Int("type");
        objByAddr[b.old] = (int)objs.size();
        objs.push_back(std::move(o));
    }
    // the scene: FileGlobal.curscene, else the first SC block
    {
        for (const BlockInfo& b : f.blocks) {
            if (std::memcmp(b.code, "GLOB", 4) != 0) continue;
            Ref g = At(b.old, "FileGlobal");
            if (g) scene = g.Deref("curscene", "Scene");
            break;
        }
        if (!scene) for (const BlockInfo& b : f.blocks) if (std::memcmp(b.code, "SC", 3) == 0) { scene = At(b.old, "Scene"); if (scene) break; }
        if (scene) {
            Ref r = scene.Sub("r");
            const double fs = r ? r.Num("frs_sec", 0, 24.0) : 24.0, fb = r ? r.Num("frs_sec_base", 0, 1.0) : 1.0;
            if (fs > 0 && fb > 0 && fs / fb <= 1000.0 && fs / fb >= 0.001) fps = fs / fb;
            Ref unit = scene.Sub("unit");
            if (unit && unit.Has("scale_length") && std::fabs(unit.Num("scale_length", 0, 1.0) - 1.0) > 1e-6) ++cntUnitScale;
        }
    }
    {
        std::unordered_set<std::uint64_t> set;
        if (scene) {
            std::vector<Ref> bases;
            if (scene.List("base", "Base", bases, kMaxList)) for (const Ref& b : bases) set.insert(b.Ptr("object"));
            std::unordered_set<std::uint64_t> seen;
            CollectCollection(scene.Deref("master_collection", "Collection"), seen, set, 0);
        }
        set.erase(0);
        bool any = false;
        for (ObjRec& o : objs) if (set.count(o.addr)) { o.include = true; any = true; }
        if (!any) { for (ObjRec& o : objs) o.include = true; ++cntNoScene; }
        for (std::size_t i = 0; i < objs.size(); ++i) {   // parents come along even when they are not in the scene
            const std::uint64_t pa = objs[i].ob.Ptr("parent");
            auto it = objByAddr.find(pa);
            if (it != objByAddr.end()) objs[i].parent = it->second;
        }
        for (std::size_t i = 0; i < objs.size(); ++i) {
            if (!objs[i].include) continue;
            int p = objs[i].parent, d = 0;
            while (p >= 0 && d++ < kMaxDepth) { objs[(std::size_t)p].include = true; p = objs[(std::size_t)p].parent; }
        }
    }
    int included = 0;
    for (const ObjRec& o : objs) included += o.include ? 1 : 0;
    if (included == 0) return Fail("Blend: the file has no objects");
    BuildNodes();
    BuildParts();
    if (!flatten) BuildAnimation();
    for (const ImportedPart& p : model.parts) {
        for (const Vec3& v : p.mesh.vertices) if (!InRange(v)) return Fail("Blend: coordinates overflow");
        for (const Vec3& v : p.mesh.normals) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return Fail("Blend: coordinates overflow");
    }
    for (const ImportedNode& nd : model.nodes)
        if (!InRange(nd.position) || !InRange(nd.scale) || !std::isfinite(nd.rotation.x + nd.rotation.y + nd.rotation.z + nd.rotation.w)) return Fail("Blend: transform overflow");
    Notes();
    return true;
}

void Reader::BuildNodes() {
    ImportedModel& m = *out;
    const Mat4 up = UpMatrix();
    ImportedNode root;
    root.name = UniqueNodeName("RootNode");
    up.Decompose(root.position, root.rotation, root.scale);
    m.nodes.assign(1, root);

    auto addNode = [&](const std::string& name, int parent, const Mat4& local) {
        ImportedNode nd;
        nd.name = UniqueNodeName(name);
        nd.parent = parent;
        if (!MatFinite(local)) { ++cntBadObj; }
        else {
            local.Decompose(nd.position, nd.rotation, nd.scale);
            const Mat4 re = Mat4::TRS(nd.position, nd.rotation, nd.scale);
            float worst = 0.0f, mag = 1.0f;
            for (int i = 0; i < 16; ++i) { worst = std::max(worst, std::fabs(re.m[i] - local.m[i])); mag = std::max(mag, std::fabs(local.m[i])); }
            if (worst > 1e-3f * mag) ++cntSheared;
        }
        m.nodes.push_back(nd);
        return (int)m.nodes.size() - 1;
    };

    // parent first order
    std::vector<std::vector<int>> kids(objs.size());
    std::vector<int> rootsList;
    for (std::size_t i = 0; i < objs.size(); ++i) {
        if (!objs[i].include) continue;
        int p = objs[i].parent, d = 0; bool cyc = false;
        while (p >= 0 && d++ <= kMaxDepth + 1) { if (p == (int)i) { cyc = true; break; } p = objs[(std::size_t)p].parent; }
        if (cyc || d > kMaxDepth) { ++cntParentCycle; objs[i].parent = -1; }
        if (objs[i].parent >= 0) kids[(std::size_t)objs[i].parent].push_back((int)i); else rootsList.push_back((int)i);
    }
    std::vector<int> stack;
    for (std::size_t i = rootsList.size(); i-- > 0;) stack.push_back(rootsList[i]);
    std::size_t visited = 0;
    while (!stack.empty()) {
        const int oi = stack.back(); stack.pop_back();
        ObjRec& o = objs[(std::size_t)oi];
        if (++visited > objs.size() + 1) break;
        const Ref& ob = o.ob;
        double loc[3], dloc[3] = {0, 0, 0}, sz[3] = {1, 1, 1}, dsc[3] = {1, 1, 1}, eul[3] = {0, 0, 0}, deul[3] = {0, 0, 0}, quat[4] = {1, 0, 0, 0}, ax[3] = {0, 1, 0};
        for (int k = 0; k < 3; ++k) { loc[k] = ob.Num("loc", (std::size_t)k); dloc[k] = ob.Num("dloc", (std::size_t)k); sz[k] = ob.Num("size", (std::size_t)k, 1.0); eul[k] = ob.Num("rot", (std::size_t)k); deul[k] = ob.Num("drot", (std::size_t)k); ax[k] = ob.Num("rotAxis", (std::size_t)k, k == 1 ? 1.0 : 0.0); }
        if (ob.Has("dscale")) { for (int k = 0; k < 3; ++k) dsc[k] = ob.Num("dscale", (std::size_t)k, 1.0); }
        else if (ob.Has("dsize")) { for (int k = 0; k < 3; ++k) sz[k] += ob.Num("dsize", (std::size_t)k); }
        for (int k = 0; k < 4; ++k) quat[k] = ob.Num("quat", (std::size_t)k, k == 0 ? 1.0 : 0.0);
        const int rotmode = ob.Has("rotmode") ? ob.Int("rotmode") : 1;
        for (int k = 0; k < 3; ++k) eul[k] += deul[k];
        if (rotmode <= 0) { bool dd = false; for (int k = 0; k < 3; ++k) if (std::fabs(deul[k]) > 1e-9) dd = true; if (dd) ++cntDeltaRot; }
        const Mat4 basis = TransMat(loc[0] + dloc[0], loc[1] + dloc[1], loc[2] + dloc[2]) * RotationOf(rotmode, eul, quat, ax, ob.Num("rotAngle")) * ScaleMat(sz[0] * dsc[0], sz[1] * dsc[1], sz[2] * dsc[2]);

        Mat4 pinv;
        { float t[16]; if (ob.ReadFloats("parentinv", t, 16)) for (int k = 0; k < 16; ++k) pinv.m[k] = t[k]; }
        if (!MatFinite(pinv)) pinv = Mat4();
        int parentNode = 0;
        Mat4 parentWorld, pre;
        bool haveParent = false;
        if (o.parent >= 0 && objs[(std::size_t)o.parent].placed) {
            const ObjRec& po = objs[(std::size_t)o.parent];
            const int ptype = ob.Int("partype") & 15;
            parentNode = po.node; parentWorld = po.world; haveParent = true;
            pre = pinv;
            if (ptype == 7 && po.armIdx >= 0) {   // parented to a bone: the parent matrix sits at the bone's tail
                const ArmRec& ar = arms[(std::size_t)po.armIdx];
                auto bi = ar.byName.find(ob.Str("parsubstr", 64));
                if (bi != ar.byName.end()) {
                    const BoneRec& br = ar.bones[(std::size_t)bi->second];
                    parentNode = br.node;
                    parentWorld = po.world * br.pose * TransMat(0, br.length, 0);
                    pre = TransMat(0, br.length, 0) * pinv;
                    ++cntBoneParent;
                } else ++cntNoBone;
            } else if (ptype == 1 || ptype == 2 || ptype == 5 || ptype == 6) ++cntVertexParent;
        }
        o.pre = pre;
        const Mat4 local = pre * basis;
        o.world = haveParent ? parentWorld * pinv * basis : basis;   // (the tail offset of a bone parent is part of parentWorld)
        o.node = addNode(o.name, parentNode, local);
        o.placed = true;

        // does the saved world matrix agree with what we computed?
        {
            float t[16];
            if (ob.ReadFloats("obmat", t, 16)) {
                bool nonzero = false; for (int k = 0; k < 16; ++k) if (t[k] != 0.0f) nonzero = true;
                if (nonzero && MatFinite(o.world)) {
                    float worst = 0, mag = 1;
                    for (int k = 0; k < 16; ++k) { if (!std::isfinite(t[k])) { worst = 1e30f; break; } worst = std::max(worst, std::fabs(t[k] - o.world.m[k])); mag = std::max(mag, std::fabs(t[k])); }
                    if (worst > 2e-3f * mag) ++cntMatMismatch;
                }
            }
        }
        if ((ob.Has("constraints") && ob.Sub("constraints").Ptr("first")) || ob.Ptr("track")) ++cntConstraints;
        if (ob.Has("modifiers")) {
            const Ref mb = ob.Sub("modifiers");
            std::uint64_t a = mb ? mb.Ptr("first") : 0;
            for (int guard = 0; a && guard < 1000; ++guard) {
                Span s = f.At(a);
                if (!s.p || s.st < 0) break;
                if (o.type != OB_MESH && f.structs[(std::size_t)s.st].name != "ArmatureModifierData") ++cntModifiers;
                if (s.n < (std::size_t)f.ps) break;
                a = f.PtrAt(s.p);
            }
        }
        if (ob.Ptr("dup_group") || ob.Ptr("instance_collection")) ++cntDupli;
        switch (o.type) {
            case 0: case 1: case 25: break;
            case 10: ++cntLamps; break;
            case 11: ++cntCameras; break;
            default: ++cntOtherTypes; break;
        }
        if (ob.Has("adt")) {
            Ref ad = ob.Deref("adt", "AnimData");
            if (ad) { if (ad.Has("nla_tracks") && ad.Sub("nla_tracks").Ptr("first")) ++cntNla; if (ad.Has("drivers") && ad.Sub("drivers").Ptr("first")) ++cntDrivers; }
        }
        if (ob.Ptr("ipo")) ++cntIpo;

        // armature: the bones become nodes under the object
        if (o.type == OB_ARMATURE && ob.Ptr("data")) {
            const int ai = ReadArmature(ob.Ptr("data"));
            ArmRec& ar = arms[(std::size_t)ai];
            if (ar.ok) {
                o.armIdx = ai;
                if (ar.obj < 0) ar.obj = oi;
                Ref pose = ob.Deref("pose", "bPose");   // the stored pose of the saved frame
                std::vector<Ref> chans;
                if (pose) pose.List("chanbase", "bPoseChannel", chans, kMaxBones);
                for (const Ref& pc : chans) {
                    auto bi = ar.byName.find(pc.Str("name", 64));
                    if (bi == ar.byName.end()) continue;
                    BoneRec& br = ar.bones[(std::size_t)bi->second];
                    for (int k = 0; k < 3; ++k) { br.loc[k] = pc.Num("loc", (std::size_t)k); br.size[k] = pc.Num("size", (std::size_t)k, 1.0); br.eul[k] = pc.Num("eul", (std::size_t)k); br.axis[k] = pc.Num("rotAxis", (std::size_t)k, k == 1 ? 1.0 : 0.0); }
                    for (int k = 0; k < 4; ++k) br.quat[k] = pc.Num("quat", (std::size_t)k, k == 0 ? 1.0 : 0.0);
                    br.angle = pc.Num("rotAngle");
                    if (pc.Has("constraints") && pc.Sub("constraints").Ptr("first")) ++cntBoneConstraints;
                    br.rotmode = pc.Has("rotmode") ? pc.Int("rotmode") : 1;
                }
                for (std::size_t bi = 0; bi < ar.bones.size(); ++bi) {
                    BoneRec& br = ar.bones[bi];
                    const Mat4 basis2 = TransMat(br.loc[0], br.loc[1], br.loc[2]) * RotationOf(br.rotmode, br.eul, br.quat, br.axis, br.angle) * ScaleMat(br.size[0], br.size[1], br.size[2]);
                    const Mat4 loc2 = br.restLocal * basis2;
                    br.pose = br.parent >= 0 ? ar.bones[(std::size_t)br.parent].pose * loc2 : loc2;
                    br.node = addNode(br.name, br.parent >= 0 ? ar.bones[(std::size_t)br.parent].node : o.node, loc2);
                    br.name = m.nodes[(std::size_t)br.node].name;   // the node's (unique) name is the joint's name
                }
            }
        }
        for (std::size_t i = kids[(std::size_t)oi].size(); i-- > 0;) stack.push_back(kids[(std::size_t)oi][i]);
    }
}

void Reader::BuildParts() {
    for (std::size_t oi = 0; oi < objs.size(); ++oi) {
        ObjRec& o = objs[oi];
        if (!o.include || !o.placed || o.type != OB_MESH) continue;
        BuildPartsOf(o, (int)oi);
        if (out->parts.size() > kMaxParts) { ++cntTruncList; break; }
    }
    if (!flatten) return;
    // flattened: one node; bake the Z-up -> Y-up rotation and each object's world matrix into the vertices
    const Mat4 up = UpMatrix();
    std::vector<int> nodeObj(out->nodes.size(), -1);
    for (std::size_t i = 0; i < objs.size(); ++i) if (objs[i].node >= 0) nodeObj[(std::size_t)objs[i].node] = (int)i;
    for (ImportedPart& p : out->parts) {
        const int oi = p.node >= 0 && (std::size_t)p.node < nodeObj.size() ? nodeObj[(std::size_t)p.node] : -1;
        const Mat4 w = oi >= 0 ? up * objs[(std::size_t)oi].world : up;
        Mat4 wi = w.Inverse();
        if (!MatFinite(wi)) wi = Mat4();   // a zero scale: normals stay as they are
        Mat4 nm;   // inverse transpose
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) nm.at(c, r) = wi.at(r, c);
        for (Vec3& v : p.mesh.vertices) v = Xform(w, v);
        for (Vec3& v : p.mesh.normals) v = XformDir(nm, v).Normalized();
        const float det = w.at(0, 0) * (w.at(1, 1) * w.at(2, 2) - w.at(2, 1) * w.at(1, 2)) - w.at(1, 0) * (w.at(0, 1) * w.at(2, 2) - w.at(2, 1) * w.at(0, 2)) + w.at(2, 0) * (w.at(0, 1) * w.at(1, 2) - w.at(1, 1) * w.at(0, 2));
        if (det < 0) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) std::swap(p.mesh.triangles[i + 1], p.mesh.triangles[i + 2]);
        p.node = 0; p.bones.clear(); p.jointIdx.clear(); p.jointWt.clear();
    }
    const ImportedNode rootNode = out->nodes[0];
    out->nodes.assign(1, rootNode);
    out->clips.clear();
}

std::vector<std::string> Reader::GroupNames(const ObjRec& o, const Ref& me) {
    std::vector<Ref> groups;
    if (o.ob.Has("defbase")) o.ob.List("defbase", "bDeformGroup", groups, 100000);
    if (groups.empty() && me && me.Has("vertex_group_names")) me.List("vertex_group_names", "bDeformGroup", groups, 100000);
    std::vector<std::string> names;
    names.reserve(groups.size());
    for (const Ref& g : groups) names.push_back(g.Str("name", 64));
    return names;
}

// Edge creases (0..1): MEdge.crease (a byte, up to 3.x), else the 4.x attributes ".edge_verts" + "crease_edge".
void Reader::ReadCreases(std::uint64_t meshAddr, MeshData& md) {
    Ref me = At(meshAddr, "Mesh");
    if (!me) return;
    const int ne = me.Has("totedge") ? me.Int("totedge") : me.Int("edges_num");
    if (ne <= 0 || (std::size_t)ne > kMaxCorners) return;
    const int nvert = (int)md.pos.size();
    const std::vector<Ref> el = Layers(Cd(me, "edata", "edge_data"));
    auto push = [&](std::uint32_t a, std::uint32_t b, float c) {
        if (!(c > 0.0f) || a >= (std::uint32_t)nvert || b >= (std::uint32_t)nvert || a == b) return;
        // stored as the 0..1 crease; EvaluateModifiers turns it into a sharpness for the level count
        md.creases.push_back(blendmod::Crease{(int)a, (int)b, std::min(c, 1.0f)});
    };
    Span es; int stride = 0;
    if (me.Ptr("medge")) es = f.At(me.Ptr("medge"));
    else if (const Ref* l = FindLayerByType(el, 3)) es = LayerData(*l);   // CD_MEDGE
    const int o1 = StructOffset("MEdge", "v1"), o2 = StructOffset("MEdge", "v2"), oc = StructOffset("MEdge", "crease");
    stride = (int)StructSize("MEdge");
    if (es.p && stride >= 9 && o1 >= 0 && o2 >= 0 && oc >= 0 && oc < stride) {
        const std::size_t n = std::min((std::size_t)ne, es.n / (std::size_t)stride);
        for (std::size_t i = 0; i < n; ++i) {
            const u8* q = es.p + i * (std::size_t)stride;
            if (q[oc]) push(f.U32(q + o1), f.U32(q + o2), (float)q[oc] / 255.0f);
        }
        return;
    }
    const Ref* ev = FindLayerByName(el, ".edge_verts");
    const Ref* cr = FindLayerByName(el, "crease_edge");
    if (!ev || !cr) return;
    const Span vs = LayerData(*ev), cs = LayerData(*cr);
    if (!vs.p || !cs.p) return;
    const std::size_t n = std::min({(std::size_t)ne, vs.n / 8, cs.n / 4});
    for (std::size_t i = 0; i < n; ++i) {
        const float c = f.F32(cs.p + i * 4);
        if (c > 0.0f && std::isfinite(c)) push(f.U32(vs.p + i * 8), f.U32(vs.p + i * 8 + 4), c);
    }
}

// The mesh to draw for object o: the stored mesh, or (when it has Mirror / Subdivision Surface modifiers) a copy
// with them applied in stack order. Settings are read by field name from the file's own struct layout.
std::shared_ptr<MeshData> Reader::EvaluateModifiers(ObjRec& o, const std::shared_ptr<MeshData>& base, std::uint64_t meshAddr) {
    if (!o.ob.Has("modifiers")) return base;
    std::shared_ptr<MeshData> cur = base;
    bool copied = false, wantCreases = false;
    auto own = [&]() -> MeshData& {
        if (!copied) {
            cur = std::make_shared<MeshData>(*base); cur->cornerN.clear(); cur->nDone = false; copied = true;
            // creases are read before any modifier runs, so a Mirror before the Subdivision copies them too
            if (wantCreases) ReadCreases(meshAddr, *cur);
        }
        return *cur;
    };
    {   // is there a Subdivision Surface that uses creases?
        std::uint64_t b = o.ob.Sub("modifiers").Ptr("first");
        for (int guard = 0; b && guard < 1000; ++guard) {
            Span sp = f.At(b);
            if (!sp.p || sp.st < 0 || sp.n < (std::size_t)f.ps) break;
            const StructInfo& si = f.structs[(std::size_t)sp.st];
            if (si.name == "SubsurfModifierData" && sp.n >= si.size) {
                Ref md; md.f = &f; md.p = sp.p; md.avail = sp.n; md.st = sp.st;
                if (md.Int("subdivType") != 1 && SubsurfUsesCreases(md)) wantCreases = true;
            }
            b = f.PtrAt(sp.p);
        }
    }
    std::uint64_t a = o.ob.Sub("modifiers").Ptr("first");
    std::set<std::string> seenHere;
    for (int guard = 0; a && guard < 1000; ++guard) {
        Span sp = f.At(a);
        if (!sp.p || sp.st < 0 || sp.n < (std::size_t)f.ps) break;
        const StructInfo& si = f.structs[(std::size_t)sp.st];
        Ref md; md.f = &f; md.p = sp.p; md.avail = sp.n; md.st = sp.st; md.addr = a;
        a = f.PtrAt(sp.p);
        if (sp.n < si.size) continue;
        const Ref head = md.Sub("modifier");
        const std::string type = si.name.size() > 12 && si.name.compare(si.name.size() - 12, 12, "ModifierData") == 0 ? si.name.substr(0, si.name.size() - 12) : si.name;
        const std::string uname = head ? head.Str("name", 64) : std::string();
        if (type == "Armature") continue;   // skinning: read as skin weights
        if (type != "Mirror" && type != "Subsurf") {
            const std::string key = "'" + uname + "' (" + type + ")";
            if (seenHere.insert(key).second) ++skippedMods[key];
            continue;
        }
        if (head && head.Has("mode") && !(head.Int("mode") & 1)) { ++cntModDisabled; continue; }   // eModifierMode_Realtime
        if (type == "Mirror") {
            blendmod::MirrorSettings ms;
            const int flag = md.Int("flag");
            const bool bits = (flag & (8 | 16 | 32)) != 0;
            for (int k = 0; k < 3; ++k) ms.axis[k] = bits ? (flag & (8 << k)) != 0 : (k == std::max(0, std::min(2, md.Int("axis"))));
            ms.merge = !(flag & 128);
            ms.tolerance = (float)std::max(0.0, std::min(1e6, md.Num("tolerance", 0, 0.001)));
            ms.flipU = (flag & 2) != 0; ms.flipV = (flag & 4) != 0;
            for (int k = 0; k < 2; ++k) {
                ms.flipOffset[k] = (float)std::max(-1e6, std::min(1e6, md.Num("uv_offset", (std::size_t)k)));
                ms.copyOffset[k] = (float)std::max(-1e6, std::min(1e6, md.Num("uv_offset_copy", (std::size_t)k)));
            }
            if (flag & (256 | 512 | 1024)) ++cntMirrorBisect;
            if (const std::uint64_t mob = md.Ptr("mirror_ob")) {
                auto it = objByAddr.find(mob);
                if (it != objByAddr.end() && objs[(std::size_t)it->second].placed && MatFinite(objs[(std::size_t)it->second].world)) {
                    ms.toSpace = objs[(std::size_t)it->second].world.Inverse() * o.world;
                    ms.fromSpace = ms.toSpace.Inverse();
                    ms.useSpace = MatFinite(ms.toSpace) && MatFinite(ms.fromSpace);
                } else ++cntMirrorNoObj;
            }
            MeshData& m = own();
            if (flag & 64) {   // MOD_MIR_VGROUP: the copy's weights go to the other side's group
                const std::vector<std::string> names = GroupNames(o, At(meshAddr, "Mesh"));
                ms.groupFlip.assign(names.size(), -1);
                for (std::size_t g = 0; g < names.size(); ++g) {
                    const std::string other = blendmod::FlipSideName(names[g]);
                    if (other.empty()) continue;
                    for (std::size_t h = 0; h < names.size(); ++h) if (names[h] == other) { ms.groupFlip[g] = (int)h; break; }
                }
            }
            blendmod::Mirror(m, ms);
        } else {
            // the higher of the viewport and render levels: what Assimp shows on the corpus (yxa_1: viewport 3 > render 2;
            // HUMAN: render 2 > viewport 1), checked by triangle counts
            int levels = std::max(md.Int("levels"), md.Int("renderLevels"));
            levels = std::max(0, std::min(levels, 11));
            if (levels == 0) continue;
            blendmod::SubsurfSettings ss;
            ss.levels = levels;
            ss.simple = md.Int("subdivType") == 1;
            const int flags = md.Int("flags");
            // Every option is found by its field name in the file's own SubsurfModifierData. The SDNA names fields but
            // holds no enum or flag values, so the meanings of the numbers below are from memory of Blender's DNA
            // headers (unverified; docs/content/model_readers.md "Subdivision Surface options").
            if (md.Has("uv_smooth")) {   // 2.8+: 0 none, 1 keep corners, 2 + junctions, 3 + concave, 4 keep boundaries, 5 all
                static const blendmod::UvSmooth kUv[] = {blendmod::UvSmooth::Linear, blendmod::UvSmooth::KeepCorners,
                                                         blendmod::UvSmooth::KeepCornersJunctions, blendmod::UvSmooth::KeepCornersJunctionsConcave,
                                                         blendmod::UvSmooth::KeepBoundaries, blendmod::UvSmooth::All};
                const int u = md.Int("uv_smooth");
                if (u >= 0 && u <= 5) ss.uvSmooth = kUv[u];
                else { ss.uvSmooth = blendmod::UvSmooth::All; ++cntSubsurfUnknownUv; }
                ss.smoothUv = ss.uvSmooth != blendmod::UvSmooth::Linear;
            } else {
                ss.smoothUv = (flags & 8) != 0;   // up to 2.7x: "Subdivide UVs"
                ss.uvSmooth = blendmod::UvSmooth::All;
            }
            ss.useCreases = SubsurfUsesCreases(md);
            // 2.9+: boundary smooth 1 = "Keep Corners" (0 = "All"); "Use Limit Surface" is on unless flag 64 is set
            ss.keepCorners = md.Has("boundary_smooth") && md.Int("boundary_smooth") == 1;
            ss.limitSurface = md.Has("quality") && !(flags & 64);
            MeshData& m = own();
            // Blender's 0..1 crease as a DeRose sharpness: 10 c^2 from 2.80 on (OpenSubdiv), c * levels before
            for (blendmod::Crease& c : m.creases) c.sharpness = blendmod::CreaseToSharpness(c.sharpness, levels, f.version >= 280);
            const std::size_t before = m.faceStart.size();
            const int done = blendmod::Subdivide(m, ss, std::min(subdivFaceBudget, kMaxFaces));
            if (done < levels) ++cntModCapped;
            const std::size_t made = m.faceStart.size() > before ? m.faceStart.size() - before : 0;
            subdivFaceBudget = made < subdivFaceBudget ? subdivFaceBudget - made : 0;
        }
    }
    return cur;
}

void Reader::BuildPartsOf(ObjRec& o, int oi) {
    (void)oi;
    ImportedModel& m = *out;
    const std::uint64_t meshAddr = o.ob.Ptr("data");
    if (!meshAddr) return;
    std::shared_ptr<MeshData> mdp = ReadMesh(meshAddr);
    if (!mdp->ok) return;
    mdp = EvaluateModifiers(o, mdp, meshAddr);
    MeshData& md = *mdp;
    if (md.faceStart.empty()) { ++cntNoFaces; return; }
    Ref me = At(meshAddr, "Mesh");
    ComputeCornerNormals(md);
    int totcol = me.Int("totcol");
    if (totcol <= 0) totcol = o.ob.Int("totcol");
    totcol = std::max(0, std::min(totcol, 4096));
    std::vector<int> slotMat((std::size_t)totcol, -1);
    {
        Span meshMats = f.At(me.Ptr("mat")), objMats = f.At(o.ob.Ptr("mat")), bits = f.At(o.ob.Ptr("matbits"));
        for (int i = 0; i < totcol; ++i) {
            const std::size_t po = (std::size_t)i * (std::size_t)f.ps;
            const bool objLinked = bits.p && (std::size_t)i < bits.n && bits.p[i];
            std::uint64_t a = 0;
            if (objLinked && objMats.p && po + (std::size_t)f.ps <= objMats.n) a = f.PtrAt(objMats.p + po);
            else if (meshMats.p && po + (std::size_t)f.ps <= meshMats.n) a = f.PtrAt(meshMats.p + po);
            slotMat[(std::size_t)i] = MaterialIndex(a);
        }
    }
    // skin: vertex group -> bone of the armature this object follows
    int armObj = -1, armIdx = -1;
    {
        if (o.parent >= 0 && objs[(std::size_t)o.parent].type == OB_ARMATURE && objs[(std::size_t)o.parent].armIdx >= 0) armObj = o.parent;
        if (armObj < 0 && o.ob.Has("modifiers")) {
            std::uint64_t a = o.ob.Sub("modifiers").Ptr("first");
            for (int guard = 0; a && guard < 1000; ++guard) {
                Span s = f.At(a);
                if (!s.p || s.st < 0 || s.n < (std::size_t)f.ps) break;
                if (f.structs[(std::size_t)s.st].name == "ArmatureModifierData" && s.n >= f.structs[(std::size_t)s.st].size) {
                    Ref amod; amod.f = &f; amod.p = s.p; amod.avail = s.n; amod.st = s.st; amod.addr = a;
                    auto it = objByAddr.find(amod.Ptr("object"));
                    if (it != objByAddr.end() && objs[(std::size_t)it->second].armIdx >= 0) { armObj = it->second; break; }
                }
                a = f.PtrAt(s.p);
            }
        }
        if (armObj >= 0) armIdx = objs[(std::size_t)armObj].armIdx;
    }
    std::vector<int> groupBone;
    bool skin = false;
    if (armIdx < 0 && !md.dvStart.empty()) ++cntGroupsNoArm;
    if (armIdx >= 0 && !md.dvStart.empty()) {
        std::vector<Ref> groups;
        if (o.ob.Has("defbase")) o.ob.List("defbase", "bDeformGroup", groups, 100000);
        if (groups.empty() && me.Has("vertex_group_names")) me.List("vertex_group_names", "bDeformGroup", groups, 100000);
        const ArmRec& ar = arms[(std::size_t)armIdx];
        groupBone.assign(groups.size(), -1);
        for (std::size_t g = 0; g < groups.size(); ++g) { auto it = ar.byName.find(groups[g].Str("name", 64)); if (it != ar.byName.end()) { groupBone[g] = it->second; skin = true; } }
    }

    std::map<int, std::vector<int>> byMat;   // material slot -> faces
    for (std::size_t fi = 0; fi < md.faceStart.size(); ++fi) {
        int mi = md.faceMat[fi];
        if (totcol > 0) mi = std::max(0, std::min(mi, totcol - 1)); else mi = -1;
        byMat[mi].push_back((int)fi);
    }
    std::vector<int> tri;
    for (auto& kv : byMat) {
        if (m.parts.size() >= kMaxParts) { ++cntTruncList; return; }
        ImportedPart part;
        part.mesh.name = o.name;
        part.material = kv.first >= 0 ? slotMat[(std::size_t)kv.first] : -1;
        part.node = o.node;
        const bool hasUv = !md.loopUv.empty();
        // weld table: open addressing over output vertex ids; a slot's key is read back from the vertex itself
        // (source vertex, normal bits, UV bits), so the table is 4 bytes a slot and makes no allocation per vertex
        std::size_t corners = 0;
        for (int fi : kv.second) corners += (std::size_t)md.faceCount[(std::size_t)fi];
        std::size_t cap = 64;
        while (cap < corners + corners / 2 && cap < ((std::size_t)1 << 24)) cap <<= 1;
        std::vector<int> table(cap, -1);
        std::size_t used = 0;
        std::vector<int> srcVert;
        srcVert.reserve(std::min<std::size_t>(corners, 1u << 22));
        part.mesh.vertices.reserve(srcVert.capacity()); part.mesh.normals.reserve(srcVert.capacity());
        if (hasUv) part.mesh.uvs.reserve(srcVert.capacity());
        part.mesh.triangles.reserve(std::min<std::size_t>((corners > 2 * kv.second.size() ? corners - 2 * kv.second.size() : 0) * 3, (std::size_t)1 << 24));
        auto keyOf = [&](int id) {
            const Vec3& n = part.mesh.normals[(std::size_t)id];
            return std::array<std::uint32_t, 6>{(std::uint32_t)srcVert[(std::size_t)id], FBits(n.x), FBits(n.y), FBits(n.z),
                                                hasUv ? FBits(part.mesh.uvs[(std::size_t)id].x) : 0u, hasUv ? FBits(part.mesh.uvs[(std::size_t)id].y) : 0u};
        };
        for (int fi : kv.second) {
            const int n = md.faceCount[(std::size_t)fi], s = md.faceStart[(std::size_t)fi];
            TriangulatePoly(md.pos, &md.loopVert[(std::size_t)s], n, tri, cntNgonFan, clipBudget);
            const std::size_t before = part.mesh.triangles.size();
            for (std::size_t ti = 0; ti + 2 < tri.size(); ti += 3) {
                int ids[3];
                for (int c = 0; c < 3; ++c) {
                    const std::size_t lp = (std::size_t)(s + tri[ti + (std::size_t)c]);
                    const int vi = md.loopVert[lp];
                    const Vec3& nn = md.cornerN[lp];
                    const Vec2 uv = hasUv ? Vec2{md.loopUv[lp].x, 1.0f - md.loopUv[lp].y} : Vec2{0, 0};
                    const std::array<std::uint32_t, 6> key{(std::uint32_t)vi, FBits(nn.x), FBits(nn.y), FBits(nn.z), hasUv ? FBits(uv.x) : 0u, hasUv ? FBits(uv.y) : 0u};
                    std::size_t slot = Hash3{}(key) & (cap - 1);
                    while (table[slot] >= 0 && keyOf(table[slot]) != key) slot = (slot + 1) & (cap - 1);
                    if (table[slot] < 0) {
                        if (++outVerts > kMaxOutVerts) { ++cntTruncList; return; }
                        ids[c] = (int)part.mesh.vertices.size();
                        part.mesh.vertices.push_back(md.pos[(std::size_t)vi]);
                        part.mesh.normals.push_back(nn);
                        if (hasUv) part.mesh.uvs.push_back(uv);
                        srcVert.push_back(vi);
                        table[slot] = ids[c];
                        if (++used * 4 > cap * 3) {   // very large parts start at 16 M slots: grow and re-insert
                            cap <<= 1;
                            table.assign(cap, -1);
                            for (int id = 0; id < (int)srcVert.size(); ++id) {
                                std::size_t sl = Hash3{}(keyOf(id)) & (cap - 1);
                                while (table[sl] >= 0) sl = (sl + 1) & (cap - 1);
                                table[sl] = id;
                            }
                        }
                    } else ids[c] = table[slot];
                }
                if (ids[0] == ids[1] || ids[1] == ids[2] || ids[0] == ids[2]) continue;   // collapsed triangle
                part.mesh.triangles.push_back(ids[0]); part.mesh.triangles.push_back(ids[1]); part.mesh.triangles.push_back(ids[2]);
            }
            (void)before;
        }
        if (part.mesh.triangles.empty()) continue;
        if (skin) {
            const ArmRec& ar = arms[(std::size_t)armIdx];
            const Mat4 meshWorld = o.world, armWorld = objs[(std::size_t)armObj].world;
            for (const BoneRec& br : ar.bones) {
                ImportedBone ib; ib.name = br.name;
                ib.inverseBind = (armWorld * br.arm).Inverse() * meshWorld;
                if (!MatFinite(ib.inverseBind)) { ib.inverseBind = Mat4(); ++cntBadArm; }
                part.bones.push_back(std::move(ib));
            }
            bool any = false;
            part.jointIdx.assign(srcVert.size(), std::array<int, 4>{0, 0, 0, 0});
            part.jointWt.assign(srcVert.size(), std::array<float, 4>{0, 0, 0, 0});
            for (std::size_t v = 0; v < srcVert.size(); ++v) {
                const std::size_t sv = (std::size_t)srcVert[v];
                std::vector<std::pair<int, float>> w;
                for (std::uint32_t k = md.dvStart[sv]; k < md.dvStart[sv + 1]; ++k) {
                    const int g = md.dvGroup[k];
                    if (g < 0 || (std::size_t)g >= groupBone.size() || groupBone[(std::size_t)g] < 0) continue;
                    const int b = groupBone[(std::size_t)g];
                    bool merged = false;
                    for (auto& e : w) if (e.first == b) { e.second += md.dvWeight[k]; merged = true; }
                    if (!merged) w.emplace_back(b, md.dvWeight[k]);
                }
                std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                if (w.size() > 4) w.resize(4);
                float sum = 0; for (auto& e : w) sum += e.second;
                if (!(sum > 0)) continue;
                any = true;
                for (std::size_t k = 0; k < w.size(); ++k) { part.jointIdx[v][k] = w[k].first; part.jointWt[v][k] = w[k].second / sum; }
            }
            if (!any) { part.bones.clear(); part.jointIdx.clear(); part.jointWt.clear(); ++cntSkinSkipped; }
        }
        m.parts.push_back(std::move(part));
    }
}

void Reader::BuildAnimation() {
    ImportedModel& m = *out;
    struct ClipAcc { std::string name; std::vector<std::pair<int, Target>> targets; };
    std::map<std::uint64_t, ClipAcc> actions;   // by action address (ordered: deterministic)
    for (std::size_t oi = 0; oi < objs.size(); ++oi) {
        ObjRec& o = objs[oi];
        if (!o.include || !o.placed || !o.ob.Has("adt")) continue;
        Ref ad = o.ob.Deref("adt", "AnimData");
        if (!ad) continue;
        const std::uint64_t aa = ad.Ptr("action");
        Ref act = At(aa, "bAction");
        if (!act) continue;
        if (!act.Has("curves")) { ++cntIpo; continue; }
        std::vector<Ref> fcs;
        act.List("curves", "FCurve", fcs, kMaxCurves);
        std::map<int, Target> tmap;   // bone index (-1 = the object itself) -> target
        for (const Ref& fc : fcs) {
            if (fc.Ptr("driver") || (fc.Has("modifiers") && fc.Sub("modifiers").Ptr("first"))) ++cntFcurveMods;
            std::string path;
            { Span s = f.At(fc.Ptr("rna_path")); if (s.p) for (std::size_t i = 0; i < s.n && i < 512 && s.p[i]; ++i) path += (char)s.p[i]; }
            const int idx = fc.Int("array_index");
            int bone = -1;
            std::string prop = path;
            if (path.compare(0, 12, "pose.bones[\"") == 0) {
                const std::size_t e = path.find("\"]", 12);
                if (e == std::string::npos || e + 3 > path.size() || path[e + 2] != '.' || o.armIdx < 0) { ++cntOtherPath; if (firstOtherPath.empty()) firstOtherPath = path; continue; }
                auto bi = arms[(std::size_t)o.armIdx].byName.find(path.substr(12, e - 12));
                if (bi == arms[(std::size_t)o.armIdx].byName.end()) { ++cntOtherPath; continue; }
                bone = bi->second;
                prop = path.substr(e + 3);
            }
            int kind = -1;
            if (prop == "location") kind = 0; else if (prop == "scale") kind = 1; else if (prop == "rotation_euler") kind = 2; else if (prop == "rotation_quaternion") kind = 3; else if (prop == "rotation_axis_angle") kind = 4;
            const int lim = kind == 3 || kind == 4 ? 4 : 3;
            if (kind < 0 || idx < 0 || idx >= lim) { ++cntOtherPath; if (firstOtherPath.empty()) firstOtherPath = path; continue; }
            Curve c;
            if (!LoadCurve(fc, c)) continue;
            Target& t = tmap[bone];
            t.obj = (int)oi; t.bone = bone; t.isBone = bone >= 0;
            t.node = bone >= 0 ? arms[(std::size_t)o.armIdx].bones[(std::size_t)bone].node : o.node;
            Curve* dst = kind == 0 ? &t.loc[idx] : kind == 1 ? &t.scl[idx] : kind == 2 ? &t.eul[idx] : kind == 3 ? &t.quat[idx] : &t.axis[idx];
            *dst = std::move(c);
            t.has[kind][idx] = true;
            ++cntCurves;
        }
        if (tmap.empty()) continue;
        ClipAcc& ca = actions[aa];
        if (ca.name.empty()) ca.name = IdName(act);
        for (auto& kv : tmap) ca.targets.emplace_back((int)oi, std::move(kv.second));
    }
    for (auto& akv : actions) {
        ClipAcc& ca = akv.second;
        double fmin = 1e300;
        for (auto& tp : ca.targets) {
            const Target& t = tp.second;
            auto scan = [&](const Curve* cs, int n, int kind) { for (int i = 0; i < n; ++i) if (t.has[kind][i] && !cs[i].keys.empty()) fmin = std::min(fmin, cs[i].keys.front().x); };
            scan(t.loc, 3, 0); scan(t.scl, 3, 1); scan(t.eul, 3, 2); scan(t.quat, 4, 3); scan(t.axis, 4, 4);
        }
        if (!(fmin < 1e299)) continue;
        ImportedClip clip;
        clip.name = ca.name.empty() ? std::string("action") : ca.name;
        for (auto& tp : ca.targets) {
            Target& t = tp.second;
            ObjRec& o = objs[(std::size_t)tp.first];
            std::vector<double> times;   // every key, plus every whole frame in between when a segment is a bezier curve
            double lo = 1e300, hi = -1e300; bool dense = false;
            auto gather = [&](const Curve* cs, int n, int kind) {
                for (int i = 0; i < n; ++i) {
                    if (!t.has[kind][i]) continue;
                    const Curve& c = cs[i];
                    if (c.extend != 0) ++cntExtrapolate;
                    for (std::size_t k = 0; k < c.keys.size(); ++k) {
                        times.push_back(c.keys[k].x);
                        if (k + 1 < c.keys.size() && c.keys[k].ipo == 0) times.push_back(c.keys[k + 1].x - 0.001);   // a held key: step just before the next
                        if (k + 1 < c.keys.size() && c.keys[k].ipo == 2) dense = true;
                    }
                    lo = std::min(lo, c.keys.front().x); hi = std::max(hi, c.keys.back().x);
                }
            };
            gather(t.loc, 3, 0); gather(t.scl, 3, 1); gather(t.eul, 3, 2); gather(t.quat, 4, 3); gather(t.axis, 4, 4);
            if (times.empty()) continue;
            if (dense) {
                if (hi - lo <= (double)kMaxDenseFrames) { for (double fr = std::ceil(lo); fr <= hi; fr += 1.0) times.push_back(fr); }
                else ++cntBigAnim;
            }
            std::sort(times.begin(), times.end());
            std::vector<double> ts;
            for (double v : times) if (ts.empty() || v - ts.back() > 1e-6) ts.push_back(v);
            if (ts.size() < 2) continue;   // one key is a placement, which the node's transform already is
            if (outKeys + ts.size() * 3 > kMaxOutKeys) { ++cntBigAnim; continue; }
            double loc[3] = {0, 0, 0}, sz[3] = {1, 1, 1}, eul[3] = {0, 0, 0}, quat[4] = {1, 0, 0, 0}, ax[4] = {0, 1, 0, 0};
            int rotmode = 1;
            Mat4 pre;
            double dl[3] = {0, 0, 0}, ds[3] = {1, 1, 1}, de[3] = {0, 0, 0};
            if (t.isBone) {
                const BoneRec& br = arms[(std::size_t)o.armIdx].bones[(std::size_t)t.bone];
                for (int k = 0; k < 3; ++k) { loc[k] = br.loc[k]; sz[k] = br.size[k]; eul[k] = br.eul[k]; ax[k] = br.axis[k]; }
                for (int k = 0; k < 4; ++k) quat[k] = br.quat[k];
                ax[3] = br.angle; rotmode = br.rotmode; pre = br.restLocal;
            } else {
                const Ref& ob = o.ob;
                for (int k = 0; k < 3; ++k) { loc[k] = ob.Num("loc", (std::size_t)k); dl[k] = ob.Num("dloc", (std::size_t)k); sz[k] = ob.Num("size", (std::size_t)k, 1.0); eul[k] = ob.Num("rot", (std::size_t)k); de[k] = ob.Num("drot", (std::size_t)k); ax[k] = ob.Num("rotAxis", (std::size_t)k, k == 1 ? 1.0 : 0.0); }
                if (ob.Has("dscale")) for (int k = 0; k < 3; ++k) ds[k] = ob.Num("dscale", (std::size_t)k, 1.0);
                for (int k = 0; k < 4; ++k) quat[k] = ob.Num("quat", (std::size_t)k, k == 0 ? 1.0 : 0.0);
                ax[3] = ob.Num("rotAngle");
                rotmode = ob.Has("rotmode") ? ob.Int("rotmode") : 1;
                pre = o.pre;
            }
            ImportedChannel ch;
            ch.node = m.nodes[(std::size_t)t.node].name;
            Quat prev; bool havePrev = false;
            for (double tt : ts) {
                double L[3], S[3], E[3], Q[4], A[4];
                for (int k = 0; k < 3; ++k) { L[k] = t.has[0][k] ? EvalCurve(t.loc[k], tt) : loc[k]; S[k] = t.has[1][k] ? EvalCurve(t.scl[k], tt) : sz[k]; E[k] = t.has[2][k] ? EvalCurve(t.eul[k], tt) : eul[k]; }
                for (int k = 0; k < 4; ++k) { Q[k] = t.has[3][k] ? EvalCurve(t.quat[k], tt) : quat[k]; A[k] = t.has[4][k] ? EvalCurve(t.axis[k], tt) : ax[k]; }
                for (int k = 0; k < 3; ++k) { L[k] += dl[k]; S[k] *= ds[k]; E[k] += de[k]; }
                const Mat4 basis = TransMat(L[0], L[1], L[2]) * RotationOf(rotmode, E, Q, A, A[3]) * ScaleMat(S[0], S[1], S[2]);
                const Mat4 local = pre * basis;
                if (!MatFinite(local)) continue;
                Vec3 p, s; Quat r;
                local.Decompose(p, r, s);
                if (havePrev && Quat::Dot(prev, r) < 0) r = Quat{-r.x, -r.y, -r.z, -r.w};
                prev = r; havePrev = true;
                const float time = (float)((tt - fmin) / fps);
                if (!std::isfinite(time) || !InRange(p) || !InRange(s)) continue;
                ch.position.push_back({time, p}); ch.rotation.push_back({time, r}); ch.scale.push_back({time, s});
            }
            outKeys += ch.position.size() * 3;
            if (!ch.position.empty()) clip.channels.push_back(std::move(ch));
        }
        if (!clip.channels.empty()) m.clips.push_back(std::move(clip));
    }
    // animation of data blocks other than objects (materials, meshes, cameras, lights, shape keys, worlds)
    for (const BlockInfo& b : f.blocks) {
        if (std::memcmp(b.code, "ME", 3) && std::memcmp(b.code, "MA", 3) && std::memcmp(b.code, "CA", 3) && std::memcmp(b.code, "LA", 3) && std::memcmp(b.code, "KE", 3) && std::memcmp(b.code, "WO", 3)) continue;
        if (b.sdna < 0 || (std::size_t)b.sdna >= f.structs.size()) continue;
        Ref idb = Root().DerefAddr(b.old, f.structs[(std::size_t)b.sdna].name.c_str());
        if (idb && idb.Has("adt")) { Ref ad = idb.Deref("adt", "AnimData"); if (ad && ad.Ptr("action")) ++cntOtherAnim; }
    }
}

void Reader::Notes() {
    auto note = [&](const std::string& s) { out->notes.push_back("Blend: " + s); };
    auto n = [](int v) { return std::to_string(v); };
    if (f.version < 250) note("file from Blender " + std::to_string(f.version / 100) + "." + std::to_string(f.version % 100) + " (before 2.5): objects, meshes, materials and armature rest poses are read; Ipo animation is not");
    if (cntNoScene) note("no scene objects found (no base list or collection): every object in the file was imported");
    if (cntLamps) note(n(cntLamps) + " light object(s) not imported (kept as empty nodes)");
    if (cntCameras) note(n(cntCameras) + " camera object(s) not imported (kept as empty nodes)");
    if (cntOtherTypes) note(n(cntOtherTypes) + " object(s) of an unsupported type (curve, surface, text, metaball, lattice, grease pencil, volume, ...) kept as empty nodes");
    if (cntModifiers) note(n(cntModifiers) + " modifier(s) on objects that are not meshes (curves, text, ...): not applied");
    {
        std::size_t shown = 0;
        for (const auto& kv : skippedMods) {
            if (++shown > 32) { note(n((int)(skippedMods.size() - 32)) + " more kinds of modifier not applied"); break; }
            note("modifier " + kv.first + " not applied (not supported) on " + n(kv.second) + " object(s)");
        }
    }
    if (cntModDisabled) note(n(cntModDisabled) + " Mirror / Subdivision modifier(s) switched off for the viewport: not applied, like Blender's viewport");
    if (cntSubsurfUnknownUv) note(n(cntSubsurfUnknownUv) + " Subdivision modifier(s) with an unknown UV Smooth value: UVs smoothed everywhere");
    if (cntModCapped) note(n(cntModCapped) + " Subdivision modifier(s) stopped at a lower level: the result would pass the face limit (4 M faces per file)");
    if (cntMirrorBisect) note(n(cntMirrorBisect) + " Mirror modifier(s) with bisect set: the mesh is mirrored without cutting it at the mirror plane");
    if (cntMirrorNoObj) note(n(cntMirrorNoObj) + " Mirror modifier(s) whose mirror object is not in the scene: mirrored in the object's own space");
    if (cntConstraints) note(n(cntConstraints) + " object(s) with constraints or a track-to target: not evaluated");
    if (cntBoneConstraints) note(n(cntBoneConstraints) + " pose bone(s) with constraints (IK, copy rotation, ...): not evaluated, the stored pose values are used");
    if (cntDupli) note(n(cntDupli) + " object(s) instance a group / collection: the instance is not expanded");
    if (cntMatMismatch) note(n(cntMatMismatch) + " object(s) whose computed world matrix differs from the matrix saved in the file (constraints, drivers, animation or an unsupported parent type)");
    if (cntVertexParent) note(n(cntVertexParent) + " object(s) parented to a vertex, curve or lattice: attached to the parent object's origin instead");
    if (cntBoneParent) note(n(cntBoneParent) + " object(s) parented to a bone (attached at the bone's tail like Blender does)");
    if (cntNoBone) note(n(cntNoBone) + " object(s) parented to a bone that does not exist in the armature");
    if (cntParentCycle) note(n(cntParentCycle) + " object(s) with a parent cycle were moved to the root");
    if (cntSheared) note(n(cntSheared) + " node transform(s) contain shear that a position / rotation / scale triple cannot hold: the shear is dropped");
    if (cntDeltaRot) note(n(cntDeltaRot) + " object(s) with a delta rotation in quaternion / axis-angle mode: the delta is ignored");
    if (cntUnitScale) note("scene unit scale is not 1: sizes are in Blender units");
    if (cntNoFaces) note(n(cntNoFaces) + " mesh object(s) without faces (vertices or edges only) not imported");
    if (cntGroupsNoArm) note(n(cntGroupsNoArm) + " mesh object(s) with vertex groups but no armature to follow: weights not imported");
    if (cntBadMesh) note(n(cntBadMesh) + " mesh(es) with missing or truncated vertex / face arrays: read as far as they go");
    int badFaces = 0, vcol = 0, autoS = 0, custN = 0, shape = 0;
    for (auto& kv : meshCache) { badFaces += kv.second->badFaces; vcol += kv.second->colourLayers > 0; autoS += kv.second->autoSmooth; custN += kv.second->customNormals; shape += kv.second->shapeKeys; }
    if (badFaces) note(n(badFaces) + " face(s) or vertices skipped: fewer than three corners, an index out of range or a non-finite coordinate");
    if (vcol) note(n(vcol) + " mesh(es) have vertex colours: the engine mesh has no vertex colour channel, they are not imported");
    if (autoS) note(n(autoS) + " mesh(es) use auto smooth with an angle: faces are smooth or flat as flagged, sharp edges and the angle are not applied");
    if (custN) note(n(custN) + " mesh(es) have custom split normals: not read");
    if (shape) note(n(shape) + " mesh(es) have shape keys (morph targets): not imported");
    if (cntNgonFan) note(n(cntNgonFan) + " polygon(s) could not be ear clipped and were fanned");
    if (cntNodeMat) note(n(cntNodeMat) + " node-based material(s) without a Principled BSDF: colours come from the material's viewport settings");
    if (cntPacked) note(n(cntPacked) + " image(s) are packed inside the .blend: only the original file name is given, the pixels are not extracted");
    if (cntSkinSkipped) note(n(cntSkinSkipped) + " mesh(es) with vertex groups but no matching armature bones: imported without a skin");
    if (cntIpo) note(n(cntIpo) + " object(s) with pre-2.5 Ipo animation: not read");
    if (cntNla) note(n(cntNla) + " object(s) with NLA tracks: only the active action is read");
    if (cntDrivers) note(n(cntDrivers) + " object(s) with drivers: not evaluated");
    if (cntFcurveMods) note(n(cntFcurveMods) + " F-Curve(s) with modifiers or drivers (cycles, noise, ...): the keys are read, the modifiers are not applied");
    if (cntEasing) note(n(cntEasing) + " keyframe(s) use an easing interpolation: treated as linear");
    if (cntExtrapolate) note(n(cntExtrapolate) + " F-Curve(s) extrapolate: the value is held after the last key");
    if (cntOtherPath) note(n(cntOtherPath) + " F-Curve(s) on properties other than location, rotation and scale of an object or pose bone were skipped (e.g. '" + firstOtherPath + "')");
    if (cntOtherAnim) note(n(cntOtherAnim) + " non-object data block(s) (material, mesh, shape key, camera, light, world) have animation: not read");
    if (cntBigAnim) note(n(cntBigAnim) + " animated node(s) skipped or sampled at keys only (too many frames or keys)");
    if (cntTruncList) note("a list in the file was longer than the reader's limit and was cut");
    if (cntBadArm) note(n(cntBadArm) + " armature(s) or bone(s) with a singular or non-finite rest matrix: the armature was dropped or the inverse bind left as identity");
    if (cntBadObj) note(n(cntBadObj) + " object(s) unreadable or with a non-finite transform");
    if (f.boundsMiss) note(n((int)std::min<std::size_t>(f.boundsMiss, 1000000)) + " field read(s) fell outside their block (damaged or unusual file); the defaults were used");
    for (const StructInfo& s : f.structs) if (!s.layoutOk && (s.name == "Object" || s.name == "Mesh" || s.name == "Bone" || s.name == "bPoseChannel" || s.name == "FCurve")) { note("struct " + s.name + " has a size in the file that does not match its fields: values may be wrong"); break; }
}


} // namespace blnd
} // namespace okay
