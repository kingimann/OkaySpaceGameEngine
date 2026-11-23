#include "OkayScriptVMInternal.hpp"

namespace okay {
namespace script_vm {

// ---- Expressions ----
struct NumberExpr : Expr { double v; explicit NumberExpr(double x) : v(x) {} Value Eval(Runtime&) override { return (float)v; } };
struct StringExpr : Expr { std::string v; explicit StringExpr(std::string s) : v(std::move(s)) {} Value Eval(Runtime&) override { return v; } };
struct BoolExpr   : Expr { bool v; explicit BoolExpr(bool b) : v(b) {} Value Eval(Runtime&) override { return v; } };

// $"text {expr} more" — a list of literal/expression segments concatenated.
struct InterpStringExpr : Expr {
    struct Seg { bool isExpr; std::string lit; ExprPtr expr; };
    std::vector<Seg> segs;
    void AddLiteral(const std::string& s) { if (!s.empty()) segs.push_back({false, s, nullptr}); }
    void AddExpr(ExprPtr e) { segs.push_back({true, "", std::move(e)}); }
    Value Eval(Runtime& r) override {
        std::string out;
        for (auto& s : segs) out += s.isExpr ? s.expr->Eval(r).AsString() : s.lit;
        return Value{out};
    }
};
struct VarExpr    : Expr {
    std::string n; explicit VarExpr(std::string s) : n(std::move(s)) {}
    Value Eval(Runtime& r) override {
        if (n == "this" && IsClassInstance(r.self)) return r.self;
        // No plain-class object: `this.hp` is the global `hp`, as it always was.
        const std::string key = (n.rfind("this.", 0) == 0 && !IsClassInstance(r.self)) ? n.substr(5) : n;
        // property reads: transform.position.x, Time.deltaTime, item.name, ...
        if (key.find('.') != std::string::npos) {
            bool ok = false; Value v = r.GetDotted(key, ok);
            if (ok) return v;
        }
        // Bare `gameObject` (as in Destroy(gameObject)) is a reference to this object (prints as its name).
        if (key == "gameObject" && r.selfClass.empty() && !r.Has(key) && r.host && r.host->gameObject) return ObjectRef(r.host->gameObject);
        return r.Get(key);
    }
};

struct AssignExpr : Expr {
    std::string n; ExprPtr value;
    AssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {}
    Value Eval(Runtime& r) override {
        Value v = value->Eval(r);
        const std::string key = (n.rfind("this.", 0) == 0 && !IsClassInstance(r.self)) ? n.substr(5) : n;
        if (key.find('.') != std::string::npos && r.SetDotted(key, v)) return v;
        r.Assign(key, v);
        return v;
    }
};

struct UnaryExpr : Expr {
    Tok op; ExprPtr e;
    UnaryExpr(Tok o, ExprPtr x) : op(o), e(std::move(x)) {}
    Value Eval(Runtime& r) override {
        Value v = e->Eval(r);
        if (op == Tok::Minus) return v.IsVec3() ? Value{v.AsVec3() * -1.0f} : Value{-v.AsFloat()};
        if (op == Tok::Not) return !v.AsBool();
        return v;
    }
};

struct LogicalExpr : Expr {
    Tok op; ExprPtr l, r;
    LogicalExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime& rt) override {
        Value lv = l->Eval(rt);
        if (op == Tok::And) return lv.AsBool() ? r->Eval(rt).AsBool() : false;
        return lv.AsBool() ? true : r->Eval(rt).AsBool();
    }
};

bool ValuesEqual(const Value& a, const Value& b);
// a ?? b: a, or b when a is null. b is not worked out when a is present.
struct CoalesceExpr : Expr {
    ExprPtr l, r;
    CoalesceExpr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime& rt) override {
        Value lv = l->Eval(rt);
        return IsNullish(lv) ? r->Eval(rt) : lv;
    }
};

// x ??= value: set x only while it is null, and only then work out value.
struct NullishAssignExpr : Expr {
    std::string n; ExprPtr value;
    NullishAssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {}
    Value Eval(Runtime& rt) override {
        Value cur = rt.Get(n);
        if (!IsNullish(cur)) return cur;
        Value v = value->Eval(rt);
        rt.Assign(n, v);
        return v;
    }
};

struct BinaryExpr : Expr {
    Tok op; ExprPtr l, r;
    BinaryExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime& rt) override {
        Value a = l->Eval(rt), b = r->Eval(rt);
        // Vector3 math: v + w, v - w, v * 2, 2 * v, v / 2,
        // v * w (component-wise, like Vector3.Scale), v == w.
        if ((a.IsVec3() || b.IsVec3()) && !a.IsString() && !b.IsString()) {
            const Vec3 x = a.AsVec3(), y = b.AsVec3();
            const bool sa = !a.IsVec3(), sb = !b.IsVec3();   // which side is a plain number
            switch (op) {
                case Tok::Plus:  return sa ? Vec3{a.AsFloat() + y.x, a.AsFloat() + y.y, a.AsFloat() + y.z}
                                      : sb ? Vec3{x.x + b.AsFloat(), x.y + b.AsFloat(), x.z + b.AsFloat()} : x + y;
                case Tok::Minus: return sb ? Vec3{x.x - b.AsFloat(), x.y - b.AsFloat(), x.z - b.AsFloat()}
                                      : sa ? Vec3{a.AsFloat() - y.x, a.AsFloat() - y.y, a.AsFloat() - y.z} : x - y;
                case Tok::Star:  return sa ? y * a.AsFloat() : sb ? x * b.AsFloat() : Vec3{x.x * y.x, x.y * y.y, x.z * y.z};
                case Tok::Slash: {
                    if (sb) { float d = b.AsFloat(); return d != 0 ? x * (1.0f / d) : Vec3::Zero; }
                    auto dv = [](float n, float d) { return d != 0 ? n / d : 0.0f; };
                    return sa ? Vec3{dv(a.AsFloat(), y.x), dv(a.AsFloat(), y.y), dv(a.AsFloat(), y.z)}
                              : Vec3{dv(x.x, y.x), dv(x.y, y.y), dv(x.z, y.z)};
                }
                case Tok::Eq: return !sa && !sb && x.x == y.x && x.y == y.y && x.z == y.z;
                case Tok::Ne: return !(!sa && !sb && x.x == y.x && x.y == y.y && x.z == y.z);
                default: break;   // comparisons fall through to magnitudes below
            }
        }
        switch (op) {
            case Tok::Plus:
                if (a.IsString() || b.IsString()) return a.AsString() + b.AsString();
                return a.AsFloat() + b.AsFloat();
            case Tok::Minus:   return a.AsFloat() - b.AsFloat();
            case Tok::Star:    return a.AsFloat() * b.AsFloat();
            case Tok::Slash:   { float d = b.AsFloat(); return d != 0 ? a.AsFloat() / d : 0.0f; }
            case Tok::Percent: { float d = b.AsFloat(); return d != 0 ? std::fmod(a.AsFloat(), d) : 0.0f; }
            case Tok::BitAnd: return (float)(SafeInt64(a.AsFloat()) & SafeInt64(b.AsFloat()));
            case Tok::BitOr:  return (float)(SafeInt64(a.AsFloat()) | SafeInt64(b.AsFloat()));
            case Tok::BitXor: return (float)(SafeInt64(a.AsFloat()) ^ SafeInt64(b.AsFloat()));
            case Tok::Lt: return a.AsFloat() <  b.AsFloat();
            case Tok::Gt: return a.AsFloat() >  b.AsFloat();
            case Tok::Le: return a.AsFloat() <= b.AsFloat();
            case Tok::Ge: return a.AsFloat() >= b.AsFloat();
            case Tok::Eq: return ValuesEqual(a, b);
            case Tok::Ne: return !ValuesEqual(a, b);
            default: return Value{};
        }
    }
};

// ---- C#-style members and methods on values ----------------------------------
// s.Length, list.Count, dict.Keys, v.magnitude ... — false when `prop` isn't one.
bool ValueProperty(const Value& v, const std::string& prop, Value& out) {
    if (v.IsObject()) return ValueProperty(Value{v.AsString()}, prop, out);   // other.Length: its name's
    if (v.IsString()) {
        if (prop == "Length" || prop == "length") { out = Value{(float)v.AsString().size()}; return true; }
        return false;
    }
    if (v.IsArray()) {
        if (prop == "Length" || prop == "length" || prop == "Count" || prop == "count") {
            auto a = v.AsArray(); out = Value{a ? (float)a->size() : 0.0f}; return true;
        }
        return false;
    }
    if (v.IsMap()) {
        auto m = v.AsMap();
        if (m) if (auto it = m->find(prop); it != m->end()) { out = it->second; return true; }   // kv.Key / kv.Value
        if (prop == "Count" || prop == "count") { out = Value{m ? (float)m->size() : 0.0f}; return true; }
        if (prop == "Keys" || prop == "Values") {
            out = Value::MakeArray();
            if (m) for (auto& kv : *m) out.AsArray()->push_back(prop == "Keys" ? Value{kv.first} : kv.second);
            return true;
        }
        return false;
    }
    const Vec3 p = v.AsVec3();
    if (prop == "x") { out = Value{p.x}; return true; }
    if (prop == "y") { out = Value{p.y}; return true; }
    if (prop == "z") { out = Value{p.z}; return true; }
    if (prop == "magnitude")    { out = Value{p.Magnitude()}; return true; }
    if (prop == "sqrMagnitude") { out = Value{p.x * p.x + p.y * p.y + p.z * p.z}; return true; }
    if (prop == "normalized") { float m = p.Magnitude(); out = Value{m > 1e-6f ? p * (1.0f / m) : Vec3::Zero}; return true; }
    return false;
}
bool ValuesEqual(const Value& a, const Value& b) {
    // An object is never null, and two variables are equal only when they are
    // the same object. Arrays and plain maps stay on the old comparison.
    if (IsClassInstance(a) || IsClassInstance(b)) {
        if (IsClassInstance(a) && IsClassInstance(b)) return a.AsMap().get() == b.AsMap().get();
        return false;
    }
    // Object references: the same object, or (against text) the same name, so
    // `other == "Target"` keeps working. A destroyed object equals null.
    if (a.IsObject() || b.IsObject()) {
        if (a.IsObject() && b.IsObject()) return a.AsObjectId() == b.AsObjectId() || (IsNullish(a) && IsNullish(b));
        const Value& o = a.IsObject() ? a : b;
        const Value& x = a.IsObject() ? b : a;
        if (x.IsString()) return o.AsString() == x.AsString();
        if (x.IsBool()) return o.AsBool() == x.AsBool();
        return IsNullish(o) && IsNullish(x);
    }
    if (a.IsString() || b.IsString()) return a.AsString() == b.AsString();
    return a.AsFloat() == b.AsFloat();
}
// s.ToUpper(), xs.Add(3), d.ContainsKey("a"), n.ToString("F2") ... Arrays and
// maps are shared references, so Add/Remove/Clear change the variable's value.
using LambdaInvoker = std::function<Value(const Value&, std::vector<Value>&)>;
bool CallValueMethod(const Value& v, const std::string& m, std::vector<Value>& a, Value& out,
                     const LambdaInvoker* invoke = nullptr) {
    auto arg = [&](std::size_t i) { return i < a.size() ? a[i] : Value{}; };
    out = Value{};
    auto isFn = [&](std::size_t i) { return invoke && i < a.size() && Runtime::IsLambda(a[i]); };
    auto call1 = [&](const Value& x) { std::vector<Value> xs{x}; return (*invoke)(a[0], xs); };
    if (m == "Invoke" && invoke && Runtime::IsLambda(v)) { out = (*invoke)(v, a); return true; }
    if (m == "ToString") {
        if (v.IsNumber() && !a.empty()) {          // "F2" / "N0" / "0.00"
            std::string f = a[0].AsString(); int dec = -1;
            if (!f.empty() && (f[0] == 'F' || f[0] == 'f' || f[0] == 'N' || f[0] == 'n'))
                dec = f.size() > 1 ? std::atoi(f.c_str() + 1) : 2;
            else if (auto d = f.find('.'); d != std::string::npos) dec = (int)(f.size() - d - 1);
            else if (!f.empty() && f.find_first_not_of('0') == std::string::npos) dec = 0;
            if (dec >= 0) { char buf[64]; std::snprintf(buf, sizeof buf, "%.*f", dec > 9 ? 9 : dec, v.AsFloat()); out = Value{std::string(buf)}; return true; }
        }
        out = Value{v.AsString()}; return true;
    }
    if (m == "Equals")    { out = Value{ValuesEqual(v, arg(0))}; return true; }
    if (m == "CompareTo") { float x = v.AsFloat(), y = arg(0).AsFloat(); out = Value{x < y ? -1.0f : (x > y ? 1.0f : 0.0f)}; return true; }
    if (v.IsString()) {
        const std::string str = v.AsString();
        auto up = [](std::string x, bool u) { for (char& c : x) c = (char)(u ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); return x; };
        auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        if (m == "ToUpper" || m == "ToUpperInvariant") { out = Value{up(str, true)}; return true; }
        if (m == "ToLower" || m == "ToLowerInvariant") { out = Value{up(str, false)}; return true; }
        if (m == "Contains")   { out = Value{str.find(arg(0).AsString()) != std::string::npos}; return true; }
        if (m == "StartsWith") { out = Value{str.rfind(arg(0).AsString(), 0) == 0}; return true; }
        if (m == "EndsWith")   { std::string e = arg(0).AsString(); out = Value{str.size() >= e.size() && str.compare(str.size() - e.size(), e.size(), e) == 0}; return true; }
        if (m == "IndexOf")    { auto p = str.find(arg(0).AsString()); out = Value{p == std::string::npos ? -1.0f : (float)p}; return true; }
        if (m == "LastIndexOf"){ auto p = str.rfind(arg(0).AsString()); out = Value{p == std::string::npos ? -1.0f : (float)p}; return true; }
        if (m == "Substring") {
            int st = SafeInt(arg(0).AsFloat()); if (st < 0) st = 0; if (st > (int)str.size()) st = (int)str.size();
            int n = a.size() > 1 ? SafeInt(a[1].AsFloat()) : (int)str.size() - st; if (n < 0) n = 0;
            out = Value{str.substr((std::size_t)st, (std::size_t)n)}; return true;
        }
        if (m == "Replace") {
            std::string from = arg(0).AsString(), to = arg(1).AsString(), r = str;
            if (!from.empty()) for (std::size_t p = 0; (p = r.find(from, p)) != std::string::npos; p += to.size()) r.replace(p, from.size(), to);
            out = Value{r}; return true;
        }
        if (m == "Trim" || m == "TrimStart" || m == "TrimEnd") {
            std::size_t b = 0, e = str.size();
            if (m != "TrimEnd")   while (b < e && ws(str[b])) ++b;
            if (m != "TrimStart") while (e > b && ws(str[e - 1])) --e;
            out = Value{str.substr(b, e - b)}; return true;
        }
        if (m == "Split") {
            std::string sep = a.empty() ? " " : a[0].AsString();
            out = Value::MakeArray(); auto arr = out.AsArray();
            if (sep.empty()) { arr->push_back(Value{str}); return true; }
            std::size_t p = 0, q;
            while ((q = str.find(sep, p)) != std::string::npos) { arr->push_back(Value{str.substr(p, q - p)}); p = q + sep.size(); }
            arr->push_back(Value{str.substr(p)}); return true;
        }
        if (m == "PadLeft" || m == "PadRight") {
            int w = SafeInt(arg(0).AsFloat()); char c = a.size() > 1 && !a[1].AsString().empty() ? a[1].AsString()[0] : ' ';
            std::string r = str;
            if ((int)r.size() < w) r = m == "PadLeft" ? std::string(w - r.size(), c) + r : r + std::string(w - r.size(), c);
            out = Value{r}; return true;
        }
        if (m == "Insert") { std::size_t p = SafeSize(arg(0).AsFloat()); std::string r = str; r.insert(std::min(p, r.size()), arg(1).AsString()); out = Value{r}; return true; }
        if (m == "Remove") {
            std::size_t p = SafeSize(arg(0).AsFloat()); std::string r = str;
            if (p < r.size()) r.erase(p, a.size() > 1 ? SafeSize(a[1].AsFloat()) : std::string::npos);
            out = Value{r}; return true;
        }
        return false;
    }
    if (v.IsArray()) {
        auto arr = v.AsArray(); if (!arr) return false;
        auto find = [&](const Value& x) { for (std::size_t i = 0; i < arr->size(); ++i) if (ValuesEqual((*arr)[i], x)) return (int)i; return -1; };
        // LINQ-style helpers that take a lambda: xs.Where(x => x > 2).Select(x => x * 10).ToList()
        if (isFn(0)) {
            const auto items = *arr;   // snapshot: the lambda may change the list
            if (m == "ForEach") { for (auto& x : items) call1(x); return true; }
            if (m == "Where" || m == "FindAll") { out = Value::MakeArray(); for (auto& x : items) if (call1(x).AsBool()) out.AsArray()->push_back(x); return true; }
            if (m == "Select") { out = Value::MakeArray(); for (auto& x : items) out.AsArray()->push_back(call1(x)); return true; }
            if (m == "Find" || m == "First" || m == "FirstOrDefault") { for (auto& x : items) if (call1(x).AsBool()) { out = x; return true; } return true; }
            if (m == "FindIndex") { for (std::size_t i = 0; i < items.size(); ++i) if (call1(items[i]).AsBool()) { out = Value{(float)i}; return true; } out = Value{-1.0f}; return true; }
            if (m == "Any" || m == "Exists") { bool r = false; for (auto& x : items) if (call1(x).AsBool()) { r = true; break; } out = Value{r}; return true; }
            if (m == "All" || m == "TrueForAll") { bool r = true; for (auto& x : items) if (!call1(x).AsBool()) { r = false; break; } out = Value{r}; return true; }
            if (m == "Count") { float n = 0; for (auto& x : items) if (call1(x).AsBool()) n += 1; out = Value{n}; return true; }
            if (m == "RemoveAll") {
                float n = 0; arr->clear();
                for (auto& x : items) { if (call1(x).AsBool()) n += 1; else arr->push_back(x); }
                out = Value{n}; return true;
            }
            if (m == "Sum" || m == "Max" || m == "Min" || m == "Average") {
                std::vector<Value> mapped; for (auto& x : items) mapped.push_back(call1(x));
                Value tmp = Value::MakeArray(); *tmp.AsArray() = mapped;
                std::vector<Value> none;
                return CallValueMethod(tmp, m, none, out);
            }
            if (m == "OrderBy" || m == "OrderByDescending") {
                std::vector<std::pair<Value, Value>> keyed; for (auto& x : items) keyed.push_back({call1(x), x});
                const bool desc = m == "OrderByDescending";
                std::stable_sort(keyed.begin(), keyed.end(), [&](const auto& p, const auto& q) {
                    bool lt = (p.first.IsString() || q.first.IsString()) ? p.first.AsString() < q.first.AsString() : p.first.AsFloat() < q.first.AsFloat();
                    bool gt = (p.first.IsString() || q.first.IsString()) ? q.first.AsString() < p.first.AsString() : q.first.AsFloat() < p.first.AsFloat();
                    return desc ? gt : lt; });
                out = Value::MakeArray(); for (auto& kq : keyed) out.AsArray()->push_back(kq.second);
                return true;
            }
            if (m == "Sort") {   // xs.Sort((a, b) => a - b)
                std::vector<Value> sorted = items;
                std::stable_sort(sorted.begin(), sorted.end(), [&](const Value& x, const Value& y) {
                    std::vector<Value> xy{x, y}; return (*invoke)(a[0], xy).AsFloat() < 0.0f; });
                *arr = sorted; return true;
            }
        }
        if (m == "Add" || m == "Push" || m == "Enqueue") { arr->push_back(arg(0)); return true; }
        if (m == "AddRange") { if (auto o = arg(0).AsArray()) { auto copy = *o; arr->insert(arr->end(), copy.begin(), copy.end()); } return true; }
        if (m == "Insert")   { int i = SafeInt(arg(0).AsFloat()); i = std::max(0, std::min(i, (int)arr->size())); arr->insert(arr->begin() + i, arg(1)); return true; }
        if (m == "Remove")   { int i = find(arg(0)); if (i >= 0) arr->erase(arr->begin() + i); out = Value{i >= 0}; return true; }
        if (m == "RemoveAt") { int i = SafeInt(arg(0).AsFloat()); if (i >= 0 && i < (int)arr->size()) arr->erase(arr->begin() + i); return true; }
        if (m == "Contains") { out = Value{find(arg(0)) >= 0}; return true; }
        if (m == "IndexOf")  { out = Value{(float)find(arg(0))}; return true; }
        if (m == "Clear")    { arr->clear(); return true; }
        if (m == "Pop")      { if (!arr->empty()) { out = arr->back(); arr->pop_back(); } return true; }
        if (m == "Dequeue")  { if (!arr->empty()) { out = arr->front(); arr->erase(arr->begin()); } return true; }
        if (m == "First")    { if (!arr->empty()) out = arr->front(); return true; }
        if (m == "Last")     { if (!arr->empty()) out = arr->back(); return true; }
        if (m == "Count")    { out = Value{(float)arr->size()}; return true; }
        if (m == "Reverse")  { std::reverse(arr->begin(), arr->end()); return true; }
        if (m == "Sort") {
            bool nums = true; for (auto& x : *arr) nums = nums && !x.IsString();
            std::stable_sort(arr->begin(), arr->end(), [&](const Value& x, const Value& y) {
                return nums ? x.AsFloat() < y.AsFloat() : x.AsString() < y.AsString(); });
            return true;
        }
        if (m == "ToArray" || m == "ToList") { out = Value::MakeArray(); *out.AsArray() = *arr; return true; }
        if (m == "Sum" || m == "Average" || m == "Max" || m == "Min") {
            if (arr->empty()) { out = Value{0.0f}; return true; }
            float acc = m == "Max" ? -1e30f : (m == "Min" ? 1e30f : 0.0f);
            for (auto& x : *arr) { float f = x.AsFloat(); acc = m == "Max" ? std::max(acc, f) : (m == "Min" ? std::min(acc, f) : acc + f); }
            out = Value{m == "Average" ? acc / (float)arr->size() : acc}; return true;
        }
        return false;
    }
    if (v.IsMap()) {
        auto mp = v.AsMap(); if (!mp) return false;
        if (m == "Add" || m == "TryAdd") { auto k = arg(0).AsString(); bool fresh = !mp->count(k); if (fresh) (*mp)[k] = arg(1); out = Value{fresh}; return true; }
        if (m == "ContainsKey") { out = Value{mp->count(arg(0).AsString()) > 0}; return true; }
        if (m == "ContainsValue") { bool f = false; for (auto& kv : *mp) f = f || ValuesEqual(kv.second, arg(0)); out = Value{f}; return true; }
        if (m == "Remove") { out = Value{mp->erase(arg(0).AsString()) > 0}; return true; }
        if (m == "Clear")  { mp->clear(); return true; }
        if (m == "GetValueOrDefault") { auto it = mp->find(arg(0).AsString()); out = it != mp->end() ? it->second : arg(1); return true; }
        return false;
    }
    return false;
}

