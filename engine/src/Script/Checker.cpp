// The checker: resolves every name, works out every type and reports anything
// that wouldn't run, or would run but do nothing. Only a program with no errors
// is executed.
#include "CheckerInternal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
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
    if (t.kind == TK::Class && t.id >= 0 && t.id < (int)classes.size()) return classes[(std::size_t)t.id].name;
    if ((t.kind == TK::List || t.kind == TK::Dict || t.kind == TK::Pair) && t.id >= 0 && t.id < (int)generics.size()) {
        const Generic& g = generics[(std::size_t)t.id];
        if (t.kind == TK::List) return "List<" + NameOf(g.a) + ">";
        return std::string(t.kind == TK::Dict ? "Dictionary<" : "KeyValuePair<") + NameOf(g.a) + ", " + NameOf(g.b) + ">";
    }
    if (t.kind == TK::Func && t.id >= 0 && t.id < (int)funcs.size()) {
        const FuncSig& f = funcs[(std::size_t)t.id];
        const bool action = f.ret.kind == TK::Void;
        std::string n = action ? "Action" : "Func";
        std::vector<std::string> parts;
        for (const Type& p : f.params) parts.push_back(NameOf(p));
        if (!action) parts.push_back(NameOf(f.ret));
        if (parts.empty()) return n;
        n += "<";
        for (std::size_t i = 0; i < parts.size(); ++i) n += (i ? ", " : "") + parts[i];
        return n + ">";
    }
    if (t.kind == TK::Lambda) return "a lambda";
    return TypeName(t);
}

Type Program::FuncOf(const std::vector<Type>& params, const Type& ret) {
    for (std::size_t i = 0; i < funcs.size(); ++i)
        if (funcs[i].params == params && funcs[i].ret == ret) return Type{TK::Func, (int)i};
    funcs.push_back({params, ret});
    return Type{TK::Func, (int)funcs.size() - 1};
}

Type Program::GenericOf(TK kind, const Type& a, const Type& b) {
    for (std::size_t i = 0; i < generics.size(); ++i)
        if (generics[i].kind == kind && generics[i].a == a && generics[i].b == b) return Type{kind, (int)i};
    generics.push_back({kind, a, b});
    return Type{kind, (int)generics.size() - 1};
}

int Program::MethodIndex(const std::string& name) const {
    const ClassDecl* c = Component();
    if (!c) return -1;
    for (std::size_t i = 0; i < c->methods.size(); ++i) if (c->methods[i].name == name) return (int)i;
    return -1;
}

namespace checker_detail {

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
        if (o == name) continue;   // never "did you mean" the very same name
        int d = EditDistance(name, o);
        if (d < bestD) { bestD = d; best = o; }
    }
    return (!best.empty() && bestD <= std::max(2, (int)name.size() / 3)) ? ". Did you mean '" + best + "'?" : "";
}

void Checker::Run() {
    if (prog_.classes.empty()) {
        if (!HasErrors()) Error({1, 1}, "A script needs a class: class Name : Behaviour { ... }");
        return;
    }
    // The Behaviour class is the component; any others are plain classes the script makes with new.
    int comp = -1;
    bool badBase = false;
    for (std::size_t i = 0; i < prog_.classes.size(); ++i) {
        ClassDecl& c = prog_.classes[i];
        if ((int)i != ClassIndex(c.name)) Error(c.loc, "'" + c.name + "' is declared twice");
        else if (api_.Find(c.name) >= 0) Error(c.loc, "'" + c.name + "' is already an engine type: pick another name");
        if (c.base == "Behaviour") {
            if (comp < 0) comp = (int)i;
            else Error(c.loc, "Only one Behaviour class per script: put '" + c.name + "' in its own script");
        } else if (!c.base.empty()) {
            badBase = true;
            if (ClassIndex(c.base) >= 0) Error(c.baseLoc, "Inheritance between classes isn't supported yet");
            else Error(c.baseLoc, "'" + c.base + "' isn't something a script can extend. Scripts extend Behaviour" + Suggest(c.base, {"Behaviour"}));
            if (comp < 0 && ClassIndex(c.base) < 0) comp = (int)i;   // check it as the component anyway
        }
    }
    if (comp < 0) {
        if (!badBase) Error(prog_.classes[0].loc, "A script class must extend Behaviour: class " + prog_.classes[0].name + " : Behaviour");
        comp = 0;
    }
    prog_.component = comp;
    cls_ = &prog_.classes[(std::size_t)comp];
    CheckEnums();

    // Every class's members first, so code can use fields and call methods in any order.
    for (ClassDecl& c : prog_.classes) { cls_ = &c; DeclareMembers(c); }
    for (ClassDecl& c : prog_.classes) {
        cls_ = &c;
        for (FieldDecl& f : c.fields) CheckField(f);
        for (MethodDecl& m : c.methods) CheckMethod(m);
    }

    // Methods nothing calls do nothing: lifecycle methods run by name and public
    // ones can be wired to UI events, everything else must be called somewhere.
    for (ClassDecl& c : prog_.classes) {
        cls_ = &c;
        for (MethodDecl& m : c.methods)
            if (!m.isCtor && m.lifecycle < 0 && !m.isPublic && !m.called) {
                std::string hint;
                if (InComponent())
                    for (const auto& l : LifecycleMethods())
                        if (EditDistance(m.name, l.name) <= 2) { hint = ". Did you mean " + std::string(l.name) + "? (names are case-sensitive)"; break; }
                Warning(m.loc, m.name + "() is never called, so it never runs" + hint);
            }
        for (FieldDecl& f : c.fields)
            if (!f.isPublic && !f.read && !HasAttr(f, "SerializeField"))
                Warning(f.loc, "'" + f.name + "' is never read, so it does nothing");
    }
    FlushLambdas();
    cls_ = &prog_.classes[(std::size_t)comp];
}

