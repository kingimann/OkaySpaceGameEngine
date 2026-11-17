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

// Guided modeling, environment: tree, rock, road, bridge, hedge, plant and street lamp (docs/guided_modeling_design.md).
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
bool DisjointRaw(const std::vector<GeneratedPart>& parts, std::string* why = nullptr) {
    std::map<std::tuple<long long, long long, long long>, std::string> seen;
    for (const GeneratedPart& p : parts) {
        std::set<std::tuple<long long, long long, long long>> mine;
        for (const Vec3& v : p.mesh.verts) mine.insert({std::llround(v.x * 1e4), std::llround(v.y * 1e4), std::llround(v.z * 1e4)});
        for (const auto& k : mine) if (seen.count(k)) { if (why) *why = p.id + " shares a corner with " + seen[k]; return false; }
        for (const auto& k : mine) seen[k] = p.id;
    }
    return true;
}
bool DisjointR(const std::vector<GeneratedPart>& p, const Recipe& r, std::string* why = nullptr) { return r.Param("detail", 1) >= 2.5 || DisjointRaw(p, why); }   // smoothed (Ultra) parts may share a corner
bool Disjoint(const std::vector<GeneratedPart>& p, std::string* why = nullptr) { return DisjointRaw(p, why); }
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
const SlotDef& SlotOf(const Template& T, const char* id) { for (const SlotDef& s : T.slots) if (s.id == id) return s; return T.slots[0]; }
uint32_t Rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
const Template& Tpl(const char* id) { RegisterBuiltinTemplates(); return *Registry::Instance().Find(id); }
std::string Hash(const PolyMesh& m) { return m.Serialize(); }
void Bounds(const PolyMesh& m, Vec3& lo, Vec3& hi) {
    lo = {1e9f, 1e9f, 1e9f}; hi = {-1e9f, -1e9f, -1e9f};
    for (const Vec3& v : m.verts) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
}

} // namespace

