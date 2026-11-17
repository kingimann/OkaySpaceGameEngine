// Headless pictures of the input windows (written outside the repository, see tools/input_shots.sh): the REAL drawing code of editor/InputActionsWindow.hpp and
// editor/InputDebugger.hpp, Dear ImGui's default scalable font, drawn by ImGui's SDL renderer backend onto a software SDL surface
// (no window, no editor, no GPU). The look is ImGui's default dark style, NOT the editor's own skin; the rows, order and texts are
// the real ones. Also the controller screens (button names, Controls screen, Controller station), the phase timeline and the
// local-players screen, drawn with ImGui from the same engine data. Not part of the CMake build; see tools/input_shots.sh.
// Written for this repository (Dear ImGui's public API only).
#include <SDL.h>
#include <imgui.h>
#include "imgui_impl_sdlrenderer2.h"
#include "InputActionsWindow.hpp"
#include "InputDebugger.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include "okay/Input/RemapModel.hpp"
#include "okay/Core/Prefs.hpp"

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace okay;
namespace pf = okay::platform;

static SDL_Surface* g_surface = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static int W = 1280, H = 720;
static float S = 1.0f;
static std::string g_out;

// A fake controller (nothing real is plugged in).
struct Dev final : pf::Gamepad {
    int id = 90; std::string name; std::uint16_t vid = 0, pid = 0; int type = -1; bool mapped = true;
    std::int16_t ax[6] = {}; std::uint8_t hat = 0;
    int InstanceId() const override { return id; }
    const char* Name() const override { return name.c_str(); }
    std::int16_t Axis(pf::GamepadAxis) const override { return 0; }
    bool Button(pf::GamepadButton) const override { return false; }
    bool Rumble(std::uint16_t, std::uint16_t, std::uint32_t) override { return true; }
    std::uint16_t VendorId() const override { return vid; }
    std::uint16_t ProductId() const override { return pid; }
    int ControllerType() const override { return type; }
    bool HasBuiltinMapping() const override { return mapped; }
    int RawAxisCount() const override { return 6; }
    int RawButtonCount() const override { return 13; }
    int RawHatCount() const override { return 1; }
    std::int16_t RawAxis(int i) const override { return ax[i]; }
    bool RawButton(int) const override { return false; }
    std::uint8_t RawHat(int) const override { return hat; }
    int PowerLevel() const override { return 2; }
};
static std::unique_ptr<Dev> MakeDev(const char* name, std::uint16_t v, std::uint16_t p, int type, bool mapped, int id = 90) {
    auto d = std::make_unique<Dev>(); d->name = name; d->vid = v; d->pid = p; d->type = type; d->mapped = mapped; d->id = id;
    d->ax[0] = 12000; d->hat = 1;
    return d;
}

static void NewContext() {
    if (ImGui::GetCurrentContext()) { ImGui_ImplSDLRenderer2_Shutdown(); ImGui::DestroyContext(); }
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_surface) SDL_FreeSurface(g_surface);
    g_surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    g_renderer = SDL_CreateSoftwareRenderer(g_surface);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fc; fc.SizePixels = 16.0f * S;
    io.Fonts->AddFontDefaultVector(&fc);   // the scalable default font: smooth at any size
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(S);
    ImGui_ImplSDLRenderer2_Init(g_renderer);
}

// Run a few frames so layout settles, then write <name>.bmp and convert it to PNG.
static void Capture(const std::string& name, const std::function<void()>& body, int frames = 8) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    for (int f = 0; f < frames; ++f) {
        InputActions::Tick(0.016f);
        ImGui_ImplSDLRenderer2_NewFrame();
        io.DisplaySize = ImVec2((float)W, (float)H);
        io.DeltaTime = 0.1f;
        ImGui::NewFrame();
        body();
        ImGui::Render();
        SDL_SetRenderDrawColor(g_renderer, 22, 24, 30, 255);
        SDL_RenderClear(g_renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
        SDL_RenderPresent(g_renderer);
    }
    const std::string bmp = g_out + "/" + name + ".bmp", png = g_out + "/" + name + ".png";
    SDL_SaveBMP(g_surface, bmp.c_str());
    const std::string cmd = "convert '" + bmp + "' '" + png + "' && rm -f '" + bmp + "'";
    if (std::system(cmd.c_str()) != 0) std::printf("convert failed for %s\n", bmp.c_str());
    std::printf("wrote %s\n", png.c_str());
}

