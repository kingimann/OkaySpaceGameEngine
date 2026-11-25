// The launcher's Command line page. See CommandLineUI.hpp. Our own code on Dear ImGui's public API; the rules (words,
// completion, history, running okay) are CommandLine.cpp.
#include "CommandLineUI.hpp"

#include "CommandLine.hpp"
#include "NavIcons.hpp"      // the launcher's shared controls: BeginSlateCombo, FitButtonW, PageFooterH, UiPx
#include "imgui_internal.h"   // SetScrollY on the output box's own window

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include "OkayTheme.hpp"   // the theme palette's colours

namespace okaylcli {

namespace {
Session& TheSession() {
    static Session s;
    return s;
}
std::string g_projectRoot, g_projectName;
char g_line[1024] = "";
unsigned g_seenRevision = 0;
bool g_focusInput = true;

int InputCallback(ImGuiInputTextCallbackData* data) {
    Session& s = TheSession();
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        std::string line;
        const bool moved = data->EventKey == ImGuiKey_UpArrow ? s.GetHistory().Up(std::string(data->Buf, (std::size_t)data->BufTextLen), line)
                                                               : s.GetHistory().Down(line);
        if (moved) { data->DeleteChars(0, data->BufTextLen); data->InsertChars(0, line.c_str()); }
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Complete what is left of the cursor; anything after it stays.
        const std::string before(data->Buf, (std::size_t)data->CursorPos);
        const Completion c = Complete(before);
        if (c.line != before) {
            data->DeleteChars(0, data->CursorPos);
            data->InsertChars(0, c.line.c_str());
            data->CursorPos = (int)c.line.size();
        }
        if (!c.choices.empty()) {
            std::string all;
            for (const std::string& w : c.choices) all += (all.empty() ? "" : "   ") + w;
            s.Note(all);
        }
    }
    return 0;
}

void Run(const Host& host) {
    Session& s = TheSession();
    s.SetProgram(host.okayExe);
    s.SetFolder(g_projectRoot);
    const std::string line = g_line;
    s.Submit(line);
    g_line[0] = 0;
    g_focusInput = true;
}

void Place(const Rect& r, ImVec2 origin) { ImGui::SetCursorPos(ImVec2(origin.x + r.x, origin.y + r.y)); }
} // namespace

const char* HintText() {
    return "Runs only the okay program, with what you type as its words; it is not a full shell.";
}

void SetProject(const std::string& root, const std::string& name) {
    g_projectRoot = root;
    g_projectName = name;
    g_focusInput = true;
}

void OpenTerminalFor(const Host& host, const std::string& root, const std::string& name) {
    std::string error;
    const bool ok = OpenTerminal(root, host.toolsDir, error);
    if (host.toast) host.toast(ok ? "Command line opened in " + (name.empty() ? root : name) : error, ok ? 1 : 2);
}

void Tick() { TheSession().Poll(); }

void Shutdown() { TheSession().Stop(); TheSession().Poll(); }

