#pragma once
#include "okay/Core/Undo.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/// Ready-made commands for okay::UndoStack:
///   SetProperty<T>          set a value through a pointer or an accessor (merges drags)
///   ListEdit<T>             add / remove / move an item of a std::vector<T>
///   SnapshotCommand         before/after text; the bridge from whole-scene snapshots
///   SnapshotRecorder        turns "capture the scene text" into SnapshotCommands
///   CreateObjectCommand, DeleteObjectCommand, ReparentObjectCommand, SetObjectProperty<Obj,T>
///                           object edits that go through stable ids and a resolver,
///                           so they survive a scene reload
namespace okay {

namespace undo_detail {

template <class T, class = void> struct HasEq : std::false_type {};
template <class T>
struct HasEq<T, std::void_t<decltype(std::declval<const T&>() == std::declval<const T&>())>> : std::true_type {};

template <class T> bool Same(const T& a, const T& b) {
    if constexpr (HasEq<T>::value) return a == b; else { (void)a; (void)b; return false; }
}

template <class T> std::size_t Bytes(const T&) { return sizeof(T); }
inline std::size_t Bytes(const std::string& s) { return sizeof(std::string) + s.capacity(); }
template <class T> std::size_t Bytes(const std::vector<T>& v) { return sizeof(v) + v.capacity() * sizeof(T); }

} // namespace undo_detail

// ---- SetProperty<T> ------------------------------------------------------------

/// Change one value from `before` to `after`.
///
///   // pointer form: the target must outlive the command (long-lived settings)
///   SetProperty<float>("Set Speed", &speed, 1.0f, 9.0f)
///   // accessor form: capture an id and resolve it on every call (scene objects);
///   // the setter returns false if the target is gone
///   SetProperty<float>("Set Mass", [&](const float& v){ auto* o = Find(id); if (!o) return false; o->mass = v; return true; },
///                      1.0f, 9.0f, /*mergeKey*/ id * 31 + 7)
///
/// Consecutive commands with the same merge key fold into one (the first
/// `before`, the last `after`). The pointer form uses the address as the key
/// (pass mergeable=false to opt out); the accessor form merges only with a key != 0.
template <class T>
class SetProperty : public ICommand {
public:
    using Setter = std::function<bool(const T&)>;

    SetProperty(std::string name, T* target, T before, T after, bool mergeable = true)
        : m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after)),
          m_target(target), m_key(mergeable ? reinterpret_cast<std::uintptr_t>(target) : 0) {}

    SetProperty(std::string name, Setter set, T before, T after, std::uintptr_t mergeKey = 0)
        : m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after)),
          m_set(std::move(set)), m_key(mergeKey) {}

    bool Do() override { return Apply(m_after); }
    bool Undo() override { return Apply(m_before); }
    std::string Name() const override { return m_name; }
    bool IsNoOp() const override { return undo_detail::Same(m_before, m_after); }
    std::size_t MemoryBytes() const override {
        return sizeof(*this) + m_name.capacity() + undo_detail::Bytes(m_before) + undo_detail::Bytes(m_after);
    }
    bool Merge(const ICommand& next) override {
        const auto* o = dynamic_cast<const SetProperty<T>*>(&next);
        if (!o || m_key == 0 || o->m_key != m_key) return false;
        m_after = o->m_after;
        return true;
    }

    const T& Before() const { return m_before; }
    const T& After() const { return m_after; }

private:
    bool Apply(const T& v) {
        if (m_target) { *m_target = v; return true; }
        return m_set ? m_set(v) : false;
    }
    std::string m_name;
    T m_before, m_after;
    T* m_target = nullptr;
    Setter m_set;
    std::uintptr_t m_key = 0;
};

/// Apply `after` to *target now and record it. The stack does Do().
template <class T>
bool UndoSet(UndoStack& stack, const std::string& name, T* target, const T& after) {
    return stack.Execute(std::make_unique<SetProperty<T>>(name, target, *target, after));
}

/// The widget already wrote the new value into *target; `before` is what it held
/// a moment ago. Records without re-applying. Call stack.BreakMerge() on release.
template <class T>
bool UndoRecordSet(UndoStack& stack, const std::string& name, T* target, const T& before) {
    return stack.Record(std::make_unique<SetProperty<T>>(name, target, before, *target));
}

// ---- ListEdit<T> ---------------------------------------------------------------

