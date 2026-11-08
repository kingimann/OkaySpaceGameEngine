#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// One undoable edit. The stack calls Do() the first time (Execute) and again on
/// redo, Undo() on undo. Both return false if the edit can't be applied (the
/// target is gone after a scene reload, a parse failed ...); the stack then drops
/// the command as stale instead of corrupting the history.
///
/// A command never touches the stack itself: edits made from inside Do()/Undo()
/// are ignored by the stack (see UndoStack::IsApplying).
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual bool Do() = 0;
    virtual bool Undo() = 0;
    /// Short label for the Edit menu / history list ("Move Cube", "Set Intensity").
    virtual std::string Name() const = 0;
    /// Absorb `next` (a command recorded right after this one) into this one, so
    /// a slider drag becomes one undo step. Return true if it was absorbed; the
    /// stack then drops `next`. After a merge Undo() must still restore the state
    /// from before THIS command and Do() the state after `next`.
    virtual bool Merge(const ICommand& next) { (void)next; return false; }
    /// Approximate bytes this command keeps alive (for the stack's memory cap).
    virtual std::size_t MemoryBytes() const { return 64; }
    /// True when applying the command would change nothing (a drag that ended
    /// where it began). The stack doesn't record such commands.
    virtual bool IsNoOp() const { return false; }
};

/// Several commands that undo and redo as one step. Undo runs the children in
/// reverse order, Do in order. Both are all-or-nothing: if a child fails half way
/// the ones already applied are rolled back, so a group is never left half done.
class CompositeCommand : public ICommand {
public:
    explicit CompositeCommand(std::string name = "") : m_name(std::move(name)) {}
    void Add(std::unique_ptr<ICommand> c) { if (c) m_children.push_back(std::move(c)); }
    std::size_t Size() const { return m_children.size(); }
    const ICommand* Child(std::size_t i) const { return i < m_children.size() ? m_children[i].get() : nullptr; }
    void SetName(std::string n) { m_name = std::move(n); }

    bool Do() override;
    bool Undo() override;
    std::string Name() const override { return m_name; }
    std::size_t MemoryBytes() const override;
    bool IsNoOp() const override;

private:
    std::string m_name;
    std::vector<std::unique_ptr<ICommand>> m_children;
};

/// Command based undo/redo history. Headless: it knows nothing about scenes or
/// UI, so the editor, tools and tests can all use it.
///
///   UndoStack undo;
///   undo.Execute(std::make_unique<SetProperty<float>>("Set Speed", &speed, speed, 9.0f));
///
///   // a slider: the widget already changed the value, so Record, not Execute.
///   float before = v;
///   if (Slider(&v)) undo.Record(std::make_unique<SetProperty<float>>("Set Speed", &v, before, v));
///   if (SliderReleased()) undo.BreakMerge();   // the next drag is a new step
///
///   undo.BeginGroup("Delete 3 objects"); ... undo.EndGroup();   // one step
///
/// Not thread safe: use it from the thread that owns the edited data.
class UndoStack {
public:
    static constexpr std::size_t kDefaultMaxCommands = 200;
    static constexpr std::size_t kDefaultMaxMemory = 64u * 1024u * 1024u;

    enum class Event {
        Pushed,     ///< a new step was recorded
        Merged,     ///< an edit was folded into the newest step
        Undone,
        Redone,
        Failed,     ///< Undo/Redo hit a stale command and dropped it
        Evicted,    ///< the oldest step fell off the end (count or memory cap)
        Cleared,
        SavePoint,  ///< MarkSaved / MarkDirty changed the dirty state
    };
    struct Notice {
        Event event;
        std::string name;   ///< the command concerned ("" for Cleared / SavePoint)
    };
    using Listener = std::function<void(const Notice&)>;

    UndoStack();
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    // ---- Recording -----------------------------------------------------
    /// Apply `cmd` (Do) and record it. False if Do failed (nothing recorded) or
    /// the stack is busy.
    bool Execute(std::unique_ptr<ICommand> cmd);
    /// Record a command whose effect is ALREADY applied (a widget changed the
    /// value itself). Do() is not called.
    bool Record(std::unique_ptr<ICommand> cmd);

    // ---- Undo / redo ---------------------------------------------------
    bool CanUndo() const { return !m_undo.empty() && m_groups.empty(); }
    bool CanRedo() const { return !m_redo.empty() && m_groups.empty(); }
    /// Undo the newest step. False if there is nothing to undo, a group is open,
    /// or the command was stale (it is dropped and LastError() says why).
    bool Undo();
    bool Redo();
    /// Undo / redo up to `n` steps (a history panel: click the 3rd entry).
    /// Returns how many steps were done.
    std::size_t UndoMany(std::size_t n);
    std::size_t RedoMany(std::size_t n);

    // ---- Groups (transactions) -----------------------------------------
    /// Everything recorded until the matching EndGroup becomes ONE step. Groups
    /// nest; only the outermost one makes a history entry. Commands inside are
    /// applied immediately (Execute) or taken as already applied (Record).
    bool BeginGroup(const std::string& name);
    /// Close the innermost group. An empty group records nothing. False if no
    /// group is open.
    bool EndGroup();
    /// Roll back everything done in the innermost group (reverse order) and
    /// discard it. False if no group is open or a child couldn't be undone.
    bool CancelGroup();
    int GroupDepth() const { return (int)m_groups.size(); }

