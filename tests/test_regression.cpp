// Tests for the regression helpers (okay/Testing/Regression.hpp): golden text and
// binary files with tolerances, the scene digest, image comparison, PPM/PNG goldens
// and the software-renderer golden image.
//
// The one committed golden is tests/golden/regression/cube.png. After an intended
// change to the software renderer, regenerate it with
//     OKAY_UPDATE_GOLDEN=1 test_regression
// then review the PNG and commit it.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Testing/Regression.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace okay;
using namespace okay::testing;

namespace {

namespace fs = std::filesystem;

std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
void Write(const std::string& path, const std::string& data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << data;
}
bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}

Image Solid(int w, int h, int r, int g, int b) {
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = img.Data() + ((std::size_t)y * w + x) * 4;
            p[0] = (std::uint8_t)r; p[1] = (std::uint8_t)g; p[2] = (std::uint8_t)b; p[3] = 255;
        }
    return img;
}
void SetPx(Image& img, int x, int y, int r, int g, int b) {
    std::uint8_t* p = img.Data() + ((std::size_t)y * img.Width() + x) * 4;
    p[0] = (std::uint8_t)r; p[1] = (std::uint8_t)g; p[2] = (std::uint8_t)b;
}

// A little world to fingerprint.
void BuildWorld(Scene& s, bool reversed = false) {
    auto make = [&](const char* name, Vec3 pos) { GameObject* g = s.CreateGameObject(name); g->transform->localPosition = pos; return g; };
    if (!reversed) {
        make("Cam", {0, 1, -5})->AddComponent<Camera>();
        make("Crate", {2, 0, 0})->AddComponent<MeshRenderer>();
        make("Floor", {0, -1, 0});
    } else {
        make("Floor", {0, -1, 0});
        make("Crate", {2, 0, 0})->AddComponent<MeshRenderer>();
        make("Cam", {0, 1, -5})->AddComponent<Camera>();
    }
}

// The scene behind the committed golden image: a lit cube in front of a camera.
void BuildCubeScene(Scene& s, float cubeX = 0.0f) {
    s.renderSettings.skybox = false;
    GameObject* sun = s.CreateGameObject("Sun");
    auto* light = sun->AddComponent<Light>();
    light->type = Light::Type::Directional;
    light->intensity = 1.0f;
    sun->transform->localRotation = Quat::Euler(40, -35, 0);
    GameObject* cube = s.CreateGameObject("Cube");
    cube->transform->localPosition = {cubeX, 0, 0};
    cube->transform->localRotation = Quat::Euler(20, 30, 0);
    auto* mr = cube->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.5f);
    mr->color = Color::FromBytes(220, 120, 60);
    GameObject* cam = s.CreateGameObject("Camera");
    cam->transform->localPosition = {0, 0.5f, 4.0f};      // cameras look down -Z
    cam->AddComponent<Camera>()->projection = Camera::Projection::Perspective;
}

} // namespace

