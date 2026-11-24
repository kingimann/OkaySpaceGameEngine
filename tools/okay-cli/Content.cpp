// okay script / scene / asset / project / vs: the project's content from the command line. Each command wraps code the
// engine and editor already have, and adds no second copy:
//   okay script   CreateScriptVM("okayscript") (Validate / ValidateAll, Load, Start, Update) and StarterScriptSource
//   okay scene    SceneSerializer, SceneDiff, SceneValidator (the checks and the asset references)
//   okay asset    the editor's Project panel model (editor/ProjectBrowser.cpp: ImportFile, Sync) on AssetDatabase
//                 (Search, DependenciesOf, UsedBy, EnsureMeta)
//   okay project  ProjectState (ParseProjectSettings, ApplyProjectSettingLine, WriteProjectSettings), ProjectVersion and
//                 the editor's project upgrade (editor/ProjectUpgrade.cpp: UpgradeJob, ProjectFileText, StampProject)
//   okay vs       okay::vs::ValidateGraphText, the graph checker the editor's Problems button (VsCheckGraph) shows
// Our own code, written on those APIs. No <thread> or <mutex> (CLAUDE.md rule 21a).
#include "Cli.hpp"
#include "CliInternal.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <system_error>

#include "ProjectBrowser.hpp"     // editor/: the Project panel's model (import, the asset index)
#include "ProjectCreate.hpp"      // shared/: WriteTextAtomic, CheckProjectName
#include "ProjectState.hpp"       // editor/: project.okayproj
#include "ProjectUpgrade.hpp"     // editor/: the project upgrade the editor runs
#include "ProjectVersion.hpp"     // shared/: the engine_version stamp
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/ImportSettings.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneDiff.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsValue.hpp"

