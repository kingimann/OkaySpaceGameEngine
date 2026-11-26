// okay::js library: JSON (ECMA-262 25.5). Written from 25.5.1 JSON.parse (the
// JSON grammar of ECMA-404 read by our own recursive-descent reader, then
// 25.5.1.1 InternalizeJSONProperty for a reviver) and 25.5.2 JSON.stringify
// (SerializeJSONProperty, SerializeJSONObject, SerializeJSONArray and
// QuoteJSONString, with toJSON, a replacer function or property list, the gap
// and cycle detection). Nesting is limited so a hostile text can not overflow the
// C++ stack, and both directions charge the run budget per character.
#include "JsInternal.hpp"
#include <cmath>
#include <cstdlib>

namespace okay::js {

namespace {

constexpr int kMaxDepth = 256;

struct Reader {
    Runtime& rt;
    const std::u16string& s;
    std::size_t i = 0;
    int depth = 0;

    [[noreturn]] void Fail(const char* what) {
        rt.ThrowError(ErrorType::SyntaxError, std::string("JSON.parse: ") + what + " at position " + std::to_string(i));
    }
    void Ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool Lit(const char* w) {
        std::size_t n = 0;
        while (w[n]) {
            if (i + n >= s.size() || s[i + n] != (char16_t)w[n]) return false;
            ++n;
        }
        i += n;
        return true;
    }
    std::u16string Str() {
        ++i;   // "
        std::u16string out;
        while (true) {
            if (i >= s.size()) Fail("unterminated string");
            char16_t c = s[i++];
            if (c == '"') break;
            if (c < 0x20) Fail("a control character in a string");
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) Fail("unterminated string");
            char16_t e = s[i++];
            switch (e) {
                case '"': out += u'"'; break;
                case '\\': out += u'\\'; break;
                case '/': out += u'/'; break;
                case 'b': out += u'\b'; break;
                case 'f': out += u'\f'; break;
                case 'n': out += u'\n'; break;
                case 'r': out += u'\r'; break;
                case 't': out += u'\t'; break;
                case 'u': {
                    if (i + 4 > s.size()) Fail("bad \\u escape");
                    unsigned v = 0;
                    for (int k = 0; k < 4; ++k) {
                        char16_t h = s[i++];
                        int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                        if (d < 0) Fail("bad \\u escape");
                        v = v * 16 + (unsigned)d;
                    }
                    out += (char16_t)v;
                    break;
                }
                default: Fail("bad escape");
            }
        }
        rt.Charge(out.size() / 16 + 1);
        return out;
    }
    Value Num() {
        std::size_t st = i;
        if (s[i] == '-') ++i;
        if (i >= s.size()) Fail("bad number");
        if (s[i] == '0') ++i;
        else if (s[i] >= '1' && s[i] <= '9') while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        else Fail("bad number");
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') Fail("bad number");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') Fail("bad number");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        std::string t;
        for (std::size_t k = st; k < i; ++k) t += (char)s[k];
        return Value::Num(std::strtod(t.c_str(), nullptr));
    }
    Value Val() {
        Ws();
        if (i >= s.size()) Fail("unexpected end of text");
        rt.Charge(1);
        char16_t c = s[i];
        if (c == '{') {
            if (++depth > kMaxDepth) Fail("nested too deeply");
            ++i;
            Object* o = rt.NewObject();
            Value ov = Value::Obj(o);
            rt.Keep(ov);
            Ws();
            if (i < s.size() && s[i] == '}') { ++i; --depth; return ov; }
            while (true) {
                Ws();
                if (i >= s.size() || s[i] != '"') Fail("expected a property name");
                std::u16string k = Str();
                Ws();
                if (i >= s.size() || s[i] != ':') Fail("expected ':'");
                ++i;
                Value v = Val();
                rt.DefineData(o, rt.Intern(k), v);
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                Fail("expected ',' or '}'");
            }
            --depth;
            return ov;
        }
        if (c == '[') {
            if (++depth > kMaxDepth) Fail("nested too deeply");
            ++i;
            ArrayObj* a = rt.NewArray();
            Value av = Value::Obj(a);
            rt.Keep(av);
            Ws();
            if (i < s.size() && s[i] == ']') { ++i; --depth; return av; }
            while (true) {
                Value v = Val();
                rt.DefineIndex(a, a->length, v);
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                Fail("expected ',' or ']'");
            }
            --depth;
            return av;
        }
        if (c == '"') return Value::Str(rt.NewString(Str()));
        if (c == '-' || (c >= '0' && c <= '9')) return Num();
        if (Lit("true")) return Value::Bool(true);
        if (Lit("false")) return Value::Bool(false);
        if (Lit("null")) return Value::Null();
        Fail("unexpected character");
    }
};

