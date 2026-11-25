// A scoped enumeration's earlier enumerators are seen by their bare names in its own list
// ([dcl.enum] p2, [basic.scope.enum]), and only there.
#include <cstdio>
enum class Mask : unsigned {
    None = 0,
    A = 1u << 0,
    B = 1u << 1,
    C = 1u << 2,
    AB = A | B,
    All = A | B | C,
    NotA = 7 & ~A,
};
enum class N { X = 3, Y = X + 1, Z = Y * 2 };
int A = 40;  // the file's own A, not Mask::A
int main() {
    std::printf("%u %u %u %d %d %d\n", (unsigned)Mask::AB, (unsigned)Mask::All, (unsigned)Mask::NotA, (int)N::Y,
                (int)N::Z, A);
}