namespace okaycli::detail {
namespace fs = std::filesystem;
using okay::str::Trim;

namespace {

std::string RootOrSay(const std::string& project, const Io& io) {
    const std::string root = FindProjectRoot(project.empty() ? "." : project);
    if (root.empty()) io.Warn("There is no OkaySpace project here. Go to a project folder, or put --project FOLDER before the command.");
    return root;
}

/// A file the person named: as given (from this folder), else inside the project.
std::string FileArg(const std::string& given, const std::string& root) {
    std::error_code ec;
    const fs::path a = fs::absolute(given, ec);
    if (fs::exists(a, ec) || root.empty()) return a.lexically_normal().string();
    const fs::path inProject = fs::path(root) / given;
    if (fs::exists(inProject, ec)) return inProject.lexically_normal().string();
    const fs::path inAssets = fs::path(root) / "Assets" / given;
    if (fs::exists(inAssets, ec)) return inAssets.lexically_normal().string();
    return a.lexically_normal().string();
}

/// Every file under the project's Assets with one of these extensions, sorted, as absolute paths.
std::vector<std::string> FilesWith(const std::string& root, const std::vector<std::string>& exts) {
    std::vector<std::string> v;
    std::error_code ec;
    const fs::path assets = fs::path(root) / "Assets";
    if (!fs::is_directory(assets, ec)) return v;
    for (fs::recursive_directory_iterator it(assets, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = okay::str::LowerAscii(it->path().extension().string());
        if (std::find(exts.begin(), exts.end(), ext) != exts.end()) v.push_back(it->path().string());
    }
    std::sort(v.begin(), v.end());
    return v;
}

/// A plain name for a new file: letters, numbers, spaces, - and _ only.
bool PlainName(const std::string& n) {
    if (Trim(n).empty() || n.size() > 80) return false;
    for (unsigned char c : n)
        if (!(std::isalnum(c) || c == ' ' || c == '-' || c == '_')) return false;
    return true;
}

std::string Plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// ---- okay script ---------------------------------------------------------------------------------------------------

/// While it lives, the engine log goes to `fn` only (not to the console).
struct QuietLog {
    okay::Log::Sink oldSink;
    bool oldConsole;
    explicit QuietLog(okay::Log::Sink fn) : oldSink(okay::Log::sink), oldConsole(okay::Log::console) {
        okay::Log::console = false;
        okay::Log::sink = std::move(fn);
    }
    ~QuietLog() { okay::Log::sink = oldSink; okay::Log::console = oldConsole; }
};

/// Hands the engine log's lines to `io` while a script runs (print goes to the log as "[script] ...").
struct LogCapture {
    okay::Log::Sink oldSink;
    bool oldConsole;
    okay::Log::Level oldLevel;
    int errors = 0;
    explicit LogCapture(const Io& io) : oldSink(okay::Log::sink), oldConsole(okay::Log::console), oldLevel(okay::Log::minLevel) {
        okay::Log::console = false;
        okay::Log::minLevel = okay::Log::Level::Info;
        okay::Log::sink = [&io, this](okay::Log::Level lv, const std::string& m) {
            std::string t = m;
            if (t.rfind("[script] ", 0) == 0) t = t.substr(9);
            if (lv == okay::Log::Level::Error) { ++errors; io.Warn(t); }
            else if (lv == okay::Log::Level::Warning) io.Warn(t);
            else io.Say(t);
        };
    }
    ~LogCapture() { okay::Log::sink = oldSink; okay::Log::console = oldConsole; okay::Log::minLevel = oldLevel; }
};

int ScriptCheck(const std::vector<std::string>& files, bool json, const std::string& root, const Io& io) {
    std::unique_ptr<okay::IScriptVM> vm = okay::CreateScriptVM("okayscript");
    if (!vm) { io.Warn("OkayScript is not in this build of okay."); return 1; }
    int errors = 0, warnings = 0;
    std::string text, js = "{\"files\":[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        bool ok = false;
        const std::string src = ReadFile(files[i], &ok);
        const std::string shown = root.empty() ? files[i] : RelTo(files[i], root);
        if (i) js += ",";
        js += "{\"file\":" + JsonText(shown) + ",\"problems\":[";
        std::vector<okay::ScriptDiagnostic> d;
        if (!ok) d.push_back({0, "the file can't be read", 0, 0});
        else d = vm->ValidateAll(src);
        for (std::size_t k = 0; k < d.size(); ++k) {
            const bool err = d[k].severity == 0;
            (err ? errors : warnings)++;
            text += shown + ":" + std::to_string(d[k].line) + (d[k].column ? ":" + std::to_string(d[k].column) : std::string()) + ": " +
                    (err ? "error: " : "warning: ") + d[k].message + "\n";
            if (k) js += ",";
            js += "{\"line\":" + std::to_string(d[k].line) + ",\"column\":" + std::to_string(d[k].column) + ",\"severity\":" +
                  JsonText(err ? "error" : "warning") + ",\"message\":" + JsonText(d[k].message) + "}";
        }
        js += "]}";
    }
    js += "],\"errors\":" + std::to_string(errors) + ",\"warnings\":" + std::to_string(warnings) + "}";
    if (json) { io.Say(js); return errors ? 1 : 0; }
    text += errors == 0 && warnings == 0 ? Plural(files.size(), "script is", "scripts are") + " fine.\n"
                                         : Plural(errors, "error", "errors") + ", " + Plural(warnings, "warning", "warnings") + ".\n";
    (errors ? io.Warn(text) : io.Say(text));
    return errors ? 1 : 0;
}

int ScriptRun(const std::string& file, const std::vector<std::string>& scriptArgs, int frames, const Io& io) {
    bool ok = false;
    const std::string src = ReadFile(file, &ok);
    if (!ok) { io.Warn("Couldn't read " + file + "."); return 1; }
    std::unique_ptr<okay::IScriptVM> vm = okay::CreateScriptVM("okayscript");
    if (!vm) { io.Warn("OkayScript is not in this build of okay."); return 1; }
    LogCapture cap(io);
    std::string err;
    if (!vm->Load(src, &err)) { io.Warn(fs::path(file).filename().string() + " does not compile: " + err); return 1; }
    // The script runs on one object in an empty scene, as a Script component would; its words come out here.
    okay::Scene scene("okay script run");
    okay::GameObject* go = scene.CreateGameObject(fs::path(file).stem().string());
    okay::ScriptHost host;
    host.gameObject = go;
    host.transform = go ? go->transform : nullptr;
    host.deltaTime = 1.0f / 60.0f;
    vm->Bind(&host);
    okay::vs::VsValue list = okay::vs::VsValue::MakeArray();
    for (const std::string& a : scriptArgs) list.AsArray()->push_back(okay::vs::VsValue(a));
    vm->SetGlobal("args", list);
    vm->CallStart();
    if (vm->HasEvent("Main")) vm->CallEvent("Main");
    for (int i = 0; i < frames; ++i) vm->CallUpdate(host.deltaTime);
    return cap.errors ? 1 : 0;
}

int ScriptNew(const std::string& name, const std::string& root, const Io& io) {
    if (!PlainName(name)) { io.Warn("Give the script a plain name (letters, numbers, spaces, - and _): okay script new Player"); return 2; }
    const fs::path file = fs::path(root) / "Assets" / "Scripts" / (name + ".okay");
    std::error_code ec;
    if (fs::exists(file, ec)) { io.Warn(RelTo(file.string(), root) + " already exists."); return 1; }
    fs::create_directories(file.parent_path(), ec);
    std::string e;
    if (!okayproj::WriteTextAtomic(file.string(), okay::StarterScriptSource("okayscript"), &e)) { io.Warn("Couldn't write the script: " + e); return 1; }
    io.Say("Made " + RelTo(file.string(), root) + " (Start and Update, empty). Check it with: okay script check " + RelTo(file.string(), root));
    return 0;
}

// ---- okay scene ----------------------------------------------------------------------------------------------------

okay::testing::ValidatorOptions CheckOptions(const std::string& root) {
    okay::testing::ValidatorOptions o;
    o.assetRoot = root;
    o.searchDirs = {(fs::path(root) / "Assets").string()};
    return o;
}

int SceneCheck(const std::vector<std::string>& files, bool json, const std::string& root, const Io& io) {
    int errors = 0, warnings = 0;
    std::string text, js = "{\"scenes\":[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        const okay::testing::ValidationReport r = okay::testing::SceneValidator::ValidateFile(files[i], CheckOptions(root));
        errors += r.Errors();
        warnings += r.Warnings();
        const std::string shown = RelTo(files[i], root);
        if (i) js += ",";
        js += "{\"file\":" + JsonText(shown) + ",\"report\":" + r.ToJson() + "}";
        if (r.Clean()) { text += shown + ": no problems.\n"; continue; }
        text += shown + ": " + Plural(r.Errors(), "error", "errors") + ", " + Plural(r.Warnings(), "warning", "warnings") + "\n";
        for (const okay::testing::ValidationIssue& is : r.issues) {
            const char* sev = is.severity == okay::testing::Severity::Error ? "error" : is.severity == okay::testing::Severity::Warning ? "warning" : "note";
            text += std::string("  ") + sev + ": " + (is.object.empty() ? std::string() : is.object + ": ") + is.message + "\n";
        }
    }
    js += "],\"errors\":" + std::to_string(errors) + ",\"warnings\":" + std::to_string(warnings) + "}";
    if (json) { io.Say(js); return errors ? 1 : 0; }
    text += errors ? "Fix the errors above; the warnings are worth a look." : "Nothing that stops the game.";
    (errors ? io.Warn(text) : io.Say(text));
    return errors ? 1 : 0;
}

int SceneInfo(const std::string& file, bool json, const std::string& root, const Io& io) {
    bool ok = false;
    const std::string text = ReadFile(file, &ok);
    if (!ok) { io.Warn("Couldn't read " + file + "."); return 1; }
    okay::Scene scene("info");
    std::string err;
    if (!okay::SceneSerializer::Deserialize(scene, text, &err)) { io.Warn(RelTo(file, root) + " does not load: " + err); return 1; }
    std::map<std::string, int> comps;
    std::map<std::string, std::string> refs;   // path -> kind
    int objects = 0, roots = (int)scene.Roots().size();
    for (const auto& o : scene.Objects()) {
        if (!o) continue;
        ++objects;
        for (const auto& c : o->Components()) if (c) comps[okay::ComponentSerializerRegistry::TypeKey(*c)]++;
        for (const okay::testing::AssetRef& a : okay::testing::SceneValidator::AssetReferences(*o)) if (!a.path.empty()) refs[a.path] = a.kind;
        // Script files are checked by the validator but are not in its asset list: add them here.
        for (okay::ScriptComponent* sc : o->GetComponents<okay::ScriptComponent>()) if (!sc->Path().empty()) refs[sc->Path()] = "script";
        for (okay::VisualScriptComponent* vs : o->GetComponents<okay::VisualScriptComponent>()) if (!vs->Path().empty()) refs[vs->Path()] = "visual script";
    }
    const okay::testing::ValidatorOptions opt = CheckOptions(root);
    if (json) {
        std::string j = "{\"file\":" + JsonText(RelTo(file, root)) + ",\"name\":" + JsonText(scene.Name()) + ",\"objects\":" + std::to_string(objects) +
                        ",\"roots\":" + std::to_string(roots) + ",\"components\":{";
        bool first = true;
        for (const auto& kv : comps) { j += (first ? "" : ",") + JsonText(kv.first) + ":" + std::to_string(kv.second); first = false; }
        j += "},\"references\":[";
        first = true;
        for (const auto& kv : refs) {
            j += std::string(first ? "" : ",") + "{\"path\":" + JsonText(kv.first) + ",\"kind\":" + JsonText(kv.second) +
                 ",\"exists\":" + (okay::testing::SceneValidator::AssetExists(kv.first, opt) ? "true" : "false") + "}";
            first = false;
        }
        io.Say(j + "]}");
        return 0;
    }
    std::string t = "Scene " + scene.Name() + " (" + RelTo(file, root) + ")\n";
    t += "  " + Plural(objects, "object", "objects") + " (" + std::to_string(roots) + " at the top)\n";
    t += "  Components:" + std::string(comps.empty() ? " none" : "") + "\n";
    for (const auto& kv : comps) t += "    " + Pad(kv.first, 24) + std::to_string(kv.second) + "\n";
    t += "  Uses:" + std::string(refs.empty() ? " no other files" : "") + "\n";
    for (const auto& kv : refs)
        t += "    " + kv.first + " (" + kv.second + ")" + (okay::testing::SceneValidator::AssetExists(kv.first, opt) ? "" : "  MISSING") + "\n";
    io.Say(t);
    return 0;
}

// ---- okay asset ----------------------------------------------------------------------------------------------------

std::string AssetsDir(const std::string& root) { return (fs::path(root) / "Assets").string(); }

/// The index path of a file the person named ("Assets/x.png", "x.png" inside Assets, or a full path).
std::string IndexPath(okaypb::Browser& b, const std::string& given, const std::string& root) {
    const std::string abs = FileArg(given, root);
    return b.Db().ToProjectPath(abs);
}

std::string AssetJson(const okay::AssetRecord& r) {
    return "{\"path\":" + JsonText("Assets/" + r.path) + ",\"type\":" + JsonText(okay::AssetTypeName(r.kind)) + ",\"guid\":" +
           JsonText(r.guid.ToString()) + ",\"size\":" + std::to_string(r.size) + "}";
}

int AssetPrint(okaypb::Browser& b, const std::vector<okay::Guid>& guids, const std::vector<const okay::AssetRecord*>& recs, bool json,
               const std::string& heading, const std::string& none, const Io& io) {
    std::vector<const okay::AssetRecord*> v = recs;
    for (const okay::Guid& g : guids) if (const okay::AssetRecord* r = b.Db().Find(g)) v.push_back(r);
    if (json) {
        std::string j = "{\"assets\":[";
        for (std::size_t i = 0; i < v.size(); ++i) j += (i ? "," : "") + AssetJson(*v[i]);
        io.Say(j + "],\"count\":" + std::to_string(v.size()) + "}");
        return 0;
    }
    if (v.empty()) { io.Say(none); return 0; }
    std::string t = heading + "\n";
    for (const okay::AssetRecord* r : v) t += "  " + Pad(std::string(okay::AssetTypeName(r->kind)), 14) + "Assets/" + r->path + "\n";
    io.Say(t + Plural(v.size(), "file", "files") + ".");
    return 0;
}

} // namespace

