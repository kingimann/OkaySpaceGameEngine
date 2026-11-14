// Items, loot tables and crafting recipes, in the files the game already loads.
// One window, three tabs: list, search, add, duplicate, delete, edit. Loot entries
// are weighted with a quantity range and a roll preview that follows the same Roll
// the game uses. Item references are a dropdown of defined items; a name that is
// not one is called out in plain English. Ctrl+Z / Ctrl+Y while the window is
// focused undo these edits (the scene's undo is left alone).
#include "ContentEditors.hpp"
#include "ConsolePanel.hpp"
#include "EditorWidgets.hpp"
#include "MainShared.hpp"
#include "imgui.h"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Inventory/LootTable.hpp"
#include "okay/Inventory/Recipe.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace okay;
using namespace okay::editor;

namespace oked {

bool g_showItemDatabase = false;

namespace {
namespace fs = std::filesystem;

enum class Ask { None, Item, LootFile, LootEntry, RecipeFile, Recipe };

struct Snap {
    int tab = 0;
    std::string path;
    std::string text;
    bool present = true;
    std::string reselect;
};

EditorState* s_ed = nullptr;
bool s_focused = false;
bool s_applying = false;
int s_tab = 0;
int s_forceTab = -1;
int s_selectTab = -1;
std::string s_pending;
bool s_needScan = true;
double s_scanned = -1.0;
std::string s_status;
bool s_dirty = false;

std::string s_itemSel;
char s_itemFilter[64] = "";

std::vector<std::string> s_lootFiles, s_recipeFiles;
std::string s_lootPath;
LootTable s_loot;
bool s_lootLoaded = false;
std::string s_lootErr;
char s_lootFilter[64] = "";
int s_rollNonce = 1;
std::string s_previewKey;
std::vector<Inventory::Stack> s_once;
std::map<std::string, int> s_sampleQty;
int s_sampleRolls = 0;

std::string s_bookPath;
RecipeBook s_book;
bool s_bookLoaded = false;
std::string s_bookErr;
int s_recipe = 0;
char s_bookFilter[64] = "";
char s_recipeFilter[64] = "";

std::vector<Snap> s_undo, s_redo;
bool s_editOpen = false;
Snap s_editBefore;

Ask s_ask = Ask::None;
int s_askIndex = -1;
std::string s_askText;

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string Trim(std::string t) {
    std::size_t a = 0, b = t.size();
    while (a < b && std::isspace((unsigned char)t[a])) ++a;
    while (b > a && std::isspace((unsigned char)t[b - 1])) --b;
    return t.substr(a, b - a);
}
bool WriteFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return (bool)f;
}
std::string Canon(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path c = fs::weakly_canonical(fs::path(path), ec);
    return ec ? path : c.string();
}
std::string Root(const EditorState& ed) {
    if (std::getenv("OKAY_UI_CONTENT"))
        if (const char* d = std::getenv("OKAY_UI_CONTENT_DIR"); d && d[0]) return Canon(d);
    return ed.projectDir();
}
fs::path SaveDir(const EditorState& ed) {
    std::string root = Root(ed);
    if (root.empty()) return {};
    fs::path assets = fs::path(root) / "Assets";
    std::error_code ec;
    if (fs::is_directory(assets, ec)) return assets;
    return root;
}
std::string ShortPath(const EditorState& ed, const std::string& path) {
    if (path.empty()) return {};
    std::string root = Root(ed);
    std::error_code ec;
    if (!root.empty()) {
        std::string rel = fs::relative(path, root, ec).generic_string();
        if (!ec && rel.rfind("..", 0) != 0) return rel;
    }
    return fs::path(path).filename().string();
}
std::string UniquePath(const fs::path& dir, const std::string& stem, const std::string& ext) {
    fs::path p = dir / (stem + ext);
    int n = 2;
    std::error_code ec;
    while (fs::exists(p, ec)) p = dir / (stem + " " + std::to_string(n++) + ext);
    return p.string();
}
std::string FirstItem() {
    for (const auto& d : ItemDatabase::Get().Items())
        if (!d.loose && !d.id.empty()) return d.id;
    return "item";
}
SDL_Texture* Icon(const std::string& id) {
    const std::string path = ItemIconPath("", id);
    return path.empty() ? nullptr : GetThumb(path);
}
std::string ItemLabel(const std::string& id) {
    const ItemDefinition* d = ItemDatabase::Get().Find(id);
    if (d && !d->loose) return d->DisplayName();
    return id.empty() ? std::string("(choose an item)") : id;
}

std::string ItemsPath(const EditorState& ed) {
    if (!ItemDatabase::Get().Path().empty()) return ItemDatabase::Get().Path();
    if (!ed.ItemDatabasePath().empty()) return ed.ItemDatabasePath();
    std::string root = Root(ed);
    return root.empty() ? std::string() : root + "/items.okayitems";
}

bool Same(const Snap& a, const Snap& b) {
    return a.tab == b.tab && a.path == b.path && a.text == b.text && a.present == b.present;
}
Snap CaptureTab(const EditorState& ed, int tab) {
    Snap s;
    s.tab = tab;
    if (tab == 0) {
        s.path = ItemsPath(ed);
        s.text = ItemDatabase::Get().Serialize();
        s.present = true;
        s.reselect = s_itemSel;
    } else if (tab == 1) {
        s.path = s_lootPath;
        s.text = s_lootPath.empty() ? std::string() : s_loot.Serialize();
        s.present = !s_lootPath.empty();
        s.reselect = s_lootPath;
    } else {
        s.path = s_bookPath;
        s.text = s_bookPath.empty() ? std::string() : s_book.Serialize();
        s.present = !s_bookPath.empty();
        s.reselect = s_bookPath;
    }
    return s;
}
void PushSnap(const Snap& before, const Snap& after) {
    if (s_applying || Same(before, after)) return;
    s_undo.push_back(before);
    if (s_undo.size() > 80) s_undo.erase(s_undo.begin());
    s_redo.clear();
    s_dirty = true;
}
bool SaveTab(EditorState& ed, int tab, bool announce) {
    if (tab == 0) {
        const std::string path = ItemsPath(ed);
        if (path.empty()) {
            s_status = "Open a project to keep item definitions with it.";
            return false;
        }
        if (!ItemDatabase::Get().Save(path)) {
            s_status = "Couldn't write " + ShortPath(ed, path);
            if (announce) ConsoleLog(s_status, 2);
            return false;
        }
        if (ItemDatabase::Get().Path().empty()) {
            std::string err;
            ItemDatabase::Get().Load(path, &err);
        }
        s_status = "Saved " + ShortPath(ed, path);
        s_dirty = false;
        if (announce) ConsoleLog("Saved item definitions to " + path);
        return true;
    }
    if (tab == 1) {
        if (s_lootPath.empty()) { s_status = "Open a project to add a loot table."; return false; }
        if (!s_loot.Save(s_lootPath)) {
            s_status = "Couldn't write " + ShortPath(ed, s_lootPath);
            if (announce) ConsoleLog(s_status, 2);
            return false;
        }
        LootTable::Forget(s_lootPath);
        s_status = "Saved " + ShortPath(ed, s_lootPath);
        s_dirty = false;
        if (announce) ConsoleLog("Saved loot table " + s_lootPath);
        return true;
    }
    if (s_bookPath.empty()) { s_status = "Open a project to add a recipe book."; return false; }
    if (!s_book.Save(s_bookPath)) {
        s_status = "Couldn't write " + ShortPath(ed, s_bookPath);
        if (announce) ConsoleLog(s_status, 2);
        return false;
    }
    RecipeBook::Forget(s_bookPath);
    s_status = "Saved " + ShortPath(ed, s_bookPath);
    s_dirty = false;
    if (announce) ConsoleLog("Saved recipe book " + s_bookPath);
    return true;
}
void CloseEdit() {
    if (!s_editOpen) return;
    s_editOpen = false;
    if (!s_ed) return;
    Snap before = s_editBefore;
    Snap now = CaptureTab(*s_ed, before.tab);
    if (Same(before, now)) return;
    PushSnap(before, now);
    SaveTab(*s_ed, before.tab, false);
}
void NoteEdit() {
    if (s_editOpen || !s_ed || s_applying) return;
    s_editBefore = CaptureTab(*s_ed, s_tab);
    s_editOpen = true;
    s_dirty = true;
}
void CommitTab(EditorState& ed, int tab, const Snap& before) {
    if (s_editOpen) CloseEdit();
    PushSnap(before, CaptureTab(ed, tab));
    SaveTab(ed, tab, false);
}

void Scan(const EditorState& ed, bool force) {
    const double now = ImGui::GetTime();
    if (!force && !s_needScan && s_scanned >= 0.0 && now - s_scanned < 2.0) return;
    s_scanned = now;
    s_needScan = false;
    s_lootFiles.clear();
    s_recipeFiles.clear();
    const std::string root = Root(ed);
    if (root.empty()) return;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_directory(ec)) {
            const std::string name = it->path().filename().string();
            if (name == ".git" || name == ".okay" || name == "build" || name == "node_modules")
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = Lower(it->path().extension().string());
        const std::string path = Canon(it->path().string());
        if (ext == ".okayloot") s_lootFiles.push_back(path);
        else if (ext == ".okayrecipes") s_recipeFiles.push_back(path);
    }
    std::sort(s_lootFiles.begin(), s_lootFiles.end());
    std::sort(s_recipeFiles.begin(), s_recipeFiles.end());
    s_lootFiles.erase(std::unique(s_lootFiles.begin(), s_lootFiles.end()), s_lootFiles.end());
    s_recipeFiles.erase(std::unique(s_recipeFiles.begin(), s_recipeFiles.end()), s_recipeFiles.end());
}