// A component reference returned by GetComponent<T>(): the string "@T" for this
// object's own component, "@T#<instance id>" for another object's (other.GetComponent<T>()).
inline bool IsComponentRef(const Value& v) {
    return v.IsString() && v.AsString().size() > 1 && v.AsString()[0] == '@';
}
// expr.member  — a property read on any expression's value.
struct MemberExpr : Expr {
    ExprPtr obj; std::string name; bool safe = false;
    MemberExpr(ExprPtr o, std::string n, bool s = false) : obj(std::move(o)), name(std::move(n)), safe(s) {}
    Value Eval(Runtime& rt) override {
        Value v = obj->Eval(rt), out;
        if (safe && IsNullish(v)) return Value{};
        if (IsClassInstance(v)) return rt.GetMember(v, name, true);
        if (IsComponentRef(v)) { std::vector<Value> a{Value{v.AsString().substr(1)}, Value{name}}; return rt.Call("__get_component", a); }
        if (v.IsObject()) {   // GameObject.Find("Door").transform.position
            GameObject* o = ResolveObject(nullptr, v);
            if (ObjectMember(o, v, name, out)) return out;
            if (!o) throw ScriptError("can't read '" + name + "': the object no longer exists");
        }
        if (ValueProperty(v, name, out)) return out;
        throw ScriptError("no property '" + name + "' on this value");
    }
};
// expr.member = value  — GetComponent<SpriteRenderer>().color = Color.red.
struct MemberAssignExpr : Expr {
    ExprPtr obj; std::string name; ExprPtr value;
    MemberAssignExpr(ExprPtr o, std::string n, ExprPtr v) : obj(std::move(o)), name(std::move(n)), value(std::move(v)) {}
    Value Eval(Runtime& rt) override {
        Value target = obj->Eval(rt), v = value->Eval(rt);
        if (IsClassInstance(target)) { rt.SetMember(target, name, v, true); return v; }
        if (target.IsObject()) {   // GameObject.Find("Door").transform.position = v
            GameObject* o = ResolveObject(nullptr, target);
            if (!o) throw ScriptError("can't set '" + name + "': the object no longer exists");
            if (!SetObjectMember(o, name, v)) throw ScriptError("can't set '" + name + "' on an object");
            return v;
        }
        if (!IsComponentRef(target))
            throw ScriptError(target.IsNumber() && target.AsFloat() == 0.0f
                ? "can't set '" + name + "': the object is null (e.g. GetComponent found no such component)"
                : "can't assign '" + name + "' on this value");
        std::vector<Value> a{Value{target.AsString().substr(1)}, Value{name}, v};
        rt.Call("__set_component", a);
        return v;
    }
};
// (int)x / (float)x / (string)x.
struct CastExpr : Expr {
    std::string type; ExprPtr e;
    CastExpr(std::string t, ExprPtr x) : type(std::move(t)), e(std::move(x)) {}
    Value Eval(Runtime& rt) override {
        Value v = e->Eval(rt);
        if (type == "string") return Value{v.AsString()};
        if (type == "bool") return Value{v.AsBool()};
        if (type == "char") return v.IsString() ? v : Value{std::string(1, (char)SafeInt(v.AsFloat()))};
        if (type == "float" || type == "double" || type == "decimal") {
            if (v.IsString()) { try { return Value{std::stof(v.AsString())}; } catch (...) { return Value{0.0f}; } }
            return Value{v.AsFloat()};
        }
        float f = v.IsString() && v.AsString().size() == 1 ? (float)(unsigned char)v.AsString()[0] : v.AsFloat();
        return Value{(float)(long long)f};   // integer casts truncate toward zero
    }
};
// expr.Method(args)  — a C#-style method on a value (string, list, dictionary, number).
struct MethodCallExpr : Expr {
    ExprPtr obj; std::string name; std::vector<ExprPtr> args; bool safe = false;
    MethodCallExpr(ExprPtr o, std::string n, bool s = false) : obj(std::move(o)), name(std::move(n)), safe(s) {}
    Value Eval(Runtime& rt) override {
        Value v = obj->Eval(rt), out;
        if (safe && IsNullish(v)) return Value{};
        std::vector<Value> vals; vals.reserve(args.size());
        for (auto& a : args) vals.push_back(a->Eval(rt));
        if (IsClassInstance(v)) return rt.CallMethod(v, name, vals, true);
        if (IsComponentRef(v)) {
            vals.insert(vals.begin(), {Value{v.AsString().substr(1)}, Value{name}});
            return rt.Call("__call_component", vals);
        }
        if (v.IsObject()) return rt.CallOnObject(v, name, vals);   // GameObject.Find("Door").SetActive(false)
        LambdaInvoker inv = [&rt](const Value& f, std::vector<Value>& xs) { return rt.CallLambda(f, xs); };
        if (CallValueMethod(v, name, vals, out, &inv)) return out;
        throw ScriptError("no method '" + name + "' on this value");
    }
};

// new Item("Potion") / new Item(). Arrays (`new Item[4]`) and List/Dictionary
// stay on their own paths; this is only a plain class.
struct NewExpr : Expr {
    std::string type; std::vector<ExprPtr> args;
    explicit NewExpr(std::string t) : type(std::move(t)) {}
    Value Eval(Runtime& rt) override {
        std::vector<Value> vals;
        vals.reserve(args.size());
        for (auto& a : args) vals.push_back(a->Eval(rt));
        return rt.MakeInstance(type, vals);
    }
};

struct CallExpr : Expr {
    std::string name; std::vector<ExprPtr> args;
    std::vector<std::string> argNames;   ///< parallel to args; empty string = positional
    explicit CallExpr(std::string n) : name(std::move(n)) {}
    Value Eval(Runtime& rt) override {
        std::vector<Value> vals;
        std::vector<std::string> names;
        vals.reserve(args.size());
        bool anyNamed = false;
        for (std::size_t i = 0; i < args.size(); ++i) {
            std::string nm = i < argNames.size() ? argNames[i] : std::string();
            if (!nm.empty()) anyNamed = true;
            names.push_back(std::move(nm));
            vals.push_back(args[i]->Eval(rt));
        }
        return rt.Call(name, vals, anyNamed ? &names : nullptr);
    }
};

// Ternary: cond ? a : b.
struct TernaryExpr : Expr {
    ExprPtr cond, a, b;
    TernaryExpr(ExprPtr c, ExprPtr x, ExprPtr y) : cond(std::move(c)), a(std::move(x)), b(std::move(y)) {}
    Value Eval(Runtime& rt) override { return cond->Eval(rt).AsBool() ? a->Eval(rt) : b->Eval(rt); }
};

// An array literal: [a, b, c].
struct ArrayExpr : Expr {
    std::vector<ExprPtr> elems;
    Value Eval(Runtime& rt) override {
        Value v = Value::MakeArray();
        auto arr = v.AsArray();
        for (auto& e : elems) arr->push_back(e->Eval(rt));
        return v;
    }
};

// Read an element: arr[index] for arrays, or map["key"] for dictionaries.
struct IndexExpr : Expr {
    ExprPtr arr, index; bool safe = false;
    IndexExpr(ExprPtr a, ExprPtr i, bool s = false) : arr(std::move(a)), index(std::move(i)), safe(s) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        if (safe && IsNullish(a)) return Value{};
        if (IsClassInstance(a)) return rt.GetMember(a, index->Eval(rt).AsString(), true);
        if (auto v = a.AsArray()) {
            int i = SafeInt(index->Eval(rt).AsFloat());
            if (i < 0 || i >= (int)v->size()) return Value{};
            return (*v)[i];
        }
        if (auto m = a.AsMap()) {
            auto it = m->find(index->Eval(rt).AsString());
            return it == m->end() ? Value{} : it->second;
        }
        return Value{};
    }
};

// Assign an element: arr[index] = value, or map["key"] = value.
struct IndexAssignExpr : Expr {
    ExprPtr arr, index, value;
    IndexAssignExpr(ExprPtr a, ExprPtr i, ExprPtr v)
        : arr(std::move(a)), index(std::move(i)), value(std::move(v)) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        Value val = value->Eval(rt);
        if (IsClassInstance(a)) { rt.SetMember(a, index->Eval(rt).AsString(), val, true); return val; }
        if (auto v = a.AsArray()) {
            int i = SafeInt(index->Eval(rt).AsFloat());
            if (i >= 0 && i < (int)v->size()) (*v)[i] = val;
            else if (i == (int)v->size()) v->push_back(val); // append at end
        } else if (auto m = a.AsMap()) {
            (*m)[index->Eval(rt).AsString()] = val;
        }
        return val;
    }
};

// Compound assign to an element: arr[index] op= value (and ++/--). Evaluates the
// container and index once, reads the current value, applies op, writes it back.
struct IndexCompoundAssignExpr : Expr {
    ExprPtr arr, index, value; Tok op;
    IndexCompoundAssignExpr(ExprPtr a, ExprPtr i, Tok o, ExprPtr v)
        : arr(std::move(a)), index(std::move(i)), value(std::move(v)), op(o) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        Value rhs = value->Eval(rt);
        if (IsClassInstance(a)) {
            std::string k = index->Eval(rt).AsString();
            Value cur = rt.GetMember(a, k, true);
            Value next;
            switch (op) {
                case Tok::Plus:  next = (cur.IsString() || rhs.IsString()) ? Value{cur.AsString() + rhs.AsString()}
                                                                           : Value{cur.AsFloat() + rhs.AsFloat()}; break;
                case Tok::Minus: next = Value{cur.AsFloat() - rhs.AsFloat()}; break;
                case Tok::Star:  next = Value{cur.AsFloat() * rhs.AsFloat()}; break;
                case Tok::Slash: { float d = rhs.AsFloat(); next = Value{d != 0 ? cur.AsFloat() / d : 0.0f}; break; }
                default: next = rhs; break;
            }
            rt.SetMember(a, k, next, true);
            return next;
        }
        auto apply = [&](const Value& cur) -> Value {
            switch (op) {
                case Tok::Plus:  if (cur.IsString() || rhs.IsString()) return cur.AsString() + rhs.AsString();
                                 return cur.AsFloat() + rhs.AsFloat();
                case Tok::Minus: return cur.AsFloat() - rhs.AsFloat();
                case Tok::Star:  return cur.AsFloat() * rhs.AsFloat();
                case Tok::Slash: { float d = rhs.AsFloat(); return d != 0 ? cur.AsFloat() / d : 0.0f; }
                default:         return rhs;
            }
        };
        if (auto v = a.AsArray()) {
            int i = SafeInt(index->Eval(rt).AsFloat());
            if (i >= 0 && i < (int)v->size()) { (*v)[i] = apply((*v)[i]); return (*v)[i]; }
        } else if (auto m = a.AsMap()) {
            std::string k = index->Eval(rt).AsString();
            Value cur = (*m).count(k) ? (*m)[k] : Value{};
            (*m)[k] = apply(cur); return (*m)[k];
        }
        return Value{};
    }
};