// ---- the commands -----------------------------------------------------------------------------------------------------

int ScriptMain(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") { io.Say(HelpText("script")); return 0; }
    const std::string sub = args[0];
    const std::vector<std::string> more(args.begin() + 1, args.end());
    if (sub == "run") {
        // Everything after the file belongs to the script, except okay's own --frames N right after the file.
        if (more.empty() || more[0] == "--help" || more[0] == "-h") { io.Say(HelpText("script run")); return more.empty() ? 2 : 0; }
        std::size_t i = 1;
        int frames = 0;
        if (more.size() > 2 && more[1] == "--frames") {
            frames = std::atoi(more[2].c_str());
            if (frames < 0 || frames > 1000000) { io.Warn("--frames needs a number from 0 up."); return 2; }
            i = 3;
        }
        if (i < more.size() && more[i] == "--") ++i;
        const std::string root = FindProjectRoot(project.empty() ? "." : project);
        return ScriptRun(FileArg(more[0], root), std::vector<std::string>(more.begin() + (std::ptrdiff_t)i, more.end()), frames, io);
    }
    okay::CommandLine cl;
    cl.Flag("json");
    if (!ParseArgs(cl, more, "script " + sub, io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("script " + sub)); return 0; }
    if (sub == "check") {
        const std::string root = FindProjectRoot(project.empty() ? "." : project);
        std::vector<std::string> files;
        for (const std::string& f : cl.Positionals()) files.push_back(FileArg(f, root));
        if (files.empty()) {
            if (root.empty()) { io.Warn("Say which script to check: okay script check <file.okay>"); return 2; }
            files = FilesWith(root, {".okay", ".okayscript"});
            if (files.empty()) { io.Say(cl.Has("json") ? std::string("{\"files\":[],\"errors\":0,\"warnings\":0}") : "The project has no scripts."); return 0; }
        }
        return ScriptCheck(files, cl.Has("json"), root, io);
    }
    if (sub == "new") {
        const std::string root = RootOrSay(project, io);
        if (root.empty()) return 1;
        if (cl.Positionals().size() != 1) { io.Warn("Say the script's name: okay script new Player"); return 2; }
        return ScriptNew(cl.Positional(0), root, io);
    }
    io.Warn("There is no okay script command called \"" + sub + "\".\n" + HelpText("script"));
    return 2;
}

