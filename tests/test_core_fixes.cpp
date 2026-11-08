#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

namespace {
// A renderer that draws nothing, so Application::Run stays quiet.
class NullRenderer : public IRenderer {
public:
    void BeginFrame(const Color&) override {}
    void EndFrame() override {}
    void SetCamera(const Vec3&, float, float) override {}
    void DrawQuad(const Vec3&, const Vec2&, const Color&, char) override {}
    Viewport GetViewport() const override { return {10, 10}; }
};

// Runs a callback on a chosen frame of Update.
class FrameHook : public Component {
public:
    int frame = 0, at = 1;
    std::function<void()> fn;
    void Update(float) override { if (++frame == at && fn) fn(); }
};

int RunFrames(Application::Config cfg, int hookFrame, std::function<void()> fn, int* framesRun = nullptr) {
    cfg.targetFps = 0.0f; cfg.maxFrames = 40; cfg.clearScreen = false;
    Application app(cfg);
    app.SetRenderer(std::make_unique<NullRenderer>());
    Scene scene("t");
    auto* go = scene.CreateGameObject("hook");
    auto* h = go->AddComponent<FrameHook>();
    h->at = hookFrame; h->fn = std::move(fn);
    app.Run(scene);
    if (framesRun) *framesRun = h->frame;
    return h->frame;
}
}

int main() {
    RUN_SUITE("core_fixes");

    // ---- Application: Game::RequestQuit ends the loop ----
    {
        Application::Config cfg;
        int frames = RunFrames(cfg, 5, [] { Game::RequestQuit(); });
        CHECK(frames >= 5 && frames <= 6);          // stopped right after the requesting frame
        CHECK(Game::QuitRequested());               // the host can still see why it ended; the next Run clears it
    }
    // ---- Application: quit key is configurable ----
    {
        Application::Config cfg; cfg.quitKey = 'x';
        int frames = RunFrames(cfg, 3, [] { Input::FeedKeys({'x'}); });
        CHECK(frames == 3);
    }
    {   // 'q' is no longer special when another key is configured
        Application::Config cfg; cfg.quitKey = 'x';
        int frames = RunFrames(cfg, 3, [] { Input::FeedKeys({'q'}); });
        CHECK(frames == 40);
    }
    {   // default still 'q'
        Application::Config cfg;
        int frames = RunFrames(cfg, 3, [] { Input::FeedKeys({'q'}); });
        CHECK(frames == 3);
    }
    {   // no quit key at all
        Application::Config cfg; cfg.quitKey = 0;
        int frames = RunFrames(cfg, 3, [] { Input::FeedKeys({'q'}); });
        CHECK(frames == 40);
    }
    {   // a stale quit request from before Run does not end the run immediately
        Game::RequestQuit();
        Application::Config cfg;
        int frames = RunFrames(cfg, 100, [] {});
        CHECK(frames == 40);
    }

    // ---- Rect::Overlaps is strict ----
    {
        Rect a(0, 0, 10, 10);
        CHECK(a.Overlaps(Rect(5, 5, 10, 10)));
        CHECK(!a.Overlaps(Rect(10, 0, 5, 5)));      // shares the right edge only
        CHECK(!a.Overlaps(Rect(0, 10, 5, 5)));      // shares the bottom edge only
        CHECK(!a.Overlaps(Rect(10, 10, 5, 5)));     // touches at a corner
        CHECK(!a.Overlaps(Rect(-5, -5, 5, 5)));     // corner from the other side
        CHECK(a.Overlaps(Rect(9.99f, 9.99f, 5, 5)));
        CHECK(a.Overlaps(Rect(2, 2, 3, 3)));        // contained
        CHECK(Rect(2, 2, 3, 3).Overlaps(a));
        CHECK(a.Overlaps(Rect(10, 10, -5, -5)));    // negative size = same rect, flipped
        CHECK(Rect(5, 5, 0, 0).Overlaps(a));        // a point inside (a click marquee)
        CHECK(!Rect(15, 5, 0, 0).Overlaps(a));
    }

    // ---- Mathf::SmoothStep ----
    {
        CHECK_NEAR(Mathf::SmoothStep(0, 1, 0.5f), 0.5f, 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(0, 1, -1.0f), 0.0f, 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(0, 1, 2.0f), 1.0f, 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(0, 10, 2.5f), 0.15625f, 1e-6);   // x=.25 -> .25^2*(3-.5)
        // reversed edges mirror the curve: a fade-out
        CHECK_NEAR(Mathf::SmoothStep(1, 0, 1.0f), 0.0f, 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(1, 0, 0.0f), 1.0f, 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(1, 0, 0.5f), 0.5f, 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(10, 0, 2.5f), Mathf::SmoothStep(0, 10, 7.5f), 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(1, 0, 0.25f), 1.0f - Mathf::SmoothStep(0, 1, 0.25f), 1e-6);
        // equal edges: a step, not NaN
        CHECK_NEAR(Mathf::SmoothStep(2, 2, 1.0f), 0.0f, 1e-6);
        CHECK_NEAR(Mathf::SmoothStep(2, 2, 3.0f), 1.0f, 1e-6);
    }

    // ---- Quat::ToEuler keeps roll near +/-90 degree pitch ----
    {
        auto near = [](const Vec3& a, const Vec3& b, float eps) {
            return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
        };
        CHECK(near(Quat::Euler(89.5f, 30, 20).ToEuler(), {89.5f, 30, 20}, 0.1f));
        CHECK(near(Quat::Euler(-89.5f, 30, 20).ToEuler(), {-89.5f, 30, 20}, 0.1f));
        CHECK(near(Quat::Euler(89.9f, 30, 20).ToEuler(), {89.9f, 30, 20}, 0.5f));
        CHECK(near(Quat::Euler(45, 30, 20).ToEuler(), {45, 30, 20}, 0.05f));
        // at exactly 90 the pair (yaw, roll) is ambiguous, but it must rebuild the same rotation
        Quat q = Quat::Euler(90, 30, 20);
        Quat r = Quat::Euler(q.ToEuler());
        Vec3 v{0.3f, 0.5f, 0.8f};
        Vec3 a = q * v, b = r * v;
        CHECK(near(a, b, 1e-2f));
        for (float p : {89.0f, 89.5f, 89.9f, -89.5f}) {
            Quat q2 = Quat::Euler(p, -40, 70);
            Quat r2 = Quat::Euler(q2.ToEuler());
            CHECK(near(q2 * v, r2 * v, 1e-2f));
        }
    }

    TEST_MAIN_RESULT();
}
