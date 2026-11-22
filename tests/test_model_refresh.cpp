// A model or a sound already placed in the open scene is brought up to date when its file changes on disk, is
// reimported, or gets new import settings in the Project panel. Before this only pictures were reloaded: a placed
// model kept the geometry it was imported with until it was deleted and dragged in again, and its import settings
// (Scale, Normals, Import animations) only reached the next copy added.
//   MarkModelSource        the placed model's top object remembers its file, the Scale it got and its own parts
//   RefreshModelsForFile   every placed copy of that file: new meshes, new and removed parts, new settings; the
//                          objects, their transforms, children you added and your material changes stay
//   RefreshAudioForFile    every Audio Source that loaded the file loads it again (new samples, new Mix to mono)
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/ModelSource.hpp"
#include "okay/Render/AssetRefresh.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

namespace {
struct Dir {
    fs::path p;
    Dir() {
        static int n = 0;
        p = fs::temp_directory_path() / ("okay_modelrefresh_" + std::to_string(++n) + "_" +
            std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(p);
    }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
    std::string at(const std::string& f) const { return (p / f).generic_string(); }
};
void Write(const std::string& path, const std::string& s) { std::ofstream(path, std::ios::binary | std::ios::trunc) << s; }

void PutF(std::vector<std::uint8_t>& b, float f) { std::uint8_t t[4]; std::memcpy(t, &f, 4); b.insert(b.end(), t, t + 4); }
void PutU16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
std::string B64(const std::vector<std::uint8_t>& d) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; unsigned val = 0; int bits = -6;
    for (std::uint8_t c : d) { val = (val << 8) + c; bits += 8; while (bits >= 0) { out += T[(val >> bits) & 0x3F]; bits -= 6; } }
    if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}
// A glTF: node "Root" with one child per name in `parts`, each showing the same mesh (two triangles sharing an edge,
// folded 90 degrees, +Z normals), `size` metres across. `animated`: the first part has a translation animation.
void WriteGltf(const std::string& path, const std::vector<std::string>& parts, float size, bool animated = false) {
    std::vector<std::uint8_t> b;
    const float pos[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (auto& q : pos) { PutF(b, q[0] * size); PutF(b, q[1] * size); PutF(b, q[2] * size); }   // 48 bytes
    for (int i = 0; i < 4; ++i) { PutF(b, 0); PutF(b, 0); PutF(b, 1); }                        // normals (48, 48)
    PutU16(b, 0); PutU16(b, 1); PutU16(b, 2); PutU16(b, 0); PutU16(b, 3); PutU16(b, 1);        // indices (96, 12)
    PutF(b, 0); PutF(b, 1);                                                                    // times (108, 8)
    PutF(b, 0); PutF(b, 0); PutF(b, 0); PutF(b, 5); PutF(b, 0); PutF(b, 0);                    // moves (116, 24)
    std::string children, nodes;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        children += (i ? "," : "") + std::to_string(i + 1);
        nodes += ",{\"name\":\"" + parts[i] + "\",\"mesh\":0}";
    }
    std::string anim;
    if (animated && !parts.empty())
        anim = ",\"animations\":[{\"name\":\"Bob\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
               "\"samplers\":[{\"input\":3,\"output\":4}]}]";
    const std::string j =
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(b.size()) +
        ",\"uri\":\"data:application/octet-stream;base64," + B64(b) + "\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":108,\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":116,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[" +
        std::to_string(size) + "," + std::to_string(size) + "," + std::to_string(size) + "]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}],"
        "\"nodes\":[{\"name\":\"Root\",\"children\":[" + children + "]}" + nodes + "],"
        "\"scenes\":[{\"nodes\":[0]}],\"scene\":0" + anim + "}";
    Write(path, j);
}
void WriteMeta(const std::string& model, const std::string& sets) {
    Write(model + ".okaymeta", "okaymeta 1\ntype model\nimporter model 1\n" + sets);
}
// What the editor does when a model is dropped into the scene.
GameObject* Place(Scene& s, const std::string& path) {
    bool ok = false;
    GameObject* r = ImportModelScene(s, path, &ok);
    if (!ok || !r) return nullptr;
    const float k = ModelImportScale(path);
    if (k != 1.0f) r->transform->localScale = r->transform->localScale * k;
    MarkModelSource(r, path, k);
    return r;
}
GameObject* Under(GameObject* root, const std::string& name) {
    if (!root) return nullptr;
    GameObject* hit = nullptr;
    root->ForEachDescendant([&](GameObject& g) { if (!hit && g.name == name && !g.IsDestroyed()) hit = &g; });
    return hit;
}
float MaxX(GameObject* g) {
    auto* mr = g ? g->GetComponent<MeshRenderer>() : nullptr;
    float m = -1.0f;
    if (mr) for (const Vec3& v : mr->mesh.vertices) m = std::max(m, v.x);
    return m;
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
bool Near(const Vec3& a, const Vec3& b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

std::string WriteWav(const std::string& path, int frames, int channels) {
    AudioClip c;
    c.sampleRate = 22050;
    c.channels = channels;
    c.samples.assign(static_cast<std::size_t>(frames) * channels, 0.25f);
    c.SaveWAV(path);
    return path;
}
} // namespace

int main() {
    RUN_SUITE("a placed model remembers its file, the Scale it was given and its own parts");
    {
        Dir d;
        const std::string m = d.at("crate.gltf");
        WriteGltf(m, {"Lid", "Box"}, 1.0f);
        Scene s;
        GameObject* r = Place(s, m);
        CHECK(r != nullptr);
        auto* src = r ? r->GetComponent<ModelSource>() : nullptr;
        CHECK(src != nullptr);
        if (src) {
            CHECK(SameAssetFile(src->path, m));
            CHECK(Near(src->appliedScale, 1.0f));
            CHECK(src->HasNode("Root/Lid") || src->HasNode("Lid"));
            CHECK(src->HasNode("Root/Box") || src->HasNode("Box"));
            CHECK(!src->HasNode("Lamp"));
        }
        CHECK(ModelNodePath(r, Under(r, "Lid")).find("Lid") != std::string::npos);
        CHECK(ModelNodePath(r, r).empty());
    }

    RUN_SUITE("the file changed: new geometry and new parts; objects, transforms, your children and materials stay");
    {
        Dir d;
        const std::string m = d.at("crate.gltf");
        WriteGltf(m, {"Lid", "Box"}, 1.0f);
        Scene s;
        GameObject* r = Place(s, m);
        GameObject* lid = Under(r, "Lid");
        CHECK(lid != nullptr && Near(MaxX(lid), 1.0f));
        const std::uint64_t rootId = r ? r->InstanceId() : 0, lidId = lid ? lid->InstanceId() : 0;
        const std::string rootGuid = r ? r->Guid() : "";
        if (r) r->transform->localPosition = Vec3(3, 0, -2);
        if (lid) {
            lid->transform->localPosition = Vec3(0, 1.5f, 0);
            auto* mr = lid->GetComponent<MeshRenderer>();
            if (mr) { mr->color = Color(1, 0, 0, 1); mr->texture = "Assets/Art/rust.png"; mr->metallic = 0.8f; }
        }
        GameObject* lamp = s.CreateGameObject("Lamp");
        if (lid) lamp->transform->SetParent(lid->transform, false);
        lamp->transform->localPosition = Vec3(0, 0.2f, 0);

        WriteGltf(m, {"Lid", "Box", "Handle"}, 2.0f);   // bigger, and a new part
        const ModelRefreshReport rep = RefreshModelsForFile(s, m);
        CHECK(rep.instances == 1);
        CHECK(rep.meshesUpdated >= 2);
        CHECK(rep.added == 1);
        CHECK(rep.removed == 0);
        CHECK(rep.failed.empty());
        CHECK(!rep.Summary().empty());
        GameObject* lid2 = Under(r, "Lid");
        CHECK(lid2 == lid);                                  // the same object, not a new copy
        CHECK(lid2 && lid2->InstanceId() == lidId);
        CHECK(r && r->InstanceId() == rootId && r->Guid() == rootGuid);
        CHECK(Near(MaxX(lid2), 2.0f));                       // the new geometry
        CHECK(Near(MaxX(Under(r, "Box")), 2.0f));
        CHECK(r && Near(r->transform->localPosition, Vec3(3, 0, -2)));
        CHECK(lid2 && Near(lid2->transform->localPosition, Vec3(0, 1.5f, 0)));
        auto* mr = lid2 ? lid2->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr && Near(mr->color.r, 1.0f) && Near(mr->color.g, 0.0f));
        CHECK(mr && mr->texture == "Assets/Art/rust.png" && Near(mr->metallic, 0.8f));
        CHECK(lamp->transform->Parent() == (lid2 ? lid2->transform : nullptr));   // your child stays where you put it
        CHECK(Near(lamp->transform->localPosition, Vec3(0, 0.2f, 0)));
        GameObject* handle = Under(r, "Handle");
        CHECK(handle != nullptr && Near(MaxX(handle), 2.0f));
        CHECK(handle && lid2 && handle->transform->Parent() == lid2->transform->Parent());   // beside its siblings
        auto* src = r ? r->GetComponent<ModelSource>() : nullptr;
        CHECK(src && src->nodes.size() == 4);               // Root, Lid, Box, Handle (the root itself is not listed)
        CHECK(src && !src->HasNode(ModelNodePath(r, lamp)));

        // A second refresh with nothing changed adds and removes nothing.
        const ModelRefreshReport again = RefreshModelsForFile(s, m);
        CHECK(again.instances == 1 && again.added == 0 && again.removed == 0);
    }

    RUN_SUITE("a part the file no longer has goes; a child you added under it moves up and is kept");
    {
        Dir d;
        const std::string m = d.at("crate.gltf");
        WriteGltf(m, {"Lid", "Box"}, 1.0f);
        Scene s;
        GameObject* r = Place(s, m);
        GameObject* box = Under(r, "Box");
        GameObject* sticker = s.CreateGameObject("Sticker");
        if (box) sticker->transform->SetParent(box->transform, false);
        sticker->transform->localPosition = Vec3(0.5f, 0, 0);
        const Vec3 world = sticker->transform->Position();
        WriteGltf(m, {"Lid"}, 1.0f);
        const ModelRefreshReport rep = RefreshModelsForFile(s, m);
        CHECK(rep.instances == 1 && rep.removed == 1 && rep.userChildrenKept == 1);
        s.FlushDestroyed();
        CHECK(Under(r, "Box") == nullptr);
        CHECK(Under(r, "Lid") != nullptr);
        CHECK(!sticker->IsDestroyed());
        CHECK(sticker->transform->Parent() != nullptr && sticker->IsSelfOrDescendantOf(r));
        CHECK(Near(sticker->transform->Position(), world));
    }

    RUN_SUITE("import settings reach models already placed: Scale, Normals, Import animations");
    {
        Dir d;
        const std::string m = d.at("bot.gltf");
        WriteGltf(m, {"Arm"}, 1.0f, /*animated=*/true);
        Scene s;
        GameObject* r = Place(s, m);
        CHECK(r && r->GetComponent<ModelAnimator>() != nullptr);
        const Vec3 scale0 = r ? r->transform->localScale : Vec3(1, 1, 1);
        r->transform->localPosition = Vec3(1, 2, 3);

        WriteMeta(m, "set scale 2\n");
        ModelRefreshReport rep = RefreshModelsForFile(s, m);
        CHECK(rep.instances == 1 && rep.scaleChanged);
        CHECK(r && Near(r->transform->localScale, scale0 * 2.0f));
        CHECK(r && Near(r->transform->localPosition, Vec3(1, 2, 3)));
        auto* src = r ? r->GetComponent<ModelSource>() : nullptr;
        CHECK(src && Near(src->appliedScale, 2.0f));
        r->transform->localScale = r->transform->localScale * 1.5f;   // you made it bigger by hand
        WriteMeta(m, "set scale 1\n");
        rep = RefreshModelsForFile(s, m);
        CHECK(r && Near(r->transform->localScale, scale0 * 1.5f));   // the setting's part goes, yours stays

        GameObject* arm = Under(r, "Arm");
        auto* mr = arm ? arm->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr && !mr->mesh.normals.empty());
        WriteMeta(m, "set normals flat\n");
        RefreshModelsForFile(s, m);
        CHECK(Under(r, "Arm") == arm);
        CHECK(mr && mr->mesh.normals.empty());               // flat: one normal per face, worked out when drawn

        WriteMeta(m, "set importAnimations false\n");
        RefreshModelsForFile(s, m);
        CHECK(r && r->GetComponent<ModelAnimator>() == nullptr);
        WriteMeta(m, "set importAnimations true\n");
        RefreshModelsForFile(s, m);
        auto* ma = r ? r->GetComponent<ModelAnimator>() : nullptr;
        CHECK(ma != nullptr && ma->ClipCount() == 1);
    }

