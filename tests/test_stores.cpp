// Store builds of a game (Steam and the Epic Games Store): the platform-services facade routing to fake Steam,
// fake Epic and no store; the game.okayconfig store lines; the Epic launcher's arguments (and that the login code
// never reaches a log); the Steam accessor falling back to the simulation when the real client cannot start; the
// Epic simulation; and the upload files Build writes (VDF text, the BuildPatchTool script, steam_appid.txt only in
// development builds, the SDK DLL only when its folder is set, and no secret anywhere in them).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Platform/Epic/Epic.hpp"
#include "okay/Platform/PlatformServices.hpp"
#include "okay/Platform/StoreExport.hpp"
#include "okay/Platform/Steam/SteamWebApi.hpp"
#include "okay/Core/DataPack.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::string Slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

// A fake Steam that records what reached it.
struct FakeSteam : ISteamService {
    std::set<std::string> unlocked;
    std::map<std::string, int> statsInt;
    std::map<std::string, std::string> cloud, presence;
    std::map<std::string, int> boards;
    int stores = 0;
    std::string overlay;
    bool Initialize(const SteamConfig&) override { return true; }
    void Shutdown() override {}
    void RunCallbacks() override {}
    bool IsAvailable() const override { return true; }
    const char* BackendName() const override { return "fake-steam"; }
    std::string UserName() const override { return "SteamPlayer"; }
    std::uint64_t UserId() const override { return 7; }
    bool UnlockAchievement(const std::string& id) override { unlocked.insert(id); return true; }
    bool IsAchievementUnlocked(const std::string& id) const override { return unlocked.count(id) != 0; }
    bool ClearAchievement(const std::string& id) override { unlocked.erase(id); return true; }
    void SetStat(const std::string&, float) override {}
    float GetStat(const std::string&) const override { return 0; }
    void SetStatInt(const std::string& n, int v) override { statsInt[n] = v; }
    int GetStatInt(const std::string& n) const override { auto it = statsInt.find(n); return it == statsInt.end() ? 0 : it->second; }
    bool StoreStats() override { ++stores; return true; }
    bool UploadLeaderboardScore(const std::string& b, std::int32_t s) override { boards[b] = s; return true; }
    bool CloudWrite(const std::string& f, const std::string& d) override { cloud[f] = d; return true; }
    std::string CloudRead(const std::string& f) const override { auto it = cloud.find(f); return it == cloud.end() ? "" : it->second; }
    bool CloudHasFile(const std::string& f) const override { return cloud.count(f) != 0; }
    bool CloudDelete(const std::string& f) override { return cloud.erase(f) != 0; }
    void ActivateOverlay(const std::string& p) override { overlay = p; }
    void SetRichPresence(const std::string& k, const std::string& v) override { presence[k] = v; }
};

// A fake Epic service.
struct FakeEpic : IEpicService {
    std::set<std::string> unlocked;
    std::map<std::string, std::int32_t> stats;
    std::vector<std::pair<std::string, std::int32_t>> ingested;
    std::map<std::string, std::string> files;
    std::string presence;
    int ticks = 0;
    bool friends = false;
    bool Initialize(const EpicConfig&) override { return true; }
    void Shutdown() override {}
    void Tick() override { ++ticks; }
    bool IsAvailable() const override { return true; }
    const char* BackendName() const override { return "fake-epic"; }
    bool Login() override { return true; }
    EpicLoginState LoginState() const override { return EpicLoginState::LoggedIn; }
    std::string DisplayName() const override { return "EpicPlayer"; }
    std::string AccountId() const override { return "abc"; }
    bool UnlockAchievement(const std::string& id) override { unlocked.insert(id); return true; }
    bool IsAchievementUnlocked(const std::string& id) const override { return unlocked.count(id) != 0; }
    bool IngestStat(const std::string& n, std::int32_t a) override { ingested.push_back({n, a}); stats[n] += a; return true; }
    std::int32_t GetStat(const std::string& n) const override { auto it = stats.find(n); return it == stats.end() ? 0 : it->second; }
    bool WriteFile(const std::string& n, const std::string& d) override { files[n] = d; return true; }
    std::string ReadFile(const std::string& n) const override { auto it = files.find(n); return it == files.end() ? "" : it->second; }
    bool HasFile(const std::string& n) const override { return files.count(n) != 0; }
    bool DeleteFile(const std::string& n) override { return files.erase(n) != 0; }
    void SetPresence(const std::string& t) override { presence = t; }
    std::string Presence() const override { return presence; }
    void ShowFriends() override { friends = true; }
};

// A "real" Steam backend whose client is not running: Initialize fails.
struct DeadSteam : FakeSteam {
    bool Initialize(const SteamConfig&) override { return false; }
    bool IsAvailable() const override { return false; }
    const char* BackendName() const override { return "steamworks"; }
};
struct DeadEpic : FakeEpic {
    bool Initialize(const EpicConfig&) override { return false; }
    bool IsAvailable() const override { return false; }
    const char* BackendName() const override { return "eos"; }
};

