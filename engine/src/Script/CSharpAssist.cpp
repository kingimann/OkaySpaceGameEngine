// Code help for C# scripts (okay/Scripting/CSharpAssist.hpp): member lists and signature
// help from the binding table and the script's own syntax tree. Our own code: it walks
// the parser's output (no checking, so a half-typed script still gets help).
#include "okay/Scripting/CSharpAssist.hpp"
#include "ScriptInternal.hpp"
#include <algorithm>
#include <set>

namespace okay::csharp {

using namespace okay::script;

namespace {

std::string RefName(const TypeRef& t) {
    std::string n = t.name;
    if (!t.args.empty()) {
        n += "<";
        for (std::size_t i = 0; i < t.args.size(); ++i) n += (i ? ", " : "") + RefName(t.args[i]);
        n += ">";
    }
    for (int r = 0; r < t.rank; ++r) n += "[]";
    return n;
}
std::string ApiTypeName(const Type& t) {
    if (t == Type::ApiStrings()) return "string[]";
    if (t.kind == TK::Any) return "object";
    if (t.kind == TK::Quaternion) return "Quaternion";
    return TypeName(t);
}
std::string ApiSignature(const ApiMethod& m) {
    std::string s = ApiTypeName(m.ret) + " " + m.name + "(";
    for (std::size_t i = 0; i < m.params.size(); ++i)
        s += (i ? ", " : "") + ApiTypeName(m.params[i]) + " " + (i < m.paramNames.size() ? m.paramNames[i] : "arg");
    return s + ")";
}

// One part of a receiver: `name`, `name(...)`, `GetComponent<T>()`, with any [i] after it.
struct Seg { std::string name, typeArg; bool call = false; int index = 0; };
std::vector<Seg> Split(const std::string& expr) {
    std::vector<Seg> out;
    Seg cur;
    int depth = 0;
    std::string word;
    for (std::size_t i = 0; i < expr.size(); ++i) {
        char c = expr[i];
        if (depth == 0 && c == '.') { cur.name = word; out.push_back(cur); cur = Seg{}; word.clear(); continue; }
        if (c == '(' || c == '[' || c == '<') {
            if (depth == 0) {
                if (c == '(') cur.call = true;
                if (c == '[') ++cur.index;
                if (c == '<') {
                    std::size_t gt = expr.find('>', i);
                    if (gt != std::string::npos) { cur.typeArg = expr.substr(i + 1, gt - i - 1); }
                }
            }
            ++depth;
            continue;
        }
        if (c == ')' || c == ']' || c == '>') { if (depth > 0) --depth; continue; }
        if (depth == 0 && c != ' ') word += c;
    }
    cur.name = word;
    out.push_back(cur);
    return out;
}

class Assist {
public:
    explicit Assist(const std::string& src) : api_(EngineApi::Get()) {
        prog_.csharp = true;
        std::vector<ScriptDiagnostic> ignore;
        Parse(Lex(src, ignore), prog_);
    }

    std::string TypeOf(const std::string& expr) {
        std::vector<Seg> segs = Split(expr);
        if (segs.empty() || segs[0].name.empty()) return "";
        std::string t = First(segs[0]);
        for (std::size_t i = 1; i < segs.size() && !t.empty(); ++i) t = Index(MemberType(t, segs[i]), segs[i].index);
        return t;
    }

