// A class with an array member of a class that has a copy constructor (Curve loc[3], Curve owning
// a std::vector) is copied element by element by that constructor; it was copied as bytes, so the
// copy and the original shared one buffer (silent wrong values). Also T(obj) and T(std::move(obj))
// for a class whose only constructor is the default one of its member initializers, and a
// two-dimensional array member with "= {}".
#include <cstdio>
#include <utility>
#include <vector>
struct Key { float x = 0; };
struct Curve { std::vector<Key> keys; int ipo = 0; };
struct Target {
    int node = -1;
    Curve loc[3], scl[2];
    bool has[5][4] = {};
};
struct Outer {
    struct Target2 {
        Curve c[2];
        int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
    };
    std::vector<Target2> ts;
    float Run() {
        Target2 t;
        t.c[1].keys.push_back(Key{2.5f});
        ts.push_back(t);
        t.c[1].keys[0].x = 9;
        return ts[0].c[1].keys[0].x + (float)ts[0].grid[1][2];
    }
};
int main() {
    Target t;
    t.loc[1].keys.push_back(Key{2.5f});
    t.has[2][3] = true;
    std::vector<Target> ts;
    ts.push_back(t);
    t.loc[1].keys[0].x = 4.0f;
    Target u = t;
    t.loc[1].keys.push_back(Key{1.0f});
    Target w;
    w = u;
    std::printf("%.1f %zu %.1f %zu %d %d\n", ts[0].loc[1].keys[0].x, t.loc[1].keys.size(), u.loc[1].keys[0].x,
                u.loc[1].keys.size(), (int)ts[0].has[2][3], (int)w.has[2][3]);
    std::vector<Curve> cs;
    cs.push_back(t.loc[1]);
    std::printf("%.1f %zu\n", cs[0].keys[1].x, cs[0].keys.size());
    Outer o;
    std::printf("%.1f\n", o.Run());
    Key k{3.5f};
    Key k2(k);
    Key k3(std::move(k2));
    std::printf("%.1f %.1f\n", k2.x, k3.x);
    return 0;
}
