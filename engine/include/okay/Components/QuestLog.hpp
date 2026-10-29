#pragma once
// Quests: definitions from a `.okayquest` text file (or typed into the Quest Log
// in the inspector), one Quest Log per scene that tracks their state and
// objective progress (saved with the scene, so save games keep them), and a
// Quest Trigger zone that completes "reach" objectives. Objectives complete
// from their sources: a dialogue ending (talk), the player's inventory (collect),
// a Health Stat dying (kill), a Quest Trigger (reach) or a script (custom).
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay {

struct QuestObjective {
    enum class Kind { Talk, Collect, Kill, Reach, Custom };
    Kind kind = Kind::Custom;
    std::string description;
    /// Talk: the NPC object or dialogue graph name; Collect: the item id; Kill: the tag
    /// (or name) of what dies; Reach: the Quest Trigger's zone name; Custom: free text.
    std::string target;
    int count = 1;            ///< Collect / Kill: how many
    bool optional = false;    ///< not needed to complete the quest

    static const char* KindName(Kind k);
    static const char* KindLabel(Kind k);
    static bool KindFromName(const std::string& s, Kind& out);
};

struct QuestReward {
    std::vector<std::pair<std::string, int>> items;          ///< item id, count
    std::vector<std::pair<std::string, std::string>> vars;   ///< story variable, value as typed
    std::string event;                                       ///< on_dialogue_event-style script event (empty = none)
    bool Empty() const { return items.empty() && vars.empty() && event.empty(); }
};

struct QuestDef {
    std::string id;
    std::string title;
    std::string description;
    std::vector<std::string> prerequisites;   ///< quest ids that must be completed first
    std::vector<QuestObjective> objectives;
    QuestReward reward;
    bool repeatable = false;
};

enum class QuestStatus { Inactive, Active, Completed, Failed };
const char* QuestStatusName(QuestStatus s);
bool QuestStatusFromName(const std::string& s, QuestStatus& out);

/// The `.okayquest` text format: one `quest` line, then its `objective` and
/// `reward` lines, for each quest.
struct QuestFile {
    static bool Parse(const std::string& text, std::vector<QuestDef>& out, std::string* err = nullptr);
    static std::string Write(const std::vector<QuestDef>& quests);
    static bool Load(const std::string& path, std::vector<QuestDef>& out, std::string* err = nullptr);
    static bool Save(const std::string& path, const std::vector<QuestDef>& quests);

    struct Issue { std::string quest; int severity = 1; std::string text; };   ///< 1 warning, 2 error
    static std::vector<Issue> Validate(const std::vector<QuestDef>& quests,
                                       const std::vector<std::string>* knownItems = nullptr);
};

/// One quest's live state.
struct QuestRecord {
    std::string id;
    QuestStatus status = QuestStatus::Inactive;
    std::vector<int> progress;   ///< per objective: items collected / kills so far
    std::vector<int> done;       ///< per objective: 1 once complete
    int completions = 0;         ///< how many times it was completed (repeatable quests)
};

class QuestLog final : public Behaviour {
public:
    std::string questFile;           ///< a .okayquest file (project-relative); loaded at Start and on demand
    std::vector<QuestDef> quests;    ///< definitions kept in the scene (the inspector's inline quests)
    bool showTracker = true;         ///< the active-quest overlay (top right)
    bool allowJournal = true;        ///< the Journal window on journalKey
    char journalKey = 'j';
    bool showNotifications = true;   ///< toasts on start / progress / completion
    bool drawUI = true;              ///< off: a game draws its own from the events and the records
    std::string tracked;             ///< the quest the tracker shows (empty = every active quest)
    std::string playerName;          ///< whose inventory Collect counts (empty = the player)

    std::vector<QuestRecord> records;   ///< runtime state; saved with the scene
    bool journalOpen = false;

    /// Told about every notification (title, body, kind 0 info / 1 success / 2 warning).
    std::function<void(const std::string&, const std::string&, int)> onNotify;
    /// The last notifications, newest last (tests, custom UIs).
    std::vector<std::string> notifications;

    /// The scene's Quest Log; with `create`, one is added to a "Quests" object when missing.
    static QuestLog* Get(Scene* scene, bool create);

    int ExecutionOrder() const override { return 10; }   ///< after dialogues and pickups this frame
    void Awake() override;
    void Start() override;
    void Update(float dt) override;

    // ---- Definitions ----
    bool LoadFile(std::string* err = nullptr);
    std::string ResolvedFile() const;
    const QuestDef* Def(const std::string& id) const;
    std::vector<const QuestDef*> Defs() const;
    std::vector<std::string> Ids() const;

    // ---- State ----
    QuestStatus Status(const std::string& id) const;
    const char* StatusName(const std::string& id) const { return QuestStatusName(Status(id)); }
    bool IsActive(const std::string& id) const { return Status(id) == QuestStatus::Active; }
    bool IsCompleted(const std::string& id) const { return Status(id) == QuestStatus::Completed; }
    /// False (with the reason) when the quest is unknown, already active, finished
    /// and not repeatable, or a prerequisite is not completed.
    bool CanStart(const std::string& id, std::string* why = nullptr) const;
    bool StartQuest(const std::string& id);
    bool CompleteQuest(const std::string& id);
    bool FailQuest(const std::string& id);
    /// Back to inactive with no progress.
    void ResetQuest(const std::string& id);
    bool CompleteObjective(const std::string& id, int index);
    bool AddProgress(const std::string& id, int index, int amount);
    int  Progress(const std::string& id, int index) const;
    bool ObjectiveDone(const std::string& id, int index) const;
    const QuestRecord* Record(const std::string& id) const;
    std::vector<std::string> ActiveIds() const;

    // ---- Objective sources ----
    void NotifyKill(GameObject* victim);
    void NotifyReach(const std::string& zone, GameObject* who);
    void NotifyDialogueEnd(const std::string& npc, const std::string& graph);

    void ToggleJournal() { journalOpen = !journalOpen; }

private:
    QuestRecord& RecordFor(const std::string& id);
    void CheckCompletion(const std::string& id);
    void GrantRewards(const QuestDef& q);
    void Notify(const std::string& title, const std::string& body, int kind);
    void FireEvent(const char* scriptEvent, const char* vsEntry, const std::string& id, float value);
    int  CountItem(const std::string& item) const;
    GameObject* Player() const;
    void DrawTracker();
    void DrawJournal();
    static void InstallDeathHook();

    std::vector<QuestDef> m_fileQuests;
    bool m_fileLoaded = false;
    std::string m_loadedPath;
    int m_journalTab = 0;
};

/// A zone that completes "reach" objectives named after it (or one objective it
/// names directly). Put it on an object with a trigger collider.
class QuestTrigger final : public Behaviour {
public:
    std::string zone;                   ///< what Reach objectives target (empty = this object's name)
    std::string triggerTag = "Player";  ///< who fires it (tag or name text; empty = anyone)
    bool once = true;
    std::string questId;                ///< optionally complete this quest's objective directly
    int objective = -1;

    void OnTriggerEnter2D(Collider2D* o) override { if (o) Fire(o->gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { if (o) Fire(o->gameObject); }
    void OnCollisionEnter2D(const Collision2D& c) override { Fire(c.gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { Fire(c.gameObject); }

    std::string ZoneName() const { return zone.empty() && gameObject ? gameObject->name : zone; }
    void Fire(GameObject* who);
    bool Fired() const { return m_fired; }
    void Reset() { m_fired = false; }

private:
    bool m_fired = false;
};

} // namespace okay
