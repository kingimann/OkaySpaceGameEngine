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
#include "okay/Components/UIRangeSlider.hpp"
#include "okay/Components/UIAspectRatio.hpp"
#include "okay/Components/UIInputValidator.hpp"
#include "okay/Components/UIGameHud.hpp"
#include "okay/Components/UIAppParts.hpp"
#include "okay/Components/UIDataGlue.hpp"
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
        if (auto* w = go->GetComponent<UIRangeSlider>()) return Usable(w->enabled, w->focusable, w->interactable);
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
        if (auto* w = go->GetComponent<UIRangeSlider>()) return w->IsFocused();
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
        if (auto* w = go->GetComponent<UIRangeSlider>()) w->SetFocused(f);
    }
    static void DropIfUnusable(GameObject* go, bool hidden) {
        if (!go || !IsFocused(go)) return;
        if (hidden || !Focusable(go)) SetFocused(go, false);
    }
    /// Left and right change this widget instead of moving the highlight.
    static bool UsesArrows(GameObject* go) {
        if (!go) return false;
        return go->GetComponent<UIDateTimePicker>() || go->GetComponent<UIPagination>()
            || go->GetComponent<UIAccordion>() || go->GetComponent<UIBreadcrumbs>() || go->GetComponent<UIRangeSlider>();
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
            return;
        }
        if (auto* w = go->GetComponent<UIRangeSlider>()) {
            if (right) w->Nudge(1);
            else if (left) w->Nudge(-1);
            else if (activate) w->SwapHandle();
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
    if (auto* w = go->GetComponent<UIRangeSlider>()) {
        out << "  uirange " << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y
            << ' ' << (int)w->anchor << ' ' << w->minValue << ' ' << w->maxValue << ' ' << w->low << ' ' << w->high
            << ' ' << w->step << ' ' << UIBit(w->wholeNumbers) << ' ' << w->minGap << ' ' << UIBit(w->showValue)
            << ' ' << UIBit(w->interactable) << ' ' << UIBit(w->focusable) << ' ' << w->cornerRadius
            << ' ' << (int)w->shape << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->fill); out << ' ';
        UIWriteColor(out, w->knob); out << ' ';
        UIWriteColor(out, w->textColor);
        out << ' ' << UIQuote(w->valueFormat) << '\n';
    }
    if (auto* w = go->GetComponent<UIAspectRatio>()) {
        out << "  uiaspect " << w->ratio << ' ' << (int)w->mode << ' ' << w->alignX << ' ' << w->alignY
            << ' ' << w->padding << '\n';
    }
    if (auto* w = go->GetComponent<UIInputValidator>()) {
        out << "  uivalidate " << (int)w->kind << ' ' << UIBit(w->required) << ' ' << w->minLength << ' ' << w->maxLength
            << ' ' << UIBit(w->useRange) << ' ' << w->minNumber << ' ' << w->maxNumber << ' ' << UIBit(w->quietUntilTouched)
            << ' ' << UIBit(w->showMessage) << ' ' << UIBit(w->showOk) << ' ';
        UIWriteColor(out, w->errorColor); out << ' ';
        UIWriteColor(out, w->okColor);
        out << ' ' << UIQuote(w->mustMatch) << ' ' << UIQuote(w->message) << '\n';
    }
    // ---- slice 2: game HUD parts, data glue ----
    auto hudBase = [&](const UIHudBase* w) {
        out << w->position.x << ' ' << w->position.y << ' ' << w->size.x << ' ' << w->size.y << ' ' << (int)w->anchor << ' ';
        UIWriteColor(out, w->background); out << ' ';
        UIWriteColor(out, w->accent); out << ' ';
        UIWriteColor(out, w->textColor);
    };
    if (auto* w = go->GetComponent<UIRadialMenu>()) {
        out << "  uiradialmenu "; hudBase(w);
        out << ' ' << w->innerRadius << ' ' << (int)w->openKey << ' ' << UIBit(w->holdToOpen) << ' ' << UIBit(w->closeOnSelect)
            << ' ' << UIBit(w->alwaysShown) << ' ' << UIQuote(w->items) << ' ' << UIQuote(w->centerText) << ' ' << UIQuote(w->chosenVar) << '\n';
    }
    if (auto* w = go->GetComponent<UIKillFeed>()) {
        out << "  uikillfeed "; hudBase(w);
        out << ' ' << w->maxEntries << ' ' << w->lifetime << ' ' << w->fadeTime << ' ' << w->rowHeight << ' ' << UIBit(w->alignRight)
            << ' ' << UIQuote(w->playerName) << ' ' << UIQuote(w->format) << ' ' << UIQuote(w->weaponFormat) << '\n';
    }
    if (auto* w = go->GetComponent<UIAmmoCounter>()) {
        out << "  uiammo "; hudBase(w);
        out << ' ' << w->current << ' ' << w->reserve << ' ' << w->magazine << ' ' << UIBit(w->reloading) << ' ' << w->lowFraction
            << ' ' << UIBit(w->showPips) << ' ' << UIBit(w->showReserve) << ' ';
        UIWriteColor(out, w->warnColor); out << ' ';
        UIWriteColor(out, w->emptyColor);
        out << ' ' << UIQuote(w->weapon) << ' ' << UIQuote(w->currentVar) << ' ' << UIQuote(w->reserveVar) << ' ' << UIQuote(w->label) << '\n';
    }
    if (auto* w = go->GetComponent<UIInteractionPrompt>()) {
        out << "  uiprompt "; hudBase(w);
        out << ' ' << w->range << ' ' << UIBit(w->alwaysShow) << ' ' << w->fadeSpeed << ' ' << UIQuote(w->key) << ' ' << UIQuote(w->action)
            << ' ' << UIQuote(w->target) << ' ' << UIQuote(w->showVar) << ' ' << UIQuote(w->playerTag) << '\n';
    }
    if (auto* w = go->GetComponent<UIObjectiveTracker>()) {
        out << "  uitracker "; hudBase(w);
        out << ' ' << w->maxQuests << ' ' << UIBit(w->showDescription) << ' ';
        UIWriteColor(out, w->doneColor);
        out << ' ' << UIQuote(w->title) << ' ' << UIQuote(w->questId) << '\n';
    }
    if (auto* w = go->GetComponent<UICompass>()) {
        out << "  uicompass "; hudBase(w);
        out << ' ' << w->heading << ' ' << w->fieldOfView << ' ' << UIBit(w->showDegrees) << ' ' << UIBit(w->showMarkerDistance) << ' ';
        UIWriteColor(out, w->markerColor);
        out << ' ' << UIQuote(w->source) << ' ' << UIQuote(w->markers) << ' ' << UIQuote(w->headingVar) << '\n';
    }
    if (auto* w = go->GetComponent<UIItemTooltipCard>()) {
        out << "  uicard "; hudBase(w);
        out << ' ' << w->count << ' ' << UIBit(w->showIcon) << ' ';
        UIWriteColor(out, w->nameColor);
        out << ' ' << UIQuote(w->item) << ' ' << UIQuote(w->itemVar) << ' ' << UIQuote(w->equipment) << ' ' << UIQuote(w->itemsFile) << '\n';
    }
    if (auto* w = go->GetComponent<UIAbilityIcon>()) {
        out << "  uiability " << (int)w->useKey << ' ' << w->cooldown << ' ' << w->maxCharges << ' ';
        UIWriteColor(out, w->iconColor); out << ' ';
        UIWriteColor(out, w->readyColor);
        out << ' ' << UIQuote(w->label) << ' ' << UIQuote(w->iconPath) << ' ' << UIQuote(w->keyText) << '\n';
    }
    // ---- slice 3: app parts ----
    auto appBase = [&](const UIAppBase* w) {
        hudBase(w);
        out << ' ' << w->fontScale << ' ' << UIBit(w->interactable);
    };
    if (auto* w = go->GetComponent<UIMenuBar>()) {
        out << "  uimenubar "; appBase(w);
        out << ' ' << w->itemHeight << ' ' << w->menuWidth << ' ' << UIQuote(w->menus) << ' ' << UIQuote(w->chosenVar) << '\n';
    }
    if (auto* w = go->GetComponent<UIToolbar>()) {
        out << "  uitoolbar "; appBase(w);
        out << ' ' << UIBit(w->showLabels) << ' ' << UIQuote(w->items) << ' ' << UIQuote(w->states) << ' ' << UIQuote(w->chosenVar) << ' ' << UIQuote(w->varPrefix) << '\n';
    }
    if (auto* w = go->GetComponent<UITreeView>()) {
        out << "  uitree "; appBase(w);
        out << ' ' << w->rowHeight << ' ' << w->indent << ' ' << w->selected << ' ' << UIQuote(w->nodes) << ' ' << UIQuote(w->expanded) << ' ' << UIQuote(w->selectedVar) << '\n';
    }
    if (auto* w = go->GetComponent<UIDataTable>()) {
        out << "  uitable "; appBase(w);
        out << ' ' << w->rowHeight << ' ' << w->headerHeight << ' ' << w->sortColumn << ' ' << UIBit(w->ascending) << ' ' << UIBit(w->sortable)
            << ' ' << UIBit(w->zebra) << ' ' << w->selected << ' ' << UIQuote(w->columns) << ' ' << UIQuote(w->rows) << ' ' << UIQuote(w->selectedVar) << '\n';
    }
    if (auto* w = go->GetComponent<UIWizard>()) {
        out << "  uiwizard "; appBase(w);
        out << ' ' << w->step << ' ' << UIBit(w->showButtons) << ' ' << UIBit(w->freeNavigation) << ' ' << UIQuote(w->steps) << ' ' << UIQuote(w->pages)
            << ' ' << UIQuote(w->validate) << ' ' << UIQuote(w->progressVar) << ' ' << UIQuote(w->stepVar) << '\n';
    }
    if (auto* w = go->GetComponent<UIFormGroup>()) {
        out << "  uiform "; appBase(w);
        out << ' ' << w->labelWidth << ' ' << w->rowHeight << ' ' << w->fieldHeight << ' ' << UIBit(w->showSummary) << ' ';
        UIWriteColor(out, w->fieldColor); out << ' ';
        UIWriteColor(out, w->fieldFocusColor);
        out << ' ' << UIQuote(w->fields) << ' ' << UIQuote(w->submitLabel) << ' ' << UIQuote(w->valuesVar) << ' ' << UIQuote(w->validVar) << '\n';
    }
    if (auto* w = go->GetComponent<UIColorSquare>()) {
        out << "  uicolorsquare "; appBase(w);
        out << ' ' << w->hue << ' ' << w->sat << ' ' << w->brightness << '\n';
    }
    if (auto* w = go->GetComponent<UIBind>())
        out << "  uidatabind " << (int)w->mode << ' ' << UIQuote(w->var) << ' ' << UIQuote(w->varHigh) << '\n';
    if (auto* w = go->GetComponent<UIVisibleIf>())
        out << "  uivisible " << UIBit(w->invert) << ' ' << UIQuote(w->condition) << ' ' << UIQuote(w->target) << '\n';
    if (auto* w = go->GetComponent<UIListBind>())
        out << "  uilistbind " << UIQuote(w->var) << ' ' << UIQuote(w->format) << ' ' << UIQuote(w->emptyText) << '\n';
    if (auto* w = go->GetComponent<UIHotReload>())
        out << "  uihotreload " << w->interval << ' ' << UIBit(w->loadAtStart) << ' ' << UIQuote(w->file) << ' ' << UIQuote(w->target) << '\n';
    if (auto* w = go->GetComponent<UIRichLinks>())
        out << "  uirichlinks " << UIBit(w->fireEvent) << ' ' << UIBit(w->storeInPrefs) << '\n';
    if (auto* w = go->GetComponent<UIFocusHint>())
        out << "  uifocushint " << w->tabIndex << ' ' << UIQuote(w->up) << ' ' << UIQuote(w->down) << ' ' << UIQuote(w->left) << ' ' << UIQuote(w->right) << '\n';
}

