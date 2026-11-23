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

void OkayScriptVM::Impl::RegisterTweenBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        // ---- Tweening (DOTween-style) ----------------------------------
        // Every tween_* call starts a tween on this object and returns its id, so it
        // can be configured and controlled afterwards (tween_loops(id, -1, "yoyo"),
        // tween_kill(id) ...). Tweens die with their object. Ease names are forgiving:
        // "out_quad", "OutQuad", "in_out_sine"...
        auto easeFromArg = [](std::vector<Value>& a, std::size_t i) -> Ease {
            if (i >= a.size() || !a[i].IsString()) return Ease::Linear;
            return Easing::FromName(a[i].AsString(), Ease::Linear);
        };
        // Apply the ease named by arg `i` to a tween: a preset name as above, or a curve
        // as text ("cubic-bezier(0.4,0,0.2,1)", "steps(4)"). Unknown text is linear.
        auto applyEase = [easeFromArg](Tweener& t, std::vector<Value>& a, std::size_t i) {
            EaseCurve c;
            if (i < a.size() && a[i].IsString() && EaseCurve::Parse(a[i].AsString(), c) && c.kind != EaseCurve::Kind::Preset)
                t.SetEaseSpec(a[i].AsString());
            else t.SetEase(easeFromArg(a, i));
        };
        auto tweens = [this]() -> TweenManager* {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                return &rt.host->gameObject->scene()->tweens();
            return nullptr;
        };
        // Optional on-complete: if arg `i` names a script function, a callback that
        // calls it (DOTween's OnComplete). Anything else yields an empty callback.
        auto fnFromArg = [this](std::vector<Value>& a, std::size_t i) -> std::function<void()> {
            if (i >= a.size() || !a[i].IsString()) return {};
            std::string fn = a[i].AsString();
            if (!rt.functions.count(fn)) return {};
            return [this, fn, alive = std::weak_ptr<int>(life)]() {
                if (alive.expired()) return;
                std::vector<Value> none; rt.Call(fn, none);
            };
        };
        // Script-facing ids for tweens (also those moved into a sequence).
        struct TweenRefs { std::unordered_map<int, std::weak_ptr<Tweener>> map; int next = 1; };
        auto refs = std::make_shared<TweenRefs>();
        auto reg = [refs](const std::shared_ptr<Tweener>& t) -> Value {
            if (!t) return Value{0.0f};
            if (refs->map.size() > 256 && (refs->next % 64) == 0)   // forget finished ones now and then
                for (auto it = refs->map.begin(); it != refs->map.end();) it = it->second.expired() ? refs->map.erase(it) : std::next(it);
            const int id = refs->next++;
            refs->map[id] = t;
            return Value{(float)id};
        };
        auto lookup = [refs](std::vector<Value>& a, std::size_t i = 0) -> std::shared_ptr<Tweener> {
            if (i >= a.size()) return nullptr;
            auto it = refs->map.find((int)std::lround(a[i].AsFloat()));
            if (it == refs->map.end()) return nullptr;
            auto t = it->second.lock();
            return t && t->IsActive() ? t : nullptr;
        };
        // Finish a new tween: ease from arg `ei` (else keep its own), on-complete from `di`.
        auto finish = [applyEase, easeFromArg, fnFromArg, reg](std::vector<Value>& a, std::shared_ptr<Tweener> t, std::size_t ei, std::size_t di) -> Value {
            if (!t) return Value{0.0f};
            if (ei < a.size() && a[ei].IsString()) applyEase(*t, a, ei);
            else if (ei != (std::size_t)-1) t->SetEase(Ease::Linear);
            if (auto f = fnFromArg(a, di)) t->OnComplete(f);
            return reg(t);
        };
        const std::size_t kNone = (std::size_t)-1;
        auto self = go;
        auto num = [](std::vector<Value>& a, std::size_t i, float def) { return i < a.size() && !a[i].IsString() ? a[i].AsFloat() : def; };
        // A script number as a count: NaN is `whenNaN`, and a huge value stops at about two billion (casting a float that
        // is out of range to int is undefined, and gave a negative count: a loop count of 1e12 became "forever").
        auto toCount = [](float v, int whenNaN) { return v != v ? whenNaN : v >= 2.0e9f ? 2000000000 : v <= -2.0e9f ? -2000000000 : (int)v; };

        b["tween_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            const float z = g->transform->localPosition.z;
            return finish(a, DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), z}, a[2].AsFloat()), 3, 4);
        };
        b["tween_move3"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        // Glide by an offset (DOTween's DOBlendableMoveBy).
        b["tween_move_by"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            auto t = DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), 0.0f}, a[2].AsFloat());
            t->SetRelative();
            return finish(a, t, 3, 4);
        };
        // World-space move (ignores the parent).
        b["tween_move_world"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::Move(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        b["tween_scale"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Scale(g, a[0].AsFloat(), a[1].AsFloat()), 2, 3);
        };
        b["tween_scale_xy"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::Scale(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localScale.z}, a[2].AsFloat()), 3, 4);
        };
        // Spin by `deg` degrees about Z (full turns work).
        b["tween_rotate"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            auto t = DOTween::Rotate(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat());
            t->SetRelative();
            return finish(a, t, 2, 3);
        };
        // Rotate to an absolute Z angle (DOTween's DORotate).
        b["tween_rotate_to"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Rotate(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat()), 2, 3);
        };
        // Rotate to x, y, z degrees (3D).
        b["tween_rotate3"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::Rotate(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        // Ping-pong forever between here and (x, y) — floating pickups, patrols.
        b["tween_loop_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            auto t = DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localPosition.z}, a[2].AsFloat());
            t->SetLoops(-1, LoopType::Yoyo);
            return finish(a, t, 3, kNone);
        };
        b["tween_loop_scale"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            auto t = DOTween::Scale(g, a[0].AsFloat(), a[1].AsFloat());
            t->SetLoops(-1, LoopType::Yoyo);
            return finish(a, t, 2, kNone);
        };
        // Spin forever: a full turn every `dur` seconds (dir +1 CCW, -1 CW).
        b["tween_loop_rotate"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.empty() || a[0].AsFloat() < 1e-3f) return Value{0.0f};
            const float dir = a.size() > 1 ? (a[1].AsFloat() < 0 ? -1.0f : 1.0f) : 1.0f;
            auto t = DOTween::Rotate(g, {0, 0, 360.0f * dir}, a[0].AsFloat());
            t->SetRelative().SetEase(Ease::Linear).SetLoops(-1, LoopType::Incremental);
            return reg(t);
        };
        // Punch the scale out and settle back (DOPunchScale) — button presses, pickups.
        b["tween_punch_scale"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float p = a[0].AsFloat();
            return reg(DOTween::PunchScale(g, {p, p, p}, a[1].AsFloat(), toCount(num(a, 2, 6.0f), 6)));
        };
        b["tween_punch_pos"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return reg(DOTween::PunchPosition(g, {a[0].AsFloat(), a[1].AsFloat(), 0}, a[2].AsFloat(), toCount(num(a, 3, 6.0f), 6)));
        };
        b["tween_punch_rotate"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return reg(DOTween::PunchRotation(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat(), toCount(num(a, 2, 6.0f), 6)));
        };
        // Shake, fading out (camera / impact shake).
        b["tween_shake"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float s = a[0].AsFloat();
            return reg(DOTween::ShakePosition(g, a[1].AsFloat(), {s, s, 0}, toCount(num(a, 2, 20.0f), 20)));
        };
        b["tween_shake_rotate"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return reg(DOTween::ShakeRotation(g, a[1].AsFloat(), {0, 0, a[0].AsFloat()}, toCount(num(a, 2, 14.0f), 14)));
        };
        b["tween_shake_scale"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float s = a[0].AsFloat();
            return reg(DOTween::ShakeScale(g, a[1].AsFloat(), {s, s, s}, toCount(num(a, 2, 14.0f), 14)));
        };
        // Jump to (x, y) in an arc `height` high: tween_jump(x, y, height, dur[, jumps][, "done"]).
        b["tween_jump"] = [self, finish, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            const int jumps = toCount(num(a, 4, 1.0f), 1);
            const std::size_t di = a.size() > 4 && a[4].IsString() ? 4 : 5;
            auto t = DOTween::Jump(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localPosition.z}, a[2].AsFloat(), jumps, a[3].AsFloat());
            return finish(a, t, kNone, di);
        };
        // Through waypoints: tween_path(dur, x1, y1, x2, y2, ...). tween_path_smooth curves through them.
        auto pathFn = [self, reg](std::vector<Value>& a, DOTween::PathType type) -> Value {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            std::vector<Vec3> pts;
            for (std::size_t i = 1; i + 1 < a.size(); i += 2) pts.push_back({a[i].AsFloat(), a[i + 1].AsFloat(), g->transform->localPosition.z});
            auto t = DOTween::Path(g, pts, a[0].AsFloat(), type);
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        b["tween_path"] = [pathFn](std::vector<Value>& a) { return pathFn(a, DOTween::PathType::Linear); };
        b["tween_path_smooth"] = [pathFn](std::vector<Value>& a) { return pathFn(a, DOTween::PathType::CatmullRom); };
        // Count this object's text from -> to (score ticks); optional prefix "Score: ".
        b["tween_number"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            const std::string prefix = (a.size() > 3 && a[3].IsString()) ? a[3].AsString() : std::string{};
            auto t = DOTween::Counter(g, a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), prefix);
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        // Type this object's text out letter by letter (dialogue): tween_text("Hello!", dur[, "done"]).
        b["tween_text"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Text(g, a[0].AsString(), a[1].AsFloat()), kNone, 2);
        };
        // A UI widget's anchored position / size — slide menus and panels in and out.
        b["tween_ui_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::AnchorPos(g, {a[0].AsFloat(), a[1].AsFloat()}, a[2].AsFloat()), 3, 4);
        };
        b["tween_ui_size"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::SizeDelta(g, {a[0].AsFloat(), a[1].AsFloat()}, a[2].AsFloat()), 3, 4);
        };
        // This object's colour (sprite, mesh, UI or text) / its alpha.
        b["tween_color"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); Color* c = DOTween::ColorOf(g);
            if (!c || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::ColorTo(g, Color{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), c->a}, a[3].AsFloat()), 4, 5);
        };
        b["tween_fade"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Fade(g, a[0].AsFloat(), a[1].AsFloat()), 2, 3);
        };
        // flash(r, g, b[, dur]): snap to a colour, then fade back (hit feedback).
        b["flash"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); Color* c = DOTween::ColorOf(g);
            if (!c || a.size() < 3) return Value{0.0f};
            const Color orig = *c;
            auto t = DOTween::ColorTo(g, orig, a.size() > 3 ? a[3].AsFloat() : 0.2f);
            *c = Color{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), orig.a};
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        // A script variable: tween_var("speed", 10, 2[, ease][, "done"]) (DOTween.To).
        b["tween_var"] = [this, tweens, finish](std::vector<Value>& a) {
            TweenManager* m = tweens(); if (!m || !rt.host || a.size() < 3) return Value{0.0f};
            const std::string name = a[0].AsString();
            std::weak_ptr<int> alive = life;
            auto t = m->To([this, alive, name]() {
                               if (alive.expired()) return 0.0f;
                               auto& g = rt.Global().vars; auto it = g.find(name);
                               return it != g.end() ? it->second.AsFloat() : 0.0f; },
                           [this, alive, name](float v) { if (!alive.expired()) rt.Global().vars[name] = Value{v}; },
                           a[1].AsFloat(), a[2].AsFloat());
            t->SetTarget(rt.host->gameObject);
            return finish(a, t, 3, 4);
        };
        // Any property of this object by path: tween_prop("Light.intensity", 3, 0.5[, ease][, "done"]).
        // One vector component is its own path ("Transform.scale.y"); an integer property rounds.
        b["tween_prop"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self();
            if (!g || a.size() < 3 || !a[0].IsString()) return Value{0.0f};
            const std::string path = a[0].AsString();
            const PropertyValue cur = PropertyRegistry::GetProperty(*g, path);
            const PropertyValue to = cur.Type() == PropertyType::Int ? PropertyValue::MakeInt(std::llround((double)a[1].AsFloat()))
                                                                      : PropertyValue::MakeFloat(a[1].AsFloat());
            return finish(a, DOTween::Property(g, path, to, a[2].AsFloat()), 3, 4);
        };
        // Call a function after a delay: tween_call(seconds, "fn") (DOVirtual.DelayedCall).
        b["tween_call"] = [this, tweens, fnFromArg, reg](std::vector<Value>& a) {
            TweenManager* m = tweens(); if (!m || a.size() < 2) return Value{0.0f};
            auto t = m->DelayedCall(a[0].AsFloat(), fnFromArg(a, 1));
            if (rt.host) t->SetTarget(rt.host->gameObject);
            return reg(t);
        };

        // ---- Settings: each returns the id, so they nest ----
        auto setting = [lookup](std::function<void(Tweener&, std::vector<Value>&)> fn) {
            return [lookup, fn](std::vector<Value>& a) -> Value {
                auto t = lookup(a, 0);
                if (t) fn(*t, a);
                return a.empty() ? Value{0.0f} : a[0];
            };
        };
        b["tween_delay"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetDelay(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["tween_ease"]     = setting([applyEase](Tweener& t, std::vector<Value>& a) { applyEase(t, a, 1); });
        b["tween_speed"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetSpeed(a.size() > 1 ? a[1].AsFloat() : 1.0f); });
        b["tween_unscaled"] = setting([](Tweener& t, std::vector<Value>& a) { t.SetUpdate(a.size() > 1 ? a[1].AsBool() : true); });
        // tween_clock(id, "ui"): follow a named clock channel ("game", "ui", "cinematic", your own); "" = scene time.
        b["tween_clock"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetClock(a.size() > 1 ? a[1].AsString() : std::string{}); });
        // tween_tag(id, "menu"): a group label for tween_kill_tag / tween_pause_tag / tween_play_tag.
        b["tween_tag"]      = setting([](Tweener& t, std::vector<Value>& a) { t.SetTag(a.size() > 1 ? a[1].AsString() : std::string{}); });
        // tween_speed_based(id): the duration you gave is a speed (units a second); the tween lasts distance / speed.
        b["tween_speed_based"] = setting([](Tweener& t, std::vector<Value>& a) { t.SetSpeedBased(a.size() > 1 ? a[1].AsBool() : true); });
        // tween_decay(id, rate): how a punch or shake dies away (0 = straight fade, 6 = snappy).
        b["tween_decay"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetDecay(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["tween_set_id"]   = setting([](Tweener& t, std::vector<Value>& a) { t.SetId(a.size() > 1 ? a[1].AsString() : std::string{}); });
        b["tween_from"]     = setting([](Tweener& t, std::vector<Value>&) { t.From(); });
        b["tween_relative"] = setting([](Tweener& t, std::vector<Value>&) { t.SetRelative(); });
        b["tween_keep"]     = setting([](Tweener& t, std::vector<Value>&) { t.SetAutoKill(false); });
        // tween_loops(id, count[, "restart" | "yoyo" | "incremental"]); count -1 = forever.
        b["tween_loops"] = setting([toCount](Tweener& t, std::vector<Value>& a) {
            std::string ty = a.size() > 2 && a[2].IsString() ? a[2].AsString() : std::string("restart");
            for (auto& ch : ty) ch = (char)std::tolower((unsigned char)ch);
            const LoopType lt = ty == "yoyo" ? LoopType::Yoyo : ty == "incremental" ? LoopType::Incremental : LoopType::Restart;
            t.SetLoops(a.size() > 1 ? toCount(a[1].AsFloat(), 1) : 1, lt);   // NaN plays once; a negative count loops forever
        });
        b["tween_on_complete"] = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnComplete(fnFromArg(a, 1)); });
        b["tween_on_step"]     = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnStepComplete(fnFromArg(a, 1)); });
        b["tween_on_start"]    = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnStart(fnFromArg(a, 1)); });
        b["tween_on_update"]   = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnUpdate(fnFromArg(a, 1)); });

        // ---- Control ----
        b["tween_kill"]      = setting([](Tweener& t, std::vector<Value>& a) { t.Kill(a.size() > 1 && a[1].AsBool()); });
        b["tween_pause"]     = setting([](Tweener& t, std::vector<Value>&) { t.Pause(); });
        b["tween_play"]      = setting([](Tweener& t, std::vector<Value>&) { t.Play(); });
        b["tween_toggle"]    = setting([](Tweener& t, std::vector<Value>&) { t.TogglePause(); });
        b["tween_restart"]   = setting([](Tweener& t, std::vector<Value>&) { t.Restart(); });
        b["tween_rewind"]    = setting([](Tweener& t, std::vector<Value>&) { t.Rewind(); });
        b["tween_complete"]  = setting([](Tweener& t, std::vector<Value>&) { t.Complete(); });
        b["tween_backwards"] = setting([](Tweener& t, std::vector<Value>&) { t.PlayBackwards(); });
        b["tween_goto"]      = setting([](Tweener& t, std::vector<Value>& a) { t.Goto(a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 && a[2].AsBool()); });
        b["tween_playing"] = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t && t->IsPlaying()}; };
        b["tween_active"]  = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t != nullptr}; };
        b["tween_progress"] = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t ? t->ElapsedPercentage() : 1.0f}; };
        // Everything on this object.
        b["tween_kill_all"]  = [self](std::vector<Value>& a) { return Value{(float)DOTween::Kill(self(), !a.empty() && a[0].AsBool())}; };
        b["tween_pause_all"] = [self](std::vector<Value>&) { return Value{(float)DOTween::Pause(self())}; };
        b["tween_play_all"]  = [self](std::vector<Value>&) { return Value{(float)DOTween::Play(self())}; };
        b["is_tweening"] = [self](std::vector<Value>&) {
            GameObject* g = self();
            return Value{g && g->scene() && g->scene()->tweens().IsTweening(g)};
        };
        // Everything with an id set by tween_set_id (or a Tween Animation's Id), scene-wide.
        auto byId = [this, tweens](int op) {
            return [this, tweens, op](std::vector<Value>& a) -> Value {
                TweenManager* m = tweens(); if (!m || a.empty()) return Value{0.0f};
                const std::string id = a[0].AsString();
                int n = op == 0 ? m->Kill(id) : op == 1 ? m->Pause(id) : op == 2 ? m->Play(id) : m->Restart(id);
                if (op == 2 || op == 3)   // Tween Animations that haven't started yet
                    n += TweenAnimation::PlayAll(*rt.host->gameObject->scene(), id, op == 3);
                return Value{(float)n};
            };
        };
        // Everything with a tag set by tween_tag, scene-wide: tween_kill_tag("menu"[, complete]), pause, play, complete.
        auto byTag = [tweens](int op) {
            return [tweens, op](std::vector<Value>& a) -> Value {
                TweenManager* m = tweens(); if (!m || a.empty()) return Value{0.0f};
                const std::string tag = a[0].AsString();
                const int n = op == 0 ? m->KillByTag(tag, a.size() > 1 && a[1].AsBool())
                            : op == 1 ? m->PauseByTag(tag) : op == 2 ? m->PlayByTag(tag) : m->CompleteByTag(tag);
                return Value{(float)n};
            };
        };
        b["tween_kill_tag"]     = byTag(0);
        b["tween_pause_tag"]    = byTag(1);
        b["tween_play_tag"]     = byTag(2);
        b["tween_complete_tag"] = byTag(3);
        b["tween_rewind_id"] = [tweens](std::vector<Value>& a) -> Value {
            TweenManager* m = tweens(); if (!m || a.empty()) return Value{0.0f};
            return Value{(float)m->Rewind(a[0].AsString())};
        };
        b["tween_kill_id"]    = byId(0);
        b["tween_pause_id"]   = byId(1);
        b["tween_play_id"]    = byId(2);
        b["tween_restart_id"] = byId(3);

        // ---- Sequences: tweens one after another / together on one timeline ----
        b["seq_new"] = [this, tweens, reg](std::vector<Value>&) {
            TweenManager* m = tweens(); if (!m) return Value{0.0f};
            auto s = m->Sequence();
            if (rt.host) s->SetTarget(rt.host->gameObject);
            return reg(s);
        };
        auto seqOp = [lookup](std::function<void(TweenSequence&, std::vector<Value>&)> fn) {
            return [lookup, fn](std::vector<Value>& a) -> Value {
                auto s = std::dynamic_pointer_cast<TweenSequence>(lookup(a, 0));
                if (s) fn(*s, a);
                return a.empty() ? Value{0.0f} : a[0];
            };
        };
        b["seq_append"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Append(t); });
        b["seq_join"]   = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Join(t); });
        b["seq_prepend"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Prepend(t); });
        b["seq_join_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) { if (auto f = fnFromArg(a, 1)) s.JoinCallback(f); });
        b["seq_insert"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 2)) s.Insert(a[1].AsFloat(), t); });
        b["seq_interval"] = seqOp([](TweenSequence& s, std::vector<Value>& a) { s.AppendInterval(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["seq_prepend_interval"] = seqOp([](TweenSequence& s, std::vector<Value>& a) { s.PrependInterval(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["seq_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) { if (auto f = fnFromArg(a, 1)) s.AppendCallback(f); });
        b["seq_insert_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) {
            if (auto f = fnFromArg(a, 2)) s.InsertCallback(a.size() > 1 ? a[1].AsFloat() : 0.0f, f);
        });

}

void OkayScriptVM::Impl::RegisterFriendlyAliasBuiltins(BuiltinCtx& c) {
        auto alias = c.alias;
        // ---- Friendly aliases: intuitive names for common builtins so games
        //      read naturally. Each points at an already-registered function. ----
        alias("delta_time", "dt");
        alias("get_key", "key");
        alias("get_key_down", "key_down");
        alias("get_key_up", "key_up");
        alias("is_key_down", "key");
        alias("random", "rand");
        alias("random_int", "randi");
        alias("random_range", "rand");
        alias("pick", "choose");
        alias("distance", "dist");
        alias("distance_to", "dist_to");
        alias("instantiate", "spawn");
        alias("instantiate3", "spawn3");
        alias("destroy_self", "destroy");
        alias("translate", "move");
        alias("rotate_z", "rotate");
        alias("set_position", "set_pos");
        alias("set_rotation", "set_rot3");
        alias("play_audio", "play_sound");
        alias("to_string", "to_str");
        alias("to_number", "to_num");
        alias("str", "to_str");
        alias("num", "to_num");
        alias("string_length", "str_len");
        alias("get_x", "pos_x");
        alias("get_y", "pos_y");
        alias("get_z", "pos_z");
        alias("velocity", "set_velocity");
        alias("screen_width", "screen_w");
        alias("screen_height", "screen_h");
        alias("debug", "debug_log");

}

void OkayScriptVM::Impl::RegisterCsApiBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto alias = c.alias;
        // ---- API: write scripts that look like C#. -----
        //      Dotted names are single tokens (see the lexer), so these read as
        //      Input.GetKeyDown("space"), Mathf.Sin(t), Debug.Log(x), etc.
        b["Vector3"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        b["Vector2"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f}};
        };
        // Color(r, g, b) -> a Vec3 of RGB (pass to set_color / set_tint).
        b["Color"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 1.0f,
                              a.size() > 1 ? a[1].AsFloat() : 1.0f,
                              a.size() > 2 ? a[2].AsFloat() : 1.0f}};
        };
        // Vector math on Vec3 values (from Vector3(...)/new Vector3(...)). Read
        // components with v.x/v.y/v.z (see property access) or vec_x/y/z(v).
        // Construct a vector value from components: vec3(x, y, z) / vec2(x, y).
        b["vec3"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        b["vec2"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f}};
        };
        b["vec_cross"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{Vec3::Zero};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            return Value{Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x}};
        };
        b["vec_reflect"] = [](std::vector<Value>& a) -> Value {
            // Reflect direction d about a normal n:  d - 2*(d·n)*n.
            if (a.size() < 2) return Value{Vec3::Zero};
            Vec3 d = a[0].AsVec3(), n = a[1].AsVec3();
            float dn = d.x * n.x + d.y * n.y + d.z * n.z;
            return Value{Vec3{d.x - 2.0f * dn * n.x, d.y - 2.0f * dn * n.y, d.z - 2.0f * dn * n.z}};
        };
        b["vec_clamp_len"] = [](std::vector<Value>& a) -> Value {
            // Clamp a vector's magnitude to at most maxLen (Vector3.ClampMagnitude).
            if (a.empty()) return Value{Vec3::Zero};
            Vec3 v = a[0].AsVec3(); float maxLen = a.size() > 1 ? a[1].AsFloat() : 1.0f;
            float m = v.Magnitude();
            if (m > maxLen && m > 0.0001f) { float s = maxLen / m; v = Vec3{v.x * s, v.y * s, v.z * s}; }
            return Value{v};
        };
        b["vec_add"]   = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) + (a.size() > 1 ? a[1].AsVec3() : Vec3::Zero)}; };
        b["vec_sub"]   = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) - (a.size() > 1 ? a[1].AsVec3() : Vec3::Zero)}; };
        b["vec_scale"] = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) * (a.size() > 1 ? a[1].AsFloat() : 1.0f)}; };
        b["vec_length"]= [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().Magnitude()}; };
        b["vec_dot"]   = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            return Value{u.x * v.x + u.y * v.y + u.z * v.z};
        };
        b["vec_distance"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            return Value{(a[0].AsVec3() - a[1].AsVec3()).Magnitude()};
        };
        b["vec_normalize"] = [](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{Vec3::Zero};
            Vec3 v = a[0].AsVec3(); float m = v.Magnitude();
            return Value{m > 1e-6f ? v * (1.0f / m) : Vec3::Zero};
        };
        // Angle in degrees between two vectors (Vector3.Angle).
        b["vec_angle"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            float mu = u.Magnitude(), mv = v.Magnitude();
            if (mu < 1e-6f || mv < 1e-6f) return Value{0.0f};
            float c = (u.x * v.x + u.y * v.y + u.z * v.z) / (mu * mv);
            return Value{std::acos(Mathf::Clamp(c, -1.0f, 1.0f)) * Mathf::Rad2Deg};
        };
        b["vec_lerp"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3(); float t = a[2].AsFloat();
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return Value{u + (v - u) * t};
        };
        b["vec_x"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().x}; };
        b["vec_y"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().y}; };
        b["vec_z"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().z}; };
        b["vec_move_towards"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 c = a[0].AsVec3(), t = a[1].AsVec3(); float md = a[2].AsFloat();
            Vec3 d = t - c; float dist = d.Magnitude();
            if (dist <= md || dist < 1e-6f) return Value{t};
            return Value{c + d * (md / dist)};
        };
        // Smooth 2D value noise in [0,1].
        b["noise"] = [](std::vector<Value>& a) -> Value {
            float x = a.size() > 0 ? a[0].AsFloat() : 0.0f;
            float y = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            // Unsigned arithmetic wraps instead of overflowing (same values as before).
            auto hash = [](unsigned xi, unsigned yi) {
                unsigned h = xi * 374761393u + yi * 668265263u;
                h = (h ^ (h >> 13)) * 1274126177u;
                return (float)((h ^ (h >> 16)) & 0xFFFFFFu) / (float)0xFFFFFFu;
            };
            // Lattice cell (floor of v, wrapped mod 2^32 like the hash) and the
            // fraction within it, safe for huge or non-finite coordinates.
            auto cell = [](float v, unsigned& c) -> float {
                if (!std::isfinite(v)) { c = 0; return 0.0f; }
                const double f = std::floor((double)v);
                c = (unsigned)(long long)std::fmod(f, 4294967296.0);
                return (float)((double)v - f);
            };
            unsigned x0, y0;
            float fx = cell(x, x0), fy = cell(y, y0);
            auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
            float sx = sm(fx), sy = sm(fy);
            float n00 = hash(x0, y0), n10 = hash(x0 + 1, y0);
            float n01 = hash(x0, y0 + 1), n11 = hash(x0 + 1, y0 + 1);
            float nx0 = n00 + (n10 - n00) * sx, nx1 = n01 + (n11 - n01) * sx;
            return Value{nx0 + (nx1 - nx0) * sy};
        };
        alias("vec3", "Vector3");
        alias("vec2", "Vector2");
        alias("Vector3.Lerp", "vec_lerp");
        alias("Vector3.Dot", "vec_dot");
        alias("Vector3.Normalize", "vec_normalize");
        alias("Vector3.MoveTowards", "vec_move_towards");
        alias("Mathf.PerlinNoise", "noise");
        alias("Mathf.Clamp01", "clamp01");
        alias("Mathf.InverseLerp", "inverse_lerp");
        alias("Mathf.Repeat", "math_repeat");
        alias("Mathf.DeltaAngle", "delta_angle");
        alias("Mathf.LerpAngle", "lerp_angle");
        alias("Mathf.Approximately", "approximately");
        alias("Mathf.Log", "log");   alias("Mathf.Exp", "exp");
        alias("Mathf.Asin", "asin"); alias("Mathf.Acos", "acos"); alias("Mathf.Atan", "atan");
        alias("Color.Lerp", "lerp_color");
        alias("Vector3.LerpUnclamped", "vec_lerp");
        alias("Vector2.Lerp", "vec_lerp");
        alias("Vector3.Angle", "vec_angle");
        alias("Vector2.Angle", "vec_angle");
        alias("Input.GetButton", "key");
        alias("Input.GetButtonDown", "key_down");
        alias("Input.GetButtonUp", "key_up");
        alias("string.Format", "format");
        alias("String.Format", "format");
        // Quaternion.Euler(x, y, z) -> a Vec3 of euler angles (assignable to
        // transform.rotation, which uses the Z component for 2D).
}