void Apply(EditorState& ed, const Snap& s) {
    s_applying = true;
    s_forceTab = s.tab;
    std::string err;
    if (s.tab == 0) {
        ItemDatabase::Get().LoadText(s.text.empty() ? "okayitems 1\n" : s.text, &err);
        s_itemSel = s.reselect;
        if (!ItemsPath(ed).empty()) ItemDatabase::Get().Save(ItemsPath(ed));
        s_dirty = false;
    } else if (!s.present) {
        std::error_code ec;
        if (!s.path.empty()) fs::remove(s.path, ec);
        if (s.tab == 1) {
            LootTable::Forget(s.path);
            s_loot = {};
            s_lootLoaded = false;
            s_lootPath = s.reselect;
        } else {
            RecipeBook::Forget(s.path);
            s_book = {};
            s_bookLoaded = false;
            s_bookPath = s.reselect;
        }
        s_needScan = true;
        s_dirty = false;
    } else if (s.tab == 1) {
        WriteFile(s.path, s.text);
        LootTable::Forget(s.path);
        s_lootPath = s.path;
        s_loot.LoadText(s.text, &err);
        s_lootLoaded = true;
        s_lootErr = err;
        s_needScan = true;
        s_previewKey.clear();
        s_dirty = false;
    } else {
        WriteFile(s.path, s.text);
        RecipeBook::Forget(s.path);
        s_bookPath = s.path;
        s_book.LoadText(s.text, &err);
        s_bookLoaded = true;
        s_bookErr = err;
        s_needScan = true;
        if (s_recipe >= (int)s_book.recipes.size()) s_recipe = (int)s_book.recipes.size() - 1;
        s_dirty = false;
    }
    s_status = "Undone";
    s_applying = false;
}

void DrawIssues(const std::vector<std::string>& issues) {
    if (issues.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.58f, 0.22f, 1.0f));
    for (const auto& m : issues) ImGui::TextWrapped("%s", m.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
}
void AfterItem() {
    if (ImGui::IsItemDeactivated()) CloseEdit();
}
bool TouchString(const char* label, std::string& v, const char* tip) {
    char b[256];
    std::snprintf(b, sizeof(b), "%s", v.c_str());
    const bool ch = ImGui::InputText(label, b, sizeof(b));
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    if (ch && std::string(b) != v) { NoteEdit(); v = b; }
    AfterItem();
    return ch;
}
bool TouchFloat(const char* label, float& v, float speed, float mn, float mx, const char* fmt) {
    float n = v;
    ImGui::SetNextItemWidth(160.0f);
    const bool ch = ImGui::DragFloat(label, &n, speed, mn, mx, fmt);
    if (ch && n != v) { NoteEdit(); v = n; }
    AfterItem();
    return ch;
}
bool TouchInt(const char* label, int& v, float speed, int mn, int mx) {
    int n = v;
    ImGui::SetNextItemWidth(140.0f);
    const bool ch = ImGui::DragInt(label, &n, speed, mn, mx);
    if (ch && n != v) { NoteEdit(); v = n; }
    AfterItem();
    return ch;
}

