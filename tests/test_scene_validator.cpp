// Tests for okay::testing::SceneValidator: missing assets, cameras, duplicate names,
// scripts, references, transforms, inactive objects, physics, and the text / JSON reports.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Components/MusicPlayer.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Testing/Regression.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

using namespace okay;
using namespace okay::testing;

namespace {

namespace fs = std::filesystem;

// A scene with the one thing most checks want: a usable camera.
GameObject* AddCamera(Scene& s, const char* name = "Main Camera") {
    GameObject* go = s.CreateGameObject(name);
    go->AddComponent<Camera>();
    return go;
}

void MakeFile(const std::string& path) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << "x";
}

// A minimal recursive-descent JSON check: the text parses, with strings, numbers,
// arrays and objects properly closed.
struct Json {
    const std::string& s;
    std::size_t i = 0;
    explicit Json(const std::string& text) : s(text) {}
    void Ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool Str() {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size() && s[i] != '"') {
            if ((unsigned char)s[i] < 0x20) return false;          // raw control characters are illegal
            if (s[i] == '\\') { ++i; if (i >= s.size()) return false;
                if (s[i] == 'u') i += 4; }
            ++i;
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    }
    bool Value() {
        Ws();
        if (i >= s.size()) return false;
        if (s[i] == '{') {
            ++i; Ws();
            if (s[i] == '}') { ++i; return true; }
            for (;;) {
                Ws(); if (!Str()) return false; Ws();
                if (i >= s.size() || s[i] != ':') return false; ++i;
                if (!Value()) return false; Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return false;
            }
        }
        if (s[i] == '[') {
            ++i; Ws();
            if (s[i] == ']') { ++i; return true; }
            for (;;) {
                if (!Value()) return false; Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return false;
            }
        }
        if (s[i] == '"') return Str();
        std::size_t start = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '.' || s[i] == '+')) ++i;
        return i > start;
    }
    bool Parses() { if (!Value()) return false; Ws(); return i == s.size(); }
};

const ValidationIssue* First(const ValidationReport& r, const std::string& code) {
    for (const auto& i : r.issues) if (i.code == code) return &i;
    return nullptr;
}

} // namespace

