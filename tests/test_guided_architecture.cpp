#include "test_framework.hpp"
#include "okay/Modeling/Guided.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>

using namespace okay;
using namespace okay::modeling;
using namespace okay::modeling::guided;

// Guided modeling, architecture: wall, stairs, door, window, roof and building (docs/guided_modeling_design.md).
namespace {

double SignedVolume(const PolyMesh& m) {
    double v = 0;
    for (int f = 0; f < (int)m.faces.size(); ++f) {
        const std::vector<int> tri = m.TriangulateFace(f);
        for (size_t k = 0; k + 2 < tri.size(); k += 3) {
            const Vec3& a = m.verts[m.faces[f].verts[tri[k]]];
            const Vec3& b = m.verts[m.faces[f].verts[tri[k + 1]]];
            const Vec3& c = m.verts[m.faces[f].verts[tri[k + 2]]];
            v += (double)Vec3::Dot(a, Vec3::Cross(b, c)) / 6.0;
        }
    }
    return v;
}
bool Finite(const PolyMesh& m) { for (const Vec3& v : m.verts) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false; return true; }
// Each part is a union of closed shells; check the whole part: valid, finite, closed, positive volume.
bool Sound(const std::vector<GeneratedPart>& parts, std::string* why = nullptr) {
    for (const GeneratedPart& p : parts) {
        const PolyMesh::Report r = p.mesh.Check();
        if (!Finite(p.mesh) || !r.Closed() || !r.Valid() || !(SignedVolume(p.mesh) > 1e-9)) {
            if (why) *why = p.id + " closed " + std::to_string(r.Closed()) + " valid " + std::to_string(r.Valid()) + " (deg " + std::to_string(r.degenerateFaces) + ") vol " + std::to_string(SignedVolume(p.mesh));
            return false;
        }
    }
    return true;
}
bool Disjoint(const std::vector<GeneratedPart>& parts, std::string* why = nullptr) {
    std::map<std::tuple<long long, long long, long long>, std::string> seen;
    for (const GeneratedPart& p : parts) {
        std::set<std::tuple<long long, long long, long long>> mine;
        for (const Vec3& v : p.mesh.verts) mine.insert({std::llround(v.x * 1e4), std::llround(v.y * 1e4), std::llround(v.z * 1e4)});
        for (const auto& k : mine) if (seen.count(k)) { if (why) *why = p.id + " shares a corner with " + seen[k]; return false; }
        for (const auto& k : mine) seen[k] = p.id;
    }
    return true;
}
const GeneratedPart* ById(const std::vector<GeneratedPart>& v, const std::string& id) { for (const auto& p : v) if (p.id == id) return &p; return nullptr; }
int Count(const std::vector<GeneratedPart>& v, const std::string& prefix) { int n = 0; for (const auto& p : v) if (p.id.rfind(prefix, 0) == 0) ++n; return n; }
bool HasFaceLike(const PolyMesh& m, const PolyMesh& src, int face) {
    auto key = [](const PolyMesh& p, const PolyFace& f) {
        std::multiset<std::tuple<long long, long long, long long>> k;
        for (int v : f.verts) k.insert({std::llround(p.verts[v].x * 1e4), std::llround(p.verts[v].y * 1e4), std::llround(p.verts[v].z * 1e4)});
        return k;
    };
    const auto want = key(src, src.faces[face]);
    for (const PolyFace& f : m.faces) if (key(m, f) == want) return true;
    return false;
}
uint32_t Rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
const Template& Tpl(const char* id) { RegisterBuiltinTemplates(); return *Registry::Instance().Find(id); }
std::string Hash(const PolyMesh& m) { return m.Serialize(); }
void Bounds(const PolyMesh& m, Vec3& lo, Vec3& hi) {
    lo = {1e9f, 1e9f, 1e9f}; hi = {-1e9f, -1e9f, -1e9f};
    for (const Vec3& v : m.verts) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
}

} // namespace

