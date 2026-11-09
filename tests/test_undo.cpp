#include "test_framework.hpp"
#include "okay/Core/Undo.hpp"
#include "okay/Core/UndoCommands.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct Flags {
    bool failDo = false;
    bool failUndo = false;
};

/// A command that writes Do/Undo calls into a log; failures and size are tunable.
class TraceCmd : public ICommand {
public:
    TraceCmd(std::string name, std::vector<std::string>* log, std::size_t bytes = 64,
             std::shared_ptr<Flags> flags = nullptr)
        : m_name(std::move(name)), m_log(log), m_bytes(bytes), m_flags(flags ? flags : std::make_shared<Flags>()) {}
    bool Do() override {
        if (m_flags->failDo) return false;
        if (m_log) m_log->push_back("do " + m_name);
        return true;
    }
    bool Undo() override {
        if (m_flags->failUndo) return false;
        if (m_log) m_log->push_back("undo " + m_name);
        return true;
    }
    std::string Name() const override { return m_name; }
    std::size_t MemoryBytes() const override { return m_bytes; }
private:
    std::string m_name;
    std::vector<std::string>* m_log;
    std::size_t m_bytes;
    std::shared_ptr<Flags> m_flags;
};

std::unique_ptr<ICommand> Trace(const std::string& n, std::vector<std::string>* log, std::size_t bytes = 64) {
    return std::make_unique<TraceCmd>(n, log, bytes);
}

/// A tiny "scene" for the object command tests: objects by id, rebuilt on "reload".
struct Obj {
    ObjectId id = 0;
    ObjectId parent = 0;
    std::string name;
    float mass = 1.0f;
};
struct World {
    std::map<ObjectId, Obj> objs;
    std::vector<ObjectId> order;   // creation order = child order among all (flat is enough here)
    Obj* Find(ObjectId id) { auto it = objs.find(id); return it == objs.end() ? nullptr : &it->second; }
    void Reload() {   // a snapshot restore: every object is a new instance
        std::map<ObjectId, Obj> fresh;
        for (auto& kv : objs) fresh[kv.first] = kv.second;
        objs.swap(fresh);
    }
    std::vector<ObjectId> Subtree(ObjectId id) {
        std::vector<ObjectId> out{id};
        for (std::size_t i = 0; i < out.size(); ++i)
            for (ObjectId o : order) if (Find(o) && Find(o)->parent == out[i]) out.push_back(o);
        return out;
    }
};

std::shared_ptr<ObjectOps> MakeOps(World& w) {
    auto ops = std::make_shared<ObjectOps>();
    ops->capture = [&w](ObjectId id, std::string& blob) {
        if (!w.Find(id)) return false;
        blob.clear();
        for (ObjectId o : w.Subtree(id)) {
            const Obj& ob = *w.Find(o);
            blob += std::to_string(ob.id) + "," + std::to_string(ob.parent) + "," + ob.name + "," + std::to_string(ob.mass) + "\n";
        }
        return true;
    };
    ops->restore = [&w](ObjectId, const std::string& blob, ObjectId parent, int) {
        std::size_t pos = 0;
        bool first = true;
        while (pos < blob.size()) {
            std::size_t e = blob.find('\n', pos);
            const std::string line = blob.substr(pos, e - pos);
            pos = e + 1;
            Obj ob;
            std::size_t a = line.find(','), b = line.find(',', a + 1), c = line.find(',', b + 1);
            ob.id = std::stoull(line.substr(0, a));
            ob.parent = std::stoull(line.substr(a + 1, b - a - 1));
            ob.name = line.substr(b + 1, c - b - 1);
            ob.mass = std::stof(line.substr(c + 1));
            if (first) { ob.parent = parent; first = false; }
            w.objs[ob.id] = ob;
            if (std::find(w.order.begin(), w.order.end(), ob.id) == w.order.end()) w.order.push_back(ob.id);
        }
        return true;
    };
    ops->destroy = [&w](ObjectId id) {
        if (!w.Find(id)) return false;
        for (ObjectId o : w.Subtree(id)) w.objs.erase(o);
        return true;
    };
    ops->locate = [&w](ObjectId id, ObjectId& parent, int& index) {
        Obj* o = w.Find(id);
        if (!o) return false;
        parent = o->parent;
        index = 0;
        return true;
    };
    ops->reparent = [&w](ObjectId id, ObjectId parent, int) {
        Obj* o = w.Find(id);
        if (!o) return false;
        o->parent = parent;
        return true;
    };
    return ops;
}

} // namespace

