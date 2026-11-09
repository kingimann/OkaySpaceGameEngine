#include "okay/Core/Undo.hpp"

#include <algorithm>

namespace okay {

// ---- CompositeCommand --------------------------------------------------------

bool CompositeCommand::Do() {
    for (std::size_t i = 0; i < m_children.size(); ++i) {
        if (!m_children[i]->Do()) {
            // Roll back what was applied so the group is never left half done.
            for (std::size_t j = i; j-- > 0;) m_children[j]->Undo();
            return false;
        }
    }
    return true;
}

bool CompositeCommand::Undo() {
    for (std::size_t i = m_children.size(); i-- > 0;) {
        if (!m_children[i]->Undo()) {
            for (std::size_t j = i + 1; j < m_children.size(); ++j) m_children[j]->Do();
            return false;
        }
    }
    return true;
}

std::size_t CompositeCommand::MemoryBytes() const {
    std::size_t n = sizeof(*this) + m_name.capacity();
    for (const auto& c : m_children) n += c->MemoryBytes();
    return n;
}

bool CompositeCommand::IsNoOp() const {
    for (const auto& c : m_children) if (!c->IsNoOp()) return false;
    return true;
}

// ---- UndoStack ---------------------------------------------------------------

UndoStack::UndoStack() : m_token(std::make_shared<int>(0)) {}

bool UndoStack::Refuse(const char* why) {
    m_lastError = why;
    return false;
}

void UndoStack::Notify(Event e, const std::string& name) {
    ++m_revision;
    if (m_listeners.empty()) return;
    const auto copy = m_listeners;   // a listener may remove itself
    const Notice n{e, name};
    BusyScope busy(*this);
    for (const auto& l : copy) if (l.second) l.second(n);
}

int UndoStack::AddListener(Listener l) {
    if (!l) return 0;
    const int id = m_nextListener++;
    m_listeners.emplace_back(id, std::move(l));
    return id;
}

void UndoStack::RemoveListener(int id) {
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(),
                                     [id](const auto& p) { return p.first == id; }),
                      m_listeners.end());
}

bool UndoStack::Execute(std::unique_ptr<ICommand> cmd) {
    if (!cmd) return Refuse("null command");
    if (m_busy) return Refuse("undo stack is busy (edit made while applying a command)");
    bool ok;
    {
        BusyScope busy(*this);
        ok = cmd->Do();
    }
    if (!ok) { m_lastError = "command failed: " + cmd->Name(); return false; }
    return Accept(std::move(cmd));
}

bool UndoStack::Record(std::unique_ptr<ICommand> cmd) {
    if (!cmd) return Refuse("null command");
    if (m_busy) return Refuse("undo stack is busy (edit made while applying a command)");
    return Accept(std::move(cmd));
}

bool UndoStack::Accept(std::unique_ptr<ICommand> cmd) {
    if (!m_groups.empty()) { AddToGroup(m_groups.back(), std::move(cmd)); return true; }
    Commit(std::move(cmd));
    return true;
}

void UndoStack::AddToGroup(Group& g, std::unique_ptr<ICommand> cmd) {
    if (cmd->IsNoOp()) return;
    // Two edits to the same thing inside one transaction are one edit.
    if (!g.children.empty() && g.children.back()->Merge(*cmd)) return;
    g.children.push_back(std::move(cmd));
}

void UndoStack::DiscardRedo() {
    if (m_redo.empty()) return;
    // The save point may live in the branch that is being thrown away.
    if (m_savePos != kLost && m_savePos > m_undo.size()) m_savePos = kLost;
    m_redo.clear();
}

void UndoStack::Commit(std::unique_ptr<ICommand> cmd) {
    if (cmd->IsNoOp()) return;
    DiscardRedo();
    const bool windowOk = m_mergeWindow <= 0.0f || m_sinceLast <= m_mergeWindow;
    if (m_mergeOpen && windowOk && !m_undo.empty() && m_undo.back().cmd->Merge(*cmd)) {
        Entry& top = m_undo.back();
        top.bytes = top.cmd->MemoryBytes();
        const std::string name = top.cmd->Name();
        // MarkSaved seals the step it saved, so a merge never touches the step the save point
        // sits on. If the merge leaves a no-op, popping it can land exactly on the save point.
        m_sinceLast = 0.0f;
        if (top.cmd->IsNoOp()) {          // dragged away and back: nothing left to undo
            m_undo.pop_back();
            m_mergeOpen = false;
        }
        Notify(Event::Merged, name);
        return;
    }
    const std::string name = cmd->Name();
    Entry e;
    e.bytes = cmd->MemoryBytes();
    e.cmd = std::move(cmd);
    m_undo.push_back(std::move(e));
    m_mergeOpen = true;
    m_sinceLast = 0.0f;
    Evict();
    Notify(Event::Pushed, name);
}

void UndoStack::Evict() {
    if (m_undo.size() <= 1) return;
    std::size_t mem = 0;
    for (const auto& e : m_undo) mem += e.bytes;
    std::vector<std::string> gone;
    while (m_undo.size() > 1 &&
           ((m_maxCommands && m_undo.size() > m_maxCommands) || (m_maxMemory && mem > m_maxMemory))) {
        mem -= m_undo.front().bytes;
        gone.push_back(m_undo.front().cmd->Name());
        m_undo.pop_front();
    }
    if (gone.empty()) return;
    const std::size_t n = gone.size();
    if (m_savePos != kLost) m_savePos = m_savePos < n ? kLost : m_savePos - n;
    for (const auto& g : gone) Notify(Event::Evicted, g);
}

