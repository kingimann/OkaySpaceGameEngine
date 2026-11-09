// Mover, Spinner and Lifetime: edge cases (zero, negative, NaN, large steps), spaces,
// frame-rate independence, pause, and exact save / load.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>
#include <string>

using namespace okay;

namespace {

bool NearV(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
bool NearQ(const Quat& a, const Quat& b, float eps = 1e-4f) { return std::fabs(Quat::Dot(a, b)) > 1.0f - eps; }
bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// the first line of `text` that starts with `rec` (after the indent), without the newline
std::string RecordLine(const std::string& text, const std::string& rec) {
    std::size_t at = 0;
    while ((at = text.find("\n  " + rec, at)) != std::string::npos) {
        const std::size_t end = text.find('\n', at + 1);
        return text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
    }
    return {};
}

} // namespace

int main() {
    RUN_SUITE("Mover");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* m = g->AddComponent<Mover>();
        m->velocity = {1.0f, 2.0f, 3.0f};
        s.Update(0.5f);
        CHECK(NearV(g->transform->Position(), {0.5f, 1.0f, 1.5f}, 1e-5f));
        s.Update(0.0f);                                           // a paused frame moves nothing
        CHECK(NearV(g->transform->Position(), {0.5f, 1.0f, 1.5f}, 1e-5f));
        m->velocity = {-1.0f, 0.0f, 0.0f};
        s.Update(1.0f);
        CHECK(NearV(g->transform->Position(), {-0.5f, 1.0f, 1.5f}, 1e-5f));
        m->velocity = Vec3::Zero;
        s.Update(10.0f);
        CHECK(NearV(g->transform->Position(), {-0.5f, 1.0f, 1.5f}, 1e-5f));

        // a bad value never poisons the position
        m->velocity = {kNaN, 1.0f, 1.0f};
        s.Update(1.0f);
        CHECK(Finite3(g->transform->localPosition));
        m->velocity = {kInf, 0.0f, 0.0f};
        s.Update(1.0f);
        CHECK(Finite3(g->transform->localPosition));
        m->velocity = {1.0f, 0.0f, 0.0f};
        m->Update(kNaN);
        CHECK(Finite3(g->transform->localPosition));
        const Vec3 keep = g->transform->localPosition;
        s.Update(1.0f);
        CHECK(NearV(g->transform->localPosition, keep + Vec3{1.0f, 0.0f, 0.0f}, 1e-4f));   // and it keeps working

        // off means off: the component, or any object above
        m->enabled = false;
        s.Update(1.0f);
        CHECK(NearV(g->transform->localPosition, keep + Vec3{1.0f, 0.0f, 0.0f}, 1e-4f));
        m->enabled = true;
        g->active = false;
        s.Update(1.0f);
        CHECK(NearV(g->transform->localPosition, keep + Vec3{1.0f, 0.0f, 0.0f}, 1e-4f));
    }
    {   // spaces, under a turned and stretched parent
        Scene s("T");
        GameObject* parent = s.CreateGameObject("P");
        parent->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);
        parent->transform->localScale = {2.0f, 2.0f, 2.0f};
        GameObject* g = s.CreateGameObject("G");
        g->transform->SetParent(parent->transform, false);
        g->transform->localRotation = Quat::Euler(0.0f, 0.0f, 30.0f);
        auto* m = g->AddComponent<Mover>();
        CHECK(m->space == Space::Local);                          // the default is what it always did
        m->velocity = {1.0f, 0.0f, 0.0f};
        s.Update(1.0f);
        CHECK(NearV(g->transform->localPosition, {1.0f, 0.0f, 0.0f}, 1e-5f));

        g->transform->localPosition = Vec3::Zero;
        m->space = Space::World;
        m->velocity = {0.0f, 0.0f, 3.0f};
        s.Update(0.5f);
        CHECK(NearV(g->transform->Position(), {0.0f, 0.0f, 1.5f}));     // 3 world units per second, whatever the parent does