/// Add, remove or move one item of a std::vector<T>.
///   Add(name, &list, index, item)     insert `item` at `index`
///   Remove(name, &list, index)        erase the item at `index` (it is kept for undo)
///   Move(name, &list, from, to)       the item at `from` ends up at index `to`
/// Use with Execute (not applied yet) or Record (the list was already edited).
/// The list is reached through a getter, so a list that lives inside a scene
/// object can be resolved again after a reload; the getter may return null.
template <class T>
class ListEdit : public ICommand {
public:
    enum class Kind { Add, Remove, Move };
    using ListGetter = std::function<std::vector<T>*()>;

    ListEdit(std::string name, ListGetter list, Kind kind, std::size_t index, T item, std::size_t toIndex = 0)
        : m_name(std::move(name)), m_list(std::move(list)), m_kind(kind), m_index(index),
          m_to(toIndex), m_item(std::move(item)) {}

    static std::unique_ptr<ListEdit> Add(std::string name, ListGetter list, std::size_t index, T item) {
        return std::make_unique<ListEdit>(std::move(name), std::move(list), Kind::Add, index, std::move(item));
    }
    static std::unique_ptr<ListEdit> Add(std::string name, std::vector<T>* list, std::size_t index, T item) {
        return Add(std::move(name), [list] { return list; }, index, std::move(item));
    }
    /// Null if `index` is out of range.
    static std::unique_ptr<ListEdit> Remove(std::string name, ListGetter list, std::size_t index) {
        std::vector<T>* v = list ? list() : nullptr;
        if (!v || index >= v->size()) return nullptr;
        T item = (*v)[index];
        return std::make_unique<ListEdit>(std::move(name), std::move(list), Kind::Remove, index, std::move(item));
    }
    static std::unique_ptr<ListEdit> Remove(std::string name, std::vector<T>* list, std::size_t index) {
        return Remove(std::move(name), [list] { return list; }, index);
    }
    /// Null if either index is out of range.
    static std::unique_ptr<ListEdit> Move(std::string name, ListGetter list, std::size_t from, std::size_t to) {
        std::vector<T>* v = list ? list() : nullptr;
        if (!v || from >= v->size() || to >= v->size()) return nullptr;
        T item = (*v)[from];
        return std::make_unique<ListEdit>(std::move(name), std::move(list), Kind::Move, from, std::move(item), to);
    }
    static std::unique_ptr<ListEdit> Move(std::string name, std::vector<T>* list, std::size_t from, std::size_t to) {
        return Move(std::move(name), [list] { return list; }, from, to);
    }

    bool Do() override {
        switch (m_kind) {
            case Kind::Add: return Insert(m_index, m_item);
            case Kind::Remove: return Erase(m_index);
            case Kind::Move: return Relocate(m_index, m_to);
        }
        return false;
    }
    bool Undo() override {
        switch (m_kind) {
            case Kind::Add: return Erase(m_index);
            case Kind::Remove: return Insert(m_index, m_item);
            case Kind::Move: return Relocate(m_to, m_index);
        }
        return false;
    }
    std::string Name() const override { return m_name; }
    bool IsNoOp() const override { return m_kind == Kind::Move && m_index == m_to; }
    std::size_t MemoryBytes() const override {
        return sizeof(*this) + m_name.capacity() + undo_detail::Bytes(m_item);
    }

private:
    std::vector<T>* List() const { return m_list ? m_list() : nullptr; }
    bool Insert(std::size_t at, const T& item) {
        std::vector<T>* v = List();
        if (!v || at > v->size()) return false;
        v->insert(v->begin() + static_cast<std::ptrdiff_t>(at), item);
        return true;
    }
    bool Erase(std::size_t at) {
        std::vector<T>* v = List();
        if (!v || at >= v->size()) return false;
        v->erase(v->begin() + static_cast<std::ptrdiff_t>(at));
        return true;
    }
    bool Relocate(std::size_t from, std::size_t to) {
        std::vector<T>* v = List();
        if (!v || from >= v->size() || to >= v->size()) return false;
        T tmp = std::move((*v)[from]);
        v->erase(v->begin() + static_cast<std::ptrdiff_t>(from));
        v->insert(v->begin() + static_cast<std::ptrdiff_t>(to), std::move(tmp));
        return true;
    }
    std::string m_name;
    ListGetter m_list;
    Kind m_kind;
    std::size_t m_index;
    std::size_t m_to;
    T m_item;
};

// ---- SnapshotCommand / SnapshotRecorder ------------------------------------------

/// A step stored as the whole state before and after, as text (the editor's
/// scene snapshot). `apply` loads a snapshot back into the live data. This is the
/// fallback for edits that have no finer command yet; it behaves like the old
/// full-scene undo but sits in the same stack as the cheap commands.
/// The texts are shared, so consecutive steps from a SnapshotRecorder hold each
/// snapshot once instead of twice.
class SnapshotCommand : public ICommand {
public:
    using Apply = std::function<bool(const std::string&)>;
    using Text = std::shared_ptr<const std::string>;