    std::vector<Member> Members(const std::string& type) {
        std::vector<Member> out;
        std::set<std::string> seen;
        auto add = [&](const std::string& n, bool method, const std::string& ty, const std::string& doc) {
            if (n.empty() || n.rfind("get_", 0) == 0 || n.rfind("set_", 0) == 0 || !seen.insert(n).second) return;
            out.push_back({n, method, ty, doc});
        };
        const bool isStatic = type.rfind("static:", 0) == 0;
        const std::string name = isStatic ? type.substr(7) : type;
        if (const ApiType* at = Api(name)) {
            for (const ApiProperty& p : isStatic ? at->staticProps : at->props) add(p.name, false, ApiTypeName(p.type), p.doc);
            for (const ApiMethod& m : isStatic ? at->staticMethods : at->methods) add(m.name, true, ApiTypeName(m.ret), m.doc);
            if (!isStatic && name == "Vector3") for (const char* c : {"x", "y", "z"}) add(c, false, "float", "");
            if (!isStatic && name == "Quaternion") for (const char* c : {"x", "y", "z", "w"}) add(c, false, "float", "");
            if (!isStatic && at->isComponent && name != "GameObject") add("gameObject", false, "GameObject", "The object this component is on.");
        } else if (name == "string") {
            if (const ApiType* st = api_.Find("string") >= 0 ? &api_.types[(std::size_t)api_.Find("string")] : nullptr) {
                for (const ApiProperty& p : st->props) add(p.name, false, ApiTypeName(p.type), p.doc);
                for (const ApiMethod& m : st->methods) add(m.name, true, ApiTypeName(m.ret), m.doc);
            }
            add("ToString", true, "string", "");
        } else if (Generic(name) == "List" || EndsArray(name)) {
            const bool list = !EndsArray(name);
            if (list) {
                add("Count", false, "int", "How many items.");
                for (const char* m : {"Add", "AddRange", "Insert", "Remove", "RemoveAt", "Clear", "Contains", "IndexOf", "Sort", "Reverse",
                                      "ToArray", "Find", "FindAll", "FindIndex", "Exists", "RemoveAll", "ForEach"}) add(m, true, "", "");
            } else {
                add("Length", false, "int", "How many items.");
                for (const char* m : {"Contains", "ToArray"}) add(m, true, "", "");
            }
            for (const char* m : {"Where", "Select", "OrderBy", "OrderByDescending", "First", "FirstOrDefault", "Last", "LastOrDefault",
                                  "Any", "All", "Count", "Sum", "Min", "Max", "Average", "ToList"}) add(m, true, "", "");
        } else if (Generic(name) == "Dictionary") {
            add("Count", false, "int", "How many entries.");
            add("Keys", false, "", ""); add("Values", false, "", "");
            for (const char* m : {"Add", "TryAdd", "Remove", "ContainsKey", "ContainsValue", "Clear", "GetValueOrDefault"}) add(m, true, "", "");
        } else if (name == "int" || name == "float" || name == "bool") {
            if (isStatic) { if (name != "bool") { add("Parse", true, name, ""); add("TryParse", true, "bool", ""); add("MaxValue", false, name, ""); add("MinValue", false, name, ""); } }
            else add("ToString", true, "string", "");
        } else if (int e = EnumIndex(name); e >= 0 && isStatic) {
            for (const EnumMember& m : prog_.enums[(std::size_t)e].members) add(m.name, false, name, "");
        } else {
            for (int k = ClassIndex(name); k >= 0; k = BaseOf(k)) {
                const ClassDecl& c = prog_.classes[(std::size_t)k];
                for (const FieldDecl& f : c.fields)
                    if ((f.isStatic || f.isConst) == isStatic) add(f.name, false, RefName(f.type), "");
                for (const PropDecl& p : c.props) if (p.isStatic == isStatic) add(p.name, false, RefName(p.type), "");
                for (const MethodDecl& m : c.methods)
                    if (!m.isCtor && m.isStatic == isStatic) add(m.name, true, m.ret.name, "");
                if (!isStatic && !c.bases.empty() && c.bases[0] == "Behaviour") {   // a Behaviour has the object's members too
                    add("gameObject", false, "GameObject", "The object this script is on.");
                    add("transform", false, "Transform", "The object's Transform.");
                    add("enabled", false, "bool", "Whether this script runs.");
                }
            }
        }
        std::sort(out.begin(), out.end(), [](const Member& a, const Member& b) { return a.name < b.name; });
        return out;
    }

