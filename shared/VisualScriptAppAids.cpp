// Visual Script app (VisualScriptApp.hpp): the beginner aids and the panels beside the canvas. The start cards over an empty graph,
// the "What is this?" card of the selected block (with its Next steps chips and Try this block), the one-line next-step hint, Pick a
// Recipe and Templates, the right-click menus of a tab, a comment frame and a block's Connect to..., the outline (events, functions,
// breakpoints, the saved values a graph uses with their values while the game runs, function inputs), the paused debugger's call
// stack and variables, and what the editor adds (Attach to Selected Object, Restore Previous Version). The words and the rules are
// the model's (VisualScriptGuide.hpp, VisualScriptStart.hpp, VisualScriptHints.hpp, VisualScriptRecipes.hpp, VsEditorActions.hpp);
// this file draws them and calls them.
//
// Written from general editor behaviour (cards, menus, side panels) and the OkayUI interface; no code, names, icons or look from
// any other editor (rule 19a).
#include "VisualScriptAppImpl.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

namespace vsapp {

// ---- small shared pieces ------------------------------------------------------------------------------------------------------

std::string VisualScriptApp::Impl::project() const {
    if (okay::editor::EditorState* e = ed()) if (!e->projectDir().empty()) return e->projectDir();
    return folder;
}

bool VisualScriptApp::Impl::overCanvasCard(float x, float y) const {
    if (helpCard.x1 > helpCard.x0 && helpCard.in(x, y)) return true;
    return false;
}

// A menu of rows over the app (a tab's, a comment frame's, Connect to...): "" is a separator. The row picked this frame (-1 none);
// `close` is set when it should go away (a pick, a click outside, Escape).
int VisualScriptApp::Impl::menuBox(const std::string& id, float px, float py, const std::vector<std::string>& rows, bool& close) {
    const float lh = UI::GetTextLineHeight() + 6;
    float w = 180, total = 8;
    for (const std::string& r : rows) { w = std::max(w, UI::CalcTextWidth(r.c_str()) + 30); total += r.empty() ? 7 : lh; }
    const float x = std::min(px, std::max(canvas.x0, canvas.x1 - w - 4)), y = std::min(py, std::max(canvas.y0 - 60, canvas.y1 - total - 4));
    int pick = -1;
    close = false;
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + id).c_str(), x, y, w, total)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + total), kPanel, 4.0f);
        dl->AddRect(UI::Vec2(x, y), UI::Vec2(x + w, y + total), kLine, 4.0f, 1.0f);
        const UI::Vec2 m = UI::GetMousePos();
        float ry = y + 4;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].empty()) { dl->AddLine(UI::Vec2(x + 6, ry + 3), UI::Vec2(x + w - 6, ry + 3), kLine); ry += 7; continue; }
            const bool over = m.x >= x && m.x < x + w && m.y >= ry && m.y < ry + lh;
            if (over) dl->AddRectFilled(UI::Vec2(x + 3, ry), UI::Vec2(x + w - 3, ry + lh), kHover, 3.0f);
            dl->AddText(UI::Vec2(x + 12, ry + 3), kText, rows[i].c_str());
            if (over && UI::IsMouseReleased(0)) pick = (int)i;
            ry += lh;
        }
        const bool outside = !(m.x >= x && m.x < x + w && m.y >= y && m.y < y + total);
        if (pick >= 0 || (outside && (UI::IsMouseClicked(0) || UI::IsMouseClicked(1))) || UI::IsKeyPressed(UI::KeyId_Escape, false)) close = true;
    }
    UI::End();
    return pick;
}

// ---- the running game -----------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::refreshLive() {
    const vsed::VsEditState& s = st();
    live = cfg.liveGraph && !s.g.path.empty() ? cfg.liveGraph(s.g.path) : nullptr;
    liveName.clear();
    if (!live) return;
    const vsed::VsDebugView& dv = vsed::DebugView();
    if (dv.stopped && dv.graph == live) { liveName = dv.object; return; }
    if (okay::editor::EditorState* e = ed())
        for (const auto& up : e->scene().Objects()) {
            if (!up) continue;
            for (auto* vc : up->GetComponents<okay::VisualScriptComponent>())
                if (vc->Graph() == live) { liveName = up->name; return; }
        }
}

