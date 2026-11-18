extern "C" int printf(const char *fmt, ...);

template <typename T>
void swapv(T &a, T &b) { T t = a; a = b; b = t; }

template <typename T>
T sumArray(const T *a, int n) {
    T s = 0;
    for (int i = 0; i < n; i = i + 1) s = s + a[i];
    return s;
}

template <typename T, int N = 3>
struct Stack {
    T data[N];
    int top;
    Stack() : top(0) {}
    bool push(T v) {
        if (top >= N) return false;
        data[top] = v;
        top = top + 1;
        return true;
    }
    T pop() {
        top = top - 1;
        return data[top];
    }
    int size() const { return top; }
};

template <typename T>
struct Node {
    T value;
    Node<T> *next;
    Node(T v) : value(v), next(0) {}
};

template <typename T>
int length(Node<T> *n) {
    int c = 0;
    while (n) {
        c = c + 1;
        n = n->next;
    }
    return c;
}

template <typename A, typename B>
struct Pair {
    A a;
    B b;
    Pair(A x, B y) : a(x), b(y) {}
    A first() const { return a; }
};

template <typename T>
Pair<T, T> twin(T v) { return Pair<T, T>(v, v); }

template <typename T>
T clampTo(T v, T lo, T hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int main() {
    int x = 1;
    int y = 2;
    swapv(x, y);
    printf("%d %d\n", x, y);
    int arr[4];
    arr[0] = 1; arr[1] = 2; arr[2] = 3; arr[3] = 4;
    printf("%d\n", sumArray(arr, 4));
    Stack<int> s;
    s.push(4);
    s.push(5);
    s.push(6);
    printf("%d %d\n", s.push(7), s.size());
    printf("%d\n", s.pop());
    Stack<char, 2> sc;
    sc.push(65);
    printf("%d\n", sc.pop());
    Node<int> n1(1);
    Node<int> n2(2);
    n1.next = &n2;
    printf("%d\n", length(&n1));
    Pair<int, char> pc(7, 66);
    printf("%d %d\n", pc.first(), pc.b);
    Pair<int, int> t = twin(9);
    printf("%d %d\n", t.a, t.b);
    printf("%d %d %d\n", clampTo(5, 1, 3), clampTo(0, 1, 3), clampTo(2, 1, 3));
    return 0;
}
