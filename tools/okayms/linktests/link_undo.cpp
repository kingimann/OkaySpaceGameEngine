// Link-and-run test of a bigger engine subsystem (Round 37): engine/src/Core/Undo.cpp, the editor's undo history
// (commands behind virtual functions, unique_ptr ownership, a deque of steps, groups, merging, the save point,
// count and memory caps, listeners kept in std::function), compiled by okay-cl into an object of its own and linked
// with this test. It drives the stack the way the editor does and prints what it saw. Built by okay-cl (/O0 /O1 /O2)
// and by MinGW g++, run under Wine; the exit code and the printed text must be the same.
#include "mini.hpp"
#include "okay/Core/Undo.hpp"
#include <memory>
#include <string>
#include <vector>

using namespace okay;

namespace {
// Sets an int from one value to another; edits of the same int merge into one step.
class SetInt : public ICommand {
public:
    SetInt(std::string name, int* target, int from, int to) : m_name(std::move(name)), m_target(target), m_from(from), m_to(to) {}
    bool Do() override {
        *m_target = m_to;
        return true;
    }
    bool Undo() override {
        *m_target = m_from;
        return true;
    }
    std::string Name() const override { return m_name; }
    bool Merge(const ICommand& next) override {
        // Only SetInt commands share a name with a SetInt here (no run-time type information in the g++ build).
        if (next.Name() != m_name) return false;
        const SetInt* o = static_cast<const SetInt*>(&next);
        if (o->m_target != m_target) return false;
        m_to = o->m_to;
        return true;
    }
    std::size_t MemoryBytes() const override { return 100; }
    bool IsNoOp() const override { return m_from == m_to; }

private:
    std::string m_name;
    int* m_target;
    int m_from;
    int m_to;
};

// A command that fails to apply.
class Broken : public ICommand {
public:
    bool Do() override { return false; }
    bool Undo() override { return false; }
    std::string Name() const override { return "Broken"; }
};

std::unique_ptr<ICommand> Set(const char* name, int* v, int to) {
    std::unique_ptr<ICommand> c(new SetInt(name, v, *v, to));
    return c;
}
}  // namespace

int main() {
    RUN_SUITE("undo");
    UndoStack undo;
    int pushed = 0;
    std::string last;
    const int id = undo.AddListener([&](const UndoStack::Notice& n) {
        if (n.event == UndoStack::Event::Pushed) ++pushed;
        last = n.name;
    });
    int a = 1;
    int b = 10;
    CHECK(undo.Execute(Set("Set A", &a, 2)));
    CHECK(undo.Execute(Set("Set B", &b, 20)));
    CHECK(a == 2 && b == 20 && undo.UndoCount() == 2 && pushed == 2 && last == "Set B");
    CHECK(undo.NextUndoName() == "Set B");
    CHECK(undo.Undo() && b == 10 && undo.CanRedo() && undo.NextRedoName() == "Set B");
    CHECK(undo.Redo() && b == 20);
    undo.MarkSaved();
    CHECK(!undo.IsDirty());
    CHECK(undo.Execute(Set("Set A", &a, 3)));
    CHECK(undo.IsDirty());
    CHECK(undo.Undo() && !undo.IsDirty() && a == 2);
    // A group is one step; undo rolls back every member.
    CHECK(undo.BeginGroup("Both"));
    CHECK(undo.Execute(Set("Set A", &a, 7)));
    CHECK(undo.Execute(Set("Set B", &b, 70)));
    CHECK(undo.EndGroup());
    CHECK(a == 7 && b == 70 && undo.NextUndoName() == "Both");
    CHECK(undo.Undo() && a == 2 && b == 20);
    CHECK(undo.Redo() && a == 7 && b == 70);
    // A group scope that is cancelled leaves nothing.
    {
        UndoGroup g(undo, "Cancelled");
        CHECK(undo.Execute(Set("Set A", &a, 99)));
        CHECK(g.Cancel());
    }
    CHECK(a == 7 && undo.NextUndoName() == "Both");
    // Merging: inside the merge window, edits of one int become one step.
    undo.SetMergeWindow(1.0f);
    const std::size_t before = undo.UndoCount();
    CHECK(undo.Record(Set("Drag", &a, 8)));
    a = 8;
    CHECK(undo.Record(Set("Drag", &a, 9)));
    a = 9;
    CHECK(undo.UndoCount() == before + 1);
    CHECK(undo.Undo() && a == 7);
    undo.BreakMerge();
    // A command that fails is not recorded.
    std::unique_ptr<ICommand> broken(new Broken());
    CHECK(!undo.Execute(std::move(broken)));
    // The count cap drops the oldest steps.
    undo.SetMaxCommands(3);
    static const char* const kSteps[] = {"Step 0", "Step 1", "Step 2", "Step 3", "Step 4", "Step 5"};
    for (int i = 0; i < 6; ++i) CHECK(undo.Execute(Set(kSteps[i], &b, 100 + i)));  // other names: no merging
    CHECK(undo.UndoCount() == 3 && b == 105);
    const std::vector<std::string> names = undo.UndoNames(2);
    CHECK(names.size() == 2 && names[0] == "Step 5" && names[1] == "Step 4");
    CHECK(undo.UndoMany(5) == 3 && b == 102);
    CHECK(undo.MemoryBytes() >= 300);
    undo.RemoveListener(id);
    const int pushed_before = pushed;
    CHECK(undo.Execute(Set("Quiet", &a, 1)));
    CHECK(pushed == pushed_before);
    undo.Clear();
    CHECK(undo.UndoCount() == 0 && undo.RedoCount() == 0 && !undo.IsDirty());
    std::printf("a %d b %d pushed %d revision %d last %s\n", a, b, pushed, (int)undo.Revision(), last.c_str());
    TEST_MAIN_RESULT();
}
