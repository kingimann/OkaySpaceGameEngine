// How much the per-frame input work costs: 100 actions, 4 controllers, a context pushed (docs/input_audit.md).
// Prints microseconds per frame and allocations per frame for the fast path and the old one (OKAY_OLD_INPUT).
// Only the allocation count is checked (it must be 0); the times are printed, not asserted, because a busy
// machine makes them jump around.
#include "test_framework.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Core/Prefs.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace okay;

static long long g_allocs = 0;
void* operator new(std::size_t n) { ++g_allocs; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static void Frame(int i) {
    static const std::vector<char> keys = {'w', 'a', ' ', 'e'};
    Input::FeedKeys(keys);
    Input::FeedMouse({(float)i, 0}, (unsigned)(i & 1));
    for (int p = 0; p < 4; ++p) Input::FeedGamepadRaw(p, {0.3f, 0.4f}, {0.1f, 0.2f}, 0.2f, 0.7f, (i & 1) ? 1u : 0u, true);
    InputActions::Tick(1.0f / 60.0f);
    volatile bool s = false;
    s = s ^ InputActions::Get("Jump"); s = s ^ InputActions::Down("Action40"); s = s ^ InputActions::Get("Fire");
    s = s ^ (InputActions::Vector("Move").x > 0);
}

int main() {
    RUN_SUITE("input_bench");
    Prefs::Clear();
    InputActions::Restore();
    for (int i = 0; i < 89; ++i) {
        char n[32]; std::snprintf(n, sizeof n, "Action%d", i);
        InputActions::Define(n, {i % 2 ? "key:e" : "key:f", "pad:x", "mouse:left"}, false);
    }
    InputActions::DefineIn("UI", "Confirm", {"key:space"});
    InputActions::PushContext("UI");
    for (bool legacy : {false, true}) {
        InputActions::SetLegacy(legacy);
        for (int i = 0; i < 200; ++i) Frame(i);
        g_allocs = 0;
        const int N = 3000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) Frame(i);
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
        std::printf("  %s: %zu actions, %.2f us per frame, %.1f allocations per frame\n", legacy ? "old path (OKAY_OLD_INPUT)" : "fast path",
                    InputActions::All().size(), us, (double)g_allocs / N);
        CHECK(g_allocs == 0);
    }
    InputActions::SetLegacy(false);
    TEST_MAIN_RESULT();
}