// ---- Statements ----
struct ExprStmt : Stmt { ExprPtr e; explicit ExprStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime& r) override { e->Eval(r); } };
struct VarDeclStmt : Stmt {
    std::string n; ExprPtr init; unsigned char kind = KAny;
    VarDeclStmt(std::string s, ExprPtr e, unsigned char k = KAny) : n(std::move(s)), init(std::move(e)), kind(k) {}
    void Exec(Runtime& r) override {
        if (kind & KStatic) {
            if (r.statics && r.statics->vars.count(n)) return;   // already set by another object
            r.DefineStatic(n, init ? init->Eval(r) : DefaultForKind(kind), (unsigned char)(kind & ~KStatic));
            return;
        }
        if (!kind) { r.Define(n, init ? init->Eval(r) : Value{}); return; }
        r.DefineTyped(n, init ? init->Eval(r) : DefaultForKind(kind), kind);
    }
};
struct ReturnStmt : Stmt { ExprPtr e; explicit ReturnStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime& r) override { throw ReturnSignal{e ? e->Eval(r) : Value{}}; } };
struct BreakStmt : Stmt { void Exec(Runtime&) override { throw BreakSignal{}; } };
struct ContinueStmt : Stmt { void Exec(Runtime&) override { throw ContinueSignal{}; } };
// throw <expr>; — raise a script error carrying the value's text.
struct ThrowStmt : Stmt {
    ExprPtr e; explicit ThrowStmt(ExprPtr x) : e(std::move(x)) {}
    void Exec(Runtime& r) override { throw ScriptError(e ? e->Eval(r).AsString() : std::string("error")); }
};
// try { ... } catch (e) { ... } — run the body; on a script/runtime error, bind the
// message to the catch variable and run the handler. Control-flow signals (return/
// break/continue) are plain structs, not std::exception, so they pass through cleanly.
struct TryStmt : Stmt {
    std::vector<StmtPtr> body, catchB;
    std::string catchVar;
    bool hasCatch = false;
    void Exec(Runtime& r) override {
        try {
            for (auto& s : body) s->Exec(r);
        } catch (const std::exception& ex) {
            if (!hasCatch) return;   // no handler: swallow the error
            if (!catchVar.empty()) r.Define(catchVar, Value{std::string(ex.what())});
            for (auto& s : catchB) s->Exec(r);
        }
    }
};
struct BlockStmt : Stmt {
    std::vector<StmtPtr> body;
    void Exec(Runtime& r) override { for (auto& s : body) s->Exec(r); }
};
struct IfStmt : Stmt {
    ExprPtr cond; std::vector<StmtPtr> thenB, elseB;
    void Exec(Runtime& r) override {
        if (cond->Eval(r).AsBool()) for (auto& s : thenB) s->Exec(r);
        else for (auto& s : elseB) s->Exec(r);
    }
};
struct WhileStmt : Stmt {
    ExprPtr cond; std::vector<StmtPtr> body;
    void Exec(Runtime& r) override {
        int guard = 0;
        while (cond->Eval(r).AsBool()) {
            try { for (auto& s : body) s->Exec(r); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&) { break; }
            if (++guard > 1000000) throw ScriptError("while loop exceeded iteration limit");
        }
    }
};
// C#-style switch: matches the subject against case labels, runs from the first
// match (with C-style fall-through) until a `break`. `default` runs if no match.
struct SwitchStmt : Stmt {
    struct Case { ExprPtr match; std::vector<StmtPtr> body; bool isDefault = false; };
    ExprPtr subject;
    std::vector<Case> cases;
    static bool Eq(const Value& a, const Value& b) {
        return (a.IsString() || b.IsString()) ? a.AsString() == b.AsString()
                                              : a.AsFloat() == b.AsFloat();
    }
    void Exec(Runtime& r) override {
        Value v = subject->Eval(r);
        int start = -1;
        for (std::size_t i = 0; i < cases.size(); ++i)
            if (!cases[i].isDefault && Eq(v, cases[i].match->Eval(r))) { start = (int)i; break; }
        if (start < 0)
            for (std::size_t i = 0; i < cases.size(); ++i)
                if (cases[i].isDefault) { start = (int)i; break; }
        if (start < 0) return;
        try {
            for (std::size_t i = start; i < cases.size(); ++i)   // fall-through until break
                for (auto& s : cases[i].body) s->Exec(r);
        } catch (BreakSignal&) {}
    }
};
// do { body } while (cond); — runs the body at least once.
struct DoWhileStmt : Stmt {
    ExprPtr cond; std::vector<StmtPtr> body;
    void Exec(Runtime& r) override {
        int guard = 0;
        do {
            try { for (auto& s : body) s->Exec(r); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&) { break; }
            if (++guard > 1000000) throw ScriptError("do-while loop exceeded iteration limit");
        } while (cond->Eval(r).AsBool());
    }
};
struct ForStmt : Stmt {
    // Function-level scoping (like the rest of the language): the init runs in
    // the current scope, so `return` inside the body can't corrupt the stack.
    StmtPtr init; ExprPtr cond; ExprPtr step; std::vector<StmtPtr> body;
    void Exec(Runtime& r) override {
        if (init) init->Exec(r);
        int guard = 0;
        while (!cond || cond->Eval(r).AsBool()) {
            try { for (auto& s : body) s->Exec(r); }
            catch (ContinueSignal&) {}        // fall through to the step
            catch (BreakSignal&) { break; }
            if (step) step->Eval(r);
            if (++guard > 1000000) throw ScriptError("for loop exceeded iteration limit");
        }
    }
};
// foreach: for x in <array> { body }
struct ForEachStmt : Stmt {
    std::string var; ExprPtr iterable; std::vector<StmtPtr> body;
    bool pairs = false;   // C# foreach over a Dictionary: items are {Key, Value}
    void Exec(Runtime& r) override {
        Value coll = iterable->Eval(r);
        r.Define(var, Value{}); // loop variable lives in the current scope
        if (auto arr = coll.AsArray()) {
            // Iterate a snapshot of the size so push during iteration is bounded.
            std::size_t n = arr->size();
            for (std::size_t i = 0; i < n && i < arr->size(); ++i) {
                r.Assign(var, (*arr)[i]);
                try { for (auto& s : body) s->Exec(r); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
            }
        } else if (auto m = coll.AsMap()) {
            // foreach over a map yields its keys (like other scripting languages);
            // use map_get(m, key) in the body to read each value. Snapshot the keys
            // so mutation during iteration is safe.
            std::vector<std::string> keys; keys.reserve(m->size());
            for (auto& kv : *m) keys.push_back(kv.first);
            for (auto& k : keys) {
                if (pairs) {
                    Value kv = Value::MakeMap();
                    (*kv.AsMap())["Key"] = Value{k};
                    auto it = m->find(k);
                    (*kv.AsMap())["Value"] = it != m->end() ? it->second : Value{};
                    r.Assign(var, kv);
                } else
                r.Assign(var, Value{k});
                try { for (auto& s : body) s->Exec(r); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
            }
        }
    }
};
// x => x * 2  /  (a, b) => { ... }
struct LambdaExpr : Expr {
    int id = 0;
    std::vector<std::string> params;
    std::vector<ExprPtr> defaults;   ///< null unless that parameter was written `a = expr`
    std::vector<StmtPtr> body;
    Value Eval(Runtime& r) override {
        r.lambdas[id] = Runtime::LambdaBody{&params, &body, &defaults};
        Value v = Value::MakeMap(); (*v.AsMap())["__lambda"] = Value{(float)id};
        return v;
    }
};
// `OnClick += fn` / `OnHurt += (n) => ...` subscribes. A number on the right
// stays ordinary compound assignment (`score += 1`), so both share `+=` / `-=`.
// Evaluates an expression owned elsewhere.
struct BorrowedExpr : Expr {
    Expr* e;
    explicit BorrowedExpr(Expr* x) : e(x) {}
    Value Eval(Runtime& rt) override { return e->Eval(rt); }
};
struct EventOrCompoundExpr : Expr {
    std::string name;
    ExprPtr rhs;
    bool remove;
    Tok bin;
    // name = name <bin> rhs, the plain compound assignment (dotted names and
    // this. included); it borrows rhs so every run can still read it.
    ExprPtr combined;
    EventOrCompoundExpr(std::string n, ExprPtr r, bool rem, Tok b)
        : name(std::move(n)), rhs(std::move(r)), remove(rem), bin(b),
          combined(std::make_unique<AssignExpr>(name, std::make_unique<BinaryExpr>(
              b, std::make_unique<VarExpr>(name), std::make_unique<BorrowedExpr>(rhs.get())))) {}
    Value Eval(Runtime& rt) override {
        auto take = [&](const Value& fn) {
            if (remove) rt.RemoveSub(name, fn);
            else rt.AddSub(name, fn);
            return fn;
        };
        if (auto* v = dynamic_cast<VarExpr*>(rhs.get())) {
            if (rt.Has(v->n) && Runtime::IsLambda(rt.Get(v->n)))
                return take(rt.Get(v->n));
            if (!rt.Has(v->n) && rt.functions.count(v->n)) {
                Value fn = Value::MakeMap();
                (*fn.AsMap())["__fn"] = Value{v->n};
                return take(fn);
            }
        } else if (dynamic_cast<LambdaExpr*>(rhs.get())) {
            return take(rhs->Eval(rt));
        }
        return combined->Eval(rt);
    }
};
// yield return <wait>;  — inside a coroutine: hand control back to the game loop.
struct YieldStmt : Stmt {
    ExprPtr e;
    void Exec(Runtime& r) override {
        Value v = e ? e->Eval(r) : Value{};
        if (auto m = v.AsMap()) {
            bool real = m->count("__real") > 0;
            if (auto it = m->find("__wait"); it != m->end()) { r.YieldCoroutine(it->second.AsFloat(), false, 0, Value{}, false, real); return; }
            if (auto it = m->find("__until"); it != m->end()) { r.YieldCoroutine(0.0f, false, 0, it->second, false); return; }
            if (auto it = m->find("__while"); it != m->end()) { r.YieldCoroutine(0.0f, false, 0, it->second, true); return; }
            if (auto it = m->find("__co"); it != m->end()) { r.YieldCoroutine(0.0f, false, SafeInt(it->second.AsFloat())); return; }
        }
        r.YieldCoroutine(0.0f, true, 0);   // null / WaitForEndOfFrame / anything else: next frame
    }
};
// Several statements in the current scope (C# `int a = 1, b = 2;`).
struct MultiStmt : Stmt {
    std::vector<StmtPtr> list;
    void Exec(Runtime& r) override { for (auto& st : list) st->Exec(r); }
};
struct FunctionStmt : Stmt {
    std::string name; FunctionDecl decl;
    void Exec(Runtime&) override { /* hoisted at compile time */ }
};

// Cap script call depth so endless recursion is an error, not a stack overflow.
// Counted per thread: each coroutine runs on its own thread (and stack).
thread_local int t_callDepth = 0;
struct CallDepthGuard {
    explicit CallDepthGuard(const std::string& what) {
        if (t_callDepth >= 200) throw ScriptError(what + " calls itself too deeply (endless recursion?)");
        ++t_callDepth;
    }
    ~CallDepthGuard() { --t_callDepth; }
};

Value Runtime::InvokeDecl(const FunctionDecl& fn, const std::string& label, std::vector<Value>& args) {
    CallDepthGuard depthGuard(label);
    scopes.push_back(Environment{});
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        const unsigned char k = i < fn.paramKinds.size() ? fn.paramKinds[i] : 0;
        if (!k) { Define(fn.params[i], i < args.size() ? args[i] : Value{}); continue; }
        try { DefineTyped(fn.params[i], i < args.size() ? args[i] : DefaultForKind(k), k); }
        catch (...) { scopes.pop_back(); throw; }
    }
    Value ret;
    try {
        for (auto& s : fn.body) s->Exec(*this);
    } catch (ReturnSignal& rs) {
        ret = rs.value;
    } catch (BreakSignal&) {
    } catch (ContinueSignal&) {
    } catch (...) {
        scopes.pop_back();
        throw;
    }
    scopes.pop_back();
    if (fn.returnKind & 0x0F) return CoerceToKind(fn.returnKind, ret, label);
    return ret;
}

UserClass* Runtime::ClassOf(const Value& inst) {
    auto mp = inst.AsMap();
    if (!mp) return nullptr;
    auto tag = mp->find("__class");
    if (tag == mp->end() || !tag->second.IsString()) return nullptr;
    auto cit = classes.find(tag->second.AsString());
    return cit == classes.end() ? nullptr : &cit->second;
}
bool Runtime::SameInstance(const Value& inst) const {
    auto a = self.AsMap(), b = inst.AsMap();
    return a && b && a.get() == b.get();
}
bool Runtime::HasMethod(const std::string& name) {
    if (selfClass.empty() || name.find('.') != std::string::npos) return false;
    auto cit = classes.find(selfClass);
    return cit != classes.end() && cit->second.methods.count(name) != 0;
}
static const FieldDef* FindField(const UserClass& c, const std::string& n) {
    for (auto& f : c.fields) if (f.name == n) return &f;
    return nullptr;
}
Value Runtime::GetMember(const Value& inst, const std::string& name, bool external) {
    UserClass* c = ClassOf(inst);
    if (!c) throw ScriptError("no field '" + name + "' on this value");
    const FieldDef* f = FindField(*c, name);
    if (!f) throw ScriptError(c->name + " has no field '" + name + "'");
    if (external && !f->isPublic && !SameInstance(inst))
        throw ScriptError("'" + name + "' is private on " + c->name);
    auto mp = inst.AsMap();
    auto it = mp->find(name);
    return it == mp->end() ? Value{} : it->second;
}
void Runtime::SetMember(const Value& inst, const std::string& name, const Value& v, bool external) {
    UserClass* c = ClassOf(inst);
    if (!c) throw ScriptError("can't set '" + name + "' on this value");
    const FieldDef* f = FindField(*c, name);
    if (!f) throw ScriptError(c->name + " has no field '" + name + "'");
    if (external && !f->isPublic && !SameInstance(inst))
        throw ScriptError("'" + name + "' is private on " + c->name);
    WriteField(inst, *f, v);
}
Value Runtime::CallMethod(const Value& inst, const std::string& method, std::vector<Value>& args, bool external) {
    UserClass* c = ClassOf(inst);
    if (!c) throw ScriptError("no method '" + method + "' on this value");
    auto mit = c->methods.find(method);
    if (mit == c->methods.end()) {
        if (const FieldDef* f = FindField(*c, method)) {
            if (external && !f->isPublic && !SameInstance(inst))
                throw ScriptError("'" + method + "' is private on " + c->name);
            auto mp = inst.AsMap();
            auto it = mp->find(method);
            Value fv = it == mp->end() ? Value{} : it->second;
            if (IsLambda(fv)) return CallLambda(fv, args);
        }
        throw ScriptError(c->name + " has no method '" + method + "'");
    }
    if (external && !mit->second.isPublic && !SameInstance(inst))
        throw ScriptError("'" + method + "' is private on " + c->name);
    Value prev = self;
    std::string prevClass = selfClass;
    self = inst;
    selfClass = c->name;
    try {
        Value ret = InvokeDecl(mit->second.decl, c->name + "." + method + "()", args);
        self = prev; selfClass = prevClass;
        return ret;
    } catch (...) {
        self = prev; selfClass = prevClass;
        throw;
    }
}
Value Runtime::MakeInstance(const std::string& type, std::vector<Value>& args) {
    CallDepthGuard guard("new " + type);
    auto cit = classes.find(type);
    if (cit == classes.end()) throw ScriptError("no class named '" + type + "'");
    UserClass& c = cit->second;
    Value inst = Value::MakeMap();
    (*inst.AsMap())["__class"] = Value{type};
    Value prev = self;
    std::string prevClass = selfClass;
    self = inst;
    selfClass = type;
    try {
        for (auto& f : c.fields) {
            Value v = f.init ? f.init->Eval(*this) : DefaultForKind(f.kind);
            if (f.kind & 0x0F) v = CoerceToKind(f.kind, v, f.name);
            (*inst.AsMap())[f.name] = std::move(v);
        }
        auto ct = c.ctors.find((int)args.size());
        if (ct != c.ctors.end()) InvokeDecl(ct->second.decl, type + "()", args);
        else if (!args.empty() || !c.ctors.empty())
            throw ScriptError(type + " has no constructor that takes " + std::to_string(args.size()) +
                              (args.size() == 1 ? " value" : " values"));
    } catch (...) {
        self = prev; selfClass = prevClass;
        throw;
    }
    self = prev; selfClass = prevClass;
    return inst;
}

namespace {
bool AnyNamed(const std::vector<std::string>* names) {
    if (!names) return false;
    for (const std::string& n : *names) if (!n.empty()) return true;
    return false;
}
// Lay call arguments onto parameter slots. Extra positional arguments are ignored,
// matching calls that already passed more values than a function reads.
void PlaceArgs(const std::vector<std::string>& params, const std::vector<Value>& args,
               const std::vector<std::string>* names, std::vector<char>& present,
               std::vector<Value>& slot, const std::string& what) {
    present.assign(params.size(), 0);
    slot.assign(params.size(), Value{});
    bool sawNamed = false;
    std::size_t next = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string nm = (names && i < names->size()) ? (*names)[i] : std::string();
        if (nm.empty()) {
            if (sawNamed) throw ScriptError(what + ": a value without a name can't follow a named one");
            if (next >= params.size()) continue;
            slot[next] = args[i]; present[next] = 1; ++next;
        } else {
            sawNamed = true;
            std::size_t idx = params.size();
            for (std::size_t p = 0; p < params.size(); ++p) if (params[p] == nm) { idx = p; break; }
            if (idx == params.size()) throw ScriptError(what + " has no parameter '" + nm + "'");
            if (present[idx]) throw ScriptError(what + ": '" + nm + "' was given twice");
            slot[idx] = args[i]; present[idx] = 1;
        }
    }
}
} // namespace

Value Runtime::Call(const std::string& name, std::vector<Value>& args, const std::vector<std::string>* names) {
    auto b = builtins.find(name);
    if (b != builtins.end()) {
        if (AnyNamed(names) && name != "StartCoroutine")
            throw ScriptError("'" + name + "' doesn't take named arguments");
        const std::vector<std::string>* prev = activeArgNames;
        activeArgNames = names;
        Value v = b->second(args);
        activeArgNames = prev;
        return v;
    }

    // Inside a method, a bare call prefers that class's own method.
    if (HasMethod(name)) return CallMethod(self, name, args, false);

    auto f = functions.find(name);
    if (f == functions.end()) {
        // `name.Method(...)` on a variable (or a dotted property like gameObject.name):
        // s.ToUpper(), enemies.Add(e), item.Use(), hp.ToString().
        auto dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            const std::string base = name.substr(0, dot), method = name.substr(dot + 1);
            bool ok = false;
            Value obj;
            if (base.find('.') == std::string::npos) {
                if (base == "this" && IsClassInstance(self)) { obj = self; ok = true; }
                else if (Has(base)) { obj = Get(base); ok = true; }
            }
            if (!ok) obj = GetDotted(base, ok);
            if (ok && IsClassInstance(obj)) return CallMethod(obj, method, args, true);
            Value out;
            if (ok && IsComponentRef(obj)) {   // rb.AddForce(...), anim.SetTrigger("Jump")
                std::vector<Value> a{Value{obj.AsString().substr(1)}, Value{method}};
                a.insert(a.end(), args.begin(), args.end());
                return Call("__call_component", a);
            }
            if (ok && obj.IsObject()) return CallOnObject(obj, method, args);   // other.SetActive(false)
            // door.transform.Translate(...) where `door` is a plain object name.
            if (ok && obj.IsString() && base.size() > 10 && base.compare(base.size() - 10, 10, ".transform") == 0 &&
                builtins.count("transform." + method)) {
                GameObject* me = host ? host->gameObject : nullptr;
                GameObject* o = ResolveObject(me ? me->scene() : nullptr, obj);
                if (!o) throw ScriptError("can't call '" + method + "': no object named '" + obj.AsString() + "'");
                ActAsObject as(host, o);
                return Call("transform." + method, args);
            }
            if (ok && (method == "CompareTag" || method == "GetComponent" || method == "SetActive")) {
                // `other` in a collision handler is the other object's name.
                std::vector<Value> a{obj};
                a.insert(a.end(), args.begin(), args.end());
                return Call("__object_" + method, a);
            }
            LambdaInvoker inv = [this](const Value& fv, std::vector<Value>& xs) { return CallLambda(fv, xs); };
            if (ok && CallValueMethod(obj, method, args, out, &inv)) return out;
        }
        // A variable holding a lambda: Action onHit = () => ...; onHit();
        if (Has(name) && IsLambda(Get(name))) return CallLambda(Get(name), args, names);
        throw ScriptError("call to undefined function '" + name + "'");
    }

    CallDepthGuard depthGuard(name + "()");
    const FunctionDecl& fn = f->second;
    std::vector<char> present;
    std::vector<Value> slot;
    PlaceArgs(fn.params, args, names, present, slot, name + "()");
    scopes.push_back(Environment{});
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        const unsigned char k = i < fn.paramKinds.size() ? fn.paramKinds[i] : 0;
        Value v;
        try {
            if (present[i]) v = slot[i];
            else if (i < fn.defaults.size() && fn.defaults[i]) v = fn.defaults[i]->Eval(*this);
            else v = k ? DefaultForKind(k) : Value{};
            if (!k) Define(fn.params[i], v);
            else DefineTyped(fn.params[i], v, k);
        } catch (...) { scopes.pop_back(); throw; }
    }

    Value ret;
    try {
        for (auto& s : fn.body) s->Exec(*this);
    } catch (ReturnSignal& rs) {
        ret = rs.value;
    } catch (BreakSignal&) {       // stray break/continue (no enclosing loop):
    } catch (ContinueSignal&) {    // end the function rather than escaping it.
    } catch (...) {
        scopes.pop_back();
        throw;
    }
    scopes.pop_back();
    if (fn.returnKind & 0x0F) return CoerceToKind(fn.returnKind, ret, name + "()");
    return ret;
}

Value Runtime::CallLambda(const Value& f, std::vector<Value>& args, const std::vector<std::string>* names) {
    auto m = f.AsMap();
    if (!m) throw ScriptError("not a function");
    auto it = m->find("__lambda");
    if (it == m->end()) throw ScriptError("not a function");
    auto lb = lambdas.find(SafeInt(it->second.AsFloat()));
    if (lb == lambdas.end()) throw ScriptError("lambda is no longer available");
    CallDepthGuard depthGuard("a lambda");
    const auto& ps = *lb->second.params;
    const std::vector<ExprPtr>* defs = lb->second.defaults;
    std::vector<char> present;
    std::vector<Value> slot;
    PlaceArgs(ps, args, names, present, slot, "lambda");
    scopes.push_back(Environment{});
    for (std::size_t i = 0; i < ps.size(); ++i) {
        try {
            Value v;
            if (present[i]) v = slot[i];
            else if (defs && i < defs->size() && (*defs)[i]) v = (*defs)[i]->Eval(*this);
            else v = Value{};
            Define(ps[i], v);
        } catch (...) { scopes.pop_back(); throw; }
    }
    Value ret;
    try { for (auto& st : *lb->second.body) st->Exec(*this); }
    catch (ReturnSignal& rs) { ret = rs.value; }
    catch (...) { scopes.pop_back(); throw; }
    scopes.pop_back();
    return ret;
}

// ---- dotted property access --------------------------------------
// Frames per second over the last half second of REAL frame times (the loop that is running the game keeps
// them, GameLoop::Stats()), so a pause, a slow-motion scale or a hit stop does not change it and a stall shows.
// Without a loop (a host that does not run one) the last frame's real time stands in.
float RealFps() {
    if (const GameLoop* loop = GameLoop::Current()) {
        const double f = loop->Stats().AverageFps(30);
        if (f > 0.0) return (float)f;
    }
    const float u = Time::UnscaledDeltaTime();
    return u > 1e-6f ? 1.0f / u : 0.0f;
}
namespace {
// Euler Z angle (degrees) of a quaternion, for transform.eulerAngles.z.
float EulerZ(const Quat& q) {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * Mathf::Rad2Deg;
}
} // namespace

