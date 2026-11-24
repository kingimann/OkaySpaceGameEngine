#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Core/ImportSettings.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include <chrono>
#include <set>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

// Import settings that really change what the engine does: a texture's Filter / Wrap / Mipmaps (the software
// renderer's sampler; the OpenGL renderer reads the same sidecar) and a model's Normals / Smoothing angle / Import
// animations (applied by ImportModelScene). Each check below would fail if the setting did nothing.

namespace {
struct Dir {
    fs::path p;
    Dir() {
        static int n = 0;
        p = fs::temp_directory_path() / ("okay_impreal_" + std::to_string(++n) + "_" +
            std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(p);
    }
    ~Dir() { std::error_code ec; fs::remove_all(p, ec); }
    std::string at(const std::string& f) const { return (p / f).generic_string(); }
};
void Write(const std::string& path, const std::string& s) { std::ofstream(path, std::ios::binary | std::ios::trunc) << s; }

// 2x2 picture: red green / blue white.
std::string MakePng(const Dir& d, const std::string& name, const std::string& meta) {
    Image img(2, 2);
    img.SetPixel(0, 0, Color(1, 0, 0, 1)); img.SetPixel(1, 0, Color(0, 1, 0, 1));
    img.SetPixel(0, 1, Color(0, 0, 1, 1)); img.SetPixel(1, 1, Color(1, 1, 1, 1));
    const std::string path = d.at(name);
    img.SavePNG(path);
    if (!meta.empty()) Write(path + ".okaymeta", "okaymeta 1\ntype texture\n" + meta);
    return path;
}
Color Sample(const std::string& path, float u, float v, float lod = 0.0f) {
    const MipChain* c = GetCachedMips(path);
    return c ? Raster::SampleMips(*c, u, v, lod) : Color(-1, -1, -1, 1);
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.02f; }
bool Is(const Color& c, float r, float g, float b) { return Near(c.r, r) && Near(c.g, g) && Near(c.b, b); }

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
// A glTF with a mesh (two triangles folded 90 degrees along an edge, carrying +Z normals on every vertex) under a
// node that has a translation animation.
std::string MakeGltf(const Dir& d, const std::string& name, const std::string& meta) {
    std::vector<std::uint8_t> b;
    const float pos[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // tri 0,1,2 (in XY) and tri 0,3,1... shares edge 0-1
    for (auto& p : pos) { PutF(b, p[0]); PutF(b, p[1]); PutF(b, p[2]); }     // 48 bytes
    for (int i = 0; i < 4; ++i) { PutF(b, 0); PutF(b, 0); PutF(b, 1); }       // normals (off 48, 48)
    PutU16(b, 0); PutU16(b, 1); PutU16(b, 2); PutU16(b, 0); PutU16(b, 3); PutU16(b, 1);   // indices (off 96, 12)
    PutF(b, 0); PutF(b, 1);                                                   // anim times (off 108, 8)
    PutF(b, 0); PutF(b, 0); PutF(b, 0); PutF(b, 5); PutF(b, 0); PutF(b, 0);   // translations (off 116, 24)
    const std::string j =
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(b.size()) +
        ",\"uri\":\"data:application/octet-stream;base64," + B64(b) + "\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":108,\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":116,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}],"
        "\"nodes\":[{\"name\":\"Root\",\"children\":[1]},{\"name\":\"Part\",\"mesh\":0}],"
        "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
        "\"samplers\":[{\"input\":3,\"output\":4}]}]}";
    const std::string path = d.at(name);
    Write(path, j);
    if (!meta.empty()) Write(path + ".okaymeta", "okaymeta 1\ntype model\n" + meta);
    return path;
}
MeshRenderer* PartMesh(Scene& s) { GameObject* g = s.Find("Part"); return g ? g->GetComponent<MeshRenderer>() : nullptr; }
} // namespace

int main() {
    RUN_SUITE("texture filter");
    {
        Dir d;
        const std::string none = MakePng(d, "a.png", "");
        const std::string pt = MakePng(d, "b.png", "importer texture 2\nset filter point\n");
        const std::string old = MakePng(d, "c.png", "importer texture 1\nset filter nearest\n");
        const std::string bil = MakePng(d, "e.png", "importer texture 2\nset filter bilinear\n");
        // Between the red and green texels: smooth filters blend them, Point takes one texel.
        CHECK(Is(Sample(none, 0.5f, 0.25f), 0.5f, 0.5f, 0));        // no sidecar: the renderer's smooth default
        CHECK(Is(Sample(pt, 0.5f, 0.25f), 0, 1, 0));                // Point: one whole texel
        CHECK(Is(Sample(old, 0.5f, 0.25f), 0, 1, 0));               // the older layout's "nearest" counts
        CHECK(Is(Sample(bil, 0.5f, 0.25f), 0.5f, 0.5f, 0));
        // Trilinear blends two mip levels, Bilinear stays on the nearest one. Level 1's blue is half on in linear light
        // (blue and white of four texels), which is sRGB 0.735 (gamma-correct mips; a plain byte average gave 0.5).
        CHECK(Near(Sample(none, 0.5f, 0.25f, 0.5f).b, 0.3677f));
        CHECK(Near(Sample(bil, 0.5f, 0.25f, 0.5f).b, 0.7354f));
    }
    RUN_SUITE("texture wrap");
    {
        Dir d;
        const std::string rep = MakePng(d, "r.png", "importer texture 2\nset filter point\nset wrap repeat\n");
        const std::string clp = MakePng(d, "c.png", "importer texture 2\nset filter point\nset wrap clamp\n");
        const std::string mir = MakePng(d, "m.png", "importer texture 2\nset filter point\nset wrap mirror\n");
        CHECK(Is(Sample(rep, 1.25f, 0.25f), 1, 0, 0) && Is(Sample(rep, 1.75f, 0.25f), 0, 1, 0));   // wraps round
        CHECK(Is(Sample(clp, 1.25f, 0.25f), 0, 1, 0) && Is(Sample(clp, -0.5f, 0.25f), 1, 0, 0));   // sticks to the edge
        CHECK(Is(Sample(mir, 1.75f, 0.25f), 1, 0, 0) && Is(Sample(mir, 1.25f, 0.25f), 0, 1, 0));   // folds back
        CHECK(Is(Sample(rep, -0.25f, 0.25f), 0, 1, 0) && Is(Sample(mir, -0.25f, 0.25f), 1, 0, 0));
    }
    RUN_SUITE("texture mipmaps");
    {
        Dir d;
        const std::string on = MakePng(d, "on.png", "importer texture 2\nset mipmaps 1\n");
        const std::string off = MakePng(d, "off.png", "importer texture 2\nset mipmaps 0\n");
        const MipChain* a = GetCachedMips(on);
        const MipChain* b = GetCachedMips(off);
        CHECK(a && a->size() == 2 && b && b->size() == 1);
        // Far away (a high level of detail): with mips the picture averages out, without them it keeps its texels.
        // The average is taken in linear light (colour picture, sRGB): each channel is half on, which is sRGB 0.735,
        // not the 0.5 a plain average of the stored bytes gave (that made far-away textures too dark).
        CHECK(Is(Sample(on, 0.25f, 0.25f, 8.0f), 0.735f, 0.735f, 0.735f));
        CHECK(Is(Sample(off, 0.25f, 0.25f, 8.0f), 1, 0, 0));
    }
    RUN_SUITE("changing a setting shows after the caches are dropped");
    {
        Dir d;
        const std::string p = MakePng(d, "live.png", "importer texture 2\nset filter bilinear\n");
        CHECK(Is(Sample(p, 0.5f, 0.25f), 0.5f, 0.5f, 0));
        Write(p + ".okaymeta", "okaymeta 1\ntype texture\nimporter texture 2\nset filter point\n");
        CHECK(Is(Sample(p, 0.5f, 0.25f), 0.5f, 0.5f, 0));          // still cached
        const unsigned gen = TextureCacheGeneration();
        InvalidateAllTextureCaches();
        CHECK(TextureCacheGeneration() != gen);                      // (the OpenGL renderer re-uploads on this)
        CHECK(Is(Sample(p, 0.5f, 0.25f), 0, 1, 0));
    }
    RUN_SUITE("software renderer draws with the settings");
    {
        Dir d;
        auto render = [&](const std::string& tex) {
            Scene scene("R");
            GameObject* go = scene.CreateGameObject("Quad");
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube(); mr->unlit = true; mr->texture = tex;
            Raster r; r.Resize(96, 96);
            r.Clear(0xFF000000u);
            Mat4 view = Mat4::LookAt({0, 0, 2.5f}, {0, 0, 0}, Vec3::Up);
            Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f);
            RenderMeshes(r, scene, proj * view, {0, 0, 2.5f});
            std::set<std::uint32_t> colours;
            for (int y = 30; y < 66; ++y) for (int x = 30; x < 66; ++x) colours.insert(r.Get(x, y));
            return std::make_pair(r.Get(48, 48), colours.size());
        };
        const auto bil = render(MakePng(d, "rb.png", "importer texture 2\nset filter bilinear\nset mipmaps 0\n"));
        const auto pnt = render(MakePng(d, "rp.png", "importer texture 2\nset filter point\nset mipmaps 0\n"));
        const auto pm  = render(MakePng(d, "rm.png", "importer texture 2\nset filter point\nset mipmaps 1\n"));
        CHECK(bil.first != 0xFF000000u && pnt.first != 0xFF000000u && pm.first != 0xFF000000u);   // the quad is drawn
        CHECK(pnt.second < 64);                      // Point: a few flat blocks
        CHECK(bil.second > 10 * pnt.second);         // Bilinear: smooth blends between the texels
    }

