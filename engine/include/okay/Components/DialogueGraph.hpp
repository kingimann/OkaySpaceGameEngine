#pragma once
// Dialogue graphs: a `.okaydialog` text asset of nodes a conversation walks
// through (lines, choices, conditions, story-variable sets, events, item and
// quest nodes), and the session that walks it. The session knows nothing about
// the scene: a DialogueHost supplies the story variables, the player's items and
// the quests, so the same graph plays in a scene (DialogueRunner) and in the
// editor's Test panel.
#include "okay/VisualScript/VsValue.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// A test on the story: a variable compare, a quest's state, an item count or a flag.
struct DialogueCondition {
    enum class Kind { None, StoryVar, QuestState, HasItem, Flag };
    Kind kind = Kind::None;
    std::string name;        ///< the variable / quest id / item id / flag
    std::string op = "==";   ///< StoryVar: == != < <= > >=   Flag: == (set) or != (clear)
    std::string value;       ///< StoryVar: the compared value; QuestState: inactive / active / completed / failed
    int count = 1;           ///< HasItem: at least this many

    bool Empty() const { return kind == Kind::None; }
    /// One line of text for labels: "gold >= 10", "flag met", "quest q1 active", "item key x1".
    std::string Text() const;
};

struct DialogueChoice {
    std::string text;
    DialogueCondition condition;   ///< shown only while it passes (None = always)
    int target = -1;               ///< the node to go to (-1 ends the conversation)
};

struct DialogueNode {
    enum class Kind { Line, Choice, Condition, Set, Event, GiveItem, TakeItem, StartQuest, CompleteObjective, Jump, End };
    int id = 0;
    Kind kind = Kind::Line;
    float x = 0.0f, y = 0.0f;      ///< where the editor draws it
    int next = -1;                 ///< Line, Set, Event, item and quest nodes, Jump: the node after this one
    // Line
    std::string speaker, text, portrait, sound;
    float autoAdvance = 0.0f;      ///< seconds before moving on (0 = wait for confirm)
    // Choice (2 to 6 options)
    std::vector<DialogueChoice> choices;
    // Condition
    DialogueCondition condition;
    int onTrue = -1, onFalse = -1;
    // Set: scope 0 = story variable, 1 = flag (value 1 sets, 0 clears), 2 = a graph local
    int setScope = 0;
    std::string name;              ///< Set: the variable / flag; Event: the event name; quest nodes: the quest id
    std::string value;             ///< Set: the value as typed (a number, text or true / false)
    // GiveItem / TakeItem
    std::string item;
    int count = 1;
    // CompleteObjective
    int objective = 0;

    /// Which output slots the node has: Line/Set/... one (next), Choice one per
    /// option, Condition two (true, false), End none.
    int OutputCount() const;
    int  Output(int i) const;
    void SetOutput(int i, int target);
    static const char* OutputName(Kind k, int i);

    static const char* KindName(Kind k);
    static const char* KindLabel(Kind k);
    static bool KindFromName(const std::string& s, Kind& out);
};

struct DialogueVariable { std::string name, value; };

/// One conversation graph. Nodes are referenced by id (stable across edits).
class DialogueGraph {
public:
    std::string name;
    int start = 0;                          ///< the first node's id
    std::vector<std::string> speakers;      ///< the speaker dropdown in the editor
    std::vector<DialogueVariable> locals;   ///< per-graph variables and their starting values
    std::vector<DialogueNode> nodes;

    const DialogueNode* Find(int id) const;
    DialogueNode* Find(int id);
    int IndexOf(int id) const;
    int NewId() const;
    DialogueNode& Add(DialogueNode::Kind kind, float x, float y);
    /// Remove a node and every link to it.
    void Remove(int id);

    struct Issue { int node = -1; int severity = 1; std::string text; };   ///< severity 1 warning, 2 error
    /// Problems: missing or unknown targets, unreachable nodes, empty lines and
    /// choices, choice counts, unknown items (when the item database knows any)
    /// and unknown quests (when `knownQuests` is given).
    std::vector<Issue> Validate(const std::vector<std::string>* knownQuests = nullptr,
                                const std::vector<std::string>* knownItems = nullptr) const;
    static bool HasErrors(const std::vector<Issue>& issues);

