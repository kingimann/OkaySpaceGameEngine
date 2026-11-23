// Texture paths named inside model files (okay/Render/TexturePath.hpp): inside the model's folder they are kept; absolute
// or "../" paths are kept only inside an allowed root (the editor sets the open project's folder), with links followed;
// everything else is dropped (#753). Checked through both code paths that read OBJ/MTL textures: the materials reader
// (ReadObjFile, used by the import seam) and Mesh::LoadOBJ (the merged-mesh import), which before this change still kept
// absolute and "../" paths as written.
#include "test_framework.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/TexturePath.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static void Spit(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream o(p, std::ios::binary); o << s; }

static std::string Canon(const fs::path& p) { std::error_code ec; return fs::weakly_canonical(fs::absolute(p), ec).lexically_normal().u8string(); }

// The texture both readers give for an OBJ whose material names `written` as its diffuse map.
struct Both { std::string seam, merged; };
static Both TextureFor(const fs::path& modelDir, const std::string& written) {
    Spit(modelDir / "m.mtl", "newmtl a\nKd 1 1 1\nmap_Kd " + written + "\n");
    Spit(modelDir / "m.obj", "mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl a\nf 1 2 3\n");
    Both b;
    ImportedModel m; std::string err;
    if (ReadObjFile((modelDir / "m.obj").u8string(), m, &err) && !m.materials.empty()) b.seam = m.materials[0].texture;
    bool ok = false;
    Mesh::LoadOBJ((modelDir / "m.obj").u8string(), &ok, &b.merged);
    if (!ok) b.merged = "<load failed>";
    return b;
}