Value Runtime::CallOnObject(const Value& ref, const std::string& method, std::vector<Value>& args) {
    if (method == "CompareTag" || method == "GetComponent" || method == "SetActive") {
        std::vector<Value> a{ref};
        a.insert(a.end(), args.begin(), args.end());
        return Call("__object_" + method, a);
    }
    // ref.transform.Translate(...), ref.transform.LookAt(...): this object's
    // transform calls, run on the referenced object.
    if (builtins.count("transform." + method)) {
        GameObject* o = ResolveObject(nullptr, ref);
        if (!o) throw ScriptError("can't call '" + method + "': the object no longer exists");
        ActAsObject as(host, o);
        return Call("transform." + method, args);
    }
    // Text methods on its name: other.StartsWith("Enemy"), other.ToString().
    Value out, nm{ref.AsString()};
    LambdaInvoker inv = [this](const Value& fv, std::vector<Value>& xs) { return CallLambda(fv, xs); };
    if (CallValueMethod(nm, method, args, out, &inv)) return out;
    throw ScriptError("no method '" + method + "' on an object reference");
}

Value Runtime::GetDotted(const std::string& name, bool& ok) {
    ok = true;
    Transform* t = host ? host->transform : nullptr;
    GameObject* g = host ? host->gameObject : nullptr;

    // Time
    if (name == "Time.deltaTime")  return Value{host ? host->deltaTime : 0.0f};
    if (name == "Time.time")       return Value{Time::ElapsedTime()};
    if (name == "Time.timeScale")  return Value{Time::TimeScale()};
    if (name == "Time.frameCount") return Value{(float)Time::FrameCount()};
    // The step of a FixedUpdate and the real frame time were both answered with this frame's SCALED delta
    // (so Time.unscaledDeltaTime read 0 in a pause and half of itself at timeScale 0.5).
    if (name == "Time.fixedDeltaTime")      return Value{Time::FixedDeltaTime()};
    if (name == "Time.physicsSteps") {
        Scene* sc = g ? g->scene() : nullptr;
        return Value{sc ? sc->PhysicsStepsPerSecond() : 0.0f};
    }
    if (name == "Time.smoothPhysics") {
        Scene* sc = g ? g->scene() : nullptr;
        return Value{sc ? sc->SmoothPhysics() : false};
    }
    if (name == "Time.unscaledDeltaTime")   return Value{Time::UnscaledDeltaTime()};
    if (name == "Time.unscaledTime")        return Value{(float)Time::UnscaledTime()};
    // Since this Play began (the game's clocks that Time.time and Time.frameCount are the process's own).
    if (name == "Time.sessionTime")         return Value{(float)Time::SessionTime()};
    if (name == "Time.sessionUnscaledTime") return Value{(float)Time::SessionUnscaledTime()};
    if (name == "Time.sessionFrameCount")   return Value{(float)Time::SessionFrameCount()};
    if (name == "Time.fps")                 return Value{RealFps()};
    if (name == "Time.paused")              return Value{Game::Paused()};
    if (name == "Time.hitchCount")          return Value{(float)(GameLoop::Current() ? GameLoop::Current()->HitchCount() : 0)};
    if (name == "Space.World")     return Value{1.0f};
    if (name == "Space.Self")      return Value{0.0f};
    // ForceMode (3D) uses the engine's ForceMode order; ForceMode2D.Impulse is 1,
    // the same as the ForceMode2D.Impulse() call. #888 F109: these read as 0 before.
    if (name == "ForceMode.Force")          return Value{0.0f};
    if (name == "ForceMode.Acceleration")   return Value{1.0f};
    if (name == "ForceMode.Impulse")        return Value{2.0f};
    if (name == "ForceMode.VelocityChange") return Value{3.0f};
    if (name == "ForceMode2D.Force")        return Value{0.0f};
    if (name == "ForceMode2D.Impulse")      return Value{1.0f};
    if (name == "RigidbodyType2D.Dynamic")   return Value{0.0f};
    if (name == "RigidbodyType2D.Kinematic") return Value{1.0f};
    if (name == "RigidbodyType2D.Static")    return Value{2.0f};
    if (name == "null")            return Value{};
    // Mathf constants
    if (name == "Mathf.PI")        return Value{Mathf::PI};
    if (name == "Mathf.Deg2Rad")   return Value{Mathf::Deg2Rad};
    if (name == "Mathf.Rad2Deg")   return Value{Mathf::Rad2Deg};
    if (name == "Mathf.Infinity")  return Value{1e30f};
    if (name == "Mathf.Epsilon")   return Value{1e-6f};
    if (name == "Random.value")    return Value{Random::Shared().Range(0.0f, 1.0f)};
    // Screen / Input
    if (name == "Screen.width")        return Value{UICanvas::Width()};
    if (name == "Screen.height")       return Value{UICanvas::Height()};
    if (name == "Input.mousePosition.x") return Value{Input::MousePosition().x};
    if (name == "Input.mousePosition.y") return Value{Input::MousePosition().y};
    if (name == "Input.mousePosition")   { Vec2 m = Input::MousePosition(); return Value{Vec3{m.x, m.y, 0.0f}}; }
    // Vector constants
    if (name == "Vector3.zero" || name == "Vector2.zero") return Value{Vec3::Zero};
    if (name == "Vector3.one"  || name == "Vector2.one")  return Value{Vec3{1, 1, 1}};
    if (name == "Quaternion.identity") return Value{Vec3::Zero};   // euler (0,0,0)
    // Color constants (RGB as a Vec3; pass to set_color/set_tint).
    if (name == "Color.red")     return Value{Vec3{1, 0, 0}};
    if (name == "Color.green")   return Value{Vec3{0, 1, 0}};
    if (name == "Color.blue")    return Value{Vec3{0, 0, 1}};
    if (name == "Color.white")   return Value{Vec3{1, 1, 1}};
    if (name == "Color.black")   return Value{Vec3{0, 0, 0}};
    if (name == "Color.yellow")  return Value{Vec3{1, 1, 0}};
    if (name == "Color.cyan")    return Value{Vec3{0, 1, 1}};
    if (name == "Color.magenta") return Value{Vec3{1, 0, 1}};
    if (name == "Color.gray" || name == "Color.grey") return Value{Vec3{0.5f, 0.5f, 0.5f}};
    if (name == "Vector3.up")    return Value{Vec3{0, 1, 0}};
    if (name == "Vector3.down")  return Value{Vec3{0, -1, 0}};
    if (name == "Vector3.right") return Value{Vec3{1, 0, 0}};
    if (name == "Vector3.left")  return Value{Vec3{-1, 0, 0}};
    if (name == "Vector3.forward") return Value{Vec3{0, 0, 1}};
    if (name == "Vector3.back")    return Value{Vec3{0, 0, -1}};
    if (name == "Vector2.up")    return Value{Vec3{0, 1, 0}};
    if (name == "Vector2.down")  return Value{Vec3{0, -1, 0}};
    if (name == "Vector2.right") return Value{Vec3{1, 0, 0}};
    if (name == "Vector2.left")  return Value{Vec3{-1, 0, 0}};

    if (t && selfClass.empty()) {
        Vec3 wp = t->Position();
        if (name == "transform.position")   return Value{wp};
        if (name == "transform.position.x") return Value{wp.x};
        if (name == "transform.position.y") return Value{wp.y};
        if (name == "transform.position.z") return Value{wp.z};
        if (name == "transform.localPosition")   return Value{t->localPosition};
        if (name == "transform.localPosition.x") return Value{t->localPosition.x};
        if (name == "transform.localPosition.y") return Value{t->localPosition.y};
        if (name == "transform.localPosition.z") return Value{t->localPosition.z};
        if (name == "transform.localScale")   return Value{t->localScale};
        if (name == "transform.localScale.x") return Value{t->localScale.x};
        if (name == "transform.localScale.y") return Value{t->localScale.y};
        if (name == "transform.localScale.z") return Value{t->localScale.z};
        if (name == "transform.eulerAngles.z" || name == "transform.rotation")
            return Value{EulerZ(t->localRotation)};
        if (name == "transform.eulerAngles" || name == "transform.localEulerAngles") return Value{t->localRotation.ToEuler()};
        if (name == "transform.eulerAngles.x" || name == "transform.localEulerAngles.x") return Value{t->localRotation.ToEuler().x};
        if (name == "transform.eulerAngles.y" || name == "transform.localEulerAngles.y") return Value{t->localRotation.ToEuler().y};
        if (name == "transform.localEulerAngles.z") return Value{t->localRotation.ToEuler().z};
        if (name == "transform.forward") return Value{t->Forward()};
        if (name == "transform.up")      return Value{t->Up()};
        if (name == "transform.right")   return Value{t->Right()};
    }
    if (g && selfClass.empty()) {
        if (name == "gameObject.name") return Value{g->name};
        if (name == "gameObject.activeSelf" || name == "gameObject.active") return Value{g->active};
        if (name == "gameObject.tag")  return Value{g->tag};
    }
    // Generic properties on a variable: Vector3 (.x/.magnitude/.normalized),
    // strings (.Length), and arrays (.Length / .Count). e.g.  v.magnitude,
    // name.Length, items.Count.
    {
        auto dot = name.rfind('.');
        if (dot != std::string::npos) {
            std::string base = name.substr(0, dot), prop = name.substr(dot + 1);
            if (base == "this" && IsClassInstance(self)) return GetMember(self, prop, false);
            if (base.find('.') == std::string::npos && Has(base)) {
                Value out, bv = Get(base);
                if (IsClassInstance(bv)) return GetMember(bv, prop, true);
                if (IsComponentRef(bv)) { std::vector<Value> a{Value{bv.AsString().substr(1)}, Value{prop}}; return Call("__get_component", a); }
                if (bv.IsObject()) {   // other.tag, other.transform, other.name
                    GameObject* o = ResolveObject(nullptr, bv);
                    if (ObjectMember(o, bv, prop, out)) return out;
                    if (!o) throw ScriptError("can't read '" + name + "': the object no longer exists");
                }
                if (ValueProperty(bv, prop, out)) return out;
                // An object's name (e.g. `other` in a collision handler): other.name, other.tag,
                // other.gameObject, other.transform.position.
                if (bv.IsString() && g && g->scene()) {
                    if (prop == "name" || prop == "gameObject" || prop == "transform") return bv;
                    if (GameObject* o = g->scene()->Find(bv.AsString())) {
                        if (prop == "tag") return Value{o->tag};
                        if (prop == "position" && o->transform) return Value{o->transform->Position()};
                        if (prop == "activeSelf") return Value{o->active};
                    }
                }
            } else if (base.find('.') != std::string::npos) {
                // A property of a property: transform.position.normalized, gameObject.name.Length.
                bool inner = false;
                Value bv = GetDotted(base, inner), out;
                if (inner && IsClassInstance(bv)) return GetMember(bv, prop, true);
                if (inner && bv.IsObject()) {   // other.transform.position
                    GameObject* o = ResolveObject(nullptr, bv);
                    if (ObjectMember(o, bv, prop, out)) return out;
                    if (!o) throw ScriptError("can't read '" + name + "': the object no longer exists");
                }
                if (inner && ValueProperty(bv, prop, out)) return out;
                if (inner && bv.IsString() && g && g->scene() && base.rfind(".transform") == base.size() - 10)
                    if (GameObject* o = g->scene()->Find(bv.AsString()))   // other.transform.position
                        if (prop == "position" && o->transform) return Value{o->transform->Position()};
            }
        }
    }
    ok = false;
    return Value{};
}

