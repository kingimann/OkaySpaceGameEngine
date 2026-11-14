// Project tag list and layer names: validation, name <-> index helpers, the project
// settings text (persisted and read back), collision matrix presets and groups, scene
// helpers (unlisted tags, tag rename) and the validator's tag / layer findings.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void Spit(const std::string& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
bool Has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& e : v) if (e == s) return true;
    return false;
}

// Every test starts from the project defaults; the lists are process-wide.
void ResetProject() {
    ProjectLists::Reset();
}

} // namespace

int main() {
    ResetProject();

    RUN_SUITE("tag names");
    {
        CHECK(Tags::IsValidName("Player"));
        CHECK(Tags::WhyInvalid("Player").empty());
        CHECK(!Tags::WhyInvalid("").empty());
        CHECK(!Tags::WhyInvalid(" lead").empty() && !Tags::WhyInvalid("trail ").empty());
        CHECK(!Tags::WhyInvalid(std::string(65, 'x')).empty());
        CHECK(!Tags::WhyInvalid("a\tb").empty());
        CHECK(Tags::IsBuiltin("Enemy") && !Tags::IsBuiltin("Boss"));
        CHECK(Tags::Add("Boss") && Tags::Contains("Boss") && !Tags::IsBuiltin("Boss"));
        CHECK(Tags::Add("Boss"));                                  // already listed: still true, no duplicate
        CHECK(Tags::Custom().size() == 1);
        CHECK(!Tags::Add("bad\n"));
        CHECK(Tags::Add("Enemy") && Tags::Custom().size() == 1);  // a built-in is not stored twice

        CHECK(Tags::Rename("Boss", "Elite"));
        CHECK(Tags::Contains("Elite") && !Tags::Contains("Boss"));
        CHECK(!Tags::Rename("Enemy", "Foe"));                      // built-in tags stay
        CHECK(!Tags::Rename("Elite", "Player"));                   // would collide
        CHECK(!Tags::Rename("Elite", "bad\n"));
        CHECK(!Tags::Rename("Missing", "X"));
        CHECK(Tags::Rename("Elite", "Elite"));                     // same name: nothing to do, fine
        CHECK(Tags::Index("Player") == 0 && Tags::Index("Elite") == static_cast<int>(Tags::Builtin().size()));
        CHECK(Tags::Index("Nope") == -1);

        // the list has a cap so a hostile file cannot make it huge
        for (int i = 0; i < 5000; ++i) Tags::Add("t" + std::to_string(i));
        CHECK(static_cast<int>(Tags::Custom().size()) <= Tags::kMaxCustom);
        ResetProject();
        CHECK(Tags::Custom().empty() && !Tags::Strict());
    }

    RUN_SUITE("layer names");
    {
        CHECK(Layers::IsValid(0) && Layers::IsValid(31) && !Layers::IsValid(-1) && !Layers::IsValid(32));
        CHECK(Layers::Name(0) == "Default" && Layers::Name(5) == "UI");
        CHECK(Layers::Name(7) == "Layer 7");
        CHECK(Layers::NameToLayer("UI") == 5);
        CHECK(Layers::NameToLayer("ui") == 5);                     // case does not matter
        CHECK(Layers::NameToLayer("  Water ") == 4);               // nor surrounding space
        CHECK(Layers::NameToLayer("Layer 7") == 7);                // an unnamed layer's display name
        CHECK(Layers::NameToLayer("12") == 12);                    // or its number
        CHECK(Layers::NameToLayer("99") == -1 && Layers::NameToLayer("") == -1 && Layers::NameToLayer("Nope") == -1);
        CHECK(Layers::IsNamed(0) && !Layers::IsNamed(7));

        CHECK(Layers::SetName(8, "Enemy"));
        CHECK(Layers::Name(8) == "Enemy" && Layers::NameToLayer("enemy") == 8 && Layers::IsNamed(8));
        CHECK(!Layers::SetName(9, "ENEMY"));                       // two layers, one name: refused
        CHECK(!Layers::SetName(9, "UI"));
        CHECK(!Layers::SetName(9, ""));                            // an empty name is ClearName's job, and 9 has none
        CHECK(!Layers::SetName(9, " padded"));
        CHECK(!Layers::SetName(9, "a,b"));                         // commas and '!' are mask-list syntax
        CHECK(!Layers::SetName(9, "!x"));
        CHECK(!Layers::SetName(9, "12"));                          // a bare number would read as an index
        CHECK(!Layers::SetName(9, std::string(33, 'x')));
        CHECK(!Layers::SetName(-1, "Neg") && !Layers::SetName(32, "High"));
        CHECK(Layers::SetName(8, "Enemy"));                        // the same name on the same layer is fine
        CHECK(Layers::SetName(8, "Foe") && Layers::NameToLayer("Enemy") == -1);
        CHECK(!Layers::SetName(0, ""));                            // layer 0 always has a name
        CHECK(Layers::ClearName(8) && Layers::Name(8) == "Layer 8" && !Layers::IsNamed(8));
        CHECK(!Layers::ClearName(0) && !Layers::ClearName(40));
        CHECK(Layers::SetName(0, "Ground") && Layers::Name(0) == "Ground");
        Layers::ResetNames();
        CHECK(Layers::Name(0) == "Default");

        // masks from names
        CHECK(Layers::SetName(10, "Enemy") && Layers::SetName(11, "Player"));
        CHECK(Layers::MaskFromList("Enemy, Player") == ((1u << 10) | (1u << 11)));
        CHECK(Layers::Mask({0, 5, 31}) == ((1u << 0) | (1u << 5) | (1u << 31)));
        CHECK(Layers::Mask({-1, 32}) == 0u);                       // out of range layers add nothing
        CHECK(Layers::MaskToList((1u << 10) | (1u << 11)) == "Enemy, Player");
        CHECK(Layers::MaskToList(1u << 7) == "Layer 7");
        CHECK(Layers::MaskFromList(Layers::MaskToList((1u << 7) | (1u << 0))) == ((1u << 7) | 1u));
        CHECK(Layers::MaskToList(0xFFFFFFFFu).empty());            // blank means every layer, as MaskFromList reads it
        CHECK(Layers::MaskToList(0u) == "none");
        Layers::ResetNames();
    }

    RUN_SUITE("project settings text");
    {
        ResetProject();
        Tags::Add("Boss"); Tags::Add("Loot");
        Tags::Strict() = true;
        Layers::SetName(8, "Enemy");
        Layers::SetName(0, "Ground");
        Layers::ClearName(4);                                      // a default name cleared on purpose
        const std::string text = ProjectLists::Write();
        CHECK(text.find("tag=Boss\n") != std::string::npos && text.find("tag=Loot\n") != std::string::npos);
        CHECK(text.find("strict_tags=1\n") != std::string::npos);
        CHECK(text.find("layer_name_8=Enemy\n") != std::string::npos);
        CHECK(text.find("layer_name_0=Ground\n") != std::string::npos);
        CHECK(text.find("layer_name_4=\n") != std::string::npos);
        CHECK(text.find("layer_name_5") == std::string::npos);     // untouched defaults are not written
        CHECK(text.find("tag=Player") == std::string::npos);       // built-ins are not written

        ResetProject();
        CHECK(Tags::Custom().empty() && Layers::Name(8) == "Layer 8");
        CHECK(ProjectLists::Apply(text) == 6);
        CHECK(Has(Tags::Custom(), "Boss") && Has(Tags::Custom(), "Loot") && Tags::Strict());
        CHECK(Layers::Name(8) == "Enemy" && Layers::Name(0) == "Ground" && Layers::Name(4) == "Layer 4");
        CHECK(ProjectLists::Write() == text);                      // stable

        // other keys are not ours and are ignored; bad values are skipped
        ResetProject();
        const std::string mixed =
            "company=Acme\nwidth=1280\ntag=Good\ntag=\ntag= bad\ntag=Good\nlayer_name_99=Nope\nlayer_name_x=No\n"
            "layer_name_9=Enemy\nlayer_name_10=enemy\nstrict_tags=0\ngarbage line\n";
        ProjectLists::Apply(mixed);
        CHECK(Tags::Custom().size() == 1 && Tags::Custom()[0] == "Good");
        CHECK(Layers::Name(9) == "Enemy" && Layers::Name(10) == "Layer 10");   // the duplicate was refused
        CHECK(!Tags::Strict());
        CHECK(!ProjectLists::ApplyLine("company", "Acme"));
        CHECK(ProjectLists::ApplyLine("tag", "Another") && Tags::Contains("Another"));

        // CRLF project files read the same
        ResetProject();
        ProjectLists::Apply("tag=Crlf\r\nlayer_name_6=Fx\r\n");
        CHECK(Tags::Contains("Crlf") && Layers::Name(6) == "Fx");
        ResetProject();
    }

    RUN_SUITE("project file: other lines are kept");
    {
        ResetProject();
        const std::string path = "layers_tags_project_test.okayproj";
        std::remove(path.c_str());
        CHECK(!ProjectLists::Load(path));                           // no file: nothing to read
        Spit(path, "company=Acme\nwidth=640\ntag=Old\nlayer_name_3=Stale\nsplash_bar=1\n");
        Tags::Add("Fresh");
        Layers::SetName(12, "Props");
        CHECK(ProjectLists::Save(path));
        const std::string after = Slurp(path);
        CHECK(after.find("company=Acme\n") != std::string::npos && after.find("width=640\n") != std::string::npos);
        CHECK(after.find("splash_bar=1\n") != std::string::npos);  // untouched
        CHECK(after.find("tag=Old") == std::string::npos);         // replaced by the live list
        CHECK(after.find("layer_name_3=Stale") == std::string::npos);
        CHECK(after.find("tag=Fresh\n") != std::string::npos && after.find("layer_name_12=Props\n") != std::string::npos);
        ResetProject();
        CHECK(ProjectLists::Load(path));
        CHECK(Tags::Contains("Fresh") && Layers::Name(12) == "Props" && !Tags::Contains("Old"));
        // a second save changes nothing
        CHECK(ProjectLists::Save(path));
        CHECK(Slurp(path) == after);
        // a file that does not exist yet is created
        std::remove(path.c_str());
        CHECK(ProjectLists::Save(path));
        CHECK(Slurp(path).find("tag=Fresh") != std::string::npos);
        std::remove(path.c_str());
        ResetProject();
    }

    RUN_SUITE("collision matrix presets");
    {
        Scene s("T");
        CHECK(!s.HasCustomLayerCollisions());
        s.ApplyLayerCollisionPreset(Scene::LayerCollisionPreset::SameLayerOnly);
        CHECK(s.LayersCollide(3, 3) && s.LayersCollide(0, 0) && !s.LayersCollide(3, 4) && !s.LayersCollide(0, 31));
        CHECK(s.HasCustomLayerCollisions());
        CHECK(s.LayerCollisionMask(3) == (1u << 3));
        // the matrix is saved with the scene
        Scene t("L");
        CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
        CHECK(t.LayersCollide(3, 3) && !t.LayersCollide(3, 4));
        s.ApplyLayerCollisionPreset(Scene::LayerCollisionPreset::Nothing);
        CHECK(!s.LayersCollide(3, 3) && !s.LayersCollide(0, 1) && s.LayerCollisionMask(7) == 0u);
        s.ApplyLayerCollisionPreset(Scene::LayerCollisionPreset::Everything);
        CHECK(s.LayersCollide(3, 4) && !s.HasCustomLayerCollisions());

        // groups: every pair between two sets of layers
        const std::uint32_t a = Layers::Mask({1, 2}), b = Layers::Mask({5, 6, 7});
        s.SetLayerCollisionGroups(a, b, false);
        CHECK(!s.LayersCollide(1, 5) && !s.LayersCollide(2, 7) && !s.LayersCollide(6, 1));
        CHECK(s.LayersCollide(1, 2) && s.LayersCollide(5, 6) && s.LayersCollide(0, 5));
        std::vector<std::pair<int, int>> off = s.DisabledLayerPairs();
        CHECK(off.size() == 6);
        for (const auto& pr : off) CHECK(pr.first <= pr.second);
        s.SetLayerCollisionGroups(a, b, true);
        CHECK(s.DisabledLayerPairs().empty() && !s.HasCustomLayerCollisions());

        // by name
        Layers::SetName(8, "Enemy"); Layers::SetName(9, "Player");
        CHECK(s.SetLayerCollisionByName("Enemy", "player", false));
        CHECK(!s.LayersCollide(8, 9));
        CHECK(!s.SetLayerCollisionByName("Enemy", "Nobody", false));
        CHECK(s.DisabledLayerPairs().size() == 1);
        Layers::ResetNames();
    }

    RUN_SUITE("scene tag helpers");
    {
        ResetProject();
        Scene s("T");
        GameObject* a = s.CreateGameObject("A"); a->tag = "Enemy";
        GameObject* b = s.CreateGameObject("B"); b->tag = "Debris";
        GameObject* c = s.CreateGameObject("C"); c->tag = "Debris";
        s.CreateGameObject("D");
        std::vector<std::string> unlisted = s.UnlistedTags();
        CHECK(unlisted.size() == 1 && unlisted[0] == "Debris");    // sorted, no duplicates, built-ins excluded
        Tags::Add("Debris");
        CHECK(s.UnlistedTags().empty());

        CHECK(s.ReplaceTag("Debris", "Rubble") == 2);
        CHECK(b->tag == "Rubble" && c->tag == "Rubble" && s.FindGameObjectsWithTag("Rubble").size() == 2);
        CHECK(s.FindWithTag("Debris") == nullptr);
        CHECK(s.ReplaceTag("Nothing", "X") == 0);
        CHECK(s.ReplaceTag("Rubble", "") == 2);                    // "" untags
        CHECK(b->tag.empty() && s.FindWithTag("Rubble") == nullptr);
        CHECK(s.ReplaceTag("Enemy", "bad\tname") == 0 && a->tag == "Enemy");   // an invalid name changes nothing
        CHECK(s.ReplaceTag("", "X") == 0);                         // "untagged" cannot be renamed in bulk

        CHECK(s.CountWithTag("Enemy") == 1 && s.CountWithTag("") == 0);
        a->SetLayer(3);
        CHECK(s.CountOnLayer(3) == 1 && s.CountOnLayer(0) == 3);
        CHECK(s.FindGameObjectsOnLayer(3).size() == 1 && s.FindGameObjectsOnLayer(3)[0] == a);
        CHECK(s.FindGameObjectsOnLayer(40).empty() && s.FindGameObjectsOnLayer(-1).empty());
        ResetProject();
    }

    RUN_SUITE("validation sees tags and layers");
    {
        ResetProject();
        const std::string text =
            "okayscene 3\n"
            "gameobject 0 \"A\"\n  tag \"Enemy\"\n  parent -1\nend\n"
            "gameobject 1 \"B\"\n  tag \"Mystery\"\n  parent -1\nend\n"
            "gameobject 2 \"C\"\n  layer 77\n  parent -1\nend\n";
        SceneValidationReport loose = SceneSerializer::Validate(text);
        CHECK(loose.Count(SceneIssue::Kind::UnknownTag) == 0);     // free tags are fine unless the project is strict
        CHECK(loose.Count(SceneIssue::Kind::BadLayer) == 1);
        Tags::Strict() = true;
        SceneValidationReport strict = SceneSerializer::Validate(text);
        CHECK(strict.Count(SceneIssue::Kind::UnknownTag) == 1);
        CHECK(strict.ToString().find("Mystery") != std::string::npos);
        Tags::Add("Mystery");
        CHECK(SceneSerializer::Validate(text).Count(SceneIssue::Kind::UnknownTag) == 0);
        ResetProject();
    }

    TEST_MAIN_RESULT();
}
