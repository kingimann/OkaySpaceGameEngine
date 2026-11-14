#pragma once
// Registration helpers for the extra in-game widgets: keyboard and gamepad
// focus, scene save/load, and theme colours. The widgets themselves live in
// their own headers.
#include "okay/Components/UISearchBar.hpp"
#include "okay/Components/UIDateTimePicker.hpp"
#include "okay/Components/UIFilePicker.hpp"
#include "okay/Components/UIAccordion.hpp"
#include "okay/Components/UIBreadcrumbs.hpp"
#include "okay/Components/UIPagination.hpp"
#include "okay/Components/UISkeleton.hpp"
#include "okay/Components/UIBottomSheet.hpp"
#include <iostream>
#include <string>

namespace okay {

inline std::string UIQuote(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

inline std::string UIReadQuoted(std::istream& in) {
    in >> std::ws;
    if (in.peek() != '"') {
        std::string tok;
        in >> tok;
        return tok;
    }
    in.get();
    std::string out;
    char c;
    while (in.get(c)) {
        if (c == '\\') { if (in.get(c)) out += c; }
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

inline void UIWriteColor(std::ostream& o, const Color& c) {
    o << c.r << ' ' << c.g << ' ' << c.b << ' ' << c.a;
}
inline void UIReadColor(std::istream& in, Color& c) {
    in >> c.r >> c.g >> c.b >> c.a;
}
inline int UIBit(bool v) { return v ? 1 : 0; }

/// Keyboard and gamepad participation. NavigateUI asks these instead of growing
/// a switch for every new widget.
struct MoreNav {
    static bool Usable(bool enabled, bool focusable, bool interactable) {
        return enabled && focusable && interactable;
    }
    static bool Focusable(GameObject* go) {
        if (!go) return false;
        if (auto* w = go->GetComponent<UISearchBar>()) return Usable(w->enabled, w->focusable, w->interactable);
        if (auto* w = go->GetComponent<UIDateTimePicker>()) return Usable(w->enabled, w->focusable, w->interactable);
        if (auto* w = go->GetComponent<UIFilePicker>()) return Usable(w->enabled, w->focusable, w->interactable);
        if (auto* w = go->GetComponent<UIAccordion>()) return Usable(w->enabled, w->focusable, w->interactable) && w->Count() > 0;
        if (auto* w = go->GetComponent<UIBreadcrumbs>()) return Usable(w->enabled, w->focusable, w->interactable) && !w->Segments().empty();
        if (auto* w = go->GetComponent<UIPagination>()) return Usable(w->enabled, w->focusable, w->interactable);
        if (auto* w = go->GetComponent<UIBottomSheet>()) return Usable(w->enabled, w->focusable, w->interactable);
        return false;
    }
    static bool HasKeyboard(GameObject* go) {
        auto* w = go ? go->GetComponent<UISearchBar>() : nullptr;
        return w && w->enabled && w->editing;
    }
    static bool IsFocused(GameObject* go) {
        if (!go) return false;
        if (auto* w = go->GetComponent<UISearchBar>()) return w->IsFocused();
        if (auto* w = go->GetComponent<UIDateTimePicker>()) return w->IsFocused();
        if (auto* w = go->GetComponent<UIFilePicker>()) return w->IsFocused();
        if (auto* w = go->GetComponent<UIAccordion>()) return w->IsFocused();
        if (auto* w = go->GetComponent<UIBreadcrumbs>()) return w->IsFocused();
        if (auto* w = go->GetComponent<UIPagination>()) return w->IsFocused();
        if (auto* w = go->GetComponent<UIBottomSheet>()) return w->IsFocused();
        return false;
    }
    static void SetFocused(GameObject* go, bool f) {
        if (!go) return;
        if (auto* w = go->GetComponent<UISearchBar>()) w->SetFocused(f);
        if (auto* w = go->GetComponent<UIDateTimePicker>()) w->SetFocused(f);
        if (auto* w = go->GetComponent<UIFilePicker>()) w->SetFocused(f);
        if (auto* w = go->GetComponent<UIAccordion>()) w->SetFocused(f);
        if (auto* w = go->GetComponent<UIBreadcrumbs>()) w->SetFocused(f);
        if (auto* w = go->GetComponent<UIPagination>()) w->SetFocused(f);
        if (auto* w = go->GetComponent<UIBottomSheet>()) w->SetFocused(f);
    }
    static void DropIfUnusable(GameObject* go, bool hidden) {
        if (!go || !IsFocused(go)) return;
        if (hidden || !Focusable(go)) SetFocused(go, false);
    }
    /// Left and right change this widget instead of moving the highlight.
    static bool UsesArrows(GameObject* go) {
        if (!go) return false;
        return go->GetComponent<UIDateTimePicker>() || go->GetComponent<UIPagination>()
            || go->GetComponent<UIAccordion>() || go->GetComponent<UIBreadcrumbs>();
    }
    static void OnKeys(GameObject* go, bool left, bool right, bool activate) {
        if (!go) return;
        if (auto* w = go->GetComponent<UISearchBar>()) {
            if (activate && !w->editing) { w->editing = true; w->SetFocused(true); }
            return;
        }
        if (auto* w = go->GetComponent<UIDateTimePicker>()) {
            if (right) w->StepDay(1);
            else if (left) w->StepDay(-1);
            return;
        }
        if (auto* w = go->GetComponent<UIFilePicker>()) {
            if (activate) w->OpenHighlighted();
            return;
        }
        if (auto* w = go->GetComponent<UIAccordion>()) {
            if (right) w->FocusStep(1);
            else if (left) w->FocusStep(-1);
            else if (activate) w->Toggle(w->value < 0 ? 0 : w->value);
            return;
        }
        if (auto* w = go->GetComponent<UIBreadcrumbs>()) {
            if (right) w->Move(1);
            else if (left) w->Move(-1);
            else if (activate) w->Select(w->value);
            return;
        }
        if (auto* w = go->GetComponent<UIPagination>()) {
            if (right) w->Next();
            else if (left) w->Previous();
            return;
        }
        if (auto* w = go->GetComponent<UIBottomSheet>()) {
            if (activate) w->Toggle();
        }
    }
};

inline void WriteMoreUI(std::ostream& out, GameObject* go) {
    if (!go) return;
    if (auto* w = go->GetComponent<UISearchBar>()) {
        const auto items = w->Items();
        out << "  uisearch " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << w->value << ' ' << UIBit(w->caseSensitive) << ' ' << UIBit(w->editing)
            << ' ' << UIBit(w->interactable) << ' ' << UIBit(w->focusable) << ' ' << w->maxLength
            << ' ' << w->fieldHeight << ' ' << w->rowHeight << ' ' << w->cornerRadius << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->field); out << ' ';
        UIWriteColor(out, w->textColor); out << ' ';
        UIWriteColor(out, w->muted); out << ' ';
        UIWriteColor(out, w->accent);
        out << ' ' << UIQuote(w->placeholder) << ' ' << UIQuote(w->query) << ' ' << items.size();
        for (const auto& item : items) out << ' ' << UIQuote(item);
        out << '\n';
    }
    if (auto* w = go->GetComponent<UIDateTimePicker>()) {
        out << "  uidate " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << w->year << ' ' << w->month << ' ' << w->day << ' ' << w->hour
            << ' ' << w->minute << ' ' << w->minYear << ' ' << w->maxYear << ' ' << UIBit(w->showTime)
            << ' ' << UIBit(w->interactable) << ' ' << UIBit(w->focusable) << ' ' << w->cornerRadius
            << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->textColor); out << ' ';
        UIWriteColor(out, w->button); out << ' ';
        UIWriteColor(out, w->muted);
        out << '\n';
    }
    if (auto* w = go->GetComponent<UIFilePicker>()) {
        out << "  uifile " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << w->value << ' ' << UIBit(w->showHidden) << ' ' << UIBit(w->interactable)
            << ' ' << UIBit(w->focusable) << ' ' << w->headerHeight << ' ' << w->rowHeight << ' ' << w->cornerRadius
            << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->textColor); out << ' ';
        UIWriteColor(out, w->accent); out << ' ';
        UIWriteColor(out, w->muted);
        out << ' ' << UIQuote(w->root) << ' ' << UIQuote(w->current) << ' ' << UIQuote(w->selected) << '\n';
    }
    if (auto* w = go->GetComponent<UIAccordion>()) {
        const auto sections = w->Sections();
        out << "  uiaccordion " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << w->value << ' ' << UIBit(w->allowMultiple) << ' ' << UIBit(w->interactable)
            << ' ' << UIBit(w->focusable) << ' ' << w->headerHeight << ' ' << w->bodyHeight << ' ' << w->cornerRadius
            << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->header); out << ' ';
        UIWriteColor(out, w->textColor); out << ' ';
        UIWriteColor(out, w->muted); out << ' ';
        UIWriteColor(out, w->accent);
        out << ' ' << sections.size();
        for (const auto& s : sections)
            out << ' ' << UIQuote(s.title) << ' ' << UIQuote(s.body) << ' ' << UIBit(s.open);
        out << '\n';
    }
    if (auto* w = go->GetComponent<UIBreadcrumbs>()) {
        out << "  uibreadcrumb " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << w->value << ' ' << UIBit(w->interactable) << ' ' << UIBit(w->focusable)
            << ' ' << w->cornerRadius << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->textColor); out << ' ';
        UIWriteColor(out, w->accent);
        out << ' ' << UIQuote(w->pathText) << '\n';
    }
    if (auto* w = go->GetComponent<UIPagination>()) {
        out << "  uipage " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << w->page << ' ' << w->pageCount << ' ' << w->pageSize << ' ' << w->totalItems
            << ' ' << UIBit(w->interactable) << ' ' << UIBit(w->focusable) << ' ' << w->cornerRadius
            << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->textColor); out << ' ';
        UIWriteColor(out, w->accent); out << ' ';
        UIWriteColor(out, w->muted);
        out << '\n';
    }
    if (auto* w = go->GetComponent<UISkeleton>()) {
        out << "  uiskeleton " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << UIBit(w->loading) << ' ' << w->lines << ' ' << UIBit(w->showAvatar)
            << ' ' << w->shimmer << ' ' << UIBit(w->interactable) << ' ' << UIBit(w->focusable)
            << ' ' << w->cornerRadius << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->bar); out << ' ';
        UIWriteColor(out, w->highlight);
        out << '\n';
    }
    if (auto* w = go->GetComponent<UIBottomSheet>()) {
        out << "  uibsheet " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << UIBit(w->open) << ' ' << w->progress << ' ' << w->duration
            << ' ' << UIBit(w->closeOnBackdrop) << ' ' << UIBit(w->dim) << ' ' << w->handle << ' ' << w->backdrop
            << ' ' << UIBit(w->interactable) << ' ' << UIBit(w->focusable) << ' ' << w->cornerRadius
            << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->textColor); out << ' ';
        UIWriteColor(out, w->accent); out << ' ';
        UIWriteColor(out, w->dimColor);
        out << ' ' << UIQuote(w->title) << '\n';
    }
}

