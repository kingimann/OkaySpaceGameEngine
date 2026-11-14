#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::vector<QuestDef> BuildQuests() {
    std::vector<QuestDef> qs;
    {
        QuestDef q; q.id = "gather"; q.title = "Ore run"; q.description = "The smith needs ore.";
        QuestObjective ob; ob.kind = QuestObjective::Kind::Collect; ob.target = "ore"; ob.count = 3; ob.description = "Collect 3 ore";
        q.objectives.push_back(ob);
        q.reward.items.push_back({"coin", 5});
        q.reward.vars.push_back({"reputation", "10"});
        q.reward.event = "gather_done";
        qs.push_back(q);
    }
    {
        QuestDef q; q.id = "hunt"; q.title = "Wolf hunt"; q.prerequisites = {"gather"};
        QuestObjective ob; ob.kind = QuestObjective::Kind::Kill; ob.target = "Wolf"; ob.count = 2; ob.description = "Kill 2 wolves";
        q.objectives.push_back(ob);
        qs.push_back(q);
    }
    {
        QuestDef q; q.id = "visit"; q.title = "The forge";
        QuestObjective ob; ob.kind = QuestObjective::Kind::Reach; ob.target = "forge"; ob.description = "Reach the forge";
        q.objectives.push_back(ob);
        qs.push_back(q);
    }
    {
        QuestDef q; q.id = "talk"; q.title = "A word with the smith";
        QuestObjective ob; ob.kind = QuestObjective::Kind::Talk; ob.target = "Smith"; ob.description = "Talk to the smith";
        q.objectives.push_back(ob);
        qs.push_back(q);
    }
    {
        QuestDef q; q.id = "custom"; q.title = "Scripted"; q.repeatable = true;
        QuestObjective ob; ob.kind = QuestObjective::Kind::Custom; ob.description = "Whatever the script says";
        QuestObjective opt; opt.kind = QuestObjective::Kind::Custom; opt.description = "A bonus"; opt.optional = true;
        q.objectives = {ob, opt};
        qs.push_back(q);
    }
    return qs;
}

// A player with a bag and a script, a smith with a two-line dialogue, two wolves, a forge zone.
struct World {
    GameObject* player = nullptr;
    Inventory* bag = nullptr;
    ScriptComponent* script = nullptr;
    QuestLog* log = nullptr;
    DialogueRunner* smith = nullptr;
    QuestTrigger* forge = nullptr;
    std::vector<std::string> notes;
};

World Build(Scene& s) {
    World w;
    w.player = s.CreateGameObject("Player");
    w.player->tag = "Player";
    w.bag = w.player->AddComponent<Inventory>();
    w.script = w.player->AddComponent<ScriptComponent>("okayscript");
    w.script->LoadSource(
        "var started = \"\"\nvar completed = \"\"\nvar reward_event = \"\"\nvar objective = -1\n"
        "function on_quest_started(id) { started = id }\n"
        "function on_quest_completed(id) { completed = id }\n"
        "function on_quest_event(name) { reward_event = name }\n"
        "function on_objective_completed(id, index) { objective = index }\n"
        "function finish() { return complete_objective(\"custom\", 0) }\n"
        "function state(id) { return quest_state(id) }\n"
        "function active(id) { return quest_active(id) }\n"
        "function fail(id) { return fail_quest(id) }\n"
        "function begin(id) { return start_quest(id) }\n");
    w.log = QuestLog::Get(&s, true);
    w.log->quests = BuildQuests();
    GameObject* smith = s.CreateGameObject("Smith");
    w.smith = smith->AddComponent<DialogueRunner>();
    w.smith->drawUI = false;
    w.smith->typewriterSpeed = 0.0f;
    {
        DialogueGraph g; g.name = "smith_talk";
        auto& a = g.Add(DialogueNode::Kind::Line, 0, 0); a.speaker = "Smith"; a.text = "Hello."; a.next = 1;
        auto& b = g.Add(DialogueNode::Kind::Line, 0, 0); b.speaker = "Smith"; b.text = "Bye."; b.next = -1;
        w.smith->SetGraph(g);
    }
    for (int i = 0; i < 2; ++i) {
        GameObject* wolf = s.CreateGameObject("Wolf");
        wolf->tag = "Wolf";
        auto* hp = wolf->AddComponent<HealthStat>();
        hp->playDeathAnim = false;
        hp->maxHealth = 10.0f;
    }
    GameObject* forge = s.CreateGameObject("Forge");
    w.forge = forge->AddComponent<QuestTrigger>();
    w.forge->zone = "forge";
    s.Start();
    s.Update(0.016f);
    return w;
}

} // namespace

