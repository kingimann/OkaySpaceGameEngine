#include "UIInspectorWidgetsMore.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include "imgui.h"
#include <cstring>
#include <string>

using namespace okay;
using okay::editor::EditorState;

namespace oked {
namespace {

bool EditColor(const char* label, Color& c, EditorState& ed) {
    float v[4] = {c.r, c.g, c.b, c.a};
    if (!ImGui::ColorEdit4(label, v)) return false;
    c = {v[0], v[1], v[2], v[3]};
    ed.dirty = true;
    return true;
}

bool EditText(const char* label, std::string& s, EditorState& ed, const char* tip) {
    char buf[512];
    std::strncpy(buf, s.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    const bool changed = ImGui::InputText(label, buf, sizeof(buf));
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    if (!changed) return false;
    s = buf;
    ed.dirty = true;
    return true;
}

void EditLines(const char* label, std::string& s, EditorState& ed, const char* tip) {
    static char buf[4096];
    std::strncpy(buf, s.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputTextMultiline(label, buf, sizeof(buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f))) {
        s = buf;
        ed.dirty = true;
    }
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
}

} // namespace

void BuildUISearchBar(GameObject* g) {
    auto* w = g->AddComponent<UISearchBar>();
    w->size = {320, 180};
    w->itemsText = "Apple\nApricot\nBanana\nCherry";
    w->placeholder = "Search";
}
void BuildUIDateTimePicker(GameObject* g) {
    auto* w = g->AddComponent<UIDateTimePicker>();
    w->size = {460, 64};
    w->year = 2026; w->month = 11; w->day = 13; w->hour = 15; w->minute = 30;
}
void BuildUIFilePicker(GameObject* g) {
    auto* w = g->AddComponent<UIFilePicker>();
    w->size = {320, 220};
}
void BuildUIAccordion(GameObject* g) {
    auto* w = g->AddComponent<UIAccordion>();
    w->size = {320, 220};
}
void BuildUIBreadcrumbs(GameObject* g) {
    auto* w = g->AddComponent<UIBreadcrumbs>();
    w->size = {420, 36};
    w->pathText = "Home/Library/Books";
    w->value = 2;
}
void BuildUIPagination(GameObject* g) {
    auto* w = g->AddComponent<UIPagination>();
    w->size = {360, 36};
    w->page = 2; w->pageCount = 8;
}
void BuildUISkeleton(GameObject* g) {
    auto* w = g->AddComponent<UISkeleton>();
    w->size = {320, 96};
    w->loading = true;
}
void BuildUIBottomSheet(GameObject* g) {
    auto* w = g->AddComponent<UIBottomSheet>();
    w->anchor = UIAnchor::BottomCenter;
    w->size = {420, 200};
    w->open = true;
    w->progress = 1.0f;
    w->duration = 0.0f;
    w->title = "Filters";
}

void BuildUIRangeSlider(GameObject* g) {
    auto* w = g->AddComponent<UIRangeSlider>();
    w->size = {300, 30};
}
void BuildUIAspectRatio(GameObject* g) {
    g->AddComponent<UIAspectRatio>();   // a container: put widgets under it and they keep this shape
}
void BuildUIInputValidation(GameObject* g) {
    if (!g->GetComponent<UIInputField>()) g->AddComponent<UIInputField>();   // it checks the field on the same object
    g->AddComponent<UIInputValidator>();
}

void DrawMoreUIComponentInspector(Component* comp, EditorState& ed, Component** toRemove,
                                  const UIInspectorHooks& hooks) {
    if (auto* w = dynamic_cast<UISearchBar*>(comp)) {
        if (!hooks.header("UI Search Bar", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##search", w->anchor, ed);
        float sz[2] = {w->size.x, w->size.y};
        if (ImGui::DragFloat2("Width and height##search", sz, 1.0f, 1.0f, 4000.0f)) { w->size = {sz[0], sz[1]}; ed.dirty = true; }
        EditText("Text to search for##search", w->query, ed, "The list below keeps only the lines that contain this.");
        EditLines("List to search##search", w->itemsText, ed, "One item on each line.");
        EditText("Hint when empty##search", w->placeholder, ed, nullptr);
        if (ImGui::Checkbox("Match capital letters##search", &w->caseSensitive)) ed.dirty = true;
        if (ImGui::Checkbox("The player can use this##search", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##search", &w->focusable)) ed.dirty = true;
        EditColor("Background##search", w->background, ed);
        EditColor("Text##search", w->textColor, ed);
        EditColor("Chosen row##search", w->accent, ed);
        ImGui::TextDisabled("The object's script gets on_change when the text or the row changes, and on_submit on Enter.");
        return;
    }
    if (auto* w = dynamic_cast<UIDateTimePicker*>(comp)) {
        if (!hooks.header("UI Date and Time", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##date", w->anchor, ed);
        if (ImGui::DragInt("Year##date", &w->year, 1.0f, w->minYear, w->maxYear)) { w->Clamp(); ed.dirty = true; }
        if (ImGui::DragInt("Month##date", &w->month, 0.1f, 1, 12)) { w->Clamp(); ed.dirty = true; }
        if (ImGui::DragInt("Day##date", &w->day, 0.1f, 1, 31)) { w->Clamp(); ed.dirty = true; }
        if (ImGui::DragInt("Hour##date", &w->hour, 0.1f, 0, 23)) { w->Clamp(); ed.dirty = true; }
        if (ImGui::DragInt("Minute##date", &w->minute, 0.1f, 0, 59)) { w->Clamp(); ed.dirty = true; }
        if (ImGui::DragInt("Earliest year##date", &w->minYear, 1.0f, 1, 9999)) { w->Clamp(); ed.dirty = true; }
        if (ImGui::DragInt("Latest year##date", &w->maxYear, 1.0f, 1, 9999)) { w->Clamp(); ed.dirty = true; }
        if (ImGui::Checkbox("Show the time as well as the date##date", &w->showTime)) ed.dirty = true;
        if (ImGui::Checkbox("The player can use this##date", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##date", &w->focusable)) ed.dirty = true;
        EditColor("Numbers##date", w->textColor, ed);
        EditColor("Arrows##date", w->button, ed);
        ImGui::TextDisabled("A day the month does not have, such as 31 February, is pulled back to the last real day.");
        ImGui::TextDisabled("on_change runs when the date or the time changes. Left and right on a gamepad step the day.");
        return;
    }
    if (auto* w = dynamic_cast<UIFilePicker*>(comp)) {
        if (!hooks.header("UI File Picker", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##file", w->anchor, ed);
        EditText("Folder this picker may open##file", w->root, ed, "Nothing outside this folder can be opened, including links that point out of it.");
        EditText("Folder on screen##file", w->current, ed, "A path inside the folder above. Empty means the folder itself.");
        EditText("Chosen file##file", w->selected, ed, "A file inside the folder above.");
        if (ImGui::Checkbox("Show names that start with a dot##file", &w->showHidden)) ed.dirty = true;
        if (ImGui::Checkbox("The player can use this##file", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##file", &w->focusable)) ed.dirty = true;
        EditColor("Names##file", w->textColor, ed);
        ImGui::TextDisabled("This stays inside the game. It does not open the computer's own file window.");
        ImGui::TextDisabled("on_change runs when the folder or the file changes. on_submit runs when a file is chosen.");
        return;
    }
    if (auto* w = dynamic_cast<UIAccordion*>(comp)) {
        if (!hooks.header("UI Accordion", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##acc", w->anchor, ed);
        EditLines("Sections##acc", w->sectionsText, ed, "One section on each line: Title | Body");
        EditText("Which sections start open##acc", w->openText, ed, "1 is open, 0 is closed, separated by commas. One number per section.");
        if (ImGui::Checkbox("More than one section can be open##acc", &w->allowMultiple)) ed.dirty = true;
        if (ImGui::Checkbox("The player can use this##acc", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##acc", &w->focusable)) ed.dirty = true;
        EditColor("Headings##acc", w->header, ed);
        EditColor("Open heading##acc", w->accent, ed);
        ImGui::TextDisabled("A click on a heading opens or closes it. on_change runs when that happens.");
        return;
    }
    if (auto* w = dynamic_cast<UIBreadcrumbs*>(comp)) {
        if (!hooks.header("UI Breadcrumbs", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##crumb", w->anchor, ed);
        EditText("Path##crumb", w->pathText, ed, "Folders separated by /. Choosing one drops the folders after it.");
        if (ImGui::DragInt("Highlighted crumb##crumb", &w->value, 0.1f, 0, 64)) ed.dirty = true;
        if (ImGui::Checkbox("The player can use this##crumb", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##crumb", &w->focusable)) ed.dirty = true;
        EditColor("Crumbs##crumb", w->textColor, ed);
        EditColor("Highlighted crumb colour##crumb", w->accent, ed);
        ImGui::TextDisabled("on_change runs when the path changes. Confirm on a gamepad keeps the highlighted crumb.");
        return;
    }
    if (auto* w = dynamic_cast<UIPagination*>(comp)) {
        if (!hooks.header("UI Pagination", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##page", w->anchor, ed);
        if (ImGui::DragInt("Page##page", &w->page, 0.1f, 1, w->pageCount < 1 ? 1 : w->pageCount)) { w->SetPage(w->page); ed.dirty = true; }
        if (ImGui::DragInt("How many pages##page", &w->pageCount, 0.1f, 1, 100000)) {
            if (w->pageCount < 1) w->pageCount = 1;
            w->SetPage(w->page);
            ed.dirty = true;
        }
        if (ImGui::DragInt("Items on each page##page", &w->pageSize, 0.1f, 1, 100000)) ed.dirty = true;
        if (ImGui::DragInt("Items in the list##page", &w->totalItems, 0.2f, 0, 10000000)) ed.dirty = true;
        if (ImGui::Checkbox("The player can use this##page", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##page", &w->focusable)) ed.dirty = true;
        EditColor("Current page##page", w->accent, ed);
        ImGui::TextDisabled("The page stays between 1 and the page count. on_change runs when it changes.");
        ImGui::TextDisabled("Left and right on a gamepad move a page.");
        return;
    }
    if (auto* w = dynamic_cast<UISkeleton*>(comp)) {
        if (!hooks.header("UI Skeleton", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##skel", w->anchor, ed);
        if (ImGui::Checkbox("Show the loading placeholder##skel", &w->loading)) ed.dirty = true;
        if (ImGui::DragInt("Text bars##skel", &w->lines, 0.1f, 1, 8)) ed.dirty = true;
        if (ImGui::Checkbox("Round picture placeholder##skel", &w->showAvatar)) ed.dirty = true;
        EditColor("Bars##skel", w->bar, ed);
        EditColor("Shine##skel", w->highlight, ed);
        ImGui::TextDisabled("Turn this off when the real content is ready. The bars move so the screen does not look stuck.");
        return;
    }
    if (auto* w = dynamic_cast<UIBottomSheet*>(comp)) {
        if (!hooks.header("UI Bottom Sheet", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##sheet", w->anchor, ed);
        EditText("Title##sheet", w->title, ed, nullptr);
        if (ImGui::Checkbox("Sheet is open##sheet", &w->open)) {
            if (w->duration <= 0.0f) w->progress = w->open ? 1.0f : 0.0f;
            ed.dirty = true;
        }
        if (ImGui::DragFloat("Seconds to slide##sheet", &w->duration, 0.01f, 0.0f, 5.0f)) ed.dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 snaps open and closed instead of sliding.");
        if (ImGui::Checkbox("A click above the sheet closes it##sheet", &w->closeOnBackdrop)) ed.dirty = true;
        if (ImGui::Checkbox("Darken the screen behind it##sheet", &w->dim)) ed.dirty = true;
        if (ImGui::Checkbox("The player can use this##sheet", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##sheet", &w->focusable)) ed.dirty = true;
        EditColor("Sheet##sheet", w->background, ed);
        ImGui::TextDisabled("The handle opens and closes it. on_change runs when it opens or closes. Confirm on a gamepad does the same.");
        return;
    }
    if (auto* w = dynamic_cast<UIRangeSlider*>(comp)) {
        if (!hooks.header("UI Range Slider", w, toRemove, true)) return;
        hooks.anchorCombo("Anchor##range", w->anchor, ed);
        float sz[2] = {w->size.x, w->size.y};
        if (ImGui::DragFloat2("Width and height##range", sz, 1.0f, 1.0f, 4000.0f)) { w->size = {sz[0], sz[1]}; ed.dirty = true; }
        if (ImGui::DragFloat("Lowest value##range", &w->minValue, 0.1f)) ed.dirty = true;
        if (ImGui::DragFloat("Highest value##range", &w->maxValue, 0.1f)) ed.dirty = true;
        if (ImGui::DragFloat("Left handle##range", &w->low, 0.1f, w->minValue, w->maxValue)) { const float v = w->low; w->SetLow(v); ed.dirty = true; }
        if (ImGui::DragFloat("Right handle##range", &w->high, 0.1f, w->minValue, w->maxValue)) { const float v = w->high; w->SetHigh(v); ed.dirty = true; }
        if (ImGui::DragFloat("Step##range", &w->step, 0.05f, 0.0f, 1000.0f)) ed.dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = smooth. Otherwise the handles snap to multiples of this.");
        if (ImGui::DragFloat("Smallest gap##range", &w->minGap, 0.1f, 0.0f, 1000.0f)) ed.dirty = true;
        if (ImGui::Checkbox("Whole numbers##range", &w->wholeNumbers)) ed.dirty = true;
        if (ImGui::Checkbox("Show the two numbers##range", &w->showValue)) ed.dirty = true;
        EditText("Number text##range", w->valueFormat, ed, "{lo} and {hi} are replaced. Empty shows \"{lo} - {hi}\".");
        if (ImGui::Checkbox("The player can use this##range", &w->interactable)) ed.dirty = true;
        if (ImGui::Checkbox("A gamepad can move here##range", &w->focusable)) ed.dirty = true;
        EditColor("Between the handles##range", w->fill, ed);
        EditColor("Track##range", w->background, ed);
        EditColor("Handles##range", w->knob, ed);
        ImGui::TextDisabled("Drag the handle nearest the click. on_change runs when either moves. Left and right move the picked handle; Confirm swaps handles.");
        return;
    }
    if (auto* w = dynamic_cast<UIAspectRatio*>(comp)) {
        if (!hooks.header("UI Aspect Ratio", w, toRemove, true)) return;
        if (ImGui::DragFloat("Width / height##aspect", &w->ratio, 0.01f, 0.05f, 20.0f)) ed.dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("16:9 is 1.78, 4:3 is 1.33, a square is 1.");
        static const char* kModes[] = {"Fit inside", "Fill (cover)", "Width controls height", "Height controls width"};
        int m = (int)w->mode;
        if (ImGui::Combo("How it sizes##aspect", &m, kModes, 4)) { w->mode = (UIAspectRatio::Mode)m; ed.dirty = true; }
        if (ImGui::SliderFloat("Left to right##aspect", &w->alignX, 0.0f, 1.0f)) ed.dirty = true;
        if (ImGui::SliderFloat("Top to bottom##aspect", &w->alignY, 0.0f, 1.0f)) ed.dirty = true;
        if (ImGui::DragFloat("Padding##aspect", &w->padding, 0.5f, 0.0f, 1000.0f)) ed.dirty = true;
        ImGui::TextDisabled("Put widgets under this object. They are laid out inside a box of this shape, centred in the space it has.");
        return;
    }
    if (auto* w = dynamic_cast<UIInputValidator*>(comp)) {
        if (!hooks.header("UI Input Validator", w, toRemove, true)) return;
        static const char* kKinds[] = {"Anything", "Email", "Whole number", "Number", "Letters", "Letters and digits"};
        int k = (int)w->kind;
        if (ImGui::Combo("Must be##valid", &k, kKinds, 6)) { w->kind = (UIInputValidator::Kind)k; ed.dirty = true; }
        if (ImGui::Checkbox("Required##valid", &w->required)) ed.dirty = true;
        if (ImGui::DragInt("Fewest characters##valid", &w->minLength, 0.1f, 0, 1000)) ed.dirty = true;
        if (ImGui::DragInt("Most characters (0 = any)##valid", &w->maxLength, 0.1f, 0, 1000)) ed.dirty = true;
        if (ImGui::Checkbox("Check the number range##valid", &w->useRange)) ed.dirty = true;
        if (w->useRange) {
            if (ImGui::DragFloat("Lowest number##valid", &w->minNumber, 0.1f)) ed.dirty = true;
            if (ImGui::DragFloat("Highest number##valid", &w->maxNumber, 0.1f)) ed.dirty = true;
        }
        EditText("Must equal this field##valid", w->mustMatch, ed, "The name of another object with a UI Input Field (a repeat-password box).");
        EditText("Your own message##valid", w->message, ed, "Empty shows a built-in sentence.");
        if (ImGui::Checkbox("Quiet until the player types##valid", &w->quietUntilTouched)) ed.dirty = true;
        if (ImGui::Checkbox("Show the message##valid", &w->showMessage)) ed.dirty = true;
        if (ImGui::Checkbox("Green ring when fine##valid", &w->showOk)) ed.dirty = true;
        EditColor("Error colour##valid", w->errorColor, ed);
        if (!w->gameObject || !w->gameObject->GetComponent<UIInputField>())
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "This object needs a UI Input Field to check.");
        ImGui::TextDisabled("The object's script gets on_valid and on_invalid when the answer changes.");
        return;
    }
}

} // namespace oked