inline bool TryReadMoreUI(const std::string& field, std::istream& in, GameObject* go) {
    if (!go) return false;
    auto bit = [](int v) { return v != 0; };
    if (field == "uisearch") {
        auto* w = go->AddComponent<UISearchBar>();
        int anchor = 0, shape = 0, cs = 0, ed = 0, inter = 1, foc = 1, count = 0;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> w->value >> cs >> ed
           >> inter >> foc >> w->maxLength >> w->fieldHeight >> w->rowHeight >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->caseSensitive = bit(cs); w->editing = bit(ed); w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->field); UIReadColor(in, w->textColor);
        UIReadColor(in, w->muted); UIReadColor(in, w->accent);
        w->placeholder = UIReadQuoted(in);
        w->query = UIReadQuoted(in);
        in >> count;
        w->itemsText.clear();
        for (int i = 0; i < count; ++i) {
            if (i) w->itemsText += '\n';
            w->itemsText += UIReadQuoted(in);
        }
        return true;
    }
    if (field == "uidate") {
        auto* w = go->AddComponent<UIDateTimePicker>();
        int anchor = 0, shape = 0, show = 1, inter = 1, foc = 1;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> w->year >> w->month >> w->day
           >> w->hour >> w->minute >> w->minYear >> w->maxYear >> show >> inter >> foc >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->showTime = bit(show); w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->textColor);
        UIReadColor(in, w->button); UIReadColor(in, w->muted);
        return true;
    }
    if (field == "uifile") {
        auto* w = go->AddComponent<UIFilePicker>();
        int anchor = 0, shape = 0, hidden = 0, inter = 1, foc = 1;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> w->value >> hidden >> inter >> foc
           >> w->headerHeight >> w->rowHeight >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->showHidden = bit(hidden); w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->textColor);
        UIReadColor(in, w->accent); UIReadColor(in, w->muted);
        w->root = UIReadQuoted(in); w->current = UIReadQuoted(in); w->selected = UIReadQuoted(in);
        return true;
    }
    if (field == "uiaccordion") {
        auto* w = go->AddComponent<UIAccordion>();
        int anchor = 0, shape = 0, multi = 0, inter = 1, foc = 1, count = 0;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> w->value >> multi >> inter >> foc
           >> w->headerHeight >> w->bodyHeight >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->allowMultiple = bit(multi); w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->header); UIReadColor(in, w->textColor);
        UIReadColor(in, w->muted); UIReadColor(in, w->accent);
        in >> count;
        w->sectionsText.clear();
        w->openText.clear();
        for (int i = 0; i < count; ++i) {
            const std::string title = UIReadQuoted(in);
            const std::string body = UIReadQuoted(in);
            int open = 0; in >> open;
            if (i) { w->sectionsText += '\n'; w->openText += ','; }
            w->sectionsText += title + " | " + body;
            w->openText += open ? '1' : '0';
        }
        return true;
    }
    if (field == "uibreadcrumb") {
        auto* w = go->AddComponent<UIBreadcrumbs>();
        int anchor = 0, shape = 0, inter = 1, foc = 1;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> w->value >> inter >> foc
           >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->textColor); UIReadColor(in, w->accent);
        w->pathText = UIReadQuoted(in);
        return true;
    }
    if (field == "uipage") {
        auto* w = go->AddComponent<UIPagination>();
        int anchor = 0, shape = 0, inter = 1, foc = 1;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> w->page >> w->pageCount
           >> w->pageSize >> w->totalItems >> inter >> foc >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->textColor);
        UIReadColor(in, w->accent); UIReadColor(in, w->muted);
        return true;
    }
    if (field == "uiskeleton") {
        auto* w = go->AddComponent<UISkeleton>();
        int anchor = 0, shape = 0, loading = 1, avatar = 1, inter = 0, foc = 0;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> loading >> w->lines >> avatar
           >> w->shimmer >> inter >> foc >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->loading = bit(loading); w->showAvatar = bit(avatar);
        w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->bar); UIReadColor(in, w->highlight);
        return true;
    }
    if (field == "uibsheet") {
        auto* w = go->AddComponent<UIBottomSheet>();
        int anchor = 0, shape = 0, open = 0, backdrop = 1, dim = 1, inter = 1, foc = 1;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> open >> w->progress >> w->duration
           >> backdrop >> dim >> w->handle >> w->backdrop >> inter >> foc >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->open = bit(open); w->closeOnBackdrop = bit(backdrop); w->dim = bit(dim);
        w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->textColor);
        UIReadColor(in, w->accent); UIReadColor(in, w->dimColor);
        w->title = UIReadQuoted(in);
        return true;
    }
    return false;
}

