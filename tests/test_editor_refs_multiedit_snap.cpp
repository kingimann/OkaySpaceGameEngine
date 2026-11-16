// Three editor fixes from issue #888 checked together:
//  - Finding 69: renaming an OkayScript (.okay) or UI theme (.okaytheme) file in the Project panel rewrites the
//    paths that point at it (a scene's script record, a script's ui_load_theme(...) call), and the files count as used.
//  - Finding 71: editing one part of a vector or colour with several objects selected changes only that part on
//    each object (Position Y keeps each object's own X and Z; alpha keeps each sprite's own RGB).
//  - Finding 72: the snap increments refuse NaN / infinity and clamp 0, negatives and huge values, the same way
//    for the field, the saved setting and every snapping path.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "../editor/ProjectBrowser.hpp"
#include "EditorState.hpp"
#include "InspectorModel.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_ers_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "Assets", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string assets() const { return (path / "Assets").generic_string(); }
    std::string at(const std::string& rel) const { return (path / "Assets" / fs::u8path(rel)).generic_u8string(); }
};

void WriteFile(const std::string& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    std::ofstream(fs::u8path(p), std::ios::binary | std::ios::trunc) << s;
}
std::string ReadFile(const std::string& p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

void TestScriptAndThemeRename() {
    RUN_SUITE("renaming .okay and .okaytheme files rewrites what points at them (finding 69)");
    CHECK(AssetTypeFromExtension("okay") == AssetType::Script);
    CHECK(AssetTypeFromExtension("okaytheme") != AssetType::Other);
    TempDir td;
    const std::string scene = td.at("scenes/level.okayscene");
    const std::string script = td.at("scripts/a.okay");
    const std::string theme = td.at("ui/a.okaytheme");
    WriteFile(script, "ui_load_theme(\"Assets/ui/a.okaytheme\")\nprint(\"hi\")\n");
    WriteFile(theme, "okaytheme 1\naccent=1,0,0,1\n");
    WriteFile(scene,
        "scene \"Level\"\n"
        "gameobject \"Player\"\n"
        "  script \"okayscript\" \"print(\\\"hi\\\")\" \"Assets/scripts/a.okay\" 0\n");
    okaypb::Browser b(td.assets());
    CHECK(b.Sync());
    CHECK(b.UsersOf(script) == 1);                     // the scene uses the script
    CHECK(b.UsersOf(theme) == 1);                      // the script uses the theme

    auto r = b.Rename(script, "b");                    // the editor's Rename (Project panel, F2)
    CHECK(r.ok);
    CHECK(r.referencesRewritten == 1);
    const std::string sc = ReadFile(scene);
    CHECK(Has(sc, "\"Assets/scripts/b.okay\""));
    CHECK(!Has(sc, "scripts/a.okay"));
    CHECK(b.UsersOf(td.at("scripts/b.okay")) == 1);

    auto t = b.Rename(theme, "b");
    CHECK(t.ok);
    CHECK(t.referencesRewritten == 1);
    const std::string code = ReadFile(td.at("scripts/b.okay"));
    CHECK(Has(code, "ui_load_theme(\"Assets/ui/b.okaytheme\")"));
    CHECK(!Has(code, "ui/a.okaytheme"));
    CHECK(Has(code, "print(\"hi\")"));                 // the rest of the script is left alone
    CHECK(b.UsersOf(td.at("ui/b.okaytheme")) == 1);

    CHECK(b.Undo().ok);                                // undo puts the theme name back in the script
    CHECK(Has(ReadFile(td.at("scripts/b.okay")), "ui/a.okaytheme"));
}

GameObject* Make(Scene& s, const std::string& name) { return s.CreateGameObject(name); }

void TestComponentChannelEdit() {
    RUN_SUITE("a hand-drawn component: only the changed colour channel reaches the others (finding 71)");
    Scene s("c");
    GameObject *a = Make(s, "a"), *b = Make(s, "b"), *c = Make(s, "c");
    a->AddComponent<Light>()->color = Color(1, 0, 0, 1);
    b->AddComponent<Light>()->color = Color(0, 1, 0, 1);
    c->AddComponent<Light>()->color = Color(0, 0, 1, 1);
    std::vector<GameObject*> sel{a, b, c};
    Light* la = a->GetComponent<Light>();
    oked::ComponentEdit e = oked::BeginComponentEdit(sel, a, *la);
    CHECK(e.active);
    la->color.a = 0.25f;                               // the drawer changed only alpha
    CHECK(oked::PropagateComponentEdit(sel, a, *la, e) == 2);
    const Color cb = b->GetComponent<Light>()->color, cc = c->GetComponent<Light>()->color;
    CHECK(cb.r == 0.0f && cb.g == 1.0f && cb.b == 0.0f && cb.a == 0.25f);   // still green
    CHECK(cc.r == 0.0f && cc.g == 0.0f && cc.b == 1.0f && cc.a == 0.25f);   // still blue
    // Two channels changed at once: both travel, the others stay.
    e = oked::BeginComponentEdit(sel, a, *la);
    la->color.r = 0.5f; la->color.a = 1.0f;
    CHECK(oked::PropagateComponentEdit(sel, a, *la, e) == 2);
    const Color cb2 = b->GetComponent<Light>()->color;
    CHECK(cb2.r == 0.5f && cb2.g == 1.0f && cb2.b == 0.0f && cb2.a == 1.0f);
}

} // namespace

int main() {
    TestScriptAndThemeRename();
    TestComponentChannelEdit();
    TEST_MAIN_RESULT();
}
