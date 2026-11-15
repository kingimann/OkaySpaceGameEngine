// "Only one per object": the table that the Add Component browser, Duplicate Component and Paste As New Component all use
// (editor/AddComponentRules.hpp). The browser's rows are written as `item(oked::CanAddOne<T>(*go), "...")`, which refuses to compile
// for a kind that is not in the table. This test reads the browser's sources and checks the other directions: every kind in the table
// has a row, no row still spells the old hand-written test out (it would not be in the table), and the table is made of real,
// distinct components that the run-time questions answer the same way the compile-time one does.
#include "test_framework.hpp"
#include "AddComponentRules.hpp"
#include <Okay.hpp>
#include "okay/Scene/PropertyRegistry.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace oked;

#ifndef OKED_EDITOR_DIR
#error "OKED_EDITOR_DIR must name the editor/ source folder"
#endif

namespace {

// Every file that adds rows to the Add Component browser.
const char* const kBrowserSources[] = {"main.cpp", "SplineTools.cpp", "LevelTools.cpp", "NavigationWindow.cpp", "BehaviorTreeWindow.cpp", "VolumeTools.cpp"};

std::string ReadFile(const std::string& name) {
    std::ifstream in(std::string(OKED_EDITOR_DIR) + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The kinds named by CanAddOne<...> in a text.
std::vector<std::string> KindsUsed(const std::string& text) {
    std::vector<std::string> out;
    const std::string key = "CanAddOne<";
    for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + key.size())) {
        const std::size_t b = at + key.size();
        const std::size_t e = text.find('>', b);
        if (e == std::string::npos) break;
        std::string k = text.substr(b, e - b);
        if (k.rfind("okay::", 0) == 0) k = k.substr(6);
        out.push_back(k);
    }
    return out;
}

void TestTableMatchesTheBrowser() {
    RUN_SUITE("the table and the Add Component browser's rows agree");
    std::set<std::string> used;
    int rows = 0, handWritten = 0;
    for (const char* file : kBrowserSources) {
        const std::string text = ReadFile(file);
        CHECK(!text.empty());
        for (const std::string& k : KindsUsed(text)) { used.insert(k); ++rows; }
        // A row whose first argument still reads `!go->GetComponent<T>()` hides a kind that Duplicate and Paste As New know nothing about.
        std::istringstream lines(text);
        std::string line;
        int n = 0;
        while (std::getline(lines, line)) {
            ++n;
            const std::size_t it = line.find("item(");
            if (it == std::string::npos || line.find("auto item") != std::string::npos) continue;
            const std::size_t label = line.find(", \"", it);   // the row's test is what stands before its label
            const std::string test = line.substr(it, label == std::string::npos ? std::string::npos : label - it);
            if (test.find("!go->GetComponent<") != std::string::npos) {
                ++handWritten;
                std::cerr << "  hand-written test in " << file << ":" << n << ": " << line << "\n";
            }
        }
    }
    CHECK(handWritten == 0);
    CHECK(rows >= 200);   // the browser has about two hundred such rows
    std::set<std::string> table;
    for (const OnePerObjectEntry& e : OnePerObjectKinds()) table.insert(e.name);
    CHECK(table.size() == OnePerObjectKinds().size());                       // no kind twice
    for (const std::string& k : table)
        if (!used.count(k)) { std::cerr << "  in the table but without a row in the browser: " << k << "\n"; CHECK(used.count(k) != 0); }
    for (const std::string& k : used)
        if (!table.count(k)) { std::cerr << "  a row for a kind that is not in the table: " << k << "\n"; CHECK(table.count(k) != 0); }
    CHECK(table == used);
    // The table is kept sorted, so a merge that adds a kind twice or out of place shows.
    std::vector<std::string> names;
    for (const OnePerObjectEntry& e : OnePerObjectKinds()) names.push_back(e.name);
    CHECK(std::is_sorted(names.begin(), names.end()));
    std::cout << "  " << table.size() << " kinds in the table, " << rows << " rows in the browser's sources\n";
}

void TestTableAnswersAtRunTime() {
    RUN_SUITE("the run-time questions answer like the browser's test");
    int registered = 0;
    for (const OnePerObjectEntry& e : OnePerObjectKinds()) {
        CHECK(IsOnePerObject(e.type));
        Scene s("t");
        GameObject* g = s.CreateGameObject("g");
        CHECK(!e.present(*g) && !HasOnePerObjectKind(*g, e.type));           // an empty object has none
    }
    for (const auto& info : PropertyRegistry::All()) {
        if (!info || !info->add || info->objectLevel) continue;
        Scene s("t");
        GameObject* g = s.CreateGameObject("g");
        Component* c = info->add(*g);
        if (!c) continue;
        const bool once = IsOnePerObject(info->type);
        CHECK(HasOnePerObjectKind(*g, info->type) == once);                  // present exactly for the kinds in the table
        if (once) {
            ++registered;
            // The name in the table is the name the registry and the scene file use for it.
            bool named = false;
            for (const OnePerObjectEntry& e : OnePerObjectKinds()) if (e.type == info->type && info->name == e.name) named = true;
            CHECK(named);
        }
    }
    CHECK(registered > 80);                                                  // most of the registry is in the table
    // Kinds the browser offers again and again are not.
    for (const std::type_index t : {std::type_index(typeid(ScriptComponent)), std::type_index(typeid(VisualScriptComponent)), std::type_index(typeid(Volume)),
                                    std::type_index(typeid(TweenAnimation)), std::type_index(typeid(NetValidator)), std::type_index(typeid(Transform))})
        CHECK(!IsOnePerObject(t));
}

void TestTemplateTest() {
    RUN_SUITE("CanAddOne is true until the object has one");
    Scene s("t");
    GameObject* g = s.CreateGameObject("g");
    CHECK(CanAddOne<Rigidbody2D>(*g) && CanAddOne<Rigidbody3D>(*g) && CanAddOne<AudioSource>(*g) && CanAddOne<Light>(*g) && CanAddOne<Camera>(*g));
    g->AddComponent<Rigidbody2D>();
    g->AddComponent<AudioSource>();
    CHECK(!CanAddOne<Rigidbody2D>(*g) && !CanAddOne<AudioSource>(*g) && CanAddOne<Rigidbody3D>(*g) && CanAddOne<Light>(*g));
    // The player controller row is three tests in one: the controller, and the first- and third-person ones.
    CHECK(CanAddOne<PlayerController>(*g) && CanAddOne<FirstPersonController>(*g) && CanAddOne<ThirdPersonController>(*g));
    g->AddComponent<FirstPersonController>();
    CHECK(CanAddOne<PlayerController>(*g) && !CanAddOne<FirstPersonController>(*g));
}

}   // namespace

int main() {
    TestTableMatchesTheBrowser();
    TestTableAnswersAtRunTime();
    TestTemplateTest();
    TEST_MAIN_RESULT();
}
