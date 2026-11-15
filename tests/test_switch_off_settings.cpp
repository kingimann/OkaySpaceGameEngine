// Settings of a switched-off feature survive save and load (Grok, issue #888 finding 1). SceneSerializer wrote the
// MeshRenderer outline / rim / toon / gradient / normal strength / AO strength values only while their feature was on,
// and the VehicleController "vehiclesusp" record only while suspension was on, so e.g. Outline Width 0.4 with Outline
// off came back as 0.03 after save and reopen, Ctrl+D or a prefab. Each check below failed before the fix.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VehicleController.hpp"
#include <string>

using namespace okay;

static GameObject* RoundTrip(Scene& b, const Scene& a) {
    std::string err;
    CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(a), &err));
    const auto roots = b.Roots();
    return roots.empty() ? nullptr : roots[0];
}

static bool Same(const Color& x, const Color& y) { return x.r == y.r && x.g == y.g && x.b == y.b; }

static void TestMeshRenderer() {
    RUN_SUITE("switch-off settings: MeshRenderer");
    Scene a("a");
    GameObject* go = a.CreateGameObject("Thing");
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->outline = false; mr->outlineWidth = 0.4f; mr->outlineColor = Color{0.5f, 0.25f, 0.125f, 1.0f};
    mr->normalMap.clear(); mr->normalStrength = 0.3f;
    mr->aoMap.clear(); mr->aoStrength = 0.5f;
    mr->shader = MeshRenderer::Shader::Standard; mr->toonBands = 5;
    mr->gradientTop = Color{0.25f, 0.5f, 0.75f, 1.0f}; mr->gradientBottom = Color{0.75f, 0.5f, 0.25f, 1.0f};
    mr->rimStrength = 0.0f; mr->rimPower = 6.0f; mr->rimColor = Color{0.125f, 0.25f, 1.0f, 1.0f};
    Scene b("b");
    GameObject* back = RoundTrip(b, a);
    CHECK(back != nullptr);
    auto* m = back ? back->GetComponent<MeshRenderer>() : nullptr;
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(!m->outline);                                                   // still off
    CHECK(m->outlineWidth == 0.4f && Same(m->outlineColor, Color{0.5f, 0.25f, 0.125f, 1.0f}));
    CHECK(m->normalStrength == 0.3f);
    CHECK(m->aoStrength == 0.5f);
    CHECK(m->shader == MeshRenderer::Shader::Standard && m->toonBands == 5);
    CHECK(Same(m->gradientTop, Color{0.25f, 0.5f, 0.75f, 1.0f}) && Same(m->gradientBottom, Color{0.75f, 0.5f, 0.25f, 1.0f}));
    CHECK(m->rimStrength == 0.0f && m->rimPower == 6.0f && Same(m->rimColor, Color{0.125f, 0.25f, 1.0f, 1.0f}));
    // a plain renderer still writes none of these records (no change for existing scenes)
    Scene c("c");
    c.CreateGameObject("Plain")->AddComponent<MeshRenderer>();
    const std::string plain = SceneSerializer::Serialize(c);
    for (const char* rec : {"  outline", "  outlineoff", "  normalmap", "  aomap", "  shader", "  rim "})
        CHECK(plain.find(rec) == std::string::npos);
    // the outline switched on still loads on
    mr->outline = true;
    Scene d("d");
    GameObject* on = RoundTrip(d, a);
    CHECK(on && on->GetComponent<MeshRenderer>()->outline && on->GetComponent<MeshRenderer>()->outlineWidth == 0.4f);
}

static void TestVehicle() {
    RUN_SUITE("switch-off settings: VehicleController suspension");
    Scene a("a");
    auto* v = a.CreateGameObject("Car")->AddComponent<VehicleController>();
    v->suspension = false; v->rideHeight = 1.25f; v->springStrength = 55.0f; v->maxTilt = 12.0f;
    Scene b("b");
    GameObject* back = RoundTrip(b, a);
    auto* w = back ? back->GetComponent<VehicleController>() : nullptr;
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(!w->suspension);
    CHECK(w->rideHeight == 1.25f && w->springStrength == 55.0f && w->maxTilt == 12.0f);
    Scene c("c");
    c.CreateGameObject("Plain")->AddComponent<VehicleController>();
    CHECK(SceneSerializer::Serialize(c).find("vehiclesusp") == std::string::npos);   // defaults: no record, as before
}

// Review: an older build keeps "outlineoff" as unknown text and writes it after the components, so a file can hold an
// "outline" record (switched on there) and a stale "outlineoff" after it. The outline that is on wins.
static void TestMixedBuilds() {
    RUN_SUITE("switch-off settings: a stale outlineoff after an outline does not switch it off");
    const std::string text =
        "okayscene 3\nname \"Mixed\"\ngameobject 0 \"T\"\n  active 1\n  parent -1\n  transform 0 0 0 0 0 0 1 1 1 1\n"
        "    mesh \"Cube\" 1 1 1 1 0 \"\" 0\n    outline 0.2 1 0 0\n  outlineoff 0.7 0 1 0\nend\n";
    Scene s("s");
    std::string err;
    CHECK(SceneSerializer::Deserialize(s, text, &err));
    const auto roots = s.Roots();
    auto* m = roots.empty() ? nullptr : roots[0]->GetComponent<MeshRenderer>();
    CHECK(m && m->outline && m->outlineWidth == 0.2f && m->outlineColor.r == 1.0f && m->outlineColor.g == 0.0f);
}

int main() {
    TestMixedBuilds();
    TestMeshRenderer();
    TestVehicle();
    TEST_MAIN_RESULT();
}