/// An engine binding's type in this program's terms (string[] gets the program's array type).
Type Checker::FromApi(const Type& t) { return t == Type::ApiStrings() ? prog_.ArrayOf(Type::Of(TK::String)) : t; }

bool Checker::HasErrors() const { for (auto& d : prog_.diags) if (d.severity == 0) return true; return false; }

int Checker::ClassIndex(const std::string& n) const {
    for (std::size_t i = 0; i < prog_.classes.size(); ++i) if (prog_.classes[i].name == n) return (int)i;
    return -1;
}

bool Checker::InComponent() const { return cls_ == &prog_.classes[(std::size_t)prog_.component]; }

int Checker::ClassId() const { return (int)(cls_ - prog_.classes.data()); }

// gameObject, transform, enabled and GetComponent belong to the object a Behaviour is on.
bool Checker::RejectOutsideComponent(const std::string& name, SrcLoc loc) {
    if (InComponent()) return false;
    Error(loc, "'" + name + "' only exists in the Behaviour class: " + cls_->name + " isn't on an object");
    return true;
}

std::string Checker::Values(std::size_t n) { return std::to_string(n) + (n == 1 ? " value" : " values"); }

void Checker::DeclareMembers(ClassDecl& c) {
    const bool comp = InComponent();
    std::set<std::string> names;
    std::set<std::size_t> ctorArgs;
    for (std::size_t i = 0; i < c.fields.size(); ++i) {
        FieldDecl& f = c.fields[i];
        f.index = (int)i;
        if (!names.insert(f.name).second) Error(f.loc, "'" + f.name + "' is declared twice");
        if (comp && IsSelfMemberName(f.name)) Error(f.loc, "'" + f.name + "' is already the object's own " + f.name + ": pick another name");
        if (!comp && f.isStatic) Error(f.loc, "static members only work in the Behaviour class for now");
        if (!comp && f.isConst) Error(f.loc, "const only works in the Behaviour class for now");
        f.resolved = ResolveType(f.type, false);
    }
    for (std::size_t i = 0; i < c.methods.size(); ++i) {
        MethodDecl& m = c.methods[i];
        for (auto& p : m.params) p.resolved = ResolveType(p.type, false);
        if (m.isCtor) {
            m.retType = Type::Of(TK::Void);
            if (comp) Error(m.loc, "A Behaviour script has no constructor: set things up in Awake() or Start()");
            else if (!ctorArgs.insert(m.params.size()).second)
                Error(m.loc, c.name + " already has a constructor that takes " + Values(m.params.size()));
            continue;
        }
        if (!comp && m.isStatic) Error(m.loc, "static members only work in the Behaviour class for now");
        if (!names.insert(m.name).second) Error(m.loc, "'" + m.name + "' is declared twice (methods can't share a name yet)");
        m.retType = m.ret.name == "void" ? Type::Of(TK::Void) : ResolveType(m.ret, false);
        if (comp) CheckLifecycleSignature(m, (int)i);
    }
}

// The method's parameters take the call's (checked) arguments.
bool Checker::ArgsFit(Expr& e, const MethodDecl& m) {
    if (e.args.size() != m.params.size()) {
        Error(e.loc, e.name + "() takes " + Values(m.params.size()) + ", not " + std::to_string(e.args.size()));
        return false;
    }
    for (std::size_t i = 0; i < e.args.size(); ++i) Expect(*e.args[i], m.params[i].resolved, m.params[i].name);
    return true;
}

void Checker::Error(SrcLoc l, const std::string& m) { prog_.diags.push_back({l.line, m, 0, l.col}); }

void Checker::Warning(SrcLoc l, const std::string& m) { prog_.diags.push_back({l.line, m, 1, l.col}); }

std::string Checker::TypeName(const Type& t) const { return prog_.NameOf(t); }

int Checker::EnumIndex(const std::string& n) const {
    for (std::size_t i = 0; i < prog_.enums.size(); ++i) if (prog_.enums[i].name == n) return (int)i;
    return -1;
}