    RUN_SUITE("model normals and animations");
    {
        Dir d;
        const std::string plain = MakeGltf(d, "plain.gltf", "");
        const std::string flat = MakeGltf(d, "flat.gltf", "importer model 1\nset normals flat\n");
        const std::string calc = MakeGltf(d, "calc.gltf", "importer model 1\nset normals calculate\nset smoothingAngle 120\n");
        const std::string hard = MakeGltf(d, "hard.gltf", "importer model 1\nset normals calculate\nset smoothingAngle 10\n");
        const std::string noAnim = MakeGltf(d, "noanim.gltf", "importer model 1\nset importAnimations 0\n");

        auto import = [](const std::string& path, Scene& s) { bool ok = false; GameObject* r = ImportModelScene(s, path, &ok); return ok ? r : nullptr; };
        {   // From the file: the authored +Z normals stay.
            Scene s("a"); GameObject* r = import(plain, s); CHECK(r);
            MeshRenderer* mr = PartMesh(s);
            CHECK(mr && mr->mesh.HasNormals() && std::fabs(mr->mesh.normals[0].z - 1.0f) < 1e-4f);
            auto* ma = r ? r->GetComponent<ModelAnimator>() : nullptr;
            CHECK(ma && ma->ClipCount() >= 1);
        }
        {   // Flat: no per-vertex normals, the renderer shades each face on its own.
            Scene s("b"); CHECK(import(flat, s));
            MeshRenderer* mr = PartMesh(s);
            CHECK(mr && !mr->mesh.HasNormals() && mr->mesh.triangles.size() == 6);
        }
        {   // Calculate with a wide angle: smooth across the 90 degree fold; the shared edge gets a blended normal.
            Scene s("c"); CHECK(import(calc, s));
            MeshRenderer* mr = PartMesh(s);
            CHECK(mr && mr->mesh.HasNormals() && mr->mesh.vertices.size() == 4);
            bool blended = false;
            if (mr) for (const auto& n : mr->mesh.normals) if (n.z > 0.3f && n.y > 0.3f) blended = true;
            CHECK(blended);
        }
        {   // Calculate with a narrow angle: the fold is a hard edge, so shared corners split into two vertices each.
            Scene s("d"); CHECK(import(hard, s));
            MeshRenderer* mr = PartMesh(s);
            CHECK(mr && mr->mesh.HasNormals() && mr->mesh.vertices.size() == 6);
            CHECK(mr && mr->mesh.autoSmoothAngle == 10.0f);
        }
        {   // Import animations off: no clips, no animators.
            Scene s("e"); GameObject* r = import(noAnim, s); CHECK(r);
            CHECK(r && !r->GetComponent<ModelAnimator>());
            GameObject* part = s.Find("Part");
            CHECK(part && !part->GetComponent<Animator>() && part->GetComponent<MeshRenderer>());   // the mesh itself still comes in
        }
    }

