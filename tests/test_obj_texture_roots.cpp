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
    {   // closing the project (empty folder) empties the list again
        SetModelTextureRoots({std::string()});
        CHECK(ModelTextureRoots().empty());
        Both b = TextureFor(models, "../Textures/wood.png");
        CHECK(b.seam.empty() && b.merged.empty());
    }
    fs::remove_all(base, ec);
    TEST_MAIN_RESULT();
}
