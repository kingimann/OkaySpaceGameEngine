// Window mode: the error modal and the "Save changes?" question as OkayUI popups (editor/OkayUIHostDialogs.cpp, the OkayUI implementation of
// oked::DialogService), run through the real frame loop with a fake backend and a software painter. Docs 6.24 step 7.
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostDialogs.hpp"
#include "FakePlatformBackend.hpp"

#include <cstdlib>
#include <string>
#include <vector>

namespace UI = OkayUI;
namespace plat = okay::platform;
using fakeplat::FakeBackend; using fakeplat::Move; using fakeplat::Btn;

int main() {
    RUN_SUITE("okayui window mode: dialogs without Dear ImGui popups");
    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(900, 600);
    FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
    oked::wm::OkayUIDialogs d;
    oked::SetDialogs(&d);
    CHECK(oked::CurrentDialogs() == &d);
    std::vector<std::pair<std::string, int>> logged; std::vector<std::pair<std::string, int>> opened;
    d.log = [&](const std::string& l, int lv) { logged.emplace_back(l, lv); };
    d.openScript = [&](const std::string& p, int l) { opened.emplace_back(p, l); };
    bool unsaved = false, prefab = false, playing = false, saveOk = true; int saves = 0, stops = 0; std::string saveErr = "disk full";
    d.ctx.inPrefabMode = [&] { return prefab; };
    d.ctx.hasUnsavedChanges = [&] { return unsaved; };
    d.ctx.sceneLabel = [] { return std::string("level1.okayscene"); };
    d.ctx.isPlaying = [&] { return playing; };
    d.ctx.stopPlay = [&] { ++stops; };
    d.ctx.save = [&](std::string& err) { ++saves; if (!saveOk) err = saveErr; return saveOk; };
    auto frame = [&] { be.now += 16; return loop.Frame([&] { d.Draw(); }); };
    auto click = [&](const UI::Rect& r) {
        const int x = (int)(r.x + r.w * 0.5f), y = (int)(r.y + r.h * 0.5f);
        be.queue.push_back(Move(x, y)); frame(); be.queue.push_back(Btn(true, x, y)); frame(); be.queue.push_back(Btn(false, x, y)); frame(); frame();
    };
    auto key = [&](plat::Key k) {
        plat::Event e; e.type = plat::EventType::KeyDown; e.key = k; be.queue.push_back(e);
        plat::Event u; u.type = plat::EventType::KeyUp; u.key = k; be.queue.push_back(u);
        frame(); frame(); frame();
    };
    for (int i = 0; i < 3; ++i) frame();
    CHECK(!d.ErrorOpen() && !d.PromptOpen());

    // ---- the error modal ----
    d.ShowError("Couldn't open the scene", "\"a.okayscene\": no such file");
    CHECK(d.ErrorOpen() && logged.size() == 1 && logged[0].second == 2 && logged[0].first.find("no such file") != std::string::npos);
    for (int i = 0; i < 4; ++i) frame();
    CHECK(d.GetProbe().errorOk.w > 50.0f && d.GetProbe().errorTitle == "Couldn't open the scene");
    const UI::DrawData dd = UI::GetDrawData();
    CHECK(dd.vertexCount > 300 && dd.dropped == 0);
    {   // the modal is centred in the window
        const UI::Rect ok = d.GetProbe().errorOk;
        CHECK(ok.x > 150.0f && ok.x < 400.0f && ok.y > 300.0f && ok.y < 420.0f);
        if (const char* shot = std::getenv("OKAYUI_SHOT")) { std::vector<std::uint8_t> px; int w = 0, h = 0; if (painter->ReadPixels(px, w, h)) okay::ImageWrite::WritePNG(shot, w, h, 4, px.data()); }
    }
    click(d.GetProbe().errorOk);
    CHECK(!d.ErrorOpen());
    d.ShowError("Again", "second"); for (int i = 0; i < 3; ++i) frame();
    CHECK(d.ErrorOpen());
    key(plat::Key::Escape);
    CHECK(!d.ErrorOpen());

    // ---- the save question ----
    int ran = 0, cancelled = 0;
    d.RunAfterSavePrompt("opening another scene", [&] { ++ran; }, [&] { ++cancelled; });
    CHECK(ran == 1 && !d.PromptOpen());                      // nothing unsaved: runs at once
    unsaved = true;
    d.RunAfterSavePrompt("opening another scene", [&] { ++ran; }, [&] { ++cancelled; });
    CHECK(ran == 1 && d.PromptOpen());
    for (int i = 0; i < 4; ++i) frame();
    CHECK(d.GetProbe().promptText == "Save changes to \"level1.okayscene\" before opening another scene?" && d.GetProbe().save.w > 50.0f);
    int ran2 = 0, cancelled2 = 0;
    d.RunAfterSavePrompt("something else", [&] { ++ran2; }, [&] { ++cancelled2; });   // one question at a time: the second is cancelled
    CHECK(ran2 == 0 && cancelled2 == 1 && d.PromptOpen());
    // Cancel
    click(d.GetProbe().cancel);
    CHECK(!d.PromptOpen() && ran == 1 && cancelled == 1 && saves == 0);
    // Don't Save runs the action without saving
    d.RunAfterSavePrompt("closing", [&] { ++ran; }, [&] { ++cancelled; });
    for (int i = 0; i < 4; ++i) frame();
    click(d.GetProbe().dontSave);
    CHECK(!d.PromptOpen() && ran == 2 && saves == 0);
    // Save that fails keeps the question open and says why; then it works
    saveOk = false; playing = true;
    d.RunAfterSavePrompt("closing", [&] { ++ran; }, [&] { ++cancelled; });
    for (int i = 0; i < 4; ++i) frame();
    click(d.GetProbe().save);
    CHECK(d.PromptOpen() && saves == 1 && stops == 1 && ran == 2);
    for (int i = 0; i < 2; ++i) frame();
    CHECK(d.GetProbe().promptError == "disk full");
    saveOk = true;
    click(d.GetProbe().save);
    CHECK(!d.PromptOpen() && saves == 2 && ran == 3);
    // Escape cancels
    playing = false;
    d.RunAfterSavePrompt("closing", [&] { ++ran; }, [&] { ++cancelled; });
    for (int i = 0; i < 4; ++i) frame();
    key(plat::Key::Escape);
    CHECK(!d.PromptOpen() && cancelled == 2 && ran == 3);
    // a prefab open: refused with the reason, the action does not run, onCancel does
    prefab = true;
    d.RunAfterSavePrompt("starting a new scene", [&] { ++ran; }, [&] { ++cancelled; });
    CHECK(ran == 3 && cancelled == 3 && d.ErrorOpen());
    for (int i = 0; i < 3; ++i) frame();
    CHECK(d.GetProbe().errorTitle == "Go back to the scene first");
    click(d.GetProbe().errorOk);
    prefab = false;
    CHECK(!d.ErrorOpen());
    d.RunAfterSavePrompt("x", {}, {});   // no action: nothing happens
    CHECK(!d.PromptOpen());

    // ---- open a script ----
    d.OpenScriptAt("Assets/Scripts/Player.cs", 12); d.OpenScriptAt("a.cs", 0);
    CHECK(opened.size() == 2 && opened[0].second == 12 && opened[1].second == 1);
    d.openScript = nullptr; const size_t before = logged.size();
    d.OpenScriptAt("b.cs", 3);
    CHECK(logged.size() == before + 1 && logged.back().second == 1);

    oked::SetDialogs(nullptr);
    CHECK(oked::CurrentDialogs() == nullptr);
    TEST_MAIN_RESULT();
}
