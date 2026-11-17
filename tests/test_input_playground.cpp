// The shipped example scene examples/input/input_playground.okayscene, run headless: the scene loads,
// every station's script compiles and shows the right words for the input it is fed, and a picture
// of it is rendered with the software painter (set OKAY_SHOT_DIR to keep it; this is NOT a real window).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Graphics/Image.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace okay;

namespace {
// A controller with raw inputs for the Controller station.
struct StationPad final : platform::Gamepad {
    std::int16_t ax[6] = {}; std::uint8_t hat = 0;
    int id = 77; std::string name = "DualSense Wireless Controller"; std::uint16_t vid = 0x054C, pid = 0x0CE6; int type = 7; bool mapped = true;
    int InstanceId() const override { return id; }
    const char* Name() const override { return name.c_str(); }
    bool HasBuiltinMapping() const override { return mapped; }
    std::int16_t Axis(platform::GamepadAxis) const override { return 0; }
    bool Button(platform::GamepadButton) const override { return false; }
    bool Rumble(std::uint16_t, std::uint16_t, std::uint32_t) override { return true; }
    std::uint16_t VendorId() const override { return vid; }
    std::uint16_t ProductId() const override { return pid; }
    int ControllerType() const override { return type; }
    int RawAxisCount() const override { return 6; }
    int RawButtonCount() const override { return 13; }
    int RawHatCount() const override { return 1; }
    std::int16_t RawAxis(int i) const override { return ax[i]; }
    bool RawButton(int) const override { return false; }
    std::uint8_t RawHat(int) const override { return hat; }
    int PowerLevel() const override { return 3; }
};
// A painter that keeps the pixels, so a test can save what the game would draw.
class MemoryPainter : public ui2d::Painter {
public:
    explicit MemoryPainter(Image& image) : m_image(image) {}
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override {
        m_r = r; m_g = g; m_b = b; m_a = a;
    }
    void FillRect(const ui2d::Rect* r) override {
        if (!r || r->w <= 0 || r->h <= 0) return;
        for (int y = r->y; y < r->y + r->h; ++y)
            for (int x = r->x; x < r->x + r->w; ++x) Plot(x, y);
    }
    void DrawRect(const ui2d::Rect* r) override {
        if (!r) return;
        DrawLine(r->x, r->y, r->x + r->w - 1, r->y);
        DrawLine(r->x, r->y, r->x, r->y + r->h - 1);
        DrawLine(r->x + r->w - 1, r->y, r->x + r->w - 1, r->y + r->h - 1);
        DrawLine(r->x, r->y + r->h - 1, r->x + r->w - 1, r->y + r->h - 1);
    }
    void DrawLine(int x0, int y0, int x1, int y1) override {
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            Plot(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void DrawPoint(int x, int y) override { Plot(x, y); }
    void SetClipRect(const ui2d::Rect* r) override { m_clipOn = r != nullptr; if (r) m_clip = *r; }
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = m_clip; }
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override { m_sx = sx > 0 ? sx : 1; m_sy = sy > 0 ? sy : 1; }
    ui2d::Tex LoadTexture(const std::string&) override { return nullptr; }
    ui2d::Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(ui2d::Tex, int* w, int* h) override { if (w) *w = 0; if (h) *h = 0; }
    void SetTextureColorMod(ui2d::Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(ui2d::Tex, std::uint8_t) override {}
    void Copy(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*) override {}
    void CopyF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*) override {}
    void CopyEx(ui2d::Tex, const ui2d::Rect*, const ui2d::Rect*, double, const ui2d::Point*, int) override {}
    void CopyExF(ui2d::Tex, const ui2d::Rect*, const ui2d::FRect*, double, const ui2d::FPoint*, int) override {}
    void Geometry(ui2d::Tex, const ui2d::Vertex*, int, const int*, int) override {}

private:
    void Plot(int x, int y) {
        x = (int)std::lround((double)x * m_sx);
        y = (int)std::lround((double)y * m_sy);
        if (x < 0 || y < 0 || x >= m_image.Width() || y >= m_image.Height()) return;
        if (m_clipOn && (x < m_clip.x || y < m_clip.y || x >= m_clip.x + m_clip.w || y >= m_clip.y + m_clip.h)) return;
        const float a = m_a / 255.0f;
        const std::size_t i = (static_cast<std::size_t>(y) * m_image.Width() + x) * 4;
        std::uint8_t* p = m_image.Data() + i;
        p[0] = (std::uint8_t)(m_r * a + p[0] * (1.0f - a));
        p[1] = (std::uint8_t)(m_g * a + p[1] * (1.0f - a));
        p[2] = (std::uint8_t)(m_b * a + p[2] * (1.0f - a));
        p[3] = 255;
    }
    Image& m_image;
    std::uint8_t m_r = 255, m_g = 255, m_b = 255, m_a = 255;
    bool m_clipOn = false;
    ui2d::Rect m_clip{};
    float m_sx = 1, m_sy = 1;
};

// Software render of a scene's screen-space text (NOT a real window) to `<OKAY_SHOT_DIR>/<file>` when that variable is set.
bool ShotTo(Scene& scene, const std::string& file) {
    const char* dir = std::getenv("OKAY_SHOT_DIR");
    if (!dir) return true;
    UICanvas::Set(1280, 720);
    scene.uiFont = "pixel";
    scene.Update(1.0f / 60.0f);
    Image image(1280, 720);
    image.Fill(Color{0.07f, 0.08f, 0.12f, 1.0f});
    MemoryPainter painter(image);
    ui2d::UIRenderOptions opt;
    opt.interactive = false;
    ui2d::RenderSceneUI(scene, painter, 1280, 720, opt);
    return image.SavePNG(std::string(dir) + "/" + file);
}
void AddText(Scene& s, const char* name, float x, float y, const std::string& text, float px = 1.5f, Color c = Color{0.85f, 0.9f, 1.0f, 1.0f}) {
    GameObject* g = s.CreateGameObject(name);
    auto* t = g->AddComponent<TextRenderer>();
    t->screenSpace = true; t->screenPos = {x, y}; t->size = {1200, 400}; t->vcenter = false; t->pixelSize = px; t->color = c; t->text = text;
}
std::string Pad(std::string s, size_t n) { if (s.size() < n) s.append(n - s.size(), ' '); return s; }

std::string Read(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
std::string TextOf(Scene& s, const char* name) {
    GameObject* g = s.Find(name);
    auto* t = g ? g->GetComponent<TextRenderer>() : nullptr;
    return t ? t->text : std::string("<missing ") + name + ">";
}
bool Has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
void Step(Scene& s, std::vector<char> keys, unsigned mouse = 0, Vec2 mp = {400, 300}, float dt = 1.0f / 60.0f) {
    Input::ClearTypedText();
    Input::FeedKeys(keys);
    Input::FeedMouse(mp, mouse);
    InputActions::Tick(dt);
    s.Update(dt);
}
} // namespace

int main() {
    RUN_SUITE("input_playground");
    Prefs::Clear();
    InputActions::Restore();
    Accessibility::Reset();

    Scene scene("playground");
    scene.physicsEnabled = false;
    std::string err;
    const std::string text = Read(std::string(OKAY_SOURCE_DIR) + "/examples/input/input_playground.okayscene");
    CHECK(!text.empty());
    CHECK(SceneSerializer::Deserialize(scene, text, &err));
    if (!err.empty()) std::cerr << "  load: " << err << "\n";
    for (const char* n : {"Setup", "A Devices", "B Timing", "C Chords", "D Curve", "E Rebind", "F Players", "G Record", "H Controller"}) {
        GameObject* g = scene.Find(n);
        CHECK(g != nullptr);
        auto* sc = g ? g->GetComponent<ScriptComponent>() : nullptr;
        CHECK(sc != nullptr);
        if (sc) {
            std::string serr;
            CHECK(sc->LoadSource(sc->Source(), &serr));
            if (!serr.empty()) std::cerr << "  " << n << " script: " << serr << "\n";
        }
    }
    scene.Start();
    Step(scene, {});
    CHECK(InputActions::Has("undo") && InputActions::Has("nextweapon"));         // Setup defined them

    // A: devices
    Step(scene, {'w'});
    CHECK(Has(TextOf(scene, "A Devices"), "last used: keyboard"));
    CHECK(Has(TextOf(scene, "A Devices"), "Move 0.00,1.00"));
    CHECK(Has(TextOf(scene, "A Devices"), "Jump button: Space"));
    Input::FeedGamepadRaw(0, {}, {}, 0, 0, 1u << Input::PadA, true); InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
    CHECK(Has(TextOf(scene, "A Devices"), "last used: gamepad"));
    Input::SetGamepadFamily(0, Input::PadFamily::PlayStation);
    Input::FeedGamepadRaw(0, {}, {}, 0, 0, 1u << Input::PadA, true); InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
    CHECK(Has(TextOf(scene, "A Devices"), "Jump button: Cross"));
    Input::FeedGamepadRaw(0, {}, {}, 0, 0, 0, false);
    Input::SetGamepadFamily(0, Input::PadFamily::Xbox);

    // B: buffering, double tap, hold to confirm
    Step(scene, {' '});
    for (int i = 0; i < 6; ++i) Step(scene, {});
    CHECK(Has(TextOf(scene, "B Timing"), "Jump remembered (0.3 s): true"));
    Step(scene, {'c'});
    CHECK(Has(TextOf(scene, "B Timing"), "Jump remembered (0.3 s): false"));
    Step(scene, {'e'}); Step(scene, {}); Step(scene, {'e'});
    CHECK(Has(TextOf(scene, "B Timing"), "DOUBLE TAP!"));
    for (int i = 0; i < 70; ++i) Step(scene, {'r'});
    CHECK(Has(TextOf(scene, "B Timing"), "100%") && Has(TextOf(scene, "B Timing"), "CONFIRMED"));
    Step(scene, {});

    // C: chord does not also crouch
    Step(scene, {Input::KeyCtrl, 'z'});
    CHECK(Has(TextOf(scene, "C Chords"), "Undo (Ctrl+Z): true") && Has(TextOf(scene, "C Chords"), "Crouch (Ctrl alone): false"));
    Step(scene, {Input::KeyCtrl});
    CHECK(Has(TextOf(scene, "C Chords"), "Undo (Ctrl+Z): false") && Has(TextOf(scene, "C Chords"), "Crouch (Ctrl alone): true"));
    Input::ClearTypedText(); Input::FeedMouseWheel(1.0f); InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
    CHECK(Has(TextOf(scene, "C Chords"), "Mouse wheel up: true"));

    // D: curve
    CHECK(Has(TextOf(scene, "D Curve"), "curve 1"));
    Step(scene, {'4'}); Step(scene, {});
    CHECK(Has(TextOf(scene, "D Curve"), "curve 2"));
    CHECK(InputActions::GetResponse("look").curve == 2.0f);

    // E: rebinding with conflict warning, reset
    Step(scene, {'6'}); Step(scene, {});
    CHECK(Has(TextOf(scene, "E Rebind"), "Jump = E") && Has(TextOf(scene, "E Rebind"), "Interact = Space"));
    CHECK(Has(TextOf(scene, "E Rebind"), "E was also bound to: Interact"));
    Step(scene, {'7'}); Step(scene, {});
    CHECK(Has(TextOf(scene, "E Rebind"), "Jump = Space") && Has(TextOf(scene, "E Rebind"), "Interact = E"));
    Step(scene, {'8'}); Step(scene, {});
    CHECK(Has(TextOf(scene, "E Rebind"), "Fire = F"));
    Step(scene, {'7'}); Step(scene, {});
    CHECK(Has(TextOf(scene, "E Rebind"), "Fire = Left Mouse"));

    // F: two players on two controllers
    Input::FeedGamepadRaw(1, {0.0f, 1.0f}, {}, 0, 0, 1u << Input::PadA, true);
    InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
    CHECK(Has(TextOf(scene, "F Players"), "P1 jump false") && Has(TextOf(scene, "F Players"), "P2 jump true"));
    CHECK(Has(TextOf(scene, "F Players"), "P2 jump true move 0.0,1.0"));
    Input::FeedGamepadRaw(1, {}, {}, 0, 0, 0, false);
    {   // H shakes controller 1 in a heartbeat; 0 switches rumble off
        float lo = 0, hi = 0, sec = 0;
        Step(scene, {'h'});
        CHECK(Input::TakeRumble(lo, hi, sec, 0) && lo > 0.5f);
        CHECK(Has(TextOf(scene, "F Players"), "Rumble strength 1"));
        InputHaptics::Stop(0);
        Step(scene, {'0'}); Step(scene, {});
        CHECK(Has(TextOf(scene, "F Players"), "Rumble strength 0"));
        Step(scene, {'0'}); Step(scene, {});
        CHECK(Has(TextOf(scene, "F Players"), "Rumble strength 1"));
        InputHaptics::Stop(0);
        CHECK(Has(TextOf(scene, "F Players"), "by key position: false"));
        Step(scene, {'k'}); Step(scene, {});
        CHECK(Has(TextOf(scene, "F Players"), "by key position: true") && Input::KeysByPosition());
        Step(scene, {'k'}); Step(scene, {});
        CHECK(Has(TextOf(scene, "F Players"), "by key position: false") && !Input::KeysByPosition());
    }

    // G: record then replay
    Step(scene, {'1'}); Step(scene, {});
    CHECK(Has(TextOf(scene, "G Record"), "recording"));
    for (int i = 0; i < 10; ++i) Step(scene, {'w'});
    Step(scene, {'2'});
    CHECK(Has(TextOf(scene, "G Record"), "frames") || Has(TextOf(scene, "G Record"), "kept"));
    Step(scene, {});
    Step(scene, {'3'});
    CHECK(Has(TextOf(scene, "G Record"), "playing"));
    InputRecorder::StopPlayback(); InputRecorder::StopRecording();

    // H: the Controller station (a fake DualSense: no real pad was used)
    {
        PadProfiles::Reset();
        CHECK(Has(TextOf(scene, "H Controller"), "H. CONTROLLER"));
        PadSet pads;
        CHECK(pads.Attach(std::make_unique<StationPad>()) == 0);
        std::vector<char> pk; pads.Feed(pk);
        Step(scene, {});
        const std::string h = TextOf(scene, "H Controller");
        CHECK(Has(h, "0x054c:0x0ce6") && Has(h, "family DualSense") && Has(h, "mapping built-in") && Has(h, "battery full"));
        CHECK(Has(h, "A Cross") && Has(h, "Back Create") && Has(h, "LT L2"));
        CHECK(Has(h, "last event: Controller 1 connected"));
        Input::FeedGamepadRaw(0, {}, {}, 0, 0, 1u << Input::PadA, true); InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
        CHECK(Has(TextOf(scene, "H Controller"), "Jump = Cross") || Has(TextOf(scene, "H Controller"), "Jump = "));
        Step(scene, {'u'}); Step(scene, {});
        CHECK(Has(TextOf(scene, "H Controller"), ">> Step 1 of 20") && Has(TextOf(scene, "H Controller"), "LEFT stick"));
        Step(scene, {'m'}); Step(scene, {});
        CHECK(!Has(TextOf(scene, "H Controller"), ">> Step"));
        Step(scene, {'p'}); Step(scene, {});
        CHECK(Has(TextOf(scene, "H Controller"), "Controller 2: none connected"));
        for (int k = 0; k < 3; ++k) { Step(scene, {'p'}); Step(scene, {}); }
        CHECK(Has(TextOf(scene, "H Controller"), "Controller 1: DualSense"));
        pads.CloseAll();
        PadProfiles::Reset();
    }

    // Pictures for the controller work: software renders (no window, no real controller).
    {
        using F = ControllerFamily;
        const F fams[] = {F::DualSense, F::DualShock4, F::XboxSeries, F::Xbox360, F::SwitchPro, F::SteamDeck, F::Unknown};
        {   // glyph sheet: every button as each family names it
            Scene sheet("glyphs"); sheet.physicsEnabled = false;
            std::string t = "CONTROLLER BUTTON NAMES BY FAMILY (text names; no artwork)\n\n" + Pad("button", 12);
            for (F f : fams) t += Pad(FamilyName(f), 15);
            t += "\n";
            for (int b = 0; b < Input::PadButtonCount; ++b) {
                t += Pad(Input::GamepadButtonName(b), 12);
                for (F f : fams) t += Pad(PadButtonName(b, f), 15);
                t += "\n";
            }
            t += "\n" + Pad("PS / Home", 12);
            for (F f : fams) t += Pad(PadExtraName(PadExtra::Guide, f), 15);
            t += "\n" + Pad("Touchpad", 12);
            for (F f : fams) t += Pad(PadExtraName(PadExtra::Touchpad, f), 15);
            t += "\n" + Pad("Mute", 12);
            for (F f : fams) t += Pad(PadExtraName(PadExtra::Mute, f), 15);
            AddText(sheet, "Sheet", 8, 8, t, 1.2f);
            CHECK(ShotTo(sheet, "glyph-sheet.png"));
        }
        struct Dev { const char* file; const char* name; std::uint16_t v, p; int type; bool mapped; };
        const Dev devs[] = {
            {"DualSense", "DualSense Wireless Controller", 0x054C, 0x0CE6, 7, true},
            {"XboxSeries", "Xbox Wireless Controller", 0x045E, 0x0B13, 2, true},
            {"SwitchPro", "Nintendo Switch Pro Controller", 0x057E, 0x2009, 5, true},
            {"Generic", "USB Gamepad (unknown maker)", 0x1234, 0x5678, -1, false}};
        for (const Dev& d : devs) {
            PadProfiles::Reset();
            PadSet set;
            auto pad = std::make_unique<StationPad>();
            pad->name = d.name; pad->vid = d.v; pad->pid = d.p; pad->type = d.type; pad->mapped = d.mapped; pad->id = 90;
            pad->ax[0] = 12000; pad->hat = 1;
            CHECK(set.Attach(std::move(pad)) == 0);
            std::vector<char> pk; set.Feed(pk);
            Input::FeedGamepadRaw(0, {}, {}, 0, 0, 1u << Input::PadA, true); InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
            if (!d.mapped) { PadProfiles::BeginSetup(0); }
            set.Feed(pk);
            Input::FeedGamepadRaw(0, {}, {}, 0, 0, 1u << Input::PadA, true); InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
            CHECK(ShotTo(scene, std::string("controller-station-") + d.file + ".png"));
            // The Controls screen with this controller's names in the gamepad column.
            Scene ctl("controls"); ctl.physicsEnabled = false;
            std::string t = std::string("CONTROLS   controller: ") + d.name + "\n\n" + Pad("action", 14) + Pad("keyboard / mouse", 22) + "gamepad\n";
            RemapOptions ro; ro.hidden = {"look", "inventory", "pause", "reload", "aim", "interact", "sprint", "crouch"};
            const std::vector<RemapRow> rows = RemapRows(ro);
            for (const RemapRow& r : rows) t += Pad(r.action, 14) + Pad(r.keyboard.bound ? r.keyboard.label : "-", 22) + (r.gamepad.bound ? r.gamepad.label : "-") + "\n";
            t += "\nPress a button on the controller to rebind; names follow the controller in use.";
            AddText(ctl, "Controls", 24, 24, t);
            CHECK(ShotTo(ctl, std::string("rebinding-") + d.file + ".png"));
            if (d.type == 7) {   // the rebinding column really changed with the family
                bool seen = false;
                for (const RemapRow& r : rows) if (r.gamepad.bound && r.gamepad.label == "Cross") seen = true;
                CHECK(seen);
            }
            set.CloseAll();
        }
        PadProfiles::Reset();
        Input::SetGamepadFamily(0, Input::PadFamily::Xbox);
    }

    // A picture (software painter; not a real window)
    {
        UICanvas::Set(1280, 720);
        scene.uiFont = "pixel";
        Input::FeedKeys({}); InputActions::Tick(1.0f / 60.0f); scene.Update(1.0f / 60.0f);
        Image image(1280, 720);
        image.Fill(Color{0.07f, 0.08f, 0.12f, 1.0f});
        MemoryPainter painter(image);
        ui2d::UIRenderOptions opt;
        opt.interactive = false;
        ui2d::RenderSceneUI(scene, painter, 1280, 720, opt);
        if (const char* dir = std::getenv("OKAY_SHOT_DIR")) {
            const std::string path = std::string(dir) + "/input-playground.png";
            CHECK(image.SavePNG(path));
        }
    }
    TEST_MAIN_RESULT();
}
