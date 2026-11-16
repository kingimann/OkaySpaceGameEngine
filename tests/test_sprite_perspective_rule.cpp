// render.sprite_under_perspective (#888 finding 40): the player and the editor's
// Game view draw SpriteRenderers only when the main camera is Orthographic, so a
// Perspective main camera hides every sprite. The Problems panel must say so and
// offer a fix that switches the camera to Orthographic.
#include "test_framework.hpp"
#include "okay/Debug/Diagnostics.hpp"
#include "okay/Debug/ProjectDiagnostics.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace okay;

namespace {

const char* kCode = "render.sprite_under_perspective";

// Only the gameplay checks: the rule lives there, and the other checks need files on disk.
ProjectDiagOptions GameplayOnly() {
    ProjectDiagOptions opt;
    opt.checkSceneFile = false;
    opt.checkSemantic = false;
    opt.checkRequirements = false;
    opt.checkGraphs = false;
    opt.checkAssetIndex = false;
    return opt;
}

std::vector<Diagnostic> Rows(const Scene& scene) {
    std::vector<Diagnostic> out;
    for (const Diagnostic& d : CollectProjectDiagnostics(scene, GameplayOnly()).items)
        if (d.code == kCode) out.push_back(d);
    return out;
}

Camera* AddCamera(Scene& scene, Camera::Projection p) {
    GameObject* go = scene.CreateGameObject("Main Camera");
    Camera* cam = go->AddComponent<Camera>();
    cam->main = true;
    cam->projection = p;
    return cam;
}

SpriteRenderer* AddSprite(Scene& scene, const char* name) {
    return scene.CreateGameObject(name)->AddComponent<SpriteRenderer>();
}

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

} // namespace

