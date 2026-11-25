// Script assist from the engine's binding table: see ScriptAssistBindings.hpp. Written for this editor from the binding
// layer's own API (okay/Scripting/ScriptBindings.hpp, okay/Scene/PropertyRegistry.hpp).
#include "ScriptAssistBindings.hpp"

#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scripting/ScriptBindings.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace oked {
namespace {

namespace sb = okay::scriptbind;

const char* TypeWord(okay::PropertyType t) {
    switch (t) {
        case okay::PropertyType::Bool: return "bool";
        case okay::PropertyType::Int: return "int";
        case okay::PropertyType::Float: return "float";
        case okay::PropertyType::String: return "string";
        case okay::PropertyType::Vec2: return "Vector2";
        case okay::PropertyType::Vec3: return "Vector3";
        case okay::PropertyType::Vec4: return "Vector4";
        case okay::PropertyType::Quat: return "Quaternion";
        case okay::PropertyType::Color: return "Color";
        case okay::PropertyType::Enum: return "enum";
        case okay::PropertyType::AssetPath: return "asset";
        case okay::PropertyType::ObjectRef: return "GameObject";
        default: return "value";
    }
}

// "void AddForce(Vector3, [int])": optional arguments in brackets.
std::string MethodLine(const sb::MethodBinding& m) {
    std::string s = std::string(sb::ArgKindTypeName(m.ret)) + " " + m.name + "(";
    for (size_t i = 0; i < m.params.size(); ++i) {
        if (i) s += ", ";
        const bool opt = (int)i >= m.required;
        if (opt) s += "[";
        s += sb::ArgKindTypeName(m.params[i]);
        if (opt) s += "]";
    }
    return s + ")";
}

struct Cache {
    std::unordered_map<std::string, std::vector<BoundMember>> byClass;   // by the class's own name
    std::map<std::string, std::string> docs;
    std::unordered_map<std::string, std::string> signatures;             // method name -> lines
    bool built = false;
};

Cache& Built() {
    static Cache c;
    if (c.built) return c;
    c.built = true;
    std::unordered_map<std::string, int> memberDocLines;
    for (const sb::ClassBinding& cls : sb::AllClasses()) {
        std::vector<BoundMember> v;
        v.push_back(BoundMember{"gameObject", "GameObject gameObject: the object this component is on", false});
        v.push_back(BoundMember{"enabled", "bool enabled: whether the component runs", false});
        const auto info = okay::PropertyRegistry::FindByName(cls.name);
        if (info) {
            for (const okay::PropertyDesc& p : info->properties) {
                if (p.level == okay::PropertyLevel::Hidden || p.name.empty()) continue;
                std::string d = std::string(TypeWord(p.type)) + " " + p.name;
                if (!p.enumOptions.empty()) {
                    d += " (";
                    for (size_t i = 0; i < p.enumOptions.size() && i < 8; ++i) d += (i ? ", " : "") + p.enumOptions[i];
                    if (p.enumOptions.size() > 8) d += ", ...";
                    d += ")";
                }
                if (!p.unit.empty()) d += ", " + p.unit;
                if (!p.tooltip.empty()) d += ": " + p.tooltip;
                v.push_back(BoundMember{p.name, d, false});
            }
        }
        std::map<std::string, std::string> methods;   // name -> its lines (overloads)
        for (const sb::MethodBinding& m : cls.methods) {
            if (!m.name || !*m.name) continue;
            std::string& lines = methods[m.name];
            const std::string line = MethodLine(m);
            if (lines.find(line) == std::string::npos) lines += (lines.empty() ? "" : "\n") + line;
            std::string& sig = c.signatures[m.name];
            const std::string qual = cls.name + "." + line.substr(line.find(' ') + 1);
            if (std::count(sig.begin(), sig.end(), '\n') < 5 && sig.find(qual) == std::string::npos) sig += (sig.empty() ? "" : "\n") + qual;
        }
        for (const auto& m : methods) v.push_back(BoundMember{m.first, m.second, true});
        // one entry per name (a property and a method of one name: the property)
        std::stable_sort(v.begin(), v.end(), [](const BoundMember& a, const BoundMember& b) { return a.name < b.name; });
        v.erase(std::unique(v.begin(), v.end(), [](const BoundMember& a, const BoundMember& b) { return a.name == b.name; }), v.end());
        // hover docs
        std::string cdoc = cls.name + " (component)";
        if (info && !info->description.empty()) cdoc += ": " + info->description;
        c.docs.emplace(cls.name, cdoc);
        for (const BoundMember& m : v) {
            if (m.name == "gameObject" || m.name == "enabled") continue;
            int& n = memberDocLines[m.name];
            if (n >= 4) continue;
            std::string& d = c.docs[m.name];
            d += (d.empty() ? "" : "\n") + cls.name + ": " + m.detail.substr(0, m.detail.find('\n'));
            ++n;
        }
        c.byClass.emplace(cls.name, std::move(v));
    }
    return c;
}

bool WordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// The class named by a GetComponent call at the end of `e`: GetComponent("X"), GetComponent('X'), GetComponent<X>().
std::string FromGetComponent(const std::string& e) {
    for (const char* call : {"GetComponent", "getComponent", "get_component"}) {
        const size_t at = e.rfind(call);
        if (at == std::string::npos || (at > 0 && WordChar(e[at - 1]))) continue;
        size_t i = at + std::char_traits<char>::length(call);
        if (i < e.size() && e[i] == '<') {
            const size_t gt = e.find('>', i);
            if (gt != std::string::npos) return e.substr(i + 1, gt - i - 1);
        } else if (i < e.size() && e[i] == '(') {
            ++i;
            while (i < e.size() && e[i] == ' ') ++i;
            if (i < e.size() && (e[i] == '"' || e[i] == '\'')) {
                const char q = e[i];
                const size_t end = e.find(q, i + 1);
                if (end != std::string::npos) return e.substr(i + 1, end - i - 1);
            }
        }
    }
    return std::string();
}

} // namespace