void OkayScriptVM::Impl::RegisterPlainAliasBuiltins(BuiltinCtx& c) {
        auto alias = c.alias;
        // ---- Plain-English aliases: say what you mean. Every name below points
        //      at an existing builtin, so beginners can guess the right word. ----
        // Input
        alias("is_key_held", "key");
        alias("key_pressed", "key_down");
        alias("key_released", "key_up");
        alias("input_horizontal", "axis_x");
        alias("input_vertical", "axis_y");
        alias("move_input_x", "axis_x");
        alias("move_input_y", "axis_y");
        alias("is_mouse_held", "mouse");
        alias("mouse_pressed", "mouse_down");
        alias("mouse_released", "mouse_up");
        alias("mouse_position_x", "mouse_x");
        alias("mouse_position_y", "mouse_y");
        // Transform / movement
        alias("move_by", "move");
        alias("shake", "tween_shake");   // shake(intensity, dur) — shorter name
        alias("face", "look_at");        // face("Name") — turn to look at an object
        alias("place_at", "set_pos");
        alias("set_position_x", "set_x");
        alias("set_position_y", "set_y");
        alias("position_x", "pos_x");
        alias("position_y", "pos_y");
        alias("position_z", "pos_z");
        alias("turn", "rotate");
        alias("face", "look_at");
        alias("chase", "follow");
        alias("run_from", "flee");
        alias("wasd_move", "on_key_move");
        alias("wasd_move3", "on_key_move3");
        alias("face_3d", "look_at3");
        alias("set_size", "set_scale");
        alias("move_towards_object", "move_toward3");
        // Time & scheduling
        alias("frame_time", "dt");
        alias("time_elapsed", "time");
        alias("game_time", "time");
        alias("call_later", "after");
        alias("invoke_after", "after");
        alias("repeat_every", "every");
        alias("set_time_scale", "set_timescale");
        alias("time_scale", "timescale");
        alias("Time.PushPause", "push_pause");
        alias("Time.PopPause", "pop_pause");
        alias("Time.IsPausedBy", "is_paused_by");
        alias("Time.HitStop", "hit_stop");
        alias("Time.SlowMotion", "slow_motion");
        alias("Time.RampTimeScale", "ramp_timescale");
        alias("ramp_time_scale", "ramp_timescale");
        // Objects & scene
        alias("create_object", "spawn");
        alias("create_object_3d", "spawn3");
        alias("remove_object", "destroy");
        alias("show_object", "activate");
        alias("hide_object", "deactivate");
        alias("object_exists", "exists");
        alias("is_object_active", "is_active");
        alias("object_x", "obj_x");
        alias("object_y", "obj_y");
        alias("object_z", "obj_z");
        alias("nearest_with_tag", "nearest_tag");
        alias("count_with_tag", "count_tag");
        // Rendering & components
        alias("set_tint", "set_color");
        alias("set_sprite", "set_texture");
        alias("set_background_color", "set_bg");
        alias("set_background", "set_bg");
        alias("set_light_direction", "set_light");
        alias("set_ui_image", "set_image");
        alias("set_button_enabled", "set_interactable");
        alias("set_progress_bar", "set_progress");
        alias("emit_particles", "emit");
        alias("play_sfx", "play_sound");
        alias("set_flip_x", "flip_x");
        alias("set_flip_y", "flip_y");
        // Camera
        alias("camera_x", "cam_x");
        alias("camera_y", "cam_y");
        alias("set_camera", "set_cam");
        alias("move_camera", "move_cam");
        alias("camera_zoom", "cam_zoom");
        alias("set_camera_zoom", "set_cam_zoom");
        alias("set_zoom", "set_cam_zoom");
        alias("shake_camera", "camera_shake");
        alias("cam_shake", "camera_shake");
        alias("zoom_camera", "camera_zoom_to");
        // Physics
        alias("is_overlapping", "overlap");
        alias("point_overlaps", "overlap");
        alias("cast_ray", "raycast");
        alias("apply_force", "add_force");
        alias("apply_impulse", "add_impulse");
        alias("get_velocity_x", "velocity_x");
        alias("get_velocity_y", "velocity_y");
        // Saving / prefs
        alias("save_value", "prefs_set");
        alias("load_number", "prefs_get");
        alias("load_value", "prefs_get");
        alias("load_text", "prefs_get_str");
        alias("write_save", "prefs_save");
        alias("read_save", "prefs_load");
        alias("set_master_volume", "set_volume");
        // Math
        alias("random_float", "rand");
        alias("random_chance", "chance");
        alias("roll_chance", "chance");
        alias("blend", "lerp");
        // Logging
        alias("log", "print");
        alias("log_message", "print");
        alias("json_stringify", "to_json");
        alias("json_parse", "from_json");

}

