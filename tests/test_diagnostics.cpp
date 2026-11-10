#include "test_framework.hpp"
#include "okay/Debug/ConsoleModel.hpp"
#include "okay/Debug/Diagnostics.hpp"
#include "okay/Debug/ProjectDiagnostics.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

namespace {
struct NeedBody : Component {};
struct NeedGun : Component {};

int CountCode(const std::vector<Diagnostic>& items, const std::string& code) {
    int n = 0;
    for (const Diagnostic& d : items) if (d.code == code) ++n;
    return n;
}
const Diagnostic* FindCode(const std::vector<Diagnostic>& items, const std::string& code) {
    for (const Diagnostic& d : items) if (d.code == code) return &d;
    return nullptr;
}
}

int main() {
    RUN_SUITE("diagnostics");

    const bool oldConsole = Log::console;
    const Log::Sink oldSink = Log::sink;
    Log::console = false;
    Log::sink = nullptr;

    // ---- explicit source and the thread-local scope ---------------------------------
    {
        ConsoleModel model;
        ConsoleSource src;
        src.kind = ConsoleSource::Kind::Object;
        src.path = "World/Player";
        src.guid = "abc123";
        src.component = "ScriptComponent";
        model.Add(Log::Level::Error, "exploded", "script", src);
        std::vector<ConsoleEntry> entries = model.Entries();
        CHECK(entries.size() == 1);
        CHECK(entries[0].source.kind == ConsoleSource::Kind::Object);
        CHECK(entries[0].source.path == "World/Player");
        CHECK(entries[0].source.guid == "abc123");
        CHECK(entries[0].source.component == "ScriptComponent");

        model.Attach();
        {
            ConsoleSource ambient;
            ambient.kind = ConsoleSource::Kind::Script;
            ambient.path = "Assets/Hero.okay";
            ambient.guid = "guid-hero";
            ambient.line = 12;
            ConsoleSourceScope scope(ambient);
            Log::Error("Hero.okay blew up");
        }
        Log::Error("no scope");
        entries = model.Entries();
        CHECK(entries.size() == 3);
        CHECK(entries[1].source.guid == "guid-hero");
        CHECK(entries[1].source.path == "Assets/Hero.okay");
        CHECK(entries[1].source.line == 12);
        CHECK(entries[1].source.kind == ConsoleSource::Kind::Script);
        CHECK(entries[2].source.guid.empty());
        model.Detach();
    }

    // ---- dedupe, clear by source, clear by owner, log once ---------------------------
    {
        ConsoleModel model;
        model.Attach();
        Diagnostics diag;
        Diagnostic d;
        d.severity = DiagSeverity::Error;
        d.code = "player.missing_collider";
        d.message = "No collider";
        d.hint = "Add one";
        d.owner = "project";
        d.source.objectGuid = "player-guid";
        d.source.objectPath = "Player";
        d.fix.kind = DiagFixKind::AddComponent;
        d.fix.arg = "BoxCollider3D";
        diag.Report(d);
        diag.Report(d);                                   // same problem again
        CHECK(diag.Size() == 1);
        CHECK(diag.Count(DiagSeverity::Error) == 1);
        CHECK(model.Size() == 1);                         // logged once
        CHECK(model.Entries()[0].source.guid == "player-guid");
        CHECK(model.Entries()[0].message.find("[Object: Player]") != std::string::npos);

        d.message = "No collider yet";
        diag.Report(d);                                   // same key, new wording
        CHECK(diag.Size() == 1);
        CHECK(diag.Items()[0].message == "No collider yet");
        CHECK(diag.Items()[0].id == 1);                   // the row keeps its id
        CHECK(model.Size() == 2);                         // the change is logged

        Diagnostic other = d;
        other.code = "player.missing_rigidbody";
        other.message = "No rigidbody";
        other.owner = "other";
        diag.Report(other);
        CHECK(diag.Size() == 2);

        DiagSource clear;
        clear.objectGuid = "player-guid";
        diag.ClearSource(clear);
        CHECK(diag.Size() == 0);                          // both rows were about that object

        d.owner = "project";
        other.owner = "graph.editor";
        diag.Report(d);
        diag.Report(other);
        diag.ClearOwner("project");
        CHECK(diag.Size() == 1);
        CHECK(diag.Items()[0].owner == "graph.editor");

        std::vector<Diagnostic> batch;
        Diagnostic again = d;
        again.message = "No collider";
        batch.push_back(again);
        const std::size_t before = model.Size();
        diag.ReplaceOwner("project", batch);
        diag.ReplaceOwner("project", batch);              // identical refresh
        CHECK(diag.Count(DiagSeverity::Error) == 2);      // project row + the graph row
        CHECK(model.Size() == before + 1);                // the second refresh did not log
        diag.ClearAll();
        CHECK(diag.Size() == 0);
        model.Detach();
    }

    // ---- validators: camera, references, scripts, player, animator, navmesh, graphs --
    {
        Scene scene("diag");
        GameObject* crate = scene.CreateGameObject("Crate");
        crate->AddComponent<SpriteRenderer>()->texture = "no/such_crate.png";

        GameObject* chaser = scene.CreateGameObject("Chaser");
        chaser->AddComponent<FollowTarget2D>()->target = "Nope";

        GameObject* player = scene.CreateGameObject("Player");
        player->AddComponent<CharacterController3D>();

        GameObject* puppet = scene.CreateGameObject("Puppet");
        puppet->AddComponent<AnimStateMachine>();

        GameObject* walker = scene.CreateGameObject("Walker");
        walker->AddComponent<NavMeshAgent>();

        GameObject* broken = scene.CreateGameObject("BrokenScript");
        ScriptComponent* sc = broken->AddComponent<ScriptComponent>();
        sc->SetPath("Assets/MissingScript.okay");

        GameObject* bad = scene.CreateGameObject("BadCode");
        bad->AddComponent<ScriptComponent>()->SetSource("function (");

        GameObject* graph = scene.CreateGameObject("BadGraph");
        graph->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 0 OnStart\nnode 1 Transform.Translate/1\nnode 2 Const \"hello\"\n"
            "data 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n");

        GameObject* dupA = scene.CreateGameObject("DupA");
        GameObject* dupB = scene.CreateGameObject("DupB");
        dupA->SetGuid("abcdefabcdefabcdefabcdefabcdefab");
        dupB->SetGuid("abcdefabcdefabcdefabcdefabcdefab");

        RequireComponent<NeedGun, NeedBody>();
        GameObject* turret = scene.CreateGameObject("Turret");
        turret->AddComponent<NeedGun>();
        turret->RemoveComponent<NeedBody>();

        AddNodeRequirement("Print", "SpriteRenderer", "This test node needs a sprite.");
        GameObject* printer = scene.CreateGameObject("Printer");
        printer->AddComponent<VisualScriptComponent>()->LoadFromText(
            "node 0 OnStart\nnode 1 Print\nexec 0 0 1\nentry OnStart 0\n");

        ProjectDiagOptions opt;
        opt.assetRoot = "";
        ProjectDiagResult result = CollectProjectDiagnostics(scene, opt);

        CHECK(CountCode(result.items, "scene.no_camera") == 1);
        CHECK(FindCode(result.items, "scene.no_camera")->severity == DiagSeverity::Error);
        CHECK(FindCode(result.items, "scene.no_camera")->fix.kind == DiagFixKind::AddCamera);
        CHECK(CountCode(result.items, "asset.missing") >= 1);
        CHECK(CountCode(result.items, "ref.missing_object") >= 1);
        CHECK(CountCode(result.items, "script.missing_file") >= 1);
        CHECK(CountCode(result.items, "script.compile") >= 1);
        CHECK(CountCode(result.items, "player.missing_collider") == 1);
        CHECK(CountCode(result.items, "player.missing_rigidbody") == 1);
        CHECK(CountCode(result.items, "animator.no_controller") == 1);
        CHECK(CountCode(result.items, "navmesh.no_bake") == 1);
        CHECK(CountCode(result.items, "graph.error") >= 1);
        CHECK(CountCode(result.items, "graph.node_requirement") == 1);
        CHECK(CountCode(result.items, "component.missing_requirement") >= 1);
        CHECK(CountCode(result.items, "scene.duplicate_guid") >= 1);

        const Diagnostic* req = FindCode(result.items, "graph.node_requirement");
        CHECK(req && req->source.nodeId == "1");
        CHECK(req && req->fix.arg == "SpriteRenderer");
        const Diagnostic* wire = nullptr;
        for (const Diagnostic& d : result.items)
            if (d.code == "graph.error" && d.source.objectPath == "BadGraph") wire = &d;
        CHECK(wire && wire->source.nodeId == "1");

        // The file check and the loaded-scene check agree on the missing texture:
        // one row, not two.
        int textureRows = 0;
        for (const Diagnostic& d : result.items)
            if (d.source.assetPath.find("such_crate") != std::string::npos) ++textureRows;
        CHECK(textureRows == 1);

        const Diagnostic* collider = FindCode(result.items, "player.missing_collider");
        CHECK(collider && collider->source.objectGuid == player->Guid());
        CHECK(ApplyDiagnosticFix(scene, *collider, nullptr));
        CHECK(player->GetComponent<Collider3D>() != nullptr);
        CHECK(player->GetComponent<BoxCollider3D>() != nullptr);

        const Diagnostic* follow = nullptr;
        for (const Diagnostic& d : result.items)
            if (d.code == "ref.missing_object" && d.source.objectGuid == chaser->Guid()) follow = &d;
        CHECK(follow && follow->fix.kind == DiagFixKind::ClearReference);
        CHECK(ApplyDiagnosticFix(scene, *follow, nullptr));
        CHECK(chaser->GetComponent<FollowTarget2D>()->target.empty());

        const Diagnostic* need = nullptr;
        for (const Diagnostic& d : result.items)
            if (d.code == "component.missing_requirement" && d.source.objectGuid == turret->Guid()) need = &d;
        CHECK(need && need->fix.kind == DiagFixKind::AddComponent);
        CHECK(ApplyDiagnosticFix(scene, *need, nullptr));
        CHECK(turret->GetComponent<NeedBody>() != nullptr);

        const Diagnostic* cam = FindCode(result.items, "scene.no_camera");
        CHECK(ApplyDiagnosticFix(scene, *cam, nullptr));
        CHECK(!scene.FindObjectsOfType<Camera>().empty());

        // A second pass drops what the fixes repaired and keeps what they did not.
        ProjectDiagResult again = CollectProjectDiagnostics(scene, opt);
        CHECK(CountCode(again.items, "player.missing_collider") == 0);
        CHECK(CountCode(again.items, "ref.missing_object") == 0 ||
              FindCode(again.items, "ref.missing_object")->source.objectGuid != chaser->Guid());
        CHECK(CountCode(again.items, "component.missing_requirement") == 0);
        CHECK(CountCode(again.items, "scene.no_camera") == 0);
        CHECK(CountCode(again.items, "player.missing_rigidbody") == 1);

        // Dedupe through the store: refreshing the same report does not grow it.
        Diagnostics diag;
        diag.SetLogging(false);
        diag.ReplaceOwner("project", result.items);
        const std::size_t n = diag.Size();
        diag.ReplaceOwner("project", result.items);
        CHECK(diag.Size() == n && n > 5);
        DiagSource gone;
        gone.objectGuid = player->Guid();
        diag.ClearSource(gone);
        for (const Diagnostic& row : diag.Items())
            CHECK(row.source.objectGuid != player->Guid());

        ClearNodeRequirements();
        CHECK(NodeRequirementsFor("Print").empty());
        ComponentRules::Clear();
    }

    // A bad wire names the input pin, so the editor can paint it red.
    {
        auto issues = vs::ValidateGraphText(
            "node 0 OnStart\nnode 1 Transform.Translate/1\nnode 2 Const \"hello\"\n"
            "data 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n");
        bool pin = false;
        for (const vs::VsIssue& issue : issues)
            if (issue.node == 1 && issue.pin == 0 && issue.pinDir == 0) pin = true;
        CHECK(pin);
    }

    // ---- AssetDatabase::FindMissing feeds the same list -----------------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_diag_missing_assets";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        {
            std::ofstream out(dir / "Level.okayscene");
            out << "sprite \"missing/ghost.png\"\n";
        }
        Scene scene("indexed");
        ProjectDiagOptions opt;
        opt.assetRoot = dir.string();
        opt.checkSemantic = false;
        opt.checkSceneFile = false;
        opt.checkRequirements = false;
        opt.checkGameplay = false;
        opt.checkGraphs = false;
        ProjectDiagResult result = CollectProjectDiagnostics(scene, opt);
        const Diagnostic* miss = FindCode(result.items, "asset.db.missing");
        CHECK(miss != nullptr);
        CHECK(miss && miss->source.assetPath.find("ghost.png") != std::string::npos);
        CHECK(miss && miss->message.find("Level.okayscene") != std::string::npos);
        int metas = 0;                       // checking a project must not write a meta file next to anything
        for (auto& e : fs::recursive_directory_iterator(dir, ec)) if (e.path().extension() == ".okaymeta") ++metas;
        CHECK(metas == 0);
        fs::remove_all(dir, ec);
    }

    Log::sink = oldSink;
    Log::console = oldConsole;
    TEST_MAIN_RESULT();
}
