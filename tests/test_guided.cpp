#include "test_framework.hpp"
#include "okay/Modeling/Guided.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <set>
#include <string>

using namespace okay;
using namespace okay::modeling;
using namespace okay::modeling::guided;

// Guided modeling (docs/guided_modeling_design.md): the fence template, the regeneration contract, user parts and
// templates, files and undo. Test names in the output start with "guided_".
namespace {

const Template& Fence() { RegisterBuiltinTemplates(); return *Registry::Instance().Find("fence"); }

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
bool Finite(const PolyMesh& m) {
    for (const Vec3& v : m.verts) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
    return true;
}
// Every part is a sound closed solid with outward faces.
bool PartsSound(const std::vector<GeneratedPart>& parts, std::string* why = nullptr) {
    for (const GeneratedPart& p : parts) {
        const PolyMesh::Report r = p.mesh.Check();
        if (!Finite(p.mesh) || !r.Closed() || !r.Valid() || !(SignedVolume(p.mesh) > 1e-9)) {
            if (why) *why = p.id + " (closed " + std::to_string(r.Closed()) + ", valid " + std::to_string(r.Valid()) + ", volume " + std::to_string(SignedVolume(p.mesh)) + ")";
            return false;
        }
    }
    return true;
}
std::string Hash(const PolyMesh& m) { return m.Serialize(); }

// Two parts never share a corner position (so they are separate pieces).
bool PartsDisjoint(const std::vector<GeneratedPart>& parts) {
    std::set<std::tuple<long long, long long, long long>> seen;
    for (const GeneratedPart& p : parts) {
        std::set<std::tuple<long long, long long, long long>> mine;
        for (const Vec3& v : p.mesh.verts) mine.insert({std::llround(v.x * 1e4), std::llround(v.y * 1e4), std::llround(v.z * 1e4)});
        for (const auto& k : mine) if (seen.count(k)) return false;
        seen.insert(mine.begin(), mine.end());
    }
    return true;
}

const GeneratedPart* PartById(const std::vector<GeneratedPart>& v, const std::string& id) {
    for (const GeneratedPart& p : v) if (p.id == id) return &p;
    return nullptr;
}

// Which faces of `m` come from the part `id` of the recipe (by Analyze's owner list).
std::vector<int> FacesOf(const PolyMesh& m, const Recipe& r, const std::string& id) {
    const Analysis a = Analyze(m, r);
    std::vector<int> f;
    for (int i = 0; i < (int)a.faceOwner.size(); ++i) if (a.faceOwner[i] == id) f.push_back(i);
    return f;
}
void MoveFaces(PolyMesh& m, const std::vector<int>& faces, Vec3 d) {
    std::set<int> vs;
    for (int f : faces) for (int v : m.faces[f].verts) vs.insert(v);
    for (int v : vs) m.verts[v] = m.verts[v] + d;
}
PolyMesh CustomBox(Vec3 at) {
    PolyMesh b;
    const float s = 0.2f;
    const Vec3 c[8] = {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s}, {-s, -s, s}, {s, -s, s}, {s, s, s}, {-s, s, s}};
    for (const Vec3& p : c) b.verts.push_back(p + at);
    const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
    for (auto& q : f) { PolyFace pf; pf.verts = {q[0], q[1], q[2], q[3]}; b.faces.push_back(pf); }
    return b;
}
// Does `m` contain a face with exactly these corner positions?
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
// Same faces, in any order.
bool SameFaces(const PolyMesh& a, const PolyMesh& b) {
    if (a.faces.size() != b.faces.size()) return false;
    for (int i = 0; i < (int)a.faces.size(); ++i) if (!HasFaceLike(b, a, i)) return false;
    return true;
}
uint32_t Rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

std::string TmpDir(const char* name) {
    const std::filesystem::path d = std::filesystem::temp_directory_path() / (std::string("okay_guided_") + name);
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d.string();
}

} // namespace

