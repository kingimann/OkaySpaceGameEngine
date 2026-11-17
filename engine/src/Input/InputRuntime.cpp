#include "okay/Input/InputRuntime.hpp"
#include "okay/Input/InputAsset.hpp"

namespace okay {
namespace {
struct State {
    PlayerInputManager players;
    std::string path;
    bool haveAsset = false;
    std::size_t cursor[PlayerInputManager::kMaxPlayers] = {};
};
State& S() { static State s; return s; }
void EnsureAsset() { State& s = S(); if (!s.haveAsset) { s.players.SetAsset(DefaultInputAsset()); s.haveAsset = true; } }
}

PlayerInputManager& InputRuntime::Players() { EnsureAsset(); return S().players; }
const std::string& InputRuntime::AssetPath() { return S().path; }

bool InputRuntime::LoadAsset(const std::string& path, std::string* error) {
    ActionSet set;
    if (!LoadInputAsset(path, set, error)) return false;
    State& s = S();
    // Players already in keep their device but start again from the new asset.
    struct Old { JoinDevice d; int pad; };
    std::vector<Old> old;
    for (int i = 0; i < s.players.Count(); ++i) old.push_back(s.players.Active(i) ? Old{s.players.DeviceOf(i), s.players.PadOf(i)} : Old{JoinDevice::Any, -2});
    for (int i = 0; i < (int)old.size(); ++i) s.players.Leave(i);
    s.players.SetAsset(set);
    s.path = path; s.haveAsset = true;
    for (const Old& o : old) if (o.pad != -2) s.players.Join(o.d, o.pad);
    return true;
}
void InputRuntime::UseDefault() {
    State& s = S();
    for (int i = 0; i < s.players.Count(); ++i) s.players.Leave(i);
    s.players.SetAsset(DefaultInputAsset()); s.path.clear(); s.haveAsset = true;
}
ActionSet* InputRuntime::Set(int player) {
    EnsureAsset();
    State& s = S();
    if (s.players.Count() == 0 && player == 0) s.players.Join(JoinDevice::Any);
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
    s.players.Update(dt);
    for (std::size_t& c : s.cursor) c = 0;
}
void InputRuntime::Reset() { S() = State{}; }
int InputRuntime::JoinNamed(const std::string& d) {
    EnsureAsset();
    PlayerInputManager& p = S().players;
    if (d == "keyboard") return p.Join(JoinDevice::Keyboard);
    if (d == "any") return p.Join(JoinDevice::Any);
    if (d.size() == 4 && d.compare(0, 3, "pad") == 0 && d[3] >= '0' && d[3] <= '3') return p.Join(JoinDevice::Pad, d[3] - '0');
    return -1;
}
} // namespace okay