    std::vector<Signature> Signatures(const std::string& receiver, const std::string& method) {
        std::vector<Signature> out;
        auto fromApi = [&](const std::vector<ApiMethod>& ms) {
            for (const ApiMethod& m : ms) {
                if (m.name != method) continue;
                Signature s;
                s.text = ApiSignature(m);
                for (std::size_t i = 0; i < m.params.size(); ++i)
                    s.params.push_back(ApiTypeName(m.params[i]) + " " + (i < m.paramNames.size() ? m.paramNames[i] : "arg"));
                s.doc = m.doc;
                out.push_back(std::move(s));
            }
        };
        auto fromClass = [&](int k, bool ctor) {
            for (; k >= 0; k = BaseOf(k)) {
                for (const MethodDecl& m : prog_.classes[(std::size_t)k].methods) {
                    if (ctor ? !m.isCtor : (m.isCtor || m.name != method)) continue;
                    Signature s;
                    s.text = (ctor ? "new " + prog_.classes[(std::size_t)k].name : m.ret.name + " " + m.name) + "(";
                    for (std::size_t i = 0; i < m.params.size(); ++i) {
                        std::string p = RefName(m.params[i].type) + " " + m.params[i].name;
                        if (m.params[i].def) p += " = ...";
                        s.params.push_back(p);
                        s.text += (i ? ", " : "") + p;
                    }
                    s.text += ")";
                    out.push_back(std::move(s));
                }
                if (ctor) break;
            }
        };
        if (receiver.empty()) {
            if (int k = ClassIndex(method); k >= 0) { fromClass(k, true); return out; }   // new Enemy(
            int comp = Component();
            if (comp >= 0) fromClass(comp, false);
            if (out.empty()) for (std::size_t k = 0; k < prog_.classes.size(); ++k) fromClass((int)k, false);
            if (out.empty() && method == "Vector3") { Signature s; s.text = "new Vector3(float x, float y, float z)"; s.params = {"float x", "float y", "float z"}; out.push_back(s); }
            if (out.empty() && method == "Quaternion") { Signature s; s.text = "new Quaternion(float x, float y, float z, float w)"; s.params = {"float x", "float y", "float z", "float w"}; out.push_back(s); }
            return out;
        }
        const std::string t = TypeOf(receiver);
        if (t.empty()) return out;
        const bool isStatic = t.rfind("static:", 0) == 0;
        const std::string name = isStatic ? t.substr(7) : t;
        if (const ApiType* at = Api(name)) fromApi(isStatic ? at->staticMethods : at->methods);
        else if (name == "string") { if (api_.Find("string") >= 0) fromApi(api_.types[(std::size_t)api_.Find("string")].methods); }
        else if (int k = ClassIndex(name); k >= 0) fromClass(k, false);
        return out;
    }

private:
    Program prog_;
    const EngineApi& api_;

    static bool EndsArray(const std::string& t) { return t.size() > 2 && t.compare(t.size() - 2, 2, "[]") == 0; }
    static std::string Generic(const std::string& t) { auto lt = t.find('<'); return lt == std::string::npos ? std::string() : t.substr(0, lt); }
    static std::vector<std::string> TypeArgs(const std::string& t) {
        std::vector<std::string> out;
        auto lt = t.find('<');
        if (lt == std::string::npos || t.back() != '>') return out;
        int depth = 0;
        std::string cur;
        for (std::size_t i = lt + 1; i + 1 < t.size(); ++i) {
            char c = t[i];
            if (c == '<') ++depth; else if (c == '>') --depth;
            if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); continue; }
            if (c != ' ') cur += c;
        }
        out.push_back(cur);
        return out;
    }
    const ApiType* Api(const std::string& n) const {
        if (n == "string") return nullptr;   // the members of text are listed on their own
        const int i = api_.Find(n);
        return i >= 0 ? &api_.types[(std::size_t)i] : nullptr;
    }
    int ClassIndex(const std::string& n) const {
        for (std::size_t i = 0; i < prog_.classes.size(); ++i) if (prog_.classes[i].name == n) return (int)i;
        return -1;
    }
    int EnumIndex(const std::string& n) const {
        for (std::size_t i = 0; i < prog_.enums.size(); ++i) if (prog_.enums[i].name == n) return (int)i;
        return -1;
    }
    int BaseOf(int k) const {
        for (const std::string& b : prog_.classes[(std::size_t)k].bases) if (int i = ClassIndex(b); i >= 0 && i != k) return i;
        return -1;
    }
    int Component() const {
        for (std::size_t i = 0; i < prog_.classes.size(); ++i)
            if (!prog_.classes[i].bases.empty() && prog_.classes[i].bases[0] == "Behaviour") return (int)i;
        return prog_.classes.empty() ? -1 : 0;
    }
    // The type after [i] (as many times as written).
    static std::string Index(std::string t, int times) {
        for (int i = 0; i < times && !t.empty(); ++i) {
            if (EndsArray(t)) t = t.substr(0, t.size() - 2);
            else if (Generic(t) == "List") { auto a = TypeArgs(t); t = a.empty() ? "" : a[0]; }
            else if (Generic(t) == "Dictionary") { auto a = TypeArgs(t); t = a.size() < 2 ? "" : a[1]; }
            else t.clear();
        }
        return t;
    }

