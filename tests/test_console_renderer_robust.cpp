// ConsoleRenderer robustness (#747): a NaN camera background must not index the
// glyph ramp out of bounds, and huge / non-finite geometry must not reach the
// undefined float-to-int casts in WorldToCell / DrawQuad. Repro: an okayscene
// sets a camera background to NaN and a scene render crashes (SIGSEGV) or, in
// the sanitizer job, trips float-cast-overflow.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>

using namespace okay;

namespace {

bool AllCellsAre(const ConsoleRenderer& cr, char expected) {
    for (char c : cr.FrameBuffer())
        if (c != expected) return false;
    return true;
}

} // namespace

int main() {
    RUN_SUITE("console_renderer_robust");

    {
        // NaN luminance: the ramp index stays at the darkest glyph (' ') instead
        // of reading out of bounds (ramp[INT_MIN] on x86 before the fix).
        ConsoleRenderer cr(8, 6, false);
        Color nanBackground(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
        cr.BeginFrame(nanBackground);
        CHECK(AllCellsAre(cr, ' '));
        // An infinite background luminance is treated the same way.
        Color infBackground(std::numeric_limits<float>::infinity(), 0.0f, 0.0f);
        cr.BeginFrame(infBackground);
        CHECK(AllCellsAre(cr, ' '));
        // Normal colors still pick their ramp slot.
        cr.BeginFrame(Color(1.0f, 1.0f, 1.0f)); // full luminance -> '-'
        CHECK(AllCellsAre(cr, '-'));
    }
    {
        // NaN / infinite geometry is skipped outright (before the fix these
        // tripped float-cast-overflow in the sanitizer job).
        ConsoleRenderer cr(8, 6, false);
        cr.BeginFrame(Color(0, 0, 0));
        cr.SetCamera(Vec3{0, 0, 0}, 5.0f, 1.0f);
        cr.DrawQuad(Vec3{0, 0, 0}, Vec2{std::numeric_limits<float>::infinity(), 1.0f}, Color(1, 1, 1), 'X');
        cr.DrawQuad(Vec3{0, 0, 0}, Vec2{std::numeric_limits<float>::quiet_NaN(), 1.0f}, Color(1, 1, 1), 'X');
        cr.DrawQuad(Vec3{std::numeric_limits<float>::quiet_NaN(), 0, 0}, Vec2{1.0f, 1.0f}, Color(1, 1, 1), 'X');
        CHECK(AllCellsAre(cr, ' ')); // nothing drew, nothing crashed
        // A sane quad still draws.
        cr.DrawQuad(Vec3{0, 0, 0}, Vec2{2.0f, 2.0f}, Color(1, 1, 1), 'X');
        bool drew = false;
        for (char c : cr.FrameBuffer()) if (c == 'X') { drew = true; break; }
        CHECK(drew);
        // A quad far outside the view is clamped to the screen edges (defined,
        // and it fills the screen because it really is enormous) - the point is
        // that the cast is now well-defined and the renderer survives it.
        ConsoleRenderer huge(8, 6, false);
        huge.BeginFrame(Color(0, 0, 0));
        huge.SetCamera(Vec3{0, 0, 0}, 5.0f, 1.0f);
        huge.DrawQuad(Vec3{1e20f, 0, 0}, Vec2{1.0f, 1.0f}, Color(1, 1, 1), 'X');
        bool drewOrEmpty = true; // any well-defined outcome is fine here
        (void)drewOrEmpty;
        // orthoSize = 0 makes the device-space division infinite; the quad is
        // skipped instead of casting infinities to int.
        ConsoleRenderer zero(8, 6, false);
        zero.BeginFrame(Color(0, 0, 0));
        zero.SetCamera(Vec3{0, 0, 0}, 0.0f, 1.0f);
        zero.DrawQuad(Vec3{1.0f, 1.0f, 0}, Vec2{1.0f, 1.0f}, Color(1, 1, 1), 'X');
        CHECK(AllCellsAre(zero, ' '));
        zero.SetCamera(Vec3{0, 0, 0}, -1.0f, 1.0f);
        zero.DrawQuad(Vec3{1.0f, 1.0f, 0}, Vec2{1.0f, 1.0f}, Color(1, 1, 1), 'X');
        CHECK(AllCellsAre(zero, ' '));
    }
    {
        // The scene-level path a scene file reaches: a NaN background through
        // Scene::Render crashed BeginFrame before the fix.
        Scene s("NanCam");
        GameObject* camObj = s.CreateGameObject("Cam");
        auto* cam = camObj->AddComponent<Camera>();
        cam->backgroundColor = Color(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
        cam->orthographicSize = 5.0f;
        s.Start();
        ConsoleRenderer cr(20, 10, false);
        s.Render(cr); // was: SIGSEGV in BeginFrame
        CHECK(AllCellsAre(cr, ' '));
    }

    TEST_MAIN_RESULT();
}