int main() {
    RUN_SUITE("guided environment");
    const char* ids[] = {"tree", "rock", "road", "bridge", "hedge", "plant", "streetlamp"};
    for (const char* id : ids) {
        const Template& T = Tpl(id);
        CHECK(T.category == "Environment");
        CHECK(T.starts.size() >= 6);
        std::set<std::string> distinct;
        for (const StartDesign& st : T.starts) {
            const Recipe r = DefaultRecipe(T, st.id);
            const auto parts = Generate(r);
            std::string why, why2;
            const bool ok = Sound(parts, &why), dj = DisjointR(parts, r, &why2);
            if (!ok) std::cerr << "  " << id << "/" << st.id << " not sound: " << why << "\n";
            if (!dj) std::cerr << "  " << id << "/" << st.id << " not disjoint: " << why2 << "\n";
            CHECK(!parts.empty() && ok && dj);
            CHECK(Hash(Assemble(parts)) == Hash(Create(r)));
            Recipe r2; std::string err;
            CHECK(Recipe::Parse(r.Serialize(), r2, &err) && Hash(Create(r2)) == Hash(Create(r)));
            distinct.insert(Hash(Assemble(parts)));
            Vec3 lo, hi; Bounds(Assemble(parts), lo, hi);
            if (!(lo.y > -1.0f && hi.y > lo.y + 0.03f)) std::cerr << "  " << id << "/" << st.id << " bounds y " << lo.y << " " << hi.y << "\n";
            CHECK(lo.y > -1.0f && hi.y > lo.y + 0.03f);
        }
        CHECK(distinct.size() == T.starts.size());
        for (const StartDesign& st : T.starts) {
            for (const SlotDef& sl : T.slots)
                for (const DesignDef& d : sl.designs) {
                    Recipe r = DefaultRecipe(T, st.id);
                    r.slotDesign[sl.id] = d.id;
                    std::string why;
                    const auto parts = Generate(r);
                    const bool ok = Sound(parts, &why) && DisjointR(parts, r, &why);
                    if (!ok) std::cerr << "  " << id << "/" << st.id << " slot " << sl.id << "=" << d.id << ": " << why << "\n";
                    CHECK(ok);
                }
            for (const ParamDef& p : T.params)
                for (double v : {p.min, p.max}) {
                    Recipe r = DefaultRecipe(T, st.id);
                    r.params[p.id] = v;
                    std::string why;
                    const auto parts = Generate(r);
                    const bool ok = Sound(parts, &why) && DisjointR(parts, r, &why);
                    if (!ok) std::cerr << "  " << id << "/" << st.id << " " << p.id << "=" << v << ": " << why << "\n";
                    CHECK(ok);
                }
        }
        Recipe bad = DefaultRecipe(T);
        for (const ParamDef& p : T.params) bad.params[p.id] = (p.id.size() % 2) ? 1e9 : std::nan("");
        CHECK(Sound(Generate(bad)));
        uint32_t seed = 2024;
        int bad2 = 0;
        for (int i = 0; i < 50; ++i) {
            Recipe r = DefaultRecipe(T, T.starts[Rng(seed) % T.starts.size()].id);
            for (const ParamDef& p : T.params) r.params[p.id] = p.min + (p.max - p.min) * (double)(Rng(seed) % 1001) / 1000.0;
            for (const SlotDef& s : T.slots) r.slotDesign[s.id] = s.designs[Rng(seed) % s.designs.size()].id;
            if (T.usesPath && Rng(seed) % 2) r.path = {{0, 0, 0}, {4, 0.3f, 0}, {4, 0.1f, 5}, {-2, 0, 6}};
            const auto parts = Generate(r);
            std::string w1, w2;
            if (!Sound(parts, &w1) || !DisjointR(parts, r, &w2)) { ++bad2; std::cerr << "  fuzz " << id << ": " << w1 << " " << w2 << "\n"; }
        }
        CHECK(bad2 == 0);
    }

    {   // trees
        const Template& T = Tpl("tree");
        const Recipe a = DefaultRecipe(T, "oak");
        const auto parts = Generate(a);
        CHECK(ById(parts, "trunk/0") && ById(parts, "crown/0"));
        Recipe c = a; c.slotDesign["crown"] = "pine";
        int changed = 0;
        for (const auto& p : Generate(c)) { const auto* q = ById(parts, p.id); if (!q || Hash(p.mesh) != Hash(q->mesh)) ++changed; }
        CHECK(changed >= 1);
        Recipe sd = a; sd.params["seed"] = 77;
        CHECK(Hash(Create(sd)) != Hash(Create(a)));
        Recipe fr = a; fr.slotDesign["fruit"] = "apples";
        CHECK(ById(Generate(fr), "fruit/0") != nullptr && ById(parts, "fruit/0") == nullptr);
        Vec3 lo, hi, l2, h2;
        Bounds(Assemble(parts), lo, hi);
        Recipe tall = a; tall.params["height"] = 12;
        Bounds(Assemble(Generate(tall)), l2, h2);
        CHECK(h2.y > hi.y + 3.0f);
        CHECK(std::fabs(lo.y) < 0.05f);
    }
    {   // rocks
        const Template& T = Tpl("rock");
        Recipe p = DefaultRecipe(T, "pile");
        CHECK(Count(Generate(p), "rock/") == 6);
        p.params["count"] = 11;
        CHECK(Count(Generate(p), "rock/") == 11);
        Recipe m = DefaultRecipe(T, "mossy");
        CHECK(Count(Generate(m), "moss/") == 4);
        Recipe rough = DefaultRecipe(T, "boulder"); rough.params["roughness"] = 0.4;
        CHECK(Hash(Create(rough)) != Hash(Create(DefaultRecipe(T, "boulder"))));
        std::set<std::string> shapesH;
        for (const DesignDef& d : T.slots[0].designs) { Recipe x = DefaultRecipe(T, "boulder"); x.slotDesign["rock"] = d.id; shapesH.insert(Hash(Create(x))); }
        CHECK(shapesH.size() == T.slots[0].designs.size());
    }
    {   // roads
        const Template& T = Tpl("road");
        Recipe r = DefaultRecipe(T, "street");
        r.path = {{0, 0, 0}, {20, 0, 0}, {20, 0, 15}};
        const auto parts = Generate(r);
        CHECK(Count(parts, "road/") == 2 && Count(parts, "kerb/") == 4 && Count(parts, "walk/") == 4 && Count(parts, "marking/") == 2);
        CHECK(Sound(parts) && Disjoint(parts));
        Vec3 lo, hi;
        Bounds(Assemble(Generate(DefaultRecipe(T, "highway"))), lo, hi);
        CHECK((hi.z - lo.z) > 13.0f);
        Recipe rail = DefaultRecipe(T, "railway");
        CHECK(Hash(Create(rail)) != Hash(Create(DefaultRecipe(T, "footpath"))));
        // a sloping path rises
        Recipe up = DefaultRecipe(T, "country"); up.path = {{0, 0, 0}, {10, 3, 0}};
        Bounds(Assemble(Generate(up)), lo, hi);
        CHECK(hi.y > 2.9f);
        std::set<std::string> surf;
        for (const DesignDef& d : T.slots[0].designs) { Recipe x = DefaultRecipe(T, "street"); x.slotDesign["surface"] = d.id; surf.insert(Hash(ById(Generate(x), "road/0")->mesh)); }
        CHECK(surf.size() == T.slots[0].designs.size());
    }
    {   // bridges
        const Template& T = Tpl("bridge");
        const auto parts = Generate(DefaultRecipe(T, "stone_arch"));
        CHECK(ById(parts, "deck/0") && ById(parts, "structure/0") && ById(parts, "rail/0") && ById(parts, "rail/1"));
        Vec3 lo, hi;
        Bounds(ById(parts, "deck/0")->mesh, lo, hi);
        CHECK(std::fabs((hi.x - lo.x) - 10.0f) < 0.01f);
        Recipe x = DefaultRecipe(T, "footbridge"); x.params["supports"] = 5;
        CHECK(Sound(Generate(x)));
        std::set<std::string> st;
        for (const DesignDef& d : T.slots[1].designs) { Recipe y = DefaultRecipe(T, "footbridge"); y.slotDesign["structure"] = d.id; st.insert(Hash(ById(Generate(y), "structure/0")->mesh)); }
        CHECK(st.size() == T.slots[1].designs.size());
    }
    {   // hedges, plants, lamps
        const Template& H = Tpl("hedge");
        Recipe h = DefaultRecipe(H, "topiary_balls"); h.path = {{0, 0, 0}, {5, 0, 0}, {5, 0, 4}};
        CHECK(Count(Generate(h), "hedge/") == 2 && Count(Generate(h), "topiary/") >= 4);
        const Template& P = Tpl("plant");
        Recipe pl = DefaultRecipe(P, "flowers");
        CHECK(Count(Generate(pl), "plant/") == 9 && Count(Generate(pl), "pot/") == 0);
        pl.slotDesign["pot"] = "clay";
        CHECK(Count(Generate(pl), "pot/") == 9);
        const Template& L = Tpl("streetlamp");
        CHECK(Count(Generate(DefaultRecipe(L, "double_arm")), "head/") == 2 && Count(Generate(DefaultRecipe(L, "victorian")), "head/") == 1);
        Vec3 lo, hi;
        Bounds(Assemble(Generate(DefaultRecipe(L, "modern"))), lo, hi);
        CHECK(hi.y > 7.0f);
    }

    // ---- the safe-regeneration rules on a tree --------------------------------------------------------------------
    {
        const Template& T = Tpl("tree");
        const Recipe a = DefaultRecipe(T, "apple");
        PolyMesh m = Create(a);
        PolyMesh box;
        const float s = 0.3f;
        const Vec3 c[8] = {{-s, 0, -s}, {s, 0, -s}, {s, 2 * s, -s}, {-s, 2 * s, -s}, {-s, 0, s}, {s, 0, s}, {s, 2 * s, s}, {-s, 2 * s, s}};
        for (const Vec3& p : c) box.verts.push_back(p + Vec3{5, 0, 0});
        const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
        for (auto& f : q) { PolyFace pf; pf.verts = {f[0], f[1], f[2], f[3]}; box.faces.push_back(pf); }
        const int start = (int)m.faces.size();
        shapes::Append(m, box);
        const Analysis an = Analyze(m, a);
        CHECK(an.ok && an.customFaces == 6 && an.Count(PartState::Edited) == 0);
        PolyMesh edited = m;
        std::set<int> vs;
        for (int i = 0; i < (int)an.faceOwner.size(); ++i) if (an.faceOwner[i] == "fruit/0") for (int v : edited.faces[i].verts) vs.insert(v);
        for (int v : vs) edited.verts[v].y += 0.2f;
        Recipe col = a; col.colors["trunk"] = Color{0.2f, 0.2f, 0.2f, 1};
        const RegenResult r1 = Regenerate(edited, a, col);
        CHECK(r1.ok && !r1.needsDecision);
        for (int i = 0; i < 6; ++i) CHECK(HasFaceLike(r1.mesh, edited, start + i));
        CHECK(Analyze(r1.mesh, r1.recipe).Count(PartState::Edited) == 1);
        Recipe fruit = a; fruit.slotDesign["fruit"] = "blossom";
        const RegenResult ask = Regenerate(edited, a, fruit);
        CHECK(!ask.ok && ask.needsDecision && Hash(ask.mesh) == Hash(edited));
        Recipe pine = a; pine.slotDesign["crown"] = "pine";
        CHECK(Hash(Regenerate(Create(a), a, pine).mesh) == Hash(Create(pine)));
        RegenOptions keep; keep.all = Decision::Keep;
        CHECK(Regenerate(edited, a, fruit, keep).ok);
    }

    TEST_MAIN_RESULT();
}
