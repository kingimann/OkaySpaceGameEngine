// OkayUI holds several contexts (okayui/src/OkayUI_Context.cpp, OkayUI_ContextState.hpp), headless: each context has its own
// windows (same title, own position, size and "appearing"), popups, keyboard focus, text being typed, input history (a press in one
// context is no click in the other), frame counter and IO; handles are distinct; GImGui is the current one; the first CreateContext
// hands out the context the program started with; DestroyContext of a second context goes back to the first. Then, with `nm`, that
// every data symbol of the okay_ui library belongs to a context or is on the shared list below (so a new variable cannot be missed).
// Why: the launcher's chat window is a second OS window with its own context (launcher/main.cpp ChatHostCreate).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIImGuiNames.hpp"   // GImGui
#include "okay/UI/OkayUIInternals.hpp"
#include "../src/OkayUI_ContextState.hpp"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
// Data symbols that are shared by every context on purpose (OkayUI_ContextState.hpp says why), by their last name part, or by the
// function they are a static of. Scratch buffers and constant tables are shared because they hold nothing between calls.
const char* const kShared[] = {
    // the frame's geometry: one frame is built and drawn at a time
    "g_verts", "g_idx", "g_ovVerts", "g_ovIdx", "g_nv", "g_ni", "g_onv", "g_oni", "g_spans", "g_nspans", "g_ovSpans", "g_novSpans", "g_cmds",
    "g_dropped", "reorderWindows()::tmp", "reorderWindows()::nsp", "g_windowList", "g_foregroundList", "g_dockHostList",
    "g_hostListDepth",   // the dock host list: nesting of one drawing call, 0 between calls
    // fonts: the atlas, the fallback fonts, the loaded font list (a font pointer works in any context)
    "g_fbFonts", "g_nfb", "kAtlasTex", "kFontBold", "kFontDefault", "g_atlas", "g_built", "Build()::bytes",
    "g_atlasRef", "g_fontPool", "g_nFontPool", "g_fontReqs", "g_nFontReqs", "g_userFonts", "g_nUserFonts", "g_b85", "g_nB85", "g_ttfs",
    "g_nTtfs", "AddFontTtfFile(char const*, float)::keep", "g_boxes",
    // one clipboard for the program; the platform callbacks are the program's
    "g_clip", "GetPlatformIO()::pio",
    // scratch and answers valid until the next call; constant tables
    "MergePendingIo(OkayUI::Input&)::text", "textSlot(int)::sink", "floatState(int, float)::sink", "openState(int, bool)::sink",
    "TextField(int, float, float, float, float, char*, int, unsigned int)::maskBuf", "unitCircle(int)::table", "cornerFan(float, float, float, int, unsigned char const*)::T",
    "cosHalfTurn(int)::t", "roundedRectPath(OkayUI::Vec2*, float, float, float, float, float)::A", "::hex", "GetStyleColorName(OkayUI::Col)::names",
    "GetDragDropPayload()::copy", "AcceptDragDropPayload(char const*, int)::out", "GetTopMostPopupModal()::ref", "TableGetSortSpecs()::arr",
    "TableGetSortSpecs()::out", "SaveIniSettingsToMemory(unsigned long*)::s", "FillContextFromWindowTable(OkayUI::Context&, unsigned int, unsigned int, unsigned int, unsigned int)::popRefs",
    "GetStyle()::s", "finalizeFrame()::reports", "Begin(char const*, float, float, float, float, bool*)::s_bareOpen",
    "UseLauncherLook(bool)::classic", "UseLauncherLook(bool)::other",
    // the design system (okay/UI/OkayUIDesign.hpp): the chosen design is the program's, the same in every window
    "design::g_lastPoll", "design::g_cur", "design::g_rev", "design::g_stamp", "design::g_follow", "Designs()::kAll",
    // the state of the one item being dragged or typed into: one item is held at a time in the whole program
    "::s_logT", "::s_pastThreshold", "colorEditRow_ed(char const*, float*, int, int)::hexBuf", "colorEditRow_ed(char const*, float*, int, int)::hexId",
    "colorEditRow_ed(char const*, float*, int, int)::hexNext",
    // the accessor functions' own statics, listed through the accessor (C(), S(), T())
    "C()::c", "S()::s", "T()::t",
    // conversion scratch (a pointer handed out for this call) and constants
    "operator OkayUI::ImDrawData*() const::dd", "operator OkayUI::ImDrawData*() const::list", "operator OkayUI::ImDrawData*() const::texList",
    "operator OkayUI::ImDrawData*() const::atlasTex", "operator OkayUI::StateStorage*() const::next", "operator OkayUI::StateStorage*() const::ring",
    "Line[abi:cxx11](int) const::empty",
    // the context manager itself
    "Reg()::r", "DefaultRec()::d", "All()::a", "g_live", "g_current", "g_currentSet", "kContextState_",
};