int main() {
    RUN_SUITE("guided architecture");
    const char* ids[] = {"wall", "stairs", "door", "window", "roof", "building"};

    // ---- every start design of every template: sound, disjoint, deterministic --------------------------------
    for (const char* id : ids) {
        const Template& T = Tpl(id);
        CHECK(T.category == "Architecture");
        CHECK(T.starts.size() >= 4);
        for (const StartDesign& st : T.starts) {
            const Recipe r = DefaultRecipe(T, st.id);
            const auto parts = Generate(r);
            std::string why, why2;
            const bool ok = Sound(parts, &why), dj = Disjoint(parts, &why2);
            if (!ok) std::cerr << "  " << id << "/" << st.id << " not sound: " << why << "\n";
            if (!dj) std::cerr << "  " << id << "/" << st.id << " not disjoint: " << why2 << "\n";
            CHECK(!parts.empty() && ok && dj);
            CHECK(Hash(Assemble(parts)) == Hash(Create(r)));
            Recipe r2;
            std::string err;
            CHECK(Recipe::Parse(r.Serialize(), r2, &err) && Hash(Create(r2)) == Hash(Create(r)));
        }
        // the starts of one template make different meshes
        std::set<std::string> distinct;
        for (const StartDesign& st : T.starts) distinct.insert(Hash(Create(DefaultRecipe(T, st.id))));
        CHECK(distinct.size() == T.starts.size());
    }

    // ---- every slot design makes sound parts ------------------------------------------------------------------
    for (const char* id : ids) {
        const Template& T = Tpl(id);
        for (const StartDesign& st : T.starts)
            for (const SlotDef& sl : T.slots)
                for (const DesignDef& d : sl.designs) {
                    Recipe r = DefaultRecipe(T, st.id);
                    r.slotDesign[sl.id] = d.id;
                    const auto parts = Generate(r);
                    std::string why;
                    const bool ok = Sound(parts, &why) && Disjoint(parts, &why);
                    if (!ok) std::cerr << "  " << id << "/" << st.id << " slot " << sl.id << "=" << d.id << ": " << why << "\n";
                    CHECK(ok);
                }
    }

    // ---- parameter ranges: min, mid, max of each; clamped nonsense; fuzz -------------------------------------
    for (const char* id : ids) {
        const Template& T = Tpl(id);
        for (const StartDesign& st : T.starts)
            for (const ParamDef& p : T.params)
                for (double v : {p.min, (p.min + p.max) * 0.5, p.max}) {
                    Recipe r = DefaultRecipe(T, st.id);
                    r.params[p.id] = v;
                    std::string why;
                    const auto parts = Generate(r);
                    const bool ok = Sound(parts, &why) && Disjoint(parts, &why);
                    if (!ok) std::cerr << "  " << id << "/" << st.id << " " << p.id << "=" << v << ": " << why << "\n";
                    CHECK(ok);
                }
        Recipe bad = DefaultRecipe(T);
        for (const ParamDef& p : T.params) bad.params[p.id] = (p.id.size() % 2) ? 1e9 : std::nan("");
        CHECK(Sound(Generate(bad)));
        uint32_t seed = 777;
        int bad2 = 0;
        for (int i = 0; i < 80; ++i) {
            Recipe r = DefaultRecipe(T, T.starts[Rng(seed) % T.starts.size()].id);
            for (const ParamDef& p : T.params) r.params[p.id] = p.min + (p.max - p.min) * (double)(Rng(seed) % 1001) / 1000.0;
            for (const SlotDef& s : T.slots) r.slotDesign[s.id] = s.designs[Rng(seed) % s.designs.size()].id;
            const auto parts = Generate(r);
            if (!Sound(parts) || !Disjoint(parts)) ++bad2;
        }
        CHECK(bad2 == 0);
    }

    // ---- walls: openings, path, corners ------------------------------------------------------------------------
    {
        const Template& T = Tpl("wall");
        Recipe r = DefaultRecipe(T, "brick_door");
        auto parts = Generate(r);
        CHECK(Count(parts, "window/") == 2 && Count(parts, "door/") == 1 && Count(parts, "wall/") == 1);
        // the wall is one shell with holes: more faces than a box, and the hole is empty space (volume less than a solid box)
        Vec3 lo, hi;
        Bounds(ById(parts, "wall/0")->mesh, lo, hi);
        const double solid = (double)(hi.x - lo.x) * (hi.y - lo.y) * (hi.z - lo.z);
        CHECK(SignedVolume(ById(parts, "wall/0")->mesh) < solid * 0.9);
        CHECK(ById(parts, "wall/0")->mesh.faces.size() > 30);
        r.params["door"] = 0; r.params["windows"] = 0;
        CHECK(Count(Generate(r), "window/") == 0);
        // an opening can be left empty
        Recipe e = DefaultRecipe(T, "brick_door");
        e.partDesign["window/0"] = "none";
        CHECK(Count(Generate(e), "window/") == 1);
        // path with corners: one panel per segment, corner panels do not share corners
        Recipe pth = DefaultRecipe(T, "plaster");
        pth.path = {{0, 0, 0}, {6, 0, 0}, {6, 0, 5}, {0, 0, 5}};
        pth.params["windows"] = 3;
        const auto pp = Generate(pth);
        CHECK(Count(pp, "wall/") == 3);
        CHECK(Sound(pp) && Disjoint(pp));
        // cap on top
        Recipe g = DefaultRecipe(T, "garden");
        CHECK(Count(Generate(g), "coping/") == 1);
    }

    // ---- stairs: shapes, step count, reaches the height ----------------------------------------------------------
    {
        const Template& T = Tpl("stairs");
        for (int type = 0; type < 4; ++type) {
            Recipe r = DefaultRecipe(T, "straight");
            r.params["type"] = type; r.params["steps"] = 15; r.params["totalHeight"] = 3.0;
            const auto parts = Generate(r);
            CHECK(Sound(parts) && Disjoint(parts));
            const int treads = Count(parts, "step/") + Count(parts, "landing/");
            CHECK(treads == 15);
            Vec3 lo, hi;
            Bounds(Assemble(parts), lo, hi);
            CHECK(hi.y >= 3.0f - 0.01f);      // the top step is at the full height
            CHECK(lo.y >= -0.05f);
            Recipe left = r, right = r; left.params["turnLeft"] = 1; right.params["turnLeft"] = 0;
            CHECK(Sound(Generate(right)) && Disjoint(Generate(right)));
        }
        // L and U stairs turn: the second flight is not in line with the first
        Recipe l = DefaultRecipe(T, "l_shaped");
        Vec3 lo, hi;
        Bounds(Assemble(Generate(l)), lo, hi);
        CHECK(hi.z - lo.z > 1.5f && hi.x - lo.x > 2.0f);
        // railing designs add parts, none removes them
        Recipe a = DefaultRecipe(T, "straight");
        const int withRail = (int)Generate(a).size();
        a.slotDesign["rail"] = "none";
        CHECK((int)Generate(a).size() < withRail);
        a.slotDesign["rail"] = "balusters";
        CHECK(ById(Generate(a), "rail/a/balusters") != nullptr);
    }

    // ---- doors and windows: designs differ; open door moves the leaf ----------------------------------------------
    {
        const Template& T = Tpl("door");
        Recipe d = DefaultRecipe(T, "panelled");
        const auto closed = Generate(d);
        d.params["open"] = 70;
        const auto open = Generate(d);
        CHECK(Hash(ById(closed, "leaf/0")->mesh) != Hash(ById(open, "leaf/0")->mesh));
        CHECK(Hash(ById(closed, "frame/0")->mesh) == Hash(ById(open, "frame/0")->mesh));   // the frame does not move
        CHECK(Sound(open) && Disjoint(open));
        Recipe dbl = DefaultRecipe(T, "double");
        CHECK(ById(Generate(dbl), "leaf/1") != nullptr);
        std::set<std::string> leafHashes;
        for (const DesignDef& ds : T.slots[1].designs) { Recipe x = DefaultRecipe(T, "flat"); x.slotDesign["leaf"] = ds.id; leafHashes.insert(Hash(ById(Generate(x), "leaf/0")->mesh)); }
        CHECK(leafHashes.size() == T.slots[1].designs.size());
        const Template& W = Tpl("window");
        std::set<std::string> grids;
        for (const DesignDef& ds : W.slots[1].designs) { Recipe x = DefaultRecipe(W, "plain"); x.slotDesign["grid"] = ds.id; grids.insert(Hash(Create(x))); }
        CHECK(grids.size() == W.slots[1].designs.size());
        Recipe sh = DefaultRecipe(W, "colonial");
        CHECK(ById(Generate(sh), "shutter/0") != nullptr && ById(Generate(sh), "shutter/1") != nullptr);
    }

    // ---- roofs: four shapes, overhang, pitch ----------------------------------------------------------------------
    {
        const Template& T = Tpl("roof");
        Recipe r = DefaultRecipe(T, "gable");
        Vec3 lo, hi;
        Bounds(ById(Generate(r), "roof/0")->mesh, lo, hi);
        CHECK(std::fabs((hi.x - lo.x) - (8 + 2 * 0.4f)) < 0.01f);   // width plus overhang both sides
        const float h30 = hi.y;
        r.params["pitch"] = 45;
        Bounds(ById(Generate(r), "roof/0")->mesh, lo, hi);
        CHECK(hi.y > h30);
        std::set<std::string> shapes;
        for (const DesignDef& d : T.slots[0].designs) { Recipe x = DefaultRecipe(T, "gable"); x.slotDesign["roof"] = d.id; shapes.insert(Hash(ById(Generate(x), "roof/0")->mesh)); }
        CHECK(shapes.size() == 4);
        r.params["overhang"] = 0;
        Bounds(ById(Generate(r), "roof/0")->mesh, lo, hi);
        CHECK(std::fabs((hi.x - lo.x) - 8.0f) < 0.01f);
    }

    // ---- building: composes the parts; swap a window, the door, the roof ------------------------------------------
    {
        const Template& T = Tpl("building");
        const Recipe a = DefaultRecipe(T, "townhouse");
        const auto parts = Generate(a);
        CHECK(Count(parts, "wall/") == 8);                           // 2 floors x 4 walls
        CHECK(Count(parts, "window/") > 8 && Count(parts, "door/") == 1);
        CHECK(ById(parts, "roof/0") && ById(parts, "foundation/0") && ById(parts, "floor/1"));
        Vec3 lo, hi;
        Bounds(Assemble(parts), lo, hi);
        CHECK(hi.y > 5.6f && (hi.x - lo.x) > 8.0f);
        // swap one window: only that part changes
        std::string firstWindow;
        for (const auto& p : parts) if (p.id.rfind("window/", 0) == 0) { firstWindow = p.id; break; }
        Recipe b = a;
        b.partDesign[firstWindow] = "colonial";
        const auto pb = Generate(b);
        int changed = 0;
        for (const auto& p : pb) if (Hash(p.mesh) != Hash(ById(parts, p.id)->mesh)) { ++changed; CHECK(p.id == firstWindow); }
        CHECK(changed == 1 && pb.size() == parts.size());
        // all windows
        Recipe c = a; c.slotDesign["window"] = "cross";
        int win = 0;
        for (const auto& p : Generate(c)) if (p.slot == "window" && Hash(p.mesh) != Hash(ById(parts, p.id)->mesh)) ++win;
        CHECK(win > 0);
        // roof shape swap touches the roof only (walls identical)
        Recipe d = a; d.slotDesign["roof"] = "gable";
        const auto pd = Generate(d);
        CHECK(Hash(ById(pd, "roof/0")->mesh) != Hash(ById(parts, "roof/0")->mesh));
        CHECK(Hash(ById(pd, "wall/0/0/0")->mesh) == Hash(ById(parts, "wall/0/0/0")->mesh));
        // door swap
        Recipe e = a; e.slotDesign["door"] = "ledged";
        CHECK(Hash(ById(Generate(e), "door/0/0/0")->mesh) != Hash(ById(parts, "door/0/0/0")->mesh));
        // floors and ridge direction
        Recipe f = a; f.params["floors"] = 4; f.params["ridgeAlong"] = 1;
        CHECK(Sound(Generate(f)) && Disjoint(Generate(f)));
        CHECK(Count(Generate(f), "wall/") == 16);
        Recipe g = a; g.params["ridgeAlong"] = 1;
        Vec3 l1, h1, l2, h2;
        Bounds(ById(Generate(a), "roof/0")->mesh, l1, h1);
        Bounds(ById(Generate(g), "roof/0")->mesh, l2, h2);
        CHECK(std::fabs((h1.x - l1.x) - (h2.x - l2.x)) < 0.01f && std::fabs((h1.z - l1.z) - (h2.z - l2.z)) < 0.01f);   // turned roof: same footprint
        CHECK(h2.y - l2.y > 1.0f);
    }

    // ---- the safe-regeneration rules on a building ----------------------------------------------------------------
    {
        const Template& T = Tpl("building");
        const Recipe a = DefaultRecipe(T, "cottage");
        PolyMesh m = Create(a);
        // the user's own addition (a box) and a pulled window
        PolyMesh crate;
        const float s = 0.4f;
        const Vec3 c[8] = {{-s, 0, -s}, {s, 0, -s}, {s, 2 * s, -s}, {-s, 2 * s, -s}, {-s, 0, s}, {s, 0, s}, {s, 2 * s, s}, {-s, 2 * s, s}};
        for (const Vec3& p : c) crate.verts.push_back(p + Vec3{12, 0, 0});
        const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
        for (auto& f : q) { PolyFace pf; pf.verts = {f[0], f[1], f[2], f[3]}; crate.faces.push_back(pf); }
        const int crateStart = (int)m.faces.size();
        shapes::Append(m, crate);
        const Analysis an0 = Analyze(m, a);
        CHECK(an0.ok && an0.customFaces == 6 && an0.Count(PartState::Edited) == 0);
        // select a window's faces, move them: edited
        std::string win;
        for (const PartInfo& p : an0.parts) if (p.id.rfind("window/", 0) == 0) { win = p.id; break; }
        PolyMesh edited = m;
        std::set<int> vs;
        for (int i = 0; i < (int)an0.faceOwner.size(); ++i) if (an0.faceOwner[i] == win) for (int v : edited.faces[i].verts) vs.insert(v);
        for (int v : vs) edited.verts[v].y += 0.1f;
        const Analysis an1 = Analyze(edited, a);
        int ed = 0;
        for (const PartInfo& p : an1.parts) if (p.state == PartState::Edited) { ++ed; CHECK(p.id == win); }
        CHECK(ed == 1);
        // change the roof: the edited window is untouched by it, so no question; custom box and edited window survive
        Recipe roofChange = a; roofChange.slotDesign["roof"] = "hip";
        const RegenResult rr = Regenerate(edited, a, roofChange);
        CHECK(rr.ok && !rr.needsDecision);
        for (int i = 0; i < 6; ++i) CHECK(HasFaceLike(rr.mesh, edited, crateStart + i));
        CHECK(Analyze(rr.mesh, rr.recipe).Count(PartState::Edited) == 1);
        // change that window's design: asks, nothing changes
        Recipe winChange = a; winChange.partDesign[win] = "colonial";
        const RegenResult ask = Regenerate(edited, a, winChange);
        CHECK(!ask.ok && ask.needsDecision && ask.conflicts.size() == 1 && ask.conflicts[0].id == win);
        CHECK(Hash(ask.mesh) == Hash(edited));
        RegenOptions keep; keep.all = Decision::Keep;
        const RegenResult kept = Regenerate(edited, a, winChange, keep);
        CHECK(kept.ok && kept.recipe.detached.count(win) == 1);
        RegenOptions disc; disc.all = Decision::Discard;
        const RegenResult dsc = Regenerate(edited, a, winChange, disc);
        CHECK(dsc.ok && Analyze(dsc.mesh, dsc.recipe).Count(PartState::Edited) == 0);
        // a plain parameter change with no edits rebuilds exactly
        Recipe wide = a; wide.params["width"] = 9;
        CHECK(Hash(Regenerate(Create(a), a, wide).mesh) == Hash(Create(wide)));
        // user parts work as designs for architecture slots (a window made from a window's faces)
        UserPart up;
        std::string err;
        std::vector<int> wf;
        for (int i = 0; i < (int)an0.faceOwner.size(); ++i) if (an0.faceOwner[i] == win) wf.push_back(i);
        CHECK(MakeUserPart(m, wf, "My window", "window", up, &err));
        Recipe u = a; u.userParts[up.id] = up; u.slotDesign["window"] = "user:" + up.id;
        const auto up2 = Generate(u);
        CHECK(Sound(up2));
        Recipe u2; CHECK(Recipe::Parse(u.Serialize(), u2, &err) && Hash(Create(u2)) == Hash(Create(u)));
    }

    TEST_MAIN_RESULT();
}
