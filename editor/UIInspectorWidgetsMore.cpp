#include "UIInspectorWidgetsMore.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include "okay/Components/UIListView.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "imgui.h"
#include <cctype>
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

// "innerRadius" -> "Inner radius"
std::string Words(const std::string& camel) {
    std::string out;
    for (std::size_t i = 0; i < camel.size(); ++i) {
        const char c = camel[i];
        if (i == 0) out += (char)std::toupper((unsigned char)c);
        else if (std::isupper((unsigned char)c)) { out += ' '; out += (char)std::tolower((unsigned char)c); }
        else out += c;
    }
    return out;
}

// One inspector for any component that registered its properties (slice 2 parts): a row for every property the
// registry lists, drawn by type, with the registered tooltip. Returns false when the component has no entry.
bool DrawRegistryRows(Component* comp, EditorState& ed) {
    auto info = PropertyRegistry::FindFor(*comp);
    if (!info) return false;
    for (const PropertyDesc& pd : info->properties) {
        if (pd.IsHidden() || pd.IsReadOnly() || pd.IsRuntimeOnly()) continue;
        ImGui::PushID(pd.name.c_str());
        const std::string label = Words(pd.name);
        const PropertyValue v = pd.Get(*comp);
        auto commit = [&](const PropertyValue& nv) { if (pd.Set(*comp, nv)) ed.dirty = true; };
        switch (pd.type) {
            case PropertyType::Bool: {
                bool b = v.AsBool();
                if (ImGui::Checkbox(label.c_str(), &b)) commit(PropertyValue::MakeBool(b));
                break;
            }
            case PropertyType::Int: {
                const bool isKey = pd.name.size() > 3 && pd.name.compare(pd.name.size() - 3, 3, "Key") == 0;
                if (isKey) {
                    const long long code = v.AsInt();
                    char buf[2] = {(code >= 33 && code < 127) ? (char)code : '\0', '\0'};
                    if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) commit(PropertyValue::MakeInt(buf[0] ? (long long)std::tolower((unsigned char)buf[0]) : 0));
                } else {
                    int i = (int)v.AsInt();
                    if (ImGui::DragInt(label.c_str(), &i, 0.2f, pd.hasRange ? (int)pd.rangeMin : -100000, pd.hasRange ? (int)pd.rangeMax : 100000)) commit(PropertyValue::MakeInt(i));
                }
                break;
            }
            case PropertyType::Float: {
                float f = v.AsFloat();
                const float step = pd.step > 0.0 ? (float)pd.step : 0.1f;
                if (ImGui::DragFloat(label.c_str(), &f, step, pd.hasRange ? (float)pd.rangeMin : -1.0e6f, pd.hasRange ? (float)pd.rangeMax : 1.0e6f)) commit(PropertyValue::MakeFloat(f));
                break;
            }
            case PropertyType::String: {
                char buf[512];
                std::strncpy(buf, v.AsString().c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) commit(PropertyValue::MakeString(buf));
                break;
            }
            case PropertyType::Vec2: {
                const Vec2 a = v.AsVec2();
                float f[2] = {a.x, a.y};
                if (ImGui::DragFloat2(label.c_str(), f, 1.0f)) commit(PropertyValue::MakeVec2({f[0], f[1]}));
                break;
            }
            case PropertyType::Color: {
                const Color c = v.AsColor();
                float f[4] = {c.r, c.g, c.b, c.a};
                if (ImGui::ColorEdit4(label.c_str(), f)) commit(PropertyValue::MakeColor({f[0], f[1], f[2], f[3]}));
                break;
            }
            case PropertyType::Enum: {
                int cur = v.AsEnum();
                const char* shown = (cur >= 0 && cur < (int)pd.enumOptions.size()) ? pd.enumOptions[(std::size_t)cur].c_str() : "";
                if (ImGui::BeginCombo(label.c_str(), shown)) {
                    for (int i = 0; i < (int)pd.enumOptions.size(); ++i)
                        if (ImGui::Selectable(pd.enumOptions[(std::size_t)i].c_str(), i == cur)) commit(PropertyValue::MakeEnum(i));
                    ImGui::EndCombo();
                }
                break;
            }
            default: break;
        }
        if (!pd.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pd.tooltip.c_str());
        ImGui::PopID();
    }
    return true;
}

