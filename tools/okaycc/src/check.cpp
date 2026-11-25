// The type checker (ISO/IEC 9899 and 14882 constraints): class Checker and typecheckTu.
// Moved out of parser.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"

namespace okaycc {
namespace compiler {
struct ET {
    std::unique_ptr<Type> type;
    bool lvalue = false;
    bool mod = false;
    bool nullc = false;
    bool bad = false;
    bool xvalue = false;  // the result of a call returning T&& (std::move): an rvalue for overloads
    int line = 1;
    int col = 1;
};

struct Pos2 {
    int line = 1;
    int col = 1;
    std::string file;
};

class Checker {
public:
    explicit Checker(Diagnostics& diags) : diags_(diags) {}

    void check(Tu& tu) {
        tu_ = &tu;
        scopes_.emplace_back();
        // Declare every function first, so a call may come before the definition (class members).
        for (auto& d : tu.decls) {
            if (d && d->function && d->type) addFunction(*d);
            else if (d && !d->vt.empty() && d->type) add(d->name, cloneType(*d->type), false, d->line, d->col);
        }
        for (auto& d : tu.decls)
            if (d && !d->vt.empty())
                for (const std::string& sym : d->vt) usedFns_.insert(sym);  // a vtable uses its slots
        std::vector<Decl*> waiting;
        for (auto& d : tu.decls) {
            if (!d) continue;
            if (d->function && d->fromInstance) {
                waiting.push_back(d.get());
                continue;
            }
            if (d->function) checkFunction(*d);
            else checkVar(*d);
        }
        // Members of class template instances: checked once something refers to them (their own
        // bodies may refer to more), the others are dropped ([temp.inst] p4).
        std::set<Decl*> done;
        for (bool more = true; more;) {
            more = false;
            for (Decl* d : waiting) {
                if (done.count(d)) continue;
                if (!usedFns_.count(d->name) && !(d->asmName.size() && usedFns_.count(d->asmName))) continue;
                done.insert(d);
                checkFunction(*d);
                more = true;
            }
        }
        std::vector<std::unique_ptr<Decl>> kept;
        for (auto& d : tu.decls)
            if (d && (!d->fromInstance || done.count(d.get()))) kept.push_back(std::move(d));
        tu.decls = std::move(kept);
        for (auto& d : lambdaDecls_) tu.decls.push_back(std::move(d));
        lambdaDecls_.clear();
        scopes_.pop_back();
    }

private:
    struct Sym {
        std::unique_ptr<Type> type;
        bool func = false;
    };

    Diagnostics& diags_;
    std::string file_;
    std::vector<std::map<std::string, Sym>> scopes_;
    int loops_ = 0;
    int switches_ = 0;
    const Type* ret_ = nullptr;
    const Type* method_ = nullptr;
    std::string inNs_;

    int quiet_ = 0;  // above 0: a probing check that must not report
    void err(int line, int col, const std::string& msg) {
        if (quiet_ > 0) return;
        diags_.error(file_.empty() ? "t.c" : file_, line, col, msg);
    }

    std::unique_ptr<Type> prim(Type::Kind k) {
        auto t = std::make_unique<Type>();
        t->kind = k;
        return t;
    }
    std::unique_ptr<Type> ptrTo(std::unique_ptr<Type> inner) {
        auto t = prim(Type::Kind::Pointer);
        t->inner = std::move(inner);
        return t;
    }

    ET bad(int line, int col) {
        ET e;
        e.type = prim(Type::Kind::Int);
        e.bad = true;
        e.line = line;
        e.col = col;
        return e;
    }

    void add(const std::string& name, std::unique_ptr<Type> type, bool func, int line, int col) {
        if (name.empty()) return;
        auto& sc = scopes_.back();
        auto it = sc.find(name);
        if (it != sc.end()) {
            if (func && it->second.func && it->second.type && type && sameType(*it->second.type, *type)) return;
            // A file-scope variable declared (extern, or a static data member in its class) and then
            // defined: the same object ([basic.def]).
            if (!func && !it->second.func && scopes_.size() == 1 && it->second.type && type &&
                sameType(*it->second.type, *type))
                return;
            err(line, col, "redefined '" + name + "'");
            return;
        }
        Sym s;
        s.type = std::move(type);
        s.func = func;
        sc.emplace(name, std::move(s));
    }

    std::set<std::string> usedFns_;  // functions some checked code refers to
    ET lookup(const std::string& name, int line, int col, bool quiet = false) {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[static_cast<std::size_t>(i)].find(name);
            if (it == scopes_[static_cast<std::size_t>(i)].end() || !it->second.type) continue;
            if (it->second.func) usedFns_.insert(name);
            ET e;
            e.type = cloneType(*it->second.type);
            e.line = line;
            e.col = col;
            if (e.type->kind == Type::Kind::Reference && e.type->inner) {
                e.type = cloneType(*e.type->inner);
                e.lvalue = true;
                e.mod = true;
                return e;
            }
            if (e.type->kind == Type::Kind::Function) {
                e.lvalue = false;
                e.mod = false;
            } else if (e.type->kind == Type::Kind::Array) {
                e.lvalue = true;
                e.mod = false;
            } else if (e.type->kind == Type::Kind::Struct) {
                e.lvalue = true;
                e.mod = true;
            } else {
                e.lvalue = true;
                e.mod = true;
            }
            return e;
        }
        if (!quiet) err(line, col, "undeclared identifier '" + name + "'");
        return bad(line, col);
    }

    ET decay(ET e) {
        if (!e.type) return e;
        if (e.type->kind == Type::Kind::Array && e.type->inner) {
            e.type = ptrTo(std::move(e.type->inner));
            e.lvalue = false;
            e.mod = false;
        } else if (e.type->kind == Type::Kind::Function) {
            e.type = ptrTo(std::move(e.type));
            e.lvalue = false;
            e.mod = false;
        }
        return e;
    }

    bool scalar(const Type& t) const { return isIntish(t) || isPtr(t) || isFp(t); }

    bool canAssign(const Type& dst, const ET& src) {
        if (src.bad || !src.type) return true;
        if ((isIntish(dst) || isFp(dst)) && src.type && (isIntish(*src.type) || isFp(*src.type))) return true;
        if (dst.kind == Type::Kind::Struct && src.type->kind == Type::Kind::Struct && sameType(dst, *src.type))
            return true;
        if (isPtr(dst) && src.nullc) return true;
        if (isPtr(dst) && isPtr(*src.type)) {
            if (isVoidPtr(dst) || isVoidPtr(*src.type)) return true;
            if (dst.inner && src.type->inner && derivedFrom(*src.type->inner, *dst.inner)) return true;
            return dst.inner && src.type->inner && sameType(*dst.inner, *src.type->inner);
        }
        return false;
    }

    // Global const (or constexpr) scalars with constant initializers, by full name: their value
    // can be used in another constant initializer ([expr.const]).
    std::map<std::string, std::unique_ptr<Expr>> constInits_;
    static bool isScalarConst(const Type& t) {
        using K = Type::Kind;
        return t.kind == K::Int || t.kind == K::Long || t.kind == K::Short || t.kind == K::Char ||
               t.kind == K::Bool || t.kind == K::Float || t.kind == K::Double;
    }
    // Replace each checked name of such a constant by (T)(its value); true if any was replaced.
    bool foldConstNames(Expr& e) {
        if (e.kind == Expr::Kind::Name) {
            auto it = constInits_.find(e.text);
            if (it == constInits_.end()) return false;
            const Type* t = lookupType(e.text);
            if (!t) return false;
            auto val = cloneExpr(*it->second);
            e.kind = Expr::Kind::Cast;
            e.text.clear();
            e.ty = cloneType(*t);
            e.ty->cnst = false;
            e.kids.clear();
            e.kids.push_back(std::move(val));
            return true;
        }
        if (e.kind == Expr::Kind::Call && e.kids.size() == 1 && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name && tu_) {
            // f() of a function whose body is return <constant>; (a constexpr function such as
            // numeric_limits<float>::infinity(), [dcl.constexpr]): the constant, as (R)(value).
            for (const auto& d : tu_->decls) {
                if (!d || !d->function || !d->body || !d->type || !d->type->inner || !d->type->params.empty()) continue;
                if (d->name != e.kids[0]->text && d->asmName != e.kids[0]->text) continue;
                const Stmt* b = d->body.get();
                if (b->kind == Stmt::Kind::Block && b->stmts.size() == 1 && b->stmts[0]) b = b->stmts[0].get();
                if (b->kind != Stmt::Kind::Return || !b->expr || !isConst(*b->expr) || !isScalarConst(*d->type->inner))
                    return false;
                auto val = cloneExpr(*b->expr);
                e.kind = Expr::Kind::Cast;
                e.text.clear();
                e.ty = cloneType(*d->type->inner);
                e.kids.clear();
                e.kids.push_back(std::move(val));
                return true;
            }
            return false;
        }
        bool any = false;
        for (auto& k : e.kids)
            if (k && foldConstNames(*k)) any = true;
        return any;
    }
    const Type* lookupType(const std::string& name) {
        auto it = scopes_.front().find(name);
        return it == scopes_.front().end() || !it->second.type ? nullptr : it->second.type.get();
    }

    bool isConst(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
        case Expr::Kind::String:
        case Expr::Kind::Float:
            return true;
        case Expr::Kind::Unary:
            if (e.text == "sizeof" && e.ty) return true;
            if (e.text == "&" && !e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name &&
                scopes_.size() == 1 && scopes_.front().count(e.kids[0]->text))
                return true;  // the address of an object with static storage ([expr.const], ISO C 6.6 p9)
            return !e.kids.empty() && e.kids[0] && isConst(*e.kids[0]);
        case Expr::Kind::Cond:
            return e.kids.size() == 3 && e.kids[0] && e.kids[1] && e.kids[2] && isConst(*e.kids[0]) &&
                   isConst(*e.kids[1]) && isConst(*e.kids[2]);
        case Expr::Kind::Bin:
            return e.kids.size() == 2 && e.kids[0] && e.kids[1] && isConst(*e.kids[0]) && isConst(*e.kids[1]);
        case Expr::Kind::Cast:
            return !e.kids.empty() && e.kids[0] && isConst(*e.kids[0]);
        case Expr::Kind::Member:
            return false;
        default:
            return false;
        }
    }

    ET checkExprBody(Expr& e) {
        ET r;
        r.line = e.line;
        r.col = e.col;
        auto need = [&](std::size_t n) {
            return e.kids.size() > n && e.kids[n] != nullptr;
        };
        switch (e.kind) {
        case Expr::Kind::Int:
            if (e.litRank > 0) r.type = prim(Type::Kind::Long);
            else if (!e.litUnsigned && e.ival > 0x7fffffffull) r.type = prim(Type::Kind::Long);
            else r.type = prim(Type::Kind::Int);
            if (e.litUnsigned || (e.litRank == 0 && e.ival > 0xffffffffull)) r.type->unsign = true;
            if (e.litUnsigned && e.ival > 0xffffffffull) r.type->kind = Type::Kind::Long;
            r.nullc = e.ival == 0;
            return r;
        case Expr::Kind::Float:
            r.type = prim(e.litRank == 1 ? Type::Kind::Float : Type::Kind::Double);
            return r;
        case Expr::Kind::Char:
            r.type = prim(Type::Kind::Int);
            r.nullc = e.ival == 0;
            return r;
        case Expr::Kind::String: {
            r.type = prim(Type::Kind::Array);
            r.type->inner = prim(Type::Kind::Char);
            auto bound = std::make_unique<Expr>();
            bound->kind = Expr::Kind::Int;
            bound->ival = static_cast<std::uint64_t>(e.text.size()) + 1;
            r.type->bound = std::move(bound);
            r.lvalue = true;
            return r;
        }
        case Expr::Kind::Name: {
            if (e.text.compare(0, 8, "\x01" "lambda:") == 0) return checkLambda(e);
            if (captureName(e)) return checkExprBody(e);
            if (method_ && scopeOf(e.text) <= 0) {  // a parameter or local hides a member ([basic.scope.block])
                const Type* st = canonOf(method_);
                if (st) {
                    for (const Param& f : st->params) {
                        if (f.name != e.text || !f.mangled.empty()) continue;
                        if (!f.type || f.type->kind == Type::Kind::Function) continue;
                        auto obj = std::make_unique<Expr>();
                        obj->kind = Expr::Kind::Name;
                        obj->text = "this";
                        obj->line = e.line;
                        obj->col = e.col;
                        auto field = std::make_unique<Expr>();
                        field->kind = Expr::Kind::Name;
                        field->text = e.text;
                        field->line = e.line;
                        field->col = e.col;
                        e.kind = Expr::Kind::Member;
                        e.text = "->";
                        e.kids.clear();
                        e.kids.push_back(std::move(obj));
                        e.kids.push_back(std::move(field));
                        return checkExprBody(e);
                    }
                }
            }
            ET hit = lookup(e.text, e.line, e.col, true);
            if (!hit.bad) return hit;
            if (method_ && e.text.find("::") == std::string::npos) {
                // A member function of the class hides the namespace's functions of that name
                // ([basic.lookup.unqual]/8): the call rewrites it as this->f, so leave the name.
                const Type* mst = canonOf(method_);
                if (mst)
                    for (const Param& f : mst->params)
                        if (f.name == e.text && !f.mangled.empty() && !isSpecialName(f.name)) {
                            if (!quiet_) err(e.line, e.col, "a member function name used without a call");
                            return bad(e.line, e.col);
                        }
            }
            {
                // The enclosing namespaces and classes, innermost first; for N::x too (the first
                // part of a qualified name is looked up the same way, [basic.lookup.qual]).
                std::string scope = inNs_;
                while (!scope.empty()) {
                    std::string q = scope + "::" + e.text;
                    ET ns = lookup(q, e.line, e.col, true);
                    if (!ns.bad) {
                        e.text = q;
                        return ns;
                    }
                    std::size_t cut = scope.rfind("::");
                    if (cut == std::string::npos) break;
                    scope = scope.substr(0, cut);
                }
            }
            if (tu_) {
                // using N::name; and using namespace N; (searched after the enclosing scopes;
                // a qualified name is tried inside each used namespace as well)
                auto ud = tu_->usingDecl.find(e.text);
                if (ud != tu_->usingDecl.end()) {
                    std::string base = ud->second;
                    std::vector<std::string> tries = {base};
                    for (std::string sc = inNs_; !sc.empty();) {
                        tries.push_back(sc + "::" + base);
                        std::size_t cut = sc.rfind("::");
                        if (cut == std::string::npos) break;
                        sc = sc.substr(0, cut);
                    }
                    for (const std::string& q : tries) {
                        ET hit2 = lookup(q, e.line, e.col, true);
                        if (!hit2.bad) {
                            e.text = q;
                            return hit2;
                        }
                    }
                }
                for (const std::string& u : tu_->usingNs) {
                    ET hit2 = lookup(u + "::" + e.text, e.line, e.col, true);
                    if (!hit2.bad) {
                        e.text = u + "::" + e.text;
                        return hit2;
                    }
                }
            }
            if (e.text == "__builtin_sqrt" || e.text == "__builtin_sqrtf") {
                // The square root instruction (lowered to SQRTSD / SQRTSS).
                ET b;
                b.line = e.line;
                b.col = e.col;
                Type::Kind k = e.text == "__builtin_sqrtf" ? Type::Kind::Float : Type::Kind::Double;
                b.type = prim(Type::Kind::Function);
                b.type->inner = prim(k);
                Param a;
                a.type = prim(k);
                b.type->params.push_back(std::move(a));
                return b;
            }
            if (e.text == "malloc" || e.text == "free") {
                ET b;
                b.line = e.line;
                b.col = e.col;
                b.type = prim(Type::Kind::Function);
                if (e.text == "malloc") {
                    b.type->inner = ptrTo(prim(Type::Kind::Void));
                    Param a;
                    a.type = prim(Type::Kind::Long);
                    b.type->params.push_back(std::move(a));
                } else {
                    b.type->inner = prim(Type::Kind::Void);
                    Param a;
                    a.type = ptrTo(prim(Type::Kind::Char));
                    b.type->params.push_back(std::move(a));
                }
                return b;
            }
            return lookup(e.text, e.line, e.col);
        }
        case Expr::Kind::Unary: {
            // sizeof(type) has no operand: only its type (it was taken for an error that was not
            // reported, which stopped the check of the arguments after it).
            if (!need(0) && !(e.text == "sizeof" && e.ty)) return bad(e.line, e.col);
            if (e.text == "!" || e.text == "-" || e.text == "+" || e.text == "~") convertIfNeeded(*e.kids[0]);
            if (e.text == "sizeof") {
                if (e.ty) {
                    if (e.ty->kind == Type::Kind::Void || e.ty->kind == Type::Kind::Function) {
                        err(e.line, e.col, "sizeof this type is not allowed");
                        return bad(e.line, e.col);
                    }
                } else {
                    if (!need(0)) return bad(e.line, e.col);
                    ET inner = checkExpr(*e.kids[0]);
                    if (inner.bad || !inner.type || inner.type->kind == Type::Kind::Void ||
                        inner.type->kind == Type::Kind::Function) {
                        err(e.line, e.col, "sizeof this expression is not allowed");
                        return bad(e.line, e.col);
                    }
                }
                r.type = prim(Type::Kind::Int);
                return r;
            }
            if (e.text == "&") {
                ET inner = checkExpr(*e.kids[0]);
                if (inner.bad) return bad(e.line, e.col);
                if (!inner.lvalue && !(inner.type && inner.type->kind == Type::Kind::Function)) {
                    err(e.line, e.col, "operand of '&' is not an lvalue");
                    return bad(e.line, e.col);
                }
                r.type = ptrTo(std::move(inner.type));
                return r;
            }
            if (e.text == "*") {
                ET inner = decay(checkExpr(*e.kids[0]));
                if (inner.bad) return bad(e.line, e.col);
                if (!inner.type || !isPtr(*inner.type) || !inner.type->inner) {
                    err(e.line, e.col, "operand is not a pointer");
                    return bad(e.line, e.col);
                }
                if (inner.type->inner->kind == Type::Kind::Void) {
                    err(e.line, e.col, "operand is not a pointer");
                    return bad(e.line, e.col);
                }
                r.type = cloneType(*inner.type->inner);
                r.lvalue = true;
                r.xvalue = e.ival == 5;
                r.mod = isIntish(*r.type) || isPtr(*r.type) || isFp(*r.type) || r.type->kind == Type::Kind::Struct;
                return r;
            }
            ET inner = decay(checkExpr(*e.kids[0]));
            if (inner.bad) return bad(e.line, e.col);
            if (e.text == "!" && inner.type && scalar(*inner.type)) {
                r.type = prim(Type::Kind::Int);
                return r;
            }
            if ((e.text == "-" || e.text == "+") && inner.type && isFp(*inner.type)) {
                r.type = cloneType(*inner.type);
                return r;
            }
            if (!inner.type || !isIntish(*inner.type)) {
                err(e.line, e.col, "operand is not an integer");
                return bad(e.line, e.col);
            }
            // -x, +x and ~x have the promoted type of x (ISO C 6.5.3.3): a long stays long.
            r.type = prim(inner.type->kind == Type::Kind::Long ? Type::Kind::Long : Type::Kind::Int);
            r.type->unsign = (inner.type->kind == Type::Kind::Long || inner.type->kind == Type::Kind::Int) && inner.type->unsign;
            return r;
        }
        case Expr::Kind::Pre:
        case Expr::Kind::Post: {
            if (!need(0)) return bad(e.line, e.col);
            ET inner = checkExpr(*e.kids[0]);
            if (inner.bad) return bad(e.line, e.col);
            if (!inner.mod || !inner.type || !(isIntish(*inner.type) || isPtr(*inner.type))) {
                err(e.line, e.col, "operand of '" + e.text + "' is not a modifiable integer or pointer");
                return bad(e.line, e.col);
            }
            r.type = cloneType(*inner.type);
            return r;
        }
        case Expr::Kind::Bin:
            return checkBin(e);
        case Expr::Kind::Assign:
            return checkAssign(e);
        case Expr::Kind::Cond: {
            if (!need(0) || !need(1) || !need(2)) return bad(e.line, e.col);
            convertIfNeeded(*e.kids[0]);
            ET c = decay(checkExpr(*e.kids[0]));
            ET a = decay(checkExpr(*e.kids[1]));
            ET b = decay(checkExpr(*e.kids[2]));
            if (c.bad || a.bad || b.bad) return bad(e.line, e.col);
            if (!c.type || !scalar(*c.type)) {
                err(e.line, e.col, "condition is not a scalar");
                return bad(e.line, e.col);
            }
            if (a.lvalue && b.lvalue && a.type && b.type && a.type->kind == Type::Kind::Struct &&
                sameType(*a.type, *b.type)) {
                // c ? x : y with two objects of one class is an lvalue (ISO/IEC 14882 [expr.cond]):
                // *(c ? &x : &y).
                auto cond = mkE(Expr::Kind::Cond, e);
                cond->kids.push_back(std::move(e.kids[0]));
                cond->kids.push_back(eUnary("&", std::move(e.kids[1]), e));
                cond->kids.push_back(eUnary("&", std::move(e.kids[2]), e));
                e.kind = Expr::Kind::Unary;
                e.text = "*";
                e.kids.clear();
                e.kids.push_back(std::move(cond));
                return checkExprBody(e);
            }
            if (a.type && b.type && a.type->kind == Type::Kind::Struct && sameType(*a.type, *b.type) && !quiet_) {
                // Two objects of one class, not both lvalues: a branch that is a temporary gets a
                // name (destroyed after the full expression only if that branch ran, by its flag),
                // and the result is *(c ? &x : &y) as above.
                for (int bi = 1; bi <= 2; ++bi) {
                    auto& k = e.kids[static_cast<std::size_t>(bi)];
                    const ET& t = bi == 1 ? a : b;
                    if (t.lvalue) {
                        k = eUnary("&", std::move(k), e);
                    } else if (k->kind == Expr::Kind::Comma && k->ival == 9 && !k->text.empty()) {
                        std::string nm = k->text;
                        k = eCommaNode(std::move(k), eUnary("&", mkName(nm, e), e), e);
                    } else {
                        std::string tmp = newTemp(*t.type, e, true);
                        auto assign = withFlag(eAssignNode(mkName(tmp, e), std::move(k), e), e);
                        k = eCommaNode(std::move(assign), eUnary("&", mkName(tmp, e), e), e);
                    }
                }
                auto cond = mkE(Expr::Kind::Cond, e);
                cond->kids = std::move(e.kids);
                e.kind = Expr::Kind::Unary;
                e.text = "*";
                e.kids.clear();
                e.kids.push_back(std::move(cond));
                return checkExprBody(e);
            }
            if (a.type && b.type && isIntish(*a.type) && isIntish(*b.type)) {
                r.type = prim(Type::Kind::Int);
                return r;
            }
            if (a.type && b.type && (isIntish(*a.type) || isFp(*a.type)) && (isIntish(*b.type) || isFp(*b.type))) {
                // Arithmetic branches with a floating one: both become the common floating type
                // ([expr.cond] p7, the usual arithmetic conversions).
                Type::Kind k = (a.type->kind == Type::Kind::Double || b.type->kind == Type::Kind::Double)
                                   ? Type::Kind::Double
                                   : Type::Kind::Float;
                for (int bi = 1; bi <= 2; ++bi) {
                    const Type* bt = bi == 1 ? a.type.get() : b.type.get();
                    if (bt->kind == k) continue;
                    auto cast = mkE(Expr::Kind::Cast, e);
                    cast->ty = prim(k);
                    cast->kids.push_back(std::move(e.kids[static_cast<std::size_t>(bi)]));
                    cast->checked = prim(k);
                    e.kids[static_cast<std::size_t>(bi)] = std::move(cast);
                }
                r.type = prim(k);
                return r;
            }
            if (a.nullc && b.type && isPtr(*b.type)) {
                r.type = cloneType(*b.type);
                return r;
            }
            if (b.nullc && a.type && isPtr(*a.type)) {
                r.type = cloneType(*a.type);
                return r;
            }
            if (a.type && b.type && isPtr(*a.type) && isPtr(*b.type) && canAssign(*a.type, b)) {
                r.type = cloneType(*a.type);
                return r;
            }
            err(e.line, e.col, "branches of '?:' do not have a common type");
            return bad(e.line, e.col);
        }
        case Expr::Kind::Call:
            return checkCall(e);
        case Expr::Kind::Index: {
            if (!need(0) || !need(1)) return bad(e.line, e.col);
            ET base = decay(checkExpr(*e.kids[0]));
            ET idx = decay(checkExpr(*e.kids[1]));
            if (base.bad || idx.bad) return bad(e.line, e.col);
            ET* ptr = nullptr;
            ET* off = nullptr;
            if (base.type && isPtr(*base.type) && idx.type && isIntish(*idx.type)) {
                ptr = &base;
                off = &idx;
            } else if (idx.type && isPtr(*idx.type) && base.type && isIntish(*base.type)) {
                ptr = &idx;
                off = &base;
            }
            (void)off;
            if (!ptr || !ptr->type->inner) {
                err(e.line, e.col, "operand is not a pointer");
                return bad(e.line, e.col);
            }
            r.type = cloneType(*ptr->type->inner);
            r.lvalue = true;
            r.mod = isIntish(*r.type) || isPtr(*r.type) || isFp(*r.type) || r.type->kind == Type::Kind::Struct;
            return r;
        }
        case Expr::Kind::Member: {
            if (!need(0) || !need(1)) return bad(e.line, e.col);
            ET obj = e.text == "->" ? decay(checkExpr(*e.kids[0])) : checkExpr(*e.kids[0]);
            if (obj.bad || !obj.type) return bad(e.line, e.col);
            const Type* st = obj.type.get();
            if (e.text == "->") {
                if (!isPtr(*obj.type) || !obj.type->inner ||
                    (obj.type->inner->kind != Type::Kind::Struct && obj.type->inner->kind != Type::Kind::Union)) {
                    err(e.line, e.col, "operand of '->' is not a pointer to a struct");
                    return bad(e.line, e.col);
                }
                st = obj.type->inner.get();
            } else if (st->kind != Type::Kind::Struct && st->kind != Type::Kind::Union) {
                err(e.line, e.col, "operand of '.' is not a struct");
                return bad(e.line, e.col);
            } else if (!obj.lvalue && st->kind == Type::Kind::Struct && !quiet_ &&
                       (e.kids[0]->kind == Expr::Kind::Call || e.kids[0]->kind == Expr::Kind::Comma)) {
                // f().n or T(args).n: the object is a temporary; it gets a name that lives to the
                // end of the full expression ([class.temporary]/4) and the field is read from it.
                std::string tmp;
                std::unique_ptr<Expr> first;
                if (e.kids[0]->kind == Expr::Kind::Comma && e.kids[0]->ival == 9 && !e.kids[0]->text.empty()) {
                    tmp = e.kids[0]->text;
                    first = std::move(e.kids[0]);
                } else {
                    tmp = newTemp(*obj.type, e, true);
                    first = withFlag(eAssignNode(mkName(tmp, e), std::move(e.kids[0]), e), e);
                }
                auto mem = mkE(Expr::Kind::Member, e);
                mem->text = ".";
                mem->kids.push_back(mkName(tmp, e));
                mem->kids.push_back(std::move(e.kids[1]));
                auto both = eCommaNode(std::move(first), std::move(mem), e);
                e.kind = Expr::Kind::Comma;
                e.text.clear();
                e.ival = 0;
                e.kids = std::move(both->kids);
                return checkExpr(e);
            }
            st = canonOf(st);
            const std::string& fname = e.kids[1]->text;
            for (const Param& f : st->params) {
                if (f.name != fname || !f.type) continue;
                r.type = cloneType(*f.type);
                r.lvalue = true;
                r.mod = isIntish(*r.type) || isPtr(*r.type) || isFp(*r.type) || r.type->kind == Type::Kind::Struct;
                return r;
            }
            err(e.line, e.col, "struct has no field '" + fname + "'");
            return bad(e.line, e.col);
        }
        case Expr::Kind::Cast: {
            if (!need(0) || !e.ty) return bad(e.line, e.col);
            ET inner = decay(checkExpr(*e.kids[0]));
            if (inner.bad) return bad(e.line, e.col);
            r.type = cloneType(*e.ty);
            r.nullc = inner.nullc && isIntish(*r.type);
            return r;
        }
        case Expr::Kind::Comma: {
            if (e.kids.empty()) return bad(e.line, e.col);
            if (e.ival == 7 && e.ty) {
                // A T(args) temporary: destroyed after the statement unless it initializes an object.
                TempInfo ti;
                ti.name = e.text;
                ti.type = cloneType(*e.ty);
                add(ti.name, cloneType(*e.ty), false, e.line, e.col);
                if (needsDtor(*e.ty)) {
                    ti.tag = e.ty->tag;
                    ti.flag = "__cf" + std::to_string(tempN_++);
                    add(ti.flag, std::make_unique<Type>(), false, e.line, e.col);
                    scopes_.back()[ti.flag].type->kind = Type::Kind::Int;
                    lastTempFlag_ = ti.flag;
                    lastTempName_ = ti.name;
                    lastTempTag_ = ti.tag;
                    lastTempRec_.clear();
                    if (ehActive()) {
                        ti.rec = "__cr" + std::to_string(tempN_++);
                        lastTempRec_ = ti.rec;
                        auto arr = std::make_unique<Type>();
                        arr->kind = Type::Kind::Array;
                        arr->inner = prim(Type::Kind::Long);
                        arr->bound = std::make_unique<Expr>();
                        arr->bound->kind = Expr::Kind::Int;
                        arr->bound->ival = 14;
                        add(ti.rec, std::move(arr), false, e.line, e.col);
                    }
                    auto first = std::move(e.kids[0]);
                    e.kids[0] = withFlag(std::move(first), e);
                }
                e.ival = 9;
                temps_.push_back(std::move(ti));
            }
            ET last = bad(e.line, e.col);
            for (auto& k : e.kids)
                if (k) last = decay(checkExpr(*k));
            last.lvalue = false;
            last.mod = false;
            return last;
        }
        case Expr::Kind::Brace:
            err(e.line, e.col, "brace initializer is not an expression");
            return bad(e.line, e.col);
        }
        return bad(e.line, e.col);
    }

