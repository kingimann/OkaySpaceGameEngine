#pragma once
// Runs an exported OkaySpace game inside the Xbox app: loads package/Data, builds the scene, steps the engine's own GameLoop
// (scene update, scripts, fixed-step physics, animation, UI logic) and hands the shell what to draw. Needs the engine
// (okay_engine_uwp on the console). UNVERIFIED: this file is only syntax-checked on Linux (g++ -fsyntax-only against the engine headers);
// it has not been linked or run, and never ran on Windows. The settings parser it uses (XboxGameConfig.hpp) IS tested on Linux.
//
// What runs: scene loading from Data/, the game config, EnsureSceneCamera, Scene::Update through GameLoop (fixed-step physics, scripts,
// animation, UI behaviours), gamepad and keyboard input through the platform layer (PadSet over the UWP backend). Drawing: the 3D view
// through the shell's D3D11Renderer present path (XboxPresent.hpp). NOT drawn yet: the 2D / UI layer (UICanvas elements), because the
// software painter has no UWP present path wired. Audio: the mixer is not started by this file (the shell opens a platform AudioDevice).
#include "XboxGameConfig.hpp"
#include "XboxPaths.hpp"
#include "XboxRecovery.hpp"
#include "XboxTitleSafe.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Components/UISafeArea.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIWorldContext.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include "okay/Input/PlatformKeys.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace okay::xbox {

/// The engine's key characters for the keys held right now (letters by the layout, digits, space, modifiers, arrows as themselves and as
/// WASD, editing keys, F1-F12 and the navigation keys). Written for this file from Input.hpp's key constants; the player does the same
/// from its own loop.
inline std::vector<char> KeysDown(platform::Backend& pb) {
    using PK = platform::Key;
    std::vector<char> d;
    for (char c = 'a'; c <= 'z'; ++c) if (pb.IsKeyDown(KeyForLetter(pb, c))) d.push_back(c);
    for (char c = '0'; c <= '9'; ++c) if (pb.IsKeyDown(KeyForLetter(pb, c))) d.push_back(c);
    if (pb.IsKeyDown(PK::Space)) d.push_back(' ');
    if (pb.IsKeyDown(PK::LShift) || pb.IsKeyDown(PK::RShift)) d.push_back(Input::KeyShift);
    if (pb.IsKeyDown(PK::LCtrl) || pb.IsKeyDown(PK::RCtrl)) d.push_back(Input::KeyCtrl);
    if (pb.IsKeyDown(PK::Backspace)) d.push_back((char)8);
    if (pb.IsKeyDown(PK::Return) || pb.IsKeyDown(PK::KpEnter)) d.push_back('\r');
    if (pb.IsKeyDown(PK::Escape)) d.push_back((char)27);
    if (pb.IsKeyDown(PK::Up)) { d.push_back('w'); d.push_back(Input::KeyUp); }
    if (pb.IsKeyDown(PK::Left)) { d.push_back('a'); d.push_back(Input::KeyLeft); }
    if (pb.IsKeyDown(PK::Down)) { d.push_back('s'); d.push_back(Input::KeyDown); }
    if (pb.IsKeyDown(PK::Right)) { d.push_back('d'); d.push_back(Input::KeyRight); }
    AppendExtendedKeys(pb, d);
    return d;
}

class XboxGameRuntime {
public:
    /// `dataDir`: the Data folder in the package (read-only), with a trailing slash or none.
    bool Start(const std::string& dataDir, std::string& err) {
        namespace fs = std::filesystem;
        dir_ = Slash(dataDir); if (!dir_.empty() && dir_.back() != '/') dir_ += '/';
        std::string text;
        { std::ifstream f(fs::u8path(dir_ + "game.okayconfig"), std::ios::binary);
          if (!f) { err = "Data/game.okayconfig not found: is this an exported game? (" + dir_ + ")"; return false; }
          std::stringstream ss; ss << f.rdbuf(); bool ok = true; std::string perr;
          text = DataPack::Unpack(ss.str(), &ok, &perr);
          if (!ok) { err = "Data/game.okayconfig could not be read: " + perr; return false; } }
        cfg_ = ParseGameConfig(text);
        const std::string rel = ResolveStartupScene(cfg_, [&](const std::string& r) { std::error_code ec; return fs::is_regular_file(fs::u8path(dir_ + r), ec); });
        if (rel.empty()) { err = "No scene to start: Data/ has no game.okayscene and the config's startup scene is missing."; return false; }
        SceneManager::ClearScenes();
        for (const std::string& s : cfg_.scenes) SceneManager::AddScene(dir_ + s);
        if (cfg_.sawPhysics) Scene::SetProjectPhysicsRate(cfg_.physicsHz, cfg_.smoothPhysics);
        scene_.reset(new Scene("Game"));
        scene_->UseFixedStepPhysics(true);
        std::string serr;
        if (!SceneSerializer::LoadFromFile(*scene_, dir_ + rel, &serr)) { err = "Could not load " + rel + ": " + serr; return false; }
        bool made = false; EnsureSceneCamera(*scene_, SceneLooks2D(*scene_), &made);
        GameLoop::Options o; o.logHitches = cfg_.development;
        loop_.reset(new GameLoop(o));
        loop_->OnUpdate = [this](float dt) { if (scene_) scene_->Update(dt); };
        scene_->Start();
        pads_.OpenAll();
        started_ = true;
        RestoreIfPending();
        Log::Info("[xbox] started '", cfg_.title, "' scene ", rel);
        return true;
    }
    bool Started() const { return started_; }
    const GameConfig& Config() const { return cfg_; }
    Scene* GetScene() { return scene_.get(); }

