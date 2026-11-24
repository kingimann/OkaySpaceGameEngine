#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterEasingBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Easing curves: map a normalized time t (0..1) to an eased 0..1 ----
        // For tweening/animation. Pass the raw progress; multiply the result into a
        // range yourself, or use with lerp(a, b, ease_out(t)).
        // A NaN counts as 0 (Easing::Evaluate does the same), so a bad value cannot hand a script NaN to write into an object.
        auto E = [](std::vector<Value>& a) { const float t = a.empty() ? 0.0f : a[0].AsFloat(); return t == t ? Mathf::Clamp01(t) : 0.0f; };
        b["ease_in"]  = [E](std::vector<Value>& a) { float t = E(a); return Value{t * t}; };
        b["ease_out"] = [E](std::vector<Value>& a) { float t = E(a); return Value{1.0f - (1.0f - t) * (1.0f - t)}; };
        b["ease_in_out"] = [E](std::vector<Value>& a) {
            float t = E(a);
            return Value{t < 0.5f ? 2.0f * t * t : 1.0f - 0.5f * (2.0f - 2.0f * t) * (2.0f - 2.0f * t)};
        };
        b["ease_in_cubic"]  = [E](std::vector<Value>& a) { float t = E(a); return Value{t * t * t}; };
        b["ease_out_cubic"] = [E](std::vector<Value>& a) { float t = 1.0f - E(a); return Value{1.0f - t * t * t}; };
        b["ease_in_out_cubic"] = [E](std::vector<Value>& a) {
            float t = E(a);
            if (t < 0.5f) return Value{4.0f * t * t * t};
            float f = -2.0f * t + 2.0f;
            return Value{1.0f - 0.5f * f * f * f};
        };
        b["ease_back"] = [E](std::vector<Value>& a) {   // slight overshoot backward then in
            // dup-allow: the ease_* builtin moved here unchanged from OkayScriptVM.cpp (S2); not a new copy
            float t = E(a); const float c1 = 1.70158f, c3 = c1 + 1.0f;
            return Value{c3 * t * t * t - c1 * t * t};
        };
        b["ease_elastic"] = [E](std::vector<Value>& a) {  // springy ease-out
            float t = E(a);
            if (t == 0.0f || t == 1.0f) return Value{t};
            const float c4 = 2.0944f;  // (2*pi)/3
            return Value{Mathf::Pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f};
        };
        b["ease_bounce"] = [E](std::vector<Value>& a) {   // bounce ease-out
            float t = E(a); const float n1 = 7.5625f, d1 = 2.75f;
            if (t < 1.0f / d1)      return Value{n1 * t * t};
            else if (t < 2.0f / d1) { t -= 1.5f / d1;  return Value{n1 * t * t + 0.75f}; }
            else if (t < 2.5f / d1) { t -= 2.25f / d1; return Value{n1 * t * t + 0.9375f}; }
            else                    { t -= 2.625f / d1; return Value{n1 * t * t + 0.984375f}; }
        };
        // ease_curve(t, "OutBack" | "cubic-bezier(0.4,0,0.2,1)" | "steps(4)" | "ease-in-out"): any ease by name or as a
        // curve (the text tween_ease takes). Text it does not know is a straight line; a curve may overshoot 0..1.
        b["ease_curve"] = [E](std::vector<Value>& a) {
            const float t = E(a);
            EaseCurve c;
            if (a.size() > 1 && a[1].IsString() && EaseCurve::Parse(a[1].AsString(), c)) return Value{c.Evaluate(t)};
            return Value{t};
        };
        // Loop t into [0,length) (Mathf.Repeat).
        b["math_repeat"] = [](std::vector<Value>& a) {
            float t = a.size() > 0 ? a[0].AsFloat() : 0, len = a.size() > 1 ? a[1].AsFloat() : 1;
            if (len <= 0.0f) return Value{0.0f};
            float r = std::fmod(t, len); if (r < 0) r += len;
            return Value{r};
        };
        // Shortest signed difference between two angles, in degrees (Mathf.DeltaAngle).
        b["delta_angle"] = [](std::vector<Value>& a) {
            float from = a.size() > 0 ? a[0].AsFloat() : 0, to = a.size() > 1 ? a[1].AsFloat() : 0;
            float d = std::fmod(to - from, 360.0f);
            if (d < -180.0f) d += 360.0f; else if (d > 180.0f) d -= 360.0f;
            return Value{d};
        };
        // Interpolate angles taking the shortest path (Mathf.LerpAngle).
        b["lerp_angle"] = [](std::vector<Value>& a) {
            float from = a.size() > 0 ? a[0].AsFloat() : 0, to = a.size() > 1 ? a[1].AsFloat() : 0,
                  t = a.size() > 2 ? a[2].AsFloat() : 0;
            float d = std::fmod(to - from, 360.0f);
            if (d < -180.0f) d += 360.0f; else if (d > 180.0f) d -= 360.0f;
            return Value{from + d * t};
        };
        // Nearly equal (Mathf.Approximately).
        b["approximately"] = [](std::vector<Value>& a) {
            float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0;
            return Value{std::fabs(x - y) < 1e-4f};
        };
        // Blend two colors/vectors by t (Color.Lerp / Vector3.Lerp on whole values).
        b["lerp_color"] = [](std::vector<Value>& a) {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 c1 = a[0].AsVec3(), c2 = a[1].AsVec3(); float t = Mathf::Clamp01(a[2].AsFloat());
            return Value{c1 + (c2 - c1) * t};
        };
        // Angle in degrees from (x1,y1) toward (x2,y2).
        b["angle_to"] = [](std::vector<Value>& a) {
            if (a.size() < 4) return Value{0.0f};
            float dx = a[2].AsFloat() - a[0].AsFloat(), dy = a[3].AsFloat() - a[1].AsFloat();
            return Value{std::atan2(dy, dx) * 57.2957795f};
        };
        // True with probability p (0..1) — random events, drops, spawns.
        b["chance"] = [](std::vector<Value>& a) {
            float p = a.empty() ? 0.5f : a[0].AsFloat();
            return Value{Random::Shared().Range(0.0f, 1.0f) < p};
        };
        // Arrays/lists.
        b["array"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            for (auto& e : a) arr->push_back(e); // array(1,2,3) -> [1,2,3]
            return v;
        };
        b["count"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) return Value{(float)arr->size()};
            return Value{0.0f};
        };
        b["push"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) arr->push_back(a[1]);
            return a.empty() ? Value{} : a[0];
        };
        b["pop"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                if (!arr->empty()) { Value last = arr->back(); arr->pop_back(); return last; }
            return Value{};
        };
        b["contains"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string n = a[1].AsString();
                for (auto& e : *arr) if (e.AsString() == n) return Value{true};
            }
            return Value{false};
        };
        b["index_of"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string n = a[1].AsString();
                for (std::size_t i = 0; i < arr->size(); ++i)
                    if ((*arr)[i].AsString() == n) return Value{(float)i};
            }
            return Value{-1.0f};
        };
        b["remove_at"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                int i = SafeInt(a[1].AsFloat());
                if (i >= 0 && i < (int)arr->size()) arr->erase(arr->begin() + i);
            }
            return a.empty() ? Value{} : a[0];
        };
        b["sum"] = [](std::vector<Value>& a) {
            float s = 0;
            if (!a.empty()) if (auto arr = a[0].AsArray()) for (auto& e : *arr) s += e.AsFloat();
            return Value{s};
        };
        b["min_of"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) {
                float m = (*arr)[0].AsFloat();
                for (auto& e : *arr) m = Mathf::Min(m, e.AsFloat());
                return Value{m};
            }
            return Value{0.0f};
        };
        b["max_of"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) {
                float m = (*arr)[0].AsFloat();
                for (auto& e : *arr) m = Mathf::Max(m, e.AsFloat());
                return Value{m};
            }
            return Value{0.0f};
        };
        b["reverse"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) std::reverse(arr->begin(), arr->end());
            return a.empty() ? Value{} : a[0];
        };
        b["sort_num"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                std::sort(arr->begin(), arr->end(),
                          [](const Value& x, const Value& y) { return x.AsFloat() < y.AsFloat(); });
            return a.empty() ? Value{} : a[0];
        };
        b["choose"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty())
                return (*arr)[(std::size_t)Random::Shared().Range(0, (int)arr->size())];   // max is exclusive
            return Value{};
        };
        b["shuffle"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                for (int i = (int)arr->size() - 1; i > 0; --i)
                    std::swap((*arr)[i], (*arr)[Random::Shared().Range(0, i + 1)]);   // 0..i inclusive: a Fisher-Yates step
            return a.empty() ? Value{} : a[0];
        };
        b["randi"] = [](std::vector<Value>& a) {
            int lo = a.size() > 0 ? SafeInt(a[0].AsFloat()) : 0;
            int hi = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 1;
            if (hi < lo) std::swap(lo, hi);
            return Value{(float)Random::Shared().Range(lo, hi)};
        };
        b["move_toward"] = [](std::vector<Value>& a) {
            float cur = a.size() > 0 ? a[0].AsFloat() : 0, tgt = a.size() > 1 ? a[1].AsFloat() : 0;
            float maxD = a.size() > 2 ? Mathf::Abs(a[2].AsFloat()) : 0;
            if (Mathf::Abs(tgt - cur) <= maxD) return Value{tgt};
            return Value{cur + (tgt > cur ? maxD : -maxD)};
        };
        // More array helpers.
        b["first"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) return arr->front();
            return Value{};
        };
        b["last"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) return arr->back();
            return Value{};
        };
        b["clear"] = [](std::vector<Value>& a) {
            if (!a.empty()) { if (auto arr = a[0].AsArray()) arr->clear(); else if (auto m = a[0].AsMap()) m->clear(); }
            return a.empty() ? Value{} : a[0];
        };
        b["insert_at"] = [](std::vector<Value>& a) {
            if (a.size() >= 3) if (auto arr = a[0].AsArray()) {
                int i = SafeInt(a[1].AsFloat());
                if (i < 0) i = 0; if (i > (int)arr->size()) i = (int)arr->size();
                arr->insert(arr->begin() + i, a[2]);
            }
            return a.empty() ? Value{} : a[0];
        };
        b["slice"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (!a.empty()) if (auto arr = a[0].AsArray()) {
                int n = (int)arr->size();
                int start = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0;
                int end   = a.size() > 2 ? SafeInt(a[2].AsFloat()) : n;
                if (start < 0) start += n; if (end < 0) end += n;
                start = start < 0 ? 0 : (start > n ? n : start);
                end   = end   < 0 ? 0 : (end   > n ? n : end);
                for (int i = start; i < end; ++i) res->push_back((*arr)[i]);
            }
            return out;
        };
        b["range"] = [](std::vector<Value>& a) {
            // range(n) -> [0..n), range(lo, hi) -> [lo..hi), range(lo, hi, step).
            Value out = Value::MakeArray(); auto res = out.AsArray();
            float lo = 0, hi = 0, step = 1;
            if (a.size() == 1) hi = a[0].AsFloat();
            else if (a.size() >= 2) { lo = a[0].AsFloat(); hi = a[1].AsFloat(); if (a.size() >= 3) step = a[2].AsFloat(); }
            if (step == 0) step = 1;
            int guard = 0;
            if (step > 0) for (float v = lo; v < hi && guard < 1000000; v += step, ++guard) res->push_back(Value{v});
            else          for (float v = lo; v > hi && guard < 1000000; v += step, ++guard) res->push_back(Value{v});
            return out;
        };
        b["sort_str"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                std::sort(arr->begin(), arr->end(),
                          [](const Value& x, const Value& y) { return x.AsString() < y.AsString(); });
            return a.empty() ? Value{} : a[0];
        };
        // More map helpers.
        b["map_values"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (!a.empty()) if (auto m = a[0].AsMap()) for (auto& kv : *m) res->push_back(kv.second);
            return out;
        };
        b["map_clear"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto m = a[0].AsMap()) m->clear();
            return a.empty() ? Value{} : a[0];
        };
        b["map_merge"] = [](std::vector<Value>& a) {
            // map_merge(dst, src): copy src's entries into dst (src wins on conflict).
            if (a.size() >= 2) if (auto d = a[0].AsMap()) if (auto s = a[1].AsMap())
                for (auto& kv : *s) (*d)[kv.first] = kv.second;
            return a.empty() ? Value{} : a[0];
        };
        // Type introspection: what kind of value is this?
        b["typeof"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string("null")};
            const Value& v = a[0];
            if (v.IsArray())  return Value{std::string("array")};
            if (v.IsMap())    return Value{std::string("map")};
            if (v.IsString()) return Value{std::string("string")};
            if (v.IsBool())   return Value{std::string("bool")};
            if (v.IsVec3())   return Value{std::string("vec3")};
            return Value{std::string("number")};
        };
        b["is_array"] = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsArray()}; };
        b["is_map"]   = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsMap()}; };
        b["is_str"]   = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsString()}; };
        b["is_bool"]  = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsBool()}; };
        b["is_num"]   = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsNumber()}; };
        // fract(x): the fractional part of x (x - floor(x)).
        b["fract"] = [](std::vector<Value>& a) {
            float x = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{x - Mathf::Floor(x)};
        };
}

