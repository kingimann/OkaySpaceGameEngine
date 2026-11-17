// The shipped sample scene examples/ui_library/ui_library_1.okayscene (UI library slice 1): two range sliders, a form with
// validated input boxes and a 16:9 aspect-ratio frame. This test loads the file from disk and plays it like a person would
// (drag a handle, type in the boxes) and checks the frame keeps its shape on several screens.
//
// The scene file is made by this test: run it with OKAY_WRITE_UI_LIBRARY_EXAMPLE=<path to ui_library_1.okayscene> to write it
// again (after a change to the builder or to the serializer). Without it the test only reads the committed file.
// Written for this engine; no other project's code or assets were used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace okay;

// The saved text without the random object ids, so a fresh build and the committed file can be compared.
static std::string WithoutGuids(const std::string& text) {
    std::string out, line;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            if (line.rfind("  guid ", 0) != 0) { out += line; out += '\n'; }
            line.clear();
        } else line += text[i];
    }
    return out;
}

int main() {
    RUN_SUITE("ui_library_example");
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/ui_library/ui_library_1.okayscene";

    if (const char* out = std::getenv("OKAY_WRITE_UI_LIBRARY_EXAMPLE")) {
        Scene s("UI Library 1");
        UILibrary::BuildSlice1(s);
        std::string err;
        const bool ok = SceneSerializer::SaveToFile(s, out, &err);
        std::printf("  wrote %s: %s %s\n", out, ok ? "ok" : "FAILED", err.c_str());
        CHECK(ok);
        TEST_MAIN_RESULT();
    }

    Scene s("play");
    s.physicsEnabled = false;
    std::string err;
    std::vector<std::string> warnings;
    const bool loaded = SceneSerializer::LoadFromFile(s, path, &err, &warnings);
    if (!loaded) std::printf("  could not load %s: %s\n", path.c_str(), err.c_str());
    CHECK(loaded);
    CHECK(warnings.empty());
    for (const auto& w : warnings) std::printf("  warning: %s\n", w.c_str());
    if (!loaded) TEST_MAIN_RESULT();

    // The file is what the builder makes today (a stale file would hide a change from the owner).
    {
        Scene fresh("UI Library 1");
        UILibrary::BuildSlice1(fresh);
        Scene reread("reread");
        std::string e2;
        CHECK(SceneSerializer::Deserialize(reread, SceneSerializer::Serialize(fresh), &e2, nullptr));
        CHECK(WithoutGuids(SceneSerializer::Serialize(reread)) == WithoutGuids(SceneSerializer::Serialize(s)));
    }

    GameObject* price = s.Find("Price");
    GameObject* level = s.Find("Level");
    GameObject* email = s.Find("Email");
    GameObject* age = s.Find("Age");
    GameObject* pass = s.Find("Password");
    GameObject* rep = s.Find("Repeat");
    GameObject* area = s.Find("Area");
    CHECK(price && level && email && age && pass && rep && area);
    if (!(price && level && email && age && pass && rep && area)) TEST_MAIN_RESULT();
    auto* rs = price->GetComponent<UIRangeSlider>();
    CHECK(rs && rs->low == 120 && rs->high == 380 && rs->step == 10);
    CHECK(level->GetComponent<UIRangeSlider>()->minGap == 3);

    s.Start();
    s.Update(0.016f);

    {   // all four boxes are empty and quiet at the start: nothing red yet
        for (GameObject* g : {email, age, pass, rep}) CHECK(!g->GetComponent<UIInputValidator>()->ShowsError());
    }
    {   // typing into the boxes, the way the game does it: the text event reaches a focused field
        auto type = [&](GameObject* g, const char* text) {
            auto* in = g->GetComponent<UIInputField>();
            in->focused = true;
            Input::FeedText(text);
            s.Update(0.016f);
            Input::ClearTypedText();
            in->focused = false;
            s.Update(0.016f);
        };
        type(email, "ada@");
        CHECK(email->GetComponent<UIInputValidator>()->ShowsError());
        type(email, "lovelace.org");
        CHECK(email->GetComponent<UIInputValidator>()->IsValid());
        type(age, "7");
        CHECK(age->GetComponent<UIInputValidator>()->Error() == "Between 13 and 120");
        age->GetComponent<UIInputField>()->text = "36";
        s.Update(0.016f);
        CHECK(age->GetComponent<UIInputValidator>()->IsValid());
        type(pass, "swordfish");
        type(rep, "swordfish");
        CHECK(rep->GetComponent<UIInputValidator>()->IsValid());
        rep->GetComponent<UIInputField>()->text = "swordfisH";
        s.Update(0.016f);
        CHECK(rep->GetComponent<UIInputValidator>()->Error() == "That is not the same password");
    }
    {   // dragging the price slider with the mouse. This headless test has no window host, so the pointer is read in the
        // widget's own frame (the slider sits at 20,96 inside its card): the same maths the window host feeds it.
        UICanvas::Set(1280.0f, 720.0f);
        const Vec2 po = rs->position, ps = rs->size;
        const float y = po.y + ps.y * 0.5f;
        Input::FeedMouse({po.x + ps.x * 0.76f, y}, 0);
        Input::FeedMouse({po.x + ps.x * 0.76f, y}, 1u << 0);
        s.Update(0.016f);
        CHECK(rs->ActiveHandle() == 1);
        Input::FeedMouse({po.x + ps.x * 0.95f, y}, 1u << 0);
        s.Update(0.016f);
        CHECK(rs->high >= 450.0f && rs->high <= 500.0f);
        CHECK(rs->low == 120);
        Input::FeedMouse({po.x + ps.x * 0.95f, y}, 0);
        s.Update(0.016f);
        Input::FeedMouse({0, 0}, 0);
    }
    {   // the aspect frame: 16:9 on a phone, a tablet and an ultrawide screen
        GameObject* frame = s.Find("Frame");
        CHECK(frame != nullptr);
        const float screens[][2] = {{1280, 720}, {1280, 900}, {720, 1280}, {2560, 1080}};
        for (const auto& sc : screens) {
            Vec2 o, sz;
            CHECK(GetUIScreenRect(frame, sc[0], sc[1], o, sz));
            CHECK_NEAR(sz.x / sz.y, 16.0f / 9.0f, 0.01);
            CHECK(sz.x > 20.0f && sz.y > 20.0f);
        }
    }
    TEST_MAIN_RESULT();
}
