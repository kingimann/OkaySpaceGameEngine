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

// Guided modeling, detail levels (Low to Ultra) for every template (docs/guided_modeling_design.md).
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
    RUN_SUITE("guided detail");
    RegisterBuiltinTemplates();
    std::vector<std::string> ids;
    for (const Template& t : Registry::Instance().All()) ids.push_back(t.id);
    CHECK(ids.size() >= 12);
    int tableRows = 0;
    for (const std::string& id : ids) {
        const Template& T = Tpl(id.c_str());
        // the shared setting is there, with four choices, default Medium
        const ParamDef* dp = nullptr;
        for (const ParamDef& p : T.params) if (p.id == "detail") dp = &p;
        CHECK(dp && dp->kind == ParamDef::Kind::Float && dp->min == 0 && dp->max == 3 && dp->def == 1);   // 0 to 3, continuous; Medium by default
        for (const StartDesign& st : T.starts) {
            long tris[4];
            for (int lv = 0; lv < 4; ++lv) {
                Recipe r = DefaultRecipe(T, st.id);
                r.params["detail"] = lv;
                const auto parts = Generate(r);
                std::string why;
                const bool ok = Sound(parts, &why) && (lv >= 3 || Disjoint(parts, &why));   // Ultra is subdivided: parts may share a corner after smoothing
                if (!ok) std::cerr << "  " << id << "/" << st.id << " level " << lv << ": " << why << "\n";
                CHECK(ok && !parts.empty());
                tris[lv] = TriangleCount(Assemble(parts));
                CHECK(Hash(Create(r)) == Hash(Create(r)));   // deterministic at every level
            }
            const bool up = tris[0] <= tris[1] && tris[1] < tris[2] && tris[2] < tris[3];
            if (!up) std::cerr << "  " << id << "/" << st.id << " triangles " << tris[0] << " " << tris[1] << " " << tris[2] << " " << tris[3] << "\n";
            CHECK(up);
            CHECK(tris[2] * 10 >= tris[1] * 12 && tris[3] * 10 >= tris[2] * 12);   // each step from Medium up adds at least 20 percent
            if (tableRows < 400) { std::printf("TABLE %s %s %ld %ld %ld %ld\n", id.c_str(), st.id.c_str(), tris[0], tris[1], tris[2], tris[3]); ++tableRows; }
        }
        // ranges: the limits of every setting at High, and a fuzz at random levels
        for (const ParamDef& p : T.params) {
            if (p.id == "detail") continue;
            for (double v : {p.min, p.max}) {
                Recipe r = DefaultRecipe(T, T.starts[0].id); r.params["detail"] = 2; r.params[p.id] = v;
                std::string why;
                const auto parts = Generate(r);
                const bool ok = Sound(parts, &why) && Disjoint(parts, &why);
                if (!ok) std::cerr << "  " << id << " High " << p.id << "=" << v << ": " << why << "\n";
                CHECK(ok);
            }
        }
        uint32_t seed = 99;
        int bad = 0;
        for (int i = 0; i < 30; ++i) {
            Recipe r = DefaultRecipe(T, T.starts[Rng(seed) % T.starts.size()].id);
            for (const ParamDef& p : T.params) r.params[p.id] = p.min + (p.max - p.min) * (double)(Rng(seed) % 1001) / 1000.0;
            for (const SlotDef& s : T.slots) r.slotDesign[s.id] = s.designs[Rng(seed) % s.designs.size()].id;
            r.params["detail"] = (double)(Rng(seed) % 301) / 100.0;   // any point of the range
            const auto parts = Generate(r);
            bool good = true;
            if (r.Param("detail") >= 2.5) { for (const auto& gp : parts) { const auto rep = gp.mesh.Check(); if (!Finite(gp.mesh) || !rep.Closed() || !rep.Valid()) good = false; } }
            else good = Sound(parts) && Disjoint(parts);
            if (!good) ++bad;
        }
        CHECK(bad == 0);
    }

    // ---- old recipes (no detail line) load as Medium -----------------------------------------------------------
    {
        const Template& T = Tpl("fence");
        Recipe r = DefaultRecipe(T, "post_rail");
        std::string text = r.Serialize();
        const size_t at = text.find("param detail");
        CHECK(at != std::string::npos);
        text.erase(at, text.find('\n', at) - at + 1);
        Recipe old; std::string err;
        CHECK(Recipe::Parse(text, old, &err));
        CHECK(old.params.count("detail") == 0);
        CHECK(Hash(Create(old)) == Hash(Create(r)));       // Medium
        Recipe lo = r; lo.params["detail"] = 0;
        CHECK(Hash(Create(lo)) != Hash(Create(r)));
        // saved in the recipe and read back
        Recipe hi = r; hi.params["detail"] = 3;
        Recipe back;
        CHECK(Recipe::Parse(hi.Serialize(), back, &err) && back.Param("detail") == 3 && Hash(Create(back)) == Hash(Create(hi)));
    }

    // ---- switching level keeps part swaps, custom additions, and asks about edited parts --------------------------
    {
        const Template& T = Tpl("building");
        Recipe a = DefaultRecipe(T, "townhouse");
        a.partDesign["window/0/0/0"] = "colonial";
        PolyMesh m = Create(a);
        const PolyMesh box = [] { PolyMesh b; const float s = 0.3f; const Vec3 c[8] = {{-s, 0, -s}, {s, 0, -s}, {s, 2 * s, -s}, {-s, 2 * s, -s}, {-s, 0, s}, {s, 0, s}, {s, 2 * s, s}, {-s, 2 * s, s}};
            for (const Vec3& p : c) b.verts.push_back(p + Vec3{15, 0, 0}); const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
            for (auto& f : q) { PolyFace pf; pf.verts = {f[0], f[1], f[2], f[3]}; b.faces.push_back(pf); } return b; }();
        const int start = (int)m.faces.size();
        shapes::Append(m, box);
        Recipe hi = a; hi.params["detail"] = 2;
        const RegenResult r = Regenerate(m, a, hi);
        CHECK(r.ok && !r.needsDecision);
        for (int i = 0; i < 6; ++i) CHECK(HasFaceLike(r.mesh, m, start + i));
        CHECK(r.recipe.partDesign.at("window/0/0/0") == "colonial");     // the swap survived the level change
        CHECK(Hash(Create(r.recipe)).size() > 0 && r.regenerated == (int)Generate(hi).size());
        CHECK(TriangleCount(r.mesh) > TriangleCount(m));
        // an edited part is asked about, nothing changes
        const Analysis an = Analyze(m, a);
        std::set<int> vs;
        for (int i = 0; i < (int)an.faceOwner.size(); ++i) if (an.faceOwner[i] == "window/0/0/1") for (int v : m.faces[i].verts) vs.insert(v);
        PolyMesh edited = m;
        for (int v : vs) edited.verts[v].y += 0.1f;
        const RegenResult ask = Regenerate(edited, a, hi);
        CHECK(!ask.ok && ask.needsDecision && Hash(ask.mesh) == Hash(edited));
        RegenOptions keep; keep.all = Decision::Keep;
        CHECK(Regenerate(edited, a, hi, keep).ok);
        // back down again with no edits is exact
        const RegenResult down = Regenerate(r.mesh, r.recipe, a);
        CHECK(down.ok && TriangleCount(down.mesh) == TriangleCount(Create(a)) + 12);
    }

    TEST_MAIN_RESULT();
}
