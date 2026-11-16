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

// Guided modeling, vehicles: car, truck, bus, motorcycle and trailer (docs/guided_modeling_design.md).
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
    RUN_SUITE("guided vehicles");
    const char* ids[] = {"car", "truck", "bus", "motorcycle", "trailer"};

    // ---- every start design: sound, disjoint, deterministic, distinct, stands on the ground -------------------
    for (const char* id : ids) {
        const Template& T = Tpl(id);
        CHECK(T.category == "Vehicles");
        CHECK(T.starts.size() >= 6);
        std::set<std::string> distinct;
        for (const StartDesign& st : T.starts) {
            const Recipe r = DefaultRecipe(T, st.id);
            const auto parts = Generate(r);
            std::string why, why2;
            const bool ok = Sound(parts, &why), dj = Disjoint(parts, &why2);
            if (!ok) std::cerr << "  " << id << "/" << st.id << " not sound: " << why << "\n";
            if (!dj) std::cerr << "  " << id << "/" << st.id << " not disjoint: " << why2 << "\n";
            CHECK(parts.size() > 8 && ok && dj);
            CHECK(Hash(Assemble(parts)) == Hash(Create(r)));
            Recipe r2; std::string err;
            CHECK(Recipe::Parse(r.Serialize(), r2, &err) && Hash(Create(r2)) == Hash(Create(r)));
            distinct.insert(Hash(Assemble(parts)));
            Vec3 lo, hi; Bounds(Assemble(parts), lo, hi);
            CHECK(std::fabs(lo.y) < 0.05f);          // the wheels stand on y = 0
            CHECK(std::fabs((lo.z + hi.z) * 0.5f) < 0.2f);   // centred across the width
        }
        CHECK(distinct.size() == T.starts.size());
    }

    // ---- every slot design on every start, every parameter at its limits, fuzz ------------------------------------
    for (const char* id : ids) {
        const Template& T = Tpl(id);
        for (const StartDesign& st : T.starts) {
            for (const SlotDef& sl : T.slots)
                for (const DesignDef& d : sl.designs) {
                    Recipe r = DefaultRecipe(T, st.id);
                    r.slotDesign[sl.id] = d.id;
                    std::string why;
                    const auto parts = Generate(r);
                    const bool ok = Sound(parts, &why) && Disjoint(parts, &why);
                    if (!ok) std::cerr << "  " << id << "/" << st.id << " slot " << sl.id << "=" << d.id << ": " << why << "\n";
                    CHECK(ok);
                }
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
        }
        Recipe bad = DefaultRecipe(T);
        for (const ParamDef& p : T.params) bad.params[p.id] = (p.id.size() % 2) ? 1e9 : std::nan("");
        CHECK(Sound(Generate(bad)));
        uint32_t seed = 4242;
        int bad2 = 0;
        for (int i = 0; i < 80; ++i) {
            Recipe r = DefaultRecipe(T, T.starts[Rng(seed) % T.starts.size()].id);
            for (const ParamDef& p : T.params) r.params[p.id] = p.min + (p.max - p.min) * (double)(Rng(seed) % 1001) / 1000.0;
            for (const SlotDef& s : T.slots) r.slotDesign[s.id] = s.designs[Rng(seed) % s.designs.size()].id;
            const auto parts = Generate(r);
            std::string w1, w2;
            if (!Sound(parts, &w1) || !Disjoint(parts, &w2)) { ++bad2; std::cerr << "  fuzz " << id << ": " << w1 << " " << w2 << "\n"; }
        }
        CHECK(bad2 == 0);
    }

    // ---- component-level parts: wheels, lights, bumpers, mirrors, glass -----------------------------------------
    {
        const Template& T = Tpl("car");
        const Recipe a = DefaultRecipe(T, "sedan");
        const auto parts = Generate(a);
        CHECK(Count(parts, "wheel/") == 4 && Count(parts, "headlight/") == 2 && Count(parts, "taillight/") == 2);
        CHECK(Count(parts, "bumper/") == 2 && Count(parts, "mirror/") == 2 && Count(parts, "glass/") == 4);
        CHECK(ById(parts, "body/0") && ById(parts, "cabin/0") && ById(parts, "grille/0"));
        // swap one wheel only
        Recipe w = a; w.partDesign["wheel/1"] = "spoke";
        const auto pw = Generate(w);
        int changed = 0;
        for (const auto& p : pw) if (Hash(p.mesh) != Hash(ById(parts, p.id)->mesh)) { ++changed; CHECK(p.id == "wheel/1"); }
        CHECK(changed == 1);
        // wheel size changes the wheels and lifts the body is not required, but the car still stands on the ground
        Recipe big = a; big.params["wheelRadius"] = 0.5; big.params["wheelWidth"] = 0.3;
        Vec3 lo, hi, lo2, hi2;
        Bounds(ById(parts, "wheel/0")->mesh, lo, hi);
        Bounds(ById(Generate(big), "wheel/0")->mesh, lo2, hi2);
        CHECK(std::fabs((hi2.y - lo2.y) - 1.0f) < 0.02f && (hi2.y - lo2.y) > (hi.y - lo.y) + 0.2f);
        CHECK(std::fabs(lo2.y) < 0.02f);
        // wheel position: the wheelbase moves the axles
        Recipe wb = a; wb.params["wheelbase"] = 3.4;
        Vec3 l3, h3;
        Bounds(ById(Generate(wb), "wheel/0")->mesh, l3, h3);
        CHECK(std::fabs((l3.x + h3.x) * 0.5f - (lo.x + hi.x) * 0.5f) > 0.3f);
        // body proportions: length, width, height
        Recipe prop = a; prop.params["length"] = 5.2; prop.params["height"] = 1.7; prop.params["width"] = 2.0;
        Vec3 bl, bh;
        Bounds(Assemble(Generate(prop)), bl, bh);
        CHECK(bh.y > 1.65f && (bh.x - bl.x) > 5.2f && (bh.z - bl.z) > 1.95f);
        // headlight and bumper designs differ
        std::set<std::string> hs, bs;
        for (const DesignDef& d : SlotOf(T, "headlight").designs) { Recipe x = a; x.slotDesign["headlight"] = d.id; hs.insert(Hash(ById(Generate(x), "headlight/0")->mesh)); }
        CHECK(hs.size() == SlotOf(T, "headlight").designs.size() && hs.size() >= 4);
        for (const DesignDef& d : SlotOf(T, "bumper").designs) { Recipe x = a; x.slotDesign["bumper"] = d.id; bs.insert(Hash(ById(Generate(x), "bumper/0")->mesh)); }
        CHECK(bs.size() == SlotOf(T, "bumper").designs.size() && bs.size() >= 4);
        std::set<std::string> wh;
        for (const DesignDef& d : SlotOf(T, "wheel").designs) { Recipe x = a; x.slotDesign["wheel"] = d.id; wh.insert(Hash(ById(Generate(x), "wheel/0")->mesh)); }
        CHECK(wh.size() == SlotOf(T, "wheel").designs.size() && wh.size() >= 6);
        for (const char* sl : {"spoiler", "roof", "exhaust"}) {
            std::set<std::string> hh;
            for (const DesignDef& d : SlotOf(T, sl).designs) { Recipe x = a; x.slotDesign[sl] = d.id; hh.insert(Hash(Create(x))); }
            CHECK(hh.size() == SlotOf(T, sl).designs.size());
        }
        // the body styles are different shapes, and pickup has a bed
        std::set<std::string> bodies;
        for (const DesignDef& d : SlotOf(T, "body").designs) { Recipe x = a; x.slotDesign["body"] = d.id; bodies.insert(Hash(Create(x))); }
        CHECK(bodies.size() == 7);
        Recipe pk = a; pk.slotDesign["body"] = "pickup";
        CHECK(ById(Generate(pk), "bed/0") != nullptr && ById(parts, "bed/0") == nullptr);
        // none removes the part
        Recipe none = a; none.slotDesign["mirror"] = "none";
        CHECK(Count(Generate(none), "mirror/") == 0);
    }
    {   // counts that follow the settings
        CHECK(Count(Generate(DefaultRecipe(Tpl("truck"), "tanker")), "wheel/") == 2 + 2 * 3);
        Recipe t = DefaultRecipe(Tpl("truck"), "box_truck"); t.params["rearAxles"] = 4;
        CHECK(Count(Generate(t), "wheel/") == 10);
        Recipe bus = DefaultRecipe(Tpl("bus"), "city"); bus.params["windows"] = 7;
        CHECK(Count(Generate(bus), "window/0/") == 7 && Count(Generate(bus), "window/1/") == 7);
        Recipe tr = DefaultRecipe(Tpl("trailer"), "box"); tr.params["axles"] = 3;
        CHECK(Count(Generate(tr), "wheel/") == 6);
        CHECK(Count(Generate(DefaultRecipe(Tpl("motorcycle"), "cruiser")), "wheel/") == 2);
        // a bike's wheels have different sizes
        Recipe m = DefaultRecipe(Tpl("motorcycle"), "standard"); m.params["frontWheelRadius"] = 0.25; m.params["rearWheelRadius"] = 0.4;
        const auto mp = Generate(m);
        Vec3 l0, h0, l1, h1;
        Bounds(ById(mp, "wheel/0")->mesh, l0, h0); Bounds(ById(mp, "wheel/1")->mesh, l1, h1);
        CHECK(std::fabs((h0.y - l0.y) - 0.5f) < 0.02f && std::fabs((h1.y - l1.y) - 0.8f) < 0.02f);
        // truck cab and cargo are swappable designs
        std::set<std::string> cargos;
        for (const DesignDef& d : SlotOf(Tpl("truck"), "cargo").designs) { Recipe x = DefaultRecipe(Tpl("truck"), "box_truck"); x.slotDesign["cargo"] = d.id; cargos.insert(Hash(ById(Generate(x), "cargo/0")->mesh)); }
        CHECK(cargos.size() == 4);
        std::set<std::string> cabs;
        for (const DesignDef& d : SlotOf(Tpl("truck"), "cab").designs) { Recipe x = DefaultRecipe(Tpl("truck"), "box_truck"); x.slotDesign["cab"] = d.id; cabs.insert(Hash(ById(Generate(x), "cab/0")->mesh)); }
        CHECK(cabs.size() == 2);
    }

    // ---- the safe-regeneration rules on a car ----------------------------------------------------------------------
    {
        const Template& T = Tpl("car");
        const Recipe a = DefaultRecipe(T, "sedan");
        PolyMesh m = Create(a);
        const Analysis an0 = Analyze(m, a);
        CHECK(an0.ok && an0.Count(PartState::Intact) == (int)Generate(a).size() && an0.customFaces == 0);
        // a custom roof box on the car
        PolyMesh box;
        const Vec3 c[8] = {{-0.3f, 1.5f, -0.3f}, {0.3f, 1.5f, -0.3f}, {0.3f, 1.7f, -0.3f}, {-0.3f, 1.7f, -0.3f}, {-0.3f, 1.5f, 0.3f}, {0.3f, 1.5f, 0.3f}, {0.3f, 1.7f, 0.3f}, {-0.3f, 1.7f, 0.3f}};
        for (const Vec3& p : c) box.verts.push_back(p + Vec3{0.2f, 0.02f, 0});
        const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
        for (auto& f : q) { PolyFace pf; pf.verts = {f[0], f[1], f[2], f[3]}; box.faces.push_back(pf); }
        const int boxStart = (int)m.faces.size();
        shapes::Append(m, box);
        // pull the front-left wheel out (the user reshaped it): edited
        const Analysis an1 = Analyze(m, a);
        PolyMesh edited = m;
        std::set<int> vs;
        for (int i = 0; i < (int)an1.faceOwner.size(); ++i) if (an1.faceOwner[i] == "wheel/0") for (int v : edited.faces[i].verts) vs.insert(v);
        for (int v : vs) edited.verts[v].x += 0.2f;
        const Analysis an2 = Analyze(edited, a);
        int ed = 0;
        for (const PartInfo& p : an2.parts) if (p.state == PartState::Edited) { ++ed; CHECK(p.id == "wheel/0"); }
        CHECK(ed == 1);
        // change the headlights and the grille: wheel/0 untouched, so no question; the box and the moved wheel survive
        Recipe hl = a; hl.slotDesign["headlight"] = "slim"; hl.colors["body"] = Color{0.1f, 0.5f, 0.2f, 1};
        const RegenResult r1 = Regenerate(edited, a, hl);
        CHECK(r1.ok && !r1.needsDecision);
        for (int i = 0; i < 6; ++i) CHECK(HasFaceLike(r1.mesh, edited, boxStart + i));
        CHECK(Analyze(r1.mesh, r1.recipe).Count(PartState::Edited) == 1);
        // change the wheel size: that wheel is touched, so it asks and nothing changes
        Recipe bigw = a; bigw.params["wheelRadius"] = 0.45;
        const RegenResult ask = Regenerate(edited, a, bigw);
        CHECK(!ask.ok && ask.needsDecision && ask.conflicts.size() == 1 && ask.conflicts[0].id == "wheel/0");
        CHECK(Hash(ask.mesh) == Hash(edited));
        RegenOptions keep; keep.all = Decision::Keep;
        const RegenResult kept = Regenerate(edited, a, bigw, keep);
        CHECK(kept.ok && kept.recipe.detached.count("wheel/0") == 1);
        RegenOptions disc; disc.all = Decision::Discard;
        CHECK(Regenerate(edited, a, bigw, disc).ok);
        // switch the whole body style with no edits: exact
        Recipe suv = a; suv.slotDesign["body"] = "suv";
        CHECK(Hash(Regenerate(Create(a), a, suv).mesh) == Hash(Create(suv)));
        // a user wheel made from a wheel's faces works as a design
        std::vector<int> wf;
        for (int i = 0; i < (int)an1.faceOwner.size(); ++i) if (an1.faceOwner[i] == "wheel/1") wf.push_back(i);
        UserPart up; std::string err;
        CHECK(MakeUserPart(m, wf, "My wheel", "wheel", up, &err));
        Recipe u = a; u.userParts[up.id] = up; u.slotDesign["wheel"] = "user:" + up.id;
        const auto up2 = Generate(u);
        CHECK(Sound(up2) && Count(up2, "wheel/") == 4);
        Recipe u2; CHECK(Recipe::Parse(u.Serialize(), u2, &err) && Hash(Create(u2)) == Hash(Create(u)));
    }

    TEST_MAIN_RESULT();
}
