// Dump(): the syntax tree as small S-expressions, for tests and debugging. Written for this project.
#include "okayscript/Ast.hpp"
#include "okayscript/Environment.hpp"
#include "okayscript/Value.hpp"

namespace okayscript {

namespace {
// An operator as written: TokName gives it in quotes ("'+'"); the tree prints it bare.
std::string OpText(Tok t) {
    const std::string s = TokName(t);
    return s.size() > 2 && s.front() == '\'' ? s.substr(1, s.size() - 2) : std::string("?");
}
std::string Opt(const ExprPtr& e) { return e ? e->Dump() : std::string("nil"); }
std::string Quote(const std::string& s) { return "\"" + s + "\""; }
std::string ArgList(const std::vector<ExprPtr>& a) {
    std::string s;
    for (auto& e : a) s += " " + e->Dump();
    return s;
}
} // namespace

std::string DumpList(const StmtList& list) {
    std::string s = "(block";
    for (auto& st : list) s += " " + st->Dump();
    return s + ")";
}

std::string NumberExpr::Dump() const { return isInt ? std::to_string(iv) + "L" : Value::FormatNumber((float)v); }
std::string StringExpr::Dump() const { return Quote(v); }
std::string BoolExpr::Dump() const { return v ? "true" : "false"; }
std::string InterpStringExpr::Dump() const {
    std::string s = "(interp";
    for (auto& g : segs) s += " " + (g.isExpr ? g.expr->Dump() : Quote(g.lit));
    return s + ")";
}
std::string VarExpr::Dump() const { return n; }
std::string AssignExpr::Dump() const { return "(= " + n + " " + value->Dump() + ")"; }
std::string UnaryExpr::Dump() const { return std::string("(") + (op == Tok::Minus ? std::string("neg") : OpText(op)) + " " + e->Dump() + ")"; }
std::string LogicalExpr::Dump() const { return std::string("(") + OpText(op) + " " + l->Dump() + " " + r->Dump() + ")"; }
std::string CoalesceExpr::Dump() const { return "(?? " + l->Dump() + " " + r->Dump() + ")"; }
std::string NullishAssignExpr::Dump() const { return "(?\?= " + n + " " + value->Dump() + ")"; }
std::string BinaryExpr::Dump() const { return std::string("(") + OpText(op) + " " + l->Dump() + " " + r->Dump() + ")"; }
std::string MemberExpr::Dump() const { return std::string(safe ? "(?. " : "(. ") + obj->Dump() + " " + name + ")"; }
std::string MemberAssignExpr::Dump() const { return "(=. " + obj->Dump() + " " + name + " " + value->Dump() + ")"; }
std::string CastExpr::Dump() const { return "(cast " + type + " " + e->Dump() + ")"; }
std::string MethodCallExpr::Dump() const { return "(method " + obj->Dump() + " " + name + ArgList(args) + ")"; }
std::string CallExpr::Dump() const {
    std::string s = "(call " + name;
    for (std::size_t i = 0; i < args.size(); ++i) {
        s += " ";
        if (i < argNames.size() && !argNames[i].empty()) s += argNames[i] + ":";
        s += args[i]->Dump();
    }
    return s + ")";
}
std::string TernaryExpr::Dump() const { return "(? " + cond->Dump() + " " + a->Dump() + " " + b->Dump() + ")"; }
std::string ArrayExpr::Dump() const { return "(list" + ArgList(elems) + ")"; }
std::string MapExpr::Dump() const {
    std::string s = "(map";
    for (std::size_t i = 0; i < keys.size(); ++i) s += " " + Quote(keys[i]) + ":" + values[i]->Dump();
    return s + ")";
}
std::string IndexExpr::Dump() const { return "([] " + arr->Dump() + " " + index->Dump() + ")"; }
std::string IndexAssignExpr::Dump() const { return "([]= " + arr->Dump() + " " + index->Dump() + " " + value->Dump() + ")"; }
std::string IndexCompoundAssignExpr::Dump() const {
    return std::string("([]") + OpText(op) + "= " + arr->Dump() + " " + index->Dump() + " " + value->Dump() + ")";
}
std::string LambdaExpr::Dump() const {
    std::string s = "(lambda (";
    for (std::size_t i = 0; i < params.size(); ++i) s += (i ? " " : "") + params[i];
    return s + ") " + DumpList(body) + ")";
}

std::string NewExpr::Dump() const { return "(new " + type + ArgList(args) + ")"; }
std::string EventOrCompoundExpr::Dump() const {
    return std::string("(") + (remove ? "-=" : "+=") + " " + name + " " + rhs->Dump() + ")";
}
std::string YieldStmt::Dump() const { return std::string(generator ? "(gen-yield " : "(yield ") + Opt(e) + ")"; }

std::string ExprStmt::Dump() const { return e->Dump(); }
std::string VarDeclStmt::Dump() const {
    return std::string("(var") + ((kind & 0x0F) ? std::string(" ") + KindName(kind) : "") + ((kind & KConst) ? " const" : "") +
           " " + n + " " + Opt(init) + ")";
}
std::string ReturnStmt::Dump() const { return "(return " + Opt(e) + ")"; }
std::string BreakStmt::Dump() const { return "(break)"; }
std::string ContinueStmt::Dump() const { return "(continue)"; }
std::string ThrowStmt::Dump() const { return "(throw " + Opt(e) + ")"; }
std::string TryStmt::Dump() const {
    return "(try " + DumpList(body) + (hasCatch ? " (catch " + (catchVar.empty() ? std::string("_") : catchVar) + " " + DumpList(catchB) + ")" : "") +
           (hasFinally ? " (finally " + DumpList(finallyB) + ")" : "") + ")";
}
std::string DestructureStmt::Dump() const {
    std::string s = std::string("(") + (declare ? "var" : "set") + (isMap ? " {" : " [");
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) s += " ";
        s += (isMap && keys[i] != names[i]) ? keys[i] + ":" + names[i] : (names[i].empty() ? std::string("_") : names[i]);
    }
    return s + (isMap ? "} " : "] ") + init->Dump() + ")";
}
std::string ImportStmt::Dump() const { return "(import " + Quote(path) + " " + alias + ")"; }
std::string BlockStmt::Dump() const { return DumpList(body); }
std::string IfStmt::Dump() const { return "(if " + cond->Dump() + " " + DumpList(thenB) + (elseB.empty() ? "" : " " + DumpList(elseB)) + ")"; }
std::string WhileStmt::Dump() const { return "(while " + cond->Dump() + " " + DumpList(body) + ")"; }
std::string DoWhileStmt::Dump() const { return "(do " + DumpList(body) + " " + cond->Dump() + ")"; }
std::string SwitchStmt::Dump() const {
    std::string s = "(switch " + subject->Dump();
    for (auto& c : cases) s += " (" + (c.isDefault ? std::string("default") : "case " + c.match->Dump()) + " " + DumpList(c.body) + ")";
    return s + ")";
}
std::string ForStmt::Dump() const {
    return "(for " + (init ? init->Dump() : std::string("nil")) + " " + Opt(cond) + " " + Opt(step) + " " + DumpList(body) + ")";
}
std::string ForEachStmt::Dump() const { return "(foreach " + var + " " + iterable->Dump() + " " + DumpList(body) + ")"; }
std::string MultiStmt::Dump() const {
    std::string s = "(multi";
    for (auto& st : list) s += " " + st->Dump();
    return s + ")";
}

