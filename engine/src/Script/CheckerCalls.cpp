// The script checker's calls: engine method overloads and arguments, calls, GetComponent, collection members and LINQ, and new (methods of class Checker, see CheckerInternal.hpp).
#include "CheckerInternal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <optional>
#include <set>

namespace okay::script::checker_detail {

// Pick the overload whose parameters fit the (already checked) arguments.
const ApiMethod* Checker::PickOverload(const std::vector<ApiMethod>& ms, const std::string& owner, Expr& e) {
    std::vector<const ApiMethod*> named;
    for (auto& m : ms) if (m.name == e.name) named.push_back(&m);
    if (named.empty()) return nullptr;
    for (const ApiMethod* m : named) {
        if (m->params.size() != e.args.size()) continue;
        bool ok = true;
        for (std::size_t i = 0; i < e.args.size(); ++i) if (!Convertible(e.args[i]->type, m->params[i])) { ok = false; break; }
        if (ok) return m;
    }
    // Explain why nothing fits.
    std::set<std::size_t> counts;
    for (const ApiMethod* m : named) counts.insert(m->params.size());
    if (!counts.count(e.args.size())) {
        std::string c;
        for (auto n : counts) c += (c.empty() ? "" : " or ") + std::to_string(n);
        Error(e.loc, owner + e.name + " takes " + c + " value" + (c == "1" ? "" : "s") + ", not " + std::to_string(e.args.size()));
    } else {
        for (const ApiMethod* m : named) if (m->params.size() == e.args.size())
            for (std::size_t i = 0; i < e.args.size(); ++i)
                if (!Convertible(e.args[i]->type, m->params[i])) {
                    Error(e.args[i]->loc, ConvertError(e.args[i]->type, m->params[i], owner + e.name + "'s " + m->paramNames[i]));
                    return nullptr;
                }
    }
    return nullptr;
}

void Checker::BindArgs(Expr& e, const ApiMethod& m) {
    for (std::size_t i = 0; i < e.args.size(); ++i) {
        Expect(*e.args[i], m.params[i], m.paramNames[i]);
        if (m.checkLiteral && e.args[i]->kind == EK::StringLit) {
            std::string msg = m.checkLiteral((int)i, e.args[i]->sval);
            if (!msg.empty()) Error(e.args[i]->loc, msg);
        }
    }
}

// f(x) / f.Invoke(x): call the function `e.object` holds.
void Checker::CheckInvoke(Expr& e) {
    for (auto& a : e.args) CheckExpr(*a);
    const Type ft = e.object->type;
    if (ft.kind == TK::Error) return;
    const std::string who = e.object->kind == EK::Name || e.object->kind == EK::Member ? e.object->name : std::string("This value");
    if (ft.kind != TK::Func) {
        Error(e.loc, (who == "This value" ? who : "'" + who + "'") + " is " + TypeName(ft) + ", not a function");
        return;
    }
    if (e.nullCond && !CanBeNull(ft)) return;
    const Program::FuncSig sig = prog_.funcs[(std::size_t)ft.id];
    if (e.args.size() != sig.params.size()) {
        Error(e.loc, (who == "This value" ? std::string("This function") : who) + " takes " + Values(sig.params.size()) + ", not " + std::to_string(e.args.size()));
        return;
    }
    for (std::size_t i = 0; i < e.args.size(); ++i) Expect(*e.args[i], sig.params[i], "Value " + std::to_string(i + 1));
    e.bind = Bind::Invoke;
    e.type = sig.ret;
}

// Turn `name(...)` (or `obj.name(...)`) into calling the function stored in that variable or field.
void Checker::AsInvoke(Expr& e, ExprPtr receiver) {
    auto f = std::make_unique<Expr>();
    f->kind = receiver ? EK::Member : EK::Name;
    f->loc = e.loc;
    f->name = e.name;
    f->object = std::move(receiver);
    f->nullCond = e.nullCond;
    e.nullCond = false;
    e.object = std::move(f);
    e.name = "Invoke";
    CheckExpr(*e.object);
    CheckInvoke(e);
}

void Checker::CheckCall(Expr& e) {
    if (e.bind == Bind::Invoke) {   // f(x)(y), xs[0](): the parser marked it
        e.bind = Bind::None;
        CheckExpr(*e.object);
        CheckInvoke(e);
        return;
    }
    if (!e.object && e.typeArg.name.empty() && (FindLocal(e.name) || (FieldIndex(e.name) >= 0 && MethodIdx(e.name) < 0))) {
        AsInvoke(e, nullptr);   // a variable or field holding a function
        return;
    }
    if (inFieldInit_ && !e.object) { Error(e.loc, "A field's starting value can't call methods: set it up in Start()"); return; }
    // GetComponent<T>() (on this object or another)
    if (e.name == "GetComponent") { CheckGetComponent(e); return; }
    if (!e.typeArg.name.empty()) { Error(e.loc, e.name + " doesn't take a <type>"); return; }
    if (!e.object || e.object->kind == EK::This || IsOwnClass(*e.object)) {
        if (e.object && e.object->kind == EK::This && method_ && method_->isStatic) {
            for (auto& a : e.args) CheckExpr(*a);
            Error(e.object->loc, "A static method has no `this`: call static methods directly");
            return;
        }
        const bool viaClass = e.object && e.object->kind == EK::Name;
        if (e.object && MethodIdx(e.name) < 0 && FieldIndex(e.name) >= 0) {   // this.onHit(): a field holding a function
            ExprPtr recv = std::move(e.object);
            AsInvoke(e, std::move(recv));
            return;
        }
        if (e.object) e.object.reset();
        if (e.name == "Destroy") {
            if (inFieldInit_) { Error(e.loc, "A field's starting value can't destroy things"); return; }
            for (auto& a : e.args) CheckExpr(*a);
            if (e.args.size() != 1) { Error(e.loc, "Destroy takes the object to destroy: Destroy(gameObject)"); return; }
            if (e.args[0]->type.kind != TK::Object || e.args[0]->type.id != api_.gameObject) {
                if (e.args[0]->type.kind != TK::Error)
                    Error(e.args[0]->loc, "Destroy needs a GameObject, not " + TypeName(e.args[0]->type) +
                                          (e.args[0]->type.kind == TK::Object ? " (use .gameObject)" : ""));
                return;
            }
            e.bind = Bind::Destroy; e.type = Type::Of(TK::Void);
            return;
        }
        int mi = MethodIdx(e.name);
        if (mi < 0) {
            for (auto& a : e.args) CheckExpr(*a);
            std::vector<std::string> opts = Names(cls_->methods);
            opts.push_back("Destroy"); opts.push_back("GetComponent");
            Error(e.loc, "Unknown method '" + e.name + "'" + Suggest(e.name, opts));
            return;
        }
        MethodDecl& m = cls_->methods[(std::size_t)mi];
        if (&m != method_) m.called = true;
        for (auto& a : e.args) CheckExpr(*a);
        if (viaClass && !m.isStatic) { Error(e.loc, "'" + e.name + "' isn't static: call it without " + cls_->name + "."); return; }
        if (!m.isStatic && RejectInStatic(e.name, e.loc)) return;
        if (!m.isStatic) UsesSelf();
        if (!ArgsFit(e, m)) return;
        e.bind = Bind::Method; e.slot = mi; e.type = m.retType;
        return;
    }
    int typeId = -1;
    int r = Receiver(*e.object, typeId);
    if (r == 0 && e.object->type.kind == TK::Func) {
        if (e.name != "Invoke") {
            for (auto& a : e.args) CheckExpr(*a);
            Error(e.loc, "A function has no '" + e.name + "': call it with Invoke(...) or (...)");
            return;
        }
        CheckInvoke(e);
        return;
    }
    if (r == 0 && e.object->type.kind == TK::Class && !e.nullCond) {   // obj.onHit(): a public field holding a function
        ClassDecl& k = prog_.classes[(std::size_t)e.object->type.id];
        bool method = false;
        for (auto& m : k.methods) if (!m.isCtor && m.name == e.name) method = true;
        for (std::size_t i = 0; i < k.fields.size() && !method; ++i) {
            FieldDecl& f = k.fields[i];
            if (f.name != e.name || f.resolved.kind != TK::Func) continue;
            if (!f.isPublic && &k != cls_) { Error(e.loc, "'" + e.name + "' is private to " + k.name + ": make it public to use it here"); return; }
            f.read = true;
            auto m = std::make_unique<Expr>();   // obj.field, its receiver already checked
            m->kind = EK::Member; m->loc = e.loc; m->name = e.name;
            m->object = std::move(e.object);
            m->bind = Bind::ObjField; m->slot = (int)i; m->type = f.resolved;
            e.object = std::move(m);
            e.name = "Invoke";
            CheckInvoke(e);
            return;
        }
    }
    for (auto& a : e.args) CheckExpr(*a);
    if (r < 0 || BadNullCond(e, r)) return;
    if (r == 3) { Error(e.loc, prog_.enums[(std::size_t)typeId].name + " has no method '" + e.name + "'"); return; }
    if (r == 1) {
        const ApiType& t = api_.types[(std::size_t)typeId];
        const ApiMethod* m = PickOverload(t.staticMethods, t.name + ".", e);
        if (!m) {
            bool exists = false;
            for (auto& x : t.staticMethods) if (x.name == e.name) exists = true;
            if (!exists) {
                std::vector<std::string> opts = Names(t.staticMethods);
                Error(e.loc, t.name + " has no method '" + e.name + "'" + Suggest(e.name, opts));
            }
            return;
        }
        BindArgs(e, *m);
        e.bind = Bind::ApiStaticMethod; e.slot = typeId; e.member = (int)(m - t.staticMethods.data()); e.type = FromApi(m->ret);
        return;
    }
    const Type& ot = e.object->type;
    if (ot.kind == TK::List || ot.kind == TK::Array) {
        const Type elem = ot.kind == TK::List ? Gen(ot).a : prog_.arrays[(std::size_t)ot.id];
        if (CheckLinq(e, ot, elem)) return;
    }
    if (ot.kind == TK::List || ot.kind == TK::Dict) { CheckCollectionCall(e, ot); return; }
    if (ot.kind == TK::Class) {
        ClassDecl& k = prog_.classes[(std::size_t)ot.id];
        std::vector<std::string> opts;
        for (std::size_t i = 0; i < k.methods.size(); ++i) {
            MethodDecl& m = k.methods[i];
            if (m.isCtor) continue;
            opts.push_back(m.name);
            if (m.name != e.name) continue;
            if (!m.isPublic && &k != cls_) { Error(e.loc, "'" + e.name + "' is private to " + k.name + ": make it public to use it here"); return; }
            if (&m != method_) m.called = true;
            if (!ArgsFit(e, m)) return;
            e.bind = Bind::ObjMethod; e.slot = (int)i; e.member = ot.id; e.type = m.retType;
            return;
        }
        Error(e.loc, k.name + " has no method '" + e.name + "'" + Suggest(e.name, opts));
        return;
    }
    if (ot.kind != TK::Object) { Error(e.loc, TypeName(ot) + " values have no methods"); return; }
    const ApiType& t = api_.types[(std::size_t)ot.id];
    const ApiMethod* m = PickOverload(t.methods, "", e);
    if (!m) {
        bool exists = false;
        for (auto& x : t.methods) if (x.name == e.name) exists = true;
        if (!exists) {
            std::vector<std::string> opts = Names(t.methods);
            Error(e.loc, t.name + " has no method '" + e.name + "'" + Suggest(e.name, opts));
        }
        return;
    }
    BindArgs(e, *m);
    e.bind = Bind::ApiMethod; e.slot = ot.id; e.member = (int)(m - t.methods.data()); e.type = m->ret;
}

void Checker::CheckGetComponent(Expr& e) {
    if (e.typeArg.name.empty()) { Error(e.loc, "Say which component: GetComponent<Transform>()"); return; }
    if (!e.args.empty()) { Error(e.loc, "GetComponent<T>() takes no values"); return; }
    int ti = api_.Find(e.typeArg.name);
    if (ti < 0 || !api_.types[(std::size_t)ti].isComponent) {
        std::vector<std::string> comps;
        for (auto& t : api_.types) if (t.isComponent) comps.push_back(t.name);
        std::string list;
        for (auto& c : comps) list += (list.empty() ? "" : ", ") + c;
        Error(e.typeArg.loc, "'" + e.typeArg.name + "' isn't a component scripts can get yet. Available: " + list);
        return;
    }
    if (e.object && e.object->kind != EK::This) {
        int typeId = -1;
        int r = Receiver(*e.object, typeId);
        if (r < 0) return;
        if (r != 0 || e.object->type.kind != TK::Object) { Error(e.loc, "GetComponent works on a GameObject or a component"); return; }
    } else {
        e.object.reset();
        if (RejectOutsideComponent("GetComponent", e.loc) || RejectInStatic("GetComponent", e.loc)) return;
        UsesSelf();
    }
    e.bind = Bind::GetComponent; e.slot = ti; e.type = Type::Obj(ti);
}

// xs.Count, d.Keys / d.Values, kv.Key / kv.Value.
void Checker::CheckCollectionMember(Expr& e, const Type& ot) {
    const Program::Generic& g = Gen(ot);
    std::vector<std::string> opts;
    if (ot.kind == TK::Pair) {
        if (e.name == "Key") { e.bind = Bind::PairKey; e.type = g.a; return; }
        if (e.name == "Value") { e.bind = Bind::PairValue; e.type = g.b; return; }
        opts = {"Key", "Value"};
    } else {
        if (e.name == "Count") { e.bind = Bind::CollCount; e.type = Type::Of(TK::Int); return; }
        opts = {"Count"};
        if (ot.kind == TK::Dict) {
            if (e.name == "Keys") { e.bind = Bind::DictKeys; e.type = prog_.GenericOf(TK::List, g.a); return; }
            if (e.name == "Values") { e.bind = Bind::DictValues; e.type = prog_.GenericOf(TK::List, g.b); return; }
            opts.push_back("Keys"); opts.push_back("Values");
        }
    }
    std::string hint = Suggest(e.name, opts);
    if (hint.empty() && ot.kind != TK::Pair && (e.name == "Length" || e.name == "Size" || e.name == "count")) hint = ". Did you mean 'Count'?";
    Error(e.loc, TypeName(ot) + " has no '" + e.name + "'" + hint);
}

// The LINQ-style methods of lists and arrays, and the List<T> methods that take
// a lambda. False when `e` isn't one of them (the plain List methods come next).
bool Checker::CheckLinq(Expr& e, const Type& ot, const Type& elem) {
    const bool list = ot.kind == TK::List;
    const std::string& n = e.name;
    const Type B = Type::Of(TK::Bool), I = Type::Of(TK::Int), F = Type::Of(TK::Float);
    const std::string who = n + "()";
    auto count = [&](std::size_t lo, std::size_t hi) {
        if (e.args.size() >= lo && e.args.size() <= hi) return true;
        Error(e.loc, who + " takes " + (lo == hi ? Values(lo) : std::to_string(lo) + " or " + Values(hi)) + ", not " + std::to_string(e.args.size()));
        return false;
    };
    auto pred = [&](std::size_t i) { Expect(*e.args[i], prog_.FuncOf({elem}, B), who + "'s condition"); };
    auto bind = [&](CollOp op, const Type& ret) { e.bind = Bind::CollMethod; e.member = (int)op; e.type = ret; return true; };
    if (n == "Where" || (list && n == "FindAll")) {
        if (count(1, 1)) pred(0);
        return bind(n == "Where" ? CollOp::Where : CollOp::FindAll, prog_.GenericOf(TK::List, elem));
    }
    if (n == "Select") {
        if (!count(1, 1)) return true;
        const Type f = InferLambda(*e.args[0], {elem}, who);
        if (f.kind == TK::Error) return true;
        const Type r = prog_.funcs[(std::size_t)f.id].ret;
        if (r.kind == TK::Void) { Error(e.args[0]->loc, "Select needs a lambda that gives a value, like x => x * 2"); return true; }
        return bind(CollOp::Select, prog_.GenericOf(TK::List, r));
    }
    if (n == "OrderBy" || n == "OrderByDescending") {
        if (!count(1, 1)) return true;
        const Type f = InferLambda(*e.args[0], {elem}, who);
        if (f.kind == TK::Error) return true;
        const Type k = prog_.funcs[(std::size_t)f.id].ret;
        if (!k.IsNumeric() && k.kind != TK::String && k.kind != TK::Enum) {
            Error(e.args[0]->loc, n + " needs a key it can sort (a number, text or an enum), like x => x.distance, not " + TypeName(k));
            return true;
        }
        return bind(n == "OrderBy" ? CollOp::OrderBy : CollOp::OrderByDescending, prog_.GenericOf(TK::List, elem));
    }
    static const std::pair<const char*, CollOp> picks[] = {
        {"First", CollOp::First}, {"FirstOrDefault", CollOp::FirstOrDefault}, {"Last", CollOp::Last}, {"LastOrDefault", CollOp::LastOrDefault}};
    for (auto& pk : picks)
        if (n == pk.first) {
            if (count(0, 1) && e.args.size() == 1) pred(0);
            return bind(pk.second, elem);
        }
    if (n == "Any" || n == "All" || n == "Count" || (list && n == "Exists")) {
        const bool needs = n == "All" || n == "Exists";
        if (count(needs ? 1 : 0, 1) && e.args.size() == 1) pred(0);
        const CollOp op = n == "Any" || n == "Exists" ? CollOp::Any : n == "All" ? CollOp::All : CollOp::Count;
        return bind(op, op == CollOp::Count ? I : B);
    }
    if (n == "Sum" || n == "Min" || n == "Max" || n == "Average") {
        if (!count(0, 1)) return true;
        Type v = elem;
        if (e.args.size() == 1) {
            const Type f = InferLambda(*e.args[0], {elem}, who);
            if (f.kind == TK::Error) return true;
            v = prog_.funcs[(std::size_t)f.id].ret;
        }
        if (!v.IsNumeric()) {
            Error(e.loc, who + " works on numbers" + (e.args.empty() ? ": give it the number to use, like xs." + n + "(x => x.score)" : ", not " + TypeName(v)));
            return true;
        }
        const CollOp op = n == "Sum" ? CollOp::Sum : n == "Min" ? CollOp::Min : n == "Max" ? CollOp::Max : CollOp::Average;
        return bind(op, op == CollOp::Average ? F : v);
    }
    if (n == "ToList") { count(0, 0); return bind(CollOp::ToList, prog_.GenericOf(TK::List, elem)); }
    if (!list && n == "ToArray") { count(0, 0); return bind(CollOp::ListToArray, ot); }
    if (!list && n == "Contains") { if (count(1, 1)) Expect(*e.args[0], elem, who); return bind(CollOp::ListContains, B); }
    if (!list) return false;
    if (n == "Find") { if (count(1, 1)) pred(0); return bind(CollOp::Find, elem); }
    if (n == "FindIndex") { if (count(1, 1)) pred(0); return bind(CollOp::FindIndex, I); }
    if (n == "RemoveAll") { if (count(1, 1)) pred(0); return bind(CollOp::RemoveAll, I); }
    if (n == "ForEach") { if (count(1, 1)) Expect(*e.args[0], prog_.FuncOf({elem}, Type::Of(TK::Void)), who); return bind(CollOp::ForEach, Type::Of(TK::Void)); }
    if (n == "Sort" && e.args.size() == 1) {   // xs.Sort((a, b) => a.score - b.score)
        Expect(*e.args[0], prog_.FuncOf({elem, elem}, I), "Sort's comparison");
        return bind(CollOp::SortBy, Type::Of(TK::Void));
    }
    return false;
}

// The methods of List<T> and Dictionary<K, V>, with the old runtime's names.
void Checker::CheckCollectionCall(Expr& e, const Type& ot) {
    const Program::Generic& g = Gen(ot);
    const Type T0 = g.a, V = g.b, I = Type::Of(TK::Int), B = Type::Of(TK::Bool), N = Type::Of(TK::Void);
    struct M { const char* name; CollOp op; std::vector<Type> params; Type ret; };
    std::vector<M> ms;
    if (ot.kind == TK::List) ms = {
        {"Add", CollOp::ListAdd, {T0}, N}, {"AddRange", CollOp::ListAddRange, {ot}, N}, {"Insert", CollOp::ListInsert, {I, T0}, N},
        {"Remove", CollOp::ListRemove, {T0}, B}, {"RemoveAt", CollOp::ListRemoveAt, {I}, N}, {"Clear", CollOp::ListClear, {}, N},
        {"Contains", CollOp::ListContains, {T0}, B}, {"IndexOf", CollOp::ListIndexOf, {T0}, I}, {"Sort", CollOp::ListSort, {}, N},
        {"Reverse", CollOp::ListReverse, {}, N}, {"ToArray", CollOp::ListToArray, {}, prog_.ArrayOf(T0)}};
    else ms = {
        {"Add", CollOp::DictAdd, {T0, V}, N}, {"TryAdd", CollOp::DictTryAdd, {T0, V}, B}, {"Remove", CollOp::DictRemove, {T0}, B},
        {"ContainsKey", CollOp::DictContainsKey, {T0}, B}, {"ContainsValue", CollOp::DictContainsValue, {V}, B},
        {"Clear", CollOp::DictClear, {}, N}, {"GetValueOrDefault", CollOp::DictGetValueOrDefault, {T0, V}, V}};
    for (const M& m : ms) {
        if (e.name != m.name) continue;
        std::size_t want = m.params.size();
        if (m.op == CollOp::DictGetValueOrDefault && e.args.size() == 1) want = 1;   // the default value is optional
        if (e.args.size() != want) {
            Error(e.loc, e.name + "() takes " + Values(want) + ", not " + std::to_string(e.args.size()));
            return;
        }
        for (std::size_t i = 0; i < want; ++i) Expect(*e.args[i], m.params[i], m.name);
        if (m.op == CollOp::ListSort && !T0.IsNumeric() && T0.kind != TK::String && T0.kind != TK::Enum) {
            Error(e.loc, "Sort() works on numbers, text and enums, not " + TypeName(T0));
            return;
        }
        e.bind = Bind::CollMethod; e.member = (int)m.op; e.type = m.ret;
        return;
    }
    std::vector<std::string> opts;
    for (const M& m : ms) opts.push_back(m.name);
    Error(e.loc, TypeName(ot) + " has no method '" + e.name + "'" + Suggest(e.name, opts));
}

// new List<int>() / new List<int> { 1, 2 } / new Dictionary<string, int>()
void Checker::CheckNewCollection(Expr& e) {
    const Type t = ResolveType(e.typeArg, false);
    if (t.kind == TK::Error) return;
    if (t.kind != TK::List && t.kind != TK::Dict) { Error(e.loc, "'new " + TypeName(t) + "' isn't something you can make"); return; }
    if (!e.hasInit && !e.args.empty()) { Error(e.loc, "new " + TypeName(t) + "() takes no values"); return; }
    if (e.hasInit && t.kind == TK::Dict) { Error(e.loc, "Fill a Dictionary with Add or d[key] = value"); return; }
    for (auto& a : e.args) Expect(*a, Gen(t).a, "This item");
    e.bind = Bind::NewCollection; e.type = t;
}

void Checker::CheckNew(Expr& e) {
    for (auto& a : e.args) CheckExpr(*a);
    if (!e.typeArg.args.empty() || e.typeArg.name == "List" || e.typeArg.name == "Dictionary") { CheckNewCollection(e); return; }
    if (e.hasInit) { Error(e.loc, "{ ... } after new only fills a List: set the fields after new " + e.typeArg.name + "()"); return; }
    if (int ci = ClassIndex(e.typeArg.name); ci >= 0) { CheckNewObject(e, ci); return; }
    if (e.typeArg.name != "Vector3") {
        if (api_.Find(e.typeArg.name) >= 0 || e.typeArg.name == "GameObject")
            Error(e.loc, "'new " + e.typeArg.name + "' isn't supported: only new Vector3(x, y, z) for now");
        else Error(e.loc, "Unknown type '" + e.typeArg.name + "'" + Suggest(e.typeArg.name, {"Vector3"}));
        return;
    }
    if (e.args.size() != 0 && e.args.size() != 2 && e.args.size() != 3) {
        Error(e.loc, "new Vector3 takes (x, y, z) or (x, y)"); return;
    }
    for (auto& a : e.args) Expect(*a, Type::Of(TK::Float), "A Vector3 part");
    e.type = Type::Of(TK::Vector3);
}

// new Node(...): the constructor whose parameters fit, or none (all fields at their starting values).
void Checker::CheckNewObject(Expr& e, int ci) {
    if (ci == prog_.component) { Error(e.loc, "Behaviour scripts are added to objects, not made with new"); return; }
    ClassDecl& k = prog_.classes[(std::size_t)ci];
    int ctor = -1;
    bool any = false;
    for (std::size_t i = 0; i < k.methods.size(); ++i)
        if (k.methods[i].isCtor) { any = true; if (k.methods[i].params.size() == e.args.size()) ctor = (int)i; }
    if (ctor < 0 && (any || !e.args.empty())) {
        Error(e.loc, k.name + " has no constructor that takes " + Values(e.args.size()));
        return;
    }
    if (ctor >= 0) {
        MethodDecl& m = k.methods[(std::size_t)ctor];
        if (!m.isPublic && &k != cls_) { Error(e.loc, k.name + "'s constructor is private: make it public to use it here"); return; }
        m.called = true;
        for (std::size_t i = 0; i < e.args.size(); ++i) Expect(*e.args[i], m.params[i].resolved, m.params[i].name);
    }
    e.bind = Bind::NewObject; e.slot = ci; e.member = ctor; e.type = Type{TK::Class, ci};
}

} // namespace checker_detail
