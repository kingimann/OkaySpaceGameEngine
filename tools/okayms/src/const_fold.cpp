// Constant folding of integer and floating-point expressions.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
// Constant folding of integers ([expr.const], [expr.arith.conv]). The value of an integer literal node is kept
// as 64 bits in the form its type gives: sign-extended for int and long long, zero-extended for unsigned int.
uint64_t const_norm(uint64_t v, const Type& t) {
    if (t.wide) return v;
    if (t.is_unsigned) return v & 0xFFFFFFFFull;
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(v))));
}

// The type of an arithmetic operation on two integer types after the usual arithmetic conversions: the wide
// operand wins; an unsigned operand wins over a signed one of the same width.
Type const_common(const Type& a, const Type& b) {
    Type r;
    r.base = 1;
    if (a.wide || b.wide) {
        r.wide = true;
        r.is_unsigned = (a.wide && b.wide) ? (a.is_unsigned || b.is_unsigned) : (a.wide ? a.is_unsigned : b.is_unsigned);
    } else {
        r.is_unsigned = a.is_unsigned || b.is_unsigned;
    }
    return r;
}

// The bytes a constant takes when it is stored in an object of arithmetic type `to` ([conv.integral],
// [conv.fpint], [conv.double]): an integer literal given to a float, a float literal given to an int, and so
// on. The low bytes of *bits are the object's value. Returns false for a type that is not arithmetic.
// The value of a numeric literal node as a double (a float literal holds float bits).
double lit_double(const Node& n) {
    if (n.type.base == 5) {
        float f = 0;
        const uint32_t low = static_cast<uint32_t>(n.number);
        std::memcpy(&f, &low, 4);
        return f;
    }
    if (n.type.base == 6) {
        double d = 0;
        std::memcpy(&d, &n.number, 8);
        return d;
    }
    if (n.type.is_unsigned && n.type.wide) return static_cast<double>(n.number);
    return static_cast<double>(static_cast<int64_t>(n.number));
}

bool const_convert(const Node& lit, const Type& to, uint64_t* bits) {
    if (to.pointers > 0) {
        *bits = lit.number;
        return true;
    }
    if (to.array_len >= 0 || to.is_ref || (to.base != 1 && to.base != 2 && to.base != 4 && to.base != 5 && to.base != 6))
        return false;
    const int from = lit.type.base;  // 1 integer, 5 float literal, 6 double literal
    double value = 0;
    bool value_is_int = false;
    int64_t as_int = 0;
    if (from == 5) {
        float f = 0;
        const uint32_t low = static_cast<uint32_t>(lit.number);
        std::memcpy(&f, &low, 4);
        value = f;
    } else if (from == 6) {
        std::memcpy(&value, &lit.number, 8);
    } else {
        value_is_int = true;
        as_int = static_cast<int64_t>(lit.number);
        value = lit.type.is_unsigned && lit.type.wide ? static_cast<double>(lit.number) : static_cast<double>(as_int);
    }
    if (to.base == 5) {
        float f;
        if (value_is_int) f = lit.type.is_unsigned && lit.type.wide ? static_cast<float>(lit.number) : static_cast<float>(as_int);
        else f = static_cast<float>(value);
        uint32_t low = 0;
        std::memcpy(&low, &f, 4);
        *bits = low;
        return true;
    }
    if (to.base == 6) {
        std::memcpy(bits, &value, 8);
        return true;
    }
    int64_t v = as_int;
    if (!value_is_int) {
        if (value != value) v = 0;
        else if (value >= 9223372036854775807.0) v = INT64_MAX;
        else if (value <= -9223372036854775808.0) v = INT64_MIN;
        else v = static_cast<int64_t>(value);
    }
    if (to.is_bool) {
        *bits = value_is_int ? (lit.number != 0 ? 1u : 0u) : (value != 0 ? 1u : 0u);
        return true;
    }
    *bits = static_cast<uint64_t>(v);
    return true;
}