int main() {
    RUN_SUITE("guided");
    const Template& T = Fence();

    // ---- guided_registry: categories, the fence is registered, other categories are honestly empty -------
    {
        CHECK(Registry::Categories().size() == 5);
        CHECK(Registry::Instance().InCategory("Architecture").size() >= 1);
        CHECK(Registry::Instance().InCategory("Vehicles").size() == 5);
        CHECK(Registry::Instance().InCategory("Environment").empty() && Registry::Instance().InCategory("Props").empty());
        CHECK(Registry::Instance().InCategory("Characters").size() == 3);
        CHECK(T.starts.size() >= 5);
        CHECK(T.slots.size() >= 5);
        RegisterBuiltinTemplates();
        CHECK(Registry::Instance().All().size() == 15);   // fence + 6 architecture + 5 vehicle + 3 character templates; registering twice does not duplicate
    }

    // ---- guided_styles: every start design makes sound, distinct, disjoint parts -------------------------
    std::set<std::string> distinct;
    for (const StartDesign& st : T.starts) {
        const Recipe r = DefaultRecipe(T, st.id);
        std::string why;
        const std::vector<GeneratedPart> parts = Generate(r);
        CHECK(parts.size() > 5);
        CHECK(PartsSound(parts, &why));
        if (!why.empty()) std::cerr << "  style " << st.id << ": " << why << "\n";
        CHECK(PartsDisjoint(parts));
        distinct.insert(Hash(Assemble(parts)));
        // a style with a gate
        Recipe g = r; g.params["gate"] = 1; g.params["length"] = 8;
        const std::vector<GeneratedPart> gp = Generate(g);
        CHECK(PartById(gp, "gate/leaf") != nullptr);
        CHECK(PartsSound(gp, &why));
        CHECK(PartsDisjoint(gp));
        // normals outward overall: the whole assembled fence has positive volume
        CHECK(SignedVolume(Assemble(parts)) > 0);
    }
    CHECK(distinct.size() == T.starts.size());   // guided_styles_distinct

    // ---- guided_determinism ------------------------------------------------------------------------------
    {
        const Recipe r = DefaultRecipe(T, "wrought_iron");
        CHECK(Hash(Create(r)) == Hash(Create(r)));
        Recipe r2;
        std::string err;
        CHECK(Recipe::Parse(r.Serialize(), r2, &err));
        CHECK(r2.Serialize() == r.Serialize());
        CHECK(Hash(Create(r2)) == Hash(Create(r)));
        // the mesh survives the .okaymesh text form exactly
        PolyMesh a = Create(r), b;
        CHECK(PolyMesh::Parse(a.Serialize(), b, &err));
        CHECK(Hash(a) == Hash(b));
    }

    // ---- guided_ranges: each parameter at its min and max, and a fuzz of random values -------------------
    {
        for (const StartDesign& st : T.starts)
            for (const ParamDef& p : T.params)
                for (double v : {p.min, p.max, (p.min + p.max) * 0.5}) {
                    Recipe r = DefaultRecipe(T, st.id);
                    r.params[p.id] = v;
                    std::string why;
                    const auto parts = Generate(r);
                    const bool ok = PartsSound(parts, &why) && PartsDisjoint(parts);
                    if (!ok) std::cerr << "  " << st.id << " " << p.id << "=" << v << " " << why << "\n";
                    CHECK(ok);
                }
        // out of range and NaN are clamped, not trusted
        Recipe bad = DefaultRecipe(T);
        bad.params["height"] = 1e9; bad.params["length"] = std::nan(""); bad.params["railCount"] = -5; bad.params["postSpacing"] = 0;
        CHECK(PartsSound(Generate(bad)));
        uint32_t seed = 12345;
        int bad2 = 0;
        for (int i = 0; i < 120; ++i) {
            Recipe r = DefaultRecipe(T, T.starts[Rng(seed) % T.starts.size()].id);
            for (const ParamDef& p : T.params) r.params[p.id] = p.min + (p.max - p.min) * (double)(Rng(seed) % 1001) / 1000.0;
            for (const SlotDef& s : T.slots) r.slotDesign[s.id] = s.designs[Rng(seed) % s.designs.size()].id;
            if (Rng(seed) % 3 == 0) r.path = {{0, 0, 0}, {3, 0.4f, 0}, {3, 0.2f, 4}, {-1, 0, 5}};
            const auto parts = Generate(r);
            if (!PartsSound(parts) || !PartsDisjoint(parts)) ++bad2;
        }
        CHECK(bad2 == 0);   // guided_fuzz
    }

    // ---- guided_path: a drawn path gets posts at every corner; a gate has a post at each edge -------------
    {
        Recipe r = DefaultRecipe(T, "picket");
        r.path = {{0, 0, 0}, {4, 0, 0}, {4, 0, 3}};
        const auto parts = Generate(r);
        std::set<std::tuple<long long, long long>> postXZ;
        for (const auto& p : parts) if (p.slot == "post") {
            Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
            for (const Vec3& v : p.mesh.verts) { lo.x = std::min(lo.x, v.x); hi.x = std::max(hi.x, v.x); lo.z = std::min(lo.z, v.z); hi.z = std::max(hi.z, v.z); }
            postXZ.insert({std::llround((lo.x + hi.x) * 50), std::llround((lo.z + hi.z) * 50)});
        }
        bool corner = false;
        for (auto& k : postXZ) if (std::get<0>(k) == 400 && std::get<1>(k) == 0) corner = true;   // (4,0)
        CHECK(corner);
        CHECK(PartsSound(parts));
        // ground follow: with a slope the last post stands higher than the first
        Recipe s = DefaultRecipe(T, "post_rail");
        s.params["slope"] = 20;
        const auto sp = Generate(s);
        float y0 = 1e9f, y1 = -1e9f;
        for (const auto& p : sp) if (p.id == "post/0") for (const Vec3& v : p.mesh.verts) y0 = std::min(y0, v.y);
        for (const auto& p : sp) if (p.slot == "post") for (const Vec3& v : p.mesh.verts) y1 = std::max(y1, v.y);
        CHECK(y1 > y0 + 1.0f);
        CHECK(PartsSound(sp));
    }

    // ---- guided_part_swap ----------------------------------------------------------------------------------
    {
        const Recipe a = DefaultRecipe(T, "picket");
        Recipe b = a;
        b.slotDesign["post"] = "round";
        const PolyMesh m = Create(a);
        const RegenResult r = Regenerate(m, a, b);
        CHECK(r.ok && !r.needsDecision);
        CHECK(Hash(r.mesh) != Hash(m));
        CHECK(r.recipe.slotDesign.at("post") == "round");
        CHECK(Hash(r.mesh) == Hash(Create(b)));   // nothing was hand-edited, so the result is exactly the new design
        // one part only
        Recipe c = a;
        c.partDesign["infill/0/2"] = "spear";
        const RegenResult r2 = Regenerate(m, a, c);
        CHECK(r2.ok);
        const auto pc = Generate(c), pa = Generate(a);
        CHECK(Hash(PartById(pc, "infill/0/2")->mesh) != Hash(PartById(pa, "infill/0/2")->mesh));
        CHECK(Hash(PartById(pc, "infill/0/3")->mesh) == Hash(PartById(pa, "infill/0/3")->mesh));
        CHECK(Hash(r2.mesh) == Hash(Create(c)));
    }

    // ---- guided_regen_custom: your own additions survive a parameter change -------------------------------
    {
        const Recipe a = DefaultRecipe(T, "picket");
        PolyMesh m = Create(a);
        const PolyMesh box = CustomBox({1.0f, 2.5f, 3.0f});
        const int boxStart = (int)m.faces.size();
        shapes::Append(m, box);
        CHECK(Analyze(m, a).customFaces == 6);
        Recipe b = a;
        b.params["height"] = 1.6;
        b.params["length"] = 9;
        b.params["railCount"] = 3;
        const RegenResult r = Regenerate(m, a, b);
        CHECK(r.ok && !r.needsDecision);
        for (int i = 0; i < 6; ++i) CHECK(HasFaceLike(r.mesh, m, boxStart + i));
        CHECK(r.customFaces == 6);
        const PolyMesh fresh = Create(b);
        CHECK((int)r.mesh.faces.size() == (int)fresh.faces.size() + 6);
        CHECK(Analyze(r.mesh, r.recipe).Count(PartState::Intact) == (int)Generate(b).size());
        CHECK(Analyze(r.mesh, r.recipe).customFaces == 6);
    }

    // ---- guided_regen_edited: hand edits are never overwritten silently ---------------------------------
    {
        const Recipe a = DefaultRecipe(T, "picket");
        PolyMesh m = Create(a);
        const std::vector<int> post1 = FacesOf(m, a, "post/1");
        CHECK(!post1.empty());
        MoveFaces(m, {post1[0]}, {0.05f, 0, 0});   // pull one face of post/1
        const Analysis an = Analyze(m, a);
        CHECK(an.ok);
        int edited = 0;
        for (const PartInfo& p : an.parts) if (p.state == PartState::Edited) { ++edited; CHECK(p.id == "post/1"); }
        CHECK(edited == 1);
        CHECK(an.Count(PartState::Deleted) == 0);

        // (1) a change that does not touch post/1: no question, the edit stays
        Recipe c1 = a;
        c1.colors["infill"] = Color{1, 0, 0, 1};
        c1.params["infillWidth"] = 0.07;
        RegenResult r1 = Regenerate(m, a, c1);
        CHECK(r1.ok && !r1.needsDecision);
        for (int f : post1) CHECK(HasFaceLike(r1.mesh, m, f));   // the pulled post is exactly as the user left it
        CHECK(Analyze(r1.mesh, r1.recipe).Count(PartState::Edited) == 1);
        CHECK(r1.recipe.detached.empty());

        // (2) a change that does: refuses, mesh and recipe unchanged
        Recipe c2 = a;
        c2.params["height"] = 1.5;
        RegenResult r2 = Regenerate(m, a, c2);
        CHECK(!r2.ok && r2.needsDecision);
        CHECK(r2.conflicts.size() == 1 && r2.conflicts[0].id == "post/1");
        CHECK(Hash(r2.mesh) == Hash(m));
        CHECK(r2.recipe.Serialize() == a.Serialize());

        // (3) keep my edits: the post detaches, stays as it was; the rest follows the new height
        RegenOptions keep; keep.all = Decision::Keep;
        RegenResult r3 = Regenerate(m, a, c2, keep);
        CHECK(r3.ok && r3.recipe.detached.count("post/1") == 1);
        {
            // post/1's original faces (the moved one and the rest) are all still there
            int found = 0;
            for (int f : post1) if (HasFaceLike(r3.mesh, m, f)) ++found;
            CHECK(found == (int)post1.size());   // every face of the edited post is still there, as edited
            CHECK(HasFaceLike(r3.mesh, m, post1[0]));
        }
        const PolyMesh want3 = Create(r3.recipe);
        CHECK((int)r3.mesh.faces.size() == (int)want3.faces.size() + (int)post1.size());
        // and it asks no more about post/1 on the next change
        Recipe c3 = r3.recipe; c3.params["height"] = 1.7;
        RegenResult r3b = Regenerate(r3.mesh, r3.recipe, c3);
        CHECK(r3b.ok && !r3b.needsDecision);

        // (4) discard my edits: post/1 is generated again at the new height
        RegenOptions disc; disc.all = Decision::Discard;
        RegenResult r4 = Regenerate(m, a, c2, disc);
        CHECK(r4.ok && r4.recipe.detached.empty());
        CHECK(Hash(r4.mesh) == Hash(Create(c2)));
        CHECK(r4.discarded == 1);

        // (5) per-part decisions
        RegenOptions per; per.perPart["post/1"] = Decision::Keep;
        CHECK(Regenerate(m, a, c2, per).ok);
    }

    // ---- guided_regen_unrelated: an edit to one picket survives changing the rails ---------------------
    {
        const Recipe a = DefaultRecipe(T, "picket");
        PolyMesh m = Create(a);
        const std::vector<int> pk = FacesOf(m, a, "infill/0/3");
        CHECK(!pk.empty());
        MoveFaces(m, pk, {0, 0.1f, 0});   // lift the whole picket (moved intact)
        Analysis an = Analyze(m, a);
        bool movedNoted = false;
        for (const PartInfo& p : an.parts) if (p.id == "infill/0/3") movedNoted = p.state == PartState::Edited && p.why.find("moved") != std::string::npos;
        CHECK(movedNoted);
        Recipe b = a;
        b.params["railCount"] = 3;
        b.params["railHeight"] = 0.1;
        RegenResult r = Regenerate(m, a, b);
        CHECK(r.ok && !r.needsDecision);
        // the lifted picket is still there, lifted
        PolyMesh orig = m;
        int has = 0;
        for (int f : pk) if (HasFaceLike(r.mesh, orig, f)) ++has;
        CHECK(has == (int)pk.size());
    }

    // ---- guided_regen_deleted: a part you deleted is not brought back by an unrelated change -------------
    {
        const Recipe a = DefaultRecipe(T, "post_rail");
        PolyMesh m = Create(a);
        const std::vector<int> p2 = FacesOf(m, a, "post/2");
        std::set<int> kill(p2.begin(), p2.end());
        PolyMesh d;
        d.verts = m.verts;
        for (int i = 0; i < (int)m.faces.size(); ++i) if (!kill.count(i)) d.faces.push_back(m.faces[i]);
        const Analysis an = Analyze(d, a);
        CHECK(an.Count(PartState::Deleted) == 1);
        Recipe b = a; b.colors["rail"] = Color{0, 1, 0, 1};
        RegenResult r = Regenerate(d, a, b);
        CHECK(r.ok && !r.needsDecision);
        CHECK(Analyze(r.mesh, r.recipe).Count(PartState::Deleted) == 1);
        Recipe c = a; c.params["height"] = 1.4;
        RegenResult rc = Regenerate(d, a, c);
        CHECK(rc.needsDecision && rc.conflicts.size() == 1 && rc.conflicts[0].state == PartState::Deleted);
        RegenOptions disc; disc.all = Decision::Discard;
        CHECK(SameFaces(Regenerate(d, a, c, disc).mesh, Create(c)));   // restored
        RegenOptions keep; keep.all = Decision::Keep;
        RegenResult rk = Regenerate(d, a, c, keep);
        CHECK(rk.ok && rk.recipe.detached.count("post/2") == 1);
        CHECK(Analyze(rk.mesh, rk.recipe).Count(PartState::Deleted) == 0);   // it is simply gone from the design now
    }

    // ---- guided_detach and guided_convert ---------------------------------------------------------------
    {
        Recipe a = DefaultRecipe(T, "ranch");
        PolyMesh m = Create(a);
        CHECK(!DetachPart(a, "nonsense/9"));
        CHECK(DetachPart(a, "post/0"));
        // detaching changes nothing in the mesh; the part is plain mesh now (custom)
        CHECK(Analyze(m, a).customFaces == 6);   // post/0 (a box) is plain mesh now
        Recipe b = a; b.params["height"] = 1.8;
        RegenResult r = Regenerate(m, a, b);
        CHECK(r.ok && !r.needsDecision);
        CHECK(HasFaceLike(r.mesh, m, FacesOf(m, DefaultRecipe(T, "ranch"), "post/0")[0]));   // post/0 stayed the old height
        CHECK(Analyze(r.mesh, r.recipe).customFaces == (int)PartById(Generate(DefaultRecipe(T, "ranch")), "post/0")->mesh.faces.size());

        const std::string dir = TmpDir("convert");
        const std::string mp = dir + "/Fence.okaymesh";
        std::string err;
        CHECK(m.Save(mp, &err));
        CHECK(SaveRecipe(SidecarPath(mp), a, &err));
        CHECK(std::filesystem::exists(dir + "/Fence.okayguided"));
        CHECK(ConvertToRegular(mp, &err));
        CHECK(!std::filesystem::exists(dir + "/Fence.okayguided"));
        PolyMesh back;
        CHECK(PolyMesh::Load(mp, back, &err));   // the mesh is untouched and still an ordinary model
        CHECK(Hash(back) == Hash(m));
        CHECK(!ConvertToRegular(mp, &err));       // nothing left to convert
    }

    // ---- guided_files: recipe save/load, a new version is refused, old model files load unchanged --------
    {
        const std::string dir = TmpDir("files");
        Recipe a = DefaultRecipe(T, "chain_link");
        a.path = {{0, 0, 0}, {5, 0, 1}};
        a.detached.insert("post/1");
        a.partDesign["post/2"] = "octagon";
        std::string err;
        CHECK(SaveRecipe(dir + "/a.okayguided", a, &err));
        Recipe b;
        CHECK(LoadRecipe(dir + "/a.okayguided", b, &err));
        CHECK(b.Serialize() == a.Serialize());
        Recipe x;
        CHECK(!Recipe::Parse("okayguided 2\ntemplate fence 1\n", x, &err));
        CHECK(err.find("newer") != std::string::npos);
        CHECK(!Recipe::Parse("hello", x, &err));
        CHECK(!Recipe::Parse("okayguided 1\nparam height\n", x, &err));
        // Regenerate refuses a model made by a newer template, and says to convert
        Recipe future = DefaultRecipe(T); future.templateVersion = 99;
        RegenResult rr = Regenerate(Create(DefaultRecipe(T)), future, DefaultRecipe(T));
        CHECK(!rr.ok && !rr.needsDecision && rr.message.find("Convert") != std::string::npos);
        // an old model file (no guided data at all) loads exactly as before
        PolyMesh cube = CustomBox({0, 0, 0}), cube2;
        CHECK(PolyMesh::Parse("# an old file\n" + cube.Serialize(), cube2, &err));
        CHECK(Hash(cube2) == Hash(cube));
        // a generated fence is a plain .okaymesh: same loader, same text
        const PolyMesh f = Create(DefaultRecipe(T));
        CHECK(f.Serialize().rfind("okaymesh 1", 0) == 0);
        // the editor's round trip through the triangle mesh keeps every part intact
        Mesh tri = f.ToMesh(nullptr, false);
        PolyMesh again = PolyMesh::FromMesh(tri, {false, true, 1.0f});
        const Analysis an = Analyze(again, DefaultRecipe(T));
        CHECK(an.ok);
        CHECK(an.Count(PartState::Intact) == (int)Generate(DefaultRecipe(T)).size());
        CHECK(an.customFaces == 0);
        // the way the editor stores it: SaveMeshAsset, LoadMeshAsset, then the polygon view the edit tools use
        const std::string dir2 = TmpDir("asset");
        const Recipe fr = DefaultRecipe(T, "wrought_iron");
        const Mesh fm = Create(fr).ToMesh();
        CHECK(SaveMeshAsset(dir2 + "/Fence.okaymesh", fm, &err));
        Mesh loaded;
        CHECK(LoadMeshAsset(dir2 + "/Fence.okaymesh", loaded, nullptr, &err));
        for (int weld = 0; weld < 2; ++weld) {
            const PolyMesh viaFile = PolyMesh::FromMesh(loaded, {weld == 1, true, 1.0f});
            const Analysis af = Analyze(viaFile, fr);
            CHECK(af.ok && af.Count(PartState::Intact) == (int)Generate(fr).size() && af.customFaces == 0);
        }
    }

    // ---- guided_user_parts: save a selection as a part, use it, round trip --------------------------------
    {
        const Recipe a = DefaultRecipe(T, "picket");
        const PolyMesh m = Create(a);
        const std::vector<int> faces = FacesOf(m, a, "post/0");
        UserPart up;
        std::string err;
        CHECK(!MakeUserPart(m, {}, "x", "post", up, &err));
        CHECK(MakeUserPart(m, faces, "My post", "post", up, &err));
        CHECK(up.mesh.faces.size() == faces.size());
        UserPart up2;
        CHECK(ParseUserPart(SerializeUserPart(up), up2, &err));
        CHECK(SerializeUserPart(up2) == SerializeUserPart(up));
        // normalised into the unit box
        for (const Vec3& v : up.mesh.verts) CHECK(v.x >= -0.5001f && v.x <= 0.5001f && v.y >= -0.0001f && v.y <= 1.0001f);
        // as a design: embedded in the recipe, every post becomes the user's shape
        Recipe b = a;
        b.userParts[up.id] = up;
        b.slotDesign["post"] = "user:" + up.id;
        const auto parts = Generate(b);
        CHECK(PartsSound(parts));
        Recipe capB = a;
        capB.userParts[up.id] = up;
        capB.slotDesign["cap"] = "user:" + up.id;
        CHECK(Hash(PartById(Generate(capB), "cap/0")->mesh) != Hash(PartById(Generate(a), "cap/0")->mesh));
        CHECK(PartsSound(Generate(capB)));
        // a recipe that carries the part loads and builds the same on "another machine" (no registry of parts needed)
        Recipe b2;
        CHECK(Recipe::Parse(b.Serialize(), b2, &err));
        CHECK(Hash(Create(b2)) == Hash(Create(b)));
        // regenerate to the user part through the normal path
        const RegenResult r = Regenerate(m, a, b);
        CHECK(r.ok && Hash(r.mesh) == Hash(Create(b)));
        // a missing part falls back to a box, never to nothing
        Recipe b3 = b; b3.userParts.clear();
        CHECK(PartById(Generate(b3), "post/0") != nullptr);
        // files in a folder
        const std::string dir = TmpDir("parts");
        CHECK(SaveUserPart(dir + "/" + up.id + ".okaypart", up, &err));
        const auto loaded = LoadUserParts(dir);
        CHECK(loaded.size() == 1 && loaded[0].id == up.id && loaded[0].slot == "post");
        CHECK(SerializeUserPart(loaded[0]) == SerializeUserPart(up));
        { std::ofstream bad(dir + "/bad.okaypart"); bad << "junk"; }
        CHECK(LoadUserParts(dir).size() == 1);   // bad files are skipped
    }

    // ---- guided_user_templates -----------------------------------------------------------------------------
    {
        Recipe a = DefaultRecipe(T, "stockade");
        a.params["height"] = 2.1;
        UserTemplate t; t.name = "My tall stockade"; t.description = "taller"; t.recipe = a;
        const std::string dir = TmpDir("templates");
        std::string err;
        CHECK(SaveUserTemplate(dir + "/tall.okaytemplate", t, &err));
        const auto got = LoadUserTemplates(dir);
        CHECK(got.size() == 1);
        CHECK(got[0].name == "My tall stockade" && got[0].description == "taller");
        CHECK(Hash(Create(got[0].recipe)) == Hash(Create(a)));
        UserTemplate x;
        CHECK(!ParseUserTemplate("okaytemplate 7\n", x, &err));
    }

    // ---- guided_undo ----------------------------------------------------------------------------------------
    {
        GuidedHistory h;
        GuidedState cur;
        cur.recipe = DefaultRecipe(T, "picket");
        cur.mesh = Create(cur.recipe);
        const std::string first = Hash(cur.mesh);
        h.Push(cur);                                  // before a parameter change
        Recipe b = cur.recipe; b.params["height"] = 1.7;
        const RegenResult r = Regenerate(cur.mesh, cur.recipe, b);
        cur.mesh = r.mesh; cur.recipe = r.recipe;
        CHECK(Hash(cur.mesh) != first);
        CHECK(h.CanUndo() && !h.CanRedo());
        CHECK(h.Undo(cur));
        CHECK(Hash(cur.mesh) == first && cur.recipe.Param("height") == 1.1);
        CHECK(h.CanRedo());
        CHECK(h.Redo(cur));
        CHECK(Hash(cur.mesh) == Hash(r.mesh) && cur.recipe.Param("height") == 1.7);
        CHECK(!h.Redo(cur));
        h.Push(cur);                                  // a new step clears redo
        CHECK(!h.CanRedo());
        h.Clear();
        CHECK(!h.CanUndo());
    }

    TEST_MAIN_RESULT();
}
