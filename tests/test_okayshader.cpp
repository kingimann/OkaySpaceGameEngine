// OkayShader language: lexer, parser, type checker, GLSL / HLSL emitters, CPU program and
// material property text. Hostile inputs and a mutation fuzz must give errors, never crashes.
#include "test_framework.hpp"
#include "okay/Render/OkayShader.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace okay::oshader;

namespace {

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// Compile and expect one error at line:col whose message contains `part`.
bool ErrAt(const std::string& src, int line, int col, const std::string& part) {
    Compiled c = Compile(src);
    if (c.ok || c.errors.empty()) { std::cerr << "    expected an error for:\n" << src << "\n"; return false; }
    const Diagnostic& d = c.errors[0];
    const bool good = d.line == line && d.col == col && Has(d.message, part);
    if (!good) std::cerr << "    got " << d.ToString() << " (wanted " << line << ":" << col << " '" << part << "')\n";
    return good;
}

SurfaceOut Run(const Compiled& c, const SurfaceIn& in, const std::string& params = "") {
    SurfaceOut o{};
    std::vector<float> p = ResolveProperties(c, params);
    if (c.cpu) RunCpu(*c.cpu, in, p.data(), nullptr, nullptr, o);
    return o;
}

const char* kLava =
    "// scrolling lava\n"
    "shader \"Lava\";\n"
    "property float speed = 0.3 range(0, 2);\n"
    "property color hot = (1.0, 0.55, 0.1);\n"
    "property vec2 scale = vec2(4.0, 4.0);\n"
    "texture detail = \"proc:noise\";\n"
    "surface {\n"
    "    float n = noise(uv * scale + vec2(0.0, time * speed));\n"
    "    vec3 cool = vec3(0.2, 0.02, 0.0);\n"
    "    albedo = mix(cool, hot, n) * sample(detail, uv).rgb;\n"
    "    emission = hot * smoothstep(0.6, 1.0, n);\n"
    "    if (n < 0.1) { discard; }\n"
    "}\n";

} // namespace

