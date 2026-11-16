#include "test_framework.hpp"
#include "okay/Modeling/Guided.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include <chrono>
#include <cmath>
#include "okay/Modeling/MeshAsset.hpp"
#include <filesystem>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>

using namespace okay;
using namespace okay::modeling;
using namespace okay::modeling::guided;

// Guided modeling, generator quality: body silhouettes, connected parts, validator, randomise, round trip, timing (docs/guided_modeling_design.md).
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

namespace {
std::vector<char> Silhouette(const PolyMesh& m, int W, int H, float x0, float x1, float y0, float y1) {   // side view, filled
    std::vector<char> g((size_t)W * H, 0);
    for (int f = 0; f < (int)m.faces.size(); ++f) {
        const std::vector<int> tri = m.TriangulateFace(f);
        for (size_t k = 0; k + 2 < tri.size(); k += 3) {
            float sx[3], sy[3];
            for (int i = 0; i < 3; ++i) { const Vec3& p = m.verts[m.faces[f].verts[tri[k + i]]]; sx[i] = (p.x - x0) / (x1 - x0) * W; sy[i] = (p.y - y0) / (y1 - y0) * H; }
            const int minx = std::max(0, (int)std::floor(std::min({sx[0], sx[1], sx[2]}))), maxx = std::min(W - 1, (int)std::ceil(std::max({sx[0], sx[1], sx[2]})));
            const int miny = std::max(0, (int)std::floor(std::min({sy[0], sy[1], sy[2]}))), maxy = std::min(H - 1, (int)std::ceil(std::max({sy[0], sy[1], sy[2]})));
            const float den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2]);
            if (std::fabs(den) < 1e-9f) continue;
            for (int y = miny; y <= maxy; ++y) for (int x = minx; x <= maxx; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float l0 = ((sy[1] - sy[2]) * (px - sx[2]) + (sx[2] - sx[1]) * (py - sy[2])) / den, l1 = ((sy[2] - sy[0]) * (px - sx[2]) + (sx[0] - sx[2]) * (py - sy[2])) / den, l2 = 1 - l0 - l1;
                if (l0 >= -1e-3f && l1 >= -1e-3f && l2 >= -1e-3f) g[(size_t)y * W + x] = 1;
            }
        }
    }
    return g;
}
double Iou(const std::vector<char>& a, const std::vector<char>& b) { long in = 0, un = 0; for (size_t i = 0; i < a.size(); ++i) { in += a[i] && b[i]; un += a[i] || b[i]; } return un ? (double)in / (double)un : 0.0; }
double Ms(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
}

