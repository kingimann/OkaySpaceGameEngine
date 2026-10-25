// The checker: resolves every name, works out every type and reports anything
// that wouldn't run, or would run but do nothing. Only a program with no errors
// is executed.
#include "ScriptInternal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>

namespace okay::script {

const std::vector<Lifecycle>& LifecycleMethods() {
    static const std::vector<Lifecycle> l = {
        {"Awake", "once, when the scene starts, before Start"},
        {"OnEnable", "once, after Awake, before Start"},
        {"Start", "once, before the first Update"},
        {"Update", "every frame"},
        {"LateUpdate", "every frame, after physics and every Update"},
        {"FixedUpdate", "50 times a second, however fast the game draws"},
        {"OnDisable", "when the object is destroyed, before OnDestroy"},
        {"OnDestroy", "when the object is destroyed"},
    };
    return l;
}

std::string TypeName(const Type& t) {
    switch (t.kind) {
        case TK::Void: return "void"; case TK::Null: return "null"; case TK::Bool: return "bool";
        case TK::Int: return "int"; case TK::Float: return "float"; case TK::String: return "string";
        case TK::Vector3: return "Vector3"; case TK::Any: return "any value";
        case TK::Enum: return "enum"; case TK::Array: return "array";
        case TK::Object: {
            const auto& api = EngineApi::Get();
            return t.id >= 0 && t.id < (int)api.types.size() ? api.types[(std::size_t)t.id].name : "object";
        }
        default: return "unknown";
    }
}

bool Program::ok() const {
    for (const auto& d : diags) if (d.severity == 0) return false;
    return component >= 0;
}

Type Program::ArrayOf(const Type& elem) {
    for (std::size_t i = 0; i < arrays.size(); ++i) if (arrays[i] == elem) return Type{TK::Array, (int)i};
    arrays.push_back(elem);
    return Type{TK::Array, (int)arrays.size() - 1};
}

std::string Program::NameOf(const Type& t) const {
    if (t.kind == TK::Enum && t.id >= 0 && t.id < (int)enums.size()) return enums[(std::size_t)t.id].name;
    if (t.kind == TK::Array && t.id >= 0 && t.id < (int)arrays.size()) return NameOf(arrays[(std::size_t)t.id]) + "[]";
    return TypeName(t);
}

int Program::MethodIndex(const std::string& name) const {
    const ClassDecl* c = Component();
    if (!c) return -1;
    for (std::size_t i = 0; i < c->methods.size(); ++i) if (c->methods[i].name == name) return (int)i;
    return -1;
}

namespace {

int EditDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            int sub = prev[j - 1] + (std::tolower((unsigned char)a[i - 1]) == std::tolower((unsigned char)b[j - 1]) ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}
std::string Suggest(const std::string& name, const std::vector<std::string>& options) {
    std::string best; int bestD = 99;
    for (const auto& o : options) {
        int d = EditDistance(name, o);
        if (d < bestD) { bestD = d; best = o; }
    }
    return (!best.empty() && bestD <= std::max(2, (int)name.size() / 3)) ? ". Did you mean '" + best + "'?" : "";
}

struct LocalVar { std::string name; Type type; int slot; SrcLoc loc; bool used = false; bool readOnly = false; };

class Checker {
public:
    explicit Checker(Program& p) : prog_(p), api_(EngineApi::Get()) {}

