// OkayShader surfaces as RHI shaders (see SurfaceShader.hpp). Written by us.
#include "okay/Render/RHI/SurfaceShader.hpp"

#include <cmath>
#include <cstring>
#include <memory>

namespace okay {
namespace rhi {
namespace {

// Varyings: world position (3), normal (3), uv (2), colour (3).
constexpr int kVarWorld = 0, kVarNormal = 3, kVarUv = 6, kVarColor = 8, kVarCount = 11;

const char* const kSurfaceVertGlsl =
    "#version 120\n"
    "attribute vec3 aPos; attribute vec3 aNormal; attribute vec2 aUV; attribute vec3 aColor;\n"
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "varying vec3 vWorld; varying vec3 vN; varying vec2 vUV; varying vec3 vCol;\n"
    "void main(){\n"
    "  vWorld = (uModel * vec4(aPos, 1.0)).xyz; vN = mat3(uModel) * aNormal; vUV = aUV; vCol = aColor;\n"
    "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "}\n";

const char* const kSurfaceFragMain =
    "uniform vec3 uEye;\n"
    "varying vec3 vWorld; varying vec3 vN; varying vec2 vUV; varying vec3 vCol;\n"
    "void main(){\n"
    "  vec3 V = uEye - vWorld; float lv = length(V); V = lv > 1e-20 ? V / lv : vec3(0.0, 0.0, 1.0);\n"
    "  vec3 N = vN; float ln = length(N); N = ln > 1e-20 ? N / ln : vec3(0.0, 1.0, 0.0);\n"
    "  OkaySurface s = okaySurface(vUV, vWorld, V, vCol, uOkTime, vec3(1.0), vec3(0.0), 1.0, N, 0.0, 0.0, 16.0);\n"
    "  gl_FragColor = vec4(s.albedo + s.emission, s.alpha);\n"
    "}\n";

struct SurfaceData { std::shared_ptr<const oshader::Compiled> compiled; };

void MulPoint(const float* m, const float* p, float w, float* out) {   // column-major m * (p, w)
    for (int r = 0; r < 4; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * w;
}

void SurfaceVert(const float (*attr)[4], const void* uniforms, const void*, float clip[4], float* var) {
    const float* u = static_cast<const float*>(uniforms);
    const float* mvp = u + kSurfaceMvpOffset / 4;
    const float* model = u + kSurfaceModelOffset / 4;
    MulPoint(mvp, attr[0], 1.0f, clip);
    float world[4]; MulPoint(model, attr[0], 1.0f, world);
    float n[4]; MulPoint(model, attr[1], 0.0f, n);
    for (int k = 0; k < 3; ++k) { var[kVarWorld + k] = world[k]; var[kVarNormal + k] = n[k]; var[kVarColor + k] = attr[3][k]; }
    var[kVarUv] = attr[2][0]; var[kVarUv + 1] = attr[2][1];
}

struct SampleBridge { const FragmentContext* ctx; };
void BridgeSample(const void* p, int slot, float u, float v, float rgba[4]) {
    static_cast<const SampleBridge*>(p)->ctx->Sample(slot, u, v, rgba);
}

void Normalize3(const float* in, float* out, const float* fallback) {
    const float l = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    for (int k = 0; k < 3; ++k) out[k] = l > 1e-20f ? in[k] / l : fallback[k];
}

bool SurfaceFrag(const float* v, const FragmentContext& ctx, float out[4]) {
    const SurfaceData& sd = *static_cast<const SurfaceData*>(ctx.userData);
    const float* u = static_cast<const float*>(ctx.uniforms);
    oshader::SurfaceIn in;
    in.uv[0] = v[kVarUv]; in.uv[1] = v[kVarUv + 1];
    const float* eye = u + kSurfaceEyeOffset / 4;
    float view[3] = {eye[0] - v[kVarWorld], eye[1] - v[kVarWorld + 1], eye[2] - v[kVarWorld + 2]};
    static const float kZ[3] = {0, 0, 1}, kY[3] = {0, 1, 0};
    Normalize3(view, in.view, kZ);
    Normalize3(v + kVarNormal, in.normal, kY);
    for (int k = 0; k < 3; ++k) { in.position[k] = v[kVarWorld + k]; in.vcolor[k] = v[kVarColor + k]; }
    in.time = u[kSurfaceTimeOffset / 4];
    oshader::SurfaceOut so;
    SampleBridge bridge{&ctx};
    oshader::RunCpu(*sd.compiled->cpu, in, u + kSurfacePropsOffset / 4, &BridgeSample, &bridge, so);
    if (so.discard) return false;
    for (int k = 0; k < 3; ++k) out[k] = so.albedo[k] + so.emission[k];
    out[3] = so.alpha;
    return true;
}

UniformType PropType(oshader::Type t) {
    switch (t) {
        case oshader::Type::Vec2: return UniformType::Vec2;
        case oshader::Type::Vec3: return UniformType::Vec3;
        case oshader::Type::Vec4: return UniformType::Vec4;
        default: return UniformType::Float;
    }
}

} // namespace

bool ShaderFromSurface(const oshader::Compiled& c, ShaderDesc& d, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    if (!c.ok || !c.cpu) return fail("the surface did not compile: " + c.FirstError());
    if (!c.unlit) return fail("lit surfaces are not on the RHI yet (round 3: lighting)");
    d = ShaderDesc{};
    d.debugName = "okayshader surface";
    d.attributes = {"aPos", "aNormal", "aUV", "aColor"};
    d.uniforms = {UniformField{"uMVP", UniformType::Mat4, kSurfaceMvpOffset},
                  UniformField{"uModel", UniformType::Mat4, kSurfaceModelOffset},
                  UniformField{"uOkTime", UniformType::Float, kSurfaceTimeOffset},
                  UniformField{"uEye", UniformType::Vec3, kSurfaceEyeOffset}};
    for (std::size_t i = 0; i < c.props.size(); ++i)
        d.uniforms.push_back(UniformField{"okp_" + c.props[i].name, PropType(c.props[i].type),
                                          kSurfacePropsOffset + 16u * (std::uint32_t)i});
    d.uniformBytes = kSurfacePropsOffset + 16u * (std::uint32_t)c.props.size();
    for (const oshader::TextureSlot& t : c.textures) d.textures.push_back("okt_" + t.name);
    d.glslVertex = kSurfaceVertGlsl;
    d.glslFragment = std::string("#version 120\n") + c.glsl + kSurfaceFragMain;
    auto data = std::make_shared<SurfaceData>();
    data->compiled = std::make_shared<const oshader::Compiled>(c);
    d.cpuVertex = &SurfaceVert;
    d.cpuFragment = &SurfaceFrag;
    d.cpuVaryings = kVarCount;
    d.cpuUserData = data.get();
    d.keepAlive = data;
    const std::string err = ValidateShaderDesc(d);
    if (!err.empty()) return fail(err);
    return true;
}

std::vector<std::uint8_t> SurfaceUniforms(const float mvp[16], const float model[16], float time, const float eye[3],
                                          const std::vector<float>& props) {
    std::vector<std::uint8_t> b(kSurfacePropsOffset + props.size() * sizeof(float), 0);
    std::memcpy(&b[kSurfaceMvpOffset], mvp, 64);
    std::memcpy(&b[kSurfaceModelOffset], model, 64);
    std::memcpy(&b[kSurfaceTimeOffset], &time, 4);
    std::memcpy(&b[kSurfaceEyeOffset], eye, 12);
    if (!props.empty()) std::memcpy(&b[kSurfacePropsOffset], props.data(), props.size() * sizeof(float));
    return b;
}

} // namespace rhi
} // namespace okay
