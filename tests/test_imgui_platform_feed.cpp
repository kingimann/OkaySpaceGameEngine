// The own Dear ImGui platform feed (shared/ImGuiPlatformFeed.hpp): the key table, event translation, and a real ImGui
// context fed through a fake host (no window, no SDL). Expected values come from the USB HID usage table (the seam's key
// numbers) and Dear ImGui's public header.
#include "ImGuiPlatformFeed.hpp"
#include <cstdio>
#include <set>

using namespace okay::platform;
namespace f = okay::imguifeed;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Event Ev(EventType t, uint32_t win = 1) { Event e; e.type = t; e.windowId = win; return e; }

static void KeyTable() {
    CHECK(f::KeyToImGui(Key::Unknown) == ImGuiKey_None);
    CHECK(f::KeyToImGui(Key::A) == ImGuiKey_A && f::KeyToImGui(Key::Z) == ImGuiKey_Z);
    CHECK(f::KeyToImGui(Key::N1) == ImGuiKey_1 && f::KeyToImGui(Key::N9) == ImGuiKey_9 && f::KeyToImGui(Key::N0) == ImGuiKey_0);
    CHECK(f::KeyToImGui(Key::F1) == ImGuiKey_F1 && f::KeyToImGui(Key::F12) == ImGuiKey_F12);
    CHECK(f::KeyToImGui((Key)104) == ImGuiKey_F13 && f::KeyToImGui((Key)115) == ImGuiKey_F24);
    CHECK(f::KeyToImGui(Key::Return) == ImGuiKey_Enter && f::KeyToImGui(Key::KpEnter) == ImGuiKey_KeypadEnter);
    CHECK(f::KeyToImGui(Key::Backspace) == ImGuiKey_Backspace && f::KeyToImGui(Key::Delete) == ImGuiKey_Delete);
    CHECK(f::KeyToImGui(Key::Left) == ImGuiKey_LeftArrow && f::KeyToImGui(Key::Up) == ImGuiKey_UpArrow);
    CHECK(f::KeyToImGui(Key::Grave) == ImGuiKey_GraveAccent && f::KeyToImGui(Key::Equals) == ImGuiKey_Equal);
    CHECK(f::KeyToImGui(Key::Kp1) == ImGuiKey_Keypad1 && f::KeyToImGui(Key::Kp9) == ImGuiKey_Keypad9 && f::KeyToImGui(Key::Kp0) == ImGuiKey_Keypad0);
    CHECK(f::KeyToImGui((Key)103) == ImGuiKey_KeypadEqual);
    CHECK(f::KeyToImGui(Key::LCtrl) == ImGuiKey_LeftCtrl && f::KeyToImGui(Key::RGui) == ImGuiKey_RightSuper);
    CHECK(f::KeyToImGui(Key::Application) == ImGuiKey_Menu);
    CHECK(f::KeyToImGui((Key)511) == ImGuiKey_None);
    // Every mapped key maps to its own ImGuiKey (no two keys share one), and the mapped count is what the table lists.
    std::set<int> seen; int mapped = 0; bool dup = false;
    for (int v = 0; v < kKeyCount; ++v) {
        const ImGuiKey k = f::KeyToImGui((Key)v);
        if (k == ImGuiKey_None) continue;
        ++mapped; if (!seen.insert((int)k).second) dup = true;
    }
    CHECK(!dup);
    CHECK(mapped == 26 + 9 + 12 + 12 + 9 + 18 + 9 + 4 + 6 + 2 + 2 + 1 + 8);   // letters, 1-9, F1-F12, F13-F24, Kp1-9, then 0 and punctuation (18), PrintScreen..PageDown (9), arrows, NumLock and keypad ops (6), Kp0 and Kp., NonUs and Menu, Kp=, 8 modifiers
    CHECK(f::MouseButtonToImGui(MouseLeft) == 0 && f::MouseButtonToImGui(MouseRight) == 1 && f::MouseButtonToImGui(MouseMiddle) == 2);
    CHECK(f::MouseButtonToImGui(MouseX1) == 3 && f::MouseButtonToImGui(MouseX2) == 4 && f::MouseButtonToImGui(0) == -1 && f::MouseButtonToImGui(9) == -1);
    CursorShape s;
    CHECK(f::CursorToSeam(ImGuiMouseCursor_TextInput, s) && s == CursorShape::TextInput);
    CHECK(f::CursorToSeam(ImGuiMouseCursor_ResizeNWSE, s) && s == CursorShape::ResizeNWSE);
    CHECK(!f::CursorToSeam(ImGuiMouseCursor_None, s));
    CHECK(f::OwnRequested("own") && !f::OwnRequested("sdl2") && !f::OwnRequested("") && !f::OwnRequested(nullptr) && !f::OwnRequested("OWN"));
}