// The same calls a game script makes, against any store.
void PlayRound(IStoreServices& s) {
    s.UnlockAchievement("first_win");
    s.SetStat("kills", 3);
    s.AddStat("kills", 2);
    s.SubmitScore("high", 900);
    s.CloudWrite("save1", "level=4");
    s.SetPresence("status", "In the Arena");
    s.AchievementProgress("collector", 10, 10);
    s.StoreStats();
}

}  // namespace


// The shipped example examples/store_kit/store_kit.okayscene is made by this function: run test_stores with
// OKAY_WRITE_STORE_KIT_EXAMPLE=<path> to write it again. Without it the test reads the committed file.
const char* kStoreKitScript =
    "// Store Kit demo: the same calls work on Steam, the Epic Games Store and with no store.\n"
    "// With no store (this scene) everything is kept in the game's save folder, so it works without any account.\n"
    "// Keys: 1 unlock an achievement, 2 add a coin, 3 submit a score, 4 save to the cloud, 5 read the save back.\n"
    "var msg = \"\"\n"
    "function start() {\n"
    "  Achievements.Unlock(\"first_launch\")\n"
    "  Stats.Add(\"launches\", 1)\n"
    "  Stats.Store()\n"
    "  msg = \"Achievement first_launch unlocked. Press 1 to 5.\"\n"
    "}\n"
    "function update(dt) {\n"
    "  if (key_down(\"1\")) { Achievements.Unlock(\"pressed_one\"); msg = \"Achievement pressed_one unlocked\" }\n"
    "  if (key_down(\"2\")) { Stats.Add(\"coins\", 1); Stats.Store(); msg = \"Coins: \" + Stats.Get(\"coins\") }\n"
    "  if (key_down(\"3\")) { Leaderboards.Submit(\"high\", Stats.Get(\"coins\") * 100); msg = \"Score sent to the high board\" }\n"
    "  if (key_down(\"4\")) { CloudSave.Write(\"slot1\", \"coins=\" + Stats.Get(\"coins\")); msg = \"Saved slot1\" }\n"
    "  if (key_down(\"5\")) { msg = \"slot1 holds: \" + CloudSave.Read(\"slot1\") }\n"
    "  set_text(\"Store: \" + Store.Name() + \"   player: \" + Store.UserName() + \"   launches: \" + Stats.Get(\"launches\") + \"   coins: \" + Stats.Get(\"coins\") + \"\\n\" + msg)\n"
    "}\n";
const char* kStoreKitGraph =
    "node 1 OnStart\n"
    "entry OnStart 1\n"
    "node 2 SteamUnlock visual_script_achievement\n"
    "exec 1 0 2\n";

void BuildStoreKitExample(Scene& s) {
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->AddComponent<Camera>();
    GameObject* ui = s.CreateGameObject("Store Kit Demo");
    auto* t = ui->AddComponent<TextRenderer>();
    t->text = "Store Kit demo";
    t->screenSpace = true;
    t->screenPos = Vec2{24.0f, 24.0f};
    auto* sc = ui->AddComponent<ScriptComponent>("okayscript");
    sc->LoadSource(kStoreKitScript);
    GameObject* vs = s.CreateGameObject("Visual Script Achievement");
    auto* v = vs->AddComponent<VisualScriptComponent>();
    std::string err;
    v->LoadFromText(kStoreKitGraph, &err);
}

