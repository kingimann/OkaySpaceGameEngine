#pragma once
// Local players: each one has its own copy of an input asset (an ActionSet), its own device pairing (the keyboard and mouse, or
// one controller) and its own control scheme. Press-any-button joining, leaving, per-player rebinding saved under the player's
// number, and one Update for all. Pure logic over Input: no SDL, no window.
#include "okay/Input/ActionSet.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay {

enum class JoinDevice { Keyboard, Pad, Any };   // Any: the keyboard and every controller, switching scheme with the last used (a single player)

class PlayerInputManager {
public:
    static constexpr int kMaxPlayers = 8;
    /// The asset every new player starts from (copied).
    void SetAsset(const ActionSet& asset) { m_asset = asset; }
    const ActionSet& Asset() const { return m_asset; }
    /// Add a player. Pad: `pad` is the controller number (0..3). Keyboard and Pad each pair a device exactly once.
    /// Returns the player index, or -1 (device already paired, bad pad number, or no room).
    int Join(JoinDevice device, int pad = -1);
    /// "Press any button to join": the first unpaired controller with a button down, or Space / Enter on an unpaired keyboard.
    /// Returns the new player's index, or -1. Call once a frame while a join screen is up.
    int PollJoin();
    void Leave(int player);
    int Count() const { return (int)m_players.size(); }
    bool Active(int player) const { return player >= 0 && player < (int)m_players.size() && m_players[(size_t)player] != nullptr; }
    ActionSet* Player(int player) { return Active(player) ? &m_players[(size_t)player]->set : nullptr; }
    const ActionSet* Player(int player) const { return Active(player) ? &m_players[(size_t)player]->set : nullptr; }
    JoinDevice DeviceOf(int player) const { return Active(player) ? m_players[(size_t)player]->device : JoinDevice::Keyboard; }
    int PadOf(int player) const { return Active(player) ? m_players[(size_t)player]->pad : -1; }
    /// A number that changes each time a player joins in this slot (0 = no player), so a new join can be told apart.
    unsigned Serial(int player) const { return Active(player) ? m_players[(size_t)player]->serial : 0u; }
    bool KeyboardPaired() const;
    bool PadPaired(int pad) const;
    /// Update every player's actions (after Input was fed).
    void Update(float dt);
    /// Overrides are saved per player: Prefs "inputov.player<N>".
    void SaveOverrides(int player) const;
    void LoadOverrides(int player);
private:
    struct Slot { ActionSet set; JoinDevice device = JoinDevice::Keyboard; int pad = -1; unsigned serial = 0; };
    ActionSet m_asset;
    unsigned m_joins = 0;
    std::vector<std::unique_ptr<Slot>> m_players;
};

} // namespace okay