// ---- the start cards (an empty graph) ---------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawStartCards(float X, float Y, float W, float H, bool overlay) {
    vsed::VsEditState& s = st();
    startCards.clear();
    startEmpty = startNever = Rect{};
    if (!vsed::VsStartVisible((int)s.g.nodes.size(), vsed::VsPrefs().hideStart, s.startedEmpty, s.missing)) return;
    const auto& dl = UI::GetWindowDrawList();
    const float fs = UI::GetFontSize();
    const UI::Font* font = UI::GetFont();
    auto centred = [&](const char* t, float size, float y, UI::Color32 col) {
        const float tw = UI::CalcTextWidth(t) * size / std::max(1.0f, fs);   // the proportional font's width, scaled to `size`
        dl->AddText(font, size, UI::Vec2(X + (W - tw) * 0.5f, Y + y), col, t);
    };
    centred("What do you want to make?", fs * 1.6f, 22.0f, Rgb(238, 242, 248));
    centred("Pick one. It appears already connected, and you can change it.", fs, 22.0f + fs * 2.3f, kDim);
    const std::vector<vsed::VsStartCard> cards = vsed::VsStartCards();
    const std::vector<vsed::VsStartRect> rects = vsed::VsStartLayout((int)cards.size(), W, H);
    const UI::Vec2 m = UI::GetMousePos();
    const bool click = !overlay && UI::IsMouseClicked(0);
    int picked = -1;
    for (std::size_t i = 0; i < cards.size() && i < rects.size(); ++i) {
        const vsed::VsStartRect& q = rects[i];
        const Rect r{X + q.x, Y + q.y, X + q.x + q.w, Y + q.y + q.h};
        startCards.push_back(r);
        const bool hot = !overlay && r.in(m.x, m.y);
        dl->AddRectFilled(UI::Vec2(r.x0, r.y0), UI::Vec2(r.x1, r.y1), hot ? Rgb(48, 58, 78) : Rgb(34, 39, 52), 10.0f);
        dl->AddRect(UI::Vec2(r.x0, r.y0), UI::Vec2(r.x1, r.y1), hot ? Rgb(130, 175, 245) : Rgb(86, 98, 124), 10.0f, hot ? 2.0f : 1.0f);
        // a small sketch of the recipe: one coloured block per step, joined by a line
        const int blocks = std::min(5, std::max(2, vsed::VsGraphNodeCount(vsed::VsRecipes()[(std::size_t)cards[i].recipe].graph)));
        float bx = r.x0 + 14.0f;
        const float by = r.y0 + 14.0f;
        for (int b = 0; b < blocks; ++b) {
            if (b) dl->AddLine(UI::Vec2(bx - 10.0f, by + 6.0f), UI::Vec2(bx, by + 6.0f), Rgb(220, 224, 232), 2.0f);
            dl->AddRectFilled(UI::Vec2(bx, by), UI::Vec2(bx + 22.0f, by + 12.0f), b == 0 ? Rgb(236, 120, 96) : Rgb(96, 160, 236), 3.0f);
            bx += 32.0f;
        }
        dl->PushClipRect(UI::Vec2(r.x0, r.y0), UI::Vec2(r.x1, r.y1));
        dl->AddText(font, fs * 1.05f, UI::Vec2(r.x0 + 14.0f, r.y0 + 36.0f), Rgb(244, 247, 252), cards[i].title.c_str(), nullptr, q.w - 28.0f);
        const float th = font->CalcTextSizeA(fs * 1.05f, FLT_MAX, q.w - 28.0f, cards[i].title.c_str()).y;
        dl->AddText(font, fs * 0.92f, UI::Vec2(r.x0 + 14.0f, r.y0 + 40.0f + th), kDim, cards[i].line.c_str(), nullptr, q.w - 28.0f);
        dl->PopClipRect();
        if (hot && click) picked = cards[i].recipe;
    }
    // under the cards: start empty, and never show them again
    float by = rects.empty() ? 140.0f : rects.back().y + rects.back().h + 20.0f;
    by = std::min(by, H - 46.0f);
    const char* empty = "Start empty";
    const float bw = UI::CalcTextWidth(empty) + 36.0f;
    startEmpty = {X + W * 0.5f - bw - 12.0f, Y + by, X + W * 0.5f - 12.0f, Y + by + 30.0f};
    const bool overEmpty = !overlay && startEmpty.in(m.x, m.y);
    dl->AddRectFilled(UI::Vec2(startEmpty.x0, startEmpty.y0), UI::Vec2(startEmpty.x1, startEmpty.y1), overEmpty ? kHover : kTabOn, 5.0f);
    dl->AddText(UI::Vec2(startEmpty.x0 + 18.0f, startEmpty.y0 + (30.0f - UI::GetTextLineHeight()) * 0.5f), kText, empty);
    const char* never = "Do not show this again";
    startNever = {X + W * 0.5f + 12.0f, Y + by + 6.0f, X + W * 0.5f + 40.0f + UI::CalcTextWidth(never), Y + by + 24.0f};
    const bool hidden = vsed::VsPrefs().hideStart;
    dl->AddRect(UI::Vec2(startNever.x0, startNever.y0), UI::Vec2(startNever.x0 + 16, startNever.y0 + 16), kDim, 3.0f, 1.0f);
    if (hidden) dl->AddRectFilled(UI::Vec2(startNever.x0 + 4, startNever.y0 + 4), UI::Vec2(startNever.x0 + 12, startNever.y0 + 12), kAccent, 2.0f);
    dl->AddText(UI::Vec2(startNever.x0 + 24.0f, startNever.y0), kText, never);
    if (click && overEmpty) s.startedEmpty = true;
    if (click && startNever.in(m.x, m.y)) { vsed::VsPrefs().hideStart = !hidden; vsed::VsPrefsSave(); }
    if (picked >= 0) { vsed::VsStartInsertRecipe(ed(), s, picked); note("Added the recipe: press Play to see it work"); }
}

// ---- the "What is this?" card of the selected block ---------------------------------------------------------------------------

