#include "okay/Core/CVar.hpp"
#include "okay/Core/Prefs.hpp"
#include "CoreThreads.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <locale>
#include <map>
#include <sstream>

namespace okay {

namespace cvar_detail {
struct Entry {
    std::string name, description;
    CVarType type = CVarType::Int;
    CVarFlags flags = CVarFlags::None;
    bool hasRange = false;
    double mn = 0.0, mx = 0.0;
    CVarValue def, cur, pending;
    bool hasPending = false;   // RestartRequired: `pending` waits for the next start
    bool fromArgs = false;     // the value in effect came from the command line
    // Lock-free mirrors of `cur` for bool / int / float.
    std::atomic<int> ai{0};
    std::atomic<float> af{0.0f};
    std::vector<std::pair<int, CVarRegistry::Listener>> listeners;
    int nextListener = 1;
};
} // namespace cvar_detail

using cvar_detail::Entry;

namespace {

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ValueEquals(const CVarValue& a, const CVarValue& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case CVarType::Bool: return a.b == b.b;
        case CVarType::Int: return a.i == b.i;
        case CVarType::Float: return a.f == b.f;
        case CVarType::String: return a.s == b.s;
    }
    return false;
}

void Mirror(Entry& e) {
    switch (e.type) {
        case CVarType::Bool: e.ai.store(e.cur.b ? 1 : 0, std::memory_order_release); break;
        case CVarType::Int: e.ai.store(e.cur.i, std::memory_order_release); break;
        case CVarType::Float: e.af.store(e.cur.f, std::memory_order_release); break;
        case CVarType::String: break;
    }
}

// Pull a number into [min, max]. True if it had to move.
bool Clamp(const Entry& e, CVarValue& v) {
    if (!e.hasRange) return false;
    if (e.type == CVarType::Int) {
        if (static_cast<double>(v.i) < e.mn) { v.i = static_cast<int>(e.mn); return true; }
        if (static_cast<double>(v.i) > e.mx) { v.i = static_cast<int>(e.mx); return true; }
    } else if (e.type == CVarType::Float) {
        if (static_cast<double>(v.f) < e.mn) { v.f = static_cast<float>(e.mn); return true; }
        if (static_cast<double>(v.f) > e.mx) { v.f = static_cast<float>(e.mx); return true; }
    }
    return false;
}

const char* TypeName(CVarType t) {
    switch (t) {
        case CVarType::Bool: return "bool";
        case CVarType::Int: return "int";
        case CVarType::Float: return "float";
        case CVarType::String: return "string";
    }
    return "?";
}

std::string RangeText(const Entry& e) {
    CVarValue lo, hi;
    if (e.type == CVarType::Int) { lo = CVarValue::Int(static_cast<int>(e.mn)); hi = CVarValue::Int(static_cast<int>(e.mx)); }
    else { lo = CVarValue::Float(static_cast<float>(e.mn)); hi = CVarValue::Float(static_cast<float>(e.mx)); }
    return CVarRegistry::FormatValue(lo) + ".." + CVarRegistry::FormatValue(hi);
}

// Parse `text` as a value of `type`. On failure `err` says what was expected.
bool ParseInto(CVarType type, const std::string& text, CVarValue& out, std::string& err) {
    switch (type) {
        case CVarType::Bool: {
            bool b;
            if (!CVarRegistry::ParseBool(Trim(text), b)) { err = "expected true or false (also on/off, yes/no, 1/0), got \"" + text + "\""; return false; }
            out = CVarValue::Bool(b);
            return true;
        }
        case CVarType::Int: {
            int i;
            if (!CVarRegistry::ParseInt(Trim(text), i)) { err = "expected a whole number, got \"" + text + "\""; return false; }
            out = CVarValue::Int(i);
            return true;
        }
        case CVarType::Float: {
            float f;
            if (!CVarRegistry::ParseFloat(Trim(text), f)) { err = "expected a number, got \"" + text + "\""; return false; }
            out = CVarValue::Float(f);
            return true;
        }
        case CVarType::String:
            out = CVarValue::String(text);
            return true;
    }
    return false;
}

thread_local int t_notifyDepth = 0;

} // namespace

// ---- parsing and formatting -----------------------------------------------------