    // A variable's declared type: a local or parameter (any method), else a field.
    std::string DeclType(const std::string& n) {
        std::string found;
        std::function<void(const Stmt&)> walk = [&](const Stmt& s) {
            if (!found.empty()) return;
            for (const LocalDecl& d : s.locals)
                if (d.name == n) { found = d.type.name == "var" ? FromInit(d.init.get()) : RefName(d.type); return; }
            for (const auto& c : s.body) if (c) walk(*c);
            for (const StmtPtr* p : {&s.then, &s.otherwise, &s.init, &s.loopBody, &s.finallyBody}) if (*p) walk(**p);
            for (const auto& sec : s.sections) for (const auto& c : sec.body) walk(*c);
            for (const auto& c : s.catches) { if (c.name == n) { found = RefName(c.type); return; } if (c.body) walk(*c.body); }
        };
        int comp = Component();
        std::vector<int> order;
        if (comp >= 0) order.push_back(comp);
        for (std::size_t k = 0; k < prog_.classes.size(); ++k) if ((int)k != comp) order.push_back((int)k);
        for (int k : order) {
            for (const MethodDecl& m : prog_.classes[(std::size_t)k].methods) {
                for (const Param& p : m.params) if (p.name == n) return RefName(p.type);
                if (m.body) walk(*m.body);
                if (!found.empty()) return found;
            }
            for (const FieldDecl& f : prog_.classes[(std::size_t)k].fields) if (f.name == n) return RefName(f.type);
        }
        return "";
    }
    std::string FromInit(const Expr* e) {
        if (!e) return "";
        switch (e->kind) {
            case EK::IntLit: return "int";
            case EK::FloatLit: return "float";
            case EK::StringLit: case EK::Interp: return "string";
            case EK::BoolLit: return "bool";
            case EK::New: return RefName(e->typeArg);
            case EK::Call:
                if (e->name == "GetComponent") return e->typeArg.name;
                return "";
            default: return "";
        }
    }
    std::string First(const Seg& s) {
        std::string t;
        if (s.name == "this" || s.name == "base") { int c = Component(); t = c >= 0 ? prog_.classes[(std::size_t)c].name : ""; }
        else if (s.name == "GetComponent" && !s.typeArg.empty()) t = s.typeArg;
        else if (s.call) {
            int comp = Component();
            for (int k = comp; k >= 0; k = BaseOf(k))
                for (const MethodDecl& m : prog_.classes[(std::size_t)k].methods) if (m.name == s.name) { t = RefName(m.ret); break; }
        } else if (!(t = DeclType(s.name)).empty()) {
        } else if (s.name == "transform") t = "Transform";
        else if (s.name == "gameObject") t = "GameObject";
        else if (s.name == "int" || s.name == "float" || s.name == "string" || s.name == "bool") t = "static:" + s.name;
        else if (api_.Find(s.name) >= 0 || ClassIndex(s.name) >= 0 || EnumIndex(s.name) >= 0) t = "static:" + s.name;
        return Index(t, s.index);
    }
    std::string MemberType(const std::string& type, const Seg& s) {
        if (s.name == "GetComponent" && !s.typeArg.empty()) return s.typeArg;
        const bool isStatic = type.rfind("static:", 0) == 0;
        const std::string name = isStatic ? type.substr(7) : type;
        if (EnumIndex(name) >= 0 && isStatic) return name;
        if ((name == "Vector3" || name == "Quaternion") && !isStatic && s.name.size() == 1) return "float";
        if (const ApiType* at = Api(name)) {
            if (s.call) { for (const ApiMethod& m : isStatic ? at->staticMethods : at->methods) if (m.name == s.name) return ApiTypeName(m.ret); }
            else for (const ApiProperty& p : isStatic ? at->staticProps : at->props) if (p.name == s.name) return ApiTypeName(p.type);
            if (s.name == "gameObject") return "GameObject";
            return "";
        }
        if (name == "string") {
            if (const ApiType* st = api_.Find("string") >= 0 ? &api_.types[(std::size_t)api_.Find("string")] : nullptr) {
                if (s.call) { for (const ApiMethod& m : st->methods) if (m.name == s.name) return ApiTypeName(m.ret); }
                else for (const ApiProperty& p : st->props) if (p.name == s.name) return ApiTypeName(p.type);
            }
            return s.name == "ToString" ? "string" : "";
        }
        if (Generic(name) == "List" || EndsArray(name)) {
            const std::string elem = Index(name, 1);
            if (s.name == "Count" || s.name == "Length" || s.name == "IndexOf" || s.name == "FindIndex" || s.name == "RemoveAll") return "int";
            if (s.name == "Find" || s.name == "First" || s.name == "FirstOrDefault" || s.name == "Last" || s.name == "LastOrDefault") return elem;
            if (s.name == "Where" || s.name == "OrderBy" || s.name == "OrderByDescending" || s.name == "FindAll" || s.name == "ToList") return "List<" + elem + ">";
            if (s.name == "ToArray") return elem + "[]";
            if (s.name == "Any" || s.name == "All" || s.name == "Contains" || s.name == "Exists") return "bool";
            return "";
        }
        if (Generic(name) == "Dictionary") {
            auto a = TypeArgs(name);
            if (s.name == "Count") return "int";
            if (a.size() == 2 && s.name == "Keys") return "List<" + a[0] + ">";
            if (a.size() == 2 && s.name == "Values") return "List<" + a[1] + ">";
            if (a.size() == 2 && s.name == "GetValueOrDefault") return a[1];
            return "";
        }
        for (int k = ClassIndex(name); k >= 0; k = BaseOf(k)) {
            const ClassDecl& c = prog_.classes[(std::size_t)k];
            if (s.call) { for (const MethodDecl& m : c.methods) if (m.name == s.name && !m.isCtor) return RefName(m.ret); }
            else {
                for (const FieldDecl& f : c.fields) if (f.name == s.name) return RefName(f.type);
                for (const PropDecl& p : c.props) if (p.name == s.name) return RefName(p.type);
                if (s.name == "transform") return "Transform";
                if (s.name == "gameObject") return "GameObject";
            }
        }
        return "";
    }
};

} // namespace

bool IsEngineComponent(const std::string& name) {
    const EngineApi& api = EngineApi::Get();
    const int i = api.Find(name);
    return i >= 0 && api.types[(std::size_t)i].isComponent;
}

std::string ReceiverType(const std::string& source, const std::string& receiver) { return Assist(source).TypeOf(receiver); }

std::vector<Member> MembersAfter(const std::string& source, const std::string& receiver) {
    Assist a(source);
    const std::string t = a.TypeOf(receiver);
    return t.empty() ? std::vector<Member>{} : a.Members(t);
}

std::vector<Signature> SignaturesFor(const std::string& source, const std::string& receiver, const std::string& method) {
    return Assist(source).Signatures(receiver, method);
}

} // namespace okay::csharp