int main() {
    RUN_SUITE("scene_validator");
    const std::string root = std::string(OKAY_TEST_OUTPUT_DIR) + "/validator_assets";
    fs::remove_all(root);
    MakeFile(root + "/textures/hero.png");
    MakeFile(root + "/audio/boom.wav");
    MakeFile(root + "/more/extra.png");
    MakeFile(root + "/scenes/near_the_scene.png");
    {
        fs::create_directories(root + "/scripts");
        std::ofstream(root + "/scripts/broken.os") << "function update(dt) {";
        std::ofstream(root + "/scripts/good.os") << "function update(dt) { move(1, 0); }";
    }

    ValidatorOptions base;
    base.assetRoot = root;

    // --- A healthy scene has no issues ---
    {
        Scene s("Healthy");
        AddCamera(s);
        GameObject* hero = s.CreateGameObject("Hero");
        hero->AddComponent<SpriteRenderer>()->texture = "textures/hero.png";
        hero->AddComponent<AudioSource>()->clipPath = "audio/boom.wav";
        auto* mr = s.CreateGameObject("Floor")->AddComponent<MeshRenderer>();
        mr->texture = "proc:grid";                         // built in: no file needed
        ValidationReport r = SceneValidator::Validate(s, base);
        CHECK(r.Clean());
        CHECK(r.sceneName == "Healthy");
        CHECK(r.ToText() == "Scene 'Healthy': no issues\n");
        CHECK(!r.HasErrors() && r.Errors() == 0 && r.Warnings() == 0 && r.Infos() == 0);
    }

    // --- Missing assets: every known field, with the object path and component ---
    {
        Scene s("Assets");
        AddCamera(s);
        GameObject* parent = s.CreateGameObject("World");
        GameObject* hero = s.CreateGameObject("Hero");
        hero->transform->SetParent(parent->transform, false);
        hero->AddComponent<SpriteRenderer>()->texture = "textures/missing.png";
        hero->AddComponent<AudioSource>()->clipPath = "audio/boom.wav";        // exists
        GameObject* prop = s.CreateGameObject("Prop");
        auto* mr = prop->AddComponent<MeshRenderer>();
        mr->meshPath = "meshes/rock.obj"; mr->texture = "textures/hero.png"; mr->normalMap = "textures/rock_n.png";
        GameObject* ui = s.CreateGameObject("Icon");
        ui->AddComponent<UIImage>()->texture = "ui/icon.png";
        s.CreateGameObject("Music")->AddComponent<MusicPlayer>()->tracks = {"audio/boom.wav", "audio/theme.ogg"};
        auto* anim = s.CreateGameObject("Anim")->AddComponent<SpriteAnimator>();
        anim->frames = {"textures/hero.png", "textures/f2.png"};
        GameObject* inst = s.CreateGameObject("Instance");
        inst->prefabSource = "prefabs/crate.okayprefab";

        ValidationReport r = SceneValidator::Validate(s, base);
        CHECK(r.CountCode("asset.missing") == 6);
        const ValidationIssue* i = First(r, "asset.missing");
        CHECK(i && i->severity == Severity::Error);
        if (i) {
            CHECK(i->object == "World/Hero");              // the full path
            CHECK(i->component == "SpriteRenderer");
            CHECK(i->message == "Missing texture 'textures/missing.png' (SpriteRenderer.texture)");
        }
        std::set<std::string> messages;
        for (const auto& is : r.issues) messages.insert(is.message);
        CHECK(messages.count("Missing mesh 'meshes/rock.obj' (MeshRenderer.meshPath)"));
        CHECK(messages.count("Missing texture 'textures/rock_n.png' (MeshRenderer.normalMap)"));
        CHECK(messages.count("Missing texture 'ui/icon.png' (UIImage.texture)"));
        CHECK(messages.count("Missing audio 'audio/theme.ogg' (MusicPlayer.tracks)"));
        CHECK(messages.count("Missing texture 'textures/f2.png' (SpriteAnimator.frames)"));
        CHECK(!messages.count("Missing texture 'textures/hero.png' (MeshRenderer.texture)"));   // exists
        const ValidationIssue* pf = First(r, "asset.prefab_missing");
        CHECK(pf && pf->severity == Severity::Warning && pf->object == "Instance");
        CHECK(r.HasErrors());

        // severity of a missing asset is configurable
        ValidatorOptions soft = base;
        soft.missingAssetSeverity = Severity::Warning;
        CHECK(SceneValidator::Validate(s, soft).Errors() == 0);
        // and the check can be turned off
        ValidatorOptions off = base;
        off.checkAssets = false;
        CHECK(SceneValidator::Validate(s, off).CountCode("asset.missing") == 0);
    }

    // --- Where assets are looked for ---
    {
        CHECK(SceneValidator::AssetExists("", base));                                   // nothing to find
        CHECK(SceneValidator::AssetExists("proc:checker", base));                       // built in
        CHECK(SceneValidator::AssetExists("https://example.com/a.png", base));          // remote
        CHECK(SceneValidator::AssetExists("textures/hero.png", base));
        CHECK(SceneValidator::AssetExists("textures\\hero.png", base));                 // Windows separators
        CHECK(!SceneValidator::AssetExists("textures/none.png", base));
        CHECK(SceneValidator::AssetExists(root + "/textures/hero.png", ValidatorOptions{}));   // absolute path
        CHECK(!SceneValidator::AssetExists(root + "/textures/none.png", ValidatorOptions{}));
        ValidatorOptions dirs;
        dirs.assetRoot = root + "/nowhere";
        dirs.searchDirs = {root + "/more"};
        CHECK(!SceneValidator::AssetExists("textures/hero.png", dirs));
        CHECK(SceneValidator::AssetExists("extra.png", dirs));                          // found in a search dir
        ValidatorOptions fake;                                                          // a virtual file system
        fake.fileExists = [](const std::string& p) { return p == "packed/thing.png"; };
        CHECK(SceneValidator::AssetExists("packed/thing.png", fake));
        CHECK(!SceneValidator::AssetExists("textures/hero.png", fake));
        // a folder is not a file
        CHECK(!SceneValidator::AssetExists("textures", base));
    }

    // --- AssetReferences: what each object points at ---
    {
        Scene s("Refs");
        GameObject* go = s.CreateGameObject("Everything");
        go->AddComponent<SpriteRenderer>()->texture = "a.png";
        go->AddComponent<AudioSource>()->clipPath = "b.wav";
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->meshPath = "c.obj"; mr->texture = "d.png"; mr->aoMap = "e.png";
        go->AddComponent<UIButton>()->icon = "f.png";
        go->AddComponent<ScriptComponent>()->SetPath("script_is_not_listed.os");
        auto refs = SceneValidator::AssetReferences(*go);
        CHECK(refs.size() == 6);
        if (refs.size() == 6) {
            CHECK(refs[0].component == "SpriteRenderer" && refs[0].field == "texture" && refs[0].kind == "texture" && refs[0].path == "a.png");
            CHECK(refs[1].kind == "audio" && refs[1].path == "b.wav");
            CHECK(refs[2].kind == "mesh" && refs[2].path == "c.obj");
            CHECK(refs[4].field == "aoMap");
            CHECK(refs[5].component == "UIButton" && refs[5].field == "icon");
        }
        CHECK(SceneValidator::AssetReferences(*s.CreateGameObject("Bare")).empty());
    }

    // --- Cameras ---
    {
        Scene none("NoCam");
        none.CreateGameObject("Thing");
        ValidationReport r = SceneValidator::Validate(none, base);
        const ValidationIssue* i = First(r, "scene.no_camera");
        CHECK(i && i->severity == Severity::Warning && i->object.empty());
        ValidatorOptions strict = base;
        strict.noCameraSeverity = Severity::Error;
        CHECK(SceneValidator::Validate(none, strict).HasErrors());
        ValidatorOptions off = base;
        off.checkCamera = false;
        CHECK(SceneValidator::Validate(none, off).Clean());

        Scene dis("Disabled");
        GameObject* cam = AddCamera(dis);
        cam->GetComponent<Camera>()->enabled = false;
        CHECK(SceneValidator::Validate(dis, base).Has("scene.no_active_camera"));
        cam->GetComponent<Camera>()->enabled = true;
        CHECK(!SceneValidator::Validate(dis, base).Has("scene.no_active_camera"));
        cam->active = false;
        ValidationReport r2 = SceneValidator::Validate(dis, base);
        CHECK(r2.Has("scene.no_active_camera"));
        CHECK(r2.Has("object.inactive"));

        Scene nested("Nested");
        GameObject* rig = nested.CreateGameObject("Rig");
        GameObject* c2 = AddCamera(nested, "Cam");
        c2->transform->SetParent(rig->transform, false);
        CHECK(SceneValidator::Validate(nested, base).Clean());
        rig->active = false;                                  // the camera is inactive through its parent
        CHECK(SceneValidator::Validate(nested, base).Has("scene.no_active_camera"));
    }

    // --- Duplicate names and singletons ---
    {
        Scene s("Names");
        AddCamera(s);
        s.CreateGameObject("Wall");
        s.CreateGameObject("Wall");
        s.CreateGameObject("Wall");
        GameObject* a = s.CreateGameObject("RoomA");
        GameObject* b = s.CreateGameObject("RoomB");
        GameObject* d1 = s.CreateGameObject("Door"); d1->transform->SetParent(a->transform, false);
        GameObject* d2 = s.CreateGameObject("Door"); d2->transform->SetParent(b->transform, false);   // different parents: fine
        GameObject* k1 = s.CreateGameObject("Key"); k1->transform->SetParent(a->transform, false);
        GameObject* k2 = s.CreateGameObject("Key"); k2->transform->SetParent(a->transform, false);
        ValidationReport r = SceneValidator::Validate(s, base);
        CHECK(r.CountCode("name.duplicate") == 2);            // Wall x3 at the root, Key x2 under RoomA
        const ValidationIssue* w = First(r, "name.duplicate");
        CHECK(w && w->severity == Severity::Warning && w->object == "Wall");
        CHECK(w && w->message.find("3 objects are named 'Wall' at the scene root") != std::string::npos);
        bool sawKey = false;
        for (const auto& i : r.issues)
            if (i.code == "name.duplicate" && i.object == "RoomA/Key" && i.message.find("under 'RoomA'") != std::string::npos) sawKey = true;
        CHECK(sawKey);
        ValidatorOptions allow = base;
        allow.allowDuplicateNames = {"Wall"};
        CHECK(SceneValidator::Validate(s, allow).CountCode("name.duplicate") == 1);
        ValidatorOptions hard = base;
        hard.duplicateNameSeverity = Severity::Error;
        CHECK(First(SceneValidator::Validate(s, hard), "name.duplicate")->severity == Severity::Error);
        ValidatorOptions off = base;
        off.checkNames = false;
        CHECK(SceneValidator::Validate(s, off).CountCode("name.duplicate") == 0);

        Scene single("Singletons");
        AddCamera(single);
        single.CreateGameObject("ES1")->AddComponent<EventSystem>();
        CHECK(SceneValidator::Validate(single, base).Clean());
        single.CreateGameObject("ES2")->AddComponent<EventSystem>();
        ValidationReport sr = SceneValidator::Validate(single, base);
        CHECK(sr.CountCode("component.duplicate_singleton") == 1);
        CHECK(First(sr, "component.duplicate_singleton")->object == "ES2");
    }

    // --- Scripts: missing files, empty, compile errors, languages ---
    {
        Scene s("Scripts");
        AddCamera(s);
        auto script = [&](const char* name) { return s.CreateGameObject(name)->AddComponent<ScriptComponent>(); };
        auto* missingOnly = script("MissingNoSource");
        missingOnly->SetPath("scripts/gone.os");
        auto* missingButEmbedded = script("MissingWithSource");
        CHECK(missingButEmbedded->LoadSource("function update(dt) { move(1, 0); }"));
        missingButEmbedded->SetPath("scripts/gone.os");
        script("Empty");
        auto* good = script("Good");
        CHECK(good->LoadSource("function update(dt) { move(1, 0); }"));
        auto* bad = script("Bad");
        bad->SetSource("function update(dt) {");                                          // never compiles
        auto* fileBad = script("FileBroken");
        fileBad->SetPath("scripts/broken.os");
        auto* fileGood = script("FileGood");
        fileGood->SetPath("scripts/good.os");
        auto* alien = s.CreateGameObject("Alien")->AddComponent<ScriptComponent>("klingon");
        alien->SetSource("qapla");

        ValidationReport r = SceneValidator::Validate(s, base);
        auto issuesFor = [&](const std::string& obj, const std::string& code) {
            int n = 0;
            for (const auto& i : r.issues) if (i.object == obj && i.code == code) ++n;
            return n;
        };
        CHECK(issuesFor("MissingNoSource", "script.missing_file") == 1);
        for (const auto& i : r.issues)
            if (i.object == "MissingNoSource" && i.code == "script.missing_file") CHECK(i.severity == Severity::Error);
        CHECK(issuesFor("MissingWithSource", "script.missing_file") == 1);
        for (const auto& i : r.issues)
            if (i.object == "MissingWithSource" && i.code == "script.missing_file") CHECK(i.severity == Severity::Warning);
        CHECK(issuesFor("MissingWithSource", "script.compile") == 0);                     // the saved copy is fine
        CHECK(issuesFor("Empty", "script.empty") == 1);
        CHECK(issuesFor("Good", "script.compile") == 0 && issuesFor("Good", "script.empty") == 0);
        CHECK(issuesFor("Bad", "script.compile") == 1);
        CHECK(issuesFor("FileBroken", "script.compile") == 1);                           // the file's text does not compile
        CHECK(issuesFor("FileGood", "script.compile") == 0 && issuesFor("FileGood", "script.missing_file") == 0);
        CHECK(issuesFor("Alien", "script.backend") == 1);
        const ValidationIssue* c = First(r, "script.compile");
        CHECK(c && c->severity == Severity::Error && c->component == "ScriptComponent");
        ValidatorOptions off = base;
        off.checkScripts = false;
        CHECK(SceneValidator::Validate(s, off).CountCode("script.compile") == 0);

        // visual scripts
        Scene v("Visual");
        AddCamera(v);
        v.CreateGameObject("VsMissing")->AddComponent<VisualScriptComponent>()->SetPath("graphs/none.okayvs");
        v.CreateGameObject("VsEmpty")->AddComponent<VisualScriptComponent>();
        ValidationReport vr = SceneValidator::Validate(v, base);
        CHECK(vr.CountCode("script.missing_file") == 1 && First(vr, "script.missing_file")->severity == Severity::Error);
        CHECK(vr.CountCode("script.empty") == 1);
    }

    // --- References by name ---
    {
        Scene s("Names");
        AddCamera(s);
        GameObject* cam = s.CreateGameObject("Chaser");
        cam->AddComponent<CameraFollow>()->targetName = "Player";
        s.CreateGameObject("Enemy")->AddComponent<FollowTarget2D>()->target = "Nobody";
        const ValidationReport rr = SceneValidator::Validate(s, base);
        CHECK(rr.CountCode("ref.missing_object") == 2);
        const ValidationIssue* i = First(rr, "ref.missing_object");
        CHECK(i && i->severity == Severity::Warning && i->component == "CameraFollow");
        CHECK(i && i->message == "CameraFollow.targetName names 'Player' but no object has that name");
        s.CreateGameObject("Player");
        s.CreateGameObject("Nobody");
        CHECK(SceneValidator::Validate(s, base).CountCode("ref.missing_object") == 0);
        cam->GetComponent<CameraFollow>()->targetName.clear();                           // empty = unused, never a problem
        CHECK(SceneValidator::Validate(s, base).Clean());
        ValidatorOptions off = base;
        off.checkReferences = false;
        s.Find("Nobody")->name = "Gone";
        CHECK(SceneValidator::Validate(s, off).CountCode("ref.missing_object") == 0);
    }

    // --- Transforms ---
    {
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        Scene s("Transforms");
        AddCamera(s);
        s.CreateGameObject("NanPos")->transform->localPosition = {nan, 0, 0};
        s.CreateGameObject("InfScale")->transform->localScale = {1, inf, 1};
        s.CreateGameObject("NegInfPos")->transform->localPosition = {0, 0, -inf};
        s.CreateGameObject("NanRot")->transform->localRotation = Quat{0, nan, 0, 1};
        s.CreateGameObject("Flat")->transform->localScale = {1, 0, 1};
        s.CreateGameObject("Skewed")->transform->localRotation = Quat{0, 0, 0, 2};
        s.CreateGameObject("Far")->transform->localPosition = {5.0e7f, 0, 0};
        s.CreateGameObject("Fine")->transform->localPosition = {100, -200, 300};
        ValidationReport r = SceneValidator::Validate(s, base);
        CHECK(r.CountCode("transform.non_finite") == 4);
        for (const auto& i : r.issues) if (i.code == "transform.non_finite") CHECK(i.severity == Severity::Error && i.component == "Transform");
        const ValidationIssue* n = First(r, "transform.non_finite");
        CHECK(n && n->object == "NanPos" && n->message.find("nan") != std::string::npos);
        CHECK(r.CountCode("transform.zero_scale") == 1 && First(r, "transform.zero_scale")->object == "Flat");
        CHECK(r.CountCode("transform.rotation_unnormalized") == 1 && First(r, "transform.rotation_unnormalized")->object == "Skewed");
        CHECK(r.CountCode("transform.extreme_position") == 1 && First(r, "transform.extreme_position")->object == "Far");
        for (const auto& i : r.issues) CHECK(i.object != "Fine");
        ValidatorOptions wide = base;
        wide.maxCoordinate = 1.0e9f;
        CHECK(SceneValidator::Validate(s, wide).CountCode("transform.extreme_position") == 0);
        ValidatorOptions off = base;
        off.checkTransforms = false;
        CHECK(SceneValidator::Validate(s, off).Clean());
        // layers
        Scene l("Layers");
        AddCamera(l);
        l.CreateGameObject("Odd")->layer = 40;
        l.CreateGameObject("Neg")->layer = -1;
        l.CreateGameObject("Ok")->layer = 31;
        CHECK(SceneValidator::Validate(l, base).CountCode("object.layer_range") == 2);
    }

    // --- Inactive objects: reported once per subtree ---
    {
        Scene s("Inactive");
        AddCamera(s);
        GameObject* top = s.CreateGameObject("Disabled");
        GameObject* mid = s.CreateGameObject("Mid"); mid->transform->SetParent(top->transform, false);
        GameObject* leaf = s.CreateGameObject("Leaf"); leaf->transform->SetParent(mid->transform, false);
        top->active = false;
        ValidationReport r = SceneValidator::Validate(s, base);
        CHECK(r.CountCode("object.inactive") == 1);
        const ValidationIssue* i = First(r, "object.inactive");
        CHECK(i && i->severity == Severity::Info && i->object == "Disabled");
        CHECK(i && i->message.find("2 descendants") != std::string::npos);
        CHECK(r.Infos() == 1 && r.Warnings() == 0 && r.Errors() == 0 && !r.HasErrors());
        leaf->active = false;                                 // an inactive object below an inactive one: still one report
        CHECK(SceneValidator::Validate(s, base).CountCode("object.inactive") == 1);
        top->active = true;
        mid->active = false;                                  // now the middle is the top of the inactive part
        ValidationReport r2 = SceneValidator::Validate(s, base);
        CHECK(r2.CountCode("object.inactive") == 1 && First(r2, "object.inactive")->object == "Disabled/Mid");
        ValidatorOptions off = base;
        off.checkInactive = false;
        CHECK(SceneValidator::Validate(s, off).Clean());
        // a lone inactive object
        Scene one("One");
        AddCamera(one);
        one.CreateGameObject("Solo")->active = false;
        CHECK(First(SceneValidator::Validate(one, base), "object.inactive")->message == "Inactive: it never runs or draws");
    }

    // --- Physics ---
    {
        Scene s("Physics");
        AddCamera(s);
        s.CreateGameObject("Ghost2D")->AddComponent<Rigidbody2D>();
        s.CreateGameObject("Ghost3D")->AddComponent<Rigidbody3D>();
        GameObject* ok2 = s.CreateGameObject("Solid2D");
        ok2->AddComponent<Rigidbody2D>(); ok2->AddComponent<BoxCollider2D>();
        GameObject* ok3 = s.CreateGameObject("Solid3D");
        ok3->AddComponent<Rigidbody3D>(); ok3->AddComponent<BoxCollider3D>();
        ValidationReport r = SceneValidator::Validate(s, base);
        CHECK(r.CountCode("physics.rigidbody_no_collider") == 2);
        std::set<std::string> who;
        for (const auto& i : r.issues) if (i.code == "physics.rigidbody_no_collider") who.insert(i.object + ":" + i.component);
        CHECK(who.count("Ghost2D:Rigidbody2D") && who.count("Ghost3D:Rigidbody3D"));
        ValidatorOptions off = base;
        off.checkPhysics = false;
        CHECK(SceneValidator::Validate(s, off).Clean());
    }

    // --- Transient and destroyed objects are not judged ---
    {
        Scene s("Transient");
        AddCamera(s);
        GameObject* t = s.CreateGameObject("Generated");
        t->transient = true;
        t->AddComponent<SpriteRenderer>()->texture = "missing/generated.png";
        t->active = false;
        GameObject* d = s.CreateGameObject("Doomed");
        d->AddComponent<SpriteRenderer>()->texture = "missing/doomed.png";
        s.Destroy(d);
        s.FlushDestroyed();
        CHECK(SceneValidator::Validate(s, base).Clean());
    }

    // --- Reports: counts, text order, JSON ---
    {
        ValidationReport r;
        r.sceneName = "Level \"One\"";
        r.issues.push_back({Severity::Info, "object.inactive", "A", "", "just so you know"});
        r.issues.push_back({Severity::Error, "asset.missing", "B/C", "SpriteRenderer", "Missing texture 'x\\y.png' (\"q\")"});
        r.issues.push_back({Severity::Warning, "name.duplicate", "", "", "tab\there\nnewline"});
        r.issues.push_back({Severity::Error, "scene.load", "", "", "second error"});
        CHECK(r.Errors() == 2 && r.Warnings() == 1 && r.Infos() == 1);
        CHECK(r.CountCode("asset.missing") == 1 && r.Has("scene.load") && !r.Has("nope"));
        const std::string text = r.ToText();
        CHECK(text.find("Scene 'Level \"One\"': 2 errors, 1 warning, 1 info\n") == 0);
        const std::size_t e1 = text.find("[ERROR] B/C: Missing texture"), e2 = text.find("[ERROR] second error"),
                          w = text.find("[WARNING] "), in = text.find("[INFO] A: just so you know");
        CHECK(e1 != std::string::npos && e2 != std::string::npos && w != std::string::npos && in != std::string::npos);
        CHECK(e1 < e2 && e2 < w && w < in);                   // errors first, then warnings, then infos
        CHECK(text.find("[asset.missing]") != std::string::npos);
        const std::string errorsOnly = r.ToText(Severity::Error);
        CHECK(errorsOnly.find("[WARNING]") == std::string::npos && errorsOnly.find("[INFO]") == std::string::npos);
        CHECK(errorsOnly.find("2 errors, 1 warning, 1 info") != std::string::npos);   // the summary still counts everything
        const std::string json = r.ToJson();
        CHECK(Json(json).Parses());
        CHECK(json.find("\"scene\":\"Level \\\"One\\\"\"") != std::string::npos);
        CHECK(json.find("\"errors\":2,\"warnings\":1,\"infos\":1") != std::string::npos);
        CHECK(json.find("\"severity\":\"error\",\"code\":\"asset.missing\",\"object\":\"B/C\",\"component\":\"SpriteRenderer\"") != std::string::npos);
        CHECK(json.find("tab\\there\\nnewline") != std::string::npos);
        CHECK(json.find("x\\\\y.png") != std::string::npos);
        ValidationReport empty;
        CHECK(Json(empty.ToJson()).Parses());
        CHECK(empty.ToJson() == "{\"scene\":\"\",\"errors\":0,\"warnings\":0,\"infos\":0,\"issues\":[]}");
        CHECK(std::string(SeverityName(Severity::Info)) == "info" && std::string(SeverityName(Severity::Warning)) == "warning" &&
              std::string(SeverityName(Severity::Error)) == "error");
        // a real report renders as valid JSON too
        Scene s("Real");
        s.CreateGameObject("Odd \"name\"")->AddComponent<SpriteRenderer>()->texture = "no\\such.png";
        CHECK(Json(SceneValidator::Validate(s, base).ToJson()).Parses());
    }

    // --- ValidateText / ValidateFile: loads into a scratch scene ---
    {
        Scene src("FromText");
        AddCamera(src);
        src.CreateGameObject("Hero")->AddComponent<SpriteRenderer>()->texture = "textures/hero.png";
        src.CreateGameObject("Ghost")->AddComponent<SpriteRenderer>()->texture = "textures/ghost.png";
        const std::string text = SceneSerializer::Serialize(src);
        ValidationReport r = SceneValidator::ValidateText(text, base);
        CHECK(r.sceneName == "FromText");
        CHECK(r.CountCode("asset.missing") == 1 && First(r, "asset.missing")->object == "Ghost");
        CHECK(r.Errors() == 1);

        ValidationReport bad = SceneValidator::ValidateText("definitely not a scene", base);
        CHECK(bad.Errors() == 1 && bad.Has("scene.load") && bad.issues[0].message.find("did not load") != std::string::npos);

        // a file: relative assets are found next to the scene file too
        Scene nextTo("Next");
        AddCamera(nextTo);
        nextTo.CreateGameObject("Pic")->AddComponent<SpriteRenderer>()->texture = "near_the_scene.png";
        const std::string scenePath = root + "/scenes/next.okayscene";
        CHECK(SceneSerializer::SaveToFile(nextTo, scenePath));
        ValidatorOptions noRoot;                              // no asset root at all
        CHECK(SceneValidator::ValidateFile(scenePath, noRoot).Clean());
        CHECK(SceneValidator::ValidateFile(scenePath, base).Clean());

        ValidationReport missing = SceneValidator::ValidateFile(root + "/scenes/missing.okayscene", base);
        CHECK(missing.Has("scene.load") && missing.sceneName.find("missing.okayscene") != std::string::npos);
    }

    // --- Validation never changes the scene ---
    {
        Scene s("Untouched");
        AddCamera(s);
        s.CreateGameObject("Wall"); s.CreateGameObject("Wall");
        s.CreateGameObject("Off")->active = false;
        s.CreateGameObject("Nan")->transform->localPosition = {std::nanf(""), 0, 0};
        s.CreateGameObject("Tex")->AddComponent<SpriteRenderer>()->texture = "gone.png";
        const SceneDigest before = DigestScene(s);
        const std::string serialized = SceneSerializer::Serialize(s);
        ValidationReport r = SceneValidator::Validate(s, base);
        CHECK(!r.Clean());
        const SceneDigest after = DigestScene(s);
        CHECK(before.hash == after.hash && before.lines == after.lines);
        CHECK(SceneSerializer::Serialize(s) == serialized);
    }

    TEST_MAIN_RESULT();
}