bool CVarRegistry::ParseBool(std::string_view text, bool& out) {
    std::string t = Lower(std::string(text));
    if (t == "1" || t == "true" || t == "on" || t == "yes") { out = true; return true; }
    if (t == "0" || t == "false" || t == "off" || t == "no") { out = false; return true; }
    return false;
}

bool CVarRegistry::ParseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') { neg = s[0] == '-'; i = 1; }
    if (i == s.size()) return false;
    long long v = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
        if (v > 2147483648LL) return false;
    }
    if (neg) v = -v;
    if (v < INT_MIN || v > INT_MAX) return false;
    out = static_cast<int>(v);
    return true;
}

bool CVarRegistry::ParseFloat(std::string_view s, float& out) {
    if (s.empty()) return false;
    bool digit = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') digit = true;
        else if (c != '+' && c != '-' && c != '.' && c != 'e' && c != 'E') return false;
    }
    if (!digit) return false;
    std::istringstream is{std::string(s)};
    is.imbue(std::locale::classic());
    double d = 0.0;
    is >> d;
    if (is.fail()) return false;
    if (is.peek() != std::char_traits<char>::eof()) return false;
    if (!std::isfinite(d) || std::fabs(d) > static_cast<double>(FLT_MAX)) return false;
    out = static_cast<float>(d);
    return true;
}

std::string CVarRegistry::FormatFloat(float v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    // The fewest digits that still read back as the same float.
    for (int p = 1; p <= 9; ++p) {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os.precision(p);
        os << v;
        std::istringstream is(os.str());
        is.imbue(std::locale::classic());
        float back = 0.0f;
        if ((is >> back) && back == v) return os.str();
    }
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os.precision(9);
    os << v;
    return os.str();
}

std::string CVarRegistry::FormatValue(const CVarValue& v) {
    switch (v.type) {
        case CVarType::Bool: return v.b ? "true" : "false";
        case CVarType::Int: return std::to_string(v.i);
        case CVarType::Float: return FormatFloat(v.f);
        case CVarType::String: return v.s;
    }
    return std::string();
}

bool CVarRegistry::IsValidName(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    const unsigned char c0 = static_cast<unsigned char>(name[0]);
    if (!(std::isalpha(c0) || c0 == '_')) return false;
    for (char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || c == '_' || c == '.')) return false;
    }
    const std::string l = Lower(name);
    return !(l == "list" || l == "reset" || l == "toggle" || l == "help");
}

// ---- the registry -------------------------------------------------------------

struct CVarRegistry::Impl {
    struct Notify {
        std::vector<Listener> fns;
        CVarValue oldV, newV;
    };

    mutable corethreads::Mutex mu;
    std::map<std::string, std::unique_ptr<Entry>> entries;
    std::map<std::string, std::string> overrides;   // `+name=value` waiting for a registration
    std::vector<std::string> errors;
    Entry* cheats = nullptr;

    // ---- lookup (lock held) ----

    Entry* FindLocked(const std::string& name) const {
        auto it = entries.find(name);
        if (it != entries.end()) return it->second.get();
        Entry* only = nullptr;
        const std::string l = Lower(name);
        for (const auto& kv : entries) {
            if (Lower(kv.first) == l) {
                if (only) return nullptr;   // ambiguous
                only = kv.second.get();
            }
        }
        return only;
    }

    CVarInfo MakeInfo(const Entry& e) const {
        CVarInfo i;
        i.name = e.name;
        i.description = e.description;
        i.type = e.type;
        i.flags = e.flags;
        i.value = FormatValue(e.cur);
        i.defaultValue = FormatValue(e.def);
        i.hasPending = e.hasPending;
        if (e.hasPending) i.pendingValue = FormatValue(e.pending);
        i.hasRange = e.hasRange;
        i.min = e.mn;
        i.max = e.mx;
        i.modified = !ValueEquals(e.cur, e.def);
        return i;
    }

    // ---- setting (lock held) ----