void DrawPage(const Host& host) {
    Session& s = TheSession();
    s.Poll();
    // A project from the list, until the person picks one (or the card's button set it).
    if (g_projectRoot.empty() && !host.projects.empty()) SetProject(host.projects[0].root, host.projects[0].name);

    const ImVec2 origin = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float footer = okl::PageFooterH() + okl::UiPx(6.0f);   // main.cpp's version line is pinned under every page
    const float frameH = ImGui::GetFrameHeight(), lineH = ImGui::GetTextLineHeight();
    PageButtons bw;
    bw.terminal = okl::FitButtonW("Open command line");
    bw.copy = okl::FitButtonW("Copy all");
    bw.clear = okl::FitButtonW("Clear", okl::UiPx(56.0f));
    bw.run = okl::FitButtonW("Run", okl::UiPx(56.0f));
    bw.stop = okl::FitButtonW("Stop", okl::UiPx(56.0f));
    const PageLayout L = LayoutPage(avail.x, std::max(frameH * 6.0f, avail.y - footer), frameH, lineH, bw);

    // ---- the project ----
    Place(L.project, origin);
    ImGui::SetNextItemWidth(L.project.w);
    const std::string shown = g_projectRoot.empty() ? std::string("No project (runs in the launcher's folder)")
                                                    : "Project: " + (g_projectName.empty() ? g_projectRoot : g_projectName);
    if (okl::BeginSlateCombo("##cliproj", shown.c_str())) {
        if (host.projects.empty()) ImGui::TextDisabled("No projects yet. Make one on the Create page.");
        for (const ProjectRef& p : host.projects) {
            ImGui::PushID(p.root.c_str());
            if (ImGui::Selectable(p.name.c_str(), p.root == g_projectRoot)) SetProject(p.root, p.name);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.root.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && !g_projectRoot.empty()) ImGui::SetTooltip("Commands run in %s", g_projectRoot.c_str());

    Place(L.terminal, origin);
    ImGui::BeginDisabled(g_projectRoot.empty());
    if (ImGui::Button("Open command line", ImVec2(L.terminal.w, L.terminal.h))) OpenTerminalFor(host, g_projectRoot, g_projectName);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A full terminal window in the project's folder, where okay works as a command.");
    Place(L.copy, origin);
    if (ImGui::Button("Copy all", ImVec2(L.copy.w, L.copy.h))) ImGui::SetClipboardText(s.Text().c_str());
    Place(L.clear, origin);
    if (ImGui::Button("Clear", ImVec2(L.clear.w, L.clear.h))) s.Clear();

    // ---- the hint ----
    Place(L.hint, origin);
    ImGui::TextDisabled("%s", HintText());
    ImGui::SameLine(0, 6);
    if (ImGui::CalcTextSize(HintText()).x + 6.0f + ImGui::CalcTextSize("Open a full shell").x <= L.hint.w && !g_projectRoot.empty()) {
        if (ImGui::TextLink("Open a full shell")) OpenTerminalFor(host, g_projectRoot, g_projectName);
    } else {
        ImGui::NewLine();
    }

    // ---- the output: read-only text, so it can be selected and copied ----
    Place(L.output, origin);
    if (host.mono) ImGui::PushFont(host.mono, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, okaytheme::kDeep);
    // New lines: show the end. For a few frames, because the box learns its new text height a frame late.
    static int s_follow = 0;
    if (s.Revision() != g_seenRevision) s_follow = 3;
    g_seenRevision = s.Revision();
    static std::string s_empty =
        "Type an okay command below and press Enter, for example:\n"
        "  help            every command\n"
        "  scene check     check the project's scenes\n"
        "  vc status       what changed since the last save\n"
        "Tab completes a command, Up and Down go through earlier ones, clear empties this box.";
    const std::string& text = s.Lines().empty() ? s_empty : s.Text();
    ImGui::InputTextMultiline("##cliout", const_cast<char*>(text.c_str()), text.size() + 1, ImVec2(L.output.w, L.output.h),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_WordWrap);
    {   // The box is a child window, the newest child of this page: scroll it to its end (imgui_internal SetScrollY).
        ImGuiWindow* page = ImGui::GetCurrentWindow();
        if (s_follow > 0 && !page->DC.ChildWindows.empty()) {
            ImGuiWindow* box = page->DC.ChildWindows.back();
            ImGui::SetScrollY(box, box->ScrollMax.y + 1000.0f);   // clamped to the end
            --s_follow;
        }
    }
    ImGui::PopStyleColor();
    if (host.mono) ImGui::PopFont();

    // ---- the line, Run and Stop ----
    Place(L.input, origin);
    if (host.mono) ImGui::PushFont(host.mono, 0.0f);
    ImGui::SetNextItemWidth(L.input.w);
    if (g_focusInput) { ImGui::SetKeyboardFocusHere(); g_focusInput = false; }
    const bool running = s.Running();
    const bool entered = ImGui::InputTextWithHint("##cliline", running ? "Running... press Stop to end it" : "okay command, for example: help",
                                                  g_line, sizeof(g_line),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                                                      ImGuiInputTextFlags_CallbackCompletion,
                                                  InputCallback);
    if (host.mono) ImGui::PopFont();
    Place(L.run, origin);
    ImGui::BeginDisabled(running);
    const bool runClicked = okl::PrimaryButton("Run", ImVec2(L.run.w, L.run.h));
    ImGui::EndDisabled();
    if ((entered || runClicked) && !running) Run(host);
    else if (entered) g_focusInput = true;
    Place(L.stop, origin);
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Stop", ImVec2(L.stop.w, L.stop.h))) s.Stop();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("End the running command, and anything it started.");
    // Leave the cursor under the page so main.cpp's footer goes below it.
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + L.stop.B() + 4.0f));
}

}  // namespace okaylcli
