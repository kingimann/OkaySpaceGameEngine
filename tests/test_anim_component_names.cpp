#include "test_framework.hpp"
#include "AnimComponentNames.hpp"
#include "VisualScriptNodeDocs.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <Okay.hpp>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// Grok's #422 finding 8: two different components were both shown as "Animator". The state graph (AnimStateMachine)
// keeps that name; the simple keyframe clip player (class Animator) is shown as "Clip Player". The class name and the
// scene token ("animator") stay, so saved scenes load as before. The visual-script "Add Animator" fix-it button used to
// add the clip player to a node that needs the state graph (SetTrigger and the other Animator nodes): now it adds the graph.
using namespace okay;
using namespace okay::editor::vsed;

static std::string ReadFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s; s << f.rdbuf();
    return s.str();
}

int main() {
    RUN_SUITE("anim_component_names");

    // ---- the shown names --------------------------------------------------------------------------------------------
    CHECK(std::strcmp(kAnimGraphTitle, "Animator") == 0);
    CHECK(std::strcmp(kClipPlayerTitle, "Clip Player") == 0);

    // ---- which component a visual-script component id means ---------------------------------------------------------
    CHECK(VsAnimComponentFor("Animator") == VsAnimComponent::Graph);         // the "Animator" nodes drive the graph
    CHECK(VsAnimComponentFor("AnimStateMachine") == VsAnimComponent::Graph);
    CHECK(VsAnimComponentFor("ClipPlayer") == VsAnimComponent::ClipPlayer);
    CHECK(VsAnimComponentFor("Clip Player") == VsAnimComponent::ClipPlayer);
    CHECK(VsAnimComponentFor("Rigidbody") == VsAnimComponent::None);
    CHECK(VsAnimComponentFor(nullptr) == VsAnimComponent::None);

    // every Animator node (SetTrigger, SetBool, ...) asks for component "Animator", which means the state graph
    int animNodes = 0, toGraph = 0;
    for (const okay::vs::VsApiEntry& e : okay::vs::VsApiEntries()) {
        if (e.apiType != "Animator" || !e.hasTarget) continue;
        ++animNodes;
        const VsNodeDoc* d = VsApiDoc(e);
        if (d && d->component == "Animator" && VsAnimComponentFor(d->component.c_str()) == VsAnimComponent::Graph) ++toGraph;
    }
    std::printf("  Animator nodes: %d, asking for the state graph: %d\n", animNodes, toGraph);
    CHECK(animNodes > 0);
    CHECK(toGraph == animNodes);

    // ---- the editor shows and adds them under those names (main.cpp is too big for a unit test: read its text) -----
    const std::string src = ReadFile(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp");
    CHECK(!src.empty());
    CHECK(src.find("CompHeader(okay::editor::vsed::kClipPlayerTitle, anm,") != std::string::npos);
    CHECK(src.find("CompHeader(okay::editor::vsed::kAnimGraphTitle, sm,") != std::string::npos);
    CHECK(src.find("\"Animator (keyframes)\"") == std::string::npos);
    CHECK(src.find("\"Clip Player (keyframes)\"") != std::string::npos);
    CHECK(src.find("if (n == \"Animator\") return add(go->AddComponent<Animator>());") == std::string::npos);
    CHECK(src.find("okay::editor::vsed::VsAnimComponentFor(id)") != std::string::npos);
    const std::string widgets = ReadFile(std::string(OKAY_SOURCE_DIR) + "/editor/EditorWidgets.cpp");
    CHECK(widgets.find("has(\"Clip Player\")") != std::string::npos);   // keeps the animation colour and glyph

    // ---- a scene saved with the clip player still loads, and saves back the same text --------------------------------
    {
        Scene s;
        GameObject* go = s.CreateGameObject("Bouncer");
        auto* an = go->AddComponent<Animator>();
        an->clip.name = "Bounce";
        an->clip.AddKey("position.y", 0.0f, 0.5f);
        an->clip.AddKey("position.y", 0.5f, 2.0f);
        an->speed = 1.5f;
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("animator") != std::string::npos);            // the token is unchanged
        Scene back;
        CHECK(SceneSerializer::Deserialize(back, text));
        GameObject* b = back.Find("Bouncer");
        CHECK(b != nullptr);
        Animator* ba = b ? b->GetComponent<Animator>() : nullptr;
        CHECK(ba != nullptr);
        CHECK(b && b->GetComponent<AnimStateMachine>() == nullptr);   // loads as the clip player, not the graph
        if (ba) { CHECK(ba->clip.name == "Bounce"); CHECK_NEAR(ba->speed, 1.5f, 1e-6); }
        CHECK(SceneSerializer::Serialize(back) == text);
    }

    TEST_MAIN_RESULT();
}
