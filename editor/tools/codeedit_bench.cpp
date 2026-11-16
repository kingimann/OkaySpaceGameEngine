// codeedit_bench: the script editor's two code widgets side by side, headless (no window, no GPU).
//   - ImGuiColorTextEdit (editor/vendor/TextEditor.*) on Dear ImGui, drawn with SDL's software renderer into a surface
//   - our OkayUI::CodeEdit through the script editor's adapter (editor/ScriptEditorCodeEdit.*), drawn with OkayUI's software rasteriser
//
//   codeedit_bench bench [lines...]        per-frame CPU time (idle, typing, scrolling), SetText time and memory, for each size
//                                          (default 1000 10000 100000); prints a Markdown table
//   codeedit_bench shots <out-directory>   the same script with colouring and an error marker in both editors:
//                                          codeedit-imgui.png, codeedit-own.png (launcher look, as in the editor),
//                                          codeedit-own-classic.png (OkayUI's 8x8 font)
//
// What is timed is the widget's own frame on the CPU: ImGui::NewFrame .. ImGui::Render for the ImGui editor, OkayUI::BeginFrame ..
// OkayUI::EndFrameData for ours. Rasterising is not timed (a GPU does that in the real editor). Memory is the bytes the process
// holds through operator new after the editor was created, given the text and drawn for a while, minus the bytes before.
// Written for this repository from the two widgets' public APIs; no code from either library's own benchmarks or tests.
#include "TextEditor.h"
#include "ScriptEditorCodeEdit.hpp"
#include "OkayUIInImGui.hpp"
#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"
#include "OkayUITestKit.hpp"
#include "JetBrainsMonoFont.h"   // the script editor's code font (JetBrains Mono, OFL), loaded the way editor/main.cpp loads it

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// ---- live heap bytes: every operator new carries a 16-byte header with its size ----
static std::atomic<long long> g_live{0};
void* operator new(std::size_t n) {
    void* p = std::malloc(n + 16);
    if (!p) throw std::bad_alloc();
    *static_cast<std::size_t*>(p) = n;
    g_live += (long long)n;
    return static_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    char* b = static_cast<char*>(p) - 16;
    g_live -= (long long)*reinterpret_cast<std::size_t*>(b);
    std::free(b);
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

namespace {

using Clock = std::chrono::steady_clock;
double MsSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

const int kW = 1280, kH = 800;          // the frame for the benchmark
// The script editor's code font sizes (editor/main.cpp kCodeFontBakePx, kCodeFontDispPx) and left margin (SetLeftMargin(22)).
const float kCodeBakePx = 48.0f, kCodeDispPx = 16.0f;
const int kLeftMargin = 22;
const float kEdX = 40, kEdY = 40, kEdW = 1000, kEdH = 700;

// ---- the script editor's language and palette (the same values as editor/main.cpp's OkayScriptLangDef and OkayScriptPalette) ----
const char* kKeywords[] = {"if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case", "default", "var", "let",
    "const", "function", "func", "def", "class", "struct", "enum", "new", "public", "private", "static", "void", "int", "float",
    "double", "bool", "string", "true", "false", "null", "this", "foreach", "in", "and", "or", "not"};
const char* kTypes[] = {"GameObject", "Transform", "Vector3", "Vector2", "Quaternion", "Color", "Mathf", "Time", "Input", "Debug",
    "Random", "List", "Dictionary"};
const char* kKnown[] = {"print", "spawn", "destroy", "transform", "deltaTime", "position", "rotate", "move", "find", "getKey"};

TextEditor::Palette Palette() {
    return TextEditor::Palette{{0xffc6b7a9, 0xff3278cc, 0xffbb9768, 0xff59876a, 0xff59876a, 0xffc8c8c8, 0xff808080, 0xffc6b7a9,
        0xffaa7698, 0xffaa7698, 0xff808a80, 0xff808a80, 0xff241f1c, 0xffe8e6e2, 0x8c915c3a, 0x80302820, 0x40eca25c, 0xffc4bbb0,
        0x3a403028, 0x22403028, 0x55ffffff, 0xffb0c94e, 0xff6dc6ff, 0xffc49ab0}};
}

TextEditor::LanguageDefinition Language() {
    TextEditor::LanguageDefinition d;
    for (const char* k : kKeywords) d.mKeywords.insert(k);
    for (const char* t : kTypes) {
        TextEditor::Identifier id; id.mDeclaration = std::string(t) + " (built-in type)";
        d.mIdentifiers.insert(std::make_pair(std::string(t), id));
        d.mTypeNames.insert(t);
    }
    for (const char* k : kKnown) { TextEditor::Identifier id; id.mDeclaration = std::string(k) + "(...) built-in"; d.mIdentifiers.insert(std::make_pair(std::string(k), id)); }
    d.mSemantic = true;
    using P = TextEditor::PaletteIndex;
    d.mTokenRegexStrings.push_back({"[$@]?\\\"(\\\\.|[^\\\"])*\\\"", P::String});
    d.mTokenRegexStrings.push_back({"#.*", P::Comment});
    d.mTokenRegexStrings.push_back({"'(\\\\.|[^'\\\\])'", P::CharLiteral});
    d.mTokenRegexStrings.push_back({"[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", P::Number});
    d.mTokenRegexStrings.push_back({"[a-zA-Z_][a-zA-Z0-9_]*", P::Identifier});
    d.mTokenRegexStrings.push_back({"[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", P::Punctuation});
    d.mCommentStart = "/*"; d.mCommentEnd = "*/"; d.mSingleLineComment = "//";
    d.mCaseSensitive = true; d.mAutoIndentation = true; d.mName = "OkayScript";
    return d;
}

// The same words and colours for ours, mapped the way editor/ScriptEditorWidget.hpp maps them.
void ConfigureOwn(scripteditor::ScriptEditorCodeEdit& s) {
    const TextEditor::LanguageDefinition d = Language();
    std::unordered_set<std::string> kw(d.mKeywords.begin(), d.mKeywords.end()), types(d.mTypeNames.begin(), d.mTypeNames.end()), known;
    std::unordered_map<std::string, std::string> info;
    for (const auto& id : d.mIdentifiers) { if (!types.count(id.first)) known.insert(id.first); info[id.first] = id.second.mDeclaration; }
    s.SetLanguageWords(kw, types, known, std::move(info));
    const TextEditor::Palette p = Palette();
    std::uint32_t c[(int)TextEditor::PaletteIndex::Max];
    for (int i = 0; i < (int)TextEditor::PaletteIndex::Max; ++i) c[i] = p[(size_t)i];
    s.SetPalette(c, (int)TextEditor::PaletteIndex::Max);
    s.SetTabSize(4);
    s.SetLineSpacing(1.55f);
    s.SetLeftMargin(kLeftMargin);   // editor/main.cpp's SetLeftMargin(22), forwarded by ScriptEditorWidget
}

// ---- a script of n lines: classes, functions, strings, numbers, comments, a block comment now and then ----
std::string MakeScript(int n) {
    std::string out;
    out.reserve((size_t)n * 40);
    int i = 0;
    char buf[256];
    while (i < n) {
        const int k = i % 20;
        switch (k) {
            case 0:  std::snprintf(buf, sizeof buf, "class Mover%d {", i); break;
            case 1:  std::snprintf(buf, sizeof buf, "    // speed in metres per second, tuned for level %d", i / 20); break;
            case 2:  std::snprintf(buf, sizeof buf, "    public float speed = %d.5f;", i % 97); break;
            case 3:  std::snprintf(buf, sizeof buf, "    var name = \"mover #%d\";", i); break;
            case 4:  std::snprintf(buf, sizeof buf, "    func update(dt) {"); break;
            case 5:  std::snprintf(buf, sizeof buf, "        if (Input.getKey(\"w\") and speed > 0) {"); break;
            case 6:  std::snprintf(buf, sizeof buf, "            transform.position = transform.position + Vector3(0, 0, speed * dt);"); break;
            case 7:  std::snprintf(buf, sizeof buf, "        } else {"); break;
            case 8:  std::snprintf(buf, sizeof buf, "            print(\"idle \" + name);   # a hash comment"); break;
            case 9:  std::snprintf(buf, sizeof buf, "        }"); break;
            case 10: std::snprintf(buf, sizeof buf, "        for (var j = 0; j < %d; j = j + 1) { rotate(j * 0.25); }", i % 13 + 1); break;
            case 11: std::snprintf(buf, sizeof buf, "    }"); break;
            case 12: std::snprintf(buf, sizeof buf, "    /* block comment line one"); break;
            case 13: std::snprintf(buf, sizeof buf, "       block comment line two */"); break;
            case 14: std::snprintf(buf, sizeof buf, "    func hit(other) {"); break;
            case 15: std::snprintf(buf, sizeof buf, "        let c = Color(1.0, 0.%d, 0.2);", i % 10); break;
            case 16: std::snprintf(buf, sizeof buf, "        destroy(other);"); break;
            case 17: std::snprintf(buf, sizeof buf, "        return true;"); break;
            case 18: std::snprintf(buf, sizeof buf, "    }"); break;
            default: std::snprintf(buf, sizeof buf, "}"); break;
        }
        out += buf;
        if (++i < n) out += '\n';
    }
    return out;
}

// ---- Dear ImGui, headless: SDL's software renderer on a surface (no window, no video driver) ----
struct ImGuiRig {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    ImGuiContext* ctx = nullptr;
    ImGuiRig(int w, int h) {
        surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
        ren = SDL_CreateSoftwareRenderer(surf);
        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::StyleColorsDark();
        {   // the code font exactly as the editor loads it: JetBrains Mono baked large, shown at 16 px
            ImFontConfig mc; mc.OversampleH = 2; mc.OversampleV = 2; mc.PixelSnapH = true;
            io.Fonts->AddFontDefault();
            codeFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(JetBrainsMono_compressed_data_base85, kCodeBakePx, &mc);
        }
        ImGui_ImplSDLRenderer2_Init(ren);
    }
    ImFont* codeFont = nullptr;
    void PushCode() { if (codeFont) ImGui::PushFont(codeFont, kCodeDispPx); }
    void PopCode() { if (codeFont) ImGui::PopFont(); }
    ~ImGuiRig() {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui::DestroyContext(ctx);
        SDL_DestroyRenderer(ren);
        SDL_FreeSurface(surf);
    }
    // One frame with the editor in a window; returns the CPU time of the ImGui part.
    double Frame(TextEditor& te, bool present = false) {
        ImGui_ImplSDLRenderer2_NewFrame();
        const auto t0 = Clock::now();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(kEdX, kEdY));
        ImGui::SetNextWindowSize(ImVec2(kEdW, kEdH));
        ImGui::Begin("Script", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        PushCode();
        te.Render("##code", ImVec2(0, 0), true);
        PopCode();
        ImGui::End();
        ImGui::Render();
        const double ms = MsSince(t0);
        // the backend still has to process texture requests (the font atlas) even when nothing is shown
        ImDrawData* dd = ImGui::GetDrawData();
        if (present) {
            SDL_SetRenderDrawColor(ren, 20, 20, 24, 255);
            SDL_RenderClear(ren);
            ImGui_ImplSDLRenderer2_RenderDrawData(dd, ren);
            SDL_RenderFlush(ren);
        } else if (dd && dd->Textures) {
            for (ImTextureData* tx : *dd->Textures) if (tx->Status != ImTextureStatus_OK) ImGui_ImplSDLRenderer2_UpdateTexture(tx);
        }
        return ms;
    }
    bool Save(const std::string& path) {
        OkayUI::soft::Bitmap b(surf->w, surf->h);
        SDL_LockSurface(surf);
        for (int y = 0; y < surf->h; ++y) std::memcpy(&b.rgba[(size_t)y * surf->w * 4], (const char*)surf->pixels + (size_t)y * surf->pitch, (size_t)surf->w * 4);
        SDL_UnlockSurface(surf);
        return okayuitest::WritePng(path, b);
    }
};

// ---- OkayUI ----
struct OwnRig {
    okayuitest::Sim sim;
    bool open = true;
    OwnRig(int w, int h) : sim((float)w, (float)h) { okayuitest::Reset(); }
    double Frame(scripteditor::ScriptEditorCodeEdit& se) {
        const auto t0 = Clock::now();
        sim.Frame([&] {
            OkayUI::BareNextWindow();
            if (OkayUI::Begin("Script", kEdX, kEdY, kEdW, kEdH)) se.Edit().Draw("##code", kEdW, kEdH);
            OkayUI::End();
        });
        return MsSince(t0);
    }
};

// ---- ours as the editor hosts it: inside the ImGui window, drawn into its draw list (editor/ScriptEditorWidget.hpp Render) ----
struct HostedRig {
    ImGuiRig im;
    okayuitest::Sim sim;
    OkayUI::CapturedGeometry geo;
    float x = kEdX, y = kEdY, w = kEdW, h = kEdH;
    HostedRig(int W, int H) : im(W, H), sim((float)W, (float)H) { okayuitest::Reset(); OkayUI::UseLauncherLook(true); }
    ~HostedRig() { OkayUI::UseLauncherLook(false); }
    // The same steps as ScriptEditorWidget::Render's OkayUI branch (at 100 % UI scale and zoom).
    void Host(scripteditor::ScriptEditorCodeEdit& se) {
        ImVec2 a = ImGui::GetContentRegionAvail();
        a.x = std::max(a.x, 8.0f); a.y = std::max(a.y, 8.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##code", a);
        okayimgui::StyleLikeImGuiChild(se.Edit(), ImGui::GetStyle());
        OkayUI::PushTextScale(1.0f);
        okayimgui::DrawOkayUIHere("##code", p, a, [&] { se.Edit().Draw("##codeedit", a.x, a.y); }, okayimgui::SdlRendererTextures(im.ren), geo);
        OkayUI::PopTextScale();
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + a.x, p.y + a.y), ImGui::GetColorU32(ImGuiCol_Border), st.ChildRounding, 0,
                                            st.ChildBorderSize > 0.0f ? st.ChildBorderSize : 1.0f);
    }
    double Frame(scripteditor::ScriptEditorCodeEdit& se, bool present = false) {
        ImGui_ImplSDLRenderer2_NewFrame();
        const auto t0 = Clock::now();
        sim.Frame([&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(x, y));
            ImGui::SetNextWindowSize(ImVec2(w, h));
            ImGui::Begin("Script", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            Host(se);
            ImGui::End();
            ImGui::Render();
        });
        const double ms = MsSince(t0);
        ImDrawData* dd = ImGui::GetDrawData();
        if (present) {
            SDL_SetRenderDrawColor(im.ren, 20, 20, 24, 255);
            SDL_RenderClear(im.ren);
            ImGui_ImplSDLRenderer2_RenderDrawData(dd, im.ren);
            SDL_RenderFlush(im.ren);
        } else if (dd && dd->Textures) {
            for (ImTextureData* tx : *dd->Textures) if (tx->Status != ImTextureStatus_OK) ImGui_ImplSDLRenderer2_UpdateTexture(tx);
        }
        return ms;
    }
};

struct Stats { double mean = 0, p95 = 0, max = 0; };
Stats Summarise(std::vector<double> v) {
    Stats s;
    if (v.empty()) return s;
    for (double x : v) s.mean += x;
    s.mean /= (double)v.size();
    std::sort(v.begin(), v.end());
    s.p95 = v[(size_t)std::min(v.size() - 1, (size_t)(v.size() * 0.95))];
    s.max = v.back();
    return s;
}

struct Result { double setText = 0, firstFrame = 0; Stats idle, typing, scrolling; long long bytes = 0; };

const int kWarm = 60, kFrames = 240;

Result BenchImGui(const std::string& text) {
    Result r;
    ImGuiRig rig(kW, kH);
    for (int i = 0; i < 3; ++i) { TextEditor warm; rig.Frame(warm); }   // the font atlas and the context's own first-frame work
    const long long before = g_live.load();
    {
        TextEditor te;
        te.SetLanguageDefinition(Language());
        te.SetPalette(Palette());
        te.SetTabSize(4);
        te.SetLineSpacing(1.55f);
        te.SetShowWhitespaces(false);   // as the script editor sets it
        te.SetLeftMargin(kLeftMargin);
        auto t0 = Clock::now();
        te.SetText(text);
        r.setText = MsSince(t0);
        r.firstFrame = rig.Frame(te);
        std::vector<double> v;
        for (int i = 0; i < kWarm; ++i) rig.Frame(te);
        for (int i = 0; i < kFrames; ++i) v.push_back(rig.Frame(te));
        r.idle = Summarise(v);
        r.bytes = g_live.load() - before;
        // click into the text so the editor has the keyboard, put the caret in the middle, type
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(kEdX + 300, kEdY + 100);
        rig.Frame(te);
        io.AddMouseButtonEvent(0, true); rig.Frame(te);
        io.AddMouseButtonEvent(0, false); rig.Frame(te);
        const int mid = te.GetTotalLines() / 2;
        te.SetCursorPosition(TextEditor::Coordinates(mid, 4));
        rig.Frame(te);
        v.clear();
        for (int i = 0; i < kFrames; ++i) { io.AddInputCharacter((unsigned)('a' + i % 26)); v.push_back(rig.Frame(te)); }
        r.typing = Summarise(v);
        if (te.GetText().find("abcdefghij") == std::string::npos) std::fprintf(stderr, "warning: ImGui typing did not land\n");
        te.SetCursorPosition(TextEditor::Coordinates(0, 0));
        rig.Frame(te);
        v.clear();
        for (int i = 0; i < kFrames; ++i) { io.AddMouseWheelEvent(0.0f, -1.0f); v.push_back(rig.Frame(te)); }
        r.scrolling = Summarise(v);
    }
    return r;
}

Result BenchOwn(const std::string& text, bool launcher) {
    Result r;
    OwnRig rig(kW, kH);
    OkayUI::UseLauncherLook(launcher);   // the editor runs OkayUI in the launcher look (Roboto); the classic look is the 8x8 font
    { scripteditor::ScriptEditorCodeEdit warm; rig.Frame(warm); }
    const long long before = g_live.load();
    {
        scripteditor::ScriptEditorCodeEdit se;
        ConfigureOwn(se);
        auto t0 = Clock::now();
        se.SetText(text);
        r.setText = MsSince(t0);
        r.firstFrame = rig.Frame(se);
        std::vector<double> v;
        for (int i = 0; i < kWarm; ++i) rig.Frame(se);
        for (int i = 0; i < kFrames; ++i) v.push_back(rig.Frame(se));
        r.idle = Summarise(v);
        r.bytes = g_live.load() - before;
        rig.sim.Move(kEdX + 300, kEdY + 100);
        rig.Frame(se);
        rig.sim.Down(); rig.Frame(se);
        rig.sim.Up(); rig.Frame(se);
        const int mid = se.GetTotalLines() / 2;
        se.SetCursorPosition(scripteditor::Coord(mid, 4));
        rig.Frame(se);
        v.clear();
        for (int i = 0; i < kFrames; ++i) { rig.sim.Type(std::string(1, (char)('a' + i % 26))); v.push_back(rig.Frame(se)); }
        r.typing = Summarise(v);
        if (se.GetText().find("abcdefghij") == std::string::npos) std::fprintf(stderr, "warning: OkayUI typing did not land\n");
        se.SetCursorPosition(scripteditor::Coord(0, 0));
        rig.Frame(se);
        v.clear();
        for (int i = 0; i < kFrames; ++i) { rig.sim.wheel = -1.0f; v.push_back(rig.Frame(se)); }
        r.scrolling = Summarise(v);
    }
    return r;
}

Result BenchHosted(const std::string& text) {
    Result r;
    HostedRig rig(kW, kH);
    { scripteditor::ScriptEditorCodeEdit warm; for (int i = 0; i < 3; ++i) rig.Frame(warm); }
    const long long before = g_live.load();
    {
        scripteditor::ScriptEditorCodeEdit se;
        ConfigureOwn(se);
        auto t0 = Clock::now();
        se.SetText(text);
        r.setText = MsSince(t0);
        r.firstFrame = rig.Frame(se);
        std::vector<double> v;
        for (int i = 0; i < kWarm; ++i) rig.Frame(se);
        for (int i = 0; i < kFrames; ++i) v.push_back(rig.Frame(se));
        r.idle = Summarise(v);
        r.bytes = g_live.load() - before;
        rig.sim.Move(kEdX + 300, kEdY + 100);
        rig.Frame(se);
        rig.sim.Down(); rig.Frame(se);
        rig.sim.Up(); rig.Frame(se);
        const int mid = se.GetTotalLines() / 2;
        se.SetCursorPosition(scripteditor::Coord(mid, 4));
        rig.Frame(se);
        v.clear();
        for (int i = 0; i < kFrames; ++i) { rig.sim.Type(std::string(1, (char)('a' + i % 26))); v.push_back(rig.Frame(se)); }
        r.typing = Summarise(v);
        if (se.GetText().find("abcdefghij") == std::string::npos) std::fprintf(stderr, "warning: hosted OkayUI typing did not land\n");
        se.SetCursorPosition(scripteditor::Coord(0, 0));
        rig.Frame(se);
        v.clear();
        for (int i = 0; i < kFrames; ++i) { rig.sim.wheel = -1.0f; v.push_back(rig.Frame(se)); }
        r.scrolling = Summarise(v);
    }
    return r;
}

int Bench(const std::vector<int>& sizes) {
    std::printf("| Lines | Editor | SetText ms | first frame ms | idle frame ms (mean / p95) | typing frame ms (mean / p95) | scrolling frame ms (mean / p95) | memory MB |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    for (int n : sizes) {
        const std::string text = MakeScript(n);
        const Result a = BenchImGui(text);
        const Result b = BenchOwn(text, false);
        const Result c = BenchOwn(text, true);
        const Result d = BenchHosted(text);
        OkayUI::UseLauncherLook(false);
        auto row = [&](const char* name, const Result& r) {
            std::printf("| %d | %s | %.2f | %.2f | %.3f / %.3f | %.3f / %.3f | %.3f / %.3f | %.1f |\n", n, name, r.setText, r.firstFrame,
                        r.idle.mean, r.idle.p95, r.typing.mean, r.typing.p95, r.scrolling.mean, r.scrolling.p95, (double)r.bytes / (1024.0 * 1024.0));
        };
        row("ImGuiColorTextEdit", a);
        row("OkayUI::CodeEdit, 8x8 font", b);
        row("OkayUI::CodeEdit, launcher look, OkayUI frame only", c);
        row("OkayUI::CodeEdit hosted in the ImGui window (as in the editor)", d);
        std::fflush(stdout);
    }
    return 0;
}

// ---- screenshots ----
const char* kShotScript =
    "// Mover: walks forward while W is held\n"
    "class Mover {\n"
    "    public float speed = 4.5f;\n"
    "    var label = \"player #1\";\n"
    "\n"
    "    func update(dt) {\n"
    "        if (Input.getKey(\"w\") and speed > 0) {\n"
    "            transform.position = transform.position + Vector3(0, 0, speed * dt);\n"
    "        } else {\n"
    "            print(\"idle \" + labl);   # typo: labl\n"
    "        }\n"
    "        /* turn a little\n"
    "           every frame */\n"
    "        rotate(0.25 * dt);\n"
    "    }\n"
    "}\n";

int Shots(const std::string& dir, const std::string& suffix) {
    const std::string prefix = "codeedit-";
    const int W = 760, H = 400;
    const float x = 10, y = 10, w = 740, h = 380;
    const std::map<int, std::string> marks{{10, "unknown name 'labl' (did you mean 'label'?)"}};
    bool ok = true;
    {
        ImGuiRig rig(W, H);
        TextEditor te;
        te.SetLanguageDefinition(Language());
        te.SetPalette(Palette());
        te.SetTabSize(4);
        te.SetLineSpacing(1.55f);
        te.SetShowWhitespaces(false);   // as the script editor sets it
        te.SetLeftMargin(kLeftMargin);
        te.SetText(kShotScript);
        te.SetErrorMarkers(marks);
        te.SetBreakpoints({14});
        te.SetCursorPosition(TextEditor::Coordinates(6, 12));
        for (int i = 0; i < 4; ++i) {
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(x, y));
            ImGui::SetNextWindowSize(ImVec2(w, h));
            ImGui::Begin("Script", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            if (i == 0) ImGui::SetNextWindowFocus();
            rig.PushCode();
            te.Render("##code", ImVec2(0, 0), true);
            rig.PopCode();
            ImGui::End();
            ImGui::Render();
            SDL_SetRenderDrawColor(rig.ren, 20, 20, 24, 255);
            SDL_RenderClear(rig.ren);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), rig.ren);
            SDL_RenderFlush(rig.ren);
        }
        std::printf("ImGuiColorTextEdit: row height %.3f, character width %.3f, text starts %.1f px right of the content origin\n",
                    te.RowHeight(), te.CharWidth(), te.TextStartX() - te.ContentOrigin().x);
        ok = rig.Save(dir + "/" + prefix + "imgui" + suffix + ".png") && ok;
    }
    {   // ours as the editor shows it: launcher look, hosted in the ImGui window (drawn into its draw list)
        HostedRig rig(W, H);
        rig.x = x; rig.y = y; rig.w = w; rig.h = h;
        scripteditor::ScriptEditorCodeEdit se;
        ConfigureOwn(se);
        se.SetText(std::string(kShotScript).substr(0, std::strlen(kShotScript) - 1));
        se.SetErrorMarkers(marks);
        se.SetBreakpoints({14});
        se.Edit().Focus();
        se.SetCursorPosition(scripteditor::Coord(6, 12));
        for (int i = 0; i < 4; ++i) rig.Frame(se, true);
        std::printf("OkayUI::CodeEdit (launcher look, hosted in ImGui): row height %.3f, character width %.3f, text starts %.1f px right of the content origin\n",
                    se.RowHeight(), se.CharWidth(), se.TextStartX() - se.ContentOrigin().x);
        ok = rig.im.Save(dir + "/" + prefix + "own" + suffix + ".png") && ok;
    }
    for (int look = 1; look < 2; ++look) {
        okayuitest::Reset();
        okayuitest::Sim sim((float)W, (float)H);
        scripteditor::ScriptEditorCodeEdit se;
        ConfigureOwn(se);
        se.SetText(std::string(kShotScript).substr(0, std::strlen(kShotScript) - 1));
        se.SetErrorMarkers(marks);
        se.SetBreakpoints({14});
        se.Edit().Focus();
        se.SetCursorPosition(scripteditor::Coord(6, 12));
        for (int i = 0; i < 4; ++i)
            sim.Frame([&] {
                OkayUI::BareNextWindow();
                if (OkayUI::Begin("Script", x, y, w, h)) se.Edit().Draw("##code", w, h);
                OkayUI::End();
            });
        const okayuitest::Bitmap& b = sim.Draw(20, 20, 24);
        std::printf("OkayUI::CodeEdit (%s): row height %.3f, character width %.3f, text starts %.1f px right of the content origin\n",
                    look == 0 ? "launcher look" : "8x8 font", se.RowHeight(), se.CharWidth(), se.TextStartX() - se.ContentOrigin().x);
        ok = okayuitest::WritePng(dir + "/" + prefix + (look == 0 ? "own" : "own-classic") + suffix + ".png", b) && ok;
        OkayUI::UseLauncherLook(false);
    }
    std::printf(ok ? "wrote %s/codeedit-{imgui,own,own-classic}%s.png\n" : "failed to write into %s%s\n", dir.c_str(), suffix.c_str());
    return ok ? 0 : 1;
}

} // namespace

