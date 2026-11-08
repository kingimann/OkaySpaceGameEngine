#include "okay/Core/Cvar.hpp"
#include "okay/Core/SaveData.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <locale>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace okay {

namespace {
std::map<std::string, std::unique_ptr<Cvar>>& Registry() {
    static std::map<std::string, std::unique_ptr<Cvar>> r; return r;
}
std::map<std::string, std::string>& Held() {   // loaded values for cvars not registered yet
    static std::map<std::string, std::string> h; return h;
}
std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string NumText(double v, bool isInt) {
    std::ostringstream o;
    if (isInt) o << (long long)v; else { o.imbue(std::locale::classic()); o << v; }
    return o.str();
}
}

const char* Cvar::TypeName() const {
    switch (m_type) { case Type::Int: return "int"; case Type::Float: return "float";
                      case Type::Bool: return "bool"; default: return "string"; }
}
int   Cvar::AsInt() const   { return m_type == Type::String ? std::atoi(m_text.c_str()) : (int)m_value; }
float Cvar::AsFloat() const { return m_type == Type::String ? (float)std::atof(m_text.c_str()) : (float)m_value; }
bool  Cvar::AsBool() const {
    if (m_type == Type::String) { const std::string l = Lower(m_text); return !(l.empty() || l == "0" || l == "false" || l == "off" || l == "no"); }
    return m_value != 0.0;
}
std::string Cvar::AsString() const {
    switch (m_type) {
        case Type::Int:    return NumText(m_value, true);
        case Type::Float:  return NumText(m_value, false);
        case Type::Bool:   return m_value != 0.0 ? "true" : "false";
        default:           return m_text;
    }
}
std::string Cvar::DefaultString() const {
    switch (m_type) {
        case Type::Int:    return NumText(m_default, true);
        case Type::Float:  return NumText(m_default, false);
        case Type::Bool:   return m_default != 0.0 ? "true" : "false";
        default:           return m_defaultText;
    }
}

void Cvar::Assign(double number, const std::string& text) {
    const bool same = m_type == Type::String ? text == m_text : number == m_value;
    if (same) return;
    m_value = number; m_text = text;
    const auto snapshot = m_callbacks;   // a callback may add/remove callbacks
    for (const Cb& cb : snapshot) cb.fn(*this);
}

bool Cvar::Set(const std::string& text, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    if (m_type == Type::String) { Assign(0.0, text); return true; }
    const std::string t = Trim(text);
    if (m_type == Type::Bool) {
        const std::string l = Lower(t);
        if (l == "1" || l == "true" || l == "on" || l == "yes") { SetBool(true); return true; }
        if (l == "0" || l == "false" || l == "off" || l == "no") { SetBool(false); return true; }
        return fail(m_name + " is a bool (use 1/0, true/false, on/off)");
    }
    if (t.empty()) return fail(m_name + " needs a " + TypeName());
    char* end = nullptr; errno = 0;
    const double v = m_type == Type::Int ? (double)std::strtoll(t.c_str(), &end, 10) : std::strtod(t.c_str(), &end);
    if (errno != 0 || !end || *end != '\0' || v != v) return fail(m_name + " needs a " + TypeName() + ", not '" + t + "'");
    if (m_type == Type::Int) SetInt((int)std::max(-2147483648.0, std::min(2147483647.0, v)));
    else SetFloat((float)v);
    return true;
}

void Cvar::SetInt(int v) {
    if (m_type == Type::String) { Assign(0.0, std::to_string(v)); return; }
    double d = v;
    if (m_hasRange) d = std::max(m_min, std::min(m_max, d));
    Assign(m_type == Type::Bool ? (d != 0.0 ? 1.0 : 0.0) : std::floor(d), "");
}
void Cvar::SetFloat(float v) {
    if (v != v) return;   // NaN: ignore
    if (m_type == Type::String) { Assign(0.0, NumText(v, false)); return; }
    if (m_type == Type::Int) { SetInt((int)std::max(-2147483648.0, std::min(2147483647.0, (double)v))); return; }
    double d = v;
    if (m_hasRange) d = std::max(m_min, std::min(m_max, d));
    Assign(m_type == Type::Bool ? (d != 0.0 ? 1.0 : 0.0) : d, "");
}
void Cvar::SetBool(bool v) {
    if (m_type == Type::String) { Assign(0.0, v ? "true" : "false"); return; }
    if (m_type == Type::Bool) { Assign(v ? 1.0 : 0.0, ""); return; }
    SetFloat(v ? 1.0f : 0.0f);
}
void Cvar::SetString(const std::string& v) {
    if (m_type == Type::String) { Assign(0.0, v); return; }
    Set(v);
}
void Cvar::Reset() { Assign(m_default, m_defaultText); }

int Cvar::OnChange(Callback cb) {
    if (!cb) return 0;
    m_callbacks.push_back({m_nextCb, std::move(cb)});
    return m_nextCb++;
}
void Cvar::RemoveOnChange(int id) {
    m_callbacks.erase(std::remove_if(m_callbacks.begin(), m_callbacks.end(),
                                     [id](const Cb& c) { return c.id == id; }), m_callbacks.end());
}

