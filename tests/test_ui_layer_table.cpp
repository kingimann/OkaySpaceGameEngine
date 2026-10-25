#include "test_framework.hpp"
#include <Okay.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::uilayers;

// The shared UI layer table (UIElement.hpp) is the only place the UI pass order
// lives: UIDefaultLayer, the player and the editor preview all read it. These
// tests pin its values (the player's historic pass order, so built games draw
// unchanged) and check that nothing keeps a private copy.

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

static std::vector<int> KindsOf(GameObject* g) {
    std::vector<UIDrawItem> items;
    UIQueueDrawItems(g, 0, items);
    std::vector<int> k;
    for (const auto& it : items) { k.push_back(it.kind); CHECK(it.layer == it.kind); CHECK(it.index == 0); }
    return k;
}

int main() {
    RUN_SUITE("ui_layer_table");

    // --- Table values: the historic player pass order ------------------
    {
        const int expected[] = {K_DropBg, K_Image, K_Panel, K_DropHi, K_Scroll, K_Progress,
                                K_Radial, K_Minimap, K_Slider, K_Stepper, K_Rating, K_Toggle,
                                K_Tabs, K_Button, K_Text, K_WorldUI, K_Dropdown, K_Input,
                                K_Crosshair, K_FocusRing, K_Tooltip};
        CHECK((int)(sizeof(expected) / sizeof(expected[0])) == K_Count);
        for (int i = 0; i < K_Count; ++i) CHECK(expected[i] == i);   // contiguous, ascending
        CHECK(K_Minimap == 7);
        CHECK(K_Button == 13);
        CHECK(K_Text == 14);
        CHECK(K_Tooltip == 20);
        for (int i = 0; i < K_Count; ++i) CHECK(UILayerName(i)[0] != '\0');
        CHECK(std::string(UILayerName(K_Text)) == "text");
        CHECK(std::string(UILayerName(-1)).empty());
        CHECK(std::string(UILayerName(K_Count)).empty());
    }

    // --- UIDefaultLayer and UIQueueDrawItems agree for every widget -----
    {
        Scene s("parity"); s.physicsEnabled = false;
        struct Case { GameObject* g; int kind; };
        std::vector<Case> cases;
        auto mk = [&](const char* n) { return s.CreateGameObject(n); };
        { auto* g = mk("im");  g->AddComponent<UIImage>();          cases.push_back({g, K_Image}); }
        { auto* g = mk("raw"); g->AddComponent<UIRawImage>();       cases.push_back({g, K_Image}); }
        { auto* g = mk("pn");  g->AddComponent<UIPanel>();          cases.push_back({g, K_Panel}); }
        { auto* g = mk("sv");  g->AddComponent<UIScrollView>();     cases.push_back({g, K_Scroll}); }
        { auto* g = mk("pb");  g->AddComponent<UIProgressBar>();    cases.push_back({g, K_Progress}); }
        { auto* g = mk("rp");  g->AddComponent<UIRadialProgress>(); cases.push_back({g, K_Radial}); }
        { auto* g = mk("mm");  g->AddComponent<Minimap>();          cases.push_back({g, K_Minimap}); }
        { auto* g = mk("sl");  g->AddComponent<UISlider>();         cases.push_back({g, K_Slider}); }
        { auto* g = mk("st");  g->AddComponent<UIStepper>();        cases.push_back({g, K_Stepper}); }
        { auto* g = mk("ra");  g->AddComponent<UIRating>();         cases.push_back({g, K_Rating}); }
        { auto* g = mk("tg");  g->AddComponent<UIToggle>();         cases.push_back({g, K_Toggle}); }
        { auto* g = mk("tb");  g->AddComponent<UITabs>();           cases.push_back({g, K_Tabs}); }
        { auto* g = mk("bt");  g->AddComponent<UIButton>();         cases.push_back({g, K_Button}); }
        { auto* g = mk("tx");  g->AddComponent<TextRenderer>()->screenSpace = true; cases.push_back({g, K_Text}); }
        { auto* g = mk("wu");  g->AddComponent<WorldUI>();          cases.push_back({g, K_WorldUI}); }
        { auto* g = mk("dd");  g->AddComponent<UIDropdown>();       cases.push_back({g, K_Dropdown}); }
        { auto* g = mk("in");  g->AddComponent<UIInputField>();     cases.push_back({g, K_Input}); }
        { auto* g = mk("ch");  g->AddComponent<Crosshair>();        cases.push_back({g, K_Crosshair}); }
        { auto* g = mk("tt");  g->AddComponent<UITooltip>();        cases.push_back({g, K_Tooltip}); }
        for (const auto& c : cases) {
            CHECK(UIDefaultLayer(c.g) == c.kind);
            std::vector<int> k = KindsOf(c.g);
            CHECK(k.size() == 1);
            if (!k.empty()) CHECK(k[0] == c.kind);
        }
        // A drop target queues its background and its highlight; its default layer
        // is the background's.
        GameObject* dt = mk("dt"); dt->AddComponent<UIDropTarget>();
        CHECK(UIDefaultLayer(dt) == K_DropBg);
        std::vector<int> k = KindsOf(dt);
        CHECK(k.size() == 2);
        if (k.size() == 2) { CHECK(k[0] == K_DropBg); CHECK(k[1] == K_DropHi); }
        // World text isn't a UI layer but is still queued (the text block skips it).
        GameObject* wt = mk("wt"); wt->AddComponent<TextRenderer>();
        CHECK(UIDefaultLayer(wt) == -1);
        // Non-UI objects queue nothing.
        CHECK(KindsOf(mk("none")).empty());
        CHECK(UIDefaultLayer(nullptr) == -1);
    }

    // --- No renderer keeps a private copy of the table ------------------
    {
        const std::string root = OKAY_SOURCE_DIR;
        // The player's pass now lives in the engine renderer; the editor preview
        // still queues its own items.
        for (const char* rel : {"/player/main.cpp", "/editor/main.cpp", "/engine/src/UI/UIRenderer.cpp"}) {
            std::string src = ReadFile(root + rel);
            CHECK(!src.empty());
            CHECK(src.find("enum UIK") == std::string::npos);
            CHECK(src.find("K_DropBg =") == std::string::npos);
        }
        CHECK(ReadFile(root + "/engine/src/UI/UIRenderer.cpp").find("UIQueueDrawItems(") != std::string::npos);
        CHECK(ReadFile(root + "/editor/main.cpp").find("UIQueueDrawItems(") != std::string::npos);
        CHECK(ReadFile(root + "/player/main.cpp").find("ui2d::RenderSceneUI(") != std::string::npos);
    }

    TEST_MAIN_RESULT();
}