    void Run() {
        if (prog_.classes.empty()) {
            if (!HasErrors()) Error({1, 1}, "A script needs a class: class Name : Behaviour { ... }");
            return;
        }
        for (std::size_t i = 1; i < prog_.classes.size(); ++i)
            Error(prog_.classes[i].loc, "Only one class per script for now: put '" + prog_.classes[i].name + "' in its own script");
        ClassDecl& c = prog_.classes[0];
        cls_ = &c;
        CheckEnums();
        if (c.base.empty()) Error(c.loc, "A script class must extend Behaviour: class " + c.name + " : Behaviour");
        else if (c.base != "Behaviour")
            Error(c.baseLoc, "'" + c.base + "' isn't something a script can extend. Scripts extend Behaviour" + Suggest(c.base, {"Behaviour"}));
        prog_.component = 0;

        // Members first, so methods can use fields and call each other in any order.
        std::set<std::string> names;
        for (std::size_t i = 0; i < c.fields.size(); ++i) {
            FieldDecl& f = c.fields[i];
            f.index = (int)i;
            if (!names.insert(f.name).second) Error(f.loc, "'" + f.name + "' is declared twice");
            if (IsSelfMemberName(f.name)) Error(f.loc, "'" + f.name + "' is already the object's own " + f.name + ": pick another name");
            f.resolved = ResolveType(f.type, false);
        }
        for (std::size_t i = 0; i < c.methods.size(); ++i) {
            MethodDecl& m = c.methods[i];
            if (!names.insert(m.name).second) Error(m.loc, "'" + m.name + "' is declared twice (methods can't share a name yet)");
            m.retType = m.ret.name == "void" ? Type::Of(TK::Void) : ResolveType(m.ret, false);
            for (auto& p : m.params) p.resolved = ResolveType(p.type, false);
            CheckLifecycleSignature(m, (int)i);
        }
        for (FieldDecl& f : c.fields) CheckField(f);
        for (std::size_t i = 0; i < c.methods.size(); ++i) CheckMethod(c.methods[i]);

        // Methods nothing calls do nothing: lifecycle methods run by name and public
        // ones can be wired to UI events, everything else must be called somewhere.
        for (MethodDecl& m : c.methods)
            if (m.lifecycle < 0 && !m.isPublic && !m.called) {
                std::string hint;
                for (const auto& l : LifecycleMethods())
                    if (EditDistance(m.name, l.name) <= 2) { hint = ". Did you mean " + std::string(l.name) + "? (names are case-sensitive)"; break; }
                Warning(m.loc, m.name + "() is never called, so it never runs" + hint);
            }
        for (FieldDecl& f : c.fields)
            if (!f.isPublic && !f.read && !HasAttr(f, "SerializeField"))
                Warning(f.loc, "'" + f.name + "' is never read, so it does nothing");
    }

private:
    Program& prog_;
    const EngineApi& api_;
    ClassDecl* cls_ = nullptr;
    MethodDecl* method_ = nullptr;
    std::vector<std::vector<LocalVar>> scopes_;
    int nextSlot_ = 0, maxSlot_ = 0, loopDepth_ = 0, switchDepth_ = 0;
    bool inFieldInit_ = false;
    std::vector<int> enumReady_;   ///< per enum: how many members have their value worked out

    bool HasErrors() const { for (auto& d : prog_.diags) if (d.severity == 0) return true; return false; }
    void Error(SrcLoc l, const std::string& m) { prog_.diags.push_back({l.line, m, 0, l.col}); }
    void Warning(SrcLoc l, const std::string& m) { prog_.diags.push_back({l.line, m, 1, l.col}); }
    std::string TypeName(const Type& t) const { return prog_.NameOf(t); }

    int EnumIndex(const std::string& n) const {
        for (std::size_t i = 0; i < prog_.enums.size(); ++i) if (prog_.enums[i].name == n) return (int)i;
        return -1;
    }

