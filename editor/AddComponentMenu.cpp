// The Add Component browser's window. See AddComponentMenu.hpp. The logic (search, categories, keys) is AddComponentModel.cpp.
#include "AddComponentMenu.hpp"

#include "EditorWidgets.hpp"
#include "imgui.h"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <typeinfo>

namespace oked {
namespace addcomp {

namespace {

const char* kPrefsFile = "okay_addcomponent.txt";   // next to okay_settings.txt: Recent and Favorites

ImVec4 CatTint(const std::string& c) {
    if (c == "Rendering")   return ImVec4(0.30f, 0.52f, 0.86f, 1);
    if (c == "Effects")     return ImVec4(0.80f, 0.42f, 0.62f, 1);
    if (c == "Physics")     return ImVec4(0.26f, 0.62f, 0.38f, 1);
    if (c == "Cameras")     return ImVec4(0.40f, 0.46f, 0.80f, 1);
    if (c == "Animation")   return ImVec4(0.20f, 0.62f, 0.66f, 1);
    if (c == "Audio")       return ImVec4(0.86f, 0.52f, 0.20f, 1);
    if (c == "Scripts")     return ImVec4(0.66f, 0.56f, 0.16f, 1);
    if (c == "Controllers") return ImVec4(0.58f, 0.40f, 0.82f, 1);
    if (c == "UI")          return ImVec4(0.52f, 0.40f, 0.86f, 1);
    if (c == "Survival")    return ImVec4(0.74f, 0.34f, 0.34f, 1);
    if (c == "Multiplayer") return ImVec4(0.24f, 0.58f, 0.74f, 1);
    if (c == "No-Code")     return ImVec4(0.36f, 0.64f, 0.30f, 1);
    if (c == "World")       return ImVec4(0.50f, 0.58f, 0.28f, 1);
    if (c == "Scripting")   return ImVec4(0.66f, 0.56f, 0.16f, 1);
    if (c == kUncategorised) return ImVec4(0.75f, 0.30f, 0.30f, 1);
    if (c == kCatForObject) return ImVec4(0.90f, 0.62f, 0.18f, 1);
    if (c == kCatFavorites) return ImVec4(0.90f, 0.72f, 0.18f, 1);
    if (c == kCatRecent)    return ImVec4(0.46f, 0.52f, 0.60f, 1);
    return ImVec4(0.46f, 0.50f, 0.58f, 1);   // Gameplay and anything new
}

std::string Fit(const std::string& s, float w) {
    if (ImGui::CalcTextSize(s.c_str()).x <= w) return s;
    std::size_t n = s.size();
    while (n > 1 && ImGui::CalcTextSize((s.substr(0, n) + "...").c_str()).x > w) --n;
    return s.substr(0, n) + "...";
}

void DrawStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool filled) {
    ImVec2 p[10];
    for (int i = 0; i < 10; ++i) {
        const float a = -1.5707963f + i * 0.6283185f, rr = (i % 2 == 0) ? r : r * 0.45f;
        p[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    if (filled) for (int i = 0; i < 10; ++i) dl->AddTriangleFilled(c, p[i], p[(i + 1) % 10], col);
    dl->AddPolyline(p, 10, col, ImDrawFlags_Closed, 1.3f);
}

// The name with the letters the search matched drawn in the accent colour and underlined.
float DrawName(ImDrawList* dl, ImVec2 pos, const std::string& name, const std::vector<int>& hits, ImU32 base, ImU32 hi, float maxW) {
    const std::string shown = Fit(name, maxW);
    const bool cut = shown.size() != name.size();
    float x = pos.x;
    std::size_t i = 0;
    while (i < shown.size()) {
        const bool h = !cut || i < shown.size() - 3 ? std::find(hits.begin(), hits.end(), (int)i) != hits.end() : false;
        std::size_t j = i + 1;
        while (j < shown.size() && (std::find(hits.begin(), hits.end(), (int)j) != hits.end()) == h) ++j;
        const std::string seg = shown.substr(i, j - i);
        const float w = ImGui::CalcTextSize(seg.c_str()).x;
        dl->AddText(ImVec2(x, pos.y), h ? hi : base, seg.c_str());
        if (h) dl->AddLine(ImVec2(x, pos.y + ImGui::GetFontSize() + 1.0f), ImVec2(x + w, pos.y + ImGui::GetFontSize() + 1.0f), hi, 1.5f);
        x += w;
        i = j;
    }
    return x - pos.x;
}

}  // namespace

void Session::LoadOnce() {
    if (prefsLoaded_) return;
    prefsLoaded_ = true;
    LoadPrefs(kPrefsFile, prefs_);
}
void Session::SaveNow() { SavePrefs(kPrefsFile, prefs_); }

std::string Session::TakeNotice() { std::string n; n.swap(notice_); return n; }

void Session::DrawNotice() const {
    if (noticeShown_.empty() || ImGui::GetTime() - noticeAt_ > 6.0) return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", noticeShown_.c_str());
    ImGui::PopTextWrapPos();
}

bool Session::Begin(const char* targetName, okay::GameObject* go) {
    LoadOnce();
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup("AddComponent");
        state_ = State(); buf_[0] = '\0'; refocus_ = true; pendingKey_.clear();
    }
    const float fs = ImGui::GetFontSize(), sc = fs / 16.0f;
    const ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(520.0f * sc, vp.x * 0.94f), std::min(420.0f * sc, vp.y * 0.92f)), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * sc, 12.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f * sc);
    const bool open = ImGui::BeginPopup("AddComponent", ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    entries_.clear(); cat_.clear(); activated_ = false;
    target_ = targetName ? targetName : "";
    before_.clear();
    if (open && !pendingKey_.empty() && go)
        for (const auto& c : go->Components()) before_.push_back(c.get());
    if (!open) pendingKey_.clear();
    return open;
}

bool Session::Item(bool absent, const char* label) {
    Entry e = MakeEntry(cat_, label, absent);
    if (absent && !pendingKey_.empty() && e.key == pendingKey_) {
        activated_ = true; activatedName_ = e.name; activatedLabel_ = e.label;
        pendingKey_.clear();
        entries_.push_back(std::move(e));
        return true;
    }
    entries_.push_back(std::move(e));
    return false;
}

void Session::FeedRegistry(okay::GameObject& go, const std::function<void(const okay::ComponentTypeInfo&)>& add) {
    std::vector<std::string> seen;
    for (const Entry& e : entries_) seen.push_back(e.nkey);
    const std::string savedCat = cat_;
    for (const RegItem& r : RegistryItems(go)) {
        if (std::find(seen.begin(), seen.end(), r.nkey) != seen.end()) continue;
        seen.push_back(r.nkey);
        cat_ = r.info->category;
        if (Item(!r.present, r.label.c_str())) add(*r.info);
    }
    cat_ = savedCat;
}

void Session::End(okay::GameObject* go) {
    pendingKey_.clear();   // chosen last frame and used by Item() (or stale)
    if (activated_) {
        // Say what came with it (the engine adds things a component needs, and some menu rows add helpers).
        std::vector<std::string> extras;
        std::vector<const okay::Component*> added;
        if (go) for (const auto& c : go->Components())
            if (std::find(before_.begin(), before_.end(), (const void*)c.get()) == before_.end()) added.push_back(c.get());
        for (std::size_t i = 0; i + 1 < added.size(); ++i) extras.push_back(PrettyTypeNameFromTypeInfo(typeid(*added[i]).name()));
        notice_ = added.empty() ? std::string() : AddedNotice(activatedName_, extras);
        noticeShown_ = notice_; noticeAt_ = ImGui::GetTime();
        prefs_.NoteUsed(activatedLabel_); SaveNow();
        activated_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const float fs = ImGui::GetFontSize(), sc = fs / 16.0f;
    ApplyPrereqs(entries_);   // "Needs a Rigidbody3D first."
    if (state_.category.empty()) state_.category = DefaultCategory(entries_, prefs_);

    // Title: what this is and who it is for.
    ImGui::PushFont(nullptr, fs * 1.1f);
    ImGui::TextUnformatted("Add Component");
    ImGui::PopFont();
    if (!target_.empty()) {
        const std::string to = Fit("to " + target_, ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(to.c_str()).x);
        ImGui::TextDisabled("%s", to.c_str());
    }
    ImGui::Dummy(ImVec2(0, 2.0f * sc));

    // The big search box. It has the keyboard when the popup opens, so you can just type.
    if (refocus_ || ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); refocus_ = false; }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * sc, 10.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * sc);
    ImGui::PushFont(nullptr, fs * 1.15f);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##acq", "Search  (try: jump, light, sound)", buf_, sizeof buf_);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    state_.SetQuery(buf_);
    view_ = Build(entries_, state_, prefs_);
    ClampCursor(state_, view_);

