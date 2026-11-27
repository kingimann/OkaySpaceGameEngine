// The Game view's device emulator, the model (editor/DeviceEmulatorModel.hpp, no UI library): the device table, safe
// areas in both orientations, placing the screen (Fit / 100% / zoom) and mapping a mouse point to the game's touch
// pixels and back, touch emulation with a second finger, rotation, the on-screen keyboard, the motion sensor, the
// conditions, the multi-device layout, screenshots, settings (with the old keys), and what the Screen API reports.
#include "test_framework.hpp"
#include "DeviceEmulatorModel.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UISafeArea.hpp"
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace okay::devemu;

static const DeviceSpec& Dev(const char* id) {
    const DeviceSpec* d = FindDevice(id);
    static DeviceSpec none;
    return d ? *d : none;
}

int main() {
    RUN_SUITE("device_emulator");

    {   // The table: many devices, unique ids, sane numbers, every inset inside the screen in all four orientations.
        const auto& all = Devices();
        CHECK(all.size() >= 40);
        std::set<std::string> ids;
        int ios = 0, android = 0, tablets = 0, folds = 0;
        for (const DeviceSpec& d : all) {
            CHECK(!d.id.empty() && !d.name.empty());
            CHECK(ids.insert(d.id).second);
            CHECK(d.width >= 320 && d.height >= d.width / 2 && d.width <= 4096 && d.height <= 4096);
            CHECK(d.ppi >= 100.0f && d.ppi <= 700.0f);
            CHECK(d.scale >= 1.0f && d.scale <= 4.0f);
            ios += d.platform == Platform::Ios; android += d.platform == Platform::Android;
            tablets += d.form == Form::Tablet; folds += d.form == Form::Foldable;
            for (int oi = 0; oi < 4; ++oi) {
                const Orientation o = (Orientation)oi;
                int w = 0, h = 0;
                ScreenPixels(d, o, w, h);
                CHECK(IsLandscape(o) ? (w == d.height && h == d.width) : (w == d.width && h == d.height));
                const Insets s = SafeInsetsPx(d, o);
                CHECK(s.left >= 0 && s.top >= 0 && s.right >= 0 && s.bottom >= 0);
                CHECK(s.left + s.right < w * 0.5f && s.top + s.bottom < h * 0.5f);
                const CutoutShape c = CutoutPx(d, o);
                if (c.kind == Cutout::Notch || c.kind == Cutout::Island || c.kind == Cutout::PunchHole) {
                    CHECK(c.box.w > 0 && c.box.h > 0);
                    CHECK(c.box.x >= -1 && c.box.y >= -1 && c.box.Right() <= w + 1 && c.box.Bottom() <= h + 1);
                    // the cut-out sits inside the unsafe band on its side
                    if (o == Orientation::Portrait) CHECK(c.box.Bottom() <= s.top + 1);
                    if (o == Orientation::LandscapeLeft) CHECK(c.box.Right() <= s.left + 1);
                    if (o == Orientation::LandscapeRight) CHECK(c.box.x >= w - s.right - 1);
                }
                CHECK(StatusBarPx(d, o) <= (IsLandscape(o) ? s.top + 1 : s.top + 1));
            }
            CHECK(CornerPx(d) >= 0 && CornerPx(d) < d.width * 0.25f);
        }
        CHECK(ios >= 12 && android >= 15 && tablets >= 8 && folds >= 3);
        CHECK(FindDevice("") == nullptr && FindDevice("no-such-phone") == nullptr);
    }
    {   // Known numbers from the spec sheets.
        const DeviceSpec& p = Dev("iphone-16-pro");
        CHECK(p.width == 1206 && p.height == 2622 && p.platform == Platform::Ios && p.cutout == Cutout::Island);
        const Insets port = SafeInsetsPx(p, Orientation::Portrait);
        CHECK_NEAR(port.top, 186.0f, 0.5f);       // 62 pt at 3x
        CHECK_NEAR(port.bottom, 102.0f, 0.5f);    // 34 pt
        const Insets land = SafeInsetsPx(p, Orientation::LandscapeLeft);
        CHECK_NEAR(land.left, 186.0f, 0.5f); CHECK_NEAR(land.right, 186.0f, 0.5f);   // iOS: both sides
        CHECK_NEAR(land.top, 0.0f, 0.01f); CHECK_NEAR(land.bottom, 63.0f, 0.5f);
        CHECK_NEAR(StatusBarPx(p, Orientation::LandscapeLeft), 0.0f, 0.01f);         // the iPhone hides it
        const DeviceSpec& px = Dev("pixel-8");
        const Insets pl = SafeInsetsPx(px, Orientation::LandscapeLeft), pr = SafeInsetsPx(px, Orientation::LandscapeRight);
        CHECK(pl.left > 0 && pl.right == 0 && pr.right > 0 && pr.left == 0);         // Android: only the camera side
        const Insets up = SafeInsetsPx(px, Orientation::PortraitUpsideDown);
        CHECK(up.bottom >= SafeInsetsPx(px, Orientation::Portrait).top - 0.5f);      // the camera is at the bottom now
        const DeviceSpec& se = Dev("iphone-se");
        CHECK(se.cutout == Cutout::HomeButton && se.nav == Nav::HomeButton && SafeInsetsPx(se, Orientation::Portrait).bottom == 0);
    }
    {   // The old saved indexes keep pointing at the same phones.
        CHECK(LegacyDeviceId(0).empty() && LegacyDeviceId(99).empty() && LegacyDeviceId(-1).empty());
        CHECK(LegacyDeviceId(1) == "iphone-16-pro" && LegacyDeviceId(7) == "iphone-se" && LegacyDeviceId(24) == "fire-hd-10");
        CHECK(Dev(LegacyDeviceId(18).c_str()).name.find("iPad Pro 12.9") != std::string::npos);
        for (int i = 1; i <= 24; ++i) CHECK(LegacyDeviceIndex(LegacyDeviceId(i)) == i);
        CHECK(LegacyDeviceIndex("pixel-9") == 0);
        CHECK(LegacyShortListId(1) == LegacyDeviceId(3) && LegacyShortListId(0).empty() && LegacyShortListId(50).empty());
    }
    {   // Turning.
        CHECK(TurnLeft(Orientation::Portrait) == Orientation::LandscapeLeft);
        CHECK(TurnRight(Orientation::Portrait) == Orientation::LandscapeRight);
        Orientation o = Orientation::Portrait;
        for (int i = 0; i < 4; ++i) o = TurnLeft(o);
        CHECK(o == Orientation::Portrait);
        CHECK(OrientationDegrees(Orientation::PortraitUpsideDown) == 180);
        RotateAnimation a;
        a.Start(Orientation::Portrait, Orientation::LandscapeLeft, 10.0, 0.4);
        CHECK(a.Running(10.1));
        CHECK_NEAR(a.Angle(10.0), -90.0f, 0.01f);   // starts drawn as it was (the new screen turned back 90)
        CHECK(std::fabs(a.Angle(10.2)) < 90.0f && std::fabs(a.Angle(10.2)) > 0.0f);
        CHECK_NEAR(a.Angle(10.5), 0.0f, 0.001f);
        CHECK(!a.Running(10.5));
        a.Start(Orientation::LandscapeRight, Orientation::Portrait, 0.0, 0.4);   // the short way round: +90 back
        CHECK_NEAR(std::fabs(a.Angle(0.0)), 90.0f, 0.01f);
    }
    {   // Placing the screen and mapping the mouse to the game's touch pixels and back.
        const Box view{100, 50, 800, 600};
        const Placement fit = PlaceScreen(view, 1080, 2400, Zoom::Fit, 100, 1.0f, {}, FrameRoom(view));
        CHECK(fit.screen.x >= view.x && fit.screen.Right() <= view.Right() + 0.01f);
        CHECK(fit.screen.y >= view.y && fit.screen.Bottom() <= view.Bottom() + 0.01f);
        CHECK_NEAR(fit.screen.w / fit.screen.h, 1080.0f / 2400.0f, 1e-3f);
        CHECK_NEAR(fit.scale, fit.screen.h / 2400.0f, 1e-5f);
        CHECK_NEAR(fit.screen.x + fit.screen.w * 0.5f, 500.0f, 0.01f);   // centred
        const Pt mouse{fit.screen.x + fit.screen.w * 0.25f, fit.screen.y + fit.screen.h * 0.5f};
        const Pt g = ViewToGame(fit, mouse);
        CHECK_NEAR(g.x, fit.screen.w * 0.25f, 1e-3f); CHECK_NEAR(g.y, fit.screen.h * 0.5f, 1e-3f);
        const Pt back = GameToView(fit, g);
        CHECK_NEAR(back.x, mouse.x, 1e-3f); CHECK_NEAR(back.y, mouse.y, 1e-3f);
        const Pt dev = ViewToDevice(fit, mouse);
        CHECK_NEAR(dev.x, 270.0f, 0.05f); CHECK_NEAR(dev.y, 1200.0f, 0.05f);   // a quarter across, half down, in device pixels
        const Pt dv = DeviceToView(fit, dev);
        CHECK_NEAR(dv.x, mouse.x, 1e-3f); CHECK_NEAR(dv.y, mouse.y, 1e-3f);
        // 100% on a 2x display: one device pixel per display pixel = half a view unit.
        const Placement act = PlaceScreen(view, 1080, 2400, Zoom::Actual, 100, 2.0f, {}, 0);
        CHECK_NEAR(act.scale, 0.5f, 1e-6f); CHECK_NEAR(act.screen.w, 540.0f, 1e-3f);
        const Placement z = PlaceScreen(view, 1080, 2400, Zoom::Percent, 50, 1.0f, {}, 0);
        CHECK_NEAR(z.scale, 0.5f, 1e-6f);
        // a screen larger than the view pans, but never past its edges
        const Placement big = PlaceScreen(view, 1080, 2400, Zoom::Actual, 100, 1.0f, Pt{0, 100000}, 0);
        CHECK_NEAR(big.screen.y, view.y, 0.01f);
        const Placement big2 = PlaceScreen(view, 1080, 2400, Zoom::Actual, 100, 1.0f, Pt{0, -100000}, 0);
        CHECK_NEAR(big2.screen.Bottom(), view.Bottom(), 0.01f);
        // dpi as the game's pixels go: the phone's inch stays an inch
        const DeviceSpec& p = Dev("pixel-8");
        CHECK_NEAR(GameDpi(p, fit), p.ppi * fit.scale, 1e-3f);
        int rw = 0, rh = 0;
        RenderSize(1080, 2400, fit, 1.0f, true, 4096, rw, rh);
        CHECK(rw == 1080 && rh == 2400);
        RenderSize(1080, 2400, fit, 1.0f, true, 1200, rw, rh);
        CHECK(rh == 1200 && rw == 540);
        RenderSize(1080, 2400, fit, 2.0f, false, 4096, rw, rh);
        CHECK(rw == (int)std::lround(fit.screen.w * 2.0f));
    }
    {   // The Screen API while emulating, and after.
        const DeviceSpec& p = Dev("iphone-16-pro");
        const Box view{0, 0, 1000, 1000};
        int w = 0, h = 0;
        ScreenPixels(p, Orientation::Portrait, w, h);
        const Placement pl = PlaceScreen(view, w, h, Zoom::Fit, 100, 1.0f, {}, 0);
        ScreenReport r = MakeScreenReport(p, Orientation::Portrait, pl, 0.0f, true);
        CHECK_NEAR(r.width, pl.screen.w, 1e-3f); CHECK_NEAR(r.height, pl.screen.h, 1e-3f);
        CHECK_NEAR(r.dpi, 460.0f * pl.scale, 1e-3f);
        CHECK_NEAR(r.safe.top, 186.0f / 2622.0f, 1e-5f); CHECK_NEAR(r.safe.bottom, 102.0f / 2622.0f, 1e-5f);
        CHECK(r.orientation == 0 && r.darkTheme);
        ApplyScreenReport(r);
        CHECK_NEAR(okay::UICanvas::Width(), pl.screen.w, 1e-3f);
        CHECK_NEAR(okay::UICanvas::Dpi(), r.dpi, 1e-3f);
        CHECK(okay::UICanvas::OrientationNow() == 0 && okay::UICanvas::DarkTheme());
        CHECK_NEAR(okay::UISafeInsets().top, r.safe.top, 1e-6f);
        // the keyboard is outside the safe area
        const float kb = KeyboardHeight(pl.screen.w, pl.screen.h, false, 102.0f * pl.scale);
        ScreenReport rk = MakeScreenReport(p, Orientation::Portrait, pl, kb, false);
        CHECK_NEAR(rk.safe.bottom, kb / pl.screen.h, 1e-5f);
        CHECK_NEAR(rk.keyboardHeight, kb, 1e-4f);
        ApplyScreenReport(rk);
        CHECK_NEAR(okay::UICanvas::KeyboardHeight(), kb, 1e-4f);
        okay::UISafeArea sa;
        okay::Vec2 o, sz;
        sa.Resolve(pl.screen.w, pl.screen.h, okay::Vec2{0, 0}, okay::Vec2{pl.screen.w, pl.screen.h}, o, sz);
        CHECK_NEAR(o.y + sz.y, pl.screen.h - kb, 0.01f);   // a Safe Area container ends where the keyboard starts
        // landscape
        ScreenPixels(p, Orientation::LandscapeRight, w, h);
        const Placement ll = PlaceScreen(view, w, h, Zoom::Fit, 100, 1.0f, {}, 0);
        const ScreenReport rl = MakeScreenReport(p, Orientation::LandscapeRight, ll, 0.0f, false);
        CHECK(rl.orientation == 3 && rl.width > rl.height);
        CHECK_NEAR(rl.safe.left, 186.0f / 2622.0f, 1e-5f);
        ResetScreenReport();
        CHECK_NEAR(okay::UICanvas::Dpi(), 96.0f, 1e-6f);
        CHECK(okay::UICanvas::Orientation() == -1 && !okay::UICanvas::DarkTheme() && okay::UICanvas::KeyboardHeight() == 0.0f);
        CHECK(okay::UISafeInsets().top == 0.0f && okay::UISafeInsets().left == 0.0f);
    }
    {   // Touch: one finger, then a second for a pinch, then two-finger pan; lifts; keyboard and off-screen presses.
        TouchEmulator t;
        std::vector<TouchEvent> ev;
        const Box scr{0, 0, 400, 800}, none{};
        t.Update({{100, 100}, false, false, false}, scr, none, ev);
        CHECK(ev.empty() && t.Fingers() == 0);   // hovering is not a touch
        t.Update({{100, 100}, true, false, false}, scr, none, ev);
        CHECK(ev.size() == 1 && ev[0].state == 0 && ev[0].pos.x == 100 && t.Fingers() == 1);
        const long long id0 = ev[0].id;
        ev.clear();
        t.Update({{120, 110}, true, false, false}, scr, none, ev);
        CHECK(ev.size() == 1 && ev[0].state == 1 && ev[0].id == id0 && ev[0].pos.x == 120);
        ev.clear();
        t.Update({{120, 110}, true, true, false}, scr, none, ev);   // the modifier: a second finger, mirrored about the centre
        CHECK(ev.size() == 1 && ev[0].state == 0 && ev[0].id != id0 && t.Fingers() == 2);
        CHECK_NEAR(ev[0].pos.x, 280.0f, 1e-3f); CHECK_NEAR(ev[0].pos.y, 690.0f, 1e-3f);
        const long long id1 = ev[0].id;
        ev.clear();
        t.Update({{150, 150}, true, true, false}, scr, none, ev);   // pinch: both move, mirrored
        CHECK(ev.size() == 2);
        CHECK_NEAR(t.Finger(1).x, 250.0f, 1e-3f); CHECK_NEAR(t.Finger(1).y, 650.0f, 1e-3f);
        ev.clear();
        t.Update({{150, 150}, true, true, true}, scr, none, ev);    // pan: the gap stays as it is
        t.Update({{170, 160}, true, true, true}, scr, none, ev);
        CHECK_NEAR(t.Finger(1).x, 270.0f, 1e-3f); CHECK_NEAR(t.Finger(1).y, 660.0f, 1e-3f);
        ev.clear();
        t.Update({{170, 160}, true, false, false}, scr, none, ev);  // modifier released: the second finger lifts
        CHECK(ev.size() == 1 && ev[0].state == 2 && ev[0].id == id1 && t.Fingers() == 1);
        ev.clear();
        t.Update({{900, 160}, true, false, false}, scr, none, ev);  // dragged off the screen: held at the edge
        CHECK(ev.size() == 1 && ev[0].pos.x <= 400.0f);
        ev.clear();
        t.Update({{170, 160}, false, false, false}, scr, none, ev);
        CHECK(ev.size() == 1 && ev[0].state == 2 && ev[0].id == id0 && t.Fingers() == 0);
        ev.clear();
        t.Update({{500, 100}, true, false, false}, scr, none, ev);  // a press off the screen starts nothing
        CHECK(ev.empty());
        t.Update({{500, 100}, false, false, false}, scr, none, ev);
        const Box kb{0, 600, 400, 200};
        t.Update({{100, 700}, true, false, false}, scr, kb, ev);    // a press on the keyboard is the keyboard's
        CHECK(ev.empty() && t.Fingers() == 0);
        t.Update({{100, 700}, false, false, false}, scr, kb, ev);
        t.Update({{100, 100}, true, true, false}, scr, none, ev);   // both fingers from the first frame
        CHECK(ev.size() == 2 && t.Fingers() == 2);
        ev.clear();
        t.Release(ev);
        CHECK(ev.size() == 2 && ev[0].state == 2 && ev[1].state == 2 && t.Fingers() == 0);
    }
    {   // The keyboard: shrinks the visible area like a phone; the keys hit-test.
        const float w = 390, h = 844;
        const float kh = KeyboardHeight(w, h, false, 34);
        CHECK(kh > h * 0.3f && kh < h * 0.5f);
        const float kl = KeyboardHeight(h, w, false, 21);
        CHECK(kl > w * 0.4f && kl < w * 0.7f);
        CHECK(KeyboardHeight(1024, 1366, true, 20) < 1366 * 0.36f);
        const KeyboardLayout k = LayoutKeyboard(w, h, false, 34, false, false);
        CHECK_NEAR(k.area.Bottom(), h, 0.01f); CHECK_NEAR(k.area.h, kh, 0.01f);
        int letters = 0; bool space = false, back = false, enter = false;
        for (const KeyCap& c : k.keys) {
            CHECK(c.box.x >= k.area.x - 0.01f && c.box.Right() <= k.area.Right() + 0.01f);
            CHECK(c.box.y >= k.area.y - 0.01f && c.box.Bottom() <= h - 34 + 0.01f);
            if (c.kind == KeyKind::Char && c.ch >= 'a' && c.ch <= 'z') ++letters;
            space |= c.kind == KeyKind::Space; back |= c.kind == KeyKind::Backspace; enter |= c.kind == KeyKind::Enter;
        }
        CHECK(letters == 26 && space && back && enter);
        for (const KeyCap& c : k.keys) {
            const KeyCap* hit = KeyAt(k, Pt{c.box.x + c.box.w * 0.5f, c.box.y + c.box.h * 0.5f});
            CHECK(hit == &c);
        }
        CHECK(KeyAt(k, Pt{10, 10}) == nullptr);
        const KeyboardLayout ks = LayoutKeyboard(w, h, false, 34, true, false);
        bool capQ = false; for (const KeyCap& c : ks.keys) capQ |= c.ch == 'Q';
        CHECK(capQ);
        const KeyboardLayout kd = LayoutKeyboard(w, h, false, 34, false, true);
        bool seven = false; for (const KeyCap& c : kd.keys) seven |= c.ch == '7';
        CHECK(seven);
    }
    {   // The motion sensor and auto-rotate.
        Vec3f g = TiltToGravity(0, 0);
        CHECK_NEAR(g.x, 0, 1e-5f); CHECK_NEAR(g.y, -1, 1e-5f); CHECK_NEAR(g.z, 0, 1e-5f);
        g = TiltToGravity(90, 0);
        CHECK_NEAR(g.z, -1, 1e-5f); CHECK_NEAR(g.y, 0, 1e-5f);
        g = TiltToGravity(30, 40);
        CHECK_NEAR(g.x * g.x + g.y * g.y + g.z * g.z, 1.0f, 1e-4f);
        CHECK(TiltToGravity(0, 30).x > 0);   // right side down
        CHECK(OrientationForGravity(TiltToGravity(0, 90), Orientation::Portrait, false) == Orientation::LandscapeRight);
        CHECK(OrientationForGravity(TiltToGravity(0, -90), Orientation::Portrait, false) == Orientation::LandscapeLeft);
        CHECK(OrientationForGravity(TiltToGravity(0, 180), Orientation::LandscapeLeft, false) == Orientation::LandscapeLeft);
        CHECK(OrientationForGravity(TiltToGravity(0, 180), Orientation::LandscapeLeft, true) == Orientation::PortraitUpsideDown);
        CHECK(OrientationForGravity(TiltToGravity(85, 90), Orientation::Portrait, false) == Orientation::Portrait);   // nearly flat
        CHECK(OrientationForGravity(TiltToGravity(0, 45), Orientation::Portrait, false) == Orientation::Portrait);    // between two
    }
    {   // Conditions.
        CHECK(!NetworkValues(Network::Online).enabled);
        CHECK(NetworkValues(Network::Slow).enabled && NetworkValues(Network::Slow).latencyMs >= 100);
        CHECK(NetworkValues(Network::Offline).loss == 1.0f);
        CHECK(EffectiveFpsCap(0, 30, true, true) == 30);
        CHECK(EffectiveFpsCap(20, 30, true, true) == 20);
        CHECK(EffectiveFpsCap(60, 30, true, true) == 30);
        CHECK(EffectiveFpsCap(60, 30, false, true) == 60);   // only while playing
        CHECK(EffectiveFpsCap(60, 30, true, false) == 60);   // only on a device
        CHECK(EffectiveFpsCap(0, 0, true, true) == 0);
    }
    {   // Several devices.
        const Box v{0, 0, 1200, 600};
        CHECK(SplitView(v, 1, 8).size() == 1);
        const auto two = SplitView(v, 2, 8);
        CHECK(two.size() == 2 && two[0].Right() <= two[1].x && std::fabs(two[0].w - two[1].w) < 0.01f);
        const auto four = SplitView(Box{0, 0, 600, 1200}, 4, 8);
        CHECK(four.size() == 4 && four[0].y == four[1].y && four[2].y > four[0].y);   // 2 x 2 in a tall view
        CHECK(SplitView(v, 4, 8)[3].Right() <= v.Right() + 0.01f);
        CHECK(SplitView(v, 9, 8).size() == 4 && SplitView(v, 0, 8).size() == 1);
    }
    {   // Screenshots.
        CHECK(ScreenshotName("iPhone 16 Pro", Orientation::Portrait, 2026, 11, 27, 15, 45, 2) == "iPhone_16_Pro_portrait_20261127_154502.png");
        CHECK(ScreenshotName("iPad Pro 12.9\"", Orientation::LandscapeLeft, 2026, 1, 2, 3, 4, 5) == "iPad_Pro_12_9_landscape_20260102_030405.png");
        std::vector<std::uint8_t> px(4 * 4 * 4);
        for (int i = 0; i < 16; ++i) px[(std::size_t)i * 4] = (std::uint8_t)i;
        int ow = 0, oh = 0;
        const auto c = CropRgba(px, 4, 4, 1, 1, 2, 2, ow, oh);
        CHECK(ow == 2 && oh == 2 && c.size() == 16 && c[0] == 5 && c[4] == 6 && c[8] == 9 && c[12] == 10);
        const auto e = CropRgba(px, 4, 4, 3, 3, 5, 5, ow, oh);
        CHECK(ow == 1 && oh == 1 && e.size() == 4 && e[0] == 15);
        CHECK(CropRgba(px, 4, 4, 9, 9, 2, 2, ow, oh).empty() && ow == 0);
    }
    {   // The status bar clock.
        CHECK(StatusClock(9, 41, Platform::Ios) == "9:41");
        CHECK(StatusClock(21, 5, Platform::Ios) == "9:05");
        CHECK(StatusClock(9, 41, Platform::Android) == "09:41");
        CHECK(StatusClock(0, 0, Platform::Ios) == "12:00");
    }
    {   // Settings: round trip, the custom device, the old keys.
        Settings s;
        s.device = "pixel-9"; s.orientation = Orientation::LandscapeRight; s.zoom = Zoom::Percent; s.zoomPercent = 75;
        s.safeArea = false; s.darkTheme = true; s.fpsCap = 30; s.network = Network::Slow; s.rotationLock = false;
        s.tiltPitch = 20; s.tiltRoll = -15; s.extra = {"iphone-se", "ipad-mini"}; s.battery = 42;
        s.custom.name = "My phone"; s.custom.width = 900; s.custom.cutout = Cutout::Notch;
        Settings r;
        ParseSettings(WriteSettings(s), r);
        CHECK(r.device == "pixel-9" && r.orientation == Orientation::LandscapeRight && r.zoom == Zoom::Percent);
        CHECK_NEAR(r.zoomPercent, 75, 1e-4f);
        CHECK(!r.safeArea && r.darkTheme && r.fpsCap == 30 && r.network == Network::Slow && !r.rotationLock);
        CHECK_NEAR(r.tiltPitch, 20, 1e-4f); CHECK_NEAR(r.tiltRoll, -15, 1e-4f);
        CHECK(r.extra.size() == 2 && r.extra[1] == "ipad-mini" && r.battery == 42);
        CHECK(r.custom.name == "My phone" && r.custom.width == 900 && r.custom.cutout == Cutout::Notch);
        CHECK(FindDevice("custom", &r.custom) == &r.custom);
        Settings bad;
        ParseSettings("zoom_percent=banana\nbattery=900\nextra=a,b,c,d,e\nnot_a_key=1\n", bad);
        CHECK_NEAR(bad.zoomPercent, 100, 1e-4f);
        CHECK(bad.battery == 100 && (int)bad.extra.size() == kMaxExtraDevices);
        Settings old;
        CHECK(ApplyLegacySetting(old, "gamedevice2", 7) && old.device == "iphone-se");
        CHECK(ApplyLegacySetting(old, "gamedeviceland", 1) && old.orientation == Orientation::LandscapeLeft);
        CHECK(ApplyLegacySetting(old, "gamesafearea", 0) && !old.safeArea);
        CHECK(ApplyLegacySetting(old, "gamedevice", 1) && old.device == LegacyDeviceId(3));
        CHECK(ApplyLegacySetting(old, "gamedevice2", 0) && old.device.empty());
        CHECK(!ApplyLegacySetting(old, "gamestats", 1));
        DeviceSpec c = DefaultCustomDevice();
        c.width = 5; c.ppi = 0; c.topPt = 1e6f;
        ClampCustomDevice(c);
        CHECK(c.width >= 64 && c.ppi >= 50 && c.topPt * c.scale <= c.height * 0.5f);
    }

    TEST_MAIN_RESULT();
}