void OkayScriptVM::Impl::RegisterCsCompatBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto alias = c.alias;
        // ---- C# compatibility -------------------------------------------
        // Collections created by `new List<T>()`, `new Dictionary<K,V>()`, `new T[n]`
        // (the parser lowers those to these calls). Lists/arrays share one type.
        b["__new_list"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            if (!a.empty()) if (auto src = a[0].AsArray()) *v.AsArray() = *src;   // initializer / copy
            return v;
        };
        b["__new_array"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            int n = a.empty() ? 0 : SafeInt(a[0].AsFloat());
            if (n > 0) v.AsArray()->assign((std::size_t)std::min(n, 1 << 20), Value{0.0f});
            return v;
        };
        b["__new_map"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap();
            if (a.empty()) return v;
            if (auto src = a[0].AsMap()) { *v.AsMap() = *src; return v; }
            if (auto pairs = a[0].AsArray())                          // { {"a", 1}, {"b", 2} }
                for (auto& pr : *pairs)
                    if (auto kv = pr.AsArray(); kv && kv->size() >= 2) (*v.AsMap())[(*kv)[0].AsString()] = (*kv)[1];
            return v;
        };
        // Invoke("Fire", 2f) / InvokeRepeating("Spawn", 1f, 0.5f) / CancelInvoke().
        b["Invoke"] = [this](std::vector<Value>& a) {
            if (!a.empty()) rt.timers.push_back({a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f, a[0].AsString(), false});
            return Value{};
        };
        b["InvokeRepeating"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            float first = a.size() > 1 ? a[1].AsFloat() : 0.0f, rate = a.size() > 2 ? a[2].AsFloat() : 1.0f;
            if (rate <= 0.0f) rate = 0.0001f;
            rt.timers.push_back({first, rate, a[0].AsString(), false});
            return Value{};
        };
        b["CancelInvoke"] = [this](std::vector<Value>& a) {
            if (a.empty()) rt.timers.clear();
            else { std::string fn = a[0].AsString(); for (auto& t : rt.timers) if (t.fn == fn) t.dead = true; }
            return Value{};
        };
        // transform.Translate(Vector3 / x, y, z) moves in the object's own space
        //; pass Space.World to move in world axes.
        // The two-number OkayScript form translate(x, y) stays a plain 2D move.
        b["transform.Translate"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (a.size() == 2 && !a[0].IsVec3()) { t->Translate({a[0].AsFloat(), a[1].AsFloat(), 0.0f}); return Value{}; }
            Vec3 d; std::size_t next;
            if (a[0].IsVec3()) { d = a[0].AsVec3(); next = 1; }
            else { d = {a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f}; next = 3; }
            const bool world = a.size() > next && a[next].AsFloat() != 0.0f;
            t->Translate(world ? d : t->localRotation * d);
            return Value{};
        };
        // transform.Rotate(x, y, z) / Rotate(Vector3) / Rotate(axis, angle); one number = 2D spin about Z.
        b["transform.Rotate"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (a[0].IsVec3() && a.size() >= 2 && !a[1].IsVec3() && a[1].AsFloat() != 0.0f && a.size() == 2 &&
                std::fabs(a[0].AsVec3().Magnitude() - 1.0f) < 1e-3f) {
                t->localRotation = (t->localRotation * Quat::AngleAxis(a[1].AsFloat(), a[0].AsVec3())).Normalized();
                return Value{};
            }
            Vec3 e = a[0].IsVec3() ? a[0].AsVec3()
                   : (a.size() == 1 ? Vec3{0, 0, a[0].AsFloat()}
                                    : Vec3{a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f});
            t->Rotate(e);
            return Value{};
        };
        alias("Debug.LogWarning", "log_warn");
        alias("Debug.LogError", "log_error");
        alias("Debug.LogFormat", "format");
        b["Input.GetAxisRaw"] = [](std::vector<Value>& a) -> Value {
            std::string ax = a.empty() ? std::string{} : a[0].AsString();
            float v = ax == "Vertical" ? Input::AxisWASD().y : Input::AxisWASD().x;
            return Value{v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f)};
        };
        b["string.IsNullOrEmpty"] = [](std::vector<Value>& a) { return Value{a.empty() || a[0].AsString().empty()}; };
        b["string.IsNullOrWhiteSpace"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{true};
            for (char c : a[0].AsString()) if (!std::isspace((unsigned char)c)) return Value{false};
            return Value{true};
        };
        b["string.Join"] = [](std::vector<Value>& a) {   // string.Join(", ", items)
            std::string sep = a.empty() ? "" : a[0].AsString(), out;
            if (a.size() > 1)
                if (auto arr = a[1].AsArray())
                    for (std::size_t i = 0; i < arr->size(); ++i) { if (i) out += sep; out += (*arr)[i].AsString(); }
            return Value{out};
        };
        alias("String.IsNullOrEmpty", "string.IsNullOrEmpty");
        alias("String.Join", "string.Join");
        auto parseNum = [](std::vector<Value>& a, bool asInt) {
            float f = 0.0f;
            if (!a.empty()) { try { f = std::stof(a[0].AsString()); } catch (...) { f = 0.0f; } }
            return Value{asInt ? (float)(long long)f : f};
        };
        b["int.Parse"]   = [parseNum](std::vector<Value>& a) { return parseNum(a, true); };
        b["float.Parse"] = [parseNum](std::vector<Value>& a) { return parseNum(a, false); };
        alias("double.Parse", "float.Parse");
        alias("Convert.ToInt32", "int.Parse");
        alias("Convert.ToSingle", "float.Parse");
        b["Mathf.RoundToInt"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::round(a[0].AsFloat())}; };
        b["Mathf.FloorToInt"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::floor(a[0].AsFloat())}; };
        b["Mathf.CeilToInt"]  = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::ceil(a[0].AsFloat())}; };
        b["Mathf.Log10"]      = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::log10(a[0].AsFloat())}; };
        alias("Vector3.Cross", "vec_cross");
        alias("Vector3.ClampMagnitude", "vec_clamp_len");
        alias("Vector3.Reflect", "vec_reflect");
        alias("Mathf.Clamp01", "clamp01");

}