int main() {
    RUN_SUITE("okayshader");

    // ---- a valid shader: declarations come through --------------------------------
    {
        Compiled c = Compile(kLava);
        CHECK(c.ok);
        if (!c.ok) std::cerr << "    " << c.FirstError("lava") << "\n";
        CHECK(c.name == "Lava");
        CHECK(!c.unlit);
        CHECK(c.usesDiscard);
        CHECK(c.props.size() == 3);
        if (c.props.size() == 3) {
            CHECK(c.props[0].name == "speed" && c.props[0].type == Type::Float && c.props[0].hasRange);
            CHECK_NEAR(c.props[0].def[0], 0.3f, 1e-6f);
            CHECK_NEAR(c.props[0].maxV, 2.0f, 1e-6f);
            CHECK(c.props[1].isColor && c.props[1].type == Type::Vec3);
            CHECK_NEAR(c.props[1].def[1], 0.55f, 1e-6f);
            CHECK(c.props[2].type == Type::Vec2 && c.props[2].def[1] == 4.0f);
        }
        CHECK(c.textures.size() == 1 && c.textures[0].name == "detail" && c.textures[0].defaultPath == "proc:noise");
        CHECK(c.cpu != nullptr);
        // GLSL: uniforms for each property, texture and time; GLSL spellings only.
        CHECK(Has(c.glsl, "uniform float okp_speed;"));
        CHECK(Has(c.glsl, "uniform vec3 okp_hot;"));
        CHECK(Has(c.glsl, "uniform sampler2D okt_detail;"));
        CHECK(Has(c.glsl, "uniform float uOkTime;"));
        CHECK(Has(c.glsl, "OkaySurface okaySurface("));
        CHECK(Has(c.glsl, "texture2D(okt_detail"));
        CHECK(Has(c.glsl, "discard;"));
        CHECK(!Has(c.glsl, "lerp(") && !Has(c.glsl, "frac(") && !Has(c.glsl, "float3"));
        // HLSL: the property block in b3, textures from t9, HLSL spellings only.
        CHECK(Has(c.hlsl, "cbuffer OkayCustom : register(b4)"));
        CHECK(Has(c.hlsl, "Texture2D okt_detail : register(t9);"));
        CHECK(Has(c.hlsl, "okt_detail.Sample(uSamp"));
        CHECK(Has(c.hlsl, "lerp("));
        CHECK(Has(c.hlsl, "OkaySurface okaySurface("));
        CHECK(!Has(c.hlsl, "mix(") && !Has(c.hlsl, "vec3") && !Has(c.hlsl, "texture2D"));
        // Every user name is renamed, so a GLSL/HLSL keyword as a local name is harmless.
        CHECK(Has(c.glsl, "l_n") && Has(c.hlsl, "l_n"));
    }
    {
        Compiled c = Compile("mode unlit; surface { albedo = vec3(1.0, 0.0, 0.0); }");
        CHECK(c.ok && c.unlit && !c.usesDiscard);
        Compiled k = Compile("surface { float texture = 1.0; float sampler = texture * 2.0; float input = sampler; alpha = input; }");
        CHECK(k.ok);   // GLSL / HLSL keywords are fine as names
    }

    // ---- errors carry line:col -------------------------------------------------------
    CHECK(ErrAt("surface {\n  vec3 a = vec2(1.0, 2.0);\n}\n", 2, 12, "vec3"));
    CHECK(ErrAt("surface {\n  albedo = foo;\n}\n", 2, 12, "foo"));
    CHECK(ErrAt("surface {\n  uv = vec2(0.0);\n}\n", 2, 3, "read-only"));
    CHECK(ErrAt("surface {\n  float a = 1.0\n  alpha = a;\n}\n", 3, 3, "';'"));
    CHECK(ErrAt("surface { alpha = wobble(1.0); }", 1, 19, "wobble"));
    CHECK(ErrAt("surface { alpha = clamp(1.0); }", 1, 19, "clamp"));
    CHECK(ErrAt("property float a = 1.0;\nproperty float a = 2.0;\nsurface { }", 2, 16, "already"));
    CHECK(ErrAt("surface { for i in 0..1000 { alpha = i; } }", 1, 23, "loop"));
    CHECK(ErrAt("surface { vec3 p = position.xq; }", 1, 29, "swizzle"));
    CHECK(ErrAt("surface { alpha = 1.0; } /* never closed", 1, 26, "comment"));
    CHECK(ErrAt("shader \"never closed;\nsurface { }", 1, 8, "string"));
    CHECK(ErrAt("surface { alpha = 1e999; }", 1, 19, "number"));
    CHECK(ErrAt("surface { if (1.0) { alpha = 0.0; } }", 1, 15, "bool"));
    CHECK(ErrAt("surface { albedo = vec3(1.0) + vec2(1.0); }", 1, 30, "vec3"));
    CHECK(ErrAt("surface { float a = 1.0; float a = 2.0; }", 1, 32, "already"));
    CHECK(ErrAt("surface { alpha = 1.0; }\nsurface { }", 2, 1, "surface"));
    CHECK(ErrAt("property float a = 1.0;", 1, 24, "surface"));
    CHECK(ErrAt("surface { speed = 1.0; }\n", 1, 11, "speed"));
    CHECK(ErrAt("property float speed = 1.0;\nsurface { speed = 2.0; }\n", 2, 11, "read-only"));
    CHECK(ErrAt("texture t;\nsurface { alpha = t; }\n", 2, 19, "texture"));
    CHECK(ErrAt("surface { alpha = sample(uv, uv).r; }", 1, 26, "texture"));
    CHECK(ErrAt("surface { albedo.xx = vec2(1.0); }", 1, 18, "twice"));
    CHECK(ErrAt("surface { alpha = 1.0 @ 2.0; }", 1, 23, "'@'"));
    {
        Compiled c = Compile("surface {\n  albedo = foo;\n}\n");
        CHECK(c.FirstError("x.okayshader") == "x.okayshader:2:12: error: unknown name 'foo'");
    }

    // ---- hostile inputs: errors, never crashes, always quick --------------------------
    {
        std::vector<std::string> hostile = {
            "", " ", "\0\0\0", "surface", "surface {", "}", "{{{{", "surface { ((((((((((( }",
            "property", "property float", "property float x =", "property float x = (1,2,3,4,5);",
            "property vec9 x = 1;", "texture", "texture t = ;", "mode", "mode sideways;",
            "surface { for i in 5..1 { } }", "surface { for i in 0.5..2 { } }", "surface { a.b.c.d = 1; }",
            "surface { alpha = 1.0 ? 2.0 : 3.0; }", "surface { alpha = (true) ? 2.0 : vec2(1.0); }",
            "surface { discard }", "surface { else { } }", "surface { if (true) else { } }",
            "surface { albedo = vec3(1.0, 2.0); }", "surface { albedo = vec3(1.0, 2.0, 3.0, 4.0); }",
            "surface { alpha = vec4(1.0).xyzwx; }", "surface { alpha = .; }", "surface { alpha = 1..2; }",
            "surface { alpha = -; }", "surface { alpha = !1.0; }", "surface { normal = cross(uv, uv); }",
        };
        std::string deep = "surface { alpha = ";
        for (int i = 0; i < 5000; ++i) deep += "(";
        deep += "1.0";
        for (int i = 0; i < 5000; ++i) deep += ")";
        deep += "; }";
        hostile.push_back(deep);
        std::string deepIf = "surface { ";
        for (int i = 0; i < 3000; ++i) deepIf += "if (true) { ";
        hostile.push_back(deepIf);
        std::string neg = "surface { alpha = ";
        for (int i = 0; i < 5000; ++i) neg += "-";
        neg += "1.0; }";
        hostile.push_back(neg);
        hostile.push_back("surface { alpha = " + std::string(100000, 'a') + "; }");
        std::string many = "surface { ";
        for (int i = 0; i < 4000; ++i) many += "float v" + std::to_string(i) + " = 1.0; ";
        many += "}";
        hostile.push_back(many);   // too many locals for the register file: an error, not a crash
        std::string props;
        for (int i = 0; i < 40; ++i) props += "property float p" + std::to_string(i) + " = 1.0;\n";
        hostile.push_back(props + "surface { }");
        hostile.push_back(std::string("surface { alpha = 1.0; }\0garbage", 34));
        const auto t0 = std::chrono::steady_clock::now();
        int failedAsExpected = 0;
        for (const std::string& h : hostile) {
            Compiled c = Compile(h);
            if (!c.ok && !c.errors.empty() && c.errors[0].line >= 1 && c.errors[0].col >= 1) ++failedAsExpected;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(failedAsExpected == (int)hostile.size());
        CHECK(ms < 5000.0);
        // The register limit is reported, not overflowed.
        CHECK(Has(Compile(many).FirstError(), "too many"));
        CHECK(Has(Compile(props + "surface { }").FirstError(), "properties"));
    }
    // Mutation fuzz: delete / duplicate / replace bytes of valid shaders. Any outcome but a crash
    // is fine; a source that still compiles must also run on the CPU without trouble.
    {
        std::uint32_t seed = 12345u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const char* seeds[] = {kLava, "surface { for i in 0..4 { albedo += vec3(i * 0.1); } alpha = albedo.x > 0.5 ? 1.0 : 0.5; }",
                               "property vec4 tint = (1,1,1,1); surface { albedo = tint.rgb * vcolor; normal = normalize(normal + view * 0.1); }"};
        const char alphabet[] = "(){};.,=+-*/<>!&|?:0123456789 abcxyzrgbvecfloatsurfaceproperty\"\n\t\\@#$";
        int compiled = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int it = 0; it < 6000; ++it) {
            std::string s = seeds[it % 3];
            const int edits = 1 + (int)(rnd() % 6);
            for (int e = 0; e < edits && !s.empty(); ++e) {
                const std::size_t at = rnd() % s.size();
                switch (rnd() % 4) {
                    case 0: s.erase(at, 1 + rnd() % 4); break;
                    case 1: s.insert(at, s.substr(rnd() % s.size(), 1 + rnd() % 8)); break;
                    case 2: s[at] = alphabet[rnd() % (sizeof(alphabet) - 1)]; break;
                    default: s[at] = (char)(rnd() & 0xFF); break;
                }
            }
            Compiled c = Compile(s);
            if (c.ok) {
                ++compiled;
                SurfaceIn in; in.uv[0] = 0.3f; in.uv[1] = 0.7f; in.time = 1.5f;
                SurfaceOut o = Run(c, in);
                (void)o;
            } else {
                CHECK(!c.errors.empty());
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "  fuzz: 6000 mutants, " << compiled << " still compiled, " << ms << " ms\n";
        CHECK(ms < 30000.0);
    }

    // ---- the CPU program computes what the source says ---------------------------------
    {
        SurfaceIn in;
        in.uv[0] = 0.25f; in.uv[1] = 0.75f;
        in.position[0] = 1.0f; in.position[1] = -2.0f; in.position[2] = 3.0f;
        in.time = 2.0f;
        Compiled c = Compile(
            "property float k = 0.5;\n"
            "surface {\n"
            "  vec3 a = vec3(uv, k);\n"
            "  albedo = a;\n"
            "  alpha = mod(-1.25, 1.0);\n"                    // GLSL mod: always >= 0 -> 0.75
            "  metallic = smoothstep(0.0, 1.0, 0.5);\n"       // 0.5
            "  specular = clamp(position.y, -1.0, 1.0);\n"    // -1
            "  float s = 0.0;\n"
            "  for i in 0..4 { s += i; }\n"                   // 0+1+2+3
            "  shininess = s;\n"
            "  emission = position.zyx;\n"
            "  emission.y = time > 1.0 ? 9.0 : 7.0;\n"
            "  if (uv.x > 0.5) { emission.x = 100.0; } else if (uv.x > 0.2) { emission.x = 50.0; } else { emission.x = 1.0; }\n"
            "  normal = normalize(vec3(0.0, 0.0, 0.0));\n"    // a zero vector normalizes to zero, not NaN
            "}\n");
        CHECK(c.ok);
        if (!c.ok) std::cerr << "    " << c.FirstError() << "\n";
        SurfaceOut o = Run(c, in);
        CHECK_NEAR(o.albedo[0], 0.25f, 1e-6f); CHECK_NEAR(o.albedo[1], 0.75f, 1e-6f); CHECK_NEAR(o.albedo[2], 0.5f, 1e-6f);
        CHECK_NEAR(o.alpha, 0.75f, 1e-6f);
        CHECK_NEAR(o.metallic, 0.5f, 1e-6f);
        CHECK_NEAR(o.specular, -1.0f, 1e-6f);
        CHECK_NEAR(o.shininess, 6.0f, 1e-6f);
        CHECK_NEAR(o.emission[0], 50.0f, 1e-6f); CHECK_NEAR(o.emission[1], 9.0f, 1e-6f); CHECK_NEAR(o.emission[2], 1.0f, 1e-6f);
        CHECK(o.normal[0] == 0.0f && o.normal[1] == 0.0f && o.normal[2] == 0.0f);
        CHECK(!o.discard);
        // The material's value replaces the default.
        SurfaceOut o2 = Run(c, in, "k=0.125");
        CHECK_NEAR(o2.albedo[2], 0.125f, 1e-6f);
    }
    {
        // Outputs start from the material's values; untouched ones pass through.
        SurfaceIn in;
        in.albedo[0] = 0.1f; in.albedo[1] = 0.2f; in.albedo[2] = 0.3f;
        in.emission[2] = 0.4f; in.alpha = 0.6f; in.metallic = 0.7f; in.specular = 0.8f; in.shininess = 32.0f;
        in.normal[0] = 0.0f; in.normal[1] = 0.0f; in.normal[2] = 1.0f;
        Compiled c = Compile("surface { albedo *= 2.0; }");
        SurfaceOut o = Run(c, in);
        CHECK_NEAR(o.albedo[2], 0.6f, 1e-6f);
        CHECK_NEAR(o.emission[2], 0.4f, 1e-6f);
        CHECK_NEAR(o.alpha, 0.6f, 1e-6f);
        CHECK_NEAR(o.metallic, 0.7f, 1e-6f); CHECK_NEAR(o.specular, 0.8f, 1e-6f); CHECK_NEAR(o.shininess, 32.0f, 1e-6f);
        CHECK_NEAR(o.normal[2], 1.0f, 1e-6f);
        // discard
        Compiled d = Compile("surface { if (uv.x < 0.5) { discard; } }");
        SurfaceIn a; a.uv[0] = 0.2f;
        CHECK(Run(d, a).discard);
        a.uv[0] = 0.8f;
        CHECK(!Run(d, a).discard);
    }
    {
        // noise / hash: in 0..1, the same for the same input, smooth (no jumps between neighbours).
        Compiled c = Compile("surface { alpha = noise(uv * 8.0); metallic = hash(uv * 8.0); specular = noise(position); }");
        CHECK(c.ok);
        float lo = 1.0f, hi = 0.0f, maxStep = 0.0f, prev = -1.0f;
        float hlo = 1.0f, hhi = 0.0f;
        for (int i = 0; i <= 2000; ++i) {
            SurfaceIn in; in.uv[0] = i / 2000.0f * 3.0f; in.uv[1] = 0.37f;
            in.position[0] = i * 0.01f; in.position[1] = 0.5f; in.position[2] = -1.5f;
            SurfaceOut o = Run(c, in);
            lo = std::fmin(lo, o.alpha); hi = std::fmax(hi, o.alpha);
            hlo = std::fmin(hlo, o.metallic); hhi = std::fmax(hhi, o.metallic);
            CHECK(o.specular >= 0.0f && o.specular <= 1.0f);
            if (prev >= 0.0f) maxStep = std::fmax(maxStep, std::fabs(o.alpha - prev));
            prev = o.alpha;
            SurfaceOut again = Run(c, in);
            if (again.alpha != o.alpha) CHECK(false);
        }
        CHECK(lo >= 0.0f && hi <= 1.0f && hi - lo > 0.5f);   // it really varies
        CHECK(hlo >= 0.0f && hhi <= 1.0f && hhi - hlo > 0.5f);
        CHECK(maxStep < 0.1f);                              // and is continuous
    }
    {
        // Textures on the CPU go through the renderer's sampler.
        Compiled c = Compile("texture a; texture b; surface { albedo = sample(b, uv * 2.0).rgb; alpha = sample(a, uv).a; }");
        CHECK(c.ok);
        struct Ctx { int calls = 0; } ctx;
        SampleFn fn = [](const void* p, int slot, float u, float v, float rgba[4]) {
            ++const_cast<Ctx*>(static_cast<const Ctx*>(p))->calls;
            rgba[0] = (float)slot; rgba[1] = u; rgba[2] = v; rgba[3] = slot == 0 ? 0.5f : 0.0f;
        };
        SurfaceIn in; in.uv[0] = 0.1f; in.uv[1] = 0.2f;
        SurfaceOut o{};
        std::vector<float> p = ResolveProperties(c, "");
        RunCpu(*c.cpu, in, p.data(), fn, &ctx, o);
        CHECK(ctx.calls == 2);
        CHECK_NEAR(o.albedo[0], 1.0f, 1e-6f); CHECK_NEAR(o.albedo[1], 0.2f, 1e-6f); CHECK_NEAR(o.albedo[2], 0.4f, 1e-6f);
        CHECK_NEAR(o.alpha, 0.5f, 1e-6f);
        // No sampler (a texture that is not bound): white.
        SurfaceOut w = Run(c, in);
        CHECK_NEAR(w.albedo[1], 1.0f, 1e-6f);
    }

    // ---- material property text ---------------------------------------------------------
    {
        std::string t;
        const float sp[1] = {0.5f};
        const float col[3] = {1.0f, 0.25f, 0.0f};
        t = SetParam(t, "speed", sp, 1);
        t = SetParam(t, "hot", col, 3);
        t = SetTextParam(t, "tex:detail", "my pic.png");
        float got[3] = {0, 0, 0};
        CHECK(GetParam(t, "speed", got, 1) && got[0] == 0.5f);
        CHECK(GetParam(t, "hot", got, 3) && got[1] == 0.25f);
        CHECK(!GetParam(t, "nothing", got, 1));
        CHECK(GetTextParam(t, "tex:detail") == "my pic.png");
        const float sp2[1] = {1.75f};
        t = SetParam(t, "speed", sp2, 1);   // replaces, keeps the others
        CHECK(GetParam(t, "speed", got, 1) && got[0] == 1.75f);
        CHECK(GetParam(t, "hot", got, 3) && got[0] == 1.0f);
        Compiled c = Compile(kLava);
        std::vector<float> v = ResolveProperties(c, t + ";unknown=3");
        CHECK(v.size() == 12);
        if (v.size() == 12) {
            CHECK(v[0] == 1.75f);                     // speed (material)
            CHECK(v[4] == 1.0f && v[5] == 0.25f);     // hot (material)
            CHECK(v[8] == 4.0f && v[9] == 4.0f);      // scale (default)
        }
        CHECK(ResolveTexture(c, 0, t) == "my pic.png");
        CHECK(ResolveTexture(c, 0, "") == "proc:noise");
        // Garbage values keep the default.
        std::vector<float> g = ResolveProperties(c, "speed=abc;hot=nan,1,1;;;=;speed");
        CHECK(g.size() == 12 && g[0] == 0.3f && g[4] == 1.0f && g[5] == 0.55f);
    }

    // ---- shader files: cache and hot reload ------------------------------------------------
    {
        const std::string path = "okayshader_test_file.okayshader";
        { std::ofstream f(path); f << "surface { alpha = 0.25; }"; }
        ClearShaderFileCache();
        ShaderFile a = LoadShaderFile(path, 0);
        CHECK(a.compiled && a.compiled->ok);
        ShaderFile a2 = LoadShaderFile(path, 0);
        CHECK(a2.generation == a.generation);           // unchanged file: same compile
        { std::ofstream f(path); f << "surface { alpha = 0.5 + ; }"; }   // a broken edit (different size)
        ShaderFile b = LoadShaderFile(path, 0);
        CHECK(b.generation != a.generation);
        CHECK(b.compiled && !b.compiled->ok);
        { std::ofstream f(path); f << "surface { alpha = 0.75; }"; }
        ShaderFile c = LoadShaderFile(path, 0);
        CHECK(c.compiled && c.compiled->ok);
        std::remove(path.c_str());
        ShaderFile m = LoadShaderFile("does/not/exist.okayshader", 0);
        CHECK(m.compiled && !m.compiled->ok && Has(m.compiled->FirstError(), "cannot read"));
    }

    // ---- the shipped example shaders all compile ---------------------------------------------
    {
        const char* files[] = {"examples/shaders/lava.okayshader", "examples/shaders/dissolve.okayshader",
                               "examples/shaders/rim_glow.okayshader"};
        for (const char* f : files) {
            std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/" + f);
            CHECK(in.good());
            std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            Compiled c = Compile(src);
            CHECK(c.ok);
            if (!c.ok) std::cerr << "    " << c.FirstError(f) << "\n";
        }
    }
    TEST_MAIN_RESULT();
}