    CVarResult SetLocked(Entry& e, CVarValue v, bool user, bool applyNow, bool fromArgs, Notify* note, std::string* err) {
        if (v.type != e.type) { if (err) *err = e.name + ": wrong type of value"; return CVarResult::ParseError; }
        if (user) {
            if (HasFlag(e.flags, CVarFlags::ReadOnly)) {
                if (err) *err = e.name + " is read-only";
                return CVarResult::ReadOnly;
            }
            if (HasFlag(e.flags, CVarFlags::Cheat) && !(cheats && cheats->ai.load(std::memory_order_acquire) != 0)) {
                if (err) *err = e.name + " is a cheat variable (set cheats true first)";
                return CVarResult::CheatsDisabled;
            }
        }
        if (e.type == CVarType::Float && std::isnan(v.f)) {
            if (err) *err = e.name + ": not a number";
            return CVarResult::ParseError;
        }
        const bool clamped = Clamp(e, v);
        e.fromArgs = fromArgs;
        if (HasFlag(e.flags, CVarFlags::RestartRequired) && !applyNow) {
            if (ValueEquals(v, e.cur)) {
                const bool had = e.hasPending;
                e.hasPending = false;
                return had ? CVarResult::Ok : (clamped ? CVarResult::Clamped : CVarResult::Unchanged);
            }
            if (e.hasPending && ValueEquals(v, e.pending)) return CVarResult::RestartPending;
            e.pending = v;
            e.hasPending = true;
            return CVarResult::RestartPending;
        }
        e.hasPending = false;
        if (ValueEquals(v, e.cur)) return clamped ? CVarResult::Clamped : CVarResult::Unchanged;
        CVarValue old = e.cur;
        e.cur = v;
        Mirror(e);
        if (note && !e.listeners.empty()) {
            for (auto& l : e.listeners) note->fns.push_back(l.second);
            note->oldV = std::move(old);
            note->newV = v;
        }
        return clamped ? CVarResult::Clamped : CVarResult::Ok;
    }

    static void Run(Notify& n) {
        if (n.fns.empty() || t_notifyDepth >= 8) return;   // a listener that keeps setting its own variable stops here
        ++t_notifyDepth;
        for (auto& fn : n.fns) { try { fn(n.oldV, n.newV); } catch (...) {} }
        --t_notifyDepth;
    }

    // Find, parse and set by name. Returns Unknown when there is no such variable.
    CVarResult SetNamed(const std::string& name, const std::string& text, bool user, bool applyNow, bool fromArgs,
                        std::string* err) {
        Notify note;
        CVarResult r;
        {
            corethreads::LockGuard g(mu);
            Entry* e = FindLocked(name);
            if (!e) {
                if (err) *err = "unknown variable '" + name + "'";
                return CVarResult::Unknown;
            }
            CVarValue v;
            std::string perr;
            if (!ParseInto(e->type, text, v, perr)) {
                if (err) *err = e->name + ": " + perr;
                return CVarResult::ParseError;
            }
            r = SetLocked(*e, std::move(v), user, applyNow, fromArgs, &note, err);
        }
        Run(note);
        return r;
    }

