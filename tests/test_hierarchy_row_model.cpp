#include "test_framework.hpp"
#include "HierarchyRowModel.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// The Hierarchy panel's flat row list (editor/HierarchyRowModel.hpp, Finding 99): the rows equal what the old
// recursive draw visited, the visible range is right, and the list is rebuilt only when something changed.
using namespace okay;
using namespace oked::hierrows;

namespace {

// What the old DrawHierarchy visited, written as it was: roots (sorted A-Z if asked, then grouped by source
// scene), then a recursive walk that goes into a node's children only when the node is open.
std::vector<GameObject*> ReferenceWalk(Scene& s, const Model& m, bool sortAZ) {
    std::vector<GameObject*> roots, out;
    for (const auto& up : s.Objects()) if (up->transform->Parent() == nullptr) roots.push_back(up.get());
    if (sortAZ) std::sort(roots.begin(), roots.end(), [](GameObject* a, GameObject* b) { return a->name < b->name; });
    bool anyMerged = false;
    for (GameObject* r : roots) if (!r->sourceScene.empty()) anyMerged = true;
    if (anyMerged)
        std::stable_sort(roots.begin(), roots.end(), [](GameObject* a, GameObject* b) { return a->sourceScene < b->sourceScene; });
    std::function<void(GameObject*)> walk = [&](GameObject* n) {
        out.push_back(n);
        if (n->transform->ChildCount() == 0 || !m.IsOpen(n)) return;
        std::vector<Transform*> kids(n->transform->Children().begin(), n->transform->Children().end());
        if (sortAZ) std::sort(kids.begin(), kids.end(), [](Transform* a, Transform* b) { return a->gameObject->name < b->gameObject->name; });
        for (Transform* k : kids) walk(k->gameObject);
    };
    for (GameObject* r : roots) walk(r);
    return out;
}

GameObject* Make(Scene& s, const char* name, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

int Depth(GameObject* g) { int d = 0; for (Transform* p = g->transform->Parent(); p; p = p->Parent()) ++d; return d; }

bool RowsMatch(Scene& s, Model& m, bool sortAZ) {
    m.Refresh(s, sortAZ);
    const std::vector<GameObject*> ref = ReferenceWalk(s, m, sortAZ);
    if (ref != m.ObjectRows()) return false;
    for (std::size_t i = 0; i < ref.size(); ++i) {
        const int r = m.RowOf(ref[i]);
        if (r < 0 || m.Rows()[(std::size_t)r].go != ref[i] || m.Rows()[(std::size_t)r].depth != Depth(ref[i])) return false;
        if (m.Rows()[(std::size_t)r].objectIndex != (int)i) return false;
    }
    return true;
}

} // namespace

int main() {
    RUN_SUITE("hierarchy_row_model");

    // ---- A small tree: the rows are the old walk's rows, deeper levels start closed --------------------
    {
        Scene s("tree");
        GameObject* a = Make(s, "A");
        GameObject* a1 = Make(s, "A1", a);
        GameObject* a2 = Make(s, "A2", a);
        GameObject* a2a = Make(s, "A2a", a2);
        GameObject* b = Make(s, "B");
        GameObject* c = Make(s, "C");
        GameObject* c1 = Make(s, "C1", c);
        Model m;
        CHECK(m.Refresh(s, false));
        CHECK(RowsMatch(s, m, false));
        CHECK((m.ObjectRows() == std::vector<GameObject*>{a, a1, a2, b, c, c1}));   // A2 starts closed: A2a hidden
        CHECK(m.RowOf(a2a) == -1);
        CHECK(m.Rows()[(std::size_t)m.RowOf(a2)].hasChildren && !m.Rows()[(std::size_t)m.RowOf(a2)].open);
        CHECK(m.Rows()[(std::size_t)m.RowOf(a)].open);

        // Nothing changed: no rebuild.
        const auto before = m.Rebuilds();
        CHECK(!m.Refresh(s, false));
        CHECK(!m.Refresh(s, false));
        CHECK(m.Rebuilds() == before);

        // Opening a node rebuilds once and lists its children.
        m.SetOpen(a2, true);
        CHECK(m.Refresh(s, false));
        CHECK(RowsMatch(s, m, false));
        CHECK(m.RowOf(a2a) == m.RowOf(a2) + 1);
        CHECK(m.Rows()[(std::size_t)m.RowOf(a2a)].depth == 2);
        CHECK(!m.Refresh(s, false));
        m.SetOpen(a2, true);                 // already open: not a change
        CHECK(!m.Refresh(s, false));

        // Closing a root hides its whole subtree.
        m.SetOpen(a, false);
        CHECK(RowsMatch(s, m, false));
        CHECK((m.ObjectRows() == std::vector<GameObject*>{a, b, c, c1}));
        m.SetOpen(a, true);

        // A new object, a reparent and a destroy each rebuild.
        auto n0 = m.Rebuilds();
        GameObject* d = Make(s, "D");
        CHECK(m.Refresh(s, false) && m.Rebuilds() == n0 + 1);
        CHECK(RowsMatch(s, m, false));
        d->transform->SetParent(c->transform, false);
        CHECK(m.Refresh(s, false));
        CHECK(RowsMatch(s, m, false));
        CHECK(m.RowOf(d) == m.RowOf(c1) + 1);
        s.DestroyImmediate(b);
        CHECK(m.Refresh(s, false));
        CHECK(RowsMatch(s, m, false));
        CHECK(m.RowOf(b) == -1);

        // Renames do not matter unsorted; with Sort A-Z they reorder the rows.
        a1->name = "Z-last";
        CHECK(!m.Refresh(s, false));
        CHECK(m.Refresh(s, true));
        CHECK(RowsMatch(s, m, true));
        CHECK(m.RowOf(a2) < m.RowOf(a1));
        a1->name = "A0";
        CHECK(m.Refresh(s, true));
        CHECK(RowsMatch(s, m, true));
        CHECK(m.RowOf(a1) < m.RowOf(a2));
        CHECK(m.Refresh(s, false));            // turning the sort off is a change too
        CHECK(RowsMatch(s, m, false));

        // Expand all / Collapse all / a subtree / reveal.
        m.SetAllOpen(s, false);
        CHECK(RowsMatch(s, m, false));
        CHECK(m.ObjectRows().size() == 2);     // A and C (B was destroyed)
        m.SetAllOpen(s, true);
        CHECK(RowsMatch(s, m, false));
        CHECK(m.RowOf(a2a) >= 0);
        m.SetSubtreeOpen(a, false);
        CHECK(RowsMatch(s, m, false));
        CHECK(m.RowOf(a1) == -1 && m.RowOf(a2a) == -1);
        m.OpenAncestors(a2a);
        CHECK(RowsMatch(s, m, false));
        CHECK(m.RowOf(a2a) >= 0);
    }

    // ---- Combined scenes: roots grouped by source scene, a header row before each group ---------------
    {
        Scene s("merged");
        GameObject* m1 = Make(s, "Main1");
        GameObject* x1 = Make(s, "X1"); x1->sourceScene = "Extra.okayscene";
        GameObject* m2 = Make(s, "Main2");
        GameObject* x2 = Make(s, "X2"); x2->sourceScene = "Extra.okayscene";
        Make(s, "X1child", x1);
        Model m;
        CHECK(RowsMatch(s, m, false));
        const auto& rows = m.Rows();
        CHECK(rows.size() == 7);
        CHECK(rows[0].header && rows[0].section.empty());
        CHECK(rows[1].go == m1 && rows[2].go == m2);
        CHECK(rows[3].header && rows[3].section == "Extra.okayscene");
        CHECK(rows[4].go == x1 && rows[5].depth == 1 && rows[6].go == x2);
    }

    // ---- 3,000 objects in groups of 50 (Finding 99's scene): rows, clipping and rebuild cost ----------
    {
        Scene s("big");
        std::vector<GameObject*> groups;
        for (int gi = 0; gi < 60; ++gi) {
            GameObject* g = Make(s, ("Group " + std::to_string(gi)).c_str());
            groups.push_back(g);
            for (int k = 0; k < 49; ++k) Make(s, "Cube", g);
        }
        CHECK(s.Objects().size() == 3000);
        Model m;
        CHECK(RowsMatch(s, m, false));
        CHECK(m.ObjectRows().size() == 3000);   // top-level groups start open: every row is listed
        // Only about a screenful is drawn: 20 rows of 18 px in a 360 px view, scrolled to row 1,000.
        const float rowH = 18.0f, viewH = 360.0f;
        ClipRange r = VisibleRange((int)m.Rows().size(), rowH, 1000.0f * rowH, viewH);
        CHECK(r.first == 999 && r.last == 1022);
        CHECK(r.last - r.first <= 23);
        // Edges: the top, the very bottom, an empty list, an unknown row height, a view taller than the list.
        CHECK((VisibleRange(3000, rowH, 0.0f, viewH) == ClipRange{0, 22}));
        CHECK((VisibleRange(3000, rowH, 2990.0f * rowH, viewH) == ClipRange{2989, 3000}));
        CHECK((VisibleRange(0, rowH, 0.0f, viewH) == ClipRange{0, 0}));
        CHECK((VisibleRange(10, 0.0f, 0.0f, viewH) == ClipRange{0, 10}));
        CHECK((VisibleRange(10, rowH, 0.0f, 10000.0f) == ClipRange{0, 10}));
        CHECK((VisibleRange(10, rowH, -50.0f, viewH) == ClipRange{0, 10}));
        // Every row in view is inside the range (a sweep over scroll positions).
        bool covered = true;
        for (float sy = 0.0f; sy < 2980.0f * rowH; sy += 7.3f) {
            ClipRange cr = VisibleRange(3000, rowH, sy, viewH);
            const int firstSeen = (int)(sy / rowH), lastSeen = std::min(2999, (int)((sy + viewH) / rowH));
            if (cr.first > firstSeen || cr.last <= lastSeen) covered = false;
        }
        CHECK(covered);
        // Reveal: a row already in view keeps the scroll; one below is brought 40% down the view.
        CHECK(RevealScroll(1005, rowH, 1000.0f * rowH, viewH) == 1000.0f * rowH);
        const float rs = RevealScroll(2500, rowH, 0.0f, viewH);
        ClipRange after = VisibleRange(3000, rowH, rs, viewH, 0);
        CHECK(2500 >= after.first && 2500 < after.last);
        CHECK(RevealScroll(0, rowH, 500.0f, viewH) == 0.0f);
        // Selecting a deep, hidden object: open its parents and it gets a row.
        m.SetOpen(groups[59], false);
        m.Refresh(s, false);
        GameObject* deep = groups[59]->transform->Children()[20]->gameObject;
        CHECK(m.RowOf(deep) == -1);
        m.OpenAncestors(deep);
        m.Refresh(s, false);
        CHECK(m.RowOf(deep) >= 0);
        // A frame with nothing changed costs a version compare, not a walk of 3,000 objects.
        using Clock = std::chrono::steady_clock;
        auto t0 = Clock::now();
        int rebuilt = 0;
        for (int f = 0; f < 1000; ++f) rebuilt += m.Refresh(s, false) ? 1 : 0;
        auto t1 = Clock::now();
        CHECK(rebuilt == 0);
        m.Invalidate();
        auto t2 = Clock::now();
        m.Refresh(s, false);
        auto t3 = Clock::now();
        std::printf("  3,000 objects: unchanged refresh %.4f ms per frame, one rebuild %.3f ms, rows drawn per frame %d of %d\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count() / 1000.0,
                    std::chrono::duration<double, std::milli>(t3 - t2).count(), r.last - r.first, (int)m.Rows().size());
    }

    TEST_MAIN_RESULT();
}