void VisualScriptApp::Impl::drawHelpCard(bool modal) {
    chips.clear();
    tryButton = helpToggle = Rect{};
    helpTitle.clear();
    if (helpCard.x1 <= helpCard.x0) return;   // drawCanvas sets it when a block is selected
    vsed::VsEditState& s = st();
    const vsed::VsEditNode* sn = s.g.find(s.selected);
    const vsed::VsDef* d = sn ? vsed::VsDefFor(s.g, *sn) : nullptr;
    if (!d) return;
    const bool open = vsed::VsPrefs().helpCardOpen;
    const float x = helpCard.x0, y = helpCard.y0, w = helpCard.x1 - helpCard.x0, h = helpCard.y1 - helpCard.y0;
    helpTitle = std::string("What is this?  ") + (d->title ? d->title : d->type);
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_helpcard").c_str(), x, y, w, h)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + h), Rgb(26, 30, 40, 244), 8.0f);
        dl->AddRect(UI::Vec2(x, y), UI::Vec2(x + w, y + h), Rgb(110, 160, 235, 190), 8.0f, 1.0f);
        dl->AddText(UI::Vec2(x + 12, y + 8), Rgb(140, 190, 255), Fit(helpTitle, w - 80).c_str());
        const UI::Vec2 m = UI::GetMousePos();
        const char* tog = open ? "Hide" : "Show";
        helpToggle = {x + w - 52, y + 5, x + w - 8, y + 27};
        const bool overTog = helpToggle.in(m.x, m.y);
        dl->AddRectFilled(UI::Vec2(helpToggle.x0, helpToggle.y0), UI::Vec2(helpToggle.x1, helpToggle.y1), overTog ? kHover : kTabOn, 4.0f);
        dl->AddText(UI::Vec2(helpToggle.x0 + 8, helpToggle.y0 + 3), kText, tog);
        if (overTog && !modal && UI::IsMouseClicked(0)) { vsed::VsPrefs().helpCardOpen = !open; vsed::VsPrefsSave(); }
        if (open) {
            UI::SetCursorScreenPos(UI::Vec2(x + 12, y + 34));
            if (UI::BeginChild((cfg.idPrefix + "_helpcardbody").c_str(), w - 20, h - 42, false)) {
                UI::PushTextWrapPos(w - 40);
                const vsed::VsHelpCard c = vsed::VsHelpCardFor(*d, &vsed::Registry());
                UI::TextWrapped(c.does.c_str());
                UI::Spacing(4);
                UI::TextColored(240, 200, 115, "What to plug in");
                for (const auto& pl : c.plugs) UI::BulletText((pl.first + ": " + pl.second).c_str());
                UI::Spacing(4);
                UI::TextColored(158, 217, 166, "A tiny example");
                std::string chain;
                for (std::size_t i = 0; i < c.chain.size(); ++i) chain += (i ? "  ->  " : "") + c.chain[i];
                UI::TextWrapped(chain.c_str());
                UI::TextDisabled(c.example.c_str());
                UI::Spacing(4);
                UI::TextColored(255, 140, 122, "Common mistakes");
                for (const std::string& mi : c.mistakes) UI::BulletText(mi.c_str());
                UI::Spacing(4);
                UI::TextColored(140, 190, 255, c.next.c_str());
                const std::vector<vsed::VsNextStep> next = vsed::VsNextSteps(*d, vsed::Registry(), 4);
                if (!next.empty()) {
                    UI::Spacing(4);
                    UI::TextUnformatted("Next steps (click to add and connect)");
                    for (std::size_t i = 0; i < next.size(); ++i) {
                        if (i) UI::SameLine();
                        const bool hit = UI::SmallButton((next[i].title + "##vsapp_chip" + std::to_string(i)).c_str());
                        const UI::Vec2 a = UI::GetItemRectMin(), b = UI::GetItemRectMax();
                        chips.push_back({Rect{a.x, a.y, b.x, b.y}, i});
                        if (UI::IsItemHovered()) UI::SetTooltip("%s", next[i].line.c_str());
                        if (hit && !modal) { AddNextStepAt(i); break; }
                    }
                }
                UI::Spacing(4);
                const bool tryIt = UI::Button("Try this block");
                const UI::Vec2 a = UI::GetItemRectMin(), b = UI::GetItemRectMax();
                tryButton = {a.x, a.y, b.x, b.y};
                if (UI::IsItemHovered()) UI::SetTooltip("Adds a small ready-made graph that uses this block. Press Play to see it work.");
                if (tryIt && !modal) TryBlock();
                UI::PopTextWrapPos();
            }
            UI::EndChild();
        }
    }
    UI::End();
}

bool VisualScriptApp::Impl::AddNextStepAt(std::size_t index) {
    vsed::VsEditState& s = st();
    const vsed::VsEditNode* sn = s.g.find(s.selected);
    const vsed::VsDef* d = sn ? vsed::VsDefFor(s.g, *sn) : nullptr;
    if (!d) return false;
    const std::vector<vsed::VsNextStep> next = vsed::VsNextSteps(*d, vsed::Registry(), 4);
    if (index >= next.size()) return false;
    const int id = vsed::VsAddNextStep(s.g, s.selected, next[index]);
    if (id < 0) return false;
    s.sel = {id}; s.selected = id; s.focusNode = id;
    s.g.dirty = true;
    note("Added " + next[index].title);
    return true;
}

bool VisualScriptApp::Impl::TryBlock() {
    vsed::VsEditState& s = st();
    const vsed::VsEditNode* sn = s.g.find(s.selected);
    const vsed::VsDef* d = sn ? vsed::VsDefFor(s.g, *sn) : nullptr;
    if (!d) return false;
    vsed::VsEditGraph tmp;
    const std::string text = vsed::VsTryGraph(*d, tmp);
    if (text.empty()) return false;
    const UI::Vec2 c = toWorld(canvas.x0 + 60.0f, (canvas.y0 + canvas.y1) * 0.5f - 60.0f);
    const bool any = !vsed::VsPasteTemplate(s, text, c.x, c.y).empty();
    if (any) note("Added a small graph that uses " + std::string(d->title ? d->title : d->type) + ": press Play to see it work");
    return any;
}