// A simple pixel difference between two screenshots of the same size: the mean absolute difference per channel (0..255, RGB),
// the share of pixels where any channel differs by more than 32, and (optional) a picture of the difference (brighter = more).
int Diff(const std::string& a, const std::string& b, const std::string& out) {
    const okayuitest::Png pa = okayuitest::ReadPng(a), pb = okayuitest::ReadPng(b);
    if (!pa.ok || !pb.ok || pa.w != pb.w || pa.h != pb.h) { std::fprintf(stderr, "cannot compare %s and %s\n", a.c_str(), b.c_str()); return 1; }
    double sum = 0; long long far = 0;
    okayuitest::Bitmap d(pa.w, pa.h);
    for (int i = 0; i < pa.w * pa.h; ++i) {
        int m = 0;
        for (int c = 0; c < 3; ++c) { const int v = std::abs((int)pa.rgba[(size_t)i * 4 + c] - (int)pb.rgba[(size_t)i * 4 + c]); sum += v; m = std::max(m, v); }
        if (m > 32) ++far;
        for (int c = 0; c < 3; ++c) d.rgba[(size_t)i * 4 + c] = (unsigned char)std::min(255, m * 2);
        d.rgba[(size_t)i * 4 + 3] = 255;
    }
    const double n = (double)pa.w * pa.h;
    std::printf("mean absolute difference %.2f / 255 per channel; %.1f%% of pixels differ by more than 32\n", sum / (n * 3.0), 100.0 * (double)far / n);
    if (!out.empty()) okayuitest::WritePng(out, d);
    return 0;
}

