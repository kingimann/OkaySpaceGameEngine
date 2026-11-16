// Issue #888 finding 30: a value typed into a Visual Script Vector3 pin
// ("0,5,0", "0 5 0", "(0, 5, 0)") or a Color pin ("#ff8000", "1,0.5,0",
// "255,128,0") must be read as written, not as zero / black, and Check Graph
// warns once when a typed Vector3 / Color value can't be read.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

static VisualScriptComponent* Attach(GameObject* go, const std::string& src) {
    auto* v = go->AddComponent<VisualScriptComponent>();
    std::string err;
    const bool ok = v->LoadFromText(src, &err);
    CHECK(ok);
    if (!ok) std::cerr << "  parse error: " << err << "\n";
    return v;
}
static void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) {
    for (int i = 0; i < frames; ++i) s.Update(dt);
}
// Check Graph messages about a typed value that can't be read.
static int Unreadable(const std::string& graph) {
    int n = 0;
    for (const vs::VsIssue& is : vs::ValidateGraphText(graph))
        if (is.message.find("can't be read") != std::string::npos) ++n;
    return n;
}
static void ExpectVec(const Vec3& v, float x, float y, float z) {
    CHECK_NEAR(v.x, x, 1e-4f);
    CHECK_NEAR(v.y, y, 1e-4f);
    CHECK_NEAR(v.z, z, 1e-4f);
}
static void ExpectColor(const vs::VsColor& c, float r, float g, float b, float a) {
    CHECK_NEAR(c.r, r, 1e-3f);
    CHECK_NEAR(c.g, g, 1e-3f);
    CHECK_NEAR(c.b, b, 1e-3f);
    CHECK_NEAR(c.a, a, 1e-3f);
}