// ---- Local names -------------------------------------------------------------------------------------------------
namespace {
struct LocalNameWalker {
    std::unordered_set<std::string>& out;
    std::vector<std::string>* order = nullptr;            ///< CollectBodyLocals: first-seen order
    std::unordered_set<std::string>* statics = nullptr;   ///< CollectBodyLocals: names declared `static`
    bool lambdas = true;                                   ///< look into expressions for lambdas
    void Add(const std::string& n) { if (out.insert(n).second && order) order->push_back(n); }
    void Fn(const FunctionDecl& f) {
        for (auto& p : f.params) Add(p);
        for (auto& d : f.defaults) if (d) E(*d);
        List(f.body, true);
    }
    void List(const StmtList& l, bool local) { for (auto& s : l) S(*s, local); }
    void S(const Stmt& st, bool local) {
        switch (st.kind) {
            case StmtKind::Expr: E(*static_cast<const ExprStmt&>(st).e); break;
            case StmtKind::VarDecl: {
                auto& s = static_cast<const VarDeclStmt&>(st);
                if (local) {
                    if ((s.kind & KStatic) && statics) statics->insert(s.n);
                    else Add(s.n);
                }
                if (s.init) E(*s.init);
                break;
            }
            case StmtKind::Return: { auto& s = static_cast<const ReturnStmt&>(st); if (s.e) E(*s.e); break; }
            case StmtKind::Throw: { auto& s = static_cast<const ThrowStmt&>(st); if (s.e) E(*s.e); break; }
            case StmtKind::Yield: { auto& s = static_cast<const YieldStmt&>(st); if (s.e) E(*s.e); break; }
            case StmtKind::Try: {
                auto& s = static_cast<const TryStmt&>(st);
                if (local && !s.catchVar.empty()) Add(s.catchVar);
                List(s.body, local); List(s.catchB, local); List(s.finallyB, local);
                break;
            }
            case StmtKind::Destructure: {
                auto& s = static_cast<const DestructureStmt&>(st);
                if (local && s.declare) for (auto& n : s.names) if (!n.empty()) Add(n);
                E(*s.init);
                break;
            }
            case StmtKind::Import: {
                auto& s = static_cast<const ImportStmt&>(st);
                if (local) Add(s.alias);
                break;
            }
            case StmtKind::Block: List(static_cast<const BlockStmt&>(st).body, local); break;
            case StmtKind::If: { auto& s = static_cast<const IfStmt&>(st); E(*s.cond); List(s.thenB, local); List(s.elseB, local); break; }
            case StmtKind::While: { auto& s = static_cast<const WhileStmt&>(st); E(*s.cond); List(s.body, local); break; }
            case StmtKind::DoWhile: { auto& s = static_cast<const DoWhileStmt&>(st); E(*s.cond); List(s.body, local); break; }
            case StmtKind::Switch: {
                auto& s = static_cast<const SwitchStmt&>(st);
                E(*s.subject);
                for (auto& c : s.cases) { if (c.match) E(*c.match); List(c.body, local); }
                break;
            }
            case StmtKind::For: {
                auto& s = static_cast<const ForStmt&>(st);
                if (s.init) S(*s.init, local);
                if (s.cond) E(*s.cond);
                if (s.step) E(*s.step);
                List(s.body, local);
                break;
            }
            case StmtKind::ForEach: {
                auto& s = static_cast<const ForEachStmt&>(st);
                if (local) Add(s.var);
                E(*s.iterable); List(s.body, local);
                break;
            }
            case StmtKind::Multi: List(static_cast<const MultiStmt&>(st).list, local); break;
            case StmtKind::Break: case StmtKind::Continue: break;
        }
    }
    void Args(const std::vector<ExprPtr>& a) { for (auto& e : a) E(*e); }
    void E(const Expr& ex) {
        if (!lambdas) return;
        switch (ex.kind) {
            case ExprKind::Number: case ExprKind::String: case ExprKind::Bool: case ExprKind::Var: break;
            case ExprKind::InterpString:
                for (auto& g : static_cast<const InterpStringExpr&>(ex).segs) if (g.isExpr) E(*g.expr);
                break;
            case ExprKind::Assign: E(*static_cast<const AssignExpr&>(ex).value); break;
            case ExprKind::Unary: E(*static_cast<const UnaryExpr&>(ex).e); break;
            case ExprKind::Logical: { auto& e = static_cast<const LogicalExpr&>(ex); E(*e.l); E(*e.r); break; }
            case ExprKind::Coalesce: { auto& e = static_cast<const CoalesceExpr&>(ex); E(*e.l); E(*e.r); break; }
            case ExprKind::NullishAssign: E(*static_cast<const NullishAssignExpr&>(ex).value); break;
            case ExprKind::Binary: { auto& e = static_cast<const BinaryExpr&>(ex); E(*e.l); E(*e.r); break; }
            case ExprKind::Member: E(*static_cast<const MemberExpr&>(ex).obj); break;
            case ExprKind::MemberAssign: { auto& e = static_cast<const MemberAssignExpr&>(ex); E(*e.obj); E(*e.value); break; }
            case ExprKind::Cast: E(*static_cast<const CastExpr&>(ex).e); break;
            case ExprKind::MethodCall: { auto& e = static_cast<const MethodCallExpr&>(ex); E(*e.obj); Args(e.args); break; }
            case ExprKind::Call: Args(static_cast<const CallExpr&>(ex).args); break;
            case ExprKind::Ternary: { auto& e = static_cast<const TernaryExpr&>(ex); E(*e.cond); E(*e.a); E(*e.b); break; }
            case ExprKind::Array: Args(static_cast<const ArrayExpr&>(ex).elems); break;
            case ExprKind::Map: Args(static_cast<const MapExpr&>(ex).values); break;
            case ExprKind::Index: { auto& e = static_cast<const IndexExpr&>(ex); E(*e.arr); E(*e.index); break; }
            case ExprKind::IndexAssign: { auto& e = static_cast<const IndexAssignExpr&>(ex); E(*e.arr); E(*e.index); E(*e.value); break; }
            case ExprKind::IndexCompoundAssign: {
                auto& e = static_cast<const IndexCompoundAssignExpr&>(ex);
                E(*e.arr); E(*e.index); E(*e.value);
                break;
            }
            case ExprKind::Lambda: {   // a lambda's parameters and declarations are locals of the lambda's own scope
                auto& e = static_cast<const LambdaExpr&>(ex);
                for (auto& p : e.params) Add(p);
                for (auto& d : e.defaults) if (d) E(*d);
                List(e.body, true);
                break;
            }
            case ExprKind::New: Args(static_cast<const NewExpr&>(ex).args); break;
            case ExprKind::EventOrCompound: E(*static_cast<const EventOrCompoundExpr&>(ex).rhs); break;
        }
    }
};
} // namespace

