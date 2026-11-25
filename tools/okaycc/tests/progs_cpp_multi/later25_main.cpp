// A class whose constructor and destructor are declared in it and defined in another file
// ([class.ctor], [class.dtor]): arrays of it (new[], delete[]) call them through helpers made at
// the end of the class, which need their prototypes. Round 24: "undeclared identifier
// '_ZN5LaterC1Ev'". The other file is later25_def.cpp.
#include <cstdio>
#include <vector>
struct Later {
    Later();
    explicit Later(int n);
    ~Later();
    std::vector<int> v;
};
int main() {
    Later *a = new Later[2];
    a[1].v.push_back(3);
    std::printf("%d %d\n", (int)a[0].v.size(), a[1].v[1]);
    delete[] a;
    {
        Later b(4);
        std::printf("%d\n", (int)b.v.size());
    }
    return 0;
}