// ---- the next-step hint ---------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawHintBar(float X, float Y, float W, float H) {
    vsed::VsEditState& s = st();
    hintLine.clear();
    vsed::VsHintFacts hf;
    hf.nodeCount = (int)s.g.nodes.size();
    hf.wireCount = (int)(s.g.execLinks.size() + s.g.dataLinks.size());
    hf.playing = live != nullptr;
    hf.userWired = wiredThisFrame;
    std::size_t sig = 0;
    for (const auto& n : s.g.nodes) {
        sig = sig * 131u + std::hash<std::string>()(n.arg);
        for (const auto& pd : n.pinDefault) sig = sig * 131u + std::hash<std::string>()(pd.second) + (std::size_t)pd.first;
    }
    hf.editedNumber = s.sigNodes > 0 && s.sigNodes == hf.nodeCount && sig != s.valueSig;
    s.valueSig = sig; s.sigNodes = hf.nodeCount;
    vsed::VsHintResult guide = vsed::VsHintStep(vsed::VsPrefs().hintsDone, hf, settings.showHint && !s.missing);
    if (guide.done != vsed::VsPrefs().hintsDone) { vsed::VsPrefs().hintsDone = guide.done; vsed::VsPrefsSave(); }
    if (vsed::VsStartVisible(hf.nodeCount, vsed::VsPrefs().hideStart, s.startedEmpty, s.missing)) guide.hint = vsed::VsHint::None;   // the cards say it
    if (guide.hint == vsed::VsHint::None) return;
    hintLine = vsed::VsHintText(guide.hint);
    const auto& dl = UI::GetWindowDrawList();
    const float lh = UI::GetTextLineHeight();
    const float maxW = std::max(120.0f, W - 220.0f);
    const float bw = std::min(maxW, UI::CalcTextWidth(hintLine.c_str()) + 44.0f);
    const UI::Vec2 b0(X + 12.0f, Y + H - lh - 24.0f), b1(X + 12.0f + bw, Y + H - 10.0f);
    dl->AddRectFilled(b0, b1, Rgb(28, 58, 96, 238), 10.0f);
    dl->AddRect(b0, b1, Rgb(110, 160, 235, 200), 10.0f, 1.0f);
    dl->AddCircleFilled(UI::Vec2(b0.x + 16.0f, (b0.y + b1.y) * 0.5f), 4.5f, Rgb(120, 190, 255));
    dl->AddText(UI::Vec2(b0.x + 30.0f, b0.y + (b1.y - b0.y - lh) * 0.5f), Rgb(232, 240, 252), Fit(hintLine, bw - 40.0f).c_str());
}

// ---- Pick a Recipe and Templates ------------------------------------------------------------------------------------------------

std::vector<std::string> VisualScriptApp::Impl::recipeNames() const {
    std::vector<std::string> out;
    for (const vsed::VsRecipe& r : vsed::VsRecipes()) out.push_back(r.name);
    for (const vsed::VsExtraRecipe& r : vsed::VsExtraRecipes()) out.push_back(r.name);
    return out;
}

bool VisualScriptApp::Impl::pickRecipe(std::size_t index) {
    vsed::VsEditState& s = st();
    const auto& main = vsed::VsRecipes();
    const auto& more = vsed::VsExtraRecipes();
    if (index >= main.size() + more.size()) return false;
    const UI::Vec2 c = toWorld(canvas.x0 + 60.0f, (canvas.y0 + canvas.y1) * 0.5f - 60.0f);
    if (index < main.size()) {
        vsed::VsPasteTemplate(s, main[index].graph, c.x, c.y);
        if (okay::editor::EditorState* e = ed()) if (okay::GameObject* go = e->selected()) vsed::VsAddTemplateComponents(go, main[index].add);
        note(std::string("Added the recipe ") + main[index].name);
    } else {
        vsed::VsPasteTemplate(s, more[index - main.size()].text, c.x, c.y);
        note(std::string("Added the recipe ") + more[index - main.size()].name);
    }
    recipeBox = false;
    return true;
}

void VisualScriptApp::Impl::drawRecipeBox(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    const float bw = 520.0f, bh = std::min(H - 40.0f, 460.0f);
    if (beginDialog(cfg.idPrefix + "_recipes", X, Y, W, H, bw, bh, bx, by)) {
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted("Pick a recipe");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 2));
        UI::TextDisabled("Start here. One click adds a small working graph.");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        if (UI::BeginChild((cfg.idPrefix + "_recipelist").c_str(), std::min(bw, W - 20.0f) - 28.0f, bh - 110.0f, true)) {
            const auto& main = vsed::VsRecipes();
            for (std::size_t i = 0; i < main.size(); ++i) {
                if (UI::Selectable((std::string(main[i].name) + "##vsapp_rc" + std::to_string(i)).c_str(), false)) { pickRecipe(i); break; }
                if (UI::IsItemHovered()) UI::SetTooltip("%s%s%s", main[i].blurb, main[i].needs[0] ? "\n" : "", main[i].needs);
            }
            UI::Spacing(4);
            UI::TextDisabled("More recipes");
            const auto& more = vsed::VsExtraRecipes();
            for (std::size_t i = 0; i < more.size(); ++i)
                if (UI::Selectable((std::string(more[i].name) + "##vsapp_rm" + std::to_string(i)).c_str(), false)) { pickRecipe(main.size() + i); break; }
        }
        UI::EndChild();
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        if (UI::Button("Close##vsapp_recipes") || UI::IsKeyPressed(UI::KeyId_Escape, false)) recipeBox = false;
    }
    UI::End();
}

