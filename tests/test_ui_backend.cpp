// The UI backend switch (shared/OkayUIBackend.hpp): parsing, persisting, precedence, and that Dear ImGui is the default.
// This is the logic the launcher (launcher.cfg `ui_backend=`), the editor (okay_settings.txt `uibackend`) and the OKAY_UI_BACKEND
// environment variable all go through.
#include "test_framework.hpp"
#include "OkayUIBackend.hpp"

#include <sstream>

using namespace okayuibackend;

// The launcher's persistence as the app does it: a launcher.cfg line out, a launcher.cfg line in.
static std::string LauncherSave(const std::string& setting) {
    std::ostringstream f;
    if (!setting.empty()) f << "ui_backend=" << setting << "\n";
    return f.str();
}
static std::string LauncherLoad(const std::string& cfg) {
    std::istringstream f(cfg);
    std::string line, setting;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.substr(0, eq) == "ui_backend") setting = Normalize(line.substr(eq + 1));
    }
    return setting;
}
// The editor's: `uibackend 1`, read with `f >> key >> value`.
static std::string EditorSave(const std::string& setting) {
    std::ostringstream f;
    f << "settingsver 3\n";
    if (!setting.empty()) f << "uibackend " << ToSettingInt(setting) << "\n";
    return f.str();
}
static std::string EditorLoad(const std::string& text) {
    std::istringstream f(text);
    std::string k; int v; std::string setting;
    while (f >> k >> v) if (k == "uibackend") setting = FromSettingInt(v);
    return setting;
}

int main() {
    RUN_SUITE("ui backend switch");
    {   // Parse: names in any case, spacing, numbers; nothing else
        Backend b = Backend::OkayUI;
        CHECK(Parse("imgui", b) && b == Backend::ImGui);
        CHECK(Parse("  Dear ImGui ", b) && b == Backend::ImGui);
        CHECK(Parse("OkayUI", b) && b == Backend::OkayUI);
        CHECK(Parse("okay ui", b) && b == Backend::OkayUI);
        CHECK(Parse("OKAYUI\r\n", b) && b == Backend::OkayUI);
        CHECK(Parse("0", b) && b == Backend::ImGui);
        CHECK(Parse("1", b) && b == Backend::OkayUI);
        b = Backend::OkayUI;
        CHECK(!Parse("", b) && b == Backend::OkayUI);          // unreadable leaves `out` alone
        CHECK(!Parse("opengl", b) && b == Backend::OkayUI);
        CHECK(!Parse("2", b) && !Parse("okayui2", b) && !Parse("imgu", b));
        CHECK(Normalize("OkayUI") == "okayui" && Normalize("Dear ImGui") == "imgui" && Normalize("garbage").empty() && Normalize("").empty());
    }
    {   // names and labels: what files hold and what screens say
        CHECK(std::string(Name(Backend::ImGui)) == "imgui" && std::string(Name(Backend::OkayUI)) == "okayui");
        CHECK(std::string(Label(Backend::ImGui)) == "Dear ImGui (default)");
        CHECK(std::string(Label(Backend::OkayUI)) == "OkayUI (preview)");
        for (Backend b : {Backend::ImGui, Backend::OkayUI}) { Backend r = Backend::ImGui; CHECK(Parse(Name(b), r) && r == b); }
    }
    {   // the default is Dear ImGui (this build was not configured with OKAY_UI_DEFAULT_BACKEND=okayui)
        CHECK(BuildDefault() == Backend::ImGui);
        CHECK(Resolve(nullptr, "", true, true) == Backend::ImGui);
        CHECK(Source(nullptr, "", true) == 0);
    }
    {   // precedence: environment over setting over default; unreadable values are ignored at every level
        CHECK(Resolve(nullptr, "okayui", true, true) == Backend::OkayUI);
        CHECK(Resolve("okayui", "", true, true) == Backend::OkayUI);
        CHECK(Resolve("imgui", "okayui", true, true) == Backend::ImGui);
        CHECK(Resolve("okayui", "imgui", true, true) == Backend::OkayUI);
        CHECK(Resolve("nonsense", "okayui", true, true) == Backend::OkayUI);
        CHECK(Resolve("", "", true, true) == Backend::ImGui);
        CHECK(Resolve(nullptr, "nonsense", true, true) == Backend::ImGui);
        CHECK(Source("okayui", "imgui", true) == 2 && Source("nonsense", "okayui", true) == 1 && Source(nullptr, "imgui", true) == 1 && Source(nullptr, "x", true) == 0);
    }
    {   // developer-only: without OKAY_DEV a stale saved value is ignored, with it the value is honoured
        const std::string stale = LauncherLoad("ui_backend=okayui\n");
        CHECK(stale == "okayui");                                                     // it is in the file
        CHECK(Resolve(nullptr, stale, false, true) == Backend::ImGui);                // ...a normal run ignores it
        CHECK(Resolve(nullptr, EditorLoad("settingsver 3\nuibackend 1\n"), false, true) == Backend::ImGui);
        CHECK(Resolve(nullptr, stale, true, true) == Backend::OkayUI);                // ...the developer flag honours it
        CHECK(Source(nullptr, stale, false) == 0 && Source(nullptr, stale, true) == 1);
        CHECK(Resolve("okayui", "", false, true) == Backend::OkayUI);                 // the environment variable still works
        CHECK(Resolve("imgui", stale, true, true) == Backend::ImGui);
        CHECK(Resolve(nullptr, "", false, true) == Backend::ImGui);                   // a normal build answers Dear ImGui
    }
    {   // the dev flag and the rows: hidden by default, shown only with the flag and only where OkayUI is compiled in
        CHECK(!DevMode(nullptr) && !DevMode("") && !DevMode("0") && DevMode("1") && DevMode("yes"));
        CHECK(!SettingRowVisible(DevMode(nullptr), true) && !SettingRowVisible(DevMode("0"), true));
        CHECK(SettingRowVisible(DevMode("1"), true) && !SettingRowVisible(DevMode("1"), false));
    }
    {   // a build without OkayUI always answers Dear ImGui, whatever is asked
        CHECK(Resolve("okayui", "okayui", true, false) == Backend::ImGui);
    }
    {   // the launcher's persistence: nothing chosen writes nothing and reads back as "never chosen"
        CHECK(LauncherSave("").empty() && LauncherLoad("").empty());
        CHECK(LauncherSave("okayui") == "ui_backend=okayui\n" && LauncherLoad(LauncherSave("okayui")) == "okayui");
        CHECK(LauncherLoad(LauncherSave("imgui")) == "imgui");
        CHECK(LauncherLoad("accent=2\nui_backend=OkayUI\ntheme=1\n") == "okayui");      // other keys around it, case ignored
        CHECK(LauncherLoad("ui_backend=garbage\n").empty());                           // a damaged line does not pick a backend
        CHECK(Resolve(nullptr, LauncherLoad("ui_backend=garbage\n"), true, true) == Backend::ImGui);
    }
    {   // the editor's persistence
        CHECK(EditorLoad(EditorSave("")).empty());
        CHECK(EditorSave("okayui").find("uibackend 1") != std::string::npos && EditorLoad(EditorSave("okayui")) == "okayui");
        CHECK(EditorLoad(EditorSave("imgui")) == "imgui");
        CHECK(EditorLoad("settingsver 3\nuibackend 7\n") == "imgui");                  // any number but 1 is Dear ImGui
        CHECK(Resolve(nullptr, EditorLoad("settingsver 3\n"), true, true) == Backend::ImGui); // an old file with no key: the default
    }
    TEST_MAIN_RESULT();
}
