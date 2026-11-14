#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Core/Log.hpp"
#include <string>

namespace okay {

/// Saves the whole game (SaveGame) to a slot every `interval` seconds of play,
/// and optionally as soon as the scene starts (a checkpoint when a level loads).
/// Drop one in a scene; nothing else to set up.
class AutoSave : public Behaviour {
public:
    float interval = 120.0f;          ///< seconds between saves (0 = only on start)
    std::string slot = "autosave";
    std::string label = "Autosave";
    bool saveOnStart = false;

    void Start() override {
        m_timer = 0.0f;
        if (saveOnStart) SaveNow();
    }
    void Update(float dt) override {
        if (interval <= 0.0f) return;
        m_timer += dt;
        if (m_timer >= interval) { m_timer = 0.0f; SaveNow(); }
    }
    /// Save to the slot now (also restarts the countdown).
    bool SaveNow() {
        m_timer = 0.0f;
        Scene* s = GetScene();
        std::string err;
        if (!s || !SaveGame::Save(*s, slot, label, &err)) { Log::Warning("Autosave failed: ", err); return false; }
        return true;
    }
    float SecondsUntilSave() const { return interval > 0.0f ? interval - m_timer : 0.0f; }

private:
    float m_timer = 0.0f;
};

} // namespace okay
