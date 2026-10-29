#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include <map>
#include <string>
#include <vector>

using namespace okay;
using Kind = DialogueNode::Kind;

namespace {

// A shop keeper: greets, checks the player's gold, offers a sword (10 gold), a
// quest, or nothing. Covers every node kind.
DialogueGraph BuildShop() {
    DialogueGraph g;
    g.name = "Smith";
    g.speakers = {"Smith", "Player"};
    g.locals = {{"visits", "0"}};
    auto& greet = g.Add(Kind::Line, 0, 0);          // 0
    greet.speaker = "Smith"; greet.text = "Welcome, traveler."; greet.next = 1;
    auto& rich = g.Add(Kind::Condition, 200, 0);    // 1: gold >= 10 ?
    rich.condition.kind = DialogueCondition::Kind::StoryVar; rich.condition.name = "gold"; rich.condition.op = ">="; rich.condition.value = "10";
    rich.onTrue = 2; rich.onFalse = 3;
    auto& yes = g.Add(Kind::Line, 400, -60);        // 2
    yes.speaker = "Smith"; yes.text = "You look like you can afford something."; yes.next = 4;
    auto& no = g.Add(Kind::Line, 400, 60);          // 3
    no.speaker = "Smith"; no.text = "Come back with some coin."; no.next = 4;
    auto& ask = g.Add(Kind::Choice, 600, 0);        // 4
    ask.choices.clear();
    DialogueChoice buy; buy.text = "Buy a sword (10 gold)"; buy.target = 5;
    buy.condition.kind = DialogueCondition::Kind::StoryVar; buy.condition.name = "gold"; buy.condition.op = ">="; buy.condition.value = "10";
    DialogueChoice quest; quest.text = "Any work for me?"; quest.target = 9;
    quest.condition.kind = DialogueCondition::Kind::QuestState; quest.condition.name = "q1"; quest.condition.op = "=="; quest.condition.value = "inactive";
    DialogueChoice bye; bye.text = "Just looking."; bye.target = 11;
    ask.choices = {buy, quest, bye};
    auto& take = g.Add(Kind::TakeItem, 800, -80);    // 5
    take.item = "gold"; take.count = 10; take.next = 6;
    auto& give = g.Add(Kind::GiveItem, 1000, -80);   // 6
    give.item = "sword"; give.count = 1; give.next = 7;
    auto& flag = g.Add(Kind::Set, 1200, -80);        // 7: flag bought
    flag.setScope = 1; flag.name = "bought"; flag.value = "true"; flag.next = 8;
    auto& ev = g.Add(Kind::Event, 1400, -80);        // 8
    ev.name = "sword_sold"; ev.next = 12;
    auto& startQ = g.Add(Kind::StartQuest, 800, 80); // 9
    startQ.name = "q1"; startQ.next = 10;
    auto& qline = g.Add(Kind::Line, 1000, 80);       // 10
    qline.speaker = "Smith"; qline.text = "Bring me three ores."; qline.next = 12;
    auto& setVar = g.Add(Kind::Set, 800, 200);       // 11: visits (local) + a story var
    setVar.setScope = 0; setVar.name = "last_visit"; setVar.value = "smith"; setVar.next = 12;
    auto& end = g.Add(Kind::End, 1600, 0);           // 12
    (void)end;
    g.start = 0;
    return g;
}

struct TestHost : DialogueHost {
    std::map<std::string, vs::VsValue> vars;
    std::map<std::string, bool> flags;
    std::map<std::string, int> items;
    std::map<std::string, std::string> quests;
    std::vector<std::string> events, objectives;
    vs::VsValue GetVar(const std::string& n) override { auto it = vars.find(n); return it == vars.end() ? vs::VsValue{} : it->second; }
    void SetVar(const std::string& n, const vs::VsValue& v) override { vars[n] = v; }
    bool HasFlag(const std::string& n) override { return flags[n]; }
    void SetFlag(const std::string& n, bool on) override { flags[n] = on; }
    std::string QuestState(const std::string& id) override { auto it = quests.find(id); return it == quests.end() ? "inactive" : it->second; }
    int  ItemCount(const std::string& it) override { return items[it]; }
    bool GiveItem(const std::string& it, int n) override { items[it] += n; return true; }
    bool TakeItem(const std::string& it, int n) override { if (items[it] < n) return false; items[it] -= n; return true; }
    void StartQuest(const std::string& id) override { quests[id] = "active"; }
    void CompleteObjective(const std::string& id, int i) override { objectives.push_back(id + "#" + std::to_string(i)); }
    void Event(const std::string& n) override { events.push_back(n); }
};

} // namespace