int main() {
    {
        RUN_SUITE("store names and config lines");
        CHECK(ParseStoreKind("Steam") == StoreKind::Steam);
        CHECK(ParseStoreKind("EPIC") == StoreKind::Epic);
        CHECK(ParseStoreKind("none") == StoreKind::None);
        CHECK(ParseStoreKind("itch") == StoreKind::None);
        CHECK(std::string(StoreKindName(StoreKind::Epic)) == "epic");

        StoreSettings none;
        CHECK(WriteStoreConfig(none).empty());   // a game for no store writes no new line

        StoreSettings s; s.store = StoreKind::Steam; s.steamAppId = 2468; s.steamRelaunch = true;
        s.epicProductId = "prod"; s.epicClientId = "client";
        const std::string text = WriteStoreConfig(s);
        CHECK(text.find("store=steam\n") != std::string::npos);
        CHECK(text.find("steam_app_id=2468\n") != std::string::npos);
        StoreSettings back;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            const auto eq = line.find('=');
            CHECK(eq != std::string::npos);
            CHECK(ApplyStoreKey(back, line.substr(0, eq), line.substr(eq + 1)));
        }
        CHECK(back.store == StoreKind::Steam && back.steamAppId == 2468 && back.steamRelaunch);
        CHECK(back.epicProductId == "prod" && back.epicClientId == "client");
        CHECK(!ApplyStoreKey(back, "width", "10"));
        CHECK(ApplyStoreKey(back, "steam_app_id", "-5") && back.steamAppId == 0);   // nonsense reads as "not set"
        CHECK(ApplyStoreKey(back, "epic_client_secret", "x") == false);             // there is no such key
    }
    {
        RUN_SUITE("store choice: OKAY_STORE wins over the config");
        CHECK(PlatformServices::Choose(StoreKind::Steam, nullptr) == StoreKind::Steam);
        CHECK(PlatformServices::Choose(StoreKind::Steam, "") == StoreKind::Steam);
        CHECK(PlatformServices::Choose(StoreKind::Steam, "epic") == StoreKind::Epic);
        CHECK(PlatformServices::Choose(StoreKind::Epic, "none") == StoreKind::None);
        CHECK(PlatformServices::Choose(StoreKind::None, "bogus") == StoreKind::None);
    }
    {
        RUN_SUITE("facade routes to Steam");
        FakeSteam steam;
        auto svc = MakeSteamStoreServices(steam);
        CHECK(svc->Kind() == StoreKind::Steam && svc->IsAvailable() && svc->UserName() == "SteamPlayer");
        PlayRound(*svc);
        CHECK(steam.unlocked.count("first_win") && steam.unlocked.count("collector"));
        CHECK(steam.statsInt["kills"] == 5 && svc->GetStat("kills") == 5);
        CHECK(steam.boards["high"] == 900);
        CHECK(steam.cloud["save1"] == "level=4" && svc->CloudRead("save1") == "level=4" && svc->CloudHas("save1"));
        CHECK(steam.presence["status"] == "In the Arena");
        CHECK(steam.stores >= 1);
        svc->ShowOverlay("achievements");
        CHECK(steam.overlay == "achievements");
        CHECK(svc->CloudDelete("save1") && !svc->CloudHas("save1"));
    }
    {
        RUN_SUITE("facade routes to Epic");
        FakeEpic epic;
        auto svc = MakeEpicStoreServices(epic);
        CHECK(svc->Kind() == StoreKind::Epic && svc->IsAvailable() && svc->UserName() == "EpicPlayer");
        PlayRound(*svc);
        CHECK(epic.unlocked.count("first_win") && epic.unlocked.count("collector"));
        // Epic stats are sent as amounts: Set sends the value (for latest / largest / smallest stats), Add sends the change (sum stats).
        CHECK(svc->GetStat("kills") == 5);
        std::vector<std::int32_t> kills;
        for (const auto& in : epic.ingested) if (in.first == "kills") kills.push_back(in.second);
        CHECK(kills.size() == 2 && kills[0] == 3 && kills[1] == 2);
        CHECK(epic.stats["high"] == 900);   // an Epic leaderboard is a stat: the score goes to it
        CHECK(epic.files["save1"] == "level=4" && svc->CloudRead("save1") == "level=4");
        CHECK(epic.presence.find("In the Arena") != std::string::npos);
        svc->Tick();
        CHECK(epic.ticks == 1);
        svc->ShowOverlay("friends");
        CHECK(epic.friends);
        CHECK(!svc->SubmitScore("", 5));   // no board name, nothing sent
    }
    {
        RUN_SUITE("facade with no store keeps everything in memory");
        auto svc = MakeNoStoreServices();
        CHECK(svc->Kind() == StoreKind::None && !svc->IsAvailable());
        PlayRound(*svc);
        CHECK(svc->IsAchievementUnlocked("first_win") && svc->IsAchievementUnlocked("collector"));
        CHECK(!svc->IsAchievementUnlocked("never"));
        CHECK(svc->GetStat("kills") == 5);
        CHECK(svc->CloudRead("save1") == "level=4");
        svc->AchievementProgress("half", 5, 10);
        CHECK(!svc->IsAchievementUnlocked("half"));
    }
    {
        RUN_SUITE("PlatformServices: configure, set, route, shut down");
        PlatformServices::Shutdown();
        auto owned = std::make_unique<FakeEpic>();
        FakeEpic* epic = owned.get();
        static std::unique_ptr<FakeEpic> keep; keep = std::move(owned);
        PlatformServices::Set(MakeEpicStoreServices(*epic));
        CHECK(PlatformServices::Active() == StoreKind::Epic);
        PlatformServices::Get().UnlockAchievement("boss");
        CHECK(epic->unlocked.count("boss"));
        PlatformServices::Tick();
        CHECK(epic->ticks == 1);
        PlatformServices::Shutdown();
        // With nothing configured and no OKAY_STORE, the facade is the no-store one.
        StoreSettings none; PlatformServices::Configure(none);
        CHECK(PlatformServices::Get().Kind() == StoreKind::None);
        PlatformServices::Shutdown();
    }
    {
        RUN_SUITE("Steam accessor falls back to the simulation when the client cannot start");
        Steam::Shutdown();
        Steam::SetFactory([] { return std::unique_ptr<ISteamService>(new DeadSteam()); });
        CHECK(std::string(Steam::Get().BackendName()) == "null");
        CHECK(Steam::Get().UnlockAchievement("works_offline"));
        CHECK(Steam::Get().IsAchievementUnlocked("works_offline"));
        Steam::Shutdown();
        Steam::SetFactory(nullptr);
        Steam::SetAppId(2468);
        CHECK(Steam::AppId() == 2468);
        Steam::SetAppId(480);
    }
    {
        RUN_SUITE("Epic accessor: simulation by default, fallback when the platform cannot start");
        Epic::Shutdown();
        Epic::SetFactory([] { return std::unique_ptr<IEpicService>(new DeadEpic()); });
        CHECK(std::string(Epic::Get().BackendName()) == "null");
        Epic::Shutdown();
        Epic::SetFactory(nullptr);
        EpicConfig cfg; cfg.launch.userName = "Robin"; cfg.launch.exchangeCode = "code123"; cfg.launch.fromLauncher = true;
        Epic::Configure(cfg);
        IEpicService& e = Epic::Get();
        CHECK(std::string(e.BackendName()) == "null" && !e.IsAvailable());
        CHECK(e.LoginState() == EpicLoginState::LoggedOut);
        CHECK(e.Login());
        CHECK(e.LoginState() == EpicLoginState::LoggedIn && e.DisplayName() == "Robin");
        CHECK(e.UnlockAchievement("a1") && e.IsAchievementUnlocked("a1"));
        CHECK(e.IngestStat("coins", 4) && e.IngestStat("coins", 6) && e.GetStat("coins") == 10);
        CHECK(e.WriteFile("slot", "x=1") && e.HasFile("slot") && e.ReadFile("slot") == "x=1");
        CHECK(e.DeleteFile("slot") && !e.HasFile("slot"));
        e.SetPresence("Exploring");
        CHECK(e.Presence() == "Exploring");
        Epic::Shutdown();
        // Without launcher arguments there is nothing to log in with.
        Epic::Configure(EpicConfig{});
        CHECK(!Epic::Get().Login() && Epic::Get().LoginState() == EpicLoginState::LoggedOut);
        Epic::Shutdown();
    }
    {
        RUN_SUITE("Epic launcher arguments");
        const std::vector<std::string> args = {"Game.exe", "-AUTH_LOGIN=unused", "-AUTH_PASSWORD=8f3c0d", "-AUTH_TYPE=exchangecode",
                                               "-epicapp=Sunflower", "-epicenv=Prod", "-EpicPortal", "-epicusername=Robin Hood",
                                               "-epicuserid=0123abcd", "-epiclocale=en-US", "-epicsandboxid=sbx9", "--windowed"};
        const EpicLaunchArgs a = ParseEpicLaunchArgs(args);
        CHECK(a.fromLauncher && a.exchangeCode == "8f3c0d" && a.authType == "exchangecode");
        CHECK(a.appId == "Sunflower" && a.environment == "Prod" && a.userName == "Robin Hood");
        CHECK(a.userId == "0123abcd" && a.locale == "en-US" && a.sandboxId == "sbx9");
        const EpicLaunchArgs b = ParseEpicLaunchArgs({"Game.exe", "-auth_password", "zz9", "-auth_type", "exchangecode"});
        CHECK(b.exchangeCode == "zz9" && b.fromLauncher);
        const EpicLaunchArgs c = ParseEpicLaunchArgs({"Game.exe", "--windowed"});
        CHECK(!c.fromLauncher && c.exchangeCode.empty());
        std::string joined;
        for (const std::string& s : RedactEpicArgs(args)) joined += s + " ";
        CHECK(joined.find("8f3c0d") == std::string::npos);
        CHECK(joined.find("-AUTH_PASSWORD=***") != std::string::npos);
        CHECK(joined.find("-epicapp=Sunflower") != std::string::npos);
        std::string joined2;
        for (const std::string& s : RedactEpicArgs({"x", "-AUTH_PASSWORD", "zz9"})) joined2 += s + " ";
        CHECK(joined2.find("zz9") == std::string::npos);
    }

    {
        RUN_SUITE("OkayScript: Achievements.Unlock and friends reach the active store");
        static FakeEpic scriptEpic;
        PlatformServices::Shutdown();
        PlatformServices::Set(MakeEpicStoreServices(scriptEpic));
        Scene s("Store"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Logic");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            "var store = \"\";\n"
            "var had = 0;\n"
            "var top = 0;\n"
            "var owns = 0;\n"
            "var who = \"\";\n"
            "function start() {\n"
            "  Achievements.Unlock(\"first_win\");\n"
            "  had = Achievements.IsUnlocked(\"first_win\");\n"
            "  Achievements.Progress(\"collector\", 3, 3);\n"
            "  Stats.Set(\"level\", 4);\n"
            "  Stats.Add(\"coins\", 10);\n"
            "  Stats.Store();\n"
            "  Leaderboards.Submit(\"high\", 1200);\n"
            "  top = Leaderboards.Top(\"high\", 5);\n"
            "  owns = Store.OwnsGame();\n"
            "  who = Store.UserName();\n"
            "  CloudSave.Write(\"slot1\", \"hp=3\");\n"
            "  Presence.Set(\"status\", \"Boss fight\");\n"
            "  store = Store.Name();\n"
            "}\n");
        s.Start();
        s.Update(0.016f);
        CHECK(scriptEpic.unlocked.count("first_win") && scriptEpic.unlocked.count("collector"));
        CHECK(scriptEpic.stats["coins"] == 10 && scriptEpic.stats["level"] == 4);
        CHECK(scriptEpic.files["slot1"] == "hp=3");
        CHECK(scriptEpic.presence.find("Boss fight") != std::string::npos);
        CHECK(sc->VM()->GetGlobal("store").AsString() == "epic");
        CHECK(sc->VM()->GetGlobal("had").AsFloat() == 1.0f);
        CHECK(sc->VM()->GetGlobal("owns").AsFloat() == 1.0f);
        CHECK(sc->VM()->GetGlobal("who").AsString() == "EpicPlayer");
        PlatformServices::Shutdown();
    }

    {
        RUN_SUITE("local store (no store): saved between runs in a folder");
        const fs::path dir = fs::temp_directory_path() / "okay_store_local_test";
        std::error_code e2; fs::remove_all(dir, e2);
        {
            auto a = MakeNoStoreServices(dir.string());
            PlayRound(*a);
            a->SubmitScore("high", 400);   // lower than 900: the best stays
            a->CloudWrite("../escape", "x");   // a name cannot leave the folder
            a->Shutdown();
        }
        auto b = MakeNoStoreServices(dir.string());
        CHECK(b->IsAchievementUnlocked("first_win") && b->IsAchievementUnlocked("collector"));
        CHECK(b->GetStat("kills") == 5);
        CHECK(b->CloudRead("save1") == "level=4");
        CHECK(b->OwnsGame());
        const auto top = b->TopScores("high", 10);
        CHECK(top.size() == 1 && top[0].score == 900 && top[0].rank == 1);
        CHECK(!fs::exists(dir.parent_path() / "escape"));
        CHECK(b->CloudRead("../escape") == "x");   // still readable by its own name
        fs::remove_all(dir, e2);
    }
