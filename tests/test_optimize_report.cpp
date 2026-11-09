// Optimization tools 27: the scene optimization report (plain structs in, findings out).
#include "test_framework.hpp"
#include "okay/Optimize/SceneOptimizationReport.hpp"
#include <cctype>
#include <cmath>
#include <string>

using namespace okay;
using namespace okay::optimize;

namespace {

OptObject Obj(const std::string& name, int tris, int mat, float x, bool isStatic = true) {
    OptObject o;
    o.name = name;
    o.triangles = tris;
    o.materialId = mat;
    o.meshId = tris;
    o.bounds = Bounds(Vec3{x, 0, 0}, Vec3{1, 1, 1});
    o.isStatic = isStatic;
    return o;
}

OptTexture Tex(const std::string& name, int size) {
    OptTexture t;
    t.name = name;
    t.width = t.height = size;
    return t;
}

// ---- a strict little JSON reader, to prove the output is well formed ---------------------------------
struct Json {
    const std::string& s;
    std::size_t i = 0;
    bool ok = true;
    explicit Json(const std::string& text) : s(text) {}
    void Ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    bool Lit(const char* w) { std::size_t n = std::string(w).size(); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
    void String() {
        if (i >= s.size() || s[i] != '"') { ok = false; return; }
        ++i;
        while (i < s.size() && s[i] != '"') {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x20) { ok = false; return; }                          // raw control characters are illegal
            if (c == '\\') {
                ++i;
                if (i >= s.size()) { ok = false; return; }
                if (std::string("\"\\/bfnrtu").find(s[i]) == std::string::npos) { ok = false; return; }
                if (s[i] == 'u') { for (int k = 0; k < 4; ++k) { ++i; if (i >= s.size() || !std::isxdigit(static_cast<unsigned char>(s[i]))) { ok = false; return; } } }
            }
            ++i;
        }
        if (i >= s.size()) { ok = false; return; }
        ++i;
    }
    void Number() {
        const std::size_t st = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        if (i == st) ok = false;
    }
    void Value() {
        Ws();
        if (i >= s.size()) { ok = false; return; }
        if (s[i] == '{') {
            ++i; Ws();
            if (s[i] == '}') { ++i; return; }
            while (ok) { Ws(); String(); Ws(); if (i >= s.size() || s[i] != ':') { ok = false; return; } ++i; Value(); Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == '}') { ++i; return; } ok = false; }
        } else if (s[i] == '[') {
            ++i; Ws();
            if (s[i] == ']') { ++i; return; }
            while (ok) { Value(); Ws(); if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == ']') { ++i; return; } ok = false; }
        } else if (s[i] == '"') String();
        else if (Lit("true") || Lit("false") || Lit("null")) {}
        else Number();
    }
    bool Valid() { Value(); Ws(); return ok && i == s.size(); }
};

}  // namespace

