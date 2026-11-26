// A data member with the name of a base class's member hides it ([class.member.lookup],
// [class.base.init]): as in okayscript/Ast.hpp, VarDeclStmt's own `kind` beside Stmt's `kind`.
#include <cstdio>
enum class SK { A, B };
struct Stmt {
    const SK kind;
    int line = 4;
    explicit Stmt(SK k) : kind(k) {}
    virtual ~Stmt() {}
    int BaseKind() const { return (int)kind; }
};
template <SK K>
struct StmtOf : Stmt {
    StmtOf() : Stmt(K) {}
};
struct Var : StmtOf<SK::B> {
    int n;
    unsigned char kind = 7;
    Var(int s, unsigned char k = 7) : n(s), kind(k) {}
    int Own() const { return kind + line; }
};
struct Inner : Var {
    long kind = 100;
    Inner() : Var(1, 2) {}
    int Sum() const { return (int)kind + Var::kind; }
};
int main() {
    Var v(3, 9);
    const Stmt &s = v;
    std::printf("%d %d %d %d %d\n", v.n, v.kind, (int)s.kind, v.BaseKind(), v.Own());
    Inner in;
    std::printf("%ld %d %d %d\n", in.kind, in.Sum(), in.Own(), (int)sizeof(Inner) > (int)sizeof(Var));
    return 0;
}
