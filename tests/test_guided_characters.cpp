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

// Guided modeling, characters: human, robot and creature (docs/editor/guided_modeling.md).
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
// Disjoint, except at Ultra (detail >= 2.5): the parts are smoothed there and may then share a corner.
bool DisjointR(const std::vector<GeneratedPart>& p, const Recipe& r, std::string* why = nullptr) { return r.Param("detail", 1) >= 2.5 || Disjoint(p, why); }
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
    RUN_SUITE("guided characters");
    const char* ids[] = {"human", "robot", "creature"};
    for (const char* id : ids) {
        const Template& T = Tpl(id);
        CHECK(T.category == "Characters");
        CHECK(T.starts.size() >= (std::string(id) == "human" ? 8u : std::string(id) == "robot" ? 6u : 10u));
        std::set<std::string> distinct;
        for (const StartDesign& st : T.starts) {
            const Recipe r = DefaultRecipe(T, st.id);
            const auto parts = Generate(r);
            std::string why, why2;
            const bool ok = Sound(parts, &why), dj = DisjointR(parts, r, &why2);
            if (!ok) std::cerr << "  " << id << "/" << st.id << " not sound: " << why << "\n";
            if (!dj) std::cerr << "  " << id << "/" << st.id << " not disjoint: " << why2 << "\n";
            CHECK(parts.size() >= 8 && ok && dj);
            CHECK(Hash(Assemble(parts)) == Hash(Create(r)));
            Recipe r2; std::string err;
            CHECK(Recipe::Parse(r.Serialize(), r2, &err) && Hash(Create(r2)) == Hash(Create(r)));
            distinct.insert(Hash(Assemble(parts)));
            Vec3 lo, hi; Bounds(Assemble(parts), lo, hi);
            CHECK(std::fabs(lo.y) < 0.08f);
            CHECK(hi.y > lo.y + 0.2f);
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
        uint32_t seed = 31337;
        int bad2 = 0;
        for (int i = 0; i < 60; ++i) {
            Recipe r = DefaultRecipe(T, T.starts[Rng(seed) % T.starts.size()].id);
            for (const ParamDef& p : T.params) r.params[p.id] = p.min + (p.max - p.min) * (double)(Rng(seed) % 1001) / 1000.0;
            for (const SlotDef& s : T.slots) r.slotDesign[s.id] = s.designs[Rng(seed) % s.designs.size()].id;
            const auto parts = Generate(r);
            std::string w1, w2;
            if (!Sound(parts, &w1) || !DisjointR(parts, r, &w2)) { ++bad2; std::cerr << "  fuzz " << id << ": " << w1 << " " << w2 << "\n"; }
        }
        CHECK(bad2 == 0);
    }

    // ---- component-level parts on a human -------------------------------------------------------------------------
    {
        const Template& T = Tpl("human");
        const Recipe a = DefaultRecipe(T, "average");
        const auto parts = Generate(a);
        CHECK(ById(parts, "head/0") && ById(parts, "torso/0") && ById(parts, "hair/0") && ById(parts, "eyes/0") && ById(parts, "nose/0") && ById(parts, "mouth/0") && ById(parts, "ears/0") && ById(parts, "neck/0"));
        CHECK(Count(parts, "arm/") == 4 && Count(parts, "leg/") == 4 && Count(parts, "hand/") == 2 && Count(parts, "foot/") == 2);
        CHECK(ById(parts, "shirt/0") && ById(parts, "pants/0") && !ById(parts, "hat/0") && !ById(parts, "cape/0"));
        // swap one hand
        Recipe h = a; h.partDesign["hand/0"] = "fingers";
        int changed = 0;
        for (const auto& p : Generate(h)) if (Hash(p.mesh) != Hash(ById(parts, p.id)->mesh)) { ++changed; CHECK(p.id == "hand/0"); }
        CHECK(changed == 1);
        // every design of head, hair, hat, glasses, shirt, pants, hand, foot differs
        for (const char* sl : {"head", "hair", "hat", "glasses", "shirt", "pants", "hand", "foot", "torso", "backpack", "cape", "shoulder", "eyes", "brows", "nose", "mouth", "ears"}) {
            const SlotDef* sd = nullptr;
            for (const SlotDef& s : T.slots) if (s.id == sl) sd = &s;
            std::set<std::string> hs;
            for (const DesignDef& d : sd->designs) { Recipe x = a; x.slotDesign[sl] = d.id; hs.insert(Hash(Create(x))); }
            if (hs.size() != sd->designs.size()) std::cerr << "  slot " << sl << " designs not all different\n";
            CHECK(hs.size() == sd->designs.size());
        }
        // proportions
        Vec3 lo, hi, lo2, hi2;
        Bounds(Assemble(parts), lo, hi);
        Recipe tall = a; tall.params["height"] = 2.1;
        Bounds(Assemble(Generate(tall)), lo2, hi2);
        CHECK(hi2.y > hi.y + 0.25f);
        Recipe wide = a; wide.params["shoulderWidth"] = 0.7;
        Bounds(Assemble(Generate(wide)), lo2, hi2);
        CHECK((hi2.x - lo2.x) > (hi.x - lo.x) + 0.15f);
        Recipe big = a; big.params["headSize"] = 0.4;
        Vec3 hl, hh, hl2, hh2;
        Bounds(ById(parts, "head/0")->mesh, hl, hh); Bounds(ById(Generate(big), "head/0")->mesh, hl2, hh2);
        CHECK((hh2.y - hl2.y) > (hh.y - hl.y) + 0.1f);
        Recipe pose = a; pose.params["armAngle"] = 80;
        Vec3 al, ah, al2, ah2;
        Bounds(ById(parts, "hand/1")->mesh, al, ah); Bounds(ById(Generate(pose), "hand/1")->mesh, al2, ah2);
        CHECK(ah2.x > ah.x + 0.2f);
        // accessories add parts
        Recipe acc = a; acc.slotDesign["hat"] = "cap"; acc.slotDesign["glasses"] = "round"; acc.slotDesign["backpack"] = "small"; acc.slotDesign["belt"] = "belt"; acc.slotDesign["cape"] = "long";
        CHECK(Generate(acc).size() == parts.size() + 5);
        // a child is smaller than an adult
        Vec3 cl, ch;
        Bounds(Assemble(Generate(DefaultRecipe(T, "child"))), cl, ch);
        CHECK(ch.y < 1.3f && hi.y > 1.6f);
    }
    {
        const Template& R = Tpl("robot");
        const auto p = Generate(DefaultRecipe(R, "standard"));
        CHECK(ById(p, "antenna/0") && ById(p, "head/0") && ById(p, "hand/0"));
        Recipe x = DefaultRecipe(R, "standard"); x.slotDesign["antenna"] = "none";
        CHECK(!ById(Generate(x), "antenna/0"));
        std::set<std::string> feet;
        for (const DesignDef& d : R.slots[8].designs) { Recipe y = DefaultRecipe(R, "standard"); y.slotDesign["foot"] = d.id; feet.insert(Hash(ById(Generate(y), "foot/0")->mesh)); }
        CHECK(R.slots[8].id == "foot" && feet.size() == R.slots[8].designs.size());
    }
    {   // creature body plans
        const Template& C = Tpl("creature");
        auto legs = [&](const char* start) { return Count(Generate(DefaultRecipe(C, start)), "leg/"); };
        CHECK(legs("dog") == 4 && legs("eagle") == 2 && legs("beetle") == 6 && legs("spider") == 8 && legs("dragon") == 4);
        CHECK(Count(Generate(DefaultRecipe(C, "dragon")), "wing/") == 2 && Count(Generate(DefaultRecipe(C, "dog")), "wing/") == 0);
        CHECK(Count(Generate(DefaultRecipe(C, "beetle")), "antenna/") == 2 && Count(Generate(DefaultRecipe(C, "dog")), "antenna/") == 0);
        Recipe d = DefaultRecipe(C, "dog"); d.slotDesign["ears"] = "pointed";
        CHECK(Hash(Create(d)) != Hash(Create(DefaultRecipe(C, "dog"))));
        Recipe w = DefaultRecipe(C, "dragon"); Vec3 l1, h1, l2, h2;
        Bounds(Assemble(Generate(w)), l1, h1);
        w.params["wingSpan"] = 4.0;
        Bounds(Assemble(Generate(w)), l2, h2);
        CHECK((h2.x - l2.x) > (h1.x - l1.x) + 0.5f);
    }

    // ---- the safe-regeneration rules on a character ---------------------------------------------------------------
    {
        const Template& T = Tpl("human");
        const Recipe a = DefaultRecipe(T, "average");
        PolyMesh m = Create(a);
        PolyMesh box;
        const float s = 0.15f;
        const Vec3 c[8] = {{-s, 1.9f, -s}, {s, 1.9f, -s}, {s, 2.1f, -s}, {-s, 2.1f, -s}, {-s, 1.9f, s}, {s, 1.9f, s}, {s, 2.1f, s}, {-s, 2.1f, s}};
        for (const Vec3& p : c) box.verts.push_back(p);
        const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
        for (auto& f : q) { PolyFace pf; pf.verts = {f[0], f[1], f[2], f[3]}; box.faces.push_back(pf); }
        const int boxStart = (int)m.faces.size();
        shapes::Append(m, box);
        const Analysis an1 = Analyze(m, a);
        CHECK(an1.ok && an1.customFaces == 6 && an1.Count(PartState::Edited) == 0);
        PolyMesh edited = m;
        std::set<int> vs;
        for (int i = 0; i < (int)an1.faceOwner.size(); ++i) if (an1.faceOwner[i] == "hand/1") for (int v : edited.faces[i].verts) vs.insert(v);
        for (int v : vs) edited.verts[v].y += 0.1f;
        // a change to the hair and clothes: the moved hand is not touched
        Recipe hair = a; hair.slotDesign["hair"] = "long"; hair.slotDesign["hat"] = "tophat"; hair.colors["shirt"] = Color{0.1f, 0.6f, 0.2f, 1};
        const RegenResult r1 = Regenerate(edited, a, hair);
        CHECK(r1.ok && !r1.needsDecision);
        for (int i = 0; i < 6; ++i) CHECK(HasFaceLike(r1.mesh, edited, boxStart + i));
        CHECK(Analyze(r1.mesh, r1.recipe).Count(PartState::Edited) == 1);
        // a change to hands asks
        Recipe hands = a; hands.slotDesign["hand"] = "fist";
        const RegenResult ask = Regenerate(edited, a, hands);
        CHECK(!ask.ok && ask.needsDecision && Hash(ask.mesh) == Hash(edited));
        RegenOptions keep; keep.all = Decision::Keep;
        CHECK(Regenerate(edited, a, hands, keep).ok);
        // taller with no edits is exact
        Recipe tall = a; tall.params["height"] = 2.0;
        CHECK(Hash(Regenerate(Create(a), a, tall).mesh) == Hash(Create(tall)));
        // user part as a hat
        std::vector<int> hf;
        for (int i = 0; i < (int)an1.faceOwner.size(); ++i) if (an1.faceOwner[i] == "hand/0") hf.push_back(i);
        UserPart up; std::string err;
        CHECK(MakeUserPart(m, hf, "My hat", "hat", up, &err));
        Recipe u = a; u.userParts[up.id] = up; u.slotDesign["hat"] = "user:" + up.id;
        CHECK(Sound(Generate(u)) && ById(Generate(u), "hat/0") != nullptr);
        Recipe u2; CHECK(Recipe::Parse(u.Serialize(), u2, &err) && Hash(Create(u2)) == Hash(Create(u)));
    }

    TEST_MAIN_RESULT();
}
