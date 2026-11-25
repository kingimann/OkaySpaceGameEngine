// Round 21: our own <memory> (unique_ptr, make_unique, shared_ptr, weak_ptr, make_shared) against libstdc++.
#include <memory>
#include <utility>
#include <vector>

extern "C" int printf(const char *fmt, ...);

static int live = 0;

struct Base {
    int id;
    Base(int i) : id(i) {
        ++live;
        printf("make %d\n", id);
    }
    virtual ~Base() {
        --live;
        printf("drop %d\n", id);
    }
    virtual int kind() const { return 1; }
};

struct Derived : Base {
    int extra;
    Derived(int i, int e) : Base(i), extra(e) {}
    ~Derived() override { printf("drop derived %d\n", extra); }
    int kind() const override { return 2; }
};

struct Pair {
    int a;
    int b;
    Pair(int x, int y) : a(x), b(y) {}
};

struct CountingDeleter {
    int *calls;
    void operator()(Pair *p) const {
        ++*calls;
        delete p;
    }
};

std::unique_ptr<Base> makeBase(int i) {
    if (i > 5) return std::make_unique<Derived>(i, i * 10);
    return std::make_unique<Base>(i);
}

struct Holder {
    std::unique_ptr<Base> item;
    std::vector<std::unique_ptr<Base>> list;
};

int main() {
    {
        std::unique_ptr<Base> a = std::make_unique<Base>(1);
        std::unique_ptr<Base> b;
        printf("%d %d %d\n", a ? 1 : 0, b ? 1 : 0, a->id);
        b = std::move(a);
        printf("%d %d %d\n", a ? 1 : 0, b ? 1 : 0, (*b).id);
        b.reset(new Base(2));
        Base *raw = b.release();
        printf("released %d %d\n", raw->id, b == nullptr ? 1 : 0);
        delete raw;
        std::unique_ptr<Base> c = makeBase(7);
        std::unique_ptr<Base> d = makeBase(3);
        printf("kinds %d %d live %d\n", c->kind(), d->kind(), live);
        c.swap(d);
        printf("after swap %d %d\n", c->id, d->id);
    }
    printf("live %d\n", live);
    {
        int calls = 0;
        {
            std::unique_ptr<Pair, CountingDeleter> p(new Pair(3, 4), CountingDeleter{&calls});
            printf("pair %d %d\n", p->a, p.get()->b);
        }
        printf("deleter calls %d\n", calls);
        std::unique_ptr<int[]> arr(new int[5]);
        for (int i = 0; i < 5; ++i) arr[i] = i * i;
        printf("arr %d %d\n", arr[2], arr[4]);
    }
    {
        Holder h;
        h.item = std::make_unique<Base>(11);
        h.list.push_back(std::make_unique<Base>(12));
        h.list.push_back(makeBase(13));
        h.list.emplace_back(new Base(14));
        printf("holder %d %d %d\n", h.item->id, (int)h.list.size(), h.list[1]->kind());
    }
    printf("live %d\n", live);
    {
        std::shared_ptr<Base> s1 = std::make_shared<Base>(21);
        printf("use %ld\n", s1.use_count());
        std::weak_ptr<Base> w = s1;
        {
            std::shared_ptr<Base> s2 = s1;
            printf("use %ld %d\n", s1.use_count(), s2->id);
            std::shared_ptr<Base> s3 = w.lock();
            printf("use %ld expired %d\n", s1.use_count(), w.expired() ? 1 : 0);
        }
        printf("use %ld\n", s1.use_count());
        std::shared_ptr<Base> s4 = std::make_shared<Derived>(22, 220);
        std::shared_ptr<Base> s5(new Derived(23, 230));
        printf("kinds %d %d live %d\n", s4->kind(), s5->kind(), live);
        s1.reset();
        printf("expired %d %d\n", w.expired() ? 1 : 0, w.lock() ? 1 : 0);
        std::shared_ptr<Pair> pp = std::make_shared<Pair>(5, 6);
        std::shared_ptr<Pair> moved = std::move(pp);
        printf("moved %d %d %d\n", pp ? 1 : 0, moved->a, (int)moved.use_count());
    }
    printf("live %d\n", live);
    return 0;
}
