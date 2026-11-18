// UI library slice 3, app parts: the button actions that drive them with no script (a UIButton's On Click names the function).
// The parts themselves are in okay/Components/UIAppParts.hpp; this file exists because UIButton includes NativeUIActions.hpp
// and the form group needs UIButton, so the dispatch cannot sit in that header.
#include "okay/Components/NativeUIActions.hpp"
#include "okay/Components/UIAppParts.hpp"
#include "okay/Components/UIColorPicker.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

bool RunAppPartAction(GameObject* go, const std::string& fn, float arg) {
    if (!go) return false;
    if (auto* c = go->GetComponent<UIMenuBar>()) {
        if (fn == "Open")  { c->Open((int)arg); return true; }   // Amount = menu number
        if (fn == "Close") { c->Close();        return true; }
    }
    if (auto* c = go->GetComponent<UITreeView>()) {
        if (fn == "ExpandAll")   { c->ExpandAll();   return true; }
        if (fn == "CollapseAll") { c->CollapseAll(); return true; }
        if (fn == "Select")      { c->Select((int)arg); return true; }   // Amount = node number
    }
    if (auto* c = go->GetComponent<UIDataTable>()) {
        if (fn == "SortBy")    { c->ClickHeader((int)arg); return true; }   // Amount = column; the same column again flips the direction
        if (fn == "ClearSort") { c->SortBy(-1, true);      return true; }
        if (fn == "Select")    { c->Select((int)arg);      return true; }   // Amount = row number in the written rows
        if (fn == "AddSampleRow") {   // a made-up row, to try scrolling and sorting (a game adds its own from a script)
            static int n = 0;
            static const char* names[] = {"Fay", "Gus", "Hal", "Ivy", "Jo", "Kai"};
            static const char* classes[] = {"Mage", "Rogue", "Knight"};
            c->AddRow(std::string(names[n % 6]) + "|" + classes[n % 3] + "|" + std::to_string(5 + (n * 7) % 50));
            ++n;
            return true;
        }
    }
    if (auto* c = go->GetComponent<UIWizard>()) {
        if (fn == "Next")    { c->Next();    return true; }
        if (fn == "Back")    { c->Back();    return true; }
        if (fn == "Restart") { c->Restart(); return true; }
    }
    if (auto* c = go->GetComponent<UIFormGroup>()) {
        if (fn == "Submit") { c->Submit(); return true; }
        if (fn == "Reset")  { c->Reset();  return true; }
    }
    if (auto* c = go->GetComponent<UIColorPicker>()) {
        if (fn == "ToggleAdvanced") { c->SetAdvanced(!c->advanced); return true; }
    }
    return false;
}

std::vector<std::string> AppPartActionNames(GameObject* go) {
    std::vector<std::string> out;
    if (!go) return out;
    auto add = [&](std::initializer_list<const char*> fns) { for (const char* f : fns) out.push_back(f); };
    if (go->GetComponent<UIMenuBar>())     add({"Open", "Close"});
    if (go->GetComponent<UITreeView>())    add({"ExpandAll", "CollapseAll", "Select"});
    if (go->GetComponent<UIDataTable>())   add({"SortBy", "ClearSort", "Select", "AddSampleRow"});
    if (go->GetComponent<UIWizard>())      add({"Next", "Back", "Restart"});
    if (go->GetComponent<UIFormGroup>())   add({"Submit", "Reset"});
    if (go->GetComponent<UIColorPicker>()) add({"ToggleAdvanced"});
    return out;
}

} // namespace okay