void OkayScriptVM::Impl::RegisterCoroutineBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto alias = c.alias;
        // ---- Coroutines: StartCoroutine(Routine()) + yield return new WaitForSeconds(t) ----
        b["WaitForSeconds"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap(); (*v.AsMap())["__wait"] = Value{a.empty() ? 0.0f : a[0].AsFloat()}; return v;
        };
        b["WaitForSecondsRealtime"] = [](std::vector<Value>& a) {   // ignores Time.timeScale
            Value v = Value::MakeMap(); (*v.AsMap())["__wait"] = Value{a.empty() ? 0.0f : a[0].AsFloat()}; (*v.AsMap())["__real"] = Value{true}; return v;
        };
        // yield return new WaitUntil(() => cond) / WaitWhile(() => cond)
        b["WaitUntil"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap(); (*v.AsMap())["__until"] = a.empty() ? Value{} : a[0]; return v;
        };
        b["WaitWhile"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap(); (*v.AsMap())["__while"] = a.empty() ? Value{} : a[0]; return v;
        };
        b["WaitForEndOfFrame"] = [](std::vector<Value>&) { return Value{}; };
        alias("WaitForFixedUpdate", "WaitForEndOfFrame");
        b["StartCoroutine"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            std::vector<Value> args(a.begin() + 1, a.end());
            std::vector<std::string> names;
            if (rt.activeArgNames && rt.activeArgNames->size() > 1)
                names.assign(rt.activeArgNames->begin() + 1, rt.activeArgNames->end());
            int id = rt.StartCoroutine(a[0].AsString(), std::move(args), std::move(names));
            Value v = Value::MakeMap(); (*v.AsMap())["__co"] = Value{(float)id}; return v;
        };
        b["StopCoroutine"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (auto m = a[0].AsMap()) {
                if (auto it = m->find("__co"); it != m->end())
                    if (auto* co = rt.FindCo(SafeInt(it->second.AsFloat()))) { rt.StopCoroutine(co); rt.ReapCoroutines(); }
                return Value{};
            }
            rt.StopCoroutines(a[0].AsString().empty() ? std::string("\x01") : a[0].AsString());
            return Value{};
        };
        b["StopAllCoroutines"] = [this](std::vector<Value>&) { rt.StopCoroutines(""); return Value{}; };

}