int main() {
    RUN_SUITE("quests");

    // ---- The .okayquest file: write, parse, validate ----
    {
        std::vector<QuestDef> qs = BuildQuests();
        const std::string text = QuestFile::Write(qs);
        std::vector<QuestDef> back;
        std::string err;
        CHECK(QuestFile::Parse(text, back, &err));
        CHECK(err.empty());
        CHECK(back.size() == qs.size());
        CHECK(QuestFile::Write(back) == text);
        CHECK(back[0].id == "gather" && back[0].title == "Ore run" && back[0].description == "The smith needs ore.");
        CHECK(back[0].objectives.size() == 1 && back[0].objectives[0].kind == QuestObjective::Kind::Collect && back[0].objectives[0].count == 3);
        CHECK(back[0].reward.items.size() == 1 && back[0].reward.items[0].first == "coin" && back[0].reward.items[0].second == 5);
        CHECK(back[0].reward.vars.size() == 1 && back[0].reward.vars[0].second == "10" && back[0].reward.event == "gather_done");
        CHECK(back[1].prerequisites.size() == 1 && back[1].prerequisites[0] == "gather");
        CHECK(back[4].repeatable && back[4].objectives.size() == 2 && back[4].objectives[1].optional);
        CHECK(QuestFile::Validate(qs).empty());
        qs.push_back(qs[0]);                     // duplicate id
        qs[1].prerequisites.push_back("ghost");  // unknown prerequisite
        auto issues = QuestFile::Validate(qs);
        int dup = 0, ghost = 0;
        for (const auto& i : issues) { if (i.text.find("Duplicate") != std::string::npos) ++dup; if (i.text.find("ghost") != std::string::npos) ++ghost; }
        CHECK(dup == 1 && ghost == 1);
        CHECK(!QuestFile::Parse("okayquest 1\nobjective kind=talk\n", back, &err));
        CHECK(!QuestFile::Parse("okayquest 1\nquest id=\"a\"\nobjective kind=fly\n", back, &err));
    }

    // ---- Objectives from every source, prerequisites, rewards once ----
    {
        Scene s("Quests");
        World w = Build(s);
        std::vector<std::string> notes;
        w.log->onNotify = [&](const std::string& t, const std::string& b, int) { notes.push_back(t + ": " + b); };
        std::string why;
        CHECK(!w.log->CanStart("hunt", &why));
        CHECK(why.find("gather") != std::string::npos);
        CHECK(!w.log->StartQuest("hunt"));
        CHECK(!w.log->StartQuest("nope"));
        CHECK(w.log->Status("hunt") == QuestStatus::Inactive);

        // Collect: counted from the player's inventory each frame.
        CHECK(w.log->StartQuest("gather"));
        CHECK(!w.log->StartQuest("gather"));              // already active
        CHECK(w.log->IsActive("gather") && w.log->tracked == "gather");
        s.Update(0.016f);
        CHECK(w.script->VM()->GetGlobal("started").AsString() == "gather");
        w.bag->Add("ore", 2);
        s.Update(0.016f);
        CHECK(w.log->Progress("gather", 0) == 2 && !w.log->ObjectiveDone("gather", 0));
        w.bag->Add("ore", 1);
        s.Update(0.016f);
        CHECK(w.log->ObjectiveDone("gather", 0));
        CHECK(w.log->Status("gather") == QuestStatus::Completed);
        CHECK(w.bag->Count("coin") == 5);                                   // reward items
        CHECK(StoryState::Get(&s, false) && StoryState::Get(&s, false)->Get("reputation").AsFloat() == 10.0f);
        s.Update(0.016f);
        CHECK(w.script->VM()->GetGlobal("completed").AsString() == "gather");
        CHECK(w.script->VM()->GetGlobal("reward_event").AsString() == "gather_done");
        CHECK(w.script->VM()->GetGlobal("objective").AsFloat() == 0.0f);
        CHECK(!w.log->CompleteQuest("gather"));                             // only once
        CHECK(!w.log->CompleteObjective("gather", 0));
        CHECK(w.bag->Count("coin") == 5);
        CHECK(!w.log->StartQuest("gather"));                                // not repeatable
        bool started = false, done = false;
        for (const auto& n : notes) { if (n == "Quest started: Ore run") started = true; if (n == "Quest completed: Ore run") done = true; }
        CHECK(started && done);
        CHECK(w.log->notifications.size() >= 3);

        // Kill: a tagged Health Stat dying counts.
        CHECK(w.log->CanStart("hunt"));
        CHECK(w.log->StartQuest("hunt"));
        std::vector<HealthStat*> wolves = s.FindObjectsOfType<HealthStat>();
        CHECK(wolves.size() == 2);
        wolves[0]->Damage(1000.0f);
        CHECK(w.log->Progress("hunt", 0) == 1 && w.log->IsActive("hunt"));
        wolves[1]->Damage(1000.0f);
        CHECK(w.log->Status("hunt") == QuestStatus::Completed);

        // Reach: the trigger zone.
        CHECK(w.log->StartQuest("visit"));
        w.forge->Fire(w.player);
        CHECK(w.log->Status("visit") == QuestStatus::Completed);
        CHECK(w.forge->Fired());
        w.forge->Fire(w.player);                                           // once: harmless

        // Talk: the dialogue on the smith ending.
        CHECK(w.log->StartQuest("talk"));
        CHECK(w.smith->StartDialogue());
        CHECK(w.log->IsActive("talk"));
        w.smith->Advance();
        CHECK(w.smith->IsActive());
        w.smith->Advance();
        CHECK(!w.smith->IsActive());
        CHECK(w.log->Status("talk") == QuestStatus::Completed);

        // Custom: scripts complete it; optional objectives don't hold it up; repeatable restarts.
        CHECK(w.script->VM()->CallEventArgs("begin", {vs::VsValue(std::string("custom"))}).AsBool());
        CHECK(w.script->VM()->CallEventArgs("state", {vs::VsValue(std::string("custom"))}).AsString() == "active");
        CHECK(w.script->VM()->CallEventArgs("active", {vs::VsValue(std::string("custom"))}).AsBool());
        CHECK(w.script->VM()->CallEventArgs("finish", {}).AsBool());
        CHECK(w.script->VM()->CallEventArgs("state", {vs::VsValue(std::string("custom"))}).AsString() == "completed");
        CHECK(w.log->Record("custom")->completions == 1);
        CHECK(w.log->StartQuest("custom"));                                 // repeatable
        CHECK(w.script->VM()->CallEventArgs("fail", {vs::VsValue(std::string("custom"))}).AsBool());
        CHECK(w.log->Status("custom") == QuestStatus::Failed);
        CHECK(!w.log->FailQuest("custom"));
        CHECK(w.log->StartQuest("custom"));                                 // a failed quest can be retried
        CHECK(w.log->AddProgress("custom", 0, 1));                          // count-less objectives need 1
        CHECK(w.log->Status("custom") == QuestStatus::Completed && w.log->Record("custom")->completions == 2);
    }

    // ---- Records and inline definitions through the scene serializer ----
    {
        Scene s("Save");
        World w = Build(s);
        w.log->questFile = "Assets/main.okayquest";
        w.log->journalKey = 'l';
        w.log->showTracker = false;
        CHECK(w.log->StartQuest("gather"));
        w.bag->Add("ore", 2);
        s.Update(0.016f);
        CHECK(w.log->StartQuest("visit"));
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("questlog") != std::string::npos && text.find("questlogdef") != std::string::npos && text.find("questtrigger") != std::string::npos);
        Scene back("Back");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        CHECK(err.empty());
        QuestLog* log2 = QuestLog::Get(&back, false);
        CHECK(log2 != nullptr);
        if (log2) {
            CHECK(log2->questFile == "Assets/main.okayquest" && log2->journalKey == 'l' && !log2->showTracker && log2->tracked == "gather");
            CHECK(log2->quests.size() == 5);
            CHECK(log2->Def("hunt") && log2->Def("hunt")->prerequisites == std::vector<std::string>{"gather"});
            CHECK(log2->Def("gather") && log2->Def("gather")->reward.items.size() == 1 && log2->Def("gather")->reward.event == "gather_done");
            CHECK(log2->Def("custom") && log2->Def("custom")->repeatable && log2->Def("custom")->objectives[1].optional);
            CHECK(log2->Status("gather") == QuestStatus::Active && log2->Progress("gather", 0) == 2);
            CHECK(log2->Status("visit") == QuestStatus::Active && log2->Status("hunt") == QuestStatus::Inactive);
            // The loaded log keeps working: the third ore completes the quest.
            back.Start();
            back.Update(0.016f);
            GameObject* p = back.Find("Player");
            CHECK(p && p->GetComponent<Inventory>() && p->GetComponent<Inventory>()->Count("ore") == 2);
            if (p) p->GetComponent<Inventory>()->Add("ore", 1);
            back.Update(0.016f);
            CHECK(log2->Status("gather") == QuestStatus::Completed);
            CHECK(p && p->GetComponent<Inventory>()->Count("coin") == 5);
        }
        auto* qt2 = back.Find("Forge") ? back.Find("Forge")->GetComponent<QuestTrigger>() : nullptr;
        CHECK(qt2 && qt2->zone == "forge");
    }

    // ---- Through a save game: quest state and story variables come back ----
    {
        const fs::path dir = fs::temp_directory_path() / "okay_test_quests_save";
        fs::remove_all(dir);
        fs::create_directories(dir);
        Save::SetBaseDir(dir.string());
        Scene s("Level");
        World w = Build(s);
        CHECK(w.log->StartQuest("gather"));
        CHECK(w.log->StartQuest("talk"));
        w.bag->Add("ore", 1);
        s.Update(0.016f);
        StoryState::Get(&s, true)->Set("chapter", vs::VsValue(2.0f));
        StoryState::Get(&s, true)->SetFlag("met_smith");
        std::string err;
        CHECK(SaveGame::Save(s, "q1", "Quests", &err));
        CHECK(err.empty());
        // Play on: finish things, then load the save and they are back where they were.
        w.bag->Add("ore", 5);
        s.Update(0.016f);
        CHECK(w.log->Status("gather") == QuestStatus::Completed);
        w.log->FailQuest("talk");
        StoryState::Get(&s, true)->Set("chapter", vs::VsValue(9.0f));
        CHECK(SaveGame::LoadNow(s, "q1", &err));
        QuestLog* log2 = QuestLog::Get(&s, false);
        CHECK(log2 != nullptr);
        if (log2) {
            CHECK(log2->Status("gather") == QuestStatus::Active && log2->Progress("gather", 0) == 1);
            CHECK(log2->Status("talk") == QuestStatus::Active);
            CHECK(log2->quests.size() == 5);
        }
        StoryState* st = StoryState::Get(&s, false);
        CHECK(st && st->Get("chapter").AsFloat() == 2.0f && st->HasFlag("met_smith"));
        GameObject* p = s.Find("Player");
        CHECK(p && p->GetComponent<Inventory>() && p->GetComponent<Inventory>()->Count("ore") == 1);
        // ...and the loaded quests still complete from their sources.
        s.Update(0.016f);
        if (p) p->GetComponent<Inventory>()->Add("ore", 2);
        s.Update(0.016f);
        CHECK(log2 && log2->Status("gather") == QuestStatus::Completed);
        auto* smith = s.Find("Smith") ? s.Find("Smith")->GetComponent<DialogueRunner>() : nullptr;
        CHECK(smith != nullptr);
        if (smith) {
            // The runner's graph came from code (not a file), so give it again after the load.
            DialogueGraph g; g.name = "smith_talk";
            auto& a = g.Add(DialogueNode::Kind::Line, 0, 0); a.text = "Hi"; a.next = -1;
            smith->SetGraph(g);
            CHECK(smith->StartDialogue());
            smith->Advance();
            CHECK(log2 && log2->Status("talk") == QuestStatus::Completed);
        }
        fs::remove_all(dir);
    }

    TEST_MAIN_RESULT();
}