bool ItemDropdown(const char* label, std::string& id) {
    ItemDatabase& db = ItemDatabase::Get();
    const bool missing = !id.empty() && !db.IsDefined(id);
    if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.25f, 1.0f));
    ImGui::SetNextItemWidth(-1.0f);
    const bool open = ImGui::BeginCombo(label, ItemLabel(id).c_str(), ImGuiComboFlags_HeightLarge);
    if (missing) ImGui::PopStyleColor();
    if (missing && ImGui::IsItemHovered())
        ImGui::SetTooltip("\"%s\" isn't in the Item Database, so this reference goes nowhere.", id.c_str());
    bool changed = false;
    if (open) {
        static char filter[64] = "";
        if (ImGui::IsWindowAppearing()) { filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##itemsearch", "Search items", filter, sizeof(filter));
        const std::string f = Lower(filter);
        bool any = false;
        for (const auto& d : db.Items()) {
            if (d.loose || d.id.empty()) continue;
            if (!f.empty() && Lower(d.id + " " + d.name + " " + d.category).find(f) == std::string::npos) continue;
            any = true;
            if (SDL_Texture* t = Icon(d.id)) { ImGui::Image((ImTextureID)t, ImVec2(18, 18)); ImGui::SameLine(); }
            const std::string row = d.DisplayName() + (d.name.empty() || d.name == d.id ? "" : "    " + d.id);
            if (ImGui::Selectable((row + "##" + d.id).c_str(), d.id == id)) {
                if (d.id != id) {
                    if (s_editOpen) CloseEdit();
                    NoteEdit();
                    id = d.id;
                    CloseEdit();
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered() && !d.description.empty()) ImGui::SetTooltip("%s", d.description.c_str());
        }
        if (!any) ImGui::TextDisabled("No items match. Add some on the Items tab.");
        ImGui::EndCombo();
    }
    return changed;
}

bool Match(const char* filter, const std::string& hay) {
    if (!filter[0]) return true;
    return Lower(hay).find(Lower(filter)) != std::string::npos;
}

void DrawItemTab(EditorState& ed) {
    ItemDatabase& db = ItemDatabase::Get();
    const std::string path = ItemsPath(ed);
    ImGui::TextDisabled("%s%s", path.empty() ? "No project open." : path.c_str(), s_dirty && s_tab == 0 ? "    unsaved" : "");
    if (ImGui::Button("+ New Item")) ContentAddItem(ed);
    ImGui::SameLine();
    ImGui::BeginDisabled(s_itemSel.empty() || !db.Find(s_itemSel));
    if (ImGui::Button("Duplicate")) {
        if (ItemDefinition* src = db.FindMutable(s_itemSel)) {
            Snap before = CaptureTab(ed, 0);
            ItemDefinition copy = *src;
            copy.loose = false;
            const std::string base = src->id + "_copy";
            copy.id = base;
            for (int n = 2; db.Find(copy.id); ++n) copy.id = base + std::to_string(n);
            if (!copy.name.empty()) copy.name += " Copy";
            s_itemSel = db.Put(copy).id;
            CommitTab(ed, 0, before);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s_itemSel.empty() || !db.Find(s_itemSel));
    if (ImGui::Button("Delete")) {
        const ItemDefinition* d = db.Find(s_itemSel);
        s_ask = Ask::Item;
        s_askText = "Delete \"" + (d ? d->DisplayName() : s_itemSel) + "\"? Inventories that name it keep the id, and Ctrl+Z brings the definition back.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save") && s_ed) SaveTab(ed, 0, true);
    ImGui::SameLine();
    ImGui::BeginDisabled(path.empty());
    if (ImGui::Button("Reload")) {
        Snap before = CaptureTab(ed, 0);
        std::string err;
        if (db.Load(path, &err)) { CommitTab(ed, 0, before); s_status = "Reloaded " + ShortPath(ed, path); }
        else s_status = err.empty() ? "Couldn't read " + path : err;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Read the file again. Ctrl+Z brings these edits back.");

    DrawIssues(db.Issues());

    ImGui::BeginChild("##itemlist", ImVec2(300, 0), true);
    SearchField("##itemfilter", s_itemFilter, sizeof(s_itemFilter), "Search items", -1.0f);
    if (s_itemSel.empty())
        for (const auto& d : db.Items()) if (!d.loose && !d.id.empty()) { s_itemSel = d.id; break; }
    for (const auto& d : db.Items()) {
        if (!Match(s_itemFilter, d.id + " " + d.name + " " + d.category)) continue;
        std::string row = d.DisplayName();
        if (d.loose) row += "  (not defined)";
        else if (!d.name.empty() && d.name != d.id) row += "    " + d.id;
        if (SDL_Texture* t = Icon(d.id)) { ImGui::Image((ImTextureID)t, ImVec2(18, 18)); ImGui::SameLine(); }
        if (ImGui::Selectable((row + "##" + d.id).c_str(), s_itemSel == d.id)) s_itemSel = d.id;
    }
    if (db.Items().empty()) ImGui::TextDisabled("No items yet.");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##itemedit", ImVec2(0, 0), true);
    ItemDefinition* d = db.FindMutable(s_itemSel);
    if (!d) ImGui::TextDisabled("Select an item, or add one.");
    else {
        char idb[128];
        std::snprintf(idb, sizeof(idb), "%s", d->id.c_str());
        if (ImGui::InputText("Id", idb, sizeof(idb))) {
            const std::string id = idb;
            if (id != d->id && !id.empty() && !db.Find(id)) {
                NoteEdit();
                d->id = id;
                s_itemSel = id;
            }
        }
        AfterItem();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The stable key inventories store. Renaming it breaks saved scenes; change the Name instead.");
        TouchString("Name", d->name, "What players see. Rename this freely.");
        TouchString("Description", d->description, nullptr);
        TouchString("Icon", d->icon, "Image path. Blank uses the icon folder plus the id.");
        if (SDL_Texture* t = Icon(d->id)) ImGui::Image((ImTextureID)t, ImVec2(56, 56));
        TouchInt("Stack Size", d->maxStack, 0.2f, 0, 100000);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Most in one stack. 0 uses only the container's own limit.");
        TouchFloat("Weight", d->weight, 0.05f, 0.0f, 10000.0f, "%.2f");
        int sz[2] = {d->gridW, d->gridH};
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::DragInt2("Grid Size", sz, 0.1f, 1, 12)) {
            if (sz[0] != d->gridW || sz[1] != d->gridH) { NoteEdit(); d->gridW = sz[0]; d->gridH = sz[1]; }
        }
        AfterItem();
        TouchInt("Rarity Tier", d->rarity, 0.1f, 0, 10);
        bool tint = d->hasRarityColor;
        if (ImGui::Checkbox("Rarity Colour", &tint) && tint != d->hasRarityColor) { NoteEdit(); d->hasRarityColor = tint; }
        AfterItem();
        if (d->hasRarityColor) {
            ImGui::SameLine();
            float c[4] = {d->rarityColor.r, d->rarityColor.g, d->rarityColor.b, d->rarityColor.a};
            if (ImGui::ColorEdit4("##rarity", c, ImGuiColorEditFlags_NoInputs)) {
                NoteEdit();
                d->rarityColor = Color(c[0], c[1], c[2], c[3]);
            }
            AfterItem();
        }
        TouchString("Category", d->category, "Food, Weapon, Material, and whatever else you sort by.");
        auto editList = [&](const char* label, std::vector<std::string>& list, const char* tip) {
            std::string shown;
            for (const auto& t : list) shown += (shown.empty() ? "" : ", ") + t;
            char b[256];
            std::snprintf(b, sizeof(b), "%s", shown.c_str());
            const bool ch = ImGui::InputText(label, b, sizeof(b));
            if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            if (ch && std::string(b) != shown) {
                NoteEdit();
                list.clear();
                std::stringstream ts(b);
                std::string t;
                while (std::getline(ts, t, ',')) { t = Trim(t); if (!t.empty()) list.push_back(t); }
            }
            AfterItem();
        };
        editList("Tags", d->tags, "Comma-separated.");
        editList("Old Names", d->legacyNames, "Names from older scenes that should load as this item.");
        int eq = (int)d->equip;
        const char* slots[] = {"None", "Head", "Chest", "Legs", "Feet", "Hands", "Back", "Main Hand", "Off Hand"};
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("Equip Slot", &eq, slots, IM_ARRAYSIZE(slots)) && eq != (int)d->equip) {
            NoteEdit();
            d->equip = (EquipSlot)eq;
        }
        AfterItem();
        TouchString("Use Action", d->useAction, "What using it does: Eat, Drink, Heal. Blank means it is not usable.");
        if (!d->useAction.empty()) TouchFloat("Use Amount", d->useAmount, 0.5f, 0.0f, 10000.0f, "%.1f");
        TouchString("Drop Prefab", d->worldPrefab, "Prefab spawned when the item is dropped in the world.");
        if (ImGui::TreeNode("Custom Fields")) {
            for (int i = 0; i < (int)d->custom.size(); ++i) {
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(140);
                TouchString("##k", d->custom[i].first, nullptr);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180);
                TouchString("##v", d->custom[i].second, nullptr);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    Snap before = CaptureTab(ed, 0);
                    d->custom.erase(d->custom.begin() + i);
                    CommitTab(ed, 0, before);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Field")) {
                Snap before = CaptureTab(ed, 0);
                d->custom.push_back({"key", "value"});
                CommitTab(ed, 0, before);
            }
            ImGui::TreePop();
        }
        if (d->loose) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.28f, 1.0f), "Not defined yet. It is left out of the file until you define it.");
            if (ImGui::Button("Define it")) {
                Snap before = CaptureTab(ed, 0);
                d->loose = false;
                CommitTab(ed, 0, before);
            }
        }
    }
    ImGui::EndChild();
}