int main() {
    RUN_SUITE("regression");
    const std::string out = std::string(OKAY_TEST_OUTPUT_DIR) + "/regression";
    fs::remove_all(out);
    fs::create_directories(out);
    const bool callerUpdates = GoldenUpdateRequested();   // OKAY_UPDATE_GOLDEN=1 test_regression refreshes the real golden
    SetEnv("OKAY_UPDATE_GOLDEN", nullptr);       // the checks below start from "compare"

    // --- GoldenUpdateRequested follows the environment ---
    {
        CHECK(!GoldenUpdateRequested());
        SetEnv("OKAY_UPDATE_GOLDEN", "0");
        CHECK(!GoldenUpdateRequested());
        SetEnv("OKAY_UPDATE_GOLDEN", "1");
        CHECK(GoldenUpdateRequested());
        SetEnv("OKAY_UPDATE_GOLDEN", nullptr);
        CHECK(!GoldenUpdateRequested());
    }

    // --- Text comparison ---
    {
        TextGoldenOptions o;
        CHECK(CompareText("a\nb\nc\n", "a\nb\nc\n", o).ok);
        CHECK(CompareText("a\r\nb\r\n", "a\nb\n", o).ok);                  // line endings
        CHECK(CompareText("a\rb\r", "a\nb\n", o).ok);
        CHECK(CompareText("a   \nb\t\n", "a\nb\n", o).ok);                // trailing whitespace
        CHECK(CompareText("a\nb\n\n\n", "a\nb", o).ok);                    // trailing newlines
        CHECK(!CompareText("a\n  b\n", "a\nb\n", o).ok);                   // leading whitespace counts
        TextGoldenOptions strict;
        strict.normalizeLineEndings = strict.trimTrailingWhitespace = strict.ignoreTrailingNewlines = false;
        CHECK(!CompareText("a\r\n", "a\n", strict).ok);
        CHECK(!CompareText("a \n", "a\n", strict).ok);
        CHECK(!CompareText("a\n\n", "a\n", strict).ok);
        CHECK(CompareText("same\n", "same\n", strict).ok);

        GoldenResult r = CompareText("one\ntwo\nTHREE\nfour\n", "one\ntwo\nthree\nfour\n", o);
        CHECK(!r.ok && r.differences == 1);
        CHECK(Contains(r.message, "1 line differ") && Contains(r.message, "line 3:"));
        CHECK(Contains(r.message, "expected: three") && Contains(r.message, "actual:   THREE"));
        r = CompareText("a\nb\n", "a\nb\nc\n", o);
        CHECK(!r.ok && Contains(r.message, "2 lines, golden has 3") && Contains(r.message, "actual:   (no such line)"));
        r = CompareText("a\nb\nc\nd\n", "a\nb\n", o);
        CHECK(!r.ok && r.differences == 2 && Contains(r.message, "expected: (no such line)"));
        // only the first few differences are spelled out
        std::string many, other;
        for (int i = 0; i < 30; ++i) { many += "x" + std::to_string(i) + "\n"; other += "y" + std::to_string(i) + "\n"; }
        r = CompareText(many, other, o);
        CHECK(r.differences == 30 && Contains(r.message, "and 20 more"));

        // numbers: absolute and relative tolerance
        TextGoldenOptions num;
        num.numberTolerance = 0.01;
        CHECK(CompareText("pos 1.004 2.000 -3.001\n", "pos 1.000 2.00 -3.0\n", num).ok);
        CHECK(!CompareText("pos 1.02 2.0\n", "pos 1.00 2.0\n", num).ok);
        CHECK(!CompareText("pos 1.0 2.0\n", "dir 1.0 2.0\n", num).ok);        // the words must still match
        CHECK(!CompareText("pos 1.0 2.0 3.0\n", "pos 1.0 2.0\n", num).ok);   // and so must the count of numbers
        CHECK(CompareText("1e-3\n", "0.0011\n", num).ok);                     // exponents are numbers
        TextGoldenOptions rel;
        rel.relativeTolerance = 0.01;
        CHECK(CompareText("total 1005\n", "total 1000\n", rel).ok);            // 0.5%
        CHECK(!CompareText("total 1020\n", "total 1000\n", rel).ok);           // 2%
        CHECK(CompareText("v 0.0\n", "v 0\n", rel).ok);
        // digits inside words are text, not numbers
        CHECK(!CompareText("Cube2 at 1.0\n", "Cube3 at 1.0\n", num).ok);
        CHECK(CompareText("Cube2 at 1.0\n", "Cube2 at 1.004\n", num).ok);
        CHECK(!CompareText("x=1.5f\n", "x=1.6f\n", num).ok);                  // "1.5f" is a token of text: exact
        CHECK(CompareText("1.5 - 2.5\n", "1.50 - 2.50\n", num).ok);
        CHECK(CompareText("a-1\n", "a-1\n", num).ok);
        CHECK(CompareText("pos=-1.5,2\n", "pos=-1.5,2.004\n", num).ok);
    }

    // --- Text golden files: missing, update, compare, env var ---
    {
        const std::string g = out + "/text/golden.txt";
        GoldenResult r = CompareTextToGolden("hello\n", g);
        CHECK(!r.ok && r.missing && Contains(r.message, "OKAY_UPDATE_GOLDEN"));
        TextGoldenOptions up;
        up.update = true;
        r = CompareTextToGolden("hello\n", g, up);                        // creates the folder and the file
        CHECK(r.ok && r.updated && Slurp(g) == "hello\n");
        r = CompareTextToGolden("hello\n", g);
        CHECK(r.ok && !r.updated && !r.missing);
        r = CompareTextToGolden("bye\n", g);
        CHECK(!r.ok && !r.missing && Contains(r.message, "differs from") && Contains(r.message, "line 1:"));
        SetEnv("OKAY_UPDATE_GOLDEN", "1");
        r = CompareTextToGolden("bye\n", g);                              // the environment asks for an update
        CHECK(r.ok && r.updated && Slurp(g) == "bye\n");
        SetEnv("OKAY_UPDATE_GOLDEN", nullptr);
        CHECK(CompareTextToGolden("bye\n", g).ok);
        // an unwritable target is reported, not ignored
        r = CompareTextToGolden("x", out + "/text/golden.txt/inside_a_file.txt", up);
        CHECK(!r.ok && Contains(r.message, "cannot write"));
    }

    // --- Binary comparison ---
    {
        std::vector<std::uint8_t> a = {1, 2, 3, 4, 5, 6}, b = a;
        BinaryGoldenOptions o;
        CHECK(CompareBinary(a, b, o).ok);
        b[2] = 4;
        GoldenResult r = CompareBinary(a, b, o);
        CHECK(!r.ok && r.differences == 1 && Contains(r.message, "byte 2: 3 vs golden 4"));
        o.byteTolerance = 1;
        CHECK(CompareBinary(a, b, o).ok);                                  // within a level
        b[2] = 9;
        CHECK(!CompareBinary(a, b, o).ok);                                 // beyond it
        o.maxDifferingBytes = 1;
        CHECK(CompareBinary(a, b, o).ok);                                  // one stray byte allowed
        b[3] = 99;
        CHECK(!CompareBinary(a, b, o).ok);                                 // two are not
        std::vector<std::uint8_t> shorter(a.begin(), a.end() - 1);
        r = CompareBinary(a, shorter, BinaryGoldenOptions{});
        CHECK(!r.ok && Contains(r.message, "size differs: 6 bytes, golden has 5"));
        CHECK(CompareBinary({}, {}, BinaryGoldenOptions{}).ok);

        // float32 mode
        auto floats = [](std::initializer_list<float> vs) {
            std::vector<std::uint8_t> v(vs.size() * 4);
            std::size_t i = 0;
            for (float f : vs) { std::memcpy(&v[i * 4], &f, 4); ++i; }
            return v;
        };
        BinaryGoldenOptions fo;
        fo.asFloat32 = true; fo.floatTolerance = 0.01f;
        CHECK(CompareBinary(floats({1.0f, 2.0f, 3.0f}), floats({1.005f, 2.0f, 2.995f}), fo).ok);
        r = CompareBinary(floats({1.0f, 2.0f, 3.0f}), floats({1.0f, 2.5f, 3.0f}), fo);
        CHECK(!r.ok && Contains(r.message, "float 1:") && r.differences == 1);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(CompareBinary(floats({nan, 1.0f}), floats({nan, 1.0f}), fo).ok);          // NaN matches NaN
        CHECK(!CompareBinary(floats({nan, 1.0f}), floats({0.0f, 1.0f}), fo).ok);
        // a tail shorter than a float is compared exactly
        std::vector<std::uint8_t> t1 = floats({1.0f}), t2 = floats({1.0f});
        t1.push_back(7); t2.push_back(8);
        CHECK(!CompareBinary(t1, t2, fo).ok);
        t2.back() = 7;
        CHECK(CompareBinary(t1, t2, fo).ok);

        // golden files
        const std::string g = out + "/bin/golden.bin";
        BinaryGoldenOptions up;
        up.update = true;
        CHECK(CompareBinaryToGolden(a, g).missing);
        r = CompareBinaryToGolden(a, g, up);
        CHECK(r.ok && r.updated && Slurp(g).size() == 6);
        CHECK(CompareBinaryToGolden(a, g).ok);
        r = CompareBinaryToGolden(shorter, g);
        CHECK(!r.ok && Contains(r.message, "binary differs from"));
    }

    // --- Scene digest: stable, order independent, rounded ---
    {
        Scene a("A"), b("B");
        BuildWorld(a);
        BuildWorld(b, true);                                                 // same things created in another order
        SceneDigest da = DigestScene(a), db = DigestScene(b);
        CHECK(da.hash == db.hash && da.lines == db.lines);
        CHECK(da.lines.size() == 3);
        CHECK(da.HashHex().size() == 16 && da.HashHex().find_first_not_of("0123456789abcdef") == std::string::npos);
        CHECK(HashScene(a) == da.hash);
        CHECK(da.ToText().compare(0, 10 + 16 + 1, "scenehash " + da.HashHex() + "\n") == 0);
        // lines are sorted and spell out the state
        CHECK(da.lines[0].compare(0, 4, "Cam|") == 0 && da.lines[1].compare(0, 6, "Crate|") == 0);
        CHECK(da.lines[1] == "Crate|active=1|layer=0|tag=|pos=2.000,0.000,0.000|rot=0.000,0.000,0.000,1.000|scale=1.000,1.000,1.000|components=MeshRenderer");
        CHECK(Contains(da.lines[0], "components=Camera"));
        CHECK(Contains(da.lines[2], "components="));                         // none: an empty list

        // moves below the rounding step do not matter; a real move does
        a.Find("Crate")->transform->localPosition = {2.0002f, 0.0f, 0.0f};
        CHECK(DigestScene(a).hash == da.hash);
        a.Find("Crate")->transform->localPosition = {2.01f, 0.0f, 0.0f};
        CHECK(DigestScene(a).hash != da.hash);
        SceneHashOptions coarse;
        coarse.decimals = 1;
        a.Find("Crate")->transform->localPosition = {2.01f, 0.0f, 0.0f};
        CHECK(DigestScene(a, coarse).hash == DigestScene(b, coarse).hash);      // 2.0 either way at one decimal
        a.Find("Crate")->transform->localPosition = {2.0f, 0.0f, 0.0f};
        CHECK(DigestScene(a).hash == da.hash);

        // negative zero never shows
        a.Find("Floor")->transform->localPosition = {-0.0001f, -1.0f, -0.0f};
        const SceneDigest dz = DigestScene(a);
        CHECK(!Contains(dz.ToText(), "-0.000"));
        CHECK(dz.hash == da.hash);

        // components, names, activity, layer, tag and parenting all change it
        const std::uint64_t base = DigestScene(b).hash;
        b.Find("Floor")->AddComponent<Light>();
        CHECK(DigestScene(b).hash != base);
        b.Find("Floor")->RemoveComponent<Light>();
        CHECK(DigestScene(b).hash == base);
        b.Find("Floor")->name = "Ground";
        CHECK(DigestScene(b).hash != base);
        b.Find("Ground")->name = "Floor";
        b.Find("Floor")->active = false;
        CHECK(DigestScene(b).hash != base);
        SceneHashOptions noInactive;
        noInactive.includeInactive = false;
        CHECK(DigestScene(b, noInactive).lines.size() == 2);
        b.Find("Floor")->active = true;
        b.Find("Floor")->layer = 5;
        CHECK(DigestScene(b).hash != base);
        SceneHashOptions noLayer;
        noLayer.includeLayerAndTag = false;
        CHECK(DigestScene(b, noLayer).hash == DigestScene(a, noLayer).hash);
        b.Find("Floor")->layer = 0;
        b.Find("Floor")->tag = "Ground";
        CHECK(DigestScene(b).hash != base);
        b.Find("Floor")->tag.clear();
        b.Find("Floor")->transform->SetParent(b.Find("Crate")->transform, false);
        CHECK(DigestScene(b).hash != base);                                  // its path (and world pose) changed
        SceneHashOptions noComps;
        noComps.includeComponents = false;
        CHECK(DigestScene(a, noComps).lines[1].find("components") == std::string::npos);

        // world pose, not local: a parent's move carries the child
        Scene p("P");
        GameObject* parent = p.CreateGameObject("Parent");
        GameObject* child = p.CreateGameObject("Child");
        child->transform->SetParent(parent->transform, false);
        child->transform->localPosition = {1, 0, 0};
        parent->transform->localPosition = {5, 0, 0};
        const SceneDigest dp = DigestScene(p);
        CHECK(Contains(dp.ToText(), "Parent/Child|active=1|layer=0|tag=|pos=6.000,0.000,0.000"));

        // duplicate paths are told apart, in scene order
        Scene dup("Dup");
        dup.CreateGameObject("Same")->transform->localPosition = {1, 0, 0};
        dup.CreateGameObject("Same")->transform->localPosition = {2, 0, 0};
        const SceneDigest dd = DigestScene(dup);
        CHECK(dd.lines.size() == 2 && dd.lines[0].compare(0, 7, "Same#2|") == 0 && dd.lines[1].compare(0, 5, "Same|") == 0);   // '#' sorts before '|'

        // q and -q are one rotation
        Scene q1("Q1"), q2("Q2");
        GameObject* o1 = q1.CreateGameObject("R"); GameObject* o2 = q2.CreateGameObject("R");
        const Quat q = Quat::Euler(10, 40, 70);
        o1->transform->localRotation = q;
        o2->transform->localRotation = Quat{-q.x, -q.y, -q.z, -q.w};
        CHECK(DigestScene(q1).hash == DigestScene(q2).hash);

        // transient and destroyed objects stay out unless asked for
        Scene tr("T");
        tr.CreateGameObject("Real");
        tr.CreateGameObject("Generated")->transient = true;
        CHECK(DigestScene(tr).lines.size() == 1);
        SceneHashOptions withTransient;
        withTransient.includeTransient = true;
        CHECK(DigestScene(tr, withTransient).lines.size() == 2);
        GameObject* gone = tr.CreateGameObject("Gone");
        tr.Destroy(gone);
        tr.FlushDestroyed();
        CHECK(DigestScene(tr).lines.size() == 1);

        // NaN / inf are spelled out, not undefined
        Scene weird("W");
        weird.CreateGameObject("N")->transform->localPosition = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
        CHECK(Contains(DigestScene(weird).lines[0], "pos=nan,inf,-inf"));

        // an empty scene has a hash too, and it is stable
        Scene e1("E1"), e2("E2");
        CHECK(DigestScene(e1).hash == DigestScene(e2).hash && DigestScene(e1).lines.empty());

        // DiffDigests lists what changed
        Scene c("C");
        BuildWorld(c);
        const SceneDigest before = DigestScene(c);
        c.Find("Crate")->transform->localPosition = {3, 0, 0};
        c.CreateGameObject("Extra");
        const std::string diff = DiffDigests(before, DigestScene(c));
        CHECK(Contains(diff, "- Crate|") && Contains(diff, "+ Crate|") && Contains(diff, "pos=3.000,0.000,0.000") && Contains(diff, "+ Extra|"));
        CHECK(!Contains(diff, "Cam|"));
        CHECK(DiffDigests(before, before).empty());
        CHECK(Contains(DiffDigests(DigestScene(e1), DigestScene(dup), 1), "... and 1 more"));
    }

    // --- Scene golden file: the same scene passes, a change fails with a readable diff ---
    {
        const std::string g = out + "/scene/world.txt";
        Scene s("World");
        BuildWorld(s);
        GoldenResult r = CheckSceneGolden(s, g);
        CHECK(r.missing && !r.ok);
        r = CheckSceneGolden(s, g, {}, /*update=*/true);
        CHECK(r.ok && r.updated);
        CHECK(Slurp(g).compare(0, 10, "scenehash ") == 0);
        CHECK(CheckSceneGolden(s, g).ok);
        Scene again("Another name, same world");
        BuildWorld(again, true);
        CHECK(CheckSceneGolden(again, g).ok);
        s.Find("Crate")->transform->localPosition = {2, 5, 0};
        r = CheckSceneGolden(s, g);
        CHECK(!r.ok && Contains(r.message, "no longer matches") && Contains(r.message, "Crate|"));
    }

    // --- Image comparison ---
    {
        Image a = Solid(8, 8, 100, 100, 100), b = Solid(8, 8, 100, 100, 100);
        ImageCompareOptions o;
        ImageCompareResult r = CompareImages(a, b, o);
        CHECK(r.match && r.differingPixels == 0 && r.maxChannelDiff == 0 && r.meanChannelDiff == 0.0 && !r.sizeMismatch);
        CHECK(r.diff.Width() == 8 && r.diff.Height() == 8);

        SetPx(b, 3, 4, 110, 100, 100);                                       // one pixel off by 10
        r = CompareImages(a, b, o);
        CHECK(!r.match && r.differingPixels == 1 && r.maxChannelDiff == 10);
        CHECK(Contains(r.message, "1 of 64 pixels"));
        CHECK_NEAR(r.meanChannelDiff, 10.0 / (64.0 * 3.0), 1e-9);
        // the diff image paints that pixel red and dims the rest
        CHECK(r.diff.Data()[(4 * 8 + 3) * 4 + 0] > 128 && r.diff.Data()[(4 * 8 + 3) * 4 + 1] == 0 && r.diff.Data()[(4 * 8 + 3) * 4 + 2] == 0);
        CHECK(r.diff.Data()[0] == r.diff.Data()[1] && r.diff.Data()[0] < 100);
        o.channelTolerance = 10;
        CHECK(CompareImages(a, b, o).match);                                  // within the tolerance
        o.channelTolerance = 9;
        CHECK(!CompareImages(a, b, o).match);
        o.maxDifferingPixels = 1;
        CHECK(CompareImages(a, b, o).match);                                  // one stray pixel is allowed
        o.maxDifferingPixels = 0;
        o.maxDifferingFraction = 1.0 / 64.0;
        CHECK(CompareImages(a, b, o).match);                                  // ...or a fraction that covers it
        o.maxDifferingFraction = 0.5 / 64.0;
        CHECK(!CompareImages(a, b, o).match);

        // alpha counts only on request
        Image c = Solid(4, 4, 10, 20, 30);
        Image d = Solid(4, 4, 10, 20, 30);
        d.Data()[3] = 0;
        ImageCompareOptions alpha;
        CHECK(CompareImages(c, d, alpha).match);
        alpha.compareAlpha = true;
        CHECK(!CompareImages(c, d, alpha).match);

        // a different size never matches, and the diff covers both
        r = CompareImages(Solid(8, 8, 0, 0, 0), Solid(10, 6, 0, 0, 0), ImageCompareOptions{});
        CHECK(!r.match && r.sizeMismatch && Contains(r.message, "8x8") && Contains(r.message, "10x6"));
        CHECK(r.diff.Width() == 10 && r.diff.Height() == 8);
        // empty images are equal to each other
        CHECK(CompareImages(Image(), Image(), ImageCompareOptions{}).match);
    }

    // --- Image files: PPM and PNG round trips ---
    {
        Image img(5, 3);
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x) SetPx(img, x, y, x * 50, y * 100, (x + y) * 20);
        for (int i = 0; i < 15; ++i) img.Data()[i * 4 + 3] = 255;
        const std::string ppm = out + "/img/a.ppm", png = out + "/img/a.png";
        CHECK(SaveImageFile(img, ppm));
        CHECK(SaveImageFile(img, png));
        CHECK(Slurp(ppm).compare(0, 11, "P6\n5 3\n255\n") == 0 && Slurp(ppm).size() == 11 + 5 * 3 * 3);
        Image fromPpm, fromPng;
        std::string err;
        CHECK(LoadImageFile(ppm, fromPpm, &err) && LoadImageFile(png, fromPng, &err));
        CHECK(CompareImages(img, fromPpm, ImageCompareOptions{}).match);
        CHECK(CompareImages(img, fromPng, ImageCompareOptions{}).match);
        // ASCII PPM with a comment, and the failures
        Write(out + "/img/ascii.ppm", "P3\n# a comment\n2 1\n255\n255 0 0  0 0 255\n");
        Image asc;
        CHECK(LoadImageFile(out + "/img/ascii.ppm", asc, &err) && asc.Width() == 2 && asc.Data()[0] == 255 && asc.Data()[6] == 255);
        Image bad;
        CHECK(!LoadImageFile(out + "/img/none.ppm", bad, &err) && Contains(err, "cannot open"));
        Write(out + "/img/trunc.ppm", "P6\n4 4\n255\nabc");
        CHECK(!LoadImageFile(out + "/img/trunc.ppm", bad, &err) && Contains(err, "truncated"));
        Write(out + "/img/notppm.ppm", "hello");
        CHECK(!LoadImageFile(out + "/img/notppm.ppm", bad, &err));
        CHECK(!SaveImageFile(Image(), out + "/img/empty.ppm"));
    }

    // --- Image goldens: missing, update, compare, diff file ---
    {
        const std::string g = out + "/gold/pic.png", diffPath = out + "/gold/pic_diff.png";
        Image pic = Solid(16, 16, 40, 90, 160);
        GoldenResult r = CheckImageGolden(pic, g);
        CHECK(!r.ok && r.missing && Contains(r.message, "OKAY_UPDATE_GOLDEN"));
        r = CheckImageGolden(pic, g, ImageCompareOptions{}, "", /*update=*/true);
        CHECK(r.ok && r.updated && fs::exists(g));
        CHECK(CheckImageGolden(pic, g).ok);
        Image off = pic;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) SetPx(off, x, y, 255, 255, 255);   // a 4x4 patch
        r = CheckImageGolden(off, g, ImageCompareOptions{}, diffPath);
        CHECK(!r.ok && r.differences == 16 && Contains(r.message, "16 of 256 pixels") && Contains(r.message, "diff image:"));
        CHECK(fs::exists(diffPath));
        Image diff;
        CHECK(LoadImageFile(diffPath, diff));
        CHECK(diff.Width() == 16 && diff.Data()[0] > 128 && diff.Data()[1] == 0);   // red where it differs
        ImageCompareOptions lenient;
        lenient.maxDifferingPixels = 16;
        CHECK(CheckImageGolden(off, g, lenient).ok);
        // the wrong size is reported as such
        r = CheckImageGolden(Solid(8, 8, 1, 1, 1), g);
        CHECK(!r.ok && Contains(r.message, "8x8"));
        // a PPM golden works the same way
        const std::string gp = out + "/gold/pic.ppm";
        CHECK(CheckImageGolden(pic, gp, ImageCompareOptions{}, "", true).updated);
        CHECK(CheckImageGolden(pic, gp).ok);
        SetEnv("OKAY_UPDATE_GOLDEN", "1");
        CHECK(CheckImageGolden(off, g).updated);
        SetEnv("OKAY_UPDATE_GOLDEN", nullptr);
        CHECK(CheckImageGolden(off, g).ok);
        // a file that is not an image is "missing" with the reason
        Write(out + "/gold/garbage.png", "not a png");
        r = CheckImageGolden(pic, out + "/gold/garbage.png");
        CHECK(!r.ok && r.missing);
    }

    // --- Rendering a scene with the software renderer ---
    {
        Scene s("Cube");
        BuildCubeScene(s);
        SceneRenderOptions ro;
        ro.width = 96; ro.height = 64;
        Image img;
        std::string err;
        CHECK(RenderSceneToImage(s, img, ro, &err));
        CHECK(img.Width() == 96 && img.Height() == 64 && img.Valid());
        // the cube is drawn in the middle and the corners show the background
        auto px = [&](int x, int y, int c) { return (int)img.Data()[((std::size_t)y * img.Width() + x) * 4 + (std::size_t)c]; };
        CHECK(px(0, 0, 0) == 166 && px(0, 0, 1) == 206 && px(0, 0, 2) == 235 && px(0, 0, 3) == 255);
        CHECK(px(95, 63, 0) == 166);
        CHECK(px(48, 32, 0) != 166 || px(48, 32, 1) != 206);
        CHECK(px(48, 32, 0) > px(48, 32, 2));                                   // orange cube: red beats blue
        // a repeat is pixel for pixel the same
        Image again;
        CHECK(RenderSceneToImage(s, again, ro, &err));
        CHECK(CompareImages(img, again, ImageCompareOptions{}).match);
        // moving the cube changes the picture
        Scene moved("Moved");
        BuildCubeScene(moved, 1.2f);
        Image other;
        CHECK(RenderSceneToImage(moved, other, ro, &err));
        ImageCompareResult cmp = CompareImages(img, other, ImageCompareOptions{});
        CHECK(!cmp.match && cmp.differingPixels > 100);
        // a different background colour shows where nothing is drawn
        SceneRenderOptions night = ro;
        night.background = Color::FromBytes(5, 5, 20);
        Image dark;
        CHECK(RenderSceneToImage(s, dark, night));
        CHECK(dark.Data()[0] == 5 && dark.Data()[2] == 20);
        // supersampling keeps the size
        SceneRenderOptions ss = ro;
        ss.supersample = 2;
        Image smooth;
        CHECK(RenderSceneToImage(s, smooth, ss));
        CHECK(smooth.Width() == 96 && smooth.Height() == 64);
        // an explicit camera, and none at all
        Camera* cam = s.Find("Camera")->GetComponent<Camera>();
        SceneRenderOptions explicitCam = ro;
        explicitCam.camera = cam;
        Image viaCam;
        CHECK(RenderSceneToImage(s, viaCam, explicitCam) && CompareImages(img, viaCam, ImageCompareOptions{}).match);
        Scene blind("Blind");
        blind.CreateGameObject("Thing")->AddComponent<MeshRenderer>();
        Image none;
        std::string why;
        CHECK(!RenderSceneToImage(blind, none, ro, &why) && Contains(why, "no enabled camera"));
        s.Find("Camera")->GetComponent<Camera>()->enabled = false;
        CHECK(!RenderSceneToImage(s, none, ro, &why));
        // absurd sizes are clamped, not crashed on
        Scene tiny("Tiny");
        BuildCubeScene(tiny);
        SceneRenderOptions silly;
        silly.width = 0; silly.height = -5;
        Image one;
        CHECK(RenderSceneToImage(tiny, one, silly) && one.Width() == 1 && one.Height() == 1);
    }

    // --- The committed golden image: a regression here means the renderer's output changed ---
    {
        Scene s("Cube");
        BuildCubeScene(s);
        SceneRenderOptions ro;
        ro.width = 128; ro.height = 80;
        Image img;
        CHECK(RenderSceneToImage(s, img, ro));
        ImageCompareOptions tol;
        tol.channelTolerance = 6;                 // compilers round shading a little differently
        tol.maxDifferingFraction = 0.01;
        const std::string golden = std::string(OKAY_SOURCE_DIR) + "/tests/golden/regression/cube.png";
        const std::string diff = out + "/cube_diff.png";
        if (callerUpdates) SetEnv("OKAY_UPDATE_GOLDEN", "1");
        GoldenResult r = CheckImageGolden(img, golden, tol, diff);
        if (callerUpdates) { SetEnv("OKAY_UPDATE_GOLDEN", nullptr); std::cout << "  golden image refreshed: " << golden << "\n"; }
        if (!r.ok) std::cerr << "  " << r.message << "\n";
        CHECK(r.ok);
    }

    TEST_MAIN_RESULT();
}
