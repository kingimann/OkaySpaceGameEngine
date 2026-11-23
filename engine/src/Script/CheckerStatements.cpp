// The script checker's statements: blocks, every statement kind, switch and conditions (methods of class Checker, see CheckerInternal.hpp).
#include "CheckerInternal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <optional>
#include <set>

namespace okay::script::checker_detail {

// Statements; return whether execution can reach the end.
bool Checker::CheckBlockBody(Stmt& b) { return CheckStmts(b.body); }

bool Checker::CheckStmts(std::vector<StmtPtr>& body) {
    bool completes = true, warned = false;
    for (auto& s : body) {
        if (!completes && !warned) { Warning(s->loc, "This can never run: it comes after a return, break or continue"); warned = true; }
        if (!CheckStmt(*s)) completes = false;
    }
    return completes;
}

bool Checker::CheckStmt(Stmt& s) {
    switch (s.kind) {
        case SK::Block: { PushScope(); bool c = CheckBlockBody(s); PopScope(); return c; }
        case SK::Expr: {
            CheckExpr(*s.expr);
            EK k = s.expr->kind;
            if (k != EK::Assign && k != EK::IncDec && k != EK::Call && !(k == EK::NullCond && s.expr->lhs->kind == EK::Call))
                Error(s.expr->loc, "This line does nothing: it works out a value but doesn't use it");
            return true;
        }
        case SK::Local: {
            for (LocalDecl& d : s.locals) {
                Type t = d.type.name == "var" && d.type.rank == 0 ? Type{} : ResolveType(d.type, true);
                if (d.init && d.init->kind == EK::ArrayLit && d.type.name == "var") {
                    for (auto& a : d.init->args) CheckExpr(*a);
                    Error(d.loc, "`var` can't tell the type of { ... }: write the type, e.g. int[] " + d.name + " = { ... }");
                    t = Type{};
                } else if (d.init && d.init->kind == EK::ArrayLit) {
                    CheckInit(*d.init, t, "'" + d.name + "'");
                } else if (d.init) {
                    CheckExpr(*d.init);
                    if (d.type.name == "var" && d.init->kind == EK::Lambda) {
                        Unpend(*d.init);
                        Error(d.loc, "`var` can't tell a lambda's type: write Func<...> or Action<...>");
                        t = Type{};
                    } else if (d.type.name == "var") {
                        t = d.init->type;
                        if (t.kind == TK::Null) { Error(d.loc, "`var` can't tell the type from null: write the type"); t = Type{}; }
                        if (t.kind == TK::Void) { Error(d.loc, "This doesn't produce a value to store"); t = Type{}; }
                    } else Expect(*d.init, t, "The value of '" + d.name + "'");
                }
                d.resolved = t;
                d.slot = Declare(d.name, t, d.loc);
            }
            return true;
        }
        case SK::If: {
            CheckCondition(*s.expr, "if");
            PushScope(); bool a = CheckStmt(*s.then); PopScope();
            bool b = true;
            if (s.otherwise) { PushScope(); b = CheckStmt(*s.otherwise); PopScope(); }
            return a || b;
        }
        case SK::While: {
            CheckCondition(*s.expr, "while");
            ++loopDepth_;
            PushScope(); CheckStmt(*s.loopBody); PopScope();
            --loopDepth_;
            return !(IsTrueLiteral(*s.expr) && !ContainsBreak(*s.loopBody));
        }
        case SK::DoWhile: {
            ++loopDepth_;
            PushScope(); CheckStmt(*s.loopBody); PopScope();
            --loopDepth_;
            CheckCondition(*s.expr, "while");
            return !(IsTrueLiteral(*s.expr) && !ContainsBreak(*s.loopBody));
        }
        case SK::For: {
            PushScope();
            if (s.init) CheckStmt(*s.init);
            if (s.expr) CheckCondition(*s.expr, "for");
            for (auto& st : s.step) {
                CheckExpr(*st);
                if (st->kind != EK::Assign && st->kind != EK::IncDec && st->kind != EK::Call)
                    Error(st->loc, "This step does nothing: use i++, i += 2 or a call");
            }
            ++loopDepth_;
            PushScope(); CheckStmt(*s.loopBody); PopScope();
            --loopDepth_;
            PopScope();
            return !( !s.expr && !ContainsBreak(*s.loopBody));
        }
        case SK::Return: {
            if (lambda_) return CheckLambdaReturn(s);
            const Type& rt = method_->retType;
            if (s.expr && method_->isCtor) { CheckExpr(*s.expr); Error(s.loc, "A constructor can't return a value"); return false; }
            if (s.expr) {
                CheckExpr(*s.expr);
                if (rt.kind == TK::Void)
                    Error(s.loc, method_->name + "() is void, so it can't return a value. Give it a return type or write `return;`");
                else Expect(*s.expr, rt, "The returned value");
            } else if (rt.kind != TK::Void && rt.kind != TK::Error) {
                Error(s.loc, method_->name + "() must return a " + TypeName(rt) + " here");
            }
            return false;
        }
        case SK::Break:
            if (loopDepth_ == 0 && switchDepth_ == 0) Error(s.loc, "break only works inside a loop or a switch");
            return false;
        case SK::Continue:
            if (loopDepth_ == 0) Error(s.loc, "continue only works inside a loop");
            return false;
        case SK::Switch: return CheckSwitch(s);
        case SK::Foreach: {
            CheckExpr(*s.expr);
            LocalDecl& d = s.locals[0];
            Type elem{};
            const Type& ct = s.expr->type;
            if (ct.kind == TK::Array) elem = prog_.arrays[(std::size_t)ct.id];
            else if (ct.kind == TK::List) elem = Gen(ct).a;
            else if (ct.kind == TK::Dict) elem = prog_.GenericOf(TK::Pair, Gen(ct).a, Gen(ct).b);
            else if (ct.kind != TK::Error) Error(s.expr->loc, "foreach needs an array, a List or a Dictionary, not " + TypeName(ct));
            Type t = d.type.name == "var" && d.type.rank == 0 ? elem : ResolveType(d.type, false);
            if (d.type.name != "var" && !Convertible(elem, t))
                Error(d.loc, "The items of " + (s.expr->kind == EK::Name ? s.expr->name : std::string("this array")) + " are " +
                             TypeName(elem) + " but '" + d.name + "' is " + TypeName(t));
            d.promote = elem.kind == TK::Int && t.kind == TK::Float;
            d.resolved = t;
            PushScope();
            d.slot = Declare(d.name, t, d.loc);
            scopes_.back().back().readOnly = true;
            scopes_.back().back().used = true;
            ++loopDepth_;
            PushScope(); CheckStmt(*s.loopBody); PopScope();
            --loopDepth_;
            PopScope();
            return true;
        }
    }
    return true;
}

bool Checker::CheckLambdaReturn(Stmt& s) {
    if (s.expr) CheckExpr(*s.expr);
    if (inferRet_) {
        if (s.expr && retState_ != 2) {
            const Type t = s.expr->type;
            if (retState_ == 1) Error(s.loc, "This lambda returns a value here but not everywhere");
            else if (t.kind == TK::Null || t.kind == TK::Lambda) Error(s.expr->loc, "Can't tell this lambda's result type from " + TypeName(t));
            else if (t.kind == TK::Void) Error(s.expr->loc, "This doesn't produce a value to return");
            else lambdaRet_ = t;
            retState_ = 2;
        } else if (s.expr) {
            Expect(*s.expr, lambdaRet_, "The returned value");
        } else {
            if (retState_ == 2) Error(s.loc, "This lambda must return " + Article(TypeName(lambdaRet_)) + " here");
            retState_ = 1;
        }
        return false;
    }
    if (s.expr) {
        if (lambdaRet_.kind == TK::Void) Error(s.loc, "This lambda is an Action, so it can't return a value");
        else Expect(*s.expr, lambdaRet_, "The returned value");
    } else if (lambdaRet_.kind != TK::Void && lambdaRet_.kind != TK::Error) {
        Error(s.loc, "This lambda must return " + Article(TypeName(lambdaRet_)) + " here");
    }
    return false;
}

// C#'s rules: the value is an int, string or enum; every case is a
// fixed value of that type, none twice; one default at most; and no section
// runs into the next (each ends in break, return or continue).
bool Checker::CheckSwitch(Stmt& s) {
    CheckExpr(*s.expr);
    const Type t = s.expr->type;
    const bool okType = t.kind == TK::Int || t.kind == TK::String || t.kind == TK::Enum;
    if (!okType && t.kind != TK::Error)
        Error(s.expr->loc, "switch works on an int, a string or an enum, not " + TypeName(t) +
                           (t.kind == TK::Float || t.kind == TK::Bool ? ": use if / else" : ""));
    bool haveDefault = false, anyBreak = false;
    std::vector<Value> seen;
    ++switchDepth_;
    for (SwitchSection& sec : s.sections) {
        PushScope();   // each section's locals are its own
        if (sec.isDefault) {
            if (haveDefault) Error(sec.defaultLoc, "A switch has only one default");
            haveDefault = true;
        }
        for (std::size_t i = 0; i < sec.labels.size(); ++i) {
            Expr& l = *sec.labels[i];
            CheckExpr(l);
            std::optional<Value> v;
            if (okType && l.type.kind != TK::Error) {
                if (!Convertible(l.type, t)) Error(l.loc, ConvertError(l.type, t, "The case value"));
                else {
                    Expect(l, t, "The case value");
                    v = Fold(l);
                    if (!v) Error(l.loc, "A case value must be fixed (a number, text, an enum value or a const), not worked out while playing");
                }
            }
            if (v) {
                for (const Value& x : seen)
                    if (x == *v) { Error(sec.labelLocs[i], "case " + ConstText(*v, t) + " appears twice in this switch"); break; }
                seen.push_back(*v);
            }
            sec.values.push_back(v ? *v : Value{});
        }
        if (CheckStmts(sec.body))
            Error(sec.loc, "Control can't fall through from one case to the next: end this case with break, return or continue");
        for (auto& st : sec.body) if (ContainsBreak(*st)) anyBreak = true;
        PopScope();
    }
    --switchDepth_;
    return !haveDefault || anyBreak;
}

bool Checker::IsTrueLiteral(const Expr& e) { return e.kind == EK::BoolLit && e.bval; }

bool Checker::ContainsBreak(const Stmt& s) {
    switch (s.kind) {
        case SK::Break: return true;
        case SK::Block: for (auto& c : s.body) if (ContainsBreak(*c)) return true; return false;
        case SK::If: return ContainsBreak(*s.then) || (s.otherwise && ContainsBreak(*s.otherwise));
        default: return false;   // a nested loop's break is its own
    }
}

void Checker::CheckCondition(Expr& e, const char* what) {
    CheckExpr(e);
    if (e.type.kind != TK::Bool && e.type.kind != TK::Error)
        Error(e.loc, std::string("The ") + what + " condition must be true/false, not " + TypeName(e.type) +
                     (e.type.IsNumeric() ? " (compare it, e.g. x > 0)" : e.type.kind == TK::Object ? " (compare it, e.g. x != null)" : ""));
}

} // namespace checker_detail
