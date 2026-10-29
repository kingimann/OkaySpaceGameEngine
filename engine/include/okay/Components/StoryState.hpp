#pragma once
// The story's global variables and flags: one StoryState per scene, made on
// demand on an object called "Story". Dialogue graphs, quests, scripts and visual
// scripts read and write it. It is an ordinary component record in the scene
// file, so a save game carries the story along with everything else.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay {

class StoryState final : public Behaviour {
public:
    std::map<std::string, vs::VsValue> vars;   ///< name -> number, text or true/false
    std::set<std::string> flags;

    /// The scene's StoryState; with `create`, one is added to a "Story" object when missing.
    static StoryState* Get(Scene* scene, bool create) {
        if (!scene) return nullptr;
        if (auto* st = scene->FindObjectOfType<StoryState>()) return st;
        if (!create) return nullptr;
        GameObject* go = scene->Find("Story");
        if (!go) go = scene->CreateGameObject("Story");
        return go->AddComponent<StoryState>();
    }

    vs::VsValue Get(const std::string& name) const {
        auto it = vars.find(name);
        return it == vars.end() ? vs::VsValue{} : it->second;
    }
    bool Has(const std::string& name) const { return vars.count(name) != 0; }
    void Set(const std::string& name, const vs::VsValue& v) { if (!name.empty()) vars[name] = v; }
    void Add(const std::string& name, float delta) { Set(name, vs::VsValue(Get(name).AsFloat() + delta)); }
    void Erase(const std::string& name) { vars.erase(name); }

    bool HasFlag(const std::string& name) const { return flags.count(name) != 0; }
    void SetFlag(const std::string& name, bool on = true) {
        if (name.empty()) return;
        if (on) flags.insert(name); else flags.erase(name);
    }

    /// Variable names in order, for inspectors and tests.
    std::vector<std::string> Names() const {
        std::vector<std::string> out;
        for (const auto& kv : vars) out.push_back(kv.first);
        return out;
    }
    void Clear() { vars.clear(); flags.clear(); }
};

} // namespace okay
