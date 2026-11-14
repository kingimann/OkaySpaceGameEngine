#pragma once
// Story nodes: dialogues, story variables and quests from a visual script.
//   Start Dialogue (arg: the NPC object or graph name), Set / Get Story Var
//   (arg: the variable), Start Quest, Complete Objective (in0: index) and
//   Quest State (arg: the quest id). The event entries (On Dialogue Event,
//   On Dialogue End, On Quest Started / Completed / Event) are plain EventNodes
//   raised by the Dialogue Runner and the Quest Log; Event Name carries the
//   event / quest id, Event Value the objective index or completion count.
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Scene/GameObject.hpp"
#include <string>

namespace okay::vs {

inline Scene* StorySceneOf(VsContext& ctx) { return ctx.gameObject ? ctx.gameObject->scene() : nullptr; }

struct StartDialogueNode : VsNode {   // arg: object name / graph name (empty = this object's runner)
    std::string name;
    explicit StartDialogueNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        DialogueRunner* r = nullptr;
        if (name.empty() && ctx.gameObject) r = ctx.gameObject->GetComponent<DialogueRunner>();
        if (!r) r = DialogueRunner::FindByName(StorySceneOf(ctx), name);
        if (r) r->StartDialogue();
        return 0;
    }
};

struct SetStoryVarNode : VsNode {   // arg: variable; in0 = value
    std::string name;
    explicit SetStoryVarNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (StoryState* st = StoryState::Get(StorySceneOf(ctx), true)) st->Set(name, In(0, g, ctx));
        return 0;
    }
};

struct GetStoryVarNode : VsNode {   // arg: variable; out0 = value
    std::string name;
    explicit GetStoryVarNode(std::string n) : name(std::move(n)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        StoryState* st = StoryState::Get(StorySceneOf(ctx), false);
        return st ? st->Get(name) : VsValue{};
    }
};

struct StartQuestNode : VsNode {   // arg: quest id
    std::string id;
    explicit StartQuestNode(std::string q) : id(std::move(q)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        if (QuestLog* q = QuestLog::Get(StorySceneOf(ctx), true)) q->StartQuest(id);
        return 0;
    }
};

struct CompleteObjectiveNode : VsNode {   // arg: quest id; in0 = objective index
    std::string id;
    explicit CompleteObjectiveNode(std::string q) : id(std::move(q)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (QuestLog* q = QuestLog::Get(StorySceneOf(ctx), true)) q->CompleteObjective(id, (int)In(0, g, ctx, VsValue{0.0f}).AsFloat());
        return 0;
    }
};

struct QuestStateNode : VsNode {   // arg: quest id; out0 = "inactive" / "active" / "completed" / "failed"
    std::string id;
    explicit QuestStateNode(std::string q) : id(std::move(q)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        QuestLog* q = QuestLog::Get(StorySceneOf(ctx), false);
        return std::string(q ? q->StatusName(id) : "inactive");
    }
};

} // namespace okay::vs
