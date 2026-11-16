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
#include "SnapIncrement.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

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

void TestTransformAxisEdit() {
    RUN_SUITE("Position Y edited with 5 objects selected keeps each object's own X and Z (finding 71)");
    Scene s("t");
    std::vector<GameObject*> sel;
    for (int i = 0; i < 5; ++i) {
        GameObject* g = Make(s, "o" + std::to_string(i));
        g->transform->localPosition = Vec3(float(i), 10.0f + float(i), -float(i) * 2.0f);
        g->transform->localScale = Vec3(1.0f + float(i), 2.0f, 3.0f);
        sel.push_back(g);
    }
    GameObject* primary = sel[2];
    // What the Inspector row does: the primary is edited, the changed axes go to the others.
    const Vec3 before = primary->transform->localPosition;
    Vec3 after = before; after.y = 0.0f;
    const unsigned mask = oked::ChangedAxes(before, after);
    CHECK(mask == 2u);
    primary->transform->localPosition = after;
    CHECK(oked::ApplyTransformAxes(sel, primary, oked::TransformField::Position, after, mask) == 4);
    for (int i = 0; i < 5; ++i) {
        const Vec3 p = sel[(std::size_t)i]->transform->localPosition;
        CHECK(p.y == 0.0f);                                  // all on the floor
        CHECK(p.x == float(i) && p.z == -float(i) * 2.0f);   // each keeps its own X and Z
    }
    // Scale X only; a camera's scale is locked and is skipped.
    sel[4]->AddComponent<Camera>();
    const Vec3 sb = primary->transform->localScale;
    Vec3 sa = sb; sa.x = 5.0f;
    primary->transform->localScale = sa;
    CHECK(oked::ApplyTransformAxes(sel, primary, oked::TransformField::Scale, sa, oked::ChangedAxes(sb, sa)) == 3);
    CHECK(sel[0]->transform->localScale.x == 5.0f && sel[0]->transform->localScale.y == 2.0f);
    CHECK(sel[4]->transform->localScale.x == 5.0f);          // (it was 5 already: 1 + 4)
    sel[4]->transform->localScale.x = 9.0f;
    CHECK(oked::ApplyTransformAxes(sel, primary, oked::TransformField::Scale, sa, 1u) == 0);
    CHECK(sel[4]->transform->localScale.x == 9.0f);          // the camera kept its scale
    // Nothing changed: nothing written.
    CHECK(oked::ApplyTransformAxes(sel, primary, oked::TransformField::Position, after, 0u) == 0);
    // Rotation angles (Euler) use the same merge.
    const Vec3 m = oked::MergeAxes(Vec3(10, 20, 30), Vec3(0, 90, 0), oked::ChangedAxes(Vec3(1, 2, 3), Vec3(1, 90, 3)));
    CHECK(m.x == 10.0f && m.y == 90.0f && m.z == 30.0f);
}

void TestSnapIncrement() {
    RUN_SUITE("snap steps: 0, -1, inf and NaN are clamped or refused (finding 72)");
    using oked::kMoveSnapLimits; using oked::kRotateSnapLimits;
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    auto zero = oked::CleanSnapIncrement(0.0f, 0.5f, kMoveSnapLimits);
    CHECK(zero.changed && zero.value == kMoveSnapLimits.min && zero.value > 0.0f && zero.note[0] != 0);
    auto neg = oked::CleanSnapIncrement(-1.0f, 0.5f, kMoveSnapLimits);
    CHECK(neg.changed && neg.value == kMoveSnapLimits.min);
    auto pinf = oked::CleanSnapIncrement(inf, 0.5f, kMoveSnapLimits);
    CHECK(pinf.changed && pinf.value == 0.5f && pinf.note[0] != 0);          // refused: the old step stays
    auto ninf = oked::CleanSnapIncrement(-inf, 0.5f, kMoveSnapLimits);
    CHECK(ninf.changed && ninf.value == 0.5f);
    auto bad = oked::CleanSnapIncrement(nan, 0.5f, kMoveSnapLimits);
    CHECK(bad.changed && bad.value == 0.5f);
    auto badPrev = oked::CleanSnapIncrement(nan, nan, kMoveSnapLimits);      // the old one was bad too: the default
    CHECK(badPrev.value == kMoveSnapLimits.fallback);
    auto huge = oked::CleanSnapIncrement(3e38f, 0.5f, kMoveSnapLimits);
    CHECK(huge.changed && huge.value == kMoveSnapLimits.max);
    auto ok = oked::CleanSnapIncrement(0.25f, 0.5f, kMoveSnapLimits);
    CHECK(!ok.changed && ok.value == 0.25f && ok.note[0] == 0);
    auto rot0 = oked::CleanSnapIncrement(0.0f, 15.0f, kRotateSnapLimits);
    CHECK(rot0.value == kRotateSnapLimits.min && rot0.value > 0.0f);
    CHECK(oked::CleanSnapIncrement(inf, 15.0f, kRotateSnapLimits).value == 15.0f);
    // Every snapping path uses SnapStep: always finite and positive, so a snapped value is never NaN.
    for (float bad2 : {0.0f, -1.0f, inf, -inf, nan, 3e38f, 1e-30f}) {
        const float st = oked::SnapStep(bad2, kMoveSnapLimits);
        CHECK(std::isfinite(st) && st >= kMoveSnapLimits.min && st <= kMoveSnapLimits.max);
        const float v = oked::SnapToStep(1.3f, bad2, kMoveSnapLimits);
        CHECK(std::isfinite(v));
        CHECK(std::isfinite(oked::SnapToStep(37.0f, bad2, kRotateSnapLimits)));
    }
    CHECK(oked::SnapToStep(1.3f, 0.5f, kMoveSnapLimits) == 1.5f);
    CHECK(oked::SnapToStep(37.0f, 15.0f, kRotateSnapLimits) == 30.0f);
    // The saved setting: 0, negative or broken loads as the default; good values come back, clamped.
    CHECK(oked::LoadSnapIncrement(0.0f, kMoveSnapLimits) == 1.0f);
    CHECK(oked::LoadSnapIncrement(-5.0f, kMoveSnapLimits) == 1.0f);
    CHECK(oked::LoadSnapIncrement(nan, kRotateSnapLimits) == 15.0f);
    CHECK(oked::LoadSnapIncrement(0.25f, kMoveSnapLimits) == 0.25f);
    CHECK(oked::LoadSnapIncrement(5000.0f, kMoveSnapLimits) == kMoveSnapLimits.max);
}

} // namespace

int main() {
    TestScriptAndThemeRename();
    TestComponentChannelEdit();
    TestTransformAxisEdit();
    TestSnapIncrement();
    TEST_MAIN_RESULT();
}