bool Runtime::SetDotted(const std::string& name, const Value& v) {
    Transform* t = host ? host->transform : nullptr;
    GameObject* g = host ? host->gameObject : nullptr;

    if (auto dot = name.find('.'); dot != std::string::npos && name.find('.', dot + 1) == std::string::npos) {
        const std::string base = name.substr(0, dot), field = name.substr(dot + 1);
        if (base == "this" && IsClassInstance(self)) { SetMember(self, field, v, false); return true; }
        if (Has(base) && IsClassInstance(Get(base))) { SetMember(Get(base), field, v, true); return true; }
    }
    if (name == "Time.timeScale") { Time::SetTimeScale(v.AsFloat()); return true; }
    if (name == "Time.physicsSteps") {
        if (Scene* sc = g ? g->scene() : nullptr) sc->SetPhysicsStepsPerSecond(v.AsFloat());
        return true;
    }
    if (name == "Time.smoothPhysics") {
        if (Scene* sc = g ? g->scene() : nullptr) sc->SetSmoothPhysics(v.AsBool());
        return true;
    }
    // door.transform.position = v / other.transform.localScale = v through an
    // object reference (or a plain object name, for transform members) held in
    // a variable. Before #888 F111 these writes were dropped without a word.
    if (auto dot = name.find('.'); dot != std::string::npos) {
        const std::string base = name.substr(0, dot), rest = name.substr(dot + 1);
        if (base.find('.') == std::string::npos && !(base == "this" && IsClassInstance(self)) && Has(base)) {
            Value bv = Get(base);
            if (bv.IsObject()) {
                GameObject* o = ResolveObject(nullptr, bv);
                if (!o) throw ScriptError("can't set '" + name + "': the object no longer exists");
                if (!SetObjectMember(o, rest, v)) throw ScriptError("can't set '" + rest + "' on an object");
                return true;
            }
            if (bv.IsString() && !IsComponentRef(bv) && rest.rfind("transform.", 0) == 0 && g && g->scene()) {
                if (GameObject* o = ResolveObject(g->scene(), bv)) if (SetObjectMember(o, rest, v)) return true;
            }
        }
    }
    // rb.velocity = ... on a GetComponent<T>() reference held in a variable.
    if (auto dot = name.find('.'); dot != std::string::npos && name.find('.', dot + 1) == std::string::npos) {
        const std::string base = name.substr(0, dot);
        if (Has(base)) {
            Value bv = Get(base);
            if (IsComponentRef(bv)) {
                std::vector<Value> a{Value{bv.AsString().substr(1)}, Value{name.substr(dot + 1)}, v};
                Call("__set_component", a);
                return true;
            }
        }
    }
    if (t && selfClass.empty()) {
        if (name == "transform.position")        { t->SetPosition(v.AsVec3()); return true; }
        if (name == "transform.localPosition")   { t->localPosition = v.AsVec3(); return true; }
        if (name == "transform.localScale")      { t->localScale = v.AsVec3(); return true; }
        if (name == "transform.position.x")      { Vec3 p = t->Position(); p.x = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.position.y")      { Vec3 p = t->Position(); p.y = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.position.z")      { Vec3 p = t->Position(); p.z = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.localPosition.x") { t->localPosition.x = v.AsFloat(); return true; }
        if (name == "transform.localPosition.y") { t->localPosition.y = v.AsFloat(); return true; }
        if (name == "transform.localPosition.z") { t->localPosition.z = v.AsFloat(); return true; }
        if (name == "transform.localScale.x")    { t->localScale.x = v.AsFloat(); return true; }
        if (name == "transform.localScale.y")    { t->localScale.y = v.AsFloat(); return true; }
        if (name == "transform.localScale.z")    { t->localScale.z = v.AsFloat(); return true; }
        if (name == "transform.eulerAngles.z")
            { t->localRotation = Quat::Euler({0, 0, v.AsFloat()}); return true; }
        if (name == "transform.eulerAngles" || name == "transform.localEulerAngles")
            { t->localRotation = Quat::Euler(v.AsVec3()); return true; }
        // transform.rotation accepts a Quaternion.Euler(...) (a Vec3 euler) or a
        // bare Z angle; use the Z component either way.
        if (name == "transform.rotation")
            { t->localRotation = Quat::Euler({0, 0, v.AsVec3().z}); return true; }
    }
    if (g && selfClass.empty()) {
        if (name == "gameObject.name")       { g->name = v.AsString(); return true; }
        if (name == "gameObject.activeSelf" || name == "gameObject.active") { g->active = v.AsBool(); return true; }
        if (name == "gameObject.tag")        { g->tag = v.AsString(); return true; }
    }
    // Generic: <var>.x/.y/.z = value, for a variable holding a Vector3.
    {
        auto dot = name.rfind('.');
        if (dot != std::string::npos && dot + 2 == name.size()) {
            std::string base = name.substr(0, dot); char comp = name[dot + 1];
            if (base.find('.') == std::string::npos && (comp == 'x' || comp == 'y' || comp == 'z') && Has(base)) {
                Vec3 vec = Get(base).AsVec3();
                if (comp == 'x') vec.x = v.AsFloat(); else if (comp == 'y') vec.y = v.AsFloat(); else vec.z = v.AsFloat();
                Assign(base, Value{vec});
                return true;
            }
        }
    }
    return false;
}

// ===================== Parser =====================
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : m_toks(std::move(toks)) { ScanEnums(); ScanUserClasses(); }

    // C# enums anywhere in the file: `enum State { Idle, Run = 5, Jump }` makes
    // State.Idle / State.Run / State.Jump number constants (0, 5, 6).
    void ScanEnums() {
        for (std::size_t i = 0; i + 2 < m_toks.size(); ++i) {
            if (m_toks[i].type != Tok::Ident || m_toks[i].text != "enum" || m_toks[i + 1].type != Tok::Ident) continue;
            const std::string en = m_toks[i + 1].text;
            std::size_t j = i + 2;
            while (m_toks[j].type != Tok::LBrace && m_toks[j].type != Tok::End) ++j;   // `: byte`
            if (m_toks[j].type != Tok::LBrace) continue;
            m_enums.insert(en);
            double next = 0;
            for (++j; m_toks[j].type != Tok::RBrace && m_toks[j].type != Tok::End; ++j) {
                if (m_toks[j].type != Tok::Ident) continue;
                std::string member = m_toks[j].text;
                if (m_toks[j + 1].type == Tok::Assign) {
                    bool neg = m_toks[j + 2].type == Tok::Minus;
                    const Token& v = m_toks[j + 2 + (neg ? 1 : 0)];
                    if (v.type == Tok::Number) next = neg ? -v.number : v.number;
                    else if (auto it = m_enumValues.find(en + "." + v.text); it != m_enumValues.end()) next = it->second;
                }
                m_enumValues[en + "." + member] = next;
                next += 1;
                while (m_toks[j + 1].type != Tok::Comma && m_toks[j + 1].type != Tok::RBrace && m_toks[j + 1].type != Tok::End) ++j;
            }
        }
    }
    // Plain classes (`class Item { ... }`, no base) are real types made with `new`.
    // A class with a base (`class Hero : Behaviour`) is the component and is not recorded.
    void ScanUserClasses() {
        for (std::size_t i = 0; i + 1 < m_toks.size(); ++i) {
            if (m_toks[i].type != Tok::Ident) continue;
            if (m_toks[i].text != "class" && m_toks[i].text != "struct") continue;
            if (m_toks[i + 1].type != Tok::Ident) continue;
            const std::size_t j = SkipTypeSuffix(i + 2);
            if (m_toks[j].type == Tok::LBrace) m_userClasses.insert(m_toks[i + 1].text);
        }
    }
    // Skip `[modifiers] enum Name [: type] { ... }` (already collected by ScanEnums).
    bool SkipEnumDecl() {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident && m_toks[i].text != "enum") ++i;
        if (m_toks[i].type != Tok::Ident || m_toks[i].text != "enum") return false;
        while (m_toks[i].type != Tok::LBrace && m_toks[i].type != Tok::End) ++i;
        int depth = 0;
        for (; m_toks[i].type != Tok::End; ++i) {
            if (m_toks[i].type == Tok::LBrace) ++depth;
            else if (m_toks[i].type == Tok::RBrace && --depth == 0) { ++i; break; }
        }
        m_pos = i;
        Match(Tok::Semicolon);
        return true;
    }

    std::vector<StmtPtr> ParseProgram(std::unordered_map<std::string, FunctionDecl>& funcs) {
        std::vector<StmtPtr> top;
        while (!Check(Tok::End)) ParseMember(top, funcs);
        return top;
    }
    std::unordered_map<std::string, UserClass> TakeClasses() { return std::move(m_classes); }

    // Parse for diagnostics only: keep going after a parse error by recording it
    // and re-synchronizing to the next statement boundary, so the editor can list
    // EVERY syntax error at once instead of just the first. The parsed output is
    // discarded — this exists purely to collect ScriptDiagnostic entries.
    // Syntax OkayScript accepts (so pasted C# runs) but that has no effect. Each is
    // reported once per line as a warning, so the editor can say exactly what to
    // delete instead of letting it look meaningful.
    std::vector<ScriptDiagnostic> NoEffectWarnings() const {
        std::vector<ScriptDiagnostic> w;
        std::set<std::pair<int, std::string>> seen;
        auto add = [&](int line, int col, const std::string& key, const std::string& msg) {
            if (seen.insert({line, key}).second) w.push_back({line, msg, 1, col});
        };
        static const std::unordered_map<std::string, const char*> kMods = {
            {"virtual",   "`virtual` does nothing: scripts don't inherit from each other. Delete it."},
            {"override",  "`override` does nothing: Start, Update and the other events are found by name. Delete it."},
            {"abstract",  "`abstract` does nothing in a script. Delete it."},
            {"sealed",    "`sealed` does nothing in a script. Delete it."},
            {"internal",  "`internal` does nothing: use `public` to show a field in the Inspector, or leave it off."},
            {"protected", "`protected` does nothing: use `public` to show a field in the Inspector, or leave it off."},
            {"partial",   "`partial` does nothing in a script. Delete it."},
            {"extern",    "`extern` does nothing in a script. Delete it."},
            {"unsafe",    "`unsafe` does nothing in a script. Delete it."},
            {"private",   "`private` is already the default (only `public` fields show in the Inspector). You can delete it."},
        };
        static const std::set<std::string> kNeverCalled = {
            "OnGUI", "OnApplicationQuit", "OnApplicationPause", "OnApplicationFocus", "OnBecameVisible",
            "OnBecameInvisible", "OnValidate", "Reset", "OnDrawGizmos", "OnDrawGizmosSelected", "OnPreRender",
            "OnPostRender", "OnRenderObject", "OnWillRenderObject", "OnParticleCollision", "OnControllerColliderHit",
            "OnJointBreak", "OnAnimatorMove", "OnAnimatorIK", "OnTransformParentChanged", "OnTransformChildrenChanged"};
        for (std::size_t i = 0; m_toks[i].type != Tok::End; ++i) {
            const Token& t = m_toks[i];
            const Token& nx = m_toks[i + 1];
            if (t.type == Tok::Number && t.suffix) {
                add(t.line, t.column, "suffix", "`" + t.text + "f`: the letter after the number does nothing (every number is "
                                      "already a decimal). Write " + t.text + ".");
                continue;
            }
            if (t.type != Tok::Ident) continue;
            if (t.text == "using" && nx.type == Tok::Ident)
                add(t.line, t.column, "using", "`using` does nothing: everything is already available. Delete this line.");
            else if (t.text == "namespace" && nx.type == Tok::Ident)
                add(t.line, t.column, "namespace", "`namespace` does nothing. Delete it (and its braces); keep what's inside.");
            else if ((t.text == "class" || t.text == "struct") && nx.type == Tok::Ident) {
                const std::size_t after = SkipTypeSuffix(i + 2);
                if (m_toks[after].type == Tok::Colon)
                    add(t.line, t.column, "class", "The class wrapper and `: " + m_toks[after + 1].text + "` do nothing: the fields "
                                         "and methods in this file are the component. Delete the `class " + nx.text +
                                         "` line and its braces.");
                // A plain `class Name { }` is a real script class. Only `: Base`
                // is still the old component wrapper, warned about above.
            } else if (nx.type == Tok::Ident) {
                auto m = kMods.find(t.text);
                if (m != kMods.end()) add(t.line, t.column, t.text, m->second);
            }
            if (kNeverCalled.count(t.text) && nx.type == Tok::LParen && i > 0 && m_toks[i - 1].type == Tok::Ident)
                add(t.line, t.column, "never", "`" + t.text + "()` is never called by the engine, so this code won't run.");
        }
        return w;
    }

    std::vector<ScriptDiagnostic> ParseProgramRecovering() {
        std::vector<ScriptDiagnostic> diags;
        std::vector<StmtPtr> top;
        std::unordered_map<std::string, FunctionDecl> funcs;
        while (!Check(Tok::End)) {
            std::size_t before = m_pos;
            try {
                ParseMember(top, funcs);
            } catch (const ScriptError& e) {
                diags.push_back({ e.line, e.what(), 0, e.column });
                if (diags.size() >= 100) break;          // stop runaway cascades
                Synchronize(before);
            }
            if (m_pos == before) ++m_pos;                // guarantee forward progress
        }
        for (auto& d : m_semantic) diags.push_back(d);
        for (auto& d : NoEffectWarnings()) diags.push_back(d);
        std::stable_sort(diags.begin(), diags.end(),
                         [](const ScriptDiagnostic& a, const ScriptDiagnostic& b) { return a.line < b.line; });
        return diags;
    }

    // Parse one top-level (or in-class) member: a function, a C#-style method,
    // a `[public] class Name : OkaySource { ... }` wrapper (its methods/fields
    // are hoisted out, so pasted C# scripts work), or a statement.
    void ParseMember(std::vector<StmtPtr>& top, std::unordered_map<std::string, FunctionDecl>& funcs) {
        SkipAttributes();   // C# attributes: [SerializeField], [Header("..")], ...
        // C# `using X;` lines — nothing to import.
        if (Check(Tok::Ident) && Peek().text == "using" && m_toks[m_pos + 1].type == Tok::Ident) {
            m_pos += 2;
            while (Check(Tok::Ident) || Check(Tok::Assign) || Check(Tok::Dot)) ++m_pos;   // using X = Y.Z;
            Match(Tok::Semicolon);
            return;
        }
        if (IsEnumAhead()) { SkipEnumDecl(); return; }
        // `namespace Game.Player { ... }` (or the file-scoped `namespace Game;`): its members are ours.
        if (Check(Tok::Ident) && Peek().text == "namespace" && m_toks[m_pos + 1].type == Tok::Ident) {
            m_pos += 2;
            while (Check(Tok::Ident) || Check(Tok::Dot)) ++m_pos;
            if (Match(Tok::Semicolon)) return;
            Expect(Tok::LBrace, "'{'");
            while (!Check(Tok::RBrace) && !Check(Tok::End)) ParseMember(top, funcs);
            Expect(Tok::RBrace, "'}'");
            return;
        }
        if (IsClassAhead()) {
            if (!ClassHasBase()) { ParseUserClass(); return; }
            while (Check(Tok::Ident) && Peek().text != "class" && Peek().text != "struct") ++m_pos;  // modifiers
            Expect(Tok::Ident, "'class'");                                // the 'class' keyword
            Expect(Tok::Ident, "class name");                            // its name
            m_pos = SkipTypeSuffix(m_pos);                               // generic class Foo<T>
            if (Match(Tok::Colon)) {
                do { Expect(Tok::Ident, "base type"); m_pos = SkipTypeSuffix(m_pos); } while (Match(Tok::Comma));
            }
            Expect(Tok::LBrace, "'{'");
            while (!Check(Tok::RBrace) && !Check(Tok::End)) ParseMember(top, funcs);
            Expect(Tok::RBrace, "'}'");
            return;
        }
        if (Check(Tok::Function)) {
            std::string name; FunctionDecl decl = ParseFunction(name);
            funcs[name] = std::move(decl);
        } else if (IsTypedFunctionAhead()) {            // C#-style: void Start() { }
            std::string name; FunctionDecl decl = ParseTypedFunction(name);
            funcs[name] = std::move(decl);
        } else if (IsBareFunctionAhead()) {             // OkayScript: update(dt) { }
            std::string name = Expect(Tok::Ident, "function name").text;
            FunctionDecl decl = ParseParamsAndBody();
            funcs[name] = std::move(decl);
        } else {
            top.push_back(ParseStatement());
        }
    }

    // A brace-less OkayScript function: `name(params) { ... }` — a single
    // identifier, then a parenthesised list, then a '{'. That last '{' is what
    // tells it apart from an ordinary call statement like `on_key_move(5)` (which
    // is followed by a newline/';'), so the `function` keyword is optional.
    bool IsBareFunctionAhead() const {
        std::size_t i = m_pos;
        if (m_toks[i].type != Tok::Ident) return false;
        // Control-flow words that are lexed as identifiers (not keyword tokens)
        // can be `word (...) { ... }`, which otherwise looks exactly like a bare
        // function definition — exclude them so e.g. `foreach (x in xs) {}` is
        // parsed as a statement, not a function named "foreach".
        if (m_toks[i].text == "foreach" || m_toks[i].text == "do") return false;
        if (m_toks[i + 1].type != Tok::LParen) return false;
        int depth = 0;
        for (i += 1; m_toks[i].type != Tok::End; ++i) {
            if (m_toks[i].type == Tok::LParen) ++depth;
            else if (m_toks[i].type == Tok::RParen && --depth == 0) { ++i; break; }
        }
        return m_toks[i].type == Tok::LBrace;
    }

    // Skip C# attributes like [SerializeField] or [Header("Stats")] that may
    // precede a field/method. Balanced brackets, so [Range(0, 1)] works too.
    void SkipAttributes() {
        while (Check(Tok::LBracket)) {
            int depth = 0;
            do {
                if (Check(Tok::LBracket)) ++depth;
                else if (Check(Tok::RBracket)) --depth;
                ++m_pos;
            } while (depth > 0 && !Check(Tok::End));
        }
    }

    bool IsEnumAhead() const {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident) { if (m_toks[i].text == "enum") return m_toks[i + 1].type == Tok::Ident; ++i; }
        return false;
    }
    // A run of identifiers containing "class" (e.g. `public class Foo`).
    bool IsClassAhead() const {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident) {
            if (m_toks[i].text == "class" || m_toks[i].text == "struct") return m_toks[i + 1].type == Tok::Ident;
            ++i;
        }
        return false;
    }
    // `class Item {` is a plain class. `class Hero : Behaviour {` is the component.
    bool ClassHasBase() const {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident && m_toks[i].text != "class" && m_toks[i].text != "struct") ++i;
        if (m_toks[i].type != Tok::Ident) return false;
        ++i;
        if (m_toks[i].type != Tok::Ident) return false;
        i = SkipTypeSuffix(i + 1);
        return m_toks[i].type == Tok::Colon;
    }