int main() {
    RUN_SUITE("guided quality");
    // ---- guided_silhouette: the car body styles are different shapes, not one body re-coloured -----------------------
    {
        const Template& T = Tpl("car");
        const char* styles[] = {"sedan", "hatchback", "coupe", "suv", "pickup", "sports", "van"};
        std::vector<std::vector<char>> sil;
        for (const char* st : styles) {
            Recipe r = DefaultRecipe(T, st);
            // the same length, height, wheelbase and clearance for all: only the body style differs
            r.params["length"] = 4.4; r.params["height"] = 1.45; r.params["width"] = 1.8; r.params["groundClear"] = 0.18; r.params["wheelbase"] = 2.7; r.params["wheelRadius"] = 0.33;
            r.slotDesign["spoiler"] = "none"; r.slotDesign["roof"] = "none"; r.params["cabinScale"] = 1.0; r.params["cabinShift"] = 0.0;
            sil.push_back(Silhouette(Create(r), 176, 60, -2.5f, 2.5f, 0.0f, 1.7f));
        }
        double sum = 0, worst = 0; int cnt = 0;
        for (size_t i = 0; i < sil.size(); ++i) for (size_t j = i + 1; j < sil.size(); ++j) { const double v = Iou(sil[i], sil[j]); sum += v; ++cnt; worst = std::max(worst, v); }
        std::printf("silhouette: mean pairwise IoU %.3f, worst pair %.3f\n", sum / cnt, worst);
        CHECK(worst < 0.93);          // no two styles are the same outline
        CHECK(sum / cnt < 0.84);      // and on average clearly different
    }
    // ---- guided_connected and guided_arches: every car is one connected object, the wheels sit in their arches ---------
    {
        const Template& T = Tpl("car");
        for (const StartDesign& st : T.starts) {
            const Recipe r = DefaultRecipe(T, st.id);
            const auto parts = Generate(r);
            const ValidationReport rep = Validate(parts);
            if (rep.components != 1) std::cerr << "  car/" << st.id << " has " << rep.components << " separate groups\n";
            CHECK(rep.components == 1 && rep.Ok());
            // arches: the body has no material under the arch top at each axle, and each wheel is centred on its arch
            const float wb = (float)r.Param("wheelbase"), wr = (float)r.Param("wheelRadius"), gc = (float)r.Param("groundClear");
            const PolyMesh& body = ById(parts, "body/0")->mesh;
            for (float ax : {wb / 2, -wb / 2}) {
                int inside = 0;
                for (const Vec3& v : body.verts) if (std::fabs(v.x - ax) < wr * 0.5f && v.y < wr + wr * 0.4f && v.y > gc + 0.02f && v.y < wr * 1.9f) ++inside;
                CHECK(inside == 0);
            }
            for (int w = 0; w < 4; ++w) {
                Vec3 lo, hi; Bounds(ById(parts, "wheel/" + std::to_string(w))->mesh, lo, hi);
                const float cxw = (lo.x + hi.x) * 0.5f;
                CHECK(std::fabs(std::fabs(cxw) - wb / 2) < 0.05f);   // centred on an axle
                CHECK(std::fabs(lo.y) < 0.02f);                      // standing on the ground
            }
        }
    }
    // ---- guided_validator: every template and level reports a sound model ---------------------------------------------
    RegisterBuiltinTemplates();
    {
        int fails = 0;
        for (const Template& T : Registry::Instance().All())
            for (const StartDesign& st : T.starts)
                for (double lv : {0.0, 1.0, 2.0, 3.0}) {
                    Recipe r = DefaultRecipe(T, st.id); r.params["detail"] = lv;
                    const ValidationReport rep = Validate(Generate(r), lv < 2.5);
                    if (!rep.Ok() || rep.parts == 0) { ++fails; std::cerr << "  " << T.id << "/" << st.id << " level " << lv << ": " << (rep.problems.empty() ? "no parts" : rep.problems[0]) << "\n"; }
                }
        CHECK(fails == 0);
    }
    // ---- guided_randomise: repeatable, within the style, 1000 seeds per template with no validation failure -----------
    {
        int total = 0, fails = 0;
        for (const Template& T : Registry::Instance().All()) {
            const Recipe base = DefaultRecipe(T, T.starts.front().id);
            CHECK(Hash(Create(Randomise(T, base, 5))) == Hash(Create(Randomise(T, base, 5))));      // repeatable
            std::set<std::string> distinct;
            for (uint32_t seed = 1; seed <= 1000; ++seed) {
                const Recipe& b = T.starts[seed % T.starts.size()].id.empty() ? base : base;
                (void)b;
                Recipe from = DefaultRecipe(T, T.starts[seed % T.starts.size()].id);
                Recipe r = Randomise(T, from, seed);
                // keep the generation cost sane: the fuzz runs at Medium
                const auto parts = Generate(r);
                const ValidationReport rep = Validate(parts);
                ++total;
                if (!rep.Ok()) { ++fails; if (fails < 6) std::cerr << "  " << T.id << " seed " << seed << ": " << (rep.problems.empty() ? "?" : rep.problems[0]) << "\n"; }
                if (seed <= 30) distinct.insert(Hash(Assemble(parts)));
            }
            CHECK(distinct.size() >= 25);   // different seeds give different models
        }
        std::printf("randomise: %d models validated, %d failures\n", total, fails);
        CHECK(fails == 0);
    }
    // ---- guided_roundtrip: generate, customise, edit by hand, save, reopen, edit again, regenerate -------------------------
    {
        const std::string dir = (std::filesystem::temp_directory_path() / "okay_guided_roundtrip").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const Template& T = Tpl("car");
        Recipe a = DefaultRecipe(T, "coupe");
        a.params["height"] = 1.3;                 // customised
        a.slotDesign["wheel"] = "mesh";
        PolyMesh m = Create(a);
        // manual edit 1: pull a headlight forward; manual edit 2: add a custom box on the roof
        const Analysis an0 = Analyze(m, a);
        std::set<int> vs;
        for (int i = 0; i < (int)an0.faceOwner.size(); ++i) if (an0.faceOwner[i] == "headlight/0") for (int v : m.faces[i].verts) vs.insert(v);
        for (int v : vs) m.verts[v].x += 0.05f;
        PolyMesh box;
        const float s = 0.2f;
        const Vec3 c[8] = {{-s, 0, -s}, {s, 0, -s}, {s, s, -s}, {-s, s, -s}, {-s, 0, s}, {s, 0, s}, {s, s, s}, {-s, s, s}};
        for (const Vec3& p : c) box.verts.push_back(p + Vec3{0, 1.35f, 0});
        const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
        for (auto& f : q) { PolyFace pf; pf.verts = {f[0], f[1], f[2], f[3]}; box.faces.push_back(pf); }
        const int box1 = (int)m.faces.size();
        shapes::Append(m, box);
        // save: the model as a mesh asset, the recipe next to it, the design as a reusable user template
        const std::string meshPath = dir + "/Coupe.okaymesh";
        std::string err;
        CHECK(SaveMeshAsset(meshPath, m.ToMesh(), &err));
        CHECK(SaveRecipe(SidecarPath(meshPath), a, &err));
        UserTemplate ut; ut.name = "My coupe"; ut.recipe = a;
        CHECK(SaveUserTemplate(dir + "/mycoupe.okaytemplate", ut, &err));
        // reopen
        Mesh loaded; CHECK(LoadMeshAsset(meshPath, loaded, nullptr, &err));
        Recipe a2; CHECK(LoadRecipe(SidecarPath(meshPath), a2, &err));
        PolyMesh m2 = PolyMesh::FromMesh(loaded, {false, true, 1.0f});
        const Analysis an1 = Analyze(m2, a2);
        CHECK(an1.ok && an1.customFaces == 6 && an1.Count(PartState::Edited) == 1);    // the box and the pulled headlight are known
        // edit again: another box far away
        PolyMesh box2 = box; for (Vec3& v : box2.verts) v.x += 4;
        const int box2Start = (int)m2.faces.size();
        shapes::Append(m2, box2);
        // regenerate with an unrelated change (body colour and a roof fitting): both boxes and the pulled headlight survive, no question asked
        Recipe b = a2; b.colors["body"] = Color{0.1f, 0.5f, 0.2f, 1}; b.slotDesign["roof"] = "rack";
        const RegenResult rr = Regenerate(m2, a2, b);
        CHECK(rr.ok && !rr.needsDecision);
        for (int i = 0; i < 6; ++i) { CHECK(HasFaceLike(rr.mesh, m2, box1 + i)); CHECK(HasFaceLike(rr.mesh, m2, box2Start + i)); }
        CHECK(Analyze(rr.mesh, rr.recipe).Count(PartState::Edited) == 1);
        // a change that touches the pulled headlight asks first
        Recipe c2 = a2; c2.slotDesign["headlight"] = "twin";
        CHECK(Regenerate(m2, a2, c2).needsDecision);
        // the saved template makes the same car again
        const auto temps = LoadUserTemplates(dir);
        CHECK(temps.size() == 1 && Hash(Create(temps[0].recipe)) == Hash(Create(a)));
        // save the regenerated result and open it once more
        CHECK(SaveMeshAsset(meshPath, rr.mesh.ToMesh(), &err) && SaveRecipe(SidecarPath(meshPath), rr.recipe, &err));
        Mesh again; Recipe a3;
        CHECK(LoadMeshAsset(meshPath, again, nullptr, &err) && LoadRecipe(SidecarPath(meshPath), a3, &err));
        const Analysis an3 = Analyze(PolyMesh::FromMesh(again, {false, true, 1.0f}), a3);
        CHECK(an3.ok && an3.customFaces == 12 && an3.Count(PartState::Edited) == 1);
    }
    // ---- guided_perf: milliseconds per template and level (printed; a loose limit keeps it from being flaky) ---------------
    {
        std::printf("PERF template/design level0 level1 level2 level3 (ms) triangles(level3)\n");
        double worstUltra = 0;
        for (const Template& T : Registry::Instance().All()) {
            const StartDesign& st = T.starts.front();
            double ms[4]; long tris = 0;
            for (int lv = 0; lv < 4; ++lv) {
                Recipe r = DefaultRecipe(T, st.id); r.params["detail"] = lv;
                const auto t0 = std::chrono::steady_clock::now();
                const PolyMesh m = Create(r);
                ms[lv] = Ms(t0);
                if (lv == 3) tris = TriangleCount(m);
            }
            worstUltra = std::max(worstUltra, ms[3]);
            std::printf("PERF %s/%s %.1f %.1f %.1f %.1f %ld\n", T.id.c_str(), st.id.c_str(), ms[0], ms[1], ms[2], ms[3], tris);
        }
        CHECK(worstUltra < 5000.0);
    }
    TEST_MAIN_RESULT();
}
