// Prefs escaping/precision (item 14) and the low-severity robustness items
// (item 15): TweenSequence self-containment, Mat4::Inverse on tiny but valid
// matrices, and signed-overflow-free hash functions (run under UBSan).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/AI/NavGrid3D.hpp"
#include "okay/Render/Gltf.hpp"
#include <cstdio>
#include <fstream>

using namespace okay;

int main() {
    RUN_SUITE("prefs_misc");

    // ---------------- Prefs: tabs / CRs in keys and values ----------------
    {
        Prefs::Clear();
        Prefs::SetString("key\twith tab", "value\twith\ttabs");
        Prefs::SetString("cr", "a\rb\r\n");
        Prefs::SetFloat("tiny", 1e-7f);
        Prefs::SetFloat("tenth", 0.1f);
        Prefs::SetFloat("big", 123456.789f);
        const char* path = "test_prefs_misc.tmp";
        CHECK(Prefs::Save(path));
        Prefs::Clear();
        CHECK(Prefs::Load(path));
        CHECK(Prefs::Has("key\twith tab"));
        CHECK(Prefs::GetString("key\twith tab") == "value\twith\ttabs");
        CHECK(Prefs::GetString("cr") == "a\rb\r\n");
        CHECK(Prefs::GetFloat("tiny") == 1e-7f);          // bit-exact round trip
        CHECK(Prefs::GetFloat("tenth") == 0.1f);
        CHECK(Prefs::GetFloat("big") == 123456.789f);
        std::remove(path);
    }
    // ---------------- Prefs: files written by the old format still load ----------------
    {
        const char* path = "test_prefs_misc_old.tmp";
        {
            std::ofstream f(path, std::ios::binary);
            f << "okayprefs 1\n"
              << "volume\t0.750000\n"                     // old std::to_string float
              << "score\t1234\n"
              << "greeting\tline1\\nline2\twith\\\\slash\n" // old: raw tab inside the value
              << "lit\tback\\\\t\n";                       // an escaped backslash then 't'
        }
        Prefs::Clear();
        CHECK(Prefs::Load(path));
        CHECK_NEAR(Prefs::GetFloat("volume"), 0.75f, 1e-6f);
        CHECK(Prefs::GetInt("score") == 1234);
        CHECK(Prefs::GetString("greeting") == "line1\nline2\twith\\slash");
        CHECK(Prefs::GetString("lit") == "back\\t");
        std::remove(path);
        Prefs::Clear();
    }

    // ---------------- TweenSequence can't contain itself ----------------
    {
        TweenManager m;
        auto a = m.Sequence();
        auto b = m.Sequence();
        auto c = m.Sequence();
        a->Append(m.Virtual(0.0f, 1.0f, 0.5f, [](float) {}));
        a->Append(b);
        b->Append(c);
        CHECK(a->Count() == 2);
        c->Append(a);                                     // a -> b -> c -> a: refused
        CHECK(c->Count() == 0);
        b->Join(a);                                       // a -> b -> a: refused
        CHECK(b->Count() == 1);
        a->Append(a);                                     // direct: refused
        CHECK(a->Count() == 2);
        for (int i = 0; i < 10; ++i) m.Update(0.1f, 0.1f);   // plays without recursing forever
    }

    // ---------------- Mat4::Inverse: relative singularity threshold ----------------
    {
        Mat4 s = Mat4::Scale({1e-4f, 1e-4f, 1e-4f});
        Mat4 inv = s.Inverse();
        Mat4 id = s * inv;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK_NEAR(id.at(r, c), r == c ? 1.0f : 0.0f, 1e-4f);
        CHECK_NEAR(inv.at(0, 0), 1e4f, 1.0f);
        Mat4 big = Mat4::TRS({1000.0f, -50.0f, 3.0f}, Quat::Euler(10.0f, 20.0f, 30.0f), {1e-4f, 2e-4f, 1e-4f});
        Mat4 id2 = big * big.Inverse();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK_NEAR(id2.at(r, c), r == c ? 1.0f : 0.0f, 1e-3f);
        // Truly singular (and NaN) matrices still return identity.
        Mat4 z = Mat4::Scale({1.0f, 0.0f, 1.0f});
        Mat4 zi = z.Inverse();
        CHECK(zi.at(0, 0) == 1.0f && zi.at(1, 1) == 1.0f);
        Mat4 n = Mat4::Scale({std::nanf(""), 1.0f, 1.0f});
        CHECK(n.Inverse().at(0, 0) == 1.0f);
    }

    // ---------------- Hashes: no signed overflow (UBSan) ----------------
    {
        Mesh m = Mesh::Sphere(1.0f, 16, 24);
        m.triColors.assign(m.TriangleCount(), Color{0.5f, 0.5f, 0.5f, 1.0f});
        m.MottleFaceColors(0.2f, 12345);
        m.JitterVertices({}, 0.01f, 777);
        m.Displace(0.05f, 3.0f, 99);
        CHECK(!m.vertices.empty());

        std::vector<std::uint8_t> out;
        std::string b64;
        for (int i = 0; i < 64; ++i) b64 += "QUJDRA==";    // "ABCD" repeated
        gltf_detail::Base64Decode(b64, out);
        CHECK(out.size() == 64 * 4);
        CHECK(out.size() >= 4 && out[0] == 'A' && out[3] == 'D');

        Scene s("h");
        GameObject* cam = s.CreateGameObject("cam");
        auto* vc = cam->AddComponent<VirtualCamera>();
        vc->shakeAmplitude = 0.5f;
        vc->shakeFrequency = 25.0f;
        vc->AddImpulse(1.0f);
        s.Start();
        for (int i = 0; i < 20; ++i) vc->Solve(0.1f);

        GameObject* vt = s.CreateGameObject("vox");
        auto* v = vt->AddComponent<VoxelTerrain>();
        std::string enc = v->EncodeDensity();
        CHECK(v->DecodeDensity(enc));

        auto path = NavGrid3D::FindPath(s, {0, 0, 0}, {-6.0f, 0, -6.0f}, nullptr, 0.75f, 0.4f, 256);
        (void)path;
    }

    TEST_MAIN_RESULT();
}