    // Every enum's members get their values, in order: an explicit value must be a
    // fixed whole number; otherwise it is one more than the member before.
    void CheckEnums() {
        enumReady_.assign(prog_.enums.size(), 0);
        for (std::size_t i = 0; i < prog_.enums.size(); ++i) {
            EnumDecl& en = prog_.enums[i];
            if ((int)i != EnumIndex(en.name)) Error(en.loc, "'" + en.name + "' is declared twice");
            else if (api_.Find(en.name) >= 0) Error(en.loc, "'" + en.name + "' is already an engine type: pick another name");
            else if (en.name == cls_->name) Error(en.loc, "'" + en.name + "' is already the class's name: pick another name");
            std::set<std::string> seen;
            std::int64_t next = 0;
            for (std::size_t k = 0; k < en.members.size(); ++k) {
                EnumMember& m = en.members[k];
                if (!seen.insert(m.name).second) Error(m.loc, "'" + m.name + "' is already in " + en.name);
                if (m.value) {
                    inFieldInit_ = true;
                    CheckExpr(*m.value);
                    inFieldInit_ = false;
                    std::optional<Value> v;
                    const TK k2 = m.value->type.kind;
                    if (k2 == TK::Int || k2 == TK::Enum) v = Fold(*m.value);
                    if (auto* iv = v ? std::get_if<std::int32_t>(&*v) : nullptr) next = *iv;
                    else if (k2 != TK::Error) Error(m.value->loc, "An enum value must be a whole number that's fixed, like " + m.name + " = 3");
                }
                if (next > 2147483647LL) { Error(m.loc, "'" + m.name + "' is too big for an int"); next = 0; }
                m.v = (std::int32_t)next;
                ++next;
                enumReady_[i] = (int)k + 1;
            }
        }
    }