    RUN_SUITE("every placed copy of the file is refreshed, others are not touched");
    {
        Dir d;
        const std::string a = d.at("a.gltf"), b = d.at("b.gltf");
        WriteGltf(a, {"P"}, 1.0f);
        WriteGltf(b, {"P"}, 1.0f);
        Scene s;
        GameObject* a1 = Place(s, a);
        GameObject* a2 = Place(s, a);
        GameObject* b1 = Place(s, b);
        WriteGltf(a, {"P"}, 3.0f);
        WriteGltf(b, {"P"}, 3.0f);
        const ModelRefreshReport rep = RefreshModelsForFile(s, a);
        CHECK(rep.instances == 2);
        CHECK(Near(MaxX(Under(a1, "P")), 3.0f) && Near(MaxX(Under(a2, "P")), 3.0f));
        CHECK(Near(MaxX(Under(b1, "P")), 1.0f));
        CHECK(RefreshModelsForFile(s, d.at("none.gltf")).instances == 0);
    }

    RUN_SUITE("the link is saved with the scene, so a refresh still works after a reload");
    {
        Dir d;
        const std::string m = d.at("crate.gltf");
        WriteGltf(m, {"Lid"}, 1.0f);
        std::string text;
        {
            Scene s;
            Place(s, m);
            text = SceneSerializer::Serialize(s);
        }
        CHECK(text.find("modelsource") != std::string::npos);
        Scene s2;
        std::string err;
        CHECK(SceneSerializer::Deserialize(s2, text, &err));
        GameObject* r = nullptr;
        for (const auto& up : s2.Objects()) if (up->GetComponent<ModelSource>()) r = up.get();
        CHECK(r != nullptr);
        auto* src = r ? r->GetComponent<ModelSource>() : nullptr;
        CHECK(src && SameAssetFile(src->path, m) && src->nodes.size() == 2);
        WriteGltf(m, {"Lid"}, 4.0f);
        CHECK(RefreshModelsForFile(s2, m).instances == 1);
        CHECK(Near(MaxX(Under(r, "Lid")), 4.0f));
    }