void VisualScriptApp::Impl::drawTemplateBox(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    const float bw = 600.0f, bh = std::min(H - 40.0f, 560.0f);
    if (beginDialog(cfg.idPrefix + "_templates", X, Y, W, H, bw, bh, bx, by)) {
        const float iw = std::min(bw, W - 20.0f) - 28.0f;
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted("Templates");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 2));
        UI::TextDisabled(ed() ? "Replaces this graph. If an object is selected, it runs the graph and gets the parts it needs."
                              : "Replaces this graph (not saved until you save it).");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        UI::SetNextItemWidth(iw);
        UI::InputTextWithHint("##vsapp_tplq", "Search templates", tplBuf, (int)sizeof tplBuf);
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        for (int i = 0; i < 4; ++i) {
            if (i) UI::SameLine();
            const std::string label = std::string(tplLevel == i ? "> " : "") + vsed::VsLevelFilterName((vsed::VsLevelFilter)i) + "##vsapp_tl";
            if (UI::SmallButton(label.c_str())) tplLevel = i;
        }
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        const vsed::VsTemplate* picked = nullptr;
        if (UI::BeginChild((cfg.idPrefix + "_tpllist").c_str(), iw, bh - 150.0f, true)) {
            const char* group = nullptr;
            int row = 0;
            for (const vsed::VsTemplate* t : vsed::VsTemplateMatches(tplBuf, tplLevel)) {
                if (!group || std::strcmp(group, t->category) != 0) { group = t->category; UI::Spacing(4); UI::TextDisabled(t->category); }
                const std::string label = std::string(t->name) + "   [" + vsed::VsLevelBadge(t->level) + "]##vsapp_tp" + std::to_string(row++);
                if (UI::Selectable(label.c_str(), false)) picked = t;
                if (UI::IsItemHovered()) UI::SetTooltip("%s%s%s", t->blurb, t->needs && t->needs[0] ? "\n" : "", t->needs ? t->needs : "");
            }
            if (row == 0) UI::TextDisabled("No template matches.");
        }
        UI::EndChild();
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        if (UI::Button("Close##vsapp_templates") || UI::IsKeyPressed(UI::KeyId_Escape, false)) templateBox = false;
        if (picked) { vsed::VsApplyTemplate(ed(), st(), *picked); templateBox = false; note(std::string("Template: ") + picked->name); }
    }
    UI::End();
}

// ---- the editor's actions -----------------------------------------------------------------------------------------------------------

bool VisualScriptApp::Impl::attachSelected() {
    okay::editor::EditorState* e = ed();
    if (!e || !e->selected()) { note("Select an object in the scene first", 1); return false; }
    std::string why;
    okay::GameObject* o = e->selected();
    if (!vsed::VsAttachToObject(*e, o, why)) { note("Could not attach the graph: " + why, 2); return false; }
    note("Attached the graph to " + o->name);
    return true;
}

void VisualScriptApp::Impl::restoreVersion(const std::string& text) {
    vsed::VsRestoreVersion(st(), text);
    note("Restored a previous version. Save if you want to keep it.");
}

// ---- right-click menus: a tab, a comment frame, a block's Connect to... ---------------------------------------------------------

void VisualScriptApp::Impl::drawTabMenu() {
    vsed::VsSession& ses = vsed::Session();
    if (tabMenu >= (int)ses.tabs.size()) { tabMenu = -1; return; }
    const std::vector<std::string> rows = {"Close", "Close Others", "Close All", "", "Reopen Closed Tab"};
    bool close = false;
    const int pick = menuBox("_tabmenu", popX, popY, rows, close);
    const int tab = tabMenu;
    if (close) tabMenu = -1;
    if (pick == 0) requestClose(tab);
    else if (pick == 1) closeMany(tab);
    else if (pick == 2) closeMany(-1);
    else if (pick == 4) vsed::VsReopenClosedGraph();
}

void VisualScriptApp::Impl::drawCommentMenu() {
    vsed::VsEditState& s = st();
    if (commentMenu < 0 || commentMenu >= (int)s.g.comments.size()) { commentMenu = -1; return; }
    static const char* colours[6] = {"Blue", "Green", "Orange", "Red", "Purple", "Grey"};
    std::vector<std::string> rows = {"Rename", ""};
    for (int k = 0; k < 6; ++k) rows.push_back(std::string(s.g.comments[(std::size_t)commentMenu].color == k ? "* " : "  ") + "Colour: " + colours[k]);
    rows.push_back("");
    rows.push_back("Delete Comment");
    bool close = false;
    const int pick = menuBox("_commentmenu", popX, popY, rows, close);
    const int ci = commentMenu;
    if (close) commentMenu = -1;
    if (pick == 0) {
        s.renameComment = ci;
        std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", s.g.comments[(std::size_t)ci].text.c_str());
        renameBox = true;
    } else if (pick >= 2 && pick < 8) { s.g.comments[(std::size_t)ci].color = pick - 2; s.g.dirty = true; }
    else if (pick == 9) { s.g.comments.erase(s.g.comments.begin() + ci); s.g.dirty = true; }
}

namespace {
struct ConnectRow { std::string label; bool exec = false, fromOutput = false; int pin = 0; okay::vs::PinType type = okay::vs::PinType::Any; };
std::vector<ConnectRow> ConnectRowsFor(const vsed::VsEditGraph& g, const vsed::VsEditNode& n, const vsed::VsDef& d) {
    std::vector<ConnectRow> rows;
    if (d.execIn) rows.push_back({"Flow in  <-  what runs this block", true, false, 0, okay::vs::PinType::Any});
    for (int j = 0; j < (int)d.execOut.size(); ++j) {
        const char* lbl = d.execOut[(std::size_t)j];
        rows.push_back({std::string("Flow out  ->  ") + (lbl && *lbl ? lbl : "Next"), true, true, j, okay::vs::PinType::Any});
    }
    for (int i = 0; i < (int)d.dataIn.size(); ++i) {
        const okay::vs::PinType pt = vsed::VsInPinType(g, n.id, i);
        rows.push_back({std::string("Value in  <-  ") + d.dataIn[(std::size_t)i] + " (" + okay::vs::PinTypeName(pt) + ")", false, false, i, pt});
    }
    for (int o = 0; o < (int)d.dataOut.size(); ++o) {
        const okay::vs::PinType pt = vsed::VsOutPinType(g, n.id, o);
        rows.push_back({std::string("Value out  ->  ") + d.dataOut[(std::size_t)o] + " (" + okay::vs::PinTypeName(pt) + ")", false, true, o, pt});
    }
    return rows;
}
}  // namespace