    /// One frame: feed the pads and keys, step the loop. `keysDown` = platform key state the shell collected as engine key chars.
    /// Returns false when the game asked to quit.
    /// The size (pixels) the UI is laid out and drawn for: anchored widgets and their hit-tests use it. 0 x 0 = UI size not set yet.
    void SetUiSize(int w, int h) { uiW_ = w; uiH_ = h; }
    /// Mouse position in UI pixels and the button mask (bit 0 = left), once per frame.
    void FeedMouse(float x, float y, unsigned mask) { Input::FeedMouse(Vec2{x, y}, mask); }
    bool Frame(double realDeltaSeconds, std::vector<char>& keysDown) {
        if (!started_) return false;
        pads_.Feed(keysDown);
        Input::FeedKeys(keysDown);
        if (uiW_ > 0 && uiH_ > 0 && scene_) PrepareUi(*scene_, uiW_, uiH_);
        const bool keepGoing = loop_->BeginFrame(realDeltaSeconds);
        // Controller-only menus: the d-pad (and arrows / WASD) move the focus between buttons, toggles, sliders, tabs...; A (Enter,
        // Space) presses. The same call the desktop player makes each frame; before 2026-11-22 the Xbox app never made it, so a menu
        // could only be used with a mouse, which a console does not have.
        if (scene_) NavigateUI(*scene_);
        return keepGoing;
    }
    /// A back request from the system (B on a console pad). While the game is paused it is an Escape tap, which closes a pause menu
    /// (PauseMenu's toggle key). During play it does nothing here: B reaches the game as an ordinary pad button anyway, and B must not
    /// open a menu or quit the game.
    void BackPressed() { if (Game::Paused()) Input::FeedKeyTap((char)27); }
    /// A key went down this frame (from the KeyDown event): a tap shorter than one frame still counts (Input::FeedKeyTap), as in the player.
    void KeyTapped(platform::Backend& pb, platform::Key k) {
        std::vector<char> tapped;
        TapCharsForKey(k, [&](char c) { return KeyForLetter(pb, c); }, tapped);
        for (char c : tapped) Input::FeedKeyTap(c);
    }
    /// Where the UI must stay on a TV (XboxTitleSafe.hpp): fills okay::UISafeInsets(), which UI Safe Area containers obey.
    static void SetTitleSafe(const SafeInsets& in) {
        const SafeInsets i = Clamped(in);
        UISafeInsetFractions& f = UISafeInsets();
        f.left = i.left; f.top = i.top; f.right = i.right; f.bottom = i.bottom;
    }
    void PadAdded(int deviceIndex) { pads_.Add(deviceIndex); }
    void PadRemoved(int instanceId) { pads_.Remove(instanceId); }
    /// The picture the shell should draw: the main camera's view-projection and eye.
    bool View(int w, int h, Mat4& vp, Vec3& eye) const {
        if (!scene_ || !scene_->mainCamera || w < 1 || h < 1) return false;
        Camera* cam = scene_->mainCamera;
        vp = cam->ProjectionMatrix((float)w / (float)h) * cam->ViewMatrix();
        eye = cam->gameObject && cam->gameObject->transform ? cam->gameObject->transform->Position() : Vec3{0, 0, 0};
        return true;
    }
    /// Where the suspend save and its marker go (ApplicationData.LocalFolder\saves, from XboxPaths). Set before Start().
    void SetSaveDir(const std::string& dir) { saveDir_ = dir; }
    /// Suspend: capture the scene with the engine's save framework (objects marked Saveable: transforms, components that save, inventories, stats...)
    /// into slot "xbox_suspend" in the save folder, then write a marker file. It runs before the system's suspend deadline (the shell calls it
    /// synchronously). Objects without a Saveable component are NOT saved: that is the framework's rule, a game must mark what matters.
    void OnSuspend() {
        Log::Info("[xbox] suspending");
        if (!scene_ || saveDir_.empty()) return;
        namespace fs = std::filesystem; std::error_code ec; fs::create_directories(fs::u8path(saveDir_), ec);
        SaveStore store(saveDir_);
        SaveDocument doc = SaveSystem::CaptureScene(*scene_);
        SaveSlotInfo meta; meta.slot = "xbox_suspend"; meta.label = "Saved when the app was suspended"; meta.scene = scene_->Name();
        std::string err;
        if (!store.Write("xbox_suspend", doc, meta, &err)) { Log::Warning("[xbox] suspend save failed: ", err); return; }
        std::ofstream(fs::u8path(saveDir_ + "/xbox_suspend.marker"), std::ios::binary) << "1";
        Log::Info("[xbox] suspend state saved");
    }
    /// Resume (the process was kept): the saved state is not needed any more, drop the marker. The scene never stopped existing, so nothing is reloaded.
    void OnResume() {
        Log::Info("[xbox] resumed");
        std::error_code ec; if (!saveDir_.empty()) std::filesystem::remove(std::filesystem::u8path(saveDir_ + "/xbox_suspend.marker"), ec);
    }
    /// Clean exit: also drop the marker (an exit is not a suspended kill).
    void OnExit() { OnResume(); }

private:
    // At start: if the marker is still there the last run ended while suspended; put the saved state back over the freshly loaded scene.
    void RestoreIfPending() {
        if (saveDir_.empty() || !scene_) return;
        namespace fs = std::filesystem; std::error_code ec;
        const bool marker = fs::exists(fs::u8path(saveDir_ + "/xbox_suspend.marker"), ec);
        SaveStore store(saveDir_);
        const StartAction a = DecideStart(marker, marker && store.Exists("xbox_suspend"));
        if (a == StartAction::Fresh) return;
        fs::remove(fs::u8path(saveDir_ + "/xbox_suspend.marker"), ec);
        if (a == StartAction::DropStaleMarker) return;
        SaveDocument doc; std::string err;
        if (!store.Read("xbox_suspend", doc, nullptr, &err)) { Log::Warning("[xbox] could not read the suspend save: ", err); return; }
        const SaveApplyReport rep = SaveSystem::ApplyScene(*scene_, doc);
        Log::Info("[xbox] restored the suspended state: ", rep.restored, " objects restored, ", rep.spawned, " spawned");
    }
    std::string saveDir_;
    // Publish the UI size and the hit-test hooks before the scene updates, so anchored widgets and their clicks agree with what RenderSceneUI draws
    // (the same things the player sets up each frame: canvas size, where a widget really is, world-space projection), then lay the UI out.
    static void PrepareUi(Scene& scene, int cw, int ch) {
        UICanvas::Set((float)cw, (float)ch);
        UIWorld().active = false;
        UIWorld().rectOf = [cw, ch](GameObject* go, Vec2& o, Vec2& sz) { if (UIHidden(go)) return false; return GetUIScreenRect(go, (float)cw, (float)ch, o, sz); };
        UIWorld().clipContains = [cw, ch](GameObject* go, const Vec2& p) { return UIClipHitTest(go, p, (float)cw, (float)ch); };
        if (Camera* cam = scene.mainCamera) {
            UIWorld().active = true; UIWorld().screenW = (float)cw; UIWorld().screenH = (float)ch;
            if (cam->gameObject && cam->gameObject->transform) UIWorld().right = cam->gameObject->transform->Right();
            UIWorld().project = [cam, cw, ch](const Vec3& p, Vec2& out, float& depth) { return cam->WorldToScreen(p, (float)cw, (float)ch, out, &depth); };
        }
        UpdateUILayouts(scene);
    }
    int uiW_ = 0, uiH_ = 0;
    std::string dir_; GameConfig cfg_; std::unique_ptr<Scene> scene_; std::unique_ptr<GameLoop> loop_; PadSet pads_; bool started_ = false;
};

} // namespace okay::xbox