private:
    const Token& Peek() const { return m_toks[m_pos]; }
    const Token& Prev() const { return m_toks[m_pos - 1]; }
    bool Check(Tok t) const { return Peek().type == t; }
    bool Match(Tok t) { if (Check(t)) { ++m_pos; return true; } return false; }
    const Token& Expect(Tok t, const char* msg) {
        if (!Check(t)) throw ScriptError(std::string("parse error: expected ") + msg, Peek().line, Peek().column);
        return m_toks[m_pos++];
    }

    // Error-recovery: after a parse error at `errStart`, skip past the offending
    // token and advance to the next likely statement start (just after a ';', or
    // before a top-level keyword) so parsing can resume and find further errors.
    void Synchronize(std::size_t errStart) {
        if (m_pos <= errStart) m_pos = errStart + 1;
        while (!Check(Tok::End)) {
            if (m_toks[m_pos - 1].type == Tok::Semicolon) return;
            switch (Peek().type) {
                case Tok::Function: case Tok::Var: case Tok::If:
                case Tok::While:    case Tok::For: case Tok::Return:
                    return;
                default: ++m_pos;
            }
        }
    }

    FunctionDecl ParseFunction(std::string& nameOut) {
        Expect(Tok::Function, "'function'");
        nameOut = Expect(Tok::Ident, "function name").text;
        return ParseParamsAndBody();
    }

    // A C#-style method declaration like `void Update()` / `private void Start()`
    // is a run of >= 2 identifiers (modifiers + return type + name) ending in a
    // name immediately followed by '('. (A plain call `foo()` is a single ident.)
    bool IsTypedFunctionAhead() const {
        int idents = 0;
        std::size_t i = ScanIdentRun(m_pos, idents);
        return idents >= 2 && m_toks[i].type == Tok::LParen;
    }

    // Skip a C# type's generic arguments and array ranks at token `i` (just past
    // its identifier): List<int>, Dictionary<string, int>, int[], float[,].
    // Returns `i` unchanged when what follows isn't type syntax.
    std::size_t SkipTypeSuffix(std::size_t i) const {
        if (m_toks[i].type == Tok::Lt) {
            int depth = 0; std::size_t j = i;
            for (; m_toks[j].type != Tok::End; ++j) {
                const Tok t = m_toks[j].type;
                if (t == Tok::Lt) ++depth;
                else if (t == Tok::Gt) { if (--depth == 0) { ++j; break; } }
                else if (t != Tok::Ident && t != Tok::Comma && t != Tok::LBracket && t != Tok::RBracket) return i;
            }
            if (depth != 0) return i;
            i = j;
        }
        while (m_toks[i].type == Tok::LBracket) {
            std::size_t j = i + 1;
            while (m_toks[j].type == Tok::Comma) ++j;
            if (m_toks[j].type != Tok::RBracket) break;
            i = j + 1;
        }
        return i;
    }
    // A run of identifiers (modifiers, a possibly generic / array type, a name).
    std::size_t ScanIdentRun(std::size_t i, int& idents) const {
        idents = 0;
        while (m_toks[i].type == Tok::Ident) { ++idents; ++i; i = SkipTypeSuffix(i); }
        return i;
    }
    // Consume such a run and return its last identifier (the declared name).
    std::string ConsumeIdentRun(const char* what) {
        std::string last = Expect(Tok::Ident, what).text;
        for (;;) {
            m_pos = SkipTypeSuffix(m_pos);
            if (!Check(Tok::Ident)) break;
            last = m_toks[m_pos++].text;
        }
        return last;
    }

    FunctionDecl ParseTypedFunction(std::string& nameOut) {
        // Consume modifier/type identifiers, keeping the last one (before '(')
        // as the function name. The return type is real: `void` can't return a
        // value, `int` / `float` / `bool` / `string` convert what's returned.
        bool isVoid = false;
        for (std::size_t i = m_pos; m_toks[i].type == Tok::Ident && m_toks[i + 1].type != Tok::LParen; i = SkipTypeSuffix(i + 1))
            if (m_toks[i].text == "void") isVoid = true;
        const unsigned char ret = (unsigned char)(DeclKindAhead() & 0x0F);
        nameOut = ConsumeIdentRun("type");
        const bool prevVoid = m_inVoid; const std::string prevFn = m_fnName;
        m_inVoid = isVoid; m_fnName = nameOut;
        FunctionDecl decl = ParseParamsAndBody();
        m_inVoid = prevVoid; m_fnName = prevFn;
        decl.returnKind = ret;
        return decl;
    }

    // Shared by `function f(...)` and `void F(...)`: parse the parameter list
    // (params may be bare `a` or typed `int a`) and the body block.
    FunctionDecl ParseParamsAndBody() {
        FunctionDecl decl;
        Expect(Tok::LParen, "'('");
        if (!Check(Tok::RParen)) {
            do {
                // `dt`, `float dt`, `List<int> xs`, `ref int n` — the last identifier is the name.
                const unsigned char pk = (unsigned char)(DeclKindAhead() & 0x0F);
                std::string p = ConsumeIdentRun("parameter");
                ExprPtr def;
                if (Match(Tok::Assign)) def = ParseExpression();   // `void Heal(int hp, int bonus = 2)`
                else if (!decl.defaults.empty() && decl.defaults.back())
                    throw ScriptError("parameter '" + p + "' needs a default too (ones with defaults come last)", Peek().line);
                for (const std::string& prev : decl.params)
                    if (prev == p) throw ScriptError("parameter '" + p + "' is already in this function", Peek().line);
                decl.params.push_back(p);
                decl.paramKinds.push_back(pk);
                decl.defaults.push_back(std::move(def));
            } while (Match(Tok::Comma));
        }
        Expect(Tok::RParen, "')'");
        decl.body = ParseBlock();
        return decl;
    }

    std::vector<StmtPtr> ParseBlock() {
        Expect(Tok::LBrace, "'{'");
        std::vector<StmtPtr> stmts;
        while (!Check(Tok::RBrace) && !Check(Tok::End)) stmts.push_back(ParseStatement());
        Expect(Tok::RBrace, "'}'");
        return stmts;
    }

    // A control-flow body: either a `{ ... }` block, or — to keep simple code short —
    // a single statement with no braces (C/JS style). So `if (hit) jump(8)` and
    // `while (alive) step()` both work without the ceremony of braces.
    std::vector<StmtPtr> ParseBlockOrStmt() {
        if (Check(Tok::LBrace)) return ParseBlock();
        return SingleStmt();
    }

    // The declared type + const-ness of the typed declaration starting at m_pos
    // (`const int MAX`, `public float speed`, `List<int> xs`). Generic / array
    // types (`int[]`, `List<int>`) hold anything.
    unsigned char DeclKindAhead() const {
        unsigned char kind = KAny, flags = 0;
        std::vector<std::pair<std::size_t, bool>> run;   // (token, has a generic / array suffix)
        for (std::size_t i = m_pos; m_toks[i].type == Tok::Ident;) {
            const std::size_t next = SkipTypeSuffix(i + 1);
            run.push_back({i, next != i + 1});
            i = next;
        }
        for (std::size_t r = 0; r + 1 < run.size(); ++r) {   // everything before the name
            const std::string& w = m_toks[run[r].first].text;
            if (w == "const" || w == "readonly") flags |= KConst;
            else if (w == "static") flags |= KStatic;
            else if (!run[r].second && TypeKind(w)) kind = TypeKind(w);
        }
        return (unsigned char)(kind | flags);
    }
    // Catch the obvious mistakes while typing: `int lives = "three";`,
    // `string name = 5;`, `int half = 0.5;` (becomes 0).
    void CheckLiteralInit(unsigned char kind, const std::string& name, Expr* init, int line, int column) {
        const unsigned char t = kind & 0x0F;
        if (!t || !init) return;
        const bool isStr = dynamic_cast<StringExpr*>(init) || dynamic_cast<InterpStringExpr*>(init);
        auto* num = dynamic_cast<NumberExpr*>(init);
        const bool isBool = dynamic_cast<BoolExpr*>(init) != nullptr;
        if ((t == KInt || t == KFloat || t == KBool) && isStr)
            m_semantic.push_back({line, "'" + name + "' is " + KindName(kind) + ", so it can't hold text", 0, column});
        else if (t == KString && (num || isBool))
            m_semantic.push_back({line, "'" + name + "' is a string: write the value in quotes", 0, column});
        else if (t == KInt && num && num->v != (double)(long long)num->v)
            m_semantic.push_back({line, "'" + name + "' is an int, so " + Fmt(num->v) + " becomes " +
                                        Fmt((double)(long long)num->v) + ". Use float for decimals", 1, column});
    }
    static std::string Fmt(double v) { std::ostringstream o; o << v; return o.str(); }

    // A C#-style typed local/field: a run of >= 2 identifiers (type + modifiers
    // + name) ending in '=' or ';' (e.g. `int score = 0;`, `Vector3 v;`).
    bool IsTypedVarDeclAhead() const {
        int idents = 0;
        std::size_t i = ScanIdentRun(m_pos, idents);
        if (idents < 2) return false;
        const Tok t = m_toks[i].type;
        return t == Tok::Assign || t == Tok::Semicolon || t == Tok::Comma || IsAutoPropertyAt(i);
    }
    // `{ get; set; }` / `{ get; private set; }` right after a declared name.
    bool IsAutoPropertyAt(std::size_t i) const {
        if (m_toks[i].type != Tok::LBrace || m_toks[i + 1].type != Tok::Ident) return false;
        static const std::set<std::string> kAcc = {"get", "set", "init", "private", "protected", "internal", "public"};
        for (++i; m_toks[i].type != Tok::RBrace; ++i) {
            if (m_toks[i].type == Tok::Semicolon) continue;
            if (m_toks[i].type != Tok::Ident || !kAcc.count(m_toks[i].text)) return false;
        }
        return true;
    }
    void SkipAutoPropertyBody() {
        if (!IsAutoPropertyAt(m_pos)) return;
        while (!Check(Tok::RBrace)) ++m_pos;
        ++m_pos;
    }
    bool HasPublicModifier() const {
        int idents = 0;
        const std::size_t end = ScanIdentRun(m_pos, idents);
        for (std::size_t i = m_pos; i < end; ++i)
            if (m_toks[i].type == Tok::Ident && m_toks[i].text == "public") return true;
        return false;
    }
    // `class Item { fields, constructors, methods }`. No base type: this is data,
    // not the component. `static` and `const` aren't supported on these yet.
    void ParseUserClass() {
        while (Check(Tok::Ident) && Peek().text != "class" && Peek().text != "struct") ++m_pos;
        Expect(Tok::Ident, "'class'");
        const int line = Peek().line;
        const std::string name = Expect(Tok::Ident, "class name").text;
        m_pos = SkipTypeSuffix(m_pos);
        if (m_classes.count(name))
            throw ScriptError("there's already a class named '" + name + "'", line);
        Expect(Tok::LBrace, "'{'");
        UserClass cls;
        cls.name = name;
        auto addField = [&](std::string fname, unsigned char kind, bool isPublic, ExprPtr init) {
            for (auto& f : cls.fields)
                if (f.name == fname) throw ScriptError(name + " already has a field named '" + fname + "'", Peek().line);
            FieldDef fd;
            fd.name = std::move(fname);
            fd.kind = (unsigned char)(kind & 0x0F);
            fd.isPublic = isPublic;
            fd.init = std::move(init);
            cls.fields.push_back(std::move(fd));
        };
        while (!Check(Tok::RBrace) && !Check(Tok::End)) {
            SkipAttributes();
            if (Check(Tok::RBrace) || Check(Tok::End)) break;
            if (Match(Tok::Semicolon)) continue;
            if (IsClassAhead()) {
                if (ClassHasBase()) throw ScriptError("a class inside " + name + " can't have a base type", Peek().line);
                ParseUserClass();
                continue;
            }
            if (IsEnumAhead()) { SkipEnumDecl(); continue; }
            const bool isPublic = HasPublicModifier();
            const unsigned char flags = DeclKindAhead();
            if (flags & (KStatic | KConst))
                throw ScriptError("plain classes can't be static or const yet", Peek().line);
            if (Check(Tok::Function) || IsTypedFunctionAhead() || IsBareFunctionAhead()) {
                std::string mname;
                FunctionDecl decl;
                if (Check(Tok::Function)) decl = ParseFunction(mname);
                else if (IsTypedFunctionAhead()) decl = ParseTypedFunction(mname);
                else {
                    mname = Expect(Tok::Ident, "function name").text;
                    decl = ParseParamsAndBody();
                }
                MethodDef md;
                md.decl = std::move(decl);
                md.isPublic = isPublic;
                if (mname == name) {
                    const int arity = (int)md.decl.params.size();
                    if (cls.ctors.count(arity))
                        throw ScriptError(name + " already has a constructor that takes " + std::to_string(arity) +
                                          (arity == 1 ? " value" : " values"), Peek().line);
                    cls.ctors.emplace(arity, std::move(md));
                } else {
                    if (cls.methods.count(mname))
                        throw ScriptError(name + " already has a method named '" + mname + "'", Peek().line);
                    cls.methods.emplace(mname, std::move(md));
                }
                continue;
            }
            if (Match(Tok::Var) || IsTypedVarDeclAhead()) {
                const unsigned char kind = Check(Tok::Ident) ? flags : KAny;
                const int declLine = Peek().line;
                std::string fname = ConsumeIdentRun("field name");
                SkipAutoPropertyBody();
                ExprPtr init;
                int initCol = Peek().column;
                if (Match(Tok::Assign)) { initCol = Peek().column; init = ParseExpression(); }
                CheckLiteralInit(kind, fname, init.get(), declLine, initCol);
                addField(std::move(fname), kind, isPublic, std::move(init));
                while (Match(Tok::Comma)) {
                    std::string n2 = Expect(Tok::Ident, "field name").text;
                    ExprPtr i2;
                    int col2 = Peek().column;
                    if (Match(Tok::Assign)) { col2 = Peek().column; i2 = ParseExpression(); }
                    CheckLiteralInit(kind, n2, i2.get(), declLine, col2);
                    addField(std::move(n2), kind, isPublic, std::move(i2));
                }
                Match(Tok::Semicolon);
                continue;
            }
            throw ScriptError("this isn't a field or a method of " + name, Peek().line);
        }
        Expect(Tok::RBrace, "'}'");
        Match(Tok::Semicolon);
        m_classes.emplace(name, std::move(cls));
    }

    StmtPtr ParseStatement() {
        // yield return <expr>; / yield break;  (coroutines)
        if (Check(Tok::Ident) && Peek().text == "yield") {
            ++m_pos;
            if (Match(Tok::Break)) { Match(Tok::Semicolon); return std::make_unique<ReturnStmt>(nullptr); }
            Expect(Tok::Return, "'return' after 'yield'");
            auto st = std::make_unique<YieldStmt>();
            if (!Check(Tok::Semicolon) && !Check(Tok::RBrace)) st->e = ParseExpression();
            Match(Tok::Semicolon);
            return st;
        }
        // throw <expr>;  — raise an error caught by an enclosing try/catch.
        if (Check(Tok::Ident) && Peek().text == "throw") {
            ++m_pos;
            ExprPtr e = Check(Tok::Semicolon) ? nullptr : ParseExpression();
            Match(Tok::Semicolon);
            return std::make_unique<ThrowStmt>(std::move(e));
        }
        // try { ... } catch (e) { ... }  (catch clause optional)
        if (Check(Tok::Ident) && Peek().text == "try") {
            ++m_pos;
            auto st = std::make_unique<TryStmt>();
            st->body = ParseBlock();
            if (Check(Tok::Ident) && Peek().text == "catch") {
                ++m_pos;
                st->hasCatch = true;
                if (Match(Tok::LParen)) {            // optional `(e)` binding
                    Match(Tok::Var);
                    if (Check(Tok::Ident)) {
                        st->catchVar = m_toks[m_pos++].text;
                        if (Check(Tok::Ident)) st->catchVar = m_toks[m_pos++].text;  // typed
                    }
                    Expect(Tok::RParen, "')'");
                }
                st->catchB = ParseBlock();
            }
            return st;
        }
        // C#-style: foreach (var item in collection) { ... }
        if (Check(Tok::Ident) && Peek().text == "foreach") {
            ++m_pos;
            Expect(Tok::LParen, "'('");
            Match(Tok::Var);
            const bool kvType = Check(Tok::Ident) && Peek().text == "KeyValuePair";
            std::string var = ConsumeIdentRun("loop variable");   // `var x`, `int x`, `KeyValuePair<K, V> kv`
            Expect(Tok::In, "'in'");
            auto fe = std::make_unique<ForEachStmt>();
            fe->var = var;
            fe->iterable = ParseExpression();
            Expect(Tok::RParen, "')'");
            // Over a dictionary, items are {Key, Value} pairs when the loop says so
            // (KeyValuePair<K, V> kv, or kv.Key / kv.Value in the body); otherwise
            // the keys, as OkayScript always did.
            const std::size_t bodyStart = m_pos;
            fe->body = ParseBlockOrStmt();
            fe->pairs = kvType;
            for (std::size_t i = bodyStart; i < m_pos && !fe->pairs; ++i)
                if (m_toks[i].type == Tok::Ident && (m_toks[i].text.rfind(var + ".Key", 0) == 0 || m_toks[i].text.rfind(var + ".Value", 0) == 0))
                    fe->pairs = true;
            return fe;
        }
        // do { body } while (cond);
        if (Check(Tok::Ident) && Peek().text == "do") {
            ++m_pos;
            auto st = std::make_unique<DoWhileStmt>();
            st->body = ParseBlock();
            if (!(Check(Tok::While) || (Check(Tok::Ident) && Peek().text == "while")))
                throw ScriptError("parse error: expected 'while' after do-block", Peek().line, Peek().column);
            ++m_pos;   // 'while'
            Expect(Tok::LParen, "'('");
            st->cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            Match(Tok::Semicolon);
            return st;
        }
        // C#-style: switch (expr) { case A: ...; break; default: ...; }
        if (Check(Tok::Ident) && Peek().text == "switch") {
            ++m_pos;
            Expect(Tok::LParen, "'('");
            auto sw = std::make_unique<SwitchStmt>();
            sw->subject = ParseExpression();
            Expect(Tok::RParen, "')'");
            Expect(Tok::LBrace, "'{'");
            while (!Check(Tok::RBrace) && !Check(Tok::End)) {
                SwitchStmt::Case c;
                if (Check(Tok::Ident) && Peek().text == "case") {
                    ++m_pos; c.match = ParseExpression(); Expect(Tok::Colon, "':'");
                } else if (Check(Tok::Ident) && Peek().text == "default") {
                    ++m_pos; c.isDefault = true; Expect(Tok::Colon, "':'");
                } else {
                    throw ScriptError("parse error: expected 'case' or 'default' in switch", Peek().line, Peek().column);
                }
                while (!Check(Tok::RBrace) && !Check(Tok::End) &&
                       !(Check(Tok::Ident) && (Peek().text == "case" || Peek().text == "default")))
                    c.body.push_back(ParseStatement());
                sw->cases.push_back(std::move(c));
            }
            Expect(Tok::RBrace, "'}'");
            return sw;
        }
        const bool isVar = Match(Tok::Var);
        if (isVar || IsTypedVarDeclAhead()) {
            // `int x`, `const float y`, `public string name`: the type and const-ness
            // are kept (they're enforced at runtime); the name is the last identifier.
            const unsigned char kind = isVar ? KAny : DeclKindAhead();
            const int declLine = Peek().line;
            std::string name = ConsumeIdentRun("variable name");
            SkipAutoPropertyBody();   // public int Lives { get; set; } = 3;
            ExprPtr init;
            int initCol = Peek().column;
            if (Match(Tok::Assign)) { initCol = Peek().column; init = ParseExpression(); }
            CheckLiteralInit(kind, name, init.get(), declLine, initCol);
            if (!Check(Tok::Comma)) { Match(Tok::Semicolon); return std::make_unique<VarDeclStmt>(name, std::move(init), kind); }
            auto multi = std::make_unique<MultiStmt>();   // int a = 1, b = 2;
            multi->list.push_back(std::make_unique<VarDeclStmt>(name, std::move(init), kind));
            while (Match(Tok::Comma)) {
                std::string n2 = Expect(Tok::Ident, "variable name").text;
                ExprPtr i2;
                int col2 = Peek().column;
                if (Match(Tok::Assign)) { col2 = Peek().column; i2 = ParseExpression(); }
                CheckLiteralInit(kind, n2, i2.get(), declLine, col2);
                multi->list.push_back(std::make_unique<VarDeclStmt>(n2, std::move(i2), kind));
            }
            Match(Tok::Semicolon);
            return multi;
        }
        if (Match(Tok::If)) {
            Expect(Tok::LParen, "'('");
            auto cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            auto st = std::make_unique<IfStmt>();
            st->cond = std::move(cond);
            st->thenB = ParseBlockOrStmt();
            if (Match(Tok::Else)) st->elseB = Check(Tok::If) ? SingleStmt() : ParseBlockOrStmt();
            return st;
        }
        if (Match(Tok::While)) {
            Expect(Tok::LParen, "'('");
            auto cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            auto st = std::make_unique<WhileStmt>();
            st->cond = std::move(cond);
            st->body = ParseBlockOrStmt();
            return st;
        }
        if (Match(Tok::For)) {
            // foreach form: for x in <array> { ... }
            if (Check(Tok::Ident)) {
                std::size_t save = m_pos;
                std::string var = m_toks[m_pos++].text;
                if (Match(Tok::In)) {
                    auto fe = std::make_unique<ForEachStmt>();
                    fe->var = var;
                    fe->iterable = ParseExpression();
                    fe->body = ParseBlockOrStmt();
                    return fe;
                }
                m_pos = save; // not foreach; fall back to C-style for
            }
            Expect(Tok::LParen, "'('");
            auto st = std::make_unique<ForStmt>();
            // init: a var declaration (var or typed), an expression, or empty.
            if (!Match(Tok::Semicolon)) {
                if (Match(Tok::Var) || IsTypedVarDeclAhead()) {
                    std::string name = ConsumeIdentRun("variable name");
                    ExprPtr in;
                    if (Match(Tok::Assign)) in = ParseExpression();
                    st->init = std::make_unique<VarDeclStmt>(name, std::move(in));
                } else {
                    st->init = std::make_unique<ExprStmt>(ParseExpression());
                }
                Expect(Tok::Semicolon, "';'");
            }
            // condition (optional).
            if (!Check(Tok::Semicolon)) st->cond = ParseExpression();
            Expect(Tok::Semicolon, "';'");
            // step (optional).
            if (!Check(Tok::RParen)) st->step = ParseExpression();
            Expect(Tok::RParen, "')'");
            st->body = ParseBlockOrStmt();
            return st;
        }
        if (Match(Tok::Return)) {
            ExprPtr e;
            const int retLine = Prev().line, retCol = Prev().column;
            if (!Check(Tok::Semicolon) && !Check(Tok::RBrace)) e = ParseExpression();
            if (e && m_inVoid)
                m_semantic.push_back({retLine, m_fnName + "() is void, so it can't return a value. "
                                               "Give it a return type (int, float, bool or string) or just write `return;`", 0, retCol});
            Match(Tok::Semicolon);
            return std::make_unique<ReturnStmt>(std::move(e));
        }
        if (Match(Tok::Break))    { Match(Tok::Semicolon); return std::make_unique<BreakStmt>(); }
        if (Match(Tok::Continue)) { Match(Tok::Semicolon); return std::make_unique<ContinueStmt>(); }
        auto e = ParseExpression();
        Match(Tok::Semicolon);
        return std::make_unique<ExprStmt>(std::move(e));
    }

    std::vector<StmtPtr> SingleStmt() {
        std::vector<StmtPtr> v;
        v.push_back(ParseStatement());
        return v;
    }

    // Expression grammar with precedence climbing.
    ExprPtr ParseExpression() {
        if (m_nesting == 0) m_operators = 0;   // a new top-level expression
        return ParseAssignment();
    }

    // Ternary: cond ? a : b (lower precedence than assignment's RHS).
    ExprPtr ParseTernary() {
        ExprPtr c = ParseCoalesce();
        if (Match(Tok::Question)) {
            ExprPtr a = ParseAssignment();
            Expect(Tok::Colon, "':'");
            ExprPtr b = ParseAssignment();
            return std::make_unique<TernaryExpr>(std::move(c), std::move(a), std::move(b));
        }
        return c;
    }
    // `x =>`, `(x) =>`, `(int a, int b) =>`, `() =>` ?
    bool IsLambdaAhead() const {
        if (Check(Tok::Ident) && m_toks[m_pos + 1].type == Tok::Arrow) return true;
        if (!Check(Tok::LParen)) return false;
        int depth = 0;
        for (std::size_t i = m_pos; i < m_toks.size(); ++i) {
            if (m_toks[i].type == Tok::LParen) ++depth;
            else if (m_toks[i].type == Tok::RParen) {
                if (--depth == 0) return i + 1 < m_toks.size() && m_toks[i + 1].type == Tok::Arrow;
            }
        }
        return false;
    }
    ExprPtr ParseLambda() {
        static int s_nextLambda = 1;
        auto lam = std::make_unique<LambdaExpr>();
        lam->id = s_nextLambda++;
        if (Check(Tok::Ident)) lam->params.push_back(m_toks[m_pos++].text);
        else {
            Expect(Tok::LParen, "'('");
            bool sawDefault = false;
            while (!Check(Tok::RParen)) {
                std::string p = ConsumeIdentRun("parameter");
                ExprPtr def;
                if (Match(Tok::Assign)) { def = ParseExpression(); sawDefault = true; }
                else if (sawDefault)
                    throw ScriptError("parameter '" + p + "' needs a default too (ones with defaults come last)", Peek().line);
                lam->params.push_back(std::move(p));
                lam->defaults.push_back(std::move(def));
                if (!Match(Tok::Comma)) break;
            }
            Expect(Tok::RParen, "')'");
        }
        Expect(Tok::Arrow, "'=>'");
        const bool prevVoid = m_inVoid; m_inVoid = false;   // a lambda may return a value
        if (Check(Tok::LBrace)) lam->body = ParseBlock();
        else lam->body.push_back(std::make_unique<ReturnStmt>(ParseExpression()));
        m_inVoid = prevVoid;
        return lam;
    }
    ExprPtr ParseAssignment() {
        if (IsLambdaAhead()) return ParseLambda();
        ExprPtr left = ParseTernary();
        if (Match(Tok::QuestionQuestionEq)) {
            if (auto* var = dynamic_cast<VarExpr*>(left.get()))
                return std::make_unique<NullishAssignExpr>(var->n, ParseAssignment());
            throw ScriptError("invalid ??=", Peek().line);
        }
        if (Match(Tok::Assign)) {
            ExprPtr value = ParseAssignment();
            if (auto* var = dynamic_cast<VarExpr*>(left.get()))
                return std::make_unique<AssignExpr>(var->n, std::move(value));
            // Assigning a member of an expression: GetComponent<SpriteRenderer>().color = c.
            if (dynamic_cast<MemberExpr*>(left.get())) {
                auto* me = static_cast<MemberExpr*>(left.release());
                auto out = std::make_unique<MemberAssignExpr>(std::move(me->obj), me->name, std::move(value));
                delete me;
                return out;
            }
            // Assigning to an array element: arr[i] = value.
            if (dynamic_cast<IndexExpr*>(left.get())) {
                auto* ix = static_cast<IndexExpr*>(left.release());
                auto out = std::make_unique<IndexAssignExpr>(
                    std::move(ix->arr), std::move(ix->index), std::move(value));
                delete ix;
                return out;
            }
            throw ScriptError("invalid assignment target", Peek().line, Peek().column);
        }
        // Compound assignment: x += e  desugars to  x = x + e. Also works on an
        // element target: arr[i] += e / map["k"] += e.
        if (Check(Tok::PlusEq) || Check(Tok::MinusEq) ||
            Check(Tok::StarEq) || Check(Tok::SlashEq) || Check(Tok::PercentEq)) {
            Tok t = Peek().type; ++m_pos;
            Tok bin = t == Tok::PlusEq ? Tok::Plus : t == Tok::MinusEq ? Tok::Minus
                    : t == Tok::StarEq ? Tok::Star : t == Tok::SlashEq ? Tok::Slash : Tok::Percent;
            ExprPtr value = ParseAssignment();
            if (auto* var = dynamic_cast<VarExpr*>(left.get())) {
                std::string name = var->n;
                // += / -= on a name may subscribe a function. A number still adds.
                if (t == Tok::PlusEq || t == Tok::MinusEq)
                    return std::make_unique<EventOrCompoundExpr>(
                        name, std::move(value), t == Tok::MinusEq, bin);
                auto combined = std::make_unique<BinaryExpr>(
                    bin, std::make_unique<VarExpr>(name), std::move(value));
                return std::make_unique<AssignExpr>(name, std::move(combined));
            }
            if (dynamic_cast<IndexExpr*>(left.get())) {
                auto* ix = static_cast<IndexExpr*>(left.release());
                auto out = std::make_unique<IndexCompoundAssignExpr>(
                    std::move(ix->arr), std::move(ix->index), bin, std::move(value));
                delete ix;
                return out;
            }
            throw ScriptError("invalid assignment target", Peek().line, Peek().column);
        }
        // Postfix increment/decrement: x++  ->  x = x + 1  (also x--). Works on an
        // element target too: arr[i]++ / map["k"]--.
        if (Check(Tok::Inc) || Check(Tok::Dec)) {
            Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
            if (auto* var = dynamic_cast<VarExpr*>(left.get())) {
                std::string name = var->n;
                auto combined = std::make_unique<BinaryExpr>(
                    bin, std::make_unique<VarExpr>(name), std::make_unique<NumberExpr>(1.0));
                return std::make_unique<AssignExpr>(name, std::move(combined));
            }
            if (dynamic_cast<IndexExpr*>(left.get())) {
                auto* ix = static_cast<IndexExpr*>(left.release());
                auto out = std::make_unique<IndexCompoundAssignExpr>(
                    std::move(ix->arr), std::move(ix->index), bin, std::make_unique<NumberExpr>(1.0));
                delete ix;
                return out;
            }
            throw ScriptError("invalid increment target", Peek().line, Peek().column);
        }
        return left;
    }
    // a + b + c ... parses in a loop, but builds a tree as deep as it is long that
    // running and freeing it walk recursively: cap the operators in one expression.
    static constexpr int kMaxOperators = 1000;
    void CountOperator() {
        if (++m_operators > kMaxOperators)
            throw ScriptError("parse error: expression is too long (more than " + std::to_string(kMaxOperators) + " operators)", Peek().line, Peek().column);
    }
    // a ?? b ?? c groups to the right, below || and above ?:.
    ExprPtr ParseCoalesce() {
        ExprPtr l = ParseOr();
        if (Match(Tok::QuestionQuestion)) {
            CountOperator();
            l = std::make_unique<CoalesceExpr>(std::move(l), ParseCoalesce());
        }
        return l;
    }
    ExprPtr ParseOr() {
        auto l = ParseAnd();
        while (Match(Tok::Or)) { CountOperator(); l = std::make_unique<LogicalExpr>(Tok::Or, std::move(l), ParseAnd()); }
        return l;
    }
    ExprPtr ParseAnd() {
        auto l = ParseBitOr();
        while (Match(Tok::And)) { CountOperator(); l = std::make_unique<LogicalExpr>(Tok::And, std::move(l), ParseBitOr()); }
        return l;
    }
    ExprPtr ParseBitOr() {
        auto l = ParseBitXor();
        while (Match(Tok::BitOr)) { CountOperator(); l = std::make_unique<BinaryExpr>(Tok::BitOr, std::move(l), ParseBitXor()); }
        return l;
    }
    ExprPtr ParseBitXor() {
        auto l = ParseBitAnd();
        while (Match(Tok::BitXor)) { CountOperator(); l = std::make_unique<BinaryExpr>(Tok::BitXor, std::move(l), ParseBitAnd()); }
        return l;
    }
    ExprPtr ParseBitAnd() {
        auto l = ParseEquality();
        while (Match(Tok::BitAnd)) { CountOperator(); l = std::make_unique<BinaryExpr>(Tok::BitAnd, std::move(l), ParseEquality()); }
        return l;
    }
    ExprPtr ParseEquality() {
        auto l = ParseComparison();
        while (Check(Tok::Eq) || Check(Tok::Ne)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseComparison()); }
        return l;
    }
    ExprPtr ParseComparison() {
        auto l = ParseTerm();
        while (Check(Tok::Lt) || Check(Tok::Gt) || Check(Tok::Le) || Check(Tok::Ge)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseTerm()); }
        return l;
    }
    ExprPtr ParseTerm() {
        auto l = ParseFactor();
        while (Check(Tok::Plus) || Check(Tok::Minus)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseFactor()); }
        return l;
    }
    ExprPtr ParseFactor() {
        auto l = ParseUnary();
        while (Check(Tok::Star) || Check(Tok::Slash) || Check(Tok::Percent)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseUnary()); }
        return l;
    }
    ExprPtr ParseUnary() {
        // Every nested expression ('(', '-', '!', ...) comes through here: cap the
        // depth so thousands of them are a parse error, not a stack overflow.
        if (m_nesting >= 256) throw ScriptError("parse error: expression nested too deeply", Peek().line, Peek().column);
        struct NestGuard { int& n; explicit NestGuard(int& d) : n(d) { ++n; } ~NestGuard() { --n; } } guard(m_nesting);
        if (Check(Tok::Minus) || Check(Tok::Not)) { Tok op = Peek().type; ++m_pos; return std::make_unique<UnaryExpr>(op, ParseUnary()); }
        // Prefix ++x / --x  ->  x = x + 1 / x = x - 1.
        if (Check(Tok::Inc) || Check(Tok::Dec)) {
            Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
            ExprPtr target = ParseUnary();
            auto* var = dynamic_cast<VarExpr*>(target.get());
            if (!var) throw ScriptError("invalid increment target", Peek().line, Peek().column);
            std::string name = var->n;
            auto combined = std::make_unique<BinaryExpr>(
                bin, std::make_unique<VarExpr>(name), std::make_unique<NumberExpr>(1.0));
            return std::make_unique<AssignExpr>(name, std::move(combined));
        }
        return ParsePostfix();
    }
    // Postfix indexing: base[i][j]...
    // One `.name` or `.name(args)` step. `safe` skips the step when the receiver is null.
    ExprPtr ParseMemberStep(ExprPtr e, bool safe) {
        std::string path = Expect(Tok::Ident, "member name").text;
        std::vector<std::string> parts;
        for (std::size_t a = 0, b; a <= path.size(); a = b + 1) {
            b = path.find('.', a); if (b == std::string::npos) b = path.size();
            parts.push_back(path.substr(a, b - a));
        }
        for (std::size_t k = 0; k + 1 < parts.size(); ++k)
            e = std::make_unique<MemberExpr>(std::move(e), parts[k], safe);
        if (Match(Tok::LParen)) {
            auto call = std::make_unique<MethodCallExpr>(std::move(e), parts.back(), safe);
            if (!Check(Tok::RParen)) {
                do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
            }
            Expect(Tok::RParen, "')'");
            return call;
        }
        return std::make_unique<MemberExpr>(std::move(e), parts.back(), safe);
    }
    ExprPtr ParsePostfix() {
        ExprPtr e = ParsePrimary();
        // After `?.`, the rest of this chain is skipped when a step is null.
        bool safe = false;
        for (;;) {
            if (Match(Tok::LBracket)) {
                ExprPtr idx = ParseExpression();
                Expect(Tok::RBracket, "']'");
                e = std::make_unique<IndexExpr>(std::move(e), std::move(idx), safe);
            } else if (Match(Tok::QuestionDot)) {
                safe = true;
                e = ParseMemberStep(std::move(e), true);
            } else if (Match(Tok::Dot)) {
                e = ParseMemberStep(std::move(e), safe);
            } else break;
        }
        return e;
    }
    // `{ a, b, c }` — a C# collection / array initializer (nested for dictionaries).
    ExprPtr ParseBraceList() {
        Expect(Tok::LBrace, "'{'");
        auto arr = std::make_unique<ArrayExpr>();
        while (!Check(Tok::RBrace) && !Check(Tok::End)) {
            arr->elems.push_back(ParseExpression());
            if (!Match(Tok::Comma)) break;
        }
        Expect(Tok::RBrace, "'}'");
        return arr;
    }
    // After `new`: arrays, lists, dictionaries, or a constructor call (new Vector3(...)).
    ExprPtr ParseNew() {
        if (Check(Tok::LBracket)) {                        // new[] { 1, 2, 3 }
            m_pos = SkipTypeSuffix(m_pos);
            return Check(Tok::LBrace) ? ParseBraceList() : std::make_unique<ArrayExpr>();
        }
        const std::size_t typeTok = m_pos;
        std::string type = Expect(Tok::Ident, "type after 'new'").text;
        const auto dotp = type.rfind('.');
        const std::string base = dotp == std::string::npos ? type : type.substr(dotp + 1);
        if (Check(Tok::Lt)) m_pos = SkipTypeSuffix(m_pos);   // generic arguments
        if (Match(Tok::LBracket)) {                         // new int[n] / new int[] { ... }
            ExprPtr size;
            if (!Check(Tok::RBracket)) size = ParseExpression();
            while (Match(Tok::Comma)) ParseExpression();
            Expect(Tok::RBracket, "']'");
            if (Check(Tok::LBrace)) return ParseBraceList();
            auto call = std::make_unique<CallExpr>("__new_array");
            if (size) call->args.push_back(std::move(size));
            return call;
        }
        static const std::set<std::string> kLists = {"List", "HashSet", "Queue", "Stack", "LinkedList", "ArrayList", "SortedSet"};
        static const std::set<std::string> kMaps  = {"Dictionary", "SortedDictionary", "Hashtable", "SortedList"};
        if (kLists.count(base) || kMaps.count(base)) {
            ExprPtr from;                                    // new List<int>(other) copies
            if (Match(Tok::LParen)) {
                if (!Check(Tok::RParen)) from = ParseExpression();
                while (Match(Tok::Comma)) ParseExpression();
                Expect(Tok::RParen, "')'");
            }
            ExprPtr init = Check(Tok::LBrace) ? ParseBraceList() : nullptr;
            auto call = std::make_unique<CallExpr>(kMaps.count(base) ? "__new_map" : "__new_list");
            if (init) call->args.push_back(std::move(init));
            else if (from) call->args.push_back(std::move(from));
            return call;
        }
        if (m_userClasses.count(base)) {                 // new Item("Potion") / new Item()
            auto made = std::make_unique<NewExpr>(base);
            if (Match(Tok::LParen)) {
                if (!Check(Tok::RParen)) {
                    do { made->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
                }
                Expect(Tok::RParen, "')'");
            }
            return made;
        }
        if (m_pos == typeTok + 1) { m_pos = typeTok; return ParsePrimary(); }   // new Vector3(1, 2, 3)
        // Some other generic type: build a call named after it.
        auto call = std::make_unique<CallExpr>(type);
        if (Match(Tok::LParen)) {
            if (!Check(Tok::RParen)) { do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma)); }
            Expect(Tok::RParen, "')'");
        }
        return call;
    }
    // Split an interpolated-string template into literal + {expression} parts,
    // compiling each embedded expression with a sub-parser. `{{`/`}}` are literals.
    ExprPtr BuildInterp(const std::string& tpl) {
        auto node = std::make_unique<InterpStringExpr>();
        std::string lit;
        for (std::size_t i = 0; i < tpl.size(); ++i) {
            char c = tpl[i];
            if (c == '{' && i + 1 < tpl.size() && tpl[i + 1] == '{') { lit += '{'; ++i; continue; }
            if (c == '}' && i + 1 < tpl.size() && tpl[i + 1] == '}') { lit += '}'; ++i; continue; }
            if (c == '{') {
                node->AddLiteral(lit); lit.clear();
                std::string expr; int depth = 1; ++i;
                for (; i < tpl.size(); ++i) {
                    if (tpl[i] == '{') ++depth;
                    else if (tpl[i] == '}') { if (--depth == 0) break; }
                    expr += tpl[i];
                }
                Lexer lx(expr);
                Parser p(lx.Scan());
                node->AddExpr(p.ParseExpression());
            } else {
                lit += c;
            }
        }
        node->AddLiteral(lit);
        return node;
    }

    // `Heal(5)` or `Heal(bonus: 4, hp: 1)`. A name is a bare identifier followed by ':'.
    void ParseCallArguments(std::vector<ExprPtr>& args, std::vector<std::string>& names) {
        if (Check(Tok::RParen)) return;
        do {
            std::string nm;
            if (Check(Tok::Ident) && m_pos + 1 < m_toks.size() && m_toks[m_pos + 1].type == Tok::Colon) {
                nm = m_toks[m_pos].text;
                m_pos += 2;
            }
            names.push_back(std::move(nm));
            args.push_back(ParseExpression());
        } while (Match(Tok::Comma));
    }

    ExprPtr ParsePrimary() {
        if (Match(Tok::Number)) return std::make_unique<NumberExpr>(Prev().number);
        if (Match(Tok::String)) return std::make_unique<StringExpr>(Prev().text);
        if (Match(Tok::InterpString)) return BuildInterp(Prev().text);
        if (Match(Tok::True))   return std::make_unique<BoolExpr>(true);
        if (Match(Tok::False))  return std::make_unique<BoolExpr>(false);
        // C# cast: (int)x, (float)a / b, (string)v.
        if (Check(Tok::LParen) && m_toks[m_pos + 1].type == Tok::Ident && m_toks[m_pos + 2].type == Tok::RParen) {
            static const std::set<std::string> kCastTypes = {
                "int", "float", "double", "long", "short", "byte", "uint", "ulong", "ushort", "sbyte",
                "decimal", "string", "bool", "char"};
            const std::string& ty = m_toks[m_pos + 1].text;
            const Tok after = m_toks[m_pos + 3].type;
            const bool operand = after == Tok::Ident || after == Tok::Number || after == Tok::LParen ||
                                 after == Tok::String || after == Tok::Minus || after == Tok::Not;
            if (kCastTypes.count(ty) && operand) {
                m_pos += 3;
                return std::make_unique<CastExpr>(ty, ParseUnary());
            }
            if (m_enums.count(ty) && operand) { m_pos += 3; return ParseUnary(); }   // (State)2
        }
        if (Match(Tok::LParen)) { auto e = ParseExpression(); Expect(Tok::RParen, "')'"); return e; }
        if (Check(Tok::LBrace)) return ParseBraceList();   // int[] a = { 1, 2, 3 };
        if (Match(Tok::LBracket)) { // array literal [a, b, c]
            auto arr = std::make_unique<ArrayExpr>();
            if (!Check(Tok::RBracket)) {
                do { arr->elems.push_back(ParseExpression()); } while (Match(Tok::Comma));
            }
            Expect(Tok::RBracket, "']'");
            return arr;
        }
        if (Check(Tok::Ident)) {
            std::string name = m_toks[m_pos++].text;
            // C#-style `new Vector3(...)`, `new List<int>()`, `new int[] { 1, 2 }`.
            if (name == "new") return ParseNew();
            // Generic call `GetComponent<Type>(args)` — pass the type name as the
            // first argument so builtins can dispatch on it. (Bounds-checked: the
            // token stream has a single End sentinel, so peeking +3 can run off.)
            if (Check(Tok::Lt) && m_pos + 3 < m_toks.size() &&
                m_toks[m_pos + 1].type == Tok::Ident &&
                m_toks[m_pos + 2].type == Tok::Gt && m_toks[m_pos + 3].type == Tok::LParen) {
                std::string typeName = m_toks[m_pos + 1].text;
                m_pos += 4;   // consume  < Type > (
                auto call = std::make_unique<CallExpr>(name);
                call->args.push_back(std::make_unique<StringExpr>(typeName));
                if (!Check(Tok::RParen)) {
                    do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
                }
                Expect(Tok::RParen, "')'");
                return call;
            }
            if (Match(Tok::LParen)) {
                auto call = std::make_unique<CallExpr>(name);
                ParseCallArguments(call->args, call->argNames);
                Expect(Tok::RParen, "')'");
                // StartCoroutine(Blink(0.2f)) names the routine instead of calling it now.
                // Named arguments inside it (`Blink(delay: 0.2)`) travel with the values.
                if ((name == "StartCoroutine" || name == "StopCoroutine") && call->args.size() == 1)
                    if (auto* inner = dynamic_cast<CallExpr*>(call->args[0].get())) {
                        auto co = std::make_unique<CallExpr>(name);
                        co->args.push_back(std::make_unique<StringExpr>(inner->name));
                        co->argNames.push_back("");
                        for (std::size_t i = 0; i < inner->args.size(); ++i) {
                            co->args.push_back(std::move(inner->args[i]));
                            co->argNames.push_back(i < inner->argNames.size() ? inner->argNames[i] : "");
                        }
                        return co;
                    }
                return call;
            }
            if (auto it = m_enumValues.find(name); it != m_enumValues.end())
                return std::make_unique<NumberExpr>(it->second);   // State.Run -> 1
            return std::make_unique<VarExpr>(name);
        }
        throw ScriptError("parse error: unexpected token", Peek().line, Peek().column);
    }

    std::vector<Token> m_toks;
    std::size_t m_pos = 0;
    int m_nesting = 0;                                      // ParseUnary recursion depth
    int m_operators = 0;                                    // binary operators in this expression
    std::set<std::string> m_enums;                          // enum type names
    std::set<std::string> m_userClasses;                    // plain classes (no base), from the prepass
    std::unordered_map<std::string, UserClass> m_classes;
    std::vector<ScriptDiagnostic> m_semantic;               // type mistakes found while parsing
    bool m_inVoid = false;                                  // parsing the body of a `void` function
    std::string m_fnName;
    std::unordered_map<std::string, double> m_enumValues;   // "State.Run" -> 1
};

} // namespace