static void FullWindow(const char* title, const std::function<void()>& inside, float x = 10, float y = 10, float w = 0, float h = 0) {
    ImGui::SetNextWindowPos(ImVec2(x * S, y * S), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w > 0 ? w * S : (float)W - 2 * x * S, h > 0 ? h * S : (float)H - 2 * y * S), ImGuiCond_Always);
    ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    inside();
    ImGui::End();
}

static const char* kFamNames[] = {"DualSense", "DualShock 4", "Xbox Series", "Xbox 360", "Switch Pro", "Steam Deck", "Generic"};
static const ControllerFamily kFams[] = {ControllerFamily::DualSense, ControllerFamily::DualShock4, ControllerFamily::XboxSeries, ControllerFamily::Xbox360,
                                         ControllerFamily::SwitchPro, ControllerFamily::SteamDeck, ControllerFamily::Unknown};

static void Reset() {
    Input::FeedKeys({}); Input::FeedMouse({400, 300}, 0);
    for (int p = 0; p < Input::kMaxGamepads; ++p) { Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false); }
}

static void Shots() {
    const std::string res = std::to_string(W) + "x" + std::to_string(H);
    Prefs::Clear(); Reset(); PadProfiles::Reset(); InputRuntime::Reset();

    // ---- the controller on screen: a DualSense, a keyboard player and a controller player (local players)
    PadSet pads;
    pads.Attach(MakeDev("DualSense Wireless Controller", 0x054C, 0x0CE6, 7, true));
    std::vector<char> pk; pads.Feed(pk);
    PlayerInputManager& pm = InputRuntime::Players();
    pm.Join(JoinDevice::Keyboard); pm.Join(JoinDevice::Pad, 0);
    pm.Player(1)->Find("Jump")->AddInteraction("hold(0.5)");
    Input::FeedKeys({'w', 'd', ' '});
    Input::FeedGamepadRaw(0, {-0.55f, 0.35f}, {}, 0, 0.8f, 1u << Input::PadA, true);
    InputActions::Tick(0.1f); InputActions::Tick(0.1f); InputActions::Tick(0.1f);

    // ---- 1. Input Actions window: the real window code
    okay::editor::detail::IaSelected() = "Move";
    for (int tab = 0; tab < 3; ++tab) {
        okay::editor::detail::IaForceTab() = tab;
        const char* tabName[] = {"action", "check", "live"};
        Capture("input-actions-window-" + std::string(tabName[tab]) + "-" + res, [&] {
            ImGui::SetNextWindowPos(ImVec2(10 * S, 10 * S), ImGuiCond_Always);
            static bool open = true;
            ImGui::SetNextWindowSize(ImVec2((float)W - 20 * S, (float)H - 20 * S), ImGuiCond_Always);
            okay::editor::DrawInputActionsWindow(&open, "");
            ImGui::SetWindowSize("Input Actions", ImVec2((float)W - 20 * S, (float)H - 20 * S));   // the window sizes itself on first use; show it large
        }, 10);
    }
    okay::editor::detail::IaForceTab() = -1;

    // ---- 2. the Input panel (real code) and its "Controllers and typed actions" section on its own
    Capture("input-panel-" + res, [&] {
        ImGui::SetNextWindowPos(ImVec2(10 * S, 10 * S), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(620 * S, (float)H - 20 * S), ImGuiCond_Always);
        static bool open = true;
        okay::editor::DrawInputDebugger(&open, true);
        ImGui::SetWindowSize("Input", ImVec2(620 * S, (float)H - 20 * S));
    });
    Capture("controllers-typed-actions-" + res, [&] {
        FullWindow("Input   Controllers and typed actions", [&] { okay::editor::DrawControllersAndTypedActions(); }, 10, 10, 760, 0);
    });

    // ---- 3. the phase timeline (drawn with ImGui from a real ActionSet per interaction)
    Capture("action-phases-" + res, [&] {
        FullWindow("Interactions and phases   (one key, 0.05 s per frame)", [&] {
            const std::string pattern = "..XXXXXXXXXXXX..XX.XX.....XX......XXXXXXXXXXXXX...";
            const char* rows[] = {"press", "release", "hold(0.5)", "tap(0.2)", "multitap(2,0.3,0.2)", "repeat(0.1,0.2)"};
            ImGui::TextDisabled("S started   P performed   C canceled   (a frame shows the strongest phase in it: P over C over S)");
            const float cw = 17.0f * S, label = 190.0f * S;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto row = [&](const char* name, const std::string& cells) {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::TextUnformatted(name);
                for (std::size_t i = 0; i < cells.size(); ++i) {
                    const char c = cells[i];
                    const ImU32 col = c == 'P' ? IM_COL32(70, 170, 90, 255) : c == 'C' ? IM_COL32(200, 80, 70, 255) : c == 'S' ? IM_COL32(200, 170, 60, 255) : c == 'X' ? IM_COL32(90, 120, 190, 255) : IM_COL32(45, 48, 58, 255);
                    const ImVec2 a(p.x + label + i * cw, p.y), b(a.x + cw - 2 * S, a.y + ImGui::GetTextLineHeight());
                    dl->AddRectFilled(a, b, col);
                    if (c == 'P' || c == 'C' || c == 'S') { const char t[2] = {c, 0}; dl->AddText(ImVec2(a.x + 4 * S, a.y), IM_COL32(255, 255, 255, 255), t); }
                }
                ImGui::Dummy(ImVec2(0, 6 * S));
            };
            row("input (X = key down)", pattern);
            for (const char* r : rows) {
                ActionSet s; s.Add("A", ValueType::Button).AddBinding("key:x"); s.Find("A")->AddInteraction(r);
                std::string cells;
                for (char ch : pattern) {
                    Input::FeedKeys(ch == 'X' ? std::vector<char>{'x'} : std::vector<char>{});
                    s.Update(0.05f);
                    char c = '-';
                    for (const PhaseEvent& e : s.Events()) { if (e.phase == Phase::Performed) c = 'P'; else if (e.phase == Phase::Canceled && c != 'P') c = 'C'; else if (e.phase == Phase::Started && c == '-') c = 'S'; }
                    cells += c;
                }
                row(r, cells);
            }
            Input::FeedKeys({'w', 'd', ' '});
        }, 10, 10, 0, 300);
    });

    // ---- 3b. typed actions under the keyboard scheme, then the controller scheme (auto switch)
    {
        ActionSet as;
        as.AddScheme({"Keyboard+Mouse", true, true, false, false}); as.AddScheme({"Gamepad", false, false, true, false});
        as.Add("Move", ValueType::Vector2).AddVector2("key:w", "key:s", "key:a", "key:d", "Keyboard+Mouse");
        as.Find("Move")->AddBinding("stick:left", "Gamepad", "deadzone(0.2,0.9)");
        as.Find("Move")->AddVector2("pad:up", "pad:down", "pad:left", "pad:right", "Gamepad");
        as.Add("Jump", ValueType::Button).AddBinding("key:space", "Keyboard+Mouse"); as.Find("Jump")->AddBinding("pad:a", "Gamepad");
        as.Add("Throttle", ValueType::Axis1D).AddAxis("key:q", "key:e", "Keyboard+Mouse"); as.Find("Throttle")->AddAxis("pad:lt", "pad:rt", "Gamepad");
        as.Add("Dash", ValueType::Button).AddChord({"key:ctrl", "key:f"});
        as.Add("Fly", ValueType::Vector3).AddVector3("key:w", "key:s", "key:a", "key:d", "key:e", "key:q");
        as.SetAutoSwitch(true);
        auto draw = [&](const char* title, const char* file) {
            Capture(std::string(file) + "-" + res, [&] {
                FullWindow(title, [&] {
                    ImGui::Text("active control scheme: %s    auto switch: %s", as.Scheme().c_str(), as.AutoSwitch() ? "on" : "off");
                    if (ImGui::BeginTable("##as", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 1.0f); ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthStretch, 1.0f); ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.6f); ImGui::TableSetupColumn("driven by", ImGuiTableColumnFlags_WidthStretch, 4.0f);
                        ImGui::TableHeadersRow();
                        for (const ActionDef& a : as.Actions()) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(a.name.c_str());
                            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(ValueTypeName(a.type));
                            ImGui::TableSetColumnIndex(2);
                            if (a.type == ValueType::Button) ImGui::Text("%s (%.2f)", a.down ? "DOWN" : "up", a.value.x);
                            else if (a.type == ValueType::Axis1D) ImGui::Text("%.2f", a.value.x == 0 ? 0.0f : a.value.x);
                            else if (a.type == ValueType::Vector2) ImGui::Text("%.2f, %.2f", a.value.x == 0 ? 0.0f : a.value.x, a.value.y == 0 ? 0.0f : a.value.y);
                            else ImGui::Text("%.2f, %.2f, %.2f", a.value.x, a.value.y, a.value.z);
                            ImGui::TableSetColumnIndex(3);
                            if (a.activeBinding < 0) ImGui::TextDisabled("-");
                            else { const BindingSpec& b = a.bindings[(std::size_t)a.activeBinding]; std::string t = b.parts[0] + (b.kind != BindingSpec::Kind::Single ? " ... (composite)" : "") + (b.processors.empty() ? "" : "  " + ProcessorsToString(b.processors)) + "  [" + (b.group.empty() ? "all" : b.group) + "]"; ImGui::TextUnformatted(t.c_str()); }
                        }
                        ImGui::EndTable();
                    }
                }, 10, 10, 0, 250);
            }, 3);
        };
        Reset();
        Input::FeedKeys({'w', 'd', Input::KeyCtrl, 'f'}); as.Update(0.016f);
        draw("Typed actions   keyboard in use: W + D, Ctrl + F", "action-set-keyboard");
        Input::FeedKeys({});
        Input::FeedGamepadRaw(0, {0.6f, 0.8f}, {}, 0, 0.7f, 1u << Input::PadA, true); as.Update(0.016f);
        draw("Typed actions   controller in use: left stick, A, right trigger", "action-set-gamepad");
        Reset();
        Input::FeedKeys({'w', 'd', ' '});
        Input::FeedGamepadRaw(0, {-0.55f, 0.35f}, {}, 0, 0.8f, 1u << Input::PadA, true);
    }

    // ---- 4. local players
    Capture("input-players-" + res, [&] {
        FullWindow("Local players   (each with its own device, scheme, maps and rebinding)", [&] {
            ImGui::BeginTable("##players", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame);
            ImGui::TableNextRow();
            for (int p = 0; p < pm.Count(); ++p) {
                ImGui::TableSetColumnIndex(p);
                const ActionSet* s = pm.Player(p);
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "PLAYER %d", p + 1);
                ImGui::Text("device: %s", pm.DeviceOf(p) == JoinDevice::Pad ? ("controller " + std::to_string(pm.PadOf(p) + 1)).c_str() : "keyboard + mouse");
                ImGui::Text("scheme: %s", s->Scheme().c_str());
                std::string maps; for (const ActionMap& m : s->Maps()) maps += m.name + (m.enabled ? " (on)  " : " (off)  ");
                ImGui::Text("maps: %s", maps.c_str());
                ImGui::Separator();
                for (const char* n : {"Move", "Look", "Jump", "Fire", "Sprint"}) {
                    const ActionDef* a = s->Find(n);
                    const float mag = a->type == ValueType::Button ? a->value.x : a->value.Length();
                    char over[64];
                    if (a->type == ValueType::Button) std::snprintf(over, sizeof over, "%s", a->down ? "DOWN" : "up");
                    else std::snprintf(over, sizeof over, "%.2f, %.2f", a->value.x, a->value.y);
                    ImGui::Text("%-8s", n); ImGui::SameLine(90 * S);
                    ImGui::ProgressBar(mag > 1.0f ? 1.0f : mag, ImVec2(200 * S, 0), over);
                    ImGui::SameLine(); ImGui::TextDisabled("%s", a->activeBinding >= 0 ? a->bindings[(std::size_t)a->activeBinding].parts[0].c_str() : "-");
                }
            }
            ImGui::EndTable();
            ImGui::Spacing();
            ImGui::TextDisabled("Keyboard player: W + D and Space.   Controller 1 player: left stick, right trigger and A (Jump has hold(0.5), shown as a bar).");
        }, 10, 10, 0, 300);
    });

    // ---- 5. glyph sheet: every button as each family names it
    Capture("glyph-sheet-" + res, [&] {
        FullWindow("Controller button names by family   (text names; no artwork)", [&] {
            if (ImGui::BeginTable("##glyphs", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn("button");
                for (const char* n : kFamNames) ImGui::TableSetupColumn(n);
                ImGui::TableHeadersRow();
                for (int b = 0; b < Input::PadButtonCount; ++b) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", Input::GamepadButtonName(b).c_str());
                    for (int f = 0; f < 7; ++f) { ImGui::TableSetColumnIndex(f + 1); ImGui::TextUnformatted(PadButtonName(b, kFams[f]).c_str()); }
                }
                const struct { const char* n; PadExtra e; } extras[] = {{"PS / Home", PadExtra::Guide}, {"Touchpad", PadExtra::Touchpad}, {"Mute", PadExtra::Mute}, {"Share", PadExtra::Share}};
                for (auto& ex : extras) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", ex.n);
                    for (int f = 0; f < 7; ++f) { ImGui::TableSetColumnIndex(f + 1); const std::string t = PadExtraName(ex.e, kFams[f]); ImGui::TextUnformatted(t.empty() ? "-" : t.c_str()); }
                }
                ImGui::EndTable();
            }
        });
    });

    pads.CloseAll(); PadProfiles::Reset(); InputRuntime::Reset(); Reset();

    // ---- 6. the Controls screen and the Controller station for each connected controller
    struct DevRow { const char* file; const char* name; std::uint16_t v, p; int type; bool mapped; };
    const DevRow devs[] = {{"DualSense", "DualSense Wireless Controller", 0x054C, 0x0CE6, 7, true}, {"XboxSeries", "Xbox Wireless Controller", 0x045E, 0x0B13, 2, true},
                           {"SwitchPro", "Nintendo Switch Pro Controller", 0x057E, 0x2009, 5, true}, {"Generic", "USB Gamepad (unknown maker)", 0x1234, 0x5678, -1, false}};
    for (const DevRow& d : devs) {
        PadProfiles::Reset(); Reset(); Prefs::Clear();
        PadSet one;
        one.Attach(MakeDev(d.name, d.v, d.p, d.type, d.mapped));
        std::vector<char> k; one.Feed(k);
        Input::FeedGamepadRaw(0, {}, {}, 0, 0, 1u << Input::PadA, true); InputActions::Tick(0.016f);
        if (!d.mapped) PadProfiles::BeginSetup(0);
        one.Feed(k);
        Capture(std::string("rebinding-") + d.file + "-" + res, [&] {
            FullWindow("Controls", [&] {
                ImGui::Text("Controller: %s", d.name);
                ImGui::TextDisabled("Press a button on the controller to rebind; the names follow the controller in use.");
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8 * S, 5 * S));
                if (ImGui::BeginTable("##controls", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableSetupColumn("Action"); ImGui::TableSetupColumn("Keyboard / mouse"); ImGui::TableSetupColumn("Gamepad");
                    ImGui::TableHeadersRow();
                    RemapOptions ro; ro.hidden = {"look", "inventory", "pause", "reload", "aim", "interact", "sprint", "crouch"};
                    for (const RemapRow& r : RemapRows(ro)) {
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.action.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.keyboard.bound ? r.keyboard.label.c_str() : "-");
                        ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(r.gamepad.bound ? r.gamepad.label.c_str() : "-");
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleVar();
            }, 10, 10, 760, 260);
        });
        Capture(std::string("controller-station-") + d.file + "-" + res, [&] {
            FullWindow("H. Controller   (the playground's Controller station)", [&] {
                ImGui::TextDisabled("P next pad   U Controller Setup (I skip, O back, M cancel, N save, L clear)   Y light   T trigger buzz");
                std::string info = PadProfiles::Describe(0);
                std::size_t i = 0;
                while (i < info.size()) { std::size_t e = info.find('\n', i); if (e == std::string::npos) e = info.size(); ImGui::TextUnformatted(info.substr(i, e - i).c_str()); i = e + 1; }
                ImGui::Separator();
                std::string raw = PadProfiles::RawText(0);
                i = 0;
                while (i < raw.size()) { std::size_t e = raw.find('\n', i); if (e == std::string::npos) e = raw.size(); ImGui::TextUnformatted(raw.substr(i, e - i).c_str()); i = e + 1; }
                ImGui::Separator();
                ImGui::Text("names:  A %s   B %s   Back %s   Start %s   LB %s   LT %s", PadProfiles::ButtonName(0, Input::PadA).c_str(), PadProfiles::ButtonName(0, Input::PadB).c_str(),
                            PadProfiles::ButtonName(0, Input::PadBack).c_str(), PadProfiles::ButtonName(0, Input::PadStart).c_str(), PadProfiles::ButtonName(0, Input::PadLB).c_str(), PadProfiles::ButtonName(0, Input::PadLT).c_str());
                ImGui::Text("Jump = %s    Fire = %s", PadProfiles::ButtonName(0, Input::PadA).c_str(), PadProfiles::ButtonName(0, Input::PadRT).c_str());
                if (ControllerSetup* su = PadProfiles::Setup(0); su && su->Active())
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), ">> Step %d of %d: %s", su->StepIndex() + 1, su->StepCount(), su->Prompt().c_str());
                PadEvent ev; (void)ev;
                ImGui::TextDisabled("last event: Controller 1 connected: %s", d.name);
            }, 10, 10, 0, 300);
        });
        one.CloseAll();
    }
}

int main(int argc, char** argv) {
    g_out = argc > 1 ? argv[1] : ".";
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    for (int r = 0; r < 2; ++r) {
        W = r == 0 ? 1280 : 1920; H = r == 0 ? 720 : 1080; S = r == 0 ? 1.0f : 1.5f;
        NewContext();
        if (!g_renderer) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
        Shots();
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
    return 0;
}
