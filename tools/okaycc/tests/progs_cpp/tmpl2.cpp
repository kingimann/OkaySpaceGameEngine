// Class templates: type and value parameters, member functions, constructors and destructors,
// nested instances, and a virtual function in a template.
extern "C" int printf(const char *fmt, ...);

template <typename T>
class Box {
public:
    T value;
    Box(T v) : value(v) { printf("Box(%d)\n", (int)sizeof(T)); }
    ~Box() { printf("~Box(%d)\n", (int)sizeof(T)); }
    T get() { return value; }
    void set(T v) { value = v; }
    T twice() { return value + value; }
};

template <typename T, int N>
struct Stack {
    T items[N];
    int top;
    Stack() : top(0) {}
    void push(T v) {
        if (top < N) {
            items[top] = v;
            top = top + 1;
        }
    }
    T pop() {
        top = top - 1;
        return items[top];
    }
    int count() { return top; }
    int capacity() { return N; }
};

template <typename T>
struct Pair {
    T first;
    T second;
    Pair(T a, T b) : first(a), second(b) {}
    T sum() { return first + second; }
};

template <typename T>
class Animal {
public:
    T legs;
    Animal(T l) : legs(l) {}
    virtual T count() { return legs; }
    virtual ~Animal() { printf("~Animal\n"); }
};

class Bird : public Animal<int> {
public:
    Bird() : Animal<int>(2) {}
    int count() { return legs * 10; }
};

int main() {
    Box<int> bi(21);
    Box<long> bl(1000000);
    Box<char> bc(65);
    bi.set(bi.twice());
    printf("box %d %ld %d\n", bi.get(), bl.get(), (int)bc.get());
    Stack<int, 4> s;
    s.push(1);
    s.push(2);
    s.push(3);
    int popped = s.pop();
    printf("stack %d %d cap %d\n", popped, s.count(), s.capacity());
    Stack<long, 10> big;
    big.push(5000000000L);
    long bigv = big.pop();
    printf("big %ld cap %d size %d\n", bigv, big.capacity(), (int)sizeof(big));
    Pair<int> pi(3, 4);
    printf("pair %d\n", pi.sum());
    Stack<Pair<int> *, 2> ps;
    ps.push(&pi);
    int psum = ps.pop()->sum();
    printf("ptrs %d %d\n", psum, ps.count());
    Animal<int> *a = new Bird();
    printf("virtual %d\n", a->count());
    delete a;
    return 0;
}
