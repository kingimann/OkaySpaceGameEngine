// Texture paths named inside model files (okay/Render/TexturePath.hpp): inside the model's folder they are kept; absolute
// or "../" paths are kept only inside an allowed root (the editor sets the open project's folder), with links followed;
// everything else is dropped (#753). Checked through both code paths that read OBJ/MTL textures: the materials reader
// (ReadObjFile, used by the import seam) and Mesh::LoadOBJ (the merged-mesh import), which before this change still kept
// absolute and "../" paths as written.
#include "test_framework.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/TexturePath.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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
    const std::string modelDir = models.u8string() + "/";

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
        // The screen itself: these are refused on the text, so no file-system call ever sees them (a network path
        // would otherwise reach the root check only after being canonicalised, which opens it on Windows).
        namespace tp = texture_path_detail;
        for (const char* bad : {"//host/share/x.png", "//?/C:/x.png", "//./pipe/x", "C:x.png", "a:b", "x?.png"})
            CHECK(tp::Screen(bad, nullptr) == tp::Kind::Refused);
        CHECK(tp::Screen("C:/Textures/x.png", nullptr) == tp::Kind::Absolute);
        CHECK(tp::Screen("/abs/x.png", nullptr) == tp::Kind::Absolute);
        CHECK(tp::Screen("tex/x.png", nullptr) == tp::Kind::Relative);
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
#ifndef _WIN32
        // The home folder, and a folder holding it, are refused (HOME pointed at a folder made here).
        const char* oldHome = std::getenv("HOME");
        const std::string savedHome = oldHome ? oldHome : "";
        const fs::path home = base / "users" / "me";
        fs::create_directories(home);
        setenv("HOME", home.u8string().c_str(), 1);
        CHECK(SetModelTextureRoots({home.u8string()}) == 0);
        CHECK(SetModelTextureRoots({(base / "users").u8string()}) == 0);
        CHECK(SetModelTextureRoots({(home / "Project").u8string()}) == 0);   // does not exist yet
        fs::create_directories(home / "Project");
        CHECK(SetModelTextureRoots({(home / "Project").u8string()}) == 1);   // a project inside home is fine
        if (oldHome) setenv("HOME", savedHome.c_str(), 1); else unsetenv("HOME");
#endif
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