int main() {
    RUN_SUITE("undo");

    // --- Basics: execute, undo, redo, names ---------------------------------------
    {
        UndoStack u;
        float speed = 1.0f;
        CHECK(!u.CanUndo() && !u.CanRedo());
        CHECK(!u.Undo() && !u.Redo());
        CHECK(UndoSet(u, "Set Speed", &speed, 9.0f));
        CHECK(speed == 9.0f);
        CHECK(u.CanUndo() && !u.CanRedo());
        CHECK(u.NextUndoName() == "Set Speed");
        CHECK(u.Undo());
        CHECK(speed == 1.0f);
        CHECK(!u.CanUndo() && u.CanRedo());
        CHECK(u.NextRedoName() == "Set Speed");
        CHECK(u.Redo());
        CHECK(speed == 9.0f);
        CHECK(u.UndoCount() == 1 && u.RedoCount() == 0);

        int n = 0;
        u.BreakMerge();
        CHECK(u.Execute(std::make_unique<SetProperty<int>>("Set N", &n, 0, 4)));
        CHECK(u.UndoCount() == 2);
        const auto names = u.UndoNames();
        CHECK(names.size() == 2 && names[0] == "Set N" && names[1] == "Set Speed");
        CHECK(u.UndoNames(1).size() == 1);
        CHECK(u.UndoMany(5) == 2);
        CHECK(n == 0 && speed == 1.0f);
        const auto rn = u.RedoNames();
        CHECK(rn.size() == 2 && rn[0] == "Set Speed" && rn[1] == "Set N");
        CHECK(u.RedoMany(1) == 1);
        CHECK(speed == 9.0f && n == 0);
    }

    // --- Redo is discarded by a new command ----------------------------------------
    {
        UndoStack u;
        std::vector<std::string> log;
        u.Execute(Trace("A", &log));
        u.Execute(Trace("B", &log));
        u.Undo();
        CHECK(u.CanRedo() && u.RedoCount() == 1);
        u.Execute(Trace("C", &log));
        CHECK(!u.CanRedo());
        CHECK(!u.Redo());
        CHECK(u.UndoCount() == 2 && u.NextUndoName() == "C");
        // Record discards redo too.
        u.Undo();
        CHECK(u.RedoCount() == 1);
        u.Record(Trace("D", &log));
        CHECK(u.RedoCount() == 0);
    }

    // --- Execute applies, Record does not; a failing Do records nothing --------------
    {
        UndoStack u;
        std::vector<std::string> log;
        u.Execute(Trace("exec", &log));
        u.Record(Trace("rec", &log));
        CHECK(log.size() == 1 && log[0] == "do exec");
        u.Undo();   // undoes "rec"
        u.Redo();   // redo calls Do on it
        CHECK(log.size() == 3 && log[1] == "undo rec" && log[2] == "do rec");

        auto flags = std::make_shared<Flags>();
        flags->failDo = true;
        const std::size_t before = u.UndoCount();
        CHECK(!u.Execute(std::make_unique<TraceCmd>("bad", &log, 64, flags)));
        CHECK(u.UndoCount() == before);
        CHECK(u.LastError().find("bad") != std::string::npos);
        CHECK(!u.Execute(nullptr) && !u.Record(nullptr));
    }

    // --- Merge: a slider drag is one step ---------------------------------------------
    {
        UndoStack u;
        float v = 1.0f;
        for (int i = 1; i <= 20; ++i) {            // the widget writes, then we record
            const float before = v;
            v = 1.0f + (float)i;
            CHECK(UndoRecordSet(u, "Set Intensity", &v, before));
        }
        CHECK(v == 21.0f);
        CHECK(u.UndoCount() == 1);
        u.BreakMerge();                            // mouse released
        { const float b = v; v = 50.0f; UndoRecordSet(u, "Set Intensity", &v, b); }   // a new drag
        CHECK(u.UndoCount() == 2);
        CHECK(u.Undo());
        CHECK(v == 21.0f);
        CHECK(u.Undo());
        CHECK(v == 1.0f);                          // the whole first drag, back to its start
        CHECK(u.Redo() && v == 21.0f);
        CHECK(u.Redo() && v == 50.0f);

        // Different targets, or a different type of command, never merge.
        UndoStack u2;
        float a = 0, b = 0;
        UndoSet(u2, "Set A", &a, 1.0f);
        UndoSet(u2, "Set B", &b, 1.0f);
        std::vector<std::string> log;
        u2.Execute(Trace("X", &log));
        u2.Execute(Trace("X", &log));
        CHECK(u2.UndoCount() == 4);
        UndoSet(u2, "Set B", &b, 2.0f);            // follows a Trace: nothing to merge into
        UndoSet(u2, "Set B", &b, 3.0f);            // same target as the one before: merges
        CHECK(u2.UndoCount() == 5);
        u2.Undo();
        CHECK(b == 1.0f);

        // mergeable=false opts out.
        UndoStack u3;
        float c = 0;
        u3.Execute(std::make_unique<SetProperty<float>>("C", &c, 0.0f, 1.0f, false));
        u3.Execute(std::make_unique<SetProperty<float>>("C", &c, 1.0f, 2.0f, false));
        CHECK(u3.UndoCount() == 2);
    }

    // --- Merge window (Tick) -----------------------------------------------------------
    {
        UndoStack u;
        u.SetMergeWindow(0.5f);
        float v = 0;
        UndoSet(u, "V", &v, 1.0f);
        u.Tick(0.2f);
        UndoSet(u, "V", &v, 2.0f);
        CHECK(u.UndoCount() == 1);                 // within the window
        u.Tick(0.6f);
        UndoSet(u, "V", &v, 3.0f);
        CHECK(u.UndoCount() == 2);                 // too late: a new step
        u.Undo();
        CHECK(v == 2.0f);
    }

    // --- Drag away and back leaves nothing behind; no-op commands are not recorded ---------
    {
        UndoStack u;
        float v = 1.0f;
        CHECK(UndoSet(u, "V", &v, 1.0f));          // no change
        CHECK(u.UndoCount() == 0 && !u.IsDirty());
        UndoSet(u, "V", &v, 5.0f);
        CHECK(u.IsDirty());
        u.MarkSaved();                             // seals; next edit is a new step
        UndoSet(u, "V", &v, 7.0f);
        UndoSet(u, "V", &v, 5.0f);                 // back to the saved value: merged into a no-op
        CHECK(v == 5.0f);
        CHECK(u.UndoCount() == 1);
        CHECK(!u.IsDirty());                       // the state equals the saved one again
    }

    // --- Groups: one step, order, nesting, empty, cancel -------------------------------------
    {
        UndoStack u;
        std::vector<std::string> log;
        CHECK(u.BeginGroup("Delete 3"));
        u.Execute(Trace("a", &log));
        u.Execute(Trace("b", &log));
        u.Record(Trace("c", &log));
        CHECK(u.GroupDepth() == 1);
        CHECK(u.UndoCount() == 0);                 // nothing in the history until EndGroup
        CHECK(!u.CanUndo());
        CHECK(!u.Undo());                          // can't undo with a group open
        CHECK(u.EndGroup());
        CHECK(u.UndoCount() == 1 && u.NextUndoName() == "Delete 3");
        log.clear();
        CHECK(u.Undo());
        CHECK(log == (std::vector<std::string>{"undo c", "undo b", "undo a"}));   // reverse order
        log.clear();
        CHECK(u.Redo());
        CHECK(log == (std::vector<std::string>{"do a", "do b", "do c"}));

        // Nested groups make one entry.
        UndoStack n;
        n.BeginGroup("outer");
        n.Execute(Trace("1", &log));
        n.BeginGroup("inner");
        n.Execute(Trace("2", &log));
        n.Execute(Trace("3", &log));
        CHECK(n.GroupDepth() == 2);
        n.EndGroup();
        n.Execute(Trace("4", &log));
        n.EndGroup();
        CHECK(n.UndoCount() == 1 && n.NextUndoName() == "outer");
        log.clear();
        n.Undo();
        CHECK(log == (std::vector<std::string>{"undo 4", "undo 3", "undo 2", "undo 1"}));

        // Empty group records nothing; unmatched EndGroup fails.
        UndoStack e;
        e.BeginGroup("nothing");
        CHECK(e.EndGroup());
        CHECK(e.UndoCount() == 0 && !e.IsDirty());
        CHECK(!e.EndGroup());

        // A group around a single unnamed edit stays mergeable.
        UndoStack m;
        float v = 0;
        for (int i = 1; i <= 5; ++i) {
            m.BeginGroup("");
            UndoSet(m, "V", &v, (float)i);
            m.EndGroup();
        }
        CHECK(m.UndoCount() == 1);

        // Two edits to the same property in one transaction fold together.
        UndoStack f;
        float w = 0;
        f.BeginGroup("Twice");
        UndoSet(f, "W", &w, 1.0f);
        UndoSet(f, "W", &w, 2.0f);
        f.EndGroup();
        f.Undo();
        CHECK(w == 0.0f);

        // A failing command inside a group isn't added, the rest still is.
        UndoStack g;
        auto flags = std::make_shared<Flags>();
        flags->failDo = true;
        log.clear();
        g.BeginGroup("partial");
        CHECK(!g.Execute(std::make_unique<TraceCmd>("bad", &log, 64, flags)));
        CHECK(g.Execute(Trace("ok", &log)));
        g.EndGroup();
        g.Undo();
        CHECK(log == (std::vector<std::string>{"do ok", "undo ok"}));

        // RAII scope + cancel.
        UndoStack s;
        float p = 1.0f, q = 1.0f;
        {
            UndoGroup grp(s, "Scoped");
            UndoSet(s, "P", &p, 2.0f);
            UndoSet(s, "Q", &q, 2.0f);
        }
        CHECK(s.UndoCount() == 1 && s.GroupDepth() == 0);
        {
            UndoGroup grp(s, "Cancelled");
            UndoSet(s, "P", &p, 9.0f);
            UndoSet(s, "Q", &q, 9.0f);
            CHECK(p == 9.0f);
            CHECK(grp.Cancel());                   // everything done in the group is rolled back
        }
        CHECK(p == 2.0f && q == 2.0f);
        CHECK(s.UndoCount() == 1 && s.GroupDepth() == 0);
        s.Undo();
        CHECK(p == 1.0f && q == 1.0f);
    }

    // --- Group atomicity ------------------------------------------------------------------------
    {
        // A child that can't undo: the children already undone are rolled forward again.
        UndoStack u;
        std::vector<std::string> log;
        auto bFlags = std::make_shared<Flags>();
        u.BeginGroup("Three");
        u.Execute(Trace("A", &log));
        u.Execute(std::make_unique<TraceCmd>("B", &log, 64, bFlags));
        u.Execute(Trace("C", &log));
        u.EndGroup();
        bFlags->failUndo = true;
        log.clear();
        CHECK(!u.Undo());
        // C was undone first, then B failed: C is re-applied so nothing is half done.
        CHECK(log == (std::vector<std::string>{"undo C", "do C"}));
        CHECK(!u.LastError().empty());
        CHECK(u.UndoCount() == 0 && u.RedoCount() == 0);   // the stale step is gone, not stuck

        // A child that can't redo: the ones already redone are undone again.
        UndoStack r;
        auto cFlags = std::make_shared<Flags>();
        r.BeginGroup("Three");
        r.Execute(Trace("A", &log));
        r.Execute(Trace("B", &log));
        r.Execute(std::make_unique<TraceCmd>("C", &log, 64, cFlags));
        r.EndGroup();
        CHECK(r.Undo());
        cFlags->failDo = true;
        log.clear();
        CHECK(!r.Redo());
        CHECK(log == (std::vector<std::string>{"do A", "do B", "undo B", "undo A"}));
        CHECK(r.UndoCount() == 0 && r.RedoCount() == 0);
    }

    // --- Capacity: command count ------------------------------------------------------------------
    {
        UndoStack u;
        u.SetMaxCommands(5);
        std::vector<std::string> log;
        for (int i = 0; i < 12; ++i) u.Execute(Trace(std::to_string(i), &log));
        CHECK(u.UndoCount() == 5);
        CHECK(u.NextUndoName() == "11");
        const auto names = u.UndoNames();
        CHECK(names.back() == "7");                // the oldest kept; 0..6 were evicted
        CHECK(u.UndoMany(100) == 5);
        CHECK(!u.CanUndo());

        UndoStack big;                              // default cap bounds the history
        for (int i = 0; i < 5000; ++i) big.Execute(Trace("x", nullptr));
        CHECK(big.UndoCount() == UndoStack::kDefaultMaxCommands);

        UndoStack unl;
        unl.SetMaxCommands(0);
        unl.SetMaxMemory(0);
        for (int i = 0; i < 1000; ++i) unl.Execute(Trace("x", nullptr));
        CHECK(unl.UndoCount() == 1000);
    }

    // --- Capacity: memory cap eviction ---------------------------------------------------------------
    {
        UndoStack u;
        u.SetMaxCommands(0);
        u.SetMaxMemory(450);
        std::vector<std::string> log;
        for (int i = 0; i < 10; ++i) u.Execute(Trace("m" + std::to_string(i), &log, 100));
        CHECK(u.UndoCount() == 4);                 // 4 * 100 <= 450 < 5 * 100
        CHECK(u.MemoryBytes() == 400);
        CHECK(u.NextUndoName() == "m9");
        CHECK(u.UndoNames().back() == "m6");       // oldest evicted first

        // A command bigger than the whole cap is still kept (it must stay undoable), alone.
        u.Execute(Trace("huge", &log, 10000));
        CHECK(u.UndoCount() == 1 && u.NextUndoName() == "huge");
        CHECK(u.Undo());

        // Lowering the cap evicts immediately.
        UndoStack v;
        v.SetMaxCommands(0);
        v.SetMaxMemory(0);
        for (int i = 0; i < 6; ++i) v.Execute(Trace("n" + std::to_string(i), &log, 100));
        CHECK(v.MemoryBytes() == 600);
        v.SetMaxMemory(250);
        CHECK(v.UndoCount() == 2 && v.NextUndoName() == "n5");
        // Redo entries count towards the reported memory.
        v.Undo();
        CHECK(v.MemoryBytes() == 200);

        // Merging a growing command updates the accounted size.
        UndoStack w;
        std::string text = "a";
        for (int i = 0; i < 3; ++i) {
            const std::string before = text;
            text += std::string(1000, 'x');
            w.Record(std::make_unique<SetProperty<std::string>>("T", &text, before, text));
        }
        CHECK(w.UndoCount() == 1);
        CHECK(w.MemoryBytes() >= 3000);
    }

    // --- Dirty tracking relative to a save point -----------------------------------------------------------
    {
        UndoStack u;
        std::vector<std::string> log;
        CHECK(!u.IsDirty());
        u.Execute(Trace("A", &log));
        CHECK(u.IsDirty());
        u.MarkSaved();
        CHECK(!u.IsDirty());
        u.Execute(Trace("B", &log));
        CHECK(u.IsDirty());
        u.Undo();
        CHECK(!u.IsDirty());                       // back at the saved state
        u.Undo();
        CHECK(u.IsDirty());
        u.Redo();
        CHECK(!u.IsDirty());
        u.Redo();
        CHECK(u.IsDirty());

        // New work after undoing past the save point: the saved state is unreachable.
        UndoStack d;
        d.Execute(Trace("A", &log));
        d.MarkSaved();                             // saved after A
        d.Undo();
        CHECK(d.IsDirty());
        d.Execute(Trace("B", &log));               // discards the redo branch that held the save point
        CHECK(d.IsDirty());
        d.Undo();
        CHECK(d.IsDirty());                        // position 0 is NOT the saved state (that was after A)
        d.Execute(Trace("C", &log));
        d.MarkSaved();
        CHECK(!d.IsDirty());

        // Edits after a save never fold into the saved step.
        UndoStack m;
        float v = 0;
        UndoSet(m, "V", &v, 1.0f);
        UndoSet(m, "V", &v, 2.0f);
        m.MarkSaved();
        UndoSet(m, "V", &v, 3.0f);
        CHECK(m.UndoCount() == 2 && m.IsDirty());
        m.Undo();
        CHECK(v == 2.0f && !m.IsDirty());

        // Evicting the save point makes the state permanently dirty until the next save.
        UndoStack e;
        e.SetMaxCommands(3);
        e.Execute(Trace("1", &log));
        e.MarkSaved();                             // saved at depth 1
        for (int i = 0; i < 5; ++i) e.Execute(Trace("x", &log));
        CHECK(e.UndoCount() == 3 && e.IsDirty());
        e.UndoMany(10);
        CHECK(e.IsDirty());
        e.MarkSaved();
        CHECK(!e.IsDirty());

        // The save point that is still in range survives eviction of older steps.
        UndoStack k;
        k.SetMaxCommands(3);
        k.Execute(Trace("a", &log));
        k.Execute(Trace("b", &log));
        k.MarkSaved();                             // depth 2
        k.Execute(Trace("c", &log));
        k.Execute(Trace("d", &log));               // evicts a: depth 3, save point now at depth 1
        CHECK(k.UndoCount() == 3 && k.IsDirty());
        k.Undo();
        k.Undo();
        CHECK(!k.IsDirty());                       // landed on the saved state

        // MarkDirty and Clear.
        UndoStack c;
        c.MarkDirty();
        CHECK(c.IsDirty());
        c.MarkSaved();
        c.Execute(Trace("a", &log));
        c.Clear();
        CHECK(!c.IsDirty() && c.UndoCount() == 0 && !c.CanUndo());

        // A dirty open group counts as dirty.
        UndoStack og;
        og.BeginGroup("g");
        CHECK(!og.IsDirty());
        og.Execute(Trace("a", &log));
        CHECK(og.IsDirty());
        og.EndGroup();
        CHECK(og.IsDirty());
    }

    // --- Listeners --------------------------------------------------------------------------------------------
    {
        UndoStack u;
        std::vector<std::string> seen;
        auto name = [](UndoStack::Event e) {
            switch (e) {
                case UndoStack::Event::Pushed: return "pushed";
                case UndoStack::Event::Merged: return "merged";
                case UndoStack::Event::Undone: return "undone";
                case UndoStack::Event::Redone: return "redone";
                case UndoStack::Event::Failed: return "failed";
                case UndoStack::Event::Evicted: return "evicted";
                case UndoStack::Event::Cleared: return "cleared";
                case UndoStack::Event::SavePoint: return "save";
            }
            return "?";
        };
        const int id = u.AddListener([&](const UndoStack::Notice& n) { seen.push_back(std::string(name(n.event)) + ":" + n.name); });
        CHECK(id != 0);
        u.SetMaxCommands(2);
        float v = 0;
        UndoSet(u, "V", &v, 1.0f);
        UndoSet(u, "V", &v, 2.0f);
        u.BreakMerge();
        UndoSet(u, "V", &v, 3.0f);
        std::vector<std::string> log;
        u.Execute(Trace("T", &log));               // evicts the oldest
        u.Undo();
        u.Redo();
        u.MarkSaved();
        u.Clear();
        const std::vector<std::string> expect = {
            "pushed:V", "merged:V", "pushed:V", "pushed:T", "evicted:V", "undone:T", "redone:T", "save:", "cleared:"};
        // Eviction is reported before the Pushed that caused it? either order is allowed, but all must appear once.
        CHECK(seen.size() == expect.size());
        for (const auto& e : expect) CHECK(std::count(seen.begin(), seen.end(), e) == 1 + (e == "pushed:V" ? 1 : 0));

        const std::size_t count = seen.size();
        u.RemoveListener(id);
        u.Execute(Trace("after", &log));
        CHECK(seen.size() == count);

        // A listener can't edit the stack (it is busy), and IsApplying is true inside it.
        UndoStack r;
        bool applyingSeen = false, refused = false;
        r.AddListener([&](const UndoStack::Notice&) {
            applyingSeen = r.IsApplying();
            refused = !r.Execute(Trace("nested", nullptr));
        });
        r.Execute(Trace("outer", nullptr));
        CHECK(applyingSeen && refused);
        CHECK(r.UndoCount() == 1);
        CHECK(!r.IsApplying());

        // A command that edits the stack from inside Do()/Undo() is refused, not recorded.
        UndoStack s;
        struct Reentrant : ICommand {
            UndoStack* st; int* refusals;
            bool Do() override { if (!st->Execute(std::make_unique<TraceCmd>("inner", nullptr))) ++*refusals; return true; }
            bool Undo() override { if (!st->Record(std::make_unique<TraceCmd>("inner", nullptr))) ++*refusals; return true; }
            std::string Name() const override { return "Reentrant"; }
        };
        int refusals = 0;
        auto re = std::make_unique<Reentrant>();
        re->st = &s; re->refusals = &refusals;
        CHECK(s.Execute(std::move(re)));
        CHECK(s.Undo());
        CHECK(refusals == 2 && s.UndoCount() == 0 && s.RedoCount() == 1);
    }

    // --- Stale commands are dropped, not stuck -----------------------------------------------------------------
    {
        UndoStack u;
        World w;
        w.objs[1] = Obj{1, 0, "Cube", 1.0f};
        w.order.push_back(1);
        auto resolve = [&w](ObjectId id) { return w.Find(id); };
        auto setMass = [](Obj& o, const float& v) { o.mass = v; };

        for (float m : {2.0f, 3.0f}) {
            Obj* o = w.Find(1);
            const float before = o->mass;
            o->mass = m;
            u.Record(std::make_unique<SetObjectProperty<Obj, float>>("Mass", resolve, 1, "mass", setMass, before, m));
            u.BreakMerge();
        }
        CHECK(u.UndoCount() == 2);
        w.objs.erase(1);                            // the object vanished (scene reloaded without it)
        CHECK(!u.Undo());
        CHECK(!u.LastError().empty());
        CHECK(u.UndoCount() == 1 && u.RedoCount() == 0);
        CHECK(!u.Undo());                           // the next one is stale too
        CHECK(u.UndoCount() == 0);

        // A stale step at the save point makes the state dirty.
        UndoStack s;
        World w2;
        w2.objs[7] = Obj{7, 0, "A", 1.0f};
        auto resolve2 = [&w2](ObjectId id) { return w2.Find(id); };
        w2.Find(7)->mass = 5.0f;
        s.Record(std::make_unique<SetObjectProperty<Obj, float>>("Mass", resolve2, 7, "mass", setMass, 1.0f, 5.0f));
        s.MarkSaved();
        w2.objs.clear();
        CHECK(!s.Undo());
        CHECK(s.IsDirty());
        s.Execute(Trace("later", nullptr));         // the stack grows back to the old depth: still not the saved state
        CHECK(s.IsDirty());
    }

    // --- SetProperty accessor form ---------------------------------------------------------------------------------
    {
        UndoStack u;
        float stored = 1.0f;
        bool alive = true;
        auto setter = [&](const float& v) { if (!alive) return false; stored = v; return true; };
        u.Execute(std::make_unique<SetProperty<float>>("Acc", setter, 1.0f, 4.0f, 1234));
        u.Execute(std::make_unique<SetProperty<float>>("Acc", setter, 4.0f, 6.0f, 1234));   // same key: merges
        CHECK(u.UndoCount() == 1 && stored == 6.0f);
        u.Execute(std::make_unique<SetProperty<float>>("Acc", setter, 6.0f, 7.0f));          // key 0: never merges
        CHECK(u.UndoCount() == 2);
        u.Undo();
        CHECK(stored == 6.0f);
        alive = false;
        CHECK(!u.Undo());                           // the accessor says the target is gone
        CHECK(u.UndoCount() == 0);
        // Pointer-less, setter-less property fails cleanly.
        UndoStack e;
        CHECK(!e.Execute(std::make_unique<SetProperty<int>>("none", SetProperty<int>::Setter(), 0, 1)));
    }

    // --- Object properties survive a scene reload ----------------------------------------------------------------------
    {
        UndoStack u;
        World w;
        w.objs[10] = Obj{10, 0, "Cube", 1.0f};
        w.objs[11] = Obj{11, 0, "Ball", 1.0f};
        auto resolve = [&w](ObjectId id) { return w.Find(id); };
        auto setName = [](Obj& o, const std::string& v) { o.name = v; };
        auto setMass = [](Obj& o, const float& v) { o.mass = v; };
        u.Execute(std::make_unique<SetObjectProperty<Obj, std::string>>("Rename", resolve, 10, "name", setName, "Cube", "Crate"));
        for (int i = 1; i <= 10; ++i) {            // a mass drag on object 11
            Obj* o = w.Find(11);
            const float before = o->mass;
            o->mass = 1.0f + (float)i;
            u.Record(std::make_unique<SetObjectProperty<Obj, float>>("Mass", resolve, 11, "mass", setMass, before, o->mass));
        }
        CHECK(u.UndoCount() == 2);                  // the 10 drag edits merged
        // A drag on a different object (same property) is a different step.
        { Obj* o = w.Find(10); const float b = o->mass; o->mass = 3; u.Record(std::make_unique<SetObjectProperty<Obj, float>>("Mass", resolve, 10, "mass", setMass, b, 3.0f)); }
        CHECK(u.UndoCount() == 3);

        w.Reload();                                 // every object is a new instance now
        CHECK(u.Undo());
        CHECK(w.Find(10)->mass == 1.0f);
        CHECK(u.Undo());
        CHECK(w.Find(11)->mass == 1.0f);
        CHECK(u.Undo());
        CHECK(w.Find(10)->name == "Cube");
        w.Reload();
        CHECK(u.Redo() && w.Find(10)->name == "Crate");
        CHECK(u.Redo() && w.Find(11)->mass == 11.0f);
    }

    // --- ListEdit ----------------------------------------------------------------------------------------------------------
    {
        UndoStack u;
        std::vector<std::string> items = {"a", "b", "c"};
        CHECK(u.Execute(ListEdit<std::string>::Add("Add d", &items, 3, "d")));
        CHECK(items == (std::vector<std::string>{"a", "b", "c", "d"}));
        CHECK(u.Execute(ListEdit<std::string>::Remove("Remove b", &items, 1)));
        CHECK(items == (std::vector<std::string>{"a", "c", "d"}));
        CHECK(u.Execute(ListEdit<std::string>::Move("Move a", &items, 0, 2)));
        CHECK(items == (std::vector<std::string>{"c", "d", "a"}));
        CHECK(u.UndoCount() == 3);
        u.Undo();
        CHECK(items == (std::vector<std::string>{"a", "c", "d"}));
        u.Undo();
        CHECK(items == (std::vector<std::string>{"a", "b", "c", "d"}));
        u.Undo();
        CHECK(items == (std::vector<std::string>{"a", "b", "c"}));
        u.Redo(); u.Redo(); u.Redo();
        CHECK(items == (std::vector<std::string>{"c", "d", "a"}));

        // Bad indices: factories refuse, a command whose list shrank fails.
        CHECK(ListEdit<std::string>::Remove("x", &items, 9) == nullptr);
        CHECK(ListEdit<std::string>::Move("x", &items, 0, 9) == nullptr);
        CHECK(!u.Execute(ListEdit<std::string>::Add("x", &items, 99, "z")));
        CHECK(items.size() == 3);
        CHECK(!ListEdit<int>::Move("x", static_cast<std::vector<int>*>(nullptr), 0, 0));
        // A no-op move isn't recorded.
        const std::size_t n = u.UndoCount();
        CHECK(u.Execute(ListEdit<std::string>::Move("same", &items, 1, 1)));
        CHECK(u.UndoCount() == n);

        // Record: the list was already edited (a UI did it).
        UndoStack r;
        std::vector<int> nums = {1, 2, 3};
        auto cmd = ListEdit<int>::Remove("Remove 2", &nums, 1);
        nums.erase(nums.begin() + 1);
        CHECK(r.Record(std::move(cmd)));
        CHECK(nums == (std::vector<int>{1, 3}));
        r.Undo();
        CHECK(nums == (std::vector<int>{1, 2, 3}));
        r.Redo();
        CHECK(nums == (std::vector<int>{1, 3}));

        // The list is reached through a getter: a null list (gone) makes the step stale.
        UndoStack g;
        std::vector<int> a = {5};
        std::vector<int>* live = &a;
        g.Execute(ListEdit<int>::Add("Add", [&live] { return live; }, 1, 6));
        live = nullptr;
        CHECK(!g.Undo());
        CHECK(g.UndoCount() == 0);
    }

    // --- Snapshot bridge ---------------------------------------------------------------------------------------------------------
    {
        UndoStack u;
        std::string doc = "scene v0";
        SnapshotRecorder rec(u, [&] { return doc; }, [&](const std::string& s) { doc = s; return true; });

        CHECK(!rec.Commit("nothing changed"));
        CHECK(u.UndoCount() == 0);
        doc = "scene v1";
        CHECK(rec.Commit("Edit 1"));
        doc = "scene v2";
        CHECK(rec.Commit("Edit 2"));
        CHECK(u.UndoCount() == 2 && u.NextUndoName() == "Edit 2");
        CHECK(u.Undo());
        CHECK(doc == "scene v1");
        CHECK(u.Undo());
        CHECK(doc == "scene v0");
        CHECK(u.Redo() && u.Redo());
        CHECK(doc == "scene v2");

        // New work after an undo: the baseline follows the undo, so the step is right.
        u.Undo();
        CHECK(doc == "scene v1");
        doc = "scene v1b";
        CHECK(rec.Commit("Edit 1b"));
        CHECK(u.Undo() && doc == "scene v1");
        CHECK(u.Undo() && doc == "scene v0");
        CHECK(!u.CanUndo());

        // Mixed stack: a cheap command in between re-baselines the recorder.
        UndoStack m;
        std::string d2 = "base";
        float knob = 0;
        SnapshotRecorder r2(m, [&] { return d2 + "|" + std::to_string(knob); },
                            [&](const std::string& s) {
                                const auto bar = s.find('|');
                                d2 = s.substr(0, bar);
                                knob = std::stof(s.substr(bar + 1));
                                return true;
                            });
        UndoSet(m, "Knob", &knob, 5.0f);            // a cheap command changes the captured state
        d2 = "base+";
        CHECK(r2.Commit("Rename"));                 // before = state WITH knob=5, so undo keeps it
        CHECK(m.Undo());
        CHECK(d2 == "base" && knob == 5.0f);
        CHECK(m.Undo());
        CHECK(knob == 0.0f);

        // Shared snapshot texts are counted once: 40 steps of a 10 KB scene are far below 40 * 2 * 10 KB.
        UndoStack big;
        big.SetMaxCommands(0);
        big.SetMaxMemory(0);
        std::string scene(10000, 'a');
        SnapshotRecorder r3(big, [&] { return scene; }, [&](const std::string& s) { scene = s; return true; });
        for (int i = 0; i < 40; ++i) { scene[(std::size_t)i] = 'b'; r3.Commit("e"); }
        CHECK(big.UndoCount() == 40);
        CHECK(big.MemoryBytes() < 40u * 10000u * 3u / 2u);   // ~ one text per step, not two
        CHECK(big.MemoryBytes() >= 40u * 10000u);

        // Direct SnapshotCommand with merge key.
        UndoStack sm;
        std::string t = "0";
        auto apply = [&](const std::string& s) { t = s; return true; };
        t = "1"; sm.Record(std::make_unique<SnapshotCommand>("S", apply, "0", "1", 77));
        t = "2"; sm.Record(std::make_unique<SnapshotCommand>("S", apply, "1", "2", 77));
        CHECK(sm.UndoCount() == 1);
        sm.Undo();
        CHECK(t == "0");
        sm.Redo();
        CHECK(t == "2");
        CHECK(!SnapshotCommand("x", apply, "a", "b").IsNoOp() && SnapshotCommand("x", apply, "a", "a").IsNoOp());
        // A failing apply (a parse error on restore) makes the step stale.
        UndoStack bad;
        bad.Record(std::make_unique<SnapshotCommand>("B", [](const std::string&) { return false; }, "a", "b"));
        CHECK(!bad.Undo());
        CHECK(bad.UndoCount() == 0);
    }

    // The recorder may die before or after the stack without touching freed memory.
    {
        auto* u = new UndoStack();
        std::string doc = "a";
        auto* rec = new SnapshotRecorder(*u, [&] { return doc; }, [&](const std::string& s) { doc = s; return true; });
        delete u;
        delete rec;                                 // the stack is gone: must not call into it
        UndoStack v;
        {
            std::string d = "a";
            SnapshotRecorder r2(v, [&] { return d; }, [&](const std::string& s) { d = s; return true; });
        }
        v.Execute(Trace("after recorder", nullptr));   // its listener is gone: nothing dangles
        CHECK(v.UndoCount() == 1);
    }

    // --- Object lifecycle commands (ids + resolver; the scene may reload) ----------------------------------------------------------
    {
        World w;
        auto ops = MakeOps(w);
        UndoStack u;

        // Create: the editor made the object, we record it.
        w.objs[1] = Obj{1, 0, "Root", 1.0f};
        w.order.push_back(1);
        auto createCmd = CreateObjectCommand::Make(ops, 1, "Create Root");
        CHECK(createCmd != nullptr);
        CHECK(u.Record(std::move(createCmd)));
        w.objs[2] = Obj{2, 1, "Child", 2.0f};
        w.order.push_back(2);
        w.objs[3] = Obj{3, 2, "Grandchild", 3.0f};
        w.order.push_back(3);
        CHECK(u.Record(CreateObjectCommand::Make(ops, 2, "Create Child")));   // captures the subtree

        // Delete the child subtree through the stack.
        CHECK(u.Execute(DeleteObjectCommand::Make(ops, 2, "Delete Child")));
        CHECK(w.Find(2) == nullptr && w.Find(3) == nullptr);
        CHECK(w.Find(1) != nullptr);

        // The scene reloads between steps; ids still resolve.
        w.Reload();
        CHECK(u.Undo());
        CHECK(w.Find(2) && w.Find(3));
        CHECK(w.Find(2)->name == "Child" && w.Find(3)->parent == 2 && w.Find(3)->mass == 3.0f);
        CHECK(w.Find(2)->parent == 1);
        w.Reload();
        CHECK(u.Redo());
        CHECK(!w.Find(2) && !w.Find(3));
        CHECK(u.Undo());                            // bring it back
        CHECK(u.Undo());                            // undo "Create Child": removes it
        CHECK(!w.Find(2) && !w.Find(3) && w.Find(1));
        CHECK(u.Redo());                            // redo create: back with its subtree
        CHECK(w.Find(2) && w.Find(3));

        // Deleting something that doesn't exist: Make refuses, nothing recorded.
        CHECK(DeleteObjectCommand::Make(ops, 999, "x") == nullptr);
        CHECK(CreateObjectCommand::Make(ops, 999, "x") == nullptr);
        CHECK(CreateObjectCommand::Make(nullptr, 1, "x") == nullptr);
        // Reparent.
        CHECK(u.Execute(ReparentObjectCommand::Make(ops, 3, 1, 0, "Reparent")));
        CHECK(w.Find(3)->parent == 1);
        CHECK(u.Undo());
        CHECK(w.Find(3)->parent == 2);
        CHECK(ReparentObjectCommand::Make(ops, 999, 1, 0, "x") == nullptr);
        auto same = ReparentObjectCommand::Make(ops, 3, 2, 0, "noop");   // already there
        CHECK(same && same->IsNoOp());

        // A group of deletes undoes together even if the scene reloads in between.
        UndoStack g;
        g.BeginGroup("Delete 2 objects");
        g.Execute(DeleteObjectCommand::Make(ops, 3, "Delete"));
        g.Execute(DeleteObjectCommand::Make(ops, 2, "Delete"));
        g.EndGroup();
        CHECK(!w.Find(2) && !w.Find(3));
        w.Reload();
        CHECK(g.Undo());
        CHECK(w.Find(2) && w.Find(3));
        CHECK(w.Find(3)->parent == 2);

        // One group deleting an object together with its parent comes back whole.
        UndoStack h;
        h.BeginGroup("Two deletes");
        h.Execute(DeleteObjectCommand::Make(ops, 3, "Delete 3"));
        h.Execute(DeleteObjectCommand::Make(ops, 1, "Delete 1"));   // takes 2 (and its subtree) with it
        h.EndGroup();
        CHECK(!w.Find(1) && !w.Find(2) && !w.Find(3));
        CHECK(h.Undo());                            // a healthy host: everything returns
        CHECK(w.Find(1) && w.Find(2) && w.Find(3));
    }

    // --- Atomic rollback with a real failing child (restore fails for the second object) -------------------------------------------------
    {
        World w;
        auto base = MakeOps(w);
        auto ops = std::make_shared<ObjectOps>(*base);
        int restoreCalls = 0;
        int failOn = 2;
        auto inner = base->restore;
        ops->restore = [&](ObjectId id, const std::string& blob, ObjectId parent, int index) {
            if (++restoreCalls == failOn) return false;
            return inner(id, blob, parent, index);
        };
        w.objs[1] = Obj{1, 0, "A", 1.0f}; w.order.push_back(1);
        w.objs[2] = Obj{2, 0, "B", 1.0f}; w.order.push_back(2);
        UndoStack u;
        u.BeginGroup("Delete A and B");
        u.Execute(DeleteObjectCommand::Make(ops, 1, "Delete A"));
        u.Execute(DeleteObjectCommand::Make(ops, 2, "Delete B"));
        u.EndGroup();
        CHECK(w.objs.empty());
        // Undo restores B first (call 1 ok), then A fails (call 2): B must be removed again.
        CHECK(!u.Undo());
        CHECK(w.objs.empty());
        CHECK(u.UndoCount() == 0);
    }

    // --- Stress: lots of random-ish edits keep counts consistent ---------------------------------------------------------------------------
    {
        UndoStack u;
        u.SetMaxCommands(50);
        int value = 0;
        std::vector<int> history = {0};
        unsigned seed = 12345;
        auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 16) & 0x7fff; };
        for (int i = 0; i < 2000; ++i) {
            const unsigned r = rnd() % 10;
            if (r < 6) {
                u.BreakMerge();
                UndoSet(u, "Set", &value, (int)(rnd() % 1000) + 1000 + i);
            } else if (r < 8) {
                u.Undo();
            } else {
                u.Redo();
            }
            CHECK(u.UndoCount() <= 50);
        }
        // Undo everything that is left: the value must be one that was valid, and a redo run returns.
        const int top = value;
        const std::size_t steps = u.UndoMany(1000);
        const std::size_t back = u.RedoMany(1000);
        CHECK(steps == back);
        CHECK(value == top);
    }

    TEST_MAIN_RESULT();
}
