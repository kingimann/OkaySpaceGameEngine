#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include <string>
#include <vector>

namespace okay {

/// Drives turn order for a turn-based game (combat, board games). Holds an ordered
/// list of participant GameObject names; the active one takes its turn, then EndTurn
/// advances to the next (wrapping and bumping the round counter). When a turn begins
/// it calls on_turn_start() on that participant's ScriptComponent, and on_round_start()
/// on this object at the top of each round — so each unit acts when it's its turn.
class TurnManager : public Behaviour {
public:
    std::vector<std::string> participants;   // names, in turn order
    int  current = 0;                        // index of the active participant
    int  round = 1;
    bool autoStart = true;                   // fire the first turn on Start()

    void Start() override {
        if (autoStart && !participants.empty()) BeginTurn();
    }

    int Count() const { return (int)participants.size(); }

    /// The active participant's name (empty if none).
    const std::string& Current() const {
        static const std::string none;
        if (current < 0 || current >= Count()) return none;
        return participants[current];
    }
    GameObject* CurrentObject() const {
        Scene* s = GetScene();
        return (s && current >= 0 && current < Count()) ? s->Find(participants[current]) : nullptr;
    }

    /// End the active turn and advance to the next participant (new round on wrap).
    void EndTurn() {
        if (participants.empty()) return;
        ++current;
        if (current >= Count()) { current = 0; ++round; Event(gameObject, "on_round_start"); }
        BeginTurn();
    }

    /// Jump to a participant by index (e.g. an initiative re-order) and start its turn.
    void SetTurn(int index) {
        if (participants.empty()) return;
        current = (index % Count() + Count()) % Count();
        BeginTurn();
    }

    /// Remove a participant (e.g. a defeated unit); keeps the turn order sensible.
    void Remove(const std::string& name) {
        for (std::size_t i = 0; i < participants.size(); ++i) {
            if (participants[i] != name) continue;
            participants.erase(participants.begin() + i);
            if ((int)i < current) --current;
            if (current >= Count()) current = 0;
            break;
        }
    }

private:
    void BeginTurn() {
        Event(CurrentObject(), "on_turn_start");
    }
    void Event(GameObject* go, const char* name) {
        if (go)
            if (auto* sc = go->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent(name);
    }
};

} // namespace okay
