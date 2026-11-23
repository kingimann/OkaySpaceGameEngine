// The script checker's expressions: names, lambdas, array and index forms, own and engine members (methods of class Checker, see CheckerInternal.hpp).
#include "CheckerInternal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <optional>
#include <set>

namespace okay::script::checker_detail {

// ---- Expressions ----
std::vector<std::string> Checker::VisibleNames() {
    std::vector<std::string> n;
    for (auto& s : scopes_) for (auto& v : s) n.push_back(v.name);
    for (auto& f : cls_->fields) n.push_back(f.name);
    for (auto& m : cls_->methods) n.push_back(m.name);
    for (auto& t : api_.types) n.push_back(t.name);
    n.push_back("gameObject"); n.push_back("transform"); n.push_back("enabled");
    return n;
}

void Checker::CheckExpr(Expr& e) {
    switch (e.kind) {
        case EK::IntLit: e.type = Type::Of(TK::Int); return;
        case EK::FloatLit: e.type = Type::Of(TK::Float); return;
        case EK::StringLit: e.type = Type::Of(TK::String); return;
        case EK::BoolLit: e.type = Type::Of(TK::Bool); return;
        case EK::NullLit: e.type = Type::Of(TK::Null); return;
        case EK::This:
            if (InComponent()) { Error(e.loc, "`this` on its own isn't a value here: use this.field or this.Method()"); return; }
            if (inFieldInit_) { Error(e.loc, "A field's starting value can't use `this`"); return; }
            UsesSelf();
            e.type = Type{TK::Class, ClassId()};
            return;
        case EK::Name: CheckName(e); return;
        case EK::Member: CheckMember(e); return;
        case EK::Call: CheckCall(e); return;
        case EK::New: CheckNew(e); return;
        case EK::Unary: CheckUnary(e); return;
        case EK::Binary: CheckBinary(e); return;
        case EK::Assign: CheckAssign(e); return;
        case EK::IncDec: CheckIncDec(e); return;
        case EK::Cast: CheckCast(e); return;
        case EK::Conditional: CheckConditional(e); return;
        case EK::Index: CheckIndex(e); return;
        case EK::NullCond: {
            CheckExpr(*e.lhs);
            const Type t = e.lhs->type;
            if (t.kind != TK::Error && t.kind != TK::Void && !CanBeNull(t)) {
                Error(e.loc, "?. gives null when '" + e.name + "' is null, but " + Article(TypeName(t)) +
                             " can't be null: check " + e.name + " != null first");
                return;
            }
            e.type = t;
            return;
        }
        case EK::Interp:
            for (auto& a : e.args) {
                CheckExpr(*a);
                if (a->type.kind == TK::Void) Error(a->loc, "This doesn't produce a value to put in the text");
                else Expect(*a, Type::Of(TK::Any), "");   // an enum shows its name
            }
            e.type = Type::Of(TK::String);
            return;
        case EK::Format: {
            CheckExpr(*e.lhs);
            const std::string& f = e.sval;
            const bool digits = f.size() >= 2 && f.size() <= 3 && std::all_of(f.begin() + 1, f.end(), [](char ch) { return std::isdigit((unsigned char)ch); });
            if ((f.empty() || (f[0] != 'F' && f[0] != 'f')) || (f.size() > 1 && !digits)) {
                Error(e.loc, "Only :F formats work here, like {time:F1} (F and the number of decimals)");
                return;
            }
            e.ival = f.size() > 1 ? std::stoi(f.substr(1)) : 2;   // C#'s plain F is 2 decimals
            if (e.ival > 15) { Error(e.loc, "At most 15 decimals: :F15"); return; }
            if (!e.lhs->type.IsNumeric() && e.lhs->type.kind != TK::Error) { Error(e.loc, ":F needs a number, not " + TypeName(e.lhs->type)); return; }
            e.type = Type::Of(TK::String);
            return;
        }
        case EK::NewArray: CheckNewArray(e); return;
        case EK::ArrayLit:
            for (auto& a : e.args) CheckExpr(*a);
            Error(e.loc, "{ ... } only works right after `=` in a declaration: write new int[] { ... }");
            return;
        case EK::Lambda:   // its type comes from where it goes (Expect)
            e.type = Type::Of(TK::Lambda);
            pending_.push_back(&e);
            return;
    }
}

void Checker::Unpend(Expr& e) { pending_.erase(std::remove(pending_.begin(), pending_.end(), &e), pending_.end()); }

// A lambda used as `to` (a Func or Action).
void Checker::CheckLambda(Expr& e, const Type& to, const std::string& what) {
    Unpend(e);
    e.type = Type{};
    if (to.kind == TK::Error) return;
    if (to.kind != TK::Func) {
        Error(e.loc, "A lambda only works where a Func or Action is expected" +
                     (to.kind == TK::Any || what.empty() ? std::string() : ": " + what + " is " + TypeName(to)));
        return;
    }
    const Program::FuncSig sig = prog_.funcs[(std::size_t)to.id];
    if (e.lambda->params.size() != sig.params.size()) {
        Error(e.loc, "This lambda takes " + Values(e.lambda->params.size()) + " but " + TypeName(to) + " gives it " + std::to_string(sig.params.size()));
        return;
    }
    LambdaBody(e, sig.params, sig.ret, false);
    e.type = to;
}

// A lambda whose result type is worked out from its body (Select, OrderBy, Sum):
// its Func type, or an error type. `who` names the method taking it.
Type Checker::InferLambda(Expr& e, const std::vector<Type>& params, const std::string& who) {
    if (e.type.kind == TK::Func) {   // a Func variable: its signature must fit
        const Program::FuncSig& sig = prog_.funcs[(std::size_t)e.type.id];
        if (sig.params == params) return e.type;
    }
    if (e.kind != EK::Lambda || e.type.kind != TK::Lambda) {
        if (e.type.kind != TK::Error) Error(e.loc, who + " needs a lambda here, like x => x * 2");
        return Type{};
    }
    Unpend(e);
    if (e.lambda->params.size() != params.size()) {
        Error(e.loc, "This lambda takes " + Values(e.lambda->params.size()) + " but " + who + " gives it " + std::to_string(params.size()));
        e.type = Type{};
        return Type{};
    }
    const Type ret = LambdaBody(e, params, Type{}, true);
    e.type = ret.kind == TK::Error ? Type{} : prog_.FuncOf(params, ret);
    return e.type;
}

bool Checker::IsStatementLike(const Expr& v) {
    return v.kind == EK::Assign || v.kind == EK::IncDec || v.kind == EK::Call || v.kind == EK::New ||
           (v.kind == EK::NullCond && v.lhs->kind == EK::Call);
}

// Check a lambda's body with these parameter types; returns its result type
// (worked out from the body when `infer`).
Type Checker::LambdaBody(Expr& e, const std::vector<Type>& params, Type ret, bool infer) {
    LambdaDecl& L = *e.lambda;
    outer_.push_back(Outer{std::move(scopes_), std::move(captures_), lambda_, nextSlot_, maxSlot_, loopDepth_, switchDepth_,
                           lambdaRet_, inferRet_, retState_});
    scopes_.clear();
    scopes_.emplace_back();
    captures_.clear();
    lambda_ = &L;
    L.captureFrom.clear();
    nextSlot_ = maxSlot_ = loopDepth_ = switchDepth_ = 0;   // break / continue don't reach the loop around a lambda
    lambdaRet_ = ret;
    inferRet_ = infer;
    retState_ = 0;
    for (std::size_t i = 0; i < L.params.size(); ++i) {
        Param& p = L.params[i];
        p.resolved = params[i];
        for (auto& v : scopes_.back()) if (v.name == p.name) Error(p.loc, "Two parameters are called '" + p.name + "'");
        scopes_.back().push_back({p.name, p.resolved, nextSlot_++, p.loc, true});
    }
    maxSlot_ = nextSlot_;
    if (L.value) {
        Expr& v = *L.value;
        CheckExpr(v);
        if (infer) {
            if (v.type.kind == TK::Null || v.type.kind == TK::Lambda) {
                Unpend(v);
                Error(v.loc, "Can't tell this lambda's result type from " + TypeName(v.type));
                lambdaRet_ = Type{};
            } else lambdaRet_ = v.type;
        } else if (ret.kind == TK::Void) {
            if (!IsStatementLike(v) && v.type.kind != TK::Error) Error(v.loc, "This lambda works out a value but an Action doesn't use it");
        } else {
            Expect(v, ret, "This lambda's value");
        }
    } else {
        const bool completes = CheckBlockBody(*L.body);
        if (infer && retState_ == 0) lambdaRet_ = Type::Of(TK::Void);
        if (completes && lambdaRet_.kind != TK::Void && lambdaRet_.kind != TK::Error)
            Error(e.loc, "This lambda must return " + Article(TypeName(lambdaRet_)) + " on every path");
    }
    PopScope();
    L.localCount = maxSlot_;
    L.ret = lambdaRet_;
    const Type result = lambdaRet_;
    Outer& o = outer_.back();
    scopes_ = std::move(o.scopes);
    captures_ = std::move(o.captures);
    lambda_ = o.lambda;
    nextSlot_ = o.nextSlot; maxSlot_ = o.maxSlot; loopDepth_ = o.loopDepth; switchDepth_ = o.switchDepth;
    lambdaRet_ = o.ret; inferRet_ = o.inferRet; retState_ = o.retState;
    outer_.pop_back();
    return result;
}

// new[] { ... }: every item has the same type (ints mixed with floats make floats).
void Checker::CheckImplicitArray(Expr& e) {
    Type elem{};
    bool same = true;
    for (std::size_t i = 0; i < e.args.size(); ++i) {
        CheckExpr(*e.args[i]);
        const Type t = e.args[i]->type;
        if (i == 0) elem = t;
        else if (elem.IsNumeric() && t.IsNumeric()) { if (t.kind == TK::Float) elem = t; }
        else if (t != elem) same = false;
    }
    if (e.args.empty() || !same || elem.kind == TK::Null || elem.kind == TK::Void) {
        Error(e.loc, "new[] can't tell the items' type: write it, e.g. new int[] { ... }");
        return;
    }
    if (elem.kind == TK::Error) return;
    CheckItems(e, prog_.ArrayOf(elem));
}

void Checker::CheckIndex(Expr& e) {
    CheckExpr(*e.object);
    CheckExpr(*e.lhs);
    const Type at = e.object->type;
    if (at.kind == TK::Error) return;
    if (at.kind == TK::Dict) {   // d[key]
        Expect(*e.lhs, Gen(at).a, "The key");
        e.type = Gen(at).b;
        return;
    }
    if (at.kind != TK::Array && at.kind != TK::List) { Error(e.loc, TypeName(at) + " values can't be indexed with [ ]"); return; }
    if (e.lhs->type.kind != TK::Int && e.lhs->type.kind != TK::Error)
        Error(e.lhs->loc, std::string(at.kind == TK::List ? "A list" : "An array") + " index must be an int, not " + TypeName(e.lhs->type));
    e.type = at.kind == TK::List ? Gen(at).a : prog_.arrays[(std::size_t)at.id];
}

void Checker::CheckNewArray(Expr& e) {
    if (e.typeArg.name.empty()) { CheckImplicitArray(e); return; }
    if (e.lhs) { CheckExpr(*e.lhs); Expect(*e.lhs, Type::Of(TK::Int), "The array's length"); }
    Type t = ResolveType(e.typeArg, false);
    if (t.kind == TK::Error) { for (auto& a : e.args) CheckExpr(*a); return; }
    CheckItems(e, t);
    if (e.hasInit && e.lhs) {
        std::optional<Value> n = e.lhs->type.kind == TK::Int ? Fold(*e.lhs) : std::nullopt;
        auto* ni = n ? std::get_if<std::int32_t>(&*n) : nullptr;
        if (!ni) Error(e.lhs->loc, "With { ... } the length must be a fixed number (or leave it out: new int[] { ... })");
        else if (*ni != (std::int32_t)e.args.size())
            Error(e.loc, "The array has length " + std::to_string(*ni) + " but " + std::to_string(e.args.size()) + " values");
    }
}

// A static method runs without an object, so it can't use the object's own members.
bool Checker::RejectInStatic(const std::string& name, SrcLoc loc) {
    if (!method_ || !method_->isStatic) return false;
    Error(loc, "'" + name + "' belongs to each object, so the static method " + method_->name + "() can't use it");
    return true;
}

// `T` in T.member, when T is the script's class (and nothing local hides it).
bool Checker::IsOwnClass(const Expr& e) {
    return e.kind == EK::Name && e.name == cls_->name && !FindLocal(e.name) && FieldIndex(e.name) < 0;
}

// e.name as one of the current class's own members (a field, or the object's
// gameObject / transform / enabled); false if it's neither.
bool Checker::BindOwnMember(Expr& e) {
    int fi = FieldIndex(e.name);
    if (fi >= 0) {
        if (inFieldInit_) { Error(e.loc, "A field's starting value can't use another field: set '" + e.name + "' up in Start()"); return true; }
        FieldDecl& f = cls_->fields[(std::size_t)fi];
        if (!f.isStatic && !f.isConst && RejectInStatic(e.name, e.loc)) return true;
        f.read = true;
        if (!f.isStatic && !f.isConst) UsesSelf();
        e.bind = f.isStatic ? Bind::StaticField : Bind::Field; e.slot = fi; e.type = f.resolved;
        return true;
    }
    if (!IsSelfMemberName(e.name)) return false;
    if (RejectOutsideComponent(e.name, e.loc)) return true;
    if (inFieldInit_) { Error(e.loc, "A field's starting value can't use '" + e.name + "' (the object isn't ready yet): set it in Start()"); return true; }
    if (RejectInStatic(e.name, e.loc)) return true;
    UsesSelf();
    e.bind = Bind::SelfMember;
    e.slot = e.name == "gameObject" ? 0 : e.name == "transform" ? 1 : 2;
    e.type = e.slot == 0 ? Type::Obj(api_.gameObject) : e.slot == 1 ? Type::Obj(api_.transform) : Type::Of(TK::Bool);
    return true;
}

void Checker::CheckName(Expr& e) {
    if (inEnumValue_)
        for (const ClassDecl& k : prog_.classes)
            for (const FieldDecl& f : k.fields)
                if (f.name == e.name) {
                    Error(e.loc, "An enum value can't use the field '" + e.name + "': write the number, or another enum value");
                    return;
                }
    if (LocalVar* v = FindLocal(e.name)) {
        v->used = true; e.bind = v->captured ? Bind::Captured : Bind::Local; e.slot = v->slot; e.type = v->type; return;
    }
    if (BindOwnMember(e)) return;
    if (MethodIdx(e.name) >= 0) { Error(e.loc, "'" + e.name + "' is a method: call it with " + e.name + "()"); return; }
    if (ClassIndex(e.name) >= 0) { Error(e.loc, "'" + e.name + "' is a class: make one with new " + e.name + "(...)"); return; }
    if (EnumIndex(e.name) >= 0) { Error(e.loc, "'" + e.name + "' is an enum: use one of its values, like " + e.name + "." +
                                               (prog_.enums[(std::size_t)EnumIndex(e.name)].members.empty() ? std::string("Value")
                                                : prog_.enums[(std::size_t)EnumIndex(e.name)].members[0].name)); return; }
    int ti = api_.Find(e.name);
    if (ti >= 0) { e.bind = Bind::StaticType; e.slot = ti; e.isStaticRecv = true; e.type = Type{}; return; }
    for (auto& en : prog_.enums)
        for (auto& m : en.members)
            if (m.name == e.name) { Error(e.loc, "Unknown name '" + e.name + "'. Did you mean " + en.name + "." + m.name + "?"); return; }
    Error(e.loc, "Unknown name '" + e.name + "'" + Suggest(e.name, VisibleNames()));
}

// Resolve `obj` as a member receiver: a static type, `this`, or a value.
// Returns: 0 = value, 1 = static type (id in typeId), 2 = this, 3 = enum (id in typeId),
// 4 = the script's own class (T.staticMember), -1 = error.
int Checker::Receiver(Expr& obj, int& typeId) {
    if (obj.kind == EK::This) {
        if (inFieldInit_) { Error(obj.loc, "A field's starting value can't use `this`"); return -1; }
        if (method_ && method_->isStatic) { Error(obj.loc, "A static method has no `this`: use the static member directly"); return -1; }
        return 2;
    }
    if (IsOwnClass(obj)) return 4;
    if (obj.kind == EK::Name && !FindLocal(obj.name) && FieldIndex(obj.name) < 0 && !IsSelfMemberName(obj.name)) {
        if (int ei = EnumIndex(obj.name); ei >= 0) { typeId = ei; return 3; }
        int ti = api_.Find(obj.name);
        if (ti >= 0) { obj.bind = Bind::StaticType; obj.slot = ti; obj.isStaticRecv = true; typeId = ti; return 1; }
    }
    CheckExpr(obj);
    if (obj.type.kind == TK::Error) return -1;
    return 0;
}

void Checker::CheckMember(Expr& e) {
    int typeId = -1;
    int r = Receiver(*e.object, typeId);
    if (r < 0 || BadNullCond(e, r)) return;
    if (r == 4) {
        int fi = FieldIndex(e.name);
        const FieldDecl* f = fi >= 0 ? &cls_->fields[(std::size_t)fi] : nullptr;
        if (f && !f->isStatic && !f->isConst) { Error(e.loc, "'" + e.name + "' isn't static: use it without " + cls_->name + "."); return; }
        if (!f && MethodIdx(e.name) < 0) { Error(e.loc, cls_->name + " has no '" + e.name + "'"); return; }
    }
    if (r == 2 || r == 4) {   // this.x, T.x: a member, never a local of the same name
        e.object.reset();
        if (BindOwnMember(e)) return;
        if (MethodIdx(e.name) >= 0) { Error(e.loc, "'" + e.name + "' is a method: call it with " + e.name + "()"); return; }
        Error(e.loc, cls_->name + " has no '" + e.name + "'" + Suggest(e.name, Names(cls_->fields)));
        return;
    }
    if (r == 3) {
        const EnumDecl& en = prog_.enums[(std::size_t)typeId];
        for (std::size_t i = 0; i < en.members.size(); ++i)
            if (en.members[i].name == e.name) {
                if ((int)i >= enumReady_[(std::size_t)typeId]) {
                    Error(e.loc, en.name + "." + e.name + " is used before its value is worked out");
                    return;
                }
                e.bind = Bind::EnumValue; e.ival = en.members[i].v;
                e.type = Type{TK::Enum, typeId};
                return;
            }
        Error(e.loc, en.name + " has no '" + e.name + "'" + Suggest(e.name, Names(en.members)));
        return;
    }
    if (r == 1) {
        const ApiType& t = api_.types[(std::size_t)typeId];
        for (std::size_t i = 0; i < t.staticProps.size(); ++i)
            if (t.staticProps[i].name == e.name) { e.bind = Bind::ApiStaticProp; e.slot = typeId; e.member = (int)i; e.type = FromApi(t.staticProps[i].type); return; }
        for (auto& m : t.staticMethods)
            if (m.name == e.name) { Error(e.loc, t.name + "." + e.name + " is a method: call it with " + e.name + "(...)"); return; }
        std::vector<std::string> opts = Names(t.staticProps);
        for (auto& m : t.staticMethods) opts.push_back(m.name);
        Error(e.loc, t.name + " has no '" + e.name + "'" + Suggest(e.name, opts));
        return;
    }
    const Type& ot = e.object->type;
    if (ot.kind == TK::Class) {
        ClassDecl& k = prog_.classes[(std::size_t)ot.id];
        for (std::size_t i = 0; i < k.fields.size(); ++i) {
            FieldDecl& f = k.fields[i];
            if (f.name != e.name) continue;
            if (!f.isPublic && &k != cls_) { Error(e.loc, "'" + e.name + "' is private to " + k.name + ": make it public to use it here"); return; }
            f.read = true;
            e.bind = Bind::ObjField; e.slot = (int)i; e.type = f.resolved;
            return;
        }
        std::vector<std::string> opts = Names(k.fields);
        for (auto& m : k.methods) if (!m.isCtor) opts.push_back(m.name);
        for (auto& m : k.methods)
            if (!m.isCtor && m.name == e.name) { Error(e.loc, e.name + " is a method: call it with " + e.name + "(...)"); return; }
        Error(e.loc, k.name + " has no '" + e.name + "'" + Suggest(e.name, opts));
        return;
    }
    if (ot.kind == TK::Vector3) {
        if (e.name == "x" || e.name == "y" || e.name == "z") {
            e.bind = Bind::VecField; e.member = e.name[0] - 'x'; e.type = Type::Of(TK::Float); return;
        }
        const ApiType& t = api_.types[(std::size_t)api_.vector3];
        for (std::size_t i = 0; i < t.props.size(); ++i)
            if (t.props[i].name == e.name) { e.bind = Bind::ApiProp; e.slot = api_.vector3; e.member = (int)i; e.type = t.props[i].type; return; }
        std::vector<std::string> opts = {"x", "y", "z"};
        for (auto& p : t.props) opts.push_back(p.name);
        Error(e.loc, "Vector3 has no '" + e.name + "'" + Suggest(e.name, opts));
        return;
    }
    if (ot.kind == TK::Object) {
        const ApiType& t = api_.types[(std::size_t)ot.id];
        for (std::size_t i = 0; i < t.props.size(); ++i)
            if (t.props[i].name == e.name) { e.bind = Bind::ApiProp; e.slot = ot.id; e.member = (int)i; e.type = t.props[i].type; return; }
        for (auto& m : t.methods)
            if (m.name == e.name) { Error(e.loc, e.name + " is a method: call it with " + e.name + "(...)"); return; }
        std::vector<std::string> opts = Names(t.props);
        for (auto& m : t.methods) opts.push_back(m.name);
        Error(e.loc, t.name + " has no '" + e.name + "'" + Suggest(e.name, opts));
        return;
    }
    if (ot.kind == TK::Array) {
        if (e.name == "Length") { e.bind = Bind::ArrayLength; e.type = Type::Of(TK::Int); return; }
        std::string hint = Suggest(e.name, {"Length"});
        if (hint.empty() && (e.name == "Count" || e.name == "Size" || e.name == "length" || e.name == "size")) hint = ". Did you mean 'Length'?";
        Error(e.loc, TypeName(ot) + " has no '" + e.name + "'" + hint);
        return;
    }
    if (ot.kind == TK::List || ot.kind == TK::Dict || ot.kind == TK::Pair) { CheckCollectionMember(e, ot); return; }
    if (ot.kind == TK::String && e.name == "Length") { Error(e.loc, "Text length isn't supported yet"); return; }
    Error(e.loc, TypeName(ot) + " values have no members");
}

} // namespace checker_detail
