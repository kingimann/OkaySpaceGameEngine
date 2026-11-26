// Autosave bundles, crash recovery and backup rotation for editor documents.
// Uses the engine RecoveryManager and BackupManager (area 22), not a second format.
#include "test_framework.hpp"
#include "DocRecovery.hpp"
#include "okay/Core/Backup.hpp"
#include "okay/Core/Recovery.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::docrec;

static void WriteFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
}

// An editor session in `folder` that autosaves each payload in turn (one second apart) and is then killed: its lock stays.
static void KilledSession(const std::string& folder, const std::string& session, const std::vector<std::string>& payloads) {
    RecoveryManager::Config cfg;
    cfg.directory = folder;
    cfg.baseName = "editor";
    cfg.sessionId = session;
    cfg.slots = 3;
    RecoveryManager m(cfg);
    std::int64_t now = 1'700'000'000;
    m.SetClock([&] { return now; });
    m.SetProcessProbe([](long) { return true; });
    CHECK(m.StartSession());
    for (const auto& p : payloads) {
        ++now;
        CHECK(m.AutosavePayload(p, "1 unsaved") == RecoveryManager::AutosaveResult::Written);
    }
}

// The next start of the editor in `folder` (the killed session's process is gone).
static std::unique_ptr<RecoveryManager> RestartedEditor(const std::string& folder) {
    RecoveryManager::Config cfg;
    cfg.directory = folder;
    cfg.baseName = "editor";
    cfg.sessionId = "next-start";
    auto m = std::make_unique<RecoveryManager>(cfg);
    m->SetClock([] { return (std::int64_t)1'700'000'500; });
    m->SetProcessProbe([](long) { return false; });
    CHECK(m->StartSession());
    return m;
}

// "Restore recovered work?" (owner, 2.903.8.128: "only Discard works"). The question is built from these helpers.
static void RestoreQuestion(const std::string& dir) {
    namespace fs = std::filesystem;
    // A session that autosaved three times leaves three files: that is ONE copy to restore (the newest), not three
    // questions in a row where the second Restore puts older text over the newer one.
    {
        const std::string folder = dir + "/rq_slots";
        KilledSession(folder, "killed", {EncodeBundle({{"graph", "untitled:1", "v1"}}),
                                         EncodeBundle({{"graph", "untitled:1", "v2"}}),
                                         EncodeBundle({{"graph", "untitled:1", "v3"}})});
        auto next = RestartedEditor(folder);
        const auto found = next->FindRecoverable();
        CHECK(found.size() == 3);
        const auto offers = MakeOffers(*next, found);
        CHECK(offers.size() == 1);
        if (offers.size() == 1) {
            CHECK(offers[0].readable);
            CHECK(offers[0].files.size() == 3);
            CHECK(offers[0].docs.size() == 1 && offers[0].docs[0].text == "v3");
            CHECK(DiscardOffer(*next, offers[0]) == 0);
        }
        CHECK(next->FindRecoverable().empty());   // Discard removed every file of that session
    }
    // A copy whose newest file can't be unpacked falls back to the one before it; when none can, the question says why.
    {
        const std::string folder = dir + "/rq_damaged";
        KilledSession(folder, "killed", {EncodeBundle({{"script", "untitled-script", "older"}}), "not a bundle"});
        auto next = RestartedEditor(folder);
        auto offers = MakeOffers(*next, next->FindRecoverable());
        CHECK(offers.size() == 1);
        if (offers.size() == 1) {
            CHECK(offers[0].readable);
            CHECK(offers[0].docs.size() == 1 && offers[0].docs[0].text == "older");
            CHECK(offers[0].note.find("not a recovery bundle") != std::string::npos);
        }
        const std::string folder2 = dir + "/rq_unreadable";
        KilledSession(folder2, "killed", {"not a bundle"});
        auto next2 = RestartedEditor(folder2);
        offers = MakeOffers(*next2, next2->FindRecoverable());
        CHECK(offers.size() == 1);
        if (offers.size() == 1) {
            CHECK(!offers[0].readable);
            CHECK(offers[0].error == "not a recovery bundle");
            CHECK(DiscardOffer(*next2, offers[0]) == 0);   // Discard still works on a copy that can't be read
        }
    }
    // The file is gone between the list and the question (another editor discarded it): said plainly, not a silent no-op.
    {
        const std::string folder = dir + "/rq_gone";
        KilledSession(folder, "killed", {EncodeBundle({{"graph", "untitled:1", "x"}})});
        auto next = RestartedEditor(folder);
        const auto found = next->FindRecoverable();
        CHECK(found.size() == 1);
        for (const auto& c : found) fs::remove(c.path);
        const auto offers = MakeOffers(*next, found);
        CHECK(offers.size() == 1 && !offers[0].readable && offers[0].error.find("can't open") != std::string::npos);
    }
    // Killed in the middle of a write: the half-written temp file is cleaned up and a torn file is never offered, while
    // the session's good file still is.
    {
        const std::string folder = dir + "/rq_torn";
        KilledSession(folder, "killed", {EncodeBundle({{"graph", "untitled:1", "good"}})});
        WriteFile(folder + "/editor-killed-1.autosave.tmp", "OKAYREC 1\nsession=killed\n");
        const std::string full = RecoveryManager::Encode("killed", 9, 1'700'000'009, "1 unsaved", EncodeBundle({{"graph", "untitled:1", "newer"}}));
        WriteFile(folder + "/editor-killed-2.autosave", full.substr(0, full.size() - 3));   // torn: the last bytes never landed
        auto next = RestartedEditor(folder);
        CHECK(!fs::exists(folder + "/editor-killed-1.autosave.tmp"));
        const auto offers = MakeOffers(*next, next->FindRecoverable());
        CHECK(offers.size() == 1 && offers[0].readable && offers[0].docs.size() == 1 && offers[0].docs[0].text == "good");
    }
    // The copy is read once when the editor starts: a later change of the working folder (the recovery folder is
    // relative to it) can no longer make Restore fail.
    {
        const std::string folder = dir + "/rq_cwd";
        fs::create_directories(folder);
        const fs::path before = fs::current_path();
        fs::current_path(folder);
        KilledSession("okay_recovery", "killed", {EncodeBundle({{"graph", "untitled:1", "cwd"}})});
        auto next = RestartedEditor("okay_recovery");
        const auto found = next->FindRecoverable();
        const auto offers = MakeOffers(*next, found);
        fs::current_path(before);
        std::string payload, err;
        CHECK(found.size() == 1 && !next->Restore(found[0], payload, &err));   // the relative path no longer resolves
        CHECK(offers.size() == 1 && offers[0].readable && offers[0].docs.size() == 1 && offers[0].docs[0].text == "cwd");
    }
    // A recovery folder with non-ASCII letters in its path (a user name like "Jöhn"). Linux only here; Windows unverified.
    {
        const std::string folder = dir + "/rq_J\xC3\xB6hn/okay_recovery";
        KilledSession(folder, "killed", {EncodeBundle({{"graph", "untitled:1", "utf8"}})});
        auto next = RestartedEditor(folder);
        const auto offers = MakeOffers(*next, next->FindRecoverable());
        CHECK(offers.size() == 1 && offers[0].readable && offers[0].docs.size() == 1 && offers[0].docs[0].text == "utf8");
    }
    // Restore brings back every document of the bundle to its own place: two documents in, two documents out, and a
    // closed script goes back to the Script Editor, not into a visual-script graph.
    {
        const std::string folder = dir + "/rq_apply";
        KilledSession(folder, "killed", {EncodeBundle({{"graph", "/p/Assets/AI.okayvs", "# OkayVS graph\nnode 1 OnStart\n"},
                                                       {"scene", "/p/Assets/Scenes/Main.okayscene", "scene text"},
                                                       {"closed", "/p/Assets/Hero.okay", "update(dt) { }\n"},
                                                       {"closed", "untitled:4", "# OkayVS graph\n"},
                                                       {"closed-script", "untitled-script", "print(1)"}})});
        auto next = RestartedEditor(folder);
        const auto offers = MakeOffers(*next, next->FindRecoverable());
        CHECK(offers.size() == 1 && offers[0].readable && offers[0].docs.size() == 5);
        std::vector<std::string> graphs, scripts, scenes, order;
        RestoreSinks to;
        to.graph = [&](const Buffer& b, std::string&) { graphs.push_back(b.key); order.push_back("graph"); return true; };
        to.script = [&](const Buffer& b, std::string&) { scripts.push_back(b.key); order.push_back("script"); return true; };
        to.scene = [&](const Buffer& b, std::string&) { scenes.push_back(b.text); order.push_back("scene"); return true; };
        if (offers.size() == 1) {
            const RestoreOutcome r = ApplyBundle(offers[0].docs, to);
            CHECK(r.Ok() && r.restored == 5);
            CHECK(graphs.size() == 2 && graphs[0] == "/p/Assets/AI.okayvs" && graphs[1] == "untitled:4");
            CHECK(scripts.size() == 2 && scripts[0] == "/p/Assets/Hero.okay" && scripts[1] == "untitled-script");
            CHECK(scenes.size() == 1 && scenes[0] == "scene text");
            CHECK(!order.empty() && order.front() == "scene");   // the scene first, so the tabs opened after it stay
            // A part that can't be restored is reported (the copy is then kept), and the rest still comes back.
            to.scene = [](const Buffer&, std::string& e) { e = "This project can't be opened yet."; return false; };
            graphs.clear(); scripts.clear();
            const RestoreOutcome bad = ApplyBundle(offers[0].docs, to);
            CHECK(!bad.Ok() && bad.restored == 4 && bad.failed.size() == 1);
            CHECK(bad.Describe().find("Main.okayscene: This project can't be opened yet.") != std::string::npos);
            CHECK(graphs.size() == 2 && scripts.size() == 2);
            const RestoreOutcome odd = ApplyBundle({{"mystery", "k", "t"}}, to);
            CHECK(!odd.Ok() && odd.restored == 0);
        }
        CHECK(RoleOf({"closed", "untitled-script", "x"}) == Role::Script);
        CHECK(RoleOf({"closed", "/p/a.OKAYVS", "x"}) == Role::Graph);
    }
    // Compare reads the files on disk once and lists what differs.
    {
        WriteFile(dir + "/cmp_same.okay", "same");
        WriteFile(dir + "/cmp_diff.okay", "old");
        const auto rows = CompareRows({{"script", dir + "/cmp_same.okay", "same"},
                                       {"script", dir + "/cmp_diff.okay", "new"},
                                       {"graph", "untitled:2", "g"}});
        CHECK(rows.size() == 2);
        if (rows.size() == 2) {
            CHECK(rows[0].find("cmp_diff.okay") != std::string::npos && rows[0].find("different") != std::string::npos);
            CHECK(rows[1].find("no file yet") != std::string::npos);
        }
    }
}

int main() {
    RUN_SUITE("doc_recovery");
    const std::string dir = (std::filesystem::temp_directory_path() / "okay_docrec_e755").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    {
        std::vector<Buffer> docs = {
            {"graph", "untitled:3", "node 1 OnStart\n"},
            {"script", "/proj/Assets/Hero.okay", "update(dt) { }\n"},
        };
        std::vector<Buffer> back;
        std::string err;
        CHECK(DecodeBundle(EncodeBundle(docs), back, &err));
        CHECK(back.size() == 2);
        CHECK(back[0].kind == "graph" && back[0].key == "untitled:3" && back[0].text == "node 1 OnStart\n");
        CHECK(back[1].text == "update(dt) { }\n");
        CHECK(BufferWorthRestoring("changed", "saved", true));
        CHECK(!BufferWorthRestoring("same", "same", true));
        CHECK(BufferWorthRestoring("hello", "", false));
        CHECK(!BufferWorthRestoring("", "", false));
    }

    {
        RecoveryManager::Config cfg;
        cfg.directory = dir + "/recovery";
        cfg.baseName = "editor";
        cfg.intervalSeconds = 30.0f;
        cfg.sessionId = "dead-session";
        cfg.slots = 3;
        RecoveryManager crashed(cfg);
        crashed.SetClock([] { return (std::int64_t)1'700'000'000; });
        crashed.SetProcessProbe([](long) { return true; });
        CHECK(crashed.StartSession());
        const std::string bundle = EncodeBundle({{"graph", "untitled:1", "node 1 Print\n"}});
        CHECK(crashed.AutosavePayload(bundle, "1 unsaved") == RecoveryManager::AutosaveResult::Written);
        // Leave the lock behind: this is a kill, not EndSession.
    }
    {
        RecoveryManager::Config cfg;
        cfg.directory = dir + "/recovery";
        cfg.baseName = "editor";
        cfg.intervalSeconds = 30.0f;
        cfg.sessionId = "next-run";
        RecoveryManager next(cfg);
        next.SetClock([] { return (std::int64_t)1'700'000'100; });
        next.SetProcessProbe([](long) { return false; });   // the previous pid is gone
        CHECK(next.StartSession());
        CHECK(next.UncleanShutdownDetected());
        auto found = next.FindRecoverable();
        CHECK(!found.empty());
        std::string payload, err;
        CHECK(next.Restore(found.front(), payload, &err));
        std::vector<Buffer> docs;
        CHECK(DecodeBundle(payload, docs, &err));
        CHECK(docs.size() == 1 && docs[0].text.find("Print") != std::string::npos);
        CHECK(found.front().timestamp >= 1'700'000'000);
        next.EndSession(true);
    }

    {
        const std::string file = dir + "/Hero.okayvs";
        BackupManager::Config cfg;
        cfg.directory = dir + "/backups";
        cfg.policy = LastSavesPolicy(5);
        cfg.skipIdentical = false;
        BackupManager backups(cfg);
        std::int64_t now = 1'700'000'000;
        backups.SetClock([&] { return now; });
        for (int i = 0; i < 7; ++i) {
            WriteFile(file, "version " + std::to_string(i));
            now += 60;
            CHECK(backups.Backup(file));
        }
        const auto list = backups.List(file);
        CHECK((int)list.size() == 5);
        if ((int)list.size() == 5) {
            std::string newest, oldest;
            CHECK(backups.Read(list.front(), newest));
            CHECK(backups.Read(list.back(), oldest));
            CHECK(newest == "version 6");
            CHECK(oldest == "version 2");
        }
    }

    // The old <scene>.autosave sidecar is read, never written.
    {
        CHECK(!WritesLegacySceneSidecar());
        const std::string scene = dir + "/Town.okayscene";
        const std::string side = scene + ".autosave";
        WriteFile(scene, "scene v1\n");
        WriteFile(side, "recovered scene\n");
        const auto sceneTime = std::filesystem::last_write_time(scene);
        std::filesystem::last_write_time(side, sceneTime + std::chrono::seconds(60));
        CHECK(NewerLegacySceneAutosave(scene) == side);
        std::filesystem::last_write_time(side, sceneTime);
        CHECK(NewerLegacySceneAutosave(scene).empty());
        std::filesystem::last_write_time(side, sceneTime - std::chrono::seconds(120));
        CHECK(NewerLegacySceneAutosave(scene).empty());
        std::filesystem::remove(side);
        CHECK(NewerLegacySceneAutosave(scene).empty());
        CHECK(!std::filesystem::exists(side));
        CHECK(NewerLegacySceneAutosave("").empty());
        const std::string gone = dir + "/Missing.okayscene";
        WriteFile(gone + ".autosave", "only the sidecar\n");
        CHECK(NewerLegacySceneAutosave(gone) == gone + ".autosave");
        std::filesystem::remove(gone + ".autosave");
        CHECK(!std::filesystem::exists(gone + ".autosave"));
    }

    RestoreQuestion(dir);

    std::filesystem::remove_all(dir);
    TEST_MAIN_RESULT();
}