    bool Parse(const std::string& text, std::string* err = nullptr);
    std::string Write() const;
    bool Load(const std::string& path, std::string* err = nullptr);
    bool Save(const std::string& path) const;
};

/// The string-table keys a conversation's text is looked up under; the text typed
/// in the graph shows while the table has none (docs/localization.md):
/// dlg.<graph>.<node> for a line, dlg.<graph>.<node>.<n> for its choice n (from 1),
/// speaker.<name> for a speaker.
std::string DialogueLineKey(const std::string& graph, int node);
std::string DialogueChoiceKey(const std::string& graph, int node, int choice);
std::string DialogueSpeakerKey(const std::string& speaker);
/// Every line, choice and speaker of a graph as (key, text), to add to a string table.
std::vector<std::pair<std::string, std::string>> DialogueStrings(const DialogueGraph& graph, const std::string& graphName);

/// A value as a dialogue file types it: "10" and "-2.5" are numbers, "true" /
/// "false" are bools, anything else is text.
vs::VsValue DialogueValueFromText(const std::string& text);
std::string DialogueValueText(const vs::VsValue& v);
/// `a op b` with the usual operators; numbers compare numerically, text by equality.
bool DialogueCompare(const vs::VsValue& a, const std::string& op, const vs::VsValue& b);

/// What a session asks the game for. Every method has a harmless default, so a
/// host implements only what it has (the editor's Test panel keeps its own maps).
struct DialogueHost {
    virtual ~DialogueHost() = default;
    virtual vs::VsValue GetVar(const std::string& /*name*/) { return {}; }
    virtual void SetVar(const std::string& /*name*/, const vs::VsValue& /*v*/) {}
    virtual bool HasFlag(const std::string& /*name*/) { return false; }
    virtual void SetFlag(const std::string& /*name*/, bool /*on*/) {}
    virtual std::string QuestState(const std::string& /*id*/) { return "inactive"; }
    virtual int  ItemCount(const std::string& /*item*/) { return 0; }
    virtual bool GiveItem(const std::string& /*item*/, int /*count*/) { return false; }
    virtual bool TakeItem(const std::string& /*item*/, int /*count*/) { return false; }
    virtual void StartQuest(const std::string& /*id*/) {}
    virtual void CompleteObjective(const std::string& /*id*/, int /*index*/) {}
    virtual void Event(const std::string& /*name*/) {}
    virtual void Sound(const std::string& /*path*/) {}
};

/// Walks a graph: Begin at the start node, then Advance past each line and Choose
/// an option at each choice. Nodes that show nothing (conditions, sets, events,
/// items, quests, jumps) run on the way to the next line or choice.
class DialogueSession {
public:
    void Begin(const DialogueGraph* graph, DialogueHost* host, int startId = -1);
    /// Move past the current line. Does nothing at a choice.
    void Advance();
    /// Pick one of the visible choices (an index into Choices()).
    bool Choose(int index);
    void Stop();

    bool Active() const { return m_active; }
    /// True once a conversation ended (End node, a missing target or Stop) until the next Begin.
    bool Ended() const { return m_ended; }
    /// The line or choice node being shown (null while inactive).
    const DialogueNode* Node() const { return m_node; }
    bool AtChoice() const { return m_node && m_node->kind == DialogueNode::Kind::Choice; }
    /// The choices whose conditions pass, in graph order.
    const std::vector<DialogueChoice>& Choices() const { return m_choices; }
    /// Nodes shown so far this conversation (ids), for transcripts and tests.
    const std::vector<int>& Trail() const { return m_trail; }
    /// The graph's per-conversation variables.
    std::map<std::string, vs::VsValue>& Locals() { return m_locals; }
    bool Test(const DialogueCondition& c);

private:
    void Run(int id);
    void Present(const DialogueNode* n);
    void Finish();
    vs::VsValue Var(const std::string& name);

    const DialogueGraph* m_graph = nullptr;
    DialogueHost* m_host = nullptr;
    const DialogueNode* m_node = nullptr;
    std::vector<DialogueChoice> m_choices;
    std::vector<int> m_trail;
    std::map<std::string, vs::VsValue> m_locals;
    bool m_active = false, m_ended = false;
};

} // namespace okay
