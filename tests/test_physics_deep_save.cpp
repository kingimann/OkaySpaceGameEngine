// Scene files and the new physics settings: the quality record, rolling resistance, the compound-children switch and the
// per-collider physics materials round-trip; a file written before them loads as Classic with the old defaults.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

static GameObject* Find(Scene& s, const std::string& name) {
    for (const auto& o : s.Objects()) if (o->name == name) return o.get();
    return nullptr;
}

int main() {
    RUN_SUITE("physics_deep_save");

    // ---- A Classic scene saves no physicsquality record (the file is byte for byte what older builds wrote) ...
    {
        Scene a("a");
        a.CreateGameObject("O")->AddComponent<Rigidbody3D>();
        const std::string text = SceneSerializer::Serialize(a);
        CHECK(text.find("physicsquality") == std::string::npos);
        CHECK(text.find("physmat") == std::string::npos);
        // ... and loads as Classic.
        Scene b("b");
        b.physics3D().quality = PhysicsQuality::Improved;       // a leftover setting must not survive loading an old file
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        CHECK(b.physics3D().quality == PhysicsQuality::Classic);
        CHECK(b.physics().quality == PhysicsQuality::Classic);
    }
    // ---- An Improved scene keeps its settings.
    {
        Scene a("a");
        a.physics3D().quality = PhysicsQuality::Improved; a.physics().quality = PhysicsQuality::Improved;
        a.physics3D().solverIterations = 17; a.physics().solverIterations = 12; a.physics3D().autoContinuous = false;
        const std::string text = SceneSerializer::Serialize(a);
        CHECK(text.find("physicsquality 1 17 0 1 12") != std::string::npos);
        Scene b("b");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        CHECK(b.physics3D().quality == PhysicsQuality::Improved);
        CHECK(b.physics().quality == PhysicsQuality::Improved);
        CHECK(b.physics3D().solverIterations == 17);
        CHECK(b.physics().solverIterations == 12);
        CHECK(!b.physics3D().autoContinuous);
    }
    // ---- Clearing a scene puts the quality back to Classic (a new scene is Classic until the editor says otherwise).
    {
        Scene a("a");
        a.physics3D().quality = PhysicsQuality::Improved;
        a.Clear();
        CHECK(a.physics3D().quality == PhysicsQuality::Classic);
        CHECK(a.physics3D().solverIterations == 10);
    }
    // ---- Rigidbody and collider fields.
    {
        Scene a("a");
        GameObject* o = a.CreateGameObject("O");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->rollingResistance = 0.07f; rb->compoundChildren = true;
        auto* c1 = o->AddComponent<BoxCollider3D>();
        auto* c2 = o->AddComponent<SphereCollider3D>();
        c2->friction = 0.05f; c2->bounciness = 0.8f; c2->frictionCombine = CombineMode::Minimum; c2->bounceCombine = CombineMode::Maximum;
        GameObject* g = a.CreateGameObject("G");
        auto* rb2 = g->AddComponent<Rigidbody2D>(); rb2->rollingResistance = 0.11f;
        auto* d1 = g->AddComponent<CircleCollider2D>(); d1->friction = 0.9f;
        (void)c1;
        const std::string text = SceneSerializer::Serialize(a);
        CHECK(text.find("physmat3d") != std::string::npos);
        Scene b("b");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        GameObject* o2 = Find(b, "O"); GameObject* g2 = Find(b, "G");
        CHECK(o2 && g2);
        if (o2 && g2) {
            auto* r = o2->GetComponent<Rigidbody3D>();
            CHECK(r && std::fabs(r->rollingResistance - 0.07f) < 1e-6f && r->compoundChildren);
            auto* b1 = o2->GetComponent<BoxCollider3D>();
            auto* s2 = o2->GetComponent<SphereCollider3D>();
            CHECK(b1 && b1->friction < 0.0f && b1->bounciness < 0.0f && b1->frictionCombine == CombineMode::Default);   // untouched stays "use the body's"
            CHECK(s2 && std::fabs(s2->friction - 0.05f) < 1e-6f && std::fabs(s2->bounciness - 0.8f) < 1e-6f);
            CHECK(s2 && s2->frictionCombine == CombineMode::Minimum && s2->bounceCombine == CombineMode::Maximum);
            auto* r2 = g2->GetComponent<Rigidbody2D>();
            CHECK(r2 && std::fabs(r2->rollingResistance - 0.11f) < 1e-6f);
            auto* c = g2->GetComponent<CircleCollider2D>();
            CHECK(c && std::fabs(c->friction - 0.9f) < 1e-6f && c->bounciness < 0.0f);
        }
    }
    // ---- A body written before these fields existed loads with the defaults (rolling 0.03, no compound children, no material).
    {
        const std::string old =
            "okayscene 3\ngameobject 0 \"O\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n"
            "  rigidbody3d 0 1 2 0.1 0.2 0 0 0 0 0.4 0.05 0 1 0\n"
            "  boxcollider3d 1 1 1 0 0 0 0 0 0\n  parent -1\nend\n";
        Scene b("b");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, old, &err));
        GameObject* o = Find(b, "O");
        CHECK(o != nullptr);
        if (o) {
            auto* r = o->GetComponent<Rigidbody3D>();
            CHECK(r && std::fabs(r->mass - 2.0f) < 1e-6f && std::fabs(r->friction - 0.4f) < 1e-6f);
            CHECK(r && std::fabs(r->rollingResistance - 0.03f) < 1e-6f && !r->compoundChildren);
            auto* c = o->GetComponent<BoxCollider3D>();
            CHECK(c && c->friction < 0.0f && c->bounciness < 0.0f);
        }
    }
    TEST_MAIN_RESULT();
}
