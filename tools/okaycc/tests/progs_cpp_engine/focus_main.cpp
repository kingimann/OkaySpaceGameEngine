// Drives the engine's own engine/src/UI/FocusGraph.cpp, compiled unchanged by okay-cc.
#include "okay/UI/FocusGraph.hpp"

extern "C" int printf(const char *fmt, ...);

static void show(const char *what, const std::vector<int> &ids) {
    printf("%s:", what);
    for (int id : ids) printf(" %d", id);
    printf("\n");
}

int main() {
    okay::FocusGraph g;
    // A 3x3 grid of buttons, then a row with a disabled one and a tab-index one.
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) g.Add(10.0f + c * 50.0f, 10.0f + r * 40.0f, 40.0f, 30.0f);
    int extra = g.Add(10.0f, 200.0f, 90.0f, 30.0f);
    okay::FocusNode special;
    special.x = 120.0f;
    special.y = 200.0f;
    special.w = 40.0f;
    special.h = 30.0f;
    special.tabIndex = 1;
    int sp = g.Add(special);
    int off = g.Add(170.0f, 200.0f, 40.0f, 30.0f);
    g.SetEnabled(off, false);
    printf("size %d first %d last %d extra %d sp %d\n", (int)g.Size(), g.First(), g.Last(), extra, sp);
    show("tab", g.TabOrder());
    show("read", g.ReadingOrder());
    show("cand", g.Candidates());
    const okay::FocusDir dirs[6] = {okay::FocusDir::Right, okay::FocusDir::Down, okay::FocusDir::Left,
                                    okay::FocusDir::Up, okay::FocusDir::Next, okay::FocusDir::Previous};
    g.SetFocus(5);
    for (int k = 0; k < 6; ++k) {
        int from = g.Focus();
        int to = g.Navigate(dirs[k]);
        printf("move %d dir %d -> %d\n", from, k, to);
    }
    okay::FocusOptions none;
    none.vertical = okay::FocusWrap::None;
    none.horizontal = okay::FocusWrap::None;
    okay::FocusOptions reading;
    reading.vertical = okay::FocusWrap::ReadingOrder;
    printf("edge %d %d %d %d\n", g.Move(3, okay::FocusDir::Right), g.Move(3, okay::FocusDir::Right, none),
           g.Move(sp, okay::FocusDir::Down, reading), g.Move(1, okay::FocusDir::Up));
    g.SetLink(1, okay::FocusDir::Down, 9);
    printf("link %d %d\n", g.Move(1, okay::FocusDir::Down), g.SetLink(1, okay::FocusDir::Next, 2));
    g.PushTrap(7);
    printf("trap %d %d %d\n", g.Trapped(), g.ActiveTrap(), g.Focus());
    int t1 = g.Add(300.0f, 10.0f, 40.0f, 30.0f, 7);
    int t2 = g.Add(300.0f, 60.0f, 40.0f, 30.0f, 7);
    g.SetFocus(t1);
    int n1 = g.Navigate(okay::FocusDir::Down);
    int n2 = g.Navigate(okay::FocusDir::Down);
    printf("in trap %d %d %d\n", n1, n2, t2);
    show("trap cand", g.Candidates());
    int popped = g.PopTrap();
    printf("pop %d %d\n", popped, g.Trapped());
    int r1 = g.Remove(5);
    int r2 = g.Remove(5);
    printf("remove %d %d %d\n", r1, r2, g.Find(5) == nullptr);
    printf("score %d %d\n", (int)(okay::FocusScore(*g.Find(1), *g.Find(2), 1.0f, 0.0f) * 10),
           (int)(okay::FocusScore(*g.Find(1), *g.Find(4), 1.0f, 0.0f, 3.0f) * 10));
    g.Clear();
    printf("clear %d %d %d\n", (int)g.Size(), g.First(), g.Move(0, okay::FocusDir::Down));
    return 0;
}