Value Internalize(Runtime& rt, const Value& holder, GcObj* key, const Value& reviver, int depth) {
    if (depth > kMaxDepth) rt.ThrowRange("JSON.parse: nested too deeply");
    Value val = rt.Get(holder, key);
    rt.Keep(val);
    if (val.IsObj()) {
        Object* o = val.O();
        if (rt.IsArray(val)) {
            std::uint64_t len = rt.LengthOf(val);
            for (std::uint64_t k = 0; k < len; ++k) {
                Runtime::ScratchScope ss(rt);
                GcObj* ik = rt.IndexKey((std::uint32_t)k);
                Value nv = Internalize(rt, val, ik, reviver, depth + 1);
                if (nv.IsUndef()) rt.Delete(o, ik);
                else rt.DefineData(o, ik, nv);
            }
        } else {
            for (GcObj* k : rt.OwnKeys(o, true, false)) {
                Prop p;
                if (!rt.GetOwnProperty(o, k, p) || !(p.flags & kEnumerable)) continue;
                Runtime::ScratchScope ss(rt);
                Value nv = Internalize(rt, val, k, reviver, depth + 1);
                if (nv.IsUndef()) rt.Delete(o, k);
                else rt.DefineData(o, k, nv);
            }
        }
    }
    Value args[2] = {key->kind == Kind::String ? Value::Str(static_cast<String*>(key)) : Value(), val};
    return rt.Call(reviver, holder, args, 2);
}

struct Writer {
    explicit Writer(Runtime& r) : rt(r) {}
    Runtime& rt;
    Value replacerFn;
    std::vector<GcObj*> propList;
    bool hasList = false;
    std::u16string gap, indent;
    std::vector<Object*> stack;
    std::u16string out;

    void Quote(const std::u16string& s) {
        out += u'"';
        static const char* hex = "0123456789abcdef";
        for (std::size_t i = 0; i < s.size(); ++i) {
            char16_t c = s[i];
            switch (c) {
                case '"': out += u"\\\""; break;
                case '\\': out += u"\\\\"; break;
                case '\b': out += u"\\b"; break;
                case '\f': out += u"\\f"; break;
                case '\n': out += u"\\n"; break;
                case '\r': out += u"\\r"; break;
                case '\t': out += u"\\t"; break;
                default: {
                    bool lone = false;
                    if (c >= 0xD800 && c <= 0xDBFF) lone = !(i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF);
                    else if (c >= 0xDC00 && c <= 0xDFFF) lone = !(i > 0 && s[i - 1] >= 0xD800 && s[i - 1] <= 0xDBFF);
                    if (c < 0x20 || lone) {
                        out += u"\\u";
                        out += (char16_t)hex[(c >> 12) & 15];
                        out += (char16_t)hex[(c >> 8) & 15];
                        out += (char16_t)hex[(c >> 4) & 15];
                        out += (char16_t)hex[c & 15];
                    } else out += c;
                }
            }
        }
        out += u'"';
        rt.Charge(s.size() / 16 + 1);
        if (out.size() > (1u << 28)) rt.ThrowRange("JSON.stringify: the result is too long");
    }

