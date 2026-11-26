// Command Line panel. See CommandLinePanel.hpp. Our own code: the commands are okay's (tools/okay-cli), the typed-line rules
// tools/okay-cli/CommandInput.cpp, the drawing OkayUI's own widgets.
#include "CommandLinePanel.hpp"

#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"   // InputTextWithHint with a callback (Up, Down, Tab), BeginChild with a size

#include <algorithm>
#include <cstring>
#include <utility>

namespace okaycli {
namespace UI = OkayUI;

CommandLinePanel::CommandLinePanel() : CommandLinePanel(Config()) {}

CommandLinePanel::CommandLinePanel(Config config) : cfg_(std::move(config)), log_(cfg_.maxLines) {
    log_.Add("Type an okay command and press Enter (for example: help). Tab completes, Up and Down go through earlier lines.",
             OutLine::Note);
}

void CommandLinePanel::SetFolder(const std::string& folder) {
    if (folder == folder_) return;
    folder_ = folder;
    log_.Add(folder_.empty() ? std::string("No project is open: commands run in this program's folder.")
                             : "Commands run in the project \"" + FolderName() + "\".",
             OutLine::Note);
}

std::string CommandLinePanel::FolderName() const {
    std::string f = folder_;
    while (f.size() > 1 && (f.back() == '/' || f.back() == '\\')) f.pop_back();
    const std::size_t cut = f.find_last_of("/\\");
    return cut == std::string::npos ? f : f.substr(cut + 1);
}

bool CommandLinePanel::Submit(const std::string& line) {
    history_.Push(line);
    choices_.clear();
    switch (Classify(line)) {
    case LineKind::Empty: return false;
    case LineKind::Clear: Clear(); return true;
    case LineKind::Okay: break;
    }
    if (job_.Running()) { Note("A command is still running. Wait for it, or press Stop."); return false; }
    log_.Add("> " + line, OutLine::Note);
    std::vector<std::string> words;
    std::string error;
    if (!SplitWords(line, words, error)) { Note(error); return false; }
    if (!words.empty() && IsOkayWord(words[0])) words.erase(words.begin());
    if (words.empty()) { Note("Type a command for okay, for example: help"); return false; }
    job_.Start(words, folder_);
    follow_ = 3;
    return true;
}

void CommandLinePanel::Pump() {
    std::vector<OutLine> lines;
    int code = 0;
    bool stopped = false;
    const bool ended = job_.Poll(lines, code, stopped);
    for (OutLine& l : lines) log_.Add(l.text, l.kind);
    if (!ended) return;
    lastExit_ = code;
    if (stopped) Note("Stopped.");
    else if (code == 0) Note("Done.");
    else Note("Finished with exit code " + std::to_string(code) + ".");
}

void CommandLinePanel::Stop() { job_.Stop(); }

void CommandLinePanel::Copy() {
    if (cfg_.copy) cfg_.copy(log_.Text());
    else UI::SetClipboardText(log_.Text().c_str());
}

bool CommandLinePanel::HistoryUp() {
    std::string line;
    if (!history_.Up(input_, line)) return false;
    SetInput(line);
    return true;
}

bool CommandLinePanel::HistoryDown() {
    std::string line;
    if (!history_.Down(line)) return false;
    SetInput(line);
    return true;
}

void CommandLinePanel::CompleteInput() {
    const Completion c = Complete(input_);
    choices_ = c.choices;
    SetInput(c.line);
}

void CommandLinePanel::SetInput(const std::string& text) {
    input_ = text.size() < sizeof(buf_) ? text : text.substr(0, sizeof(buf_) - 1);
    std::memcpy(buf_, input_.c_str(), input_.size() + 1);
}

int CommandLinePanel::OnInputKey(int key, char* buf, int cap, int& len) {
    input_.assign(buf, (std::size_t)std::max(len, 0));
    bool changed = false;
    if (key == UI::KeyId_Up) changed = HistoryUp();
    else if (key == UI::KeyId_Down) changed = HistoryDown();
    else if (key == UI::KeyId_Tab) { CompleteInput(); changed = true; }
    if (!changed || cap <= 0) return 0;
    const std::size_t n = std::min(input_.size(), (std::size_t)cap - 1);
    std::memcpy(buf, input_.data(), n);
    buf[n] = '\0';
    len = (int)n;
    return 1;
}

namespace {

int InputCallback(UI::InputTextCallbackData* d) {
    auto* panel = static_cast<CommandLinePanel*>(d->UserData);
    if (!panel || !d->Buf) return 0;
    int len = d->BufTextLen;
    if (panel->OnInputKey(d->EventKey, d->Buf, d->BufSize, len)) {
        d->BufTextLen = len;
        d->CursorPos = d->SelectionStart = d->SelectionEnd = len;
        d->BufDirty = true;
    }
    return 0;
}

// The colours of the three kinds of line: okay's words in the theme's text colour, its problems red, the panel's own notes
// (the command typed, how it ended) dimmed.
void LineText(const OutLine& l) {
    const char* t = l.text.empty() ? " " : l.text.c_str();
    if (l.kind == OutLine::Err) UI::TextColored(240, 112, 102, t);
    else if (l.kind == OutLine::Note) UI::TextDisabled(t);
    else UI::TextUnformatted(t);
}

} // namespace

void CommandLinePanel::Frame(float x, float y, float w, float h) {
    Pump();
    if (log_.Revision() != seenRevision_) { seenRevision_ = log_.Revision(); follow_ = 3; }
    const std::string win = "CommandLine" + cfg_.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin(win.c_str(), x, y, w, h)) {
        UI::PushID(cfg_.idPrefix.c_str());
        // ---- the top row: where commands run, then Stop, Copy and Clear on the right ----
        const float bw = 64.0f, gap = 6.0f;
        const float rowW = UI::GetContentRegionAvail().x;
        const std::string where = folder_.empty() ? std::string("No project") : "Project: " + FolderName();
        UI::AlignTextToFramePadding();
        UI::TextDisabled(where.c_str());
        if (!folder_.empty() && UI::IsItemHovered()) UI::SetTooltip("%s", folder_.c_str());
        UI::SameLine(std::max(rowW - 3.0f * bw - 2.0f * gap, 0.0f), 0.0f);
        UI::BeginDisabled(!job_.Running());
        if (UI::Button("Stop", UI::Vec2{bw, 0.0f})) Stop();
        UI::EndDisabled();
        UI::SameLine(0.0f, gap);
        if (UI::Button("Copy", UI::Vec2{bw, 0.0f})) Copy();
        UI::SameLine(0.0f, gap);
        if (UI::Button("Clear", UI::Vec2{bw, 0.0f})) Clear();

        // ---- the output: every line, scrolled to the end while new lines come ----
        const float rowH = UI::GetFrameHeightWithSpacing();
        const float below = rowH * (choices_.empty() ? 1.0f : 2.0f) + 4.0f;   // the input row, and the choices row (a frame tall) when shown
        const float outH = std::max(UI::GetContentRegionAvail().y - below, 20.0f);
        if (outH != lastOutH_) { lastOutH_ = outH; follow_ = std::max(follow_, 2); }   // the choices row came or went
        if (UI::BeginChild("##out", UI::Vec2{0.0f, outH}, UI::Child_Borders)) {
            // Text rows one font line tall, as a terminal; only the rows in view are laid out (5,000 lines stay cheap).
            UI::PushStyleVar(UI::StyleVar_TextRowHeight, UI::GetFontSize());
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{4.0f, 2.0f});
            UI::ListClipper clip;
            clip.Begin((int)log_.Lines().size());
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) LineText(log_.Lines()[(std::size_t)i]);
            clip.End();
            UI::PopStyleVar(2);
            if (follow_ > 0) { UI::SetScrollY(UI::GetScrollMaxY()); --follow_; }
        }
        UI::EndChild();

        // ---- what Tab found, when more than one command fits ----
        if (!choices_.empty()) {
            std::string list;
            for (const std::string& c : choices_) list += (list.empty() ? "" : "   ") + c;
            UI::TextDisabled(list.c_str());
        }

        // ---- the input line and Run ----
        UI::AlignTextToFramePadding();
        UI::TextUnformatted(">");
        UI::SameLine();
        UI::SetNextItemWidth(std::max(UI::GetContentRegionAvail().x - bw - gap, 40.0f));
        if (focusInput_) { UI::SetKeyboardFocusHere(); focusInput_ = false; }
        std::memcpy(buf_, input_.c_str(), std::min(input_.size() + 1, sizeof(buf_)));
        buf_[sizeof(buf_) - 1] = '\0';
        const unsigned flags = UI::InputText_EnterReturnsTrue | UI::InputText_CallbackHistory | UI::InputText_CallbackCompletion;
        bool run = UI::InputTextWithHint("##input", "okay command, for example: help", buf_, (int)sizeof(buf_), flags, &InputCallback, this);
        input_ = buf_;
        UI::SameLine(0.0f, gap);
        UI::BeginDisabled(job_.Running());
        if (UI::Button("Run", UI::Vec2{bw, 0.0f})) run = true;
        UI::EndDisabled();
        if (run && !input_.empty()) {
            const std::string line = input_;
            if (Submit(line) || Classify(line) != LineKind::Okay) SetInput(std::string());
            focusInput_ = true;
        }
        UI::PopID();
    }
    UI::End();
}

} // namespace okaycli
