// The editor's startup questions take turns (issue #888, "Upgrade project" item 0).
// A project with no engine_version opened with --open, plus a recovery copy left by a crash, made the "Project version"
// question and "Restore recovered work?" each re-open their modal every frame at the same ImGui popup level; they closed
// each other, neither was drawn, ImGui blocked all input and the scene never opened. editor/StartupModals.hpp now decides
// which question may ask in a frame. These checks step "frames" through that startup with the real ProjectGate and a real
// RecoveryManager on temp folders: exactly one question asks at a time, the version question first, then the recovery
// question, then nothing; the scene opens after the version question is answered; an answered recovery copy is deleted.
// Written from the bug report and the editor's own code (no outside code).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Recovery.hpp"
#include "../editor/ProjectUpgrade.hpp"
#include "../editor/StartupModals.hpp"

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;
namespace st = okay::editor::startup;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_startupmodals_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec; fs::remove_all(path, ec); fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};
void WriteFile(const fs::path& p, const std::string& s) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}
std::string SceneText() {
    Scene s("S");
    s.CreateGameObject("Car");
    return SceneSerializer::Serialize(s);
}
// Leave what a crashed editor leaves in `dir`: a session whose lock is still there, with `copies` autosaves.
void LeaveCrashedSession(const std::string& dir, int copies) {
    RecoveryManager::Config cfg;
    cfg.directory = dir; cfg.baseName = "editor"; cfg.sessionId = "crashed"; cfg.skipUnchanged = false;
    RecoveryManager dead(cfg);
    dead.StartSession();
    for (int i = 0; i < copies; ++i) dead.AutosavePayload("unsaved work " + std::to_string(i), "1 unsaved");
    // no EndSession: the process "died"
}
// The editor's manager for a folder; every other session's process counts as gone.
RecoveryManager::Config ManagerCfg(const std::string& dir, const std::string& session) {
    RecoveryManager::Config cfg;
    cfg.directory = dir; cfg.baseName = "editor"; cfg.sessionId = session;
    return cfg;
}
struct Frame { int asking = 0; bool version = false, recovery = false, chooser = false, save = false, sceneAutosave = false; };
Frame Ask(const st::Pending& p) {
    Frame f;
    f.version = st::MayAsk(st::Modal::ProjectVersion, p);
    f.save = st::MayAsk(st::Modal::SaveChanges, p);
    f.chooser = st::MayAsk(st::Modal::ProjectChooser, p);
    f.sceneAutosave = st::MayAsk(st::Modal::SceneAutosave, p);
    f.recovery = st::MayAsk(st::Modal::Recovery, p);
    f.asking = (int)f.version + (int)f.save + (int)f.chooser + (int)f.sceneAutosave + (int)f.recovery;
    return f;
}
}  // namespace