// Every enum's members get their values, in order: an explicit value must be a
// fixed whole number; otherwise it is one more than the member before.
void Checker::CheckEnums() {
    enumReady_.assign(prog_.enums.size(), 0);
    for (std::size_t i = 0; i < prog_.enums.size(); ++i) {
        EnumDecl& en = prog_.enums[i];
        if ((int)i != EnumIndex(en.name)) Error(en.loc, "'" + en.name + "' is declared twice");
        else if (api_.Find(en.name) >= 0) Error(en.loc, "'" + en.name + "' is already an engine type: pick another name");
        else if (ClassIndex(en.name) >= 0) Error(en.loc, "'" + en.name + "' is already a class's name: pick another name");
        std::set<std::string> seen;
        std::int64_t next = 0;
        for (std::size_t k = 0; k < en.members.size(); ++k) {
            EnumMember& m = en.members[k];
            if (!seen.insert(m.name).second) Error(m.loc, "'" + m.name + "' is already in " + en.name);
            if (m.value) {
                inFieldInit_ = inEnumValue_ = true;
                CheckExpr(*m.value);
                inFieldInit_ = inEnumValue_ = false;
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

// ---- Fields ----
void Checker::CheckField(FieldDecl& f) {
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
        scopes_.clear();
        nextSlot_ = maxSlot_ = 0;
        CheckInit(*f.init, f.resolved, "The starting value of '" + f.name + "'");
        inFieldInit_ = false;
        FlushLambdas();
        if (f.isConst && !IsConstant(*f.init))
            Error(f.init->loc, "A const's value must be fixed (numbers, text, true/false), not worked out while playing");
    }
}

bool Checker::IsConstant(const Expr& e) {
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
void Checker::CheckMethod(MethodDecl& m) {
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
    FlushLambdas();
}

// Lambdas that ended up somewhere without a Func or Action type.
void Checker::FlushLambdas() {
    for (Expr* e : pending_) Error(e->loc, "A lambda only works where a Func or Action is expected");
    pending_.clear();
}

void Checker::PushScope() { scopes_.emplace_back(); }

void Checker::PopScope() {
    for (auto& v : scopes_.back())
        if (!v.used) Warning(v.loc, "'" + v.name + "' is never used, so it does nothing");
    nextSlot_ -= (int)scopes_.back().size();
    scopes_.pop_back();
}

LocalVar* Checker::FindIn(std::vector<std::vector<LocalVar>>& scopes, std::deque<LocalVar>& captures, const std::string& n) {
    for (auto s = scopes.rbegin(); s != scopes.rend(); ++s)
        for (auto& v : *s) if (v.name == n) return &v;
    for (auto& v : captures) if (v.name == n) return &v;
    return nullptr;
}

// A variable by name. Inside a lambda, one from the code around it becomes a
// capture of this lambda (and of every lambda in between).
LocalVar* Checker::FindLocal(const std::string& n) {
    if (LocalVar* v = FindIn(scopes_, captures_, n)) return v;
    if (outer_.empty()) return nullptr;
    LocalVar* v = FindAt(outer_.size() - 1, n);
    return v ? AddCapture(lambda_, captures_, *v) : nullptr;
}

LocalVar* Checker::FindAt(std::size_t level, const std::string& n) {
    Outer& o = outer_[level];
    if (LocalVar* v = FindIn(o.scopes, o.captures, n)) return v;
    if (level == 0) return nullptr;   // level 0 is the method itself
    LocalVar* v = FindAt(level - 1, n);
    return v ? AddCapture(o.lambda, o.captures, *v) : nullptr;
}

LocalVar* Checker::AddCapture(LambdaDecl* l, std::deque<LocalVar>& captures, LocalVar& from) {
    from.used = true;
    l->captureFrom.push_back(from.captured ? -1 - from.slot : from.slot);
    LocalVar c = from;
    c.slot = (int)captures.size();
    c.captured = true;
    captures.push_back(c);
    return &captures.back();
}

// Is `n` a variable here or in the code around this lambda? (Without capturing it.)
bool Checker::NameTaken(const std::string& n) {
    if (FindIn(scopes_, captures_, n)) return true;
    for (Outer& o : outer_) if (FindIn(o.scopes, o.captures, n)) return true;
    return false;
}

// The lambdas being checked use the object (its fields, methods or `this`).
void Checker::UsesSelf() {
    if (lambda_) lambda_->usesSelf = true;
    for (Outer& o : outer_) if (o.lambda) o.lambda->usesSelf = true;
}

int Checker::Declare(const std::string& n, Type t, SrcLoc loc) {
    if (NameTaken(n)) Error(loc, "'" + n + "' is already a variable here: pick another name");
    else if (FieldIndex(n) >= 0) Warning(loc, "'" + n + "' hides the field with the same name");
    scopes_.back().push_back({n, t, nextSlot_++, loc, false});
    maxSlot_ = std::max(maxSlot_, nextSlot_);
    return scopes_.back().back().slot;
}

int Checker::FieldIndex(const std::string& n) const {
    for (auto& f : cls_->fields) if (f.name == n) return f.index;
    return -1;
}

int Checker::MethodIdx(const std::string& n) const {
    for (std::size_t i = 0; i < cls_->methods.size(); ++i)
        if (!cls_->methods[i].isCtor && cls_->methods[i].name == n) return (int)i;   // constructors run through new
    return -1;
}

} // namespace checker_detail


void Check(Program& prog) {
    if (!prog.classes.empty() || prog.diags.empty()) {
        checker_detail::Checker c(prog);
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
