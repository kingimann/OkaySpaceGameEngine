// What each syntax-tree node does when it runs.
// dup-allow-begin: ported from the engine's own OkayScriptVM nodes (engine/src/Scripting/OkayScriptVM.cpp, written for this project) without vectors, scene objects, classes, coroutines and events; the engine moves onto this copy in the integration step (docs/status/UNFINISHED.md "OkayScript standalone core")
// Written for this project. The rules are the engine's: numbers and text mix with + as text, a division by zero gives 0,
// an index outside a list reads as 0, a name nobody set reads as 0. Errors are RuntimeErrors; Stmt::Run gives them the
// line of the statement that was running.
#include "ValueMethods.hpp"
#include "okayscript/Ast.hpp"
#include "okayscript/Interpreter.hpp"
#include <cmath>
#include <cstdlib>

namespace okayscript {

using detail::ToInt;
using detail::ToInt64;

void Stmt::Run(Runtime& r) {
    try {
        Exec(r);
    } catch (ScriptError& e) {
        if (e.line == 0) e.line = line;
        throw;
    }
}

namespace {
void RunList(const StmtList& list, Runtime& r) {
    for (auto& s : list) s->Run(r);
}
// `this.hp` outside a class is the global `hp` (classes are not part of the standalone language).
std::string StripThis(const std::string& n) { return n.rfind("this.", 0) == 0 ? n.substr(5) : n; }
} // namespace

Value NumberExpr::Eval(Runtime&) { return Value{v}; }
Value StringExpr::Eval(Runtime&) { return Value{v}; }
Value BoolExpr::Eval(Runtime&) { return Value{v}; }

Value InterpStringExpr::Eval(Runtime& r) {
    std::string out;
    for (auto& s : segs) out += s.isExpr ? s.expr->Eval(r).AsString() : s.lit;
    return Value{out};
}

Value VarExpr::Eval(Runtime& r) {
    const std::string key = StripThis(n);
    if (key.find('.') != std::string::npos) {
        bool ok = false;
        Value v = r.GetDotted(key, ok);
        if (ok) return v;
    }
    return r.Get(key);
}

Value AssignExpr::Eval(Runtime& r) {
    Value v = value->Eval(r);
    const std::string key = StripThis(n);
    if (key.find('.') != std::string::npos && r.SetDotted(key, v)) return v;
    r.Assign(key, v);
    return v;
}

Value UnaryExpr::Eval(Runtime& r) {
    Value v = e->Eval(r);
    if (op == Tok::Minus) return Value{-v.AsNumber()};
    if (op == Tok::Not) return Value{!v.AsBool()};
    return v;
}

Value LogicalExpr::Eval(Runtime& rt) {
    Value lv = l->Eval(rt);
    if (op == Tok::And) return Value{lv.AsBool() ? r->Eval(rt).AsBool() : false};
    return Value{lv.AsBool() ? true : r->Eval(rt).AsBool()};
}

Value CoalesceExpr::Eval(Runtime& rt) {
    Value lv = l->Eval(rt);
    return IsNullish(lv) ? r->Eval(rt) : lv;
}

Value NullishAssignExpr::Eval(Runtime& rt) {
    Value cur = rt.Get(n);
    if (!IsNullish(cur)) return cur;
    Value v = value->Eval(rt);
    rt.Assign(n, v);
    return v;
}

Value BinaryExpr::Eval(Runtime& rt) {
    Value a = l->Eval(rt), b = r->Eval(rt);
    switch (op) {
        case Tok::Plus:
            if (a.IsString() || b.IsString()) return Value{a.AsString() + b.AsString()};
            return Value{a.AsNumber() + b.AsNumber()};
        case Tok::Minus:   return Value{a.AsNumber() - b.AsNumber()};
        case Tok::Star:    return Value{a.AsNumber() * b.AsNumber()};
        case Tok::Slash:   { const double d = b.AsNumber(); return Value{d != 0 ? a.AsNumber() / d : 0.0}; }
        case Tok::Percent: { const double d = b.AsNumber(); return Value{d != 0 ? std::fmod(a.AsNumber(), d) : 0.0}; }
        case Tok::BitAnd: return Value{(double)(ToInt64(a.AsNumber()) & ToInt64(b.AsNumber()))};
        case Tok::BitOr:  return Value{(double)(ToInt64(a.AsNumber()) | ToInt64(b.AsNumber()))};
        case Tok::BitXor: return Value{(double)(ToInt64(a.AsNumber()) ^ ToInt64(b.AsNumber()))};
        case Tok::Lt: return Value{a.AsNumber() <  b.AsNumber()};
        case Tok::Gt: return Value{a.AsNumber() >  b.AsNumber()};
        case Tok::Le: return Value{a.AsNumber() <= b.AsNumber()};
        case Tok::Ge: return Value{a.AsNumber() >= b.AsNumber()};
        case Tok::Eq: return Value{ValuesEqual(a, b)};
        case Tok::Ne: return Value{!ValuesEqual(a, b)};
        default: return Value{};
    }
}

Value MemberExpr::Eval(Runtime& rt) {
    Value v = obj->Eval(rt), out;
    if (safe && IsNullish(v)) return Value{};
    if (detail::ValueProperty(v, name, out)) return out;
    throw RuntimeError("no property '" + name + "' on this value (" + v.TypeName() + ")", line);
}

Value MemberAssignExpr::Eval(Runtime& rt) {
    Value target = obj->Eval(rt), v = value->Eval(rt);
    if (auto m = target.AsMap()) { (*m)[name] = v; return v; }
    throw RuntimeError("can't assign '" + name + "' on this value (" + target.TypeName() + ")", line);
}

Value CastExpr::Eval(Runtime& rt) {
    Value v = e->Eval(rt);
    if (type == "string") return Value{v.AsString()};
    if (type == "bool") return Value{v.AsBool()};
    if (type == "char") return v.IsString() ? v : Value{std::string(1, (char)ToInt(v.AsNumber()))};
    if (type == "float" || type == "double" || type == "decimal") {
        if (v.IsString()) {
            const std::string s = v.AsString();
            char* end = nullptr;
            const double d = std::strtod(s.c_str(), &end);
            return Value{end == s.c_str() ? 0.0 : d};
        }
        return Value{v.AsNumber()};
    }
    const double f = v.IsString() && v.AsString().size() == 1 ? (double)(unsigned char)v.AsString()[0] : v.AsNumber();
    return Value{(double)ToInt64(f)};   // integer casts truncate toward zero
}

Value MethodCallExpr::Eval(Runtime& rt) {
    Value v = obj->Eval(rt), out;
    if (safe && IsNullish(v)) return Value{};
    std::vector<Value> vals;
    vals.reserve(args.size());
    for (auto& a : args) vals.push_back(a->Eval(rt));
    detail::LambdaInvoker inv = [&rt](const Value& f, std::vector<Value>& xs) { return rt.CallLambda(f, xs); };
    if (detail::CallValueMethod(v, name, vals, out, &inv)) return out;
    throw RuntimeError("no method '" + name + "' on this value (" + v.TypeName() + ")", line);
}

Value CallExpr::Eval(Runtime& rt) {
    std::vector<Value> vals;
    std::vector<std::string> names;
    vals.reserve(args.size());
    bool anyNamed = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string nm = i < argNames.size() ? argNames[i] : std::string();
        if (!nm.empty()) anyNamed = true;
        names.push_back(std::move(nm));
        vals.push_back(args[i]->Eval(rt));
    }
    return rt.Call(name, vals, anyNamed ? &names : nullptr, line);
}