    // 25.5.2.2 SerializeJSONProperty: false when the value is not serializable (undefined, a function, a symbol)
    bool Prop(const Value& holder, GcObj* key, Value value) {
        rt.Charge(1);
        if (value.IsObj()) {
            Value toJSON = rt.Get(value, rt.atoms->toJSON);
            if (rt.IsCallable(toJSON)) {
                Value k = key->kind == Kind::String ? Value::Str(static_cast<String*>(key)) : Value();
                value = rt.Call(toJSON, value, &k, 1);
            }
        }
        if (!replacerFn.IsUndef()) {
            Value args[2] = {key->kind == Kind::String ? Value::Str(static_cast<String*>(key)) : Value(), value};
            value = rt.Call(replacerFn, holder, args, 2);
        }
        rt.Keep(value);
        if (value.IsObj()) {
            Object* o = value.O();
            if (o->cls == Cls::NumberBox) value = Value::Num(rt.ToNumber(value));
            else if (o->cls == Cls::StringBox) value = Value::Str(rt.ToString(value));
            else if (o->cls == Cls::BoolBox) value = static_cast<BoxObj*>(o)->prim;
        }
        switch (value.t) {
            case Tag::Null: out += u"null"; return true;
            case Tag::Bool: out += value.b ? u"true" : u"false"; return true;
            case Tag::String: Quote(value.S()->s); return true;
            case Tag::Number:
                if (std::isfinite(value.n)) out += rt.NumberToU16(value.n);
                else out += u"null";
                return true;
            case Tag::Object:
                if (value.O()->IsCallable()) return false;
                if (rt.IsArray(value)) Array(value);
                else Obj(value);
                return true;
            default: return false;
        }
    }
    void Enter(Object* o) {
        for (Object* s : stack)
            if (s == o) rt.ThrowType("JSON.stringify: the value has a cycle (an object contains itself)");
        if (stack.size() > (std::size_t)kMaxDepth) rt.ThrowRange("JSON.stringify: nested too deeply");
        stack.push_back(o);
    }
    void Obj(const Value& v) {
        Object* o = v.O();
        Enter(o);
        std::u16string stepback = indent;
        indent += gap;
        std::vector<GcObj*> keys;
        if (hasList) keys = propList;
        else
            for (GcObj* k : rt.OwnKeys(o, true, false)) {
                okay::js::Prop p;
                if (rt.GetOwnProperty(o, k, p) && (p.flags & kEnumerable)) keys.push_back(k);
            }
        out += u'{';
        bool any = false;
        for (GcObj* k : keys) {
            Runtime::ScratchScope ss(rt);
            Value pv = rt.Get(v, k);
            std::size_t mark = out.size();
            if (any) out += u',';
            if (!gap.empty()) { out += u'\n'; out += indent; }
            Quote(static_cast<String*>(k)->s);
            out += u':';
            if (!gap.empty()) out += u' ';
            if (!Prop(v, k, pv)) out.resize(mark);
            else any = true;
        }
        if (any && !gap.empty()) { out += u'\n'; out += stepback; }
        out += u'}';
        stack.pop_back();
        indent = stepback;
    }
    void Array(const Value& v) {
        Enter(v.O());
        std::u16string stepback = indent;
        indent += gap;
        std::uint64_t len = rt.LengthOf(v);
        out += u'[';
        for (std::uint64_t k = 0; k < len; ++k) {
            Runtime::ScratchScope ss(rt);
            if (k) out += u',';
            if (!gap.empty()) { out += u'\n'; out += indent; }
            GcObj* ik = rt.IndexKey((std::uint32_t)k);
            if (!Prop(v, ik, rt.GetIndex(v, (std::uint32_t)k))) out += u"null";
        }
        if (len && !gap.empty()) { out += u'\n'; out += stepback; }
        out += u']';
        stack.pop_back();
        indent = stepback;
    }
};

} // namespace

void InstallJsonLib(Runtime& rt) {
    Object* j = rt.NewObject();
    DefValue(rt, rt.global, "JSON", Value::Obj(j));
    j->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("JSON")), kConfigurable);
    DefNative(rt, j, "parse", [](Runtime& rt, CallArgs& a) {
        std::u16string text = rt.ToU16(a[0]);
        Reader r{rt, text};
        Value v = r.Val();
        r.Ws();
        if (r.i != text.size()) r.Fail("extra text after the value");
        if (rt.IsCallable(a[1])) {
            rt.Keep(v);
            Object* root = rt.NewObject();
            rt.DefineData(root, rt.atoms->empty, v);
            return Internalize(rt, Value::Obj(root), rt.atoms->empty, a[1], 0);
        }
        return v;
    }, 2);
    DefNative(rt, j, "stringify", [](Runtime& rt, CallArgs& a) {
        Writer w(rt);
        Value rep = a[1];
        if (rt.IsCallable(rep)) w.replacerFn = rep;
        else if (rt.IsArray(rep)) {
            w.hasList = true;
            std::uint64_t len = rt.LengthOf(rep);
            for (std::uint64_t k = 0; k < len; ++k) {
                Value e = rt.GetIndex(rep, (std::uint32_t)k);
                if (e.IsObj() && (e.O()->cls == Cls::NumberBox || e.O()->cls == Cls::StringBox)) e = Value::Str(rt.ToString(e));
                if (!e.IsStr() && !e.IsNum()) continue;
                GcObj* key = rt.ToPropertyKey(e);
                if (std::find(w.propList.begin(), w.propList.end(), key) == w.propList.end()) w.propList.push_back(key);
            }
        }
        Value space = a[2];
        if (space.IsObj() && space.O()->cls == Cls::NumberBox) space = Value::Num(rt.ToNumber(space));
        else if (space.IsObj() && space.O()->cls == Cls::StringBox) space = Value::Str(rt.ToString(space));
        if (space.IsNum()) {
            double n = std::min(10.0, rt.ToIntegerOrInfinity(space));
            if (n >= 1) w.gap = std::u16string((std::size_t)n, u' ');
        } else if (space.IsStr()) w.gap = space.S()->s.substr(0, 10);
        Object* wrapper = rt.NewObject();
        rt.DefineData(wrapper, rt.atoms->empty, a[0]);
        if (!w.Prop(Value::Obj(wrapper), rt.atoms->empty, a[0])) return Value();
        return Value::Str(rt.NewString(std::move(w.out)));
    }, 3);
}

} // namespace okay::js