std::vector<std::string> VisualScriptApp::ConnectRows(int node) const {
    std::vector<std::string> out;
    const vsed::VsEditGraph& g = vsed::State().g;
    const vsed::VsEditNode* n = g.find(node);
    const vsed::VsDef* d = n ? vsed::VsDefFor(g, *n) : nullptr;
    if (!d) return out;
    for (const ConnectRow& r : ConnectRowsFor(g, *n, *d)) {
        const std::size_t fit = vsed::VsNodesFittingPin(r.exec, r.fromOutput, r.type).size();
        out.push_back(r.label + "   (" + std::to_string(fit) + " fit)");
    }
    return out;
}

bool VisualScriptApp::Impl::connectFrom(int node, std::size_t row) {
    vsed::VsEditState& s = st();
    const vsed::VsEditNode* n = s.g.find(node);
    const vsed::VsDef* d = n ? vsed::VsDefFor(s.g, *n) : nullptr;
    if (!d) return false;
    const std::vector<ConnectRow> rows = ConnectRowsFor(s.g, *n, *d);
    if (row >= rows.size()) return false;
    const ConnectRow& r = rows[row];
    s.linkPending = true; s.linkExec = r.exec; s.linkFromOutput = r.fromOutput; s.linkNode = node; s.linkPin = r.pin;
    // the new block goes beside the plug: right of an output, left of an input
    const vsed::VsBoxF box = vsed::VsLiveBox(s, *n);
    const float wx = r.fromOutput ? n->x + box.w + 60.0f : n->x - 260.0f, wy = n->y;
    const UI::Vec2 sp = toScreen(wx, wy);
    openPalette(sp.x, sp.y, wx, wy);
    connectNode = -1;
    return true;
}

bool VisualScriptApp::ConnectFrom(int node, std::size_t row) { return m->connectFrom(node, row); }

void VisualScriptApp::Impl::drawConnectMenu() {
    vsed::VsEditState& s = st();
    const vsed::VsEditNode* n = s.g.find(connectNode);
    const vsed::VsDef* d = n ? vsed::VsDefFor(s.g, *n) : nullptr;
    if (!d) { connectNode = -1; return; }
    std::vector<std::string> rows = {"Pick a plug to see what fits it"};
    std::vector<ConnectRow> cr = ConnectRowsFor(s.g, *n, *d);
    for (const ConnectRow& r : cr) rows.push_back(r.label);
    if (cr.empty()) rows.push_back("(this block has no plugs)");
    bool close = false;
    const int pick = menuBox("_connectmenu", popX, popY, rows, close);
    const int node = connectNode;
    if (close) connectNode = -1;
    if (pick >= 1 && pick - 1 < (int)cr.size()) connectFrom(node, (std::size_t)(pick - 1));
}

