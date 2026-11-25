// Round 24: enable_shared_from_this ([util.smartptr.enab]): shared_from_this from a member, from
// make_shared and from shared_ptr(new T), the use counts, weak_from_this, destruction when the
// last owner goes, a class with virtual functions over the base, and a class without the base.
#include <cstdio>
#include <memory>
#include <vector>
struct Tween : std::enable_shared_from_this<Tween> {
    int id;
    explicit Tween(int i) : id(i) {}
    ~Tween() { printf("~Tween %d\n", id); }
    std::shared_ptr<Tween> Self() { return shared_from_this(); }
    virtual int Kind() const { return 1; }
};
struct Fade : Tween {
    explicit Fade(int i) : Tween(i) {}
    int Kind() const override { return 2; }
};
struct Plain {
    int v = 5;
};
int main() {
    std::vector<std::shared_ptr<Tween>> keep;
    {
        auto t = std::make_shared<Tween>(1);
        keep.push_back(t->Self());
        printf("uses %ld\n", t.use_count());
        std::shared_ptr<Tween> u(new Tween(2));
        std::shared_ptr<Tween> v = u->Self();
        std::weak_ptr<Tween> w = u->weak_from_this();
        printf("uses %ld %d\n", u.use_count(), w.expired() ? 1 : 0);
    }
    printf("kept %d uses %ld\n", keep[0]->id, keep[0].use_count());
    std::shared_ptr<Tween> f = std::make_shared<Fade>(3);
    int kind = f->Self()->Kind();
    printf("kind %d uses %ld\n", kind, f.use_count());
    std::shared_ptr<Plain> p(new Plain);
    printf("%d\n", p->v);
    keep.clear();
    printf("end\n");
}