static void Translation() {
    std::vector<f::Action> a;
    Event k = Ev(EventType::KeyDown); k.key = Key::Space;
    f::Translate(k, 1, a);
    CHECK(a.size() == 1 && a[0].kind == f::Action::Key && a[0].key == ImGuiKey_Space && a[0].down);
    a.clear(); k.type = EventType::KeyUp; f::Translate(k, 1, a);
    CHECK(a.size() == 1 && !a[0].down);
    a.clear(); k.type = EventType::KeyDown; k.repeat = true; f::Translate(k, 1, a);
    CHECK(a.empty());                               // ImGui repeats keys itself
    a.clear(); k.repeat = false; k.key = Key::Unknown; f::Translate(k, 1, a);
    CHECK(a.empty());
    a.clear(); k.key = Key::A; f::Translate(k, 2, a);
    CHECK(a.empty());                               // another window's key
    a.clear(); f::Translate(k, 0, a);
    CHECK(a.size() == 1);                           // window 0 = every window

    a.clear(); Event t = Ev(EventType::TextInput); t.text = "h\xC3\xA9";
    f::Translate(t, 1, a);
    CHECK(a.size() == 1 && a[0].kind == f::Action::Text && a[0].text == t.text);
    a.clear(); t.text.clear(); f::Translate(t, 1, a);
    CHECK(a.empty());

    a.clear(); Event m = Ev(EventType::MouseMove); m.x = 12; m.y = 34;
    f::Translate(m, 1, a);
    CHECK(a.size() == 1 && a[0].kind == f::Action::MousePos && a[0].x == 12.0f && a[0].y == 34.0f);

    a.clear(); Event b = Ev(EventType::MouseButtonDown); b.button = MouseRight; b.x = 5; b.y = 6;
    f::Translate(b, 1, a);
    CHECK(a.size() == 2 && a[0].kind == f::Action::MousePos && a[1].kind == f::Action::MouseButton && a[1].button == 1 && a[1].down);
    a.clear(); b.type = EventType::MouseButtonUp; b.button = 9; f::Translate(b, 1, a);
    CHECK(a.empty());

    a.clear(); Event w = Ev(EventType::MouseWheel); w.wheelX = -1; w.wheelY = 2;
    f::Translate(w, 1, a);
    CHECK(a.size() == 1 && a[0].kind == f::Action::Wheel && a[0].x == -1.0f && a[0].y == 2.0f);

    a.clear(); f::Translate(Ev(EventType::WindowFocusLost), 1, a);
    CHECK(a.size() == 1 && a[0].kind == f::Action::Focus && !a[0].down);
    a.clear(); f::Translate(Ev(EventType::WindowFocusGained), 1, a);
    CHECK(a.size() == 1 && a[0].down);

    for (EventType e : {EventType::Quit, EventType::WindowClose, EventType::WindowResized, EventType::WindowExposed, EventType::DropFile,
                        EventType::GamepadAdded, EventType::GamepadRemoved, EventType::FingerDown, EventType::TextEditing,
                        EventType::WindowDpiChanged, EventType::NoEvent}) {
        a.clear(); f::Translate(Ev(e), 1, a); CHECK(a.empty());
    }
}

struct Fake {
    int w = 800, h = 600, dw = 1600, dh = 1200;
    bool mouseIn = true, minimized = false;
    double now = 10.0;
    std::string clip;
    int shapes = 0; CursorShape lastShape = CursorShape::Arrow;
    int hides = 0, shows = 0, warps = 0, imeRects = 0;
    f::FeedHost Host() {
        f::FeedHost h2;
        h2.windowSize = [this](int& x, int& y) { x = w; y = h; };
        h2.drawableSize = [this](int& x, int& y) { x = dw; y = dh; };
        h2.hasMouseFocus = [this]() { return mouseIn; };
        h2.isMinimized = [this]() { return minimized; };
        h2.nowSeconds = [this]() { return now; };
        h2.setCursorShape = [this](CursorShape s) { ++shapes; lastShape = s; return true; };
        h2.showCursor = [this](bool on) { if (on) ++shows; else ++hides; };
        h2.getClipboard = [this]() { return clip; };
        h2.setClipboard = [this](const std::string& t) { clip = t; return true; };
        h2.warpMouse = [this](int, int) { ++warps; };
        h2.setTextInputRect = [this](int, int, int, int) { ++imeRects; };
        return h2;
    }
};