bool EndsWith(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }
std::string LastPart(const std::string& nIn) {   // "OkayUI::ui_core::g_hot" -> "g_hot"; function statics keep "f(args)::x"
    std::string n = nIn;
    for (std::size_t at; (at = n.find("(anonymous namespace)::")) != std::string::npos;) n.erase(at, 23);
    int depth = 0; std::size_t cut = 0;
    for (std::size_t i = 0; i + 1 < n.size(); ++i) {
        if (n[i] == '(' ) ++depth; else if (n[i] == ')') --depth;
        else if (depth == 0 && n[i] == ':' && n[i + 1] == ':') cut = i + 2;
    }
    std::string last = n.substr(cut);
    // a function static: keep "function(args)::name"
    const std::size_t p = n.rfind(")::");
    if (p != std::string::npos && p + 3 == cut) {
        std::size_t open = n.rfind('(', p); int d = 0;
        for (std::size_t i = p + 1; i-- > 0;) { if (n[i] == ')') ++d; else if (n[i] == '(' && --d == 0) { open = i; break; } }
        std::size_t start = n.rfind("::", open); start = start == std::string::npos ? 0 : start + 2;
        last = n.substr(start);
    }
    return last;
}
bool IsShared(const std::string& nameIn) {
    std::string name = nameIn;
    for (std::size_t at; (at = name.find("(anonymous namespace)::")) != std::string::npos;) name.erase(at, 23);
    const std::string last = LastPart(name);
    for (const char* s : kShared) {
        const std::string k = s;
        if (last == k || EndsWith(name, k) || (k.back() == '_' && last.compare(0, k.size(), k) == 0)) return true;
    }
    return false;
}
} // namespace