    // The value of a constant expression (literals, const fields, enum values and
    // operators on them), or nothing if it is worked out while playing.
    std::optional<Value> Fold(const Expr& e) {
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
    std::string ConstText(const Value& v, const Type& t) const {
        if (t.kind == TK::Enum) {
            const EnumDecl& en = prog_.enums[(std::size_t)t.id];
            if (auto* i = std::get_if<std::int32_t>(&v)) if (const std::string* n = en.NameOf(*i)) return en.name + "." + *n;
        }
        if (auto* s = std::get_if<std::string>(&v)) return "\"" + *s + "\"";
        return ValueToString(v);
    }

    static bool IsSelfMemberName(const std::string& n) { return n == "gameObject" || n == "transform" || n == "enabled"; }
    static bool HasAttr(const FieldDecl& f, const char* n) {
        for (auto& a : f.attrs) if (a.name == n) return true;
        return false;
    }

    Type ResolveType(const TypeRef& t, bool allowVar) {
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
        if (t.name == "int") return Type::Of(TK::Int);
        if (t.name == "float") return Type::Of(TK::Float);
        if (t.name == "bool") return Type::Of(TK::Bool);
        if (t.name == "string") return Type::Of(TK::String);
        if (t.name == "Vector3") return Type::Of(TK::Vector3);
        if (t.name == "var" && allowVar) return Type{};   // inferred by the caller
        if (t.name == "double") { Error(t.loc, "Use float for decimal numbers (double isn't supported)"); return Type{}; }
        if (t.name == "void") { Error(t.loc, "void isn't a type a value can have"); return Type{}; }
        if (int ei = EnumIndex(t.name); ei >= 0) return Type{TK::Enum, ei};
        int id = api_.Find(t.name);
        if (id >= 0 && !api_.types[(std::size_t)id].isStatic && !api_.types[(std::size_t)id].isValueType) return Type::Obj(id);
        if (id >= 0) { Error(t.loc, t.name + " can't hold a value: use its members, like " + t.name + ".something"); return Type{}; }
        std::vector<std::string> opts = {"int", "float", "bool", "string", "Vector3"};
        for (auto& at : api_.types) if (!at.isStatic && !at.isValueType) opts.push_back(at.name);
        for (auto& en : prog_.enums) opts.push_back(en.name);
        Error(t.loc, "Unknown type '" + t.name + "'" + Suggest(t.name, opts));
        return Type{};
    }

    void CheckLifecycleSignature(MethodDecl& m, int) {
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
    static bool Convertible(const Type& from, const Type& to) {
        if (from.kind == TK::Error || to.kind == TK::Error) return true;   // already reported
        if (to.kind == TK::Any) return from.kind != TK::Void;
        if (from == to) return true;
        if (from.kind == TK::Int && to.kind == TK::Float) return true;
        if (from.kind == TK::Null && (to.kind == TK::Object || to.kind == TK::Array)) return true;
        return false;
    }
    std::string ConvertError(const Type& from, const Type& to, const std::string& what) {
        if (from.kind == TK::Void) return what + " doesn't produce a value";
        if (from.kind == TK::Float && to.kind == TK::Int)
            return what + " is a float but needs an int: write (int)(...) to drop the decimals";
        if (to.kind == TK::String) return what + " is " + TypeName(from) + ", not text: join it into text with \"\" + value";
        return what + " is " + TypeName(from) + " but needs " + TypeName(to);
    }
    // Check `e` can be used as `to`; mark int->float promotion.
    void Expect(Expr& e, const Type& to, const std::string& what) {
        if (!Convertible(e.type, to)) { Error(e.loc, ConvertError(e.type, to, what)); return; }
        if (e.type.kind == TK::Int && to.kind == TK::Float) e.toFloat = true;
        if (e.type.kind == TK::Enum && to.kind == TK::Any) e.enumStr = e.type.id;
    }
    // Check a starting value, which may be a { ... } list when `to` is an array.
    void CheckInit(Expr& init, const Type& to, const std::string& what) {
        if (init.kind != EK::ArrayLit) { CheckExpr(init); Expect(init, to, what); return; }
        if (to.kind == TK::Error) { for (auto& a : init.args) CheckExpr(*a); return; }
        if (to.kind != TK::Array) {
            for (auto& a : init.args) CheckExpr(*a);
            Error(init.loc, "{ ... } makes an array, but " + what + " is " + TypeName(to));
            return;
        }
        CheckItems(init, to);
    }
    void CheckItems(Expr& e, const Type& arr) {
        const Type elem = prog_.arrays[(std::size_t)arr.id];
        for (auto& a : e.args) { CheckExpr(*a); Expect(*a, elem, "This item"); }
        e.type = arr;
    }

    // ---- Fields ----
    void CheckField(FieldDecl& f) {
        for (Attribute& a : f.attrs) {
            if (a.name == "Header" || a.name == "Tooltip") {
                if (a.args.size() != 1 || a.args[0]->kind != EK::StringLit)
                    Error(a.loc, "[" + a.name + "(\"text\")] needs one piece of text");
            } else if (a.name == "Range") {
                if (a.args.size() != 2) Error(a.loc, "[Range(min, max)] needs two numbers");
                if (!f.resolved.IsNumeric() && f.resolved.kind != TK::Error) Error(a.loc, "[Range] only works on int or float fields");
            } else if (a.name == "SerializeField") {
                if (f.isPublic) Error(a.loc, "[SerializeField] does nothing on a public field (it's already in the Inspector). Delete it");
            } else {
                Error(a.loc, "[" + a.name + "] isn't an attribute OkayScript uses. Fields can have [Header], [Tooltip], [Range] and [SerializeField]");
                continue;
            }
            if (a.name != "SerializeField" && !f.isPublic && !HasAttr(f, "SerializeField"))
                Error(a.loc, "[" + a.name + "] only shows in the Inspector for public fields: make '" + f.name + "' public or delete it");
        }
        if (f.isConst && f.isPublic) Error(f.loc, "A const can't be public: the Inspector can't change it. Remove `public`");
        if (f.init) {
            inFieldInit_ = true;
            CheckInit(*f.init, f.resolved, "The starting value of '" + f.name + "'");
            inFieldInit_ = false;
            if (f.isConst && !IsConstant(*f.init))
                Error(f.init->loc, "A const's value must be fixed (numbers, text, true/false), not worked out while playing");
        }
    }
    static bool IsConstant(const Expr& e) {
        switch (e.kind) {
            case EK::IntLit: case EK::FloatLit: case EK::StringLit: case EK::BoolLit: return true;
            case EK::Unary: return IsConstant(*e.lhs);
            case EK::Binary: return IsConstant(*e.lhs) && IsConstant(*e.rhs);
            case EK::Cast: return IsConstant(*e.lhs);
            case EK::Member: return e.bind == Bind::EnumValue;
            default: return false;
        }
    }

    // ---- Methods ----
    void CheckMethod(MethodDecl& m) {
        method_ = &m;
        scopes_.clear();
        scopes_.emplace_back();
        nextSlot_ = 0; maxSlot_ = 0; loopDepth_ = 0; switchDepth_ = 0;
        for (Param& p : m.params) {
            for (auto& v : scopes_.back()) if (v.name == p.name) Error(p.loc, "Two parameters are called '" + p.name + "'");
            scopes_.back().push_back({p.name, p.resolved, nextSlot_++, p.loc, true});
        }
        maxSlot_ = nextSlot_;
        bool completes = CheckBlockBody(*m.body);
        if (completes && m.retType.kind != TK::Void && m.retType.kind != TK::Error)
            Error(m.loc, m.name + "() must return a " + TypeName(m.retType) + " on every path");
        PopScope();
        m.localCount = maxSlot_;
        method_ = nullptr;
    }

    void PushScope() { scopes_.emplace_back(); }
    void PopScope() {
        for (auto& v : scopes_.back())
            if (!v.used) Warning(v.loc, "'" + v.name + "' is never used, so it does nothing");
        nextSlot_ -= (int)scopes_.back().size();
        scopes_.pop_back();
    }
    LocalVar* FindLocal(const std::string& n) {
        for (auto s = scopes_.rbegin(); s != scopes_.rend(); ++s)
            for (auto& v : *s) if (v.name == n) return &v;
        return nullptr;
    }
    int Declare(const std::string& n, Type t, SrcLoc loc) {
        if (FindLocal(n)) Error(loc, "'" + n + "' is already a variable here: pick another name");
        else if (FieldIndex(n) >= 0) Warning(loc, "'" + n + "' hides the field with the same name");
        scopes_.back().push_back({n, t, nextSlot_++, loc, false});
        maxSlot_ = std::max(maxSlot_, nextSlot_);
        return scopes_.back().back().slot;
    }
    int FieldIndex(const std::string& n) const {
        for (auto& f : cls_->fields) if (f.name == n) return f.index;
        return -1;
    }
    int MethodIdx(const std::string& n) const {
        for (std::size_t i = 0; i < cls_->methods.size(); ++i) if (cls_->methods[i].name == n) return (int)i;
        return -1;
    }

    // Statements; return whether execution can reach the end.
    bool CheckBlockBody(Stmt& b) { return CheckStmts(b.body); }
    bool CheckStmts(std::vector<StmtPtr>& body) {
        bool completes = true, warned = false;
        for (auto& s : body) {
            if (!completes && !warned) { Warning(s->loc, "This can never run: it comes after a return, break or continue"); warned = true; }
            if (!CheckStmt(*s)) completes = false;
        }
        return completes;
    }
    bool CheckStmt(Stmt& s) {
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
                        if (d.type.name == "var") {
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
                const Type& rt = method_->retType;
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
                if (s.expr->type.kind == TK::Array) elem = prog_.arrays[(std::size_t)s.expr->type.id];
                else if (s.expr->type.kind != TK::Error) Error(s.expr->loc, "foreach needs an array, not " + TypeName(s.expr->type));
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

    // C#'s rules: the value is an int, string or enum; every case is a
    // fixed value of that type, none twice; one default at most; and no section
    // runs into the next (each ends in break, return or continue).
    bool CheckSwitch(Stmt& s) {
        CheckExpr(*s.expr);
        const Type t = s.expr->type;
        const bool okType = t.kind == TK::Int || t.kind == TK::String || t.kind == TK::Enum;
        if (!okType && t.kind != TK::Error)
            Error(s.expr->loc, "switch works on an int, a string or an enum, not " + TypeName(t) +
                               (t.kind == TK::Float || t.kind == TK::Bool ? ": use if / else" : ""));
        bool haveDefault = false, anyBreak = false;
        std::vector<Value> seen;
        ++switchDepth_;
        PushScope();
        for (SwitchSection& sec : s.sections) {
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
        }
        PopScope();
        --switchDepth_;
        return !haveDefault || anyBreak;
    }
    static bool IsTrueLiteral(const Expr& e) { return e.kind == EK::BoolLit && e.bval; }
    static bool ContainsBreak(const Stmt& s) {
        switch (s.kind) {
            case SK::Break: return true;
            case SK::Block: for (auto& c : s.body) if (ContainsBreak(*c)) return true; return false;
            case SK::If: return ContainsBreak(*s.then) || (s.otherwise && ContainsBreak(*s.otherwise));
            default: return false;   // a nested loop's break is its own
        }
    }
    void CheckCondition(Expr& e, const char* what) {
        CheckExpr(e);
        if (e.type.kind != TK::Bool && e.type.kind != TK::Error)
            Error(e.loc, std::string("The ") + what + " condition must be true/false, not " + TypeName(e.type) +
                         (e.type.IsNumeric() ? " (compare it, e.g. x > 0)" : e.type.kind == TK::Object ? " (compare it, e.g. x != null)" : ""));
    }

    // ---- Expressions ----
    std::vector<std::string> VisibleNames() {
        std::vector<std::string> n;
        for (auto& s : scopes_) for (auto& v : s) n.push_back(v.name);
        for (auto& f : cls_->fields) n.push_back(f.name);
        for (auto& m : cls_->methods) n.push_back(m.name);
        for (auto& t : api_.types) n.push_back(t.name);
        n.push_back("gameObject"); n.push_back("transform"); n.push_back("enabled");
        return n;
    }

    void CheckExpr(Expr& e) {
        switch (e.kind) {
            case EK::IntLit: e.type = Type::Of(TK::Int); return;
            case EK::FloatLit: e.type = Type::Of(TK::Float); return;
            case EK::StringLit: e.type = Type::Of(TK::String); return;
            case EK::BoolLit: e.type = Type::Of(TK::Bool); return;
            case EK::NullLit: e.type = Type::Of(TK::Null); return;
            case EK::This: Error(e.loc, "`this` on its own isn't a value here: use this.field or this.Method()"); return;
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
        }
    }

    // new[] { ... }: every item has the same type (ints mixed with floats make floats).
    void CheckImplicitArray(Expr& e) {
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

    void CheckIndex(Expr& e) {
        CheckExpr(*e.object);
        CheckExpr(*e.lhs);
        const Type at = e.object->type;
        if (at.kind == TK::Error) return;
        if (at.kind != TK::Array) { Error(e.loc, TypeName(at) + " values can't be indexed with [ ]"); return; }
        if (e.lhs->type.kind != TK::Int && e.lhs->type.kind != TK::Error)
            Error(e.lhs->loc, "An array index must be an int, not " + TypeName(e.lhs->type));
        e.type = prog_.arrays[(std::size_t)at.id];
    }

    void CheckNewArray(Expr& e) {
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
    bool RejectInStatic(const std::string& name, SrcLoc loc) {
        if (!method_ || !method_->isStatic) return false;
        Error(loc, "'" + name + "' belongs to each object, so the static method " + method_->name + "() can't use it");
        return true;
    }
    // `T` in T.member, when T is the script's class (and nothing local hides it).
    bool IsOwnClass(const Expr& e) {
        return e.kind == EK::Name && e.name == cls_->name && !FindLocal(e.name) && FieldIndex(e.name) < 0;
    }

    void CheckName(Expr& e) {
        if (LocalVar* v = FindLocal(e.name)) {
            if (inFieldInit_) { Error(e.loc, "Unknown name '" + e.name + "'"); return; }
            v->used = true; e.bind = Bind::Local; e.slot = v->slot; e.type = v->type; return;
        }
        int fi = FieldIndex(e.name);
        if (fi >= 0) {
            if (inFieldInit_) { Error(e.loc, "A field's starting value can't use another field: set '" + e.name + "' up in Start()"); return; }
            FieldDecl& f = cls_->fields[(std::size_t)fi];
            if (!f.isStatic && !f.isConst && RejectInStatic(e.name, e.loc)) return;
            f.read = true;
            e.bind = f.isStatic ? Bind::StaticField : Bind::Field; e.slot = fi; e.type = f.resolved; return;
        }
        if (IsSelfMemberName(e.name)) {
            if (inFieldInit_) { Error(e.loc, "A field's starting value can't use '" + e.name + "' (the object isn't ready yet): set it in Start()"); return; }
            if (RejectInStatic(e.name, e.loc)) return;
            e.bind = Bind::SelfMember;
            e.slot = e.name == "gameObject" ? 0 : e.name == "transform" ? 1 : 2;
            e.type = e.slot == 0 ? Type::Obj(api_.gameObject) : e.slot == 1 ? Type::Obj(api_.transform) : Type::Of(TK::Bool);
            return;
        }
        if (MethodIdx(e.name) >= 0) { Error(e.loc, "'" + e.name + "' is a method: call it with " + e.name + "()"); return; }
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
    int Receiver(Expr& obj, int& typeId) {
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

    template <typename L>
    std::vector<std::string> Names(const std::vector<L>& v) { std::vector<std::string> n; for (auto& x : v) n.push_back(x.name); return n; }

    void CheckMember(Expr& e) {
        int typeId = -1;
        int r = Receiver(*e.object, typeId);
        if (r < 0 || BadNullCond(e, r)) return;
        if (r == 4) {
            int fi = FieldIndex(e.name);
            const FieldDecl* f = fi >= 0 ? &cls_->fields[(std::size_t)fi] : nullptr;
            if (f && !f->isStatic && !f->isConst) { Error(e.loc, "'" + e.name + "' isn't static: use it without " + cls_->name + "."); return; }
            if (!f && MethodIdx(e.name) < 0) { Error(e.loc, cls_->name + " has no '" + e.name + "'"); return; }
        }
        if (r == 2 || r == 4) {   // this.x, T.x
            Expr tmp; tmp.kind = EK::Name; tmp.name = e.name; tmp.loc = e.loc;
            CheckName(tmp);
            e.bind = tmp.bind; e.slot = tmp.slot; e.type = tmp.type;
            e.object.reset();
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
                if (t.staticProps[i].name == e.name) { e.bind = Bind::ApiStaticProp; e.slot = typeId; e.member = (int)i; e.type = t.staticProps[i].type; return; }
            for (auto& m : t.staticMethods)
                if (m.name == e.name) { Error(e.loc, t.name + "." + e.name + " is a method: call it with " + e.name + "(...)"); return; }
            std::vector<std::string> opts = Names(t.staticProps);
            for (auto& m : t.staticMethods) opts.push_back(m.name);
            Error(e.loc, t.name + " has no '" + e.name + "'" + Suggest(e.name, opts));
            return;
        }
        const Type& ot = e.object->type;
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
        if (ot.kind == TK::String && e.name == "Length") { Error(e.loc, "Text length isn't supported yet"); return; }
        Error(e.loc, TypeName(ot) + " values have no members");
    }

    // Pick the overload whose parameters fit the (already checked) arguments.
    const ApiMethod* PickOverload(const std::vector<ApiMethod>& ms, const std::string& owner, Expr& e) {
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
    void BindArgs(Expr& e, const ApiMethod& m) {
        for (std::size_t i = 0; i < e.args.size(); ++i) {
            Expect(*e.args[i], m.params[i], m.paramNames[i]);
            if (m.checkLiteral && e.args[i]->kind == EK::StringLit) {
                std::string msg = m.checkLiteral((int)i, e.args[i]->sval);
                if (!msg.empty()) Error(e.args[i]->loc, msg);
            }
        }
    }

    void CheckCall(Expr& e) {
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
            if (e.args.size() != m.params.size()) {
                Error(e.loc, e.name + "() takes " + std::to_string(m.params.size()) + " value" + (m.params.size() == 1 ? "" : "s") +
                             ", not " + std::to_string(e.args.size()));
                return;
            }
            for (std::size_t i = 0; i < e.args.size(); ++i) Expect(*e.args[i], m.params[i].resolved, m.params[i].name);
            e.bind = Bind::Method; e.slot = mi; e.type = m.retType;
            return;
        }
        int typeId = -1;
        int r = Receiver(*e.object, typeId);
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
            e.bind = Bind::ApiStaticMethod; e.slot = typeId; e.member = (int)(m - t.staticMethods.data()); e.type = m->ret;
            return;
        }
        const Type& ot = e.object->type;
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

    void CheckGetComponent(Expr& e) {
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
            if (RejectInStatic("GetComponent", e.loc)) return;
        }
        e.bind = Bind::GetComponent; e.slot = ti; e.type = Type::Obj(ti);
    }

    void CheckNew(Expr& e) {
        for (auto& a : e.args) CheckExpr(*a);
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

    void CheckUnary(Expr& e) {
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

    static bool CanBeNull(const Type& t) { return t.kind == TK::Object || t.kind == TK::Array; }
    static std::string Article(const std::string& n) { return (std::string("aeiouAEIOU").find(n[0]) != std::string::npos ? "an " : "a ") + n; }
    // ?. only on something that might be null: an object or an array.
    bool BadNullCond(Expr& e, int receiverKind) {
        if (!e.nullCond || (receiverKind == 0 && CanBeNull(e.object->type))) return false;
        Error(e.loc, "?. only works on objects and arrays, which can be null: use . here");
        return true;
    }

    void CheckBinary(Expr& e) {
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
                if ((a.kind == TK::Array || a.kind == TK::Null) && (b.kind == TK::Array || b.kind == TK::Null) && a.kind != b.kind) return;
                if (a.kind == TK::Array && a == b) return;
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
    bool Assignable(Expr& e) {
        if (e.kind == EK::Index) return true;
        switch (e.bind) {
            case Bind::StaticField: return true;
            case Bind::Local:
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
                if (o.bind == Bind::Local || o.bind == Bind::Field || o.bind == Bind::VecField || o.kind == EK::Index) return Assignable(o);
                Error(e.loc, "This changes a copy, so it would do nothing: copy the vector into a variable, change ." +
                             e.name + ", then assign it back (e.g. var p = transform.position; p." + e.name + " = 1; transform.position = p;)");
                return false;
            }
            default:
                Error(e.loc, "This can't be assigned to");
                return false;
        }
    }

    void CheckAssign(Expr& e) {
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

    void CheckIncDec(Expr& e) {
        CheckExpr(*e.lhs);
        if (e.lhs->type.kind == TK::Error) return;
        if (!e.lhs->type.IsNumeric()) { Error(e.loc, std::string(TokenName(e.op)) + " needs a number, not " + TypeName(e.lhs->type)); return; }
        if (!Assignable(*e.lhs)) return;
        e.type = e.lhs->type;
    }

    void CheckCast(Expr& e) {
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

    void CheckConditional(Expr& e) {
        CheckCondition(*e.third, "?:");
        CheckExpr(*e.lhs);
        CheckExpr(*e.rhs);
        const Type a = e.lhs->type, b = e.rhs->type;
        if (a.kind == TK::Error || b.kind == TK::Error) return;
        if (a == b) { e.type = a; return; }
        if (a.IsNumeric() && b.IsNumeric()) { e.type = Type::Of(TK::Float); e.lhs->toFloat = a.kind == TK::Int; e.rhs->toFloat = b.kind == TK::Int; return; }
        if ((a.kind == TK::Object || a.kind == TK::Array) && b.kind == TK::Null) { e.type = a; return; }
        if (a.kind == TK::Null && (b.kind == TK::Object || b.kind == TK::Array)) { e.type = b; return; }
        Error(e.loc, "Both sides of ?: must be the same type (" + TypeName(a) + " vs " + TypeName(b) + ")");
    }
};

} // namespace

void Check(Program& prog) {
    if (!prog.classes.empty() || prog.diags.empty()) {
        Checker c(prog);
        c.Run();
    }
    std::stable_sort(prog.diags.begin(), prog.diags.end(),
                     [](const ScriptDiagnostic& a, const ScriptDiagnostic& b) { return a.line < b.line; });
}

std::shared_ptr<Program> Compile(const std::string& src) {
    auto p = std::make_shared<Program>();
    std::vector<Token> toks = Lex(src, p->diags);
    Parse(toks, *p);
    Check(*p);
    return p;
}

} // namespace okay::script
