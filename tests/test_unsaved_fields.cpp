// Known unsaved fields: authored settings that the scene file does not keep, so they reset to the default on
// save, duplicate and prefab. The list here must match the table "Fields the scene file does not save" in
// docs/properties.md, and every listed field must really be lost (otherwise the record was fixed and the row,
// and this entry, must go, and the field should be registered as a property).
//
// Why a list: C++ cannot enumerate a component's fields, so "a field saved by neither the record nor the
// registry" cannot be found automatically. This keeps every known one in one place and makes the table fail
// the build when it goes stale in either direction. The PauseMenu fields were the first entries; they are
// fixed (test pausemenu_save).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {
struct Entry {
    std::string path;                                   // Type.field as in the docs table
    std::function<void(GameObject&)> setNonDefault;     // add the component, change the field
    std::function<bool(GameObject&)> isDefault;         // after the round trip: is it back to the default?
};

template <class C, class F> Entry Field(const char* path, F C::*m, F changed) {
    return Entry{path,
        [m, changed](GameObject& go) { go.AddComponent<C>()->*m = changed; },
        [m](GameObject& go) { const C def; auto* c = go.GetComponent<C>(); return c && c->*m == def.*m; }};
}
template <class C> Entry ColorField(const char* path, Color C::*m) {
    return Entry{path,
        [m](GameObject& go) { go.AddComponent<C>()->*m = Color{0.125f, 0.25f, 0.5f, 0.75f}; },
        [m](GameObject& go) {
            const C def; auto* c = go.GetComponent<C>();
            if (!c) return false;
            const Color a = c->*m, b = def.*m;
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }};
}

std::vector<Entry> Known() {
    return {
        Field<VoxelDigger, int>("VoxelDigger.removeFloaters", &VoxelDigger::removeFloaters, 3),
        Field<BuilderMode, char>("BuilderMode.nextBrushKey", &BuilderMode::nextBrushKey, 'n'),
        Field<BuilderMode, char>("BuilderMode.prevBrushKey", &BuilderMode::prevBrushKey, 'b'),
        Field<BuilderMode, char>("BuilderMode.rotateKey", &BuilderMode::rotateKey, 't'),
        Field<BuilderMode, char>("BuilderMode.scaleUpKey", &BuilderMode::scaleUpKey, 'u'),
        Field<BuilderMode, char>("BuilderMode.scaleDownKey", &BuilderMode::scaleDownKey, 'd'),
        Field<BuilderMode, char>("BuilderMode.grabKey", &BuilderMode::grabKey, 'h'),
        Field<BuilderMode, char>("BuilderMode.saveMapKey", &BuilderMode::saveMapKey, 'k'),
        Field<InventoryUI, bool>("InventoryUI.gamepadNav", &InventoryUI::gamepadNav, false),
        ColorField<BlockBuilder>("BlockBuilder.previewFree", &BlockBuilder::previewFree),
        ColorField<BlockBuilder>("BlockBuilder.previewBusy", &BlockBuilder::previewBusy),
        Field<StructureBuilder, char>("StructureBuilder.rotateKey", &StructureBuilder::rotateKey, 't'),
        Field<StructureBuilder, char>("StructureBuilder.upgradeKey", &StructureBuilder::upgradeKey, 'h'),
        Field<StructureBuilder, char>("StructureBuilder.repairKey", &StructureBuilder::repairKey, 'j'),
        Field<StructureBuilder, bool>("StructureBuilder.pieceHotkeys", &StructureBuilder::pieceHotkeys, false),
        ColorField<StructureBuilder>("StructureBuilder.previewFree", &StructureBuilder::previewFree),
        ColorField<StructureBuilder>("StructureBuilder.previewBusy", &StructureBuilder::previewBusy),
    };
}

// The `Type.field` names in the docs table, from its heading to the next blank line.
std::set<std::string> DocsTable(bool& found) {
    found = false;
    std::set<std::string> names;
    std::ifstream f(std::string(OKAY_SOURCE_DIR) + "/docs/properties.md");
    std::string line;
    bool in = false;
    while (std::getline(f, line)) {
        if (!in) { if (line.rfind("### Fields the scene file does not save", 0) == 0) { in = true; found = true; } continue; }
        if (line.rfind("### ", 0) == 0 || line.rfind("## ", 0) == 0) break;
        if (line.rfind("| `", 0) != 0) continue;
        const std::size_t a = 3, b = line.find('`', a);
        if (b != std::string::npos) names.insert(line.substr(a, b - a));
    }
    return names;
}
}  // namespace

static void TestEachIsLost() {
    RUN_SUITE("unsaved fields: every listed field is really lost on save and load");
    for (const Entry& e : Known()) {
        Scene a("a");
        GameObject* go = a.CreateGameObject("Thing");
        e.setNonDefault(*go);
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("b");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        const auto roots = b.Roots();
        CHECK(!roots.empty());
        if (roots.empty()) continue;
        const bool lost = e.isDefault(*roots[0]);
        if (!lost) std::cerr << "  " << e.path << " is saved now: remove it from this list and the docs table, and register it\n";
        CHECK(lost);
    }
}

static void TestDocsMatch() {
    RUN_SUITE("unsaved fields: the docs table lists exactly these");
    bool found = false;
    const std::set<std::string> docs = DocsTable(found);
    CHECK(found);
    std::set<std::string> code;
    for (const Entry& e : Known()) code.insert(e.path);
    for (const auto& n : code) if (!docs.count(n)) { std::cerr << "  missing from docs/properties.md: " << n << "\n"; CHECK(false); }
    for (const auto& n : docs) if (!code.count(n)) { std::cerr << "  in docs/properties.md but not tested: " << n << "\n"; CHECK(false); }
    CHECK(docs.size() == code.size());
}

int main() {
    TestEachIsLost();
    TestDocsMatch();
    TEST_MAIN_RESULT();
}