int SceneMain(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") { io.Say(HelpText("scene")); return 0; }
    const std::string sub = args[0];
    okay::CommandLine cl;
    cl.Flag("json");
    if (!ParseArgs(cl, std::vector<std::string>(args.begin() + 1, args.end()), "scene " + sub, io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("scene " + sub)); return 0; }
    const bool json = cl.Has("json");
    if (sub == "diff") {   // works on any two files, also outside a project
        if (cl.Positionals().size() != 2) { io.Warn("Give two scenes: okay scene diff <a> <b>"); return 2; }
        const std::string root = FindProjectRoot(project.empty() ? "." : project);
        bool okA = false, okB = false;
        const std::string a = ReadFile(FileArg(cl.Positional(0), root), &okA), b = ReadFile(FileArg(cl.Positional(1), root), &okB);
        if (!okA || !okB) { io.Warn("Couldn't read " + (okA ? cl.Positional(1) : cl.Positional(0)) + "."); return 1; }
        const okay::SceneDiff::Result d = okay::SceneDiff::Compute(a, b);
        if (!d.ok) { io.Warn("Couldn't compare them as scenes: " + d.error); return 1; }
        io.Say(d.Empty() ? std::string("The two scenes are the same (only how the files are laid out may differ).") : d.ToText());
        return 0;
    }
    const std::string root = RootOrSay(project, io);
    if (root.empty()) return 1;
    if (sub == "list") {
        const std::vector<std::string> v = FilesWith(root, {".okayscene"});
        const std::string start = MainScene(root);
        if (json) {
            std::string j = "{\"scenes\":[";
            for (std::size_t i = 0; i < v.size(); ++i)
                j += std::string(i ? "," : "") + "{\"path\":" + JsonText(RelTo(v[i], root)) + ",\"start\":" + (v[i] == start ? "true" : "false") + "}";
            io.Say(j + "],\"count\":" + std::to_string(v.size()) + "}");
            return 0;
        }
        if (v.empty()) { io.Say("The project has no scenes. Make one with: okay scene new Level1"); return 0; }
        std::string t = "Scenes:\n";
        for (const std::string& s : v) t += "  " + RelTo(s, root) + (s == start ? "   (the start scene)" : "") + "\n";
        io.Say(t + Plural(v.size(), "scene", "scenes") + ".");
        return 0;
    }
    if (sub == "new") {
        if (cl.Positionals().size() != 1 || !PlainName(cl.Positional(0))) {
            io.Warn("Give the scene a plain name (letters, numbers, spaces, - and _): okay scene new Level2");
            return 2;
        }
        const fs::path file = fs::path(root) / "Assets" / "Scenes" / (cl.Positional(0) + ".okayscene");
        std::error_code ec;
        if (fs::exists(file, ec)) { io.Warn(RelTo(file.string(), root) + " already exists."); return 1; }
        fs::create_directories(file.parent_path(), ec);
        okay::Scene empty(cl.Positional(0));
        std::string e;
        if (!okayproj::WriteTextAtomic(file.string(), okay::SceneSerializer::Serialize(empty), &e)) { io.Warn("Couldn't write the scene: " + e); return 1; }
        io.Say("Made the empty scene " + RelTo(file.string(), root) + ". Open it with: okay open " + RelTo(file.string(), root));
        return 0;
    }
    if (sub == "check") {
        std::vector<std::string> files;
        for (const std::string& f : cl.Positionals()) files.push_back(FileArg(f, root));
        if (files.empty()) files = FilesWith(root, {".okayscene"});
        if (files.empty()) { io.Say(json ? std::string("{\"scenes\":[],\"errors\":0,\"warnings\":0}") : "The project has no scenes."); return 0; }
        return SceneCheck(files, json, root, io);
    }
    if (sub == "info") {
        std::string file = cl.Positional(0);
        file = file.empty() ? MainScene(root) : FileArg(file, root);
        if (file.empty()) { io.Warn("The project has no scene yet."); return 1; }
        return SceneInfo(file, json, root, io);
    }
    io.Warn("There is no okay scene command called \"" + sub + "\".\n" + HelpText("scene"));
    return 2;
}

