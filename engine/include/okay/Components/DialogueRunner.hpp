#pragma once
// Dialogue Runner: put it on an NPC or an object, point it at a `.okaydialog`
// graph, and the player talks to it with the interact key in range (E, like the
// pickups) or a script starts it. It walks the graph with a DialogueSession,
// pauses the player's controller while a conversation runs, draws the built-in
// dialogue box (speaker, typewriter text, portrait slot, choice buttons) through
// the host's OkayUI bridge in the launcher look, and tells scripts what happens
// (on_dialogue_event(name), on_dialogue_end()).
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

class DialogueRunner final : public Behaviour {
public:
    std::string graphFile;              ///< the .okaydialog (project-relative)
    int   startMode = 0;                ///< 0: the interact key within range; 1: scripts only
    char  interactKey = 'e';
    float interactRange = 3.0f;
    std::string promptText;             ///< the prompt while in range ("" = "<key> to talk")
    std::string promptObject = "Hold Prompt";   ///< the Hold Prompt label the prompt shows on (when the scene has one)
    bool  pausePlayer = true;           ///< controllers pause look and movement while talking
    float typewriterSpeed = 40.0f;      ///< characters per second (0 = whole line at once)
    float autoAdvance = 0.0f;           ///< seconds a line waits before moving on when the graph gives none (0 = confirm)
    bool  drawUI = true;                ///< off: a game draws its own box from CurrentLine() and the events
    bool  canCancel = true;             ///< Escape ends the conversation
    int   startNode = -1;               ///< the node to start at (-1 = the graph's start)
    std::string playerName;             ///< who talks (empty = the player: tag "Player" or the camera's subject)

    /// What the box shows: the speaker, the whole line and the part typed so far,
    /// the portrait path, and the visible choices at a choice node.
    struct Line {
        int nodeId = -1;
        std::string speaker, text, shownText, portrait;
        std::vector<std::string> choices;
        bool choice = false;      ///< waiting for a choice
        bool complete = false;    ///< the typewriter has shown the whole line
    };

    /// C++ hooks: a dialogue Event node, and the end of a conversation (tests, custom UIs).
    std::function<void(const std::string&)> onEvent;
    std::function<void()> onEnd;
    /// Events fired this conversation, in order.
    std::vector<std::string> events;

    int ExecutionOrder() const override { return -5; }   ///< before pickups and controllers: it takes E first
    void Start() override;
    void Update(float dt) override;
    void OnDisable() override { Stop(); }
    /// The scene may be going away: end the session without telling anyone.
    void OnDestroy() override { m_session.Stop(); m_wasActive = false; }

    // ---- Runtime API ----
    /// Start the conversation (loading the graph when needed). False when nothing could start.
    bool StartDialogue(int nodeId = -1);
    /// Move past the current line (first finishing the typewriter when it is still typing).
    void Advance();
    /// Pick a choice (0-based, into CurrentLine().choices).
    bool Choose(int index);
    void Stop();
    bool IsActive() const { return m_session.Active(); }
    Line CurrentLine() const;
    DialogueSession& Session() { return m_session; }

    // ---- The graph ----
    const DialogueGraph& Graph() const { return m_graph; }
    /// Use a graph built in code (tests, generated conversations) instead of graphFile.
    void SetGraph(const DialogueGraph& g) { m_graph = g; m_loaded = true; m_loadedPath.clear(); }
    bool LoadGraph(std::string* err = nullptr);
    std::string ResolvedGraphFile() const;
    /// The graph's name, else the file's stem (what "talk to" objectives may name).
    std::string GraphName() const;

    /// The player is within interactRange.
    bool InRange() const;
    GameObject* Player() const;

    /// The runner with a conversation running in `scene`, if any.
    static DialogueRunner* ActiveIn(Scene* scene);
    /// The string-table keys of the box's and the prompt's own words with their
    /// English text; a key the table has replaces the English. The graph's lines,
    /// choices and speakers have keys of their own (DialogueLineKey...).
    static const std::vector<std::pair<std::string, std::string>>& LocKeys();
    /// A runner by its object's name, else by its graph name or file stem.
    static DialogueRunner* FindByName(Scene* scene, const std::string& name);

private:
    struct SceneHost final : DialogueHost {
        DialogueRunner* r = nullptr;
        vs::VsValue GetVar(const std::string& name) override;
        void SetVar(const std::string& name, const vs::VsValue& v) override;
        bool HasFlag(const std::string& name) override;
        void SetFlag(const std::string& name, bool on) override;
        std::string QuestState(const std::string& id) override;
        int  ItemCount(const std::string& item) override;
        bool GiveItem(const std::string& item, int count) override;
        bool TakeItem(const std::string& item, int count) override;
        void StartQuest(const std::string& id) override;
        void CompleteObjective(const std::string& id, int index) override;
        void Event(const std::string& name) override;
        void Sound(const std::string& path) override;
    };

    void Present();
    void Ended();
    void HandleInput();
    void DrawUI();
    void SetPrompt(const std::string& text);
    std::string ResolveAsset(const std::string& path) const;

    DialogueGraph m_graph;
    bool m_loaded = false;
    std::string m_loadedPath;
    DialogueSession m_session;
    SceneHost m_host;
    float m_shown = 0.0f;      ///< characters typed so far
    float m_lineTime = 0.0f;   ///< seconds since the line finished typing
    int   m_lineId = -1;
    bool  m_wasActive = false;
    std::string m_promptShown;
};

/// True while a built-in story overlay on `go` owns the pointer (a running dialogue
/// or an open quest journal). The hosts free the cursor for it, as for an open bag.
bool StoryUIModal(const GameObject* go);

} // namespace okay