    // ---- Merging (slider drags) ----------------------------------------
    /// The next command starts a new step even if it could merge (call when the
    /// mouse is released). MarkSaved / Undo / Redo / Clear do this too.
    void BreakMerge() { m_mergeOpen = false; }
    /// With a window > 0 a command only merges if it arrives within `seconds`
    /// (of the previous one); drive it with Tick(). 0 = no time limit.
    void SetMergeWindow(float seconds) { m_mergeWindow = seconds; }
    float MergeWindow() const { return m_mergeWindow; }
    void Tick(float dt) { if (dt > 0.0f) m_sinceLast += dt; }

    // ---- Capacity ------------------------------------------------------
    /// Oldest steps are dropped beyond these (0 = unlimited). The newest step is
    /// always kept, even if it alone exceeds the memory cap.
    void SetMaxCommands(std::size_t n) { m_maxCommands = n; Evict(); }
    void SetMaxMemory(std::size_t bytes) { m_maxMemory = bytes; Evict(); }
    std::size_t MaxCommands() const { return m_maxCommands; }
    std::size_t MaxMemory() const { return m_maxMemory; }

    // ---- Dirty tracking ------------------------------------------------
    /// The current state is what is on disk. IsDirty() is false again whenever
    /// undo/redo lands back on it, and true if it can no longer be reached
    /// (its steps were evicted, merged into, or the redo branch was discarded).
    void MarkSaved();
    /// Changes the stack doesn't know about were made (an untracked edit):
    /// IsDirty() stays true until the next MarkSaved.
    void MarkDirty();
    bool IsDirty() const;

    // ---- History -------------------------------------------------------
    std::size_t UndoCount() const { return m_undo.size(); }
    std::size_t RedoCount() const { return m_redo.size(); }
    std::string NextUndoName() const;
    std::string NextRedoName() const;
    /// Names newest first (what Undo would undo first); at most `max` (0 = all).
    std::vector<std::string> UndoNames(std::size_t max = 0) const;
    std::vector<std::string> RedoNames(std::size_t max = 0) const;
    /// Bytes held by all recorded steps (undo and redo), as the commands report.
    std::size_t MemoryBytes() const;
    /// Bumps on every change; a UI can redraw only when it moves.
    std::uint64_t Revision() const { return m_revision; }
    void Clear();   ///< forget all history (after loading another scene); the state counts as saved

    // ---- Observers -----------------------------------------------------
    /// Called after every change. Listeners must not edit the stack (such calls
    /// are refused). Returns an id for RemoveListener.
    int AddListener(Listener l);
    void RemoveListener(int id);

    /// True while a command is being applied or a listener is running. Editor
    /// code that reacts to "property changed" can skip recording while this is set.
    bool IsApplying() const { return m_busy > 0; }
    const std::string& LastError() const { return m_lastError; }

    /// Observers that outlive the stack can hold this and test expired().
    std::weak_ptr<void> LifeToken() const { return m_token; }

private:
    struct Entry {
        std::unique_ptr<ICommand> cmd;
        std::size_t bytes = 0;
    };
    struct Group {
        std::string name;
        std::vector<std::unique_ptr<ICommand>> children;
    };
    struct BusyScope {
        explicit BusyScope(UndoStack& s) : st(s) { ++st.m_busy; }
        ~BusyScope() { --st.m_busy; }
        UndoStack& st;
    };

    bool Accept(std::unique_ptr<ICommand> cmd);
    void Commit(std::unique_ptr<ICommand> cmd);
    void AddToGroup(Group& g, std::unique_ptr<ICommand> cmd);
    void DiscardRedo();
    void Evict();
    void Notify(Event e, const std::string& name);
    bool Refuse(const char* why);

    static constexpr std::size_t kLost = static_cast<std::size_t>(-1);

    std::deque<Entry> m_undo;
    std::vector<Entry> m_redo;           // back() is the next redo
    std::vector<Group> m_groups;
    std::size_t m_maxCommands = kDefaultMaxCommands;
    std::size_t m_maxMemory = kDefaultMaxMemory;
    std::size_t m_savePos = 0;           // undo depth at the save point; kLost if unreachable
    bool m_mergeOpen = false;
    float m_mergeWindow = 0.0f;
    float m_sinceLast = 0.0f;
    int m_busy = 0;
    std::uint64_t m_revision = 0;
    std::string m_lastError;
    std::vector<std::pair<int, Listener>> m_listeners;
    int m_nextListener = 1;
    std::shared_ptr<int> m_token;
};

/// Scope helper: opens a group, closes it on scope exit (or rolls it back with
/// Cancel()).
///   { UndoGroup g(undo, "Paste"); ... }
class UndoGroup {
public:
    UndoGroup(UndoStack& s, const std::string& name) : m_stack(s), m_open(s.BeginGroup(name)) {}
    ~UndoGroup() { if (m_open) m_stack.EndGroup(); }
    UndoGroup(const UndoGroup&) = delete;
    UndoGroup& operator=(const UndoGroup&) = delete;
    /// Roll the group back and close it.
    bool Cancel() { if (!m_open) return false; m_open = false; return m_stack.CancelGroup(); }
    bool IsOpen() const { return m_open; }
private:
    UndoStack& m_stack;
    bool m_open;
};

} // namespace okay
