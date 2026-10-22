#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/VisualScript/NodesWave4.hpp"
#include <cmath>
#include <string>

using namespace okay;

// Visual scripting, fourth wave: several entries per event, key / timer /
// custom events, object variables, save/load, text and math helpers, flow
// nodes, and the debug trace the editor reads.
static VisualScriptComponent* Attach(Scene& s, const char* name, const std::string& text) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) std::printf("load error: %s\n", err.c_str());
    return vc;
}

int main() {
    RUN_SUITE("vs_wave4");

    {   // Two On Update nodes both run (only the last one used to).
        Scene s; s.physicsEnabled = false;
        auto* vc = Attach(s, "A",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 AddVar a\ndefault 2 0 1\nexec 1 0 2\n"
            "node 3 OnUpdate\nentry OnUpdate 3\nnode 4 AddVar b\ndefault 4 0 1\nexec 3 0 4\n"
            "node 5 OnStart\nentry OnStart 5\nnode 6 AddVar s\ndefault 6 0 1\nexec 5 0 6\n"
            "node 7 OnStart\nentry OnStart 7\nnode 8 AddVar s\ndefault 8 0 1\nexec 7 0 8\n");
        s.Start();
        s.Update(0.016f); s.Update(0.016f);
        CHECK(std::fabs(vc->GetVariable("a").AsFloat() - 2.0f) < 1e-4f);
        CHECK(std::fabs(vc->GetVariable("b").AsFloat() - 2.0f) < 1e-4f);
        CHECK(std::fabs(vc->GetVariable("s").AsFloat() - 2.0f) < 1e-4f);
    }

    {   // On Key Pressed / Released and On Timer.
        Scene s; s.physicsEnabled = false;
        auto* vc = Attach(s, "K",
            "node 1 OnKey space\nentry OnKey:space 1\nnode 2 AddVar down\ndefault 2 0 1\nexec 1 0 2\n"
            "node 3 OnKeyUp space\nentry OnKeyUp:space 3\nnode 4 AddVar up\ndefault 4 0 1\nexec 3 0 4\n"
            "node 5 OnTimer 0.5\nentry OnTimer:0.5 5\nnode 6 AddVar ticks\ndefault 6 0 1\nexec 5 0 6\n");
        s.Start();
        Input::FeedKeys({' '}); s.Update(0.1f);
        Input::FeedKeys({' '}); s.Update(0.1f);
        Input::FeedKeys({});    s.Update(0.1f);
        CHECK(std::fabs(vc->GetVariable("down").AsFloat() - 1.0f) < 1e-4f);
        CHECK(std::fabs(vc->GetVariable("up").AsFloat() - 1.0f) < 1e-4f);
        for (int i = 0; i < 7; ++i) s.Update(0.1f);   // 1.0 s total -> 2 ticks
        CHECK(std::fabs(vc->GetVariable("ticks").AsFloat() - 2.0f) < 1e-4f);
    }

    {   // Send / Broadcast Event with a value; Set / Get Object Var.
        Scene s; s.physicsEnabled = false;
        auto* rx = Attach(s, "Receiver",
            "node 1 OnEvent Hit\nentry OnEvent:Hit 1\nnode 2 SetVar got\nnode 3 EventValue\nexec 1 0 2\ndata 2 0 3 0\n"
            "node 4 SetVar from\nnode 5 OtherName\nexec 2 0 4\ndata 4 0 5 0\n");
        auto* rx2 = Attach(s, "Other",
            "node 1 OnEvent Hit\nentry OnEvent:Hit 1\nnode 2 AddVar hits\ndefault 2 0 1\nexec 1 0 2\n");
        auto* tx = Attach(s, "Sender",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 SendEvent Hit\ndefault 2 0 \"Receiver\"\ndefault 2 1 25\nexec 1 0 2\n"
            "node 3 BroadcastEvent Hit\ndefault 3 0 7\nexec 2 0 3\n"
            "node 4 SetObjectVar hp\ndefault 4 0 \"Other\"\ndefault 4 1 99\nexec 3 0 4\n"
            "node 5 SetVar read\nnode 6 GetObjectVar hp\ndefault 6 0 \"Other\"\nexec 4 0 5\ndata 5 0 6 0\n");
        s.Start();
        CHECK(std::fabs(rx->GetVariable("got").AsFloat() - 7.0f) < 1e-4f);   // broadcast came last
        CHECK(rx->GetVariable("from").AsString() == "Sender");
        CHECK(std::fabs(rx2->GetVariable("hits").AsFloat() - 1.0f) < 1e-4f); // only the broadcast reached it
        CHECK(std::fabs(rx2->GetVariable("hp").AsFloat() - 99.0f) < 1e-4f);
        CHECK(std::fabs(tx->GetVariable("read").AsFloat() - 99.0f) < 1e-4f);
    }

    {   // Text, math, tag queries, flow helpers.
        Scene s; s.physicsEnabled = false;
        GameObject* e1 = s.CreateGameObject("E1"); e1->tag = "Enemy"; e1->transform->localPosition = {5, 0, 0};
        GameObject* e2 = s.CreateGameObject("E2"); e2->tag = "Enemy"; e2->transform->localPosition = {1, 0, 0};
        auto* vc = Attach(s, "T",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 SetVar fmt\nnode 3 Format \"Score: {0} / {1}\"\ndefault 3 0 12\ndefault 3 1 \"x\"\nexec 1 0 2\ndata 2 0 3 0\n"
            "node 4 SetVar sub\nnode 5 Substring\ndefault 5 0 \"hello world\"\ndefault 5 1 6\nexec 2 0 4\ndata 4 0 5 0\n"
            "node 6 SetVar rep\nnode 7 Replace\ndefault 7 0 \"a-b-c\"\ndefault 7 1 \"-\"\ndefault 7 2 \"+\"\nexec 4 0 6\ndata 6 0 7 0\n"
            "node 8 SetVar num\nnode 9 ToNumber\ndefault 9 0 \"3.5\"\nexec 6 0 8\ndata 8 0 9 0\n"
            "node 10 SetVar snap\nnode 11 Snap\ndefault 11 0 7.3\ndefault 11 1 2\nexec 8 0 10\ndata 10 0 11 0\n"
            "node 12 SetVar wrap\nnode 13 Wrap\ndefault 13 0 370\ndefault 13 1 0\ndefault 13 2 360\nexec 10 0 12\ndata 12 0 13 0\n"
            "node 14 SetVar near\nnode 15 NearestWithTag Enemy\nexec 12 0 14\ndata 14 0 15 0\n"
            "node 16 SetVar cnt\nnode 17 CountWithTag Enemy\nexec 14 0 16\ndata 16 0 17 0\n"
            "node 18 SetVar trim\nnode 19 Trim\ndefault 19 0 \"  hi  \"\nexec 16 0 18\ndata 18 0 19 0\n");
        s.Start();
        CHECK(vc->GetVariable("fmt").AsString() == "Score: 12 / x");
        CHECK(vc->GetVariable("sub").AsString() == "world");
        CHECK(vc->GetVariable("rep").AsString() == "a+b+c");
        CHECK(std::fabs(vc->GetVariable("num").AsFloat() - 3.5f) < 1e-4f);
        CHECK(std::fabs(vc->GetVariable("snap").AsFloat() - 8.0f) < 1e-4f);
        CHECK(std::fabs(vc->GetVariable("wrap").AsFloat() - 10.0f) < 1e-3f);
        CHECK(vc->GetVariable("near").AsString() == "E2");
        CHECK(std::fabs(vc->GetVariable("cnt").AsFloat() - 2.0f) < 1e-4f);
        CHECK(vc->GetVariable("trim").AsString() == "hi");
        // Noise: smooth, bounded, zero on lattice points.
        CHECK(std::fabs(vs::VsPerlin2(3.0f, 4.0f)) < 1e-5f);
        float n = vs::VsPerlin2(0.37f, 1.81f);
        CHECK(n > -1.01f && n < 1.01f);
        CHECK(std::fabs(vs::VsPerlin2(0.37f, 1.81f) - vs::VsPerlin2(0.371f, 1.81f)) < 0.02f);
    }

    {   // Counter, Every N, and the debug trace.
        vs::NodeGraph::Tracing() = true;
        Scene s; s.physicsEnabled = false;
        auto* vc = Attach(s, "C",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Counter\nexec 1 0 2\n"
            "node 3 EveryN\ndefault 3 0 3\nexec 2 0 3\nnode 4 AddVar third\ndefault 4 0 1\nexec 3 0 4\n"
            "node 5 SetVar count\nexec 4 0 5\nnode 6 Add\ndefault 6 1 0\ndata 6 0 2 0\ndata 5 0 6 0\n");
        s.Start();
        for (int i = 0; i < 9; ++i) s.Update(0.016f);
        CHECK(std::fabs(vc->GetVariable("third").AsFloat() - 3.0f) < 1e-4f);
        CHECK(std::fabs(vc->GetVariable("count").AsFloat() - 9.0f) < 1e-4f);
        vs::NodeGraph* g = vc->Graph();
        int n2 = g->NodeByFileId(2), n6 = g->NodeByFileId(6);
        CHECK(n2 >= 0 && g->Node(n2)->execCount == 9);
        CHECK(n6 >= 0 && !g->Node(n6)->lastOut.empty() && std::fabs(g->Node(n6)->lastOut[0].AsFloat() - 9.0f) < 1e-4f);
        vs::NodeGraph::Tracing() = false;
    }

    TEST_MAIN_RESULT();
}
