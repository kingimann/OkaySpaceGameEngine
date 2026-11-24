#include "okay/Input/InputRuntime.hpp"
#include "okay/Input/InputAsset.hpp"
#include <fstream>

namespace okay {
namespace {
struct State {
    PlayerInputManager players;
    std::string path;
    bool haveAsset = false;
    std::size_t cursor[PlayerInputManager::kMaxPlayers] = {};
    ActionSet table;                 ///< THE action table: the asset plus the code-defined actions, every device at once
    bool tableBuilt = false, building = false;
    unsigned tableAsset = 0;          ///< the asset serial the table was built from
    unsigned p0Serial = 0, p0Rev = 0, p0Table = 0;   ///< what player 0's bindings were last copied from
};
State& S() { static State s; return s; }
unsigned& Serial() { static unsigned n = 0; return n; }
void Hook(State& s) { s.players.onJoin = [](ActionSet& set) { InputActions::PrepareSet(set); }; }
void EnsureAsset() { State& s = S(); if (!s.haveAsset) { s.players.SetAsset(DefaultInputAsset()); s.haveAsset = true; } Hook(s); }
// The asset changed (or was forgotten): the table is built again from it on next use.
void DropTable() { S().tableBuilt = false; }
}

PlayerInputManager& InputRuntime::Players() { EnsureAsset(); return S().players; }
const std::string& InputRuntime::AssetPath() { return S().path; }

ActionSet& InputRuntime::Actions() {
    State& s = S();
    if (!s.building && (!s.tableBuilt || s.tableAsset != s.players.AssetSerial())) {
        EnsureAsset();
        s.tableAsset = s.players.AssetSerial();
        s.building = true;
        s.table = s.players.Asset();
        s.table.SetPad(-1);
        s.table.SetAutoSwitch(false);
        s.table.SetAllSchemes(true);   // the keyboard and every controller count together, as InputActions always read them
        InputActions::PrepareTable(s.table);
        s.building = false;
        s.tableBuilt = true;
        ++Serial();
    }
    return s.table;
}
ActionSet* InputRuntime::ActionsIfBuilt() { State& s = S(); return s.tableBuilt && !s.building && s.tableAsset == s.players.AssetSerial() ? &s.table : nullptr; }
unsigned InputRuntime::ActionsSerial() { return Serial(); }
void InputRuntime::RebuildActions() { DropTable(); }

void InputRuntime::ForEachSet(void (*fn)(ActionSet&, const void*), const void* arg) {
    State& s = S();
    if (ActionSet* t = ActionsIfBuilt()) fn(*t, arg);
    for (int i = 0; i < s.players.Count(); ++i) if (ActionSet* p = s.players.Player(i)) fn(*p, arg);
}

void InputRuntime::SyncPlayer0() {
    State& s = S();
    ActionSet* p = s.players.Player(0);
    ActionSet* t = ActionsIfBuilt();
    if (!p || !t) return;
    const unsigned serial = s.players.Serial(0);
    if (serial == s.p0Serial && t->Revision() == s.p0Rev && Serial() == s.p0Table) return;
    for (const ActionDef& d : t->Actions()) {
        ActionDef* mine = p->Find(d.name);
        if (!mine) { mine = &p->Add(d.name, d.type, d.map); mine->pressPoint = d.pressPoint; mine->interactions = d.interactions; mine->processors = d.processors; }
        mine->bindings = d.bindings;
        mine->authored = d.authored;
        mine->reshaped = d.reshaped;
        mine->sum = d.sum;
        mine->response = d.response;
    }
    p->Touch();
    s.p0Serial = serial; s.p0Rev = t->Revision(); s.p0Table = Serial();
}

bool InputRuntime::LoadAsset(const std::string& path, std::string* error) {
    ActionSet set;
    if (!LoadInputAsset(path, set, error)) return false;
    State& s = S();
    Hook(s);
    // Players already in keep their device but start again from the new asset.
    struct Old { JoinDevice d; int pad; };
    std::vector<Old> old;
    for (int i = 0; i < s.players.Count(); ++i) old.push_back(s.players.Active(i) ? Old{s.players.DeviceOf(i), s.players.PadOf(i)} : Old{JoinDevice::Any, -2});
    for (int i = 0; i < (int)old.size(); ++i) s.players.Leave(i);
    s.players.SetAsset(set);
    s.path = path; s.haveAsset = true;
    DropTable();
    for (const Old& o : old) if (o.pad != -2) s.players.Join(o.d, o.pad);
    return true;
}
void InputRuntime::UseDefault() {
    State& s = S();
    Hook(s);
    for (int i = 0; i < s.players.Count(); ++i) s.players.Leave(i);
    s.players.SetAsset(DefaultInputAsset()); s.path.clear(); s.haveAsset = true;
    DropTable();
}
ActionSet* InputRuntime::Set(int player) {
    EnsureAsset();
    State& s = S();
    Actions();
    if (s.players.Count() == 0 && player == 0) s.players.Join(JoinDevice::Any);
    SyncPlayer0();
    return s.players.Player(player);
}
bool InputRuntime::PopEvent(int player, std::string& out) {
    State& s = S();
    const ActionSet* a = player >= 0 && player < PlayerInputManager::kMaxPlayers ? s.players.Player(player) : nullptr;
    if (!a) return false;
    std::size_t& c = s.cursor[player];
    if (c >= a->Events().size()) return false;
    const PhaseEvent& e = a->Events()[c++];
    out = e.action + ":" + PhaseName(e.phase);
    return true;
}
void InputRuntime::Update(float dt) {
    State& s = S();
    if (s.players.Count() == 0) return;
    SyncPlayer0();
    s.players.Update(dt);
    for (std::size_t& c : s.cursor) c = 0;
}
bool InputRuntime::UseProjectAsset(const std::string& projectDir) {
    Reset();
    if (projectDir.empty()) return false;
    const char last = projectDir.back();
    const std::string path = projectDir + (last == '/' || last == '\\' ? "" : "/") + kProjectAssetRef;
    std::ifstream f(path);
    return f.good() && LoadAsset(path);
}
void InputRuntime::Reset() { S() = State{}; }
bool InputRuntime::Rebind(int player, const std::string& action, int binding, int part, const std::string& path) {
    if (player == 0) {   // player 0's bindings are the table's
        Set(0);
        ActionSet& t = Actions();
        const ActionDef* d = t.FindNoCase(action);
        if (!d || !t.Rebind(d->name, binding, part, path)) return false;
        SyncPlayer0();
        return true;
    }
    ActionSet* s = Set(player);
    return s && s->Rebind(action, binding, part, path);
}
bool InputRuntime::ResetBinding(int player, const std::string& action, int binding) {
    ActionSet* s = Set(player);
    if (!s) return false;
    if (player == 0) s = &Actions();
    bool ok = true;
    if (action.empty()) s->ResetOverrides();
    else { const ActionDef* d = s->FindNoCase(action); ok = d && s->ResetOverride(d->name, binding); }
    if (player == 0) SyncPlayer0();
    return ok;
}
void InputRuntime::SaveOverrides(int player) {
    if (!Set(player)) return;
    if (player == 0) InputActions::Save();   // the one store: "inputov.player0" and the per-action form
    else S().players.SaveOverrides(player);
}
int InputRuntime::JoinNamed(const std::string& d) {
    EnsureAsset();
    PlayerInputManager& p = S().players;
    if (d == "keyboard") return p.Join(JoinDevice::Keyboard);
    if (d == "any") return p.Join(JoinDevice::Any);
    if (d.size() == 4 && d.compare(0, 3, "pad") == 0 && d[3] >= '0' && d[3] <= '3') return p.Join(JoinDevice::Pad, d[3] - '0');
    return -1;
}
} // namespace okay
