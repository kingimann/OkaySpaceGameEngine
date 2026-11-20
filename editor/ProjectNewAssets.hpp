#pragma once
// The starting text of the files the Project panel's Create menu makes for the story and AI windows: a dialogue, a quest
// file, a behavior tree and a cutscene. Each one is written by the same classes the windows save with
// (DialogueGraph::Write, QuestFile::Write, BehaviorTree::Serialize, Sequence::Write), so the window opens it as it opens
// any file of that type, and a game loads it the same way. The panel writes the text with okaypb::Browser::MakeFile
// (atomic, never over an existing file), so these functions only make the text.
//
// Written from the starting documents the Dialogue and Behavior Tree windows already make for their own "New" button
// (docs/dialogue.md, docs/gameplay/behavior_trees.md); tests/test_project_menu_ops.cpp reads each text back and validates it.
#include "okay/AI/BehaviorTree.hpp"
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/Sequence.hpp"

#include <string>
#include <vector>

namespace okaypb {

/// One NPC line that ends the conversation, with the NPC and Player in the speaker list.
inline std::string NewDialogueText(const std::string& name) {
    okay::DialogueGraph g;
    g.name = name.empty() ? std::string("New Dialogue") : name;
    g.speakers = {"NPC", "Player"};
    okay::DialogueNode& n = g.Add(okay::DialogueNode::Kind::Line, 80.0f, 120.0f);
    n.speaker = "NPC";
    n.text = "Hello there.";
    g.start = n.id;
    return g.Write();
}

/// One example quest with one objective, so the Quests tab has something to edit.
inline std::string NewQuestText() {
    okay::QuestDef q;
    q.id = "quest1";
    q.title = "New quest";
    q.description = "What the player has to do.";
    okay::QuestObjective o;
    o.kind = okay::QuestObjective::Kind::Custom;
    o.description = "Do the thing";
    q.objectives.push_back(o);
    return okay::QuestFile::Write({q});
}

/// A Selector at the top with a one-second Wait under it: a tree that checks clean and runs, ready to build on.
inline std::string NewBehaviorTreeText(const std::string& name) {
    okay::BehaviorTree t;
    t.name = name.empty() ? std::string("Tree") : name;
    const int root = t.AddNode(okay::BtNodeType::Selector);
    t.AddNode(okay::BtNodeType::Wait, root);
    return t.Serialize();
}

/// An empty cutscene sequence with the given name.
inline std::string NewSequenceText(const std::string& name) {
    okay::Sequence s;
    s.name = name.empty() ? std::string("Sequence") : name;
    return s.Write();
}

} // namespace okaypb
