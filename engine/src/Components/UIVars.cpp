// UI variable access and the small condition language (okay/Components/UIVars.hpp).
#include "okay/Components/UIVars.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace okay {
namespace uivars {

std::string FormatNumber(float f) {
    if (std::fabs(f - (float)(long long)f) < 1e-4f && std::fabs(f) < 1e15f) return std::to_string((long long)f);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", f);
    std::string s = buf;
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

namespace {

bool ParseNumber(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (!end || end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

Value FromVs(const vs::VsValue& v) {
    Value r;
    r.found = true;
    if (v.IsNumber()) { r.isNumber = true; r.number = v.AsFloat(); r.text = FormatNumber(r.number); }
    else if (v.IsBool()) { r.isNumber = true; r.number = v.AsBool() ? 1.0f : 0.0f; r.text = v.AsBool() ? "true" : "false"; }
    else r.text = v.AsString();
    return r;
}

const vs::VarMap* SceneVars(const GameObject* go) {
    const Scene* s = go ? go->scene() : nullptr;
    return s ? &vs::SceneVariables(s) : nullptr;
}

} // namespace

Value Read(const GameObject* go, const std::string& name) {
    Value r;
    if (name.empty()) return r;
    if (const vs::VarMap* sv = SceneVars(go)) {
        auto it = sv->find(name);
        if (it != sv->end()) return FromVs(it->second);
    }
    {
        auto& gv = vs::GlobalVariables();
        auto it = gv.find(name);
        if (it != gv.end()) return FromVs(it->second);
    }
    {
        auto& sv = ActionList::StrVars();
        auto it = sv.find(name);
        if (it != sv.end()) { r.found = true; r.text = it->second; r.isNumber = ParseNumber(r.text, r.number); return r; }
    }
    {
        auto& v = ActionList::Vars();
        auto it = v.find(name);
        if (it != v.end()) { r.found = r.isNumber = true; r.number = it->second; r.text = FormatNumber(r.number); return r; }
    }
    if (Prefs::Has(name)) {
        r.found = true;
        r.text = Prefs::GetString(name, "");
        r.isNumber = ParseNumber(r.text, r.number);
    }
    return r;
}

bool ReadNumber(const GameObject* go, const std::string& name, float& out) {
    const Value v = Read(go, name);
    if (!v.found || !v.isNumber) return false;
    out = v.number;
    return true;
}

void WriteNumber(GameObject* go, const std::string& name, float v) {
    if (name.empty()) return;
    if (Scene* s = go ? go->scene() : nullptr) {
        auto& sv = vs::SceneVariables(s);
        auto it = sv.find(name);
        if (it != sv.end()) { it->second = vs::VsValue(v); return; }
    }
    {
        auto& gv = vs::GlobalVariables();
        auto it = gv.find(name);
        if (it != gv.end()) { it->second = vs::VsValue(v); return; }
    }
    ActionList::Vars()[name] = v;
    ActionList::StrVars().erase(name);   // a text variable of the same name would hide the number
}

void WriteText(GameObject* go, const std::string& name, const std::string& text) {
    (void)go;
    if (name.empty()) return;
    ActionList::StrVars()[name] = text;
}

std::string FillTemplate(const std::string& format, const std::map<std::string, std::string>& fields) {
    std::string out;
    out.reserve(format.size());
    for (std::size_t i = 0; i < format.size(); ++i) {
        const char c = format[i];
        if (c == '{') {
            if (i + 1 < format.size() && format[i + 1] == '{') { out += '{'; ++i; continue; }
            const std::size_t end = format.find('}', i + 1);
            if (end == std::string::npos) { out += c; continue; }
            std::string key = format.substr(i + 1, end - i - 1), fallback;
            const std::size_t bar = key.find('|');
            if (bar != std::string::npos) { fallback = key.substr(bar + 1); key.erase(bar); }
            auto it = fields.find(key);
            out += (it != fields.end() && !it->second.empty()) ? it->second : fallback;
            i = end;
        } else if (c == '}' && i + 1 < format.size() && format[i + 1] == '}') {
            out += '}'; ++i;
        } else out += c;
    }
    return out;
}

// ---- the condition language -----------------------------------------------------------------------------------------
namespace {

struct Tok { enum K { End, Num, Str, Name, Op, LP, RP } k = End; std::string s; float n = 0.0f; };

// A hand-written recursive-descent evaluator. Values are Value (found / number / text).
class Parser {
public:
    Parser(const std::string& s, const std::function<Value(const std::string&)>& lk) : m_src(s), m_lookup(lk) { Advance(); }
    bool Run(std::string* err) {
        const Value v = Or();
        if (m_error.empty() && m_tok.k != Tok::End) m_error = "unexpected text after the condition";
        if (err) *err = m_error;
        return Truthy(v);
    }
    static bool Truthy(const Value& v) {
        if (!v.found) return false;
        if (v.isNumber) return v.number != 0.0f;
        std::string t;
        for (char c : v.text) t += (char)std::tolower((unsigned char)c);
        return !(t.empty() || t == "0" || t == "false" || t == "no" || t == "off");
    }
private:
    std::string m_src;
    std::function<Value(const std::string&)> m_lookup;
    std::size_t m_pos = 0;
    Tok m_tok;
    std::string m_error;

    void Fail(const char* why) { if (m_error.empty()) m_error = why; }
    void Advance() {
        // Re-lex from m_pos each time (a tiny state machine; conditions are short).
        std::size_t i = m_pos;
        const std::string& s = m_src;
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        Tok t;
        if (i >= s.size()) { m_pos = i; m_tok = t; return; }
        const char c = s[i];
        auto two = [&](const char* op) { return s.compare(i, 2, op) == 0; };
        if (c == '(') { t.k = Tok::LP; ++i; }
        else if (c == ')') { t.k = Tok::RP; ++i; }
        else if (c == '"' || c == '\'') {
            const char q = c; ++i;
            while (i < s.size() && s[i] != q) t.s += s[i++];
            if (i >= s.size()) Fail("a quote is not closed"); else ++i;
            t.k = Tok::Str;
        } else if (two("==") || two("!=") || two("<=") || two(">=") || two("&&") || two("||")) { t.k = Tok::Op; t.s = s.substr(i, 2); i += 2; }
        else if (c == '<' || c == '>' || c == '!') { t.k = Tok::Op; t.s = std::string(1, c); ++i; }
        else if (std::isdigit((unsigned char)c) || (c == '-' && i + 1 < s.size() && (std::isdigit((unsigned char)s[i + 1]) || s[i + 1] == '.')) || (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) {
            char* end = nullptr;
            t.n = std::strtof(s.c_str() + i, &end);
            i = (std::size_t)(end - s.c_str());
            t.k = Tok::Num;
        } else if (std::isalpha((unsigned char)c) || c == '_') {
            while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' || s[i] == ':')) t.s += s[i++];
            t.k = Tok::Name;
        } else { Fail("a character the condition language does not know"); ++i; t.k = Tok::End; m_pos = i; m_tok = t; return; }
        m_pos = i;
        m_tok = t;
    }
    static Value Num(float v) { Value r; r.found = r.isNumber = true; r.number = v; r.text = FormatNumber(v); return r; }
    Value Or() {
        Value l = And();
        while (m_tok.k == Tok::Op && m_tok.s == "||") { Advance(); Value r = And(); l = Num((Truthy(l) || Truthy(r)) ? 1.0f : 0.0f); }
        return l;
    }
    Value And() {
        Value l = Not();
        while (m_tok.k == Tok::Op && m_tok.s == "&&") { Advance(); Value r = Not(); l = Num((Truthy(l) && Truthy(r)) ? 1.0f : 0.0f); }
        return l;
    }
    Value Not() {
        if (m_tok.k == Tok::Op && m_tok.s == "!") { Advance(); return Num(Truthy(Not()) ? 0.0f : 1.0f); }
        return Compare();
    }
    Value Compare() {
        Value l = Operand();
        if (m_tok.k == Tok::Op && (m_tok.s == "==" || m_tok.s == "!=" || m_tok.s == "<" || m_tok.s == ">" || m_tok.s == "<=" || m_tok.s == ">=")) {
            const std::string op = m_tok.s;
            Advance();
            Value r = Operand();
            bool res = false;
            if (!l.found || !r.found) res = (op == "!=");   // a name nobody set equals nothing
            else if (l.isNumber && r.isNumber) {
                const float a = l.number, b = r.number;
                res = op == "==" ? a == b : op == "!=" ? a != b : op == "<" ? a < b : op == ">" ? a > b : op == "<=" ? a <= b : a >= b;
            } else {
                const int c = l.text.compare(r.text);
                res = op == "==" ? c == 0 : op == "!=" ? c != 0 : op == "<" ? c < 0 : op == ">" ? c > 0 : op == "<=" ? c <= 0 : c >= 0;
            }
            return Num(res ? 1.0f : 0.0f);
        }
        return l;
    }
    Value Operand() {
        Value r;
        switch (m_tok.k) {
            case Tok::Num: r = Num(m_tok.n); Advance(); return r;
            case Tok::Str: r.found = true; r.text = m_tok.s; r.isNumber = ParseNumber(r.text, r.number); Advance(); return r;
            case Tok::Name: {
                const std::string name = m_tok.s;
                Advance();
                if (name == "true") return Num(1.0f);
                if (name == "false") return Num(0.0f);
                return m_lookup ? m_lookup(name) : Value();
            }
            case Tok::LP: {
                Advance();
                r = Or();
                if (m_tok.k == Tok::RP) Advance(); else Fail("a ) is missing");
                return r;
            }
            case Tok::Op:
                if (m_tok.s == "!") { Advance(); return Num(Truthy(Operand()) ? 0.0f : 1.0f); }
                Fail("a value was expected"); Advance(); return r;
            default: Fail("the condition ends too early"); return r;
        }
    }
};

} // namespace

bool EvaluateWith(const std::function<Value(const std::string&)>& lookup, const std::string& condition, std::string* error) {
    if (condition.find_first_not_of(" \t\r\n") == std::string::npos) { if (error) error->clear(); return true; }   // no condition: always true
    Parser p(condition, lookup);
    std::string err;
    const bool v = p.Run(&err);
    if (error) *error = err;
    return err.empty() ? v : false;
}

bool Evaluate(const GameObject* go, const std::string& condition, std::string* error) {
    return EvaluateWith([go](const std::string& n) { return Read(go, n); }, condition, error);
}

} // namespace uivars
} // namespace okay
