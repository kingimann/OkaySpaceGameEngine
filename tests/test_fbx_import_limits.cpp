#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelImport.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

// Hostile FBX files through the importer, on the DEFAULT path (ufbx) and with our own reader switched on
// (which hands a refused file to ufbx): a Model parent chain far deeper than any stack, files that
// declare too many animation stacks / layers / curves, and many takes pointing at the same curves. The
// import must fail cleanly (ok == false, no crash, no unbounded allocation). Files are written to the
// temp directory, never the source tree.
using namespace okay;
namespace fs = std::filesystem;

static fs::path TempFile(const char* tag) {
    return fs::temp_directory_path() / (std::string("okay_fbx_limits_") + tag + "_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()) + ".fbx");
}
static const char* kHeader =
    "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n";

// n Models in one parent chain (flat element tree: the depth lives in Connections only).
static std::string ChainFbx(int n) {
    std::string s = kHeader; s += "Objects:  {\n";
    for (int i = 1; i <= n; ++i) s += "\tModel: " + std::to_string(i) + ", \"Model::M" + std::to_string(i) + "\", \"Null\" {\n\t}\n";
    s += "}\nConnections:  {\n\tC: \"OO\", 1, 0\n";
    for (int i = 2; i <= n; ++i) s += "\tC: \"OO\", " + std::to_string(i) + ", " + std::to_string(i - 1) + "\n";
    return s + "}\n";
}
// One animated model: `stacks` takes sharing one layer and `curveNodes` curve nodes (translation, rotation,
// scale), each with one curve running `spanSec` seconds. `extraLayers` empty layers and `extraCurves` bare
// curves pad the counts. The curves are cubic with `keys` keys, so ufbx resamples them at 30 Hz (at most 32 samples per key).
static std::string AnimFbx(int stacks, int extraLayers, int extraCurves, double spanSec, int curveNodes = 1, int keys = 2) {
    static const char* kProp[3] = {"Lcl Translation", "Lcl Rotation", "Lcl Scaling"};
    std::string s = kHeader; s += "Objects:  {\n\tModel: 1, \"Model::M\", \"Null\" {\n\t}\n";
    for (int i = 0; i < stacks; ++i) s += "\tAnimationStack: " + std::to_string(100 + i) + ", \"AnimStack::T" + std::to_string(i) + "\", \"\" {\n\t}\n";
    s += "\tAnimationLayer: 9000, \"AnimLayer::L\", \"\"\n";
    for (int i = 0; i < extraLayers; ++i) s += "\tAnimationLayer: " + std::to_string(10000 + i) + ", \"AnimLayer::E\", \"\"\n";
    for (int k = 0; k < curveNodes; ++k) {
        s += "\tAnimationCurveNode: " + std::to_string(20 + k) + ", \"AnimCurveNode::N\", \"\" {\n\t\tProperties70:  {\n\t\t\tP: \"d|X\", \"Number\", \"\", \"A\", 0.0\n\t\t\tP: \"d|Y\", \"Number\", \"\", \"A\", 0.0\n\t\t\tP: \"d|Z\", \"Number\", \"\", \"A\", 0.0\n\t\t}\n\t}\n";
        std::string kt, kv;
        for (int i = 0; i < keys; ++i) { kt += (i ? "," : "") + std::to_string((long long)(spanSec * 46186158000.0 * i / (keys - 1))); kv += (i ? "," : "") + std::string(i & 1 ? "10.0" : "0.0"); }
        s += "\tAnimationCurve: " + std::to_string(30 + k) + ", \"AnimCurve::x\", \"\" {\n\t\tKeyVer: 4008\n\t\tKeyTime: *" + std::to_string(keys) + " {\n\t\t\ta: " + kt + "\n\t\t}\n"
             "\t\tKeyValueFloat: *" + std::to_string(keys) + " {\n\t\t\ta: " + kv + "\n\t\t}\n\t\tKeyAttrFlags: *1 {\n\t\t\ta: 1032\n\t\t}\n\t\tKeyAttrDataFloat: *4 {\n\t\t\ta: 0,0,0,0\n\t\t}\n"
             "\t\tKeyAttrRefCount: *1 {\n\t\t\ta: " + std::to_string(keys) + "\n\t\t}\n\t}\n";
    }
    for (int i = 0; i < extraCurves; ++i)   // (valid one-key curves, nothing connected to them)
        s += "\tAnimationCurve: " + std::to_string(500000 + i) + ", \"c\", \"\" {KeyTime: *1 {a: 0} KeyValueFloat: *1 {a: 0} KeyAttrFlags: *1 {a: 4} KeyAttrDataFloat: *4 {a: 0,0,0,0} KeyAttrRefCount: *1 {a: 1}}\n";
    s += "}\nConnections:  {\n\tC: \"OO\", 1, 0\n";
    for (int k = 0; k < curveNodes; ++k)
        s += "\tC: \"OP\", " + std::to_string(20 + k) + ", 1, \"" + kProp[k] + "\"\n\tC: \"OP\", " + std::to_string(30 + k) + ", " + std::to_string(20 + k) + ", \"d|X\"\n\tC: \"OO\", " + std::to_string(20 + k) + ", 9000\n";
    for (int i = 0; i < stacks; ++i) s += "\tC: \"OO\", 9000, " + std::to_string(100 + i) + "\n";
    return s + "}\n";
}

// Imports `text` with ufbx (own = false) or with our reader on (own = true, ufbx behind it); returns ok.
static bool Import(const std::string& text, bool own, const char* tag, int* clips = nullptr) {
    const fs::path f = TempFile(tag);
    { std::ofstream o(f, std::ios::binary); o.write(text.data(), (std::streamsize)text.size()); }
    fbx::SetOwnReaderEnabled(own);
    Scene sc("limits"); bool ok = false;
    GameObject* root = ImportModelScene(sc, f.string(), &ok);
    fbx::SetOwnReaderEnabled(false);
    if (clips) { auto* ma = root ? root->GetComponent<ModelAnimator>() : nullptr; *clips = ma ? (int)ma->clips.size() : 0; }
    std::error_code ec; fs::remove(f, ec);
    CHECK(ok == (root != nullptr));   // a failed import hands back nothing
    return ok && root;
}

int main() {
    RUN_SUITE("fbx import: deep node chain (flat file, depth only in Connections)");
    for (bool own : {false, true}) {
        CHECK(!Import(ChainFbx(100000), own, "deep"));                    // far past the stack: refused, not recursed
        CHECK(!Import(ChainFbx(3000), own, "deep3k"));                    // past the depth cap (2048)
        int clips = 0;
        CHECK(Import(ChainFbx(1500), own, "ok1500", &clips));            // a deep but legal chain still imports (iteratively)
    }

    RUN_SUITE("fbx import: animation counts");
    const fbx::Limits lim;
    for (bool own : {false, true}) {
        int clips = 0;
        CHECK(Import(AnimFbx(3, 0, 0, 10.0), own, "sane", &clips) && clips == 3);
        CHECK(!Import(AnimFbx((int)lim.maxAnimStacks + 1, 0, 0, 1.0), own, "stacks"));
        CHECK(!Import(AnimFbx(1, (int)lim.maxAnimLayers, 0, 1.0), own, "layers"));
        CHECK(!Import(AnimFbx(1, 0, (int)lim.maxAnimCurves, 1.0), own, "curves"));
    }

    RUN_SUITE("fbx import: whole-file bake budget (many takes sharing one curve)");
    for (bool own : {false, true}) {
        // every take alone bakes fine; 128 of them with three curve nodes of 1000 keys over 1000 s are millions of baked keys
        // (our reader refuses it by its own budget and hands the file to ufbx, which refuses it by the same numbers)
        CHECK(!Import(AnimFbx((int)lim.maxAnimStacks, 0, 0, 1000.0, 3, 1000), own, "budget"));
    }
    TEST_MAIN_RESULT();
}
