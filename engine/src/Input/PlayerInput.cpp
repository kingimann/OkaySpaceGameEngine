#include "okay/Input/PlayerInput.hpp"

namespace okay {

bool PlayerInputManager::KeyboardPaired() const {
    for (const auto& p : m_players) if (p && p->device != JoinDevice::Pad) return true;
    return false;
}
bool PlayerInputManager::PadPaired(int pad) const {
    for (const auto& p : m_players) if (p && (p->device == JoinDevice::Any || (p->device == JoinDevice::Pad && p->pad == pad))) return true;
    return false;
}

int PlayerInputManager::Join(JoinDevice device, int pad) {
    if (device == JoinDevice::Pad) { if (pad < 0 || pad >= Input::kMaxGamepads || PadPaired(pad)) return -1; }
    else if (KeyboardPaired()) return -1;
    if (device == JoinDevice::Any) for (int p = 0; p < Input::kMaxGamepads; ++p) if (PadPaired(p)) return -1;
    int idx = -1;
    for (int i = 0; i < (int)m_players.size() && idx < 0; ++i) if (!m_players[(size_t)i]) idx = i;
    if (idx < 0) { if ((int)m_players.size() >= kMaxPlayers) return -1; m_players.emplace_back(); idx = (int)m_players.size() - 1; }
    auto slot = std::make_unique<Slot>();
    slot->set = m_asset;
    if (onJoin) onJoin(slot->set);
    slot->device = device; slot->pad = device == JoinDevice::Pad ? pad : -1;
    ActionSet& s = slot->set;
    // The scheme that fits the device; a pad player reads their own controller whatever the bindings say.
    for (const ControlScheme& c : s.Schemes()) {
        if (device == JoinDevice::Pad && c.gamepad) { s.SetScheme(c.name); break; }
        if (device != JoinDevice::Pad && (c.keyboard || c.mouse)) { s.SetScheme(c.name); break; }
    }
    s.SetPad(slot->pad);
    s.SetAutoSwitch(device == JoinDevice::Any);
    slot->serial = ++m_joins;
    m_players[(size_t)idx] = std::move(slot);
    LoadOverrides(idx);
    return idx;
}

int PlayerInputManager::PollJoin() {
    for (int p = 0; p < Input::kMaxGamepads; ++p)
        if (Input::GamepadConnected(p) && !PadPaired(p) && Input::FirstGamepadButtonDown(p) >= 0) return Join(JoinDevice::Pad, p);
    if (!KeyboardPaired() && (Input::GetKeyDown(' ') || Input::GetKeyDown('\n') || Input::GetKeyDown('\r'))) return Join(JoinDevice::Keyboard);
    return -1;
}

void PlayerInputManager::Leave(int player) {
    if (!Active(player)) return;
    SaveOverrides(player);
    m_players[(size_t)player].reset();
    while (!m_players.empty() && !m_players.back()) m_players.pop_back();
}

void PlayerInputManager::Update(float dt) { for (auto& p : m_players) if (p) p->set.Update(dt); }
void PlayerInputManager::SaveOverrides(int player) const { if (Active(player)) m_players[(size_t)player]->set.SaveOverrides("player" + std::to_string(player)); }
void PlayerInputManager::LoadOverrides(int player) { if (Active(player)) m_players[(size_t)player]->set.LoadOverrides("player" + std::to_string(player)); }

} // namespace okay