void RebuildPreview() {
    const std::string key = s_loot.Serialize() + "#" + std::to_string(s_rollNonce);
    if (key == s_previewKey) return;
    s_previewKey = key;
    s_once.clear();
    s_sampleQty.clear();
    s_sampleRolls = 0;
    std::uint64_t seed = 1469598103934665603ull;
    for (unsigned char c : key) seed = (seed ^ c) * 1099511628211ull;
    Random rng(seed);
    s_once = s_loot.Roll(rng);
    s_sampleRolls = 400;
    for (int i = 0; i < s_sampleRolls; ++i)
        for (const auto& st : s_loot.Roll(rng)) s_sampleQty[st.item] += st.count;
}

void DrawLootTab(EditorState& ed) {
    Scan(ed, false);
    if (!s_pending.empty() && s_tab == 1) {
        const std::string want = Canon(s_pending);
        if (std::find(s_lootFiles.begin(), s_lootFiles.end(), want) != s_lootFiles.end()) {
            s_lootPath = want;
            s_lootLoaded = false;
        }
        s_pending.clear();
    }
    if (s_lootPath.empty() && !s_lootFiles.empty()) { s_lootPath = s_lootFiles.front(); s_lootLoaded = false; }
    if (!s_lootLoaded) {
        s_loot = {};
        s_lootErr.clear();
        s_lootLoaded = true;
        s_previewKey.clear();
        if (!s_lootPath.empty() && !s_loot.Load(s_lootPath, &s_lootErr) && s_lootErr.empty())
            s_lootErr = "Couldn't read this loot table.";
    }

    const bool have = !s_lootPath.empty();
    if (ImGui::Button("+ New Table")) ContentCreateLoot(ed);
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::Button("Duplicate")) {
        const fs::path from(s_lootPath);
        const std::string path = UniquePath(from.parent_path(), from.stem().string() + " Copy", ".okayloot");
        Snap before;
        before.tab = 1;
        before.path = path;
        before.present = false;
        before.reselect = s_lootPath;
        if (s_loot.Save(path)) {
            s_lootPath = Canon(path);
            s_lootLoaded = true;
            s_needScan = true;
            s_previewKey.clear();
            CommitTab(ed, 1, before);
        } else ConsoleLog("Couldn't duplicate " + s_lootPath, 2);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        s_ask = Ask::LootFile;
        s_askText = "Delete " + fs::path(s_lootPath).filename().string() + "? The file is removed. Ctrl+Z puts it back.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::Button("Save")) SaveTab(ed, 1, true);
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        Snap before = CaptureTab(ed, 1);
        LootTable fresh;
        std::string err;
        if (fresh.Load(s_lootPath, &err)) {
            s_loot = fresh;
            s_lootErr.clear();
            s_previewKey.clear();
            CommitTab(ed, 1, before);
            s_status = "Reloaded " + ShortPath(ed, s_lootPath);
        } else s_status = err;
    }
    ImGui::EndDisabled();
    if (!s_status.empty() && s_tab == 1) { ImGui::SameLine(); ImGui::TextDisabled("%s", s_status.c_str()); }

    ImGui::BeginChild("##lootfiles", ImVec2(260, 0), true);
    SearchField("##lootfilter", s_lootFilter, sizeof(s_lootFilter), "Search tables", -1.0f);
    if (s_lootFiles.empty()) ImGui::TextWrapped("No loot tables yet. New Table saves an .okayloot file in the project.");
    for (const auto& f : s_lootFiles) {
        const std::string name = fs::path(f).filename().string();
        if (!Match(s_lootFilter, name)) continue;
        if (ImGui::Selectable(name.c_str(), f == s_lootPath)) {
            if (f != s_lootPath) {
                if (s_editOpen) CloseEdit();
                s_lootPath = f;
                s_lootLoaded = false;
                s_loot = {};
                s_lootErr.clear();
                s_lootLoaded = true;
                s_previewKey.clear();
                if (!s_loot.Load(f, &s_lootErr) && s_lootErr.empty()) s_lootErr = "Couldn't read this loot table.";
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ShortPath(ed, f).c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##lootedit", ImVec2(0, 0), true);
    if (!have) ImGui::TextDisabled("Select a loot table, or add one.");
    else {
        ImGui::TextDisabled("%s%s", ShortPath(ed, s_lootPath).c_str(), s_dirty && s_tab == 1 ? "    unsaved" : "");
        if (!s_lootErr.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", s_lootErr.c_str());
        DrawIssues(s_loot.Issues(ItemDatabase::Get()));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rolls");
        ImGui::SameLine();
        int r0 = s_loot.rollsMin, r1 = s_loot.rollsMax;
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::DragInt("##rmin", &r0, 0.1f, 0, 1000)) {
            if (r0 != s_loot.rollsMin) { NoteEdit(); s_loot.rollsMin = r0; if (s_loot.rollsMax < r0) s_loot.rollsMax = r0; }
        }
        AfterItem();
        ImGui::SameLine();
        ImGui::TextUnformatted("to");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::DragInt("##rmax", &r1, 0.1f, 0, 1000)) {
            if (r1 != s_loot.rollsMax) { NoteEdit(); s_loot.rollsMax = r1; if (s_loot.rollsMin > r1) s_loot.rollsMin = r1; }
        }
        AfterItem();
        ImGui::SameLine();
        ImGui::TextDisabled("how many times the table is picked");

        const float previewH = 340.0f;
        ImGui::BeginChild("##lootentries", ImVec2(0, -previewH), false);
        if (ImGui::BeginTable("lootrows", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 2.4f);
            ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Chance", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)s_loot.entries.size(); ++i) {
                LootTable::Entry& e = s_loot.entries[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableNextColumn();
                ItemDropdown("##item", e.item);
                ImGui::TableNextColumn();
                float w = e.weight;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("##w", &w, 0.05f, 0.01f, 100000.0f, "%.2f") && w != e.weight) { NoteEdit(); e.weight = w; }
                AfterItem();
                ImGui::TableNextColumn();
                int mn = e.min;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragInt("##mn", &mn, 0.1f, 0, 9999) && mn != e.min) {
                    NoteEdit(); e.min = mn; if (e.max < e.min) e.max = e.min;
                }
                AfterItem();
                ImGui::TableNextColumn();
                int mx = e.max;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragInt("##mx", &mx, 0.1f, 0, 9999) && mx != e.max) {
                    NoteEdit(); e.max = mx; if (e.min > e.max) e.min = e.max;
                }
                AfterItem();
                ImGui::TableNextColumn();
                float pct = e.chance * 100.0f;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##ch", &pct, 0.0f, 100.0f, "%.0f%%") && pct != e.chance * 100.0f) {
                    NoteEdit(); e.chance = pct / 100.0f;
                }
                AfterItem();
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Copy")) {
                    Snap before = CaptureTab(ed, 1);
                    s_loot.entries.insert(s_loot.entries.begin() + i + 1, e);
                    CommitTab(ed, 1, before);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    s_ask = Ask::LootEntry;
                    s_askIndex = i;
                    s_askText = "Remove the drop \"" + (e.item.empty() ? std::string("this entry") : ItemLabel(e.item)) + "\"? Ctrl+Z puts it back.";
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("+ Entry")) {
            Snap before = CaptureTab(ed, 1);
            LootTable::Entry e;
            e.item = FirstItem();
            e.weight = 1.0f;
            e.min = 1;
            e.max = 1;
            e.chance = 1.0f;
            s_loot.entries.push_back(e);
            CommitTab(ed, 1, before);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Weight picks the row. Chance is the odds it then drops. From and To are the count.");
        ImGui::EndChild();

        ImGui::BeginChild("##lootpreview", ImVec2(0, 0), true);
        ImGui::TextUnformatted("Roll preview");
        ImGui::SameLine();
        if (ImGui::SmallButton("Roll again")) { ++s_rollNonce; s_previewKey.clear(); }
        ImGui::SameLine();
        ImGui::TextDisabled("The game rolls this same table.");
        RebuildPreview();
        if (s_loot.entries.empty()) ImGui::TextDisabled("Add an entry to see what drops.");
        else {
            ImGui::TextUnformatted("One roll:");
            if (s_once.empty()) ImGui::BulletText("Nothing dropped.");
            for (const auto& st : s_once) ImGui::BulletText("%s  x %d", ItemLabel(st.item).c_str(), st.count);
            ImGui::Spacing();
            ImGui::Text("Over %d rolls", s_sampleRolls);
            const auto shares = s_loot.Shares();
            const float avgRolls = 0.5f * (float)(s_loot.rollsMin + s_loot.rollsMax);
            std::map<std::string, float> expect;
            for (const auto& sh : shares) {
                if (sh.index < 0 || sh.index >= (int)s_loot.entries.size()) continue;
                const auto& e = s_loot.entries[sh.index];
                expect[sh.item] += avgRolls * sh.dropShare * 0.5f * (float)(e.min + e.max);
            }
            int peak = 1;
            for (const auto& kv : s_sampleQty) peak = std::max(peak, kv.second);
            std::vector<std::string> names;
            for (const auto& e : s_loot.entries) if (std::find(names.begin(), names.end(), e.item) == names.end()) names.push_back(e.item);
            for (const auto& kv : s_sampleQty) if (std::find(names.begin(), names.end(), kv.first) == names.end()) names.push_back(kv.first);
            for (const auto& name : names) {
                if (name.empty()) continue;
                const int got = s_sampleQty.count(name) ? s_sampleQty[name] : 0;
                const float per = s_sampleRolls > 0 ? (float)got / (float)s_sampleRolls : 0.0f;
                const float exp = expect.count(name) ? expect[name] : 0.0f;
                char line[160];
                std::snprintf(line, sizeof(line), "%s   sample %.2f / roll    expected about %.2f", ItemLabel(name).c_str(), per, exp);
                ImGui::ProgressBar(peak > 0 ? (float)got / (float)peak : 0.0f, ImVec2(-1.0f, 0.0f), line);
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void EnsureBook() {
    if (s_bookLoaded) return;
    s_book = {};
    s_bookErr.clear();
    s_bookLoaded = true;
    if (s_bookPath.empty()) return;
    if (!s_book.Load(s_bookPath, &s_bookErr) && s_bookErr.empty()) s_bookErr = "Couldn't read this recipe book.";
    if (s_recipe >= (int)s_book.recipes.size()) s_recipe = (int)s_book.recipes.size() - 1;
    if (s_recipe < 0 && !s_book.recipes.empty()) s_recipe = 0;
}

void DrawRecipeTab(EditorState& ed) {
    Scan(ed, false);
    if (!s_pending.empty() && s_tab == 2) {
        const std::string want = Canon(s_pending);
        if (std::find(s_recipeFiles.begin(), s_recipeFiles.end(), want) != s_recipeFiles.end()) {
            s_bookPath = want;
            s_bookLoaded = false;
            s_recipe = 0;
        }
        s_pending.clear();
    }
    if (s_bookPath.empty() && !s_recipeFiles.empty()) { s_bookPath = s_recipeFiles.front(); s_bookLoaded = false; }
    EnsureBook();
    if (s_recipe >= (int)s_book.recipes.size()) s_recipe = (int)s_book.recipes.size() - 1;

    const bool have = !s_bookPath.empty();
    if (ImGui::Button("+ New Book")) ContentCreateRecipe(ed);
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::Button("Duplicate")) {
        const fs::path from(s_bookPath);
        const std::string path = UniquePath(from.parent_path(), from.stem().string() + " Copy", ".okayrecipes");
        Snap before;
        before.tab = 2;
        before.path = path;
        before.present = false;
        before.reselect = s_bookPath;
        if (s_book.Save(path)) {
            s_bookPath = Canon(path);
            s_bookLoaded = true;
            s_needScan = true;
            CommitTab(ed, 2, before);
        } else ConsoleLog("Couldn't duplicate " + s_bookPath, 2);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        s_ask = Ask::RecipeFile;
        s_askText = "Delete " + fs::path(s_bookPath).filename().string() + "? The file is removed. Ctrl+Z puts it back.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::Button("Save")) SaveTab(ed, 2, true);
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        Snap before = CaptureTab(ed, 2);
        RecipeBook fresh;
        std::string err;
        if (fresh.Load(s_bookPath, &err)) {
            s_book = fresh;
            s_bookErr.clear();
            if (s_recipe >= (int)s_book.recipes.size()) s_recipe = (int)s_book.recipes.size() - 1;
            CommitTab(ed, 2, before);
            s_status = "Reloaded " + ShortPath(ed, s_bookPath);
        } else s_status = err;
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("##bookfiles", ImVec2(230, 0), true);
    SearchField("##bookfilter", s_bookFilter, sizeof(s_bookFilter), "Search books", -1.0f);
    if (s_recipeFiles.empty()) ImGui::TextWrapped("No recipe books yet. New Book saves an .okayrecipes file in the project.");
    for (const auto& f : s_recipeFiles) {
        const std::string name = fs::path(f).filename().string();
        if (!Match(s_bookFilter, name)) continue;
        if (ImGui::Selectable(name.c_str(), f == s_bookPath)) {
            if (f != s_bookPath) {
                if (s_editOpen) CloseEdit();
                s_bookPath = f;
                s_bookLoaded = false;
                s_recipe = 0;
                EnsureBook();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ShortPath(ed, f).c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##recipelist", ImVec2(230, 0), true);
    SearchField("##recipefilter", s_recipeFilter, sizeof(s_recipeFilter), "Search recipes", -1.0f);
    if (have && ImGui::Button("+ Recipe", ImVec2(-1, 0))) {
        Snap before = CaptureTab(ed, 2);
        Recipe r;
        r.output = FirstItem();
        r.outputCount = 1;
        r.inputs.push_back({r.output, 1});
        s_book.recipes.push_back(r);
        s_recipe = (int)s_book.recipes.size() - 1;
        CommitTab(ed, 2, before);
    }
    for (int i = 0; i < (int)s_book.recipes.size(); ++i) {
        const Recipe& r = s_book.recipes[i];
        const std::string hay = r.output + " " + ItemLabel(r.output);
        if (!Match(s_recipeFilter, hay)) continue;
        std::string row = ItemLabel(r.output) + "  x" + std::to_string(std::max(1, r.outputCount));
        if (ImGui::Selectable((row + "##r" + std::to_string(i)).c_str(), s_recipe == i)) s_recipe = i;
    }
    if (have && s_book.recipes.empty()) ImGui::TextDisabled("This book has no recipes.");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##recipeedit", ImVec2(0, 0), true);
    if (!have) ImGui::TextDisabled("Select a recipe book, or add one.");
    else if (s_recipe < 0 || s_recipe >= (int)s_book.recipes.size()) {
        ImGui::TextDisabled("%s", ShortPath(ed, s_bookPath).c_str());
        if (!s_bookErr.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", s_bookErr.c_str());
        DrawIssues(s_book.Issues(ItemDatabase::Get()));
        ImGui::TextDisabled("Select a recipe, or add one.");
    } else {
        Recipe& r = s_book.recipes[s_recipe];
        ImGui::TextDisabled("%s%s", ShortPath(ed, s_bookPath).c_str(), s_dirty && s_tab == 2 ? "    unsaved" : "");
        if (!s_bookErr.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", s_bookErr.c_str());
        DrawIssues(s_book.Issues(ItemDatabase::Get()));
        ImGui::TextUnformatted("Makes");
        ItemDropdown("##out", r.output);
        ImGui::SetNextItemWidth(120.0f);
        TouchInt("Count", r.outputCount, 0.1f, 1, 9999);
        TouchFloat("Crafting Time (seconds)", r.craftTime, 0.05f, 0.0f, 3600.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long the craft takes. 0 finishes at once.");
        TouchString("Bench", r.station, "Only at an open crafting station with this tag. Blank means anywhere.");
        TouchString("Unlock", r.unlockFlag, "Hidden until this game variable is not zero. Blank means it is always known.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Ingredients");
        for (int i = 0; i < (int)r.inputs.size(); ++i) {
            ImGui::PushID(1000 + i);
            ImGui::SetNextItemWidth(280.0f);
            ItemDropdown("##in", r.inputs[i].item);
            ImGui::SameLine();
            int n = r.inputs[i].count;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragInt("##n", &n, 0.1f, 1, 9999) && n != r.inputs[i].count) { NoteEdit(); r.inputs[i].count = n; }
            AfterItem();
            ImGui::SameLine();
            ImGui::BeginDisabled(r.inputs.size() <= 1);
            if (ImGui::SmallButton("Remove")) {
                Snap before = CaptureTab(ed, 2);
                r.inputs.erase(r.inputs.begin() + i);
                CommitTab(ed, 2, before);
                ImGui::EndDisabled();
                ImGui::PopID();
                break;
            }
            ImGui::EndDisabled();
            if (r.inputs.size() <= 1 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("A recipe needs at least one ingredient, or the file leaves it out. Delete the recipe to remove it.");
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Ingredient")) {
            Snap before = CaptureTab(ed, 2);
            r.inputs.push_back({FirstItem(), 1});
            CommitTab(ed, 2, before);
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Also gives");
        for (int i = 0; i < (int)r.byproducts.size(); ++i) {
            ImGui::PushID(2000 + i);
            ImGui::SetNextItemWidth(280.0f);
            ItemDropdown("##by", r.byproducts[i].item);
            ImGui::SameLine();
            int n = r.byproducts[i].count;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragInt("##bn", &n, 0.1f, 1, 9999) && n != r.byproducts[i].count) { NoteEdit(); r.byproducts[i].count = n; }
            AfterItem();
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                Snap before = CaptureTab(ed, 2);
                r.byproducts.erase(r.byproducts.begin() + i);
                CommitTab(ed, 2, before);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Extra Output")) {
            Snap before = CaptureTab(ed, 2);
            r.byproducts.push_back({FirstItem(), 1});
            CommitTab(ed, 2, before);
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Duplicate Recipe")) {
            Snap before = CaptureTab(ed, 2);
            s_book.recipes.insert(s_book.recipes.begin() + s_recipe + 1, r);
            ++s_recipe;
            CommitTab(ed, 2, before);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Recipe")) {
            s_ask = Ask::Recipe;
            s_askText = "Delete the recipe for \"" + ItemLabel(r.output) + "\"? Ctrl+Z puts it back.";
        }
    }
    ImGui::EndChild();
}

void Confirm(EditorState& ed) {
    if (s_ask != Ask::None) ImGui::OpenPopup("Delete this?");
    if (!ImGui::BeginPopupModal("Delete this?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("%s", s_askText.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Delete", ImVec2(120, 0))) {
        if (s_ask == Ask::Item) {
            Snap before = CaptureTab(ed, 0);
            ItemDatabase::Get().Remove(s_itemSel);
            s_itemSel.clear();
            CommitTab(ed, 0, before);
        } else if (s_ask == Ask::LootFile) {
            Snap before = CaptureTab(ed, 1);
            std::error_code ec;
            fs::remove(s_lootPath, ec);
            LootTable::Forget(before.path);
            s_lootPath.clear();
            s_loot = {};
            s_lootLoaded = false;
            s_needScan = true;
            CommitTab(ed, 1, before);
        } else if (s_ask == Ask::LootEntry && s_askIndex >= 0 && s_askIndex < (int)s_loot.entries.size()) {
            Snap before = CaptureTab(ed, 1);
            s_loot.entries.erase(s_loot.entries.begin() + s_askIndex);
            s_previewKey.clear();
            CommitTab(ed, 1, before);
        } else if (s_ask == Ask::RecipeFile) {
            Snap before = CaptureTab(ed, 2);
            std::error_code ec;
            fs::remove(s_bookPath, ec);
            RecipeBook::Forget(before.path);
            s_bookPath.clear();
            s_book = {};
            s_bookLoaded = false;
            s_needScan = true;
            CommitTab(ed, 2, before);
        } else if (s_ask == Ask::Recipe && s_recipe >= 0 && s_recipe < (int)s_book.recipes.size()) {
            Snap before = CaptureTab(ed, 2);
            s_book.recipes.erase(s_book.recipes.begin() + s_recipe);
            if (s_recipe >= (int)s_book.recipes.size()) s_recipe = (int)s_book.recipes.size() - 1;
            CommitTab(ed, 2, before);
        }
        s_ask = Ask::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) { s_ask = Ask::None; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

} // namespace

void DrawContentEditors(EditorState& ed) {
    if (!g_showItemDatabase) { s_focused = false; s_ed = nullptr; return; }
    s_ed = &ed;
    const bool shot = std::getenv("OKAY_UI_CONTENT") && std::getenv("OKAY_UI_SHOT");
    if (shot) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 12.0f, vp->WorkPos.y + 36.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::max(640.0f, vp->WorkSize.x - 24.0f), std::max(480.0f, vp->WorkSize.y - 48.0f)), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(1180, 740), ImGuiCond_FirstUseEver);
    }
    if (!ImGui::Begin("Items, Loot & Recipes", &g_showItemDatabase)) {
        s_focused = false;
        s_ed = nullptr;
        ImGui::End();
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
    if (shot) ImGui::GetIO().MousePos = ImVec2(-10000.0f, -10000.0f); // a parked cursor, so a tooltip doesn't cover the shot
    if (s_forceTab >= 0 && s_forceTab <= 2) { s_selectTab = s_forceTab; s_tab = s_forceTab; s_forceTab = -1; }
    if (ImGui::BeginTabBar("##contenttabs")) {
        const ImGuiTabItemFlags items = s_selectTab == 0 ? ImGuiTabItemFlags_SetSelected : 0;
        const ImGuiTabItemFlags loot = s_selectTab == 1 ? ImGuiTabItemFlags_SetSelected : 0;
        const ImGuiTabItemFlags recipes = s_selectTab == 2 ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Items", nullptr, items)) { s_tab = 0; DrawItemTab(ed); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Loot Tables", nullptr, loot)) { s_tab = 1; DrawLootTab(ed); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Recipes", nullptr, recipes)) { s_tab = 2; DrawRecipeTab(ed); ImGui::EndTabItem(); }
        s_selectTab = -1;
        ImGui::EndTabBar();
    }
    Confirm(ed);
    s_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsPopupOpen("Delete this?");
    ImGui::PopStyleVar(2);
    ImGui::End();
    s_ed = nullptr;
}

bool ContentEditorsFocused() { return s_focused && g_showItemDatabase; }

bool ContentEditorsUndo(EditorState& ed, bool redo) {
    if (s_editOpen) { s_ed = &ed; CloseEdit(); s_ed = nullptr; }
    auto& from = redo ? s_redo : s_undo;
    auto& to = redo ? s_undo : s_redo;
    if (from.empty()) return false;
    s_ed = &ed;
    to.push_back(CaptureTab(ed, from.back().tab));
    Snap s = from.back();
    from.pop_back();
    Apply(ed, s);
    s_ed = nullptr;
    return true;
}

void OpenContentTab(int tab) {
    g_showItemDatabase = true;
    if (tab < 0) tab = 0;
    if (tab > 2) tab = 2;
    s_forceTab = tab;
}

void OpenContentFile(const std::string& path) {
    g_showItemDatabase = true;
    s_needScan = true;
    const std::string ext = Lower(fs::path(path).extension().string());
    if (ext == ".okayloot") { s_forceTab = 1; s_pending = path; }
    else if (ext == ".okayrecipes") { s_forceTab = 2; s_pending = path; }
    else s_forceTab = 0;
}

void ContentAddItem(EditorState& ed) {
    g_showItemDatabase = true;
    s_forceTab = 0;
    if (s_editOpen && s_ed) CloseEdit();
    Snap before = CaptureTab(ed, 0);
    ItemDefinition d;
    int n = 1;
    do { d.id = "item" + std::to_string(n++); } while (ItemDatabase::Get().Find(d.id));
    d.name = "New Item";
    s_itemSel = ItemDatabase::Get().Put(d).id;
    PushSnap(before, CaptureTab(ed, 0));
    SaveTab(ed, 0, false);
}

void ContentCreateLoot(EditorState& ed) {
    g_showItemDatabase = true;
    s_forceTab = 1;
    const fs::path dir = SaveDir(ed);
    if (dir.empty()) {
        ConsoleLog("Open a project before adding a loot table.", 1);
        s_status = "Open a project to add a loot table.";
        return;
    }
    const std::string path = UniquePath(dir, "Loot Table", ".okayloot");
    LootTable t;
    t.rollsMin = 1;
    t.rollsMax = 1;
    LootTable::Entry e;
    e.item = FirstItem();
    e.weight = 1.0f;
    e.min = 1;
    e.max = 1;
    e.chance = 1.0f;
    t.entries.push_back(e);
    if (!t.Save(path)) { ConsoleLog("Couldn't write " + path, 2); return; }
    Snap before;
    before.tab = 1;
    before.path = Canon(path);
    before.present = false;
    before.reselect = s_lootPath;
    s_loot = t;
    s_lootPath = before.path;
    s_lootLoaded = true;
    s_lootErr.clear();
    s_needScan = true;
    s_previewKey.clear();
    s_pending.clear();
    PushSnap(before, CaptureTab(ed, 1));
    s_dirty = false;
    s_status = "Saved " + ShortPath(ed, s_lootPath);
}

void ContentCreateRecipe(EditorState& ed) {
    g_showItemDatabase = true;
    s_forceTab = 2;
    const fs::path dir = SaveDir(ed);
    if (dir.empty()) {
        ConsoleLog("Open a project before adding a recipe book.", 1);
        s_status = "Open a project to add a recipe book.";
        return;
    }
    const std::string path = UniquePath(dir, "Recipe Book", ".okayrecipes");
    RecipeBook book;
    Recipe r;
    r.output = FirstItem();
    r.outputCount = 1;
    r.inputs.push_back({r.output, 1});
    book.recipes.push_back(r);
    if (!book.Save(path)) { ConsoleLog("Couldn't write " + path, 2); return; }
    Snap before;
    before.tab = 2;
    before.path = Canon(path);
    before.present = false;
    before.reselect = s_bookPath;
    s_book = book;
    s_bookPath = before.path;
    s_bookLoaded = true;
    s_bookErr.clear();
    s_recipe = 0;
    s_needScan = true;
    s_pending.clear();
    PushSnap(before, CaptureTab(ed, 2));
    s_dirty = false;
    s_status = "Saved " + ShortPath(ed, s_bookPath);
}

void ContentEditorsDevHooks(EditorState& ed) {
    (void)ed;
    if (!std::getenv("OKAY_UI_CONTENT") && !std::getenv("OKAY_UI_ITEMS")) return;
    g_showItemDatabase = true;
    static int once = 0;
    if (once) return;
    once = 1;
    if (const char* dir = std::getenv("OKAY_UI_CONTENT_DIR"); dir && dir[0]) {
        const std::string items = std::string(dir) + "/items.okayitems";
        std::error_code ec;
        if (fs::exists(items, ec)) ItemDatabase::Get().Load(items);
    }
    if (const char* tab = std::getenv("OKAY_UI_CONTENT_TAB")) s_forceTab = std::atoi(tab);
    s_needScan = true;
}

} // namespace oked