void OkayScriptVM::Impl::RegisterBuiltins() {
        InstallSharedApis();
        BuiltinCtx c(rt);
        RegisterDebugBuiltins(c);
        RegisterScriptUiBuiltins(c);
        RegisterMotionHelperBuiltins(c);
        RegisterInputBuiltins(c);
        RegisterScoreSpawnBuiltins(c);
        RegisterObjectPoolBuiltins(c);
        RegisterNamedObjectBuiltins(c);
        RegisterNpcBuiltins(c);
        RegisterSpawnerBuiltins(c);
        RegisterBuildModeBuiltins(c);
        RegisterMovementHelperBuiltins(c);
        RegisterCameraControlBuiltins(c);
        RegisterCameraRigBuiltins(c);
        RegisterUiByNameBuiltins(c);
        RegisterUiRuntimeBuiltins(c);
        RegisterUiControlBuiltins(c);
        RegisterBody2DBuiltins(c);
        RegisterSpriteEffectBuiltins(c);
        RegisterAudioSaveBuiltins(c);
        RegisterDisplayBuiltins(c);
        RegisterStoryBuiltins(c);
        RegisterNavigationBuiltins(c);
        RegisterGridPathBuiltins(c);
        RegisterNavmeshQueryBuiltins(c);
        RegisterBehaviorTreeBuiltins(c);
        RegisterSplineBuiltins(c);
        RegisterMusicBuiltins(c);
        RegisterRaycast2DBuiltins(c);
        RegisterSceneLoadBuiltins(c);
        RegisterCharacterAnimBuiltins(c);
        RegisterAnimStateMachineBuiltins(c);
        RegisterBlendShapeBuiltins(c);
        RegisterIkBuiltins(c);
        RegisterInventoryBuiltins(c);
        RegisterNetworkBuiltins(c);
        RegisterMatchmakingQueueBuiltins(c);
        RegisterMatchResultBuiltins(c);
        RegisterServerBrowserBuiltins(c);
        RegisterSteamBuiltins(c);
        RegisterFriendsBuiltins(c);
        RegisterLobbyBuiltins(c);
        RegisterPlayFabBuiltins(c);
        RegisterAccountBuiltins(c);
        RegisterEasingBuiltins(c);
        RegisterBitsJsonStringBuiltins(c);
        RegisterTransformHelperBuiltins(c);
        RegisterPhysicsQueryBuiltins(c);
        RegisterMaterialEventBuiltins(c);
        RegisterMathUtilityBuiltins(c);
        RegisterTweenBuiltins(c);
        RegisterFriendlyAliasBuiltins(c);
        RegisterCsApiBuiltins(c);
        RegisterVehicleBuiltins(c);
        RegisterPlainAliasBuiltins(c);
        RegisterCsCompatBuiltins(c);
        RegisterCoroutineBuiltins(c);
        RegisterObjectLifecycleBuiltins(c);
        RegisterComponentAccessBuiltins(c);
        CheckSharedApis();
}

