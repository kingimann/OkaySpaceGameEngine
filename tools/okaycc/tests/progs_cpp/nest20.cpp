// Round 20: classes whose member functions take earlier classes by reference. Each pointer or
// reference used to copy the whole class it names, so the types grew exponentially with the
// chain (Optimize/PortalCells.cpp did not finish). Now it compiles at once.
extern "C" int printf(const char *fmt, ...);
struct C0 {
    int v = 1;
    int get() const { return v; }
};
struct C1 {
    int v = 2;
    int mix(const C0 &a, const C0 &b) const { return v + a.get() + b.get(); }
    int twice(const C0 &a, const C0 *p) const { return a.get() + p->get(); }
    int pick(const C0 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C2 {
    int v = 3;
    int mix(const C1 &a, const C0 &b) const { return v + a.get() + b.get(); }
    int twice(const C1 &a, const C0 *p) const { return a.get() + p->get(); }
    int pick(const C1 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C3 {
    int v = 4;
    int mix(const C2 &a, const C1 &b) const { return v + a.get() + b.get(); }
    int twice(const C2 &a, const C1 *p) const { return a.get() + p->get(); }
    int pick(const C2 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C4 {
    int v = 5;
    int mix(const C3 &a, const C2 &b) const { return v + a.get() + b.get(); }
    int twice(const C3 &a, const C2 *p) const { return a.get() + p->get(); }
    int pick(const C3 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C5 {
    int v = 6;
    int mix(const C4 &a, const C3 &b) const { return v + a.get() + b.get(); }
    int twice(const C4 &a, const C3 *p) const { return a.get() + p->get(); }
    int pick(const C4 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C6 {
    int v = 7;
    int mix(const C5 &a, const C4 &b) const { return v + a.get() + b.get(); }
    int twice(const C5 &a, const C4 *p) const { return a.get() + p->get(); }
    int pick(const C5 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C7 {
    int v = 8;
    int mix(const C6 &a, const C5 &b) const { return v + a.get() + b.get(); }
    int twice(const C6 &a, const C5 *p) const { return a.get() + p->get(); }
    int pick(const C6 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C8 {
    int v = 9;
    int mix(const C7 &a, const C6 &b) const { return v + a.get() + b.get(); }
    int twice(const C7 &a, const C6 *p) const { return a.get() + p->get(); }
    int pick(const C7 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C9 {
    int v = 10;
    int mix(const C8 &a, const C7 &b) const { return v + a.get() + b.get(); }
    int twice(const C8 &a, const C7 *p) const { return a.get() + p->get(); }
    int pick(const C8 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C10 {
    int v = 11;
    int mix(const C9 &a, const C8 &b) const { return v + a.get() + b.get(); }
    int twice(const C9 &a, const C8 *p) const { return a.get() + p->get(); }
    int pick(const C9 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C11 {
    int v = 12;
    int mix(const C10 &a, const C9 &b) const { return v + a.get() + b.get(); }
    int twice(const C10 &a, const C9 *p) const { return a.get() + p->get(); }
    int pick(const C10 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C12 {
    int v = 13;
    int mix(const C11 &a, const C10 &b) const { return v + a.get() + b.get(); }
    int twice(const C11 &a, const C10 *p) const { return a.get() + p->get(); }
    int pick(const C11 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C13 {
    int v = 14;
    int mix(const C12 &a, const C11 &b) const { return v + a.get() + b.get(); }
    int twice(const C12 &a, const C11 *p) const { return a.get() + p->get(); }
    int pick(const C12 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C14 {
    int v = 15;
    int mix(const C13 &a, const C12 &b) const { return v + a.get() + b.get(); }
    int twice(const C13 &a, const C12 *p) const { return a.get() + p->get(); }
    int pick(const C13 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C15 {
    int v = 16;
    int mix(const C14 &a, const C13 &b) const { return v + a.get() + b.get(); }
    int twice(const C14 &a, const C13 *p) const { return a.get() + p->get(); }
    int pick(const C14 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C16 {
    int v = 17;
    int mix(const C15 &a, const C14 &b) const { return v + a.get() + b.get(); }
    int twice(const C15 &a, const C14 *p) const { return a.get() + p->get(); }
    int pick(const C15 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C17 {
    int v = 18;
    int mix(const C16 &a, const C15 &b) const { return v + a.get() + b.get(); }
    int twice(const C16 &a, const C15 *p) const { return a.get() + p->get(); }
    int pick(const C16 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C18 {
    int v = 19;
    int mix(const C17 &a, const C16 &b) const { return v + a.get() + b.get(); }
    int twice(const C17 &a, const C16 *p) const { return a.get() + p->get(); }
    int pick(const C17 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C19 {
    int v = 20;
    int mix(const C18 &a, const C17 &b) const { return v + a.get() + b.get(); }
    int twice(const C18 &a, const C17 *p) const { return a.get() + p->get(); }
    int pick(const C18 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C20 {
    int v = 21;
    int mix(const C19 &a, const C18 &b) const { return v + a.get() + b.get(); }
    int twice(const C19 &a, const C18 *p) const { return a.get() + p->get(); }
    int pick(const C19 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C21 {
    int v = 22;
    int mix(const C20 &a, const C19 &b) const { return v + a.get() + b.get(); }
    int twice(const C20 &a, const C19 *p) const { return a.get() + p->get(); }
    int pick(const C20 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C22 {
    int v = 23;
    int mix(const C21 &a, const C20 &b) const { return v + a.get() + b.get(); }
    int twice(const C21 &a, const C20 *p) const { return a.get() + p->get(); }
    int pick(const C21 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
struct C23 {
    int v = 24;
    int mix(const C22 &a, const C21 &b) const { return v + a.get() + b.get(); }
    int twice(const C22 &a, const C21 *p) const { return a.get() + p->get(); }
    int pick(const C22 &a) const { return a.get() > v ? a.get() : v; }
    int get() const { return v; }
};
int main() {
    C0 c0;
    C1 c1;
    C2 c2;
    C3 c3;
    C4 c4;
    C5 c5;
    C6 c6;
    C7 c7;
    C8 c8;
    C9 c9;
    C10 c10;
    C11 c11;
    C12 c12;
    C13 c13;
    C14 c14;
    C15 c15;
    C16 c16;
    C17 c17;
    C18 c18;
    C19 c19;
    C20 c20;
    C21 c21;
    C22 c22;
    C23 c23;
    int t = 0;
    t += c2.mix(c1, c0) + c2.twice(c1, &c0) + c2.pick(c1);
    t += c3.mix(c2, c1) + c3.twice(c2, &c1) + c3.pick(c2);
    t += c4.mix(c3, c2) + c4.twice(c3, &c2) + c4.pick(c3);
    t += c5.mix(c4, c3) + c5.twice(c4, &c3) + c5.pick(c4);
    t += c6.mix(c5, c4) + c6.twice(c5, &c4) + c6.pick(c5);
    t += c7.mix(c6, c5) + c7.twice(c6, &c5) + c7.pick(c6);
    t += c8.mix(c7, c6) + c8.twice(c7, &c6) + c8.pick(c7);
    t += c9.mix(c8, c7) + c9.twice(c8, &c7) + c9.pick(c8);
    t += c10.mix(c9, c8) + c10.twice(c9, &c8) + c10.pick(c9);
    t += c11.mix(c10, c9) + c11.twice(c10, &c9) + c11.pick(c10);
    t += c12.mix(c11, c10) + c12.twice(c11, &c10) + c12.pick(c11);
    t += c13.mix(c12, c11) + c13.twice(c12, &c11) + c13.pick(c12);
    t += c14.mix(c13, c12) + c14.twice(c13, &c12) + c14.pick(c13);
    t += c15.mix(c14, c13) + c15.twice(c14, &c13) + c15.pick(c14);
    t += c16.mix(c15, c14) + c16.twice(c15, &c14) + c16.pick(c15);
    t += c17.mix(c16, c15) + c17.twice(c16, &c15) + c17.pick(c16);
    t += c18.mix(c17, c16) + c18.twice(c17, &c16) + c18.pick(c17);
    t += c19.mix(c18, c17) + c19.twice(c18, &c17) + c19.pick(c18);
    t += c20.mix(c19, c18) + c20.twice(c19, &c18) + c20.pick(c19);
    t += c21.mix(c20, c19) + c21.twice(c20, &c19) + c21.pick(c20);
    t += c22.mix(c21, c20) + c22.twice(c21, &c20) + c22.pick(c21);
    t += c23.mix(c22, c21) + c23.twice(c22, &c21) + c23.pick(c22);
    printf("%d\n", t);
    return 0;
}
