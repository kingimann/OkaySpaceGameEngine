// Round 17: enum class, enum names as types, default member initializers.
extern "C" int printf(const char *fmt, ...);

namespace ui {
enum class Dir { Up, Down, Left = 5, Right };
enum Color : unsigned char { Red, Green = 3, Blue };

struct Node {
    int id = 7;
    float x = 1.5f, y = 0, w = 2, h = 4;
    bool on = true;
    int link[4] = {1, 2};
    Dir d = Dir::Left;
    float CenterX() const { return x + w * 0.5f; }
};

struct Opt {
    Dir vertical = Dir::Down;
    float weight{2.0f};
    int plain;
    Opt() : plain(9) {}
    Opt(int p) : weight(3.0f), plain(p) {}
};

struct Holder {
    Node node;
    int count = 3;
};

int code(Dir d) {
    switch (d) {
    case Dir::Up: return 10;
    case Dir::Down: return 20;
    default: return (int)d;
    }
}
}  // namespace ui

int main() {
    ui::Node n;
    printf("node %d %d %d %d %d %d %d\n", n.id, (int)(n.CenterX() * 10), (int)n.y, n.on, n.link[0], n.link[1] + n.link[3],
           (int)n.d);
    ui::Opt a;
    ui::Opt b(4);
    printf("opt %d %d %d %d %d\n", (int)a.vertical, (int)(a.weight * 10), a.plain, (int)(b.weight * 10), b.plain);
    ui::Holder hd;
    printf("holder %d %d\n", hd.node.id, hd.count);
    ui::Dir d = ui::Dir::Right;
    ui::Color c = ui::Blue;
    printf("enum %d %d %d %d %d\n", (int)d, ui::code(ui::Dir::Up), ui::code(ui::Dir::Down), ui::code(d), (int)c);
    printf("cmp %d %d\n", d == ui::Dir::Right, ui::Color::Green == ui::Green);
    return 0;
}
