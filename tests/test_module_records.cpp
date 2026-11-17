#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshLink.hpp"
#include "okay/Components/NavMeshModifier.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Components/Ragdoll.hpp"
#include <sstream>

using namespace okay;

static std::vector<std::string> Lines(const std::string& t) {
    std::vector<std::string> v; std::istringstream in(t); std::string l;
    while (std::getline(in, l)) v.push_back(l);
    return v;
}
static bool HasPrefix(const std::vector<std::string>& v, const char* p) {
    for (auto& l : v) if (l.compare(0, std::string(p).size(), p) == 0) return true;
    return false;
}

int main() {
    RUN_SUITE("module_records");
    auto& reg = ModuleRegistry::Get();
    const bool skyOn = reg.IsEnabled("sky_weather"), ragOn = reg.IsEnabled("ragdoll"), navOn = reg.IsEnabled("ai_navigation");

    if (skyOn && ragOn && navOn) {
        // ---- records written by the modules read back to the same values, and writing again gives the same text
        Scene s("T");
        GameObject* a = s.CreateGameObject("Sky");
        auto* d = a->AddComponent<DayNightCycle>(); d->dayLengthSeconds = 321.0f; d->paused = true; d->nightIntensity = 0.07f;
        GameObject* b = s.CreateGameObject("Body");
        auto* r = b->AddComponent<Ragdoll>(); r->gravity = -3.5f; r->iterations = 9; r->ragdollOnDeath = false;
        auto* na = b->AddComponent<NavMeshAgent>(); na->speed = 7.5f; na->areaMask = 5u;
        auto* nm = b->AddComponent<NavMeshModifier>(); nm->area = 2;
        auto* nl = b->AddComponent<NavMeshLink>(); nl->cost = 4.0f; nl->bidirectional = false;
        auto* no = b->AddComponent<NavMeshObstacle>(); no->carve = true; no->timeToStationary = 0.9f;
        auto* ns = b->AddComponent<NavMeshSurface>(); ns->settings.agentRadius = 0.77f; ns->autoBounds = false;
        const std::string text = SceneSerializer::Serialize(s);
        auto ls = Lines(text);
        CHECK(HasPrefix(ls, "  daynight 321 ") && HasPrefix(ls, "  ragdoll grav=-3.5 ") && HasPrefix(ls, "  navagent radius="));
        CHECK(HasPrefix(ls, "  navmod area=2 kids=") && HasPrefix(ls, "  navlink type=") && HasPrefix(ls, "  navobstacle sx="));
        CHECK(HasPrefix(ls, "  navsurface radius=0.77 "));
        Scene back("B");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        auto* d2 = back.Find("Sky") ? back.Find("Sky")->GetComponent<DayNightCycle>() : nullptr;
        GameObject* b2 = back.Find("Body");
        CHECK(d2 && d2->dayLengthSeconds == 321.0f && d2->paused && d2->nightIntensity == 0.07f);
        CHECK(b2 && b2->GetComponent<Ragdoll>() && b2->GetComponent<Ragdoll>()->gravity == -3.5f && b2->GetComponent<Ragdoll>()->iterations == 9 && !b2->GetComponent<Ragdoll>()->ragdollOnDeath);
        CHECK(b2 && b2->GetComponent<NavMeshAgent>() && b2->GetComponent<NavMeshAgent>()->speed == 7.5f && b2->GetComponent<NavMeshAgent>()->areaMask == 5u);
        CHECK(b2 && b2->GetComponent<NavMeshModifier>() && b2->GetComponent<NavMeshModifier>()->area == 2);
        CHECK(b2 && b2->GetComponent<NavMeshLink>() && b2->GetComponent<NavMeshLink>()->cost == 4.0f && !b2->GetComponent<NavMeshLink>()->bidirectional);
        CHECK(b2 && b2->GetComponent<NavMeshObstacle>() && b2->GetComponent<NavMeshObstacle>()->carve);
        CHECK(b2 && b2->GetComponent<NavMeshSurface>() && b2->GetComponent<NavMeshSurface>()->settings.agentRadius == 0.77f && !b2->GetComponent<NavMeshSurface>()->autoBounds);
        CHECK(SceneSerializer::Serialize(back) == text);
    } else {
        // ---- a build without those modules keeps their records as they are and says why
        Scene s("T");
        s.CreateGameObject("Body");
        std::string text = SceneSerializer::Serialize(s);
        const size_t e = text.find("\nend");
        CHECK(e != std::string::npos);
        text.insert(e + 1, "  ragdoll grav=-3.5 iters=9\n");
        Scene back("B");
        std::string err;
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(back, text, &err, &warnings));
        CHECK(back.Find("Body") && back.Find("Body")->GetComponent<Ragdoll>() == nullptr);
        bool said = false;
        for (auto& w : warnings) if (w.find("ragdoll") != std::string::npos && w.find("is off in this build") != std::string::npos) said = true;
        CHECK(said);
        CHECK(SceneSerializer::Serialize(back).find("  ragdoll grav=-3.5 iters=9") != std::string::npos);   // not lost
    }
    TEST_MAIN_RESULT();
}
