// Automatic LOD generation when a model is brought into a scene (feature map 2.8): the model's import settings
// (`<model>.okaymeta`, Project panel > Import Settings) say how many levels, how much each one keeps and the
// screen-size thresholds; ImportModelScene adds a LOD Group with meshes made by our MeshSimplify.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/ImportSettings.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

// A UV sphere written as an OBJ (positions, UVs, normals), 2 * seg * (seg - 1) triangles.
static std::string WriteSphereObj(const fs::path& dir, const char* name, int seg) {
    const std::string path = (dir / name).string();
    std::ofstream o(path);
    const int rings = seg, sides = seg * 2;
    for (int r = 0; r <= rings; ++r)
        for (int s = 0; s <= sides; ++s) {
            const float th = 3.14159265f * r / rings, ph = 6.2831853f * s / sides;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            o << "v " << x << " " << y << " " << z << "\n";
            o << "vt " << (float)s / sides << " " << (float)r / rings << "\n";
            o << "vn " << x << " " << y << " " << z << "\n";
        }
    auto id = [&](int r, int s) { return r * (sides + 1) + s + 1; };
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < sides; ++s) {
            const int a = id(r, s), b = id(r + 1, s), c = id(r + 1, s + 1), d = id(r, s + 1);
            if (r != 0) o << "f " << a << "/" << a << "/" << a << " " << b << "/" << b << "/" << b << " " << d << "/" << d << "/" << d << "\n";
            if (r != rings - 1) o << "f " << b << "/" << b << "/" << b << " " << c << "/" << c << "/" << c << " " << d << "/" << d << "/" << d << "\n";
        }
    return path;
}

