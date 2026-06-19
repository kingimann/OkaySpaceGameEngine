#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("scriptapi");

    // New 3D rigidbody + identity builtins drive a Rigidbody3D from OkayScript.
    {
        Scene s("api");
        GameObject* o = s.CreateGameObject("Hero");
        o->tag = "player";
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->bodyType = Rigidbody3D::BodyType::Kinematic; // move purely by velocity
        auto* sc = o->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        bool ok = sc->LoadSource(
            "var who = \"\";\n"
            "var tg = false;\n"
            "function start() {\n"
            "    set_velocity3(0, 0, 4);\n"
            "    who = name();\n"
            "    tg = has_tag(\"player\");\n"
            "}\n"
            "function update(dt) {\n"
            "    if (pos_z() > 1) { jump(2); }\n"
            "}\n", &err);
        CHECK(ok);
        if (!ok) std::cerr << "  script error: " << err << "\n";

        s.Start();
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f); // ~1s
        // Moved forward in +Z at 4 u/s for ~1s.
        CHECK(o->transform->localPosition.z > 1.0f);
        CHECK(sc->VM()->GetGlobal("who").AsString() == "Hero");
        CHECK(sc->VM()->GetGlobal("tg").AsBool());
    }

    // Editor clipboard path: serialize an object and instantiate it from text.
    {
        Scene s("clip");
        GameObject* a = s.CreateGameObject("Box");
        a->transform->localPosition = {2, 3, 4};
        a->AddComponent<BoxCollider3D>();
        std::string text = SceneSerializer::SerializeObject(*a);

        GameObject* b = SceneSerializer::InstantiateFromText(s, text);
        CHECK(b != nullptr);
        if (b) {
            CHECK(b->GetComponent<BoxCollider3D>() != nullptr);
            CHECK_NEAR(b->transform->localPosition.x, 2.0f, 1e-5f);
        }
    }

    TEST_MAIN_RESULT();
}
