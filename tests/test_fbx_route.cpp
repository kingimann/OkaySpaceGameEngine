// FBX is read only by our own reader (okay::fbx): ufbx was deleted on 2026-11-23 (CLAUDE.md rule 17).
//
// 1. The build: no ufbx sources under engine/, and no engine, shared, editor, launcher or player file includes "ufbx.h"
//    or calls a ufbx_ function (comments are not counted).
// 2. The route: every .fbx file in this repository's test corpora (tests/, examples/, assets/) and, when
//    OKAY_MODEL_CORPUS names a folder (the test folder of an Assimp checkout, docs/content/model_corpus.md), every .fbx
//    below it, is read by our own reader: fbx::LoadFile, BuildParts and BuildClips succeed, and ImportModelScene logs
//    "read by the own FBX reader". A file only another reader could read would fail here. The one known exception is
//    transparentTest.fbx in the Assimp folder, a damaged file (its NUL bytes were replaced by spaces) that no reader
//    opens; it must still fail cleanly, and the model_corpus manifest records it as FAIL.
// Written for this project.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Core/ImportSidecar.hpp"   // ImportLower
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static std::string DropComments(const std::string& t) {
    std::string o; o.reserve(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '/' && i + 1 < t.size() && t[i + 1] == '/') { while (i < t.size() && t[i] != '\n') ++i; o += '\n'; continue; }
        if (t[i] == '/' && i + 1 < t.size() && t[i + 1] == '*') { i += 2; while (i + 1 < t.size() && !(t[i] == '*' && t[i + 1] == '/')) ++i; ++i; continue; }
        o += t[i];
    }
    return o;
}

static void NoUfbxInTheBuild() {
    RUN_SUITE("fbx: no ufbx in the build");
    const fs::path root = OKAY_SOURCE_DIR;
    CHECK(!fs::exists(root / "engine" / "third_party" / "ufbx"));
    const std::regex use(R"((#\s*include\s*["<]ufbx\.h[">])|\bufbx_[a-z])");
    int bad = 0;
    for (const char* d : {"engine", "shared", "editor", "launcher", "player"}) {
        if (!fs::is_directory(root / d)) continue;
        for (auto& e : fs::recursive_directory_iterator(root / d)) {
            if (!e.is_regular_file()) continue;
            const std::string x = ImportLower(e.path().extension().string());
            if (x != ".cpp" && x != ".hpp" && x != ".h" && x != ".c" && x != ".mm") continue;
            std::ifstream f(e.path(), std::ios::binary);
            std::stringstream ss; ss << f.rdbuf();
            if (std::regex_search(DropComments(ss.str()), use)) { std::cerr << "  uses ufbx: " << e.path().string() << "\n"; ++bad; }
        }
    }
    CHECK(bad == 0);
}

static std::vector<std::string> FbxFilesBelow(const fs::path& dir) {
    std::vector<std::string> out;
    if (!fs::is_directory(dir)) return out;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied); it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_directory() && ImportLower(it->path().filename().string()).rfind("build", 0) == 0) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file() && ImportLower(it->path().extension().string()) == ".fbx") out.push_back(it->path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

static void EveryFbxGoesToOurReader() {
    RUN_SUITE("fbx: every FBX file in the corpora is read by our own reader");
    const fs::path root = OKAY_SOURCE_DIR;
    std::vector<std::string> files;
    // The repository's files are imported from a copy in the temp folder: the import writes embedded pictures next to the
    // file (<name>_tex0.png), and a test must not write into the source tree.
    const fs::path tmp = fs::temp_directory_path() / ("okay_fbx_route_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp);
    int serial = 0;
    for (const char* d : {"tests", "examples", "assets"})
        for (const std::string& f : FbxFilesBelow(root / d)) {
            const fs::path to = tmp / (std::to_string(serial++) + "_" + fs::path(f).filename().string());
            fs::copy_file(f, to, fs::copy_options::overwrite_existing);
            files.push_back(to.string());
        }
    const std::size_t own = files.size();
    if (const char* c = std::getenv("OKAY_MODEL_CORPUS"); c && *c) {
        auto v = FbxFilesBelow(c);
        std::cout << "  " << v.size() << " FBX files from OKAY_MODEL_CORPUS\n";
        files.insert(files.end(), v.begin(), v.end());
    } else {
        std::cout << "  (OKAY_MODEL_CORPUS not set: only this repository's files)\n";
    }
    std::cout << "  " << own << " FBX files in this repository\n";
    CHECK(own >= 15);

    std::string lastLine;
    Log::Sink old = Log::sink;
    Log::sink = [&](Log::Level, const std::string& m) { if (m.find("model import '") != std::string::npos) lastLine = m; };
    int read = 0, damaged = 0;
    for (const std::string& path : files) {
        // tests/assets/fbx/legacy/old50_*.fbx are FBX 5.0 files that must be refused (with a message), like the damaged one
        const bool knownDamaged = fs::path(path).filename() == "transparentTest.fbx" || fs::path(path).filename().string().find("_old50_") != std::string::npos;
        fbx::Scene s; std::string err;
        const bool ours = fbx::LoadFile(path, s, &err) && fbx::BuildParts(s, &err) && fbx::BuildClips(s, &err);
        lastLine.clear();
        Scene scene; scene.physicsEnabled = false; bool ok = false;
        GameObject* r = ImportModelScene(scene, path, &ok);
        if (knownDamaged) {   // no reader opens it: it fails, and nothing else claims it
            ++damaged;
            CHECK(!ours && !r && !ok);
            continue;
        }
        if (!ours) std::cerr << "  " << path << ": our reader: " << err << "\n";
        CHECK(ours);
        CHECK(ok && r);
        const bool byOurs = lastLine.find("read by the own FBX reader") != std::string::npos;
        if (!byOurs) std::cerr << "  " << path << ": " << (lastLine.empty() ? std::string("(no import line)") : lastLine) << "\n";
        CHECK(byOurs);
        CHECK(ImportLower(lastLine).find("ufbx") == std::string::npos);
        // the single-mesh path too (a file with meshes)
        if (!s.meshes.empty()) {
            bool mok = false; std::string tex;
            Mesh m = ImportModel(path, &mok, &tex);
            if (!mok) std::cerr << "  " << path << ": ImportModel failed\n";
            CHECK(mok && !m.vertices.empty());
        }
        ++read;
    }
    Log::sink = old;
    std::error_code ec; fs::remove_all(tmp, ec);
    std::cout << "  " << read << " read by our reader, " << damaged << " known damaged\n";
}

int main() {
    NoUfbxInTheBuild();
    EveryFbxGoesToOurReader();
    TEST_MAIN_RESULT();
}