static MeshRenderer* FirstMesh(Scene& s) {
    for (const auto& up : s.Objects())
        if (auto* mr = up->GetComponent<MeshRenderer>(); mr && !mr->mesh.vertices.empty()) return mr;
    return nullptr;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "okay_lod_import_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    RUN_SUITE("import settings keys");
    {
        ImportSettings d = ImportSettings::Defaults(AssetType::Model);
        CHECK(!d.model.generateLods);
        CHECK(d.model.lodLevels == 3);
        CHECK_NEAR(d.model.lodReduction, 0.5f, 1e-6f);
        // Off: the stored text is what it was before (no reimport of existing models).
        bool any = false;
        for (const auto& kv : d.ToPairs()) any = any || kv.first.rfind("lod", 0) == 0 || kv.first == "generateLods";
        CHECK(!any);
        ImportSettings s = ImportSettings::FromPairs(AssetType::Model, 1,
            {{"generateLods", "1"}, {"lodLevels", "4"}, {"lodReduction", "0.4"}, {"lodScreen", "0.5,0.2,0.08"}});
        CHECK(s.model.generateLods);
        CHECK(s.model.lodLevels == 4);
        CHECK_NEAR(s.model.lodReduction, 0.4f, 1e-6f);
        CHECK(s.model.lodScreen.size() == 3u);
        if (s.model.lodScreen.size() == 3u) CHECK_NEAR(s.model.lodScreen[1], 0.2f, 1e-6f);
        ImportSettings back = ImportSettings::FromPairs(AssetType::Model, 1, s.ToPairs());
        CHECK(back.SameAs(s));
        CHECK(s.extra.empty());
        // Out of range values are pulled in.
        ImportSettings c = ImportSettings::FromPairs(AssetType::Model, 1, {{"generateLods", "1"}, {"lodLevels", "40"}, {"lodReduction", "2"}});
        CHECK(c.model.lodLevels == 6);
        CHECK(c.model.lodReduction > 0.0f && c.model.lodReduction < 1.0f);
    }

    RUN_SUITE("sidecar reader");
    {
        const std::string path = WriteSphereObj(dir, "plain.obj", 8);
        ModelImportOptions none = ReadModelImportOptions(path);
        CHECK(!none.generateLods);
        std::ofstream(path + ".okaymeta") << "importer model 1\nset generateLods 1\nset lodLevels 3\nset lodReduction 0.5\nset lodScreen 0.4,0.15\n";
        ModelImportOptions o = ReadModelImportOptions(path);
        CHECK(o.generateLods && o.lodLevels == 3);
        CHECK_NEAR(o.lodReduction, 0.5f, 1e-6f);
        CHECK(o.lodScreen.size() == 2u);
    }

    RUN_SUITE("import adds a LOD Group");
    {
        const std::string path = WriteSphereObj(dir, "ball.obj", 24);
        {
            Scene s("plain");
            bool ok = false;
            CHECK(ImportModelScene(s, path, &ok) && ok);
            MeshRenderer* mr = FirstMesh(s);
            CHECK(mr && !mr->gameObject->GetComponent<LODGroup>());   // no settings: no LOD Group
        }
        std::ofstream(path + ".okaymeta") << "importer model 1\nset generateLods 1\nset lodLevels 3\nset lodReduction 0.5\nset lodScreen 0.4,0.15\n";
        Scene s("lod");
        bool ok = false;
        CHECK(ImportModelScene(s, path, &ok) && ok);
        MeshRenderer* mr = FirstMesh(s);
        LODGroup* lod = mr ? mr->gameObject->GetComponent<LODGroup>() : nullptr;
        CHECK(lod != nullptr);
        if (lod) {
            const int full = mr->mesh.TriangleCount();
            CHECK(lod->levels.size() == 3u);
            CHECK(lod->lodMeshes.size() == 2u);
            CHECK(lod->useScreenSize);
            CHECK_NEAR(lod->levels[0].screenSize, 0.4f, 1e-5f);
            CHECK_NEAR(lod->levels[1].screenSize, 0.15f, 1e-5f);
            CHECK_NEAR(lod->levels[1].ratio, 0.5f, 1e-5f);
            CHECK_NEAR(lod->levels[2].ratio, 0.25f, 1e-5f);
            if (lod->lodMeshes.size() == 2u) {
                const int t1 = lod->lodMeshes[0].TriangleCount(), t2 = lod->lodMeshes[1].TriangleCount();
                std::printf("  imported sphere: %d -> %d -> %d triangles\n", full, t1, t2);
                CHECK(t1 < full && t1 > full / 3);
                CHECK(t2 < t1 && t2 > full / 8);
                CHECK(lod->lodMeshes[0].uvs.size() == lod->lodMeshes[0].vertices.size());   // UVs kept
            }
            CHECK(lod->fadeMode == LODGroup::FadeCrossFade);
            CHECK(lod->cullScreen < lod->levels[1].screenSize);
            // The group survives a save and load (levels rebuilt from the ratios).
            Scene back; std::string err;
            CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(s), &err));
            LODGroup* lb = nullptr;
            for (const auto& up : back.Objects()) if (auto* g = up->GetComponent<LODGroup>()) lb = g;
            CHECK(lb && lb->levels.size() == 3u && lb->useScreenSize);
        }
        // Without explicit thresholds each level halves the screen size of the one before.
        std::ofstream(path + ".okaymeta") << "importer model 1\nset generateLods 1\nset lodLevels 4\nset lodReduction 0.6\n";
        Scene s2("auto");
        CHECK(ImportModelScene(s2, path, &ok) && ok);
        mr = FirstMesh(s2);
        lod = mr ? mr->gameObject->GetComponent<LODGroup>() : nullptr;
        CHECK(lod && lod->levels.size() == 4u);
        if (lod && lod->levels.size() == 4u) {
            CHECK(lod->levels[0].screenSize > lod->levels[1].screenSize);
            CHECK(lod->levels[1].screenSize > lod->levels[2].screenSize);
            CHECK_NEAR(lod->levels[1].ratio, 0.6f, 1e-5f);
            CHECK_NEAR(lod->levels[2].ratio, 0.36f, 1e-5f);
        }
    }
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