    // Keys (the search box keeps the keyboard; the two columns are just where Up / Down act).
    struct K { ImGuiKey imgui; Key model; bool repeat; };
    static const K keys[] = {{ImGuiKey_UpArrow, Key::Up, true}, {ImGuiKey_DownArrow, Key::Down, true},
                             {ImGuiKey_PageUp, Key::PageUp, true}, {ImGuiKey_PageDown, Key::PageDown, true},
                             {ImGuiKey_Tab, Key::Tab, false}, {ImGuiKey_Enter, Key::Enter, false},
                             {ImGuiKey_KeypadEnter, Key::Enter, false}, {ImGuiKey_Escape, Key::Escape, false}};
    for (const K& k : keys) {
        if (!ImGui::IsKeyPressed(k.imgui, k.repeat)) continue;
        Outcome o = HandleKey(state_, view_, entries_, k.model);
        if (o.close) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        if (o.activate >= 0) pendingKey_ = entries_[o.activate].key;
        if (k.model == Key::Tab) refocus_ = true;
        if (k.model == Key::Up || k.model == Key::Down || k.model == Key::PageUp || k.model == Key::PageDown) scrollToCursor_ = true;
        if (state_.query != buf_) std::snprintf(buf_, sizeof buf_, "%s", state_.query.c_str());
        if (o.rebuild) { view_ = Build(entries_, state_, prefs_); ClampCursor(state_, view_); }
    }