// ---- the outline --------------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawOutline(float X, float Y, float W, float H, bool modal) {
    if (okayuiapp::BeginFilled(cfg.idPrefix + "_outline", X, Y, W, H, kPanel)) {
        UI::GetWindowDrawList()->AddLine(UI::Vec2(X + W - 1, Y), UI::Vec2(X + W - 1, Y + H), kLine);
        vsed::VsEditState& s = st();
        const vsed::VsEditGraph& g = s.g;
        UI::SetCursorScreenPos(UI::Vec2(X + 8, Y + 8));
        if (UI::BeginChild((cfg.idPrefix + "_outlinelist").c_str(), W - 14, H - 14, false)) {
            UI::PushTextWrapPos(W - 24);
            const vsed::VsOutline o = vsed::VsBuildOutline(g);
            auto jump = [&](int node) { s.sel = {node}; s.selected = node; s.focusNode = node; };
            auto rows = [&](const std::vector<vsed::VsOutlineRow>& list, const char* tag, bool removable) {
                int remove = -1;
                for (std::size_t i = 0; i < list.size(); ++i) {
                    const bool on = s.sel.count(list[i].node) > 0;
                    if (UI::Selectable((Fit(list[i].label, W - 30) + "##" + tag + std::to_string(i)).c_str(), on) && !modal) jump(list[i].node);
                    if (UI::IsItemHovered()) UI::SetTooltip("%s", list[i].tip.c_str());
                    if (removable && UI::IsItemClicked(1)) remove = list[i].node;
                }
                return remove;
            };
            UI::TextUnformatted(vsed::kPanelEvents);
            rows(o.events, "vsapp_ev", false);
            if (o.events.empty()) UI::TextDisabled(vsed::kPanelNoEvents);
            if (!o.functions.empty()) { UI::Spacing(6); UI::TextUnformatted(vsed::kPanelNamedActions); rows(o.functions, "vsapp_fn", false); }
            if (!o.breakpoints.empty()) {
                UI::Spacing(6);
                UI::TextUnformatted(vsed::kPanelStopPoints);
                const int remove = rows(o.breakpoints, "vsapp_bp", true);
                if (remove >= 0 && !modal) okay::vs::VsDebugger::Get().SetBreakpoint(g.path, remove, false);
                if (UI::SmallButton("Remove all##vsapp_bpall") && !modal)
                    for (const vsed::VsOutlineRow& r : o.breakpoints) okay::vs::VsDebugger::Get().SetBreakpoint(g.path, r.node, false);
            }
            // the saved values the blocks use, by who can see them; while the game runs, what each holds now
            okay::VisualScriptComponent* liveComp = nullptr;
            if (live) if (okay::editor::EditorState* e = ed())
                for (const auto& up : e->scene().Objects()) {
                    if (!up || liveComp) continue;
                    for (auto* vc : up->GetComponents<okay::VisualScriptComponent>()) if (vc->Graph() == live) { liveComp = vc; break; }
                }
            UI::Spacing(6);
            UI::TextUnformatted(vsed::kPanelUsed);
            if (o.values.empty()) UI::TextDisabled(vsed::kPanelNoneUsed);
            bool anyScope = false;
            vsed::VsVarScope last = vsed::VsVarScope::Object;
            for (std::size_t i = 0; i < o.values.size(); ++i) {
                const vsed::VsVarRef& v = o.values[i];
                if (!anyScope || v.scope != last) { UI::TextDisabled(vsed::VsVarScopeName(v.scope)); last = v.scope; anyScope = true; }
                std::string row = v.name;
                if (liveComp) row += "   =   " + okay::vs::VsToLiteral(liveComp->GetVariable(v.name));
                if (UI::Selectable((Fit(row, W - 30) + "##vsapp_val" + std::to_string(i)).c_str(), s.sel.count(v.nodeId) > 0) && !modal) jump(v.nodeId);
                if (UI::IsItemHovered()) UI::SetTooltip("%s", vsed::kPanelUsedTip);
            }
            UI::Spacing(6);
            if (UI::CollapsingHeader(vsed::kPanelInputs, !o.inputs.empty())) {
                if (o.inputs.empty()) UI::TextDisabled(vsed::kPanelNoInputs);
                for (std::size_t i = 0; i < o.inputs.size(); ++i) {
                    const vsed::VsParamRef& p = o.inputs[i];
                    std::string row = p.function + "  " + (p.name.empty() ? std::string("(no inputs)") : p.name + (p.type.empty() ? "" : " : " + p.type));
                    if (UI::Selectable((Fit(row, W - 30) + "##vsapp_in" + std::to_string(i)).c_str(), s.sel.count(p.nodeId) > 0) && !modal) jump(p.nodeId);
                }
            }
            UI::PopTextWrapPos();
        }
        UI::EndChild();
    }
    UI::End();
}

// ---- the paused game ------------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawPaused() {
    const vsed::VsDebugView& dv = vsed::DebugView();
    UI::TextColored(255, 210, 76, "Paused");
    auto row = [](const std::string& name, const okay::vs::VsValue& v) { UI::TextWrapped((name + " = " + vsed::VsDebugText(v)).c_str()); };
    if (UI::CollapsingHeader("Call stack##vsapp_dbg", true)) {
        for (int i = (int)dv.frames.size() - 1; i >= 0; --i) UI::BulletText((dv.frames[(std::size_t)i].name + "  (function)").c_str());
        UI::BulletText((dv.event + "  (event on '" + dv.object + "')").c_str());
    }
    if (!dv.frames.empty()) {
        const auto& f = dv.frames.back();
        if (UI::CollapsingHeader(("Function " + f.name + "##vsapp_dbgfn").c_str(), true)) {
            okay::vs::FunctionSig sig;
            bool haveSig = false;
            for (const auto& mn : st().g.nodes)
                if (mn.type == "FunctionStart" && okay::vs::FunctionNameOf(mn.arg) == f.name) { haveSig = okay::vs::ParseFunctionSignature(mn.arg, sig); break; }
            for (std::size_t i = 0; i < f.args.size(); ++i) row(haveSig && i < sig.params.size() ? sig.params[i].name : "arg " + std::to_string(i), f.args[i]);
            std::vector<std::pair<std::string, okay::vs::VsValue>> locals(f.locals.begin(), f.locals.end());
            std::sort(locals.begin(), locals.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& kv : locals) row("local " + kv.first, kv.second);
            if (f.args.empty() && locals.empty()) UI::TextDisabled("(no inputs or locals)");
        }
    }
    auto section = [&](const std::string& title, const std::vector<std::pair<std::string, okay::vs::VsValue>>& vars, bool open) {
        if (!UI::CollapsingHeader(title.c_str(), open)) return;
        if (vars.empty()) UI::TextDisabled("(none)");
        for (const auto& kv : vars) row(kv.first, kv.second);
    };
    section("Variables on '" + dv.object + "'##vsapp_dbgv", dv.vars, true);
    section("Scene variables##vsapp_dbgs", dv.sceneVars, false);
    section("Global variables##vsapp_dbgg", dv.globalVars, false);
    UI::Spacing(6);
}

// ---- VisualScriptApp: the aids, read back by tests ----------------------------------------------------------------------------------