int main() {
    RUN_SUITE("startup_modals");

    // 1. The bug's startup: --open a scene of a project with no engine_version, and a recovery copy from a crash.
    {
        TempDir td;
        const fs::path root = td.path / "vehicle";
        WriteFile(root / "Assets" / "vehicle_test.okayscene", SceneText());
        const std::string scene = (root / "Assets" / "vehicle_test.okayscene").string();
        const std::string recDir = (td.path / "userdata" / "okay_recovery").string();
        LeaveCrashedSession(recDir, 2);   // two rotating copies of the same session

        RecoveryManager rec(ManagerCfg(recDir, "now"));
        rec.SetProcessProbe([](long) { return false; });
        CHECK(rec.StartSession());
        std::vector<RecoveryManager::Candidate> copies = rec.FindRecoverable();
        CHECK(copies.size() == 2);

        okayup::GateEnv env; env.running = "2.874.0";
        okayup::ProjectGate gate(env);
        int opened = 0;
        gate.Open(scene, [&] { ++opened; });
        CHECK(gate.Busy() && gate.prompt().kind == okayup::PromptKind::Unknown);
        CHECK(opened == 0);

        // Both questions are due. For 30 frames exactly one asks, and it is the version question.
        int bad = 0, versionFrames = 0;
        for (int f = 0; f < 30; ++f) {
            gate.Tick(std::chrono::milliseconds(10));
            const Frame fr = Ask({gate.Busy(), false, false, !copies.empty()});
            if (fr.asking != 1) ++bad;
            if (fr.version && !fr.recovery) ++versionFrames;
        }
        std::cerr << "[startup_modals] both due: frames with not exactly one question=" << bad << " of 30\n";
        CHECK(bad == 0);
        CHECK(versionFrames == 30);

        // Answer it: Upgrade. The progress and the result are the same window; recovery still waits.
        gate.Answer(okayup::Choice::Upgrade);
        int steps = 0;
        while (gate.prompt().kind == okayup::PromptKind::Working && steps++ < 5000) {
            gate.Tick(std::chrono::milliseconds(10));
            const Frame fr = Ask({gate.Busy(), false, false, !copies.empty()});
            if (fr.asking != 1 || !fr.version) ++bad;
        }
        CHECK(gate.prompt().kind == okayup::PromptKind::Done);
        CHECK(bad == 0);
        gate.Answer(okayup::Choice::Continue);   // "Open project"
        CHECK(!gate.Busy());
        CHECK(opened == 1);                       // the --open scene opens

        // Now the recovery question, alone.
        int recoveryFrames = 0;
        for (int f = 0; f < 10; ++f) {
            const Frame fr = Ask({gate.Busy(), false, false, !copies.empty()});
            if (fr.asking == 1 && fr.recovery) ++recoveryFrames;
        }
        CHECK(recoveryFrames == 10);

        // Answered ("Discard"): every copy of that crashed session goes, so the question does not come back for the older copy.
        std::vector<std::string> sessions;
        for (const auto& c : copies) sessions.push_back(c.sessionId);
        const std::vector<std::size_t> drop = st::CopiesToDiscard(sessions, 0, true);
        CHECK(drop.size() == 2);
        for (std::size_t i : drop) rec.Discard(copies[i]);
        copies = rec.FindRecoverable();
        CHECK(copies.empty());
        CHECK(Ask({gate.Busy(), false, false, !copies.empty()}).asking == 0);   // nothing left to ask: the editor is free
        rec.EndSession(true);
    }

    // 2. The order, one question at a time, whatever is due.
    {
        CHECK(Ask({}).asking == 0);
        Frame f = Ask({true, true, true, true, true});
        CHECK(f.asking == 1 && f.version);                              // the version question first
        f = Ask({false, true, false, true});
        CHECK(f.asking == 1 && f.save);                                 // "Save changes?" before recovery
        f = Ask({false, false, true, true});
        CHECK(f.asking == 1 && f.chooser);                              // the project chooser before recovery
        f = Ask({false, false, false, true, true});
        CHECK(f.asking == 1 && f.sceneAutosave);                        // an old "<scene>.autosave" (scene just opened) before the session's copy
        f = Ask({false, false, false, true});
        CHECK(f.asking == 1 && f.recovery);
        CHECK(!st::MayAsk(st::Modal::None, {true, true, true, true, true}));
    }

    // 3. Which copies an answer deletes.
    {
        const std::vector<std::string> s = {"b", "b", "a", "b"};
        std::vector<std::size_t> d = st::CopiesToDiscard(s, 0, true);
        CHECK((d == std::vector<std::size_t>{0, 1, 3}));               // every copy of session b; a's copy stays (another crash)
        d = st::CopiesToDiscard(s, 0, false);
        CHECK((d == std::vector<std::size_t>{0}));                      // an unreadable copy goes alone: the older copy is offered next
        d = st::CopiesToDiscard(s, 2, true);
        CHECK((d == std::vector<std::size_t>{2}));
        CHECK(st::CopiesToDiscard(s, 9, true).empty());
    }

    // 4. Where the recovery copies live: under the user's data folder (absolute), the old relative folder read once.
    {
        st::RecoveryFolders r = st::PlanRecoveryFolders("/home/u/.local/share/OkaySpace", "/opt/OkaySpace/Tools");
        CHECK(r.folder == "/home/u/.local/share/OkaySpace/okay_recovery");
        CHECK(r.legacy == "/opt/OkaySpace/Tools/okay_recovery");
        r = st::PlanRecoveryFolders("C:\\Users\\u\\AppData\\Roaming\\OkaySpace", "D:\\Games\\OkaySpace");
        CHECK(r.folder == "C:/Users/u/AppData/Roaming/OkaySpace/okay_recovery");
        CHECK(r.legacy == "D:/Games/OkaySpace/okay_recovery");
        r = st::PlanRecoveryFolders("OkaySpace", "/opt/x");               // no HOME / APPDATA: the start folder, made absolute
        CHECK(r.folder == "/opt/x/okay_recovery");
        CHECK(r.legacy.empty());                                         // the same folder: nothing extra to read
        r = st::PlanRecoveryFolders("/data/OkaySpace/", "/data/OkaySpace");
        CHECK(r.folder == "/data/OkaySpace/okay_recovery");
        CHECK(r.legacy.empty());
    }

    // 5. A copy left in the old folder is still found (read in place by a manager that starts no session there), and answered once.
    {
        TempDir td;
        const std::string oldDir = (td.path / "start" / "okay_recovery").string();
        const std::string newDir = (td.path / "userdata" / "okay_recovery").string();
        LeaveCrashedSession(oldDir, 1);
        RecoveryManager rec(ManagerCfg(newDir, "now"));
        rec.SetProcessProbe([](long) { return false; });
        CHECK(rec.StartSession());
        CHECK(rec.FindRecoverable().empty());
        RecoveryManager legacy(ManagerCfg(oldDir, ""));
        legacy.SetProcessProbe([](long) { return false; });
        std::vector<RecoveryManager::Candidate> old = legacy.FindRecoverable();
        CHECK(old.size() == 1);
        std::string payload;
        CHECK(!old.empty() && rec.Restore(old[0], payload) && payload == "unsaved work 0");   // the editor's manager reads it by its path
        if (!old.empty()) rec.Discard(old[0]);
        CHECK(legacy.FindRecoverable().empty());
        rec.EndSession(true);
    }

    TEST_MAIN_RESULT();
}