void OkayScriptVM::Impl::RegisterBitsJsonStringBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Bitwise / integer helpers (flags, masks, tile ids, packing) ------
        // Values are treated as 32-bit integers. Handy for bit flags and tilemaps.
        auto I = [](std::vector<Value>& a, std::size_t i) -> std::int32_t {
            return i < a.size() ? (std::int32_t)a[i].AsFloat() : 0;
        };
        b["bit_and"] = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) & I(a,1))}; };
        b["bit_or"]  = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) | I(a,1))}; };
        b["bit_xor"] = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) ^ I(a,1))}; };
        b["bit_not"] = [I](std::vector<Value>& a) { return Value{(float)(~I(a,0))}; };
        b["shl"]     = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) << (I(a,1) & 31))}; };
        b["shr"]     = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) >> (I(a,1) & 31))}; };
        // Set / clear / toggle / test a single bit (bit index 0..31).
        b["bit_set"]    = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) |  (1 << (I(a,1) & 31)))}; };
        b["bit_clear"]  = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) & ~(1 << (I(a,1) & 31)))}; };
        b["bit_toggle"] = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) ^  (1 << (I(a,1) & 31)))}; };
        b["bit_test"]   = [I](std::vector<Value>& a) { return Value{(I(a,0) & (1 << (I(a,1) & 31))) != 0}; };
        // hex(n) -> "0x1f" ; parse_int("ff", 16) -> 255 (base defaults to 10).
        b["hex"] = [I](std::vector<Value>& a) {
            char buf[24]; std::snprintf(buf, sizeof(buf), "0x%x", (unsigned)I(a, 0));
            return Value{std::string(buf)};
        };
        b["parse_int"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            int base = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 10;
            return Value{(float)std::strtol(s.c_str(), nullptr, base)};
        };
        // JSON: serialize any value (arrays/maps included) to a string, and parse a
        // JSON string back into arrays/maps/numbers/strings/bools — for save data,
        // config, and network payloads. Aliased as json_stringify / json_parse.
        b["to_json"] = [](std::vector<Value>& a) {
            std::string out; if (!a.empty()) ToJson(a[0], out); else out = "null";
            return Value{out};
        };
        b["from_json"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            std::string js = a[0].AsString();   // keep alive: JsonReader holds a reference
            JsonReader r(js);
            return r.Parse();
        };
        // Higher-order array ops: each takes an array and the NAME of a script
        // function to apply per element, so games can write functional-style code
        // (map/filter/reduce) with a named callback. (call() lives further down.)
        // Each walks a copy of the array: the callback runs script, which may push to, clear or
        // remove from the very array being walked, and that must not pull it out from under the loop.
        b["map_fn"] = [this](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{e}; res->push_back(rt.Call(fn, ca)); }
            }
            return out;
        };
        b["filter_fn"] = [this](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) res->push_back(e); }
            }
            return out;
        };
        b["reduce_fn"] = [this](std::vector<Value>& a) {
            // reduce_fn(arr, "fn", init): acc = fn(acc, element) folded left.
            if (a.size() < 2) return Value{};
            Value acc = a.size() >= 3 ? a[2] : Value{};
            if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{acc, e}; acc = rt.Call(fn, ca); }
            }
            return acc;
        };
        b["for_each"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{e}; rt.Call(fn, ca); }
            }
            return a.empty() ? Value{} : a[0];
        };
        b["find_fn"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) return e; }
            }
            return Value{};
        };
        b["any_fn"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) return Value{true}; }
            }
            return Value{false};
        };
        b["all_fn"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{e}; if (!rt.Call(fn, ca).AsBool()) return Value{false}; }
            }
            return Value{true};
        };
        b["count_fn"] = [this](std::vector<Value>& a) {
            float n = 0;
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (const Value& e : std::vector<Value>(*arr)) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) n += 1.0f; }
            }
            return Value{n};
        };
        // String helpers.
        b["str_len"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : (float)a[0].AsString().size()}; };
        b["upper"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            for (auto& c : s) c = (char)std::toupper((unsigned char)c);
            return Value{s};
        };
        b["lower"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            for (auto& c : s) c = (char)std::tolower((unsigned char)c);
            return Value{s};
        };
        b["substr"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            int start = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0;
            int n = a.size() > 2 ? SafeInt(a[2].AsFloat()) : (int)s.size();
            if (start < 0) start = 0;
            if (start > (int)s.size()) return Value{std::string{}};
            if (n < 0) n = 0;
            return Value{s.substr(start, n)};
        };
        b["char_at"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            int i = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0;
            if (i < 0 || i >= (int)s.size()) return Value{std::string{}};
            return Value{std::string(1, s[i])};
        };
        b["str_find"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{-1.0f};
            auto pos = a[0].AsString().find(a[1].AsString());
            return Value{pos == std::string::npos ? -1.0f : (float)pos};
        };
        b["str_contains"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{false};
            return Value{a[0].AsString().find(a[1].AsString()) != std::string::npos};
        };
        b["starts_with"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{false};
            const std::string s = a[0].AsString(), p = a[1].AsString();
            return Value{s.size() >= p.size() && s.compare(0, p.size(), p) == 0};
        };
        b["ends_with"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{false};
            const std::string s = a[0].AsString(), p = a[1].AsString();
            return Value{s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0};
        };
        // Replace every occurrence of `find` with `repl`.
        b["replace"] = [](std::vector<Value>& a) {
            if (a.size() < 3) return Value{a.empty() ? std::string{} : a[0].AsString()};
            std::string s = a[0].AsString(), find = a[1].AsString(), repl = a[2].AsString();
            if (find.empty()) return Value{s};
            std::size_t pos = 0;
            while ((pos = s.find(find, pos)) != std::string::npos) {
                s.replace(pos, find.size(), repl);
                pos += repl.size();
            }
            return Value{s};
        };
        // Trim leading/trailing ASCII whitespace.
        b["trim"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            auto notspace = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
            s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
            return Value{s};
        };
        // Trim only the leading / only the trailing whitespace.
        b["trim_start"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); }));
            return Value{s};
        };
        b["trim_end"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
            return Value{s};
        };
        // Reverse the characters of a string.
        b["str_reverse"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            std::reverse(s.begin(), s.end());
            return Value{s};
        };
        // capitalize("hello") -> "Hello" (first letter up, rest untouched).
        b["capitalize"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
            return Value{s};
        };
        // title_case("hello world") -> "Hello World" (capitalize each word).
        b["title_case"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            bool start = true;
            for (auto& c : s) {
                if (std::isspace((unsigned char)c)) { start = true; }
                else { c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c); start = false; }
            }
            return Value{s};
        };
        // Repeat a string n times ("=" * 10 style separators/bars).
        // Script-supplied counts are capped so one line can't hang the frame or
        // exhaust memory (issue #719); longer results are truncated, not errors.
        constexpr int kMaxRepeatCount = 1 << 20;
        auto repeatCount = [](std::vector<Value>& a) {
            const double raw = a.size() > 1 ? a[1].AsFloat() : 0.0;
            if (!(raw > 0.0)) return 0;   // also covers NaN
            const double capped = raw < (double)kMaxRepeatCount ? raw : (double)kMaxRepeatCount;
            return (int)capped;
        };
        b["repeat"] = [repeatCount](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            const std::string s = a[0].AsString();
            int n = repeatCount(a);
            // A cap can still make n copies of a long s huge, so the budget is on
            // the result size: shrink the count until it fits.
            if (!s.empty()) {
                const std::size_t maxLen = (std::size_t)kMaxRepeatCount * s.size() > (1u << 22) ? (1u << 22) / s.size() : kMaxRepeatCount;
                if ((std::size_t)n > maxLen) n = (int)maxLen;
            }
            std::string out;
            for (int i = 0; i < n; ++i) out += s;
            return Value{out};
        };
        // pad_left("7", 3)      -> "  7"   ;  pad_left("7", 3, "0") -> "007"
        b["pad_left"] = [repeatCount](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            const int width = repeatCount(a);
            char fill = (a.size() > 2 && !a[2].AsString().empty()) ? a[2].AsString()[0] : ' ';
            while ((int)s.size() < width) s.insert(s.begin(), fill);
            return Value{s};
        };
        b["pad_right"] = [repeatCount](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            const int width = repeatCount(a);
            char fill = (a.size() > 2 && !a[2].AsString().empty()) ? a[2].AsString()[0] : ' ';
            while ((int)s.size() < width) s += fill;
            return Value{s};
        };
        // format("hp={} of {}", 7, 9) -> "hp=7 of 9" (sequential), and also
        // C#-style positional placeholders: format("{0} / {1}", a, b).
        b["format"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            const std::string tmpl = a[0].AsString();
            std::string out; std::size_t seq = 1;
            for (std::size_t i = 0; i < tmpl.size(); ++i) {
                if (tmpl[i] == '{') {
                    std::size_t close = tmpl.find('}', i);
                    if (close != std::string::npos) {
                        std::string inside = tmpl.substr(i + 1, close - i - 1);
                        if (inside.empty()) {                 // {} -> next arg in order
                            if (seq < a.size()) out += a[seq++].AsString();
                        } else {
                            bool num = !inside.empty();
                            for (char ch : inside) if (!std::isdigit((unsigned char)ch)) num = false;
                            // Digits only, parsed without exceptions: std::stoi throws
                            // std::out_of_range on a huge index and aborts the frame
                            // (issue #719). An index beyond the args is an empty field.
                            if (num) {
                                long long idx = 0;
                                const auto r = std::from_chars(inside.data(), inside.data() + inside.size(), idx);
                                if (r.ec == std::errc() && idx >= 0 && (std::size_t)idx + 1 < a.size()) out += a[(std::size_t)idx + 1].AsString();
                            }
                            else { out += '{'; out += inside; out += '}'; }   // leave literal
                        }
                        i = close;
                        continue;
                    }
                }
                out += tmpl[i];
            }
            return Value{out};
        };
        okayscript::InstallConversionBuiltins<Value>(b);   // to_num / num, to_str / str (shared with the okayscript CLI)
        b["split"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray();
            if (a.empty()) return out;
            std::string s = a[0].AsString();
            std::string sep = a.size() > 1 ? a[1].AsString() : " ";
            auto arr = out.AsArray();
            if (sep.empty()) { for (char c : s) arr->push_back(std::string(1, c)); return out; }
            std::size_t pos = 0, next;
            while ((next = s.find(sep, pos)) != std::string::npos) {
                arr->push_back(s.substr(pos, next - pos));
                pos = next + sep.size();
            }
            arr->push_back(s.substr(pos));
            return out;
        };
        b["join"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            auto arr = a[0].AsArray();
            std::string sep = a.size() > 1 ? a[1].AsString() : "";
            if (!arr) return Value{std::string{}};
            std::string s;
            for (std::size_t i = 0; i < arr->size(); ++i) { if (i) s += sep; s += (*arr)[i].AsString(); }
            return Value{s};
        };
        // Maps / dictionaries (string keys).
        b["map"] = [](std::vector<Value>&) { return Value::MakeMap(); };
        b["map_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 3) if (auto m = a[0].AsMap()) (*m)[a[1].AsString()] = a[2];
            return a.empty() ? Value{} : a[0];
        };
        b["map_get"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto m = a[0].AsMap()) {
                auto it = m->find(a[1].AsString());
                if (it != m->end()) return it->second;
            }
            return a.size() > 2 ? a[2] : Value{};
        };
        b["map_has"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto m = a[0].AsMap()) return Value{m->count(a[1].AsString()) != 0};
            return Value{false};
        };
        b["map_remove"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto m = a[0].AsMap()) m->erase(a[1].AsString());
            return a.empty() ? Value{} : a[0];
        };
        b["map_keys"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray();
            if (!a.empty()) if (auto m = a[0].AsMap()) {
                auto arr = out.AsArray();
                for (auto& kv : *m) arr->push_back(kv.first);
            }
            return out;
        };
        b["map_count"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto m = a[0].AsMap()) return Value{(float)m->size()};
            return Value{0.0f};
        };
        b["pi"]    = [](std::vector<Value>&) { return Value{Mathf::PI}; };
        b["deg2rad"] = [](std::vector<Value>& a) { return Value{(a.empty() ? 0 : a[0].AsFloat()) * Mathf::Deg2Rad}; };
        b["rad2deg"] = [](std::vector<Value>& a) { return Value{(a.empty() ? 0 : a[0].AsFloat()) * Mathf::Rad2Deg}; };
}

