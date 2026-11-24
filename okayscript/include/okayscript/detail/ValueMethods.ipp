#pragma once
// Properties and methods on OkayScript values (s.Length, xs.Add(3), m.ContainsKey("a"), v.magnitude, n.ToString("F2")),
// for any value type with the script-value interface. Moved from the engine's OkayScriptVM.cpp (ValueProperty /
// CallValueMethod), written for this project; only Value became the template parameter V.
#include "okayscript/detail/Support.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace okayscript {
namespace detail {

template <class V>
bool ValueProperty(const V& v, const std::string& prop, V& out) {
    if (v.IsObject()) return ValueProperty<V>(V{v.AsString()}, prop, out);   // other.Length: its name's
    if (v.IsString()) {
        if (prop == "Length" || prop == "length") { out = V{(float)v.AsString().size()}; return true; }
        return false;
    }
    if (v.IsArray()) {
        if (prop == "Length" || prop == "length" || prop == "Count" || prop == "count") {
            auto a = v.AsArray(); out = V{a ? (float)a->size() : 0.0f}; return true;
        }
        return false;
    }
    if (v.IsMap()) {
        auto m = v.AsMap();
        if (m) if (auto it = m->find(prop); it != m->end()) { out = it->second; return true; }   // kv.Key / kv.V
        if (prop == "Count" || prop == "count") { out = V{m ? (float)m->size() : 0.0f}; return true; }
        if (prop == "Keys" || prop == "Values") {
            out = V::MakeArray();
            if (m) for (auto& kv : *m) out.AsArray()->push_back(prop == "Keys" ? V{kv.first} : kv.second);
            return true;
        }
        return false;
    }
    using Vec = VecOf<V>;
    const Vec p = v.AsVec3();
    if (prop == "x") { out = V{p.x}; return true; }
    if (prop == "y") { out = V{p.y}; return true; }
    if (prop == "z") { out = V{p.z}; return true; }
    if (prop == "magnitude")    { out = V{p.Magnitude()}; return true; }
    if (prop == "sqrMagnitude") { out = V{p.x * p.x + p.y * p.y + p.z * p.z}; return true; }
    if (prop == "normalized") { float m = p.Magnitude(); out = V{m > 1e-6f ? p * (1.0f / m) : Vec::Zero}; return true; }
    return false;
}
// s.ToUpper(), xs.Add(3), d.ContainsKey("a"), n.ToString("F2") ... Arrays and
// maps are shared references, so Add/Remove/Clear change the variable's value.
template <class V>
bool CallValueMethod(const V& v, const std::string& m, std::vector<V>& a, V& out,
                     const LambdaInvoker<V>* invoke) {
    auto arg = [&](std::size_t i) { return i < a.size() ? a[i] : V{}; };
    out = V{};
    auto isFn = [&](std::size_t i) { return invoke && i < a.size() && IsLambdaValue(a[i]); };
    auto call1 = [&](const V& x) { std::vector<V> xs{x}; return (*invoke)(a[0], xs); };
    if (m == "Invoke" && invoke && IsLambdaValue(v)) { out = (*invoke)(v, a); return true; }
    if (m == "ToString") {
        if (v.IsNumber() && !a.empty()) {          // "F2" / "N0" / "0.00"
            std::string f = a[0].AsString(); int dec = -1;
            if (!f.empty() && (f[0] == 'F' || f[0] == 'f' || f[0] == 'N' || f[0] == 'n'))
                dec = f.size() > 1 ? std::atoi(f.c_str() + 1) : 2;
            else if (auto d = f.find('.'); d != std::string::npos) dec = (int)(f.size() - d - 1);
            else if (!f.empty() && f.find_first_not_of('0') == std::string::npos) dec = 0;
            if (dec >= 0) { char buf[64]; std::snprintf(buf, sizeof buf, "%.*f", dec > 9 ? 9 : dec, v.AsFloat()); out = V{std::string(buf)}; return true; }
        }
        out = V{v.AsString()}; return true;
    }
    if (m == "Equals")    { out = V{ValuesEqual(v, arg(0))}; return true; }
    if (m == "CompareTo") { float x = v.AsFloat(), y = arg(0).AsFloat(); out = V{x < y ? -1.0f : (x > y ? 1.0f : 0.0f)}; return true; }
    if (v.IsString()) {
        const std::string str = v.AsString();
        auto up = [](std::string x, bool u) { for (char& c : x) c = (char)(u ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); return x; };
        auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        if (m == "ToUpper" || m == "ToUpperInvariant") { out = V{up(str, true)}; return true; }
        if (m == "ToLower" || m == "ToLowerInvariant") { out = V{up(str, false)}; return true; }
        if (m == "Contains")   { out = V{str.find(arg(0).AsString()) != std::string::npos}; return true; }
        if (m == "StartsWith") { out = V{str.rfind(arg(0).AsString(), 0) == 0}; return true; }
        if (m == "EndsWith")   { std::string e = arg(0).AsString(); out = V{str.size() >= e.size() && str.compare(str.size() - e.size(), e.size(), e) == 0}; return true; }
        if (m == "IndexOf")    { auto p = str.find(arg(0).AsString()); out = V{p == std::string::npos ? -1.0f : (float)p}; return true; }
        if (m == "LastIndexOf"){ auto p = str.rfind(arg(0).AsString()); out = V{p == std::string::npos ? -1.0f : (float)p}; return true; }
        if (m == "Substring") {
            int st = SafeInt(arg(0).AsFloat()); if (st < 0) st = 0; if (st > (int)str.size()) st = (int)str.size();
            int n = a.size() > 1 ? SafeInt(a[1].AsFloat()) : (int)str.size() - st; if (n < 0) n = 0;
            out = V{str.substr((std::size_t)st, (std::size_t)n)}; return true;
        }
        if (m == "Replace") {
            std::string from = arg(0).AsString(), to = arg(1).AsString(), r = str;
            if (!from.empty()) for (std::size_t p = 0; (p = r.find(from, p)) != std::string::npos; p += to.size()) r.replace(p, from.size(), to);
            out = V{r}; return true;
        }
        if (m == "Trim" || m == "TrimStart" || m == "TrimEnd") {
            std::size_t b = 0, e = str.size();
            if (m != "TrimEnd")   while (b < e && ws(str[b])) ++b;
            if (m != "TrimStart") while (e > b && ws(str[e - 1])) --e;
            out = V{str.substr(b, e - b)}; return true;
        }
        if (m == "Split") {
            std::string sep = a.empty() ? " " : a[0].AsString();
            out = V::MakeArray(); auto arr = out.AsArray();
            if (sep.empty()) { arr->push_back(V{str}); return true; }
            std::size_t p = 0, q;
            while ((q = str.find(sep, p)) != std::string::npos) { arr->push_back(V{str.substr(p, q - p)}); p = q + sep.size(); }
            arr->push_back(V{str.substr(p)}); return true;
        }
        if (m == "PadLeft" || m == "PadRight") {
            int w = SafeInt(arg(0).AsFloat()); char c = a.size() > 1 && !a[1].AsString().empty() ? a[1].AsString()[0] : ' ';
            std::string r = str;
            if ((int)r.size() < w) r = m == "PadLeft" ? std::string(w - r.size(), c) + r : r + std::string(w - r.size(), c);
            out = V{r}; return true;
        }
        if (m == "Insert") { std::size_t p = SafeSize(arg(0).AsFloat()); std::string r = str; r.insert(std::min(p, r.size()), arg(1).AsString()); out = V{r}; return true; }
        if (m == "Remove") {
            std::size_t p = SafeSize(arg(0).AsFloat()); std::string r = str;
            if (p < r.size()) r.erase(p, a.size() > 1 ? SafeSize(a[1].AsFloat()) : std::string::npos);
            out = V{r}; return true;
        }
        return false;
    }
    if (v.IsArray()) {
        auto arr = v.AsArray(); if (!arr) return false;
        auto find = [&](const V& x) { for (std::size_t i = 0; i < arr->size(); ++i) if (ValuesEqual((*arr)[i], x)) return (int)i; return -1; };
        // LINQ-style helpers that take a lambda: xs.Where(x => x > 2).Select(x => x * 10).ToList()
        if (isFn(0)) {
            const auto items = *arr;   // snapshot: the lambda may change the list
            if (m == "ForEach") { for (auto& x : items) call1(x); return true; }
            if (m == "Where" || m == "FindAll") { out = V::MakeArray(); for (auto& x : items) if (call1(x).AsBool()) out.AsArray()->push_back(x); return true; }
            if (m == "Select") { out = V::MakeArray(); for (auto& x : items) out.AsArray()->push_back(call1(x)); return true; }
            if (m == "Find" || m == "First" || m == "FirstOrDefault") { for (auto& x : items) if (call1(x).AsBool()) { out = x; return true; } return true; }
            if (m == "FindIndex") { for (std::size_t i = 0; i < items.size(); ++i) if (call1(items[i]).AsBool()) { out = V{(float)i}; return true; } out = V{-1.0f}; return true; }
            if (m == "Any" || m == "Exists") { bool r = false; for (auto& x : items) if (call1(x).AsBool()) { r = true; break; } out = V{r}; return true; }
            if (m == "All" || m == "TrueForAll") { bool r = true; for (auto& x : items) if (!call1(x).AsBool()) { r = false; break; } out = V{r}; return true; }
            if (m == "Count") { float n = 0; for (auto& x : items) if (call1(x).AsBool()) n += 1; out = V{n}; return true; }
            if (m == "RemoveAll") {
                float n = 0; arr->clear();
                for (auto& x : items) { if (call1(x).AsBool()) n += 1; else arr->push_back(x); }
                out = V{n}; return true;
            }
            if (m == "Sum" || m == "Max" || m == "Min" || m == "Average") {
                std::vector<V> mapped; for (auto& x : items) mapped.push_back(call1(x));
                V tmp = V::MakeArray(); *tmp.AsArray() = mapped;
                std::vector<V> none;
                return CallValueMethod<V>(tmp, m, none, out, nullptr);
            }
            if (m == "OrderBy" || m == "OrderByDescending") {
                std::vector<std::pair<V, V>> keyed; for (auto& x : items) keyed.push_back({call1(x), x});
                const bool desc = m == "OrderByDescending";
                std::stable_sort(keyed.begin(), keyed.end(), [&](const auto& p, const auto& q) {
                    bool lt = (p.first.IsString() || q.first.IsString()) ? p.first.AsString() < q.first.AsString() : p.first.AsFloat() < q.first.AsFloat();
                    bool gt = (p.first.IsString() || q.first.IsString()) ? q.first.AsString() < p.first.AsString() : q.first.AsFloat() < p.first.AsFloat();
                    return desc ? gt : lt; });
                out = V::MakeArray(); for (auto& kq : keyed) out.AsArray()->push_back(kq.second);
                return true;
            }
            if (m == "Sort") {   // xs.Sort((a, b) => a - b)
                std::vector<V> sorted = items;
                std::stable_sort(sorted.begin(), sorted.end(), [&](const V& x, const V& y) {
                    std::vector<V> xy{x, y}; return (*invoke)(a[0], xy).AsFloat() < 0.0f; });
                *arr = sorted; return true;
            }
        }
        if (m == "Add" || m == "Push" || m == "Enqueue") { arr->push_back(arg(0)); return true; }
        if (m == "AddRange") { if (auto o = arg(0).AsArray()) { auto copy = *o; arr->insert(arr->end(), copy.begin(), copy.end()); } return true; }
        if (m == "Insert")   { int i = SafeInt(arg(0).AsFloat()); i = std::max(0, std::min(i, (int)arr->size())); arr->insert(arr->begin() + i, arg(1)); return true; }
        if (m == "Remove")   { int i = find(arg(0)); if (i >= 0) arr->erase(arr->begin() + i); out = V{i >= 0}; return true; }
        if (m == "RemoveAt") { int i = SafeInt(arg(0).AsFloat()); if (i >= 0 && i < (int)arr->size()) arr->erase(arr->begin() + i); return true; }
        if (m == "Contains") { out = V{find(arg(0)) >= 0}; return true; }
        if (m == "IndexOf")  { out = V{(float)find(arg(0))}; return true; }
        if (m == "Clear")    { arr->clear(); return true; }
        if (m == "Pop")      { if (!arr->empty()) { out = arr->back(); arr->pop_back(); } return true; }
        if (m == "Dequeue")  { if (!arr->empty()) { out = arr->front(); arr->erase(arr->begin()); } return true; }
        if (m == "First")    { if (!arr->empty()) out = arr->front(); return true; }
        if (m == "Last")     { if (!arr->empty()) out = arr->back(); return true; }
        if (m == "Count")    { out = V{(float)arr->size()}; return true; }
        if (m == "Reverse")  { std::reverse(arr->begin(), arr->end()); return true; }
        if (m == "Sort") {
            bool nums = true; for (auto& x : *arr) nums = nums && !x.IsString();
            std::stable_sort(arr->begin(), arr->end(), [&](const V& x, const V& y) {
                return nums ? x.AsFloat() < y.AsFloat() : x.AsString() < y.AsString(); });
            return true;
        }
        if (m == "ToArray" || m == "ToList") { out = V::MakeArray(); *out.AsArray() = *arr; return true; }
        if (m == "Sum" || m == "Average" || m == "Max" || m == "Min") {
            if (arr->empty()) { out = V{0.0f}; return true; }
            float acc = m == "Max" ? -1e30f : (m == "Min" ? 1e30f : 0.0f);
            for (auto& x : *arr) { float f = x.AsFloat(); acc = m == "Max" ? std::max(acc, f) : (m == "Min" ? std::min(acc, f) : acc + f); }
            out = V{m == "Average" ? acc / (float)arr->size() : acc}; return true;
        }
        return false;
    }
    if (v.IsMap()) {
        auto mp = v.AsMap(); if (!mp) return false;
        if (m == "Add" || m == "TryAdd") { auto k = arg(0).AsString(); bool fresh = !mp->count(k); if (fresh) (*mp)[k] = arg(1); out = V{fresh}; return true; }
        if (m == "ContainsKey") { out = V{mp->count(arg(0).AsString()) > 0}; return true; }
        if (m == "ContainsValue") { bool f = false; for (auto& kv : *mp) f = f || ValuesEqual(kv.second, arg(0)); out = V{f}; return true; }
        if (m == "Remove") { out = V{mp->erase(arg(0).AsString()) > 0}; return true; }
        if (m == "Clear")  { mp->clear(); return true; }
        if (m == "GetValueOrDefault") { auto it = mp->find(arg(0).AsString()); out = it != mp->end() ? it->second : arg(1); return true; }
        return false;
    }
    return false;
}

} // namespace detail
} // namespace okayscript