void OkayScriptVM::Impl::RegisterObjectLifecycleBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        auto findNamed = c.findNamed;
        auto alias = c.alias;
        // ---- Objects: Destroy(obj[, seconds]), Instantiate(prefab, pos, rot), CompareTag ----
        b["Destroy"] = [this, findNamed](std::vector<Value>& a) {
            Value target = a.empty() ? Value{std::string()} : a[0];
            if (IsComponentRef(target)) return Value{};   // Destroy(component): ignore
            const float delay = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            // A plain name equal to this object's own name means this object (as before);
            // a reference always means exactly the object it names.
            if (me && !target.IsObject() && target.AsString() == me->name) target = Value{std::string()};
            if (delay > 0.0f) { rt.pendingDestroy.push_back({delay, target}); return Value{}; }
            GameObject* g = findNamed(target);
            if (g && g->scene()) g->scene()->Destroy(g);
            return Value{};
        };
        alias("Object.Destroy", "Destroy");
        b["Instantiate"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (a.size() >= 2 && a[1].IsVec3()) {   // Instantiate(prefab, position, rotation)
                Vec3 p = a[1].AsVec3();
                std::vector<Value> b3{a[0], Value{p.x}, Value{p.y}, Value{p.z}};
                return rt.Call("spawn3", b3);
            }
            return rt.Call("spawn", a);
        };
        alias("Object.Instantiate", "Instantiate");
        // GameObject.Find("Enemy") -> a reference to the first ACTIVE object with that
        // name (or a "Parent/Child" path), or null. #888 F110: it used to return a
        // switched-off object too. FindIncludingInactive is the explicit "any" variant.
        b["GameObject.Find"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || a[0].AsString().empty()) return Value{};
            Scene* s = sceneOf(); if (!s) return Value{};
            if (a[0].IsObject()) { GameObject* g = ResolveObject(s, a[0]); return g && g->ActiveInHierarchy() ? ObjectRef(g) : Value{}; }
            const std::string n = a[0].AsString();
            for (const auto& up : s->Objects()) if (up && up->name == n && up->ActiveInHierarchy()) return ObjectRef(up.get());
            if (n.find('/') != std::string::npos)
                if (GameObject* g = s->Find(n); g && g->ActiveInHierarchy()) return ObjectRef(g);
            return Value{};
        };
        b["GameObject.FindIncludingInactive"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || a[0].AsString().empty()) return Value{};
            Scene* s = sceneOf();
            return ObjectRef(ResolveObject(s, a[0]));
        };
        b["GameObject.FindWithTag"] = [this](std::vector<Value>& a) -> Value {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) for (const auto& up : s->Objects()) if (up && up->ActiveInHierarchy() && up->tag == a[0].AsString()) return ObjectRef(up.get());
            return Value{};
        };
        alias("GameObject.FindGameObjectWithTag", "GameObject.FindWithTag");
        b["GameObject.FindGameObjectsWithTag"] = [this](std::vector<Value>& a) -> Value {
            Value out = Value::MakeArray();
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) for (const auto& up : s->Objects()) if (up && up->ActiveInHierarchy() && up->tag == a[0].AsString()) out.AsArray()->push_back(ObjectRef(up.get()));
            return out;
        };
        b["CompareTag"] = [go](std::vector<Value>& a) {
            GameObject* g = go(); return Value{g && !a.empty() && g->tag == a[0].AsString()};
        };
        alias("gameObject.CompareTag", "CompareTag");
        b["__object_CompareTag"] = [findNamed](std::vector<Value>& a) {
            GameObject* g = a.empty() ? nullptr : findNamed(a[0]);
            return Value{g && a.size() > 1 && g->tag == a[1].AsString()};
        };
        b["__object_SetActive"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (a[0].IsObject()) {   // exactly the referenced object
                if (GameObject* g = ResolveObject(nullptr, a[0])) g->active = a.size() < 2 || a[1].AsBool();
                return Value{};
            }
            if (Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr)
                for (const auto& up : s->Objects())
                    if (up && up->name == a[0].AsString()) { up->active = a.size() < 2 || a[1].AsBool(); break; }
            return Value{};
        };
        b["transform.LookAt"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (!a[0].IsVec3()) return rt.Call("look_at", a);
            Vec3 d = a[0].AsVec3() - t->Position();
            if (d.Magnitude() > 1e-6f) t->localRotation = Quat::LookRotation(d.Normalized());
            return Value{};
        };

}

