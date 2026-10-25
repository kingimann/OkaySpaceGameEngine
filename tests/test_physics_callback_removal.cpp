// Removing a collider / rigidbody (or a sibling component) from inside an
// OnCollisionEnter / OnTriggerEnter handler used to leave the physics step
// holding freed pointers (the pair loop, the other side's dispatch, the sleep
// pass and the saved contact set). Each case here was a use-after-free.
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

enum class Act { RemoveSelf, RemoveOther, RemoveSibling };

// ---------------------------------------------------------------- 2D
class Count2D : public Behaviour {
public:
    int hits = 0;
    void OnCollisionEnter2D(const Collision2D&) override { ++hits; }
    void OnCollisionStay2D(const Collision2D&) override { ++hits; }
    void OnTriggerEnter2D(Collider2D*) override { ++hits; }
    void OnTriggerStay2D(Collider2D*) override { ++hits; }
};

class Remover2D : public Behaviour {
public:
    Act act = Act::RemoveSelf;
    int fired = 0;
    void Do(GameObject* other) {
        ++fired;
        if (act == Act::RemoveSelf) {
            gameObject->RemoveComponent<Collider2D>();
            gameObject->RemoveComponent<Rigidbody2D>();
        } else if (act == Act::RemoveOther && other) {
            other->RemoveComponent<Collider2D>();
            other->RemoveComponent<Rigidbody2D>();
        } else if (act == Act::RemoveSibling) {
            gameObject->RemoveComponent<Count2D>();
        }
    }
    void OnCollisionEnter2D(const Collision2D& c) override { Do(c.gameObject); }
    void OnTriggerEnter2D(Collider2D* o) override { Do(o ? o->gameObject : nullptr); }
};

static void Run2D(Act act, bool trigger) {
    Scene s("p2");
    GameObject* objs[3];
    for (int i = 0; i < 3; ++i) {
        objs[i] = s.CreateGameObject("o");
        objs[i]->transform->localPosition = {0.2f * i, 0, 0};   // all overlap
        auto* rb = objs[i]->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f;
        auto* bc = objs[i]->AddComponent<BoxCollider2D>();
        bc->size = {1, 1};
        bc->isTrigger = trigger;
    }
    auto* r = objs[0]->AddComponent<Remover2D>();
    r->act = act;
    objs[0]->AddComponent<Count2D>();                           // after the remover: a sibling it may remove
    s.Start();
    for (int f = 0; f < 4; ++f) s.Update(1.0f / 60.0f);
    CHECK(r->fired >= 1);
    if (act == Act::RemoveSelf) CHECK(r->fired == 1);
    if (act == Act::RemoveSibling) CHECK(objs[0]->GetComponent<Count2D>() == nullptr);
}

// ---------------------------------------------------------------- 3D
class Count3D : public Behaviour {
public:
    int hits = 0;
    void OnCollisionEnter3D(const Collision3D&) override { ++hits; }
    void OnCollisionStay3D(const Collision3D&) override { ++hits; }
    void OnTriggerEnter3D(Collider3D*) override { ++hits; }
    void OnTriggerStay3D(Collider3D*) override { ++hits; }
};

class Remover3D : public Behaviour {
public:
    Act act = Act::RemoveSelf;
    int fired = 0;
    void Do(GameObject* other) {
        ++fired;
        if (act == Act::RemoveSelf) {
            gameObject->RemoveComponent<Collider3D>();
            gameObject->RemoveComponent<Rigidbody3D>();
        } else if (act == Act::RemoveOther && other) {
            other->RemoveComponent<Collider3D>();
            other->RemoveComponent<Rigidbody3D>();
        } else if (act == Act::RemoveSibling) {
            gameObject->RemoveComponent<Count3D>();
        }
    }
    void OnCollisionEnter3D(const Collision3D& c) override { Do(c.gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { Do(o ? o->gameObject : nullptr); }
};

static void Run3D(Act act, bool trigger) {
    Scene s("p3");
    GameObject* objs[3];
    for (int i = 0; i < 3; ++i) {
        objs[i] = s.CreateGameObject("o");
        objs[i]->transform->localPosition = {0.2f * i, 0, 0};
        auto* rb = objs[i]->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        auto* bc = objs[i]->AddComponent<BoxCollider3D>();
        bc->size = {1, 1, 1};
        bc->isTrigger = trigger;
    }
    auto* r = objs[0]->AddComponent<Remover3D>();
    r->act = act;
    objs[0]->AddComponent<Count3D>();
    s.Start();
    for (int f = 0; f < 4; ++f) s.Update(1.0f / 60.0f);
    CHECK(r->fired >= 1);
    if (act == Act::RemoveSelf) CHECK(r->fired == 1);
    if (act == Act::RemoveSibling) CHECK(objs[0]->GetComponent<Count3D>() == nullptr);
}

int main() {
    RUN_SUITE("physics_callback_removal");
    for (bool trig : {false, true}) {
        Run2D(Act::RemoveSelf, trig);
        Run2D(Act::RemoveOther, trig);
        Run2D(Act::RemoveSibling, trig);
        Run3D(Act::RemoveSelf, trig);
        Run3D(Act::RemoveOther, trig);
        Run3D(Act::RemoveSibling, trig);
    }

    // A normal contact (no removal) still reports enter on both sides.
    {
        Scene s("n");
        Count2D* c[2];
        for (int i = 0; i < 2; ++i) {
            GameObject* g = s.CreateGameObject("o");
            g->transform->localPosition = {0.5f * i, 0, 0};
            g->AddComponent<Rigidbody2D>()->gravityScale = 0.0f;
            g->AddComponent<BoxCollider2D>()->size = {1, 1};
            c[i] = g->AddComponent<Count2D>();
        }
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(c[0]->hits >= 1);
        CHECK(c[1]->hits >= 1);
    }
    TEST_MAIN_RESULT();
}
