// Round 33: a conditional of two lvalues returned from a function that returns a reference
// ([expr.cond] p4, [stmt.return]); engine/include/okay/Components/LedgeHandsIK.hpp's Arm(i).
#include <cstdio>
struct Limb {
    int v;
};
struct Hold {
    Limb* leftArm = nullptr;
    Limb* rightArm = nullptr;
    int counts[2] = {0, 0};
    Limb*& Arm(int i) { return i == 0 ? leftArm : rightArm; }
    int& Count(bool second) { return second ? counts[1] : (counts[0]); }
    const int& Pick(int i) const { return i > 0 ? counts[1] : i < 0 ? counts[0] : counts[1]; }
};
static double gx = 1.5, gy = 2.5;
double& Either(bool a) { return a ? gx : gy; }
int main() {
    Hold h;
    Limb a{3}, b{4};
    h.Arm(1) = &a;
    h.Arm(0) = &b;
    ++h.Count(true);
    h.Count(false) += 5;
    Either(false) = 7.25;
    std::printf("%d %d %d %d %d %d\n", h.rightArm->v, h.leftArm->v, h.counts[0], h.counts[1], h.Pick(-1), h.Pick(0));
    std::printf("%.2f %.2f\n", gx, gy);
    return h.Arm(0) == &b ? 0 : 1;
}