Value TernaryExpr::Eval(Runtime& rt) { return cond->Eval(rt).AsBool() ? a->Eval(rt) : b->Eval(rt); }

Value ArrayExpr::Eval(Runtime& rt) {
    Value v = Value::MakeArray();
    auto arr = v.AsArray();
    for (auto& e : elems) arr->push_back(e->Eval(rt));
    return v;
}

Value IndexExpr::Eval(Runtime& rt) {
    Value a = arr->Eval(rt);
    if (safe && IsNullish(a)) return Value{};
    if (auto v = a.AsArray()) {
        const int i = ToInt(index->Eval(rt).AsNumber());
        if (i < 0 || i >= (int)v->size()) return Value{};
        return (*v)[i];
    }
    if (auto m = a.AsMap()) {
        auto it = m->find(index->Eval(rt).AsString());
        return it == m->end() ? Value{} : it->second;
    }
    if (a.IsString()) {   // "abc"[1] -> "b"
        const std::string s = a.AsString();
        const int i = ToInt(index->Eval(rt).AsNumber());
        if (i < 0 || i >= (int)s.size()) return Value{};
        return Value{std::string(1, s[(std::size_t)i])};
    }
    return Value{};
}

Value IndexAssignExpr::Eval(Runtime& rt) {
    Value a = arr->Eval(rt);
    Value val = value->Eval(rt);
    if (auto v = a.AsArray()) {
        const int i = ToInt(index->Eval(rt).AsNumber());
        if (i >= 0 && i < (int)v->size()) (*v)[i] = val;
        else if (i == (int)v->size()) v->push_back(val);   // one past the end appends
    } else if (auto m = a.AsMap()) {
        (*m)[index->Eval(rt).AsString()] = val;
    }
    return val;
}