// Folds `left op right` when at least one operand is a float or double literal ([expr.arith.conv],
// [expr.mul], [expr.add], [expr.rel]). Two floats are computed as floats, anything with a double as a double.
// Returns false for an operator that does not fold.
bool fold_fp_binary(Tok op, const Node& l, const Node& r, Node* out) {
    const bool use_double = l.type.base == 6 || r.type.base == 6;
    const bool cmp = op == TEq || op == TNe || op == TLt || op == TGt || op == TLe || op == TGe;
    const double a = lit_double(l);
    const double c = lit_double(r);
    *out = Node{};
    out->kind = Node::IntLit;
    if (op == TAndAnd || op == TOrOr) {
        out->type.base = 1;
        out->number = op == TAndAnd ? (a != 0 && c != 0) : (a != 0 || c != 0);
        return true;
    }
    if (cmp) {
        const float fa = static_cast<float>(a);
        const float fc = static_cast<float>(c);
        const bool both_float = !use_double;
        bool v = false;
        if (op == TEq) v = both_float ? fa == fc : a == c;
        else if (op == TNe) v = both_float ? fa != fc : a != c;
        else if (op == TLt) v = both_float ? fa < fc : a < c;
        else if (op == TGt) v = both_float ? fa > fc : a > c;
        else if (op == TLe) v = both_float ? fa <= fc : a <= c;
        else v = both_float ? fa >= fc : a >= c;
        out->type.base = 1;
        out->number = v ? 1 : 0;
        return true;
    }
    if (op != TPlus && op != TMinus && op != TStar && op != TSlash) return false;
    if (use_double) {
        double v = 0;
        if (op == TPlus) v = a + c;
        else if (op == TMinus) v = a - c;
        else if (op == TStar) v = a * c;
        else v = a / c;
        out->type.base = 6;
        std::memcpy(&out->number, &v, 8);
        return true;
    }
    const float fa = static_cast<float>(a);
    const float fc = static_cast<float>(c);
    volatile float v = 0;
    if (op == TPlus) v = fa + fc;
    else if (op == TMinus) v = fa - fc;
    else if (op == TStar) v = fa * fc;
    else v = fa / fc;
    const float res = v;
    uint32_t low = 0;
    std::memcpy(&low, &res, 4);
    out->type.base = 5;
    out->number = low;
    return true;
}

// Folds `left op right` for two integer literal nodes into *out. Returns false when the operator is not one
// that folds; *err is set for an error (division by zero, a shift count that is too large).
bool fold_int_binary(Tok op, const Node& l, const Node& r, Node* out, std::string* err) {
    const bool shift = op == TShl || op == TShr;
    const bool logic = op == TAndAnd || op == TOrOr;
    Type rt = shift ? Type{} : const_common(l.type, r.type);
    if (shift) {
        rt.base = 1;
        rt.wide = l.type.wide;
        rt.is_unsigned = l.type.is_unsigned;
    }
    const uint64_t a = const_norm(l.number, shift ? rt : rt);
    const uint64_t c = shift ? r.number : const_norm(r.number, rt);
    const bool uns = rt.is_unsigned;
    uint64_t result = 0;
    bool int_result = false;  // the result is an int 0 or 1 (a comparison or && ||)
    if (logic) {
        result = op == TAndAnd ? (l.number != 0 && r.number != 0) : (l.number != 0 || r.number != 0);
        int_result = true;
    } else if (op == TPlus) result = a + c;
    else if (op == TMinus) result = a - c;
    else if (op == TStar) result = a * c;
    else if (op == TSlash || op == TPercent) {
        if (c == 0) {
            *err = "division by zero";
            return false;
        }
        if (uns) result = op == TSlash ? a / c : a % c;
        else {
            const int64_t sa = static_cast<int64_t>(a);
            const int64_t sc = static_cast<int64_t>(c);
            if (sc == -1) result = op == TSlash ? 0 - a : 0;
            else result = static_cast<uint64_t>(op == TSlash ? sa / sc : sa % sc);
        }
    } else if (op == TAmp) result = a & c;
    else if (op == TPipe) result = a | c;
    else if (op == TCaret) result = a ^ c;
    else if (shift) {
        if (c >= 64) {
            *err = "shift count is too large";
            return false;
        }
        if (op == TShl) result = a << c;
        else if (uns) result = a >> c;
        else result = static_cast<uint64_t>(static_cast<int64_t>(a) >> c);
    } else if (op == TEq || op == TNe || op == TLt || op == TGt || op == TLe || op == TGe) {
        int_result = true;
        const bool lt = uns ? a < c : static_cast<int64_t>(a) < static_cast<int64_t>(c);
        const bool gt = uns ? a > c : static_cast<int64_t>(a) > static_cast<int64_t>(c);
        if (op == TEq) result = a == c;
        else if (op == TNe) result = a != c;
        else if (op == TLt) result = lt;
        else if (op == TGt) result = gt;
        else if (op == TLe) result = !gt;
        else result = !lt;
    } else {
        return false;
    }
    *out = Node{};
    out->kind = Node::IntLit;
    if (int_result) {
        out->number = result;
        out->type.base = 1;
    } else {
        out->type = rt;
        out->number = const_norm(result, rt);
    }
    return true;
}


}  // namespace compiler
}  // namespace okayms