void CollectBodyLocals(const StmtList& body, std::vector<std::string>& names, std::unordered_set<std::string>& statics) {
    std::unordered_set<std::string> seen(names.begin(), names.end());
    LocalNameWalker w{seen};
    w.order = &names;
    w.statics = &statics;
    w.lambdas = false;
    w.List(body, true);
}

void CollectLocalNames(Program& prog) {
    LocalNameWalker w{prog.localNames};
    w.List(prog.top, false);   // top-level declarations are globals
    for (auto& kv : prog.functions) w.Fn(kv.second);
    for (auto& kv : prog.classes) {
        for (auto& f : kv.second.fields) if (f.init) w.E(*f.init);
        for (auto& f : kv.second.staticFields) if (f.init) w.E(*f.init);
        for (auto& m : kv.second.methods) w.Fn(m.second.decl);
        for (auto& m : kv.second.staticMethods) w.Fn(m.second.decl);
        for (auto& c : kv.second.ctors) w.Fn(c.second.decl);
    }
}

// ---- Closures: which locals a lambda inside a body uses ----------------------------------------------------------
namespace {
/// The name a use refers to: the variable before the first '.', without a "this." in front of it.
std::string BaseName(const std::string& n) {
    if (n.compare(0, 5, "this.") == 0) return std::string();
    const auto dot = n.find('.');
    return dot == std::string::npos ? n : n.substr(0, dot);
}
/// Every variable name a piece of code reads or writes (`uses`), and the free names of the lambdas in it (`inLambdas`:
/// what each uses that it does not declare itself, nested lambdas included).
struct UseWalker {
    std::unordered_set<std::string>* uses = nullptr;
    std::unordered_set<std::string>* inLambdas = nullptr;
    void Use(const std::string& n) {
        const std::string b = BaseName(n);
        if (!b.empty() && uses) uses->insert(b);
    }
    void List(const StmtList& l) { for (auto& s : l) S(*s); }
    void S(const Stmt& st) {
        switch (st.kind) {
            case StmtKind::Expr: E(*static_cast<const ExprStmt&>(st).e); break;
            case StmtKind::VarDecl: { auto& s = static_cast<const VarDeclStmt&>(st); Use(s.n); if (s.init) E(*s.init); break; }
            case StmtKind::Return: { auto& s = static_cast<const ReturnStmt&>(st); if (s.e) E(*s.e); break; }
            case StmtKind::Throw: { auto& s = static_cast<const ThrowStmt&>(st); if (s.e) E(*s.e); break; }
            case StmtKind::Yield: { auto& s = static_cast<const YieldStmt&>(st); if (s.e) E(*s.e); break; }
            case StmtKind::Try: {
                auto& s = static_cast<const TryStmt&>(st);
                if (!s.catchVar.empty()) Use(s.catchVar);
                List(s.body); List(s.catchB); List(s.finallyB);
                break;
            }
            case StmtKind::Block: List(static_cast<const BlockStmt&>(st).body); break;
            case StmtKind::If: { auto& s = static_cast<const IfStmt&>(st); E(*s.cond); List(s.thenB); List(s.elseB); break; }
            case StmtKind::While: { auto& s = static_cast<const WhileStmt&>(st); E(*s.cond); List(s.body); break; }
            case StmtKind::DoWhile: { auto& s = static_cast<const DoWhileStmt&>(st); E(*s.cond); List(s.body); break; }
            case StmtKind::Switch: {
                auto& s = static_cast<const SwitchStmt&>(st);
                E(*s.subject);
                for (auto& c : s.cases) { if (c.match) E(*c.match); List(c.body); }
                break;
            }
            case StmtKind::For: {
                auto& s = static_cast<const ForStmt&>(st);
                if (s.init) S(*s.init);
                if (s.cond) E(*s.cond);
                if (s.step) E(*s.step);
                List(s.body);
                break;
            }
            case StmtKind::ForEach: { auto& s = static_cast<const ForEachStmt&>(st); Use(s.var); E(*s.iterable); List(s.body); break; }
            case StmtKind::Multi: List(static_cast<const MultiStmt&>(st).list); break;
            case StmtKind::Destructure: {
                auto& s = static_cast<const DestructureStmt&>(st);
                for (auto& n : s.names) if (!n.empty()) Use(n);
                E(*s.init);
                break;
            }
            case StmtKind::Import: Use(static_cast<const ImportStmt&>(st).alias); break;
            case StmtKind::Break: case StmtKind::Continue: break;
        }
    }
    void Args(const std::vector<ExprPtr>& a) { for (auto& e : a) E(*e); }
    void E(const Expr& ex) {
        switch (ex.kind) {
            case ExprKind::Number: case ExprKind::String: case ExprKind::Bool: break;
            case ExprKind::Var: Use(static_cast<const VarExpr&>(ex).n); break;
            case ExprKind::InterpString:
                for (auto& g : static_cast<const InterpStringExpr&>(ex).segs) if (g.isExpr) E(*g.expr);
                break;
            case ExprKind::Assign: { auto& e = static_cast<const AssignExpr&>(ex); Use(e.n); E(*e.value); break; }
            case ExprKind::Unary: E(*static_cast<const UnaryExpr&>(ex).e); break;
            case ExprKind::Logical: { auto& e = static_cast<const LogicalExpr&>(ex); E(*e.l); E(*e.r); break; }
            case ExprKind::Coalesce: { auto& e = static_cast<const CoalesceExpr&>(ex); E(*e.l); E(*e.r); break; }
            case ExprKind::NullishAssign: { auto& e = static_cast<const NullishAssignExpr&>(ex); Use(e.n); E(*e.value); break; }
            case ExprKind::Binary: { auto& e = static_cast<const BinaryExpr&>(ex); E(*e.l); E(*e.r); break; }
            case ExprKind::Member: E(*static_cast<const MemberExpr&>(ex).obj); break;
            case ExprKind::MemberAssign: { auto& e = static_cast<const MemberAssignExpr&>(ex); E(*e.obj); E(*e.value); break; }
            case ExprKind::Cast: E(*static_cast<const CastExpr&>(ex).e); break;
            case ExprKind::MethodCall: { auto& e = static_cast<const MethodCallExpr&>(ex); E(*e.obj); Args(e.args); break; }
            case ExprKind::Call: { auto& e = static_cast<const CallExpr&>(ex); Use(e.name); Args(e.args); break; }
            case ExprKind::Ternary: { auto& e = static_cast<const TernaryExpr&>(ex); E(*e.cond); E(*e.a); E(*e.b); break; }
            case ExprKind::Array: Args(static_cast<const ArrayExpr&>(ex).elems); break;
            case ExprKind::Map: Args(static_cast<const MapExpr&>(ex).values); break;
            case ExprKind::Index: { auto& e = static_cast<const IndexExpr&>(ex); E(*e.arr); E(*e.index); break; }
            case ExprKind::IndexAssign: { auto& e = static_cast<const IndexAssignExpr&>(ex); E(*e.arr); E(*e.index); E(*e.value); break; }
            case ExprKind::IndexCompoundAssign: {
                auto& e = static_cast<const IndexCompoundAssignExpr&>(ex);
                E(*e.arr); E(*e.index); E(*e.value);
                break;
            }
            case ExprKind::Lambda: {
                // What the lambda uses minus what it declares itself is free in it: a capture of the code around it.
                auto& e = static_cast<const LambdaExpr&>(ex);
                std::unordered_set<std::string> inner;
                UseWalker w{&inner, &inner};
                w.List(e.body);
                for (auto& d : e.defaults) if (d) w.E(*d);
                std::unordered_set<std::string> own(e.params.begin(), e.params.end());
                std::vector<std::string> order;
                std::unordered_set<std::string> statics;
                CollectBodyLocals(e.body, order, statics);
                own.insert(order.begin(), order.end());
                for (const std::string& n : inner)
                    if (!own.count(n)) {
                        if (inLambdas) inLambdas->insert(n);
                        if (uses) uses->insert(n);
                    }
                break;
            }
            case ExprKind::New: Args(static_cast<const NewExpr&>(ex).args); break;
            case ExprKind::EventOrCompound: {
                auto& e = static_cast<const EventOrCompoundExpr&>(ex);
                Use(e.name);
                E(*e.rhs);
                break;
            }
        }
    }
};
bool GenWalk(const StmtList& l);
bool GenWalk(const Stmt& st) {
    switch (st.kind) {
        case StmtKind::Yield: return static_cast<const YieldStmt&>(st).generator;
        case StmtKind::Try: {
            auto& s = static_cast<const TryStmt&>(st);
            return GenWalk(s.body) || GenWalk(s.catchB) || GenWalk(s.finallyB);
        }
        case StmtKind::Block: return GenWalk(static_cast<const BlockStmt&>(st).body);
        case StmtKind::If: { auto& s = static_cast<const IfStmt&>(st); return GenWalk(s.thenB) || GenWalk(s.elseB); }
        case StmtKind::While: return GenWalk(static_cast<const WhileStmt&>(st).body);
        case StmtKind::DoWhile: return GenWalk(static_cast<const DoWhileStmt&>(st).body);
        case StmtKind::Switch: {
            for (auto& c : static_cast<const SwitchStmt&>(st).cases) if (GenWalk(c.body)) return true;
            return false;
        }
        case StmtKind::For: return GenWalk(static_cast<const ForStmt&>(st).body);
        case StmtKind::ForEach: return GenWalk(static_cast<const ForEachStmt&>(st).body);
        case StmtKind::Multi: return GenWalk(static_cast<const MultiStmt&>(st).list);
        default: return false;
    }
}
bool GenWalk(const StmtList& l) {
    for (auto& s : l) if (GenWalk(*s)) return true;
    return false;
}
} // namespace

void CollectCapturedNames(const std::vector<std::string>& params, const StmtList& body, std::unordered_set<std::string>& out) {
    std::unordered_set<std::string> captured;
    UseWalker w{nullptr, &captured};
    w.List(body);
    if (captured.empty()) return;
    std::vector<std::string> names(params.begin(), params.end());
    std::unordered_set<std::string> statics;
    CollectBodyLocals(body, names, statics);
    for (const std::string& n : names)
        if (captured.count(n) && !statics.count(n)) out.insert(n);
}

bool IsGeneratorBody(const StmtList& body) { return GenWalk(body); }

} // namespace okayscript