Value IndexCompoundAssignExpr::Eval(Runtime& rt) {
    Value a = arr->Eval(rt);
    Value rhs = value->Eval(rt);
    auto apply = [&](const Value& cur) -> Value {
        switch (op) {
            case Tok::Plus:  if (cur.IsString() || rhs.IsString()) return Value{cur.AsString() + rhs.AsString()};
                             return Value{cur.AsNumber() + rhs.AsNumber()};
            case Tok::Minus: return Value{cur.AsNumber() - rhs.AsNumber()};
            case Tok::Star:  return Value{cur.AsNumber() * rhs.AsNumber()};
            case Tok::Slash: { const double d = rhs.AsNumber(); return Value{d != 0 ? cur.AsNumber() / d : 0.0}; }
            case Tok::Percent: { const double d = rhs.AsNumber(); return Value{d != 0 ? std::fmod(cur.AsNumber(), d) : 0.0}; }
            default:         return rhs;
        }
    };
    if (auto v = a.AsArray()) {
        const int i = ToInt(index->Eval(rt).AsNumber());
        if (i >= 0 && i < (int)v->size()) { (*v)[i] = apply((*v)[i]); return (*v)[i]; }
    } else if (auto m = a.AsMap()) {
        const std::string k = index->Eval(rt).AsString();
        Value cur = m->count(k) ? (*m)[k] : Value{};
        (*m)[k] = apply(cur);
        return (*m)[k];
    }
    return Value{};
}

Value LambdaExpr::Eval(Runtime& r) {
    r.lambdas[id] = Runtime::LambdaBody{&params, &body, &defaults};
    Value v = Value::MakeMap();
    (*v.AsMap())["__lambda"] = Value{(double)id};
    return v;
}

// ---- Statements --------------------------------------------------------------------------------------------------
void ExprStmt::Exec(Runtime& r) { e->Eval(r); }

void VarDeclStmt::Exec(Runtime& r) {
    if (!kind) { r.Define(n, init ? init->Eval(r) : Value{}); return; }
    r.DefineTyped(n, init ? init->Eval(r) : DefaultForKind(kind), kind);
}