static float g_wheelSeen = 0;   // io.MouseWheel as the frame saw it (ImGui's own scrolling uses it up during the frame)
static void OneFrame(f::Feed& feed, ImGuiIO& io, const char* label = "x") {
    feed.NewFrame(io);
    ImGui::NewFrame();
    if (io.MouseWheel != 0.0f) g_wheelSeen = io.MouseWheel;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(300, 200));
    ImGui::Begin(label);
    ImGui::End();
    ImGui::Render();
}

static void Context() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    unsigned char* px; int fw, fh; io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
    Fake fk; f::Feed feed;
    feed.Init(io, 1, fk.Host());
    CHECK((io.BackendFlags & ImGuiBackendFlags_HasMouseCursors) && (io.BackendFlags & ImGuiBackendFlags_HasSetMousePos));
    CHECK(!(io.BackendFlags & ImGuiBackendFlags_HasGamepad) && !(io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports));

    OneFrame(feed, io);
    CHECK(io.DisplaySize.x == 800 && io.DisplaySize.y == 600);
    CHECK(io.DisplayFramebufferScale.x == 2.0f && io.DisplayFramebufferScale.y == 2.0f);
    CHECK(io.DeltaTime > 0);
    fk.now = 10.5; OneFrame(feed, io);
    CHECK(io.DeltaTime > 0.49f && io.DeltaTime < 0.51f);
    fk.now = 10.5; OneFrame(feed, io);     // a frame with no time passing must not hand ImGui a zero
    CHECK(io.DeltaTime > 0);
    fk.minimized = true; OneFrame(feed, io);
    CHECK(io.DisplaySize.x == 0 && io.DisplaySize.y == 0);
    fk.minimized = false; fk.w = 640; fk.h = 480; fk.dw = 640; fk.dh = 480; OneFrame(feed, io);
    CHECK(io.DisplaySize.x == 640 && io.DisplayFramebufferScale.x == 1.0f);

    // keys, modifiers, text, mouse
    Event e = Ev(EventType::KeyDown); e.key = Key::LCtrl;
    CHECK(feed.ProcessEvent(e, io));
    e.key = Key::C; feed.ProcessEvent(e, io);
    OneFrame(feed, io);
    OneFrame(feed, io);   // Dear ImGui spreads a modifier change and a key over frames when they would otherwise mix
    CHECK(ImGui::IsKeyDown(ImGuiKey_C) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && io.KeyCtrl);
    // two Ctrl keys: releasing one keeps Ctrl held, releasing both clears it
    e.key = Key::RCtrl; e.type = EventType::KeyDown; feed.ProcessEvent(e, io);
    e.type = EventType::KeyUp; e.key = Key::C; feed.ProcessEvent(e, io); e.key = Key::LCtrl; feed.ProcessEvent(e, io);
    OneFrame(feed, io); OneFrame(feed, io);
    CHECK(!ImGui::IsKeyDown(ImGuiKey_C) && io.KeyCtrl);
    e.key = Key::RCtrl; feed.ProcessEvent(e, io);
    OneFrame(feed, io); OneFrame(feed, io);
    CHECK(!io.KeyCtrl);
    e.type = EventType::KeyDown; e.key = Key::LShift; feed.ProcessEvent(e, io); e.key = Key::LAlt; feed.ProcessEvent(e, io); e.key = Key::LGui; feed.ProcessEvent(e, io);
    OneFrame(feed, io); OneFrame(feed, io); OneFrame(feed, io);
    CHECK(io.KeyShift && io.KeyAlt && io.KeySuper);
    feed.ProcessEvent(Ev(EventType::WindowFocusLost), io); OneFrame(feed, io); feed.ProcessEvent(Ev(EventType::WindowFocusGained), io);
    OneFrame(feed, io); OneFrame(feed, io);
    CHECK(!io.KeyShift && !io.KeyAlt && !io.KeySuper);

    Event t = Ev(EventType::TextInput); t.text = "ab";
    feed.ProcessEvent(t, io);
    CHECK(io.InputQueueCharacters.Size == 0);       // queued as an event; the frame turns it into characters
    OneFrame(feed, io);

    Event mv = Ev(EventType::MouseMove); mv.x = 50; mv.y = 60;
    feed.ProcessEvent(mv, io);
    Event bd = Ev(EventType::MouseButtonDown); bd.button = MouseLeft; bd.x = 50; bd.y = 60;
    feed.ProcessEvent(bd, io);
    OneFrame(feed, io);
    CHECK(io.MousePos.x == 50 && io.MousePos.y == 60 && io.MouseDown[0]);
    Event wh = Ev(EventType::MouseWheel); wh.wheelY = 3;
    feed.ProcessEvent(wh, io);
    OneFrame(feed, io);
    CHECK(g_wheelSeen == 3.0f);
    Event other = Ev(EventType::MouseMove, 7); other.x = 1; other.y = 1;
    CHECK(!feed.ProcessEvent(other, io));           // not this window
    bd.type = EventType::MouseButtonUp; feed.ProcessEvent(bd, io); OneFrame(feed, io);
    CHECK(!io.MouseDown[0]);

    // pointer leaves the window: no mouse; comes back: position again
    fk.mouseIn = false; OneFrame(feed, io); OneFrame(feed, io);
    CHECK(!ImGui::IsMousePosValid(&io.MousePos));
    fk.mouseIn = true; mv.x = 20; mv.y = 21; feed.ProcessEvent(mv, io); OneFrame(feed, io);
    CHECK(io.MousePos.x == 20 && ImGui::IsMousePosValid(&io.MousePos));
    // a held button keeps the pointer (a drag out of the window)
    bd.type = EventType::MouseButtonDown; feed.ProcessEvent(bd, io); OneFrame(feed, io);
    fk.mouseIn = false; OneFrame(feed, io);
    CHECK(ImGui::IsMousePosValid(&io.MousePos));
    bd.type = EventType::MouseButtonUp; feed.ProcessEvent(bd, io); OneFrame(feed, io);

    // focus loss releases held keys
    e = Ev(EventType::KeyDown); e.key = Key::W; feed.ProcessEvent(e, io); OneFrame(feed, io);
    CHECK(ImGui::IsKeyDown(ImGuiKey_W));
    feed.ProcessEvent(Ev(EventType::WindowFocusLost), io); OneFrame(feed, io);
    CHECK(!ImGui::IsKeyDown(ImGuiKey_W));
    feed.ProcessEvent(Ev(EventType::WindowFocusGained), io);

    // clipboard through the host
    ImGui::SetClipboardText("hello");
    CHECK(fk.clip == "hello");
    fk.clip = "from outside";
    const char* got = ImGui::GetClipboardText();
    CHECK(got && std::string(got) == "from outside");
    fk.clip.clear();
    got = ImGui::GetClipboardText();
    CHECK(!got || *got == 0);

    // cursor: the shape follows ImGui; None hides; leaving None shows again
    fk.shapes = 0; fk.hides = 0; fk.shows = 0;
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    feed.NewFrame(io);
    CHECK(fk.lastShape == CursorShape::TextInput && fk.shapes >= 1);
    const int before = fk.shapes;
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    feed.NewFrame(io);
    CHECK(fk.shapes == before);                      // unchanged shape: no call
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    feed.NewFrame(io);
    CHECK(fk.hides == 1);
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    feed.NewFrame(io);
    CHECK(fk.shows == 1 && fk.lastShape == CursorShape::Hand);

    // DPI event remembered for this window only
    Event d = Ev(EventType::WindowDpiChanged, 1); d.x = 144;
    feed.ProcessEvent(d, io);
    CHECK(feed.Dpi() == 144.0f);
    d.windowId = 5; d.x = 192; feed.ProcessEvent(d, io);
    CHECK(feed.Dpi() == 144.0f);

    feed.Shutdown(io);
    CHECK(!(io.BackendFlags & ImGuiBackendFlags_HasMouseCursors) && io.BackendPlatformName == nullptr);
    ImGui::DestroyContext();
}

int main() {
    KeyTable();
    Translation();
    Context();
    std::printf("imgui_platform_feed: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
