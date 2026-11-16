// Issue #888 findings 52, 53 and 65:
//  52  Spawner / NPCController / BehaviorTreeRunner seeds came from (uint32_t)(|x|*73856093 + |z|*19349663),
//      which overflows (undefined behaviour; saturates on x86-64) past about 58 units from the origin, so
//      every spawner out there spawned in the same pattern. Spawners at x = 100, 200, 1000 must differ.
//  53  .okayquest prerequisites lost their spaces (and split on commas inside an id) on save + load.
//  65  PluralText / Localize used the current language's plural rule on text that fell back to the
//      default language, and PluralText skipped the parent language (fr-CA -> fr) that Get uses.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/PositionSeed.hpp"
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {

// Three spawn offsets (relative to the spawner) from a spawner standing at (x, 0, z).
std::vector<Vec3> SpawnPattern(float x, float z) {
    Scene sc("s");
    GameObject* tmpl = sc.CreateGameObject("Blueprint");
    GameObject* host = sc.CreateGameObject("Spawner");
    host->transform->localPosition = {x, 0, z};
    auto* sp = host->AddComponent<Spawner>();
    sp->templateName = "Blueprint";
    sp->autoStart = false;
    sp->maxAlive = 0;
    sp->spawnRadius = 5.0f;
    sc.Update(0.016f);   // Start() picks the seed
    std::vector<Vec3> out;
    for (int i = 0; i < 3; ++i) {
        GameObject* c = sp->SpawnOne();
        if (c) out.push_back(c->transform->Position() - Vec3{x, 0, z});
    }
    (void)tmpl;
    return out;
}

std::string Key(const std::vector<Vec3>& v) {
    std::string s;
    char buf[64];
    for (const Vec3& p : v) { std::snprintf(buf, sizeof(buf), "(%.3f,%.3f)", p.x, p.z); s += buf; }
    return s;
}

void TestSpawnerSeeds() {
    RUN_SUITE("52: spawners far from the origin get different patterns");
    const float xs[] = {0.0f, 0.5f, 1.0f, 10.0f, 50.0f, 58.0f, 59.0f, 100.0f, 200.0f, 1000.0f, -100.0f, 1.0e6f};
    std::set<std::string> seen;
    for (float x : xs) {
        const std::vector<Vec3> p = SpawnPattern(x, 0.0f);
        CHECK(p.size() == 3);
        const std::string k = Key(p);
        std::printf("  spawner x=%g: %s\n", x, k.c_str());
        CHECK(seen.insert(k).second);   // no two positions share a pattern
    }
    // Far along z and in both axes too.
    CHECK(Key(SpawnPattern(0.0f, 300.0f)) != Key(SpawnPattern(0.0f, 400.0f)));
    CHECK(Key(SpawnPattern(300.0f, 300.0f)) != Key(SpawnPattern(400.0f, 400.0f)));
    // The same place gives the same pattern (the seed is still deterministic).
    CHECK(Key(SpawnPattern(100.0f, 7.0f)) == Key(SpawnPattern(100.0f, 7.0f)));

    // The shared helper: defined for any float, -0 == +0, salts and nearby points differ.
    CHECK(PositionSeed(0.0f, 0.0f, 7u) == PositionSeed(-0.0f, -0.0f, 7u));
    CHECK(PositionSeed(1.0f, 2.0f, 7u) != PositionSeed(1.0f, 2.0f, 1u));
    CHECK(PositionSeed(1.0f, 2.0f, 7u) != PositionSeed(2.0f, 1.0f, 7u));
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    (void)PositionSeed(inf, -inf, 7u);
    (void)PositionSeed(nan, 1.0e30f, 7u);
    std::set<std::uint32_t> seeds;
    int n = 0;
    for (int x = -500; x <= 500; x += 10)
        for (int z = -500; z <= 500; z += 10) { seeds.insert(PositionSeed((float)x, (float)z, 7u)); ++n; }
    std::printf("  %d grid points -> %zu different seeds\n", n, seeds.size());
    CHECK((int)seeds.size() == n);
}

QuestDef Quest(const std::string& id, std::vector<std::string> pre = {}) {
    QuestDef q; q.id = id; q.title = id; q.prerequisites = std::move(pre);
    return q;
}

void TestQuestPrereqs() {
    RUN_SUITE("53: quest prerequisites with spaces and commas survive save + load");
    std::vector<QuestDef> qs;
    qs.push_back(Quest("Find the sword"));
    qs.push_back(Quest("a,b"));
    qs.push_back(Quest(" padded "));
    qs.push_back(Quest("say \"hi\""));
    qs.push_back(Quest("back\\slash"));
    qs.push_back(Quest("Slay dragon", {"Find the sword", "a,b", " padded ", "say \"hi\"", "back\\slash", "plain"}));
    const std::string text = QuestFile::Write(qs);
    std::vector<QuestDef> back;
    std::string err;
    CHECK(QuestFile::Parse(text, back, &err));
    CHECK(back.size() == qs.size());
    if (back.size() == qs.size()) {
        const auto& pre = back.back().prerequisites;
        std::printf("  prereqs after load:");
        for (const auto& p : pre) std::printf(" [%s]", p.c_str());
        std::printf("\n");
        CHECK(pre == qs.back().prerequisites);
        for (std::size_t i = 0; i < qs.size(); ++i) CHECK(back[i].id == qs[i].id);
    }
    CHECK(QuestFile::Write(back) == text);   // write -> parse -> write is stable

    // Old files: a plain comma list (with or without spaces around the commas) still loads.
    {
        std::vector<QuestDef> old;
        CHECK(QuestFile::Parse("okayquest 1\nquest id=\"c\" pre=\"a,b\"\nquest id=\"d\" pre=\"a , b ,c\"\n", old, &err));
        CHECK(old.size() == 2);
        if (old.size() == 2) {
            CHECK((old[0].prerequisites == std::vector<std::string>{"a", "b"}));
            CHECK((old[1].prerequisites == std::vector<std::string>{"a", "b", "c"}));
        }
    }

    // In play: completing "Find the sword" unlocks "Slay dragon" after the reload.
    {
        Scene sc("q");
        auto* log = sc.CreateGameObject("Quests")->AddComponent<QuestLog>();
        log->quests = back;
        log->quests.push_back(Quest("plain"));
        for (const char* id : {"Find the sword", "a,b", " padded ", "say \"hi\"", "back\\slash", "plain"}) {
            CHECK(log->StartQuest(id));
            if (!log->IsCompleted(id)) CHECK(log->CompleteQuest(id));   // a quest with no objectives completes at once
            CHECK(log->IsCompleted(id));
        }
        std::string why;
        const bool ok = log->CanStart("Slay dragon", &why);
        std::printf("  CanStart(Slay dragon)=%d %s\n", ok ? 1 : 0, why.c_str());
        CHECK(ok);
    }
}

StringTable PluralTable() {
    StringTable t;
    t.AddLanguage("en", "English");
    t.AddLanguage("fr", "Français");
    t.AddLanguage("fr-CA", "Français (Canada)");
    t.defaultLanguage = "en";
    t.Set("coins.one", "en", "{count} coin");
    t.Set("coins.other", "en", "{count} coins");
    t.Set("gems.one", "en", "{count} gem");
    t.Set("gems.other", "en", "{count} gems");
    t.Set("gems.one", "fr", "{count} gemme");
    t.Set("gems.other", "fr", "{count} gemmes");
    t.Set("shop", "en", "{0, plural, one {# coin} other {# coins}}");
    t.Set("hello", "en", "Hello");
    t.Set("hello", "fr", "Bonjour");
    return t;
}

void TestPluralFallback() {
    RUN_SUITE("65: plural fallback uses the source language's rule and the parent language");
    Localization::SetTable(PluralTable());
    CHECK(Localization::SetLanguage("fr"));
    // (a) English text shown to a French player follows the English rule (0 and 1.5 are plural in English).
    const std::string c0 = Localization::PluralText("coins", 0);
    const std::string c15 = Localization::PluralText("coins", 1.5);
    const std::string c1 = Localization::PluralText("coins", 1);
    const std::string s0 = Localization::Localize("shop", 0);
    std::printf("  fr PluralText(coins,0)=\"%s\" (coins,1.5)=\"%s\" (coins,1)=\"%s\" Localize(shop,0)=\"%s\"\n",
                c0.c_str(), c15.c_str(), c1.c_str(), s0.c_str());
    CHECK(c0 == "0 coins");
    CHECK(c15 == "1.5 coins");
    CHECK(c1 == "1 coin");
    CHECK(s0 == "0 coins");
    // French text keeps the French rule (0 is singular in French).
    CHECK(Localization::PluralText("gems", 0) == "0 gemme");
    CHECK(Localization::PluralText("gems", 2) == "2 gemmes");

    // (b) fr-CA has no plural text of its own: the fr parent is used, as Get does.
    CHECK(Localization::SetLanguage("fr-CA"));
    CHECK(Localization::Get("hello") == "Bonjour");
    const std::string g0 = Localization::PluralText("gems", 0);
    const std::string g5 = Localization::PluralText("gems", 5);
    std::printf("  fr-CA PluralText(gems,0)=\"%s\" (gems,5)=\"%s\"\n", g0.c_str(), g5.c_str());
    CHECK(g0 == "0 gemme");
    CHECK(g5 == "5 gemmes");
    CHECK(Localization::PluralText("coins", 0) == "0 coins");   // past fr to en: the English rule

    // The current language's own text is unchanged.
    CHECK(Localization::SetLanguage("en"));
    CHECK(Localization::PluralText("coins", 1) == "1 coin");
    CHECK(Localization::PluralText("gems", 0) == "0 gems");
    Localization::SetTable(StringTable());
}

} // namespace

int main() {
    TestSpawnerSeeds();
    TestQuestPrereqs();
    TestPluralFallback();
    TEST_MAIN_RESULT();
}