const std::vector<BoundMember>* BoundMembersOf(const std::string& className) {
    if (className.empty()) return nullptr;
    const sb::ClassBinding* cls = sb::FindClass(className);
    if (!cls) return nullptr;
    Cache& c = Built();
    const auto it = c.byClass.find(cls->name);
    return it == c.byClass.end() ? nullptr : &it->second;
}

std::string BoundClassOf(const std::string& src, const std::string& receiver) {
    std::string r = receiver;
    while (!r.empty() && r.back() == ' ') r.pop_back();
    if (r.empty()) return std::string();
    auto known = [](const std::string& name) -> std::string {
        const sb::ClassBinding* cls = name.empty() ? nullptr : sb::FindClass(name);
        return cls ? cls->name : std::string();
    };
    if (r.back() == ')' || r.back() == '>') return known(FromGetComponent(r));
    // a variable: `name = GetComponent(...)` anywhere in the script, or `Type name` with a bound class for Type
    bool ident = true;
    for (char ch : r) if (!WordChar(ch)) { ident = false; break; }
    if (!ident) return std::string();
    const size_t n = src.size();
    for (size_t i = src.find(r); i != std::string::npos; i = src.find(r, i + 1)) {
        if (i > 0 && WordChar(src[i - 1])) continue;
        const size_t e = i + r.size();
        if (e < n && WordChar(src[e])) continue;
        size_t a = e; while (a < n && (src[a] == ' ' || src[a] == '\t')) ++a;
        if (a < n && src[a] == '=' && !(a + 1 < n && src[a + 1] == '=')) {
            size_t lineEnd = src.find_first_of(";\n", a);
            const std::string init = src.substr(a + 1, lineEnd == std::string::npos ? std::string::npos : lineEnd - a - 1);
            // the first call in the initializer, up to its closing bracket
            const size_t g = init.find("etComponent");
            if (g != std::string::npos) {
                size_t close = init.find_first_of(")>", g);
                if (close != std::string::npos) {
                    const std::string k = known(FromGetComponent(init.substr(0, close + 1)));
                    if (!k.empty()) return k;
                }
            }
        }
        // `Type name`: the word before it
        size_t b = i; while (b > 0 && (src[b - 1] == ' ' || src[b - 1] == '\t')) --b;
        if (b == i) continue;
        size_t ts = b; while (ts > 0 && WordChar(src[ts - 1])) --ts;
        if (ts == b) continue;
        const std::string ty = src.substr(ts, b - ts);
        if (ty == "var" || ty == "let" || ty == "local" || ty == "new" || ty == "return") continue;
        const std::string k = known(ty);
        if (!k.empty()) return k;
    }
    return std::string();
}

std::vector<std::string> BoundMemberWords(const std::string& src, const std::string& receiver) {
    std::vector<std::string> out;
    const std::vector<BoundMember>* m = BoundMembersOf(BoundClassOf(src, receiver));
    if (!m) return out;
    for (const BoundMember& b : *m) out.push_back(b.method ? b.name + "(" : b.name);
    return out;
}

const std::string* BoundMethodSignature(const std::string& method) {
    Cache& c = Built();
    const auto it = c.signatures.find(method);
    return it == c.signatures.end() ? nullptr : &it->second;
}

const std::map<std::string, std::string>& BoundDocs() { return Built().docs; }

} // namespace oked
