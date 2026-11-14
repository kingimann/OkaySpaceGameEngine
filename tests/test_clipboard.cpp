// Clipboard text: the memory store, a stand-in clipboard, input-field copy and
// paste, and the text-script and visual-script functions.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "VisualScriptCategories.hpp"
#include "VisualScriptFriendly.hpp"
#include "VisualScriptNodeHelp.hpp"
#include <Okay.hpp>
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::vsed;

namespace {

struct FakeClipboard final : Clipboard {
    std::string stored;
    bool accept = true;
    std::string GetText() const override { return stored; }
    bool SetText(const std::string& text) override {
        stored = text;
        return accept;
    }
};

void ReleaseKeys() { Input::FeedKeys({}); }

void Tap(Scene& s, const std::vector<char>& keys) {
    Input::ClearTypedText();
    ReleaseKeys();
    s.Update(0.016f);
    Input::FeedKeys(keys);
    s.Update(0.016f);
}

} // namespace

int main() {
    RUN_SUITE("clipboard");
    okaytest::Sandbox sandbox("clipboard", OKAY_SOURCE_DIR);
    MemoryClipboard box;
    InstallGameClipboard(&box);

    // The cleaner drops controls, keeps tab and newline, repairs a broken
    // sequence by skipping it, and stops at the character cap.
    {
        const std::string dirty = std::string("a\x01" "b\t\nc\r") + "\xff" "\xfe" "ok";
        CHECK(SanitizeClipboardText(dirty) == "ab\t\ncok");
        std::string longText(kMaxClipboardChars + 40, 'z');
        CHECK(SanitizeClipboardText(longText).size() == kMaxClipboardChars);
        std::string huge(kMaxClipboardBytes + 100, 'q');
        CHECK(SanitizeClipboardText(huge).size() == kMaxClipboardChars);
        CHECK(SanitizeClipboardText(std::string("h\xc3\xa9llo")) == "h\xc3\xa9llo");
        // U+0080 is a control. U+FFFD typed on purpose stays.
        CHECK(SanitizeClipboardText(std::string("x\xc2\x80y\xef\xbf\xbdz")) == "xy\xef\xbf\xbdz");
    }

    // The public get/set always sanitizes, including text a stand-in returns raw.
    {
        CHECK(SetClipboardText("keep\x7fme"));
        CHECK(GetClipboardText() == "keepme");
        FakeClipboard fake;
        fake.stored = std::string("no\x02pe");
        InstallGameClipboard(&fake);
        CHECK(GetClipboardText() == "nope");
        fake.accept = false;
        CHECK(!SetClipboardText(std::string("a\x01" "b")));
        CHECK(fake.stored == "ab");
        InstallGameClipboard(&box);
        CHECK(GetClipboardText() == "keepme");
    }

    // A focused field copies, cuts and pastes. An unfocused one ignores the keys.
    {
        SetClipboardText("");
        Scene s("Field");
        s.physicsEnabled = false;
        UICanvas::Set(1280, 720);
        auto* field = s.CreateGameObject("Name")->AddComponent<UIInputField>();
        field->anchor = UIAnchor::TopLeft;
        field->position = {0, 0};
        field->size = {200, 40};
        field->maxLength = 8;
        field->focused = true;
        s.Start();

        field->text = "Hi";
        SetClipboardText(std::string("!\x01\nX"));
        Tap(s, {Input::KeyCtrl, 'v'});
        CHECK(field->text == "Hi!X");   // newline and the control are not inserted on one line

        field->multiline = true;
        field->text.clear();
        SetClipboardText("a\nb\x03");
        Tap(s, {Input::KeyCmd, 'v'});
        CHECK(field->text == "a\nb");

        Tap(s, {Input::KeyCtrl, 'c'});
        CHECK(GetClipboardText() == "a\nb");

        Tap(s, {Input::KeyCtrl, 'x'});
        CHECK(field->text.empty());
        CHECK(GetClipboardText() == "a\nb");

        field->text = "stay";
        Tap(s, {Input::KeyCtrl, 'a'});   // no selection, so this is not select-all
        CHECK(field->text == "stay");

        field->focused = false;
        SetClipboardText("no");
        Tap(s, {Input::KeyCtrl, 'v'});
        CHECK(field->text == "stay");

        field->focused = true;
        field->contentType = UIInputField::ContentType::Integer;
        field->text.clear();
        field->maxLength = 8;
        SetClipboardText("4a2!");
        Tap(s, {Input::KeyCtrl, 'v'});
        CHECK(field->text == "42");

        field->contentType = UIInputField::ContentType::Password;
        field->text = "secret";
        SetClipboardText("marker");
        Tap(s, {Input::KeyCtrl, 'c'});
        CHECK(GetClipboardText() == "marker");
        Tap(s, {Input::KeyCmd, 'x'});
        CHECK(field->text == "secret");
        CHECK(GetClipboardText() == "marker");
        ReleaseKeys();
    }

    // Text script and the matching visual-script nodes.
    {
        SetClipboardText("");
        OkayScriptVM direct;
        std::string err;
        const auto set = direct.CallBuiltin("set_clipboard_text", {vs::VsValue(std::string("ok\x01\n!"))}, &err);
        CHECK(err.empty());
        CHECK(set.AsFloat() == 1.0f);
        const auto got = direct.CallBuiltin("get_clipboard_text", {}, &err);
        CHECK(got.AsString() == "ok\n!");

        Scene s("Script");
        s.physicsEnabled = false;
        GameObject* host = s.CreateGameObject("Host");
        auto* sc = host->AddComponent<ScriptComponent>();
        CHECK(sc->LoadSource(
            "function go() { set_clipboard_text(\"Paste me\"); return get_clipboard_text(); }\n", &err));
        if (!err.empty()) std::printf("  script: %s\n", err.c_str());
        s.Start();
        const auto returned = sc->VM()->CallEventArgs("go", {}, &err);
        CHECK(err.empty());
        CHECK(returned.AsString() == "Paste me");
        CHECK(GetClipboardText() == "Paste me");

        CHECK(vs::IsBuiltinNode("Fn.get_clipboard_text"));
        CHECK(vs::IsBuiltinNode("Fn.set_clipboard_text"));
        CHECK(VsFriendlyBuiltinTitleString("get_clipboard_text") == "Get Clipboard Text");
        CHECK(VsFriendlyBuiltinTitleString("set_clipboard_text") == "Set Clipboard Text");
        CHECK(LintVsTitle("Get Clipboard Text", VsNameKind::Reading).empty());
        CHECK(LintVsTitle("Set Clipboard Text", VsNameKind::Action).empty());
        const char* getHelp = VsNodeHelp("Fn.get_clipboard_text");
        const char* setHelp = VsNodeHelp("Fn.set_clipboard_text");
        CHECK(getHelp && std::strstr(getHelp, "clipboard") && std::strchr(getHelp, '\n') == nullptr);
        CHECK(setHelp && std::strstr(setHelp, "clipboard") && std::strchr(setHelp, '\n') == nullptr);
        CHECK(std::strcmp(VsPlaceForBuiltin("get_clipboard_text").category, "Text") == 0);
        CHECK(std::strcmp(VsPlaceForBuiltin("set_clipboard_text").category, "Text") == 0);

        auto* vc = host->AddComponent<VisualScriptComponent>();
        const char* graph =
            "node 1 OnUpdate\n"
            "entry OnUpdate 1\n"
            "node 2 Fn.set_clipboard_text\n"
            "default 2 0 \"fromgraph\"\n"
            "exec 1 0 2\n"
            "node 3 Fn.get_clipboard_text\n"
            "node 4 SetVar copied\n"
            "exec 2 0 3\n"
            "exec 3 0 4\n"
            "data 4 0 3 0\n";
        CHECK(vc->LoadFromText(graph, &err));
        if (!err.empty()) std::printf("  graph: %s\n", err.c_str());
        s.Update(1.0f / 60.0f);
        CHECK(GetClipboardText() == "fromgraph");
        CHECK(vc->GetVariable("copied").AsString() == "fromgraph");
    }

    InstallGameClipboard(nullptr);
    CHECK(sandbox.Verify());
    TEST_MAIN_RESULT();
}