// For a profiler: only idle frames of one editor ("idle own 100000 2000", "idle imgui 100000 2000", "idle hosted 1000 2000": ours in
// the ImGui window as the editor runs it); "empty" draws the same window with no editor in it, the frame cost both toolkits pay anyway.
int Idle(const std::string& which, int lines, int frames, bool launcher) {
    const std::string text = MakeScript(lines);
    std::vector<double> v;
    if (which == "imgui") {
        ImGuiRig rig(kW, kH);
        TextEditor te;
        te.SetLanguageDefinition(Language()); te.SetPalette(Palette()); te.SetShowWhitespaces(false);
        te.SetText(text);
        for (int i = 0; i < frames; ++i) v.push_back(rig.Frame(te));
    } else if (which == "imgui-empty") {
        ImGuiRig rig(kW, kH);
        for (int i = 0; i < frames; ++i) {
            ImGui_ImplSDLRenderer2_NewFrame();
            const auto t0 = Clock::now();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(kEdX, kEdY)); ImGui::SetNextWindowSize(ImVec2(kEdW, kEdH));
            ImGui::Begin("Script", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            ImGui::End();
            ImGui::Render();
            v.push_back(MsSince(t0));
            ImDrawData* dd = ImGui::GetDrawData();
            if (dd && dd->Textures) for (ImTextureData* tx : *dd->Textures) if (tx->Status != ImTextureStatus_OK) ImGui_ImplSDLRenderer2_UpdateTexture(tx);
        }
    } else if (which == "hosted") {   // ours inside the ImGui window, as the editor runs it
        HostedRig rig(kW, kH);
        scripteditor::ScriptEditorCodeEdit se;
        ConfigureOwn(se);
        se.SetText(text);
        for (int i = 0; i < frames; ++i) v.push_back(rig.Frame(se));
    } else {
        OwnRig rig(kW, kH);
        if (launcher) OkayUI::UseLauncherLook(true);
        scripteditor::ScriptEditorCodeEdit se;
        ConfigureOwn(se);
        se.SetText(text);
        for (int i = 0; i < frames; ++i) {
            if (which == "own-empty") {
                const auto t0 = Clock::now();
                rig.sim.Frame([&] { OkayUI::BareNextWindow(); OkayUI::Begin("Script", kEdX, kEdY, kEdW, kEdH); OkayUI::End(); });
                v.push_back(MsSince(t0));
            } else {
                v.push_back(rig.Frame(se));
            }
        }
    }
    const Stats st = Summarise(std::vector<double>(v.begin() + std::min<size_t>(v.size(), 20), v.end()));
    std::printf("%s%s %d lines: idle frame mean %.4f ms, p95 %.4f ms\n", which.c_str(), launcher ? " (launcher look)" : "", lines, st.mean, st.p95);
    return 0;
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "bench";
    if (mode == "shots") return Shots(argc > 2 ? argv[2] : ".", argc > 3 ? argv[3] : "");
    if (mode == "diff" && argc > 3) return Diff(argv[2], argv[3], argc > 4 ? argv[4] : "");
    if (mode == "idle") return Idle(argc > 2 ? argv[2] : "own", argc > 3 ? std::atoi(argv[3]) : 100000, argc > 4 ? std::atoi(argv[4]) : 1000,
                                    argc > 5 && std::string(argv[5]) == "launcher");
    std::vector<int> sizes;
    for (int i = 2; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
    if (sizes.empty()) sizes = {1000, 10000, 100000};
    return Bench(sizes);
}