        g->transform->localPosition = Vec3::Zero;
        const Vec3 before = g->transform->Position();
        m->space = Space::Self;
        m->velocity = Vec3::Forward * 2.0f;
        const Vec3 fwd = g->transform->Forward();
        s.Update(0.5f);
        CHECK(NearV(g->transform->Position() - before, fwd * 2.0f * 0.5f));   // world units along its own forward, whatever the parent's scale
    }

    RUN_SUITE("Spinner");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* sp = g->AddComponent<Spinner>();
        sp->angularVelocity = {0.0f, 0.0f, 90.0f};
        s.Update(1.0f);
        CHECK(NearQ(g->transform->localRotation, Quat::Euler(0.0f, 0.0f, 90.0f)));
        sp->angularVelocity = {0.0f, 0.0f, -90.0f};
        s.Update(1.0f);
        CHECK(NearQ(g->transform->localRotation, Quat::Identity));
        sp->angularVelocity = Vec3::Zero;
        s.Update(1.0f);
        CHECK(NearQ(g->transform->localRotation, Quat::Identity));
        s.Update(0.0f);
        CHECK(NearQ(g->transform->localRotation, Quat::Identity));

        // bad values leave the rotation alone
        sp->angularVelocity = {kNaN, 0.0f, 90.0f};
        s.Update(1.0f);
        CHECK(NearQ(g->transform->localRotation, Quat::Identity));
        sp->angularVelocity = {0.0f, kInf, 0.0f};
        s.Update(1.0f);
        CHECK(NearQ(g->transform->localRotation, Quat::Identity));
        sp->angularVelocity = {0.0f, 0.0f, 90.0f};
        sp->Update(kNaN);
        CHECK(NearQ(g->transform->localRotation, Quat::Identity));
    }
    {   // a spin about several axes is the same whatever the frame rate
        Scene a("A"), b("B");
        GameObject* ga = a.CreateGameObject("G");
        GameObject* gb = b.CreateGameObject("G");
        ga->AddComponent<Spinner>()->angularVelocity = {60.0f, 90.0f, 30.0f};
        gb->AddComponent<Spinner>()->angularVelocity = {60.0f, 90.0f, 30.0f};
        a.Update(1.0f);                                           // one big step
        for (int i = 0; i < 128; ++i) b.Update(1.0f / 128.0f);    // many small ones
        CHECK(NearQ(ga->transform->localRotation, gb->transform->localRotation, 1e-3f));
        // a full turn per second about one axis comes back after a second, however it is cut up
        Scene c("C");
        GameObject* gc = c.CreateGameObject("G");
        gc->AddComponent<Spinner>()->angularVelocity = {0.0f, 360.0f, 0.0f};
        for (int i = 0; i < 60; ++i) c.Update(1.0f / 60.0f);
        CHECK(NearQ(gc->transform->localRotation, Quat::Identity, 1e-3f));
    }
    {   // a long run keeps the rotation a unit quaternion
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Spinner>()->angularVelocity = {37.0f, 53.0f, 71.0f};
        for (int i = 0; i < 20000; ++i) s.Update(0.016f);
        const Quat q = g->transform->localRotation;
        CHECK_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-4f);
    }
    {   // spaces
        Scene s("T");
        GameObject* parent = s.CreateGameObject("P");
        parent->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        GameObject* g = s.CreateGameObject("G");
        g->transform->SetParent(parent->transform, false);
        g->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);
        auto* sp = g->AddComponent<Spinner>();
        CHECK(sp->space == Space::Self);
        sp->angularVelocity = {0.0f, 0.0f, 90.0f};
        const Quat start = g->transform->localRotation;
        s.Update(1.0f);
        CHECK(NearQ(g->transform->localRotation, start * Quat::Euler(0.0f, 0.0f, 90.0f)));
        g->transform->localRotation = start;
        sp->space = Space::Local;
        s.Update(1.0f);
        CHECK(NearQ(g->transform->localRotation, Quat::Euler(0.0f, 0.0f, 90.0f) * start));
        g->transform->localRotation = start;
        sp->space = Space::World;
        const Quat w0 = g->transform->Rotation();
        s.Update(1.0f);
        CHECK(NearQ(g->transform->Rotation(), Quat::Euler(0.0f, 0.0f, 90.0f) * w0));
    }

    RUN_SUITE("Lifetime");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        GameObject* kid = s.CreateGameObject("Kid");
        kid->transform->SetParent(g->transform, false);
        auto* lt = g->AddComponent<Lifetime>();
        lt->seconds = 1.0f;
        CHECK(lt->Elapsed() == 0.0f && lt->Remaining() == 1.0f);
        s.Update(0.25f); s.Update(0.25f);
        CHECK(lt->Elapsed() == 0.5f && lt->Remaining() == 0.5f);
        CHECK(!g->IsDestroyed());
        s.Update(0.5f);                                           // exactly on time
        CHECK(g->IsDestroyed() && kid->IsDestroyed());            // the children go with it
        CHECK(s.Objects().empty());
    }
    {   // zero and negative: gone on the first update
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        a->AddComponent<Lifetime>()->seconds = 0.0f;
        GameObject* b = s.CreateGameObject("B");
        b->AddComponent<Lifetime>()->seconds = -5.0f;
        s.Update(0.016f);
        CHECK(a->IsDestroyed() && b->IsDestroyed());
    }
    {   // infinity is forever, a paused frame takes no time off
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        a->AddComponent<Lifetime>()->seconds = kInf;
        GameObject* b = s.CreateGameObject("B");
        auto* lb = b->AddComponent<Lifetime>();
        lb->seconds = 1.0f;
        for (int i = 0; i < 100; ++i) s.Update(10.0f);
        CHECK(!a->IsDestroyed() && b->IsDestroyed());
        GameObject* c = s.CreateGameObject("C");
        auto* lc = c->AddComponent<Lifetime>();
        lc->seconds = 1.0f;
        for (int i = 0; i < 100; ++i) s.Update(0.0f);
        CHECK(!c->IsDestroyed() && lc->Elapsed() == 0.0f);
        lc->enabled = false;                                       // disabling the component pauses it
        s.Update(5.0f);
        CHECK(!c->IsDestroyed() && lc->Elapsed() == 0.0f);
        lc->enabled = true;
        lc->SetElapsed(0.75f);
        CHECK(lc->Remaining() == 0.25f);
        s.Update(0.25f);
        CHECK(c->IsDestroyed());
        // a negative or bad frame time never extends or shortens the life
        GameObject* d = s.CreateGameObject("D");
        auto* ld = d->AddComponent<Lifetime>();
        ld->seconds = 2.0f;
        ld->Update(-1.0f);
        ld->Update(kNaN);
        CHECK(ld->Elapsed() == 0.0f);
        ld->SetElapsed(kNaN);
        CHECK(ld->Elapsed() == 0.0f);
        ld->Restart();
        CHECK(ld->Elapsed() == 0.0f);
    }

    RUN_SUITE("save and load keep the exact numbers");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* m = g->AddComponent<Mover>();
        m->velocity = {0.1f, 1.0f / 3.0f, 123456.79f};
        auto* sp = g->AddComponent<Spinner>();
        sp->angularVelocity = {1e-7f, -0.7f, 36000.0f};
        auto* lt = g->AddComponent<Lifetime>();
        lt->seconds = 0.1f;
        const std::string text = SceneSerializer::Serialize(s);
        // defaults are not written: the text of a scene that never uses the new fields is as before
        CHECK(RecordLine(text, "mover") == "  mover 0.1 0.33333334 123456.79");
        CHECK(RecordLine(text, "spinner") == "  spinner 1e-07 -0.7 36000");
        CHECK(RecordLine(text, "lifetime") == "  lifetime 0.1");
        Scene t("L");
        CHECK(SceneSerializer::Deserialize(t, text));
        GameObject* b = t.Find("G");
        CHECK(b && b->GetComponent<Mover>() && b->GetComponent<Spinner>() && b->GetComponent<Lifetime>());
        if (b && b->GetComponent<Mover>() && b->GetComponent<Spinner>() && b->GetComponent<Lifetime>()) {
            CHECK(b->GetComponent<Mover>()->velocity.x == 0.1f);
            CHECK(b->GetComponent<Mover>()->velocity.y == 1.0f / 3.0f);
            CHECK(b->GetComponent<Mover>()->velocity.z == 123456.79f);
            CHECK(b->GetComponent<Spinner>()->angularVelocity.x == 1e-7f);
            CHECK(b->GetComponent<Spinner>()->angularVelocity.y == -0.7f);
            CHECK(b->GetComponent<Spinner>()->angularVelocity.z == 36000.0f);
            CHECK(b->GetComponent<Lifetime>()->seconds == 0.1f);
        }
        CHECK(SceneSerializer::Serialize(t) == text);              // no drift on a second save
    }
    {   // spaces and elapsed time are optional trailing fields
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* m = g->AddComponent<Mover>();
        m->velocity = {1.0f, 2.0f, 3.0f};
        m->space = Space::World;
        auto* sp = g->AddComponent<Spinner>();
        sp->angularVelocity = {0.0f, 90.0f, 0.0f};
        sp->space = Space::Local;
        auto* lt = g->AddComponent<Lifetime>();
        lt->seconds = 5.0f;
        s.Update(1.5f);                                           // runs all three
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(RecordLine(text, "mover") == "  mover 1 2 3 1");
        CHECK(RecordLine(text, "spinner") == "  spinner 0 90 0 0");
        CHECK(RecordLine(text, "lifetime") == "  lifetime 5 1.5");
        Scene t("L");
        CHECK(SceneSerializer::Deserialize(t, text));
        GameObject* b = t.Find("G");
        CHECK(b && b->GetComponent<Mover>()->space == Space::World);
        CHECK(b && b->GetComponent<Spinner>()->space == Space::Local);
        CHECK(b && b->GetComponent<Lifetime>()->Elapsed() == 1.5f && b->GetComponent<Lifetime>()->Remaining() == 3.5f);
        t.Update(3.5f);                                           // the saved game picks up where it was
        CHECK(b && b->IsDestroyed());
        CHECK(SceneSerializer::Serialize(t).find("lifetime") == std::string::npos);
    }
    {   // older text still loads, and a malformed trailing field does not fail the load
        Scene t("L");
        const std::string old =
            "okayscene 3\ngameobject 0 \"A\"\n  parent -1\n  mover 1 2 3\n  spinner 0 0 90\n  lifetime 4\nend\n";
        CHECK(SceneSerializer::Deserialize(t, old));
        GameObject* a = t.Find("A");
        CHECK(a && a->GetComponent<Mover>()->space == Space::Local && a->GetComponent<Spinner>()->space == Space::Self);
        CHECK(a && a->GetComponent<Lifetime>()->Elapsed() == 0.0f && a->GetComponent<Lifetime>()->seconds == 4.0f);
        Scene u("L2");
        const std::string odd =
            "okayscene 3\ngameobject 0 \"A\"\n  parent -1\n  mover 1 2 3 9\n  spinner 0 0 90 -4\n  lifetime 4 -2\nend\n";
        CHECK(SceneSerializer::Deserialize(u, odd));             // out of range space values become the default
        GameObject* b = u.Find("A");
        CHECK(b && b->GetComponent<Mover>()->space == Space::Local && b->GetComponent<Spinner>()->space == Space::Self);
        CHECK(b && b->GetComponent<Lifetime>()->Elapsed() == 0.0f);   // a negative elapsed time is not time
    }

    RUN_SUITE("a clone keeps what the original was set to");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->AddComponent<Mover>()->velocity = {4.0f, 5.0f, 6.0f};
        g->GetComponent<Mover>()->space = Space::Self;
        g->AddComponent<Lifetime>()->seconds = 3.0f;
        GameObject* c = s.Instantiate(*g);
        CHECK(c && c->GetComponent<Mover>() && c->GetComponent<Mover>()->space == Space::Self);
        CHECK(c && NearV(c->GetComponent<Mover>()->velocity, {4.0f, 5.0f, 6.0f}, 0.0f));
        CHECK(c && c->GetComponent<Lifetime>()->seconds == 3.0f);
    }

    TEST_MAIN_RESULT();
}
