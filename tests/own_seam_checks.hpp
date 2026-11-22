#pragma once
// Checks shared by the tests of the import seam's own-reader routing (test_own_dispatch_e4, test_own_q3bsp_dispatch) and
// of the compile-time defaults (test_own_e4_default, test_own_gltf1_default); the flatten check is also used by
// test_own_dispatch_c, _d, _e, _e3 and _e5: set and clear environment variables, write a
// fixture to disk, and the usual "the seam reads it with our reader and gets what the reader gives directly" checks.
// Written for this engine.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace seam {

inline void SetEnv(const char* n, const char* v) {
#ifdef _WIN32
    _putenv_s(n, v);
#else
    ::setenv(n, v, 1);
#endif
}
inline void UnsetEnv(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    ::unsetenv(n);
#endif
}
inline void Write(const std::filesystem::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }
inline std::size_t Verts(const okay::ImportedModel& m) { std::size_t n = 0; for (const okay::ImportedPart& p : m.parts) n += p.mesh.vertices.size(); return n; }

/// With our reader picked for `ext` at run time, the seam reads `path` with backend "own" and gets what `direct(path, model,
/// &err)` gives: the same parts, vertices, nodes and materials (and clips when `sameClips`), `tris` triangles both ways.
template <class Direct>
void CheckSeamMatchesDirect(const std::string& path, const char* label, const char* ext, Direct&& direct, std::size_t tris, bool sameClips) {
    using namespace okay;
    SetImportBackend(ext, ImportBackend::Own);
    CHECK(EffectiveImportBackend(ext) == ImportBackend::Own);
    ImportedModel a, b; std::string err;
    const bool ok = ImportModelData(path, a, ImportBackend::Default, false, &err);
    if (!ok) std::fprintf(stderr, "  %s: %s\n", label, err.c_str());
    CHECK(ok && a.backend == "own");
    CHECK(direct(path, b, &err));
    CHECK(a.parts.size() == b.parts.size() && Verts(a) == Verts(b) && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size() &&
          (!sameClips || a.clips.size() == b.clips.size()));
    CHECK(fx::TriCount(a) == tris && fx::TriCount(b) == tris);
}

/// With nothing chosen at run time, the format's own variable `env` decides: "1" picks our reader (and the seam reads
/// `path` with it), "0" does not. The variable is cleared afterwards.
inline void CheckEnvSwitch(const std::string& path, const char* ext, const char* env) {
    using namespace okay;
    SetImportBackend(ext, ImportBackend::Default);
    SetEnv(env, "1");
    CHECK(EffectiveImportBackend(ext) == ImportBackend::Own);
    ImportedModel e1; std::string err;
    CHECK(ImportModelData(path, e1, ImportBackend::Default, false, &err) && e1.backend == "own");
    SetEnv(env, "0");
    CHECK(EffectiveImportBackend(ext) != ImportBackend::Own);
    UnsetEnv(env);
}

/// Flatten: `path` read with our reader and flatten on gives one root node, no clips, no skins and `tris` triangles.
inline void CheckFlattened(const std::string& path, std::size_t tris) {
    using namespace okay;
    ImportedModel f; std::string err;
    CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && f.clips.empty() && fx::TriCount(f) == tris);
    for (const ImportedPart& p : f.parts) CHECK(p.bones.empty() && p.node == 0);
}

/// True when `ext` is in ImportableExtensions().
inline bool Listed(const char* ext) { return (" " + okay::ImportableExtensions() + " ").find(std::string(" ") + ext + " ") != std::string::npos; }

/// Our reader is off by default and the build has no Assimp: `ext` is not ours, not listed, and reading `path` fails with
/// a message that names the switch `env`.
inline void CheckOffWithoutAssimp(const std::string& path, const char* ext, const char* env) {
    using namespace okay;
    CHECK(EffectiveImportBackend(ext) != ImportBackend::Own);
    CHECK(!Listed(ext));
    ImportedModel d; std::string e2;
    CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(env) != std::string::npos);
}

/// The compile-time default: `path` imported with the Default backend and nothing set at run time is read by our reader
/// when the build made it the default (`expectOwn`), and is not read by it otherwise.
inline void CheckDefaultBackend(const std::string& path, bool expectOwn) {
    using namespace okay;
    ImportedModel m; std::string err;
    const bool ok = ImportModelData(path, m, ImportBackend::Default, false, &err);
    if (expectOwn) {
        if (!ok) std::fprintf(stderr, "  %s: %s\n", path.c_str(), err.c_str());
        CHECK(ok && m.backend == "own" && !m.parts.empty());
    } else {
        CHECK(!ok || m.backend != "own");   // default OFF: not read by our reader unless asked
    }
}

} // namespace seam
