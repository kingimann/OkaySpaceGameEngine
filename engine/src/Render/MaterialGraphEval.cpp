// Material graph: CPU evaluator (the reference the shader text must agree with).
#include "okay/Render/MaterialGraph.hpp"
#include <algorithm>
#include <cmath>

namespace okay::matgraph {

namespace {
inline float Fract(float x) { return x - std::floor(x); }
inline float Mix(float a, float b, float t) { return a * (1.0f - t) + b * t; }
inline float Sat(float x) { return std::min(std::max(x, 0.0f), 1.0f); }
} // namespace

// Cell hash, written for this engine: a lattice index is folded into one number with two
// irrational steps (the 2D additive-recurrence constants 1/phi2 and 1/phi2^2 from the
// published R2 sequence), then scrambled with a quadratic and wrapped. Same formula as okm_hash
// in the generated shader text (MaterialGraphCodegen.cpp).
float Hash12(float x, float y) {
    float a = Fract(x * 0.7548777f + y * 0.5698403f);
    return Fract(a * (a * 97.0f + 31.0f));
}

float Noise2(float u, float v, float scale) {
    float px = u * scale, py = v * scale;
    float ix = std::floor(px), iy = std::floor(py);
    float fx = px - ix, fy = py - iy;
    float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
    float a = Hash12(ix, iy), b = Hash12(ix + 1.0f, iy), c = Hash12(ix, iy + 1.0f), d = Hash12(ix + 1.0f, iy + 1.0f);
    return Mix(Mix(a, b, ux), Mix(c, d, ux), uy);
}

MaterialOutputs EvaluateProgram(const Program& p, const EvalInput& in, std::vector<MgValue>* values) {
    std::vector<MgValue> val(p.ops.size());
    auto splat = [](float x, int n) { MgValue r; r.n = n; for (int k = 0; k < n; ++k) r.v[k] = x; return r; };
    for (size_t i = 0; i < p.ops.size(); ++i) {
        const IrOp& o = p.ops[i];
        MgValue r; r.n = std::max(1, PinComponents(o.type));
        auto A = [&]() -> const MgValue& { return val[o.a]; };
        auto B = [&]() -> const MgValue& { return val[o.b]; };
        auto C = [&]() -> const MgValue& { return val[o.c]; };
        auto D = [&]() -> const MgValue& { return val[o.d]; };
        switch (o.op) {
            case Op::Const: for (int k = 0; k < r.n; ++k) r.v[k] = o.v[k]; break;
            case Op::Param: {
                auto it = in.params.find(o.s);
                for (int k = 0; k < r.n; ++k) r.v[k] = it != in.params.end() ? it->second[k] : o.v[k];
                break;
            }
            case Op::TextureObj: case Op::SamplerObj: r.n = 1; break;   // handles, no value
            case Op::Uv: r.v[0] = in.uv[0]; r.v[1] = in.uv[1]; break;
            case Op::WorldPos: for (int k = 0; k < 3; ++k) r.v[k] = in.worldPos[k]; break;
            case Op::Normal: for (int k = 0; k < 3; ++k) r.v[k] = in.normal[k]; break;
            case Op::View: for (int k = 0; k < 3; ++k) r.v[k] = in.view[k]; break;
            case Op::Time: r.v[0] = in.time; break;
            case Op::Splat: r = splat(A().v[0], r.n); break;
            case Op::Rgb: for (int k = 0; k < 3; ++k) r.v[k] = A().v[k]; break;
            case Op::Alpha: r.v[0] = A().v[3]; break;
            case Op::Add: for (int k = 0; k < r.n; ++k) r.v[k] = A().v[k] + B().v[k]; break;
            case Op::Mul: for (int k = 0; k < r.n; ++k) r.v[k] = A().v[k] * B().v[k]; break;
            case Op::Lerp: for (int k = 0; k < r.n; ++k) r.v[k] = Mix(A().v[k], B().v[k], C().v[k]); break;
            case Op::Clamp: for (int k = 0; k < r.n; ++k) r.v[k] = std::min(std::max(A().v[k], B().v[k]), C().v[k]); break;
            case Op::Sat: for (int k = 0; k < r.n; ++k) r.v[k] = Sat(A().v[k]); break;
            case Op::Pow: for (int k = 0; k < r.n; ++k) r.v[k] = std::pow(std::max(A().v[k], 0.0f), B().v[k]); break;
            case Op::OneMinus: for (int k = 0; k < r.n; ++k) r.v[k] = 1.0f - A().v[k]; break;
            case Op::Dot: { float s = 0; for (int k = 0; k < A().n; ++k) s += A().v[k] * B().v[k]; r.v[0] = s; break; }
            case Op::Norm: {
                float s = 0; for (int k = 0; k < r.n; ++k) s += A().v[k] * A().v[k];
                float inv = 1.0f / std::sqrt(std::max(s, 1e-20f));
                for (int k = 0; k < r.n; ++k) r.v[k] = A().v[k] * inv;
                break;
            }
            case Op::Fresnel: {   // a = normal, b = view, c = exponent, d = base reflect
                float s = 0; for (int k = 0; k < 3; ++k) s += A().v[k] * B().v[k];
                float f0 = D().v[0];
                r.v[0] = f0 + (1.0f - f0) * std::pow(1.0f - Sat(s), C().v[0]);
                break;
            }
            case Op::Panner: for (int k = 0; k < 2; ++k) r.v[k] = A().v[k] + B().v[k] * C().v[0]; break;
            case Op::Noise: r.v[0] = Noise2(A().v[0], A().v[1], B().v[0]); break;
            case Op::Sample: {
                float rgba[4] = {1, 1, 1, 1};
                const IrOp& t = p.ops[o.a]; const IrOp& s = p.ops[o.b];
                if (in.texture) in.texture(t.s, s.imm, C().v[0], C().v[1], rgba);
                for (int k = 0; k < 4; ++k) r.v[k] = rgba[k];
                break;
            }
        }
        val[i] = r;
    }
    auto get = [&](int pin, int k) { return p.out[pin] >= 0 ? val[p.out[pin]].v[k] : 0.0f; };
    MaterialOutputs m{};
    for (int k = 0; k < 3; ++k) { m.baseColor[k] = get(kBaseColor, k); m.emissive[k] = get(kEmissive, k); m.normal[k] = get(kNormal, k); }
    m.metallic = get(kMetallic, 0); m.roughness = get(kRoughness, 0); m.opacity = get(kOpacity, 0);
    if (values) *values = std::move(val);
    return m;
}

} // namespace okay::matgraph
