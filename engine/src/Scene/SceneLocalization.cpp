#include "okay/Scene/SceneLocalization.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/WorldUI.hpp"
#include "okay/Components/HoldPrompt.hpp"
#include "okay/Components/LoadingScreen.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace okay {

namespace {
// What a scene was last brought up to date with.
struct Stamp {
    std::uint32_t version = 0;
    std::uint64_t structure = ~0ull, components = ~0ull;
    std::string language;
    bool seen = false;
};
std::unordered_map<std::uint64_t, Stamp>& Stamps() {
    static std::unordered_map<std::uint64_t, Stamp> s;
    return s;
}
} // namespace

void LocalizeObject(GameObject& go) {
    if (auto* tr = go.GetComponent<TextRenderer>()) tr->loc.Apply(tr->text);
    if (auto* b = go.GetComponent<UIButton>()) b->loc.Apply(b->label);
    if (auto* t = go.GetComponent<UIToggle>()) t->loc.Apply(t->label);
    if (auto* tt = go.GetComponent<UITooltip>()) tt->loc.Apply(tt->text);
    if (auto* w = go.GetComponent<WorldUI>()) w->loc.Apply(w->text);
    if (auto* in = go.GetComponent<UIInputField>()) in->loc.Apply(in->placeholder);
}

void LocalizeScene(Scene& scene, bool force) {
    Stamp& st = Stamps()[scene.Uid()];
    const std::uint32_t version = Localization::Version();
    if (!force && st.version == version && st.structure == scene.StructureVersion() &&
        st.components == scene.ComponentVersion())
        return;
    st.version = version;
    st.structure = scene.StructureVersion();
    st.components = scene.ComponentVersion();
    const auto& objs = scene.Objects();
    for (std::size_t i = 0; i < objs.size(); ++i)
        if (objs[i] && !objs[i]->IsDestroyed()) LocalizeObject(*objs[i]);

    const std::string lang = Localization::Language();
    if (!st.seen || lang == st.language) { st.seen = true; st.language = lang; return; }
    st.language = lang;
    const std::vector<vs::VsValue> args{vs::VsValue(lang)};
    for (std::size_t i = 0; i < objs.size(); ++i) {   // by index: a handler may create objects
        GameObject* go = objs[i].get();
        if (!go || go->IsDestroyed() || !ScriptsRun(go)) continue;
        for (auto* sc : go->GetComponents<ScriptComponent>())
            if (sc->VM()) sc->VM()->CallEventArgs("on_language_changed", args);
        for (auto* vc : go->GetComponents<VisualScriptComponent>()) vc->RaiseEvent("OnLanguageChanged", nullptr, lang);
    }
}

std::vector<LocKeyUse> CollectLocKeys(const Scene& scene) {
    std::vector<LocKeyUse> out;
    auto add = [&](const std::string& key, GameObject* go, const char* where) {
        if (!key.empty()) out.push_back({key, go, where});
    };
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go) continue;
        if (auto* tr = go->GetComponent<TextRenderer>()) add(tr->loc.key, go, "Text");
        if (auto* b = go->GetComponent<UIButton>()) add(b->loc.key, go, "Button");
        if (auto* t = go->GetComponent<UIToggle>()) add(t->loc.key, go, "Toggle");
        if (auto* tt = go->GetComponent<UITooltip>()) add(tt->loc.key, go, "Tooltip");
        if (auto* w = go->GetComponent<WorldUI>()) add(w->loc.key, go, "World UI");
        if (auto* in = go->GetComponent<UIInputField>()) add(in->loc.key, go, "Input Field");
        if (auto* hp = go->GetComponent<HoldPrompt>(); hp && hp->localized)
            for (const auto& k : HoldPrompt::LocKeys()) add(k.first, go, "Hold Prompt");
        if (auto* ls = go->GetComponent<LoadingScreen>(); ls && ls->localized) {
            add(ls->title, go, "Loading Screen");
            add(ls->subtitle, go, "Loading Screen");
            for (const auto& t : ls->tips) add(t, go, "Loading Screen tip");
        }
        if (auto* dr = go->GetComponent<DialogueRunner>()) {   // the graph's lines, choices and speakers
            DialogueGraph g = dr->Graph();
            if (g.nodes.empty() && !dr->graphFile.empty()) g.Load(dr->ResolvedGraphFile());
            const std::string name = !g.name.empty() ? g.name : std::filesystem::path(dr->graphFile).stem().string();
            for (const auto& kv : DialogueStrings(g, name)) add(kv.first, go, "Dialogue");
            for (const auto& kv : DialogueRunner::LocKeys()) add(kv.first, go, "Dialogue box");
        }
        if (auto* ql = go->GetComponent<QuestLog>()) {
            std::vector<QuestDef> defs = ql->quests, fromFile;
            if (!ql->questFile.empty() && QuestFile::Load(ql->ResolvedFile(), fromFile))
                defs.insert(defs.end(), fromFile.begin(), fromFile.end());
            for (const QuestDef& q : defs)
                for (const auto& kv : QuestStrings(q)) add(kv.first, go, "Quest");
            for (const auto& kv : QuestLog::LocKeys()) add(kv.first, go, "Quest UI");
        }
    }
    return out;
}

} // namespace okay