int main() {
    RUN_SUITE("optimize_report");

    // ---- an empty and a healthy scene have nothing to report ----------------------------------------------
    {
        OptimizationReport empty = AnalyzeScene(OptScene());
        CHECK(empty.findings.empty());
        CHECK(empty.stats.objects == 0 && empty.stats.triangles == 0);
        CHECK(empty.ToText().find("nothing to fix") != std::string::npos);
        CHECK(Json(empty.ToJson()).Valid());

        OptScene healthy;
        for (int i = 0; i < 50; ++i) {
            OptObject o = Obj("Prop" + std::to_string(i), 2000, 1, static_cast<float>(i) * 3.0f);
            o.textures.push_back(Tex("wood", 512));
            healthy.objects.push_back(o);
        }
        OptMaterial m;
        m.id = 1; m.name = "Wood"; m.shader = "lit"; m.signature = "a";
        healthy.materials.push_back(m);
        OptimizationReport rep = AnalyzeScene(healthy);
        CHECK(rep.findings.empty());
        CHECK(rep.stats.objects == 50 && rep.stats.staticObjects == 50 && rep.stats.triangles == 100000);
        CHECK(rep.stats.uniqueTextures == 1);                                           // the same texture counted once
        CHECK_NEAR(rep.stats.textureMB, 512.0 * 512.0 * 4.0 * 1.3333 / (1024.0 * 1024.0), 0.01);
        CHECK(rep.stats.estimatedDrawCalls == 50);
        CHECK(rep.Count(Severity::Critical) == 0 && rep.Count(Severity::Warning) == 0 && rep.Count(Severity::Info) == 0);
    }

    // ---- tiny draw calls ----------------------------------------------------------------------------------------------
    {
        OptScene s;
        for (int i = 0; i < 300; ++i) s.objects.push_back(Obj("Pebble" + std::to_string(i), 12, 1, static_cast<float>(i)));
        s.objects.push_back(Obj("Big", 5000, 1, 0.0f));
        OptimizationReport r = AnalyzeScene(s);
        const Finding* f = r.Get(FindingKind::TinyDrawCalls);
        CHECK(f != nullptr);
        CHECK(f->severity == Severity::Warning);                                        // 300 >= 100, below 500
        CHECK(f->objects.size() == 300);
        CHECK(f->listed.size() == 8 && f->listed[0] == "Pebble0");
        CHECK(f->detail.find("300") != std::string::npos);
        CHECK(!f->suggestion.empty());
        CHECK(r.findings.size() == 1);
        // Severity scales with the count.
        OptSettings strict;
        strict.tinyCriticalCount = 200;
        CHECK(AnalyzeScene(s, strict).Get(FindingKind::TinyDrawCalls)->severity == Severity::Critical);
        OptSettings lax;
        lax.tinyWarnCount = 500;                                                          // 300 is past half of it: worth a note
        CHECK(AnalyzeScene(s, lax).Get(FindingKind::TinyDrawCalls)->severity == Severity::Info);
        lax.tinyWarnCount = 100000;
        CHECK(!AnalyzeScene(s, lax).Has(FindingKind::TinyDrawCalls));                   // too few to matter
        // The threshold for "tiny" is a setting.
        OptSettings none;
        none.tinyTriangleLimit = 10;
        CHECK(!AnalyzeScene(s, none).Has(FindingKind::TinyDrawCalls));
        // Transparent ones are left to the overdraw check, not counted here.
        OptScene t = s;
        for (OptObject& o : t.objects) o.transparent = true;
        CHECK(!AnalyzeScene(t).Has(FindingKind::TinyDrawCalls));
    }

    // ---- huge textures on small objects ----------------------------------------------------------------------------------
    {
        OptScene s;
        for (int i = 0; i < 3; ++i) {
            OptObject o = Obj("Pebble" + std::to_string(i), 800, 1, static_cast<float>(i));
            o.bounds = Bounds(Vec3{static_cast<float>(i), 0, 0}, Vec3{0.3f, 0.3f, 0.3f});            // 30 cm across
            o.textures.push_back(Tex("pebble_" + std::to_string(i), 4096));
            s.objects.push_back(o);
        }
        OptObject sane = Obj("Statue", 8000, 1, 10.0f);
        sane.bounds = Bounds(Vec3{10, 0, 0}, Vec3{4, 4, 4});
        sane.textures.push_back(Tex("statue", 1024));                                                // 1024 on a 4 m statue is fine
        s.objects.push_back(sane);
        OptObject small = Obj("Coin", 200, 1, 20.0f);
        small.bounds = Bounds(Vec3{20, 0, 0}, Vec3{0.1f, 0.1f, 0.1f});
        small.textures.push_back(Tex("coin", 256));                                                  // small texture: never flagged
        s.objects.push_back(small);
        OptimizationReport r = AnalyzeScene(s);
        const Finding* f = r.Get(FindingKind::HugeTextureSmallObject);
        CHECK(f != nullptr);
        CHECK(f->objects.size() == 3);                                                                // the pebbles, not the statue or the coin
        CHECK(f->severity == Severity::Critical);                                                     // ~270 MB wasted
        CHECK(f->impact > 200.0f);
        CHECK(f->listed.size() == 3 && f->listed[0] == "Pebble0");
        // A known on-screen size is used when there is one: a 2048 texture on something 100 pixels tall.
        OptScene px;
        OptObject big = Obj("Distant", 500, 1, 0.0f);
        big.bounds = Bounds(Vec3{0, 0, 0}, Vec3{50, 50, 50});                                        // huge in the world ...
        big.screenPixels = 100.0f;                                                                    // ... but tiny on screen
        big.textures.push_back(Tex("sky_tile", 2048));
        px.objects.push_back(big);
        CHECK(AnalyzeScene(px).Has(FindingKind::HugeTextureSmallObject));
        px.objects[0].screenPixels = 2000.0f;                                                         // up close it is fine
        CHECK(!AnalyzeScene(px).Has(FindingKind::HugeTextureSmallObject));
        px.objects[0].screenPixels = -1.0f;                                                           // unknown: the world size says it is big
        CHECK(!AnalyzeScene(px).Has(FindingKind::HugeTextureSmallObject));
        // Severity follows the waste.
        OptScene one;
        OptObject o = Obj("Cup", 400, 1, 0.0f);
        o.bounds = Bounds(Vec3{0, 0, 0}, Vec3{0.2f, 0.2f, 0.2f});
        o.textures.push_back(Tex("cup", 2048));
        one.objects.push_back(o);
        const OptimizationReport cupReport = AnalyzeScene(one);
        const Finding* cf = cupReport.Get(FindingKind::HugeTextureSmallObject);
        CHECK(cf != nullptr && cf->severity == Severity::Warning && cf->impact > 8.0f && cf->impact < 64.0f);
    }

    // ---- high poly without LOD -------------------------------------------------------------------------------------------------------
    {
        OptScene s;
        s.objects.push_back(Obj("Rock", 50000, 1, 0.0f));
        s.objects.push_back(Obj("Tree", 30000, 1, 5.0f));
        OptObject withLod = Obj("Statue", 80000, 1, 10.0f);
        withLod.hasLod = true;
        s.objects.push_back(withLod);
        s.objects.push_back(Obj("Crate", 500, 1, 15.0f));
        OptimizationReport r = AnalyzeScene(s);
        const Finding* f = r.Get(FindingKind::HighPolyNoLod);
        CHECK(f != nullptr && f->severity == Severity::Warning);
        CHECK(f->objects.size() == 2);                                                                // Statue has LODs, Crate is light
        CHECK(f->listed[0] == "Rock" && f->listed[1] == "Tree");
        CHECK(f->detail.find("densest has 50000") != std::string::npos);                                // the statue's 80000 is not counted
        CHECK(f->suggestion.find("GenerateLodChain") != std::string::npos);
        s.objects.push_back(Obj("Scan", 250000, 1, 20.0f));
        CHECK(AnalyzeScene(s).Get(FindingKind::HighPolyNoLod)->severity == Severity::Critical);
        OptSettings relaxed;
        relaxed.highPolyTriangles = 1000000;
        CHECK(!AnalyzeScene(s, relaxed).Has(FindingKind::HighPolyNoLod));
        // Skinned characters count too.
        OptScene sk;
        OptObject hero = Obj("Hero", 40000, 1, 0.0f, false);
        hero.skinned = true;
        sk.objects.push_back(hero);
        CHECK(AnalyzeScene(sk).Has(FindingKind::HighPolyNoLod));
    }

    // ---- duplicate materials ---------------------------------------------------------------------------------------------------------------
    {
        OptScene s;
        auto mat = [](int id, const char* name, const char* shader, const char* sig) {
            OptMaterial m;
            m.id = id; m.name = name; m.shader = shader; m.signature = sig;
            return m;
        };
        // Five copies of the same brick material, two copies of grass (different shader than brick), one unique.
        for (int i = 0; i < 5; ++i) s.materials.push_back(mat(10 + i, ("Brick" + std::to_string(i)).c_str(), "lit", "brick"));
        s.materials.push_back(mat(20, "Grass", "lit", "grass"));
        s.materials.push_back(mat(21, "Grass 2", "lit", "grass"));
        s.materials.push_back(mat(30, "Glass", "glass", "glass"));
        s.materials.push_back(mat(40, "Brick (unlit)", "unlit", "brick"));                          // same signature, other shader: not a duplicate
        for (int i = 0; i < 12; ++i) s.objects.push_back(Obj("Wall" + std::to_string(i), 100, 10 + i % 5, static_cast<float>(i)));
        for (int i = 0; i < 4; ++i) s.objects.push_back(Obj("Lawn" + std::to_string(i), 100, 20 + i % 2, static_cast<float>(i)));
        OptimizationReport r = AnalyzeScene(s);
        int dups = 0;
        const Finding* worst = nullptr;
        for (const Finding& f : r.findings) if (f.kind == FindingKind::DuplicateMaterials) { ++dups; if (!worst) worst = &f; }
        CHECK(dups == 2);                                                                             // one finding per duplicate group
        CHECK(worst != nullptr && worst->severity == Severity::Warning);                                 // 5 identical materials
        CHECK(worst->objects.size() == 12);                                                              // the walls
        CHECK(worst->detail.find("Brick0") != std::string::npos);
        CHECK(worst->title.find('5') != std::string::npos);
        // The grass pair is minor.
        const Finding* grass = nullptr;
        for (const Finding& f : r.findings) if (f.kind == FindingKind::DuplicateMaterials && f.objects.size() == 4) grass = &f;
        CHECK(grass != nullptr && grass->severity == Severity::Info);
        // Without a material table nothing can be said.
        s.materials.clear();
        CHECK(!AnalyzeScene(s).Has(FindingKind::DuplicateMaterials));
    }

    // ---- overdraw risk --------------------------------------------------------------------------------------------------------------------------
    {
        OptScene s;
        for (int i = 0; i < 10; ++i) {                                                                    // ten smoke quads on top of each other
            OptObject q = Obj("Smoke" + std::to_string(i), 2, 1, 0.1f * static_cast<float>(i), false);
            q.transparent = true;
            s.objects.push_back(q);
        }
        OptObject lone = Obj("Glass", 2, 2, 50.0f, false);                                                // a lone transparent pane is fine
        lone.transparent = true;
        s.objects.push_back(lone);
        s.objects.push_back(Obj("Floor", 2, 3, 0.0f));                                                    // opaque things do not count
        OptimizationReport r = AnalyzeScene(s);
        const Finding* f = r.Get(FindingKind::OverdrawRisk);
        CHECK(f != nullptr);
        CHECK(f->severity == Severity::Warning);
        CHECK(f->objects.size() == 10);
        CHECK(f->impact == 10.0f);                                                                        // ten layers deep
        CHECK(f->detail.find("10 layers") != std::string::npos);
        for (int i = 0; i < 12; ++i) {
            OptObject q = Obj("More" + std::to_string(i), 2, 1, 0.2f, false);
            q.transparent = true;
            s.objects.push_back(q);
        }
        CHECK(AnalyzeScene(s).Get(FindingKind::OverdrawRisk)->severity == Severity::Critical);           // 22 deep
        // Spread out, the same quads do not overlap.
        OptScene spread;
        for (int i = 0; i < 10; ++i) { OptObject q = Obj("Q", 2, 1, 5.0f * static_cast<float>(i), false); q.transparent = true; spread.objects.push_back(q); }
        CHECK(!AnalyzeScene(spread).Has(FindingKind::OverdrawRisk));
        // The layer threshold is a setting.
        OptSettings sens;
        sens.overdrawLayers = 20;
        OptScene ten;
        for (int i = 0; i < 10; ++i) { OptObject q = Obj("Q", 2, 1, 0.0f, false); q.transparent = true; ten.objects.push_back(q); }
        CHECK(AnalyzeScene(ten).Has(FindingKind::OverdrawRisk));
        CHECK(!AnalyzeScene(ten, sens).Has(FindingKind::OverdrawRisk));
        // Overlap needs all three axes: stacked in x but far apart in y is not overdraw.
        OptScene apart;
        for (int i = 0; i < 10; ++i) { OptObject q = Obj("Q", 2, 1, 0.0f, false); q.transparent = true; q.bounds.center.y = 10.0f * static_cast<float>(i); apart.objects.push_back(q); }
        CHECK(!AnalyzeScene(apart).Has(FindingKind::OverdrawRisk));
    }

    // ---- budgets: draw calls and texture memory -----------------------------------------------------------------------------------------------------
    {
        OptScene s;
        for (int i = 0; i < 2500; ++i) s.objects.push_back(Obj("O" + std::to_string(i), 1000, 1, static_cast<float>(i) * 2.0f));
        OptimizationReport r = AnalyzeScene(s);
        const Finding* f = r.Get(FindingKind::TooManyDrawCalls);
        CHECK(f != nullptr && f->severity == Severity::Warning && f->impact == 500.0f);
        s.objects.resize(2500);
        for (int i = 0; i < 2000; ++i) s.objects.push_back(Obj("P" + std::to_string(i), 1000, 1, static_cast<float>(i) * 2.0f));
        CHECK(AnalyzeScene(s).Get(FindingKind::TooManyDrawCalls)->severity == Severity::Critical);
        OptScene fine;
        for (int i = 0; i < 100; ++i) fine.objects.push_back(Obj("O", 1000, 1, static_cast<float>(i)));
        CHECK(!AnalyzeScene(fine).Has(FindingKind::TooManyDrawCalls));

        OptScene tex;
        for (int i = 0; i < 40; ++i) {
            OptObject o = Obj("T" + std::to_string(i), 2000, 1, static_cast<float>(i) * 3.0f);
            o.textures.push_back(Tex("big_" + std::to_string(i), 4096));                                  // 40 x ~85 MB
            tex.objects.push_back(o);
        }
        OptimizationReport tr = AnalyzeScene(tex);
        CHECK(tr.stats.uniqueTextures == 40);
        CHECK(tr.stats.textureMB > 3000.0f);
        const Finding* tf = tr.Get(FindingKind::TextureMemory);
        CHECK(tf != nullptr && tf->severity == Severity::Critical);                                       // over twice the 1 GB budget
        OptSettings big;
        big.textureBudgetMB = 8000.0f;
        CHECK(!AnalyzeScene(tex, big).Has(FindingKind::TextureMemory));
    }

    // ---- a messy scene: ordering and the two output formats ---------------------------------------------------------------------------------------------------
    {
        OptScene s;
        for (int i = 0; i < 300; ++i) s.objects.push_back(Obj("Pebble" + std::to_string(i), 12, 1, static_cast<float>(i)));           // warning
        OptObject scan = Obj("Scan \"Alpha\"\\v2\n(copy)", 250000, 1, 0.0f);                                                          // critical + a name that needs escaping
        s.objects.push_back(scan);
        OptObject cup = Obj("Cup", 400, 1, 0.0f);
        cup.bounds = Bounds(Vec3{0, 0, 0}, Vec3{0.2f, 0.2f, 0.2f});
        cup.textures.push_back(Tex("cup", 2048));                                                                                     // warning (waste a few MB .. tens)
        s.objects.push_back(cup);
        OptMaterial a, b;
        a.id = 1; a.name = "M1"; a.shader = "lit"; a.signature = "same";
        b.id = 2; b.name = "M2"; b.shader = "lit"; b.signature = "same";
        OptMaterial c, d;
        c.id = 3; c.name = "M3"; c.shader = "lit"; c.signature = "other";
        d.id = 4; d.name = "M4"; d.shader = "lit"; d.signature = "other";
        s.materials = {a, b, c, d};
        s.objects.push_back(Obj("OnM2", 100, 2, 1.0f));
        s.objects.push_back(Obj("OnM3", 5000, 3, 2.0f));                                                                               // info: a pair of identical materials used once each
        s.objects.push_back(Obj("OnM4", 5000, 4, 3.0f));
        OptimizationReport r = AnalyzeScene(s);
        CHECK(r.findings.size() >= 4);
        // Worst first: severities never increase down the list.
        for (std::size_t i = 1; i < r.findings.size(); ++i) CHECK(static_cast<int>(r.findings[i - 1].severity) >= static_cast<int>(r.findings[i].severity));
        CHECK(r.findings[0].severity == Severity::Critical && r.findings[0].kind == FindingKind::HighPolyNoLod);
        CHECK(r.Count(Severity::Critical) == 1);
        CHECK(r.Count(Severity::Warning) >= 2 && r.Count(Severity::Info) >= 1);
        CHECK(r.Count(Severity::Critical) + r.Count(Severity::Warning) + r.Count(Severity::Info) == static_cast<int>(r.findings.size()));
        // Text.
        const std::string text = r.ToText();
        CHECK(text.find("[CRITICAL]") != std::string::npos && text.find("[WARNING]") != std::string::npos && text.find("[INFO]") != std::string::npos);
        CHECK(text.find("[CRITICAL]") < text.find("[WARNING]") && text.find("[WARNING]") < text.find("[INFO]"));
        CHECK(text.find("fix:") != std::string::npos);
        CHECK(text.find("(301 total)") != std::string::npos);                                                                          // long lists are cut and counted
        CHECK(text.find("objects " + std::to_string(r.stats.objects) + " (") != std::string::npos);
        // JSON: well formed, special characters escaped, the same findings.
        const std::string json = r.ToJson();
        CHECK(Json(json).Valid());
        CHECK(json.find("\"kind\":\"high_poly_no_lod\"") != std::string::npos);
        CHECK(json.find("\"severity\":\"critical\"") != std::string::npos);
        CHECK(json.find("\\\"Alpha\\\"") != std::string::npos && json.find("\\\\v2") != std::string::npos && json.find("\\n(copy)") != std::string::npos);
        CHECK(json.find('\n') == std::string::npos);                                                                                   // one line, no raw newlines
        CHECK(json.find("\"objects\":" + std::to_string(r.stats.objects) + ",") != std::string::npos);
        CHECK(json.find("\"counts\":{\"critical\":1") != std::string::npos);
        std::size_t kinds = 0;
        for (std::size_t p = json.find("\"kind\""); p != std::string::npos; p = json.find("\"kind\"", p + 1)) ++kinds;
        CHECK(kinds == r.findings.size());
        // Names fall back to the index.
        OptScene anon;
        for (int i = 0; i < 3; ++i) { OptObject o = Obj("", 90000, 1, static_cast<float>(i)); anon.objects.push_back(o); }
        CHECK(AnalyzeScene(anon).Get(FindingKind::HighPolyNoLod)->listed[0] == "#0");
        // Deterministic.
        CHECK(AnalyzeScene(s).ToJson() == json && AnalyzeScene(s).ToText() == text);
        CHECK(std::string(SeverityName(Severity::Warning)) == "warning" && std::string(FindingKindName(FindingKind::OverdrawRisk)) == "overdraw_risk");
    }

    TEST_MAIN_RESULT();
}