int AssetMain(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") { io.Say(HelpText("asset")); return 0; }
    const std::string sub = args[0];
    okay::CommandLine cl;
    cl.Flag("json").Value("type", "T").Value("to", "FOLDER");
    if (!ParseArgs(cl, std::vector<std::string>(args.begin() + 1, args.end()), "asset " + sub, io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("asset " + sub)); return 0; }
    const std::string root = RootOrSay(project, io);
    if (root.empty()) return 1;
    std::error_code ec;
    if (!fs::is_directory(AssetsDir(root), ec)) { io.Warn("The project has no Assets folder."); return 1; }
    okaypb::Browser b(AssetsDir(root));
    if (!b.Sync()) { io.Warn("Couldn't read the Assets folder."); return 1; }
    const bool json = cl.Has("json");
    if (sub == "list") {
        okay::AssetQuery q;
        if (cl.Has("type")) {
            const okay::AssetType t = okay::AssetTypeFromName(cl.Get("type"));
            if (t == okay::AssetType::Unknown) { io.Warn("\"" + cl.Get("type") + "\" is not an asset type (for example texture, model, audio, scene, prefab, script)."); return 2; }
            q.types.push_back(t);
        }
        return AssetPrint(b, {}, b.Db().Search(q).items, json, "Assets:", "No assets.", io);
    }
    if (sub == "find") {
        if (cl.Positionals().empty()) { io.Warn("Say what to look for: okay asset find tree"); return 2; }
        okay::AssetQuery q;
        q.text = JoinWords(cl.Positionals());
        return AssetPrint(b, {}, b.Db().Search(q).items, json, "Found:", "Nothing is called \"" + q.text + "\".", io);
    }
    if (sub == "used-by" || sub == "uses") {
        if (cl.Positionals().size() != 1) { io.Warn("Say which file: okay asset " + sub + " Assets/Textures/grass.png"); return 2; }
        const std::string path = IndexPath(b, cl.Positional(0), root);
        const okay::Guid g = b.Db().PathToGuid(path);
        if (g.IsNull()) { io.Warn(cl.Positional(0) + " is not an asset in this project."); return 1; }
        if (sub == "used-by")
            return AssetPrint(b, b.Db().UsedBy(g), {}, json, "Assets/" + path + " is used by:", "Nothing uses Assets/" + path + ".", io);
        return AssetPrint(b, b.Db().DependenciesOf(g), {}, json, "Assets/" + path + " uses:", "Assets/" + path + " uses no other asset.", io);
    }
    if (sub == "import") {
        if (cl.Positionals().size() != 1) { io.Warn("Say which file to bring in: okay asset import C:/Downloads/tree.glb [--to Models]"); return 2; }
        const std::string src = fs::absolute(cl.Positional(0), ec).string();
        if (!fs::is_regular_file(src, ec)) { io.Warn(cl.Positional(0) + " is not a file."); return 1; }
        std::string to = cl.Get("to", "");
        fs::path dest = fs::path(AssetsDir(root));
        if (!to.empty()) {
            if (to.rfind("Assets/", 0) == 0 || to == "Assets") to = to.size() > 6 ? to.substr(7) : std::string();
            dest = (dest / to).lexically_normal();
            fs::create_directories(dest, ec);
        }
        // The editor's own import (the Project panel's Import button): a free name, all or nothing.
        const okaypb::Result r = b.ImportFile(src, dest.string());
        if (!r.ok || r.paths.empty()) { io.Warn("Couldn't import it: " + r.error); return 1; }
        b.Sync();
        const okay::Guid g = b.Db().PathToGuid(b.Db().ToProjectPath(r.paths[0]));
        std::string e;
        // The .okaymeta beside it (its GUID and the default import settings for its type), as AssetDatabase writes it.
        if (g.IsNull() || !b.Db().EnsureMeta(g, &e)) { io.Warn("Imported " + RelTo(r.paths[0], root) + ", but its .okaymeta could not be written: " + e); return 1; }
        const okay::AssetRecord* rec = b.Db().Find(g);
        io.Say("Imported " + RelTo(r.paths[0], root) + " as " + (rec ? std::string(okay::AssetTypeName(rec->kind)) : std::string("an asset")) +
               ", with its .okaymeta.");
        return 0;
    }
    io.Warn("There is no okay asset command called \"" + sub + "\".\n" + HelpText("asset"));
    return 2;
}