OkayScriptVM::OkayScriptVM() : m_impl(std::make_unique<Impl>()) {
    m_impl->RegisterBuiltins();
}
OkayScriptVM::~OkayScriptVM() = default;

bool OkayScriptVM::Load(const std::string& source, std::string* error) {
    try {
        Lexer lex(source);
        Parser parser(lex.Scan());
        m_impl->rt.StopCoroutines("");
        m_impl->rt.pendingDestroy.clear();
        m_impl->rt.functions.clear();
        m_impl->rt.subs.clear();
        m_impl->rt.classes.clear();
        m_impl->rt.self = Value{};
        m_impl->rt.selfClass.clear();
        m_impl->rt.statics = SharedStatics(source);
        m_impl->topLevel = parser.ParseProgram(m_impl->rt.functions);
        m_impl->rt.classes = parser.TakeClasses();
        // Bare mode: no functions defined -> the top-level statements ARE the update
        // loop (run every frame), so a whole script can be a single line. Otherwise run
        // top-level once now for globals/setup.
        m_impl->bareMode = m_impl->rt.functions.empty() && !m_impl->topLevel.empty();
        // Always run the top-level once now so globals/setup exist right after
        // Load (the contract every caller relies on). In bare mode the top-level
        // is also the per-frame loop, so this first run is the first tick — the
        // first Update skips re-running it to keep the overall tick count intact.
        for (auto& s : m_impl->topLevel) s->Exec(m_impl->rt);
        m_impl->bareSkipFirstUpdate = m_impl->bareMode;
        m_impl->loaded = true;
        return true;
    } catch (const ReturnSignal&) {
        return true; // a bare top-level return is harmless
    } catch (const BreakSignal&) {
        m_impl->loaded = true; return true; // stray break/continue: harmless
    } catch (const ContinueSignal&) {
        m_impl->loaded = true; return true;
    } catch (const ScriptError& e) {
        // Prefix the source line when known, so editors can show a diagnostic.
        std::string msg = e.what();
        if (e.line > 0) {
            std::string where = "line " + std::to_string(e.line);
            if (e.column > 0) where += ", column " + std::to_string(e.column);
            msg = where + ": " + msg;
        }
        if (error) *error = msg;
        Log::Error("OkayScript: ", msg);
        m_impl->loaded = false;
        return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        Log::Error("OkayScript: ", e.what());
        m_impl->loaded = false;
        return false;
    }
}

void OkayScriptVM::AddBuiltin(const std::string& name, Builtin fn) {
    m_impl->rt.builtins[name] = std::move(fn);
}

vs::VsValue OkayScriptVM::CallBuiltin(const std::string& name, std::vector<vs::VsValue> args, std::string* error) {
    auto it = m_impl->rt.builtins.find(name);
    if (it == m_impl->rt.builtins.end()) {
        if (error) *error = "no text-script builtin named \"" + name + "\"";
        return {};
    }
    try {
        return it->second(args);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return {};
    }
}

std::vector<std::string> OkayScriptVM::BuiltinNames() const {
    std::vector<std::string> out;
    out.reserve(m_impl->rt.builtins.size());
    for (const auto& kv : m_impl->rt.builtins) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

bool OkayScriptVM::Validate(const std::string& source, std::string* error) {
    // Lex + parse ONLY — never Exec — so live editor diagnostics have no side
    // effects (no logging, no globals mutated, no scene calls). Catches syntax and
    // parse errors; runtime/semantic errors still surface on a real Load/Run.
    try {
        Lexer lex(source);
        Parser parser(lex.Scan());
        std::unordered_map<std::string, FunctionDecl> throwaway;
        parser.ParseProgram(throwaway);
        return true;
    } catch (const ScriptError& e) {
        if (error) {
            std::string msg = e.what();
            if (e.line > 0) {
                std::string where = "line " + std::to_string(e.line);
                if (e.column > 0) where += ", column " + std::to_string(e.column);
                msg = where + ": " + msg;
            }
            *error = msg;
        }
        return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::vector<ScriptDiagnostic> OkayScriptVM::ValidateAll(const std::string& source) {
    // Like Validate, but recovers after each parse error to report ALL of them for
    // the editor's Problems panel. Lexer errors (unterminated string, bad char) can't
    // be recovered from, so those surface as a single diagnostic.
    std::vector<ScriptDiagnostic> diags;
    try {
        Lexer lex(source);
        Parser parser(lex.Scan());
        diags = parser.ParseProgramRecovering();
    } catch (const ScriptError& e) {
        diags.push_back({ e.line, e.what(), 0, e.column });
    } catch (const std::exception& e) {
        diags.push_back({ 0, e.what() });
    }
    return diags;
}

void OkayScriptVM::Bind(ScriptHost* host) { m_impl->rt.host = host; }

namespace {
// Call the first of `names` that the script defines (with the given args).
// Lets a script use PascalCase (Start/Update) or the classic
// lowercase (start/update) names interchangeably.
void CallFirst(Runtime& rt, std::initializer_list<const char*> names,
               std::vector<Value>& args, const char* label) {
    for (const char* n : names) {
        if (rt.functions.count(n)) {
            try { rt.Call(n, args); }
            catch (const std::exception& e) { Log::Error("OkayScript ", label, "(): ", e.what()); }
            return;
        }
    }
}
} // namespace

void OkayScriptVM::CallStart() {
    if (!m_impl->loaded) return;
    std::vector<Value> none;
    CallFirst(m_impl->rt, {"Awake", "awake"}, none, "Awake");   // Awake, then OnEnable, then Start
    CallFirst(m_impl->rt, {"OnEnable", "on_enable"}, none, "OnEnable");
    CallFirst(m_impl->rt, {"Start", "start"}, none, "Start");
}

void OkayScriptVM::CallUpdate(float deltaTime) {
    if (!m_impl->loaded) return;
    if (m_impl->rt.host) m_impl->rt.host->deltaTime = deltaTime;
    std::vector<Value> args{Value{deltaTime}};
    // Bare mode: re-run the top-level statements every frame (they ARE the loop).
    // Load already ran them once, so skip that first re-run to avoid double-ticking.
    if (m_impl->bareMode) {
        if (m_impl->bareSkipFirstUpdate) {
            m_impl->bareSkipFirstUpdate = false;
        } else {
            try { for (auto& s : m_impl->topLevel) s->Exec(m_impl->rt); }
            catch (const ReturnSignal&) {}     // a stray top-level return/break/continue
            catch (const BreakSignal&) {}      // ends this frame's run, like it does in Load
            catch (const ContinueSignal&) {}
            catch (const std::exception& e) { Log::Error("OkayScript (bare): ", e.what()); }
        }
    }
    CallFirst(m_impl->rt, {"Update", "update"}, args, "Update");
    CallFirst(m_impl->rt, {"LateUpdate", "late_update"}, args, "LateUpdate");
    // Scheduled after()/every() callbacks tick even without an update().
    if (!m_impl->rt.timers.empty()) {
        try { m_impl->rt.TickTimers(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript timer: ", e.what()); }
    }
    Runtime& rt = m_impl->rt;
    if (!rt.coroutines.empty()) {
        try { rt.TickCoroutines(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript coroutine: ", e.what()); }
    }
    ++rt.frame;
    // Destroy(obj, seconds) countdowns.
    if (!rt.pendingDestroy.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene()) {
        Scene* sc = rt.host->gameObject->scene();
        std::vector<Value> due;
        for (auto& pd : rt.pendingDestroy) { pd.first -= deltaTime; if (pd.first <= 0.0f) due.push_back(pd.second); }
        rt.pendingDestroy.erase(std::remove_if(rt.pendingDestroy.begin(), rt.pendingDestroy.end(),
                                               [](const auto& pd) { return pd.first <= 0.0f; }), rt.pendingDestroy.end());
        GameObject* me = rt.host->gameObject;
        for (const auto& n : due) {
            GameObject* g = (!n.IsObject() && n.AsString().empty()) ? me : ResolveObject(sc, n);
            if (g) sc->Destroy(g);
            if (g == me) break;   // this script is going away with its object
        }
    }
}

void OkayScriptVM::CallEvent(const std::string& function) {
    CallEventWith(function, vs::VsValue{});
}

namespace {
const std::unordered_map<std::string, std::vector<std::string>>& EventAliases() {
    static const std::unordered_map<std::string, std::vector<std::string>> kAliases = {
        // Inventory events (C#-style handler names work too).
        {"on_item_added",       {"OnItemAdded"}},
        {"on_item_removed",     {"OnItemRemoved"}},
        {"on_inventory_full",   {"OnInventoryFull"}},
        {"on_item_used",        {"OnItemUsed"}},
        {"on_item_dropped",     {"OnItemDropped"}},
        {"on_item_picked_up",   {"OnItemPickedUp"}},
        {"on_equip",            {"OnEquip"}},
        {"on_unequip",          {"OnUnequip"}},
        {"on_craft",            {"OnCraft"}},
        {"on_craft_failed",     {"OnCraftFailed"}},
        {"on_chest_opened",     {"OnChestOpened"}},
        {"on_chest_closed",     {"OnChestClosed"}},
        // Physics — distinct enter/stay/exit. The legacy on_trigger/on_collision
        // are accepted as enter handlers so older scripts keep working.
        {"on_trigger_enter",   {"OnTriggerEnter", "OnTriggerEnter2D", "on_trigger"}},
        {"on_trigger_stay",    {"OnTriggerStay",  "OnTriggerStay2D"}},
        {"on_trigger_exit",    {"OnTriggerExit",  "OnTriggerExit2D"}},
        {"on_collision_enter", {"OnCollisionEnter", "OnCollisionEnter2D", "on_collision"}},
        {"on_collision_stay",  {"OnCollisionStay", "OnCollisionStay2D"}},
        {"on_collision_exit",  {"OnCollisionExit", "OnCollisionExit2D"}},
        {"on_collision", {"OnCollisionEnter", "OnCollisionEnter2D"}},
        {"on_trigger",   {"OnTriggerEnter",   "OnTriggerEnter2D"}},
        // Pointer / mouse.
        {"on_mouse_enter", {"OnMouseEnter", "OnPointerEnter"}},
        {"on_mouse_exit",  {"OnMouseExit",  "OnPointerExit"}},
        {"on_mouse_over",  {"OnMouseOver"}},
        {"on_mouse_down",  {"OnMouseDown"}},
        {"on_mouse_up",    {"OnMouseUp", "OnMouseUpAsButton"}},
        {"on_click",     {"OnClick", "OnMouseClick", "OnMouseUpAsButton", "OnPointerClick"}},
        {"on_modal_close", {"OnModalClose"}},   // grok/ui-toast
        {"on_change",    {"OnValueChanged"}},
        {"on_submit",    {"OnSubmit"}},
        {"on_tab_close", {"OnTabClose"}},
        {"on_toggle",    {"OnToggle", "OnValueChanged"}},
        {"on_drop",      {"OnDrop"}},
        {"on_receive",   {"OnReceive"}},
        {"on_drag",      {"OnDrag"}},
        {"on_drag_start",{"OnBeginDrag"}},
        // Lifecycle the ScriptComponent drives.
        {"on_fixed_update", {"FixedUpdate", "fixed_update"}},
        {"on_enable",       {"OnEnable"}},
        {"on_disable",      {"OnDisable"}},
        {"on_destroy",      {"OnDestroy"}},
        // Build mode / logic links (BuildMode, LogicTarget).
        {"on_object_placed",  {"OnObjectPlaced"}},
        {"on_object_removed", {"OnObjectRemoved"}},
        {"on_logic_signal",   {"OnLogicSignal"}},
        // Anti-cheat (AntiCheatConfig raises these on the host).
        {"on_cheat_detected", {"OnCheatDetected"}},
        {"on_validate_rpc",   {"OnValidateRpc"}},
        {"on_validate_hit",   {"OnValidateHit"}},
        // Cutscenes (SequencePlayer).
        {"on_sequence_event",    {"OnSequenceEvent"}},
        {"on_sequence_started",  {"OnSequenceStarted"}},
        {"on_sequence_finished", {"OnSequenceFinished"}},
        // Localization (the player picked another language).
        {"on_language_changed",  {"OnLanguageChanged"}},
        // Splines (SplineFollower: "end" or "start").
        {"on_spline_end",        {"OnSplineEnd"}},
    };
    return kAliases;
}

// The handler a script defines for an engine event name: the requested name
// wins; otherwise an alias, so scripts can name handlers in PascalCase
// (OnCollisionEnter, OnClick, ...). "" = none.
template <class Functions>
std::string ResolveEventHandler(const Functions& functions, const std::string& function) {
    const auto& kAliases = EventAliases();
    if (functions.count(function)) return function;
    auto it = kAliases.find(function);
    if (it != kAliases.end())
        for (const auto& alt : it->second)
            if (functions.count(alt)) return alt;
    return {};
}

// "OnClick" and "on_click" are the same event. A name that isn't in the table
// (OnHurt, OnEvent_Hit) stays as written.
std::string CanonEvent(const std::string& name) {
    const auto& kAliases = EventAliases();
    if (kAliases.count(name)) return name;
    for (const auto& kv : kAliases)
        for (const auto& alt : kv.second)
            if (alt == name) return kv.first;
    return name;
}

bool SameListener(const Value& a, const Value& b) {
    auto ma = a.AsMap(), mb = b.AsMap();
    if (!ma || !mb) return false;
    if (ma->count("__lambda") && mb->count("__lambda"))
        return (*ma)["__lambda"].AsFloat() == (*mb)["__lambda"].AsFloat();
    if (ma->count("__fn") && mb->count("__fn"))
        return (*ma)["__fn"].AsString() == (*mb)["__fn"].AsString();
    return false;
}
} // namespace

void Runtime::AddSub(const std::string& event, const Value& fn) {
    subs.push_back(Sub{event, fn});
}

void Runtime::RemoveSub(const std::string& event, const Value& fn) {
    const std::string canon = CanonEvent(event);
    for (auto it = subs.begin(); it != subs.end(); ++it) {
        if (CanonEvent(it->name) == canon && SameListener(it->fn, fn)) {
            subs.erase(it);
            return;
        }
    }
}

bool Runtime::HasSub(const std::string& event) const {
    const std::string canon = CanonEvent(event);
    for (const auto& s : subs)
        if (CanonEvent(s.name) == canon) return true;
    return false;
}

void Runtime::FireSubs(const std::string& event, std::vector<Value>& args) {
    const std::string canon = CanonEvent(event);
    const std::vector<Sub> snap = subs;
    for (const Sub& s : snap) {
        if (CanonEvent(s.name) != canon) continue;
        auto m = s.fn.AsMap();
        if (!m) continue;
        if (m->count("__fn")) {
            const std::string fn = (*m)["__fn"].AsString();
            if (functions.count(fn)) Call(fn, args);
            continue;
        }
        if (IsLambda(s.fn)) CallLambda(s.fn, args);
    }
}

void OkayScriptVM::CallEventWith(const std::string& function, const vs::VsValue& arg) {
    CallEventArgs(function, std::vector<vs::VsValue>{arg});
}

bool OkayScriptVM::HasEvent(const std::string& function) const {
    return m_impl->loaded &&
           (!ResolveEventHandler(m_impl->rt.functions, function).empty() || m_impl->rt.HasSub(function));
}

vs::VsValue OkayScriptVM::CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& given,
                                        std::string* error) {
    if (!m_impl->loaded) return {};
    std::vector<Value> all(given.begin(), given.end());
    const std::string fn = ResolveEventHandler(m_impl->rt.functions, function);
    vs::VsValue ret;
    if (!fn.empty()) {
        // Up to as many arguments as the handler declares (OnTriggerEnter(Collider
        // other) takes one; a handler with none gets none). Call gives any it
        // declares beyond those its usual default for the parameter's type.
        const std::size_t want = std::min(m_impl->rt.functions[fn].params.size(), all.size());
        std::vector<Value> args(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(want));
        try { ret = m_impl->rt.Call(fn, args); }
        catch (const std::exception& e) {
            if (error) *error = e.what();
            else Log::Error("OkayScript ", fn.c_str(), "(): ", e.what());
        }
    }
    // Listeners added with += run as well, including when no function is defined.
    try { m_impl->rt.FireSubs(function, all); }
    catch (const std::exception& e) {
        if (error && error->empty()) *error = e.what();
        else if (!error) Log::Error("OkayScript ", function.c_str(), " listener: ", e.what());
    }
    return ret;
}

vs::VsValue OkayScriptVM::GetGlobal(const std::string& name) const {
    auto& g = m_impl->rt.Global().vars;
    auto it = g.find(name);
    return it != g.end() ? it->second : vs::VsValue{};
}

std::vector<std::string> OkayScriptVM::GlobalNames() const {
    std::vector<std::string> out;
    for (const auto& kv : m_impl->rt.Global().vars) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

void OkayScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    m_impl->rt.SetGlobalChecked(name, v);
}

} // namespace okay