void ReturnStmt::Exec(Runtime& r) { throw ReturnSignal{e ? e->Eval(r) : Value{}}; }
void BreakStmt::Exec(Runtime&) { throw BreakSignal{}; }
void ContinueStmt::Exec(Runtime&) { throw ContinueSignal{}; }

void ThrowStmt::Exec(Runtime& r) { throw RuntimeError(e ? e->Eval(r).AsString() : std::string("error"), line); }

void TryStmt::Exec(Runtime& r) {
    try {
        RunList(body, r);
    } catch (const std::exception& ex) {
        if (!hasCatch) return;   // try without catch: the error is dropped (engine rule)
        if (!catchVar.empty()) r.Define(catchVar, Value{std::string(ex.what())});
        RunList(catchB, r);
    }
}

void BlockStmt::Exec(Runtime& r) { RunList(body, r); }

void IfStmt::Exec(Runtime& r) {
    if (cond->Eval(r).AsBool()) RunList(thenB, r);
    else RunList(elseB, r);
}

void WhileStmt::Exec(Runtime& r) {
    long long n = 0;
    while (cond->Eval(r).AsBool()) {
        try { RunList(body, r); }
        catch (ContinueSignal&) {}
        catch (BreakSignal&) { break; }
        r.CountIteration(n, "while loop");
    }
}

void DoWhileStmt::Exec(Runtime& r) {
    long long n = 0;
    do {
        try { RunList(body, r); }
        catch (ContinueSignal&) {}
        catch (BreakSignal&) { break; }
        r.CountIteration(n, "do-while loop");
    } while (cond->Eval(r).AsBool());
}

void SwitchStmt::Exec(Runtime& r) {
    auto eq = [](const Value& a, const Value& b) {
        return (a.IsString() || b.IsString()) ? a.AsString() == b.AsString() : a.AsNumber() == b.AsNumber();
    };
    Value v = subject->Eval(r);
    int start = -1;
    for (std::size_t i = 0; i < cases.size(); ++i)
        if (!cases[i].isDefault && eq(v, cases[i].match->Eval(r))) { start = (int)i; break; }
    if (start < 0)
        for (std::size_t i = 0; i < cases.size(); ++i)
            if (cases[i].isDefault) { start = (int)i; break; }
    if (start < 0) return;
    try {
        for (std::size_t i = (std::size_t)start; i < cases.size(); ++i) RunList(cases[i].body, r);   // falls through
    } catch (BreakSignal&) {}
}

void ForStmt::Exec(Runtime& r) {
    if (init) init->Run(r);
    long long n = 0;
    while (!cond || cond->Eval(r).AsBool()) {
        try { RunList(body, r); }
        catch (ContinueSignal&) {}
        catch (BreakSignal&) { break; }
        if (step) step->Eval(r);
        r.CountIteration(n, "for loop");
    }
}

void ForEachStmt::Exec(Runtime& r) {
    Value coll = iterable->Eval(r);
    r.Define(var, Value{});   // the loop variable lives in the current scope
    if (auto arr = coll.AsArray()) {
        const std::size_t n = arr->size();   // items added while looping are not visited
        for (std::size_t i = 0; i < n && i < arr->size(); ++i) {
            r.Assign(var, (*arr)[i]);
            try { RunList(body, r); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&) { break; }
        }
    } else if (auto m = coll.AsMap()) {
        std::vector<std::string> keys;
        keys.reserve(m->size());
        for (auto& kv : *m) keys.push_back(kv.first);
        for (auto& k : keys) {
            if (pairs) {
                Value kv = Value::MakeMap();
                (*kv.AsMap())["Key"] = Value{k};
                auto it = m->find(k);
                (*kv.AsMap())["Value"] = it != m->end() ? it->second : Value{};
                r.Assign(var, kv);
            } else {
                r.Assign(var, Value{k});
            }
            try { RunList(body, r); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&) { break; }
        }
    }
}

void MultiStmt::Exec(Runtime& r) { RunList(list, r); }

} // namespace okayscript
// dup-allow-end