int main() {
    RUN_SUITE("okayui contexts");

    {   // handles: the first CreateContext is the start-up context; later ones are new and do not change the current one
        Reset();
        UI::Context* a = UI::CreateContext();
        CHECK(a != nullptr && UI::GetCurrentContext() == a && GImGui == a);
        UI::Context* b = UI::CreateContext();
        CHECK(b != nullptr && b != a && UI::GetCurrentContext() == a);
        UI::SetCurrentContext(b); CHECK(UI::GetCurrentContext() == b && GImGui == b);
        UI::SetCurrentContext(a); CHECK(UI::GetCurrentContext() == a);
        UI::DestroyContext(b); CHECK(UI::GetCurrentContext() == a);
        UI::SetCurrentContext(b); CHECK(UI::GetCurrentContext() == a);   // a destroyed handle is ignored
    }
    {   // windows, popups and appearing: the same titles in two contexts are two windows
        Reset();
        UI::Context* a = UI::CreateContext();
        UI::Context* b = UI::CreateContext();
        Sim sa(400, 300), sb(500, 400);
        int appearA = 0, appearB = 0;
        bool popupA = false, popupB = false;
        auto drawA = [&] {
            UI::SetNextWindowPos({20, 30}, UI::Cond_FirstUseEver); UI::SetNextWindowSize({150, 100}, UI::Cond_FirstUseEver);
            UI::Begin("Chat"); if (UI::IsWindowAppearing()) ++appearA;
            if (sa.frames == 1) UI::OpenPopup("menu");
            popupA = UI::BeginPopup("menu"); if (popupA) { UI::Text("item"); UI::EndPopup(); }
            UI::End();
        };
        auto drawB = [&] {
            UI::SetNextWindowPos({200, 120}, UI::Cond_FirstUseEver); UI::SetNextWindowSize({220, 160}, UI::Cond_FirstUseEver);
            UI::Begin("Chat"); if (UI::IsWindowAppearing()) ++appearB;
            popupB = UI::BeginPopup("menu"); if (popupB) { UI::Text("item"); UI::EndPopup(); }
            UI::End();
        };
        for (int i = 0; i < 6; ++i) {   // the launcher's order: the main window's frame, then the chat window's frame
            UI::SetCurrentContext(a); sa.Frame(drawA);
            UI::SetCurrentContext(b); sb.Frame(drawB);
        }
        CHECK(appearA == 1 && appearB == 1);   // drawn every frame of its own context: appears once (not every frame)
        CHECK(popupA && !popupB);              // the popup opened in A is not open in B
        UI::WindowSlotInfo wa[8], wb[8];
        UI::SetCurrentContext(a); const int na = UI::ListWindowSlots(wa, 8);
        UI::SetCurrentContext(b); const int nb = UI::ListWindowSlots(wb, 8);
        CHECK(na == 1 && nb == 1);
        CHECK(na == 1 && wa[0].pos.x == 20.0f && wa[0].size.x == 150.0f);
        CHECK(nb == 1 && wb[0].pos.x == 200.0f && wb[0].size.x == 220.0f);
        UI::SetCurrentContext(a); CHECK(UI::GetIO().DisplaySize.x == 400.0f);
        UI::SetCurrentContext(b); CHECK(UI::GetIO().DisplaySize.x == 500.0f);
        UI::SetCurrentContext(a); UI::DestroyContext(b);
    }
    {   // input history: a button held down in A's frames does not click in B; each click lands in its own context only
        Reset();
        UI::Context* a = UI::CreateContext();
        UI::Context* b = UI::CreateContext();
        Sim sa(300, 200), sb(300, 200);
        int clicksA = 0, clicksB = 0;
        UI::Rect r{};
        auto ui = [&r](int& n) { return [&n, &r] { UI::SetNextWindowPos({0, 0}); UI::SetNextWindowSize({300, 200}); UI::Begin("w"); if (UI::Button("Go")) ++n; r = UI::GetItemRect(); UI::End(); }; };
        for (int i = 0; i < 3; ++i) { UI::SetCurrentContext(a); sa.Frame(ui(clicksA)); UI::SetCurrentContext(b); sb.Frame(ui(clicksB)); }
        CHECK(r.w > 0.0f);   // (the button sits at the same place in both)
        sa.Move(r.x + 5, r.y + 5); sb.Move(400, 400);
        UI::SetCurrentContext(a); sa.Frame(ui(clicksA)); UI::SetCurrentContext(b); sb.Frame(ui(clicksB));
        sa.Down();
        UI::SetCurrentContext(a); sa.Frame(ui(clicksA)); UI::SetCurrentContext(b); sb.Frame(ui(clicksB));
        sb.Move(r.x + 5, r.y + 5);   // B's pointer moves over its button while A's button is held
        UI::SetCurrentContext(a); sa.Frame(ui(clicksA)); UI::SetCurrentContext(b); sb.Frame(ui(clicksB));
        sa.Up();
        UI::SetCurrentContext(a); sa.Frame(ui(clicksA)); UI::SetCurrentContext(b); sb.Frame(ui(clicksB));
        CHECK(clicksA == 1 && clicksB == 0);
        sb.Down(); UI::SetCurrentContext(a); sa.Frame(ui(clicksA)); UI::SetCurrentContext(b); sb.Frame(ui(clicksB));
        sb.Up();   UI::SetCurrentContext(a); sa.Frame(ui(clicksA)); UI::SetCurrentContext(b); sb.Frame(ui(clicksB));
        CHECK(clicksA == 1 && clicksB == 1);
        UI::SetCurrentContext(a); UI::DestroyContext(b);
    }
    {   // keyboard focus and typed text stay in their context
        Reset();
        UI::Context* a = UI::CreateContext();
        UI::Context* b = UI::CreateContext();
        Sim sa(300, 200), sb(300, 200);
        char bufA[32] = "", bufB[32] = "";
        bool focusB = false;
        auto uiA = [&] { UI::SetNextWindowPos({0, 0}); UI::SetNextWindowSize({300, 200}); UI::Begin("w"); if (sa.frames == 0) UI::SetKeyboardFocusHere(); UI::InputText("##t", bufA, 32); UI::End(); };
        auto uiB = [&] { UI::SetNextWindowPos({0, 0}); UI::SetNextWindowSize({300, 200}); UI::Begin("w"); UI::InputText("##t", bufB, 32); focusB = UI::IsItemActive() || UI::IsItemFocused(); UI::End(); };
        for (int i = 0; i < 3; ++i) { UI::SetCurrentContext(a); sa.Frame(uiA); UI::SetCurrentContext(b); sb.Frame(uiB); }
        sa.text = "hi";
        UI::SetCurrentContext(a); sa.Frame(uiA); UI::SetCurrentContext(b); sb.Frame(uiB);
        sb.text = "zz";   // B has no focused field: nothing is typed anywhere
        UI::SetCurrentContext(a); sa.Frame(uiA); UI::SetCurrentContext(b); sb.Frame(uiB);
        CHECK(std::strcmp(bufA, "hi") == 0);
        CHECK(bufB[0] == 0 && !focusB);
        UI::SetCurrentContext(a); CHECK(UI::GetIO().WantTextInput || UI::GetIO().wantTextInput);
        UI::SetCurrentContext(b); CHECK(!UI::GetIO().WantTextInput && !UI::GetIO().wantTextInput);
        UI::SetCurrentContext(a); UI::DestroyContext(b);
    }
    {   // frame counters and the theme are per context; destroying the live second context brings the first back
        Reset();
        UI::Context* a = UI::CreateContext(); UI::SetCurrentContext(a);
        Sim sa(200, 100); sa.Frames(5, [] {});
        const int fa = UI::GetFrameCount();
        UI::Style().rounding = 3.0f;
        UI::Context* b = UI::CreateContext();
        UI::SetCurrentContext(b);
        CHECK(UI::GetFrameCount() == 0 && UI::Style().rounding != 3.0f);   // a new context starts from the start-up values
        Sim sb(200, 100); sb.Frames(2, [] {});
        CHECK(UI::GetFrameCount() == 2);
        UI::Style().rounding = 11.0f;
        UI::DestroyContext(b);              // the current one: none is current now, the first context's values are back in place
        CHECK(UI::GetCurrentContext() == nullptr);
        UI::SetCurrentContext(a);
        CHECK(UI::GetFrameCount() == fa && UI::Style().rounding == 3.0f);
    }
    {   // every data symbol of the library belongs to a context or is shared on purpose
#if defined(OKAYUI_LIB_PATH) && defined(OKAYUI_NM)
        std::set<std::string> registered;
        for (int i = 0; i < UI::ctxstate::RegisteredCount(); ++i) {
            std::string n = UI::ctxstate::RegisteredName(i);
            const std::size_t p = n.rfind("::"); if (p != std::string::npos && n.find('(') == std::string::npos) n = n.substr(p + 2);
            registered.insert(n);
        }
        CHECK(registered.size() > 200);
        const std::string cmd = std::string(OKAYUI_NM) + " -C --defined-only \"" + OKAYUI_LIB_PATH + "\" 2>/dev/null";
        FILE* f = popen(cmd.c_str(), "r");
        CHECK(f != nullptr);
        int seen = 0; std::vector<std::string> missing;
        char line[4096];
        while (f && std::fgets(line, sizeof line, f)) {
            std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            // "<address> <type> <name>": data in .bss / .data (b B d D)
            if (l.size() < 20) continue;
            const std::size_t sp = l.find(' '); if (sp == std::string::npos || sp + 3 > l.size()) continue;
            const char t = l[sp + 1];
            if (t != 'b' && t != 'B' && t != 'd' && t != 'D') continue;
            const std::string name = l.substr(sp + 3);
            if (name.find("guard variable") == 0 || name.find("typeinfo") == 0 || name.find("vtable") == 0 || name.find("OkayUI") != 0) continue;
            ++seen;
            const std::string last = LastPart(name);
            if (registered.count(last) || IsShared(name)) continue;
            if (name.find(")::") != std::string::npos && registered.count(last.substr(0, last.find("::")))) continue;   // C()::c through C()
            missing.push_back(name);
        }
        if (f) pclose(f);
        CHECK(seen > 250);
        for (const std::string& m : missing) std::fprintf(stderr, "  not listed for a context and not on the shared list: %s\n", m.c_str());
        CHECK(missing.empty());
#else
        std::printf("  (nm check not built on this platform)\n");
#endif
    }
    TEST_MAIN_RESULT();
}