    RUN_SUITE("sidecar travels with the asset");
    {
        Dir d;
        const std::string a = MakePng(d, "one.png", "importer texture 2\nset filter point\nset wrap clamp\nset mipmaps 0\n");
        fs::copy_file(a + ".okaymeta", d.at("two.png.okaymeta"));   // what a rename / move / build copy does
        fs::copy_file(a, d.at("two.png"));
        const TextureSampling t = TextureImportSampling(d.at("two.png"));
        CHECK(t.filter == TextureSampling::Point && t.wrap == TextureSampling::Clamp && !t.mipmaps);
        const TextureSampling dflt = TextureImportSampling(d.at("nothing.png"));
        CHECK(dflt.filter == TextureSampling::Trilinear && dflt.wrap == TextureSampling::Repeat && dflt.mipmaps);
    }
    RUN_SUITE("a settings file written for another reason keeps the sampling");
    {
        Dir d;
        // The stored default is what a picture with no settings file renders with.
        const ImportSettings def = ImportSettings::Defaults(AssetType::Texture);
        CHECK(def.texture.filter == TextureImportSettings::Filter::Trilinear);
        const std::string none = MakePng(d, "none.png", "");
        CHECK(TextureImportSampling(none).filter == TextureSampling::Trilinear);
        // Change only Largest side, write the whole block as the editor does: the sampling must not move.
        ImportSettings edited = def;
        edited.texture.maxSize = 512;
        std::string meta = "importer texture " + std::to_string(edited.version) + "\n";
        for (const auto& kv : edited.ToPairs()) meta += "set " + kv.first + " " + kv.second + "\n";
        const std::string withMeta = MakePng(d, "withmeta.png", meta);
        const TextureSampling t = TextureImportSampling(withMeta);
        CHECK(t.filter == TextureSampling::Trilinear && t.wrap == TextureSampling::Repeat && t.mipmaps);
        CHECK(Near(Sample(withMeta, 0.5f, 0.25f, 0.5f).b, Sample(none, 0.5f, 0.25f, 0.5f).b));   // identical pixels
    }
    // The loaders read the sidecar the same way the asset database does (ImportSettings::FromPairs): values are
    // unescaped (the database writes them through savefmt::Escape) and an older layout is migrated with the same
    // steps, so a key renamed in a later version reaches the texture, audio and model loaders (Grok #422 item 7).
    RUN_SUITE("the loaders read the sidecar through ImportSettings");
    {
        Dir d;
        // Escaped values: "p\oint" is "point" once unescaped.
        const std::string esc = MakePng(d, "esc.png", "importer texture 2\nset filter p\\oint\nset wrap cl\\amp\n");
        const TextureSampling te = TextureImportSampling(esc);
        CHECK(te.filter == TextureSampling::Point && te.wrap == TextureSampling::Clamp);
        CHECK(Is(Sample(esc, 0.5f, 0.25f), 0, 1, 0));               // the software renderer's cache samples it as Point
        // A v1 texture: `linear 1` became `srgb 0` in v2 (the GPU upload reads srgb), `mips` became `mipmaps`.
        const std::string v1 = MakePng(d, "v1.png", "importer texture 1\nset linear 1\nset mips 0\nset maxsize 64\n");
        CHECK(ReadImportSetting(v1, "texture", "srgb") == "0");
        CHECK(!TextureImportSampling(v1).mipmaps && TextureImportMaxSize(v1) == 64);
        // Audio: an escaped load mode and the v1 keys.
        const std::string wav = d.at("s.wav"), wav1 = d.at("s1.wav");
        Write(wav + ".okaymeta", "okaymeta 1\ntype audio\nimporter audio 2\nset loadMode str\\eam\nset forceMono 1\n");
        Write(wav1 + ".okaymeta", "okaymeta 1\ntype audio\nimporter audio 1\nset stream 1\nset mono 1\n");
        CHECK(ReadAudioImportOptions(wav).stream && ReadAudioImportOptions(wav).forceMono);
        CHECK(ReadAudioImportOptions(wav1).stream && ReadAudioImportOptions(wav1).forceMono);
        // Model: an escaped scale ("2\.5" is 2.5) and normals.
        const std::string mdl = d.at("m.glb");
        Write(mdl + ".okaymeta", "okaymeta 1\ntype model\nimporter model 1\nset scale 2\\.5\nset normals fl\\at\n");
        CHECK(ModelImportScale(mdl) == 2.5f && ReadModelImportOptions(mdl).normals == ModelImportOptions::Flat);
        // The reader caches per file but sees an edited sidecar at once (no cache flush needed).
        Write(mdl + ".okaymeta", "okaymeta 1\ntype model\nimporter model 1\nset scale 4\nset normals calculate\n");
        CHECK(ModelImportScale(mdl) == 4.0f && ReadModelImportOptions(mdl).normals == ModelImportOptions::Calculate);
        fs::remove(mdl + ".okaymeta");
        CHECK(ModelImportScale(mdl) == 1.0f);                        // removed: back to the default
    }
    // Older key names under a current (v2) header: the loaders understood them before the one-reader change, so
    // they still do (texture `filter nearest`, `mips 0`; audio `stream 1`, `mono 1`).
    RUN_SUITE("older key names under a v2 header still count");
    {
        Dir d;
        const std::string near2 = MakePng(d, "near2.png", "importer texture 2\nset filter nearest\n");
        const std::string mips2 = MakePng(d, "mips2.png", "importer texture 2\nset mips 0\n");
        CHECK(TextureImportSampling(near2).filter == TextureSampling::Point);
        CHECK(!TextureImportSampling(mips2).mipmaps);
        const std::string st = d.at("st.wav"), mo = d.at("mo.wav");
        Write(st + ".okaymeta", "okaymeta 1\ntype audio\nimporter audio 2\nset stream 1\n");
        Write(mo + ".okaymeta", "okaymeta 1\ntype audio\nimporter audio 2\nset mono 1\n");
        CHECK(ReadAudioImportOptions(st).stream && !ReadAudioImportOptions(st).forceMono);
        CHECK(ReadAudioImportOptions(mo).forceMono && !ReadAudioImportOptions(mo).stream);
        // A current key wins over an older one in the same block.
        const std::string both = MakePng(d, "both.png", "importer texture 2\nset mipmaps 1\nset mips 0\n");
        CHECK(TextureImportSampling(both).mipmaps);
    }