    ImGui::Dummy(ImVec2(0, 4.0f * sc));
    const float footerH = ImGui::GetTextLineHeightWithSpacing() * 2.0f + 6.0f * sc;
    DrawBody(std::max(60.0f * sc, ImGui::GetContentRegionAvail().y - footerH), sc);

    // Footer: why something cannot be added, or the keys.
    ImGui::PushTextWrapPos(0.0f);
    if (!state_.notice.empty()) ImGui::TextColored(ImVec4(0.92f, 0.66f, 0.22f, 1.0f), "%s", state_.notice.c_str());
    else ImGui::TextDisabled("Type to search    Up / Down: move    Enter: add    Tab: categories    Esc: close");
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}

void Session::DrawBody(float bodyH, float sc) {
    const float fs = ImGui::GetFontSize();
    ImDrawList* dl = nullptr;
    const float leftW = std::max(110.0f * sc, std::min(160.0f * sc, ImGui::GetWindowWidth() * 0.30f));

    // ---- left: categories --------------------------------------------------------------------------------------------
    ImGui::BeginChild("##accats", ImVec2(leftW, bodyH), ImGuiChildFlags_None);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
    dl = ImGui::GetWindowDrawList();
    bool sepDone = false;
    for (std::size_t i = 0; i < view_.cats.size(); ++i) {
        const CatCount& c = view_.cats[i];
        if (!c.special && !sepDone && i > 0) { ImGui::Separator(); sepDone = true; }
        const bool sel = !view_.searching && state_.category == c.name;
        const float rowH = fs * 1.9f;
        ImGui::PushID((int)i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##cat", sel, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2(w, rowH))) {
            state_.SelectCategory(c.name); buf_[0] = '\0'; refocus_ = true;
        }
        const ImVec4 tint = CatTint(c.name);
        dl->AddCircleFilled(ImVec2(p.x + fs * 0.75f, p.y + rowH * 0.5f), fs * 0.28f, ImGui::ColorConvertFloat4ToU32(tint));
        const ImU32 txt = ImGui::GetColorU32(c.count > 0 || sel ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        const std::string nm = Fit(c.name, w - fs * 3.0f);
        dl->AddText(ImVec2(p.x + fs * 1.4f, p.y + (rowH - fs) * 0.5f), txt, nm.c_str());
        char cnt[16]; std::snprintf(cnt, sizeof cnt, "%d", c.count);
        dl->AddText(ImVec2(p.x + w - ImGui::CalcTextSize(cnt).x - fs * 0.4f, p.y + (rowH - fs) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), cnt);
        if (sel && state_.column == Column::Categories && !view_.searching)
            dl->AddRect(p, ImVec2(p.x + w, p.y + rowH), ImGui::ColorConvertFloat4ToU32(AccentCol(0.95f)), 4.0f * sc, 0, 1.5f);
        ImGui::PopID();
    }
    ImGui::PopItemFlag(); ImGui::PopItemFlag();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 8.0f * sc);

    // ---- right: component cards ---------------------------------------------------------------------------------------
    ImGui::BeginChild("##accards", ImVec2(0, bodyH), ImGuiChildFlags_None);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 5.0f * sc));
    dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const bool showCat = view_.searching || state_.category == kCatForObject || state_.category == kCatRecent || state_.category == kCatFavorites;
    for (int i = 0; i < (int)view_.rows.size(); ++i) {
        const Row& r = view_.rows[i];
        const Entry& e = entries_[r.entry];
        const float cardH = fs * 3.3f;
        const float cw = ImGui::GetContentRegionAvail().x;
        if (r.groupStart) {   // a sub-group heading (Colliders 3D, Joints ...) above its first card
            ImGui::Dummy(ImVec2(0, fs * 0.25f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(r.group.c_str());
            ImGui::PopStyleColor();
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(cw, cardH));
        const bool hov = ImGui::IsItemHovered();
        if (hov && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) && state_.cursor != i) { state_.cursor = i; state_.notice.clear(); }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal)) {   // hover preview: what it adds
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(fs * 22.0f);
            ImGui::TextUnformatted(PreviewText(e).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        const bool cur = i == state_.cursor;
        const ImVec2 q(p.x + cw, p.y + cardH);
        const float rad = 8.0f * sc;
        ImU32 bg = ImGui::GetColorU32(hov ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        if (cur) bg = ImGui::ColorConvertFloat4ToU32(AccentCol(0.22f));
        dl->AddRectFilled(p, q, bg, rad);
        if (cur) dl->AddRect(p, q, ImGui::ColorConvertFloat4ToU32(AccentCol(0.95f)), rad, 0, 1.5f);

        const float dim = e.available ? 1.0f : 0.5f;
        // badge
        const float bs = fs * 2.0f;
        const ImVec2 b0(p.x + fs * 0.6f, p.y + (cardH - bs) * 0.5f);
        ImVec4 tint = CatTint(e.category); tint.w = dim;
        dl->AddRectFilled(b0, ImVec2(b0.x + bs, b0.y + bs), ImGui::ColorConvertFloat4ToU32(tint), 6.0f * sc);
        const std::string badge = Badge(e.name);
        const ImVec2 bt = ImGui::CalcTextSize(badge.c_str());
        dl->AddText(ImVec2(b0.x + (bs - bt.x) * 0.5f, b0.y + (bs - bt.y) * 0.5f), IM_COL32(255, 255, 255, e.available ? 255 : 170), badge.c_str());

        // two lines of text
        const float x0 = b0.x + bs + fs * 0.7f;
        const float rightW = fs * 3.6f;
        const float textW = std::max(40.0f, cw - (x0 - p.x) - rightW);
        const float y1 = p.y + fs * 0.5f, y2 = y1 + fs + 4.0f * sc;
        const ImU32 base = ImGui::GetColorU32(ImGuiCol_Text, dim);
        const ImU32 hi = ImGui::ColorConvertFloat4ToU32(AccentCol(1.0f));
        const float nameW = DrawName(dl, ImVec2(x0, y1), e.name, r.hits, base, hi, textW);
        if (showCat && nameW + fs * 5.0f < textW)
            dl->AddText(ImVec2(x0 + nameW + fs * 0.6f, y1), ImGui::GetColorU32(ImGuiCol_TextDisabled), e.category.c_str());
        std::string line2; ImU32 c2;
        if (!e.available)            { line2 = e.reason; c2 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.92f, 0.66f, 0.22f, 1.0f)); }
        else if (!r.note.empty())    { line2 = r.note; c2 = ImGui::ColorConvertFloat4ToU32(AccentCol(1.0f)); }
        else {
            line2 = e.description;
            const std::vector<std::string> also = AlsoAdds(e.name);
            if (!also.empty()) line2 += "  Also adds " + also.front() + ".";
            c2 = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        }
        dl->AddText(ImVec2(x0, y2), c2, Fit(line2, textW).c_str());

        // star and plus on the right
        const float midY = p.y + cardH * 0.5f;
        const ImVec2 plusC(q.x - fs * 1.2f, midY), starC(plusC.x - fs * 1.6f, midY);
        const bool fav = prefs_.IsFavorite(e.label);
        if (hov || cur || fav)
            DrawStar(dl, starC, fs * 0.55f, fav ? IM_COL32(236, 184, 40, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled), fav);
        const float pr = fs * 0.62f;
        if (e.available) {
            dl->AddCircleFilled(plusC, pr, ImGui::ColorConvertFloat4ToU32(AccentCol(cur || hov ? 1.0f : 0.75f)));
            dl->AddLine(ImVec2(plusC.x - pr * 0.5f, plusC.y), ImVec2(plusC.x + pr * 0.5f, plusC.y), IM_COL32_WHITE, 2.0f);
            dl->AddLine(ImVec2(plusC.x, plusC.y - pr * 0.5f), ImVec2(plusC.x, plusC.y + pr * 0.5f), IM_COL32_WHITE, 2.0f);
        } else {
            dl->AddCircle(plusC, pr, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f), 0, 1.5f);
            dl->AddLine(ImVec2(plusC.x - pr * 0.45f, plusC.y), ImVec2(plusC.x + pr * 0.45f, plusC.y), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f), 1.5f);
        }
        if (clicked) {
            const float dx = io.MousePos.x - starC.x, dy = io.MousePos.y - starC.y;
            if (dx * dx + dy * dy < fs * fs * 0.8f) { prefs_.ToggleFavorite(e.label); SaveNow(); refocus_ = true; }
            else {
                state_.cursor = i;
                Outcome o = Choose(state_, entries_, r.entry);
                if (o.activate >= 0) pendingKey_ = entries_[o.activate].key;
                refocus_ = true;
            }
        }
        if (cur && scrollToCursor_) { ImGui::SetScrollHereY(0.5f); scrollToCursor_ = false; }
        ImGui::PopID();
    }
    scrollToCursor_ = false;

    if (view_.rows.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        if (view_.searching) {
            ImGui::TextDisabled("Nothing matches \"%s\".", state_.query.c_str());
            if (!view_.didYouMean.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Did you mean...");
                for (const std::string& s : view_.didYouMean) {
                    if (ImGui::Button(s.c_str())) { std::snprintf(buf_, sizeof buf_, "%s", s.c_str()); state_.SetQuery(buf_); refocus_ = true; }
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            } else {
                ImGui::TextDisabled("Try a short word, like light, jump or sound.");
            }
        } else if (state_.category == kCatRecent) ImGui::TextDisabled("Components you add show up here.");
        else if (state_.category == kCatFavorites) ImGui::TextDisabled("Click the star on a component to keep it here.");
        else ImGui::TextDisabled("Nothing in this group.");
        ImGui::PopTextWrapPos();
    }
    ImGui::PopStyleVar();
    ImGui::PopItemFlag(); ImGui::PopItemFlag();
    ImGui::EndChild();
}

}  // namespace addcomp
}  // namespace oked