int main() {
    RUN_SUITE("dialogue");

    // ---- Parse / write round trip ----
    {
        DialogueGraph g = BuildShop();
        const std::string text = g.Write();
        DialogueGraph back;
        std::string err;
        CHECK(back.Parse(text, &err));
        CHECK(err.empty());
        CHECK(back.name == "Smith");
        CHECK(back.speakers.size() == 2 && back.speakers[1] == "Player");
        CHECK(back.locals.size() == 1 && back.locals[0].name == "visits" && back.locals[0].value == "0");
        CHECK(back.nodes.size() == g.nodes.size());
        CHECK(back.Write() == text);   // stable
        const DialogueNode* ask = back.Find(4);
        CHECK(ask && ask->kind == Kind::Choice && ask->choices.size() == 3);
        CHECK(ask && ask->choices[0].condition.kind == DialogueCondition::Kind::StoryVar && ask->choices[0].condition.value == "10");
        CHECK(ask && ask->choices[1].condition.kind == DialogueCondition::Kind::QuestState && ask->choices[1].target == 9);
        const DialogueNode* cond = back.Find(1);
        CHECK(cond && cond->onTrue == 2 && cond->onFalse == 3 && cond->condition.op == ">=");
        const DialogueNode* give = back.Find(6);
        CHECK(give && give->item == "sword" && give->count == 1 && give->next == 7);
        // Text with quotes and newlines survives.
        DialogueGraph q;
        auto& n = q.Add(Kind::Line, 1, 2);
        n.text = "She said \"hi\"\nand left \\ quickly";
        DialogueGraph q2;
        CHECK(q2.Parse(q.Write()));
        CHECK(q2.nodes.size() == 1 && q2.nodes[0].text == n.text);
        // A broken file is refused with a reason.
        DialogueGraph bad;
        CHECK(!bad.Parse("okayscene 3\n", &err));
        CHECK(!err.empty());
        CHECK(!bad.Parse("okaydialog 1\nnode id=0 kind=teleport\n", &err));
    }

    // ---- Traversal: rich player buys the sword ----
    {
        DialogueGraph g = BuildShop();
        TestHost host;
        host.vars["gold"] = vs::VsValue(20.0f);
        host.items["gold"] = 20;
        DialogueSession s;
        s.Begin(&g, &host);
        CHECK(s.Active());
        CHECK(s.Node() && s.Node()->id == 0);
        CHECK(s.Locals()["visits"].AsFloat() == 0.0f);
        s.Advance();
        CHECK(s.Node() && s.Node()->id == 2);   // the condition picked the rich line
        s.Advance();
        CHECK(s.AtChoice());
        CHECK(s.Choices().size() == 3);         // every option passes its condition
        CHECK(s.Choose(0));                     // buy: take 10 gold, give a sword, flag, event, end
        CHECK(!s.Active() && s.Ended());
        CHECK(host.items["gold"] == 10);
        CHECK(host.items["sword"] == 1);
        CHECK(host.flags["bought"]);
        CHECK(host.events.size() == 1 && host.events[0] == "sword_sold");
        // The trail lists what ran, silent nodes included.
        const auto& t = s.Trail();
        CHECK(t.size() == 9 && t[0] == 0 && t[1] == 1 && t[2] == 2 && t[3] == 4 && t.back() == 12);
    }

    // ---- Traversal: poor player, hidden option, quest started ----
    {
        DialogueGraph g = BuildShop();
        TestHost host;
        host.vars["gold"] = vs::VsValue(3.0f);
        DialogueSession s;
        s.Begin(&g, &host);
        s.Advance();
        CHECK(s.Node() && s.Node()->id == 3);
        s.Advance();
        CHECK(s.Choices().size() == 2);         // "Buy a sword" is hidden
        CHECK(s.Choices()[0].text == "Any work for me?");
        CHECK(s.Choose(0));
        CHECK(host.quests["q1"] == "active");
        CHECK(s.Node() && s.Node()->id == 10);
        s.Advance();
        CHECK(s.Ended());
        // Once the quest is active that option disappears too; "Just looking" sets a story variable.
        s.Begin(&g, &host);
        s.Advance(); s.Advance();
        CHECK(s.Choices().size() == 1);
        CHECK(s.Choose(0));
        CHECK(host.vars["last_visit"].AsString() == "smith");
        CHECK(!s.Choose(0));                    // nothing to choose after the end
        // Flag and item conditions.
        DialogueCondition f; f.kind = DialogueCondition::Kind::Flag; f.name = "bought";
        CHECK(!s.Test(f));
        host.flags["bought"] = true;
        CHECK(s.Test(f));
        f.op = "!=";
        CHECK(!s.Test(f));
        DialogueCondition it; it.kind = DialogueCondition::Kind::HasItem; it.name = "sword"; it.count = 2;
        host.items["sword"] = 1;
        CHECK(!s.Test(it));
        host.items["sword"] = 2;
        CHECK(s.Test(it));
        // Text compare and typed values.
        CHECK(DialogueCompare(vs::VsValue(std::string("smith")), "==", DialogueValueFromText("smith")));
        CHECK(DialogueValueFromText("true").IsBool() && DialogueValueFromText("2.5").IsNumber() && DialogueValueFromText("hello").IsString());
        CHECK(DialogueCompare(vs::VsValue(2.0f), "<", vs::VsValue(2.5f)));
    }

    // ---- Validation ----
    {
        DialogueGraph g = BuildShop();
        std::vector<std::string> quests{"q1"};
        std::vector<std::string> items{"gold", "sword"};
        auto issues = g.Validate(&quests, &items);
        CHECK(!DialogueGraph::HasErrors(issues));
        g.Find(3)->next = 99;   // a missing target
        issues = g.Validate(&quests, &items);
        CHECK(DialogueGraph::HasErrors(issues));
        bool missing = false;
        for (const auto& i : issues) if (i.node == 3 && i.severity == 2) missing = true;
        CHECK(missing);
        // Unreachable node, empty option, unknown item / quest.
        DialogueGraph h;
        h.Add(Kind::Line, 0, 0).text = "start";
        auto& lonely = h.Add(Kind::Line, 0, 100);
        lonely.text = "nobody comes here";
        auto& ch = h.Add(Kind::Choice, 0, 200);
        ch.choices[0].text.clear();
        h.Find(0)->next = ch.id;
        auto& gi = h.Add(Kind::GiveItem, 0, 300);
        gi.item = "unobtainium"; gi.next = -1;
        ch.choices[1].target = gi.id;
        auto& sq = h.Add(Kind::StartQuest, 0, 400);
        sq.name = "nope"; sq.next = -1;
        issues = h.Validate(&quests, &items);
        int unreachable = 0, emptyOpt = 0, unknownItem = 0, unknownQuest = 0;
        for (const auto& i : issues) {
            if (i.node == lonely.id && i.text.find("Unreachable") != std::string::npos) ++unreachable;
            if (i.node == ch.id && i.severity == 2 && i.text.find("no text") != std::string::npos) ++emptyOpt;
            if (i.node == gi.id && i.text.find("Unknown item") != std::string::npos) ++unknownItem;
            if (i.node == sq.id && i.text.find("Unknown quest") != std::string::npos) ++unknownQuest;
        }
        CHECK(unreachable == 1 && emptyOpt == 1 && unknownItem == 1 && unknownQuest == 1);
        CHECK(DialogueGraph::HasErrors(issues));
        // Remove unlinks.
        h.Remove(gi.id);
        CHECK(h.Find(ch.id)->choices[1].target == -1);
    }

    // ---- The runner in a scene: Advance / Choose, typewriter, items, quests, script events ----
    {
        Scene scene("Town");
        GameObject* player = scene.CreateGameObject("Player");
        player->tag = "Player";
        auto* bag = player->AddComponent<Inventory>();
        bag->Add("gold", 20);
        auto* sc = player->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var last_event = \"\"\nvar ended = 0\n"
            "function on_dialogue_event(name) { last_event = name }\n"
            "function on_dialogue_end() { ended = ended + 1 }\n"
            "function go() { return start_dialogue(\"Smith\") }\n"
            "function active() { return dialogue_active() }\n"
            "function remember() { set_story(\"hero\", \"Ann\") set_story(\"gold\", 7) return get_story(\"hero\") }\n"));
        GameObject* smith = scene.CreateGameObject("Smith");
        smith->transform->SetPosition(Vec3{2.0f, 0.0f, 0.0f});
        auto* runner = smith->AddComponent<DialogueRunner>();
        runner->SetGraph(BuildShop());
        runner->typewriterSpeed = 20.0f;   // 20 characters per second
        runner->drawUI = false;
        auto* log = QuestLog::Get(&scene, true);
        QuestDef q; q.id = "q1"; q.title = "Ore run";
        QuestObjective ob; ob.kind = QuestObjective::Kind::Collect; ob.target = "ore"; ob.count = 3; ob.description = "Collect ore";
        q.objectives.push_back(ob);
        log->quests.push_back(q);
        StoryState::Get(&scene, true)->Set("gold", vs::VsValue(20.0f));
        scene.Start();
        scene.Update(0.016f);

        CHECK(!runner->IsActive());
        CHECK(runner->CurrentLine().nodeId == -1);
        CHECK(runner->InRange());
        // The interact key starts it (the player is 2 m away, range 3).
        Input::FeedKeys({'e'});
        scene.Update(0.016f);
        Input::FeedKeys({});
        CHECK(runner->IsActive());
        CHECK(DialogueRunner::ActiveIn(&scene) == runner);
        DialogueRunner::Line l = runner->CurrentLine();
        CHECK(l.speaker == "Smith" && l.text == "Welcome, traveler.");
        CHECK(!l.complete && l.shownText.size() < l.text.size());   // still typing
        scene.Update(0.5f);
        l = runner->CurrentLine();
        CHECK(l.shownText.size() >= 10 && l.shownText.size() < l.text.size());
        runner->Advance();                                          // first Advance finishes the typewriter
        l = runner->CurrentLine();
        CHECK(l.complete && l.nodeId == 0);
        runner->typewriterSpeed = 0.0f;                             // from here on lines show at once
        runner->Advance();                                          // then moves on
        CHECK(runner->CurrentLine().nodeId == 2);
        CHECK(runner->CurrentLine().complete);
        runner->Advance();
        l = runner->CurrentLine();
        CHECK(l.choice && l.choices.size() == 3 && l.choices[2] == "Just looking.");
        CHECK(!runner->Choose(7));
        CHECK(runner->Choose(0));                                   // buy the sword
        CHECK(!runner->IsActive());
        CHECK(bag->Count("gold") == 10 && bag->Count("sword") == 1);
        CHECK(StoryState::Get(&scene, false)->HasFlag("bought"));
        CHECK(runner->events.size() == 1 && runner->events[0] == "sword_sold");
        scene.Update(0.016f);
        CHECK(sc->VM()->GetGlobal("last_event").AsString() == "sword_sold");
        CHECK(sc->VM()->GetGlobal("ended").AsFloat() == 1.0f);
        // Second visit, after spending (the story's gold is what the graph tests):
        // the sword option is hidden and the quest option starts the quest.
        StoryState::Get(&scene, false)->Set("gold", vs::VsValue(5.0f));
        CHECK(runner->StartDialogue());
        runner->Advance();
        CHECK(runner->CurrentLine().text == "Come back with some coin.");
        runner->Advance();
        l = runner->CurrentLine();
        CHECK(l.choice && l.choices.size() == 2 && l.choices[0] == "Any work for me?");
        CHECK(runner->Choose(0));
        CHECK(log->IsActive("q1"));
        CHECK(runner->CurrentLine().text == "Bring me three ores.");
        runner->Stop();
        CHECK(!runner->IsActive());
        scene.Update(0.016f);
        CHECK(sc->VM()->GetGlobal("ended").AsFloat() == 2.0f);
        // Number keys choose, Escape cancels.
        CHECK(runner->StartDialogue());
        runner->Advance(); runner->Advance();
        CHECK(runner->CurrentLine().choice);
        Input::FeedKeys({'1'});
        scene.Update(0.016f);
        Input::FeedKeys({});
        CHECK(!runner->IsActive() && runner->CurrentLine().nodeId == -1);
        CHECK(runner->StartDialogue());
        Input::FeedKeys({(char)27});
        scene.Update(0.016f);
        Input::FeedKeys({});
        CHECK(!runner->IsActive());
        // Scripts: start_dialogue by name, dialogue_active, set / get story.
        CHECK(sc->VM()->CallEventArgs("go", {}).AsBool());
        CHECK(sc->VM()->CallEventArgs("active", {}).AsBool());
        CHECK(sc->VM()->CallEventArgs("remember", {}).AsString() == "Ann");
        CHECK(StoryState::Get(&scene, false)->Get("gold").AsFloat() == 7.0f);
        runner->Stop();
    }

    // ---- StoryState and DialogueRunner records through the scene serializer ----
    {
        Scene scene("Save");
        auto* st = StoryState::Get(&scene, true);
        st->Set("gold", vs::VsValue(12.5f));
        st->Set("hero", vs::VsValue(std::string("Ann \"the\" Brave")));
        st->Set("met", vs::VsValue(true));
        st->SetFlag("bought");
        st->SetFlag("door open");
        GameObject* smith = scene.CreateGameObject("Smith");
        auto* dr = smith->AddComponent<DialogueRunner>();
        dr->graphFile = "Assets/smith.okaydialog";
        dr->interactKey = 't'; dr->interactRange = 4.5f; dr->typewriterSpeed = 0.0f; dr->promptText = "Talk";
        dr->pausePlayer = false; dr->startMode = 1; dr->startNode = 3; dr->canCancel = false;
        smith->AddComponent<QuestTrigger>()->zone = "forge";
        const std::string text = SceneSerializer::Serialize(scene);
        CHECK(text.find("storystate") != std::string::npos);
        Scene back("Back");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        CHECK(err.empty());
        auto* st2 = StoryState::Get(&back, false);
        CHECK(st2 != nullptr);
        if (st2) {
            CHECK(st2->Get("gold").IsNumber() && st2->Get("gold").AsFloat() == 12.5f);
            CHECK(st2->Get("hero").IsString() && st2->Get("hero").AsString() == "Ann \"the\" Brave");
            CHECK(st2->Get("met").IsBool() && st2->Get("met").AsBool());
            CHECK(st2->HasFlag("bought") && st2->HasFlag("door open") && !st2->HasFlag("x"));
        }
        auto* dr2 = back.Find("Smith") ? back.Find("Smith")->GetComponent<DialogueRunner>() : nullptr;
        CHECK(dr2 != nullptr);
        if (dr2) {
            CHECK(dr2->graphFile == "Assets/smith.okaydialog" && dr2->interactKey == 't');
            CHECK_NEAR(dr2->interactRange, 4.5f, 1e-5);
            CHECK(dr2->typewriterSpeed == 0.0f && dr2->promptText == "Talk" && !dr2->pausePlayer);
            CHECK(dr2->startMode == 1 && dr2->startNode == 3 && !dr2->canCancel);
        }
        auto* qt2 = back.Find("Smith") ? back.Find("Smith")->GetComponent<QuestTrigger>() : nullptr;
        CHECK(qt2 && qt2->zone == "forge" && qt2->triggerTag == "Player" && qt2->once);
    }

    TEST_MAIN_RESULT();
}