/// Bake a theme's colours onto whichever of these widgets the object has.
inline void StyleMoreWidgets(GameObject* go, const Color& panel, const Color& text, const Color& muted,
                             const Color& accent, const Color& border) {
    if (!go) return;
    auto mix = [](Color a, Color b, float t) {
        return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f);
    };
    const Color card = mix(panel, text, 0.08f);
    const Color well = mix(panel, text, 0.16f);
    if (auto* w = go->GetComponent<UISearchBar>()) {
        w->background = card; w->field = well; w->textColor = text; w->muted = muted; w->accent = accent;
    }
    if (auto* w = go->GetComponent<UIDateTimePicker>()) {
        w->background = card; w->textColor = text; w->button = accent; w->muted = muted;
    }
    if (auto* w = go->GetComponent<UIFilePicker>()) {
        w->background = card; w->textColor = text; w->accent = accent; w->muted = muted;
    }
    if (auto* w = go->GetComponent<UIAccordion>()) {
        w->background = card; w->header = well; w->textColor = text; w->muted = muted; w->accent = accent;
    }
    if (auto* w = go->GetComponent<UIBreadcrumbs>()) {
        w->background = card; w->textColor = text; w->accent = accent;
    }
    if (auto* w = go->GetComponent<UIPagination>()) {
        w->background = card; w->textColor = text; w->accent = accent; w->muted = muted;
    }
    if (auto* w = go->GetComponent<UISkeleton>()) {
        w->background = card; w->bar = well; w->highlight = mix(well, text, 0.35f);
    }
    if (auto* w = go->GetComponent<UIBottomSheet>()) {
        w->background = card; w->textColor = text; w->accent = accent;
        Color dim = border; dim.a = 0.55f; w->dimColor = dim;
    }
}

} // namespace okay
