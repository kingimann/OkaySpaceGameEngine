#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Core/ScriptProtect.hpp>

using namespace okay;
using namespace okay::scriptprotect;

int main() {
    RUN_SUITE("script_protect");

    // Toggle off: byte-for-byte unaffected.
    {
        const std::string src = "// hi\non_menu() { x = 1 }\n";
        Options off;
        CHECK(ProtectSource(src, off) == src);
    }

    // Line comments stripped; code kept.
    {
        const std::string src = "x = 1 // set x\ny = 2\n";
        const std::string stripped = StripComments(src);
        CHECK(stripped.find("set x") == std::string::npos);
        CHECK(stripped.find("x = 1") != std::string::npos);
        CHECK(stripped.find("y = 2") != std::string::npos);
    }

    // Block comments stripped, including multi-line.
    {
        const std::string src = "a = 1\n/* this\nspans lines */\nb = 2\n";
        const std::string stripped = StripComments(src);
        CHECK(stripped.find("spans lines") == std::string::npos);
        CHECK(stripped.find("a = 1") != std::string::npos);
        CHECK(stripped.find("b = 2") != std::string::npos);
    }

    // A comment-looking sequence inside a string literal is NOT a comment.
    {
        const std::string src = "s = \"not // a comment\"\nt = \"also not /* a comment */ here\"\n";
        const std::string stripped = StripComments(src);
        CHECK(stripped.find("not // a comment") != std::string::npos);
        CHECK(stripped.find("also not /* a comment */ here") != std::string::npos);
    }

    // Interpolated string with an expression hole: the hole's own strings and braces are
    // still correctly skipped, so a '"' inside the hole doesn't end the outer string early.
    {
        const std::string src = "s = $\"hi {name} // not a comment\"\nkept = 1\n";
        const std::string stripped = StripComments(src);
        CHECK(stripped.find("not a comment") != std::string::npos);
        CHECK(stripped.find("kept = 1") != std::string::npos);
    }

    // Whitespace collapse: blank/comment-only lines vanish, trailing whitespace trimmed,
    // real lines keep their content and relative order.
    {
        const std::string src = "a = 1   \n\n\n   \nb = 2\n";
        const std::string collapsed = CollapseWhitespace(src);
        CHECK(collapsed == "a = 1\nb = 2\n");
    }

    // The toggle's full pass still runs identically to the original: same outputs for
    // the same inputs, loaded through the real OkayScript VM (ScriptComponent), which is
    // the existing script test infrastructure this task asks to reuse.
    {
        const std::string src =
            "// A gameplay script.\n"
            "var changed = 0;\n"
            "function start() {\n"
            "    /* tally a few steps */\n"
            "    for (i = 0; i < 3; i = i + 1) {\n"
            "        changed = changed + 1; // one per step\n"
            "    }\n"
            "}\n";
        Options on; on.protectScriptSource = true;
        const std::string protectedSrc = ProtectSource(src, on);
        CHECK(protectedSrc.find("A gameplay script") == std::string::npos);
        CHECK(protectedSrc.find("one per step") == std::string::npos);
        CHECK(protectedSrc.find("tally a few steps") == std::string::npos);
        CHECK(protectedSrc.find("function start") != std::string::npos);   // hook name untouched

        Scene sceneBefore("B");
        auto* goBefore = sceneBefore.CreateGameObject("Hero");
        auto* scBefore = goBefore->AddComponent<ScriptComponent>();
        CHECK(scBefore->LoadSource(src));
        sceneBefore.Start();
        sceneBefore.Update(0.016f);

        Scene sceneAfter("A");
        auto* goAfter = sceneAfter.CreateGameObject("Hero");
        auto* scAfter = goAfter->AddComponent<ScriptComponent>();
        CHECK(scAfter->LoadSource(protectedSrc));
        sceneAfter.Start();
        sceneAfter.Update(0.016f);

        CHECK(scBefore->VM()->GetGlobal("changed").AsFloat() == scAfter->VM()->GetGlobal("changed").AsFloat());
        CHECK(scAfter->VM()->GetGlobal("changed").AsFloat() == 3.0f);
    }

    TEST_MAIN_RESULT();
}