void OkayScriptVM::Impl::RegisterMathUtilityBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- More everyday math / utility helpers ----------------------
        auto N = [](std::vector<Value>& a, std::size_t i, float d = 0.0f) {
            return i < a.size() ? a[i].AsFloat() : d;
        };
        // approach(cur, target, step) -> step toward target without overshooting.
        b["approach"] = [N](std::vector<Value>& a) {
            float c = N(a, 0), t = N(a, 1), s = std::fabs(N(a, 2));
            if (c < t) return Value{std::min(c + s, t)};
            if (c > t) return Value{std::max(c - s, t)};
            return Value{t};
        };
        // remap(v, inLo, inHi, outLo, outHi) -> rescale a value between ranges.
        b["remap"] = [N](std::vector<Value>& a) {
            float v = N(a, 0), il = N(a, 1), ih = N(a, 2), ol = N(a, 3), oh = N(a, 4, 1.0f);
            if (ih == il) return Value{ol};
            return Value{ol + (v - il) / (ih - il) * (oh - ol)};
        };
        b["frac"]   = [N](std::vector<Value>& a) { float v = N(a, 0); return Value{v - std::floor(v)}; };
        b["mod"]    = [N](std::vector<Value>& a) { float b2 = N(a, 1, 1.0f); float r = b2 != 0 ? std::fmod(N(a, 0), b2) : 0; if (r < 0) r += std::fabs(b2); return Value{r}; };
        b["snap"]   = [N](std::vector<Value>& a) { float s = N(a, 1, 1.0f); return Value{s != 0 ? std::round(N(a, 0) / s) * s : N(a, 0)}; };
        b["is_nan"] = [N](std::vector<Value>& a) { float v = N(a, 0); return Value{std::isnan(v) ? 1.0f : 0.0f}; };
        b["is_finite"] = [N](std::vector<Value>& a) { float v = N(a, 0); return Value{std::isfinite(v) ? 1.0f : 0.0f}; };
        b["avg"]    = [](std::vector<Value>& a) { if (a.empty()) return Value{0.0f}; float s = 0; for (auto& v : a) s += v.AsFloat(); return Value{s / a.size()}; };
        b["min3"]   = [N](std::vector<Value>& a) { return Value{std::min(N(a, 0), std::min(N(a, 1), N(a, 2)))}; };
        b["max3"]   = [N](std::vector<Value>& a) { return Value{std::max(N(a, 0), std::max(N(a, 1), N(a, 2)))}; };
        b["lerp_angle"] = [N](std::vector<Value>& a) {
            float fr = N(a, 0), to = N(a, 1), t = N(a, 2);
            float d = std::fmod(to - fr + 540.0f, 360.0f) - 180.0f;   // shortest path
            return Value{fr + d * t};
        };
        b["str_repeat"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString(); int n = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0;
            std::string out; for (int i = 0; i < n; ++i) out += s; return Value{out};
        };

}

} // namespace okay