    std::vector<std::string> NamesLocked(const std::string& prefix) const {
        std::vector<std::string> out;
        for (auto it = entries.lower_bound(prefix); it != entries.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
            out.push_back(it->first);
        return out;
    }

    std::string DescribeLine(const Entry& e) const;
};

CVarRegistry::CVarRegistry() : m_impl(new Impl) {
    Entry* c = Register("cheats", CVarValue::Bool(false), "Allow cheat variables to be changed", CVarFlags::None, false, 0.0, 0.0);
    corethreads::LockGuard g(m_impl->mu);
    m_impl->cheats = c;
}

CVarRegistry::~CVarRegistry() = default;

CVarRegistry& CVarRegistry::Global() {
    static CVarRegistry* g = new CVarRegistry;   // never destroyed: statics elsewhere may still read variables at exit
    return *g;
}

cvar_detail::Entry* CVarRegistry::Register(const std::string& name, const CVarValue& def, const std::string& description,
                                           CVarFlags flags, bool hasRange, double minV, double maxV) {
    corethreads::LockGuard g(m_impl->mu);
    Impl& I = *m_impl;
    if (!IsValidName(name)) {
        I.errors.push_back("invalid variable name '" + name + "'");
        return nullptr;
    }
    auto found = I.entries.find(name);
    if (found != I.entries.end()) {
        if (found->second->type == def.type) return found->second.get();
        I.errors.push_back("variable '" + name + "' is already registered as " + TypeName(found->second->type) +
                           ", not " + TypeName(def.type));
        return nullptr;
    }
    if (def.type != CVarType::Int && def.type != CVarType::Float) hasRange = false;
    if (hasRange && minV > maxV) std::swap(minV, maxV);

    auto e = std::make_unique<Entry>();
    e->name = name;
    e->description = description;
    e->type = def.type;
    e->flags = flags;
    e->hasRange = hasRange;
    e->mn = minV;
    e->mx = maxV;
    e->def = def;
    if (e->type == CVarType::Float && std::isnan(e->def.f)) e->def.f = 0.0f;
    Clamp(*e, e->def);
    e->cur = e->def;
    Mirror(*e);

    // A saved value from the Prefs file wins over the default...
    if (HasFlag(flags, CVarFlags::Persist)) {
        const std::string key = std::string(kPrefsPrefix) + name;
        if (Prefs::Has(key)) {
            CVarValue v;
            std::string err;
            if (ParseInto(e->type, Prefs::GetString(key), v, err) && !(v.type == CVarType::Float && std::isnan(v.f))) {
                Clamp(*e, v);
                e->cur = v;
                Mirror(*e);
            }
        }
    }
    // ...and a command-line value that came before the registration wins over that.
    for (auto it = I.overrides.begin(); it != I.overrides.end(); ++it) {
        if (it->first != name && Lower(it->first) != Lower(name)) continue;
        CVarValue v;
        std::string err;
        if (!ParseInto(e->type, it->second, v, err)) {
            I.errors.push_back("+" + it->first + ": " + err);
        } else {
            const CVarResult r = I.SetLocked(*e, v, true, true, true, nullptr, &err);
            if (!Accepted(r)) I.errors.push_back("+" + it->first + ": " + err);
        }
        I.overrides.erase(it);
        break;
    }
    Entry* raw = e.get();
    I.entries[name] = std::move(e);
    return raw;
}

// ---- typed access ---------------------------------------------------------------

bool CVarRegistry::ReadBool(const Entry* e) { return e->ai.load(std::memory_order_acquire) != 0; }
int CVarRegistry::ReadInt(const Entry* e) { return e->ai.load(std::memory_order_acquire); }
float CVarRegistry::ReadFloat(const Entry* e) { return e->af.load(std::memory_order_acquire); }

std::string CVarRegistry::ReadString(const Entry* e) const {
    corethreads::LockGuard g(m_impl->mu);
    return e->cur.s;
}

CVarValue CVarRegistry::DefaultOf(const Entry* e) const {
    corethreads::LockGuard g(m_impl->mu);
    return e->def;
}

CVarResult CVarRegistry::StoreValue(Entry* e, const CVarValue& v) {
    if (!e) return CVarResult::Unknown;
    Impl::Notify note;
    CVarResult r;
    {
        corethreads::LockGuard g(m_impl->mu);
        r = m_impl->SetLocked(*e, v, false, false, false, &note, nullptr);
    }
    Impl::Run(note);
    return r;
}

CVarResult CVarRegistry::ResetEntry(Entry* e) {
    if (!e) return CVarResult::Unknown;
    CVarValue d;
    {
        corethreads::LockGuard g(m_impl->mu);
        d = e->def;
    }
    return StoreValue(e, d);
}

int CVarRegistry::AddListener(Entry* e, Listener fn) {
    if (!e || !fn) return 0;
    corethreads::LockGuard g(m_impl->mu);
    const int id = e->nextListener++;
    e->listeners.emplace_back(id, std::move(fn));
    return id;
}

void CVarRegistry::RemoveListener(Entry* e, int id) {
    if (!e) return;
    corethreads::LockGuard g(m_impl->mu);
    auto& ls = e->listeners;
    ls.erase(std::remove_if(ls.begin(), ls.end(), [id](const std::pair<int, Listener>& p) { return p.first == id; }), ls.end());
}

bool CVarRegistry::NeedsRestart(const Entry* e) const {
    corethreads::LockGuard g(m_impl->mu);
    return e->hasPending;
}

const std::string& CVarRegistry::NameOf(const Entry* e) const { return e->name; }

// ---- by name --------------------------------------------------------------------

bool CVarRegistry::Exists(const std::string& name) const {
    corethreads::LockGuard g(m_impl->mu);
    return m_impl->FindLocked(name) != nullptr;
}

std::size_t CVarRegistry::Count() const {
    corethreads::LockGuard g(m_impl->mu);
    return m_impl->entries.size();
}

bool CVarRegistry::GetString(const std::string& name, std::string& out) const {
    corethreads::LockGuard g(m_impl->mu);
    const Entry* e = m_impl->FindLocked(name);
    if (!e) return false;
    out = FormatValue(e->cur);
    return true;
}

bool CVarRegistry::Info(const std::string& name, CVarInfo& out) const {
    corethreads::LockGuard g(m_impl->mu);
    const Entry* e = m_impl->FindLocked(name);
    if (!e) return false;
    out = m_impl->MakeInfo(*e);
    return true;
}

CVarResult CVarRegistry::SetFromString(const std::string& name, const std::string& text, std::string* error) {
    return m_impl->SetNamed(name, text, true, false, false, error);
}

CVarResult CVarRegistry::Reset(const std::string& name, std::string* error) {
    Impl::Notify note;
    CVarResult r;
    {
        corethreads::LockGuard g(m_impl->mu);
        Entry* e = m_impl->FindLocked(name);
        if (!e) {
            if (error) *error = "unknown variable '" + name + "'";
            return CVarResult::Unknown;
        }
        r = m_impl->SetLocked(*e, e->def, true, false, false, &note, error);
    }
    Impl::Run(note);
    return r;
}

void CVarRegistry::ResetAll() {
    std::vector<Impl::Notify> notes;
    {
        corethreads::LockGuard g(m_impl->mu);
        for (auto& kv : m_impl->entries) {
            Entry& e = *kv.second;
            e.hasPending = false;
            Impl::Notify n;
            m_impl->SetLocked(e, e.def, false, true, false, &n, nullptr);
            e.fromArgs = false;
            if (!n.fns.empty()) notes.push_back(std::move(n));
        }
    }
    for (auto& n : notes) Impl::Run(n);
}

// ---- enumeration ----------------------------------------------------------------

std::vector<CVarInfo> CVarRegistry::List(const std::string& prefix) const {
    corethreads::LockGuard g(m_impl->mu);
    std::vector<CVarInfo> out;
    for (auto it = m_impl->entries.lower_bound(prefix);
         it != m_impl->entries.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        out.push_back(m_impl->MakeInfo(*it->second));
    return out;
}

std::vector<std::string> CVarRegistry::Names(const std::string& prefix) const {
    corethreads::LockGuard g(m_impl->mu);
    return m_impl->NamesLocked(prefix);
}

std::string CVarRegistry::CompleteCommon(const std::string& prefix) const {
    const std::vector<std::string> m = Names(prefix);
    if (m.empty()) return prefix;
    std::string common = m.front();
    for (const std::string& s : m) {
        std::size_t n = 0;
        while (n < common.size() && n < s.size() && common[n] == s[n]) ++n;
        common.resize(n);
    }
    return common;
}

std::vector<std::string> CVarRegistry::RestartPending() const {
    corethreads::LockGuard g(m_impl->mu);
    std::vector<std::string> out;
    for (const auto& kv : m_impl->entries) if (kv.second->hasPending) out.push_back(kv.first);
    return out;
}

// ---- command line ---------------------------------------------------------------

int CVarRegistry::ApplyArgs(int argc, const char* const* argv, std::vector<std::string>* errors) {
    int applied = 0;
    auto fail = [&](const std::string& msg) { if (errors) errors->push_back(msg); };
    for (int i = 0; i < argc; ++i) {
        const char* a = argv ? argv[i] : nullptr;
        if (!a || a[0] != '+' || a[1] == '\0') continue;
        std::string token = a + 1;
        std::string name, value;
        bool haveValue = false;
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos) {
            name = token.substr(0, eq);
            value = token.substr(eq + 1);
            haveValue = true;
        } else {
            name = token;
            CVarInfo info;
            const bool known = Info(name, info);
            if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '+') {
                const std::string next = argv[i + 1];
                bool b;
                if (!known || info.type != CVarType::Bool || ParseBool(next, b)) {   // a bool flag is not given the next word unless it reads as a bool
                    value = next;
                    haveValue = true;
                    ++i;
                }
            }
            if (!haveValue && known && info.type == CVarType::Bool) { value = "true"; haveValue = true; }
        }
        if (name.empty()) { fail("empty variable name in '" + std::string(a) + "'"); continue; }
        if (!haveValue) { fail("+" + name + ": missing value"); continue; }
        std::string err;
        const CVarResult r = m_impl->SetNamed(name, value, true, true, true, &err);
        if (r == CVarResult::Unknown) {
            if (!IsValidName(name)) { fail("+" + name + ": not a valid variable name"); continue; }
            corethreads::LockGuard g(m_impl->mu);
            m_impl->overrides[name] = value;   // applied if it registers later
        } else if (Accepted(r)) {
            ++applied;
        } else {
            fail("+" + name + ": " + err);
        }
    }
    return applied;
}