int ProjectMain(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") { io.Say(HelpText("project")); return 0; }
    const std::string sub = args[0];
    okay::CommandLine cl;
    cl.Flag("json");
    if (!ParseArgs(cl, std::vector<std::string>(args.begin() + 1, args.end()), "project " + sub, io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("project " + sub)); return 0; }
    const std::string root = RootOrSay(project, io);
    if (root.empty()) return 1;
    const std::string file = oked::ProjectSettingsFile(root);
    bool have = false;
    const std::string text = ReadFile(file, &have);
    oked::ProjectSettings s;
    oked::ParseProjectSettings(s, text);
    const okayver::Stamp st = okayver::ReadProjectStamp(root);
    if (sub == "info") {
        const std::string start = MainScene(root);
        const std::size_t scenes = FilesWith(root, {".okayscene"}).size();
        if (cl.Has("json")) {
            io.Say("{\"name\":" + JsonText(fs::path(root).filename().string()) + ",\"folder\":" + JsonText(root) + ",\"engineVersion\":" +
                   JsonText(st.version) + ",\"thisEngine\":" + JsonText(EngineVersion()) + ",\"gameVersion\":" + JsonText(s.version) +
                   ",\"company\":" + JsonText(s.company) + ",\"width\":" + std::to_string(s.defaultWidth) + ",\"height\":" +
                   std::to_string(s.defaultHeight) + ",\"startScene\":" + JsonText(start.empty() ? std::string() : RelTo(start, root)) +
                   ",\"scenes\":" + std::to_string(scenes) + "}");
            return 0;
        }
        std::string t = "Project " + fs::path(root).filename().string() + " at " + root + "\n";
        t += "  Made with engine: " + (st.version.empty() ? std::string("not recorded") : st.version) + " (this okay is " + EngineVersion() + ")\n";
        t += "  Game version: " + std::string(s.version[0] ? s.version : "not set") + "   Company: " + (s.company[0] ? s.company : "not set") + "\n";
        t += "  Window: " + std::to_string(s.defaultWidth) + " x " + std::to_string(s.defaultHeight) + "\n";
        t += "  " + Plural(scenes, "scene", "scenes") + "; start scene: " + (start.empty() ? std::string("none yet") : RelTo(start, root)) + "\n";
        if (!have) t += "  There is no project.okayproj yet; open the project once in the editor, or set a setting with okay project settings set.\n";
        io.Say(t);
        return 0;
    }
    if (sub == "settings") {
        const std::string verb = cl.Positional(0);
        const std::string generated = oked::WriteProjectSettings(s, "");
        auto valueOf = [&](const std::string& key, std::string& out) {
            std::istringstream in(generated);
            for (std::string line; std::getline(in, line);)
                if (line.rfind(key + "=", 0) == 0) { out = line.substr(key.size() + 1); return true; }
            return false;
        };
        if (verb.empty() || verb == "list") {
            io.Say("Project settings (project.okayproj):\n" + generated);
            return 0;
        }
        if (verb == "get") {
            if (cl.Positionals().size() != 2) { io.Warn("Say which setting: okay project settings get width"); return 2; }
            std::string v;
            if (!valueOf(cl.Positional(1), v)) { io.Warn("There is no project setting called \"" + cl.Positional(1) + "\". See them all with: okay project settings"); return 1; }
            io.Say(v);
            return 0;
        }
        if (verb == "set") {
            if (cl.Positionals().size() < 3) { io.Warn("Say the setting and its value: okay project settings set width 1920"); return 2; }
            const std::string key = cl.Positional(1), value = JoinWords(cl.Positionals(), 2);
            std::string warning;
            if (!oked::ApplyProjectSettingLine(s, key, value, &warning)) {
                io.Warn("There is no project setting called \"" + key + "\". See them all with: okay project settings");
                return 1;
            }
            // The editor's own writer: the lines okay does not own (tags, layers, a newer editor's keys) and the engine
            // stamp are kept as they are. The stamp is the project's, not this okay's: changing a setting is not an upgrade.
            const std::string keepStamp = st.version.empty() ? std::string() : st.version;
            const std::string out = okayup::ProjectFileText(file, oked::WriteProjectSettings(s), keepStamp);
            std::string e;
            if (!okayproj::WriteTextAtomic(file, out, &e)) { io.Warn("Couldn't write project.okayproj: " + e); return 1; }
            const std::string after = [&] {
                oked::ProjectSettings s2;
                oked::ParseProjectSettings(s2, out);
                std::string v;
                std::istringstream in(oked::WriteProjectSettings(s2, ""));
                for (std::string line; std::getline(in, line);) if (line.rfind(key + "=", 0) == 0) v = line.substr(key.size() + 1);
                return v;
            }();
            io.Say(key + " is " + after + " now." + (warning.empty() ? std::string() : " (" + warning + ")"));
            return 0;
        }
        io.Warn("Use okay project settings [get <key> | set <key> <value>].");
        return 2;
    }
    if (sub == "upgrade") {
        using D = okayver::Decision;
        const D d = okayver::Decide(st.version, EngineVersion());
        if (d == D::Newer) { io.Warn("The project was saved by a newer engine (" + st.version + "). Use that okay and editor, or newer."); return 1; }
        if (d == D::DevBuild) { io.Warn("This okay is a development build, so it can't stamp a version on the project."); return 1; }
        if (d == D::Same || d == D::SamePatchLine) { io.Say("The project is already on this engine (" + st.version + "). Nothing to do."); return 0; }
        // The editor's upgrade: a full backup first, every scene and prefab checked, then rewritten; on any failure the
        // files are put back.
        okayup::Options opt;
        opt.projectDir = root;
        opt.fromVersion = st.version;
        opt.toVersion = okayver::NormalizeVersion(EngineVersion());
        okayup::UpgradeJob job(opt);
        job.Run();
        const okayup::Result& r = job.result();
        if (!r.ok) { io.Warn("The upgrade did not work: " + r.error + (r.restoreNote.empty() ? std::string() : " " + r.restoreNote)); return 1; }
        std::string e;
        okayup::StampProject(root, opt.toVersion, &e);
        std::string t = "Upgraded the project to " + opt.toVersion + ": " + Plural((std::size_t)r.scenes, "scene", "scenes") + " and " +
                        Plural((std::size_t)r.prefabs, "prefab", "prefabs") + " checked, " + std::to_string(r.rewritten) + " rewritten.";
        if (!r.backupDir.empty()) t += "\nThe project as it was is kept in " + r.backupDir + ".";
        for (const std::string& l : r.losses) t += "\nNote: " + l;
        io.Say(t);
        return 0;
    }
    io.Warn("There is no okay project command called \"" + sub + "\".\n" + HelpText("project"));
    return 2;
}