struct Slice2Part { const char* title; const char* help; };

// Which slice 2 component this is (null when it is not one), with its heading and a plain sentence of help.
const Slice2Part* Slice2Of(Component* comp) {
    static const Slice2Part kRadial{"UI Radial Menu", "Hold the open key, point at a wedge, let go to choose. on_select runs on the object's script; Chosen() is the wedge number (0 is the top)."};
    static const Slice2Part kFeed{"UI Kill Feed", "Lines stack here and fade out. A script calls UIKillFeed.Push(killer, victim, weapon)."};
    static const Slice2Part kAmmo{"UI Ammo Counter", "Reads a Ranged Weapon (the Player's, or the object named in Weapon), or the variables named below, or the numbers typed here."};
    static const Slice2Part kPrompt{"UI Interaction Prompt", "Shows only while the player is within Range of the Target object. Pressing the key runs on_interact on the script."};
    static const Slice2Part kTracker{"UI Objective Tracker", "Lists the active quests of the scene's Quest Log. Turn Show Tracker off on the Quest Log so the two do not both draw."};
    static const Slice2Part kCompass{"UI Compass", "The strip turns with the Source object (or the main camera). North is +Z. Put object names in Markers, separated by |."};
    static const Slice2Part kCard{"UI Item Tooltip Card", "Shows one item from the item database: set Item, or Item Var to a variable that holds an item, or call UIItemTooltipCard.Show(id, count)."};
    static const Slice2Part kAbility{"UI Ability Icon", "Needs a UI Radial Progress on the same object: that is the cooldown ring. Use() starts the cooldown; the key does the same."};
    static const Slice2Part kBind{"UI Bind", "Keeps the slider, stepper, toggle, dropdown, range slider or input field on this object and a variable the same, both ways."};
    static const Slice2Part kVisible{"UI Visible If", "Shows or hides the Target (or this object) while the game runs. Examples: hp < 30   !hasKey   score >= 10 && lives > 0   state == \"menu\"."};
    static const Slice2Part kListBind{"UI List Bind", "Fills the List View on this object from an array variable. The List View's first child is the row template; {item}, {index} and map fields fill its texts."};
    static const Slice2Part kHot{"UI Hot Reload", "Watches a markup file for the UI Document, or an .okaytheme for the UI Theme, on the Target object. Save the file and the screen updates while the game runs."};
    static const Slice2Part kHint{"UI Focus Hint", "Tab and Shift+Tab walk the Tab Index (lowest first). Up, Down, Left, Right name the object focus goes to; a - keeps focus where it is."};
    static const Slice2Part kLinks{"UI Rich Links", "Makes [url=target]words[/url] in this object's rich text clickable. A click runs on_link and stores the target in the Prefs value ui_link."};
    if (dynamic_cast<UIRadialMenu*>(comp)) return &kRadial;
    if (dynamic_cast<UIKillFeed*>(comp)) return &kFeed;
    if (dynamic_cast<UIAmmoCounter*>(comp)) return &kAmmo;
    if (dynamic_cast<UIInteractionPrompt*>(comp)) return &kPrompt;
    if (dynamic_cast<UIObjectiveTracker*>(comp)) return &kTracker;
    if (dynamic_cast<UICompass*>(comp)) return &kCompass;
    if (dynamic_cast<UIItemTooltipCard*>(comp)) return &kCard;
    if (dynamic_cast<UIAbilityIcon*>(comp)) return &kAbility;
    if (dynamic_cast<UIBind*>(comp)) return &kBind;
    if (dynamic_cast<UIVisibleIf*>(comp)) return &kVisible;
    if (dynamic_cast<UIListBind*>(comp)) return &kListBind;
    if (dynamic_cast<UIHotReload*>(comp)) return &kHot;
    if (dynamic_cast<UIFocusHint*>(comp)) return &kHint;
    if (dynamic_cast<UIRichLinks*>(comp)) return &kLinks;
    return nullptr;
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

// ---- slice 2: create helpers (GameObject > UI > Game HUD and Data) ----
void BuildUIRadialMenu(GameObject* g) {
    auto* w = g->AddComponent<UIRadialMenu>();
    w->size = {320, 320};
    w->openKey = 'q';
}
void BuildUIKillFeed(GameObject* g) {
    auto* w = g->AddComponent<UIKillFeed>();
    w->anchor = UIAnchor::TopRight; w->size = {360, 140};
    w->Push("Player", "Raider", "Rifle");
    w->lifetime = 600.0f;   // a sample line that stays while you edit; the game's own lines use the default of 6 s
}
void BuildUIAmmoCounter(GameObject* g) {
    auto* w = g->AddComponent<UIAmmoCounter>();
    w->anchor = UIAnchor::BottomRight; w->size = {220, 84};
}
void BuildUIInteractionPrompt(GameObject* g) {
    auto* w = g->AddComponent<UIInteractionPrompt>();
    w->anchor = UIAnchor::BottomCenter; w->size = {320, 40};
    w->alwaysShow = true;   // visible while you edit; turn it off and name a Target to hide it until the player is near
}
void BuildUIObjectiveTracker(GameObject* g) {
    auto* w = g->AddComponent<UIObjectiveTracker>();
    w->anchor = UIAnchor::TopRight; w->size = {300, 180};
}
void BuildUICompass(GameObject* g) {
    auto* w = g->AddComponent<UICompass>();
    w->anchor = UIAnchor::TopCenter; w->size = {560, 36};
}
void BuildUIItemTooltipCard(GameObject* g) {
    auto* w = g->AddComponent<UIItemTooltipCard>();
    w->size = {280, 160};
}
void BuildUIAbilityIcon(GameObject* g) {
    auto* r = g->AddComponent<UIRadialProgress>();
    r->size = {72, 72}; r->thickness = 6.0f; r->anchor = UIAnchor::BottomCenter;
    r->fill = Color::FromBytes(20, 22, 30, 235); r->background = Color::FromBytes(120, 220, 150, 200);
    g->AddComponent<UIAbilityIcon>();
}
void BuildUIBoundSlider(GameObject* g) {
    auto* s = g->AddComponent<UISlider>();
    s->size = {300, 24};
    g->AddComponent<UIBind>()->var = "volume";
}
void BuildUIBoundList(GameObject* g) {
    auto* lv = g->AddComponent<UIListView>();
    lv->size = {300, 220};
    GameObject* row = g->scene() ? g->scene()->CreateGameObject("Row") : nullptr;
    if (row) {
        row->transform->SetParent(g->transform, false);
        auto* t = row->AddComponent<TextRenderer>();
        t->screenSpace = true; t->text = "{item}"; t->pixelSize = 2.0f;
    }
    auto* lb = g->AddComponent<UIListBind>();
    lb->var = "items";
}
void BuildUIHotReload(GameObject* g) {
    g->AddComponent<UIHotReload>();
}
void BuildUIVisibleIf(GameObject* g) {
    g->AddComponent<UIVisibleIf>();
}

void DrawMoreUIComponentInspector(Component* comp, EditorState& ed, Component** toRemove,
                                  const UIInspectorHooks& hooks) {
    if (const Slice2Part* part = Slice2Of(comp)) {   // slice 2: a registry-driven inspector for the HUD parts and the data glue
        if (!hooks.header(part->title, comp, toRemove, true)) return;
        if (!DrawRegistryRows(comp, ed)) ImGui::TextDisabled("This part has no properties to show.");
        if (dynamic_cast<UIAbilityIcon*>(comp) && comp->gameObject && !comp->gameObject->GetComponent<UIRadialProgress>())
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "This object needs a UI Radial Progress (the cooldown ring).");
        if (dynamic_cast<UIBind*>(comp) && comp->gameObject && !static_cast<UIBind*>(comp)->HasWidget())
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "This object needs a Slider, Stepper, Toggle, Dropdown, Range Slider or Input Field.");
        if (dynamic_cast<UIListBind*>(comp) && comp->gameObject && !comp->gameObject->GetComponent<UIListView>())
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "This object needs a UI List View.");
        ImGui::TextDisabled("%s", part->help);
        return;
    }
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
