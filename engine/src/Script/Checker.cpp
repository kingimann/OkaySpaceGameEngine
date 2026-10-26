// The checker: resolves every name, works out every type and reports anything
// that wouldn't run, or would run but do nothing. Only a program with no errors
// is executed.
#include "ScriptInternal.hpp"
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
        if (o == name) continue;   // never "did you mean" the very same name
        int d = EditDistance(name, o);
        if (d < bestD) { bestD = d; best = o; }
    }
    return (!best.empty() && bestD <= std::max(2, (int)name.size() / 3)) ? ". Did you mean '" + best + "'?" : "";
}

struct LocalVar {
    std::string name; Type type; int slot; SrcLoc loc; bool used = false; bool readOnly = false;
    bool captured = false;   ///< a lambda's copy of a variable from the code around it: slot = capture index
};

class Checker {
public:
    explicit Checker(Program& p) : prog_(p), api_(EngineApi::Get()) {}

    void Run() {
        if (prog_.classes.empty()) {
            if (!HasErrors()) Error({1, 1}, "A script needs a class: class Name : Behaviour { ... }");
            return;
        }
        // The Behaviour class is the component; any others are plain classes (and
        // interfaces) the script makes with new.
        int comp = -1;
        bool badBase = false;
        std::vector<std::string> extendable = {"Behaviour"};
        for (auto& k : prog_.classes) extendable.push_back(k.name);
        for (std::size_t i = 0; i < prog_.classes.size(); ++i) {
            ClassDecl& c = prog_.classes[i];
            if ((int)i != ClassIndex(c.name)) Error(c.loc, "'" + c.name + "' is declared twice");
            else if (api_.Find(c.name) >= 0) Error(c.loc, "'" + c.name + "' is already an engine type: pick another name");
            for (std::size_t b = 0; b < c.bases.size(); ++b) {
                const std::string& n = c.bases[b];
                const SrcLoc l = c.baseLocs[b];
                if (n == "Behaviour") {
                    if (c.isInterface || c.isAbstract) Error(l, std::string(c.isInterface ? "An interface" : "An abstract class") + " can't extend Behaviour");
                    else if (b != 0) Error(l, "Behaviour comes first: class " + c.name + " : Behaviour");
                    else if (comp < 0) comp = (int)i;
                    else Error(c.loc, "Only one Behaviour class per script: put '" + c.name + "' in its own script");
                    continue;
                }
                const int bi = ClassIndex(n);
                if (bi < 0) {
                    badBase = true;
                    Error(l, "'" + n + "' isn't something a script can extend. Scripts extend Behaviour, a class or an interface" + Suggest(n, extendable));
                    if (comp < 0 && b == 0 && !c.isInterface) comp = (int)i;   // check it as the component anyway
                    continue;
                }
                const ClassDecl& k = prog_.classes[(std::size_t)bi];
                if (bi == (int)i) Error(l, "A class can't extend itself");
                else if (k.isInterface) c.interfaces.push_back(bi);
                else if (c.isInterface) Error(l, "An interface can only extend interfaces: " + n + " is a class");
                else if (b != 0) Error(l, "A class extends one class, named first: " + n + " is a class");
                else c.baseClass = bi;
            }
        }
        if (comp < 0) {
            if (!badBase) {
                int first = 0;   // the first class that isn't an interface
                while (first + 1 < (int)prog_.classes.size() && prog_.classes[(std::size_t)first].isInterface) ++first;
                Error(prog_.classes[(std::size_t)first].loc, "A script class must extend Behaviour: class " + prog_.classes[(std::size_t)first].name + " : Behaviour");
                comp = first;
            } else comp = 0;
        }
        prog_.component = comp;
        ClassDecl& compCls = prog_.classes[(std::size_t)comp];
        if (compCls.baseClass >= 0 || !compCls.interfaces.empty()) {
            Error(compCls.baseLocs.size() > 1 ? compCls.baseLocs[1] : compCls.loc, "A Behaviour class can't extend a class or implement interfaces yet");
            compCls.baseClass = -1;
            compCls.interfaces.clear();
        }
        LinkClasses();
        prog_.exceptionClass = ClassIndex("Exception");
        cls_ = &compCls;
        CheckEnums();

        // Every class's members first, so code can use fields and call methods in any order.
        for (ClassDecl& c : prog_.classes) { cls_ = &c; DeclareMembers(c); }
        for (std::size_t i = 0; i < prog_.classes.size(); ++i) Layout((int)i);
        for (ClassDecl& c : prog_.classes) { cls_ = &c; CheckInheritance(c); }
        for (ClassDecl& c : prog_.classes) {
            cls_ = &c;
            for (FieldDecl& f : c.fields) CheckField(f);
            for (MethodDecl& m : c.methods) if (m.body) CheckMethod(m);
        }
        BuildDispatch();

        // Methods nothing calls do nothing: lifecycle methods run by name and public
        // ones can be wired to UI events, everything else must be called somewhere.
        for (ClassDecl& c : prog_.classes) {
            if (c.builtin) continue;
            cls_ = &c;
            for (MethodDecl& m : c.methods)
                if (!m.isCtor && m.lifecycle < 0 && !m.isPublic && !m.called && !m.isAbstract) {
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

private:
    Program& prog_;
    const EngineApi& api_;
    ClassDecl* cls_ = nullptr;
    MethodDecl* method_ = nullptr;
    std::vector<std::vector<LocalVar>> scopes_;
    int nextSlot_ = 0, maxSlot_ = 0, loopDepth_ = 0, switchDepth_ = 0;
    bool inFieldInit_ = false;
    bool inEnumValue_ = false;   ///< checking an enum member's value (fields aren't declared yet)
    bool allowEvent_ = false;    ///< checking the target of += / -= (another class's event is allowed there)
    int catchDepth_ = 0;         ///< inside a catch (throw; works)
    int finallyDepth_ = 0;       ///< inside a finally (return, and break / continue to outside it, don't)
    std::vector<int> enumReady_;   ///< per enum: how many members have their value worked out

    // ---- Lambdas ----
    // A lambda is checked like a small method inside the code around it: its own
    // scopes and slots, plus captures for the variables it uses from outside.
    struct Outer {
        std::vector<std::vector<LocalVar>> scopes;
        std::deque<LocalVar> captures;
        LambdaDecl* lambda;
        int nextSlot, maxSlot, loopDepth, switchDepth;
        Type ret;
        bool inferRet;
        int retState;
        int catchDepth, finallyDepth;
    };
    std::vector<Outer> outer_;            ///< the code around the lambda being checked, innermost last
    LambdaDecl* lambda_ = nullptr;        ///< the lambda being checked
    std::deque<LocalVar> captures_;       ///< its captured variables (stable addresses)
    Type lambdaRet_;
    bool inferRet_ = false;               ///< its result type comes from its returns (Select, OrderBy)
    int retState_ = 0;                    ///< while inferring: 0 no return yet, 1 `return;`, 2 a value
    std::vector<Expr*> pending_;          ///< lambdas not yet given a Func / Action type

    bool HasErrors() const { for (auto& d : prog_.diags) if (d.severity == 0) return true; return false; }
    int ClassIndex(const std::string& n) const {
        for (std::size_t i = 0; i < prog_.classes.size(); ++i) if (prog_.classes[i].name == n) return (int)i;
        return -1;
    }
    bool InComponent() const { return cls_ == &prog_.classes[(std::size_t)prog_.component]; }
    int ClassId() const { return (int)(cls_ - prog_.classes.data()); }
    // gameObject, transform, enabled and GetComponent belong to the object a Behaviour is on.
    bool RejectOutsideComponent(const std::string& name, SrcLoc loc) {
        if (InComponent()) return false;
        Error(loc, "'" + name + "' only exists in the Behaviour class: " + cls_->name + " isn't on an object");
        return true;
    }
    static std::string Values(std::size_t n) { return std::to_string(n) + (n == 1 ? " value" : " values"); }

    // ---- Classes that extend classes and implement interfaces ----
    ClassDecl& Cls(int i) { return prog_.classes[(std::size_t)i]; }
    // Break loops (A : B, B : A), then work out what each class is.
    void LinkClasses() {
        const int n = (int)prog_.classes.size();
        for (int i = 0; i < n; ++i) {
            int steps = 0;
            for (int c = Cls(i).baseClass; c >= 0; c = Cls(c).baseClass)
                if (c == i || ++steps > n) { Error(Cls(i).baseLocs[0], Cls(i).name + " ends up extending itself"); Cls(i).baseClass = -1; break; }
        }
        for (int i = 0; i < n; ++i) {
            ClassDecl& c = Cls(i);
            c.isA.assign((std::size_t)n, 0);
            std::vector<int> todo = {i};
            while (!todo.empty()) {
                const int k = todo.back();
                todo.pop_back();
                if (c.isA[(std::size_t)k]) continue;
                c.isA[(std::size_t)k] = 1;
                if (Cls(k).baseClass >= 0) todo.push_back(Cls(k).baseClass);
                for (int f : Cls(k).interfaces) todo.push_back(f);
            }
        }
        for (int i = 0; i < n; ++i)   // an interface that reaches itself through others
            for (std::size_t j = 0; j < Cls(i).interfaces.size(); ++j)
                if (Cls(Cls(i).interfaces[j]).isA[(std::size_t)i]) {
                    Error(Cls(i).loc, Cls(i).name + " ends up extending itself");
                    Cls(i).interfaces.clear();
                    break;
                }
    }
    bool IsA(int sub, int super) const {
        return sub >= 0 && super >= 0 && prog_.classes[(std::size_t)sub].isA.size() > (std::size_t)super && prog_.classes[(std::size_t)sub].isA[(std::size_t)super];
    }
    // Each object's fields: the classes it extends first, then its own.
    void Layout(int i) {
        ClassDecl& c = Cls(i);
        if (c.fieldCount > 0 || (c.fields.empty() && c.baseClass < 0)) return;
        int at = 0;
        if (c.baseClass >= 0) { Layout(c.baseClass); at = Cls(c.baseClass).fieldCount; }
        for (FieldDecl& f : c.fields) f.index = at++;
        c.fieldCount = at;
    }
    int NameId(const std::string& n) {
        auto& names = prog_.methodNames;
        for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int)i;
        names.push_back(n);
        return (int)names.size() - 1;
    }
    // The field `n` of class k or a class it extends (and which class has it).
    FieldDecl* FindField(int k, const std::string& n, int* owner = nullptr) {
        for (int c = k; c >= 0; c = Cls(c).baseClass)
            for (FieldDecl& f : Cls(c).fields)
                if (f.name == n) { if (owner) *owner = c; return &f; }
        return nullptr;
    }
    // The method `n` of class k, a class it extends, or (for interfaces and abstract classes) an interface it has.
    MethodDecl* FindMethod(int k, const std::string& n, int* owner = nullptr, int* index = nullptr) {
        auto in = [&](int c) -> MethodDecl* {
            auto& ms = Cls(c).methods;
            for (std::size_t i = 0; i < ms.size(); ++i)
                if (!ms[i].isCtor && ms[i].name == n) { if (owner) *owner = c; if (index) *index = (int)i; return &ms[i]; }
            return nullptr;
        };
        for (int c = k; c >= 0; c = Cls(c).baseClass) if (MethodDecl* m = in(c)) return m;
        for (std::size_t c = 0; c < prog_.classes.size(); ++c)
            if (Cls(k).isA[c] && Cls((int)c).isInterface) if (MethodDecl* m = in((int)c)) return m;
        return nullptr;
    }
    // Members of class `owner` the code being checked can use.
    bool CanUse(int owner, bool isPublic, bool isProtected) const {
        return isPublic || owner == ClassId() || (isProtected && IsA(ClassId(), owner));
    }
    std::string NotUsable(const std::string& name, int owner) {
        if (IsA(ClassId(), owner))
            return "'" + name + "' is private to " + Cls(owner).name + ": make it protected (or public) to use it in classes that extend it";
        return "'" + name + "' is private to " + Cls(owner).name + ": make it public to use it here";
    }
    // obj.M(...) with obj of class (or interface) k, its arguments already checked:
    // runs the method the object's own class has under that name.
    void CallClassMethod(Expr& e, int k) {
        int owner = -1;
        MethodDecl* m = FindMethod(k, e.name, &owner);
        if (!m) {
            std::vector<std::string> opts;
            for (std::size_t c = 0; c < prog_.classes.size(); ++c)
                if (Cls(k).isA[c]) for (auto& x : Cls((int)c).methods) if (!x.isCtor) opts.push_back(x.name);
            Error(e.loc, Cls(k).name + " has no method '" + e.name + "'" + Suggest(e.name, opts));
            return;
        }
        if (!CanUse(owner, m->isPublic, m->isProtected)) { Error(e.loc, NotUsable(e.name, owner)); return; }
        if (m != method_) m->called = true;
        if (!ArgsFit(e, *m)) return;
        e.bind = Bind::ObjMethod; e.slot = m->nameId; e.member = k; e.type = m->retType;
    }
    // base.M(...): the method of the class this one extends, not this class's override.
    void CheckBaseCall(Expr& e) {
        for (auto& a : e.args) CheckExpr(*a);
        if (cls_->baseClass < 0) { Error(e.object->loc, cls_->name + " doesn't extend a class, so there's no base"); return; }
        if (method_ && method_->isStatic) { Error(e.object->loc, "A static method has no base"); return; }
        int owner = -1, index = -1;
        MethodDecl* m = FindMethod(cls_->baseClass, e.name, &owner, &index);
        if (!m || Cls(owner).isInterface) { Error(e.loc, Cls(cls_->baseClass).name + " has no method '" + e.name + "'"); return; }
        if (!m->body) { Error(e.loc, Cls(owner).name + "." + e.name + "() is abstract: there's nothing to call"); return; }
        if (!CanUse(owner, m->isPublic, m->isProtected)) { Error(e.loc, NotUsable(e.name, owner)); return; }
        m->called = true;
        if (!ArgsFit(e, *m)) return;
        UsesSelf();
        e.object.reset();
        e.bind = Bind::BaseMethod; e.slot = index; e.member = owner; e.type = m->retType;
    }
    static bool SameSignature(const MethodDecl& a, const MethodDecl& b) {
        if (a.params.size() != b.params.size() || a.retType != b.retType) return false;
        for (std::size_t i = 0; i < a.params.size(); ++i) if (a.params[i].resolved != b.params[i].resolved) return false;
        return true;
    }
    std::string Signature(const MethodDecl& m) {
        std::string s = (m.retType.kind == TK::Void ? std::string("void") : TypeName(m.retType)) + " " + m.name + "(";
        for (std::size_t i = 0; i < m.params.size(); ++i) s += (i ? ", " : "") + TypeName(m.params[i].resolved);
        return s + ")";
    }
    // override / virtual / abstract and interfaces, the C# way.
    void CheckInheritance(ClassDecl& c) {
        const int self = ClassId();
        for (FieldDecl& f : c.fields) {
            int owner = -1;
            if (c.baseClass >= 0 && (FindField(c.baseClass, f.name, &owner) || FindMethod(c.baseClass, f.name, &owner)))
                Error(f.loc, "'" + f.name + "' is already in " + Cls(owner).name + ": pick another name");
        }
        for (MethodDecl& m : c.methods) {
            if (m.isCtor) continue;
            if (m.isStatic && (m.isVirtual || m.isOverride || m.isAbstract)) Error(m.loc, "A static method can't be virtual, override or abstract");
            if (m.isAbstract && !c.isAbstract && !c.isInterface) Error(m.loc, "Only an abstract class can have abstract methods: write abstract class " + c.name);
            if ((m.isVirtual || m.isAbstract) && !c.isInterface && !m.isPublic && !m.isProtected)
                Error(m.loc, "A virtual or abstract method can't be private: make it public or protected");
            int owner = -1;
            if (c.baseClass >= 0 && FindField(c.baseClass, m.name, &owner)) { Error(m.loc, "'" + m.name + "' is already a field in " + Cls(owner).name); continue; }
            MethodDecl* inh = c.baseClass >= 0 ? FindMethod(c.baseClass, m.name, &owner) : nullptr;
            if (inh && Cls(owner).isInterface) inh = nullptr;   // an interface's method is implemented, not overridden
            if (!inh) {
                if (m.isOverride) Error(m.loc, "There's no virtual " + m.name + "() in what " + c.name + " extends, so there's nothing to override");
                continue;
            }
            if (!m.isOverride) Error(m.loc, m.name + "() is already in " + Cls(owner).name + ": to change what it does, mark it virtual there and override here");
            else if (!inh->isVirtual && !inh->isAbstract && !inh->isOverride) Error(m.loc, m.name + "() in " + Cls(owner).name + " isn't virtual, so it can't be overridden: mark it virtual");
            else if (!SameSignature(m, *inh)) Error(m.loc, "An override must match what it overrides: " + Signature(*inh) + " in " + Cls(owner).name);
            else if (m.isPublic != inh->isPublic) Error(m.loc, "An override keeps the access of what it overrides: make it " + std::string(inh->isPublic ? "public" : "protected"));
            m.called = true;   // runs whenever the method it overrides is called
        }
        if (c.isInterface) return;
        if (c.baseClass >= 0 && !CtorWith(c.baseClass, 0) && HasCtor(c.baseClass) && !HasCtor(self))
            Error(c.loc, c.name + " needs a constructor that calls base(...): " + Cls(c.baseClass).name + " has no constructor that takes 0 values");
        // Everything abstract must have a body somewhere, unless the class is abstract itself.
        for (std::size_t k = 0; k < prog_.classes.size(); ++k) {
            if (!c.isA[k] || (int)k == self) continue;
            for (MethodDecl& am : Cls((int)k).methods) {
                if (!am.isAbstract) continue;
                int owner = -1;
                MethodDecl* impl = nullptr;
                for (int x = self; x >= 0 && !impl; x = Cls(x).baseClass)
                    for (MethodDecl& cm : Cls(x).methods)
                        if (!cm.isCtor && cm.name == am.name && (cm.body || (c.isAbstract && Cls((int)k).isInterface))) { impl = &cm; owner = x; break; }
                if (!impl) {
                    if (c.isAbstract && !Cls((int)k).isInterface) continue;
                    Error(c.loc, c.name + (Cls((int)k).isInterface ? " must have " + Cls((int)k).name + "'s method: public " + Signature(am)
                                                                     : " must override " + Cls((int)k).name + "." + am.name + "()"));
                    continue;
                }
                if (Cls((int)k).isInterface) {
                    if (!SameSignature(*impl, am)) Error(impl->loc, impl->name + "() must match " + Cls((int)k).name + "'s " + Signature(am));
                    else if (!impl->isPublic) Error(impl->loc, impl->name + "() is " + Cls((int)k).name + "'s, so it must be public");
                    impl->called = true;
                    (void)owner;
                }
            }
        }
    }
    // Exception's Message: set by its constructor, read by everyone else.
    bool IsBuiltinField(const FieldDecl& f) {
        for (const ClassDecl& c : prog_.classes)
            if (c.builtin) for (const FieldDecl& g : c.fields) if (&g == &f) return true;
        return false;
    }
    bool HasCtor(int k) { for (auto& m : Cls(k).methods) if (m.isCtor) return true; return false; }
    int CtorWith(int k, std::size_t params) {
        for (std::size_t i = 0; i < Cls(k).methods.size(); ++i)
            if (Cls(k).methods[i].isCtor && Cls(k).methods[i].params.size() == params) return (int)i + 1;   // + 1: 0 is "none"
        return 0;
    }
    // Which method each class's objects run for each name: the most derived body.
    void BuildDispatch() {
        for (std::size_t i = 0; i < prog_.classes.size(); ++i) {
            ClassDecl& c = Cls((int)i);
            c.dispatch.assign(prog_.methodNames.size(), {-1, -1});
            if (c.isInterface) continue;
            std::vector<int> chain;
            for (int k = (int)i; k >= 0; k = Cls(k).baseClass) chain.push_back(k);
            for (auto k = chain.rbegin(); k != chain.rend(); ++k)
                for (std::size_t m = 0; m < Cls(*k).methods.size(); ++m) {
                    const MethodDecl& md = Cls(*k).methods[m];
                    if (!md.isCtor && md.body && md.nameId >= 0) c.dispatch[(std::size_t)md.nameId] = {*k, (int)m};
                }
        }
    }

    void DeclareMembers(ClassDecl& c) {
        const bool comp = InComponent();
        std::set<std::string> names;
        std::set<std::size_t> ctorArgs;
        for (std::size_t i = 0; i < c.fields.size(); ++i) {
            FieldDecl& f = c.fields[i];
            if (!names.insert(f.name).second) Error(f.loc, "'" + f.name + "' is declared twice");
            if (comp && IsSelfMemberName(f.name)) Error(f.loc, "'" + f.name + "' is already the object's own " + f.name + ": pick another name");
            if (!comp && f.isStatic) Error(f.loc, "static members only work in the Behaviour class for now");
            if (!comp && f.isConst) Error(f.loc, "const only works in the Behaviour class for now");
            f.resolved = ResolveType(f.type, false);
            if (f.isEvent && f.resolved.kind != TK::Func && f.resolved.kind != TK::Error)
                Error(f.loc, "An event holds functions: event Action " + f.name + "; or event Action<int> " + f.name + ";");
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
            m.nameId = NameId(m.name);
            if (!comp && m.isStatic) Error(m.loc, "static members only work in the Behaviour class for now");
            if (!names.insert(m.name).second) Error(m.loc, "'" + m.name + "' is declared twice (methods can't share a name yet)");
            m.retType = m.ret.name == "void" ? Type::Of(TK::Void) : ResolveType(m.ret, false);
            if (comp) CheckLifecycleSignature(m, (int)i);
        }
    }
    // The method's parameters take the call's (checked) arguments.
    bool ArgsFit(Expr& e, const MethodDecl& m) {
        if (e.args.size() != m.params.size()) {
            Error(e.loc, e.name + "() takes " + Values(m.params.size()) + ", not " + std::to_string(e.args.size()));
            return false;
        }
        for (std::size_t i = 0; i < e.args.size(); ++i) Expect(*e.args[i], m.params[i].resolved, m.params[i].name);
        return true;
    }
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
                    const FieldDecl& f = FieldAt(e.slot);
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
    Type ResolveGeneric(const TypeRef& t) {
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
    static bool IsKeyType(const Type& t) {
        return t.kind == TK::Int || t.kind == TK::String || t.kind == TK::Bool || t.kind == TK::Enum ||
               t.kind == TK::Object || t.kind == TK::Class;
    }
    const Program::Generic& Gen(const Type& t) const { return prog_.generics[(std::size_t)t.id]; }

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
    bool Convertible(const Type& from, const Type& to) const {
        if (from.kind == TK::Error || to.kind == TK::Error) return true;   // already reported
        if (to.kind == TK::Any) return from.kind != TK::Void;
        if (from == to) return true;
        if (from.kind == TK::Int && to.kind == TK::Float) return true;
        if (from.kind == TK::Null && CanBeNull(to)) return true;
        if (from.kind == TK::Class && to.kind == TK::Class) return IsA(from.id, to.id);   // a Boss is an Enemy
        if (from.kind == TK::Lambda) return to.kind == TK::Func || to.kind == TK::Any;   // the lambda itself is checked by Expect
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
        if (e.kind == EK::Name && e.type.kind == TK::Lambda && to.kind == TK::Func) MethodAsLambda(e, prog_.funcs[(std::size_t)to.id].params.size());
        if (e.kind == EK::Lambda && e.type.kind == TK::Lambda) { CheckLambda(e, to, what); return; }
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
            scopes_.clear();
            nextSlot_ = maxSlot_ = 0;
            CheckInit(*f.init, f.resolved, "The starting value of '" + f.name + "'");
            inFieldInit_ = false;
            FlushLambdas();
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
        if (m.isCtor) CheckBaseCtor(m);
        bool completes = CheckBlockBody(*m.body);
        if (completes && m.retType.kind != TK::Void && m.retType.kind != TK::Error)
            Error(m.loc, m.name + "() must return a " + TypeName(m.retType) + " on every path");
        PopScope();
        m.localCount = maxSlot_;
        method_ = nullptr;
        FlushLambdas();
    }
    // A constructor of a class that extends another runs one of the base class's
    // constructors first: the : base(...) one, or the one that takes nothing.
    void CheckBaseCtor(MethodDecl& m) {
        for (auto& a : m.baseArgs) CheckExpr(*a);
        if (cls_->baseClass < 0) {
            if (m.hasBaseCall) Error(m.baseLoc, cls_->name + " doesn't extend a class, so there's no base(...) to call");
            return;
        }
        const int b = cls_->baseClass;
        const int ctor = CtorWith(b, m.baseArgs.size()) - 1;
        if (ctor < 0 && (HasCtor(b) || !m.baseArgs.empty())) {
            Error(m.hasBaseCall ? m.baseLoc : m.loc, Cls(b).name + " has no constructor that takes " + Values(m.baseArgs.size()) +
                                                     (m.hasBaseCall ? std::string() : ": call base(...) with what it needs"));
            return;
        }
        auto call = std::make_unique<Expr>();
        call->kind = EK::Call;
        call->loc = m.hasBaseCall ? m.baseLoc : m.loc;
        call->name = Cls(b).name;
        if (ctor >= 0) {
            MethodDecl& bc = Cls(b).methods[(std::size_t)ctor];
            if (!CanUse(b, bc.isPublic, bc.isProtected)) { Error(call->loc, Cls(b).name + "'s constructor is private: make it protected or public"); return; }
            bc.called = true;
            for (std::size_t i = 0; i < m.baseArgs.size(); ++i) Expect(*m.baseArgs[i], bc.params[i].resolved, bc.params[i].name);
        }
        call->args = std::move(m.baseArgs);
        call->bind = Bind::BaseCtor; call->slot = b; call->member = ctor; call->type = Type::Of(TK::Void);
        m.baseCall = std::move(call);
    }

    // Lambdas that ended up somewhere without a Func or Action type.
    void FlushLambdas() {
        for (Expr* e : pending_)
            Error(e->loc, e->kind == EK::Name ? "'" + e->name + "' is a method: call it with " + e->name + "()"
                                              : std::string("A lambda only works where a Func or Action is expected"));
        pending_.clear();
    }

    void PushScope() { scopes_.emplace_back(); }
    void PopScope() {
        for (auto& v : scopes_.back())
            if (!v.used) Warning(v.loc, "'" + v.name + "' is never used, so it does nothing");
        nextSlot_ -= (int)scopes_.back().size();
        scopes_.pop_back();
    }
    static LocalVar* FindIn(std::vector<std::vector<LocalVar>>& scopes, std::deque<LocalVar>& captures, const std::string& n) {
        for (auto s = scopes.rbegin(); s != scopes.rend(); ++s)
            for (auto& v : *s) if (v.name == n) return &v;
        for (auto& v : captures) if (v.name == n) return &v;
        return nullptr;
    }
    // A variable by name. Inside a lambda, one from the code around it becomes a
    // capture of this lambda (and of every lambda in between).
    LocalVar* FindLocal(const std::string& n) {
        if (LocalVar* v = FindIn(scopes_, captures_, n)) return v;
        if (outer_.empty()) return nullptr;
        LocalVar* v = FindAt(outer_.size() - 1, n);
        return v ? AddCapture(lambda_, captures_, *v) : nullptr;
    }
    LocalVar* FindAt(std::size_t level, const std::string& n) {
        Outer& o = outer_[level];
        if (LocalVar* v = FindIn(o.scopes, o.captures, n)) return v;
        if (level == 0) return nullptr;   // level 0 is the method itself
        LocalVar* v = FindAt(level - 1, n);
        return v ? AddCapture(o.lambda, o.captures, *v) : nullptr;
    }
    static LocalVar* AddCapture(LambdaDecl* l, std::deque<LocalVar>& captures, LocalVar& from) {
        from.used = true;
        l->captureFrom.push_back(from.captured ? -1 - from.slot : from.slot);
        LocalVar c = from;
        c.slot = (int)captures.size();
        c.captured = true;
        captures.push_back(c);
        return &captures.back();
    }
    // Is `n` a variable here or in the code around this lambda? (Without capturing it.)
    bool NameTaken(const std::string& n) {
        if (FindIn(scopes_, captures_, n)) return true;
        for (Outer& o : outer_) if (FindIn(o.scopes, o.captures, n)) return true;
        return false;
    }
    // The lambdas being checked use the object (its fields, methods or `this`).
    void UsesSelf() {
        if (lambda_) lambda_->usesSelf = true;
        for (Outer& o : outer_) if (o.lambda) o.lambda->usesSelf = true;
    }
    int Declare(const std::string& n, Type t, SrcLoc loc) {
        if (NameTaken(n)) Error(loc, "'" + n + "' is already a variable here: pick another name");
        else if (FieldIndex(n) >= 0) Warning(loc, "'" + n + "' hides the field with the same name");
        scopes_.back().push_back({n, t, nextSlot_++, loc, false});
        maxSlot_ = std::max(maxSlot_, nextSlot_);
        return scopes_.back().back().slot;
    }
    // A field of the current class, its own or one it extends (by slot or by name).
    int FieldIndex(const std::string& n) {
        FieldDecl* f = FindField(ClassId(), n);
        return f ? f->index : -1;
    }
    FieldDecl& FieldAt(int slot) {
        for (int c = ClassId(); c >= 0; c = Cls(c).baseClass)
            for (FieldDecl& f : Cls(c).fields) if (f.index == slot) return f;
        return cls_->fields[(std::size_t)slot];   // not reached: every bound slot is one of these
    }
    int MethodIdx(const std::string& n) const {
        for (std::size_t i = 0; i < cls_->methods.size(); ++i)
            if (!cls_->methods[i].isCtor && cls_->methods[i].name == n) return (int)i;   // constructors run through new
        return -1;
    }
    // A method the current class has, its own or one it extends.
    bool HasMethod(const std::string& n) { return FindMethod(ClassId(), n) != nullptr; }

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
                if (finallyDepth_ > 0) { if (s.expr) CheckExpr(*s.expr); Error(s.loc, "return can't leave a finally block"); return false; }
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
                if (loopDepth_ == 0 && switchDepth_ == 0)
                    Error(s.loc, finallyDepth_ > 0 ? "break can't leave a finally block" : "break only works inside a loop or a switch");
                return false;
            case SK::Continue:
                if (loopDepth_ == 0) Error(s.loc, finallyDepth_ > 0 ? "continue can't leave a finally block" : "continue only works inside a loop");
                return false;
            case SK::Try: return CheckTry(s);
            case SK::Throw:
                if (!s.expr) {
                    if (catchDepth_ == 0) Error(s.loc, "throw; on its own only works inside a catch (it throws the caught exception again)");
                    return false;
                }
                CheckExpr(*s.expr);
                if (s.expr->type.kind != TK::Error && !(s.expr->type.kind == TK::Class && IsA(s.expr->type.id, prog_.exceptionClass)))
                    Error(s.expr->loc, "throw needs an Exception, like throw new Exception(\"what went wrong\"), not " + TypeName(s.expr->type));
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

    // try / catch / finally: it finishes normally if the try block or a catch does
    // (and the finally does).
    bool CheckTry(Stmt& s) {
        bool completes = CheckStmt(*s.then);
        std::vector<Type> earlier;
        const Type any{TK::Class, prog_.exceptionClass};
        for (CatchClause& c : s.catches) {
            Type t = any;
            if (c.typed) {
                t = ResolveType(c.type, false);
                if (t.kind != TK::Error && !(t.kind == TK::Class && IsA(t.id, prog_.exceptionClass))) {
                    Error(c.type.loc, "catch takes Exception or a class that extends it, not " + TypeName(t));
                    t = Type{};
                }
            }
            if (t.kind == TK::Class)
                for (const Type& e : earlier)
                    if (IsA(t.id, e.id)) {
                        Error(c.loc, "An earlier catch already catches " + TypeName(t) + (t == e ? std::string() : " (as " + TypeName(e) + ")") + ", so this one never runs");
                        break;
                    }
            if (t.kind == TK::Class) earlier.push_back(t);
            c.resolved = t;
            PushScope();
            if (!c.name.empty()) c.slot = Declare(c.name, t, c.nameLoc);
            ++catchDepth_;
            if (CheckStmt(*c.body)) completes = true;
            --catchDepth_;
            PopScope();
        }
        if (s.finallyBody) {
            const int loops = loopDepth_, switches = switchDepth_;
            loopDepth_ = switchDepth_ = 0;   // break / continue can't leave it either
            ++finallyDepth_;
            const bool f = CheckStmt(*s.finallyBody);
            --finallyDepth_;
            loopDepth_ = loops; switchDepth_ = switches;
            if (!f) completes = false;
        }
        return completes;
    }

    bool CheckLambdaReturn(Stmt& s) {
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
    static bool IsTrueLiteral(const Expr& e) { return e.kind == EK::BoolLit && e.bval; }
    static bool ContainsBreak(const Stmt& s) {
        switch (s.kind) {
            case SK::Break: return true;
            case SK::Block: for (auto& c : s.body) if (ContainsBreak(*c)) return true; return false;
            case SK::If: return ContainsBreak(*s.then) || (s.otherwise && ContainsBreak(*s.otherwise));
            case SK::Try:
                if (ContainsBreak(*s.then)) return true;
                for (auto& c : s.catches) if (ContainsBreak(*c.body)) return true;
                return false;
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
            case EK::Is: case EK::As: CheckTypeTest(e); return;
            case EK::Lambda:   // its type comes from where it goes (Expect)
                e.type = Type::Of(TK::Lambda);
                pending_.push_back(&e);
                return;
        }
    }

    // OnDied as a function: the lambda ($0, ...) => OnDied($0, ...), remembering the
    // method so that -= OnDied removes what += OnDied added.
    void MethodAsLambda(Expr& e, std::size_t params) {
        auto L = std::make_unique<LambdaDecl>();
        L->method = NameId(e.name);
        auto call = std::make_unique<Expr>();
        call->kind = EK::Call; call->loc = e.loc; call->name = e.name;
        for (std::size_t i = 0; i < params; ++i) {
            Param p;
            p.name = "$" + std::to_string(i);   // not a name a script can write
            p.loc = e.loc;
            auto arg = std::make_unique<Expr>();
            arg->kind = EK::Name; arg->loc = e.loc; arg->name = p.name;
            call->args.push_back(std::move(arg));
            L->params.push_back(std::move(p));
        }
        L->value = std::move(call);
        e.kind = EK::Lambda;
        e.lambda = std::move(L);
    }

    void Unpend(Expr& e) { pending_.erase(std::remove(pending_.begin(), pending_.end(), &e), pending_.end()); }

    // A lambda used as `to` (a Func or Action).
    void CheckLambda(Expr& e, const Type& to, const std::string& what) {
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
    Type InferLambda(Expr& e, const std::vector<Type>& params, const std::string& who) {
        if (e.type.kind == TK::Func) {   // a Func variable: its signature must fit
            const Program::FuncSig& sig = prog_.funcs[(std::size_t)e.type.id];
            if (sig.params == params) return e.type;
        }
        if (e.kind == EK::Name && e.type.kind == TK::Lambda) MethodAsLambda(e, params.size());
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
    static bool IsStatementLike(const Expr& v) {
        return v.kind == EK::Assign || v.kind == EK::IncDec || v.kind == EK::Call || v.kind == EK::New ||
               (v.kind == EK::NullCond && v.lhs->kind == EK::Call);
    }
    // Check a lambda's body with these parameter types; returns its result type
    // (worked out from the body when `infer`).
    Type LambdaBody(Expr& e, const std::vector<Type>& params, Type ret, bool infer) {
        LambdaDecl& L = *e.lambda;
        outer_.push_back(Outer{std::move(scopes_), std::move(captures_), lambda_, nextSlot_, maxSlot_, loopDepth_, switchDepth_,
                               lambdaRet_, inferRet_, retState_, catchDepth_, finallyDepth_});
        catchDepth_ = finallyDepth_ = 0;   // a lambda's code runs later, not inside the catch / finally around it
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
                Expect(v, ret, L.method >= 0 ? v.name + "()" : std::string("This lambda's value"));
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
        catchDepth_ = o.catchDepth; finallyDepth_ = o.finallyDepth;
        outer_.pop_back();
        return result;
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

    // e.name as one of the current class's own members (a field, or the object's
    // gameObject / transform / enabled); false if it's neither.
    bool BindOwnMember(Expr& e) {
        int owner = -1;
        if (FieldDecl* fp = FindField(ClassId(), e.name, &owner)) {
            if (inFieldInit_) { Error(e.loc, "A field's starting value can't use another field: set '" + e.name + "' up in Start()"); return true; }
            FieldDecl& f = *fp;
            const int fi = f.index;
            if (!CanUse(owner, f.isPublic, f.isProtected)) { Error(e.loc, NotUsable(e.name, owner)); return true; }
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

    void CheckName(Expr& e) {
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
        if (HasMethod(e.name)) {   // a method used as a function (Died += OnDied): Expect turns it into one
            e.type = Type::Of(TK::Lambda);
            pending_.push_back(&e);
            return;
        }
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
    int Receiver(Expr& obj, int& typeId) {
        if (obj.kind == EK::Name && obj.name == "base" && !FindLocal("base") && !InComponent() && cls_->baseClass >= 0)
            obj.kind = EK::This;   // base.field is the same field as this.field
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

    static std::string EventOutside(const FieldDecl& f, const ClassDecl& k) {
        return "'" + f.name + "' is an event of " + k.name + ": outside " + k.name + " it can only be added to (+=) or removed from (-=)";
    }
    void CheckMember(Expr& e) {
        const bool allowEvent = allowEvent_;   // only for this member, not for what's inside it
        allowEvent_ = false;
        int typeId = -1;
        int r = Receiver(*e.object, typeId);
        if (r < 0 || BadNullCond(e, r)) return;
        if (r == 4) {
            int fi = FieldIndex(e.name);
            const FieldDecl* f = fi >= 0 ? &cls_->fields[(std::size_t)fi] : nullptr;
            if (f && !f->isStatic && !f->isConst) { Error(e.loc, "'" + e.name + "' isn't static: use it without " + cls_->name + "."); return; }
            if (!f && !HasMethod(e.name)) { Error(e.loc, cls_->name + " has no '" + e.name + "'"); return; }
        }
        if (r == 2 || r == 4) {   // this.x, T.x: a member, never a local of the same name
            e.object.reset();
            if (BindOwnMember(e)) return;
            if (HasMethod(e.name)) { Error(e.loc, "'" + e.name + "' is a method: call it with " + e.name + "()"); return; }
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
                if (t.staticProps[i].name == e.name) { e.bind = Bind::ApiStaticProp; e.slot = typeId; e.member = (int)i; e.type = t.staticProps[i].type; return; }
            for (auto& m : t.staticMethods)
                if (m.name == e.name) { Error(e.loc, t.name + "." + e.name + " is a method: call it with " + e.name + "(...)"); return; }
            std::vector<std::string> opts = Names(t.staticProps);
            for (auto& m : t.staticMethods) opts.push_back(m.name);
            Error(e.loc, t.name + " has no '" + e.name + "'" + Suggest(e.name, opts));
            return;
        }
        const Type& ot = e.object->type;
        if (ot.kind == TK::Class) {
            int owner = -1;
            if (FieldDecl* f = FindField(ot.id, e.name, &owner)) {
                if (!CanUse(owner, f->isPublic, f->isProtected)) { Error(e.loc, NotUsable(e.name, owner)); return; }
                if (f->isEvent && owner != ClassId() && !allowEvent) { Error(e.loc, EventOutside(*f, Cls(owner))); return; }
                f->read = true;
                e.bind = Bind::ObjField; e.slot = f->index; e.type = f->resolved;
                return;
            }
            if (FindMethod(ot.id, e.name)) { Error(e.loc, e.name + " is a method: call it with " + e.name + "(...)"); return; }
            std::vector<std::string> opts;
            for (std::size_t c = 0; c < prog_.classes.size(); ++c)
                if (Cls(ot.id).isA[c]) {
                    for (auto& f : Cls((int)c).fields) opts.push_back(f.name);
                    for (auto& m : Cls((int)c).methods) if (!m.isCtor) opts.push_back(m.name);
                }
            Error(e.loc, Cls(ot.id).name + " has no '" + e.name + "'" + Suggest(e.name, opts));
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

    // f(x) / f.Invoke(x): call the function `e.object` holds.
    void CheckInvoke(Expr& e) {
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
    void AsInvoke(Expr& e, ExprPtr receiver) {
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

    void CheckCall(Expr& e) {
        if (e.bind == Bind::Invoke) {   // f(x)(y), xs[0](): the parser marked it
            e.bind = Bind::None;
            CheckExpr(*e.object);
            CheckInvoke(e);
            return;
        }
        if (!e.object && e.typeArg.name.empty() && (FindLocal(e.name) || (FieldIndex(e.name) >= 0 && !HasMethod(e.name)))) {
            AsInvoke(e, nullptr);   // a variable or field holding a function
            return;
        }
        if (inFieldInit_ && !e.object) { Error(e.loc, "A field's starting value can't call methods: set it up in Start()"); return; }
        if (e.object && e.object->kind == EK::Name && e.object->name == "base" && !FindLocal("base")) { CheckBaseCall(e); return; }
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
            if (e.object && !HasMethod(e.name) && FieldIndex(e.name) >= 0) {   // this.onHit(): a field holding a function
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
            if (!InComponent() && HasMethod(e.name)) {   // a plain class's method (maybe one it extends): run as the object's class has it
                for (auto& a : e.args) CheckExpr(*a);
                if (viaClass) { Error(e.loc, "'" + e.name + "' isn't static: call it without " + cls_->name + "."); return; }
                auto self = std::make_unique<Expr>();
                self->kind = EK::This; self->loc = e.loc; self->type = Type{TK::Class, ClassId()};
                e.object = std::move(self);
                UsesSelf();
                CallClassMethod(e, ClassId());
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
        if (r == 0 && e.object->type.kind == TK::Class && !e.nullCond && !FindMethod(e.object->type.id, e.name)) {   // obj.onHit(): a field holding a function
            int owner = -1;
            FieldDecl* f = FindField(e.object->type.id, e.name, &owner);
            if (f && f->resolved.kind == TK::Func) {
                if (!CanUse(owner, f->isPublic, f->isProtected)) { Error(e.loc, NotUsable(e.name, owner)); return; }
                if (f->isEvent && owner != ClassId()) { Error(e.loc, EventOutside(*f, Cls(owner))); return; }
                f->read = true;
                auto m = std::make_unique<Expr>();   // obj.field, its receiver already checked
                m->kind = EK::Member; m->loc = e.loc; m->name = e.name;
                m->object = std::move(e.object);
                m->bind = Bind::ObjField; m->slot = f->index; m->type = f->resolved;
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
            e.bind = Bind::ApiStaticMethod; e.slot = typeId; e.member = (int)(m - t.staticMethods.data()); e.type = m->ret;
            return;
        }
        const Type& ot = e.object->type;
        if (ot.kind == TK::List || ot.kind == TK::Array) {
            const Type elem = ot.kind == TK::List ? Gen(ot).a : prog_.arrays[(std::size_t)ot.id];
            if (CheckLinq(e, ot, elem)) return;
        }
        if (ot.kind == TK::List || ot.kind == TK::Dict) { CheckCollectionCall(e, ot); return; }
        if (ot.kind == TK::Class) { CallClassMethod(e, ot.id); return; }
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
            if (RejectOutsideComponent("GetComponent", e.loc) || RejectInStatic("GetComponent", e.loc)) return;
            UsesSelf();
        }
        e.bind = Bind::GetComponent; e.slot = ti; e.type = Type::Obj(ti);
    }

    // xs.Count, d.Keys / d.Values, kv.Key / kv.Value.
    void CheckCollectionMember(Expr& e, const Type& ot) {
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
    bool CheckLinq(Expr& e, const Type& ot, const Type& elem) {
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
    void CheckCollectionCall(Expr& e, const Type& ot) {
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
    void CheckNewCollection(Expr& e) {
        const Type t = ResolveType(e.typeArg, false);
        if (t.kind == TK::Error) return;
        if (t.kind != TK::List && t.kind != TK::Dict) { Error(e.loc, "'new " + TypeName(t) + "' isn't something you can make"); return; }
        if (!e.hasInit && !e.args.empty()) { Error(e.loc, "new " + TypeName(t) + "() takes no values"); return; }
        if (e.hasInit && t.kind == TK::Dict) { Error(e.loc, "Fill a Dictionary with Add or d[key] = value"); return; }
        for (auto& a : e.args) Expect(*a, Gen(t).a, "This item");
        e.bind = Bind::NewCollection; e.type = t;
    }

    void CheckNew(Expr& e) {
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
    void CheckNewObject(Expr& e, int ci) {
        if (ci == prog_.component) { Error(e.loc, "Behaviour scripts are added to objects, not made with new"); return; }
        ClassDecl& k = prog_.classes[(std::size_t)ci];
        if (k.isInterface) { Error(e.loc, k.name + " is an interface: make one of the classes that implement it"); return; }
        if (k.isAbstract) { Error(e.loc, k.name + " is abstract: make one of the classes that extend it"); return; }
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
            if (!CanUse(ci, m.isPublic, m.isProtected)) { Error(e.loc, k.name + "'s constructor is private: make it public to use it here"); return; }
            m.called = true;
            for (std::size_t i = 0; i < e.args.size(); ++i) Expect(*e.args[i], m.params[i].resolved, m.params[i].name);
        }
        e.bind = Bind::NewObject; e.slot = ci; e.member = ctor; e.type = Type{TK::Class, ci};
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

    // Can a value of class a also be a b? (One extends the other, or either is an
    // interface some other class might implement.)
    bool MayBeSame(int a, int b) const {
        return IsA(a, b) || IsA(b, a) || prog_.classes[(std::size_t)a].isInterface || prog_.classes[(std::size_t)b].isInterface;
    }
    // x is T / x as T
    void CheckTypeTest(Expr& e) {
        CheckExpr(*e.lhs);
        const Type t = ResolveType(e.typeArg, false);
        const Type a = e.lhs->type;
        if (t.kind == TK::Error || a.kind == TK::Error) return;
        const char* op = e.kind == EK::Is ? "is" : "as";
        if (t.kind != TK::Class || a.kind != TK::Class) {
            Error(e.loc, std::string(op) + " works on the script's classes and interfaces, not " + TypeName(a.kind != TK::Class ? a : t));
            return;
        }
        if (!MayBeSame(a.id, t.id)) { Error(e.loc, "This " + TypeName(a) + " is never " + Article(TypeName(t))); return; }
        e.slot = t.id;
        e.type = e.kind == EK::Is ? Type::Of(TK::Bool) : t;
    }

    static bool CanBeNull(const Type& t) {
        return t.kind == TK::Object || t.kind == TK::Array || t.kind == TK::Class || t.kind == TK::List || t.kind == TK::Dict ||
               t.kind == TK::Func;
    }
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
                // Arrays, lists, dictionaries and class objects compare as references (or with null).
                if (a.kind != TK::Object && b.kind != TK::Object && (CanBeNull(a) || CanBeNull(b)) &&
                    (a == b || a.kind == TK::Null || b.kind == TK::Null || (a.kind == TK::Class && b.kind == TK::Class && MayBeSame(a.id, b.id))))
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
    bool Assignable(Expr& e) {
        if (e.kind == EK::Index) return true;
        switch (e.bind) {
            case Bind::StaticField: return true;
            case Bind::ObjField: {
                FieldDecl* f = FindField(e.object->type.id, e.name);
                if (f && IsBuiltinField(*f) && !cls_->builtin) { Error(e.loc, "'" + e.name + "' can only be read"); return false; }
                return true;
            }
            case Bind::Local: case Bind::Captured:
                if (LocalVar* v = FindLocal(e.name); v && v->readOnly) {
                    Error(e.loc, "'" + e.name + "' is the foreach variable, so it can't be changed");
                    return false;
                }
                return true;
            case Bind::ArrayLength: Error(e.loc, "'Length' can only be read"); return false;
            case Bind::EnumValue: Error(e.loc, "'" + e.name + "' is an enum value, so it can't change"); return false;
            case Bind::Field: {
                FieldDecl& f = FieldAt(e.slot);
                if (IsBuiltinField(f) && !cls_->builtin) { Error(e.loc, "'" + f.name + "' can only be read: pass it to base(...)"); return false; }
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

    void CheckAssign(Expr& e) {
        // A plain `x = ...` writes x without reading it, so it doesn't count as a use.
        LocalVar* lv = nullptr; FieldDecl* fd = nullptr;
        bool wasUsed = false;
        if (e.op == T::Assign && e.lhs->kind == EK::Name) {
            if ((lv = FindLocal(e.lhs->name))) wasUsed = lv->used;
            else if ((fd = FindField(ClassId(), e.lhs->name))) wasUsed = fd->read;
        }
        allowEvent_ = e.op == T::PlusEq || e.op == T::MinusEq;
        CheckExpr(*e.lhs);
        allowEvent_ = false;
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
        if (lt.kind == TK::Func && (e.op == T::PlusEq || e.op == T::MinusEq)) {   // add or remove a function (an event's handler)
            Expect(*e.rhs, lt, "The function");
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
        if (from.kind == TK::Class && to.kind == TK::Class && MayBeSame(from.id, to.id)) {   // (Boss)enemy: checked while playing
            e.type = to;
            e.slot = IsA(from.id, to.id) ? 0 : 1;
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
        if (CanBeNull(a) && b.kind == TK::Null) { e.type = a; return; }
        if (a.kind == TK::Class && b.kind == TK::Class && (IsA(a.id, b.id) || IsA(b.id, a.id))) {   // the one both are
            e.type = IsA(a.id, b.id) ? b : a;
            return;
        }
        if (a.kind == TK::Null && CanBeNull(b)) { e.type = b; return; }
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

// Classes every script has, added after the script's own (so they never move its
// line numbers or class indexes). A script's own class of the same name wins.
static void AddBuiltins(Program& p) {
    if (p.classes.empty()) return;
    for (const ClassDecl& c : p.classes) if (c.name == "Exception") return;
    static const char* const kExceptionSource =
        "class Exception {\n"
        "    public string Message;\n"
        "    public Exception() { Message = \"Exception of type 'Exception' was thrown.\"; }\n"
        "    public Exception(string message) { Message = message; }\n"
        "}\n";
    Program builtins;
    std::vector<ScriptDiagnostic> none;
    Parse(Lex(kExceptionSource, none), builtins);
    for (ClassDecl& c : builtins.classes) {
        c.builtin = true;
        p.classes.push_back(std::move(c));
    }
}

std::shared_ptr<Program> Compile(const std::string& src) {
    auto p = std::make_shared<Program>();
    std::vector<Token> toks = Lex(src, p->diags);
    Parse(toks, *p);
    AddBuiltins(*p);
    Check(*p);
    return p;
}

} // namespace okay::script
