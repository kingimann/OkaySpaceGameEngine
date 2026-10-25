// A contact handler that removes the OTHER object's collider used to leave the
// rest of that dispatch handing the freed collider to later handlers (and to
// Physics2D/3D::CurrentTrigger()). Here a "KillOther" component runs first and
// a "ReadOther" component after it reads the collider it is given — each case
// was a heap-use-after-free under ASan.
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static float g_sink = 0.0f;   // keeps the reads observable

// ---------------------------------------------------------------- 2D
struct KillOther2D : Behaviour {
    bool self = false;          // remove our own collider instead of the other's
    void Kill(GameObject* other) {
        if (self) gameObject->RemoveComponent<Collider2D>();
        else if (other) other->RemoveComponent<Collider2D>();
    }
    void OnCollisionEnter2D(const Collision2D& c) override { Kill(c.gameObject); }
    void OnTriggerEnter2D(Collider2D* o) override { Kill(o ? o->gameObject : nullptr); }
};
struct ReadOther2D : Behaviour {
    int calls = 0;
    void OnCollisionEnter2D(const Collision2D& c) override { ++calls; if (c.collider) g_sink += c.collider->offset.x; }
    void OnTriggerEnter2D(Collider2D* o) override {
        ++calls;
        if (o) g_sink += o->offset.x;
        if (const TriggerContact2D* t = Physics2D::CurrentTrigger()) { if (t->self) g_sink += t->self->offset.x; }
    }
};

static void Run2D(bool trigger, bool killSelf) {
    Scene s("d2");
    GameObject* a = s.CreateGameObject("A");
    GameObject* b = s.CreateGameObject("B");
    b->transform->localPosition = {0.5f, 0, 0};
    for (GameObject* g : {a, b}) {
        g->AddComponent<Rigidbody2D>()->gravityScale = 0.0f;
        g->AddComponent<BoxCollider2D>()->isTrigger = trigger;
    }
    a->AddComponent<KillOther2D>()->self = killSelf;
    auto* r = a->AddComponent<ReadOther2D>();
    s.Start();
    for (int i = 0; i < 3; ++i) s.Update(1.0f / 60.0f);
    CHECK(r->calls == 0);                                    // its collider argument was gone: not called
}

// ---------------------------------------------------------------- 3D
struct KillOther3D : Behaviour {
    bool self = false;
    void Kill(GameObject* other) {
        if (self) gameObject->RemoveComponent<Collider3D>();
        else if (other) other->RemoveComponent<Collider3D>();
    }
    void OnCollisionEnter3D(const Collision3D& c) override { Kill(c.gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { Kill(o ? o->gameObject : nullptr); }
};
struct ReadOther3D : Behaviour {
    int calls = 0;
    void OnCollisionEnter3D(const Collision3D& c) override { ++calls; if (c.collider) g_sink += c.collider->offset.x; }
    void OnTriggerEnter3D(Collider3D* o) override {
        ++calls;
        if (o) g_sink += o->offset.x;
        if (const TriggerContact3D* t = Physics3D::CurrentTrigger()) { if (t->self) g_sink += t->self->offset.x; }
    }
};

static void Run3D(bool trigger, bool killSelf) {
    Scene s("d3");
    GameObject* a = s.CreateGameObject("A");
    GameObject* b = s.CreateGameObject("B");
    b->transform->localPosition = {0.5f, 0, 0};
    for (GameObject* g : {a, b}) {
        g->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
        g->AddComponent<BoxCollider3D>()->isTrigger = trigger;
    }
    a->AddComponent<KillOther3D>()->self = killSelf;
    auto* r = a->AddComponent<ReadOther3D>();
    s.Start();
    for (int i = 0; i < 3; ++i) s.Update(1.0f / 60.0f);
    CHECK(r->calls == 0);
}

int main() {
    RUN_SUITE("physics_dispatch_removal");
    for (bool trig : {false, true}) {
        Run2D(trig, false);
        Run3D(trig, false);
    }
    // Removing our OWN collider inside a trigger callback: CurrentTrigger()->self
    // must not be handed to the next handler.
    Run2D(true, true);
    Run3D(true, true);

    // A plain contact still reaches every handler on both sides.
    {
        Scene s("n");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        b->transform->localPosition = {0.5f, 0, 0};
        ReadOther2D* r[2];
        int k = 0;
        for (GameObject* g : {a, b}) {
            g->AddComponent<Rigidbody2D>()->gravityScale = 0.0f;
            g->AddComponent<BoxCollider2D>();
            r[k++] = g->AddComponent<ReadOther2D>();
        }
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(r[0]->calls == 1 && r[1]->calls == 1);
    }
    std::cout << "(sink " << g_sink << ")\n";
    TEST_MAIN_RESULT();
}