void OkayScriptVM::Impl::RegisterComponentAccessBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto findNamed = c.findNamed;
        auto alias = c.alias;
        // ---- Components: GetComponent<T>() returns a reference ("@T") whose
        // fields / methods map onto the engine component. ----
        auto compOf = [](GameObject* g, const std::string& t) -> bool {
            if (!g) return false;
            if (t == "Transform" || t == "GameObject") return true;
            if (t == "SpriteRenderer") return g->GetComponent<SpriteRenderer>();
            if (t == "MeshRenderer" || t == "Renderer" || t == "SkinnedMeshRenderer")
                return g->GetComponent<MeshRenderer>() || g->GetComponent<SpriteRenderer>();
            if (t == "TextRenderer" || t == "Text" || t == "TMP_Text" || t == "TextMeshPro" || t == "TextMeshProUGUI")
                return g->GetComponent<TextRenderer>();
            if (t == "Rigidbody2D") return g->GetComponent<Rigidbody2D>();
            if (t == "Rigidbody")   return g->GetComponent<Rigidbody3D>();
            if (t == "AudioSource") return g->GetComponent<AudioSource>();
            if (t == "NetworkObject") return g->GetComponent<NetworkObject>();
            if (t == "NetworkTransform") return g->GetComponent<NetworkTransform>();
            if (t == "SyncVar") return g->GetComponent<SyncVar>();
            if (t == "FirstPersonHands") return g->GetComponent<FirstPersonHands>() != nullptr;
            if (t == "Animator" || t == "Animation") return true;   // resolved by the anim_* builtins
            if (t == "IK") return true;                              // resolved by the ik_* builtins
            return false;
        };
        // The engine component a C# type name refers to (for .enabled).
        auto compPtr = [](GameObject* g, const std::string& t) -> Component* {
            if (!g) return nullptr;
            if (t == "SpriteRenderer") return g->GetComponent<SpriteRenderer>();
            if (t == "MeshRenderer" || t == "SkinnedMeshRenderer") return g->GetComponent<MeshRenderer>();
            if (t == "Renderer") { if (auto* m = g->GetComponent<MeshRenderer>()) return m; return g->GetComponent<SpriteRenderer>(); }
            if (t == "TextRenderer" || t == "Text" || t == "TMP_Text" || t == "TextMeshPro" || t == "TextMeshProUGUI")
                return g->GetComponent<TextRenderer>();
            if (t == "Rigidbody2D") return g->GetComponent<Rigidbody2D>();
            if (t == "Rigidbody")   return g->GetComponent<Rigidbody3D>();
            if (t == "AudioSource") return g->GetComponent<AudioSource>();
            if (t == "NetworkObject") return g->GetComponent<NetworkObject>();
            if (t == "NetworkTransform") return g->GetComponent<NetworkTransform>();
            if (t == "SyncVar") return g->GetComponent<SyncVar>();
            if (t == "FirstPersonHands") return g->GetComponent<FirstPersonHands>();
            return nullptr;
        };
        b["GetComponent"] = [this, compOf](std::vector<Value>& a) -> Value {
            if (!rt.host || !rt.host->gameObject || a.empty()) return Value{};
            const std::string c = a[0].AsString();
            return compOf(rt.host->gameObject, c) ? Value{"@" + c} : Value{};
        };
        alias("gameObject.GetComponent", "GetComponent");
        alias("TryGetComponent", "GetComponent");
        // other.GetComponent<T>(): "@T#<id>" names that object's component, so
        // what is done with it lands on `other` (#888 F111: it used to be "@T",
        // which every handler below read as this script's own component).
        b["__object_GetComponent"] = [findNamed, compOf, go](std::vector<Value>& a) -> Value {
            GameObject* g = a.empty() ? nullptr : findNamed(a[0]);
            if (!g || a.size() < 2 || !compOf(g, a[1].AsString())) return Value{};
            if (g == go()) return Value{"@" + a[1].AsString()};
            return Value{"@" + a[1].AsString() + "#" + std::to_string(g->InstanceId())};
        };
        // The object and type a component reference names: "T" is this object's,
        // "T#<id>" another object's (nullptr once that object is gone).
        auto compTarget = [go](const Value& ref, std::string& type) -> GameObject* {
            const std::string body = ref.AsString();
            const auto hash = body.find('#');
            if (hash == std::string::npos) { type = body; return go(); }
            type = body.substr(0, hash);
            return GameObject::FromInstanceId(std::strtoull(body.c_str() + hash + 1, nullptr, 10));
        };
        // Rigidbody / Rigidbody2D members a script used that don't exist: said once each.
        auto unknownMember = [](const std::string& type, const std::string& member) {
            static std::unordered_set<std::string> told;
            if (told.insert(type + "." + member).second)
                Log::Warning("[script] ", type, " has no property '", member, "' (ignored)");
        };
        b["__get_component"] = [this, compTarget, compPtr, unknownMember](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{};
            std::string t;
            GameObject* g = compTarget(a[0], t); if (!g) return Value{};
            ActAsObject as(rt.host, g);   // Transform / Animator members read the referenced object
            const std::string p = a[1].AsString();
            auto rgb = [](const Color& c) { return Value{Vec3{c.r, c.g, c.b}}; };
            if (t == "Transform") { bool ok = false; Value v = rt.GetDotted("transform." + p, ok); return v; }
            if (p == "enabled") { Component* c = compPtr(g, t); return Value{c ? c->enabled : true}; }
            if (auto* sr = g->GetComponent<SpriteRenderer>(); sr && (t == "SpriteRenderer" || t == "Renderer")) {
                if (p == "color" || p == "material.color") return rgb(sr->color);
                if (p == "flipX") return Value{sr->flipX};
            }
            if (auto* mr = g->GetComponent<MeshRenderer>(); mr && (t == "MeshRenderer" || t == "Renderer" || t == "SkinnedMeshRenderer"))
                if (p == "color" || p == "material.color") return rgb(mr->color);
            if (auto* tr = g->GetComponent<TextRenderer>()) {
                if (p == "text") return Value{tr->text};
                if (p == "color") return rgb(tr->color);
            }
            if (auto* rb = g->GetComponent<Rigidbody2D>(); rb && t == "Rigidbody2D") {
                if (p == "velocity" || p == "linearVelocity") return Value{Vec3{rb->velocity.x, rb->velocity.y, 0.0f}};
                if (p == "gravityScale") return Value{rb->gravityScale};
                if (p == "angularVelocity") return Value{rb->angularVelocity};   // degrees per second
                if (p == "mass") return Value{rb->mass};
                if (p == "drag" || p == "linearDamping") return Value{rb->drag};
                if (p == "angularDrag" || p == "angularDamping") return Value{rb->angularDrag};
                if (p == "isKinematic") return Value{rb->bodyType == Rigidbody2D::BodyType::Kinematic};
                if (p == "bodyType") return Value{(float)(int)rb->bodyType};
                if (p == "freezeRotation") return Value{rb->freezeRotation};
                if (p == "bounciness") return Value{rb->bounciness};
                if (p == "friction") return Value{rb->friction};
                if (p == "position") return Value{g->transform ? g->transform->Position() : Vec3::Zero};
                if (p == "sleeping" || p == "isSleeping") return Value{rb->sleeping};
                unknownMember("Rigidbody2D", p);
                return Value{};
            }
            if (auto* rb = g->GetComponent<Rigidbody3D>(); rb && t == "Rigidbody") {
                if (p == "velocity" || p == "linearVelocity") return Value{rb->velocity};
                if (p == "angularVelocity") return Value{rb->angularVelocity};   // radians per second
                if (p == "mass") return Value{rb->mass};
                if (p == "drag" || p == "linearDamping") return Value{rb->drag};
                if (p == "angularDrag" || p == "angularDamping") return Value{rb->angularDrag};
                if (p == "gravityScale") return Value{rb->gravityScale};
                if (p == "useGravity") return Value{rb->gravityScale != 0.0f};
                if (p == "isKinematic") return Value{rb->bodyType == Rigidbody3D::BodyType::Kinematic};
                if (p == "freezeRotation") return Value{rb->freezeRotation};
                if (p == "bounciness") return Value{rb->bounciness};
                if (p == "friction") return Value{rb->friction};
                if (p == "maxFallSpeed") return Value{rb->maxFallSpeed};
                if (p == "position") return Value{g->transform ? g->transform->Position() : Vec3::Zero};
                if (p == "sleeping" || p == "isSleeping") return Value{rb->sleeping};
                unknownMember("Rigidbody", p);
                return Value{};
            }
            if (auto* au = g->GetComponent<AudioSource>(); au && t == "AudioSource") {
                if (p == "volume") return Value{au->volume};
                if (p == "pitch") return Value{au->pitch};
                if (p == "pan") return Value{au->pan};
                if (p == "lowPass") return Value{au->lowPass};
                if (p == "doppler") return Value{au->doppler};
                if (p == "dopplerScale") return Value{au->dopplerScale};
                if (p == "occlude") return Value{au->occlude};
                if (p == "occlusionVolume") return Value{au->occlusionVolume};
                if (p == "occlusionLowPass") return Value{au->occlusionLowPass};
                if (p == "distanceLowPass") return Value{au->distanceLowPass};
                if (p == "distanceLowPassHz") return Value{au->distanceLowPassHz};
                if (p == "isPlaying") return Value{au->IsPlaying()};
            }
            if (auto* o = g->GetComponent<NetworkObject>(); o && t == "NetworkObject") {
                if (p == "netId") return Value{o->netId};
                if (p == "owned" || p == "isOwned") return Value{o->IsLocallyOwned()};
                if (p == "authority") return Value{(float)o->authority};
            }
            if (auto* sv = g->GetComponent<SyncVar>(); sv && t == "SyncVar") {
                if (p == "name") return Value{sv->name};
                if (p == "value") return Value{sv->Get()};
            }
            if (t == "FirstPersonHands") return FpHandsGet(g->GetComponent<FirstPersonHands>(), p);
            return Value{};
        };
        b["__set_component"] = [this, compTarget, compPtr, unknownMember](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{};
            std::string t;
            GameObject* g = compTarget(a[0], t); if (!g) return Value{};
            ActAsObject as(rt.host, g);   // Transform members write the referenced object
            const std::string p = a[1].AsString();
            const Value& v = a[2];
            auto col = [&](const Color& old) {
                if (v.IsVec3()) { Vec3 c = v.AsVec3(); return Color{c.x, c.y, c.z, old.a}; }
                return old;
            };
            if (t == "Transform") { rt.SetDotted("transform." + p, v); return Value{}; }
            if (p == "enabled") { if (Component* c = compPtr(g, t)) c->enabled = v.AsBool(); return Value{}; }
            if (p == "color" || p == "material.color") {
                if (auto* sr = g->GetComponent<SpriteRenderer>(); sr && (t == "SpriteRenderer" || t == "Renderer")) sr->color = col(sr->color);
                if (auto* mr = g->GetComponent<MeshRenderer>(); mr && t != "SpriteRenderer") mr->color = col(mr->color);
                if (auto* tr = g->GetComponent<TextRenderer>(); tr && (t == "Text" || t == "TextRenderer" || t.rfind("TMP", 0) == 0 || t.rfind("TextMesh", 0) == 0)) tr->color = col(tr->color);
                return Value{};
            }
            if (p == "flipX") if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->flipX = v.AsBool();
            if (p == "text") if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = v.AsString();
            if (t == "Rigidbody2D") if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                if (p == "velocity" || p == "linearVelocity") { Vec3 q = v.AsVec3(); rb->velocity = {q.x, q.y}; rb->WakeUp(); }
                else if (p == "gravityScale") { rb->gravityScale = v.AsFloat(); rb->WakeUp(); }
                else if (p == "angularVelocity") { rb->angularVelocity = v.AsFloat(); rb->WakeUp(); }
                else if (p == "mass") {
                    if (!(v.AsFloat() > 0.0f)) throw ScriptError("Rigidbody2D mass must be more than 0");
                    rb->mass = v.AsFloat();
                }
                else if (p == "drag" || p == "linearDamping") rb->drag = std::max(0.0f, v.AsFloat());
                else if (p == "angularDrag" || p == "angularDamping") rb->angularDrag = std::max(0.0f, v.AsFloat());
                else if (p == "isKinematic") { rb->bodyType = v.AsBool() ? Rigidbody2D::BodyType::Kinematic : Rigidbody2D::BodyType::Dynamic; rb->WakeUp(); }
                else if (p == "bodyType") {
                    const int k = SafeInt(v.AsFloat());
                    rb->bodyType = k == 1 ? Rigidbody2D::BodyType::Kinematic : k == 2 ? Rigidbody2D::BodyType::Static : Rigidbody2D::BodyType::Dynamic;
                    rb->WakeUp();
                }
                else if (p == "freezeRotation") rb->freezeRotation = v.AsBool();
                else if (p == "bounciness") rb->bounciness = v.AsFloat();
                else if (p == "friction") rb->friction = v.AsFloat();
                else if (p == "position") { if (g->transform) g->transform->SetPosition(v.AsVec3()); rb->WakeUp(); }
                else if (p != "enabled") unknownMember("Rigidbody2D", p);
                return Value{};
            }
            if (t == "Rigidbody") if (auto* rb = g->GetComponent<Rigidbody3D>()) {
                if (p == "velocity" || p == "linearVelocity") { rb->velocity = v.AsVec3(); rb->WakeUp(); }
                else if (p == "angularVelocity") { rb->angularVelocity = v.AsVec3(); rb->WakeUp(); }
                else if (p == "mass") {
                    if (!(v.AsFloat() > 0.0f)) throw ScriptError("Rigidbody mass must be more than 0");
                    rb->mass = v.AsFloat();
                }
                else if (p == "drag" || p == "linearDamping") rb->drag = std::max(0.0f, v.AsFloat());
                else if (p == "angularDrag" || p == "angularDamping") rb->angularDrag = std::max(0.0f, v.AsFloat());
                else if (p == "gravityScale") { rb->gravityScale = v.AsFloat(); rb->WakeUp(); }
                else if (p == "useGravity") {   // off = gravityScale 0; on = 1 unless a scale is already set
                    if (!v.AsBool()) rb->gravityScale = 0.0f;
                    else if (rb->gravityScale == 0.0f) rb->gravityScale = 1.0f;
                    rb->WakeUp();
                }
                else if (p == "isKinematic") { rb->bodyType = v.AsBool() ? Rigidbody3D::BodyType::Kinematic : Rigidbody3D::BodyType::Dynamic; rb->WakeUp(); }
                else if (p == "freezeRotation") rb->freezeRotation = v.AsBool();
                else if (p == "bounciness") rb->bounciness = v.AsFloat();
                else if (p == "friction") rb->friction = v.AsFloat();
                else if (p == "maxFallSpeed") rb->maxFallSpeed = std::max(0.0f, v.AsFloat());
                else if (p == "position") { if (g->transform) g->transform->SetPosition(v.AsVec3()); rb->WakeUp(); }
                else if (p != "enabled") unknownMember("Rigidbody", p);
                return Value{};
            }
            if (t == "AudioSource") if (auto* au = g->GetComponent<AudioSource>()) {
                if (p == "volume") au->volume = v.AsFloat();
                if (p == "pitch") au->pitch = v.AsFloat();
                if (p == "pan") au->pan = v.AsFloat();
                if (p == "lowPass") au->lowPass = v.AsFloat();
                if (p == "doppler") au->doppler = v.AsBool();
                if (p == "dopplerScale") au->dopplerScale = v.AsFloat();
                if (p == "occlude") au->occlude = v.AsBool();
                if (p == "occlusionVolume") au->occlusionVolume = v.AsFloat();
                if (p == "occlusionLowPass") au->occlusionLowPass = v.AsFloat();
                if (p == "distanceLowPass") au->distanceLowPass = v.AsBool();
                if (p == "distanceLowPassHz") au->distanceLowPassHz = v.AsFloat();
            }
            if (t == "NetworkObject") if (auto* o = g->GetComponent<NetworkObject>()) {
                if (p == "netId") o->netId = v.AsString();
                if (p == "owned") o->SetOwned(v.AsBool());
                if (p == "authority") o->authority = (NetworkObject::Authority)SafeInt(v.AsFloat());
            }
            if (t == "SyncVar") if (auto* sv = g->GetComponent<SyncVar>()) {
                if (p == "name") sv->name = v.AsString();
                if (p == "value") sv->Set(v.AsString());
            }
            if (t == "FirstPersonHands") FpHandsSet(g->GetComponent<FirstPersonHands>(), p, v);
            return Value{};
        };
        b["__call_component"] = [this, compTarget](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{};
            std::string t;
            GameObject* g = compTarget(a[0], t); if (!g) return Value{};
            ActAsObject as(rt.host, g);   // Animator / IK / Transform calls run on the referenced object
            const std::string m = a[1].AsString();
            std::vector<Value> rest(a.begin() + 2, a.end());
            auto arg = [&](std::size_t i) { return i < rest.size() ? rest[i] : Value{}; };
            if (t == "Animator" || t == "Animation") {
                if (m == "SetTrigger") { std::vector<Value> x{arg(0)}; return rt.Call("anim_trigger", x); }
                if (m == "SetBool")    { std::vector<Value> x{arg(0), Value{arg(1).AsBool() ? 1.0f : 0.0f}}; return rt.Call("anim_set_bool", x); }
                if (m == "SetFloat" || m == "SetInteger") { std::vector<Value> x{arg(0), arg(1)}; return rt.Call("anim_set_float", x); }
                if (m == "GetFloat") return rt.Call("anim_get_float", rest);
                if (m == "GetBool") return rt.Call("anim_get_bool", rest);
                if (m == "GetInt" || m == "GetInteger") return rt.Call("anim_get_int", rest);
                if (m == "SetLayerWeight") return rt.Call("anim_set_layer_weight", rest);
                if (m == "GetLayerWeight") return rt.Call("anim_get_layer_weight", rest);
                if (m == "SetRootMotion") return rt.Call("set_root_motion", rest);
                if (m == "SetCharacterModel") return rt.Call("set_character_model", rest);
                if (m == "Play" || m == "CrossFade" || m == "CrossFadeInFixedTime") {
                    std::vector<Value> x{arg(0)};
                    if (m != "Play" && rest.size() >= 2) x.push_back(arg(1));
                    return rt.Call("play_action", x);
                }
                return Value{};
            }
            if (t == "IK") {
                if (m == "SetKindWeight") return rt.Call("ik_set_weight", rest);
                if (m == "SetLimbTarget") return rt.Call("ik_set_target", rest);
                if (m == "ClearLimbTarget") return rt.Call("ik_clear_target", rest);
                if (m == "LookAt") return rt.Call("ik_look_at", rest);
                if (m == "SetWeight") return rt.Call("ik_set_weight", rest);
                return Value{};
            }
            if (t == "Rigidbody2D") if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                // A force is a vector or two numbers, then an optional mode: ForceMode2D.Force (0) /
                // ForceMode2D.Impulse (1); ForceMode.Impulse (2) and VelocityChange (3) are taken too.
                const bool vecFirst = arg(0).IsVec3();
                Vec3 f = vecFirst ? arg(0).AsVec3() : Vec3{arg(0).AsFloat(), arg(1).AsFloat(), 0.0f};
                const std::size_t modeAt = vecFirst ? 1 : 2;
                const int mode = rest.size() > modeAt ? SafeInt(arg(modeAt).AsFloat()) : 0;
                const ForceMode fm = mode == 3 ? ForceMode::VelocityChange : (mode == 1 || mode == 2) ? ForceMode::Impulse : ForceMode::Force;
                if (m == "AddForce") { rb->AddForce(Vec2{f.x, f.y}, fm); return Value{}; }
                if (m == "AddRelativeForce") {   // in the body's own axes
                    Vec3 w = g->transform ? g->transform->Rotation() * Vec3{f.x, f.y, 0.0f} : f;
                    rb->AddForce(Vec2{w.x, w.y}, fm); return Value{};
                }
                if (m == "AddImpulse") { rb->AddImpulse(Vec2{f.x, f.y}); return Value{}; }
                if (m == "AddTorque") {   // (torque [, mode]); an impulse is spread over one physics step
                    const float tq = arg(0).AsFloat();
                    const int md = rest.size() > 1 ? SafeInt(arg(1).AsFloat()) : 0;
                    const float step = Time::FixedDeltaTime() > 0.0f ? Time::FixedDeltaTime() : 1.0f / 60.0f;
                    rb->AddTorque(md == 0 ? tq : tq / step);
                    return Value{};
                }
                if (m == "AddForceAtPosition") {
                    Vec3 pt = vecFirst ? arg(1).AsVec3() : Vec3{arg(2).AsFloat(), arg(3).AsFloat(), 0.0f};
                    rb->AddForceAtPosition(Vec2{f.x, f.y}, Vec2{pt.x, pt.y});
                    return Value{};
                }
                if (m == "MoveRotation") { if (g->transform) g->transform->localRotation = Quat::Euler({0, 0, arg(0).AsFloat()}); return Value{}; }
                if (m == "WakeUp") { rb->WakeUp(); return Value{}; }
                if (m == "Sleep") { rb->sleeping = true; return Value{}; }
                if (m == "IsSleeping") return Value{rb->sleeping};
                if (m == "IsAwake") return Value{!rb->sleeping};
                if (m != "AddExplosionForce" && m != "MovePosition")
                    throw ScriptError("Rigidbody2D has no method '" + m + "'");
                if (m == "AddExplosionForce") {
                    float up = 0.0f;
                    Vec2 c; float force = 0.0f, radius = 0.0f;
                    if (arg(0).IsVec3()) {
                        Vec3 v = arg(0).AsVec3();
                        c = {v.x, v.y};
                        force = rest.size() > 1 ? arg(1).AsFloat() : 0.0f;
                        radius = rest.size() > 2 ? arg(2).AsFloat() : 0.0f;
                        up = rest.size() > 3 ? arg(3).AsFloat() : 0.0f;
                    } else {
                        c = {arg(0).AsFloat(), rest.size() > 1 ? arg(1).AsFloat() : 0.0f};
                        force = rest.size() > 2 ? arg(2).AsFloat() : 0.0f;
                        radius = rest.size() > 3 ? arg(3).AsFloat() : 0.0f;
                        up = rest.size() > 4 ? arg(4).AsFloat() : 0.0f;
                    }
                    rb->AddExplosionForce(force, c, radius, up);
                }
                if (m == "MovePosition" && g->transform) g->transform->localPosition = {f.x, f.y, g->transform->localPosition.z};
                return Value{};
            }
            if (t == "Rigidbody") if (auto* rb = g->GetComponent<Rigidbody3D>()) {
                // #888 F109: only AddForce (without its ForceMode) and MovePosition worked; the rest
                // returned null without a word. A force is a vector or three numbers, then an optional
                // ForceMode (Force 0, Acceleration 1, Impulse 2, VelocityChange 3).
                const bool vecFirst = arg(0).IsVec3();
                Vec3 f = vecFirst ? arg(0).AsVec3() : Vec3{arg(0).AsFloat(), arg(1).AsFloat(), arg(2).AsFloat()};
                const std::size_t next = vecFirst ? 1 : 3;   // the argument after the force
                auto modeAt = [&](std::size_t i) {
                    const int md = rest.size() > i ? SafeInt(arg(i).AsFloat()) : 0;
                    return md == 1 ? ForceMode::Acceleration : md == 2 ? ForceMode::Impulse
                         : md == 3 ? ForceMode::VelocityChange : ForceMode::Force;
                };
                auto local = [&](const Vec3& v) { return g->transform ? g->transform->Rotation() * v : v; };
                // Torque: Force adds to the step's torque; an Impulse is spread over one physics
                // step; Acceleration and VelocityChange change the spin directly (radians per second).
                auto torque = [&](const Vec3& tq, ForceMode md) {
                    const float step = Time::FixedDeltaTime() > 0.0f ? Time::FixedDeltaTime() : 1.0f / 60.0f;
                    if (md == ForceMode::Force) rb->AddTorque(tq);
                    else if (md == ForceMode::Impulse) rb->AddTorque(tq * (1.0f / step));
                    else if (md == ForceMode::Acceleration) { rb->angularVelocity = rb->angularVelocity + tq * step; rb->WakeUp(); }
                    else { rb->angularVelocity = rb->angularVelocity + tq; rb->WakeUp(); }
                };
                if (m == "AddForce") { rb->AddForce(f, modeAt(next)); return Value{}; }
                if (m == "AddRelativeForce") { rb->AddForce(local(f), modeAt(next)); return Value{}; }
                if (m == "AddImpulse") { rb->AddImpulse(f); return Value{}; }
                if (m == "AddTorque") { torque(f, modeAt(next)); return Value{}; }
                if (m == "AddRelativeTorque") { torque(local(f), modeAt(next)); return Value{}; }
                if (m == "AddForceAtPosition") {   // (force, point [, mode]); Impulse / VelocityChange kick at once
                    const Vec3 pt = vecFirst ? arg(1).AsVec3() : Vec3{arg(3).AsFloat(), arg(4).AsFloat(), arg(5).AsFloat()};
                    const ForceMode md = modeAt(vecFirst ? 2 : 6);
                    const float step = Time::FixedDeltaTime() > 0.0f ? Time::FixedDeltaTime() : 1.0f / 60.0f;
                    const float scale = (md == ForceMode::Impulse || md == ForceMode::VelocityChange) ? 1.0f / step : 1.0f;
                    rb->AddForceAtPosition(f * scale, pt);
                    return Value{};
                }
                if (m == "AddExplosionForce") {   // (force, center, radius [, upwards])
                    const Vec3 c = arg(1).AsVec3();
                    rb->AddExplosionForce(arg(0).AsFloat(), c, arg(2).AsFloat(), rest.size() > 3 ? arg(3).AsFloat() : 0.0f);
                    return Value{};
                }
                if (m == "MovePosition") { if (g->transform) g->transform->localPosition = f; return Value{}; }
                if (m == "MoveRotation") { if (g->transform) g->transform->localRotation = Quat::Euler(arg(0).AsVec3()); return Value{}; }
                if (m == "WakeUp") { rb->WakeUp(); return Value{}; }
                if (m == "Sleep") { rb->sleeping = true; return Value{}; }
                if (m == "IsSleeping") return Value{rb->sleeping};
                if (m == "IsAwake") return Value{!rb->sleeping};
                throw ScriptError("Rigidbody has no method '" + m + "'");
            }
            if (t == "AudioSource") if (auto* au = g->GetComponent<AudioSource>()) {
                if (m == "Play") au->Play();
                if (m == "Stop") au->Stop();
                if (m == "Pause") au->Pause();
                if (m == "Resume") au->Resume();
                if (m == "PlayOneShot") au->PlayOneShot(rest.empty() ? 1.0f : arg(0).AsFloat());
                if (m == "FadeIn") au->FadeIn(rest.empty() ? 1.0f : arg(0).AsFloat());
                if (m == "FadeOut") au->FadeOut(rest.empty() ? 1.0f : arg(0).AsFloat());
                return Value{};
            }
            if (t == "NetworkObject") if (auto* o = g->GetComponent<NetworkObject>()) {
                if (m == "Rpc" && !rest.empty()) o->Rpc(arg(0).AsString(), rest.size() > 1 ? arg(1).AsString() : std::string{});
                if (m == "RpcTo" && rest.size() >= 2) o->RpcTo(SafeUInt32(arg(0).AsFloat()), arg(1).AsString(), rest.size() > 2 ? arg(2).AsString() : std::string{});
                if (m == "RpcReliable" && !rest.empty()) o->RpcReliable(arg(0).AsString(), rest.size() > 1 ? arg(1).AsString() : std::string{});
                return Value{};
            }
            if (t == "SyncVar") if (auto* sv = g->GetComponent<SyncVar>()) {
                if (m == "Set") sv->Set(arg(0).AsString());
                if (m == "Get") return Value{sv->Get()};
                return Value{};
            }
            if (t == "FirstPersonHands") { FpHandsCall(g->GetComponent<FirstPersonHands>(), m); return Value{}; }
            if (t == "Transform") {
                std::vector<Value> x = rest;
                return rt.Call("transform." + m, x);
            }
            return Value{};
        };
        b["ForceMode2D.Impulse"] = [](std::vector<Value>&) { return Value{1.0f}; };
}

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