    SnapshotCommand(std::string name, Apply apply, Text before, Text after, std::uint64_t mergeKey = 0)
        : m_name(std::move(name)), m_apply(std::move(apply)), m_before(std::move(before)),
          m_after(std::move(after)), m_key(mergeKey) {}
    SnapshotCommand(std::string name, Apply apply, std::string before, std::string after, std::uint64_t mergeKey = 0)
        : SnapshotCommand(std::move(name), std::move(apply),
                          std::make_shared<const std::string>(std::move(before)),
                          std::make_shared<const std::string>(std::move(after)), mergeKey) {}

    bool Do() override { return m_apply && m_after && m_apply(*m_after); }
    bool Undo() override { return m_apply && m_before && m_apply(*m_before); }
    std::string Name() const override { return m_name; }
    bool IsNoOp() const override { return m_before == m_after || (m_before && m_after && *m_before == *m_after); }
    bool Merge(const ICommand& next) override {
        const auto* o = dynamic_cast<const SnapshotCommand*>(&next);
        if (!o || m_key == 0 || o->m_key != m_key) return false;
        m_after = o->m_after;
        return true;
    }
    /// A `before` shared with the neighbouring step is not counted twice (so the
    /// total can be a text or so short once the oldest step is evicted).
    std::size_t MemoryBytes() const override {
        std::size_t n = sizeof(*this) + m_name.capacity();
        if (m_after) n += m_after->size();
        if (m_before && m_before.use_count() == 1) n += m_before->size();
        return n;
    }

private:
    std::string m_name;
    Apply m_apply;
    Text m_before, m_after;
    std::uint64_t m_key;
};

/// Keeps the last known snapshot so an edit can be recorded after it was made:
///
///   SnapshotRecorder rec(undo, [&]{ return Serialize(scene); }, [&](const std::string& s){ return Load(scene, s); });
///   ... user edits the scene ...
///   rec.Commit("Move Cube");     // captures the new text, records before -> after
///
/// The recorder watches the stack: after any other command, undo or redo it takes
/// a fresh baseline itself, so it can share a stack with cheap commands.
class SnapshotRecorder {
public:
    using Capture = std::function<std::string()>;

    SnapshotRecorder(UndoStack& stack, Capture capture, SnapshotCommand::Apply apply)
        : m_stack(&stack), m_capture(std::move(capture)), m_apply(std::move(apply)), m_life(stack.LifeToken()) {
        m_listener = stack.AddListener([this](const UndoStack::Notice& n) { OnNotice(n); });
        Reset();
    }
    ~SnapshotRecorder() {
        if (!m_life.expired()) m_stack->RemoveListener(m_listener);
    }
    SnapshotRecorder(const SnapshotRecorder&) = delete;
    SnapshotRecorder& operator=(const SnapshotRecorder&) = delete;

    /// Take the live state as the new baseline (after a load, or an edit that
    /// should not be undoable).
    void Reset() {
        m_baseline = std::make_shared<const std::string>(m_capture ? m_capture() : std::string());
    }
    /// Capture the live state; if it differs from the baseline record the step.
    /// Returns true if a step was recorded (or merged).
    bool Commit(const std::string& name, std::uint64_t mergeKey = 0) {
        if (!m_capture) return false;
        auto after = std::make_shared<const std::string>(m_capture());
        if (m_baseline && *m_baseline == *after) return false;
        auto cmd = std::make_unique<SnapshotCommand>(name, m_apply, m_baseline, after, mergeKey);
        m_committing = true;
        const bool ok = m_stack->Record(std::move(cmd));
        m_committing = false;
        m_baseline = after;
        return ok;
    }
    const std::string& Baseline() const { static const std::string empty; return m_baseline ? *m_baseline : empty; }

private:
    void OnNotice(const UndoStack::Notice& n) {
        if (m_committing) return;
        switch (n.event) {
            case UndoStack::Event::Pushed: case UndoStack::Event::Merged: case UndoStack::Event::Undone:
            case UndoStack::Event::Redone: case UndoStack::Event::Failed: case UndoStack::Event::Cleared:
                Reset();
                break;
            default: break;
        }
    }
    UndoStack* m_stack;
    Capture m_capture;
    SnapshotCommand::Apply m_apply;
    std::weak_ptr<void> m_life;
    int m_listener = 0;
    bool m_committing = false;
    SnapshotCommand::Text m_baseline;
};