std::vector<std::string> CVarRegistry::UnresolvedOverrides() const {
    corethreads::LockGuard g(m_impl->mu);
    std::vector<std::string> out;
    for (const auto& kv : m_impl->overrides) out.push_back(kv.first);
    return out;
}

void CVarRegistry::ClearUnresolvedOverrides() {
    corethreads::LockGuard g(m_impl->mu);
    m_impl->overrides.clear();
}

// ---- the console ----------------------------------------------------------------

std::string CVarRegistry::Impl::DescribeLine(const Entry& e) const {
    std::string s = e.name + " = " + FormatValue(e.cur);
    if (e.hasPending) s += " (restart required: " + FormatValue(e.pending) + " after restart)";
    if (!ValueEquals(e.cur, e.def)) s += " (default " + FormatValue(e.def) + ")";
    return s;
}

std::string CVarRegistry::Execute(const std::string& lineIn) {
    try {
        const std::string line = Trim(lineIn);
        if (line.empty() || line[0] == '#' || line.compare(0, 2, "//") == 0) return std::string();

        // name, then an optional '=' and the value
        std::size_t p = 0;
        while (p < line.size() && !std::isspace(static_cast<unsigned char>(line[p])) && line[p] != '=') ++p;
        const std::string name = line.substr(0, p);
        std::string rest = line.substr(p);
        bool hasValue = false;
        {
            std::size_t q = 0;
            while (q < rest.size() && std::isspace(static_cast<unsigned char>(rest[q]))) ++q;
            if (q < rest.size() && rest[q] == '=') { hasValue = true; ++q; }
            rest = Trim(rest.substr(q));
            if (!rest.empty()) hasValue = true;
        }
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') rest = rest.substr(1, rest.size() - 2);

        const std::string cmd = Lower(name);
        const bool isCommand = cmd == "list" || cmd == "reset" || cmd == "toggle" || cmd == "help";

        if (isCommand && cmd == "help") {
            return "name            show a variable\n"
                   "name value      set it (also name=value)\n"
                   "toggle name     flip a bool\n"
                   "reset name      back to the default\n"
                   "list [prefix]   list variables";
        }
        if (isCommand && cmd == "list") {
            const std::vector<CVarInfo> infos = List(rest);
            if (infos.empty()) return "no variables match '" + rest + "'";
            std::string out;
            corethreads::LockGuard g(m_impl->mu);
            for (const CVarInfo& i : infos) {
                const Entry* e = m_impl->FindLocked(i.name);
                if (!out.empty()) out += '\n';
                out += e ? m_impl->DescribeLine(*e) : i.name + " = " + i.value;
            }
            return out;
        }
        if (isCommand) {   // reset / toggle: the variable is the next word
            if (rest.empty()) return "error: " + cmd + " needs a variable name";
            CVarInfo info;
            if (!Info(rest, info)) return "error: unknown variable '" + rest + "'";
            std::string err;
            CVarResult r;
            if (cmd == "reset") {
                r = Reset(info.name, &err);
            } else {
                if (info.type != CVarType::Bool) return "error: " + info.name + " is not a bool";
                r = SetFromString(info.name, info.value == "true" ? "false" : "true", &err);
            }
            if (!Accepted(r)) return "error: " + err;
            corethreads::LockGuard g(m_impl->mu);
            const Entry* e = m_impl->FindLocked(info.name);
            return e ? m_impl->DescribeLine(*e) : info.name;
        }

        CVarInfo info;
        if (!Info(name, info)) {
            std::string msg = "error: unknown variable '" + name + "'";
            const std::vector<std::string> similar = Names(name);
            if (!similar.empty()) {
                msg += " (did you mean ";
                for (std::size_t i = 0; i < similar.size() && i < 5; ++i) msg += (i ? ", " : "") + similar[i];
                msg += "?)";
            }
            return msg;
        }
        if (!hasValue) {
            corethreads::LockGuard g(m_impl->mu);
            const Entry* e = m_impl->FindLocked(info.name);
            if (!e) return "error: unknown variable '" + name + "'";
            std::string s = m_impl->DescribeLine(*e);
            std::string tags = TypeName(e->type);
            if (e->hasRange) tags += " " + RangeText(*e);
            if (HasFlag(e->flags, CVarFlags::ReadOnly)) tags += ", read-only";
            if (HasFlag(e->flags, CVarFlags::Cheat)) tags += ", cheat";
            if (HasFlag(e->flags, CVarFlags::Persist)) tags += ", saved";
            if (HasFlag(e->flags, CVarFlags::RestartRequired)) tags += ", restart required";
            s += " [" + tags + "]";
            if (!e->description.empty()) s += " - " + e->description;
            return s;
        }
        std::string err;
        const CVarResult r = SetFromString(info.name, rest, &err);
        if (!Accepted(r)) return "error: " + err;
        corethreads::LockGuard g(m_impl->mu);
        const Entry* e = m_impl->FindLocked(info.name);
        if (!e) return info.name;
        std::string s = m_impl->DescribeLine(*e);
        if (r == CVarResult::Clamped) s += " (clamped to " + RangeText(*e) + ")";
        return s;
    } catch (...) {
        return "error: could not run that line";
    }
}

