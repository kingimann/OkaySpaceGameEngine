// Fixes for issue #888 findings 140, 141, 142 and 131 (headless).
//  140: a particle's "Fade Out" depends only on its age, at any frame rate.
//  141: Scene Manager > Rename of the open scene moves the file and never saves the unsaved edits.
//  142: a quit request that arrives while another modal is open is not lost: Exit keeps working.
//  131: UIInputField: Backspace and Delete repeat while held, Delete deletes forward, and
//       Left / Right / Home / End move a caret that typing, Backspace and Delete use.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIInputField.hpp"
#include "EditorState.hpp"
#include "SceneRenameOps.hpp"
#include "QuitPromptState.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
using okay::editor::EditorState;
namespace fs = std::filesystem;

namespace {

std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Alpha of a single default particle (2 s life, no motion) after `seconds`, stepped at `fps`.
float FadedAlpha(float fps, float seconds, float startAlpha) {
    Scene scene;
    GameObject* go = scene.CreateGameObject("fx");
    auto* ps = go->AddComponent<ParticleSystem>();
    ps->emissionRate = 0.0f;
    ps->startLifetime = 2.0f;
    ps->startVelocity = Vec3{0.0f, 0.0f, 0.0f};
    ps->velocityRandom = 0.0f;
    ps->gravity = Vec3{0.0f, 0.0f, 0.0f};
    ps->windInfluence = 0.0f;
    ps->startColor = Color{1.0f, 0.5f, 0.25f, startAlpha};
    ps->Update(0.0f);
    ps->Emit(1);
    const float dt = 1.0f / fps;
    const int frames = (int)std::lround(seconds * fps);
    for (int i = 0; i < frames; ++i) ps->Update(dt);
    for (const auto& p : ps->Particles()) if (p.alive) return p.color.a;
    return -1.0f;
}

// One frame of keyboard input for a field: the held keys, then Update.
void Frame(UIInputField* in, const std::vector<char>& held, float dt, const std::string& typed = {}) {
    Input::ClearTypedText();
    if (!typed.empty()) Input::FeedText(typed);
    Input::FeedKeys(held);
    in->Update(dt);
}
// Press and release a key (two frames).
void Tap(UIInputField* in, char key, float dt = 1.0f / 60.0f) {
    Frame(in, {key}, dt);
    Frame(in, {}, dt);
}

}  // namespace

