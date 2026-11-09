// Transform world math (scene graph 1.3, wave 4): InverseTransformPoint / InverseTransformVector on a
// zero-scale (singular) object, SetPosition and SetParent(keep world) under a chain with non-uniform scale
// and rotation (a shear), negative scale parents, zero-scale parents, Mover and Spinner in World space
// under turned parents, SetPositionAndRotation and SetParent at a sibling index.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

namespace {

bool AllFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool AllFinite(const Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }
float Dist(const Vec3& a, const Vec3& b) { return (a - b).Magnitude(); }
// Two unit quaternions are the same turn when their dot product is +-1.
float QuatGap(const Quat& a, const Quat& b) { return 1.0f - std::fabs(Quat::Dot(a.Normalized(), b.Normalized())); }

} // namespace

int main() {
    RUN_SUITE("InverseTransformPoint on a zero-scale object");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("Flat");
        g->transform->localPosition = {5.0f, 0.0f, 0.0f};
        g->transform->localScale = {0.0f, 1.0f, 1.0f};   // squashed flat along x: the matrix cannot be inverted
        const Vec3 world{7.0f, 3.0f, 2.0f};
        const Vec3 local = g->transform->InverseTransformPoint(world);
        CHECK(AllFinite(local));
        // The collapsed axis has no local coordinate (0); the surviving axes come back exactly.
        CHECK_NEAR(local.x, 0.0f, 1e-5f);
        CHECK_NEAR(local.y, 3.0f, 1e-5f);
        CHECK_NEAR(local.z, 2.0f, 1e-5f);
        // The nearest point of the object to `world` (its flat plane) is what the local point maps back to.
        const Vec3 back = g->transform->TransformPoint(local);
        CHECK_NEAR(back.x, 5.0f, 1e-5f);
        CHECK_NEAR(back.y, 3.0f, 1e-5f);
        CHECK_NEAR(back.z, 2.0f, 1e-5f);
        // it is not the world point (what the identity fallback used to answer)
        CHECK(Dist(local, world) > 1.0f);
        const Vec3 vec = g->transform->InverseTransformVector({4.0f, 6.0f, 8.0f});
        CHECK(AllFinite(vec));
        CHECK_NEAR(vec.x, 0.0f, 1e-5f);
        CHECK_NEAR(vec.y, 6.0f, 1e-5f);
        CHECK_NEAR(vec.z, 8.0f, 1e-5f);
    }
    {   // all three axes collapsed: every point reads as the origin of the object
        Scene s("T");
        GameObject* g = s.CreateGameObject("Dot");
        g->transform->localPosition = {1.0f, 2.0f, 3.0f};
        g->transform->localScale = {0.0f, 0.0f, 0.0f};
        const Vec3 local = g->transform->InverseTransformPoint({9.0f, 9.0f, 9.0f});
        CHECK(AllFinite(local));
        CHECK_NEAR(local.x, 0.0f, 1e-6f);
        CHECK_NEAR(local.y, 0.0f, 1e-6f);
        CHECK_NEAR(local.z, 0.0f, 1e-6f);
    }
    {   // turned and flat: the answer is in the object's own axes
        Scene s("T");
        GameObject* g = s.CreateGameObject("TurnedFlat");
        g->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);       // local x -> world y
        g->transform->localScale = {1.0f, 0.0f, 1.0f};                       // local y (world -x) collapsed
        const Vec3 local = g->transform->InverseTransformPoint({0.0f, 2.0f, 4.0f});
        CHECK(AllFinite(local));
        CHECK_NEAR(local.x, 2.0f, 1e-4f);   // 2 along the object's x (world y)
        CHECK_NEAR(local.y, 0.0f, 1e-4f);   // collapsed
        CHECK_NEAR(local.z, 4.0f, 1e-4f);
    }
    {   // singular because a PARENT is flat
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        p->transform->localScale = {1.0f, 0.0f, 1.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        c->transform->localPosition = {0.0f, 5.0f, 0.0f};
        const Vec3 local = c->transform->InverseTransformPoint({3.0f, 7.0f, 1.0f});
        CHECK(AllFinite(local));
        CHECK_NEAR(local.x, 3.0f, 1e-4f);
        CHECK_NEAR(local.y, 0.0f, 1e-4f);
        CHECK_NEAR(local.z, 1.0f, 1e-4f);
    }
    {   // an invertible object still inverts exactly (the matrix path is unchanged), shear chain included
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        a->transform->localScale = {2.0f, 1.0f, 1.0f};
        GameObject* b = s.CreateGameObject("B");
        b->transform->SetParent(a->transform, false);
        b->transform->localRotation = Quat::Euler(0.0f, 0.0f, 40.0f);
        b->transform->localPosition = {1.0f, 2.0f, 3.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(b->transform, false);
        c->transform->localScale = {1.0f, 3.0f, 0.5f};
        c->transform->localPosition = {-1.0f, 0.5f, 2.0f};
        const Vec3 p{0.7f, -1.2f, 2.5f};
        const Vec3 round = c->transform->InverseTransformPoint(c->transform->TransformPoint(p));
        CHECK(Dist(round, p) < 1e-4f);
        const Vec3 v{1.0f, 2.0f, 3.0f};
        const Vec3 roundV = c->transform->InverseTransformVector(c->transform->TransformVector(v));
        CHECK(Dist(roundV, v) < 1e-4f);
    }

    RUN_SUITE("SetPosition keeps the position under a chain with a shear");
    {
        // grandparent stretched along x, parent turned: the child's world matrix has a shear, so the
        // position back to local has to use the whole parent matrix, not rotation and scale one after another.
        Scene s("T");
        GameObject* gp = s.CreateGameObject("GP");
        gp->transform->localScale = {2.0f, 1.0f, 1.0f};
        GameObject* p = s.CreateGameObject("P");
        p->transform->SetParent(gp->transform, false);
        p->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        c->transform->localPosition = {1.0f, 1.0f, 0.0f};
        const Vec3 world = c->transform->Position();
        c->transform->SetPosition(world);
        CHECK(Dist(c->transform->localPosition, Vec3{1.0f, 1.0f, 0.0f}) < 1e-4f);
        CHECK(Dist(c->transform->Position(), world) < 1e-4f);
        // moving it to another world point lands exactly there
        c->transform->SetPosition({-3.0f, 2.0f, 5.0f});
        CHECK(Dist(c->transform->Position(), Vec3{-3.0f, 2.0f, 5.0f}) < 1e-4f);
    }

    RUN_SUITE("SetParent keeps the world pose under non-uniform, negative and zero scale");
    {   // a parent with a different scale on each axis, turned
        Scene s("T");
        GameObject* np = s.CreateGameObject("NewParent");
        np->transform->localPosition = {3.0f, -2.0f, 1.0f};
        np->transform->localRotation = Quat::Euler(10.0f, 20.0f, 30.0f);
        np->transform->localScale = {2.0f, 0.5f, 4.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->localPosition = {1.0f, 2.0f, 3.0f};
        c->transform->localRotation = Quat::Euler(0.0f, 45.0f, 0.0f);
        c->transform->localScale = {1.0f, 1.0f, 1.0f};
        const Vec3 pos = c->transform->Position();
        const Quat rot = c->transform->Rotation();
        const Vec3 ls = c->transform->LossyScale();
        CHECK(c->transform->SetParent(np->transform, true));
        CHECK(Dist(c->transform->Position(), pos) < 1e-4f);
        CHECK(QuatGap(c->transform->Rotation(), rot) < 1e-5f);
        CHECK(Dist(c->transform->LossyScale(), ls) < 1e-4f);
        // and back to the root
        CHECK(c->transform->SetParent(nullptr, true));
        CHECK(Dist(c->transform->Position(), pos) < 1e-4f);
        CHECK(QuatGap(c->transform->Rotation(), rot) < 1e-5f);
        CHECK(Dist(c->transform->LossyScale(), ls) < 1e-4f);
    }
    {   // a mirrored parent (negative scale), turned
        Scene s("T");
        GameObject* np = s.CreateGameObject("Mirror");
        np->transform->localPosition = {1.0f, 1.0f, 1.0f};
        np->transform->localRotation = Quat::Euler(0.0f, 0.0f, 30.0f);
        np->transform->localScale = {-1.0f, 1.0f, 1.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->localPosition = {4.0f, 1.0f, -2.0f};
        c->transform->localRotation = Quat::Euler(15.0f, 0.0f, 0.0f);
        const Vec3 pos = c->transform->Position();
        const Quat rot = c->transform->Rotation();
        const Vec3 ls = c->transform->LossyScale();
        CHECK(c->transform->SetParent(np->transform, true));
        CHECK(AllFinite(c->transform->localPosition) && AllFinite(c->transform->localRotation) && AllFinite(c->transform->localScale));
        CHECK(Dist(c->transform->Position(), pos) < 1e-4f);
        CHECK(QuatGap(c->transform->Rotation(), rot) < 1e-5f);
        CHECK(Dist(c->transform->LossyScale(), ls) < 1e-4f);
        CHECK(c->transform->localScale.x < 0.0f);   // the mirror is carried on the child so the world scale holds
    }
    {   // a child that is itself mirrored goes under a negative-scale parent and keeps its world scale
        Scene s("T");
        GameObject* np = s.CreateGameObject("Mirror");
        np->transform->localScale = {-2.0f, -2.0f, -2.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->localScale = {-1.0f, 1.0f, 1.0f};
        c->transform->localPosition = {1.0f, 0.0f, 0.0f};
        const Vec3 ls = c->transform->LossyScale();
        const Vec3 pos = c->transform->Position();
        CHECK(c->transform->SetParent(np->transform, true));
        CHECK(Dist(c->transform->LossyScale(), ls) < 1e-5f);
        CHECK(Dist(c->transform->Position(), pos) < 1e-5f);
    }
    {   // a zero-scale parent: nothing turns into NaN or infinity, the flat axis keeps the old local value
        Scene s("T");
        GameObject* np = s.CreateGameObject("Flat");
        np->transform->localScale = {0.0f, 1.0f, 1.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->localPosition = {2.0f, 3.0f, 4.0f};
        c->transform->localScale = {1.0f, 2.0f, 3.0f};
        CHECK(c->transform->SetParent(np->transform, true));
        CHECK(AllFinite(c->transform->localPosition) && AllFinite(c->transform->localScale) && AllFinite(c->transform->localRotation));
        CHECK(AllFinite(c->transform->Position()) && AllFinite(c->transform->LossyScale()));
        CHECK_NEAR(c->transform->localScale.y, 2.0f, 1e-5f);
        CHECK_NEAR(c->transform->localScale.z, 3.0f, 1e-5f);
        // SetPosition under it stays finite as well
        c->transform->SetPosition({9.0f, 9.0f, 9.0f});
        CHECK(AllFinite(c->transform->localPosition));
        CHECK_NEAR(c->transform->localPosition.y, 9.0f, 1e-4f);
    }

    RUN_SUITE("a mover in World space under turned and stretched parents");
    {
        // The world displacement of a World-space mover is velocity * dt whatever the parent chain looks like.
        Scene s("T");
        GameObject* gp = s.CreateGameObject("GP");
        gp->transform->localScale = {3.0f, 1.0f, 2.0f};
        GameObject* p = s.CreateGameObject("P");
        p->transform->SetParent(gp->transform, false);
        p->transform->localRotation = Quat::Euler(0.0f, 0.0f, 60.0f);
        p->transform->localPosition = {1.0f, 0.0f, 0.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        c->transform->localPosition = {1.0f, 2.0f, 3.0f};
        auto* m = c->AddComponent<Mover>();
        m->space = Space::World;
        m->velocity = {1.0f, 2.0f, 0.5f};
        s.Update(0.0f);
        const Vec3 start = c->transform->Position();
        for (int i = 0; i < 4; ++i) s.Update(0.25f);
        const Vec3 moved = c->transform->Position() - start;
        CHECK(Dist(moved, m->velocity) < 1e-3f);
    }
    {   // Self space under a turned parent: along the object's own forward axis in world units
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        p->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);
        p->transform->localScale = {2.0f, 2.0f, 2.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        auto* m = c->AddComponent<Mover>();
        m->space = Space::Self;
        m->velocity = {0.0f, 0.0f, 3.0f};
        s.Update(0.0f);
        const Vec3 start = c->transform->Position();
        s.Update(1.0f);
        const Vec3 moved = c->transform->Position() - start;
        const Vec3 forward = c->transform->Forward();
        CHECK(Dist(moved, forward * 3.0f) < 1e-3f);
    }
    {   // zero and non-finite velocity: nothing moves, nothing turns into NaN
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* m = g->AddComponent<Mover>();
        m->space = Space::World;
        for (const Vec3 v : {Vec3{0, 0, 0}, Vec3{std::numeric_limits<float>::quiet_NaN(), 0, 0},
                             Vec3{0, std::numeric_limits<float>::infinity(), 0}}) {
            m->velocity = v;
            s.Update(0.5f);
            CHECK(AllFinite(g->transform->Position()));
            CHECK(Dist(g->transform->Position(), Vec3{0, 0, 0}) < 1e-6f);
        }
    }

    RUN_SUITE("a spinner in World, Local and Self space under a turned parent");
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        p->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        auto* sp = c->AddComponent<Spinner>();
        sp->angularVelocity = {0.0f, 90.0f, 0.0f};
        sp->space = Space::World;
        s.Update(0.0f);
        const Quat before = c->transform->Rotation();
        s.Update(1.0f);
        // exactly a quarter turn about the WORLD y axis
        const Quat expected = (Quat::AngleAxis(90.0f, Vec3{0, 1, 0}) * before).Normalized();
        CHECK(QuatGap(c->transform->Rotation(), expected) < 1e-4f);
        CHECK(AllFinite(c->transform->localRotation));

        sp->space = Space::Local;   // about the parent's y axis
        const Quat l0 = c->transform->localRotation;
        s.Update(1.0f);
        const Quat lexp = (Quat::AngleAxis(90.0f, Vec3{0, 1, 0}) * l0).Normalized();
        CHECK(QuatGap(c->transform->localRotation, lexp) < 1e-4f);

        sp->space = Space::Self;
        const Quat s0 = c->transform->localRotation;
        s.Update(1.0f);
        const Quat sexp = (s0 * Quat::AngleAxis(90.0f, Vec3{0, 1, 0})).Normalized();
        CHECK(QuatGap(c->transform->localRotation, sexp) < 1e-4f);
    }
    {   // zero and NaN speed leave the rotation alone
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->transform->localRotation = Quat::Euler(10.0f, 20.0f, 30.0f);
        const Quat q = g->transform->localRotation;
        auto* sp = g->AddComponent<Spinner>();
        for (const Vec3 v : {Vec3{0, 0, 0}, Vec3{std::numeric_limits<float>::quiet_NaN(), 1, 1},
                             Vec3{std::numeric_limits<float>::infinity(), 0, 0}}) {
            sp->angularVelocity = v;
            s.Update(0.5f);
            CHECK(AllFinite(g->transform->localRotation));
            CHECK(QuatGap(g->transform->localRotation, q) < 1e-6f);
        }
    }

    RUN_SUITE("SetPositionAndRotation");
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        p->transform->localPosition = {10.0f, 0.0f, 0.0f};
        p->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        p->transform->localScale = {2.0f, 2.0f, 2.0f};
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        const Quat turn = Quat::Euler(0.0f, 45.0f, 0.0f);
        c->transform->SetPositionAndRotation({1.0f, 2.0f, 3.0f}, turn);
        CHECK(Dist(c->transform->Position(), Vec3{1.0f, 2.0f, 3.0f}) < 1e-4f);
        CHECK(QuatGap(c->transform->Rotation(), turn) < 1e-5f);
        c->transform->SetLocalPositionAndRotation({1.0f, 0.0f, 0.0f}, Quat::Identity);
        CHECK(Dist(c->transform->localPosition, Vec3{1.0f, 0.0f, 0.0f}) < 1e-6f);
        CHECK(QuatGap(c->transform->localRotation, Quat::Identity) < 1e-6f);
        // a root: world and local are the same thing
        p->transform->SetPositionAndRotation({0.0f, 5.0f, 0.0f}, Quat::Identity);
        CHECK(Dist(p->transform->localPosition, Vec3{0.0f, 5.0f, 0.0f}) < 1e-6f);
        // a rotation that is not unit length is normalized
        c->transform->SetLocalPositionAndRotation({0, 0, 0}, Quat{0.0f, 0.0f, 2.0f, 2.0f});
        CHECK_NEAR(c->transform->localRotation.Magnitude(), 1.0f, 1e-5f);
    }

    RUN_SUITE("SetParent at a sibling index");
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        std::vector<GameObject*> kids;
        for (int i = 0; i < 4; ++i) {
            GameObject* k = s.CreateGameObject("k" + std::to_string(i));
            k->transform->SetParent(p->transform, false);
            kids.push_back(k);
        }
        GameObject* x = s.CreateGameObject("X");
        x->transform->localPosition = {5.0f, 0.0f, 0.0f};
        CHECK(x->transform->SetParent(p->transform, true, 1));
        CHECK(x->transform->GetSiblingIndex() == 1);
        CHECK(p->transform->GetChild(0) == kids[0]->transform);
        CHECK(p->transform->GetChild(2) == kids[1]->transform);
        CHECK(p->transform->ChildCount() == 5);
        CHECK(Dist(x->transform->Position(), Vec3{5.0f, 0.0f, 0.0f}) < 1e-5f);
        // an index past the end appends; a negative one inserts first
        GameObject* y = s.CreateGameObject("Y");
        CHECK(y->transform->SetParent(p->transform, false, 99));
        CHECK(y->transform->GetSiblingIndex() == 5);
        GameObject* z = s.CreateGameObject("Z");
        CHECK(z->transform->SetParent(p->transform, false, -3));
        CHECK(z->transform->GetSiblingIndex() == 0);
        // moving a child that is already under that parent just reorders it
        CHECK(z->transform->SetParent(p->transform, false, 3));
        CHECK(z->transform->GetSiblingIndex() == 3);
        CHECK(p->transform->ChildCount() == 7);
        // to the root list at an index
        CHECK(y->transform->SetParent(nullptr, true, 0));
        CHECK(y->transform->GetSiblingIndex() == 0);
        CHECK(y->transform->Parent() == nullptr);
        // refused moves change nothing
        const int count = p->transform->ChildCount();
        CHECK(!p->transform->SetParent(x->transform, true, 0));   // would form a loop
        CHECK(p->transform->ChildCount() == count);
    }

    TEST_MAIN_RESULT();
}
