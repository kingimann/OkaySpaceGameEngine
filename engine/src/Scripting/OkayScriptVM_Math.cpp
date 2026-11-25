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
        // Blend two colors/vectors by t (Color.Lerp / Vector3.Lerp on whole values).
        b["lerp_color"] = [](std::vector<Value>& a) {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 c1 = a[0].AsVec3(), c2 = a[1].AsVec3(); float t = Mathf::Clamp01(a[2].AsFloat());
            return Value{c1 + (c2 - c1) * t};
        };
        // The maths, list, map and type helpers that were here (math_repeat, delta_angle, approximately, angle_to,
        // array, count, push, pop, ..., typeof, is_*, fract) are the language core's shared set now:
        // okayscript/include/okayscript/CoreBuiltins.hpp, installed in RegisterBitsJsonStringBuiltins, and the
        // random ones (chance, choose, shuffle, randi) with rand in RegisterInputBuiltins.
}

void OkayScriptVM::Impl::RegisterBitsJsonStringBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // Bits, JSON, strings (format, split, join, ...), conversions, lists, maps, sets, the callback helpers
        // (map_fn, filter_fn, ...) and time: the language core's shared set, the same in the okayscript CLI
        // (okayscript/include/okayscript/CoreBuiltins.hpp). A callback is a function's name (as before) or a lambda.
        okayscript::InstallConversionBuiltins<Value>(b);
        okayscript::InstallTextBuiltins<Value>(b);
        okayscript::InstallCollectionBuiltins<Value>(b, [this](const Value& fn, std::vector<Value>& args) {
            return fn.IsString() ? rt.Call(fn.AsString(), args) : rt.CallValue(fn, args);
        });
        okayscript::InstallJsonBuiltins<Value>(b);
        okayscript::InstallTimeBuiltins<Value>(b);
}

void OkayScriptVM::Impl::RegisterMathUtilityBuiltins(BuiltinCtx&) {
        // approach, remap, frac, mod, snap, is_nan, is_finite, avg, min3, max3, lerp_angle and str_repeat moved to
        // the language core (okayscript::InstallMathBuiltins / InstallTextBuiltins, CoreBuiltins.hpp).
}

} // namespace okay