inline bool TryReadMoreUI(const std::string& field, std::istream& in, GameObject* go) {
    if (!go) return false;
    auto bit = [](int v) { return v != 0; };
    auto readHud = [&](UIHudBase* w) {
        int anchor = 0;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor;
        w->anchor = (UIAnchor)anchor;
        UIReadColor(in, w->background); UIReadColor(in, w->accent); UIReadColor(in, w->textColor);
    };
    auto readApp = [&](UIAppBase* w) {
        readHud(w);
        int inter = 1;
        in >> w->fontScale >> inter;
        w->interactable = bit(inter);
    };
    if (field == "uimenubar") {
        auto* w = go->AddComponent<UIMenuBar>();
        readApp(w);
        in >> w->itemHeight >> w->menuWidth;
        w->menus = UIReadQuoted(in); w->chosenVar = UIReadQuoted(in);
        return true;
    }
    if (field == "uitoolbar") {
        auto* w = go->AddComponent<UIToolbar>();
        int labels = 1;
        readApp(w);
        in >> labels;
        w->showLabels = bit(labels);
        w->items = UIReadQuoted(in); w->states = UIReadQuoted(in); w->chosenVar = UIReadQuoted(in); w->varPrefix = UIReadQuoted(in);
        return true;
    }
    if (field == "uitree") {
        auto* w = go->AddComponent<UITreeView>();
        readApp(w);
        in >> w->rowHeight >> w->indent >> w->selected;
        w->nodes = UIReadQuoted(in); w->expanded = UIReadQuoted(in); w->selectedVar = UIReadQuoted(in);
        return true;
    }
    if (field == "uitable") {
        auto* w = go->AddComponent<UIDataTable>();
        int asc = 1, sortable = 1, zebra = 1;
        readApp(w);
        in >> w->rowHeight >> w->headerHeight >> w->sortColumn >> asc >> sortable >> zebra >> w->selected;
        w->ascending = bit(asc); w->sortable = bit(sortable); w->zebra = bit(zebra);
        w->columns = UIReadQuoted(in); w->rows = UIReadQuoted(in); w->selectedVar = UIReadQuoted(in);
        return true;
    }
    if (field == "uiwizard") {
        auto* w = go->AddComponent<UIWizard>();
        int buttons = 1, freeNav = 0;
        readApp(w);
        in >> w->step >> buttons >> freeNav;
        w->showButtons = bit(buttons); w->freeNavigation = bit(freeNav);
        w->steps = UIReadQuoted(in); w->pages = UIReadQuoted(in); w->validate = UIReadQuoted(in);
        w->progressVar = UIReadQuoted(in); w->stepVar = UIReadQuoted(in);
        return true;
    }
    if (field == "uiform") {
        auto* w = go->AddComponent<UIFormGroup>();
        int summary = 1;
        readApp(w);
        in >> w->labelWidth >> w->rowHeight >> w->fieldHeight >> summary;
        w->showSummary = bit(summary);
        UIReadColor(in, w->fieldColor); UIReadColor(in, w->fieldFocusColor);
        w->fields = UIReadQuoted(in); w->submitLabel = UIReadQuoted(in); w->valuesVar = UIReadQuoted(in); w->validVar = UIReadQuoted(in);
        return true;
    }
    if (field == "uicolorsquare") {
        auto* w = go->AddComponent<UIColorSquare>();
        readApp(w);
        in >> w->hue >> w->sat >> w->brightness;
        return true;
    }
    if (field == "uiradialmenu") {
        auto* w = go->AddComponent<UIRadialMenu>();
        int key = 0, hold = 1, close = 1, always = 0;
        readHud(w);
        in >> w->innerRadius >> key >> hold >> close >> always;
        w->openKey = (char)key; w->holdToOpen = bit(hold); w->closeOnSelect = bit(close); w->alwaysShown = bit(always);
        w->items = UIReadQuoted(in); w->centerText = UIReadQuoted(in); w->chosenVar = UIReadQuoted(in);
        return true;
    }
    if (field == "uikillfeed") {
        auto* w = go->AddComponent<UIKillFeed>();
        int right = 1;
        readHud(w);
        in >> w->maxEntries >> w->lifetime >> w->fadeTime >> w->rowHeight >> right;
        w->alignRight = bit(right);
        w->playerName = UIReadQuoted(in); w->format = UIReadQuoted(in); w->weaponFormat = UIReadQuoted(in);
        return true;
    }
    if (field == "uiammo") {
        auto* w = go->AddComponent<UIAmmoCounter>();
        int rel = 0, pips = 1, res = 1;
        readHud(w);
        in >> w->current >> w->reserve >> w->magazine >> rel >> w->lowFraction >> pips >> res;
        w->reloading = bit(rel); w->showPips = bit(pips); w->showReserve = bit(res);
        UIReadColor(in, w->warnColor); UIReadColor(in, w->emptyColor);
        w->weapon = UIReadQuoted(in); w->currentVar = UIReadQuoted(in); w->reserveVar = UIReadQuoted(in); w->label = UIReadQuoted(in);
        return true;
    }
    if (field == "uiprompt") {
        auto* w = go->AddComponent<UIInteractionPrompt>();
        int always = 0;
        readHud(w);
        in >> w->range >> always >> w->fadeSpeed;
        w->alwaysShow = bit(always);
        w->key = UIReadQuoted(in); w->action = UIReadQuoted(in); w->target = UIReadQuoted(in);
        w->showVar = UIReadQuoted(in); w->playerTag = UIReadQuoted(in);
        return true;
    }
    if (field == "uitracker") {
        auto* w = go->AddComponent<UIObjectiveTracker>();
        int desc = 0;
        readHud(w);
        in >> w->maxQuests >> desc;
        w->showDescription = bit(desc);
        UIReadColor(in, w->doneColor);
        w->title = UIReadQuoted(in); w->questId = UIReadQuoted(in);
        return true;
    }
    if (field == "uicompass") {
        auto* w = go->AddComponent<UICompass>();
        int deg = 1, dist = 1;
        readHud(w);
        in >> w->heading >> w->fieldOfView >> deg >> dist;
        w->showDegrees = bit(deg); w->showMarkerDistance = bit(dist);
        UIReadColor(in, w->markerColor);
        w->source = UIReadQuoted(in); w->markers = UIReadQuoted(in); w->headingVar = UIReadQuoted(in);
        return true;
    }
    if (field == "uicard") {
        auto* w = go->AddComponent<UIItemTooltipCard>();
        int icon = 1;
        readHud(w);
        in >> w->count >> icon;
        w->showIcon = bit(icon);
        UIReadColor(in, w->nameColor);
        w->item = UIReadQuoted(in); w->itemVar = UIReadQuoted(in); w->equipment = UIReadQuoted(in); w->itemsFile = UIReadQuoted(in);
        return true;
    }
    if (field == "uiability") {
        auto* w = go->AddComponent<UIAbilityIcon>();
        int key = 0;
        in >> key >> w->cooldown >> w->maxCharges;
        w->useKey = (char)key;
        UIReadColor(in, w->iconColor); UIReadColor(in, w->readyColor);
        w->label = UIReadQuoted(in); w->iconPath = UIReadQuoted(in); w->keyText = UIReadQuoted(in);
        return true;
    }
    if (field == "uidatabind") {
        auto* w = go->AddComponent<UIBind>();
        int mode = 0;
        in >> mode;
        if (mode < 0 || mode > 2) mode = 0;
        w->mode = (UIBind::Mode)mode;
        w->var = UIReadQuoted(in); w->varHigh = UIReadQuoted(in);
        return true;
    }
    if (field == "uivisible") {
        auto* w = go->AddComponent<UIVisibleIf>();
        int inv = 0;
        in >> inv;
        w->invert = bit(inv);
        w->condition = UIReadQuoted(in); w->target = UIReadQuoted(in);
        return true;
    }
    if (field == "uilistbind") {
        auto* w = go->AddComponent<UIListBind>();
        w->var = UIReadQuoted(in); w->format = UIReadQuoted(in); w->emptyText = UIReadQuoted(in);
        return true;
    }
    if (field == "uihotreload") {
        auto* w = go->AddComponent<UIHotReload>();
        int load = 1;
        in >> w->interval >> load;
        w->loadAtStart = bit(load);
        w->file = UIReadQuoted(in); w->target = UIReadQuoted(in);
        return true;
    }
    if (field == "uirichlinks") {
        auto* w = go->AddComponent<UIRichLinks>();
        int ev = 1, pr = 1;
        in >> ev >> pr;
        w->fireEvent = bit(ev); w->storeInPrefs = bit(pr);
        return true;
    }
    if (field == "uifocushint") {
        auto* w = go->AddComponent<UIFocusHint>();
        in >> w->tabIndex;
        w->up = UIReadQuoted(in); w->down = UIReadQuoted(in); w->left = UIReadQuoted(in); w->right = UIReadQuoted(in);
        return true;
    }
    if (field == "uirange") {
        auto* w = go->AddComponent<UIRangeSlider>();
        int anchor = 0, shape = 0, whole = 0, show = 1, inter = 1, foc = 1;
        in >> w->position.x >> w->position.y >> w->size.x >> w->size.y >> anchor >> w->minValue >> w->maxValue
           >> w->low >> w->high >> w->step >> whole >> w->minGap >> show >> inter >> foc >> w->cornerRadius >> shape;
        w->anchor = (UIAnchor)anchor; w->shape = (UIShape)shape;
        w->wholeNumbers = bit(whole); w->showValue = bit(show); w->interactable = bit(inter); w->focusable = bit(foc);
        UIReadColor(in, w->background); UIReadColor(in, w->fill); UIReadColor(in, w->knob); UIReadColor(in, w->textColor);
        w->valueFormat = UIReadQuoted(in);
        return true;
    }
    if (field == "uiaspect") {
        auto* w = go->AddComponent<UIAspectRatio>();
        int mode = 0;
        in >> w->ratio >> mode >> w->alignX >> w->alignY >> w->padding;
        if (mode < 0 || mode > (int)UIAspectRatio::Mode::HeightControls) mode = 0;
        w->mode = (UIAspectRatio::Mode)mode;
        return true;
    }
    if (field == "uivalidate") {
        auto* w = go->AddComponent<UIInputValidator>();
        int kind = 0, req = 1, range = 0, quiet = 1, msg = 1, ok = 0;
        in >> kind >> req >> w->minLength >> w->maxLength >> range >> w->minNumber >> w->maxNumber >> quiet >> msg >> ok;
        if (kind < 0 || kind > (int)UIInputValidator::Kind::LettersAndDigits) kind = 0;
        w->kind = (UIInputValidator::Kind)kind;
        w->required = bit(req); w->useRange = bit(range); w->quietUntilTouched = bit(quiet);
        w->showMessage = bit(msg); w->showOk = bit(ok);
        UIReadColor(in, w->errorColor); UIReadColor(in, w->okColor);
        w->mustMatch = UIReadQuoted(in);
        w->message = UIReadQuoted(in);
        return true;
    }
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
