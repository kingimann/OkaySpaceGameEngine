// ObjectPool, fifth pass: a reused object is put back to what a fresh copy of the template has (rigidbody state,
// elapsed Lifetime, the pose, tag, layer, `active` and enabled flags of the whole hierarchy), a delayed Destroy
// does not outlive the use it was asked for, objects destroyed while pooled and a deferred Clear leave the pool
// right, the limit does not count dead objects, and Get / Release do not look at every object the pool holds.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct Clearer : public Behaviour {   // empties the scene from inside the frame (the call is deferred)
    ObjectPool* pool = nullptr;
    GameObject* got = nullptr;
    bool done = false;
    void Update(float) override {
        if (done) return;
        done = true;
        gameObject->scene()->Clear();
        if (pool) got = pool->Get();   // made after the call: goes with the old scene
    }
};

} // namespace

int main() {
    RUN_SUITE("scene counters: UpdateCount, IsDestroyPending, CancelDelayedDestroy");
    {
        Scene s("T");
        const std::uint64_t u = s.UpdateCount();
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(s.UpdateCount() == u + 2);
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        CHECK(!s.IsDestroyPending(a) && !s.IsDestroyPending(nullptr));
        s.Destroy(a);
        CHECK(s.IsDestroyPending(a));                                     // queued for the end of the frame
        s.Destroy(b, 5.0f);
        CHECK(s.IsDestroyPending(b));                                     // waiting
        CHECK(!s.CancelDelayedDestroy(c) && !s.CancelDelayedDestroy(nullptr));
        CHECK(!s.CancelDelayedDestroy(a));                                // a queued one is not a delayed one
        CHECK(s.CancelDelayedDestroy(b) && !s.IsDestroyPending(b));
        CHECK(!s.CancelDelayedDestroy(b));
        s.Update(10.0f);
        CHECK(a->IsDestroyed() && !b->IsDestroyed());                     // b was taken back: nothing destroys it
        s.Destroy(b, 1.0f);                                               // and a new request works again
        s.Update(2.0f);
        CHECK(b->IsDestroyed());
    }

    RUN_SUITE("a reused body starts at rest");
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("Bullet");
        auto* trb = tpl->AddComponent<Rigidbody2D>();
        trb->gravityScale = 0.0f;
        auto* trb3 = tpl->AddComponent<Rigidbody3D>();
        trb3->gravityScale = 0.0f;
        tpl->AddComponent<Lifetime>()->seconds = 5.0f;
        ObjectPool pool(sc);
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl);
        sc.Update(0.016f);

        GameObject* a = pool.Get();
        auto* rb2 = a->GetComponent<Rigidbody2D>();
        auto* rb3 = a->GetComponent<Rigidbody3D>();
        auto* life = a->GetComponent<Lifetime>();
        CHECK(rb2 && rb3 && life);
        sc.Update(0.016f);
        rb2->velocity = {3.0f, 4.0f}; rb2->angularVelocity = 7.0f; rb2->AddForce({100.0f, 0.0f}); rb2->AddTorque(50.0f);
        rb2->sleeping = true; rb2->poseHistory.valid = true;
        rb3->velocity = {1.0f, 2.0f, 3.0f}; rb3->angularVelocity = {1.0f, 1.0f, 1.0f}; rb3->AddForce({0.0f, 100.0f, 0.0f});
        rb3->sleeping = true; rb3->groundedOnTerrain = true; rb3->hasPrevPos = true; rb3->poseHistory.valid = true;
        life->SetElapsed(4.5f);
        CHECK(pool.Release(a));
        sc.Update(0.016f);
        GameObject* b = pool.Get();
        CHECK(b == a);                                                    // the same object, used again
        CHECK(rb2->velocity.x == 0.0f && rb2->velocity.y == 0.0f && rb2->angularVelocity == 0.0f);
        CHECK(!rb2->sleeping && !rb2->poseHistory.valid);
        CHECK(rb3->velocity.x == 0.0f && rb3->velocity.y == 0.0f && rb3->velocity.z == 0.0f);
        CHECK(rb3->angularVelocity.x == 0.0f && !rb3->sleeping && !rb3->groundedOnTerrain && !rb3->hasPrevPos && !rb3->poseHistory.valid);
        CHECK(life->Elapsed() == 0.0f);                                   // lives its full five seconds again
        // The forces that were waiting are gone too: a step moves nothing (gravity is off on both bodies).
        const Vec3 at = b->transform->Position();
        sc.Update(0.1f);
        CHECK(b->transform->Position().x == at.x && b->transform->Position().y == at.y && b->transform->Position().z == at.z);
        CHECK(!b->IsDestroyed());
        // A new object needs none of this (and the hook is not called for it).
        GameObject* fresh = pool.Get();
        CHECK(fresh != b && fresh->GetComponent<Rigidbody2D>()->velocity.x == 0.0f);
    }

    RUN_SUITE("a delayed Destroy ends with the use it was asked for");
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("Spark");
        tpl->AddComponent<Mover>();
        ObjectPool pool(sc);
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl);
        sc.Update(0.016f);
        GameObject* a = pool.Get();
        sc.Update(0.016f);
        sc.Destroy(a, 1.0f);                                              // "clean up after a second"
        sc.Update(0.3f);
        CHECK(pool.Release(a));                                           // hit something after 0.3 s
        CHECK(!sc.IsDestroyPending(a));                                   // the request went with the use
        sc.Update(0.016f);
        GameObject* b = pool.Get();
        CHECK(b == a);
        sc.Update(5.0f);                                                  // far past the old deadline
        CHECK(!a->IsDestroyed() && a->active);
        // asking for a delayed destroy of a pooled object that is idle: it is not handed out afterwards
        CHECK(pool.Release(b));
        sc.Destroy(b, 10.0f);
        sc.Update(0.016f);
        GameObject* c = pool.Get();
        CHECK(c != b);                                                    // b is on its way out
        sc.Update(11.0f);
        CHECK(b->IsDestroyed() && !c->IsDestroyed());
        CHECK(pool.IdleCount() == 0);
    }

    RUN_SUITE("a reused hierarchy is put back, object by object");
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("Enemy");
        tpl->tag = "Enemy";
        tpl->layer = 2;
        tpl->transform->localScale = {2.0f, 2.0f, 2.0f};
        GameObject* tarm = sc.CreateGameObject("Arm");
        tarm->transform->SetParent(tpl->transform, false);
        tarm->transform->localPosition = {0.0f, 1.0f, 0.0f};
        tarm->tag = "Limb";
        tarm->layer = 3;
        tarm->AddComponent<Mover>();
        GameObject* thand = sc.CreateGameObject("Hand");
        thand->transform->SetParent(tarm->transform, false);
        thand->transform->localRotation = Quat::Euler(0.0f, 30.0f, 0.0f);
        ObjectPool pool(sc);
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl);
        sc.Update(0.016f);

        GameObject* a = pool.Get();
        sc.Update(0.016f);
        GameObject* arm = a->transform->Find("Arm")->gameObject;
        GameObject* hand = a->transform->Find("Arm/Hand")->gameObject;
        CHECK(arm && hand && arm->GetComponent<Mover>());
        // a life of changes
        a->tag = "Dead"; a->layer = 9; a->transform->localPosition = {5.0f, 5.0f, 5.0f}; a->transform->localScale = {1.0f, 1.0f, 1.0f};
        arm->transform->localPosition = {9.0f, 9.0f, 9.0f}; arm->active = false; arm->tag = "Gone"; arm->layer = 12;
        arm->GetComponent<Mover>()->enabled = false;
        hand->transform->localRotation = Quat::Euler(0.0f, 0.0f, 80.0f);
        hand->active = false;
        pool.Release(a);
        sc.Update(0.016f);
        GameObject* b = pool.Get();
        CHECK(b == a);
        CHECK(b->tag == "Enemy" && b->layer == 2 && b->active);
        CHECK(b->transform->localScale.x == 2.0f && b->transform->localPosition.x == 0.0f);
        CHECK(arm->active && arm->tag == "Limb" && arm->layer == 3);
        CHECK(arm->transform->localPosition.x == 0.0f && arm->transform->localPosition.y == 1.0f && arm->transform->localPosition.z == 0.0f);
        CHECK(arm->GetComponent<Mover>()->enabled);                       // switched back on
        CHECK(hand->active);
        CHECK(std::fabs(hand->transform->localRotation.y - Quat::Euler(0.0f, 30.0f, 0.0f).y) < 1e-6f);

        // A hierarchy that no longer has the template's shape: only the root can be matched to the template.
        GameObject* extra = sc.CreateGameObject("Extra");
        extra->transform->SetParent(b->transform, false);
        extra->transform->localPosition = {7.0f, 7.0f, 7.0f};
        b->tag = "Dead"; b->transform->localPosition = {3.0f, 3.0f, 3.0f};
        arm->transform->localPosition = {4.0f, 4.0f, 4.0f};
        pool.Release(b);
        sc.Update(0.016f);
        GameObject* c = pool.Get();
        CHECK(c == b && c->tag == "Enemy" && c->transform->localPosition.x == 0.0f);
        CHECK(arm->transform->localPosition.x == 4.0f);                    // left alone: it cannot be told which is which
        CHECK(extra->transform->localPosition.x == 7.0f);
    }

    RUN_SUITE("objects destroyed while pooled");
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("Spark");
        tpl->AddComponent<Mover>();
        ObjectPool pool(sc);
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl);
        sc.Update(0.016f);
        GameObject* a = pool.Get();
        sc.Update(0.016f);
        CHECK(pool.Release(a));
        sc.Destroy(a);                                                    // queued for the end of the frame: still listed
        GameObject* b = pool.Get();
        CHECK(b != nullptr && b != a);                                    // not handed out: it is going
        sc.Update(0.016f);
        CHECK(a->IsDestroyed());
        CHECK(pool.IdleCount() == 0 && pool.ActiveCount() == 1);          // b only
        CHECK(!pool.Release(a));                                          // a shell is not ours to take back
        // Released, then destroyed after the frame boundary, behind the pool's back.
        CHECK(pool.Release(b));
        sc.Update(0.016f);
        sc.Destroy(b);
        sc.Update(0.016f);
        CHECK(pool.IdleCount() == 0 && pool.ActiveCount() == 0);
        GameObject* c = pool.Get();
        CHECK(c != nullptr && !c->IsDestroyed() && c != a && c != b);
        // Released after it was queued for destruction: the pool lets go of it.
        sc.Destroy(c);
        CHECK(pool.Release(c));
        sc.Update(0.016f);
        CHECK(c->IsDestroyed() && pool.IdleCount() == 0 && pool.ActiveCount() == 0);
        // A destroyed template is ignored; the source stays as it was.
        GameObject* t2 = sc.CreateGameObject("T2");
        sc.Destroy(t2);
        sc.Update(0.016f);
        ObjectPool other(sc);
        other.SetTemplate(*t2);
        CHECK(!other.HasSource() && other.Get() == nullptr);
        pool.SetTemplate(*t2);
        CHECK(pool.HasSource() && pool.Get() != nullptr && pool.Get()->name == "Spark");
    }

    RUN_SUITE("a Clear that waits for the end of the frame");
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("Bullet");
        tpl->AddComponent<Mover>();
        ObjectPool pool(sc);
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl);
        sc.Update(0.016f);
        pool.maxSize = 8;
        pool.Prewarm(4);
        std::vector<GameObject*> held;
        for (int i = 0; i < 3; ++i) held.push_back(pool.Get());
        sc.Update(0.016f);
        CHECK(pool.IdleCount() == 1 && pool.ActiveCount() == 3);
        GameObject* clearerObject = sc.CreateGameObject("Clearer");
        auto* cl = clearerObject->AddComponent<Clearer>();
        cl->pool = &pool;
        sc.Update(0.016f);                                                // Clear() inside Update, then Get(): both go at the end of the frame
        CHECK(sc.Objects().empty() && !sc.HasPendingClear());
        CHECK(pool.IdleCount() == 0 && pool.ActiveCount() == 0);
        GameObject* fresh = pool.Get();
        CHECK(fresh != nullptr && fresh->scene() == &sc && sc.Objects().size() == 1);
        CHECK(pool.Release(fresh));
        sc.Update(0.016f);
        GameObject* again = pool.Get();
        CHECK(again == fresh);                                            // the pool carries on in the emptied scene
        // An immediate Clear outside a frame
        sc.Clear();
        CHECK(pool.IdleCount() == 0 && pool.ActiveCount() == 0 && pool.Get() != nullptr);
    }

    RUN_SUITE("capacity");
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("B");
        tpl->AddComponent<Mover>();
        ObjectPool pool(sc);
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl);
        sc.Update(0.016f);
        pool.maxSize = 3;
        std::vector<GameObject*> held;
        for (int i = 0; i < 10; ++i) held.push_back(pool.Get());
        sc.Update(0.016f);
        for (GameObject* g : held) pool.Release(g);                       // all in one frame
        CHECK(pool.IdleCount() == 3);                                     // the others were destroyed
        sc.Update(0.016f);
        CHECK(sc.Objects().size() == 3 && pool.IdleCount() == 3 && pool.ActiveCount() == 0);
        // The three idle ones are destroyed behind the pool's back: they do not use up the limit.
        std::vector<GameObject*> idle;
        for (const auto& o : sc.Objects()) idle.push_back(o.get());
        for (GameObject* g : idle) sc.Destroy(g);
        sc.Update(0.016f);
        GameObject* x = pool.Get();
        CHECK(pool.Release(x));
        sc.Update(0.016f);
        CHECK(!x->IsDestroyed() && pool.IdleCount() == 1);
        // A limit of zero keeps nothing.
        pool.maxSize = 0;
        GameObject* y = pool.Get();
        CHECK(pool.Release(y));
        sc.Update(0.016f);
        CHECK(y->IsDestroyed() || pool.IdleCount() == 1);                  // the idle x was reused first: y is a new object that is not kept
        pool.Prewarm(5);
        CHECK(pool.IdleCount() <= 1);
        pool.maxSize = -4;
        pool.Prewarm(5);
        CHECK(pool.IdleCount() <= 1);
    }

    RUN_SUITE("Get and Release do not look at every object the pool holds");
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("B");
        tpl->AddComponent<Mover>();
        ObjectPool pool(sc);
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl);
        sc.Update(0.016f);
        const int n = 2000;
        pool.maxSize = n + 10;
        pool.Prewarm(n);
        sc.Update(0.016f);
        CHECK(pool.CreatedCount() == n);
        const std::uint64_t before = pool.LookupCount();
        std::vector<GameObject*> held;
        for (int i = 0; i < n; ++i) held.push_back(pool.Get());           // every one reused
        CHECK(pool.ReuseCount() == n && pool.CreatedCount() == n);
        for (GameObject* g : held) pool.Release(g);                        // all in one frame
        sc.Update(0.016f);
        held.clear();
        for (int i = 0; i < n; ++i) held.push_back(pool.Get());           // all again, next frame
        CHECK(pool.ReuseCount() == 2 * n && pool.CreatedCount() == n);
        const std::uint64_t used = pool.LookupCount() - before;
        CHECK(used <= 6u * static_cast<std::uint64_t>(n));                // a lookup or two for each call, not n for each
        CHECK(pool.ActiveCount() == n && pool.IdleCount() == 0);
        // the counts, asked for twice with nothing changed in between, look once
        const std::uint64_t a = pool.LookupCount();
        (void)pool.IdleCount();
        const std::uint64_t b = pool.LookupCount();
        (void)pool.ActiveCount();
        CHECK(pool.LookupCount() == b && b - a <= static_cast<std::uint64_t>(n) + 10);
    }

    TEST_MAIN_RESULT();
}
