// Core wave 6, 1.17 Scene validation: the two validators (SceneSerializer::Validate / Scene::Validate, which work on
// the scene text, and Testing/SceneValidator, which looks at the loaded scene) now agree and cover each other.
//   - A damaged data pack is a load error that says so ("tampered or unreadable") in both.
//   - Both decide "does this file exist" with SceneValidator::AssetExists: a built-in "proc:" texture is not a missing
//     file, and the working directory is searched as well as the asset root.
//   - The text validator also checks fonts and every component, and names the object that points at the file.
//   - SceneValidator::ValidateText / ValidateFile carry the duplicate-guid and broken-parent findings
//     (checkLinks), which the loader repairs and a check on the loaded scene could not see.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/TextRenderer.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace okay;
using namespace okay::testing;
namespace fs = std::filesystem;

static void PutFile(const std::string& path, const std::string& bytes) {
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << bytes;
}

int main() {
    RUN_SUITE("core_validation");

    const fs::path dir = fs::temp_directory_path() / ("okay_core_validation_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path oldCwd = fs::current_path();
    fs::current_path(dir);

    // A small valid scene to start from.
    Scene s("V"); s.physicsEnabled = false;
    s.CreateGameObject("Main Camera")->AddComponent<Camera>();
    GameObject* sprite = s.CreateGameObject("Sprite");
    sprite->AddComponent<SpriteRenderer>()->texture = "proc:checker";
    const std::string text = SceneSerializer::Serialize(s);

    // --- built-in textures are not missing files ---
    {
        SceneValidationReport r = s.Validate();
        CHECK(r.Count(SceneIssue::Kind::MissingAsset) == 0);       // "proc:checker" used to be reported as "file not found"
        CHECK(SceneSerializer::Validate(text).Ok());
        ValidationReport sem = SceneValidator::ValidateText(text);
        CHECK(sem.Count(Severity::Error) == 0 && !sem.Has("asset.missing"));
        sprite->GetComponent<SpriteRenderer>()->texture = "http://example.invalid/tex.png";   // remote: not checked
        CHECK(s.Validate().Ok());
        sprite->GetComponent<SpriteRenderer>()->texture = "proc:checker";
    }

    // --- the same rule for real files: asset root, working directory, and a miss ---
    {
        sprite->GetComponent<SpriteRenderer>()->texture = "tex/hero.png";
        const std::string t = SceneSerializer::Serialize(s);
        CHECK(SceneSerializer::Validate(t).Count(SceneIssue::Kind::MissingAsset) == 1);
        CHECK(SceneValidator::ValidateText(t).Has("asset.missing"));
        PutFile("root/tex/hero.png", "x");
        ValidatorOptions vo; vo.assetRoot = "root";
        CHECK(SceneSerializer::Validate(t, "root").Ok());
        CHECK(!SceneValidator::ValidateText(t, vo).Has("asset.missing"));
        PutFile("tex/hero.png", "x");                                            // beside the working directory
        CHECK(SceneSerializer::Validate(t).Ok());
        CHECK(SceneSerializer::Validate(t, "elsewhere").Ok());                  // an asset root that has it not: cwd still counts
        fs::remove_all("tex"); fs::remove_all("root");
        sprite->GetComponent<SpriteRenderer>()->texture = "proc:checker";
    }

    // --- fonts and the object that points at a missing file ---
    {
        GameObject* label = s.CreateGameObject("Label");
        auto* tr = label->AddComponent<TextRenderer>();
        tr->fontPath = "fonts/missing.ttf";
        const std::string t = SceneSerializer::Serialize(s);
        SceneValidationReport r = SceneSerializer::Validate(t);
        CHECK(r.Count(SceneIssue::Kind::MissingAsset) == 1);                    // a missing font went unnoticed here
        bool named = false;
        for (const auto& i : r.issues)
            if (i.kind == SceneIssue::Kind::MissingAsset && i.object == "Label" && i.message.find("fonts/missing.ttf") != std::string::npos &&
                i.message.find("TextRenderer.fontPath") != std::string::npos) named = true;
        CHECK(named);
        ValidationReport sem = SceneValidator::ValidateText(t);
        CHECK(sem.CountCode("asset.missing") == 1);                             // both validators see the same one
        PutFile("fonts/missing.ttf", "x");
        CHECK(SceneSerializer::Validate(t).Ok());
        CHECK(!SceneValidator::ValidateText(t).Has("asset.missing"));
        fs::remove_all("fonts");
        label->GetComponent<TextRenderer>()->fontPath = "";
        s.Destroy(label);
        s.Update(0.016f);
    }

    // --- a damaged data pack ---
    {
        const std::string packed = DataPack::Pack(text);
        PutFile("good.okayscene", packed);
        CHECK(SceneSerializer::ValidateFile("good.okayscene").Ok());
        CHECK(!SceneValidator::ValidateFile("good.okayscene").HasErrors());

        std::string flipped = packed; flipped[packed.size() / 2] = (char)(flipped[packed.size() / 2] ^ 0x21);
        PutFile("flipped.okayscene", flipped);
        PutFile("cut.okayscene", packed.substr(0, packed.size() / 2));
        PutFile("stub.okayscene", packed.substr(0, 9));                          // cut inside the header
        for (const char* name : {"flipped.okayscene", "cut.okayscene", "stub.okayscene"}) {
            SceneValidationReport r = SceneSerializer::ValidateFile(name);
            CHECK(!r.Ok());                                                      // it used to unpack to "" and say "missing 'okayscene' header"
            CHECK(r.Count(SceneIssue::Kind::LoadError) == 1);
            CHECK(r.ToString().find("tampered or unreadable") != std::string::npos);
            ValidationReport sem = SceneValidator::ValidateFile(name);
            CHECK(sem.Has("scene.load") && sem.HasErrors());
            CHECK(sem.ToText().find("tampered or unreadable") != std::string::npos);
        }
    }

    // --- the semantic validator carries the file-level findings ---
    {
        std::string bad = text;
        bad += "gameobject 40 \"Broken\"\n  active 1\n  guid \"abc\"\n  parent 77\nend\n";
        bad += "gameobject 41 \"Twin\"\n  active 1\n  guid \"abc\"\n  parent -1\nend\n";
        ValidationReport r = SceneValidator::ValidateText(bad);
        CHECK(r.CountCode("scene.broken_parent") == 1 && r.CountCode("scene.duplicate_guid") == 1);
        bool broken = false;
        for (const auto& i : r.issues)
            if (i.code == "scene.broken_parent") broken = i.severity == Severity::Error && i.object.find("Broken") != std::string::npos;
        CHECK(broken);
        CHECK(r.HasErrors());
        CHECK(r.ToText().find("scene.broken_parent") != std::string::npos);
        CHECK(r.ToJson().find("scene.duplicate_guid") != std::string::npos);

        ValidatorOptions off; off.checkLinks = false;
        ValidationReport quiet = SceneValidator::ValidateText(bad, off);
        CHECK(quiet.CountCode("scene.broken_parent") == 0 && quiet.CountCode("scene.duplicate_guid") == 0);

        // The same through a file, packed or not.
        PutFile("bad.okayscene", bad);
        PutFile("bad_packed.okayscene", DataPack::Pack(bad));
        for (const char* name : {"bad.okayscene", "bad_packed.okayscene"}) {
            ValidationReport f = SceneValidator::ValidateFile(name);
            CHECK(f.CountCode("scene.broken_parent") == 1 && f.CountCode("scene.duplicate_guid") == 1);
        }
        // A clean scene stays clean.
        ValidationReport clean = SceneValidator::ValidateText(text);
        CHECK(clean.CountCode("scene.broken_parent") == 0 && clean.CountCode("scene.duplicate_guid") == 0);
        CHECK(ValidateSceneLinks(text).Ok());
        CHECK(ValidateSceneLinks(bad).issues.size() == 2);
        CHECK(ValidateSceneLinks("").Ok() && ValidateSceneLinks("garbage\n\n  parent 1\n").Ok());

        // The text validator reports the same two (and the loader's repairs are what made them invisible later).
        SceneValidationReport fmt = SceneSerializer::Validate(bad);
        CHECK(fmt.Count(SceneIssue::Kind::BrokenParentLink) == 1 && fmt.Count(SceneIssue::Kind::DuplicateGuid) == 1);
    }

    // --- a parent cycle is a load error in both ---
    {
        const std::string header = text.substr(0, text.find('\n') + 1);   // the "okayscene N" line
        std::string cyc = header + "gameobject 1 \"A\"\n  active 1\n  parent 2\nend\ngameobject 2 \"B\"\n  active 1\n  parent 1\nend\n";
        SceneValidationReport r = SceneSerializer::Validate(cyc);
        CHECK(r.Count(SceneIssue::Kind::LoadError) == 1 && r.ToString().find("cycle") != std::string::npos);
        ValidationReport sem = SceneValidator::ValidateText(cyc);
        CHECK(sem.Has("scene.load") && sem.ToText().find("cycle") != std::string::npos);
    }

    fs::current_path(oldCwd);
    std::error_code ec;
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