// ---- Prefs ----------------------------------------------------------------------

int CVarRegistry::StoreToPrefs() const {
    corethreads::LockGuard g(m_impl->mu);
    int n = 0;
    for (const auto& kv : m_impl->entries) {
        const Entry& e = *kv.second;
        if (!HasFlag(e.flags, CVarFlags::Persist) || e.fromArgs) continue;
        const CVarValue& v = e.hasPending ? e.pending : e.cur;
        const std::string key = std::string(kPrefsPrefix) + e.name;
        if (ValueEquals(v, e.def)) { Prefs::Delete(key); continue; }   // the default needs no saving
        Prefs::SetString(key, FormatValue(v));
        ++n;
    }
    return n;
}

int CVarRegistry::LoadFromPrefs() {
    std::vector<Impl::Notify> notes;
    int n = 0;
    {
        corethreads::LockGuard g(m_impl->mu);
        for (auto& kv : m_impl->entries) {
            Entry& e = *kv.second;
            if (!HasFlag(e.flags, CVarFlags::Persist)) continue;
            const std::string key = std::string(kPrefsPrefix) + e.name;
            if (!Prefs::Has(key)) continue;
            CVarValue v;
            std::string err;
            if (!ParseInto(e.type, Prefs::GetString(key), v, err)) continue;
            Impl::Notify note;
            const CVarResult r = m_impl->SetLocked(e, v, false, true, false, &note, nullptr);
            if (Accepted(r)) ++n;
            if (!note.fns.empty()) notes.push_back(std::move(note));
        }
    }
    for (auto& note : notes) Impl::Run(note);
    return n;
}

bool CVarRegistry::SavePersistent(const std::string& prefsPath) const {
    StoreToPrefs();
    return Prefs::Save(prefsPath);
}

bool CVarRegistry::LoadPersistent(const std::string& prefsPath) {
    const bool ok = Prefs::Load(prefsPath);
    LoadFromPrefs();
    return ok;
}

// ---- cheats and diagnostics -----------------------------------------------------

bool CVarRegistry::CheatsEnabled() const {
    corethreads::LockGuard g(m_impl->mu);
    return m_impl->cheats && m_impl->cheats->ai.load(std::memory_order_acquire) != 0;
}

void CVarRegistry::SetCheatsEnabled(bool on) {
    Entry* c;
    {
        corethreads::LockGuard g(m_impl->mu);
        c = m_impl->cheats;
    }
    StoreValue(c, CVarValue::Bool(on));
}

std::vector<std::string> CVarRegistry::RegistrationErrors() const {
    corethreads::LockGuard g(m_impl->mu);
    return m_impl->errors;
}

} // namespace okay
