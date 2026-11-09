// Spawning: Instantiate with a position, rotation and parent, Duplicate (same parent, next
// to the original, unique name), UniqueName, hierarchy path, layer helpers, name queries,
// world bounds of a hierarchy.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace {

bool NearV(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
bool NearQ(const Quat& a, const Quat& b, float eps = 1e-4f) { return std::fabs(Quat::Dot(a, b)) > 1.0f - eps; }

std::vector<std::string> ChildNames(const Transform* t) {
    std::vector<std::string> out;
    for (const Transform* c : t->Children()) out.push_back(c->gameObject->name);
    return out;
}
std::vector<std::string> RootNames(Scene& s) {
    std::vector<std::string> out;
    for (GameObject* g : s.Roots()) out.push_back(g->name);
    return out;
}

} // namespace

int main() {
    RUN_SUITE("Instantiate: position, rotation, parent");
    {
        Scene s("T");
        GameObject* proto = s.CreateGameObject("Bullet");
        proto->transform->localScale = {2.0f, 2.0f, 2.0f};
        proto->AddComponent<Mover>()->velocity = {7.0f, 0.0f, 0.0f};   // a component the serializer knows: it is what a clone copies
        GameObject* kid = s.CreateGameObject("Fin");
        kid->transform->SetParent(proto->transform, false);
        kid->transform->localPosition = {0.0f, 1.0f, 0.0f};

        const Quat turn = Quat::Euler(0.0f, 90.0f, 0.0f);
        GameObject* a = s.Instantiate(*proto, Vec3{4.0f, 5.0f, 6.0f}, turn);
        CHECK(a && a != proto);
        CHECK(NearV(a->transform->Position(), {4.0f, 5.0f, 6.0f}) && NearQ(a->transform->Rotation(), turn));
        CHECK(NearV(a->transform->localScale, {2.0f, 2.0f, 2.0f}));
        CHECK(a->GetComponent<Mover>() && a->GetComponent<Mover>()->velocity.x == 7.0f);
        CHECK(a->transform->ChildCount() == 1 && a->transform->Parent() == nullptr);

        // under a parent: the position and rotation are WORLD values
        GameObject* holder = s.CreateGameObject("Holder");
        holder->transform->localPosition = {10.0f, 0.0f, 0.0f};
        holder->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        GameObject* b = s.Instantiate(*proto, Vec3{1.0f, 2.0f, 3.0f}, turn, holder->transform);
        CHECK(b && b->transform->Parent() == holder->transform);
        CHECK(NearV(b->transform->Position(), {1.0f, 2.0f, 3.0f}));
        CHECK(NearQ(b->transform->Rotation(), turn));

        // parent only: local pose kept (default) or the world pose kept
        proto->transform->localPosition = {3.0f, 0.0f, 0.0f};
        GameObject* c = s.Instantiate(*proto, holder->transform);
        CHECK(c && c->transform->Parent() == holder->transform);
        CHECK(NearV(c->transform->localPosition, {3.0f, 0.0f, 0.0f}));
        GameObject* d = s.Instantiate(*proto, holder->transform, /*worldPositionStays=*/true);
        CHECK(d && NearV(d->transform->Position(), {3.0f, 0.0f, 0.0f}));
        CHECK(d->transform->Parent() == holder->transform);
        // a null parent is a root
        GameObject* e = s.Instantiate(*proto, static_cast<Transform*>(nullptr));
        CHECK(e && e->transform->Parent() == nullptr);

        // a parent in another scene is refused: the clone is made as a root
        Scene other("O");
        GameObject* foreign = other.CreateGameObject("F");
        GameObject* f = s.Instantiate(*proto, foreign->transform);
        CHECK(f && f->transform->Parent() == nullptr && foreign->transform->ChildCount() == 0);

        // the original is not moved by any of this
        CHECK(NearV(proto->transform->localPosition, {3.0f, 0.0f, 0.0f}));
        CHECK(proto->transform->Parent() == nullptr);
    }

    RUN_SUITE("Instantiate of a child object");
    {
        Scene s("T");
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localPosition = {100.0f, 0.0f, 0.0f};
        GameObject* child = s.CreateGameObject("Child");
        child->transform->SetParent(parent->transform, false);
        child->transform->localPosition = {1.0f, 0.0f, 0.0f};
        // the plain call makes a root with the original's local pose: unchanged
        GameObject* c1 = s.Instantiate(*child);
        CHECK(c1 && c1->transform->Parent() == nullptr && NearV(c1->transform->localPosition, {1.0f, 0.0f, 0.0f}));
    }

    RUN_SUITE("unique names");
    {
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        CHECK(s.UniqueName("Cube") == "Cube");                    // free: as asked
        s.CreateGameObject("Cube");
        CHECK(s.UniqueName("Cube") == "Cube (1)");
        s.CreateGameObject("Cube (1)");
        CHECK(s.UniqueName("Cube") == "Cube (2)");
        CHECK(s.UniqueName("Cube (1)") == "Cube (2)");            // a numbered name counts from its stem
        CHECK(s.UniqueName("Cube (7)") == "Cube (7)");            // a free numbered name is kept
        s.CreateGameObject("Cube (3)");
        CHECK(s.UniqueName("Cube (3)") == "Cube (2)");            // the first free number
        CHECK(s.UniqueName("") == "GameObject");
        CHECK(s.UniqueName("A (x)") == "A (x)");                  // not a number: part of the name
        // names only have to be unique among siblings
        GameObject* kid = s.CreateGameObject("Cube");
        kid->transform->SetParent(root->transform, false);
        CHECK(s.UniqueName("Cube", root->transform) == "Cube (1)");
        CHECK(s.UniqueName("Cube", kid->transform) == "Cube");
        CHECK(s.UniqueName("Root", root->transform) == "Root");   // no child called that
    }

    RUN_SUITE("Duplicate");
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        p->transform->localPosition = {5.0f, 0.0f, 0.0f};
        GameObject* a = s.CreateGameObject("Crate");
        GameObject* z = s.CreateGameObject("Z");
        a->transform->SetParent(p->transform, false);
        z->transform->SetParent(p->transform, false);
        a->transform->localPosition = {1.0f, 2.0f, 3.0f};
        a->transform->localRotation = Quat::Euler(10.0f, 20.0f, 30.0f);
        GameObject* leaf = s.CreateGameObject("Leaf");
        leaf->transform->SetParent(a->transform, false);
        a->tag = "Box"; a->layer = 4;
        a->AddComponent<Mover>()->velocity = {42.0f, 0.0f, 0.0f};

        GameObject* d = s.Duplicate(*a);
        CHECK(d && d != a);
        CHECK(d->name == "Crate (1)");
        CHECK(d->transform->Parent() == p->transform);
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"Crate", "Crate (1)", "Z"}));   // right after the original
        CHECK(NearV(d->transform->localPosition, {1.0f, 2.0f, 3.0f}) && NearQ(d->transform->localRotation, a->transform->localRotation));
        CHECK(d->tag == "Box" && d->layer == 4 && d->GetComponent<Mover>() && d->GetComponent<Mover>()->velocity.x == 42.0f);
        CHECK(d->transform->ChildCount() == 1 && d->transform->GetChild(0)->gameObject->name == "Leaf");
        CHECK(d->transform->GetChild(0)->gameObject != leaf);       // a deep copy
        CHECK(d->Guid() != a->Guid() && d->InstanceId() != a->InstanceId());
        CHECK(NearV(d->transform->Position(), a->transform->Position()));   // same place in the world

        GameObject* d2 = s.Duplicate(*d);                           // duplicating the copy
        CHECK(d2 && d2->name == "Crate (2)");
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"Crate", "Crate (1)", "Crate (2)", "Z"}));

        // a root: placed right after the original among the roots
        GameObject* r = s.Duplicate(*p);
        CHECK(r && r->name == "P (1)" && r->transform->Parent() == nullptr);
        CHECK((RootNames(s) == std::vector<std::string>{"P", "P (1)"}));
        CHECK(NearV(r->transform->Position(), p->transform->Position()));
        CHECK(r->transform->ChildCount() == 4);
        // the structure moved, so caches re-resolve
        const std::uint64_t v = s.StructureVersion();
        s.Duplicate(*z);
        CHECK(s.StructureVersion() != v);
        // a destroyed original gives nothing
        s.Destroy(z);
        s.FlushDestroyed();
        CHECK(s.Duplicate(*z) == nullptr);
    }

    RUN_SUITE("layer helpers");
    {
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        a->transform->SetParent(root->transform, false);
        b->transform->SetParent(a->transform, false);
        CHECK(root->SetLayer(5));
        CHECK(root->layer == 5 && a->layer == 0);
        CHECK(root->SetLayer(6, /*includeChildren=*/true));
        CHECK(root->layer == 6 && a->layer == 6 && b->layer == 6);
        CHECK(!root->SetLayer(32) && !root->SetLayer(-1) && !root->SetLayer(99, true));
        CHECK(root->layer == 6 && b->layer == 6);                   // refused: nothing changed
        CHECK(root->SetLayer(0, true) && b->layer == 0);
        CHECK(s.FindGameObjectsWithName("A").size() == 1 && s.FindGameObjectsWithName("Nope").empty());
        GameObject* a2 = s.CreateGameObject("A");
        CHECK(s.FindGameObjectsWithName("A").size() == 2 && s.FindGameObjectsWithName("A")[1] == a2);
        CHECK(a->CompareTag("") == false);
        a->tag = "Enemy";
        CHECK(a->CompareTag("Enemy") && !a->CompareTag("enemy") && !a->CompareTag("Player"));
    }

    RUN_SUITE("world bounds of a hierarchy");
    {
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        root->transform->localPosition = {10.0f, 0.0f, 0.0f};
        GameObject* box = s.CreateGameObject("Box");
        box->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();     // unit cube, centred
        box->transform->SetParent(root->transform, false);
        box->transform->localScale = {2.0f, 4.0f, 6.0f};
        Bounds bb = Scene::WorldBounds(*box, false);
        CHECK(!bb.IsEmpty());
        CHECK(NearV(bb.center, {10.0f, 0.0f, 0.0f}) && NearV(bb.Size(), {2.0f, 4.0f, 6.0f}));
        CHECK(Scene::WorldBounds(*root, false).IsEmpty());          // the root itself draws nothing
        Bounds all = Scene::WorldBounds(*root);
        CHECK(NearV(all.Size(), {2.0f, 4.0f, 6.0f}));
        // a second renderer further along extends it
        GameObject* spr = s.CreateGameObject("Spr");
        spr->AddComponent<SpriteRenderer>()->size = {2.0f, 2.0f};
        spr->transform->SetParent(root->transform, false);
        spr->transform->localPosition = {10.0f, 0.0f, 0.0f};
        all = Scene::WorldBounds(*root);
        CHECK(NearV(all.Max(), {21.0f, 2.0f, 3.0f}) && NearV(all.Min(), {9.0f, -2.0f, -3.0f}));
        // a turned object: the box of the turned box
        box->transform->localScale = {2.0f, 2.0f, 2.0f};
        box->transform->localRotation = Quat::Euler(0.0f, 45.0f, 0.0f);
        Bounds turned = Scene::WorldBounds(*box, false);
        CHECK_NEAR(turned.Size().x, 2.0f * std::sqrt(2.0f), 1e-2f);
        CHECK_NEAR(turned.Size().y, 2.0f, 1e-3f);
        // inactive objects and subtrees are left out
        spr->active = false;
        CHECK(NearV(Scene::WorldBounds(*root).Max(), turned.Max()));
        box->active = false;
        CHECK(Scene::WorldBounds(*root).IsEmpty());
    }

    TEST_MAIN_RESULT();
}
