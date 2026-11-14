// Transform audit: SetParent keeps the world pose (rotated, non-uniformly scaled parents),
// refuses cycles / destroyed parents / parents of another scene, the world-matrix cache
// follows deep reparenting, sibling order APIs, child lookups, space-aware Translate /
// Rotate, RotateAround, TransformVector, LookAt.
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
bool NearQ(const Quat& a, const Quat& b, float eps = 1e-4f) {
    return std::fabs(Quat::Dot(a, b)) > 1.0f - eps;
}

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

struct Spawner : public Behaviour {
    void OnDestroy() override {}
};

} // namespace

int main() {
    RUN_SUITE("SetParent keeps the world pose under rotated, scaled parents");
    {
        Scene s("T");
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localPosition = {3.0f, -2.0f, 5.0f};
        parent->transform->localRotation = Quat::Euler(0.0f, 40.0f, 0.0f);
        parent->transform->localScale = {2.0f, 1.0f, 3.0f};
        GameObject* grand = s.CreateGameObject("GrandParent");
        grand->transform->localPosition = {-1.0f, 4.0f, 0.5f};
        grand->transform->localRotation = Quat::Euler(25.0f, 0.0f, 10.0f);
        grand->transform->localScale = {1.5f, 1.5f, 1.5f};
        parent->transform->SetParent(grand->transform, false);

        GameObject* child = s.CreateGameObject("Child");
        child->transform->localPosition = {7.0f, 1.0f, -3.0f};
        child->transform->localRotation = Quat::Euler(20.0f, 0.0f, 0.0f);
        child->transform->localScale = {1.0f, 2.0f, 1.0f};
        const Vec3 p = child->transform->Position();
        const Quat r = child->transform->Rotation();
        const Vec3 sc = child->transform->LossyScale();

        CHECK(child->transform->SetParent(parent->transform, true));
        CHECK(NearV(child->transform->Position(), p));
        CHECK(NearQ(child->transform->Rotation(), r));
        CHECK(NearV(child->transform->LossyScale(), sc));

        // and back out again
        CHECK(child->transform->SetParent(nullptr, true));
        CHECK(NearV(child->transform->Position(), p));
        CHECK(NearQ(child->transform->Rotation(), r));
        CHECK(NearV(child->transform->LossyScale(), sc));
        CHECK(child->transform->Parent() == nullptr);

        // keeping the LOCAL pose instead
        child->transform->localPosition = {1.0f, 2.0f, 3.0f};
        CHECK(child->transform->SetParent(parent->transform, false));
        CHECK(NearV(child->transform->localPosition, {1.0f, 2.0f, 3.0f}, 1e-6f));
    }

    RUN_SUITE("SetParent refusals");
    {
        Scene s("T"), other("O");
        GameObject* a = s.CreateGameObject("a");
        GameObject* b = s.CreateGameObject("b");
        GameObject* c = s.CreateGameObject("c");
        b->transform->SetParent(a->transform);
        c->transform->SetParent(b->transform);
        CHECK(!a->transform->SetParent(a->transform));                  // itself
        CHECK(!a->transform->SetParent(c->transform));                  // a descendant
        CHECK(a->transform->SetParent(nullptr));                        // already a root: fine
        CHECK(b->transform->SetParent(nullptr, true) && b->transform->Parent() == nullptr);

        // a parent that was destroyed (only a frozen shell is left) is refused
        GameObject* dead = s.CreateGameObject("dead");
        GameObject* orphan = s.CreateGameObject("orphan");
        s.Destroy(dead);
        s.FlushDestroyed();
        CHECK(dead->IsDestroyed());
        CHECK(!orphan->transform->SetParent(dead->transform));
        CHECK(orphan->transform->Parent() == nullptr);
        CHECK(dead->transform->ChildCount() == 0);

        // a parent that lives in another scene would dangle when either scene goes
        GameObject* foreign = other.CreateGameObject("foreign");
        GameObject* mine = s.CreateGameObject("mine");
        CHECK(!mine->transform->SetParent(foreign->transform));
        CHECK(mine->transform->Parent() == nullptr);
        CHECK(foreign->transform->ChildCount() == 0);
    }

    RUN_SUITE("world matrix cache and WorldVersion");
    {
        Scene s("T");
        std::vector<GameObject*> g;
        for (int i = 0; i < 6; ++i) {
            g.push_back(s.CreateGameObject("g" + std::to_string(i)));
            g.back()->transform->localPosition = {float(i), 0.5f * float(i), -1.0f};
            g.back()->transform->localRotation = Quat::Euler(10.0f * float(i), 15.0f, 5.0f * float(i));
            g.back()->transform->localScale = {1.0f + 0.1f * float(i), 1.0f, 0.5f + 0.2f * float(i)};
            if (i > 0) g.back()->transform->SetParent(g[static_cast<std::size_t>(i - 1)]->transform, false);
        }
        auto manual = [&](std::size_t upTo) {
            Mat4 m = Mat4::Identity();
            for (std::size_t i = 0; i <= upTo; ++i)
                m = m * Mat4::TRS(g[i]->transform->localPosition, g[i]->transform->localRotation, g[i]->transform->localScale);
            return m;
        };
        auto sameMat = [](const Mat4& a, const Mat4& b) {
            for (int i = 0; i < 16; ++i) if (std::fabs(a.m[i] - b.m[i]) > 1e-3f) return false;
            return true;
        };
        CHECK(sameMat(g[5]->transform->LocalToWorldMatrix(), manual(5)));

        // move node 3 (with its subtree) under node 1: caches below it follow
        g[3]->transform->SetParent(g[1]->transform, /*worldPositionStays=*/false);
        Mat4 expect = Mat4::TRS(g[0]->transform->localPosition, g[0]->transform->localRotation, g[0]->transform->localScale) *
                      Mat4::TRS(g[1]->transform->localPosition, g[1]->transform->localRotation, g[1]->transform->localScale);
        for (int i : {3, 4, 5}) {
            const Transform& t = *g[static_cast<std::size_t>(i)]->transform;
            expect = expect * Mat4::TRS(t.localPosition, t.localRotation, t.localScale);
        }
        CHECK(sameMat(g[5]->transform->LocalToWorldMatrix(), expect));

        // WorldVersion moves with the pose and only then
        const unsigned long long v0 = g[5]->transform->WorldVersion();
        CHECK(g[5]->transform->WorldVersion() == v0);
        g[2]->transform->localPosition.x += 1.0f;                       // not an ancestor of 5 any more
        CHECK(g[5]->transform->WorldVersion() == v0);
        g[0]->transform->localPosition.x += 1.0f;                       // the root of it
        CHECK(g[5]->transform->WorldVersion() != v0);
    }

    RUN_SUITE("sibling order: children");
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        GameObject* d = s.CreateGameObject("D");
        for (GameObject* g : {a, b, c, d}) g->transform->SetParent(p->transform);
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"A", "B", "C", "D"}));

        c->transform->SetAsFirstSibling();
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"C", "A", "B", "D"}));
        c->transform->SetAsLastSibling();
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"A", "B", "D", "C"}));
        CHECK(d->transform->PlaceBefore(a->transform));
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"D", "A", "B", "C"}));
        CHECK(d->transform->PlaceAfter(b->transform));
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"A", "B", "D", "C"}));
        CHECK(!a->transform->PlaceBefore(a->transform));                // itself
        CHECK(!a->transform->PlaceAfter(nullptr));
        CHECK(a->transform->GetSiblingIndex() == 0 && c->transform->GetSiblingIndex() == 3);

        a->transform->SetSiblingIndex(-5);                              // clamped
        CHECK(a->transform->GetSiblingIndex() == 0);
        a->transform->SetSiblingIndex(99);
        CHECK(a->transform->GetSiblingIndex() == 3);
        CHECK((ChildNames(p->transform) == std::vector<std::string>{"B", "D", "C", "A"}));

        // placing next to a non-sibling reparents, keeping the world pose
        GameObject* other = s.CreateGameObject("Other");
        other->transform->localPosition = {9.0f, 0.0f, 0.0f};
        p->transform->localPosition = {1.0f, 1.0f, 1.0f};
        other->transform->SetParent(nullptr);
        const Vec3 world = other->transform->Position();
        CHECK(other->transform->PlaceAfter(b->transform));
        CHECK(other->transform->Parent() == p->transform);
        CHECK(NearV(other->transform->Position(), world));
        CHECK(other->transform->GetSiblingIndex() == 1);
        // a transform cannot be placed next to one of its own descendants
        CHECK(!p->transform->PlaceBefore(b->transform));
    }

    RUN_SUITE("sibling order: roots bump the structure version");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        CHECK((RootNames(s) == std::vector<std::string>{"A", "B", "C"}));
        std::uint64_t v = s.StructureVersion();
        c->transform->SetAsFirstSibling();
        CHECK((RootNames(s) == std::vector<std::string>{"C", "A", "B"}));
        CHECK(s.StructureVersion() != v);
        v = s.StructureVersion();
        a->transform->SetAsLastSibling();
        CHECK((RootNames(s) == std::vector<std::string>{"C", "B", "A"}));
        CHECK(s.StructureVersion() != v);
        v = s.StructureVersion();
        b->transform->SetSiblingIndex(2);
        CHECK((RootNames(s) == std::vector<std::string>{"C", "A", "B"}));
        CHECK(s.StructureVersion() != v);
        v = s.StructureVersion();
        s.ReorderSibling(c, b, /*after=*/true);
        CHECK((RootNames(s) == std::vector<std::string>{"A", "B", "C"}));
        CHECK(s.StructureVersion() != v);
        // moving nothing changes nothing
        v = s.StructureVersion();
        a->transform->SetAsFirstSibling();
        CHECK(s.StructureVersion() == v);
    }

    RUN_SUITE("child lookups");
    {
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        GameObject* arm = s.CreateGameObject("Arm");
        GameObject* hand = s.CreateGameObject("Hand");
        GameObject* finger = s.CreateGameObject("Finger");
        GameObject* leg = s.CreateGameObject("Leg");
        arm->transform->SetParent(root->transform);
        hand->transform->SetParent(arm->transform);
        finger->transform->SetParent(hand->transform);
        leg->transform->SetParent(root->transform);
        Transform* rt = root->transform;
        CHECK(rt->GetChild(0) == arm->transform && rt->GetChild(1) == leg->transform);
        CHECK(rt->GetChild(-1) == nullptr && rt->GetChild(2) == nullptr);
        CHECK(rt->Find("Arm") == arm->transform);
        CHECK(rt->Find("Arm/Hand/Finger") == finger->transform);
        CHECK(rt->Find("Arm/Nope") == nullptr);
        CHECK(rt->Find("") == nullptr);
        CHECK(rt->Find("Hand") == nullptr);                             // direct children only
        CHECK(rt->FindDeep("Finger") == finger->transform);
        CHECK(rt->FindDeep("Root") == nullptr);                         // not itself
        CHECK(rt->FindDeep("Missing") == nullptr);
        CHECK(finger->transform->IsChildOf(rt));
        CHECK(finger->transform->IsChildOf(finger->transform));         // identical counts
        CHECK(!leg->transform->IsChildOf(arm->transform));
        CHECK(!finger->transform->IsChildOf(nullptr));
        CHECK(finger->transform->Root() == rt);
        CHECK(rt->Root() == rt);
        CHECK(finger->transform->Depth() == 3 && rt->Depth() == 0);
        CHECK(finger->Path() == "Root/Arm/Hand/Finger");
        CHECK(root->Path() == "Root");
        CHECK(s.FindByPath(finger->Path()) == finger);

        arm->transform->localPosition = {2.0f, 0.0f, 0.0f};
        root->transform->localPosition = {0.0f, 10.0f, 0.0f};
        const Vec3 hw = hand->transform->Position();
        rt->DetachChildren();
        CHECK(rt->ChildCount() == 0);
        CHECK(arm->transform->Parent() == nullptr && leg->transform->Parent() == nullptr);
        CHECK(hand->transform->Parent() == arm->transform);             // only the direct children moved
        CHECK(NearV(hand->transform->Position(), hw));
    }

    RUN_SUITE("Translate and Rotate in each space");
    {
        Scene s("T");
        GameObject* parent = s.CreateGameObject("P");
        parent->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);
        parent->transform->localScale = {2.0f, 2.0f, 2.0f};
        parent->transform->localPosition = {10.0f, 0.0f, 0.0f};
        GameObject* child = s.CreateGameObject("C");
        child->transform->SetParent(parent->transform, false);
        child->transform->localRotation = Quat::Euler(0.0f, 0.0f, 30.0f);

        // Local (the parent's frame): what Translate(delta) always did
        child->transform->localPosition = Vec3::Zero;
        child->transform->Translate({1.0f, 0.0f, 0.0f});
        CHECK(NearV(child->transform->localPosition, {1.0f, 0.0f, 0.0f}));
        child->transform->localPosition = Vec3::Zero;
        child->transform->Translate({1.0f, 0.0f, 0.0f}, Space::Local);
        CHECK(NearV(child->transform->localPosition, {1.0f, 0.0f, 0.0f}));

        // World: the world position moves by exactly that vector, parent scale and turn included
        child->transform->localPosition = {0.5f, 0.5f, 0.5f};
        Vec3 w0 = child->transform->Position();
        child->transform->Translate({0.0f, 0.0f, 1.0f}, Space::World);
        CHECK(NearV(child->transform->Position(), w0 + Vec3{0.0f, 0.0f, 1.0f}));

        // Self: along the object's own axes
        const Vec3 w1 = child->transform->Position();
        const Vec3 fwdBefore = child->transform->Forward();
        child->transform->Translate(Vec3::Forward, Space::Self);
        const Vec3 moved = child->transform->Position() - w1;
        // the child moved one world unit along its own Forward, whatever the parent's scale
        CHECK(NearV(moved, fwdBefore));

        // Rotate: default and Self turn around the object's own axes, Local around the parent's, World around the world's
        child->transform->localRotation = Quat::Identity;
        child->transform->Rotate({0.0f, 45.0f, 0.0f});
        CHECK(NearQ(child->transform->localRotation, Quat::Euler(0.0f, 45.0f, 0.0f)));
        child->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        child->transform->Rotate({0.0f, 45.0f, 0.0f}, Space::Self);
        CHECK(NearQ(child->transform->localRotation, Quat::Euler(0.0f, 0.0f, 90.0f) * Quat::Euler(0.0f, 45.0f, 0.0f)));
        child->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        child->transform->Rotate({0.0f, 45.0f, 0.0f}, Space::Local);
        CHECK(NearQ(child->transform->localRotation, Quat::Euler(0.0f, 45.0f, 0.0f) * Quat::Euler(0.0f, 0.0f, 90.0f)));
        const Quat wr = child->transform->Rotation();
        child->transform->Rotate({0.0f, 45.0f, 0.0f}, Space::World);
        CHECK(NearQ(child->transform->Rotation(), Quat::Euler(0.0f, 45.0f, 0.0f) * wr));
        // axis + angle form
        child->transform->localRotation = Quat::Identity;
        child->transform->Rotate(Vec3::Up, 90.0f, Space::Local);
        CHECK(NearQ(child->transform->localRotation, Quat::AngleAxis(90.0f, Vec3::Up)));
    }

    RUN_SUITE("RotateAround, TransformVector, LookAt");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->transform->localPosition = {1.0f, 0.0f, 0.0f};
        g->transform->RotateAround({0.0f, 0.0f, 0.0f}, Vec3::Up, 90.0f);
        CHECK(NearV(g->transform->Position(), Quat::AngleAxis(90.0f, Vec3::Up) * Vec3{1.0f, 0.0f, 0.0f}));
        CHECK(NearQ(g->transform->Rotation(), Quat::AngleAxis(90.0f, Vec3::Up)));
        // around a point that is not the origin, a point at that centre stays put
        g->transform->localPosition = {4.0f, 4.0f, 4.0f};
        g->transform->RotateAround({4.0f, 4.0f, 4.0f}, Vec3::Right, 33.0f);
        CHECK(NearV(g->transform->Position(), {4.0f, 4.0f, 4.0f}));

        GameObject* sc = s.CreateGameObject("Scaled");
        sc->transform->localScale = {2.0f, 3.0f, 4.0f};
        sc->transform->localPosition = {5.0f, 0.0f, 0.0f};
        CHECK(NearV(sc->transform->TransformVector({1.0f, 1.0f, 1.0f}), {2.0f, 3.0f, 4.0f}));   // scale, no translation
        CHECK(NearV(sc->transform->InverseTransformVector({2.0f, 3.0f, 4.0f}), {1.0f, 1.0f, 1.0f}));
        CHECK(NearV(sc->transform->TransformDirection({1.0f, 0.0f, 0.0f}), {1.0f, 0.0f, 0.0f}));  // direction ignores scale

        // LookAt: a root, and a child of a turned, stretched parent
        GameObject* look = s.CreateGameObject("Look");
        look->transform->localPosition = {1.0f, 2.0f, 3.0f};
        look->transform->LookAt({1.0f, 2.0f, 13.0f});
        CHECK(NearV(look->transform->Forward(), {0.0f, 0.0f, 1.0f}));
        look->transform->LookAt(look->transform->Position());           // at itself: nothing happens
        CHECK(NearV(look->transform->Forward(), {0.0f, 0.0f, 1.0f}));
        GameObject* par = s.CreateGameObject("Par");
        par->transform->localRotation = Quat::Euler(10.0f, 70.0f, 20.0f);
        par->transform->localScale = {2.0f, 0.5f, 3.0f};
        par->transform->localPosition = {-3.0f, 1.0f, 2.0f};
        GameObject* kid = s.CreateGameObject("Kid");
        kid->transform->SetParent(par->transform, false);
        kid->transform->localPosition = {1.0f, 1.0f, 1.0f};
        const Vec3 target{10.0f, -4.0f, 7.0f};
        kid->transform->LookAt(target);
        Vec3 want = target - kid->transform->Position();
        want = want * (1.0f / want.Magnitude());
        CHECK(NearV(kid->transform->Forward(), want, 1e-3f));
        // looking straight up with the default up hint stays finite and points up
        look->transform->LookAt(look->transform->Position() + Vec3{0.0f, 5.0f, 0.0f});
        CHECK(std::isfinite(look->transform->Rotation().x) && std::isfinite(look->transform->Rotation().w));
        CHECK(NearV(look->transform->Forward(), {0.0f, 1.0f, 0.0f}, 1e-3f));
    }

    RUN_SUITE("thousands of children");
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        const int n = 20000;
        for (int i = 0; i < n; ++i) {
            GameObject* c = s.CreateGameObject("c" + std::to_string(i));
            c->transform->SetParent(p->transform, false);
            c->transform->localPosition = {float(i), 0.0f, 0.0f};
        }
        CHECK(p->transform->ChildCount() == n);
        Transform* last = p->transform->GetChild(n - 1);
        CHECK(last && last->GetSiblingIndex() == n - 1);
        last->SetAsFirstSibling();
        CHECK(p->transform->GetChild(0) == last && last->GetSiblingIndex() == 0);
        CHECK(p->transform->Find("c12345") != nullptr);
        p->transform->localPosition = {0.0f, 1.0f, 0.0f};
        CHECK_NEAR(p->transform->Find("c500")->Position().y, 1.0f, 1e-4f);
        p->transform->DetachChildren();
        CHECK(p->transform->ChildCount() == 0);
        CHECK(static_cast<int>(s.Roots().size()) == n + 1);
        CHECK_NEAR(s.Find("c500")->transform->Position().y, 1.0f, 1e-4f);   // world pose kept
        CHECK_NEAR(s.Find("c500")->transform->Position().x, 500.0f, 1e-3f);
    }

    TEST_MAIN_RESULT();
}