int main() {
    RUN_SUITE("obj_texture_roots");
    const fs::path base = fs::temp_directory_path() / fs::u8path(u8"okay_tex_roots_Müller");
    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path proj = base / "proj", models = proj / "Models", textures = proj / "Textures", outside = base / "outside", evil = base / "proj_evil";
    Spit(textures / "wood.png", "png");
    Spit(outside / "secret.png", "secret");
    Spit(evil / "x.png", "x");
    fs::create_directories(models);
    bool haveLink = false;
    fs::create_symlink(outside / "secret.png", textures / "link.png", ec);   // a link inside the project to a file outside it
    haveLink = !ec;
    ec.clear();
    const std::string wood = Canon(textures / "wood.png");
    // The model's folder as both readers take it from the model's path: everything up to and including the last
    // separator, which is a backslash on Windows (the path the test hands them ends in "Models", backslash, "m.obj" there).
    // The two checks that compare a kept name with this folder failed on Windows while it was always written with '/'.
    const std::string modelDir = models.u8string() + std::string(1, (char)fs::path::preferred_separator);

    {   // no roots (the default, and the player): only the model's own folder, as since #753
        SetModelTextureRoots({});
        CHECK(ModelTextureRoots().empty());
        Both b = TextureFor(models, "tex/inner.png");                       // inside the folder: kept, next to the model
        CHECK(b.seam == modelDir + "tex/inner.png" && b.merged == modelDir + "tex/inner.png");
        b = TextureFor(models, "../Textures/wood.png");                       // climbs out: dropped
        CHECK(b.seam.empty() && b.merged.empty());
        b = TextureFor(models, wood);                                         // absolute: dropped
        CHECK(b.seam.empty() && b.merged.empty());
        b = TextureFor(models, Canon(outside / "secret.png"));
        CHECK(b.seam.empty() && b.merged.empty());
    }
    {   // the project as the allowed root (what the editor sets): absolute and "../" paths inside it come back
        SetModelTextureRoots({proj.u8string()});
        CHECK(ModelTextureRoots().size() == 1);
        Both b = TextureFor(models, "../Textures/wood.png");
        CHECK(b.seam == wood && b.merged == wood);
        b = TextureFor(models, wood);
        CHECK(b.seam == wood && b.merged == wood);
        std::string backslashes = wood;
        for (char& c : backslashes) if (c == '/') c = '\\';
        if (!backslashes.empty() && backslashes[0] == '\\') {   // a POSIX path written with backslashes still means the same file
            b = TextureFor(models, backslashes);
            CHECK(b.seam == wood && b.merged == wood);
        }
        b = TextureFor(models, "tex/inner.png");                              // inside the model's folder: unchanged
        CHECK(b.seam == modelDir + "tex/inner.png");
        // outside the root: still dropped
        b = TextureFor(models, Canon(outside / "secret.png"));
        CHECK(b.seam.empty() && b.merged.empty());
        b = TextureFor(models, "../../outside/secret.png");
        CHECK(b.seam.empty() && b.merged.empty());
        b = TextureFor(models, "../../../../../../../../etc/passwd");
        CHECK(b.seam.empty() && b.merged.empty());
        b = TextureFor(models, Canon(evil / "x.png"));                       // a sibling whose name starts like the project's
        CHECK(b.seam.empty() && b.merged.empty());
        b = TextureFor(models, "../../proj_evil/x.png");
        CHECK(b.seam.empty() && b.merged.empty());
        if (haveLink) {                                                       // a link inside the project that leads outside it
            b = TextureFor(models, "../Textures/link.png");
            CHECK(b.seam.empty() && b.merged.empty());
            b = TextureFor(models, Canon(textures) + "/link.png");
            CHECK(b.seam.empty() && b.merged.empty());
        } else std::printf("  (symbolic links cannot be made here: the link case was skipped)\n");
        // the bare resolver: "" and a missing file inside the root
        CHECK(ResolveModelTexturePath("", modelDir).empty());
        CHECK(ResolveModelTexturePath("../Textures/missing.png", modelDir) == Canon(textures / "missing.png"));
    }
    {   // refused on the text alone, before the disk is touched (network, device and drive-relative paths, NUL, wildcards)
        SetModelTextureRoots({proj.u8string()});
        for (const char* bad : {"//host/share/x.png", "\\\\host\\share\\x.png", "\\\\?\\C:\\x.png", "//?/C:/x.png",
                                "C:x.png", "tex/a:b.png", "a?b.png", "a*.png", "a|b.png"}) {
            Both b = TextureFor(models, bad);
            if (!(b.seam.empty() && b.merged.empty())) std::printf("  not refused: %s\n", bad);
            CHECK(b.seam.empty() && b.merged.empty());
            CHECK(ResolveModelTexturePath(bad, modelDir).empty());
        }
        CHECK(ResolveModelTexturePath(std::string("tex/a\0b.png", 11), modelDir).empty());
        // A name that is not UTF-8 (a hostile or damaged file) is refused on the text: on Windows std::filesystem::u8path
        // throws on it, which crashed the IQM, LWS, COB and batch-D readers' fuzz tests there.
        CHECK(ResolveModelTexturePath("tex/\xff\xfe.png", modelDir).empty());
        CHECK(ResolveModelTexturePath("tex/\xc3(.png", modelDir).empty());
        { std::string why; CHECK(texture_path_detail::Screen("a\xe2\x82.png", &why) == texture_path_detail::Kind::Refused && why.find("UTF-8") != std::string::npos); }
        CHECK(texture_path_detail::Screen("caf\xc3\xa9.png", nullptr) != texture_path_detail::Kind::Refused);   // real UTF-8 still passes
        // A refused name is not dropped silently: one warning names the file (bytes that are not UTF-8 written as \xNN),
        // the model's folder and the reason, so a user with an old Latin-1 .mtl sees why the texture is missing.
        {
            std::vector<std::string> warnings;
            Log::Sink before = Log::sink;
            Log::sink = [&](Log::Level l, const std::string& m) { if (l == Log::Level::Warning) warnings.push_back(m); };
            CHECK(ResolveModelTexturePath("tex/caf\xe9.png", modelDir).empty());
            Log::sink = before;
            CHECK(warnings.size() == 1);
            const std::string w = warnings.empty() ? std::string() : warnings[0];
            if (warnings.size() != 1) std::printf("  warnings: %zu\n", warnings.size());
            CHECK(w.find("tex/caf\\xE9.png") != std::string::npos);   // the file, with the bad byte shown
            CHECK(w.find("not valid UTF-8") != std::string::npos);     // why
            CHECK(w.find(modelDir) != std::string::npos);               // which model folder named it
            CHECK(texture_path_detail::Printable("a\x01\xff.png") == "a\\x01\\xFF.png" && texture_path_detail::Printable("caf\xc3\xa9.png") == "caf\xc3\xa9.png");
        }
        // The screen itself: these are refused on the text, so no file-system call ever sees them (a network path
        // would otherwise reach the root check only after being canonicalised, which opens it on Windows).
        namespace tp = texture_path_detail;
        for (const char* bad : {"//host/share/x.png", "//?/C:/x.png", "//./pipe/x", "C:x.png", "a:b", "x?.png"})
            CHECK(tp::Screen(bad, nullptr) == tp::Kind::Refused);
        CHECK(tp::Screen("C:/Textures/x.png", nullptr) == tp::Kind::Absolute);
        CHECK(tp::Screen("/abs/x.png", nullptr) == tp::Kind::Absolute);
        CHECK(tp::Screen("tex/x.png", nullptr) == tp::Kind::Relative);
        // Windows reserved device names and components Windows trims (trailing '.' or space), refused on every platform.
        for (const char* bad : {"con.png", "CON", "Prn.txt", "tex/aux.png", "a/NUL.tar.gz", "com1.png", "LPT9", "tex/Com9.x",
                                "con .png", "tex/a.png.", "tex/a.png ", "tex./a.png", "tex /a.png", "...", "C:/Textures/nul.png"}) {
            if (tp::Screen(bad, nullptr) != tp::Kind::Refused) std::printf("  not refused: '%s'\n", bad);
            CHECK(tp::Screen(bad, nullptr) == tp::Kind::Refused);
            const unsigned long before = tp::CanonicalCalls().load();
            CHECK(ResolveModelTexturePath(bad, modelDir).empty());
            CHECK(tp::CanonicalCalls().load() == before);   // no disk access
        }
        for (const char* good : {"console.png", "com0.png", "com10.png", "lpt.png", "tex/../tex/a.png", "./a.png", "a.b.png", "auxiliary/x.png"})
            CHECK(tp::Screen(good, nullptr) != tp::Kind::Refused);
        {   // through both readers, with a real file that a trimmed name would have found
            Spit(models / "nul.png", "x");
            Both b = TextureFor(models, "nul.png");
            CHECK(b.seam.empty() && b.merged.empty());
        }
        CHECK(ResolveModelTexturePath(".", modelDir).empty());              // the folder itself is not a file
        CHECK(ResolveModelTexturePath("./", modelDir).empty());
        CHECK(ResolveModelTexturePath("tex/..", modelDir).empty());
    }
    {   // which roots are accepted
        CHECK(SetModelTextureRoots({"/"}) == 0);                              // a file-system root
        CHECK(SetModelTextureRoots({"relative/project"}) == 0);               // not absolute
        CHECK(SetModelTextureRoots({(base / "missing_dir").u8string()}) == 0); // does not exist
        CHECK(SetModelTextureRoots({(textures / "wood.png").u8string()}) == 0); // not a folder
        CHECK(SetModelTextureRoots({"/tmp"}) == 0);                           // one level deep
        CHECK(SetModelTextureRoots({proj.u8string(), "/"}) == 1);             // the bad one is left out
        CHECK(ModelTextureRoots().size() == 1);
        // The home folder, and a folder holding it, are refused. Each of HOME, USERPROFILE and HOMEDRIVE+HOMEPATH is
        // looked at on every platform, so this runs on Linux by setting the variables.
        auto getv = [](const char* n) { const char* v = std::getenv(n); return std::pair<bool, std::string>(v != nullptr, v ? v : ""); };
        auto setv = [](const char* n, const std::pair<bool, std::string>& v) {
#ifdef _WIN32
            _putenv_s(n, v.first ? v.second.c_str() : "");
#else
            if (v.first) setenv(n, v.second.c_str(), 1); else unsetenv(n);
#endif
        };
        const auto sHome = getv("HOME"), sProf = getv("USERPROFILE"), sDrive = getv("HOMEDRIVE"), sPath = getv("HOMEPATH");
        const std::pair<bool, std::string> unset(false, "");
        const fs::path home = base / "users" / "me";
        fs::create_directories(home / "Project");
        // 1. HOME alone
        setv("HOME", {true, home.u8string()}); setv("USERPROFILE", unset); setv("HOMEDRIVE", unset); setv("HOMEPATH", unset);
        CHECK(SetModelTextureRoots({home.u8string()}) == 0);
        CHECK(SetModelTextureRoots({(base / "users").u8string()}) == 0);
        CHECK(SetModelTextureRoots({(home / "Project").u8string()}) == 1);   // a project inside home is fine
        // 2. USERPROFILE alone (a Windows-style environment: no HOME)
        const fs::path prof = base / "winusers" / "me";
        fs::create_directories(prof / "Project");
        setv("HOME", unset); setv("USERPROFILE", {true, prof.u8string()});
        CHECK(SetModelTextureRoots({prof.u8string()}) == 0);
        CHECK(SetModelTextureRoots({(base / "winusers").u8string()}) == 0);
        CHECK(SetModelTextureRoots({(prof / "Project").u8string()}) == 1);
        // 3. HOMEDRIVE + HOMEPATH alone: the drive part is the first component of an absolute path on this machine
        const fs::path hp = base / "hpusers" / "me";
        fs::create_directories(hp / "Project");
        const std::string hpStr = hp.u8string();
        setv("USERPROFILE", unset);
        if (hp.root_name().empty()) { setv("HOMEDRIVE", {true, (base / "hpusers").u8string()}); setv("HOMEPATH", {true, std::string("/me")}); }
        else { setv("HOMEDRIVE", {true, hp.root_name().u8string()}); setv("HOMEPATH", {true, hp.root_directory().u8string() + hp.relative_path().u8string()}); }
        CHECK(SetModelTextureRoots({hpStr}) == 0);
        CHECK(SetModelTextureRoots({(base / "hpusers").u8string()}) == 0);
        CHECK(SetModelTextureRoots({(hp / "Project").u8string()}) == 1);
        // 4. all unset: nothing is refused for being a home folder
        setv("HOMEDRIVE", unset); setv("HOMEPATH", unset);
        CHECK(SetModelTextureRoots({hpStr}) == 1);
        setv("HOME", sHome); setv("USERPROFILE", sProf); setv("HOMEDRIVE", sDrive); setv("HOMEPATH", sPath);
    }
    {   // links: a root reached through a link, and a linked folder inside the model's folder
        std::error_code lec;
        const fs::path projLink = base / "proj_link";
        fs::create_directory_symlink(proj, projLink, lec);
        if (!lec) {
            CHECK(SetModelTextureRoots({projLink.u8string()}) == 1);
            Both b = TextureFor(projLink / "Models", "../Textures/wood.png");
            CHECK(b.seam == wood && b.merged == wood);
            b = TextureFor(projLink / "Models", (projLink / "Textures" / "wood.png").u8string());
            CHECK(b.seam == wood && b.merged == wood);
        } else std::printf("  (symbolic links cannot be made here: the linked-root case was skipped)\n");
        lec.clear();
        fs::create_directory_symlink(outside, models / "texlink", lec);   // a folder link inside the model's folder
        if (!lec) {
            SetModelTextureRoots({});
            Both b = TextureFor(models, "texlink/secret.png");
            CHECK(b.seam.empty() && b.merged.empty());
            SetModelTextureRoots({proj.u8string()});
            b = TextureFor(models, "texlink/secret.png");
            CHECK(b.seam.empty() && b.merged.empty());
            fs::remove(models / "texlink", lec);
        } else std::printf("  (symbolic links cannot be made here: the linked-folder case was skipped)\n");
    }
    {   // the material file itself (mtllib) is held to the same place
        SetModelTextureRoots({});
        Spit(outside / "evil.mtl", "newmtl a\nKd 1 1 1\nmap_Kd evil.png\n");
        Spit(models / "m2.obj", "mtllib ../../outside/evil.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl a\nf 1 2 3\n");
        ImportedModel m; std::string err;
        CHECK(ReadObjFile((models / "m2.obj").u8string(), m, &err));
        bool anyTexture = false;
        for (const auto& mat : m.materials) if (!mat.texture.empty()) anyTexture = true;
        CHECK(!anyTexture);
        std::string merged; bool ok = false;   // LoadOBJ only writes it when it finds a texture
        Mesh::LoadOBJ((models / "m2.obj").u8string(), &ok, &merged);
        CHECK(ok && merged.empty());
        Spit(models / "m3.obj", "mtllib //host/share/evil.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl a\nf 1 2 3\n");
        merged.clear();
        Mesh::LoadOBJ((models / "m3.obj").u8string(), &ok, &merged);
        CHECK(ok && merged.empty());
        // The test above passes for any refusal, so also prove the network path never reached the disk: nothing was sent
        // to the canonicalisation step (which opens a path on Windows), for either reader or the resolver.
        namespace tp = texture_path_detail;
        const unsigned long before = tp::CanonicalCalls().load();
        merged.clear();
        Mesh::LoadOBJ((models / "m3.obj").u8string(), &ok, &merged);
        ImportedModel m3; std::string err3;
        CHECK(ReadObjFile((models / "m3.obj").u8string(), m3, &err3));
        CHECK(ResolveModelTexturePath("//host/share/evil.mtl", modelDir).empty());
        CHECK(merged.empty());
        CHECK(tp::CanonicalCalls().load() == before);
        // control: a path that is allowed does reach the canonicalisation step, so the counter really counts
        CHECK(!ResolveModelTexturePath("m.mtl", modelDir).empty());
        CHECK(tp::CanonicalCalls().load() > before);
    }
    {   // closing the project (empty folder) empties the list again
        SetModelTextureRoots({std::string()});
        CHECK(ModelTextureRoots().empty());
        Both b = TextureFor(models, "../Textures/wood.png");
        CHECK(b.seam.empty() && b.merged.empty());
    }
    fs::remove_all(base, ec);
    TEST_MAIN_RESULT();
}
