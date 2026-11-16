// Damaged or unusual data files load safely (issue #888, findings 74, 75 and 78):
//   74  project.okayproj: nan, inf, negative and huge numbers are clamped or ignored, with a warning each.
//   75  writing a texture's .okaymeta (a tag) does not shrink the texture: no meta and a default meta load the same.
//   78  .okaymat: a texture path with spaces round-trips, and one inf / nan / bad number drops only that field.
// Temp folders only; nothing sleeps.
#include "test_framework.hpp"
#include "../editor/ProjectState.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Render/Material.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using namespace okay;

namespace {

// Warnings logged while `fn` runs.
template <class F> std::vector<std::string> CaptureWarnings(F fn) {
    std::vector<std::string> got;
    Log::Sink old = Log::sink;
    Log::sink = [&](Log::Level l, const std::string& msg) { if (l == Log::Level::Warning) got.push_back(msg); };
    fn();
    Log::sink = old;
    return got;
}
bool AnyHas(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x.find(s) != std::string::npos) return true;
    return false;
}
bool Finite3(const float* a) { return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]); }
bool In01(const float* a, int n) { for (int i = 0; i < n; ++i) if (!(a[i] >= 0.0f && a[i] <= 1.0f)) return false; return true; }

std::string ReadAll(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

void ProjectFile() {
    RUN_SUITE("74: project.okayproj numbers");
    const oked::ProjectSettings def;
    // Each numeric key with nan, inf, a negative and a huge value.
    struct Case { const char* key; const char* value; };
    const Case cases[] = {
        {"width", "nan"}, {"width", "inf"}, {"width", "-5"}, {"width", "999999999999"},
        {"height", "nan"}, {"height", "inf"}, {"height", "-5"}, {"height", "99999999"},
        {"gravity2d", "nan inf"}, {"gravity2d", "inf 0"}, {"gravity2d", "-1e30 -9.81"}, {"gravity2d", "0 1e30"},
        {"gravity3d", "0 nan 0"}, {"gravity3d", "inf 0 0"}, {"gravity3d", "0 -1e30 0"}, {"gravity3d", "1e30 0 0"},
        {"physics_hz", "nan"}, {"physics_hz", "inf"}, {"physics_hz", "-5"}, {"physics_hz", "1e30"},
        {"ambient", "nan"}, {"ambient", "inf"}, {"ambient", "-5"}, {"ambient", "1e30"},
        {"splash_bg_top", "nan 1e39 -3"}, {"splash_bg_top", "inf 0 0"}, {"splash_bg_top", "-5 0 0"}, {"splash_bg_top", "1e30 0 0"},
        {"splash_bg_bot", "nan 0 0"}, {"splash_bg_bot", "inf 0 0"}, {"splash_bg_bot", "0 -5 0"}, {"splash_bg_bot", "0 0 1e30"},
        {"splash_accent", "nan 0 0"}, {"splash_accent", "0 inf 0"}, {"splash_accent", "-5 0 0"}, {"splash_accent", "0 0 1e30"},
    };
    int warned = 0;
    for (const Case& c : cases) {
        oked::ProjectSettings s;
        const std::string text = std::string(c.key) + "=" + c.value + "\n";
        const auto w = CaptureWarnings([&] { oked::ParseProjectSettings(s, text); });
        const bool ok =
            s.defaultWidth >= 16 && s.defaultWidth <= 16384 && s.defaultHeight >= 16 && s.defaultHeight <= 16384 &&
            std::isfinite(s.gravity2D[0]) && std::isfinite(s.gravity2D[1]) && std::fabs(s.gravity2D[0]) <= 1000.0f &&
            std::fabs(s.gravity2D[1]) <= 1000.0f && Finite3(s.gravity3D) && std::fabs(s.gravity3D[0]) <= 1000.0f &&
            std::fabs(s.gravity3D[1]) <= 1000.0f && std::fabs(s.gravity3D[2]) <= 1000.0f &&
            s.physicsHz >= 1.0f && s.physicsHz <= 500.0f && s.ambient >= 0.0f && s.ambient <= 1.0f &&
            In01(s.splashBgTop, 3) && In01(s.splashBgBot, 3) && In01(s.splashAccent, 3);
        if (!ok) std::cerr << "  bad value kept for " << text;
        CHECK(ok);
        const bool hasWarn = AnyHas(w, c.key);
        if (!hasWarn) std::cerr << "  no warning for " << text;
        if (hasWarn) ++warned;
    }
    CHECK(warned == (int)(sizeof(cases) / sizeof(cases[0])));

    // A value that is not a number keeps the default; one out of range goes to the nearest end.
    {
        oked::ProjectSettings s;
        oked::ParseProjectSettings(s, "width=nan\nheight=-5\ngravity3d=0 nan 0\nambient=nan\nphysics_hz=nan\nsplash_bg_top=nan 1e39 -3\n"
                                      "gravity2d=0 -1e30\nsplash_accent=2 0.5 -1\n");
        CHECK(s.defaultWidth == def.defaultWidth);                     // not a number: the default
        CHECK(s.defaultHeight == 16);                                  // negative: the smallest size
        CHECK(s.gravity3D[0] == def.gravity3D[0] && s.gravity3D[1] == def.gravity3D[1] && s.gravity3D[2] == def.gravity3D[2]);
        CHECK(s.ambient == def.ambient && s.physicsHz == def.physicsHz);
        CHECK(s.splashBgTop[0] == def.splashBgTop[0] && s.splashBgTop[1] == def.splashBgTop[1] && s.splashBgTop[2] == def.splashBgTop[2]);
        CHECK(s.gravity2D[0] == 0.0f && s.gravity2D[1] == -1000.0f);   // huge: clamped
        CHECK(s.splashAccent[0] == 1.0f && s.splashAccent[1] == 0.5f && s.splashAccent[2] == 0.0f);
    }
    // Good values are untouched and give no warning.
    {
        oked::ProjectSettings s;
        const auto w = CaptureWarnings([&] {
            oked::ParseProjectSettings(s, "width=1920\nheight=1080\ngravity3d=0 -20 0\nambient=0.75\nphysics_hz=120\nsplash_bg_top=0.1 0.2 0.3\n");
        });
        CHECK(w.empty());
        CHECK(s.defaultWidth == 1920 && s.defaultHeight == 1080 && s.gravity3D[1] == -20.0f && s.ambient == 0.75f && s.physicsHz == 120.0f);
        CHECK(s.splashBgTop[2] == 0.3f);
    }
    okay::ProjectLists::Reset();
}

void TextureTag(const fs::path& dir) {
    RUN_SUITE("75: tagging a texture keeps its size");
    fs::create_directories(dir / "Art");
    const int W = 4096, H = 8;
    std::vector<std::uint8_t> px((std::size_t)W * H * 4, 200);
    const fs::path png = dir / "Art" / "big.png";
    CHECK(ImageWrite::WritePNG(png.string(), W, H, 4, px.data()));

    auto loadedWidth = [&] {
        Image img;
        if (!img.Load(png.string())) return -1;
        ApplyTextureImportSettings(png.string(), img);   // what the texture caches do
        return img.Width();
    };
    CHECK(loadedWidth() == W);   // no meta: full size

    AssetDatabase db;
    ScanOptions o;
    o.createMetas = false;       // as the editor scans
    o.indexUnmetaed = true;
    o.logWarnings = false;
    db.Scan(dir.string(), o);
    const AssetRecord* r = db.Find(std::string("Art/big.png"));
    CHECK(r != nullptr);
    if (!r) return;
    // The settings the editor shows for a texture with no meta are what it loads at.
    CHECK(r->settings.texture.maxSize == 0);
    std::string err;
    CHECK(db.AddLabel(r->guid, "ui", &err));
    CHECK(fs::exists(fs::path(png.string() + ".okaymeta")));
    const int after = loadedWidth();
    if (after != W) std::cerr << "  after tag: loaded " << after << " wide (meta: " << ReadAll(png.string() + ".okaymeta") << ")\n";
    CHECK(after == W);
    // Reload the database: still the full size.
    AssetDatabase db2;
    db2.Scan(dir.string(), o);
    const AssetRecord* r2 = db2.Find(std::string("Art/big.png"));
    CHECK(r2 && r2->HasLabel("ui") && r2->settings.texture.maxSize == 0);
    CHECK(loadedWidth() == W);
    // A size someone chose is still applied.
    ImportSettings s = r2->settings;
    s.texture.maxSize = 1024;
    CHECK(db2.SetSettings(r2->guid, s));
    CHECK(loadedWidth() == 1024);
}

void MaterialFile() {
    RUN_SUITE("78: .okaymat paths and bad numbers");
    Material m;
    m.texture = "Assets/Art/stone wall.png";
    m.unlit = true;
    m.tiling = {4.0f, 2.0f};
    m.rimStrength = 0.5f;
    m.outline = true;
    m.uvScroll = {0.25f, 0.0f};
    m.triplanar = true;
    m.shader = MeshRenderer::Shader::Toon;
    m.toonBands = 5;
    {
        const Material b = Material::FromText(m.ToText());
        if (b.texture != m.texture) std::cerr << "  texture came back as '" << b.texture << "'\n";
        CHECK(b.texture == m.texture);
        CHECK(b.unlit && b.tiling.x == 4.0f && b.rimStrength == 0.5f && b.outline && b.uvScroll.x == 0.25f && b.triplanar);
        CHECK(b.shader == MeshRenderer::Shader::Toon && b.toonBands == 5);
    }
    // Other awkward paths: Windows folders with spaces, a quote, a backslash, a lone "-".
    for (const std::string p : {std::string("C:\\Users\\John Smith\\tex.png"), std::string("Art/say \"hi\".png"),
                                std::string("Art\\plain.png"), std::string("-"), std::string("  lead.png"), std::string("tab\there.png")}) {
        Material x; x.texture = p;
        const Material b = Material::FromText(x.ToText());
        if (b.texture != p) std::cerr << "  '" << p << "' came back as '" << b.texture << "'\n";
        CHECK(b.texture == p);
    }
    // A plain path is still written bare, so older engines read the file as before.
    {
        Material x; x.texture = "Assets/brick.png";
        CHECK(x.ToText().find("texture Assets/brick.png\n") != std::string::npos);
    }
    // One inf keeps every other field (emissive.r is written as "inf").
    {
        Material x = m;
        x.emissive.r = INFINITY;
        x.emissive.g = 0.5f;
        const Material b = Material::FromText(x.ToText());
        CHECK(std::isfinite(b.emissive.r));
        CHECK(b.emissive.g == 0.5f);
        if (b.texture != m.texture || !b.unlit) std::cerr << "  fields after the inf were lost\n";
        CHECK(b.texture == m.texture && b.unlit && b.tiling.x == 4.0f && b.rimStrength == 0.5f && b.outline &&
              b.uvScroll.x == 0.25f && b.triplanar && b.shader == MeshRenderer::Shader::Toon);
    }
    // nan, a word and a missing number each drop only their own field.
    {
        const Material b = Material::FromText(
            "okaymat 1\ncolor 0.5 nan 0.25 1\nemissive x 0.5 0\nspecular 0.3\nflags 1 1\ntiling 2 3\nshader 77 4\n"
            "rim 0.5 2 1 1 1\nuvanim 0.1 0.2 1\ntexture Assets/a.png\n");
        CHECK(b.color.r == 0.5f && b.color.g == Material{}.color.g && b.color.b == 0.25f);
        CHECK(b.emissive.r == Material{}.emissive.r && b.emissive.g == 0.5f);
        CHECK(b.specular == 0.3f && b.shininess == Material{}.shininess);
        CHECK(b.unlit && b.doubleSided && b.tiling.y == 3.0f && b.rimPower == 2.0f && b.triplanar);
        CHECK(b.shader == MeshRenderer::Shader::Standard);   // an unknown shader id is not kept
        CHECK(b.toonBands == 4);
        CHECK(b.texture == "Assets/a.png");
    }
    // An old-format file (as every earlier version wrote it) loads, CRLF included.
    {
        const Material b = Material::FromText(
            "okaymat 1\r\ncolor 0.2 0.4 0.6 1\r\nemissive 1 0.5 0\r\nspecular 0.75 48\r\nflags 1 0\r\ntiling 3 2\r\n"
            "shader 2 4\r\nrim 0.5 3 1 1 1\r\noutline 1 0.05 0 0 0\r\nuvanim 0.25 0 1\r\ntexture assets/brick.png\r\n");
        CHECK(b.color.g == 0.4f && b.emissive.g == 0.5f && b.shininess == 48.0f && b.unlit && !b.doubleSided);
        CHECK(b.tiling.x == 3.0f && b.shader == MeshRenderer::Shader::Toon && b.toonBands == 4 && b.outline && b.outlineWidth == 0.05f);
        CHECK(b.uvScroll.x == 0.25f && b.triplanar && b.texture == "assets/brick.png");
        // The oldest files have no texture line ("-") or a bare "-".
        CHECK(Material::FromText("okaymat 1\ntexture -\n").texture.empty());
        // An old file that wrote a spaced path bare now loads the whole path.
        CHECK(Material::FromText("okaymat 1\ntexture Assets/Art/stone wall.png\n").texture == "Assets/Art/stone wall.png");
    }
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("okay_data_load_checks_" + std::to_string((long long)std::hash<std::string>{}(fs::current_path().string()) & 0xffffff));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ProjectFile();
    TextureTag(dir);
    MaterialFile();
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
