#pragma once
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"
#include "okay/Components/SurvivalSystems.hpp"
#include "okay/Components/Crafting.hpp"   // Crafting has no NativeUIActions dependency
#include "okay/Components/UIModal.hpp"
#include "okay/Components/UIDialogueBox.hpp"
#include "okay/Components/UIDamageNumbers.hpp"
#include "okay/Components/UIGameHud.hpp"
#include <string>
#include <vector>

namespace okay {

/// Slice 3 app parts (UIAppParts.cpp): run `fn` on the part on `go`; the function names this part offers.
bool RunAppPartAction(GameObject* go, const std::string& fn, float arg);
std::vector<std::string> AppPartActionNames(GameObject* go);

/// Bridge a Button On Click (or any UI action) to a **native** component
/// method on `go`, passing `arg` as the amount. Returns true if a native component
/// handled the name; UIButton falls back to a script's CallEvent when this is false,
/// so custom script functions still work and the fixed native verbs take priority.
///
/// This is what lets no-code survival buttons work: set Target = Player,
/// Function = Drink, Arg = 25 and the click calls SurvivalStats::Drink(25) directly.
inline bool InvokeNativeUIAction(GameObject* go, const std::string& fn, float arg) {
    if (!go) return false;
    bool on = arg != 0.0f;

    // Any object: a button can hide, show or flip a window / panel / page with no script.
    if (fn == "HideObject")   { go->active = false; return true; }
    if (fn == "ShowObject")   { go->active = true;  return true; }
    if (fn == "ToggleObject") { go->active = !go->active; return true; }

    if (auto* s = go->GetComponent<SurvivalStats>()) {
        if (fn == "Damage")       { s->Damage(arg);   return true; }
        if (fn == "Heal")         { s->Heal(arg);     return true; }
        if (fn == "Eat")          { s->Eat(arg);      return true; }
        if (fn == "Drink")        { s->Drink(arg);    return true; }
        if (fn == "Breathe")      { s->Breathe(arg);  return true; }
        if (fn == "Warm")         { s->Warm(arg);     return true; }
        if (fn == "AddArmor")     { s->AddArmor(arg); return true; }
        if (fn == "Revive")       { s->Revive();      return true; }
        if (fn == "SetSprinting") { s->SetSprinting(on); return true; }
        if (fn == "SetSubmerged") { s->SetSubmerged(on); return true; }
        if (fn == "SetCold")      { s->SetCold(on);   return true; }
    }
    if (auto* c = go->GetComponent<HealthStat>()) {
        if (fn == "Damage")   { c->Damage(arg);   return true; }
        if (fn == "Heal")     { c->Heal(arg);     return true; }
        if (fn == "AddArmor") { c->AddArmor(arg); return true; }
        if (fn == "Revive")   { c->Revive();      return true; }
    }
    if (auto* c = go->GetComponent<HungerStat>()) {
        if (fn == "Eat")          { c->Eat(arg);        return true; }
        if (fn == "SetSprinting") { c->SetSprinting(on); return true; }
    }
    if (auto* c = go->GetComponent<ThirstStat>()) {
        if (fn == "Drink")        { c->Drink(arg);       return true; }
        if (fn == "SetSprinting") { c->SetSprinting(on);  return true; }
    }
    if (auto* c = go->GetComponent<StaminaStat>()) {
        if (fn == "TryJump")      { c->TryJump();         return true; }
        if (fn == "SetSprinting") { c->SetSprinting(on);  return true; }
    }
    if (auto* c = go->GetComponent<OxygenStat>()) {
        if (fn == "Breathe")      { c->Breathe(arg);      return true; }
        if (fn == "SetSubmerged") { c->SetSubmerged(on);  return true; }
    }
    if (auto* c = go->GetComponent<TemperatureStat>()) {
        if (fn == "Warm")        { c->Warm(arg);       return true; }
        if (fn == "SetCold")     { c->SetCold(on);     return true; }
        if (fn == "SetNearFire") { c->SetNearFire(on); return true; }
    }
    if (auto* c = go->GetComponent<SleepStat>()) {
        if (fn == "Rest")       { c->Rest(arg);      return true; }
        if (fn == "SetResting") { c->SetResting(on); return true; }
    }
    if (auto* c = go->GetComponent<SanityStat>()) {
        if (fn == "Restore")     { c->Restore(arg);     return true; }
        if (fn == "SetInDanger") { c->SetInDanger(on);  return true; }
    }
    if (auto* c = go->GetComponent<RadiationStat>()) {
        if (fn == "AddRadiation")   { c->AddRadiation(arg);   return true; }
        if (fn == "TakeAntiRad")    { c->TakeAntiRad(arg);    return true; }
        if (fn == "SetInRadiation") { c->SetInRadiation(on);  return true; }
    }
    if (auto* c = go->GetComponent<BleedingStat>()) {
        if (fn == "Wound")   { c->Wound(arg);   return true; }
        if (fn == "Bandage") { c->Bandage();    return true; }
    }
    if (auto* c = go->GetComponent<PoisonStat>()) {
        if (fn == "Poison")  { c->Poison(arg);  return true; }
        if (fn == "Cure")    { c->Cure(arg);    return true; }
        if (fn == "CureAll") { c->CureAll();    return true; }
    }
    if (auto* c = go->GetComponent<WetnessStat>()) {
        if (fn == "AddWetness") { c->AddWetness(arg); return true; }
        if (fn == "DryOff")     { c->DryOff(arg);     return true; }
        if (fn == "SetInWater") { c->SetInWater(on);  return true; }
    }
    if (auto* c = go->GetComponent<CarryWeightStat>()) {
        if (fn == "AddLoad")    { c->AddLoad(arg);    return true; }
        if (fn == "RemoveLoad") { c->RemoveLoad(arg); return true; }
        if (fn == "SetLoad")    { c->SetLoad(arg);    return true; }
    }
    if (auto* c = go->GetComponent<SurvivalSave>()) {
        if (fn == "Save") { c->Save(); return true; }
        if (fn == "Load") { c->Load(); return true; }
    }
    if (auto* c = go->GetComponent<Crafting>()) {
        if (fn == "Craft") { c->CraftIndex((int)arg); return true; }   // Amount = recipe index
    }
    if (auto* c = go->GetComponent<UIModal>()) {   // a dialog's own buttons open / close it
        if (fn == "Open")  { c->Open();  return true; }
        if (fn == "Close") { c->Close(); return true; }
    }
    if (auto* c = go->GetComponent<UIDialogueBox>()) {   // a dialogue box's own buttons
        if (fn == "Choose")  { c->Choose((int)arg); return true; }   // Amount = choice index
        if (fn == "Advance") { c->Advance();        return true; }
        if (fn == "Restart") { return c->Restart(); }              // show the last line again (a Talk button)
        if (fn == "Close")   { c->Close();          return true; }
    }
    if (auto* c = go->GetComponent<UIDamageNumbers>()) {   // floating combat text from a button, no script
        if (c->RunAction(fn, arg)) return true;
    }
    // UI library slice 2: the game HUD parts, from a button with no script.
    if (auto* c = go->GetComponent<UIRadialMenu>()) {
        if (fn == "Open")   { c->Open();         return true; }
        if (fn == "Close")  { c->Close(false);   return true; }
        if (fn == "Choose") { c->Choose((int)arg); return true; }   // Amount = wedge number
    }
    if (auto* c = go->GetComponent<UIKillFeed>()) {
        if (fn == "AddSampleKill") {   // a made-up line, to try the feed (a game pushes its own from a script)
            static int n = 0;
            static const char* killers[] = {"Player", "Raider", "Wolf", "Sentry"};
            static const char* guns[] = {"Rifle", "Axe", "Bow", ""};
            c->Push(killers[n % 4], killers[(n + 1 + (n / 4) % 3) % 4], guns[n % 4]);   // the victim is never the killer
            ++n;
            return true;
        }
        if (fn == "Clear") { c->Clear(); return true; }
    }
    if (auto* c = go->GetComponent<UIAbilityIcon>()) {
        if (fn == "Use")   { c->Use();   return true; }
        if (fn == "Reset") { c->Reset(); return true; }
    }
    if (auto* c = go->GetComponent<UIAmmoCounter>()) {   // these change the typed numbers (a counter that reads a weapon or variables follows those)
        if (fn == "Shoot")  { if (c->current > 0) --c->current; return true; }
        if (fn == "Reload") {
            const int need = c->magazine - c->current;
            const int take = c->reserve < 0 ? need : (need < c->reserve ? need : c->reserve);
            c->current += take;
            if (c->reserve >= 0) c->reserve -= take;
            return true;
        }
    }
    // UI library slice 3: the app parts (UIAppParts.cpp; they need UIButton, which includes this header, so they live in a .cpp).
    if (RunAppPartAction(go, fn, arg)) return true;
    // Consumables::UseItem is dispatched by UIButton (Consumables depends on this
    // header, so it can't be referenced here without a cycle).
    return false;
}

/// The native methods callable on `go` right now, for the editor's On Click function
/// picker. Empty when `go` has none of the native gameplay components.
inline std::vector<std::string> NativeUIActionNames(GameObject* go) {
    std::vector<std::string> out;
    if (!go) return out;
    auto add = [&](std::initializer_list<const char*> fns) {
        for (const char* f : fns) out.push_back(f);
    };
    if (go->GetComponent<SurvivalStats>())
        add({"Eat", "Drink", "Heal", "Damage", "Breathe", "Warm", "AddArmor",
             "Revive", "SetSprinting", "SetSubmerged", "SetCold"});
    if (go->GetComponent<HealthStat>())      add({"Heal", "Damage", "AddArmor", "Revive"});
    if (go->GetComponent<HungerStat>())      add({"Eat", "SetSprinting"});
    if (go->GetComponent<ThirstStat>())      add({"Drink", "SetSprinting"});
    if (go->GetComponent<StaminaStat>())     add({"TryJump", "SetSprinting"});
    if (go->GetComponent<OxygenStat>())      add({"Breathe", "SetSubmerged"});
    if (go->GetComponent<TemperatureStat>()) add({"Warm", "SetCold", "SetNearFire"});
    if (go->GetComponent<SleepStat>())       add({"Rest", "SetResting"});
    if (go->GetComponent<SanityStat>())      add({"Restore", "SetInDanger"});
    if (go->GetComponent<RadiationStat>())   add({"AddRadiation", "TakeAntiRad", "SetInRadiation"});
    if (go->GetComponent<BleedingStat>())    add({"Wound", "Bandage"});
    if (go->GetComponent<PoisonStat>())      add({"Poison", "Cure", "CureAll"});
    if (go->GetComponent<WetnessStat>())     add({"AddWetness", "DryOff", "SetInWater"});
    if (go->GetComponent<CarryWeightStat>()) add({"AddLoad", "RemoveLoad", "SetLoad"});
    if (go->GetComponent<SurvivalSave>())    add({"Save", "Load"});
    if (go->GetComponent<Crafting>())        add({"Craft"});
    if (go->GetComponent<UIModal>())         add({"Open", "Close"});
    if (go->GetComponent<UIDialogueBox>())   add({"Choose", "Advance", "Restart", "Close"});
    if (go->GetComponent<UIDamageNumbers>()) add({"Hit", "Crit", "Heal", "Miss", "Info"});
    if (go->GetComponent<UIRadialMenu>())    add({"Open", "Close", "Choose"});
    if (go->GetComponent<UIKillFeed>())      add({"AddSampleKill", "Clear"});
    if (go->GetComponent<UIAbilityIcon>())   add({"Use", "Reset"});
    if (go->GetComponent<UIAmmoCounter>())   add({"Shoot", "Reload"});
    for (const std::string& n : AppPartActionNames(go)) out.push_back(n);
    return out;
}

} // namespace okay