bool UndoStack::Undo() {
    if (m_busy) return Refuse("undo stack is busy");
    if (!m_groups.empty()) return Refuse("a group is open");
    if (m_undo.empty()) return Refuse("nothing to undo");
    const std::size_t oldSize = m_undo.size();
    Entry e = std::move(m_undo.back());
    m_undo.pop_back();
    m_mergeOpen = false;
    const std::string name = e.cmd->Name();
    bool ok;
    {
        BusyScope busy(*this);
        ok = e.cmd->Undo();
    }
    if (!ok) {
        // Stale (its target is gone): drop it. What lies beyond can't be trusted.
        m_lastError = "can't undo '" + name + "' (target changed or missing); step dropped";
        m_redo.clear();
        if (m_savePos != kLost && m_savePos >= oldSize) m_savePos = kLost;
        Notify(Event::Failed, name);
        return false;
    }
    m_redo.push_back(std::move(e));
    Notify(Event::Undone, name);
    return true;
}

bool UndoStack::Redo() {
    if (m_busy) return Refuse("undo stack is busy");
    if (!m_groups.empty()) return Refuse("a group is open");
    if (m_redo.empty()) return Refuse("nothing to redo");
    Entry e = std::move(m_redo.back());
    m_redo.pop_back();
    m_mergeOpen = false;
    const std::string name = e.cmd->Name();
    bool ok;
    {
        BusyScope busy(*this);
        ok = e.cmd->Do();
    }
    if (!ok) {
        m_lastError = "can't redo '" + name + "' (target changed or missing); step dropped";
        m_redo.clear();
        if (m_savePos != kLost && m_savePos > m_undo.size()) m_savePos = kLost;
        Notify(Event::Failed, name);
        return false;
    }
    m_undo.push_back(std::move(e));
    Notify(Event::Redone, name);
    return true;
}

std::size_t UndoStack::UndoMany(std::size_t n) {
    std::size_t done = 0;
    while (done < n && Undo()) ++done;
    return done;
}

std::size_t UndoStack::RedoMany(std::size_t n) {
    std::size_t done = 0;
    while (done < n && Redo()) ++done;
    return done;
}

// ---- Groups ------------------------------------------------------------------

bool UndoStack::BeginGroup(const std::string& name) {
    if (m_busy) return Refuse("undo stack is busy");
    Group g;
    g.name = name;
    m_groups.push_back(std::move(g));
    return true;
}

bool UndoStack::EndGroup() {
    if (m_busy) return Refuse("undo stack is busy");
    if (m_groups.empty()) return Refuse("no group is open");
    Group g = std::move(m_groups.back());
    m_groups.pop_back();
    if (g.children.empty()) return true;   // an empty transaction records nothing

    std::unique_ptr<ICommand> cmd;
    if (g.children.size() == 1 && g.name.empty()) {
        cmd = std::move(g.children.front());   // keeps single edits mergeable
    } else {
        std::string name = g.name;
        if (name.empty()) name = g.children.front()->Name();
        auto comp = std::make_unique<CompositeCommand>(name);
        for (auto& c : g.children) comp->Add(std::move(c));
        cmd = std::move(comp);
    }
    if (!m_groups.empty()) {
        if (!cmd->IsNoOp()) m_groups.back().children.push_back(std::move(cmd));
    } else {
        Commit(std::move(cmd));
    }
    return true;
}

bool UndoStack::CancelGroup() {
    if (m_busy) return Refuse("undo stack is busy");
    if (m_groups.empty()) return Refuse("no group is open");
    Group g = std::move(m_groups.back());
    m_groups.pop_back();
    bool ok = true;
    BusyScope busy(*this);
    for (std::size_t i = g.children.size(); i-- > 0;) {
        if (!g.children[i]->Undo()) { ok = false; m_lastError = "can't roll back '" + g.children[i]->Name() + "'"; }
    }
    return ok;
}

// ---- Dirty tracking ----------------------------------------------------------

void UndoStack::MarkSaved() {
    m_savePos = m_undo.size();
    m_mergeOpen = false;   // later edits must not fold into a step that is on disk
    Notify(Event::SavePoint, "");
}

void UndoStack::MarkDirty() {
    m_savePos = kLost;
    Notify(Event::SavePoint, "");
}

bool UndoStack::IsDirty() const {
    for (const auto& g : m_groups) if (!g.children.empty()) return true;
    return m_savePos != m_undo.size();
}

// ---- History -----------------------------------------------------------------

std::string UndoStack::NextUndoName() const {
    return m_undo.empty() ? std::string() : m_undo.back().cmd->Name();
}

std::string UndoStack::NextRedoName() const {
    return m_redo.empty() ? std::string() : m_redo.back().cmd->Name();
}

std::vector<std::string> UndoStack::UndoNames(std::size_t max) const {
    std::vector<std::string> v;
    for (auto it = m_undo.rbegin(); it != m_undo.rend(); ++it) {
        if (max && v.size() >= max) break;
        v.push_back(it->cmd->Name());
    }
    return v;
}

std::vector<std::string> UndoStack::RedoNames(std::size_t max) const {
    std::vector<std::string> v;
    for (auto it = m_redo.rbegin(); it != m_redo.rend(); ++it) {
        if (max && v.size() >= max) break;
        v.push_back(it->cmd->Name());
    }
    return v;
}

std::size_t UndoStack::MemoryBytes() const {
    std::size_t n = 0;
    for (const auto& e : m_undo) n += e.bytes;
    for (const auto& e : m_redo) n += e.bytes;
    return n;
}

void UndoStack::Clear() {
    if (m_busy) { Refuse("undo stack is busy"); return; }
    m_undo.clear();
    m_redo.clear();
    m_groups.clear();
    m_savePos = 0;
    m_mergeOpen = false;
    m_sinceLast = 0.0f;
    Notify(Event::Cleared, "");
}

} // namespace okay