// ---- Object commands --------------------------------------------------------------

/// Object ids must stay valid for the life of the history (the engine's
/// GameObject::InstanceId, a GUID, a path hash ...). Commands never keep an
/// object pointer: they re-find the object by id on every Do/Undo, so a scene
/// reload or a deleted-then-restored object can't leave a dangling pointer. If the
/// object can't be found the command fails and the stack drops it.
using ObjectId = std::uint64_t;

/// What the object commands need from the scene. The editor fills these in with
/// lambdas that talk to the live scene; every one re-resolves ids on each call.
/// `parent` 0 means "scene root"; `index` -1 means "last child".
struct ObjectOps {
    /// Serialize the object and its whole subtree to text. False if it doesn't exist.
    std::function<bool(ObjectId id, std::string& blob)> capture;
    /// Re-create the subtree from `blob` under `parent` at `index`, giving the
    /// objects back the ids they had (so older history entries still resolve).
    std::function<bool(ObjectId id, const std::string& blob, ObjectId parent, int index)> restore;
    /// Remove the object and its subtree.
    std::function<bool(ObjectId id)> destroy;
    /// Where the object sits now: its parent id and child index.
    std::function<bool(ObjectId id, ObjectId& parent, int& index)> locate;
    /// Move the object under `parent` at `index`.
    std::function<bool(ObjectId id, ObjectId parent, int index)> reparent;
};
using ObjectOpsPtr = std::shared_ptr<const ObjectOps>;

/// An object was just created (and is now in the scene): undo removes it, redo
/// re-creates it from the text captured here. Use with Record.
class CreateObjectCommand : public ICommand {
public:
    /// Captures the object immediately; null if it can't be captured.
    static std::unique_ptr<CreateObjectCommand> Make(ObjectOpsPtr ops, ObjectId id, std::string name) {
        if (!ops || !ops->capture || !ops->restore || !ops->destroy) return nullptr;
        std::string blob;
        if (!ops->capture(id, blob)) return nullptr;
        ObjectId parent = 0; int index = -1;
        if (ops->locate) ops->locate(id, parent, index);
        return std::unique_ptr<CreateObjectCommand>(
            new CreateObjectCommand(std::move(ops), id, std::move(name), std::move(blob), parent, index));
    }
    bool Do() override { return m_ops->restore(m_id, m_blob, m_parent, m_index); }
    bool Undo() override { return m_ops->destroy(m_id); }
    std::string Name() const override { return m_name; }
    std::size_t MemoryBytes() const override { return sizeof(*this) + m_name.capacity() + m_blob.capacity(); }
    ObjectId Id() const { return m_id; }

private:
    CreateObjectCommand(ObjectOpsPtr ops, ObjectId id, std::string name, std::string blob, ObjectId parent, int index)
        : m_ops(std::move(ops)), m_id(id), m_name(std::move(name)), m_blob(std::move(blob)),
          m_parent(parent), m_index(index) {}
    ObjectOpsPtr m_ops;
    ObjectId m_id;
    std::string m_name, m_blob;
    ObjectId m_parent;
    int m_index;
};

/// Delete an object (and its subtree). Make() captures it BEFORE it is removed;
/// use with Execute. Undo restores it with the same ids and position.
class DeleteObjectCommand : public ICommand {
public:
    /// Null if the object doesn't exist / can't be captured.
    static std::unique_ptr<DeleteObjectCommand> Make(ObjectOpsPtr ops, ObjectId id, std::string name) {
        if (!ops || !ops->capture || !ops->restore || !ops->destroy) return nullptr;
        std::string blob;
        if (!ops->capture(id, blob)) return nullptr;
        ObjectId parent = 0; int index = -1;
        if (ops->locate) ops->locate(id, parent, index);
        return std::unique_ptr<DeleteObjectCommand>(
            new DeleteObjectCommand(std::move(ops), id, std::move(name), std::move(blob), parent, index));
    }
    bool Do() override { return m_ops->destroy(m_id); }
    bool Undo() override { return m_ops->restore(m_id, m_blob, m_parent, m_index); }
    std::string Name() const override { return m_name; }
    std::size_t MemoryBytes() const override { return sizeof(*this) + m_name.capacity() + m_blob.capacity(); }
    ObjectId Id() const { return m_id; }

private:
    DeleteObjectCommand(ObjectOpsPtr ops, ObjectId id, std::string name, std::string blob, ObjectId parent, int index)
        : m_ops(std::move(ops)), m_id(id), m_name(std::move(name)), m_blob(std::move(blob)),
          m_parent(parent), m_index(index) {}
    ObjectOpsPtr m_ops;
    ObjectId m_id;
    std::string m_name, m_blob;
    ObjectId m_parent;
    int m_index;
};

