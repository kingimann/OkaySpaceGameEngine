// The script checker's operators: unary, binary, assignment, increment, cast and ?: (methods of class Checker, see CheckerInternal.hpp).
#include "CheckerInternal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <optional>
#include <set>

namespace okay::script::checker_detail {

void Checker::CheckUnary(Expr& e) {
    CheckExpr(*e.lhs);
    const Type& t = e.lhs->type;
    if (t.kind == TK::Error) return;
    if (e.op == T::Not) {
        if (t.kind != TK::Bool) Error(e.loc, "! needs true/false, not " + TypeName(t));
        e.type = Type::Of(TK::Bool);
        return;
    }
    if (!t.IsNumeric() && t.kind != TK::Vector3) { Error(e.loc, "- needs a number or a Vector3, not " + TypeName(t)); return; }
    e.type = t;
}

bool Checker::CanBeNull(const Type& t) {
    return t.kind == TK::Object || t.kind == TK::Array || t.kind == TK::Class || t.kind == TK::List || t.kind == TK::Dict ||
           t.kind == TK::Func;
}

std::string Checker::Article(const std::string& n) { return (std::string("aeiouAEIOU").find(n[0]) != std::string::npos ? "an " : "a ") + n; }

// ?. only on something that might be null: an object or an array.
bool Checker::BadNullCond(Expr& e, int receiverKind) {
    if (!e.nullCond || (receiverKind == 0 && CanBeNull(e.object->type))) return false;
    Error(e.loc, "?. only works on objects and arrays, which can be null: use . here");
    return true;
}

void Checker::CheckBinary(Expr& e) {
    CheckExpr(*e.lhs);
    CheckExpr(*e.rhs);
    const Type a = e.lhs->type, b = e.rhs->type;
    if (a.kind == TK::Error || b.kind == TK::Error) return;
    if (e.op == T::QuestionQuestion) {   // a ?? b: a, or b when a is null (or a destroyed object)
        if (!CanBeNull(a)) { Error(e.loc, "?? only works on objects and arrays, which can be null: " + Article(TypeName(a)) + " never is"); return; }
        Expect(*e.rhs, a, "The fallback");
        e.type = a;
        return;
    }
    if (a.kind == TK::Void || b.kind == TK::Void) { Error(e.loc, "A method that returns nothing can't be used in a calculation"); return; }
    auto numeric = [&]() {
        if (a.kind == TK::Float || b.kind == TK::Float) {
            e.type = Type::Of(TK::Float);
            e.promoteL = a.kind == TK::Int; e.promoteR = b.kind == TK::Int;
        } else e.type = Type::Of(TK::Int);
    };
    const std::string op = TokenName(e.op);
    switch (e.op) {
        case T::Plus:
            if (a.kind == TK::String || b.kind == TK::String) {
                e.type = Type::Of(TK::String);
                if (a.kind == TK::Enum) e.lhs->enumStr = a.id;
                if (b.kind == TK::Enum) e.rhs->enumStr = b.id;
                return;
            }
            [[fallthrough]];
        case T::Minus:
            if (a.IsNumeric() && b.IsNumeric()) { numeric(); return; }
            if (a.kind == TK::Vector3 && b.kind == TK::Vector3) { e.type = a; return; }
            break;
        case T::Star:
            if (a.IsNumeric() && b.IsNumeric()) { numeric(); return; }
            if (a.kind == TK::Vector3 && b.IsNumeric()) { e.type = a; e.promoteR = b.kind == TK::Int; return; }
            if (a.IsNumeric() && b.kind == TK::Vector3) { e.type = b; e.promoteL = a.kind == TK::Int; return; }
            break;
        case T::Slash:
            if (a.IsNumeric() && b.IsNumeric()) { numeric(); return; }
            if (a.kind == TK::Vector3 && b.IsNumeric()) { e.type = a; e.promoteR = b.kind == TK::Int; return; }
            break;
        case T::Percent:
            if (a.IsNumeric() && b.IsNumeric()) { numeric(); return; }
            break;
        case T::Lt: case T::Gt: case T::Le: case T::Ge:
            if (a.IsNumeric() && b.IsNumeric()) { numeric(); e.type = Type::Of(TK::Bool); return; }
            if (a.kind == TK::Enum && a == b) { e.type = Type::Of(TK::Bool); return; }
            break;
        case T::EqEq: case T::NotEq:
            e.type = Type::Of(TK::Bool);
            if (a.IsNumeric() && b.IsNumeric()) {
                e.promoteL = a.kind == TK::Int && b.kind == TK::Float; e.promoteR = b.kind == TK::Int && a.kind == TK::Float; return;
            }
            if (a == b && (a.kind == TK::Bool || a.kind == TK::String || a.kind == TK::Vector3 || a.kind == TK::Enum)) return;
            // Arrays, lists, dictionaries and class objects compare as references (or with null).
            if (a.kind != TK::Object && b.kind != TK::Object && (CanBeNull(a) || CanBeNull(b)) &&
                (a == b || a.kind == TK::Null || b.kind == TK::Null))
                return;
            if ((a.kind == TK::Object || a.kind == TK::Null) && (b.kind == TK::Object || b.kind == TK::Null)) {
                if (a.kind == TK::Null && b.kind == TK::Null) { Error(e.loc, "Comparing null with null is always true"); return; }
                if (a.kind == TK::Object && b.kind == TK::Object && a != b)
                    { Error(e.loc, "A " + TypeName(a) + " is never the same as a " + TypeName(b)); return; }
                return;
            }
            break;
        case T::AndAnd: case T::OrOr:
            if (a.kind == TK::Bool && b.kind == TK::Bool) { e.type = a; return; }
            Error(e.loc, op + " needs true/false on both sides");
            return;
        default: break;
    }
    Error(e.loc, "Can't use " + op + " on " + TypeName(a) + " and " + TypeName(b));
    e.type = Type{};
}

// Can `e` be assigned to? Explains why not.
bool Checker::Assignable(Expr& e) {
    if (e.kind == EK::Index) return true;
    switch (e.bind) {
        case Bind::StaticField: case Bind::ObjField: return true;
        case Bind::Local: case Bind::Captured:
            if (LocalVar* v = FindLocal(e.name); v && v->readOnly) {
                Error(e.loc, "'" + e.name + "' is the foreach variable, so it can't be changed");
                return false;
            }
            return true;
        case Bind::ArrayLength: Error(e.loc, "'Length' can only be read"); return false;
        case Bind::EnumValue: Error(e.loc, "'" + e.name + "' is an enum value, so it can't change"); return false;
        case Bind::Field: {
            FieldDecl& f = cls_->fields[(std::size_t)e.slot];
            if (f.isConst) { Error(e.loc, "'" + f.name + "' is const, so it can't change"); return false; }
            return true;
        }
        case Bind::SelfMember:
            if (e.slot == 2) return true;
            Error(e.loc, "'" + e.name + "' can't be replaced (change its members instead)");
            return false;
        case Bind::ApiProp: {
            const ApiProperty& p = api_.types[(std::size_t)e.slot].props[(std::size_t)e.member];
            if (!p.set) { Error(e.loc, "'" + p.name + "' can only be read"); return false; }
            return true;
        }
        case Bind::ApiStaticProp: {
            const ApiProperty& p = api_.types[(std::size_t)e.slot].staticProps[(std::size_t)e.member];
            if (!p.set) { Error(e.loc, "'" + p.name + "' can only be read"); return false; }
            return true;
        }
        case Bind::VecField: {
            Expr& o = *e.object;
            if (o.bind == Bind::Local || o.bind == Bind::Captured || o.bind == Bind::Field || o.bind == Bind::VecField || o.bind == Bind::ObjField || o.kind == EK::Index)
                return Assignable(o);
            Error(e.loc, "This changes a copy, so it would do nothing: copy the vector into a variable, change ." +
                         e.name + ", then assign it back (e.g. var p = transform.position; p." + e.name + " = 1; transform.position = p;)");
            return false;
        }
        default:
            Error(e.loc, "This can't be assigned to");
            return false;
    }
}

void Checker::CheckAssign(Expr& e) {
    // A plain `x = ...` writes x without reading it, so it doesn't count as a use.
    LocalVar* lv = nullptr; FieldDecl* fd = nullptr;
    bool wasUsed = false;
    if (e.op == T::Assign && e.lhs->kind == EK::Name) {
        if ((lv = FindLocal(e.lhs->name))) wasUsed = lv->used;
        else if (int fi = FieldIndex(e.lhs->name); fi >= 0) { fd = &cls_->fields[(std::size_t)fi]; wasUsed = fd->read; }
    }
    CheckExpr(*e.lhs);
    if (lv) lv->used = wasUsed;
    if (fd) fd->read = wasUsed;
    CheckExpr(*e.rhs);
    if (e.lhs->type.kind == TK::Error) return;
    if (!Assignable(*e.lhs)) return;
    const Type lt = e.lhs->type;
    e.type = lt;
    if (e.op == T::Assign) { Expect(*e.rhs, lt, "The value"); return; }
    if (e.op == T::QuestionQuestionEq) {
        if (!CanBeNull(lt)) { Error(e.loc, "??= only works on objects and arrays, which can be null: " + Article(TypeName(lt)) + " never is"); return; }
        Expect(*e.rhs, lt, "The value");
        return;
    }
    const Type rt = e.rhs->type;
    if (rt.kind == TK::Error) return;
    bool ok = false;
    switch (e.op) {
        case T::PlusEq:
            ok = (lt.kind == TK::String) || (lt.IsNumeric() && rt.IsNumeric()) || (lt.kind == TK::Vector3 && rt.kind == TK::Vector3);
            break;
        case T::MinusEq: ok = (lt.IsNumeric() && rt.IsNumeric()) || (lt.kind == TK::Vector3 && rt.kind == TK::Vector3); break;
        case T::StarEq: case T::SlashEq: ok = (lt.IsNumeric() && rt.IsNumeric()) || (lt.kind == TK::Vector3 && rt.IsNumeric()); break;
        case T::PercentEq: ok = lt.IsNumeric() && rt.IsNumeric(); break;
        default: break;
    }
    if (!ok) { Error(e.loc, "Can't use " + std::string(TokenName(e.op)) + " with " + TypeName(lt) + " and " + TypeName(rt)); return; }
    if (lt.kind == TK::Int && rt.kind == TK::Float)
        Error(e.rhs->loc, "This is a float but '" + (e.lhs->name.empty() ? std::string("the target") : e.lhs->name) +
                          "' is an int: write (int)(...) to drop the decimals");
    if (lt.kind == TK::Float && rt.kind == TK::Int) e.rhs->toFloat = true;
}

void Checker::CheckIncDec(Expr& e) {
    CheckExpr(*e.lhs);
    if (e.lhs->type.kind == TK::Error) return;
    if (!e.lhs->type.IsNumeric()) { Error(e.loc, std::string(TokenName(e.op)) + " needs a number, not " + TypeName(e.lhs->type)); return; }
    if (!Assignable(*e.lhs)) return;
    e.type = e.lhs->type;
}

void Checker::CheckCast(Expr& e) {
    CheckExpr(*e.lhs);
    Type to = ResolveType(e.typeArg, false);
    const Type& from = e.lhs->type;
    if (from.kind == TK::Error || to.kind == TK::Error) return;
    if (to.IsNumeric() && from.IsNumeric()) {
        if (to == from) Warning(e.loc, "This is already " + TypeName(to) + ": the cast does nothing");
        e.type = to;
        return;
    }
    if ((to.kind == TK::Enum && (from.IsNumeric() || from.kind == TK::Enum)) || (from.kind == TK::Enum && to.IsNumeric())) {
        if (to == from) Warning(e.loc, "This is already " + TypeName(to) + ": the cast does nothing");
        e.type = to;
        return;
    }
    if (to.kind == TK::String) { Error(e.loc, "To turn a value into text, join it: \"\" + value"); return; }
    Error(e.loc, "Can't turn " + TypeName(from) + " into " + TypeName(to));
}

void Checker::CheckConditional(Expr& e) {
    CheckCondition(*e.third, "?:");
    CheckExpr(*e.lhs);
    CheckExpr(*e.rhs);
    const Type a = e.lhs->type, b = e.rhs->type;
    if (a.kind == TK::Error || b.kind == TK::Error) return;
    if (a == b) { e.type = a; return; }
    if (a.IsNumeric() && b.IsNumeric()) { e.type = Type::Of(TK::Float); e.lhs->toFloat = a.kind == TK::Int; e.rhs->toFloat = b.kind == TK::Int; return; }
    if (CanBeNull(a) && b.kind == TK::Null) { e.type = a; return; }
    if (a.kind == TK::Null && CanBeNull(b)) { e.type = b; return; }
    Error(e.loc, "Both sides of ?: must be the same type (" + TypeName(a) + " vs " + TypeName(b) + ")");
}

} // namespace checker_detail