int VsMain(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") { io.Say(HelpText("vs")); return 0; }
    const std::string sub = args[0];
    okay::CommandLine cl;
    cl.Flag("json");
    if (!ParseArgs(cl, std::vector<std::string>(args.begin() + 1, args.end()), "vs " + sub, io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vs " + sub)); return 0; }
    const bool json = cl.Has("json");
    const std::string root = FindProjectRoot(project.empty() ? "." : project);
    if (sub == "list") {
        if (root.empty()) { RootOrSay(project, io); return 1; }
        const std::vector<std::string> v = FilesWith(root, {".okayvs"});
        if (json) {
            std::string j = "{\"graphs\":[";
            for (std::size_t i = 0; i < v.size(); ++i) j += (i ? "," : "") + JsonText(RelTo(v[i], root));
            io.Say(j + "],\"count\":" + std::to_string(v.size()) + "}");
            return 0;
        }
        if (v.empty()) { io.Say("The project has no Visual Script graphs (.okayvs)."); return 0; }
        std::string t = "Visual Script graphs:\n";
        for (const std::string& f : v) t += "  " + RelTo(f, root) + "\n";
        io.Say(t + Plural(v.size(), "graph", "graphs") + ".");
        return 0;
    }
    if (sub == "check") {
        std::vector<std::string> files;
        for (const std::string& f : cl.Positionals()) files.push_back(FileArg(f, root));
        if (files.empty()) {
            if (root.empty()) { io.Warn("Say which graph to check: okay vs check <file.okayvs>"); return 2; }
            files = FilesWith(root, {".okayvs"});
        }
        int errors = 0, warnings = 0;
        std::string t, j = "{\"graphs\":[";
        for (std::size_t i = 0; i < files.size(); ++i) {
            bool ok = false;
            const std::string text = ReadFile(files[i], &ok);
            const std::string shown = root.empty() ? files[i] : RelTo(files[i], root);
            std::vector<okay::vs::VsIssue> issues;
            if (!ok) issues.push_back({0, -1, "the file can't be read", -1, 0});
            else {
                // The checker logs the lines it could not read instead of listing them: those become warnings here.
                std::vector<std::string> skipped;
                {
                    QuietLog quiet([&](okay::Log::Level lv, const std::string& m) { if (lv >= okay::Log::Level::Warning) skipped.push_back(m); });
                    issues = okay::vs::ValidateGraphText(text, root.empty() ? fs::path(files[i]).parent_path().string() : root);
                }
                for (const std::string& m : skipped) issues.push_back({1, -1, m, -1, 0});
            }
            j += std::string(i ? "," : "") + "{\"file\":" + JsonText(shown) + ",\"problems\":[";
            if (issues.empty()) t += shown + ": no problems.\n";
            for (std::size_t k = 0; k < issues.size(); ++k) {
                const bool err = issues[k].severity == 0;
                (err ? errors : warnings)++;
                t += shown + ": " + (err ? "error: " : "warning: ") + (issues[k].node >= 0 ? "node " + std::to_string(issues[k].node) + ": " : std::string()) +
                     issues[k].message + "\n";
                j += std::string(k ? "," : "") + "{\"node\":" + std::to_string(issues[k].node) + ",\"severity\":" + JsonText(err ? "error" : "warning") +
                     ",\"message\":" + JsonText(issues[k].message) + "}";
            }
            j += "]}";
        }
        j += "],\"errors\":" + std::to_string(errors) + ",\"warnings\":" + std::to_string(warnings) + "}";
        if (json) { io.Say(j); return errors ? 1 : 0; }
        if (files.empty()) { io.Say("The project has no Visual Script graphs (.okayvs)."); return 0; }
        t += Plural((std::size_t)errors, "error", "errors") + ", " + Plural((std::size_t)warnings, "warning", "warnings") + ".";
        (errors ? io.Warn(t) : io.Say(t));
        return errors ? 1 : 0;
    }
    io.Warn("There is no okay vs command called \"" + sub + "\".\n" + HelpText("vs"));
    return 2;
}

} // namespace okaycli::detail
