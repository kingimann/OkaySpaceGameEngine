// Dump(): the syntax tree as small S-expressions, for tests and debugging. Written for this project.
#include "okayscript/Ast.hpp"
#include "okayscript/Environment.hpp"
#include "okayscript/Value.hpp"

namespace okayscript {

namespace {
const char* OpText(Tok t) {
    switch (t) {
        case Tok::Plus: return "+"; case Tok::Minus: return "-"; case Tok::Star: return "*"; case Tok::Slash: return "/";
        case Tok::Percent: return "%"; case Tok::Eq: return "=="; case Tok::Ne: return "!="; case Tok::Lt: return "<";
        case Tok::Gt: return ">"; case Tok::Le: return "<="; case Tok::Ge: return ">="; case Tok::And: return "&&";
        case Tok::Or: return "||"; case Tok::Not: return "!"; case Tok::BitAnd: return "&"; case Tok::BitOr: return "|";
        case Tok::BitXor: return "^"; default: return "?";
    }
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

std::string NumberExpr::Dump() const { return Value::FormatNumber((float)v); }
std::string StringExpr::Dump() const { return Quote(v); }
std::string BoolExpr::Dump() const { return v ? "true" : "false"; }
std::string InterpStringExpr::Dump() const {
    std::string s = "(interp";
    for (auto& g : segs) s += " " + (g.isExpr ? g.expr->Dump() : Quote(g.lit));
    return s + ")";
}
std::string VarExpr::Dump() const { return n; }
std::string AssignExpr::Dump() const { return "(= " + n + " " + value->Dump() + ")"; }
std::string UnaryExpr::Dump() const { return std::string("(") + (op == Tok::Minus ? "neg" : OpText(op)) + " " + e->Dump() + ")"; }
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
std::string YieldStmt::Dump() const { return "(yield " + Opt(e) + ")"; }

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
    return "(try " + DumpList(body) + (hasCatch ? " (catch " + (catchVar.empty() ? std::string("_") : catchVar) + " " + DumpList(catchB) + ")" : "") + ")";
}
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

} // namespace okayscript