    // ---- temporaries made while checking (hidden locals, destroyed after the statement) ----
    struct TempInfo {
        std::string name;
        std::string tag;                 // class to destroy ("" = nothing to destroy)
        std::string flag;                // hidden int, set when the object was built (declared with it)
        std::string rec;                 // with exceptions: the unwinding record (14 longs) of the object
        std::unique_ptr<Type> type;      // set when the declaration still has to be inserted
        Pos2 pos;
    };
    std::vector<TempInfo> temps_;
    int tempN_ = 0;
    std::string lastTempFlag_;  // the destruction flag of the temporary newTemp just made, or ""
    std::string lastTempRec_;   // its unwinding record, or ""
    std::string lastTempName_;
    std::string lastTempTag_;
    // "first, flag = 1 (, record the object)": the flag records that the object was built.
    std::unique_ptr<Expr> withFlag(std::unique_ptr<Expr> first, const Expr& at) {
        if (lastTempFlag_.empty()) return first;
        auto one = mkE(Expr::Kind::Int, at);
        one->ival = 1;
        auto set = mkE(Expr::Kind::Assign, at);
        set->text = "=";
        set->ival = 3;
        set->kids.push_back(mkName(lastTempFlag_, at));
        set->kids.push_back(std::move(one));
        auto c = mkE(Expr::Kind::Comma, at);
        c->kids.push_back(std::move(first));
        c->kids.push_back(std::move(set));
        if (!lastTempRec_.empty()) {
            // Record it for unwinding: rec[1] = 0, rec[2] = &object, rec[3] = destructor, push(rec).
            auto idx = [&](std::uint64_t k) {
                auto ix = mkE(Expr::Kind::Index, at);
                ix->kids.push_back(mkName(lastTempRec_, at));
                auto n = mkE(Expr::Kind::Int, at);
                n->ival = k;
                ix->kids.push_back(std::move(n));
                return ix;
            };
            auto toLong = [&](std::unique_ptr<Expr> x) {
                auto cst = mkE(Expr::Kind::Cast, at);
                cst->ty = prim(Type::Kind::Long);
                cst->kids.push_back(std::move(x));
                return cst;
            };
            auto zero = mkE(Expr::Kind::Int, at);
            c->kids.push_back(eAssignNode(idx(1), std::move(zero), at));
            c->kids.push_back(eAssignNode(idx(2), toLong(eUnary("&", mkName(lastTempName_, at), at)), at));
            auto it = canonStructs().find(lastTempTag_);
            const Param* dp = it == canonStructs().end() ? nullptr : findSpecial(it->second.get(), "<dtor>");
            if (dp) c->kids.push_back(eAssignNode(idx(3), toLong(mkName(dp->mangled, at)), at));
            auto push = mkE(Expr::Kind::Call, at);
            push->kids.push_back(mkName("__okay_eh_push", at));
            push->kids.push_back(mkName(lastTempRec_, at));
            c->kids.push_back(std::move(push));
        }
        return c;
    }
    // The temporary (a Comma made by the parser for T(args)) initializes an object or is returned:
    // nothing destroys it separately, and its unwinding record is dropped.
    void consumed(Expr& comma) {
        comma.ival = 8;
        bool hadRecord = false;
        for (TempInfo& t : temps_)
            if (t.name == comma.text) hadRecord = !t.rec.empty() && !t.tag.empty();
        eraseTemp(comma.text);
        if (hadRecord && comma.kids.size() >= 2) {
            auto pop = mkE(Expr::Kind::Call, comma);
            pop->kids.push_back(mkName("__okay_eh_pop", comma));
            comma.kids.insert(comma.kids.end() - 1, std::move(pop));
        }
    }
    std::map<const Expr*, ET> etCache_;

    static ET copyET(const ET& a) {
        ET r;
        if (a.type) r.type = cloneType(*a.type);
        r.lvalue = a.lvalue;
        r.mod = a.mod;
        r.nullc = a.nullc;
        r.bad = a.bad;
        r.xvalue = a.xvalue;
        r.line = a.line;
        r.col = a.col;
        return r;
    }
    static bool needsDtor(const Type& t) {
        if (t.kind != Type::Kind::Struct || t.tag.empty()) return false;
        auto it = canonStructs().find(t.tag);
        return it != canonStructs().end() && it->second && it->second->needDtor;
    }
    static bool hasUserCopy(const Type& t) {
        if (t.kind != Type::Kind::Struct || t.tag.empty()) return false;
        auto it = canonStructs().find(t.tag);
        if (it == canonStructs().end() || !it->second) return false;
        for (const Param& p : it->second->params) {
            if (p.name != "<ctor>" || !p.type || p.type->params.size() != 1) continue;
            const Type* a = p.type->params[0].type.get();
            if (a && a->kind == Type::Kind::Reference && a->inner && a->inner->kind == Type::Kind::Struct &&
                a->inner->tag == t.tag)
                return true;
        }
        return false;
    }
    // A hidden local of type t, declared before the statement being checked.
    std::string newTemp(const Type& t, const Expr& at, bool destroy) {
        TempInfo ti;
        ti.name = "__ct" + std::to_string(tempN_++);
        ti.type = cloneType(t);
        ti.pos.line = at.line;
        ti.pos.col = at.col;
        ti.pos.file = at.file;
        lastTempFlag_.clear();
        lastTempRec_.clear();
        if (destroy && needsDtor(t)) {
            ti.tag = t.tag;
            ti.flag = "__cf" + std::to_string(tempN_++);
            lastTempFlag_ = ti.flag;
            lastTempName_ = ti.name;
            lastTempTag_ = t.tag;
            if (ehActive()) {
                ti.rec = "__cr" + std::to_string(tempN_++);
                lastTempRec_ = ti.rec;
            }
            add(ti.flag, std::make_unique<Type>(), false, at.line, at.col);
            scopes_.back()[ti.flag].type->kind = Type::Kind::Int;
        }
        if (!ti.rec.empty()) {
            auto arr = std::make_unique<Type>();
            arr->kind = Type::Kind::Array;
            arr->inner = prim(Type::Kind::Long);
            arr->bound = std::make_unique<Expr>();
            arr->bound->kind = Expr::Kind::Int;
            arr->bound->ival = 14;
            add(ti.rec, std::move(arr), false, at.line, at.col);
        }
        add(ti.name, cloneType(t), false, at.line, at.col);
        std::string name = ti.name;
        temps_.push_back(std::move(ti));
        return name;
    }
    // The named temporary turned out to initialize an object: nothing destroys it separately.
    void eraseTemp(const std::string& name) {
        for (std::size_t i = 0; i < temps_.size(); ++i)
            if (temps_[i].name == name) {
                temps_[i].tag.clear();  // still declared (the flag is set), but not destroyed
                return;
            }
    }
    std::unique_ptr<Expr> eUnary(const char* op, std::unique_ptr<Expr> k, const Expr& at) {
        auto u = mkE(Expr::Kind::Unary, at);
        u->text = op;
        u->kids.push_back(std::move(k));
        return u;
    }
    std::unique_ptr<Expr> eAssignNode(std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, const Expr& at) {
        auto a = mkE(Expr::Kind::Assign, at);
        a->text = "=";
        a->ival = 3;  // a plain copy: operator= is not used
        a->kids.push_back(std::move(l));
        a->kids.push_back(std::move(r));
        return a;
    }
    std::unique_ptr<Expr> eCommaNode(std::unique_ptr<Expr> a, std::unique_ptr<Expr> b, const Expr& at) {
        auto c = mkE(Expr::Kind::Comma, at);
        c->kids.push_back(std::move(a));
        c->kids.push_back(std::move(b));
        return c;
    }