    RUN_SUITE("a file that no longer imports leaves the placed model as it was");
    {
        Dir d;
        const std::string m = d.at("crate.gltf");
        WriteGltf(m, {"Lid"}, 1.0f);
        Scene s;
        GameObject* r = Place(s, m);
        Write(m, "{ not json");
        const ModelRefreshReport rep = RefreshModelsForFile(s, m);
        CHECK(rep.instances == 0 && rep.failed.size() == 1);
        CHECK(Under(r, "Lid") != nullptr && Near(MaxX(Under(r, "Lid")), 1.0f));
    }

    RUN_SUITE("file names: relative, ./ and absolute spellings name the same file");
    {
        Dir d;
        const std::string m = d.at("x.gltf");
        Write(m, "x");
        CHECK(SameAssetFile(m, d.at("sub/../x.gltf")));
        CHECK(!SameAssetFile(m, d.at("y.gltf")));
        CHECK(!SameAssetFile("", m));
    }

    RUN_SUITE("a sound already loaded by an Audio Source is loaded again when its file changes");
    {
        Dir d;
        const std::string w = WriteWav(d.at("hit.wav"), 100, 1);
        Scene s;
        auto* loaded = s.CreateGameObject("Loaded")->AddComponent<AudioSource>();
        loaded->clipPath = w;
        CHECK(AttachAudioFile(*loaded, w) != AudioAttach::Failed);
        CHECK(loaded->Clip().FrameCount() == 100);
        auto* idle = s.CreateGameObject("NotLoaded")->AddComponent<AudioSource>();
        idle->clipPath = w;                                   // named but never loaded: left for its own load
        auto* other = s.CreateGameObject("Other")->AddComponent<AudioSource>();
        other->clipPath = d.at("other.wav");
        WriteWav(w, 300, 2);
        CHECK(RefreshAudioForFile(s, w) == 1);
        CHECK(loaded->Clip().FrameCount() == 300 && loaded->Clip().channels == 2);
        CHECK(idle->Clip().samples.empty());
        Write(w + ".okaymeta", "okaymeta 1\ntype audio\nimporter audio 1\nset forceMono true\n");
        CHECK(RefreshAudioForFile(s, w) == 1);
        CHECK(loaded->Clip().channels == 1 && loaded->Clip().FrameCount() == 300);   // Mix to mono now applies
    }

    TEST_MAIN_RESULT();
}