bool VisualScriptApp::StartCardsShown() const { return !m->startCards.empty(); }
bool VisualScriptApp::StartCardOnScreen(int i, float& x0, float& y0, float& x1, float& y1) const {
    if (i < 0 || i >= (int)m->startCards.size()) return false;
    const Impl::Rect& r = m->startCards[(std::size_t)i];
    x0 = r.x0; y0 = r.y0; x1 = r.x1; y1 = r.y1;
    return true;
}
const std::string& VisualScriptApp::HintLine() const { return m->hintLine; }
const std::string& VisualScriptApp::HelpCardTitle() const { return m->helpTitle; }
std::vector<std::string> VisualScriptApp::NextStepChips() const {
    std::vector<std::string> out;
    const vsed::VsEditState& s = vsed::State();
    const vsed::VsEditNode* sn = s.g.find(s.selected);
    const vsed::VsDef* d = sn ? vsed::VsDefFor(s.g, *sn) : nullptr;
    if (!d) return out;
    for (const vsed::VsNextStep& n : vsed::VsNextSteps(*d, vsed::Registry(), 4)) out.push_back(n.title);
    return out;
}
bool VisualScriptApp::AddNextStep(std::size_t index) { return m->AddNextStepAt(index); }
bool VisualScriptApp::TryThisBlock() { return m->TryBlock(); }
std::string VisualScriptApp::BlockSentence(int node) const { const auto it = m->sentences.find(node); return it == m->sentences.end() ? std::string() : it->second; }
std::string VisualScriptApp::BlockBadge(int node) const { const auto it = m->badges.find(node); return it == m->badges.end() ? std::string() : it->second; }
std::vector<std::string> VisualScriptApp::RecipeNames() const { return m->recipeNames(); }
bool VisualScriptApp::PickRecipe(std::size_t index) { return m->pickRecipe(index); }
std::vector<std::string> VisualScriptApp::TemplateNames(const std::string& query, int level) const {
    std::vector<std::string> out;
    for (const vsed::VsTemplate* t : vsed::VsTemplateMatches(query, level)) out.push_back(t->name);
    return out;
}
bool VisualScriptApp::ApplyTemplate(const std::string& name) {
    const vsed::VsTemplate* t = vsed::VsTemplateNamed(name.c_str());
    if (!t) return false;
    vsed::VsApplyTemplate(m->ed(), m->st(), *t);
    return true;
}
bool VisualScriptApp::AttachToSelected() { return m->attachSelected(); }
std::vector<std::string> VisualScriptApp::PreviousVersions() const {
    std::vector<std::string> out;
    const std::string& path = vsed::State().g.path;
    if (vsed::VsBackups && !path.empty()) for (const auto& v : vsed::VsBackups(path)) out.push_back(v.first);
    return out;
}
bool VisualScriptApp::RestoreVersion(std::size_t index) {
    const std::string& path = m->st().g.path;
    if (!vsed::VsBackups || path.empty()) return false;
    const auto versions = vsed::VsBackups(path);
    if (index >= versions.size()) return false;
    m->restoreVersion(versions[index].second);
    return true;
}
void VisualScriptApp::CloseOthers(int tab) { m->closeMany(tab); }
void VisualScriptApp::CloseAll() { m->closeMany(-1); }
std::vector<std::string> VisualScriptApp::OutlineRows() const {
    std::vector<std::string> out;
    const vsed::VsOutline o = vsed::VsBuildOutline(vsed::State().g);
    for (const auto& r : o.events) out.push_back(std::string(vsed::kPanelEvents) + ": " + r.label);
    for (const auto& r : o.functions) out.push_back(std::string(vsed::kPanelNamedActions) + ": " + r.label);
    for (const auto& r : o.breakpoints) out.push_back(std::string(vsed::kPanelStopPoints) + ": " + r.label);
    for (const auto& v : o.values) out.push_back(std::string(vsed::kPanelSaved) + ": " + v.name);
    for (const auto& p : o.inputs) out.push_back(std::string(vsed::kPanelInputs) + ": " + p.function + " " + p.name);
    return out;
}

bool VisualScriptApp::CommentGripOnScreen(int comment, float& x, float& y) const {
    for (const auto& gp : m->grips) if (gp.first == comment) { x = gp.second.x; y = gp.second.y; return true; }
    return false;
}
bool VisualScriptApp::CommentOnScreen(int comment, float& x0, float& y0, float& x1, float& y1) const {
    if (comment < 0 || comment >= (int)m->commentRects.size()) return false;
    const Impl::Rect& r = m->commentRects[(std::size_t)comment];
    x0 = r.x0; y0 = r.y0; x1 = r.x1; y1 = r.y1;
    return true;
}
bool VisualScriptApp::KnotOnScreen(int knot, float& x, float& y) const {
    if (knot < 0 || knot >= (int)m->knotsDrawn.size()) return false;
    x = m->knotsDrawn[(std::size_t)knot].x; y = m->knotsDrawn[(std::size_t)knot].y;
    return true;
}
std::string VisualScriptApp::LiveLine(int node) const { const auto it = m->liveLines.find(node); return it == m->liveLines.end() ? std::string() : it->second; }
std::string VisualScriptApp::MissingComponent(int node) const {
    okay::editor::EditorState* e = m->ed();
    const vsed::VsEditGraph& g = vsed::State().g;
    const vsed::VsEditNode* n = g.find(node);
    const vsed::VsDef* d = n ? vsed::VsDefFor(g, *n) : nullptr;
    if (!e || !d) return std::string();
    const vsed::VsReqGap gap = vsed::VsRequirementGap(vsed::VsGraphOwner(*e, g.path), vsed::VsNodeDocFor(n->type), vsed::VsTargetSet(g, *n, *d));
    return gap.missing ? gap.addId : std::string();
}
bool VisualScriptApp::AddMissingComponent(int node) {
    const std::string id = MissingComponent(node);
    okay::editor::EditorState* e = m->ed();
    if (id.empty() || !e || !vsed::VsAddComponentById) return false;
    return vsed::VsAddComponentById(vsed::VsGraphOwner(*e, vsed::State().g.path), id.c_str());
}

}  // namespace vsapp
