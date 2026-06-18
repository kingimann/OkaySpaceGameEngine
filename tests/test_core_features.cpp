#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

namespace { struct Marker : Behaviour { int value = 0; }; }

int main() {
    RUN_SUITE("core_features");

    // --- Random: determinism + ranges ---
    {
        Random a(12345), b(12345);
        bool sameStream = true;
        for (int i = 0; i < 100; ++i) if (a.NextUInt64() != b.NextUInt64()) sameStream = false;
        CHECK(sameStream); // same seed -> same sequence

        Random r(7);
        bool inRange = true;
        for (int i = 0; i < 1000; ++i) {
            float v = r.Range(2.0f, 5.0f);
            if (v < 2.0f || v >= 5.0f) inRange = false;
            int n = r.Range(0, 4);
            if (n < 0 || n >= 4) inRange = false;
        }
        CHECK(inRange);
        CHECK(r.InsideUnitCircle().Magnitude() <= 1.0001f);
    }

    // --- Math: Mathf extras, Rect, Bounds ---
    {
        CHECK_NEAR(Mathf::InverseLerp(10.0f, 20.0f, 15.0f), 0.5f, 1e-5);
        CHECK_NEAR(Mathf::DeltaAngle(350.0f, 10.0f), 20.0f, 1e-3);
        // Result (360) is equivalent to 0 as an angle; compare via DeltaAngle.
        CHECK_NEAR(Mathf::DeltaAngle(0.0f, Mathf::LerpAngle(350.0f, 10.0f, 0.5f)), 0.0f, 1e-3);

        Rect rc = Rect::Centered({0, 0}, {4, 2});
        CHECK(rc.Contains({1.0f, 0.5f}));
        CHECK(!rc.Contains({3.0f, 0.0f}));
        CHECK(rc.Overlaps(Rect::Centered({3, 0}, {4, 2})));

        Bounds bd({0, 0, 0}, {2, 2, 2});
        CHECK(bd.Contains({0.5f, 0.5f, 0.5f}));
        CHECK(!bd.Contains({2.0f, 0, 0}));
        bd.Encapsulate({5, 0, 0});
        CHECK(bd.Contains({5, 0, 0}));
    }

    // --- EventBus ---
    {
        struct Scored { int points; };
        EventBus bus;
        int total = 0;
        int token = bus.Subscribe<Scored>([&](const Scored& e) { total += e.points; });
        bus.Publish(Scored{10});
        bus.Publish(Scored{5});
        CHECK(total == 15);
        bus.Unsubscribe<Scored>(token);
        bus.Publish(Scored{100});
        CHECK(total == 15); // no longer subscribed
        CHECK(bus.SubscriberCount<Scored>() == 0);
    }

    // --- Scheduler: Invoke, InvokeRepeating, Tween ---
    {
        Scheduler s;
        int fired = 0;
        s.Invoke(1.0f, [&] { ++fired; });
        for (int i = 0; i < 9; ++i) s.Update(0.1f); // 0.9s
        CHECK(fired == 0);
        s.Update(0.2f); // crosses 1.0s
        CHECK(fired == 1);

        int repeats = 0;
        s.InvokeRepeating(0.5f, [&] { ++repeats; }, 3);
        for (int i = 0; i < 40; ++i) s.Update(0.1f); // 4s -> fires 3 times then stops
        CHECK(repeats == 3);

        float last = -1.0f; bool completed = false;
        s.Tween(1.0f, [&](float t) { last = t; }, [&] { completed = true; });
        for (int i = 0; i < 10; ++i) s.Update(0.1f);
        s.Update(0.05f);
        CHECK(completed);
        CHECK_NEAR(last, 1.0f, 1e-3);
    }

    // --- Scene::FindObjectsOfType ---
    {
        Scene scene("Find");
        scene.CreateGameObject("A")->AddComponent<Marker>()->value = 1;
        scene.CreateGameObject("B")->AddComponent<Marker>()->value = 2;
        scene.CreateGameObject("C"); // no marker
        auto markers = scene.FindObjectsOfType<Marker>();
        CHECK(markers.size() == 2);
        CHECK(scene.FindObjectOfType<Marker>() != nullptr);

        int sum = 0; for (auto* m : markers) sum += m->value;
        CHECK(sum == 3);
    }

    // --- Scene scheduler integration (Invoke ticks during Update) ---
    {
        Scene scene("SchedScene");
        int ran = 0;
        scene.scheduler().Invoke(0.5f, [&] { ++ran; });
        scene.Start();
        for (int i = 0; i < 4; ++i) scene.Update(0.1f); // 0.4s
        CHECK(ran == 0);
        scene.Update(0.2f); // 0.6s
        CHECK(ran == 1);
    }

    TEST_MAIN_RESULT();
}
