// Gesture recogniser: synthetic finger streams.
// On-screen stick, D-pad and touch button: they drive the action map, and a finger
// on a control is not also a swipe. Screenshots of the controls are written outside
// the source tree.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Graphics/Image.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

int CountKind(const std::vector<GestureEvent>& ev, GestureKind kind) {
    int n = 0;
    for (const GestureEvent& e : ev) if (e.kind == kind) ++n;
    return n;
}
const GestureEvent* FindKind(const std::vector<GestureEvent>& ev, GestureKind kind) {
    for (const GestureEvent& e : ev) if (e.kind == kind) return &e;
    return nullptr;
}

std::vector<GestureEvent> Play(std::initializer_list<TouchSample> samples) {
    GestureRecognizer rec;
    std::vector<GestureEvent> out;
    double last = 0.0;
    for (const TouchSample& s : samples) {
        rec.Feed(s, out);
        rec.Advance(s.time, out);
        last = s.time;
    }
    rec.Advance(last + 1.0, out);
    return out;
}

void EndTouches() {
    std::vector<long long> ids;
    std::vector<Vec2> at;
    for (int i = 0; i < Input::TouchCount(); ++i) {
        ids.push_back(Input::GetTouch(i).id);
        at.push_back(Input::GetTouch(i).position);
    }
    for (std::size_t i = 0; i < ids.size(); ++i) Input::FeedTouch(ids[i], 2, at[i]);
    Input::ClearTypedText();
    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);
    Input::FeedMouse({0, 0}, 0);
    Gestures::Reset();
    TouchClaims::Clear();
    InputActions::ClearContributions();
}

void Frame(Scene& scene, double time) {
    TouchControls::Sample(scene);
    Gestures::Tick(time);
    InputActions::Tick();
}

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

bool SaveShot(Image& image, const std::string& name) {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::path("/tmp/okay-touch-controls") / name;
    const fs::path art = fs::path("/opt/cursor/artifacts") / name;
    std::error_code ec;
    fs::create_directories(tmp.parent_path(), ec);
    fs::create_directories(art.parent_path(), ec);
    const bool a = image.SavePNG(tmp.string());
    const bool b = image.SavePNG(art.string());
    if (!a) std::cerr << "  could not write " << tmp << "\n";
    if (!b) std::cerr << "  could not write " << art << "\n";
    return a && b;
}

void RenderControls(Scene& scene, int w, int h, const std::string& file) {
    Image image(w, h);
    image.Fill(Color{0.07f, 0.08f, 0.12f, 1.0f});
    MemoryPainter painter(image);
    ui2d::UIRenderOptions opt;
    opt.interactive = false;
    ui2d::RenderSceneUI(scene, painter, w, h, opt);
    CHECK(SaveShot(image, file));
}

} // namespace

