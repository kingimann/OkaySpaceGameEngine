// Linux test for XboxPadHub.hpp (gamepad hot-plug and player slots). Pure C++. UNVERIFIED with real pads.
#include "XboxPadHub.hpp"
#include <cstdio>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
using okay::xbox::PadHub; using okay::xbox::PadHubEvent;
int main() {
    { // lowest free slot, distinct instance ids
        PadHub h(3, 1000);
        CHECK(h.OnAdded("A", 0) == 0 && h.OnAdded("B", 0) == 1 && h.OnAdded("A", 5) == 0);
        CHECK(h.InstanceId(0) != h.InstanceId(1) && h.PresentCount() == 2 && h.SlotOf("B") == 1 && h.SlotOf("Z") == -1);
        auto ev = h.TakeEvents(); CHECK(ev.size() == 2 && ev[0].kind == PadHubEvent::Added && h.TakeEvents().empty());
    }
    { // lost pad keeps its slot and id inside the grace time, same pad returns as the same player
        PadHub h(2, 1000); h.OnAdded("A", 0); h.OnAdded("B", 0); h.TakeEvents();
        const int id = h.InstanceId(0);
        h.OnRemoved("A", 100); CHECK(!h.Present(0) && h.PresentCount() == 1);
        CHECK(h.OnAdded("C", 500) == -1);                      // slot 0 still held, slot 1 taken: refused
        CHECK(h.OnAdded("A", 900) == 0 && h.InstanceId(0) == id && h.Present(0));
        auto ev = h.TakeEvents();
        CHECK(ev.size() == 3 && ev[0].kind == PadHubEvent::Lost && ev[1].kind == PadHubEvent::Refused && ev[2].kind == PadHubEvent::Reconnected);
    }
    { // after the grace time the slot is freed and a new pad may take it with a new id
        PadHub h(1, 1000); h.OnAdded("A", 0); const int id = h.InstanceId(0); h.OnRemoved("A", 10); h.TakeEvents();
        h.Tick(1009); CHECK(h.SlotOf("A") == 0 && h.TakeEvents().empty());
        h.Tick(1010); auto ev = h.TakeEvents(); CHECK(ev.size() == 1 && ev[0].kind == PadHubEvent::Removed && ev[0].slot == 0 && h.SlotOf("A") == -1);
        CHECK(h.OnAdded("B", 1011) == 0 && h.InstanceId(0) != id);
        CHECK(h.OnAdded("A", 1012) == -1);
    }
    { // removing an unknown or already-lost pad does nothing; bad slots answer safely; clock going backwards frees nothing
        PadHub h(2, 100); h.OnRemoved("nope", 0); h.OnAdded("A", 50); h.OnRemoved("A", 60); h.OnRemoved("A", 70); h.TakeEvents();
        h.Tick(10); CHECK(h.SlotOf("A") == 0);
        CHECK(h.InstanceId(-1) == -1 && h.InstanceId(9) == -1 && !h.Present(9) && h.InstanceId(1) == -1);
        PadHub z(0); CHECK(z.SlotCount() == 1);
    }
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("xbox_uwp_padhub: all passed\n"); return 0;
}