// ---- registry ----------------------------------------------------------------

// Cvar's constructor is private; Cvars is a friend, so build here.
Cvar& Cvars::Int(const std::string& name, int def, const std::string& desc, int lo, int hi, bool persist) {
    if (Cvar* e = Find(name)) return *e;
    Cvar* c = new Cvar();
    c->m_name = name; c->m_desc = desc; c->m_type = Cvar::Type::Int; c->m_persist = persist;
    c->m_hasRange = lo != hi;
    if (c->m_hasRange) { c->m_min = std::min(lo, hi); c->m_max = std::max(lo, hi); }
    double d = def; if (c->m_hasRange) d = std::max(c->m_min, std::min(c->m_max, d));
    c->m_value = c->m_default = d;
    Registry()[name].reset(c);
    auto h = Held().find(name);
    if (h != Held().end()) { std::string v = h->second; Held().erase(h); c->Set(v); }
    return *c;
}
Cvar& Cvars::Float(const std::string& name, float def, const std::string& desc, float lo, float hi, bool persist) {
    if (Cvar* e = Find(name)) return *e;
    Cvar* c = new Cvar();
    c->m_name = name; c->m_desc = desc; c->m_type = Cvar::Type::Float; c->m_persist = persist;
    c->m_hasRange = lo != hi;
    if (c->m_hasRange) { c->m_min = std::min(lo, hi); c->m_max = std::max(lo, hi); }
    double d = def; if (c->m_hasRange) d = std::max(c->m_min, std::min(c->m_max, d));
    c->m_value = c->m_default = d;
    Registry()[name].reset(c);
    auto h = Held().find(name);
    if (h != Held().end()) { std::string v = h->second; Held().erase(h); c->Set(v); }
    return *c;
}
Cvar& Cvars::Bool(const std::string& name, bool def, const std::string& desc, bool persist) {
    if (Cvar* e = Find(name)) return *e;
    Cvar* c = new Cvar();
    c->m_name = name; c->m_desc = desc; c->m_type = Cvar::Type::Bool; c->m_persist = persist;
    c->m_value = c->m_default = def ? 1.0 : 0.0;
    Registry()[name].reset(c);
    auto h = Held().find(name);
    if (h != Held().end()) { std::string v = h->second; Held().erase(h); c->Set(v); }
    return *c;
}
Cvar& Cvars::String(const std::string& name, const std::string& def, const std::string& desc, bool persist) {
    if (Cvar* e = Find(name)) return *e;
    Cvar* c = new Cvar();
    c->m_name = name; c->m_desc = desc; c->m_type = Cvar::Type::String; c->m_persist = persist;
    c->m_text = c->m_defaultText = def;
    Registry()[name].reset(c);
    auto h = Held().find(name);
    if (h != Held().end()) { std::string v = h->second; Held().erase(h); c->Set(v); }
    return *c;
}

Cvar* Cvars::Find(const std::string& name) {
    auto it = Registry().find(name);
    return it == Registry().end() ? nullptr : it->second.get();
}

std::vector<Cvar*> Cvars::All(const std::string& prefix) {
    std::vector<Cvar*> out;
    for (auto& kv : Registry())
        if (kv.first.compare(0, prefix.size(), prefix) == 0) out.push_back(kv.second.get());
    return out;   // std::map keeps them sorted
}

bool Cvars::Set(const std::string& name, const std::string& value, std::string* error) {
    Cvar* c = Find(name);
    if (!c) { if (error) *error = "unknown variable '" + name + "'"; return false; }
    return c->Set(value, error);
}

bool Cvars::SetFromAssignment(const std::string& a, std::string* error) {
    const std::size_t eq = a.find('=');
    if (eq == std::string::npos || eq == 0) { if (error) *error = "expected name=value, got '" + a + "'"; return false; }
    return Set(Trim(a.substr(0, eq)), a.substr(eq + 1), error);
}

void Cvars::ResetAll() { for (auto& kv : Registry()) kv.second->Reset(); }

bool Cvars::Save(const std::string& path) {
    std::map<std::string, std::string> lines;
    for (auto& kv : Held()) lines[kv.first] = kv.second;
    for (auto& kv : Registry()) {
        const Cvar& c = *kv.second;
        if (!c.Persist() || c.IsDefault()) { lines.erase(kv.first); continue; }
        std::string v = c.AsString();
        std::replace(v.begin(), v.end(), '\n', ' ');
        std::replace(v.begin(), v.end(), '\r', ' ');
        lines[kv.first] = v;
    }
    std::string out = "# console variables\n";
    for (auto& kv : lines) out += kv.first + " " + kv.second + "\n";
    return savefmt::AtomicWrite(path, out);
}

bool Cvars::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos) continue;
        const std::string name = line.substr(0, sp), value = Trim(line.substr(sp));
        if (Cvar* c = Find(name)) c->Set(value);
        else Held()[name] = value;
    }
    return true;
}

void Cvars::Clear() { Registry().clear(); Held().clear(); }

} // namespace okay
