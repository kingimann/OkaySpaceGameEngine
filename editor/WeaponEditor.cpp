#include "WeaponEditor.hpp"
#include "ConsolePanel.hpp"
#include "imgui.h"
#include "okay/Combat/WeaponDefinition.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace oked {

bool g_showWeapons = false;
bool g_focused = false;

namespace {

std::vector<std::string> g_undo, g_redo;
bool g_dirty = false;
std::string g_sel;
int g_gestureFrame = -1;
std::string g_gestureSnap;

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool EndsWith(const std::string& s, const char* ext) {
    const std::size_t n = std::strlen(ext);
    return s.size() >= n && Lower(s.substr(s.size() - n)) == ext;
}

std::string BookPath(okay::editor::EditorState& ed) {
    okay::WeaponBook& book = okay::WeaponBook::Get();
    if (!book.Path().empty()) return book.Path();
    return ed.WeaponBookPath();
}

void NoteBefore() {
    const int frame = ImGui::GetFrameCount();
    if (g_gestureFrame == frame) return;
    g_gestureSnap = okay::WeaponBook::Get().Serialize();
    g_gestureFrame = frame;
}

void CommitGesture() {
    if (g_gestureFrame < 0) return;
    if (g_undo.empty() || g_undo.back() != g_gestureSnap) {
        g_undo.push_back(g_gestureSnap);
        if (g_undo.size() > 50) g_undo.erase(g_undo.begin());
    }
    g_redo.clear();
    g_dirty = true;
    g_gestureFrame = -1;
}

void PushNow() {
    const std::string now = okay::WeaponBook::Get().Serialize();
    if (g_undo.empty() || g_undo.back() != now) {
        g_undo.push_back(now);
        if (g_undo.size() > 50) g_undo.erase(g_undo.begin());
    }
    g_redo.clear();
    g_dirty = true;
}

} // namespace

bool WeaponEditorFocused() { return g_focused; }

bool WeaponEditorUndo(bool redo) {
    auto& from = redo ? g_redo : g_undo;
    auto& to = redo ? g_undo : g_redo;
    if (from.empty()) return false;
    to.push_back(okay::WeaponBook::Get().Serialize());
    okay::WeaponBook::Get().LoadText(from.back());
    from.pop_back();
    g_dirty = true;
    return true;
}

void OpenWeaponFile(const std::string& path) {
    std::string err;
    if (!okay::WeaponBook::Get().Load(path, &err)) {
        ConsoleLog("Couldn't open " + path + (err.empty() ? "" : ": " + err), 2);
        return;
    }
    g_showWeapons = true;
    g_dirty = false;
    g_undo.clear();
    g_redo.clear();
    if (!okay::WeaponBook::Get().Weapons().empty()) g_sel = okay::WeaponBook::Get().Weapons().front().id;
}

bool WeaponReferenceField(const char* label, std::string& weaponId, std::string& weaponFile, bool guns) {
    bool changed = false;
    okay::WeaponBook& book = okay::WeaponBook::Get();
    const okay::WeaponBook* fileBook = weaponFile.empty() ? nullptr : &okay::WeaponBook::Cached(weaponFile);
    std::string shown = weaponId.empty() ? std::string("(none)") : weaponId;
    if (const okay::WeaponDefinition* w = book.Find(weaponId)) shown = w->DisplayName();
    else if (fileBook) if (const okay::WeaponDefinition* w = fileBook->Find(weaponId)) shown = w->DisplayName();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(label, shown.c_str())) {
        if (ImGui::Selectable("(none)", weaponId.empty())) {
            if (!weaponId.empty()) { weaponId.clear(); changed = true; }
        }
        auto list = [&](const okay::WeaponBook& b) {
            for (const auto& w : b.Weapons()) {
                if ((w.kind == okay::WeaponKind::Gun) != guns) continue;
                const bool on = w.id == weaponId;
                if (ImGui::Selectable((w.DisplayName() + "##" + w.id).c_str(), on)) {
                    if (weaponId != w.id) { weaponId = w.id; changed = true; }
                }
            }
        };
        list(book);
        if (fileBook && fileBook != &book) list(*fileBook);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick a weapon, or drop an .okayweapon file on this slot.");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const std::string path = p->Data ? (const char*)p->Data : "";
            if (EndsWith(path, ".okayweapon")) {
                weaponFile = path;
                const okay::WeaponBook& dropped = okay::WeaponBook::Cached(path);
                const okay::WeaponDefinition* keep = dropped.Find(weaponId);
                if (!keep) {
                    weaponId.clear();
                    for (const auto& w : dropped.Weapons())
                        if ((w.kind == okay::WeaponKind::Gun) == guns) { weaponId = w.id; break; }
                }
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (!weaponFile.empty()) {
        ImGui::TextDisabled("%s", weaponFile.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Project list")) { weaponFile.clear(); changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use the project's weapon list instead of this file.");
    }
    if (!weaponId.empty() && !book.Find(weaponId) && !(fileBook && fileBook->Find(weaponId)))
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "No weapon named \"%s\" is in the list.", weaponId.c_str());
    return changed;
}