    // An operand that is cheap and safe to check twice: a name, a member, an index or a dereference
    // built from those. (Checking a call twice is avoided; a chain would check itself too often.)
    static bool simpleOperand(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Name: return true;
        case Expr::Kind::Member: return !e.kids.empty() && e.kids[0] && simpleOperand(*e.kids[0]);
        case Expr::Kind::Unary: return e.text == "*" && !e.kids.empty() && e.kids[0] && simpleOperand(*e.kids[0]);
        case Expr::Kind::Index:
            return e.kids.size() == 2 && e.kids[0] && e.kids[1] && simpleOperand(*e.kids[0]) &&
                   (e.kids[1]->kind == Expr::Kind::Int || e.kids[1]->kind == Expr::Kind::Name);
        default: return false;
        }
    }

    // C++ operator overloading for member operators: "a + b" on an object whose class declares
    // operator+ becomes the call a.operator+(b). Written from ISO/IEC 14882 [over.match.oper]
    // (member candidates only; no non-member operators, no conversion sequences).
    // Returns false when the expression is left alone. Only a simple left operand is looked at.
    bool rewriteOperator(Expr& e) {
        std::string op;
        std::size_t lhsAt = 0;
        bool post = false;
        switch (e.kind) {
        case Expr::Kind::Bin:
        case Expr::Kind::Assign:
            if (e.kids.size() != 2 || (e.kind == Expr::Kind::Assign && e.ival == 3)) return false;
            op = "operator" + e.text;
            break;
        case Expr::Kind::Index:
            if (e.kids.size() != 2) return false;
            op = "operator[]";
            break;
        case Expr::Kind::Pre:
            if (e.kids.size() != 1) return false;
            op = "operator" + e.text;
            break;
        case Expr::Kind::Post:
            if (e.kids.size() != 1) return false;
            op = "operator" + e.text;
            post = true;
            break;
        case Expr::Kind::Unary:
            if (e.kids.size() != 1 || (e.text != "-" && e.text != "+" && e.text != "!" && e.text != "~" && e.text != "*"))
                return false;
            op = "operator" + e.text;
            break;
        case Expr::Kind::Call:
            if (e.kids.empty() || !e.kids[0] || e.kids[0]->kind != Expr::Kind::Name) return false;
            op = "operator()";
            break;
        default: return false;
        }
        if (!e.kids[lhsAt]) return false;
        Expr& lhs = *e.kids[lhsAt];
        const bool simple = simpleOperand(lhs);
        // A left operand that is a call or another operator expression (a + b + c): check it once,
        // keep the result for the check that follows, and use it only for a class with the operator.
        if (!simple && e.kind != Expr::Kind::Bin && e.kind != Expr::Kind::Index) return false;
        if (!simple && lhs.kind != Expr::Kind::Call && lhs.kind != Expr::Kind::Bin && lhs.kind != Expr::Kind::Comma &&
            lhs.kind != Expr::Kind::Unary)
            return false;
        if (e.kind == Expr::Kind::Call && lhs.text.compare(0, 1, "\x01") == 0) return false;
        ++quiet_;
        ET probe = checkExpr(lhs);
        --quiet_;
        if (!probe.bad && probe.type && probe.type->kind == Type::Kind::Reference && probe.type->inner &&
            probe.type->inner->kind == Type::Kind::Struct) {
            // A call that returns a reference to an object (f() != nullptr with T &f()): an lvalue.
            probe.type = cloneType(*probe.type->inner);
            probe.lvalue = true;
        }
        const bool usable = !probe.bad && probe.type && probe.type->kind == Type::Kind::Struct;
        const Type* st = usable ? canonOf(probe.type.get()) : nullptr;
        bool found = false;
        if (st)
            for (const Param& f : st->params)
                if (f.name == op && !f.mangled.empty()) found = true;
        const bool refCall = lhs.kind == Expr::Kind::Call ||
                             (lhs.kind == Expr::Kind::Unary && lhs.text == "*" && !lhs.kids.empty() && lhs.kids[0] &&
                              lhs.kids[0]->kind == Expr::Kind::Call);
        if (!simple && !(found && probe.lvalue && refCall)) {
            // (A call that returns a reference to an object, *f() inside, is an lvalue: the member
            // call below is made on it directly.)
            if (probe.bad) return false;
            if (!found || probe.lvalue) {
                etCache_[&lhs] = copyET(probe);
                return false;
            }
            // An object that is not an lvalue (the result of a call): put it in a hidden local and
            // call the operator on that. A T(args) temporary already is one: use it as it is.
            std::string tmp;
            std::unique_ptr<Expr> assign;
            if (e.kids[lhsAt]->kind == Expr::Kind::Comma && e.kids[lhsAt]->ival == 9 && !e.kids[lhsAt]->text.empty()) {
                tmp = e.kids[lhsAt]->text;
                assign = std::move(e.kids[lhsAt]);
            } else {
                tmp = newTemp(*probe.type, e, true);
                assign = withFlag(eAssignNode(mkName(tmp, e), std::move(e.kids[lhsAt]), e), e);
            }
            auto member = mkE(Expr::Kind::Member, e);
            member->text = ".";
            member->kids.push_back(mkName(tmp, e));
            member->kids.push_back(mkName(op, e));
            auto call = mkE(Expr::Kind::Call, e);
            call->kids.push_back(std::move(member));
            for (std::size_t i = 1; i < e.kids.size(); ++i) call->kids.push_back(std::move(e.kids[i]));
            std::unique_ptr<Expr> last = std::move(call);
            std::unique_ptr<Expr> assignResult;
            const Type* opRet = nullptr;
            for (const Param& f : st->params)
                if (f.name == op && !f.mangled.empty() && f.type) opRet = f.type->inner.get();
            if (opRet && opRet->kind == Type::Kind::Struct) {
                // An object result is kept in a hidden local too, so the whole expression is an
                // address-bearing name like any other object.
                std::string res = newTemp(*opRet, e, false);
                assignResult = eAssignNode(mkName(res, e), std::move(last), e);
                last = mkName(res, e);
            }
            auto both = eCommaNode(std::move(assign), std::move(last), e);
            if (assignResult) both->kids.insert(both->kids.begin() + 1, std::move(assignResult));
            e.kind = Expr::Kind::Comma;
            e.text.clear();
            e.ival = 0;
            e.kids = std::move(both->kids);
            return true;
        }
        if (probe.bad || !usable || !st) return false;
        if (!found || !probe.lvalue) return false;
        auto member = mkE(Expr::Kind::Member, e);
        member->text = ".";
        member->kids.push_back(std::move(e.kids[lhsAt]));
        member->kids.push_back(mkName(op, e));
        std::vector<std::unique_ptr<Expr>> kids;
        kids.push_back(std::move(member));
        for (std::size_t i = 1; i < e.kids.size(); ++i) kids.push_back(std::move(e.kids[i]));
        if (post) {
            auto zero = mkE(Expr::Kind::Int, e);
            kids.push_back(std::move(zero));
        }
        e.kids = std::move(kids);
        e.kind = Expr::Kind::Call;
        e.text.clear();
        return true;
    }

    // Non-member operators ([over.match.oper] p3, the non-member candidates; [basic.lookup.argdep]):
    // "s * v" with no member operator* in v's class becomes the call operator*(s, v) when a
    // function of that name is declared in the current namespace, an enclosing one, or the
    // namespace of a class operand (argument-dependent lookup). A candidate is viable when it takes
    // as many arguments, every class operand meets a parameter of the same class (by value or
    // reference) and every other operand meets a parameter that is not a class; the call then
    // chooses among the overloads of the name as any call does. Operators on two numbers, and
    // &&, || and the comma, are never rewritten.
    std::set<std::string> freeOps_;  // short names ("operator*") of every non-member operator seen
    bool rewriteFreeOperator(Expr& e) {
        std::size_t arity = 0;
        if (e.kind == Expr::Kind::Bin && e.kids.size() == 2) arity = 2;
        else if (e.kind == Expr::Kind::Unary && e.kids.size() == 1 &&
                 (e.text == "-" || e.text == "+" || e.text == "!" || e.text == "~"))
            arity = 1;
        if (arity == 0 || e.text == "&&" || e.text == "||" || e.text == ",") return false;
        std::string op = "operator" + e.text;
        if (!freeOps_.count(op)) return false;
        for (std::size_t i = 0; i < arity; ++i)
            if (!e.kids[i]) return false;
        // The operand types: a cached result is read without taking it; anything else is probed
        // quietly and cached, so the real check later uses this same result.
        std::vector<ET> types;
        bool anyClass = false;
        for (std::size_t i = 0; i < arity; ++i) {
            Expr& k = *e.kids[i];
            auto cached = etCache_.find(&k);
            if (cached != etCache_.end()) {
                types.push_back(copyET(cached->second));
            } else {
                if (!simpleOperand(k) && k.kind != Expr::Kind::Int && k.kind != Expr::Kind::Float &&
                    k.kind != Expr::Kind::Char && k.kind != Expr::Kind::String && k.kind != Expr::Kind::Call &&
                    k.kind != Expr::Kind::Bin && k.kind != Expr::Kind::Comma && k.kind != Expr::Kind::Unary &&
                    k.kind != Expr::Kind::Cast)
                    return false;
                ++quiet_;
                ET t = checkExpr(k);
                --quiet_;
                if (t.bad || !t.type) return false;
                if (!simpleOperand(k)) etCache_[&k] = copyET(t);
                types.push_back(std::move(t));
            }
            if (types.back().type && types.back().type->kind == Type::Kind::Struct) anyClass = true;
        }
        if (!anyClass) return false;
        auto bare = [](const Type* t) {
            while (t && t->kind == Type::Kind::Reference && t->inner) t = t->inner.get();
            return t;
        };
        auto viable = [&](const Type* fn) {
            if (!fn || fn->kind != Type::Kind::Function || fn->params.size() != arity) return false;
            for (std::size_t i = 0; i < arity; ++i) {
                const Type* pt = bare(fn->params[i].type.get());
                const Type* at = types[i].type.get();
                if (!pt || !at) return false;
                bool pc = pt->kind == Type::Kind::Struct, ac = at->kind == Type::Kind::Struct;
                if (pc != ac) return false;
                if (pc) {
                    const Type* a = canonOf(at);
                    const Type* b = canonOf(pt);
                    if (!a || !b || a->tag != b->tag) return false;
                }
            }
            return true;
        };
        std::vector<std::string> names;
        auto addScopes = [&](std::string scope) {
            for (;;) {
                names.push_back(scope.empty() ? op : scope + "::" + op);
                if (scope.empty()) break;
                std::size_t cut = scope.rfind("::");
                scope = cut == std::string::npos ? std::string() : scope.substr(0, cut);
            }
        };
        addScopes(inNs_);
        for (const ET& t : types) {
            if (!t.type || t.type->kind != Type::Kind::Struct) continue;
            const Type* c = canonOf(t.type.get());
            std::string tag = c ? c->tag : t.type->tag;
            std::size_t lt = tag.find('<');
            std::size_t cut = tag.rfind("::", lt == std::string::npos ? std::string::npos : lt);
            if (cut != std::string::npos) addScopes(tag.substr(0, cut));
        }
        for (const std::string& name : names) {
            bool ok = false;
            auto ov = overloads_.find(name);
            if (ov != overloads_.end()) {
                for (const Param& p : ov->second)
                    if (viable(p.type.get())) ok = true;
            } else {
                auto f = scopes_.front().find(name);
                ok = f != scopes_.front().end() && f->second.func && viable(f->second.type.get());
            }
            if (!ok) continue;
            auto call = mkE(Expr::Kind::Call, e);
            call->kids.push_back(mkName(name, e));
            for (std::size_t i = 0; i < arity; ++i) call->kids.push_back(std::move(e.kids[i]));
            e.kind = Expr::Kind::Call;
            e.text.clear();
            e.ival = 0;
            e.kids = std::move(call->kids);
            e.checked.reset();
            return true;
        }
        return false;
    }

    // x->m where x is an object of a class with operator->: (x.operator->())->m ([over.ref]). The
    // result is a pointer here (an operator-> that returns another class is not followed further).
    void rewriteArrow(Expr& e) {
        Expr& lhs = *e.kids[0];
        if (!simpleOperand(lhs) && lhs.kind != Expr::Kind::Call) return;
        ++quiet_;
        ET probe = checkExpr(lhs);
        --quiet_;
        const Type* pt = probe.type.get();
        if (pt && pt->kind == Type::Kind::Reference && pt->inner) pt = pt->inner.get();
        if (probe.bad || !pt || pt->kind != Type::Kind::Struct) return;
        const Type* st = canonOf(pt);
        bool found = false;
        if (st)
            for (const Param& f : st->params)
                if (f.name == "operator->" && !f.mangled.empty()) found = true;
        if (!found) return;
        auto member = mkE(Expr::Kind::Member, e);
        member->text = ".";
        member->kids.push_back(std::move(e.kids[0]));
        member->kids.push_back(mkName("operator->", e));
        auto call = mkE(Expr::Kind::Call, e);
        call->kids.push_back(std::move(member));
        e.kids[0] = std::move(call);
    }

    // An object of a class with a conversion function used where a number or pointer is needed:
    // e becomes e.operator T() (the first conversion function the class has). Only an lvalue
    // object is converted (a call result is not), and the result is not converted again.
    void convertIfNeeded(Expr& e) {
        const bool call = e.kind == Expr::Kind::Call;
        if (!simpleOperand(e) && !call) return;
        ++quiet_;
        ET probe = checkExpr(e);
        --quiet_;
        // A call result (f() in "if (f())", [conv] p4 contextual conversion) is converted on a hidden
        // copy: the member call below puts an object result in a hidden local.
        if (probe.bad || !probe.type || probe.type->kind != Type::Kind::Struct || (!probe.lvalue && !call)) return;
        const Type* st = canonOf(probe.type.get());
        if (!st) return;
        const Param* conv = nullptr;
        for (const Param& f : st->params)
            if (f.name.compare(0, 9, "operator ") == 0 && !f.mangled.empty()) {
                conv = &f;
                break;
            }
        if (!conv) return;
        auto self = std::make_unique<Expr>();
        self->kind = e.kind;
        self->text = e.text;
        self->ival = e.ival;
        self->litUnsigned = e.litUnsigned;
        self->litRank = e.litRank;
        self->line = e.line;
        self->col = e.col;
        self->file = e.file;
        self->kids = std::move(e.kids);
        self->ty = std::move(e.ty);
        auto member = mkE(Expr::Kind::Member, e);
        member->text = ".";
        member->kids.push_back(std::move(self));
        member->kids.push_back(mkName(conv->name, e));
        e.kind = Expr::Kind::Call;
        e.text.clear();
        e.ival = 0;
        e.kids.clear();
        e.kids.push_back(std::move(member));
        e.checked.reset();
    }

    ET checkExpr(Expr& e) {
        if (!e.file.empty()) file_ = e.file;
        {
            auto cached = etCache_.find(&e);
            if (cached != etCache_.end()) {
                ET r = std::move(cached->second);
                etCache_.erase(cached);
                return r;
            }
        }
        if ((e.kind == Expr::Kind::Assign || e.kind == Expr::Kind::Pre || e.kind == Expr::Kind::Post) &&
            !e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Cond && e.kids[0]->kids.size() == 3) {
            // (c ? a : b) = v, ++(c ? a : b): with two lvalues of one type the conditional is an
            // lvalue (ISO/IEC 14882 [expr.cond]); it becomes *(c ? &a : &b).
            Expr& cond = *e.kids[0];
            auto lv = mkE(Expr::Kind::Cond, cond);
            lv->kids.push_back(std::move(cond.kids[0]));
            lv->kids.push_back(eUnary("&", std::move(cond.kids[1]), cond));
            lv->kids.push_back(eUnary("&", std::move(cond.kids[2]), cond));
            auto d = eUnary("*", std::move(lv), cond);
            e.kids[0] = std::move(d);
        }
        if (e.kind == Expr::Kind::Assign && e.text == "=" && e.kids.size() == 2 && e.kids[0] && e.kids[1] &&
            e.kids[1]->kind == Expr::Kind::Brace)
            braceAssign(e);
        if (e.kind == Expr::Kind::Member && e.text == "->" && e.kids.size() == 2 && e.kids[0]) rewriteArrow(e);
        if (!rewriteOperator(e)) rewriteFreeOperator(e);
        ET r = checkExprBody(e);
        if (r.type) e.checked = cloneType(*r.type);
        return r;
    }

    ET checkBin(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return bad(e.line, e.col);
        convertIfNeeded(*e.kids[0]);
        convertIfNeeded(*e.kids[1]);
        ET a = decay(checkExpr(*e.kids[0]));
        ET b = decay(checkExpr(*e.kids[1]));
        if (a.bad || b.bad || !a.type || !b.type) return bad(e.line, e.col);
        const std::string& op = e.text;
        auto result = [&](std::unique_ptr<Type> t) {
            ET r;
            r.type = std::move(t);
            r.line = e.line;
            r.col = e.col;
            return r;
        };
        int aw = fpWidthOf(a.type.get());
        int bw = fpWidthOf(b.type.get());
        if ((aw || bw) && (op == "+" || op == "-" || op == "*" || op == "/" || op == "<" || op == ">" || op == "<=" ||
                           op == ">=" || op == "==" || op == "!=")) {
            bool aok = aw || isIntish(*a.type);
            bool bok = bw || isIntish(*b.type);
            if (aok && bok) {
                if (op == "+" || op == "-" || op == "*" || op == "/") {
                    int w = (aw == 8 || bw == 8) ? 8 : 4;
                    return result(prim(w == 8 ? Type::Kind::Double : Type::Kind::Float));
                }
                return result(prim(Type::Kind::Int));
            }
        }
        // The usual arithmetic conversions (ISO C 6.3.1.8): char, short and bool become int; a long
        // operand makes the result long, unsigned when that long is unsigned; otherwise int,
        // unsigned when an int operand is. A shift has the promoted type of its left operand.
        auto arithType = [&](const Type& x, const Type& y) {
            bool xl = x.kind == Type::Kind::Long, yl = y.kind == Type::Kind::Long;
            bool xi = x.kind == Type::Kind::Int, yi = y.kind == Type::Kind::Int;
            auto t = prim(xl || yl ? Type::Kind::Long : Type::Kind::Int);
            if (xl || yl) t->unsign = (xl && x.unsign) || (yl && y.unsign);
            else t->unsign = (xi && x.unsign) || (yi && y.unsign);
            return t;
        };
        auto promoted = [&](const Type& x) {
            auto t = prim(x.kind == Type::Kind::Long ? Type::Kind::Long : Type::Kind::Int);
            t->unsign = (x.kind == Type::Kind::Long || x.kind == Type::Kind::Int) && x.unsign;
            return t;
        };
        if (op == "+" ) {
            if (isIntish(*a.type) && isIntish(*b.type)) return result(arithType(*a.type, *b.type));
            if (isPtr(*a.type) && isIntish(*b.type)) return result(cloneType(*a.type));
            if (isIntish(*a.type) && isPtr(*b.type)) return result(cloneType(*b.type));
        } else if (op == "-") {
            if (isIntish(*a.type) && isIntish(*b.type)) return result(arithType(*a.type, *b.type));
            if (isPtr(*a.type) && isIntish(*b.type)) return result(cloneType(*a.type));
            if (isPtr(*a.type) && isPtr(*b.type) && a.type->inner && b.type->inner &&
                sameType(*a.type->inner, *b.type->inner))
                return result(prim(Type::Kind::Long));  // ptrdiff_t
        } else if (op == "<<" || op == ">>") {
            if (isIntish(*a.type) && isIntish(*b.type)) return result(promoted(*a.type));
        } else if (op == "*" || op == "/" || op == "%" || op == "&" || op == "|" || op == "^") {
            if (isIntish(*a.type) && isIntish(*b.type)) return result(arithType(*a.type, *b.type));
        } else if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=") {
            bool arith = isIntish(*a.type) && isIntish(*b.type);
            bool ptrs = isPtr(*a.type) && isPtr(*b.type) && a.type->inner && b.type->inner &&
                        sameType(*a.type->inner, *b.type->inner);
            bool null = (isPtr(*a.type) && b.nullc) || (isPtr(*b.type) && a.nullc);
            if (arith || ptrs || null) return result(prim(Type::Kind::Int));
        } else if (op == "&&" || op == "||") {
            if (scalar(*a.type) && scalar(*b.type)) return result(prim(Type::Kind::Int));
        }
        err(e.line, e.col, "invalid operands to '" + op + "'");
        return bad(e.line, e.col);
    }

    ET checkAssign(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return bad(e.line, e.col);
        ET lhs = checkExpr(*e.kids[0]);
        if (!lhs.bad && lhs.type && lhs.type->kind != Type::Kind::Struct) convertIfNeeded(*e.kids[1]);
        ET rhs = decay(checkExpr(*e.kids[1]));
        if (lhs.bad || rhs.bad || !lhs.type) return bad(e.line, e.col);
        if (!lhs.mod) {
            err(e.line, e.col, "cannot assign to this expression");
            return bad(e.line, e.col);
        }
        if (e.text == "=") {
            if (!canAssign(*lhs.type, rhs)) {
                err(e.line, e.col, "cannot assign this value");
                return bad(e.line, e.col);
            }
        } else if (!isIntish(*lhs.type) || !rhs.type || !isIntish(*rhs.type)) {
            // a op= b on a floating or pointer lvalue: a = a op b ([expr.ass] p7: a is evaluated once,
            // so this is only done when a has no side effects).
            bool arith = (isFp(*lhs.type) && (isFp(*rhs.type) || isIntish(*rhs.type))) ||
                         (isIntish(*lhs.type) && isFp(*rhs.type)) ||
                         (isPtr(*lhs.type) && isIntish(*rhs.type) && (e.text == "+=" || e.text == "-="));
            if (arith && hasSideEffects(*e.kids[0]) && e.text.size() >= 2) {
                // The left side has effects (v[i] *= x with an operator[] call): take its address
                // once, then *p = *p op x ([expr.ass] p7).
                std::string tmp = newTemp(*ptrTo(cloneType(*lhs.type)), e, false);
                auto setp = eAssignNode(mkName(tmp, e), eUnary("&", std::move(e.kids[0]), e), e);
                auto bin = mkE(Expr::Kind::Bin, e);
                bin->text = e.text.substr(0, e.text.size() - 1);
                bin->kids.push_back(eUnary("*", mkName(tmp, e), e));
                bin->kids.push_back(std::move(e.kids[1]));
                auto as = mkE(Expr::Kind::Assign, e);
                as->text = "=";
                as->kids.push_back(eUnary("*", mkName(tmp, e), e));
                as->kids.push_back(std::move(bin));
                e.kind = Expr::Kind::Comma;
                e.text.clear();
                e.ival = 0;
                e.kids.clear();
                e.kids.push_back(std::move(setp));
                e.kids.push_back(std::move(as));
                return checkExprBody(e);
            }
            if (!arith || hasSideEffects(*e.kids[0]) || e.text.size() < 2) {
                err(e.line, e.col, "cannot assign this value");
                return bad(e.line, e.col);
            }
            auto bin = mkE(Expr::Kind::Bin, e);
            bin->text = e.text.substr(0, e.text.size() - 1);
            bin->kids.push_back(cloneExpr(*e.kids[0]));
            bin->kids.push_back(std::move(e.kids[1]));
            e.kids[1] = std::move(bin);
            e.text = "=";
            return checkAssign(e);
        }
        ET r;
        r.type = cloneType(*lhs.type);
        r.line = e.line;
        r.col = e.col;
        return r;
    }

    std::unique_ptr<Expr> mkE(Expr::Kind k, const Expr& at) {
        auto n = std::make_unique<Expr>();
        n->kind = k;
        n->line = at.line;
        n->col = at.col;
        n->file = at.file;
        return n;
    }
    std::unique_ptr<Expr> mkName(const std::string& text, const Expr& at) {
        auto n = mkE(Expr::Kind::Name, at);
        n->text = text;
        return n;
    }

    // Score how well an argument fits a parameter: 0 exact, 1 by conversion, -1 not at all.
    int fitScore(const Type& param, const ET& arg) {
        if (arg.bad || !arg.type) return 0;
        if (param.kind == Type::Kind::Reference && param.inner && param.rref) {
            // T&& prefers an rvalue or the result of std::move; an lvalue fits only as a last resort
            // (a forwarding reference in a template instance takes it).
            bool rv = !arg.lvalue || arg.xvalue;
            if (arg.type->cnst && !param.inner->cnst) return -1;  // const B&& does not bind to B&&
            if (sameType(*param.inner, *arg.type)) return rv ? 0 : 3;
            return derivedFrom(*arg.type, *param.inner) ? (rv ? 1 : 3) : -1;
        }
        if (param.kind == Type::Kind::Reference && param.inner) {
            // A temporary binds to const T& ([dcl.init.ref]; const is not tracked, so any T&),
            // worse than to T&&.
            if (!arg.lvalue)
                return sameType(*param.inner, *arg.type) || derivedFrom(*arg.type, *param.inner) ? 2 : -1;
            if (arg.type->cnst && !param.inner->cnst) return -1;  // a const object does not bind to B&
            if (arg.xvalue) return sameType(*param.inner, *arg.type) ? 2 : -1;  // T& for a moved value: worse than T&&
            if (sameType(*param.inner, *arg.type)) return 0;
            return derivedFrom(*arg.type, *param.inner) ? 1 : -1;
        }
        if (sameType(param, *arg.type)) return 0;
        if (!canAssign(param, arg)) return -1;
        // [over.ics.rank]: a promotion (float to double, a small integer or bool to int) beats a
        // conversion (float to int, int to long, ...).
        using K = Type::Kind;
        if (param.kind == K::Double && arg.type->kind == K::Float) return 1;
        if (param.kind == K::Int && !param.unsign &&
            (arg.type->kind == K::Char || arg.type->kind == K::Short || arg.type->kind == K::Bool))
            return 1;
        return 4;
    }

    // A prototype type that names a template parameter ("\x02T", alone or inside an instance tag).
    static bool hasPlaceholder(const Type& t) {
        if ((t.kind == Type::Kind::Struct || t.kind == Type::Kind::Union) && t.tag.find('\x02') != std::string::npos)
            return true;
        if (t.inner && hasPlaceholder(*t.inner)) return true;
        for (const Param& p : t.params)
            if (p.type && hasPlaceholder(*p.type)) return true;
        return false;
    }

    // Choose among overloads by argument count, then by how well the argument types fit.
    // 'firstArg' is the index in call.kids of the first user argument.
    // Fewer arguments than parameters, and every missing one has a default argument.
    static bool fitsWithDefaults(const Type& fn, std::size_t nargs) {
        if (nargs >= fn.params.size()) return false;
        for (std::size_t i = nargs; i < fn.params.size(); ++i)
            if (!fn.params[i].defArg) return false;
        return true;
    }
    // Append the default arguments a call leaves out ([dcl.fct.default]); 'skip' parameters of fn
    // (this) have no argument in the call yet.
    // Each use of a default argument that makes a temporary (T{}) gets its own hidden name.
    static void renameIn(Expr& e, const std::string& from, const std::string& to) {
        if (e.kind == Expr::Kind::Name && e.text == from) e.text = to;
        if (e.kind == Expr::Kind::Comma && e.text == from) e.text = to;
        for (auto& k : e.kids)
            if (k) renameIn(*k, from, to);
    }
    static void freshTemps(Expr& e) {
        static int n = 0;
        if (e.kind == Expr::Kind::Comma && e.ival == 7 && !e.text.empty()) {
            std::string to = e.text + "_d" + std::to_string(n++);
            std::string from = e.text;  // a copy: renameIn changes e.text
            renameIn(e, from, to);
        }
        for (auto& k : e.kids)
            if (k) freshTemps(*k);
    }
    void addDefaults(Expr& call, const Type& fn, std::size_t argsBefore, const std::string& cls = std::string()) {
        std::size_t nargs = call.kids.size() - 1 - argsBefore;
        // A default argument's names are looked up where the function was declared
        // ([dcl.fct.default]/5): in the namespaces of the callee's name, innermost first; for a
        // member function, in its class first (a static member such as npos), then the class's
        // namespaces.
        std::string ns = cls;
        if (ns.empty() && call.kids[0] && call.kids[0]->kind == Expr::Kind::Name) {
            const std::string& cn = call.kids[0]->text;
            std::size_t cut = cn.rfind("::");
            if (cut != std::string::npos && cn.compare(0, 2, "_Z") != 0) ns = cn.substr(0, cut);
        }
        for (std::size_t i = nargs; i < fn.params.size(); ++i) {
            if (!fn.params[i].defArg) break;
            auto d = cloneExpr(*fn.params[i].defArg);
            freshTemps(*d);
            if (!ns.empty()) qualifyNames(*d, ns);
            call.kids.push_back(std::move(d));
        }
    }
    void qualifyNames(Expr& e, const std::string& ns) {
        if (e.kind == Expr::Kind::Name && !e.text.empty() && e.text[0] != '\x01' && e.text.find("::") == std::string::npos &&
            scopeOf(e.text) < 0) {
            for (std::string sc = ns; !sc.empty();) {
                if (scopes_.front().count(sc + "::" + e.text)) {
                    e.text = sc + "::" + e.text;
                    return;
                }
                std::size_t cut = sc.rfind("::");
                if (cut == std::string::npos) break;
                sc = sc.substr(0, cut);
            }
            return;
        }
        for (auto& k : e.kids)
            if (k) qualifyNames(*k, ns);
    }

    const Param* pickOverload(const std::vector<const Param*>& cands, Expr& call, std::size_t firstArg) {
        std::size_t nargs = call.kids.size() - firstArg;
        std::vector<const Param*> same;
        for (const Param* c : cands) {
            if (!c->type) continue;
            if (c->type->params.size() == nargs || (c->type->variadic && nargs >= c->type->params.size()) ||
                fitsWithDefaults(*c->type, nargs))
                same.push_back(c);
        }
        if (same.empty()) return cands.empty() ? nullptr : cands[0];
        if (same.size() == 1) return same[0];
        std::vector<ET> ets;
        for (std::size_t i = 0; i < nargs; ++i) {
            if (call.kids[firstArg + i] && call.kids[firstArg + i]->kind == Expr::Kind::Brace) {
                ets.push_back(bad(call.line, call.col));  // {a, b}: fits any class parameter
                continue;
            }
            ets.push_back(decay(checkExpr(*call.kids[firstArg + i])));
        }
        const Param* best = nullptr;
        int bestScore = 1 << 30;
        for (const Param* c : same) {
            int total = 0;
            bool ok = true;
            const TemplateSig* tsig = nullptr;
            std::vector<std::unique_ptr<Type>> tb;
            if (c->mangled.compare(0, 7, "\x01" "mtmpl:") == 0) {
                // A member template: it fits when its parameters deduce ([temp.deduct.call]); a
                // non-template that fits as well is better ([over.match.best] p2.4).
                auto st = funcTemplateSigs().find(c->mangled.substr(7));
                if (st == funcTemplateSigs().end()) continue;
                tsig = &st->second;
                tb.resize(tsig->names.size());
                total = 1;
            }
            for (std::size_t i = 0; i < nargs && i < c->type->params.size(); ++i) {
                if (!c->type->params[i].type) continue;
                if (tsig && hasPlaceholder(*c->type->params[i].type)) {
                    if (ets[i].bad || !ets[i].type) continue;
                    ET full = checkExpr(*call.kids[firstArg + i]);
                    if (!deduceArg(*c->type->params[i].type, full, *tsig, tb)) {
                        ok = false;
                        break;
                    }
                    continue;
                }
                int sc = fitScore(*c->type->params[i].type, ets[i]);
                if (sc < 0) {
                    ok = false;
                    break;
                }
                total += sc;
            }
            for (std::size_t k = 0; tsig && ok && k < tb.size(); ++k)
                if (!tb[k]) ok = false;  // a parameter no argument deduces: not viable
            if (ok && total < bestScore) {
                bestScore = total;
                best = c;
            }
        }
        return best ? best : same[0];
    }

    // ((R(*)(T*, ...))(*(void***)self)[slot]): the Itanium C++ ABI virtual call sequence.
    std::unique_ptr<Expr> vcallee(const Expr& at, std::unique_ptr<Expr> self, int slot, const std::string& tag,
                                  const Type& userFn) {
        auto cast1 = mkE(Expr::Kind::Cast, at);
        cast1->ty = ptrTo(ptrTo(ptrTo(prim(Type::Kind::Void))));
        cast1->kids.push_back(std::move(self));
        auto deref = mkE(Expr::Kind::Unary, at);
        deref->text = "*";
        deref->kids.push_back(std::move(cast1));
        auto idx = mkE(Expr::Kind::Index, at);
        idx->kids.push_back(std::move(deref));
        auto n = mkE(Expr::Kind::Int, at);
        n->ival = static_cast<std::uint64_t>(slot);
        idx->kids.push_back(std::move(n));
        auto fn = prim(Type::Kind::Function);
        fn->inner = userFn.inner ? cloneType(*userFn.inner) : prim(Type::Kind::Void);
        Param me;
        me.name = "this";
        auto st = prim(Type::Kind::Struct);
        st->tag = tag;
        me.type = ptrTo(std::move(st));
        fn->params.push_back(std::move(me));
        for (const Param& q : userFn.params) {
            Param c;
            c.name = q.name;
            if (q.type) c.type = cloneType(*q.type);
            fn->params.push_back(std::move(c));
        }
        auto cast2 = mkE(Expr::Kind::Cast, at);
        cast2->ty = ptrTo(std::move(fn));
        cast2->kids.push_back(std::move(idx));
        return cast2;
    }

    // Match a prototype type (with "\x02T" placeholders) against an argument type and bind T.
    // One call argument against one parameter pattern. A forwarding reference (T&& with T a
    // parameter of this template, [temp.deduct.call]/3) given an lvalue deduces T as an lvalue
    // reference, so std::forward<T> collapses back to T& ([dcl.ref]/6).
    bool deduceArg(const Type& pat, const ET& a, const TemplateSig& sig, std::vector<std::unique_ptr<Type>>& bound) {
        if (pat.kind == Type::Kind::Reference && pat.rref && pat.inner && pat.inner->kind == Type::Kind::Struct &&
            !pat.inner->tag.empty() && pat.inner->tag[0] == '\x02' && a.lvalue && !a.xvalue && a.type &&
            a.type->kind != Type::Kind::Function) {
            for (std::size_t i = 0; i < sig.names.size(); ++i) {
                if (sig.names[i] != pat.inner->tag.substr(1)) continue;
                auto r = prim(Type::Kind::Reference);
                r->inner = cloneType(*a.type);
                if (!bound[i]) {
                    bound[i] = std::move(r);
                    return true;
                }
                return sameType(*bound[i], *r);
            }
        }
        ET d = decay(copyET(a));
        return deduceType(pat, *d.type, sig, bound);
    }

    bool deduceType(const Type& pat, const Type& arg, const TemplateSig& sig,
                    std::vector<std::unique_ptr<Type>>& bound) {
        if (pat.kind == Type::Kind::Struct && !pat.tag.empty() && pat.tag[0] == '\x02') {
            for (std::size_t i = 0; i < sig.names.size(); ++i) {
                if (sig.names[i] != pat.tag.substr(1)) continue;
                std::unique_ptr<Type> a = cloneType(arg);
                if (a->kind == Type::Kind::Array && a->inner) a = ptrTo(std::move(a->inner));
                if (!bound[i]) {
                    bound[i] = std::move(a);
                    return true;
                }
                return sameType(*bound[i], *a);
            }
            return true;
        }
        if (pat.kind == Type::Kind::Struct && pat.tag.find('\x02') != std::string::npos &&
            arg.kind == Type::Kind::Struct) {
            // A class template instance written with the parameters (Node<T>): its tag holds the
            // marker; match it against the argument's tag piece by piece.
            const std::string& P = pat.tag;
            const std::string& Q = arg.tag;
            std::size_t i = 0;
            std::size_t j = 0;
            while (i < P.size() && j < Q.size()) {
                std::size_t k = i;
                std::size_t n = 0;
                while (k < P.size() && P[k] >= '0' && P[k] <= '9') n = n * 10 + static_cast<std::size_t>(P[k++] - '0');
                if (k > i && k < P.size() && P[k] == '\x02' && n >= 1) {
                    std::string name = P.substr(k + 1, n - 1);
                    std::size_t end = mangledEnd(Q, j);
                    if (end == j) return true;
                    auto found = typeFromMangle(Q.substr(j, end - j));
                    for (std::size_t b = 0; b < sig.names.size() && found; ++b)
                        if (sig.names[b] == name) {
                            if (!bound[b]) bound[b] = std::move(found);
                            else if (!sameType(*bound[b], *found)) return false;
                        }
                    i = k + n;
                    j = end;
                    continue;
                }
                if (P[i] != Q[j]) return true;
                ++i;
                ++j;
            }
            return true;
        }
        if (pat.kind == Type::Kind::Pointer && pat.inner) {
            if ((arg.kind == Type::Kind::Pointer || arg.kind == Type::Kind::Array) && arg.inner)
                return deduceType(*pat.inner, *arg.inner, sig, bound);
            return true;
        }
        if (pat.kind == Type::Kind::Reference && pat.inner) return deduceType(*pat.inner, arg, sig, bound);
        if (pat.kind == Type::Kind::Array && pat.inner) {
            if ((arg.kind == Type::Kind::Pointer || arg.kind == Type::Kind::Array) && arg.inner)
                return deduceType(*pat.inner, *arg.inner, sig, bound);
            return true;
        }
        return true;
    }

    // obj.f(args) where f is a member function template: deduce its parameters from the arguments
    // (e.kids[1..]), ask for the instance (made when the file is parsed again) and return its name.
    std::string memberInstance(const std::string& key, Expr& e, std::size_t firstArg = 1) {
        auto it = funcTemplateSigs().find(key);
        if (it == funcTemplateSigs().end() || !it->second.fn) {
            err(e.line, e.col, "unknown member template");
            return {};
        }
        const TemplateSig& sig = it->second;
        if (sig.fn->params.size() != e.kids.size() - firstArg) {
            err(e.line, e.col, "wrong number of arguments");
            return {};
        }
        std::vector<std::unique_ptr<Type>> bound(sig.names.size());
        for (std::size_t i = 0; i < sig.fn->params.size(); ++i) {
            ET a = checkExpr(*e.kids[i + firstArg]);
            if (a.bad || !a.type) return {};
            ET d = decay(copyET(a));
            if (sig.fn->params[i].type && !deduceArg(*sig.fn->params[i].type, a, sig, bound)) {
                err(e.line, e.col, "conflicting types for a template argument");
                return {};
            }
        }
        std::vector<TArg> args;
        for (std::size_t i = 0; i < sig.names.size(); ++i) {
            if (!bound[i]) {
                err(e.line, e.col, "cannot deduce template argument '" + sig.names[i] + "'");
                return {};
            }
            TArg a;
            a.type = std::move(bound[i]);
            args.push_back(std::move(a));
        }
        std::string inst = instFuncName(key, sig, args);
        bool known = false;
        for (const InstReq& r : instRequests())
            if (r.name == inst) known = true;
        if (!known) {
            InstReq r;
            r.tmpl = key;
            r.name = inst;
            r.args = std::move(args);
            instRequests().push_back(std::move(r));
            instDirty() = true;
        }
        return inst;
    }

    // An argument of another type for a class parameter: if the class has a constructor of one
    // parameter (not its copy constructor) that takes it, the argument becomes T(arg), a temporary.
    bool convertingTemp(Expr& arg, const Type& from, const Type& want) {
        const Type* T = canonOf(&want);
        if (!T || T->kind != Type::Kind::Struct || T->tag.empty()) return false;
        bool ok = false;
        for (const Param& p : T->params) {
            if (p.name != "<ctor>" || !p.type || p.type->params.size() != 1 || !p.type->params[0].type) continue;
            const Type* pt = p.type->params[0].type.get();
            if (pt->kind == Type::Kind::Reference && pt->inner && pt->inner->kind == Type::Kind::Struct &&
                pt->inner->tag == T->tag)
                continue;  // the copy constructor
            ET probe;
            probe.type = cloneType(from);
            probe = decay(std::move(probe));  // "text" is a char array: it is passed as const char *
            if (p.mangled.compare(0, 7, "\x01" "mtmpl:") == 0) {
                // A converting constructor template (unique_ptr<Base> from unique_ptr<Derived>&&).
                auto st = funcTemplateSigs().find(p.mangled.substr(7));
                if (st == funcTemplateSigs().end()) continue;
                std::vector<std::unique_ptr<Type>> tb(st->second.names.size());
                ET a;
                a.type = cloneType(from);
                bool all = deduceArg(*p.type->params[0].type, a, st->second, tb);
                for (auto& b : tb)
                    if (!b) all = false;
                if (all) ok = true;
                continue;
            }
            if (sameType(*pt, *probe.type) || canAssign(*pt, probe)) ok = true;
        }
        if (!ok) return false;
        std::string tmp = "__cv" + std::to_string(tempN_++);
        auto call = mkE(Expr::Kind::Call, arg);
        call->kids.push_back(mkName("\x01" "ctor:" + T->tag, arg));
        call->kids.push_back(eUnary("&", mkName(tmp, arg), arg));
        auto inner = std::make_unique<Expr>();
        inner->kind = arg.kind;
        inner->text = arg.text;
        inner->ival = arg.ival;
        inner->litUnsigned = arg.litUnsigned;
        inner->litRank = arg.litRank;
        inner->line = arg.line;
        inner->col = arg.col;
        inner->file = arg.file;
        inner->kids = std::move(arg.kids);
        inner->ty = std::move(arg.ty);
        inner->checked = std::move(arg.checked);
        call->kids.push_back(std::move(inner));
        arg.kind = Expr::Kind::Comma;
        arg.ival = 7;
        arg.text = tmp;
        arg.ty = cloneType(*T);
        arg.kids.clear();
        arg.checked.reset();
        arg.kids.push_back(std::move(call));
        arg.kids.push_back(mkName(tmp, arg));
        return true;
    }

    // A braced list where an object of class T is expected (an argument {a, b}, [dcl.init.list]):
    // a temporary T. A constructor whose parameter count matches takes the values; otherwise the
    // class is an aggregate: its implicit constructor (default member initializers) runs if it has
    // one, then each value goes to the next field, and without one the other fields are zero.
    // x = {a, b} ([expr.ass] p9): for a class, x = T{a, b} (a temporary made from the list, then
    // the class's assignment); for a scalar, x = {} is x = 0 and x = {v} is x = v.
    void braceAssign(Expr& e) {
        ++quiet_;
        ET lhs = checkExpr(*e.kids[0]);
        --quiet_;
        if (lhs.bad || !lhs.type) return;
        Expr& b = *e.kids[1];
        if (lhs.type->kind == Type::Kind::Struct) {
            braceToTemp(b, *lhs.type);
            return;
        }
        if (b.kids.size() > 1) return;  // the checker reports the list
        if (b.kids.empty()) {
            auto zero = mkE(Expr::Kind::Int, b);
            zero->ival = 0;
            e.kids[1] = std::move(zero);
        } else {
            e.kids[1] = std::move(b.kids[0]);
        }
    }

    void braceToTemp(Expr& b, const Type& want) {
        const Type* T = canonOf(&want);
        if (!T || T->tag.empty()) return;
        std::string tag = T->tag;
        std::vector<std::unique_ptr<Expr>> vals = std::move(b.kids);
        b.kids.clear();
        bool viaCtor = false;
        for (const Param& p : T->params) {
            if (p.name != "<ctor>" || !p.type || p.type->params.size() != vals.size()) continue;
            if (vals.empty()) continue;
            const Type* a0 = p.type->params[0].type.get();
            bool copy = vals.size() == 1 && a0 && a0->kind == Type::Kind::Reference && a0->inner &&
                        a0->inner->kind == Type::Kind::Struct && a0->inner->tag == tag;
            if (!copy) viaCtor = true;
        }
        std::string tmp = "__bt" + std::to_string(tempN_++);
        std::unique_ptr<Expr> seq;
        auto addE = [&](std::unique_ptr<Expr> x) { seq = seq ? eCommaNode(std::move(seq), std::move(x), b) : std::move(x); };
        auto self = [&]() { return eUnary("&", mkName(tmp, b), b); };
        if (viaCtor) {
            auto call = mkE(Expr::Kind::Call, b);
            call->kids.push_back(mkName("\x01" "ctor:" + tag, b));
            call->kids.push_back(self());
            for (auto& v : vals) call->kids.push_back(std::move(v));
            addE(std::move(call));
        } else {
            if (T->needCtor) {
                auto call = mkE(Expr::Kind::Call, b);
                call->kids.push_back(mkName("\x01" "ctor:" + tag, b));
                call->kids.push_back(self());
                addE(std::move(call));
            } else {
                // An aggregate: members the list does not name are value-initialized, zero here.
                auto call = mkE(Expr::Kind::Call, b);
                call->kids.push_back(mkName("\x01" "zero:", b));
                call->kids.push_back(self());
                addE(std::move(call));
            }
            std::size_t i = 0;
            for (const Param& f : T->params) {
                if (!f.mangled.empty() || !f.type || f.type->kind == Type::Kind::Function || f.name == "__vptr" ||
                    f.name == "__basepad" || f.name.empty() || f.name[0] == '<')
                    continue;
                std::unique_ptr<Expr> v;
                if (i < vals.size()) v = std::move(vals[i]);
                else if (!T->needCtor && f.type->kind != Type::Kind::Struct && f.type->kind != Type::Kind::Union &&
                         f.type->kind != Type::Kind::Array)
                    v = mkE(Expr::Kind::Int, b);
                ++i;
                if (!v) continue;
                if (v->kind == Expr::Kind::Brace && f.type->kind == Type::Kind::Struct) braceToTemp(*v, *f.type);
                auto field = mkE(Expr::Kind::Member, b);
                field->text = ".";
                field->kids.push_back(mkName(tmp, b));
                field->kids.push_back(mkName(f.name, b));
                addE(eAssignNode(std::move(field), std::move(v), b));
            }
            if (i < vals.size()) err(b.line, b.col, "too many initializers");
        }
        if (!seq) seq = mkE(Expr::Kind::Int, b);
        b.kind = Expr::Kind::Comma;
        b.ival = 7;
        b.text = tmp;
        b.ty = cloneType(*T);
        b.kids.push_back(std::move(seq));
        b.kids.push_back(mkName(tmp, b));
    }

    ET checkCall(Expr& e) {
        if (e.kids.empty() || !e.kids[0]) return bad(e.line, e.col);
        if (e.kids[0]->kind == Expr::Kind::Name && e.kids[0]->text == "\x01" "zero:" && e.kids.size() == 2 &&
            e.kids[1]) {
            checkExpr(*e.kids[1]);  // made by braceToTemp: zero an aggregate's bytes (lowerCall)
            ET r;
            r.type = prim(Type::Kind::Void);
            return r;
        }
        if (e.kids[0]->kind == Expr::Kind::Member && e.kids[0]->text == "->" && e.kids[0]->kids.size() == 2 &&
            e.kids[0]->kids[0])
            rewriteArrow(*e.kids[0]);  // p->f() with a smart pointer p: p.operator->()->f()
        if (e.kids[0]->kind == Expr::Kind::Name && e.kids.size() == 2 && e.kids[1] &&
            (e.kids[0]->text == "\x01" "begin" || e.kids[0]->text == "\x01" "end")) {
            // The range of a range-for: an array's first and one-past-last element, or the
            // class's begin() and end() ([stmt.ranged]).
            bool isEnd = e.kids[0]->text == "\x01" "end";
            auto rb = e.kids[1]->kind == Expr::Kind::Name ? rangeBound_.find(e.kids[1]->text) : rangeBound_.end();
            if (rb != rangeBound_.end()) {
                auto arr = std::move(e.kids[1]);
                auto bound = mkE(Expr::Kind::Int, e);
                if (isEnd) bound->ival = rb->second;
                e.kind = Expr::Kind::Bin;
                e.text = "+";
                e.kids.clear();
                e.kids.push_back(std::move(arr));
                e.kids.push_back(std::move(bound));
                return checkExprBody(e);
            }
            auto mem = mkE(Expr::Kind::Member, e);
            mem->text = ".";
            mem->kids.push_back(std::move(e.kids[1]));
            mem->kids.push_back(mkName(isEnd ? "end" : "begin", e));
            e.kids.clear();
            e.kids.push_back(std::move(mem));
        }
        if (!closureFn_.empty() && !(e.kids[0]->kind == Expr::Kind::Name &&
                                     (e.kids[0]->text.empty() || e.kids[0]->text[0] == '\x01'))) {
            // f(args) with f a closure object (a name, or a captured one: *__self->f): its
            // function, with &f first. The type decides, so a callee already rewritten counts.
            const Type* ft = nullptr;
            std::unique_ptr<Type> probed;
            if (e.kids[0]->kind == Expr::Kind::Name) {
                int at = scopeOf(e.kids[0]->text);
                if (at >= 0) ft = scopes_[static_cast<std::size_t>(at)][e.kids[0]->text].type.get();
                else if (!lamStack_.empty()) ft = nullptr;
            } else if (e.kids[0]->kind == Expr::Kind::Unary || e.kids[0]->kind == Expr::Kind::Member) {
                ++quiet_;
                ET t = checkExpr(*e.kids[0]);
                --quiet_;
                if (!t.bad && t.type) probed = cloneType(*t.type);
                ft = probed.get();
            }
            if (ft && ft->kind == Type::Kind::Reference && ft->inner) ft = ft->inner.get();
            if (ft && ft->kind == Type::Kind::Struct && closureFn_.count(ft->tag)) {
                std::string fname = closureFn_[ft->tag];
                auto obj = std::move(e.kids[0]);
                e.kids[0] = mkName(fname, e);
                e.kids.insert(e.kids.begin() + 1, eUnary("&", std::move(obj), e));
            }
        }
        Expr& c0 = *e.kids[0];
        static const std::string kCtor = "\x01" "ctor:";
        static const std::string kTmpl = "\x01" "tmpl:";
        if (c0.kind == Expr::Kind::Name && c0.text.compare(0, kTmpl.size(), kTmpl) == 0 && c0.kids.empty() &&
            scopeOf(c0.text.substr(kTmpl.size())) > 0)
            c0.text = c0.text.substr(kTmpl.size());  // a local of that name (a lambda) hides the templates
        if (c0.kind == Expr::Kind::Name && c0.text.compare(0, kTmpl.size(), kTmpl) == 0 && c0.kids.empty() && method_) {
            // Inside a member function a member of the class hides a namespace function template of
            // the same name ([basic.lookup.unqual], [class.member.lookup]): std::string::find
            // calling find(...) means this->find, not std::find.
            std::string bareName = c0.text.substr(kTmpl.size());
            const Type* st = canonOf(method_);
            if (st)
                for (const Param& f : st->params)
                    if (f.name == bareName && !f.mangled.empty() && !isSpecialName(f.name)) c0.text = bareName;
        }
        if (c0.kind == Expr::Kind::Name && c0.text.compare(0, kTmpl.size(), kTmpl) == 0) {
            std::string name = c0.text.substr(kTmpl.size());
            // The function templates of this name (name, name#1, ...): the first whose parameter
            // count fits and whose arguments deduce ([temp.over], [temp.deduct.call]).
            std::vector<std::string> keys;
            for (int k = 0;; ++k) {
                std::string key = k == 0 ? name : name + "#" + std::to_string(k);
                auto it = funcTemplateSigs().find(key);
                if (it == funcTemplateSigs().end()) break;
                if (it->second.fn && it->second.fn->params.size() == e.kids.size() - 1) keys.push_back(key);
            }
            if (keys.empty()) {
                if (!funcTemplateSigs().count(name)) err(e.line, e.col, "unknown function template '" + name + "'");
                else err(e.line, e.col, "wrong number of arguments");
                return bad(e.line, e.col);
            }
            std::vector<ET> argTypes;
            for (std::size_t i = 1; i < e.kids.size(); ++i) {
                ET a = checkExpr(*e.kids[i]);
                if (a.bad || !a.type) return bad(e.line, e.col);
                argTypes.push_back(std::move(a));
            }
            const TemplateSig* chosen = nullptr;
            std::vector<std::unique_ptr<Type>> bound;
            for (const std::string& key : keys) {
                const TemplateSig& cand = funcTemplateSigs()[key];
                std::vector<std::unique_ptr<Type>> b(cand.names.size());
                for (std::size_t i = 0; i < c0.kids.size() && i < b.size(); ++i)
                    if (c0.kids[i] && c0.kids[i]->ty) b[i] = cloneType(*c0.kids[i]->ty);
                bool ok = true;
                for (std::size_t i = 0; i < cand.fn->params.size() && ok; ++i)
                    if (cand.fn->params[i].type && !deduceArg(*cand.fn->params[i].type, argTypes[i], cand, b))
                        ok = false;
                for (std::size_t i = 0; i < b.size() && ok; ++i)
                    if (!b[i]) ok = false;
                if (!ok) continue;
                chosen = &cand;
                name = key;
                bound = std::move(b);
                break;
            }
            if (!chosen) {
                err(e.line, e.col, "no function template '" + name + "' fits these arguments");
                return bad(e.line, e.col);
            }
            const TemplateSig& sig = *chosen;
            std::vector<TArg> args;
            for (std::size_t i = 0; i < sig.names.size(); ++i) {
                if (!bound[i]) {
                    err(e.line, e.col, "cannot deduce template argument '" + sig.names[i] + "'");
                    return bad(e.line, e.col);
                }
                TArg a;
                a.type = std::move(bound[i]);
                args.push_back(std::move(a));
            }
            std::string inst = instFuncName(name, sig, args);
            bool known = false;
            for (const InstReq& r : instRequests())
                if (r.name == inst) known = true;
            if (!known) {
                InstReq r;
                r.tmpl = name;
                r.name = inst;
                r.args = std::move(args);
                instRequests().push_back(std::move(r));
                instDirty() = true;
            }
            c0.text = inst;
        } else if (c0.kind == Expr::Kind::Name && c0.text.compare(0, 8, "\x01" "dtorif:") == 0) {
            // The destructor of a local of a class used inside its own body: run it if the
            // finished class has one.
            auto it = canonStructs().find(c0.text.substr(8));
            const Param* d = nullptr;
            if (it != canonStructs().end() && it->second)
                for (const Param& p : it->second->params)
                    if (p.name == "<dtor>" && !p.mangled.empty()) d = &p;
            if (!d) {
                e.kind = Expr::Kind::Int;
                e.ival = 0;
                e.kids.clear();
                e.text.clear();
                return checkExpr(e);
            }
            c0.text = d->mangled;
            return checkCall(e);
        } else if (c0.kind == Expr::Kind::Name && c0.text.compare(0, kCtor.size(), kCtor) == 0) {
            std::string tag = c0.text.substr(kCtor.size());
            auto it = canonStructs().find(tag);
            std::vector<const Param*> cands;
            if (it != canonStructs().end() && it->second)
                for (const Param& p : it->second->params)
                    if (p.name == "<ctor>") cands.push_back(&p);
            if (cands.empty() && e.kids.size() == 2 && it != canonStructs().end() && it->second) {
                // T() for a class without constructors: value-initialized, every field zero
                // ([dcl.init] p8). A class or array field is left as it is.
                std::unique_ptr<Expr> seq;
                for (const Param& f : it->second->params) {
                    if (!f.mangled.empty() || !f.type || f.type->kind == Type::Kind::Function ||
                        f.type->kind == Type::Kind::Struct || f.type->kind == Type::Kind::Union ||
                        f.type->kind == Type::Kind::Array || f.name.empty() || f.name[0] == '<' || f.name == "__vptr")
                        continue;
                    auto field = mkE(Expr::Kind::Member, e);
                    field->text = "->";
                    field->kids.push_back(cloneExpr(*e.kids[1]));
                    field->kids.push_back(mkName(f.name, e));
                    auto as = eAssignNode(std::move(field), mkE(Expr::Kind::Int, e), e);
                    seq = seq ? eCommaNode(std::move(seq), std::move(as), e) : std::move(as);
                }
                if (!seq) seq = mkE(Expr::Kind::Int, e);
                e.kind = Expr::Kind::Comma;
                e.text.clear();
                e.ival = 0;
                e.kids.clear();
                e.kids.push_back(std::move(seq));
                e.kids.push_back(mkE(Expr::Kind::Int, e));
                return checkExprBody(e);
            }
            if (cands.empty()) {
                err(e.line, e.col, "'" + tag + "' has no constructor");
                return bad(e.line, e.col);
            }
            if (e.kids.size() == 3 && e.kids[2] && e.kids[2]->kind == Expr::Kind::Brace && e.kids[2]->ival == 5) {
                // T{a, b, ...} for a class with an initializer-list constructor ([over.match.list]):
                // the elements go into a hidden array and the constructor gets
                // std::initializer_list<E>{array, n} ([dcl.init.list]/5).
                for (const Param* c : cands) {
                    if (!c->type || c->type->params.size() != 1 || !c->type->params[0].type) continue;
                    const Type* pt = c->type->params[0].type.get();
                    if (pt->kind == Type::Kind::Reference && pt->inner) pt = pt->inner.get();
                    if (pt->kind != Type::Kind::Struct || (pt->tag.compare(0, 22, "std::initializer_list<") != 0 && pt->tag.compare(0, 17, "initializer_list<") != 0)) continue;
                    auto list = initList(*e.kids[2], *pt);
                    if (!list) return bad(e.line, e.col);
                    e.kids[2] = std::move(list);
                    c0.text = c->mangled;
                    return checkCall(e);
                }
                // no such constructor: the elements are the arguments
                auto br = std::move(e.kids[2]);
                e.kids.pop_back();
                for (auto& k : br->kids) e.kids.push_back(std::move(k));
                return checkCall(e);
            }
            const Param* pick = pickOverload(cands, e, 2);
            c0.text = pick->mangled;
            if (c0.text.compare(0, 7, "\x01" "mtmpl:") == 0) {
                // A constructor template ([temp.mem]): deduce from the arguments, ask for the instance.
                c0.text = memberInstance(c0.text.substr(7), e, 2);
                if (c0.text.empty()) return bad(e.line, e.col);
            }
        } else if (c0.kind == Expr::Kind::Name && c0.text == "\x01" "throw") {
            if (e.kids.size() == 1) {
                c0.text = "__okay_eh_rethrow";  // throw;
                return checkCall(e);
            }
            // throw e: the object is copied into memory the runtime owns, then thrown with the names
            // of its type (a class: the class, then its bases) and its destructor.
            ET src = decay(checkExpr(*e.kids[1]));
            if (src.bad || !src.type) return bad(e.line, e.col);
            std::unique_ptr<Type> T = cloneType(*src.type);
            std::string types;
            std::string dtorName;
            if (T->kind == Type::Kind::Struct && !T->tag.empty()) {
                std::string walk = T->tag;
                while (!walk.empty()) {
                    auto cit = canonStructs().find(walk);
                    if (cit == canonStructs().end() || !cit->second) break;
                    Type tmp;
                    tmp.kind = Type::Kind::Struct;
                    tmp.tag = walk;
                    types += mangleType(tmp) + "\n";
                    walk = cit->second->base;
                }
                auto cit = canonStructs().find(T->tag);
                const Param* d1 = cit != canonStructs().end() ? findSpecial(cit->second.get(), "<dtor>") : nullptr;
                if (d1) dtorName = d1->mangled;
            } else {
                types = mangleTypeNoCv(*T);
            }
            std::string th = newTemp(*ptrTo(cloneType(*T)), e, false);
            auto sizeOf = mkE(Expr::Kind::Unary, e);
            sizeOf->text = "sizeof";
            sizeOf->ty = cloneType(*T);
            std::vector<std::unique_ptr<Expr>> aa;
            aa.push_back(std::move(sizeOf));
            auto allocCall = mkE(Expr::Kind::Call, e);
            allocCall->kids.push_back(mkName("__okay_alloc", e));
            for (auto& k : aa) allocCall->kids.push_back(std::move(k));
            auto cast = mkE(Expr::Kind::Cast, e);
            cast->ty = ptrTo(cloneType(*T));
            cast->kids.push_back(std::move(allocCall));
            auto allocAssign = eAssignNode(mkName(th, e), std::move(cast), e);
            std::unique_ptr<Expr> copy;
            if (T->kind == Type::Kind::Struct) {
                auto idx0 = mkE(Expr::Kind::Index, e);
                idx0->kids.push_back(mkName(th, e));
                idx0->kids.push_back(mkE(Expr::Kind::Int, e));
                auto self = eUnary("&", std::move(idx0), e);
                auto cc = mkE(Expr::Kind::Call, e);
                cc->kids.push_back(mkName("\x01" "cinit:" + T->tag, e));
                cc->kids.push_back(std::move(self));
                cc->kids.push_back(std::move(e.kids[1]));
                copy = std::move(cc);
            } else {
                auto idx0 = mkE(Expr::Kind::Index, e);
                idx0->kids.push_back(mkName(th, e));
                idx0->kids.push_back(mkE(Expr::Kind::Int, e));
                copy = eAssignNode(std::move(idx0), std::move(e.kids[1]), e);
            }
            auto throwCall = mkE(Expr::Kind::Call, e);
            throwCall->kids.push_back(mkName("__okay_throw", e));
            auto objLong = mkE(Expr::Kind::Cast, e);
            objLong->ty = prim(Type::Kind::Long);
            objLong->kids.push_back(mkName(th, e));
            throwCall->kids.push_back(std::move(objLong));
            auto typesStr = mkE(Expr::Kind::String, e);
            typesStr->text = types;
            throwCall->kids.push_back(std::move(typesStr));
            if (dtorName.empty()) {
                auto zero = mkE(Expr::Kind::Int, e);
                throwCall->kids.push_back(std::move(zero));
            } else {
                auto dl = mkE(Expr::Kind::Cast, e);
                dl->ty = prim(Type::Kind::Long);
                dl->kids.push_back(mkName(dtorName, e));
                throwCall->kids.push_back(std::move(dl));
            }
            e.kind = Expr::Kind::Comma;
            e.text.clear();
            e.ival = 0;
            e.kids.clear();
            e.kids.push_back(std::move(allocAssign));
            e.kids.push_back(std::move(copy));
            e.kids.push_back(std::move(throwCall));
            return checkExprBody(e);
        } else if (c0.kind == Expr::Kind::Name && c0.text.compare(0, 7, "\x01" "cinit:") == 0 && e.kids.size() == 3) {
            // T x = y: the copy constructor when y is an lvalue of class T, else a plain copy of
            // the bytes (y is a temporary: the object is built in place).
            std::string tag = c0.text.substr(7);
            ET src = checkExpr(*e.kids[2]);
            if (src.bad) return bad(e.line, e.col);
            if (e.kids[2]->kind == Expr::Kind::Comma && e.kids[2]->ival == 9) {
                consumed(*e.kids[2]);  // built in place
            }
            const bool sameClass = src.type && src.type->kind == Type::Kind::Struct && src.type->tag == tag;
            // Another type (an int for a constructor taking an int) is an ordinary constructor call;
            // an object of the class is copied (copy constructor, else bytes).
            bool useCtor = !sameClass || (src.lvalue && hasUserCopy(*src.type));
            if (useCtor) {
                e.kids[0]->text = "\x01" "ctor:" + tag;
                return checkCall(e);
            }
            auto self = std::move(e.kids[1]);
            auto rhs = std::move(e.kids[2]);
            // self is "&name": assign to the name itself.
            std::unique_ptr<Expr> target = std::move(self->kids[0]);
            e.kind = Expr::Kind::Assign;
            e.text = "=";
            e.ival = 3;  // an initialization: operator= must not be used
            e.kids.clear();
            e.kids.push_back(std::move(target));
            e.kids.push_back(std::move(rhs));
            return checkExpr(e);
        } else if (c0.kind == Expr::Kind::Name && c0.text == "\x01" "delete[]" && e.kids.size() == 2 && e.kids[1]) {
            // delete[] p: a class with a destructor goes through __vdel_ (count in front of the
            // block); anything else is just freed.
            ET a = decay(checkExpr(*e.kids[1]));
            if (a.bad || !a.type) return bad(e.line, e.col);
            const Type* cls = isPtr(*a.type) && a.type->inner ? canonOf(a.type->inner.get()) : nullptr;
            const Param* d1 = cls && cls->kind == Type::Kind::Struct ? findSpecial(cls, "<dtor>") : nullptr;
            if (d1) c0.text = "__vdel_" + mangleNested(cls->tag);
            else c0.text = "\x01" "delete";
            return checkCall(e);
        } else if (c0.kind == Expr::Kind::Name && c0.text == "\x01" "delete" && e.kids.size() == 2 && e.kids[1]) {
            ET a = decay(checkExpr(*e.kids[1]));
            if (a.bad || !a.type) return bad(e.line, e.col);
            const Type* cls = isPtr(*a.type) && a.type->inner ? canonOf(a.type->inner.get()) : nullptr;
            const Param* d1 = cls && cls->kind == Type::Kind::Struct ? findSpecial(cls, "<dtor>") : nullptr;
            auto freeCall = [&](const Expr& at, std::unique_ptr<Expr> arg) {
                auto cast = mkE(Expr::Kind::Cast, at);
                cast->ty = ptrTo(prim(Type::Kind::Char));
                cast->kids.push_back(std::move(arg));
                auto call = mkE(Expr::Kind::Call, at);
                call->kids.push_back(mkName("free", at));
                call->kids.push_back(std::move(cast));
                return call;
            };
            if (!d1) {
                c0.text = "free";
                auto cast = mkE(Expr::Kind::Cast, e);
                cast->ty = ptrTo(prim(Type::Kind::Char));
                cast->kids.push_back(std::move(e.kids[1]));
                e.kids[1] = std::move(cast);
            } else {
                if (hasSideEffects(*e.kids[1])) {
                    err(e.line, e.col, "delete of an expression with side effects is not supported");
                    return bad(e.line, e.col);
                }
                auto self = [&]() { return cloneExpr(*e.kids[1]); };
                auto work = mkE(Expr::Kind::Comma, e);
                if (d1->vslot >= 0) {
                    const Param* d0 = findSpecial(cls, "<dtor0>");
                    auto call = mkE(Expr::Kind::Call, e);
                    call->kids.push_back(vcallee(e, self(), d0->vslot, a.type->inner->tag, *d0->type));
                    call->kids.push_back(self());
                    work->kids.push_back(std::move(call));
                } else {
                    auto call = mkE(Expr::Kind::Call, e);
                    call->kids.push_back(mkName(d1->mangled, e));
                    call->kids.push_back(self());
                    work->kids.push_back(std::move(call));
                    work->kids.push_back(freeCall(e, self()));
                }
                auto zero = mkE(Expr::Kind::Int, e);
                work->kids.push_back(std::move(zero));
                auto cond = mkE(Expr::Kind::Cond, e);
                cond->kids.push_back(self());
                cond->kids.push_back(std::move(work));
                cond->kids.push_back(mkE(Expr::Kind::Int, e));
                e.kind = Expr::Kind::Cond;
                e.text.clear();
                e.kids = std::move(cond->kids);
                return checkExprBody(e);
            }
        } else if (c0.kind == Expr::Kind::Name && method_ && c0.text.find("::") == std::string::npos) {
            const Type* st = canonOf(method_);
            bool isMethod = false;
            if (st)
                for (const Param& f : st->params)
                    if (f.name == c0.text && !f.mangled.empty() && !isSpecialName(f.name)) isMethod = true;
            if (isMethod) {
                std::string fname = c0.text;
                c0.kind = Expr::Kind::Member;
                c0.text = "->";
                c0.kids.clear();
                c0.kids.push_back(mkName("this", c0));
                c0.kids.push_back(mkName(fname, c0));
            }
        } else if (c0.kind == Expr::Kind::Name && method_ && c0.text.find("::") != std::string::npos) {
            std::size_t cc = c0.text.rfind("::");
            std::string prefix = c0.text.substr(0, cc);
            std::string fname = c0.text.substr(cc + 2);
            auto it = canonStructs().find(prefix);
            if (it == canonStructs().end() && !inNs_.empty()) it = canonStructs().find(inNs_ + "::" + prefix);
            if (it != canonStructs().end() && it->second) {
                std::vector<const Param*> cands;
                for (const Param& f : it->second->params)
                    if (f.name == fname && !f.mangled.empty() && !isSpecialName(f.name)) cands.push_back(&f);
                if (!cands.empty()) {
                    const Param* pick = pickOverload(cands, e, 1);
                    if (pick->type) addDefaults(e, *pick->type, 0, it->second->tag);
                    c0.text = pick->mangled;
                    e.kids.insert(e.kids.begin() + 1, mkName("this", e));
                }
            }
        }
        if (e.kids[0]->kind == Expr::Kind::Member && e.kids[0]->kids.size() >= 2 && e.kids[0]->kids[0] &&
            e.kids[0]->kids[1]) {
            Expr& mem = *e.kids[0];
            ET obj = mem.text == "->" ? decay(checkExpr(*mem.kids[0])) : checkExpr(*mem.kids[0]);
            const Type* st = obj.type.get();
            if (mem.text == "->" && st && isPtr(*st) && st->inner) st = st->inner.get();
            st = canonOf(st);
            if (st) {
                std::vector<const Param*> cands;
                for (const Param& f : st->params)
                    if (f.name == mem.kids[1]->text && !f.mangled.empty() && !isSpecialName(f.name))
                        cands.push_back(&f);
                if (!cands.empty()) {
                    const Param* fp = pickOverload(cands, e, 1);
                    const Param& f = *fp;
                    if (f.type) addDefaults(e, *f.type, 0, st->tag);
                    std::string target = f.mangled;
                    if (target.compare(0, 7, "\x01" "mtmpl:") == 0) {
                        target = memberInstance(target.substr(7), e);
                        if (target.empty()) return bad(e.line, e.col);
                    }
                    std::unique_ptr<Expr> selfArg;
                    if (mem.text == "->") {
                        selfArg = std::move(mem.kids[0]);
                    } else if (!obj.lvalue && mem.kids[0]->kind == Expr::Kind::Comma && mem.kids[0]->ival == 9 &&
                               !mem.kids[0]->text.empty()) {
                        // T(args).f(): the temporary already has a name and lives to the end of the
                        // full expression ([class.temporary]/4); call on it, do not copy its bytes.
                        std::string tname = mem.kids[0]->text;
                        selfArg = eCommaNode(std::move(mem.kids[0]), eUnary("&", mkName(tname, e), e), e);
                    } else if (!obj.lvalue && obj.type && obj.type->kind == Type::Kind::Struct && !obj.bad) {
                        // A member call on an object that is a result (f().g()): the object goes into
                        // a hidden local and the call is made on that.
                        std::string tmp = newTemp(*obj.type, e, true);
                        auto assign = withFlag(eAssignNode(mkName(tmp, e), std::move(mem.kids[0]), e), e);
                        selfArg = eCommaNode(std::move(assign), eUnary("&", mkName(tmp, e), e), e);
                    } else {
                        selfArg = std::make_unique<Expr>();
                        selfArg->kind = Expr::Kind::Unary;
                        selfArg->text = "&";
                        selfArg->line = mem.line;
                        selfArg->col = mem.col;
                        selfArg->kids.push_back(std::move(mem.kids[0]));
                    }
                    std::unique_ptr<Expr> callee;
                    if (f.vslot >= 0) {
                        if (hasSideEffects(*selfArg)) {
                            // p->f() where p is a call (a smart pointer's operator->): the object's
                            // address is computed once into a hidden pointer. The callee is lowered
                            // before the arguments, so the vtable load sets it and 'this' reads it.
                            auto cls = prim(Type::Kind::Struct);
                            cls->tag = st->tag;
                            std::string tp = newTemp(*ptrTo(std::move(cls)), e, false);
                            auto set = eCommaNode(eAssignNode(mkName(tp, e), std::move(selfArg), e), mkName(tp, e), e);
                            callee = vcallee(e, std::move(set), f.vslot, st->tag, *f.type);
                            selfArg = mkName(tp, e);
                        } else {
                            callee = vcallee(e, cloneExpr(*selfArg), f.vslot, st->tag, *f.type);
                        }
                    } else {
                        callee = std::make_unique<Expr>();
                        callee->kind = Expr::Kind::Name;
                        callee->text = target;
                        callee->line = e.line;
                        callee->col = e.col;
                    }
                    std::vector<std::unique_ptr<Expr>> kids;
                    kids.push_back(std::move(callee));
                    kids.push_back(std::move(selfArg));
                    for (std::size_t i = 1; i < e.kids.size(); ++i) kids.push_back(std::move(e.kids[i]));
                    e.kids = std::move(kids);
                }
            }
        }
        resolveOverload(e);
        ET callee = decay(checkExpr(*e.kids[0]));
        if (callee.bad || !callee.type) return bad(e.line, e.col);
        if (!isPtr(*callee.type) || !callee.type->inner || callee.type->inner->kind != Type::Kind::Function) {
            err(e.line, e.col, "called expression is not a function");
            return bad(e.line, e.col);
        }
        const Type& fn = *callee.type->inner;
        if (!fn.variadic && e.kids.size() - 1 < fn.params.size()) addDefaults(e, fn, 0);
        for (std::size_t i = 0; i < fn.params.size() && i + 1 < e.kids.size(); ++i) {
            Expr* a = e.kids[i + 1].get();
            const Type* pt = fn.params[i].type.get();
            if (pt && pt->kind == Type::Kind::Reference && pt->inner) pt = pt->inner.get();
            if (a && a->kind == Expr::Kind::Brace && pt && pt->kind == Type::Kind::Struct) braceToTemp(*a, *pt);
        }
        std::size_t nargs = e.kids.size() - 1;
        if (fn.variadic) {
            if (nargs < fn.params.size()) {
                err(e.line, e.col, "too few arguments");
                return bad(e.line, e.col);
            }
        } else if (nargs != fn.params.size()) {
            err(e.line, e.col, "wrong number of arguments");
            return bad(e.line, e.col);
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (fn.params[i].type && fn.params[i].type->kind == Type::Kind::Reference && fn.params[i].type->inner) {
                // A reference parameter receives the address of an lvalue of the referred type.
                if (e.kids[i + 1] && e.kids[i + 1]->ival == 1 &&
                    (e.kids[i + 1]->kind == Expr::Kind::Unary || e.kids[i + 1]->kind == Expr::Kind::Comma)) {
                    // Already wrapped by an earlier check of this call.
                    if (checkExpr(*e.kids[i + 1]).bad) return bad(e.line, e.col);
                    continue;
                }
                ET src = checkExpr(*e.kids[i + 1]);
                if (src.bad) return bad(e.line, e.col);
                const Type& want = *fn.params[i].type->inner;
                if (src.type && !sameType(want, *src.type) && !derivedFrom(*src.type, want) &&
                    convertingTemp(*e.kids[i + 1], *src.type, want)) {
                    // f(const std::string &) called with "text": a temporary made by the converting
                    // constructor, bound to the reference ([over.ics.user], [class.conv.ctor]).
                    src = checkExpr(*e.kids[i + 1]);
                    if (src.bad) return bad(e.line, e.col);
                }
                if (src.type && want.kind != Type::Kind::Struct && want.kind != Type::Kind::Union &&
                    want.kind != Type::Kind::Array && src.type->kind != Type::Kind::Struct &&
                    (want.cnst || fn.params[i].type->rref) && !sameType(want, *src.type) && canAssign(want, src)) {
                    // A const T& (or T&&) of a number or pointer given a value of another type
                    // (std::min<std::uint64_t>(u32, u64)): the value is converted into a temporary
                    // of type T and the reference binds to it ([dcl.init.ref] p5.4).
                    auto wt = cloneType(want);
                    wt->cnst = false;
                    std::string tmp = newTemp(*wt, e, false);
                    auto first = eAssignNode(mkName(tmp, e), std::move(e.kids[i + 1]), e);
                    auto both = eCommaNode(std::move(first), eUnary("&", mkName(tmp, e), e), e);
                    both->ival = 1;
                    e.kids[i + 1] = std::move(both);
                    if (checkExpr(*e.kids[i + 1]).bad) return bad(e.line, e.col);
                    continue;
                }
                if (!src.lvalue) {
                    // A reference to const may bind to a temporary: copy the value into a hidden
                    // local and pass its address.
                    if (!src.type || !sameType(want, *src.type)) {
                        err(e.line, e.col, "a reference argument must be an lvalue");
                        return bad(e.line, e.col);
                    }
                    std::unique_ptr<Expr> first;
                    std::unique_ptr<Expr> addr;
                    if (e.kids[i + 1]->kind == Expr::Kind::Comma && e.kids[i + 1]->ival == 9) {
                        // A T(args) temporary already has a name and a destructor call: bind to it.
                        std::string existing = e.kids[i + 1]->text;
                        first = std::move(e.kids[i + 1]);
                        addr = eUnary("&", mkName(existing, e), e);
                    } else {
                        std::string tmp = newTemp(*src.type, e, true);
                        first = withFlag(eAssignNode(mkName(tmp, e), std::move(e.kids[i + 1]), e), e);
                        addr = eUnary("&", mkName(tmp, e), e);
                    }
                    auto both = eCommaNode(std::move(first), std::move(addr), e);
                    both->ival = 1;
                    e.kids[i + 1] = std::move(both);
                    if (checkExpr(*e.kids[i + 1]).bad) return bad(e.line, e.col);
                    continue;
                }
                if (src.type && !sameType(want, *src.type) && !derivedFrom(*src.type, want)) {
                    err(e.line, e.col, "cannot bind this reference argument");
                    return bad(e.line, e.col);
                }
                auto wrapped = std::make_unique<Expr>();
                wrapped->kind = Expr::Kind::Unary;
                wrapped->text = "&";
                wrapped->ival = 1;
                wrapped->line = e.kids[i + 1]->line;
                wrapped->col = e.kids[i + 1]->col;
                wrapped->file = e.kids[i + 1]->file;
                wrapped->kids.push_back(std::move(e.kids[i + 1]));
                e.kids[i + 1] = std::move(wrapped);
                ET ptr = checkExpr(*e.kids[i + 1]);
                if (ptr.bad) return bad(e.line, e.col);
                continue;
            }
            if (fn.params[i].type && fn.params[i].type->kind != Type::Kind::Struct) convertIfNeeded(*e.kids[i + 1]);
            ET arg = decay(checkExpr(*e.kids[i + 1]));
            if (arg.bad) return bad(e.line, e.col);
            if (fn.params[i].type && fn.params[i].type->kind == Type::Kind::Struct && arg.type &&
                sameType(*fn.params[i].type, *arg.type)) {
                Expr& argExpr = *e.kids[i + 1];
                if (argExpr.kind == Expr::Kind::Comma && argExpr.ival == 9) {
                    // A T(args) temporary passed by value initializes the parameter directly.
                    consumed(argExpr);
                } else if (arg.lvalue && (hasUserCopy(*arg.type) || needsDtor(*arg.type))) {
                    // The caller copies the object into a hidden local (by the copy constructor, or
                    // bytewise) and destroys that copy after the statement.
                    std::string tmp = newTemp(*arg.type, e, true);
                    std::unique_ptr<Expr> made;
                    if (hasUserCopy(*arg.type)) {
                        auto ctorName = mkName("\x01" "ctor:" + arg.type->tag, e);
                        std::vector<std::unique_ptr<Expr>> kids;
                        kids.push_back(std::move(ctorName));
                        kids.push_back(eUnary("&", mkName(tmp, e), e));
                        kids.push_back(std::move(e.kids[i + 1]));
                        auto call = mkE(Expr::Kind::Call, e);
                        call->kids = std::move(kids);
                        made = std::move(call);
                    } else {
                        made = eAssignNode(mkName(tmp, e), std::move(e.kids[i + 1]), e);
                    }
                    made = withFlag(std::move(made), e);
                    e.kids[i + 1] = eCommaNode(std::move(made), mkName(tmp, e), e);
                    arg = decay(checkExpr(*e.kids[i + 1]));
                    if (arg.bad) return bad(e.line, e.col);
                }
            }
            if (fn.params[i].type && !canAssign(*fn.params[i].type, arg)) {
                err(e.line, e.col, "cannot assign argument");
                return bad(e.line, e.col);
            }
        }
        for (std::size_t i = fn.params.size(); i < nargs; ++i) {
            ET arg = decay(checkExpr(*e.kids[i + 1]));
            if (arg.bad) return bad(e.line, e.col);
            if (arg.type && (arg.type->kind == Type::Kind::Char || arg.type->kind == Type::Kind::Short)) {
                // The default argument promotion to int ([expr.call] p12). The node keeps its
                // signedness: an unsigned char is loaded zero-extended (168 stays 168).
                auto promoted = prim(Type::Kind::Int);
                promoted->unsign = arg.type->unsign;
                e.kids[i + 1]->checked = std::move(promoted);
            }
        }
        ET r;
        r.line = e.line;
        r.col = e.col;
        r.type = fn.inner ? cloneType(*fn.inner) : prim(Type::Kind::Int);
        if (r.type && r.type->kind == Type::Kind::Reference && r.type->inner && e.ival == 2) {
            auto ptrTy = std::make_unique<Type>();
            ptrTy->kind = Type::Kind::Pointer;
            ptrTy->inner = cloneType(*r.type->inner);
            r.type = std::move(ptrTy);
        } else if (r.type && r.type->kind == Type::Kind::Reference && r.type->inner) {
            // A call that returns a reference is the pointer it returns, dereferenced: the call
            // moves into a child and this node becomes "*call" (ival 5 when it returns T&&).
            const bool xv = r.type->rref;
            auto ptrTy = std::make_unique<Type>();
            ptrTy->kind = Type::Kind::Pointer;
            ptrTy->inner = cloneType(*r.type->inner);
            auto inner = std::make_unique<Expr>();
            inner->kind = e.kind;
            inner->text = e.text;
            inner->ival = 2;
            inner->line = e.line;
            inner->col = e.col;
            inner->file = e.file;
            inner->kids = std::move(e.kids);
            inner->checked = std::move(ptrTy);
            e.kids.clear();
            e.kind = Expr::Kind::Unary;
            e.text = "*";
            e.ival = xv ? 5 : 0;
            e.kids.push_back(std::move(inner));
            r.type = cloneType(*r.type->inner);
            r.lvalue = true;
            r.xvalue = xv;
            r.mod = true;
        }
        return r;
    }

    // Brace elision ([dcl.init.aggr] p15, ISO C 6.7.9 p20): in "std::array<int, 3> a = {1, 2, 3}" the
    // list for the class is flat, but its one member is an array that takes all three values. The
    // list is rebuilt with the braces written out, so the lowering sees {{1, 2, 3}}. An element that
    // is an aggregate (an array, or a class with no constructor) takes as many of the following
    // values as it needs unless the value is a braced list or an object of the element's own class.
    const Type* aggregateOf(const Type* t) {
        if (!t) return nullptr;
        if (t->kind == Type::Kind::Array) return t;
        if (t->kind != Type::Kind::Struct) return nullptr;
        const Type* c = canonOf(t);
        if (!c || c->needCtor || c->hasVptr || !c->base.empty()) return nullptr;
        return c;
    }
    std::vector<const Type*> aggElems(const Type* agg, std::size_t& count) {
        std::vector<const Type*> out;
        count = 0;
        if (agg->kind == Type::Kind::Array) {
            out.push_back(agg->inner.get());
            if (agg->bound && agg->bound->kind == Expr::Kind::Int) count = agg->bound->ival;
            else count = static_cast<std::size_t>(-1);  // T a[] = {...}: every value
            return out;
        }
        for (const Param& f : agg->params)
            if (f.mangled.empty() && f.type && f.type->kind != Type::Kind::Function && !isSpecialName(f.name) &&
                f.bits < 0)
                out.push_back(f.type.get());
        count = out.size();
        return out;
    }
    std::unique_ptr<Expr> elide(const Type* t, std::vector<std::unique_ptr<Expr>>& items, std::size_t& pos,
                                const Expr& at) {
        auto out = mkE(Expr::Kind::Brace, at);
        const Type* agg = aggregateOf(t);
        if (!agg) return out;
        std::size_t count = 0;
        std::vector<const Type*> elems = aggElems(agg, count);
        for (std::size_t k = 0; k < count && pos < items.size(); ++k) {
            const Type* et = agg->kind == Type::Kind::Array ? elems[0] : elems[k];
            std::unique_ptr<Expr>& it = items[pos];
            const Type* sub = aggregateOf(et);
            if (it && it->kind == Expr::Kind::Brace) {
                normalizeBrace(et, *it);
                out->kids.push_back(std::move(it));
                ++pos;
                continue;
            }
            bool direct = !sub || !it;
            if (!direct && sub->kind == Type::Kind::Array && it->kind == Expr::Kind::String) direct = true;
            if (!direct && sub->kind == Type::Kind::Struct) {
                ++quiet_;
                ET probe = checkExpr(*it);
                --quiet_;
                const Type* pt = probe.type.get();
                if (pt && pt->kind == Type::Kind::Reference && pt->inner) pt = pt->inner.get();
                if (probe.bad || (pt && pt->kind == Type::Kind::Struct && canonOf(pt) == sub)) direct = true;
            }
            if (direct) {
                out->kids.push_back(std::move(it));
                ++pos;
            } else {
                out->kids.push_back(elide(et, items, pos, at));
            }
        }
        return out;
    }
    void normalizeBrace(const Type* t, Expr& b) {
        if (!aggregateOf(t)) return;
        std::vector<std::unique_ptr<Expr>> items = std::move(b.kids);
        std::size_t pos = 0;
        auto rebuilt = elide(t, items, pos, b);
        b.kids = std::move(rebuilt->kids);
        for (; pos < items.size(); ++pos) b.kids.push_back(std::move(items[pos]));  // too many: reported later
    }

    void checkBraceKids(Expr& b) {
        for (auto& k : b.kids) {
            if (!k) continue;
            if (k->kind == Expr::Kind::Brace) checkBraceKids(*k);
            else checkExpr(*k);
        }
    }

    std::map<std::string, std::uint64_t> rangeBound_;

    // ---- Overloaded functions ([over.load], [over.match]) ----
    // The first function of a name keeps it. A later one with other parameter types is an overload:
    // it gets the internal name name\x04k and the Itanium symbol, and a call picks among the set.
    std::map<std::string, std::vector<Param>> overloads_;
    void addFunction(Decl& d) {
        if (d.methodTag.empty()) {
            std::size_t cut = d.name.rfind("::");
            std::string shortN = cut == std::string::npos ? d.name : d.name.substr(cut + 2);
            if (shortN.compare(0, 8, "operator") == 0) freeOps_.insert(shortN);
        }
        auto& sc = scopes_.front();
        auto it = sc.find(d.name);
        bool cFile = d.file.size() >= 2 && d.file.compare(d.file.size() - 2, 2, ".c") == 0;
        bool plain = it == sc.end() || !it->second.func || !it->second.type || !d.methodTag.empty() ||
                     d.name.compare(0, 2, "_Z") == 0 || cFile;
        if (!overloads_.count(d.name) && (plain || sameType(*it->second.type, *d.type))) {
            add(d.name, cloneType(*d.type), true, d.line, d.col);  // C: a conflicting type stays an error
            return;
        }
        std::string base = d.name;
        auto& set = overloads_[base];
        if (set.empty()) {
            Param first;
            first.name = base;
            first.mangled = base;
            first.type = cloneType(*it->second.type);
            set.push_back(std::move(first));
        }
        for (const Param& p : set)
            if (p.type && sameType(*p.type, *d.type)) {
                d.name = p.mangled;  // another declaration (or the definition) of one overload
                if (p.mangled != base) d.asmName = mangleFunc(base, *d.type);
                add(d.name, cloneType(*d.type), true, d.line, d.col);
                return;
            }
        Param o;
        o.name = base;
        o.mangled = base + "\x04" + std::to_string(set.size());
        o.type = cloneType(*d.type);
        set.push_back(std::move(o));
        d.name = set.back().mangled;
        d.asmName = mangleFunc(base, *d.type);
        add(d.name, cloneType(*d.type), true, d.line, d.col);
    }
    // A call by name of an overloaded function: choose by the arguments.
    void resolveOverload(Expr& e) {
        if (overloads_.empty() || e.kids.empty() || !e.kids[0] || e.kids[0]->kind != Expr::Kind::Name) return;
        std::string name = e.kids[0]->text;
        auto it = overloads_.find(name);
        if (it == overloads_.end() && name.find("::") == std::string::npos && scopeOf(name) <= 0) {
            std::string scope = inNs_;
            while (!scope.empty() && it == overloads_.end()) {
                it = overloads_.find(scope + "::" + name);
                std::size_t cut = scope.rfind("::");
                if (cut == std::string::npos) break;
                scope = scope.substr(0, cut);
            }
        }
        if (it == overloads_.end() && tu_ && name.find("::") == std::string::npos && scopeOf(name) < 0) {
            auto ud = tu_->usingDecl.find(name);
            if (ud != tu_->usingDecl.end()) it = overloads_.find(ud->second);
            for (std::size_t k = 0; it == overloads_.end() && k < tu_->usingNs.size(); ++k)
                it = overloads_.find(tu_->usingNs[k] + "::" + name);
        }
        if (it == overloads_.end()) return;
        if (scopeOf(name) > 0) return;  // a local of that name hides the functions
        std::vector<const Param*> cands;
        for (const Param& p : it->second) cands.push_back(&p);
        const Param* pick = pickOverload(cands, e, 1);
        if (pick) e.kids[0]->text = pick->mangled;
    }

    // {a, b, ...} as a std::initializer_list<E> (our <initializer_list>: fields b_ and n_): a hidden
    // array E[n] set from the elements and a hidden list pointing at it. Both live to the end of the
    // full expression's statement block, like the temporaries of [dcl.init.list]/6.
    std::unique_ptr<Expr> initList(Expr& br, const Type& il) {
        auto it = canonStructs().find(il.tag);
        const Type* elem = nullptr;
        if (it != canonStructs().end() && it->second)
            for (const Param& f : it->second->params)
                if (f.name == "b_" && f.type && f.type->inner) elem = f.type->inner.get();
        if (!elem) {
            err(br.line, br.col, "std::initializer_list without its element type");
            return nullptr;
        }
        auto E = cloneType(*elem);
        E->cnst = false;
        std::size_t n = br.kids.size();
        auto arrT = prim(Type::Kind::Array);
        arrT->inner = cloneType(*E);
        arrT->bound = mkE(Expr::Kind::Int, br);
        arrT->bound->ival = n == 0 ? 1 : n;
        std::string arr = newTemp(*arrT, br, false);
        std::string lst = newTemp(il, br, false);
        std::unique_ptr<Expr> seq;
        auto add = [&](std::unique_ptr<Expr> x) { seq = seq ? eCommaNode(std::move(seq), std::move(x), br) : std::move(x); };
        for (std::size_t i = 0; i < n; ++i) {
            auto at = mkE(Expr::Kind::Index, br);
            at->kids.push_back(mkName(arr, br));
            auto ix = mkE(Expr::Kind::Int, br);
            ix->ival = i;
            at->kids.push_back(std::move(ix));
            std::unique_ptr<Expr> v = std::move(br.kids[i]);
            if (v && v->kind == Expr::Kind::Brace && E->kind == Type::Kind::Struct) braceToTemp(*v, *E);
            if (E->kind == Type::Kind::Struct && !E->tag.empty()) {
                auto call = mkE(Expr::Kind::Call, br);
                call->kids.push_back(mkName("\x01" "cinit:" + E->tag, br));
                call->kids.push_back(eUnary("&", std::move(at), br));
                call->kids.push_back(std::move(v));
                add(std::move(call));
            } else {
                add(eAssignNode(std::move(at), std::move(v), br));
            }
        }
        auto fld = [&](const char* f) {
            auto m = mkE(Expr::Kind::Member, br);
            m->text = ".";
            m->kids.push_back(mkName(lst, br));
            m->kids.push_back(mkName(f, br));
            return m;
        };
        auto cnt = mkE(Expr::Kind::Int, br);
        cnt->ival = n;
        add(eAssignNode(fld("b_"), mkName(arr, br), br));
        add(eAssignNode(fld("n_"), std::move(cnt), br));
        return eCommaNode(std::move(seq), mkName(lst, br), br);
    }

    // ---- Lambdas (see LambdaInfo in ast.hpp; [expr.prim.lambda]) ----
    Tu* tu_ = nullptr;
    Decl* curFn_ = nullptr;      // the function being checked (its return type may be deduced)
    std::size_t fnBase_ = 0;     // index in scopes_ of that function's outermost scope
    struct LamCtx {
        LambdaInfo* li;
        std::size_t fnBase;   // the enclosing function's outermost scope
        std::size_t lamBase;  // the lambda's own outermost scope
        std::vector<std::pair<std::string, int>> used;  // captures made: name, 0 copy 1 reference 2 array
    };
    std::vector<LamCtx> lamStack_;
    std::vector<std::unique_ptr<Decl>> lambdaDecls_;
    std::map<std::string, std::string> closureFn_;  // closure class tag -> its function

    int scopeOf(const std::string& name) const {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i)
            if (scopes_[static_cast<std::size_t>(i)].count(name)) return i;
        return -1;
    }

    // A name used inside a lambda that is a local of the enclosing function: a capture. By
    // reference it becomes *__self->name (the closure holds a pointer), by copy __self->name.
    bool captureName(Expr& e) {
        if (lamStack_.empty() || e.text.empty() || e.text.find("::") != std::string::npos) return false;
        LamCtx& lc = lamStack_.back();
        int at = scopeOf(e.text);
        // A local of the function around the outermost lambda, of an enclosing lambda, or of the
        // function this lambda is in. A capture from further out is taken by this lambda, and its
        // closure is filled in the enclosing lambda, which then captures the name itself.
        const std::size_t root = lamStack_.front().fnBase;
        if (at < static_cast<int>(root) || at >= static_cast<int>(lc.lamBase)) return false;
        const Sym& sym = scopes_[static_cast<std::size_t>(at)].at(e.text);
        if (sym.func || !sym.type) return false;
        int how = -1;
        for (const auto& c : lc.li->caps)
            if (c.first == e.text) how = c.second ? 1 : 0;
        if (how < 0 && lc.li->mode == 1) how = 1;
        if (how < 0 && lc.li->mode == 2) how = 0;
        if (how < 0) {
            err(e.line, e.col, "'" + e.text + "' is not captured by the lambda");
            return false;
        }
        const Type* referent = sym.type.get();
        if (referent->kind == Type::Kind::Reference && referent->inner) referent = referent->inner.get();
        if (how == 0 && needsDtor(*referent)) {
            err(e.line, e.col, "capturing a class with a destructor by copy is not supported");
            return false;
        }
        // An array is held as a pointer to its first element (indexing it is the same); a copy
        // of an array is not made (it is taken by reference: see the plan).
        if (referent->kind == Type::Kind::Array && referent->inner) how = 2;
        auto cit = canonStructs().find(lc.li->tag);
        bool have = false;
        for (const auto& u : lc.used)
            if (u.first == e.text) have = true;
        if (!have && cit != canonStructs().end() && cit->second) {
            Param f;
            f.name = e.text;
            if (how == 2) f.type = ptrTo(cloneType(*referent->inner));
            else f.type = how == 1 ? ptrTo(cloneType(*referent)) : cloneType(*referent);
            cit->second->params.push_back(std::move(f));
            lc.used.emplace_back(e.text, how);
        }
        std::string name = e.text;
        auto mem = mkE(Expr::Kind::Member, e);
        mem->text = "->";
        mem->kids.push_back(mkName("__self", e));
        mem->kids.push_back(mkName(name, e));
        e.kids.clear();
        if (how == 1) {
            e.kind = Expr::Kind::Unary;
            e.text = "*";
            e.kids.push_back(std::move(mem));
        } else {
            e.kind = Expr::Kind::Member;
            e.text = "->";
            e.kids = std::move(mem->kids);
        }
        return true;
    }

    // The lambda expression: check its body now, inside the enclosing function, then make the
    // closure object: a hidden local whose fields are set from the captured variables.
    ET checkLambda(Expr& e) {
        std::size_t idx = static_cast<std::size_t>(std::stoul(e.text.substr(8)));
        if (!tu_ || idx >= tu_->lambdas.size() || !tu_->lambdas[idx].fn) {
            err(e.line, e.col, "bad lambda");
            return bad(e.line, e.col);
        }
        LambdaInfo& li = tu_->lambdas[idx];
        auto cls = prim(Type::Kind::Struct);
        cls->tag = li.tag;
        {
            Param pad;  // an empty closure still has size 1, as in g++
            pad.name = "__pad";
            pad.type = prim(Type::Kind::Char);
            cls->params.push_back(std::move(pad));
        }
        canonStructs()[li.tag] = cloneType(*cls);
        Decl& d = *li.fn;
        std::vector<TempInfo> savedTemps = std::move(temps_);
        temps_.clear();
        const Type* savedRet = ret_;
        const Type* savedMethod = method_;
        Decl* savedFn = curFn_;
        std::size_t savedBase = fnBase_;
        int savedLoops = loops_, savedSwitches = switches_;
        lamStack_.push_back(LamCtx{&li, fnBase_, scopes_.size(), {}});
        scopes_.front()[d.name].type = cloneType(*d.type);
        scopes_.front()[d.name].func = true;
        scopes_.emplace_back();
        fnBase_ = scopes_.size() - 1;
        for (const Param& p : d.type->params)
            if (!p.name.empty() && p.type) add(p.name, cloneType(*p.type), false, d.line, d.col);
        ret_ = d.type->inner.get();
        curFn_ = &d;
        {
            // A lambda in a member function that captures this ([this], [&] or [=], used members
            // implicitly) reads the members through the captured this ([expr.prim.lambda.capture]/7).
            bool self = li.mode != 0;
            for (const auto& c : li.caps)
                if (c.first == "this") self = true;
            if (!self) method_ = nullptr;
        }
        loops_ = 0;
        switches_ = 0;
        if (d.body) checkStmts(*d.body);
        if (isAuto(d.type->inner.get())) d.type->inner = prim(Type::Kind::Void);
        scopes_.front()[d.name].type = cloneType(*d.type);
        scopes_.pop_back();
        std::vector<std::pair<std::string, int>> used = std::move(lamStack_.back().used);
        lamStack_.pop_back();
        ret_ = savedRet;
        method_ = savedMethod;
        curFn_ = savedFn;
        fnBase_ = savedBase;
        loops_ = savedLoops;
        switches_ = savedSwitches;
        temps_ = std::move(savedTemps);
        closureFn_[li.tag] = d.name;
        lambdaDecls_.push_back(std::move(li.fn));
        auto full = cloneType(*canonStructs()[li.tag]);
        std::string tmp = newTemp(*full, e, false);
        std::unique_ptr<Expr> seq;
        for (const auto& u : used) {
            auto field = mkE(Expr::Kind::Member, e);
            field->text = ".";
            field->kids.push_back(mkName(tmp, e));
            field->kids.push_back(mkName(u.first, e));
            auto val = u.second == 1 ? eUnary("&", mkName(u.first, e), e) : mkName(u.first, e);
            auto as = eAssignNode(std::move(field), std::move(val), e);
            seq = seq ? eCommaNode(std::move(seq), std::move(as), e) : std::move(as);
        }
        e.kids.clear();
        if (!seq) {
            e.kind = Expr::Kind::Name;
            e.text = tmp;
            return checkExprBody(e);
        }
        e.kind = Expr::Kind::Comma;
        e.text.clear();
        e.ival = 0;
        e.kids.push_back(std::move(seq));
        e.kids.push_back(mkName(tmp, e));
        return checkExprBody(e);
    }  // hidden range-for pointers to an array: its length
    static bool isAuto(const Type* t) { return t && t->kind == Type::Kind::Struct && t->tag == "\x03" "auto"; }

    // auto x = e; auto &x = e; ([dcl.spec.auto]): the type of e, without references, arrays decayed.
    // A hidden range-for variable bound to a value that is not an lvalue holds it by value.
    bool deduceAuto(Decl& d) {
        bool ref = d.type->kind == Type::Kind::Reference && isAuto(d.type->inner.get());
        bool ptr = d.type->kind == Type::Kind::Pointer && isAuto(d.type->inner.get());  // auto *p = e;
        if (!ref && !ptr && !isAuto(d.type.get())) return false;
        if (!d.init) {
            err(d.line, d.col, "'auto' needs an initializer");
            d.type = prim(Type::Kind::Int);
            return true;
        }
        ET src = checkExpr(*d.init);
        if (src.bad || !src.type) {
            d.type = prim(Type::Kind::Int);
            add(d.name, cloneType(*d.type), false, d.line, d.col);
            return true;
        }
        bool hidden = d.name.compare(0, 4, "__rg") == 0;
        if (hidden && src.type->kind == Type::Kind::Array && src.type->inner) {
            // The range is an array: hold a pointer to its first element and remember the length.
            rangeBound_[d.name] = src.type->bound && src.type->bound->kind == Expr::Kind::Int ? src.type->bound->ival : 0;
            d.type = ptrTo(cloneType(*src.type->inner));
            add(d.name, cloneType(*d.type), false, d.line, d.col);
            return true;
        }
        if (ref && src.lvalue) {
            auto r = prim(Type::Kind::Reference);
            r->inner = cloneType(*src.type);
            d.type = std::move(r);
        } else {
            ET v = decay(std::move(src));
            if (!hidden && needsDtor(*v.type)) err(d.line, d.col, "'auto' for a class with a destructor is not supported");
            d.type = cloneType(*v.type);
        }
        add(d.name, cloneType(*d.type), false, d.line, d.col);
        return true;
    }

    // An integer constant expression made of literals, casts and operators.
    static bool evalInt(const Expr& e, std::int64_t& out) {
        if (e.kind == Expr::Kind::Int || e.kind == Expr::Kind::Char) {
            out = static_cast<std::int64_t>(e.ival);
            return true;
        }
        if (e.kind == Expr::Kind::Cast && !e.kids.empty() && e.kids[0]) return evalInt(*e.kids[0], out);
        if (e.kind == Expr::Kind::Unary && e.text == "-" && !e.kids.empty() && e.kids[0]) {
            if (!evalInt(*e.kids[0], out)) return false;
            out = -out;
            return true;
        }
        if (e.kind == Expr::Kind::Bin && e.kids.size() == 2 && e.kids[0] && e.kids[1]) {
            std::int64_t a = 0, b = 0;
            if (!evalInt(*e.kids[0], a) || !evalInt(*e.kids[1], b)) return false;
            if (e.text == "+") out = a + b;
            else if (e.text == "-") out = a - b;
            else if (e.text == "*") out = a * b;
            else if (e.text == "/" && b != 0) out = a / b;
            else if (e.text == "%" && b != 0) out = a % b;
            else if (e.text == "<<" && b >= 0 && b < 63) out = a << b;
            else if (e.text == ">>" && b >= 0 && b < 63) out = a >> b;
            else return false;
            return true;
        }
        return false;
    }
    // T a[N] with N a named constant (a static constexpr member, a const global): the length.
    void foldBound(Type& t) {
        if (t.kind != Type::Kind::Array || !t.bound) return;
        if (t.inner) foldBound(*t.inner);
        if (t.bound->kind == Expr::Kind::Int) return;
        ++quiet_;
        checkExpr(*t.bound);
        --quiet_;
        foldConstNames(*t.bound);
        std::int64_t n = 0;
        if (!evalInt(*t.bound, n) || n <= 0) return;
        auto lit = mkE(Expr::Kind::Int, *t.bound);
        lit->ival = static_cast<std::uint64_t>(n);
        t.bound = std::move(lit);
    }

    void checkVar(Decl& d) {
        if (!d.type) return;
        foldBound(*d.type);
        // A namespace-scope or static member variable N::C::x: its initializer looks up names in
        // N::C, then N ([basic.lookup.unqual]/13).
        std::string savedNs = inNs_;
        struct RestoreNs {
            std::string& ns;
            std::string saved;
            ~RestoreNs() { ns = saved; }
        } restoreNs{inNs_, savedNs};
        if (scopes_.size() == 1 && d.name.find("::") != std::string::npos) inNs_ = d.name.substr(0, d.name.rfind("::"));
        if (!d.vt.empty()) return;  // a vtable: already declared
        if (deduceAuto(d)) return;
        if (d.type->kind == Type::Kind::Reference) {
            if (!d.init || !d.type->inner) {
                err(d.line, d.col, "a reference must be initialized");
            } else {
                ET src = checkExpr(*d.init);
                if (!src.bad && !src.lvalue) err(d.line, d.col, "a reference must bind to an lvalue");
                if (!src.bad && src.type && !sameType(*d.type->inner, *src.type) && !derivedFrom(*src.type, *d.type->inner))
                    err(d.line, d.col, "cannot bind this reference");
            }
            add(d.name, cloneType(*d.type), false, d.line, d.col);
            return;
        }
        if (d.type->kind == Type::Kind::Void ||
            (d.type->kind == Type::Kind::Array && d.type->inner && d.type->inner->kind == Type::Kind::Void)) {
            err(d.line, d.col, "variable has type void");
        }
        if (d.type->kind == Type::Kind::Array && d.type->unsized && d.type->inner && d.init &&
            ((d.init->kind == Expr::Kind::String && d.type->inner->kind == Type::Kind::Char) ||
             d.init->kind == Expr::Kind::Brace)) {
            // T a[] = {..} or char a[] = "..": the length is the initializer's ([dcl.array] p3), so
            // sizeof(a) is right where it is checked.
            auto b = mkE(Expr::Kind::Int, *d.init);
            b->ival = d.init->kind == Expr::Kind::String ? d.init->text.size() + 1 : d.init->kids.size();
            if (b->ival > 0) {
                d.type->bound = std::move(b);
                d.type->unsized = false;
            }
        }
        if (d.init && d.init->kind == Expr::Kind::Brace) {
            normalizeBrace(d.type.get(), *d.init);
            checkBraceKids(*d.init);
        } else if (d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Array) {
            checkExpr(*d.init);
        } else if (d.init) {
            if (d.type->kind != Type::Kind::Struct) convertIfNeeded(*d.init);
            ET src = decay(checkExpr(*d.init));
            if (d.init->kind == Expr::Kind::Comma && d.init->ival == 9) {
                consumed(*d.init);  // T x = T(args): built in place, the temporary is not destroyed
            }
            if (!src.bad && scopes_.size() == 1 && !isConst(*d.init) && foldConstNames(*d.init)) {
                // N::b = N::a / 2 with a a const global of constant value: put the value in.
                src = decay(checkExpr(*d.init));
            }
            if (!src.bad && !canAssign(*d.type, src)) err(d.line, d.col, "cannot assign this value");
            else if (!src.bad && scopes_.size() == 1 && !isConst(*d.init))
                err(d.line, d.col, "initializer is not a constant");
            if (!src.bad && scopes_.size() == 1 && isConst(*d.init) && isScalarConst(*d.type) &&
                (d.type->cnst || d.name.find("::") != std::string::npos))
                constInits_[d.name] = cloneExpr(*d.init);
        }
        add(d.name, cloneType(*d.type), false, d.line, d.col);
    }

    void checkFunction(Decl& d) {
        if (!d.type) return;
        add(d.name, cloneType(*d.type), true, d.line, d.col);
        if (!d.body) return;
        scopes_.emplace_back();
        const Type* saved = ret_;
        const Type* savedMethod = method_;
        std::string savedNs = inNs_;
        Decl* savedFn = curFn_;
        std::size_t savedBase = fnBase_;
        curFn_ = &d;
        fnBase_ = scopes_.size() - 1;
        ret_ = d.type->inner.get();
        method_ = nullptr;
        inNs_.clear();
        {
            std::size_t cut = d.name.rfind("::");
            if (cut != std::string::npos) inNs_ = d.name.substr(0, cut);
        }
        // Inside a member function, names are looked up in the class, then in the namespaces
        // around it ([basic.lookup.unqual]): the class tag is the scope.
        if (!d.methodTag.empty()) inNs_ = d.methodTag;
        if (!d.methodTag.empty() && !d.staticMember) {
            auto it = canonStructs().find(d.methodTag);
            if (it != canonStructs().end()) method_ = it->second.get();
        }
        for (const Param& p : d.type->params) {
            if (p.name.empty() || !p.type) continue;
            add(p.name, cloneType(*p.type), false, d.line, d.col);
        }
        if (d.body->kind == Stmt::Kind::Block) {
            checkStmts(*d.body);
        } else {
            checkStmt(*d.body);
        }
        if (isAuto(d.type->inner.get())) d.type->inner = prim(Type::Kind::Void);
        ret_ = saved;
        method_ = savedMethod;
        inNs_ = savedNs;
        curFn_ = savedFn;
        fnBase_ = savedBase;
        scopes_.pop_back();
    }

    void checkCond(Expr& e) {
        convertIfNeeded(e);
        ET c = decay(checkExpr(e));
        if (!c.bad && c.type && !scalar(*c.type)) err(e.line, e.col, "condition is not a scalar");
    }

    // A single statement that is the body of if, while or for becomes a block, so that hidden
    // temporaries made inside it are destroyed inside it.
    static void blockify(std::unique_ptr<Stmt>& body) {
        if (!body || body->kind == Stmt::Kind::Block) return;
        auto blk = std::make_unique<Stmt>();
        blk->kind = Stmt::Kind::Block;
        blk->line = body->line;
        blk->col = body->col;
        blk->file = body->file;
        blk->stmts.push_back(std::move(body));
        body = std::move(blk);
    }

    // Declarations of the hidden locals and their flags (the flag starts at 0).
    void declareTemps(std::vector<std::unique_ptr<Stmt>>& out, std::vector<TempInfo>& temps) {
        for (TempInfo& t : temps) {
            if (t.type) {
                auto d = std::make_unique<Decl>();
                d->name = t.name;
                d->line = t.pos.line;
                d->col = t.pos.col;
                d->file = t.pos.file;
                d->type = std::move(t.type);
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = d->line;
                ds->col = d->col;
                ds->decl = std::move(d);
                out.push_back(std::move(ds));
            }
            if (!t.rec.empty()) {
                auto d = std::make_unique<Decl>();
                d->name = t.rec;
                d->line = t.pos.line;
                d->col = t.pos.col;
                d->file = t.pos.file;
                auto arr = std::make_unique<Type>();
                arr->kind = Type::Kind::Array;
                arr->inner = prim(Type::Kind::Long);
                arr->bound = std::make_unique<Expr>();
                arr->bound->kind = Expr::Kind::Int;
                arr->bound->ival = 14;
                d->type = std::move(arr);
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = d->line;
                ds->col = d->col;
                ds->decl = std::move(d);
                out.push_back(std::move(ds));
            }
            if (!t.flag.empty()) {
                auto d = std::make_unique<Decl>();
                d->name = t.flag;
                d->line = t.pos.line;
                d->col = t.pos.col;
                d->file = t.pos.file;
                d->type = std::make_unique<Type>();
                d->init = std::make_unique<Expr>();
                d->init->kind = Expr::Kind::Int;
                d->init->ival = 0;
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = d->line;
                ds->col = d->col;
                ds->decl = std::move(d);
                out.push_back(std::move(ds));
            }
        }
    }
    // "if (flag) dtor(&tmp);" for each temporary that has a destructor, latest first.
    std::vector<std::unique_ptr<Stmt>> dtorStmts(std::vector<TempInfo>& temps, int line, int col,
                                                 const std::string& file) {
        std::vector<std::unique_ptr<Stmt>> dtors;
        for (std::size_t i = temps.size(); i-- > 0;) {
            if (temps[i].tag.empty()) continue;
            auto it = canonStructs().find(temps[i].tag);
            const Param* dp = it == canonStructs().end() ? nullptr : findSpecial(it->second.get(), "<dtor>");
            if (!dp) continue;
            Expr at;
            at.line = line;
            at.col = col;
            at.file = file;
            auto call = mkE(Expr::Kind::Call, at);
            call->kids.push_back(mkName(dp->mangled, at));
            call->kids.push_back(eUnary("&", mkName(temps[i].name, at), at));
            auto es = std::make_unique<Stmt>();
            es->kind = Stmt::Kind::Expr;
            es->line = line;
            es->col = col;
            es->file = file;
            es->expr = std::move(call);
            std::unique_ptr<Stmt> last = std::move(es);
            if (!temps[i].rec.empty()) {
                auto blk2 = std::make_unique<Stmt>();
                blk2->kind = Stmt::Kind::Block;
                blk2->line = line;
                blk2->col = col;
                blk2->file = file;
                blk2->stmts.push_back(std::move(last));
                auto popCall = mkE(Expr::Kind::Call, at);
                popCall->kids.push_back(mkName("__okay_eh_pop", at));
                auto ps = std::make_unique<Stmt>();
                ps->kind = Stmt::Kind::Expr;
                ps->line = line;
                ps->col = col;
                ps->file = file;
                ps->expr = std::move(popCall);
                blk2->stmts.push_back(std::move(ps));
                last = std::move(blk2);
            }
            if (!temps[i].flag.empty()) {
                auto g = std::make_unique<Stmt>();
                g->kind = Stmt::Kind::If;
                g->line = line;
                g->col = col;
                g->file = file;
                g->expr = mkName(temps[i].flag, at);
                g->thenS = std::move(last);
                last = std::move(g);
            }
            checkStmt(*last);
            dtors.push_back(std::move(last));
        }
        return dtors;
    }

    // Puts the hidden locals' declarations before 'st' and the destructor calls after it (before
    // a return, with the returned value kept in another hidden local).
    void flushTemps(std::vector<std::unique_ptr<Stmt>>& out, std::unique_ptr<Stmt> st, std::vector<TempInfo>& temps) {
        declareTemps(out, temps);
        std::vector<std::unique_ptr<Stmt>> dtors = dtorStmts(temps, st->line, st->col, st->file);
        if (dtors.empty()) {
            out.push_back(std::move(st));
            return;
        }
        if (st->kind == Stmt::Kind::Return && st->expr && ret_ && ret_->kind != Type::Kind::Void &&
            ret_->kind != Type::Kind::Reference) {
            // return e; the value is computed, then the temporaries die, then the function returns.
            std::string rv = "__crv" + std::to_string(tempN_++);
            auto d = std::make_unique<Decl>();
            d->name = rv;
            d->line = st->line;
            d->col = st->col;
            d->file = st->file;
            d->type = cloneType(*ret_);
            d->init = std::move(st->expr);
            add(rv, cloneType(*ret_), false, st->line, st->col);
            auto ds = std::make_unique<Stmt>();
            ds->kind = Stmt::Kind::Decl;
            ds->line = st->line;
            ds->col = st->col;
            ds->decl = std::move(d);
            out.push_back(std::move(ds));
            for (auto& dt : dtors) out.push_back(std::move(dt));
            Expr at;
            at.line = st->line;
            at.col = st->col;
            at.file = st->file;
            st->expr = mkName(rv, at);
            st->expr->checked = cloneType(*ret_);
            out.push_back(std::move(st));
            return;
        }
        out.push_back(std::move(st));
        for (auto& dt : dtors) out.push_back(std::move(dt));
    }

    // The statements of a block, in the scope the caller opened.
    void checkStmts(Stmt& block) {
        std::vector<std::unique_ptr<Stmt>> out;
        for (auto& st : block.stmts) {
            if (!st) continue;
            std::vector<TempInfo> outer = std::move(temps_);
            temps_.clear();
            checkStmt(*st);
            std::vector<TempInfo> mine = std::move(temps_);
            temps_ = std::move(outer);
            if (mine.empty()) {
                out.push_back(std::move(st));
                continue;
            }
            flushTemps(out, std::move(st), mine);
        }
        block.stmts = std::move(out);
    }

    void checkStmt(Stmt& s) {
        if (!s.file.empty()) file_ = s.file;
        switch (s.kind) {
        case Stmt::Kind::Block: {
            scopes_.emplace_back();
            checkStmts(s);
            scopes_.pop_back();
            break;
        }
        case Stmt::Kind::Empty:
            break;
        case Stmt::Kind::Expr:
            if (s.expr) checkExpr(*s.expr);
            break;
        case Stmt::Kind::Return:
            if (!ret_) {
                err(s.line, s.col, "return outside a function");
                break;
            }
            if (isAuto(ret_) && curFn_ && curFn_->type) {
                // A deduced return type ([dcl.spec.auto]): the type of the first return.
                if (!s.expr) {
                    curFn_->type->inner = prim(Type::Kind::Void);
                } else {
                    ET v = decay(checkExpr(*s.expr));
                    curFn_->type->inner = v.type ? cloneType(*v.type) : prim(Type::Kind::Int);
                }
                ret_ = curFn_->type->inner.get();
                if (scopes_.front().count(curFn_->name))
                    scopes_.front()[curFn_->name].type = cloneType(*curFn_->type);
                break;
            }
            if (ret_->kind == Type::Kind::Void) {
                if (s.expr) err(s.line, s.col, "return type does not match");
            } else if (!s.expr) {
                err(s.line, s.col, "return type does not match");
            } else if (ret_->kind == Type::Kind::Reference && ret_->inner) {
                ET v = checkExpr(*s.expr);
                if (!v.bad && (!v.lvalue || !v.type ||
                               (!sameType(*ret_->inner, *v.type) && !derivedFrom(*v.type, *ret_->inner))))
                    err(s.line, s.col, "return type does not match");
            } else {
                if (ret_->kind != Type::Kind::Struct) convertIfNeeded(*s.expr);
                ET v = decay(checkExpr(*s.expr));
                if (!v.bad && v.type && ret_->kind == Type::Kind::Struct && !canAssign(*ret_, v) &&
                    convertingTemp(*s.expr, *v.type, *ret_)) {
                    // return d; for a class with a converting constructor ([stmt.return] p2, copy-initialization).
                    v = decay(checkExpr(*s.expr));
                }
                if (s.expr->kind == Expr::Kind::Comma && s.expr->ival == 9) {
                    consumed(*s.expr);  // returned: it is the result object
                }
                if (!v.bad && !canAssign(*ret_, v)) err(s.line, s.col, "return type does not match");
            }
            break;
        case Stmt::Kind::If: {
            blockify(s.thenS);
            blockify(s.elseS);
            std::vector<TempInfo> outer = std::move(temps_);
            temps_.clear();
            if (s.expr) checkCond(*s.expr);
            std::vector<TempInfo> condTemps = std::move(temps_);
            temps_ = std::move(outer);
            if (s.thenS) checkStmt(*s.thenS);
            if (s.elseS) checkStmt(*s.elseS);
            if (!condTemps.empty() && s.expr) {
                // The condition's temporaries die before either branch runs:
                // { decls; int c = cond; dtors; if (c) then else }.
                Expr at;
                at.line = s.line;
                at.col = s.col;
                at.file = s.file;
                auto inner = std::make_unique<Stmt>();
                inner->kind = Stmt::Kind::If;
                inner->line = s.line;
                inner->col = s.col;
                inner->file = s.file;
                std::string cv = "__cc" + std::to_string(tempN_++);
                inner->expr = mkName(cv, at);
                inner->expr->checked = prim(Type::Kind::Long);
                inner->thenS = std::move(s.thenS);
                inner->elseS = std::move(s.elseS);
                std::vector<std::unique_ptr<Stmt>> parts;
                declareTemps(parts, condTemps);
                auto d = std::make_unique<Decl>();
                d->name = cv;
                d->line = s.line;
                d->col = s.col;
                d->file = s.file;
                d->type = prim(Type::Kind::Long);  // wide enough for a pointer condition
                d->init = std::move(s.expr);
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = s.line;
                ds->col = s.col;
                ds->decl = std::move(d);
                parts.push_back(std::move(ds));
                for (auto& dt : dtorStmts(condTemps, s.line, s.col, s.file)) parts.push_back(std::move(dt));
                parts.push_back(std::move(inner));
                s.kind = Stmt::Kind::Block;
                s.stmts = std::move(parts);
                s.expr.reset();
            }
            break;
        }
        case Stmt::Kind::While: {
            blockify(s.body);
            std::vector<TempInfo> outer = std::move(temps_);
            temps_.clear();
            if (s.expr) checkCond(*s.expr);
            std::vector<TempInfo> condTemps = std::move(temps_);
            temps_ = std::move(outer);
            ++loops_;
            if (s.body) checkStmt(*s.body);
            --loops_;
            if (!condTemps.empty() && s.expr && s.body) {
                // Temporaries of the condition live for one test: while (1) { decls; if (!cond) {
                // dtors; break; } dtors; body }.
                auto blk = std::make_unique<Stmt>();
                blk->kind = Stmt::Kind::Block;
                blk->line = s.line;
                blk->col = s.col;
                blk->file = s.file;
                declareTemps(blk->stmts, condTemps);
                Expr at;
                at.line = s.line;
                at.col = s.col;
                at.file = s.file;
                auto notc = eUnary("!", std::move(s.expr), at);
                notc->checked = prim(Type::Kind::Int);
                auto stop = std::make_unique<Stmt>();
                stop->kind = Stmt::Kind::If;
                stop->line = s.line;
                stop->col = s.col;
                stop->file = s.file;
                stop->expr = std::move(notc);
                auto exitBlk = std::make_unique<Stmt>();
                exitBlk->kind = Stmt::Kind::Block;
                exitBlk->line = s.line;
                exitBlk->file = s.file;
                for (auto& d : dtorStmts(condTemps, s.line, s.col, s.file)) exitBlk->stmts.push_back(std::move(d));
                auto brk = std::make_unique<Stmt>();
                brk->kind = Stmt::Kind::Break;
                brk->line = s.line;
                brk->file = s.file;
                exitBlk->stmts.push_back(std::move(brk));
                stop->thenS = std::move(exitBlk);
                blk->stmts.push_back(std::move(stop));
                for (auto& d : dtorStmts(condTemps, s.line, s.col, s.file)) blk->stmts.push_back(std::move(d));
                blk->stmts.push_back(std::move(s.body));
                s.body = std::move(blk);
                s.expr = mkE(Expr::Kind::Int, at);
                s.expr->ival = 1;
                s.expr->checked = prim(Type::Kind::Int);
            }
            break;
        }
        case Stmt::Kind::For: {
            blockify(s.body);
            scopes_.emplace_back();
            if (s.init) checkStmt(*s.init);
            std::vector<TempInfo> outer = std::move(temps_);
            temps_.clear();
            if (s.expr) checkCond(*s.expr);
            std::vector<TempInfo> condTemps = std::move(temps_);
            temps_ = std::move(outer);
            if (s.expr2) checkExpr(*s.expr2);
            ++loops_;
            if (s.body) checkStmt(*s.body);
            --loops_;
            if (!condTemps.empty() && s.expr && s.body) {
                // The condition's temporaries live for one test:
                // for (init; ; step) { decls; c = cond; dtors; if (!c) break; body }.
                Expr at;
                at.line = s.line;
                at.col = s.col;
                at.file = s.file;
                auto blk = std::make_unique<Stmt>();
                blk->kind = Stmt::Kind::Block;
                blk->line = s.line;
                blk->col = s.col;
                blk->file = s.file;
                declareTemps(blk->stmts, condTemps);
                std::string cv = "__cc" + std::to_string(tempN_++);
                auto d = std::make_unique<Decl>();
                d->name = cv;
                d->line = s.line;
                d->col = s.col;
                d->file = s.file;
                d->type = prim(Type::Kind::Long);
                d->init = std::move(s.expr);
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = s.line;
                ds->col = s.col;
                ds->decl = std::move(d);
                blk->stmts.push_back(std::move(ds));
                for (auto& dt : dtorStmts(condTemps, s.line, s.col, s.file)) blk->stmts.push_back(std::move(dt));
                auto notc = eUnary("!", mkName(cv, at), at);
                notc->checked = prim(Type::Kind::Int);
                notc->kids[0]->checked = prim(Type::Kind::Long);
                auto stop = std::make_unique<Stmt>();
                stop->kind = Stmt::Kind::If;
                stop->line = s.line;
                stop->col = s.col;
                stop->file = s.file;
                stop->expr = std::move(notc);
                auto brk = std::make_unique<Stmt>();
                brk->kind = Stmt::Kind::Break;
                brk->line = s.line;
                brk->file = s.file;
                stop->thenS = std::move(brk);
                blk->stmts.push_back(std::move(stop));
                blk->stmts.push_back(std::move(s.body));
                s.body = std::move(blk);
            }
            scopes_.pop_back();
            break;
        }
        case Stmt::Kind::Break:
            if (loops_ == 0 && switches_ == 0) err(s.line, s.col, "break is not inside a loop or switch");
            break;
        case Stmt::Kind::Switch: {
            if (!s.expr) break;
            ET c = decay(checkExpr(*s.expr));
            if (!c.bad && (!c.type || !isIntish(*c.type))) err(s.line, s.col, "switch value is not an integer");
            ++switches_;
            bool sawDef = false;
            for (auto& cs : s.stmts) {
                if (!cs || cs->kind != Stmt::Kind::Case) continue;
                if (!cs->expr) {
                    if (sawDef) err(cs->line, cs->col, "more than one default");
                    sawDef = true;
                } else {
                    ET v = checkExpr(*cs->expr);
                    if (!v.bad && (!v.type || !isIntish(*v.type))) err(cs->line, cs->col, "case value is not an integer");
                }
                if (cs->body) checkStmt(*cs->body);
            }
            --switches_;
            break;
        }
        case Stmt::Kind::Continue:
            if (loops_ == 0) err(s.line, s.col, "continue is not inside a loop");
            break;
        case Stmt::Kind::Decl:
            if (s.decl) checkVar(*s.decl);
            break;
        case Stmt::Kind::Case:
        case Stmt::Kind::Goto:
        case Stmt::Kind::Label:
            break;
        }
    }
};

void typecheckTu(Tu& tu, Diagnostics& diags) {
    Checker checker(diags);
    checker.check(tu);
}


}  // namespace compiler
}  // namespace okaycc
