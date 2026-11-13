#include "test_framework.hpp"
#include "../editor/ProjectState.hpp"
#include <Okay.hpp>

using namespace okay;
using namespace oked;

int main() {
    RUN_SUITE("serialize");

    // Build a scene with hierarchy, a camera, and a sprite.
    Scene scene("Level1");
    GameObject* cam = scene.CreateGameObject("MainCamera");
    auto* c = cam->AddComponent<Camera>();
    c->orthographicSize = 9.0f;

    GameObject* parent = scene.CreateGameObject("Parent");
    parent->transform->localPosition = {3, 4, 0};
    parent->tag = "player";

    GameObject* child = scene.CreateGameObject("Child");
    child->transform->SetParent(parent->transform);
    child->transform->localPosition = {1, 0, 0};
    auto* sr = child->AddComponent<SpriteRenderer>();
    sr->glyph = 'Z';
    sr->color = Color::Red;
    sr->size  = {2.0f, 3.0f};

    std::string text = SceneSerializer::Serialize(scene);
    CHECK(!text.empty());

    // Round-trip into a fresh scene.
    Scene loaded("tmp");
    std::string err;
    bool ok = SceneSerializer::Deserialize(loaded, text, &err);
    CHECK(ok);
    if (!ok) std::cerr << "  deserialize error: " << err << "\n";

    CHECK(loaded.Name() == "Level1");
    CHECK(loaded.Objects().size() == 3);

    GameObject* lc = loaded.Find("Child");
    GameObject* lp = loaded.Find("Parent");
    CHECK(lc != nullptr);
    CHECK(lp != nullptr);
    if (lc && lp) {
        // Hierarchy preserved: child world pos = parent(3,4) + child local(1,0).
        CHECK(lc->transform->Parent() == lp->transform);
        CHECK(lc->transform->Position() == Vec3(4, 4, 0));
        CHECK(lp->tag == "player");
        auto* lsr = lc->GetComponent<SpriteRenderer>();
        CHECK(lsr != nullptr);
        if (lsr) {
            CHECK(lsr->glyph == 'Z');
            CHECK_NEAR(lsr->size.x, 2.0f, 0.001f);
            CHECK_NEAR(lsr->color.r, 1.0f, 0.001f);
        }
    }
    GameObject* lcam = loaded.Find("MainCamera");
    CHECK(lcam != nullptr);
    if (lcam) {
        auto* cc = lcam->GetComponent<Camera>();
        CHECK(cc != nullptr);
        if (cc) CHECK_NEAR(cc->orthographicSize, 9.0f, 0.001f);
    }

    // Per-scene render settings (skybox + ambient) save and load with the scene.
    {
        Scene s("env");
        s.renderSettings.skybox = false;
        s.renderSettings.skyTop = Color::FromBytes(10, 20, 30);
        s.renderSettings.ambient = 0.42f;
        s.renderSettings.fog = true;
        s.renderSettings.fogStart = 12.0f;
        s.renderSettings.fogEnd = 77.0f;
        s.renderSettings.vignette = 0.6f;
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        CHECK(s2.renderSettings.skybox == false);
        CHECK_NEAR(s2.renderSettings.skyTop.r, 10.0f / 255.0f, 0.01f);
        CHECK_NEAR(s2.renderSettings.ambient, 0.42f, 1e-4f);
        CHECK(s2.renderSettings.fog == true);
        CHECK_NEAR(s2.renderSettings.fogStart, 12.0f, 1e-3f);
        CHECK_NEAR(s2.renderSettings.fogEnd, 77.0f, 1e-3f);
        CHECK_NEAR(s2.renderSettings.vignette, 0.6f, 1e-4f);

        // A scene file without the line keeps defaults (skybox on, no vignette) after Clear.
        Scene s3("y"); SceneSerializer::Deserialize(s3, "name \"NoEnv\"\n");
        CHECK(s3.renderSettings.skybox == true);
        CHECK_NEAR(s3.renderSettings.vignette, 0.0f, 1e-4f);
    }

    // Font fields round-trip: scene default UI font, TextRenderer.fontPath, and
    // UIButton.fontPath all save and reload (and old scenes without them are fine).
    {
        Scene s("fonts");
        s.uiFont = "Assets/Fonts/DejaVuSans.ttf";
        GameObject* t = s.CreateGameObject("Label");
        auto* tr = t->AddComponent<TextRenderer>();
        tr->text = "Hi"; tr->fontPath = "Assets/Custom.ttf";
        GameObject* b = s.CreateGameObject("Btn");
        auto* bt = b->AddComponent<UIButton>();
        bt->label = "Play"; bt->fontPath = "Assets/Btn.otf";

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); std::string e;
        CHECK(SceneSerializer::Deserialize(s2, txt, &e));
        CHECK(s2.uiFont == "Assets/Fonts/DejaVuSans.ttf");
        if (auto* lt = s2.Find("Label")) {
            auto* ltr = lt->GetComponent<TextRenderer>();
            CHECK(ltr && ltr->fontPath == "Assets/Custom.ttf");
        }
        if (auto* lb = s2.Find("Btn")) {
            auto* lbt = lb->GetComponent<UIButton>();
            CHECK(lbt && lbt->fontPath == "Assets/Btn.otf");
        }
        // A scene with no fonts set leaves them empty after a round-trip (and an old
        // file simply omits the tokens — back-compat).
        Scene plain("plain"); plain.CreateGameObject("X")->AddComponent<UIButton>();
        Scene p2("z"); CHECK(SceneSerializer::Deserialize(p2, SceneSerializer::Serialize(plain)));
        CHECK(p2.uiFont.empty());
        if (auto* x = p2.Find("X")) { auto* xb = x->GetComponent<UIButton>(); CHECK(xb && xb->fontPath.empty()); }
    }

    // --- Combine scenes: additive merge keeps the host, offsets new roots, tags source ---
    {
        // A "chunk" scene with its own gravity + one object at (5,0,0).
        Scene chunk("Town");
        chunk.physics().gravity = {0.0f, -20.0f};
        chunk.CreateGameObject("House")->transform->localPosition = {5, 0, 0};
        std::string chunkText = SceneSerializer::Serialize(chunk);

        // Host scene with its own object + gravity.
        Scene host("World");
        host.physics().gravity = {0.0f, -9.8f};
        host.CreateGameObject("Player");
        std::size_t before = host.Objects().size();

        std::vector<GameObject*> roots; std::string err;
        CHECK(SceneSerializer::MergeFromText(host, chunkText, Vec3{100, 0, 0}, &roots, &err));
        CHECK(host.Objects().size() == before + 1);           // House added, Player kept
        CHECK(host.Name() == "World");                        // host name preserved
        CHECK_NEAR(host.physics().gravity.y, -9.8f, 0.001f);  // host gravity preserved (not Town's)
        CHECK(roots.size() == 1);
        CHECK_NEAR(roots[0]->transform->localPosition.x, 105.0f, 0.001f);  // 5 + offset 100

        // Tag the merged root and confirm sourceScene round-trips through serialization.
        roots[0]->sourceScene = "Town";
        Scene reloaded("r");
        CHECK(SceneSerializer::Deserialize(reloaded, SceneSerializer::Serialize(host), &err));
        CHECK(reloaded.Find("House")->sourceScene == "Town");
        CHECK(reloaded.Find("Player")->sourceScene.empty());
    }

    // ---- Prefab instance linking: instances remember their source prefab ----
    {
        Scene src("src");
        GameObject* p = src.CreateGameObject("Turret");
        p->transform->localPosition = {1, 2, 3};
        p->AddComponent<Camera>();
        const std::string path = "/tmp/okay_turret_link.okayprefab";
        CHECK(SceneSerializer::SaveObjectToFile(*p, path));
        // The prefab FILE itself must not carry a stale instance link.
        {
            std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf();
            CHECK(ss.str().find("prefab ") == std::string::npos);
        }
        // Instantiating from the file links the new root to its source.
        Scene scn("scn");
        GameObject* inst = SceneSerializer::InstantiateFromFile(scn, path);
        CHECK(inst != nullptr);
        CHECK(inst->prefabSource == path);
        // The link round-trips through scene serialization.
        Scene back("back"); std::string e2;
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(scn), &e2));
        CHECK(back.Find("Turret")->prefabSource == path);
    }

    // A negative pitch must not make the loader drop the AudioSource's optional
    // mixer fields: the gate peeked for digits only, '-' failed it, the rest of
    // the line was left on the stream, and the next save wrote the source wrong.
    {
        Scene s("audio");
        GameObject* g = s.CreateGameObject("Negative pitch");
        auto* au = g->AddComponent<AudioSource>();
        au->clipPath = "tone.wav";
        au->volume = 0.8f;
        au->pitch = -1.0f;
        au->pan = -0.5f;
        au->spatial = true;
        au->minDistance = 2.0f; au->maxDistance = 30.0f;
        au->group = AudioGroup::Voice;
        au->rolloff = AudioRolloff::Inverse;
        const std::string text = SceneSerializer::Serialize(s);
        Scene r("r");
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(r, text, nullptr, &warnings));
        CHECK(warnings.empty());
        GameObject* back = r.Find("Negative pitch");
        CHECK(back != nullptr);
        if (back) {
            auto* got = back->GetComponent<AudioSource>();
            CHECK(got != nullptr);
            if (got) {
                CHECK(got->clipPath == "tone.wav" && got->volume == 0.8f && got->pitch == -1.0f);
                CHECK(got->pan == -0.5f && got->spatial && got->minDistance == 2.0f && got->maxDistance == 30.0f);
                CHECK(got->group == AudioGroup::Voice && got->rolloff == AudioRolloff::Inverse);
            }
        }
        CHECK(SceneSerializer::Serialize(r) == text);   // the reload saves the same thing
    }

    // Build-profile file text (ProjectState.hpp): a hostile profile name cannot
    // inject keys or extra "@profile" blocks, and values keep '=', '|' and '\\'
    // through escape/unescape.
    {
        const std::string evil = "X\nproduct=hacked\n@profile Injected\ndir=/evil";
        const std::vector<BuildProfile> one = ParseBuildProfiles(WriteBuildProfiles({BuildProfile{evil, BuildSettings{}, "", "", 0}}));
        CHECK(one.size() == 1);   // no forged second profile, no forged keys
        CHECK(one[0].name.find('\n') == std::string::npos && one[0].name.find('=') == std::string::npos);
        CHECK(one[0].productName.empty() && one[0].outputDir.empty());
        const std::string evilText = WriteBuildProfiles(one);
        CHECK(evilText.find("product=hacked") == std::string::npos && evilText.find("dir=/evil") == std::string::npos);
        {   // exactly one line starts with "@profile " (the name cannot start one, even mid-line)
            int blocks = 0;
            std::istringstream lf(evilText);
            for (std::string l; std::getline(lf, l);) if (l.rfind("@profile ", 0) == 0) ++blocks;
            CHECK(blocks == 1);
        }

        BuildProfile hostile; hostile.name = "bad=name";   // '=' in the name is stripped, not a key break
        hostile.productName = "a=b"; hostile.outputDir = "p|q=r\\s";
        std::snprintf(hostile.settings.company, sizeof(hostile.settings.company), "c|d=e\\f"); std::snprintf(hostile.settings.version, sizeof(hostile.settings.version), "v=x");
        hostile.settings.scenes = {{"a|b=c\\d", true}, {"plain", false}};
        const std::string htext = WriteBuildProfiles({hostile});
        CHECK(htext.find("\nproduct=a=b") == std::string::npos);   // still one key=value line
        const auto hb = ParseBuildProfiles(htext);
        CHECK(hb.size() == 1 && hb[0].name == "badname");
        CHECK(hb[0].productName == "a=b" && hb[0].outputDir == "p|q=r\\s");
        CHECK(std::string(hb[0].settings.company) == "c|d=e\\f" && std::string(hb[0].settings.version) == "v=x");
        CHECK(hb[0].settings.scenes.size() == 2 && hb[0].settings.scenes[0].path == "a|b=c\\d" && hb[0].settings.scenes[0].on);
        CHECK(hb[0].settings.scenes[1].path == "plain" && !hb[0].settings.scenes[1].on);
        CHECK(WriteBuildProfiles(hb) == htext);   // stable
    }

    TEST_MAIN_RESULT();
}