int main() {
    RUN_SUITE("fix_140_141_142_131");
    Log::console = false;

    // ---- 140: Fade Out is start alpha x (life / maxLife), at 30, 60 and 144 fps ----------------------------
    {
        const float ages[] = {0.25f, 0.5f, 1.0f, 1.5f};
        const float fpsList[] = {30.0f, 60.0f, 144.0f};
        for (float fps : fpsList)
            for (float age : ages) {
                const float a = FadedAlpha(fps, age, 1.0f);
                const float want = 1.0f - age / 2.0f;
                if (std::fabs(a - want) > 0.01f)
                    std::cerr << "  140: fps " << fps << " age " << age << " alpha " << a << " want " << want << "\n";
                CHECK_NEAR(a, want, 0.01f);
            }
        // A start colour below full alpha fades from that alpha, not from 1.
        CHECK_NEAR(FadedAlpha(60.0f, 1.0f, 0.8f), 0.4f, 0.01f);
        // The same age gives the same alpha at 30 and 144 fps.
        CHECK_NEAR(FadedAlpha(30.0f, 0.5f, 1.0f), FadedAlpha(144.0f, 0.5f, 1.0f), 0.005f);
    }

    // ---- 141: renaming the open scene keeps the saved file and the unsaved edits ---------------------------
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path() / "okay_fix141_test";
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "Assets", ec);
        const fs::path from = dir / "Assets" / "r3_terrain.okayscene";
        const fs::path dest = dir / "Assets" / "r3_terrain_b.okayscene";

        EditorState ed;
        ed.scene().CreateGameObject("SavedHill");
        CHECK(ed.Save(from.string()));
        const std::string saved = ReadAll(from);
        CHECK(saved.find("SavedHill") != std::string::npos);
        // An unsaved edit.
        ed.scene().CreateGameObject("UnsavedFlatten");
        ed.dirty = true;

        int saves = 0;
        oked::SceneRenameHooks hooks;
        hooks.saveOpenSceneAs = [&](const std::string& p) { ++saves; return ed.Save(p); };
        hooks.followOpenScene = [&](const std::string& p) { ed.FollowMovedFile(p); };
        std::string err;
        const bool ok = oked::RenameSceneFile(from.string(), dest.string(), true, hooks, err);
        CHECK(ok);
        CHECK(saves == 0);                                         // rename never saves
        CHECK(!fs::exists(from));                                  // the old name is gone (moved)
        CHECK(fs::exists(dest));
        const std::string moved = ReadAll(dest);
        CHECK(moved == saved);                                     // the saved version survives, byte for byte
        CHECK(moved.find("UnsavedFlatten") == std::string::npos);  // the unsaved edit was not written
        CHECK(ed.dirty);                                           // still unsaved
        CHECK(fs::path(ed.path()).filename() == dest.filename());  // the editor follows the new name
        bool liveHasEdit = false;
        for (const auto& up : ed.scene().Objects()) if (up && up->name == "UnsavedFlatten") liveHasEdit = true;
        CHECK(liveHasEdit);                                        // the edit is still in memory

        // A failed move leaves everything as it was.
        std::string err2;
        CHECK(!oked::RenameSceneFile((dir / "Assets" / "missing.okayscene").string(),
                                     (dir / "Assets" / "x.okayscene").string(), true, hooks, err2));
        CHECK(!err2.empty());
        CHECK(saves == 0);
        CHECK(fs::exists(dest));
        CHECK(fs::path(ed.path()).filename() == dest.filename());

        // A scene that is not open is renamed the same way and nothing follows it.
        const fs::path other = dir / "Assets" / "other.okayscene";
        { std::ofstream f(other, std::ios::binary); f << "other scene\n"; }   // binary: Windows text mode writes "\r\n"
        int follows = 0;
        oked::SceneRenameHooks h2;
        h2.followOpenScene = [&](const std::string&) { ++follows; };
        std::string err3;
        CHECK(oked::RenameSceneFile(other.string(), (dir / "Assets" / "other2.okayscene").string(), false, h2, err3));
        CHECK(follows == 0);
        CHECK(ReadAll(dir / "Assets" / "other2.okayscene") == "other scene\n");
        fs::remove_all(dir, ec);
    }

    // ---- 142: a quit that arrives while another modal is open still works ----------------------------------
    {
        // Normal: Exit with unsaved work opens the prompt; with none it quits at once.
        oked::QuitPromptState s;
        CHECK(oked::StepQuitPrompt(s, true, false, false, false) == oked::QuitStep::QuitNow);
        CHECK(oked::StepQuitPrompt(s, true, true, false, false) == oked::QuitStep::OpenPrompt);
        CHECK(oked::StepQuitPrompt(s, true, true, true, false) == oked::QuitStep::None);   // prompt showing
        oked::QuitPromptAnswered(s);

        // The window is closed while "Save changes?" is up. The prompt can't show on top, or that
        // modal knocks it down. Once the other modal closes, the prompt must show.
        oked::QuitPromptState q;
        bool quit = true;
        oked::QuitStep st = oked::StepQuitPrompt(q, quit, true, false, true);
        // Whatever happened this frame, the prompt is not really showing on the next frames.
        bool opened = false;
        for (int frame = 0; frame < 5 && !opened; ++frame) {
            st = oked::StepQuitPrompt(q, quit, true, false, false);   // other modal closed, prompt not up
            if (st == oked::QuitStep::OpenPrompt) opened = true;
        }
        CHECK(opened);
        if (!opened) std::cerr << "  142: the quit request is stuck; Exit and the close button do nothing now\n";

        // After Cancel, a later File > Exit opens the prompt again.
        oked::QuitPromptAnswered(q);
        CHECK(oked::StepQuitPrompt(q, true, true, false, false) == oked::QuitStep::OpenPrompt);
        oked::QuitPromptAnswered(q);

        // While another modal is up the request waits (it is not opened underneath and not dropped).
        oked::QuitPromptState w;
        CHECK(oked::StepQuitPrompt(w, true, true, false, true) == oked::QuitStep::None);
        CHECK(!w.open);
        CHECK(oked::StepQuitPrompt(w, true, true, false, false) == oked::QuitStep::OpenPrompt);
        // A quit with nothing unsaved still quits at once, even with a modal up.
        oked::QuitPromptState z;
        CHECK(oked::StepQuitPrompt(z, true, false, false, true) == oked::QuitStep::QuitNow);
    }

    // ---- 131: UIInputField key repeat, Delete and caret movement ------------------------------------------
    {
        Scene scene;
        GameObject* go = scene.CreateGameObject("field");
        auto* in = go->AddComponent<UIInputField>();
        in->focused = true;
        const float dt = 1.0f / 60.0f;
        const char BS = (char)8;

        // Hold Backspace for 1 s: it repeats.
        in->text = "hello world, this is a long line of text";
        const std::size_t before = in->text.size();
        for (int i = 0; i < 60; ++i) Frame(in, {BS}, dt);
        Frame(in, {}, dt);
        const std::size_t removed = before - in->text.size();
        CHECK(removed >= 10);
        if (removed < 10) std::cerr << "  131: holding Backspace 1 s removed " << removed << " character(s)\n";
        // A single tap removes exactly one.
        in->text = "hello world";
        Tap(in, BS);
        CHECK(in->text == "hello worl");

        // Left x5 then type X inserts at the caret.
        in->text = "hello world";
        for (int i = 0; i < 5; ++i) Tap(in, Input::KeyLeft);
        Frame(in, {}, dt, "X");
        CHECK(in->text == "hello Xworld");
        if (in->text != "hello Xworld") std::cerr << "  131: Left x5 + X gave \"" << in->text << "\"\n";
        // Delete removes the character after the caret.
        Tap(in, Input::KeyDelete);
        CHECK(in->text == "hello Xorld");
        // Backspace removes the one before it.
        Tap(in, BS);
        CHECK(in->text == "hello orld");
        // Home then Delete removes the first character; End then type appends.
        Tap(in, Input::KeyHome);
        Tap(in, Input::KeyDelete);
        CHECK(in->text == "ello orld");
        Tap(in, Input::KeyEnd);
        Frame(in, {}, dt, "!");
        CHECK(in->text == "ello orld!");
        // Right at the end does nothing; Delete at the end does nothing.
        Tap(in, Input::KeyRight);
        Tap(in, Input::KeyDelete);
        CHECK(in->text == "ello orld!");

        // Holding Delete repeats too.
        in->text = "abcdefghijklmnopqrstuvwxyz";
        Tap(in, Input::KeyHome);
        for (int i = 0; i < 60; ++i) Frame(in, {Input::KeyDelete}, dt);
        Frame(in, {}, dt);
        CHECK(in->text.size() <= 16);

        // The caret the renderer draws: the shown text before it (masked in a password field).
        in->text = "secret";
        in->caret = 3;
        CHECK(in->DisplayTextBeforeCaret() == "sec");
        in->contentType = UIInputField::ContentType::Password;
        CHECK(in->DisplayTextBeforeCaret() == "***");
        in->contentType = UIInputField::ContentType::Standard;
        // UTF-8: Left steps over a whole character, Backspace removes all of its bytes.
        in->text = "caf\xC3\xA9!";
        in->caret = -1;
        CHECK(in->MoveCaretLeft());
        CHECK(in->MoveCaretLeft());
        CHECK(in->CaretIndex() == 3);
        CHECK(in->DeleteForward());
        CHECK(in->text == "caf!");
        // Text set from code with the caret at the end keeps it at the end.
        in->text = "abc"; in->caret = -1;
        in->text = "abcdef";
        CHECK(in->CaretIndex() == 6);
        // A caret past the end (the text got shorter) is clamped.
        in->caret = 50;
        CHECK(in->CaretIndex() == in->text.size());
        // Integer field: a minus sign only goes first.
        in->contentType = UIInputField::ContentType::Integer;
        in->text = "42"; in->caret = 1;
        CHECK(!in->TypeText("-"));
        in->caret = 0;
        CHECK(in->TypeText("-"));
        CHECK(in->text == "-42");
        CHECK(in->TypeText("7"));    // the caret is after the '-' now, so a digit goes there
        CHECK(in->text == "-742");
        in->caret = 0;
        CHECK(!in->TypeText("5"));   // ...but never in front of the '-'
        in->contentType = UIInputField::ContentType::Standard;
        // Multiline: Home / End stay on the caret's line.
        in->multiline = true;
        in->text = "one\ntwo";
        in->caret = 5;               // inside "two"
        in->MoveCaretHome();
        CHECK(in->CaretIndex() == 4);
        in->caret = 1;
        in->MoveCaretEnd();
        CHECK(in->CaretIndex() == 3);
        in->multiline = false;
        // Key repeat is frame-rate independent: 1 s held at 30 and at 144 fps removes about the same.
        auto heldRemoved = [&](float fps) {
            in->text = std::string(60, 'x'); in->caret = -1;
            Frame(in, {}, 1.0f / fps);
            const int frames = (int)fps;
            for (int i = 0; i < frames; ++i) Frame(in, {BS}, 1.0f / fps);
            Frame(in, {}, 1.0f / fps);
            return 60 - (int)in->text.size();
        };
        const int r30 = heldRemoved(30.0f), r144 = heldRemoved(144.0f);
        CHECK(std::abs(r30 - r144) <= 2);
    }

    TEST_MAIN_RESULT();
}
