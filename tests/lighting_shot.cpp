// Headless pictures of the lighting test scenes (software renderer, NOT a real window). A tool, not a test.
// Usage: lighting_shot <outdir> [tag]   writes <outdir>/<tag>_<scene>.png (default tag "now").
#include "lighting_scenes.hpp"
#include "okay/Render/LightingPresets.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <filesystem>
#include <cstdio>
using namespace lightscene;
namespace {
// A camera that looks along `v` (so the scene opens, and plays, with a good view).
void AddCamera(Scene& s, const View& v) {
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = v.eye;
    cam->transform->localRotation = Quat::LookRotation((v.target - v.eye).Normalized(), Vec3{0, 1, 0});
    auto* c = cam->AddComponent<Camera>();
    c->main = true; c->fieldOfView = v.fov;
}
const char* Slug(const std::string& n) {
    if (n == "Sunny day") return "sunny_day";
    if (n == "Sunset") return "sunset";
    if (n == "Night with lamps") return "night_with_lamps";
    if (n == "Indoor") return "indoor";
    return "spooky_fog";
}
// One scene per preset: the village (or the room, for Indoor) with the preset applied, saved to <outdir>/<slug>.okayscene.
// With a picture dir, also a headless render of each (software renderer).
int WritePresetScenes(const std::string& outdir, const std::string& picdir) {
    int n = 0; const LightingPresetInfo* list = LightingPresetList(n);
    std::filesystem::create_directories(outdir);
    for (int i = 0; i < n; ++i) {
        Scene s(list[i].name);
        const std::string name = list[i].name;
        View v = name == "Indoor" ? IndoorRoom(s, false) : Village(s);
        ApplyLightingPreset(s, name);
        AddCamera(s, v);
        s.Update(0.0f);
        const std::string path = outdir + "/" + Slug(name) + ".okayscene";
        if (!SceneSerializer::SaveToFile(s, path)) { std::fprintf(stderr, "FAILED to save %s\n", path.c_str()); return 1; }
        std::printf("wrote %s\n", path.c_str());
        if (!picdir.empty()) {
            std::filesystem::create_directories(picdir);
            // sky colour behind the geometry from the preset (the software frame has no procedural sky)
            Vec3 top, hor, bot; EffectiveSkyColors(s.renderSettings, top, hor, bot);
            const std::uint32_t sky = 0xFF000000u | ((std::uint32_t)(Mathf::Clamp01(hor.z) * 255) << 16) |
                                      ((std::uint32_t)(Mathf::Clamp01(hor.y) * 255) << 8) | (std::uint32_t)(Mathf::Clamp01(hor.x) * 255);
            Image im = Render(s, v, 480, 300, sky);
            im.SavePNG(picdir + "/preset_" + Slug(name) + ".png");
        }
    }
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--examples") return WritePresetScenes(argv[2], argc > 3 ? argv[3] : "");
    if (argc < 2) { std::fprintf(stderr, "usage: lighting_shot <outdir> [tag]   |   lighting_shot --examples <scene dir> [picture dir]\n"); return 2; }
    const std::string dir = argv[1], tag = argc > 2 ? argv[2] : "now";
    struct Item { const char* name; View (*build)(Scene&); };
    const Item items[] = {{"sun", SunAndCubes}, {"room", IndoorRoom}, {"spot", SpotCone}, {"points", ManyPoints}};
    for (const Item& it : items) {
        Scene s(it.name);
        View v = it.build(s);
        Image im = Render(s, v, 480, 300);
        const std::string p = dir + "/" + tag + "_" + it.name + ".png";
        std::printf("%s %s\n", im.SavePNG(p) ? "wrote" : "FAILED", p.c_str());
    }
    return 0;
}
