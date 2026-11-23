// The script checker's constants, types and conversions: folding constant expressions, resolving type names, and the rules for converting one type to another (methods of class Checker, see CheckerInternal.hpp).
#include "CheckerInternal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <optional>
#include <set>

namespace okay::script::checker_detail {

// The value of a constant expression (literals, const fields, enum values and
// operators on them), or nothing if it is worked out while playing.
std::optional<Value> Checker::Fold(const Expr& e) {
    std::optional<Value> r;
    switch (e.kind) {
        case EK::IntLit: r = Value{e.ival}; break;
        case EK::FloatLit: r = Value{e.fval}; break;
        case EK::StringLit: r = Value{e.sval}; break;
        case EK::BoolLit: r = Value{e.bval}; break;
        case EK::Member: if (e.bind == Bind::EnumValue) r = Value{e.ival}; break;
        case EK::Name:
            if (e.bind == Bind::Field) {
                const FieldDecl& f = cls_->fields[(std::size_t)e.slot];
                if (f.isConst && f.init) r = Fold(*f.init);
            }
            break;
        case EK::Unary: {
            auto a = Fold(*e.lhs);
            if (!a) break;
            if (auto* b = std::get_if<bool>(&*a)) { if (e.op == T::Not) r = Value{!*b}; }
            else if (auto* i = std::get_if<std::int32_t>(&*a)) r = Value{(std::int32_t)(std::uint32_t)(0u - (std::uint32_t)*i)};
            else if (auto* f = std::get_if<float>(&*a)) r = Value{-*f};
            break;
        }
        case EK::Cast: {
            auto a = Fold(*e.lhs);
            if (!a) break;
            if (e.type.kind == TK::Float) {
                if (auto* i = std::get_if<std::int32_t>(&*a)) r = Value{(float)*i}; else r = a;
            } else if (auto* f = std::get_if<float>(&*a)) {
                if (std::isfinite(*f) && *f > -2147483649.0f && *f < 2147483648.0f) r = Value{(std::int32_t)*f};
            } else r = a;
            break;
        }
        case EK::Binary: {
            auto a = Fold(*e.lhs), b = Fold(*e.rhs);
            if (!a || !b) break;
            if (e.type.kind == TK::String && e.op == T::Plus) { r = Value{ValueToString(*a) + ValueToString(*b)}; break; }
            auto* x = std::get_if<std::int32_t>(&*a);
            auto* y = std::get_if<std::int32_t>(&*b);
            if (e.type.kind != TK::Int || !x || !y) break;
            const std::int64_t p = *x, q = *y;
            std::int64_t v = 0;
            switch (e.op) {
                case T::Plus: v = p + q; break;
                case T::Minus: v = p - q; break;
                case T::Star: v = p * q; break;
                case T::Slash: if (q == 0) return std::nullopt; v = p / q; break;     // 64-bit: can't overflow
                case T::Percent: if (q == 0) return std::nullopt; v = p % q; break;
                default: return std::nullopt;
            }
            r = Value{(std::int32_t)(std::uint32_t)(std::uint64_t)v};
            break;
        }
        default: break;
    }
    if (r && e.toFloat) if (auto* i = std::get_if<std::int32_t>(&*r)) r = Value{(float)*i};
    return r;
}

// How a case value reads in a message.
std::string Checker::ConstText(const Value& v, const Type& t) const {
    if (t.kind == TK::Enum) {
        const EnumDecl& en = prog_.enums[(std::size_t)t.id];
        if (auto* i = std::get_if<std::int32_t>(&v)) if (const std::string* n = en.NameOf(*i)) return en.name + "." + *n;
    }
    if (auto* s = std::get_if<std::string>(&v)) return "\"" + *s + "\"";
    return ValueToString(v);
}

bool Checker::IsSelfMemberName(const std::string& n) { return n == "gameObject" || n == "transform" || n == "enabled"; }

bool Checker::HasAttr(const FieldDecl& f, const char* n) {
    for (auto& a : f.attrs) if (a.name == n) return true;
    return false;
}

Type Checker::ResolveType(const TypeRef& t, bool allowVar) {
    if (t.rank > 0) {
        if (t.name == "var") { Error(t.loc, "`var` can't be an array type: write the type, e.g. int[]"); return Type{}; }
        if (t.name == "void") { Error(t.loc, "An array can't hold void"); return Type{}; }
        TypeRef base = t;
        base.rank = 0;
        Type r = ResolveType(base, false);
        if (r.kind == TK::Error) return r;
        for (int i = 0; i < t.rank; ++i) r = prog_.ArrayOf(r);
        return r;
    }
    if (!t.args.empty() || t.name == "List" || t.name == "Dictionary" || t.name == "KeyValuePair" || t.name == "Func" || t.name == "Action")
        return ResolveGeneric(t);
    if (t.name == "int") return Type::Of(TK::Int);
    if (t.name == "float") return Type::Of(TK::Float);
    if (t.name == "bool") return Type::Of(TK::Bool);
    if (t.name == "string") return Type::Of(TK::String);
    if (t.name == "Vector3") return Type::Of(TK::Vector3);
    if (t.name == "var" && allowVar) return Type{};   // inferred by the caller
    if (t.name == "double") { Error(t.loc, "Use float for decimal numbers (double isn't supported)"); return Type{}; }
    if (t.name == "void") { Error(t.loc, "void isn't a type a value can have"); return Type{}; }
    if (int ei = EnumIndex(t.name); ei >= 0) return Type{TK::Enum, ei};
    if (int ci = ClassIndex(t.name); ci >= 0) {
        if (ci == prog_.component) { Error(t.loc, "'" + t.name + "' is this script's Behaviour class: it can't be used as a type yet"); return Type{}; }
        return Type{TK::Class, ci};
    }
    int id = api_.Find(t.name);
    if (id >= 0 && !api_.types[(std::size_t)id].isStatic && !api_.types[(std::size_t)id].isValueType) return Type::Obj(id);
    if (id >= 0) { Error(t.loc, t.name + " can't hold a value: use its members, like " + t.name + ".something"); return Type{}; }
    std::vector<std::string> opts = {"int", "float", "bool", "string", "Vector3"};
    for (auto& at : api_.types) if (!at.isStatic && !at.isValueType) opts.push_back(at.name);
    for (auto& en : prog_.enums) opts.push_back(en.name);
    for (auto& k : prog_.classes) opts.push_back(k.name);
    Error(t.loc, "Unknown type '" + t.name + "'" + Suggest(t.name, opts));
    return Type{};
}

// List<T>, Dictionary<K, V> and KeyValuePair<K, V> (what foreach over a dictionary gives).
Type Checker::ResolveGeneric(const TypeRef& t) {
    if (t.name == "List") {
        if (t.args.size() != 1) { Error(t.loc, "List takes one type: List<int>"); return Type{}; }
        Type a = ResolveType(t.args[0], false);
        return a.kind == TK::Error ? a : prog_.GenericOf(TK::List, a);
    }
    if (t.name == "Dictionary" || t.name == "KeyValuePair") {
        if (t.args.size() != 2) { Error(t.loc, t.name + " takes two types: " + t.name + "<string, int>"); return Type{}; }
        Type k = ResolveType(t.args[0], false), v = ResolveType(t.args[1], false);
        if (k.kind == TK::Error || v.kind == TK::Error) return Type{};
        if (!IsKeyType(k)) {
            Error(t.args[0].loc, "A Dictionary key must be an int, string, bool, enum or object, not " + TypeName(k));
            return Type{};
        }
        return prog_.GenericOf(t.name == "Dictionary" ? TK::Dict : TK::Pair, k, v);
    }
    if (t.name == "Func" || t.name == "Action") {   // Func<int, bool>: takes an int, gives a bool
        const bool func = t.name == "Func";
        if (func && t.args.empty()) { Error(t.loc, "Func needs at least the type it gives: Func<int>, Func<int, bool>"); return Type{}; }
        std::vector<Type> ps;
        Type ret = Type::Of(TK::Void);
        for (std::size_t i = 0; i < t.args.size(); ++i) {
            const bool isRet = func && i + 1 == t.args.size();
            if (t.args[i].name == "void" && t.args[i].rank == 0) {
                Error(t.args[i].loc, isRet ? "A Func gives a value: for one that gives nothing, use Action<...>" : "A parameter can't be void");
                return Type{};
            }
            Type a = ResolveType(t.args[i], false);
            if (a.kind == TK::Error) return a;
            if (isRet) ret = a; else ps.push_back(a);
        }
        return prog_.FuncOf(ps, ret);
    }
    Error(t.loc, "Only List<T>, Dictionary<K, V>, Func<...> and Action<...> are generic here");
    return Type{};
}

bool Checker::IsKeyType(const Type& t) {
    return t.kind == TK::Int || t.kind == TK::String || t.kind == TK::Bool || t.kind == TK::Enum ||
           t.kind == TK::Object || t.kind == TK::Class;
}

const Program::Generic& Checker::Gen(const Type& t) const { return prog_.generics[(std::size_t)t.id]; }

void Checker::CheckLifecycleSignature(MethodDecl& m, int) {
    const auto& lc = LifecycleMethods();
    for (std::size_t i = 0; i < lc.size(); ++i) {
        if (m.name != lc[i].name) continue;
        m.lifecycle = (int)i;
        if (m.isStatic) Error(m.loc, m.name + "() can't be static: the engine calls it on each object");
        if (m.retType.kind != TK::Void) Error(m.loc, m.name + "() must be void: the engine ignores anything it returns");
        if (!m.params.empty()) Error(m.loc, m.name + "() takes no parameters (the engine calls it " + lc[i].when + ")");
    }
}

// ---- Conversions ----
bool Checker::Convertible(const Type& from, const Type& to) {
    if (from.kind == TK::Error || to.kind == TK::Error) return true;   // already reported
    if (to.kind == TK::Any) return from.kind != TK::Void;
    if (from == to) return true;
    if (from.kind == TK::Int && to.kind == TK::Float) return true;
    if (from.kind == TK::Null && CanBeNull(to)) return true;
    if (from.kind == TK::Lambda) return to.kind == TK::Func || to.kind == TK::Any;   // the lambda itself is checked by Expect
    return false;
}

std::string Checker::ConvertError(const Type& from, const Type& to, const std::string& what) {
    if (from.kind == TK::Void) return what + " doesn't produce a value";
    if (from.kind == TK::Float && to.kind == TK::Int)
        return what + " is a float but needs an int: write (int)(...) to drop the decimals";
    if (to.kind == TK::String) return what + " is " + TypeName(from) + ", not text: join it into text with \"\" + value";
    return what + " is " + TypeName(from) + " but needs " + TypeName(to);
}

// Check `e` can be used as `to`; mark int->float promotion.
void Checker::Expect(Expr& e, const Type& to, const std::string& what) {
    if (e.kind == EK::Lambda && e.type.kind == TK::Lambda) { CheckLambda(e, to, what); return; }
    if (!Convertible(e.type, to)) { Error(e.loc, ConvertError(e.type, to, what)); return; }
    if (e.type.kind == TK::Int && to.kind == TK::Float) e.toFloat = true;
    if (e.type.kind == TK::Enum && to.kind == TK::Any) e.enumStr = e.type.id;
}

// Check a starting value, which may be a { ... } list when `to` is an array.
void Checker::CheckInit(Expr& init, const Type& to, const std::string& what) {
    if (init.kind != EK::ArrayLit) { CheckExpr(init); Expect(init, to, what); return; }
    if (to.kind == TK::Error) { for (auto& a : init.args) CheckExpr(*a); return; }
    if (to.kind != TK::Array) {
        for (auto& a : init.args) CheckExpr(*a);
        Error(init.loc, "{ ... } makes an array, but " + what + " is " + TypeName(to));
        return;
    }
    CheckItems(init, to);
}

void Checker::CheckItems(Expr& e, const Type& arr) {
    const Type elem = prog_.arrays[(std::size_t)arr.id];
    for (auto& a : e.args) { CheckExpr(*a); Expect(*a, elem, "This item"); }
    e.type = arr;
}

} // namespace checker_detail