#if defined(FAKE_STEAM_LIB)
    {
        RUN_SUITE("Store Kit: the developer's Steam library loaded at run time (a stand-in library)");
        CHECK(CreateDynamicSteamService((fs::temp_directory_path() / "no_such_steam_api.so").string()) == nullptr);
        auto svc = CreateDynamicSteamService(FAKE_STEAM_LIB);
        CHECK(svc != nullptr);
        if (svc) {
            SteamConfig cfg; cfg.appId = 2468;
            CHECK(svc->Initialize(cfg));
            CHECK(svc->IsAvailable() && std::string(svc->BackendName()) == "steam-runtime");
            CHECK(svc->UserName() == "FakeSteamUser" && svc->UserId() == 76561197960287930ull);
            CHECK(svc->UnlockAchievement("ACH_WIN") && svc->IsAchievementUnlocked("ACH_WIN") && !svc->IsAchievementUnlocked("ACH_X"));
            svc->SetStatInt("kills", 7);
            CHECK(svc->GetStatInt("kills") == 7);
            CHECK(svc->CloudWrite("s.sav", std::string("a\0b", 3)) && svc->CloudRead("s.sav") == std::string("a\0b", 3));
            CHECK(svc->CloudDelete("s.sav") && !svc->CloudHasFile("s.sav"));
            CHECK(svc->OwnsApp(2468) && !svc->OwnsApp(1));
            CHECK(svc->Language() == "german");
            // Through the facade, the way a game uses it.
            auto store = MakeSteamStoreServices(*svc);
            store->ShowOverlay("achievements");
            store->SetPresence("status", "Racing");
            store->Tick();
            store->Tick();
            svc->Shutdown();
            CHECK(!svc->IsAvailable());
        }
        // The restart check: the library says "relaunch" for app 999, and the accessor reports it.
        Steam::Shutdown();
        Steam::SetAppId(999); Steam::SetRelaunchCheck(true);
        Steam::EnableRuntimeLibrary(true, FAKE_STEAM_LIB);
        CHECK(std::string(Steam::Get().BackendName()) == "null");   // the simulation keeps the game alive ...
        CHECK(Steam::RelaunchRequested());                          // ... and the player is told to quit
        Steam::Shutdown();
        Steam::SetAppId(2468); Steam::SetRelaunchCheck(false);
        CHECK(std::string(Steam::Get().BackendName()) == "steam-runtime");
        Steam::Shutdown();
        Steam::EnableRuntimeLibrary(false);
        Steam::SetAppId(480);
        CHECK(std::string(Steam::Get().BackendName()) == "null");   // off by default
        Steam::Shutdown();
    }