/// Move an object under another parent / to another sibling position. Make()
/// remembers where it is now; use with Execute (or Record if already moved and
/// you pass the old place with MakeFrom).
class ReparentObjectCommand : public ICommand {
public:
    static std::unique_ptr<ReparentObjectCommand> Make(ObjectOpsPtr ops, ObjectId id, ObjectId newParent,
                                                       int newIndex, std::string name) {
        if (!ops || !ops->locate || !ops->reparent) return nullptr;
        ObjectId oldParent = 0; int oldIndex = -1;
        if (!ops->locate(id, oldParent, oldIndex)) return nullptr;
        return MakeFrom(std::move(ops), id, oldParent, oldIndex, newParent, newIndex, std::move(name));
    }
    static std::unique_ptr<ReparentObjectCommand> MakeFrom(ObjectOpsPtr ops, ObjectId id, ObjectId oldParent,
                                                           int oldIndex, ObjectId newParent, int newIndex,
                                                           std::string name) {
        if (!ops || !ops->reparent) return nullptr;
        return std::unique_ptr<ReparentObjectCommand>(
            new ReparentObjectCommand(std::move(ops), id, oldParent, oldIndex, newParent, newIndex, std::move(name)));
    }
    bool Do() override { return m_ops->reparent(m_id, m_newParent, m_newIndex); }
    bool Undo() override { return m_ops->reparent(m_id, m_oldParent, m_oldIndex); }
    std::string Name() const override { return m_name; }
    bool IsNoOp() const override { return m_oldParent == m_newParent && m_oldIndex == m_newIndex; }
    std::size_t MemoryBytes() const override { return sizeof(*this) + m_name.capacity(); }

private:
    ReparentObjectCommand(ObjectOpsPtr ops, ObjectId id, ObjectId op, int oi, ObjectId np, int ni, std::string name)
        : m_ops(std::move(ops)), m_id(id), m_name(std::move(name)),
          m_oldParent(op), m_oldIndex(oi), m_newParent(np), m_newIndex(ni) {}
    ObjectOpsPtr m_ops;
    ObjectId m_id;
    std::string m_name;
    ObjectId m_oldParent; int m_oldIndex;
    ObjectId m_newParent; int m_newIndex;
};

/// Set one property of the object `id`. The command stores the id and a
/// resolver, never the object: after a scene reload the resolver finds the new
/// instance. Consecutive edits to the same (object, property) merge.
///
///   SetObjectProperty<GameObject, std::string>("Rename", resolve, id, "name",
///       [](GameObject& o, const std::string& v){ o.name = v; }, oldName, newName)
template <class Obj, class T>
class SetObjectProperty : public ICommand {
public:
    using Resolver = std::function<Obj*(ObjectId)>;
    using Setter = std::function<void(Obj&, const T&)>;

    SetObjectProperty(std::string name, Resolver resolve, ObjectId id, std::string property, Setter set,
                      T before, T after, bool mergeable = true)
        : m_name(std::move(name)), m_resolve(std::move(resolve)), m_id(id), m_property(std::move(property)),
          m_set(std::move(set)), m_before(std::move(before)), m_after(std::move(after)), m_mergeable(mergeable) {}

    bool Do() override { return Apply(m_after); }
    bool Undo() override { return Apply(m_before); }
    std::string Name() const override { return m_name; }
    bool IsNoOp() const override { return undo_detail::Same(m_before, m_after); }
    bool Merge(const ICommand& next) override {
        const auto* o = dynamic_cast<const SetObjectProperty<Obj, T>*>(&next);
        if (!o || !m_mergeable || !o->m_mergeable || o->m_id != m_id || o->m_property != m_property) return false;
        m_after = o->m_after;
        return true;
    }
    std::size_t MemoryBytes() const override {
        return sizeof(*this) + m_name.capacity() + m_property.capacity()
             + undo_detail::Bytes(m_before) + undo_detail::Bytes(m_after);
    }
    ObjectId Id() const { return m_id; }
    const std::string& Property() const { return m_property; }

private:
    bool Apply(const T& v) {
        Obj* o = m_resolve ? m_resolve(m_id) : nullptr;
        if (!o || !m_set) return false;
        m_set(*o, v);
        return true;
    }
    std::string m_name;
    Resolver m_resolve;
    ObjectId m_id;
    std::string m_property;
    Setter m_set;
    T m_before, m_after;
    bool m_mergeable;
};

} // namespace okay