void WeaponEditorDevHooks(okay::editor::EditorState&) {
    if (!std::getenv("OKAY_UI_WEAPONS")) return;
    g_showWeapons = true;
    if (const char* file = std::getenv("OKAY_UI_WEAPONS_FILE")) {
        static bool once = false;
        if (!once) { once = true; OpenWeaponFile(file); g_showWeapons = true; }
    }
}

void DrawWeaponEditor(okay::editor::EditorState& ed) {
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Weapons", &g_showWeapons)) { g_focused = false; ImGui::End(); return; }
    g_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    okay::WeaponBook& book = okay::WeaponBook::Get();
    const std::string path = BookPath(ed);
    if (path.empty()) ImGui::TextDisabled("Open a project to keep weapons with it.");
    else ImGui::TextDisabled("%s%s", path.c_str(), g_dirty ? "  (unsaved)" : "");

    if (ImGui::Button("New melee")) {
        PushNow();
        okay::WeaponDefinition w;
        int n = 1;
        do { w.id = "weapon" + std::to_string(n++); } while (book.Find(w.id));
        w.name = "New weapon";
        w.kind = okay::WeaponKind::Melee;
        g_sel = book.Put(w).id;
    }
    ImGui::SameLine();
    if (ImGui::Button("New gun")) {
        PushNow();
        okay::WeaponDefinition w;
        int n = 1;
        do { w.id = "gun" + std::to_string(n++); } while (book.Find(w.id));
        w.name = "New gun";
        w.kind = okay::WeaponKind::Gun;
        w.swingTime = 0;
        g_sel = book.Put(w).id;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && !path.empty()) {
        if (book.Save(path)) { g_dirty = false; ConsoleLog("Saved weapons to " + path); }
        else ConsoleLog("Couldn't save " + path, 2);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload") && !path.empty()) {
        std::string err;
        if (book.Load(path, &err)) { g_dirty = false; g_undo.clear(); g_redo.clear(); }
        else ConsoleLog("Couldn't reload " + path + (err.empty() ? "" : ": " + err), 2);
    }
    for (const std::string& issue : book.Issues())
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s", issue.c_str());

    static char filter[64] = "";
    ImGui::BeginChild("##weaponlist", ImVec2(220, 0), true);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##wflt", "Search", filter, sizeof(filter));
    const std::string f = Lower(filter);
    for (const auto& w : book.Weapons()) {
        const std::string hay = Lower(w.id + " " + w.name + " " + w.item);
        if (!f.empty() && hay.find(f) == std::string::npos) continue;
        const char* kind = w.kind == okay::WeaponKind::Gun ? "gun" : "melee";
        const std::string row = w.DisplayName() + "  (" + kind + ")##" + w.id;
        if (ImGui::Selectable(row.c_str(), g_sel == w.id)) g_sel = w.id;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##weaponedit", ImVec2(0, 0), true);
    okay::WeaponDefinition* w = book.FindMutable(g_sel);
    if (!w) ImGui::TextDisabled("Select a weapon, or add one.");
    else {
        auto edited = [&](bool ch) { if (ch) CommitGesture(); };
        auto text = [&](const char* label, std::string& v, const char* tip) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", v.c_str());
            const bool ch = ImGui::InputText(label, buf, sizeof(buf));
            if (ImGui::IsItemActivated()) NoteBefore();
            if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            if (ch) { v = buf; edited(true); }
        };
        std::string id = w->id;
        char idBuf[128];
        std::snprintf(idBuf, sizeof(idBuf), "%s", id.c_str());
        if (ImGui::InputText("Id", idBuf, sizeof(idBuf))) {
            NoteBefore();
            const std::string next = idBuf;
            if (!next.empty() && next != w->id && !book.Find(next)) { w->id = next; g_sel = next; edited(true); }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The name other components store. Renaming it breaks scenes that already point here.");
        text("Name", w->name, "What people see in the weapon list.");
        int kind = (int)w->kind;
        const char* kinds[] = {"Melee", "Gun"};
        if (ImGui::Combo("Kind", &kind, kinds, 2)) { NoteBefore(); w->kind = (okay::WeaponKind)kind; edited(true); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Melee is a swing. A gun spends rounds and reloads.");

        const okay::ItemDatabase& items = okay::ItemDatabase::Get();
        std::string itemShown = w->item.empty() ? std::string("(none)") : w->item;
        if (const okay::ItemDefinition* def = items.Find(w->item)) itemShown = def->DisplayName();
        if (ImGui::BeginCombo("Item", itemShown.c_str())) {
            if (ImGui::Selectable("(none)", w->item.empty())) { NoteBefore(); w->item.clear(); edited(true); }
            for (const auto& it : items.Items()) {
                if (ImGui::Selectable((it.DisplayName() + "##item" + it.id).c_str(), it.id == w->item)) {
                    NoteBefore(); w->item = it.id; edited(true);
                }
            }
            if (!w->item.empty() && !items.Find(w->item))
                ImGui::Selectable((w->item + " (not in the item list)").c_str(), true);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The item this weapon belongs to. Equipping it selects this weapon when a fighter has not picked one itself.");

        auto num = [&](const char* label, float& v, float speed, float mn, float mx, const char* tip, const char* fmt = "%.2f") {
            const float old = v;
            const bool ch = ImGui::DragFloat(label, &v, speed, mn, mx, fmt);
            if (ImGui::IsItemActivated()) {
                const float now = v;
                v = old;
                NoteBefore();
                v = now;
            }
            if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            if (ch && v != old) edited(true);
        };
        num("Damage", w->damage, 0.1f, 0.0f, 100000.0f, "How much health one hit removes.");
        num("Range", w->range, 0.05f, 0.0f, 1000.0f, w->kind == okay::WeaponKind::Gun
            ? "How far an instant shot reaches." : "How far the swing reaches.");
        num("Knockback", w->knockback, 0.1f, 0.0f, 100000.0f, "How hard a hit pushes a body away.");
        if (w->kind == okay::WeaponKind::Melee) {
            ImGui::SeparatorText("Swing");
            num("Swing time", w->swingTime, 0.01f, 0.0f, 30.0f, "How long the swing lasts, in seconds. 0 lands the hit at once.");
            num("Arc", w->arc, 1.0f, 0.0f, 360.0f, "How wide the swing is, in degrees. 360 hits all around.", "%.0f deg");
            num("Hit window opens", w->hitStart, 0.01f, 0.0f, 30.0f, "Seconds into the swing when a hit can land.");
            num("Hit window closes", w->hitEnd, 0.01f, 0.0f, 30.0f, "Seconds into the swing when the hit closes.");
            int steps = w->combo;
            if (ImGui::DragInt("Combo hits", &steps, 0.1f, 1, 20)) { NoteBefore(); w->combo = steps; edited(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How many hits belong to one chain. 1 is a single swing.");
            num("Combo window", w->comboWindow, 0.01f, 0.0f, 30.0f, "Seconds after a swing to continue the chain.");
            num("Combo bonus", w->comboScale, 0.01f, 0.0f, 10.0f, "Extra damage fraction for each hit after the first.");
            bool box = w->useHitbox;
            if (ImGui::Checkbox("Use a hit box", &box)) { NoteBefore(); w->useHitbox = box; edited(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A box in front of the fighter, instead of the cone.");
            if (w->useHitbox) {
                num("Hit box width", w->hitboxWidth, 0.05f, 0.0f, 100.0f, "How wide the box is.");
                num("Hit box height", w->hitboxHeight, 0.05f, 0.0f, 100.0f, "How tall the box is.");
                num("Hit box depth", w->hitboxDepth, 0.05f, 0.0f, 100.0f, "How far in front the box reaches.");
            }
        } else {
            ImGui::SeparatorText("Gun");
            num("Shots per second", w->fireRate, 0.05f, 0.0f, 100.0f, "How fast the gun can fire.");
            int mag = w->magazine;
            if (ImGui::DragInt("Rounds in the gun", &mag, 0.2f, 0, 100000)) { NoteBefore(); w->magazine = mag; edited(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How many rounds the gun holds. 0 means it never reloads.");
            int spare = w->ammo;
            if (ImGui::DragInt("Spare rounds", &spare, 0.2f, -1, 1000000)) { NoteBefore(); w->ammo = spare; edited(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rounds waiting for the next reload. -1 means they never run out.");
            num("Reload time", w->reload, 0.01f, 0.0f, 60.0f, "Seconds to refill the gun from the spare rounds.");
            num("Spread", w->spread, 0.05f, 0.0f, 180.0f, "Aim wobble, in degrees. The top-down shooter uses this.");
            bool scan = w->hitscan;
            if (ImGui::Checkbox("Instant shot", &scan)) { NoteBefore(); w->hitscan = scan; edited(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: the shot lands at once. Off: it spawns a projectile.");
            if (!w->hitscan) {
                text("Projectile", w->projectile, "A prefab spawned for each shot. Empty makes a plain projectile.");
                num("Projectile speed", w->projectileSpeed, 0.1f, 0.0f, 1000.0f, "How fast that projectile flies.");
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Duplicate")) {
            PushNow();
            okay::WeaponDefinition copy = *w;
            int n = 2;
            do { copy.id = w->id + "_" + std::to_string(n++); } while (book.Find(copy.id));
            copy.name = w->DisplayName() + " copy";
            g_sel = book.Put(copy).id;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            PushNow();
            book.Remove(w->id);
            g_sel.clear();
            w = nullptr;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace oked