#endif

    {
        RUN_SUITE("Steam Web API check: schema, ids used in scripts, key only in a header");
        namespace sw = okay::steamweb;
        const std::string body = R"({"game":{"gameName":"Moon Hop","gameVersion":"3","availableGameStats":{
            "stats":[{"name":"kills","defaultvalue":0,"displayName":"Kills"},{"name":"coins","defaultvalue":0,"displayName":""}],
            "achievements":[{"name":"ACH_WIN","defaultvalue":0,"displayName":"Winner \"one\"","hidden":0,"description":"a, b","icon":"x","icongray":"y"},
                            {"name":"ACH_COLLECT","defaultvalue":0,"displayName":"C","hidden":1,"description":"","icon":"","icongray":""}]}}})";
        const sw::Schema sc = sw::ParseSchema(body);
        CHECK(sc.ok && sc.gameName == "Moon Hop");
        CHECK(sc.achievements.size() == 2 && sc.achievements[0] == "ACH_WIN" && sc.achievements[1] == "ACH_COLLECT");
        CHECK(sc.stats.size() == 2 && sc.stats[1] == "coins");
        CHECK(!sw::ParseSchema("{\"game\":{").ok);
        CHECK(!sw::ParseSchema("{}").ok);
        CHECK(sw::ParseSchema(R"({"game":{}})").ok);   // an app with nothing defined yet

        const std::string script = "function win() { Achievements.Unlock(\"ACH_WIN\"); steam_unlock(\"ACH_TYPO\");\n"
                                   "  Stats.Add(\"coins\", 1); steam_set_stat_int(\"deaths\", 1); Achievements.Unlock(\"ACH_WIN\"); }\n"
                                   "node 4 SteamUnlock ACH_COLLECT\n";
        const auto ach = sw::FindAchievementIds(script);
        CHECK(ach.size() == 3);
        const auto st = sw::FindStatNames(script);
        CHECK(st.size() == 2);
        const auto problems = sw::Compare(sc, ach, st);
        CHECK(problems.size() == 2);
        std::string all; for (auto& p : problems) all += p + "\n";
        CHECK(all.find("ACH_TYPO") != std::string::npos && all.find("deaths") != std::string::npos);

        const std::string key = "PUBLISHERKEY-1234";
        std::string seenUrl; std::vector<std::string> seenHeaders;
        auto fake = [&](const std::string& url, const std::vector<std::string>& h, int* status, std::string* out) {
            seenUrl = url; seenHeaders = h; *status = 200; *out = body; return true;
        };
        const sw::Schema got = sw::FetchSchema(2468, key, fake);
        CHECK(got.ok && got.achievements.size() == 2);
        CHECK(seenUrl.find("appid=2468") != std::string::npos && seenUrl.find(key) == std::string::npos);
        bool inHeader = false; for (auto& h : seenHeaders) if (h == "x-webapi-key: " + key) inHeader = true;
        CHECK(inHeader);
        auto refuse = [&](const std::string&, const std::vector<std::string>&, int* status, std::string* out) { *status = 403; *out = "no"; return true; };
        const sw::Schema bad = sw::FetchSchema(2468, key, refuse);
        CHECK(!bad.ok && bad.error.find("403") != std::string::npos && bad.error.find(key) == std::string::npos);
        CHECK(!sw::FetchSchema(2468, "", fake).ok);
        CHECK(sw::FetchSchema(2468, "", fake).error.find("STEAM_WEB_API_KEY") != std::string::npos);
    }

    {
        RUN_SUITE("example scene examples/store_kit/store_kit.okayscene plays with the local store");
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/store_kit/store_kit.okayscene";
        if (const char* out = std::getenv("OKAY_WRITE_STORE_KIT_EXAMPLE")) {
            Scene w("Store Kit");
            BuildStoreKitExample(w);
            std::string err;
            CHECK(SceneSerializer::SaveToFile(w, out, &err));
            std::printf("  wrote %s %s\n", out, err.c_str());
        }
        const fs::path dir = fs::temp_directory_path() / "okay_store_kit_example";
        std::error_code e3; fs::remove_all(dir, e3);
        PlatformServices::Shutdown();
        PlatformServices::SetLocalFolder(dir.string());
        PlatformServices::Configure(StoreSettings{});
        Scene s("Store Kit");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(s, path, &err));
        if (!err.empty()) std::cerr << "    " << err << "\n";
        s.physicsEnabled = false;
        s.Start();
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(PlatformServices::Get().Kind() == StoreKind::None);
        CHECK(PlatformServices::Get().IsAchievementUnlocked("first_launch"));
        CHECK(PlatformServices::Get().IsAchievementUnlocked("visual_script_achievement"));
        CHECK(PlatformServices::Get().GetStat("launches") == 1);
        GameObject* demo = s.Find("Store Kit Demo");
        CHECK(demo != nullptr);
        if (demo) if (auto* t = demo->GetComponent<TextRenderer>()) {
            CHECK(t->text.find("Store: none") != std::string::npos);
            CHECK(t->text.find("first_launch") != std::string::npos);
        }
        PlatformServices::Shutdown();
        // Kept in the folder: a second run sees it.
        CHECK(MakeNoStoreServices(dir.string())->IsAchievementUnlocked("first_launch"));
        PlatformServices::SetLocalFolder("");
        fs::remove_all(dir, e3);
    }

    // ---- export files -----------------------------------------------------------------------------------------
    const fs::path root = fs::temp_directory_path() / ("okay_stores_test_" + std::to_string((unsigned long long)std::hash<std::string>{}(__TIME__)));
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path build = root / "build", data = build / "Moon Hop_Data", sdk = root / "steamsdk", eos = root / "eossdk";
    fs::create_directories(data, ec);
    { std::ofstream(build / "Moon Hop.exe") << "exe"; }
    fs::create_directories(sdk / "redistributable_bin" / "win64", ec);
    { std::ofstream(sdk / "redistributable_bin" / "win64" / "steam_api64.dll") << "steamdll"; }
    fs::create_directories(eos / "SDK" / "Bin", ec);
    { std::ofstream(eos / "SDK" / "Bin" / "EOSSDK-Win64-Shipping.dll") << "eosdll"; }

    const std::string kSecret = "TOPSECRET-eos-9d1f", kBptSecret = "BPT-SECRET-77aa";
    auto env = [&](const char* name) -> std::string {
        if (std::string(name) == stores::EosClientSecretEnv()) return kSecret;
        if (std::string(name) == stores::BptClientSecretEnv()) return kBptSecret;
        return "";
    };
    auto baseInput = [&] {
        stores::ExportInput in;
        in.buildDir = build.string(); in.dataDir = data.string(); in.exeName = "Moon Hop.exe"; in.gameName = "Moon Hop";
        in.version = "1.2.0"; in.buildNumber = 17; in.getEnv = env;
        return in;
    };
    {
        RUN_SUITE("Steam export: VDF text");
        stores::ExportInput in = baseInput();
        in.store = StoreKind::Steam; in.steam.appId = 2468; in.steam.depotId = 2469; in.steam.branch = "beta";
        const std::string app = stores::SteamAppBuildVdf(in);
        CHECK(app.find("\"AppBuild\"") != std::string::npos);
        CHECK(app.find("\"AppID\" \"2468\"") != std::string::npos);
        CHECK(app.find("\"SetLive\" \"beta\"") != std::string::npos);
        CHECK(app.find("\"2469\" \"depot_build_2469.vdf\"") != std::string::npos);
        CHECK(app.find("\"ContentRoot\" \"..\\..\\\"") != std::string::npos);
        CHECK(app.find("Moon Hop 1.2.0 build 17") != std::string::npos);
        const std::string dep = stores::SteamDepotBuildVdf(in);
        CHECK(dep.find("\"DepotID\" \"2469\"") != std::string::npos);
        CHECK(dep.find("\"LocalPath\" \"*\"") != std::string::npos && dep.find("\"Recursive\" \"1\"") != std::string::npos);
        CHECK(dep.find("\"FileExclusion\" \"StoreUpload\\*\"") != std::string::npos);
        CHECK(dep.find("\"FileExclusion\" \"steam_appid.txt\"") != std::string::npos);
        // braces balance in both files
        for (const std::string* t : {&app, &dep}) {
            int depth = 0; bool neg = false;
            for (char ch : *t) { if (ch == '{') ++depth; if (ch == '}') { --depth; if (depth < 0) neg = true; } }
            CHECK(depth == 0 && !neg);
        }
        // A description with quotes cannot break the file.
        in.steam.description = "Big \"update\"";
        CHECK(stores::SteamAppBuildVdf(in).find("Big 'update'") != std::string::npos);
        const std::string bat = stores::SteamUploadBat(in);
        CHECK(bat.find("+run_app_build") != std::string::npos && bat.find("app_build_2468.vdf") != std::string::npos);
        CHECK(bat.find("STEAM_USER") != std::string::npos && bat.find("+quit") != std::string::npos);
        CHECK(stores::SteamUploadSh(in).find("+run_app_build") != std::string::npos);
    }
    {
        RUN_SUITE("Steam export: validation");
        stores::ExportInput in = baseInput(); in.store = StoreKind::Steam;
        CHECK(!stores::Validate(in).empty());              // no app id
        in.steam.appId = 2468;
        std::vector<std::string> warn;
        CHECK(stores::Validate(in, &warn).empty());          // depot defaults to app id + 1
        CHECK(!warn.empty());                                 // ... but says so, and that no SDK path is set
        in.steam.branch = "default";
        warn.clear(); stores::Validate(in, &warn);
        bool liveWarn = false; for (auto& w : warn) if (w.find("every player") != std::string::npos) liveWarn = true;
        CHECK(liveWarn);
        in.steam.branch = "bad branch!";
        CHECK(!stores::Validate(in).empty());
    }
    {
        RUN_SUITE("Steam export: development build writes steam_appid.txt and the DLL");
        stores::ExportInput in = baseInput();
        in.store = StoreKind::Steam; in.steam.appId = 2468; in.steam.depotId = 2469; in.steam.sdkPath = sdk.string();
        in.developmentBuild = true;
        const stores::ExportResult r = stores::WriteStoreFiles(in);
        CHECK(r.ok && r.errors.empty());
        CHECK(Slurp(build / "steam_appid.txt") == "2468\n");
        CHECK(Slurp(build / "steam_api64.dll") == "steamdll");
        CHECK(fs::exists(build / "StoreUpload" / "steam" / "app_build_2468.vdf"));
        CHECK(fs::exists(build / "StoreUpload" / "steam" / "depot_build_2469.vdf"));
        CHECK(fs::exists(build / "StoreUpload" / "steam" / "upload.bat"));
        CHECK(fs::exists(build / "StoreUpload" / "README.txt"));
        CHECK(!fs::exists(build / "StoreUpload" / "epic"));
        // A shipping build afterwards removes the stale steam_appid.txt.
        in.developmentBuild = false;
        CHECK(stores::WriteStoreFiles(in).ok);
        CHECK(!fs::exists(build / "steam_appid.txt"));
        // Without an SDK path no DLL is copied, and the developer is told.
        fs::remove(build / "steam_api64.dll", ec);
        in.steam.sdkPath.clear();
        const stores::ExportResult r2 = stores::WriteStoreFiles(in);
        CHECK(r2.ok && !fs::exists(build / "steam_api64.dll"));
        bool told = false; for (auto& w : r2.warnings) if (w.find("steam_api64.dll") != std::string::npos) told = true;
        CHECK(told);
        // An SDK path that does not hold the DLL is an error the developer sees.
        in.steam.sdkPath = (root / "nowhere").string();
        const stores::ExportResult r3 = stores::WriteStoreFiles(in);
        bool missing = false; for (auto& w : r3.warnings) if (w.find("not found") != std::string::npos) missing = true;
        CHECK(missing);
    }
    {
        RUN_SUITE("Epic export: BuildPatchTool script and no secret anywhere");
        fs::remove_all(build / "StoreUpload", ec);
        stores::ExportInput in = baseInput();
        in.store = StoreKind::Epic;
        in.epic.productId = "p1"; in.epic.sandboxId = "s1"; in.epic.deploymentId = "d1"; in.epic.clientId = "c1";
        in.epic.organizationId = "o1"; in.epic.artifactId = "a1"; in.epic.bptClientId = "bc1"; in.epic.sdkPath = eos.string();
        CHECK(stores::Validate(in).empty());
        const std::string bat = stores::EpicUploadBat(in);
        CHECK(bat.find("-mode=UploadBinary") != std::string::npos);
        CHECK(bat.find("-OrganizationId=\"o1\"") != std::string::npos && bat.find("-ProductId=\"p1\"") != std::string::npos);
        CHECK(bat.find("-ArtifactId=\"a1\"") != std::string::npos && bat.find("-ClientId=\"bc1\"") != std::string::npos);
        CHECK(bat.find("-ClientSecretEnvVar=\"EPIC_BPT_CLIENT_SECRET\"") != std::string::npos);
        CHECK(bat.find("-AppLaunch=\"Moon Hop.exe\"") != std::string::npos);
        CHECK(bat.find("-BuildVersion=\"1.2.0-b17-win64\"") != std::string::npos);
        CHECK(bat.find("-FileIgnoreList=") != std::string::npos);
        CHECK(stores::EpicBuildVersion("1.0 beta/2", 3) == "1.0_beta_2-b3-win64");
        CHECK(stores::EpicIgnoreList(in).find("StoreUpload") != std::string::npos);

        const stores::ExportResult r = stores::WriteStoreFiles(in);
        CHECK(r.ok && r.errors.empty());
        CHECK(fs::exists(build / "StoreUpload" / "epic" / "upload.bat"));
        CHECK(fs::exists(build / "StoreUpload" / "epic" / "ignore.txt"));
        CHECK(Slurp(build / "EOSSDK-Win64-Shipping.dll") == "eosdll");
        CHECK(!fs::exists(build / "steam_appid.txt"));
        // The runtime secret is in the game, packed, and reads back.
        const fs::path secretFile = data / stores::EpicSecretFileName();
        CHECK(fs::exists(secretFile));
        CHECK(DataPack::IsPacked(Slurp(secretFile)));
        CHECK(stores::ReadEpicSecretFile(data.string()) == kSecret);
        // No plain copy of either secret in any file of the build, nor in what the export reports.
        int scanned = 0;
        for (auto& e : fs::recursive_directory_iterator(build)) {
            if (!e.is_regular_file()) continue;
            const std::string t = Slurp(e.path());
            ++scanned;
            if (t.find(kSecret) != std::string::npos || t.find(kBptSecret) != std::string::npos)
                std::cerr << "    secret found in " << e.path() << "\n";
            CHECK(t.find(kSecret) == std::string::npos);
            CHECK(t.find(kBptSecret) == std::string::npos);
        }
        CHECK(scanned >= 5);
        for (const auto& l : r.warnings) CHECK(l.find(kSecret) == std::string::npos && l.find(kBptSecret) == std::string::npos);
        for (const auto& l : r.written) CHECK(l.find(kSecret) == std::string::npos);
        const std::string cfg = WriteStoreConfig(stores::SettingsFor(in));
        CHECK(cfg.find("store=epic") != std::string::npos && cfg.find("epic_client_id=c1") != std::string::npos);
        CHECK(cfg.find(kSecret) == std::string::npos);

        // Without the environment variable: no secret file, and a warning naming the variable (not a value).
        in.getEnv = [](const char*) { return std::string(); };
        const stores::ExportResult r2 = stores::WriteStoreFiles(in);
        CHECK(!fs::exists(secretFile));
        bool named = false; for (auto& w : r2.warnings) if (w.find("OKAY_EOS_CLIENT_SECRET") != std::string::npos) named = true;
        CHECK(named);
        // Missing upload ids are reported.
        in.epic.organizationId.clear();
        CHECK(!stores::Validate(in).empty());
    }
    {
        RUN_SUITE("no store: nothing is written");
        fs::remove_all(build / "StoreUpload", ec);
        { std::ofstream(build / "steam_appid.txt") << "480\n"; }
        stores::ExportInput in = baseInput();
        const stores::ExportResult r = stores::WriteStoreFiles(in);
        CHECK(r.ok && r.written.empty());
        CHECK(!fs::exists(build / "StoreUpload"));
        CHECK(!fs::exists(build / "steam_appid.txt"));   // a stale one from an earlier Steam build goes
    }
    fs::remove_all(root, ec);

    TEST_MAIN_RESULT();
}