int main() {
    RUN_SUITE("sprite_perspective_rule");
    const bool oldConsole = Log::console;
    Log::console = false;

    // ---- a perspective main camera and two sprites: one row naming both ----------
    {
        Scene scene;
        AddCamera(scene, Camera::Projection::Perspective);
        AddSprite(scene, "Runner");
        AddSprite(scene, "Coin");
        const std::vector<Diagnostic> rows = Rows(scene);
        CHECK(rows.size() == 1);
        if (!rows.empty()) {
            const Diagnostic& d = rows[0];
            CHECK(d.severity == DiagSeverity::Warning);
            CHECK(Has(d.message, "2 sprites won't draw: the main camera is Perspective (switch it to Orthographic)"));
            CHECK(Has(d.message, "Main Camera"));
            CHECK(Has(d.message, "Runner"));
            CHECK(Has(d.message, "Coin"));
            CHECK(d.source.objectPath == "Main Camera");
            CHECK(d.source.component == "Camera");
            CHECK(!d.hint.empty());
            CHECK(d.fix.kind == DiagFixKind::MakeOrthographic);
            std::cout << "  message: " << d.message << "\n";
        }
    }

    // ---- one sprite reads "1 sprite" -----------------------------------------------
    {
        Scene scene;
        AddCamera(scene, Camera::Projection::Perspective);
        AddSprite(scene, "Only");
        const std::vector<Diagnostic> rows = Rows(scene);
        CHECK(rows.size() == 1);
        CHECK(!rows.empty() && Has(rows[0].message, "1 sprite won't draw"));
    }

    // ---- an orthographic main camera: nothing ----------------------------------------
    {
        Scene scene;
        AddCamera(scene, Camera::Projection::Orthographic);
        AddSprite(scene, "Runner");
        AddSprite(scene, "Coin");
        CHECK(Rows(scene).empty());
    }

    // ---- no sprites: nothing -----------------------------------------------------------
    {
        Scene scene;
        AddCamera(scene, Camera::Projection::Perspective);
        scene.CreateGameObject("Cube");
        CHECK(Rows(scene).empty());
    }

    // ---- no camera at all: other rules cover it, this one stays quiet --------------------
    {
        Scene scene;
        AddSprite(scene, "Runner");
        CHECK(Rows(scene).empty());
    }

    // ---- disabled sprites are not counted: they would not draw under any camera ----------
    {
        Scene scene;
        AddCamera(scene, Camera::Projection::Perspective);
        AddSprite(scene, "Shown");
        AddSprite(scene, "Off")->enabled = false;
        GameObject* hidden = scene.CreateGameObject("Hidden");
        hidden->AddComponent<SpriteRenderer>();
        hidden->active = false;
        GameObject* parent = scene.CreateGameObject("Folder");
        parent->active = false;
        GameObject* child = scene.CreateGameObject("Child");
        child->AddComponent<SpriteRenderer>();
        child->transform->SetParent(parent->transform);
        const std::vector<Diagnostic> rows = Rows(scene);
        CHECK(rows.size() == 1);
        if (!rows.empty()) {
            CHECK(Has(rows[0].message, "1 sprite won't draw"));
            CHECK(Has(rows[0].message, "Shown"));
            CHECK(!Has(rows[0].message, "Off"));
            CHECK(!Has(rows[0].message, "Hidden"));
            CHECK(!Has(rows[0].message, "Child"));
        }

        Scene allOff;
        AddCamera(allOff, Camera::Projection::Perspective);
        AddSprite(allOff, "Off")->enabled = false;
        CHECK(Rows(allOff).empty());
    }

    // ---- the camera the game renders through decides, not the first camera found ----------
    {
        Scene scene;
        GameObject* side = scene.CreateGameObject("Side Camera");
        Camera* sideCam = side->AddComponent<Camera>();   // Camera::main defaults to true
        sideCam->main = false;
        sideCam->projection = Camera::Projection::Perspective;
        AddCamera(scene, Camera::Projection::Orthographic);                             // Main
        AddSprite(scene, "Runner");
        CHECK(Rows(scene).empty());

        Scene other;
        GameObject* ortho = other.CreateGameObject("Side Camera");
        Camera* orthoCam = ortho->AddComponent<Camera>();
        orthoCam->main = false;
        orthoCam->projection = Camera::Projection::Orthographic;
        AddCamera(other, Camera::Projection::Perspective);                               // Main
        AddSprite(other, "Runner");
        const std::vector<Diagnostic> rows = Rows(other);
        CHECK(rows.size() == 1);
        CHECK(!rows.empty() && rows[0].source.objectPath == "Main Camera");
    }

    // ---- the list is capped ------------------------------------------------------------------
    {
        Scene scene;
        AddCamera(scene, Camera::Projection::Perspective);
        for (int i = 0; i < 12; ++i) AddSprite(scene, ("Tile" + std::to_string(i)).c_str());
        const std::vector<Diagnostic> rows = Rows(scene);
        CHECK(rows.size() == 1);
        if (!rows.empty()) {
            CHECK(Has(rows[0].message, "12 sprites won't draw"));
            CHECK(Has(rows[0].message, "Tile0"));
            CHECK(!Has(rows[0].message, "Tile11"));
            CHECK(Has(rows[0].message, "and 7 more"));
        }
    }

    // ---- the fix flips the projection, and the row goes away ---------------------------------
    {
        Scene scene;
        Camera* cam = AddCamera(scene, Camera::Projection::Perspective);
        AddSprite(scene, "Runner");
        AddSprite(scene, "Coin");
        const std::vector<Diagnostic> rows = Rows(scene);
        CHECK(rows.size() == 1);
        if (!rows.empty()) {
            std::string err;
            CHECK(ApplyDiagnosticFix(scene, rows[0], &err));
            CHECK(err.empty());
            CHECK(cam->projection == Camera::Projection::Orthographic);
            CHECK(Rows(scene).empty());
            // Applying it again is harmless and reports that nothing is left to do.
            CHECK(!ApplyDiagnosticFix(scene, rows[0], &err));
            CHECK(!err.empty());
        }
    }

    // ---- the shipped examples: list the ones that trigger it -----------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::path(OKAY_SOURCE_DIR) / "examples";
        std::vector<std::string> hits;
        int scanned = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != ".okayscene") continue;
            Scene scene;
            std::string err;
            if (!SceneSerializer::LoadFromFile(scene, it->path().string(), &err)) continue;
            ++scanned;
            for (const Diagnostic& d : Rows(scene))
                hits.push_back(it->path().lexically_relative(root).generic_string() + ": " + d.message);
        }
        std::sort(hits.begin(), hits.end());
        std::cout << "  scanned " << scanned << " example scenes, " << hits.size() << " trigger " << kCode << "\n";
        for (const std::string& h : hits) std::cout << "    " << h << "\n";
        CHECK(scanned > 0);   // a list, not a pass/fail: the examples are being switched to Orthographic elsewhere
    }

    Log::console = oldConsole;
    TEST_MAIN_RESULT();
}