    // A settings file rewritten with the same size and the same file time is still read again while it is new: file
    // times tick coarsely (on Windows a rewrite a few milliseconds later often keeps the time), so tex_dds_load's
    // "set compress 0" -> "set compress 1" read the old value from the cache there. The time is pinned here so Linux
    // sees exactly what Windows does. A file that has settled (older than 2 s) is served from the cache as before.
    RUN_SUITE("a rewrite with the same time and size is seen while the file is new");
    {
        Dir d;
        const std::string pic = d.at("same.png");
        const std::string meta = pic + ".okaymeta";
        Write(meta, "okaymeta 1\ntype texture\nimporter texture 2\nset compress 0\n");
        const auto t = fs::last_write_time(meta);
        CHECK(ReadImportSetting(pic, "texture", "compress") == "0");
        Write(meta, "okaymeta 1\ntype texture\nimporter texture 2\nset compress 1\n");
        fs::last_write_time(meta, t);
        CHECK(ReadImportSetting(pic, "texture", "compress") == "1");   // (was "0": the cache matched time and size)
        // Settled: an hour old, read once, then changed behind its back with the same time and size: the cache answers.
        const auto old = t - std::chrono::hours(1);
        fs::last_write_time(meta, old);
        CHECK(ReadImportSetting(pic, "texture", "compress") == "1");
        Write(meta, "okaymeta 1\ntype texture\nimporter texture 2\nset compress 0\n");
        fs::last_write_time(meta, old);
        CHECK(ReadImportSetting(pic, "texture", "compress") == "1");   // cached: no read for a settled file
        ClearImportSidecarCache();
        CHECK(ReadImportSetting(pic, "texture", "compress") == "0");
    }

    TEST_MAIN_RESULT();
}