int main() {
    RUN_SUITE("vs_vec3_text");

    // ---- Set Position (Vector) with each typed format ends at (0, 5, 0).
    for (const char* typed : {"0,5,0", "0 5 0", "(0, 5, 0)", " ( 0 ,5 , 0 ) ", "0, 5, 0"}) {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Hero");
        go->transform->localPosition = {7, 7, 7};
        Attach(go, std::string("node 1 OnStart\nentry OnStart 1\nnode 2 SetPositionVec\n"
                               "default 2 0 \"") + typed + "\"\nexec 1 0 2\n");
        s.Start(); Tick(s, 1);
        ExpectVec(go->transform->Position(), 0, 5, 0);
    }

    // ---- A single number still means (n, n, n); a real Vec3 is unchanged.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Hero");
        Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 SetPositionVec\ndefault 2 0 5\nexec 1 0 2\n");
        s.Start(); Tick(s, 1);
        ExpectVec(go->transform->Position(), 5, 5, 5);
        ExpectVec(vs::VsValue(5.0f).AsVec3(), 5, 5, 5);
        ExpectVec(vs::VsValue(std::string("5")).AsVec3(), 5, 5, 5);
        ExpectVec(vs::VsValue(Vec3{1, 2, 3}).AsVec3(), 1, 2, 3);
        ExpectVec(vs::VsValue(std::string("1,2")).AsVec3(), 1, 2, 0);   // x,y: z is 0
    }

    // ---- Text that isn't a vector (or isn't finite) reads as zero.
    for (const char* bad : {"nan,0,0", "1,inf,0", "abc", "1,2,3,4", "1,,2", "1,2,", "(1,2,3", "1a,2,3"})
        ExpectVec(vs::VsValue(std::string(bad)).AsVec3(), 0, 0, 0);

    // ---- Vec3 Add of the texts "1,2,3" and "1,2,3" is (2, 4, 6).
    {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Adder");
        auto* v = Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 Vec3Add\n"
                             "default 2 0 \"1,2,3\"\ndefault 2 1 \"1,2,3\"\n"
                             "node 3 SetVar r\ndata 3 0 2 0\nexec 1 0 3\n");
        s.Start(); Tick(s, 1);
        ExpectVec(v->GetVariable("r").AsVec3(), 2, 4, 6);
    }

    // ---- Tween Position to a typed "0,5,0" ends there.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Slide");
        Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 3 TweenPosition linear\n"
                   "default 3 0 \"0,5,0\"\ndefault 3 1 0.2\nexec 1 0 3\n");
        s.Start(); Tick(s, 8, 0.05f);
        ExpectVec(go->transform->Position(), 0, 5, 0);
    }

    // ---- Raycast 3D from a typed origin hits a box that's off to the side.
    {
        Scene s("Ray");
        GameObject* me = s.CreateGameObject("Eye");
        GameObject* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {20, 0, -10};
        wall->AddComponent<BoxCollider3D>()->size = {4, 4, 1};
        auto* v = Attach(me, "node 0 OnStart\nentry OnStart 0\nnode 1 Raycast3D\n"
                             "default 1 0 \"20,0,0\"\ndefault 1 1 \"(0, 0, -1)\"\ndefault 1 2 100\n"
                             "node 2 SetVar hit\ndata 2 0 1 0\nnode 3 SetVar what\ndata 3 0 1 4\n"
                             "exec 0 0 2\nexec 2 0 3\n");
        s.Start(); Tick(s, 1);
        CHECK(v->GetVariable("hit").AsBool());
        CHECK(v->GetVariable("what").AsString() == "Wall");
    }

    // ---- Color text.
    {
        ExpectColor(vs::VsValue(std::string("#ff8000")).AsColor(), 1, 128 / 255.0f, 0, 1);
        ExpectColor(vs::VsValue(std::string("#FF800080")).AsColor(), 1, 128 / 255.0f, 0, 128 / 255.0f);
        ExpectColor(vs::VsValue(std::string("1,0.5,0")).AsColor(), 1, 0.5f, 0, 1);
        ExpectColor(vs::VsValue(std::string("1, 0.5, 0, 0.25")).AsColor(), 1, 0.5f, 0, 0.25f);
        ExpectColor(vs::VsValue(std::string("255,128,0")).AsColor(), 1, 128 / 255.0f, 0, 1);
        ExpectColor(vs::VsValue(std::string("(255 128 0 255)")).AsColor(), 1, 128 / 255.0f, 0, 1);
        ExpectColor(vs::VsValue(0.5f).AsColor(), 0.5f, 0.5f, 0.5f, 1);   // a number is a grey
        ExpectColor(vs::VsValue(std::string("nonsense")).AsColor(), 0, 0, 0, 1);
        ExpectColor(vs::VsValue(std::string("#ff80")).AsColor(), 0, 0, 0, 1);
        ExpectColor(vs::VsValue::Color(0.1f, 0.2f, 0.3f, 0.4f).AsColor(), 0.1f, 0.2f, 0.3f, 0.4f);

        // Split Color of a typed "#ff8000" in a graph.
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Paint");
        auto* v = Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 SplitColor\ndefault 2 0 \"#ff8000\"\n"
                             "node 3 SetVar r\ndata 3 0 2 0\nnode 4 SetVar g\ndata 4 0 2 1\n"
                             "node 5 SetVar b\ndata 5 0 2 2\nexec 1 0 3\nexec 3 0 4\nexec 4 0 5\n");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(v->GetVariable("r").AsFloat(), 1.0f, 1e-3f);
        CHECK_NEAR(v->GetVariable("g").AsFloat(), 128 / 255.0f, 1e-3f);
        CHECK_NEAR(v->GetVariable("b").AsFloat(), 0.0f, 1e-3f);
    }

    // ---- Check Graph: one warning for a typed value it can't read, none for good ones.
    {
        const std::string head = "node 1 OnStart\nentry OnStart 1\nnode 2 SetPositionVec\nexec 1 0 2\n";
        CHECK(Unreadable(head + "default 2 0 \"up a bit\"\n") == 1);
        CHECK(Unreadable(head + "default 2 0 \"nan,0,0\"\n") == 1);
        CHECK(Unreadable(head + "default 2 0 \"0,5,0\"\n") == 0);
        CHECK(Unreadable(head + "default 2 0 \"(0, 5, 0)\"\n") == 0);
        CHECK(Unreadable(head + "default 2 0 5\n") == 0);
        CHECK(Unreadable(head) == 0);
        const std::string col = "node 1 OnStart\nentry OnStart 1\nnode 2 SplitColor\nnode 3 SetVar r\n"
                                "data 3 0 2 0\nexec 1 0 3\n";
        CHECK(Unreadable(col + "default 2 0 \"#ff8000\"\n") == 0);
        CHECK(Unreadable(col + "default 2 0 \"255,128,0\"\n") == 0);
        CHECK(Unreadable(col + "default 2 0 \"orange\"\n") == 1);
        // A text pin is not checked.
        CHECK(Unreadable("node 1 OnStart\nentry OnStart 1\nnode 2 Print\ndefault 2 0 \"hello\"\nexec 1 0 2\n") == 0);
    }

    TEST_MAIN_RESULT();
}