int main() {
    RUN_SUITE("touch_controls");
    InputActions::Restore();

    // ---- Recogniser: synthetic streams ------------------------------------
    {
        auto ev = Play({{1, 0, 10, 10, 0.0}, {1, 2, 12, 11, 0.12}});
        CHECK(CountKind(ev, GestureKind::Tap) == 1);
        CHECK(CountKind(ev, GestureKind::DoubleTap) == 0);
        CHECK(CountKind(ev, GestureKind::Swipe) == 0);
    }
    {
        auto ev = Play({
            {1, 0, 10, 10, 0.0}, {1, 2, 10, 10, 0.10},
            {1, 0, 14, 12, 0.22}, {1, 2, 16, 14, 0.30},
        });
        CHECK(CountKind(ev, GestureKind::DoubleTap) == 1);
        CHECK(CountKind(ev, GestureKind::Tap) == 0);
    }
    {
        GestureRecognizer rec;
        std::vector<GestureEvent> ev;
        rec.Feed({1, 0, 40, 40, 0.0}, ev);
        rec.Advance(0.20, ev);
        CHECK(CountKind(ev, GestureKind::LongPress) == 0);
        rec.Advance(0.55, ev);
        CHECK(CountKind(ev, GestureKind::LongPress) == 1);
        rec.Advance(0.90, ev);
        CHECK(CountKind(ev, GestureKind::LongPress) == 1);
        rec.Feed({1, 2, 40, 40, 0.95}, ev);
        CHECK(CountKind(ev, GestureKind::Tap) == 0);
        CHECK(CountKind(ev, GestureKind::Swipe) == 0);
    }
    {
        auto ev = Play({{1, 0, 0, 100, 0.0}, {1, 1, 40, 100, 0.08}, {1, 2, 140, 110, 0.16}});
        CHECK(CountKind(ev, GestureKind::Swipe) == 1);
        const GestureEvent* s = FindKind(ev, GestureKind::Swipe);
        CHECK(s && s->directionName == "right");
        CHECK(s && s->direction.x > 0.7f && std::fabs(s->direction.y) < 0.4f);
    }
    {
        auto up = Play({{1, 0, 80, 200, 0.0}, {1, 2, 80, 40, 0.12}});
        CHECK(CountKind(up, GestureKind::Swipe) == 1);
        CHECK(FindKind(up, GestureKind::Swipe)->directionName == "up");
        CHECK(FindKind(up, GestureKind::Swipe)->direction.y > 0.5f);
        auto down = Play({{1, 0, 80, 40, 0.0}, {1, 2, 70, 200, 0.12}});
        CHECK(FindKind(down, GestureKind::Swipe)->directionName == "down");
        auto left = Play({{1, 0, 200, 80, 0.0}, {1, 2, 20, 80, 0.12}});
        CHECK(FindKind(left, GestureKind::Swipe)->directionName == "left");
    }
    {
        auto slow = Play({{1, 0, 0, 0, 0.0}, {1, 2, 100, 0, 2.0}});
        CHECK(CountKind(slow, GestureKind::Swipe) == 0);
        CHECK(CountKind(slow, GestureKind::Tap) == 0);
    }
    {
        GestureRecognizer rec;
        std::vector<GestureEvent> ev;
        rec.Feed({1, 0, 0, 0, 0.0}, ev);
        rec.Feed({2, 0, 100, 0, 0.0}, ev);
        rec.Feed({2, 1, 260, 0, 0.2}, ev);
        CHECK(CountKind(ev, GestureKind::Pinch) >= 1);
        CHECK(rec.PinchScale() > 2.0f);
        const GestureEvent* p = FindKind(ev, GestureKind::Pinch);
        CHECK(p && p->delta > 0.5f);
        CHECK(CountKind(ev, GestureKind::Tap) == 0);
    }
    {
        GestureRecognizer rec;
        std::vector<GestureEvent> ev;
        rec.Feed({1, 0, 0, 0, 0.0}, ev);
        rec.Feed({2, 0, 100, 0, 0.0}, ev);
        rec.Feed({2, 1, 0, 100, 0.2}, ev);   // the second finger moves downward: clockwise, negative twist
        CHECK(CountKind(ev, GestureKind::Rotate) >= 1);
        CHECK(rec.TwistRadians() < -0.7f);
        CHECK(CountKind(ev, GestureKind::Swipe) == 0);
    }

    // ---- Controls drive the action map ------------------------------------
    UICanvas::Set(1280, 720);
    EndTouches();
    Input::SetTouchScreen(true);
    {
        Scene scene("stick");
        scene.physicsEnabled = false;
        auto* stick = scene.CreateGameObject("Stick")->AddComponent<VirtualJoystick>();
        stick->onlyOnTouch = true;
        Input::ClearTypedText();
        Input::FeedTouch(7, 0, {120, 600});   // centre of the default bottom-left stick
        Frame(scene, 1.0);
        Input::ClearTypedText();
        Input::FeedTouch(7, 1, {190, 600});   // to the right
        Frame(scene, 1.1);
        const Vec2 move = InputActions::Vector("Move");
        CHECK(move.x > 0.5f);
        CHECK(std::fabs(move.y) < 0.2f);
        CHECK(stick->value.x > 0.5f);
        CHECK(!Gestures::Happened("swipe"));   // the stick owns this finger
        // A key bound to Move still works, and the two together stay inside length 1.
        Input::FeedKeys({'w'});
        Frame(scene, 1.2);
        const Vec2 both = InputActions::Vector("Move");
        CHECK(both.y > 0.4f);
        CHECK(both.x * both.x + both.y * both.y <= 1.01f);
        Input::FeedKeys({});
        EndTouches();
        Frame(scene, 1.3);
        CHECK(InputActions::Vector("Move").x == 0.0f && InputActions::Vector("Move").y == 0.0f);
    }
    {
        Scene scene("pad");
        scene.physicsEnabled = false;
        auto* pad = scene.CreateGameObject("Pad")->AddComponent<TouchDPad>();
        pad->onlyOnTouch = true;
        Input::ClearTypedText();
        Input::FeedTouch(3, 0, {355, 605});   // right side of the default pad
        Frame(scene, 2.0);
        CHECK(InputActions::Vector("Move").x > 0.9f);
        CHECK(pad->value.x == 1.0f && pad->value.y == 0.0f);
        EndTouches();
    }
    {
        Scene scene("both");
        scene.physicsEnabled = false;
        auto* stick = scene.CreateGameObject("Stick")->AddComponent<VirtualJoystick>();
        auto* button = scene.CreateGameObject("Jump")->AddComponent<TouchButton>();
        stick->onlyOnTouch = true;
        button->onlyOnTouch = true;
        Input::ClearTypedText();
        Input::FeedTouch(1, 0, {120, 600});
        Input::FeedTouch(2, 0, {1180, 620});
        Frame(scene, 3.0);
        CHECK(InputActions::Get("Jump"));
        CHECK(InputActions::Down("Jump"));
        Input::ClearTypedText();
        Input::FeedTouch(1, 1, {120, 520});   // stick up
        Input::FeedTouch(2, 1, {1180, 620});  // button still held
        Frame(scene, 3.1);
        CHECK(InputActions::Get("Jump"));
        CHECK(!InputActions::Down("Jump"));
        CHECK(InputActions::Vector("Move").y > 0.4f);
        Input::ClearTypedText();
        Input::FeedTouch(2, 2, {1180, 620});
        Input::FeedTouch(1, 2, {120, 520});
        Frame(scene, 3.2);
        CHECK(InputActions::Up("Jump"));
        CHECK(!InputActions::Get("Jump"));
    }
    {
        // Hidden until a touch screen is known. A mouse can use the same button
        // once that option is off, and the keyboard binding is untouched.
        Scene scene("mouse");
        scene.physicsEnabled = false;
        auto* button = scene.CreateGameObject("Jump")->AddComponent<TouchButton>();
        button->onlyOnTouch = true;
        EndTouches();
        Input::SetTouchScreen(false);
        Input::FeedMouse({1180, 620}, 0);
        Input::FeedMouse({1180, 620}, 1u);
        Frame(scene, 4.0);
        CHECK(!InputActions::Get("Jump"));
        button->onlyOnTouch = false;
        Input::FeedMouse({1180, 620}, 0);
        Input::FeedMouse({1180, 620}, 1u);
        Frame(scene, 4.1);
        CHECK(InputActions::Get("Jump"));
        CHECK(InputActions::Down("Jump"));
        Input::FeedKeys({' '});
        Input::FeedMouse({1180, 620}, 0);
        Frame(scene, 4.2);
        CHECK(InputActions::Get("Jump"));   // Space still holds Jump after the button lifts
        Input::FeedKeys({});
        Frame(scene, 4.3);
        CHECK(!InputActions::Get("Jump"));
    }
    {
        Scene scene("float");
        scene.physicsEnabled = false;
        auto* stick = scene.CreateGameObject("Float")->AddComponent<VirtualJoystick>();
        stick->floating = true;
        stick->onlyOnTouch = false;
        stick->anchor = UIAnchor::BottomLeft;
        stick->position = {0, 0};
        stick->size = {400, 280};   // the lower-left region a floating stick listens in
        Input::SetTouchScreen(false);
        EndTouches();
        Input::FeedMouse({80, 640}, 0);
        Input::FeedMouse({80, 640}, 1u);
        Frame(scene, 5.0);
        Input::FeedMouse({200, 640}, 1u);
        Frame(scene, 5.1);
        CHECK(stick->tracking);
        CHECK(stick->value.x > 0.4f);
        CHECK(InputActions::Vector("Move").x > 0.4f);
        Input::FeedMouse({140, 640}, 0);
        Frame(scene, 5.2);
        CHECK(!stick->tracking);
        CHECK(InputActions::Vector("Move").x == 0.0f);
    }

    // ---- Text script and a visual-script node --------------------------------
    EndTouches();
    Input::SetTouchScreen(true);
    {
        Scene scene("script");
        scene.physicsEnabled = false;
        GameObject* read = scene.CreateGameObject("Readout");
        read->AddComponent<TextRenderer>()->text = "waiting";
        auto* sc = read->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(
            "function update(dt) {\n"
            "  if (gesture(\"tap\")) set_text(\"tap\");\n"
            "  if (gesture(\"double tap\")) set_text(\"double\");\n"
            "  if (gesture(\"swipe up\")) set_text(swipe_direction());\n"
            "  if (action(\"jump\")) set_text(\"jump\");\n"
            "}\n", &err));
        if (!err.empty()) std::cerr << "  script: " << err << "\n";
        GameObject* graph = scene.CreateGameObject("Graph");
        auto* vs = graph->AddComponent<VisualScriptComponent>();
        CHECK(vs->LoadFromText(
            "node 1 OnTap\n"
            "entry OnTap 1\n"
            "node 2 SetVar sawTap\n"
            "default 2 0 1\n"
            "exec 1 0 2\n"
            "node 3 OnSwipe up\n"
            "entry OnSwipe:up 3\n"
            "node 4 SetVar sawSwipe\n"
            "default 4 0 1\n"
            "exec 3 0 4\n", &err));
        if (!err.empty()) std::cerr << "  graph: " << err << "\n";
        scene.Start();
        Input::ClearTypedText();
        Input::FeedTouch(9, 0, {8, 8});
        Frame(scene, 6.0);
        Input::ClearTypedText();
        Input::FeedTouch(9, 2, {8, 8});
        Frame(scene, 6.1);
        Frame(scene, 6.6);   // the tap waits out the double-tap window
        scene.Update(1.0f / 60.0f);
        CHECK(read->GetComponent<TextRenderer>()->text == "tap");
        CHECK(vs->GetVariable("sawTap").AsFloat() == 1.0f);

        Input::ClearTypedText();
        Input::FeedTouch(9, 0, {8, 200});
        Frame(scene, 7.0);
        Input::ClearTypedText();
        Input::FeedTouch(9, 2, {8, 40});
        Frame(scene, 7.12);
        scene.Update(1.0f / 60.0f);
        CHECK(read->GetComponent<TextRenderer>()->text == "up");
        CHECK(vs->GetVariable("sawSwipe").AsFloat() == 1.0f);

        auto* button = scene.CreateGameObject("Jump")->AddComponent<TouchButton>();
        button->onlyOnTouch = true;
        Input::ClearTypedText();
        Input::FeedTouch(4, 0, {1180, 620});
        Frame(scene, 8.0);
        scene.Update(1.0f / 60.0f);
        CHECK(read->GetComponent<TextRenderer>()->text == "jump");
        EndTouches();
    }

    // ---- Save and the demo scene ----------------------------------------------
    {
        Scene scene("save");
        auto* stick = scene.CreateGameObject("Stick")->AddComponent<VirtualJoystick>();
        stick->action = "Strafe";
        stick->floating = true;
        stick->onlyOnTouch = false;
        stick->deadZone = 0.2f;
        auto* pad = scene.CreateGameObject("Pad")->AddComponent<TouchDPad>();
        pad->diagonals = false;
        pad->action = "Move";
        auto* button = scene.CreateGameObject("Fire")->AddComponent<TouchButton>();
        button->action = "Fire";
        button->label = "Fire";
        const std::string text = SceneSerializer::Serialize(scene);
        Scene back("back");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        if (!err.empty()) std::cerr << "  save: " << err << "\n";
        auto* stick2 = back.Find("Stick")->GetComponent<VirtualJoystick>();
        auto* pad2 = back.Find("Pad")->GetComponent<TouchDPad>();
        auto* button2 = back.Find("Fire")->GetComponent<TouchButton>();
        CHECK(stick2 && stick2->action == "Strafe" && stick2->floating && !stick2->onlyOnTouch);
        CHECK(stick2 && std::fabs(stick2->deadZone - 0.2f) < 0.001f);
        CHECK(pad2 && !pad2->diagonals && pad2->action == "Move");
        CHECK(button2 && button2->action == "Fire" && button2->label == "Fire");
        CHECK(SceneSerializer::Serialize(back) == text);
    }
    {
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/examples/touch_controls/touch_controls.okayscene");
        CHECK(in.good());
        std::stringstream ss;
        ss << in.rdbuf();
        Scene demo("demo");
        std::string err;
        CHECK(SceneSerializer::Deserialize(demo, ss.str(), &err));
        if (!err.empty()) std::cerr << "  demo: " << err << "\n";
        CHECK(demo.Find("Stick") && demo.Find("Stick")->GetComponent<VirtualJoystick>());
        CHECK(demo.Find("Pad") && demo.Find("Pad")->GetComponent<TouchDPad>());
        CHECK(demo.Find("Jump") && demo.Find("Jump")->GetComponent<TouchButton>());
        auto* readout = demo.Find("Readout");
        auto* readoutScript = readout ? readout->GetComponent<ScriptComponent>() : nullptr;
        CHECK(readoutScript);
        if (readoutScript) {
            std::string serr;
            CHECK(readoutScript->LoadSource(readoutScript->Source(), &serr));
            if (!serr.empty()) std::cerr << "  readout script: " << serr << "\n";
        }
        auto* marker = demo.Find("Marker");
        auto* markerScript = marker ? marker->GetComponent<ScriptComponent>() : nullptr;
        CHECK(markerScript);
        if (markerScript) {
            std::string serr;
            CHECK(markerScript->LoadSource(markerScript->Source(), &serr));
            if (!serr.empty()) std::cerr << "  marker script: " << serr << "\n";
        }
        std::ifstream graph(std::string(OKAY_SOURCE_DIR) + "/examples/touch_controls/touch_controls.okayvs");
        CHECK(graph.good());
        std::stringstream gs;
        gs << graph.rdbuf();
        std::string gerr;
        CHECK(vs::NodeGraph::Parse(gs.str(), &gerr) != nullptr);
        if (!gerr.empty()) std::cerr << "  demo graph: " << gerr << "\n";
    }

    // ---- Pictures of the three controls ---------------------------------------
    {
        Scene hud("hud");
        hud.physicsEnabled = false;
        hud.uiFont = "pixel";   // the 8x8 font, so this headless painter can draw the words
        auto* cap = hud.CreateGameObject("Caption")->AddComponent<TextRenderer>();
        cap->text = "Touch controls";
        cap->screenSpace = true;
        cap->pixelSize = 3.0f;
        cap->screenPos = {24, 24};
        cap->color = Color::White;
        auto* stick = hud.CreateGameObject("Stick")->AddComponent<VirtualJoystick>();
        stick->onlyOnTouch = false;
        stick->fadeWhenIdle = false;
        stick->tracking = true;
        stick->knob = {48, -20};
        auto* pad = hud.CreateGameObject("Pad")->AddComponent<TouchDPad>();
        pad->onlyOnTouch = false;
        pad->value = {1, 0};
        auto* button = hud.CreateGameObject("Jump")->AddComponent<TouchButton>();
        button->onlyOnTouch = false;
        button->pressed = true;
        button->label = "Jump";
        RenderControls(hud, 1280, 720, "touch-controls-hud.png");

        auto one = [](const char* name, auto build, const char* file) {
            Scene s(name);
            s.physicsEnabled = false;
            s.uiFont = "pixel";
            UICanvas::Set(420, 420);
            build(s.CreateGameObject(name));
            RenderControls(s, 420, 420, file);
        };
        one("Joy", [](GameObject* g) {
            auto* j = g->AddComponent<VirtualJoystick>();
            j->onlyOnTouch = false;
            j->fadeWhenIdle = false;
            j->anchor = UIAnchor::Center;
            j->position = {0, 0};
            j->size = {180, 180};
            j->tracking = true;
            j->knob = {40, -30};
        }, "touch-joystick.png");
        one("Pad", [](GameObject* g) {
            auto* d = g->AddComponent<TouchDPad>();
            d->onlyOnTouch = false;
            d->anchor = UIAnchor::Center;
            d->position = {0, 0};
            d->size = {160, 160};
            d->value = {0, 1};
        }, "touch-dpad.png");
        one("Button", [](GameObject* g) {
            auto* b = g->AddComponent<TouchButton>();
            b->onlyOnTouch = false;
            b->anchor = UIAnchor::Center;
            b->position = {0, 0};
            b->size = {140, 140};
            b->label = "Jump";
            b->pressed = true;
        }, "touch-button.png");
        UICanvas::Set(1280, 720);
    }

    TEST_MAIN_RESULT();
}
