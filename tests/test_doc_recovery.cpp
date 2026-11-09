// Autosave bundles, crash recovery and backup rotation for editor documents.
// Uses the engine RecoveryManager and BackupManager (area 22), not a second format.
#include "test_framework.hpp"
#include "DocRecovery.hpp"
#include "okay/Core/Backup.hpp"
#include "okay/Core/Recovery.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
using namespace okay::editor::docrec;

static void WriteFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
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

    std::filesystem::remove_all(dir);
    TEST_MAIN_RESULT();
}
