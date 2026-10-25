// Parent cycles: Transform::SetParent refuses a self/descendant parent, and the
// scene / prefab / creation loaders reject a file whose parent links form a
// cycle instead of building a looping hierarchy (which used to recurse forever
// on the first world-transform evaluation).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>

using namespace okay;

static bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Evaluate every object's world pose; a cyclic hierarchy overflows the stack here.
static void TouchWorld(Scene& s) {
    for (const auto& up : s.Objects())
        if (up && up->transform) (void)Finite(up->transform->Position());
}

static const char* kCycle =
    "okayscene 1\n"
    "gameobject 0 \"A\"\n  transform 1 0 0 0 0 0 1 1 1 1\n  parent 1\nend\n"
    "gameobject 1 \"B\"\n  transform 0 1 0 0 0 0 1 1 1 1\n  parent 0\nend\n";

static const char* kSelf =
    "okayscene 1\n"
    "gameobject 0 \"A\"\n  transform 1 0 0 0 0 0 1 1 1 1\n  parent 0\nend\n";

// A three-object loop where one object carries a skinned mesh (the reported
// overflow path: its joints are resolved by walking up to the top ancestor).
static const char* kSkinCycle =
    "okayscene 1\n"
    "gameobject 0 \"Root\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent 2\nend\n"
    "gameobject 1 \"Bone\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent 0\nend\n"
    "gameobject 2 \"Body\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent 1\n"
    "  skinmesh 3 0 0 0 1 0 0 0 1 0 0 0 3 0 1 2 3 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0"
    " 1 \"Bone\" 1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\nend\n";

// A well-formed hierarchy (child listed before its parent) — an "old scene".
static const char* kOldScene =
    "okayscene 1\n"
    "gameobject 0 \"Leaf\"\n  transform 1 0 0 0 0 0 1 1 1 1\n  parent 1\nend\n"
    "gameobject 1 \"Mid\"\n  transform 0 2 0 0 0 0 1 1 1 1\n  parent 2\nend\n"
    "gameobject 2 \"Top\"\n  transform 0 0 3 0 0 0 1 1 1 1\n  parent -1\nend\n";

int main() {
    RUN_SUITE("parent_cycles");

    // --- Transform::SetParent refuses self and descendants ---
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("a");
        GameObject* b = s.CreateGameObject("b");
        GameObject* c = s.CreateGameObject("c");
        a->transform->SetParent(a->transform);
        CHECK(a->transform->Parent() == nullptr);
        CHECK(a->transform->ChildCount() == 0);

        b->transform->SetParent(a->transform);
        c->transform->SetParent(b->transform);
        a->transform->SetParent(c->transform);          // grandchild: refused
        CHECK(a->transform->Parent() == nullptr);
        CHECK(c->transform->ChildCount() == 0);
        a->transform->SetParent(b->transform);          // child: refused
        CHECK(a->transform->Parent() == nullptr);
        CHECK(Finite(c->transform->Position()));

        // A legal reparent still works.
        c->transform->SetParent(a->transform);
        CHECK(c->transform->Parent() == a->transform);
        CHECK(b->transform->ChildCount() == 0);
    }

    // --- Scene Deserialize rejects cycles ---
    {
        Scene s("T");
        std::string err;
        const bool ok = SceneSerializer::Deserialize(s, kCycle, &err);
        CHECK(!ok);
        CHECK(!err.empty());
        TouchWorld(s);
    }
    {
        Scene s("T");
        std::string err;
        const bool ok = SceneSerializer::Deserialize(s, kSelf, &err);
        CHECK(!ok);
        CHECK(!err.empty());
        TouchWorld(s);
    }
    {
        Scene s("T");
        std::string err;
        const bool ok = SceneSerializer::Deserialize(s, kSkinCycle, &err);
        CHECK(!ok);
        CHECK(!err.empty());
        if (ok) s.Start();                               // ResolveJoints walks to the top ancestor
        TouchWorld(s);
    }

    // --- Prefab instantiate rejects cycles ---
    {
        Scene s("T");
        std::string err;
        GameObject* g = SceneSerializer::InstantiateFromText(s, kCycle, &err);
        CHECK(g == nullptr);
        CHECK(!err.empty());
        TouchWorld(s);
    }

    // --- Build creation rejects cycles and leaves the scene alone ---
    {
        Scene s("T");
        std::string body = kCycle;
        std::string text = "okaycreation 1\nobjects 1\nobject\n" + body + "endobject\n";
        std::string err;
        const std::size_t before = s.Objects().size();
        CHECK(!BuildCreation::Deserialize(s, text, true, &err));
        CHECK(!err.empty());
        CHECK(s.Objects().size() == before);
        TouchWorld(s);
    }

    // --- Old scenes load exactly as before ---
    {
        Scene s("T");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, kOldScene, &err));
        CHECK(err.empty());
        GameObject* leaf = s.Find("Leaf");
        GameObject* mid  = s.Find("Mid");
        GameObject* top  = s.Find("Top");
        CHECK(leaf && mid && top);
        if (leaf && mid && top) {
            CHECK(leaf->transform->Parent() == mid->transform);
            CHECK(mid->transform->Parent() == top->transform);
            CHECK(top->transform->Parent() == nullptr);
            Vec3 p = leaf->transform->Position();
            CHECK_NEAR(p.x, 1.0f, 1e-5f);
            CHECK_NEAR(p.y, 2.0f, 1e-5f);
            CHECK_NEAR(p.z, 3.0f, 1e-5f);
            // Round trip keeps the same structure.
            std::string text = SceneSerializer::Serialize(s);
            Scene s2("T2");
            CHECK(SceneSerializer::Deserialize(s2, text, &err));
            GameObject* leaf2 = s2.Find("Leaf");
            CHECK(leaf2 && leaf2->transform->Parent() &&
                  leaf2->transform->Parent()->gameObject->name == "Mid");
        }
    }

    TEST_MAIN_RESULT();
}
