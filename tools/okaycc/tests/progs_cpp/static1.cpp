// Round 16: static members, constexpr, named casts, unnamed and nested namespaces, nested braces.
extern "C" int printf(const char *fmt, ...);

namespace outer {
namespace {
const unsigned char kTable[3][4] = {
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {9, 10, 11, 12},
};
int hidden(int x) { return x * 2; }
}  // namespace

struct Grid {
    static constexpr int Width = 4;
    static constexpr int Height = 3;
    static constexpr double Scale = 2.5;
    static constexpr float Half = 0.5f;
    static int created;
    int id;
    short lo, *none, pair[2];

    Grid() : id(0), lo(3), none(nullptr) {
        ++created;
        id = created;
        pair[0] = 4;
        pair[1] = 5;
    }
    static const unsigned char *Row(int y);
    static bool Cell(int x, int y) {
        if (x < 0 || y < 0 || x >= Width || y >= Height) return false;
        return (Row(y)[x] & 1u) != 0;
    }
    static int Count() { return created; }
    int Twice() const;
};

int Grid::created = 0;

const unsigned char *Grid::Row(int y) { return kTable[y]; }

int Grid::Twice() const { return hidden(id) + Width; }

}  // namespace outer

namespace a::b {
constexpr int kDepth = 7;
int depth() { return kDepth + 1; }
}  // namespace a::b

constexpr long kBig = 1L << 40;
constexpr double kNeg = -1.25;
const int kMask = ~0xF0 & 0xFF;

int main() {
    int on = 0;
    for (int y = 0; y < outer::Grid::Height; ++y)
        for (int x = 0; x < outer::Grid::Width; ++x)
            if (outer::Grid::Cell(x, y)) ++on;
    printf("on %d\n", on);
    printf("row %d %d\n", outer::Grid::Row(2)[3], outer::Grid::Cell(9, 0));
    outer::Grid g1;
    outer::Grid g2;
    printf("count %d %d %d\n", outer::Grid::Count(), outer::Grid::created, g2.id);
    printf("twice %d %d fields %d %d %d %d\n", g1.Twice(), g2.Twice(), g1.lo, g1.none == nullptr, g2.pair[0] + g2.pair[1],
           static_cast<int>(sizeof(outer::Grid)));
    printf("depth %d %d\n", a::b::depth(), a::b::kDepth);
    long big = kBig;
    int small = static_cast<int>(big >> 38);
    const unsigned char *p = reinterpret_cast<const unsigned char *>(&big);
    printf("cast %d %d %u\n", small, static_cast<int>(p[5]), static_cast<unsigned>(static_cast<unsigned char>(300)));
    printf("fp %d %d %d mask %d\n", static_cast<int>(outer::Grid::Scale * 4), static_cast<int>(outer::Grid::Half * 10),
           static_cast<int>(kNeg * 8), kMask);
    int local[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int partial[4] = {7};
    printf("local %d %d %d %d\n", local[0][2], local[1][0], partial[0], partial[3]);
    return on;
}
