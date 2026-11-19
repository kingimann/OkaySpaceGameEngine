// C# host, stage 1 (pure logic, runs without .NET): runtime search order, version choice,
// runtimeconfig.json text, plain-words errors, hostfxr discovery in a fake install folder.
#include "test_framework.hpp"
#include "okay/CSharp/DotnetHost.hpp"
#include "okay/CSharp/CSharpCompiler.hpp"
#include "okay/CSharp/CSharpScriptVM.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include <sstream>
#include <chrono>
#include <map>
#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace okay::csharp;
using okay::CSharpScriptVM;
namespace fs = std::filesystem;

static void Touch(const fs::path& p) { fs::create_directories(p.parent_path()); std::ofstream(p) << "x"; }


// ---- a fake managed side: records calls, so the VM logic runs without .NET
struct FakeScript : IManagedScript {
    std::vector<std::string>* calls; std::map<std::string, std::pair<std::string, std::string>> fields;
    std::string failOn; double spinMs = 0; std::uint64_t owner = 0;
    std::vector<std::string> overridden{"Awake", "Start", "Update", "OnTriggerEnter", "FixedUpdate", "OnEnable", "OnDestroy"};
    void SetOwner(std::uint64_t id) override { owner = id; calls->push_back("owner=" + std::to_string(id)); }
    bool Has(const std::string& m) override { return std::find(overridden.begin(), overridden.end(), m) != overridden.end(); }
    bool Invoke(const std::string& m, double num, const std::string& str, std::string* err) override {
        calls->push_back(m + "(" + std::to_string((int)num) + "," + str + ")");
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < spinMs) {}
        if (m == failOn) { *err = "line 7: NullReferenceException: Object reference not set"; return false; }
        return true;
    }
    std::string Fields() override { std::string s; for (auto& f : fields) s += f.first + "\t" + f.second.first + "\t" + f.second.second + "\n"; return s; }
    bool SetField(const std::string& n, const std::string& v, std::string* err) override {
        auto it = fields.find(n); if (it == fields.end()) { *err = "no field"; return false; }
        it->second.second = v; return true;
    }
};
struct FakeBackend : IManagedBackend {
    std::vector<std::string> calls; int created = 0; FakeScript* last = nullptr; std::string failOn; double spinMs = 0;
    bool Compile(const std::string& src, std::string* path, std::vector<std::string>* errs) override {
        if (src.find("SYNTAXERR") != std::string::npos) { errs->push_back("line 3: ; expected (CS1002)"); return false; }
        *path = "fake.dll"; return true;
    }
    std::unique_ptr<IManagedScript> Create(const std::string&, const std::string& cls, std::string* err) override {
        if (cls == "Broken") { *err = "Broken: constructor threw"; return nullptr; }
        auto s = std::make_unique<FakeScript>(); s->calls = &calls; s->failOn = failOn; s->spinMs = spinMs;
        s->fields["speed"] = {"float", "5"}; s->fields["name"] = {"string", "Bob"}; s->fields["go"] = {"bool", "true"};
        s->fields["dir"] = {"Vector3", "1,2,3"};
        ++created; last = s.get(); return s;
    }
};

int main() {
    RUN_SUITE("csharp_host");
    {   // search order
        DotnetEnv e; e.windows = true; e.dotnetRoot = "D:/dn"; e.programFiles = "C:/PF"; e.programFilesX86 = "C:/PF86";
        e.home = "C:/Users/a"; e.gameDir = "G:/game";
        auto r = RuntimeSearchRoots(e);
        CHECK(r.size() == 5);
        CHECK(r[0] == "G:/game/runtime");
        CHECK(r[1] == "D:/dn");
        CHECK(r[2] == "C:/PF/dotnet");
        CHECK(r[3] == "C:/PF86/dotnet");
        CHECK(r[4] == "C:/Users/a/.dotnet");
        DotnetEnv l; l.home = "/home/a";
        auto lr = RuntimeSearchRoots(l);
        CHECK(lr.size() == 4 && lr[0] == "/usr/share/dotnet" && lr[1] == "/usr/lib/dotnet" && lr[3] == "/home/a/.dotnet");
        DotnetEnv d; d.dotnetRoot = "/usr/share/dotnet";   // no duplicate
        auto dr = RuntimeSearchRoots(d);
        CHECK(std::count(dr.begin(), dr.end(), std::string("/usr/share/dotnet")) == 1);
    }
    {   // versions
        CHECK(ParseDotnetVersion("8.0.11").ok && ParseDotnetVersion("8.0.11").major == 8);
        CHECK(ParseDotnetVersion("9.0.0-preview.1").preview);
        CHECK(!ParseDotnetVersion("banana").ok);
        CHECK(!ParseDotnetVersion("8.0").ok);
        CHECK(!ParseDotnetVersion("").ok);
        CHECK(PickRuntimeVersion({"6.0.30", "8.0.4", "8.0.11", "7.0.1"}, 8) == "8.0.11");
        CHECK(PickRuntimeVersion({"9.0.0-preview.1", "8.0.11"}, 8) == "9.0.0-preview.1");   // newer major wins
        CHECK(PickRuntimeVersion({"9.0.0-rc.1", "9.0.0"}, 8) == "9.0.0");                     // stable beats preview
        CHECK(PickRuntimeVersion({"6.0.30", "7.0.1"}, 8).empty());
        CHECK(PickRuntimeVersion({}, 8).empty());
        CHECK(PickRuntimeVersion({"x", "8.0.1"}, 8) == "8.0.1");
    }
    {   // runtimeconfig
        std::string j = MakeRuntimeConfigJson("8.0.11");
        CHECK(j.find("\"tfm\": \"net8.0\"") != std::string::npos);
        CHECK(j.find("\"name\": \"Microsoft.NETCore.App\"") != std::string::npos);
        CHECK(j.find("\"version\": \"8.0.11\"") != std::string::npos);
        CHECK(MakeRuntimeConfigJson("9.0.1").find("net9.0") != std::string::npos);
    }
    {   // messages
        CHECK(RuntimeNotFoundMessage().find("The .NET runtime was not found. Install .NET 8 from") == 0);
        CHECK(RuntimeNotFoundMessage().find("dotnet.microsoft.com") != std::string::npos);
        CHECK(SdkNotFoundMessage().find("SDK") != std::string::npos);
        CHECK(NotEnabledMessage().find("C# is not enabled in this build") == 0);
        CHECK(HostfxrErrorText(0x80008096).find("install .NET 8") != std::string::npos);
        CHECK(HostfxrErrorText(0x12345678).find("0x12345678") != std::string::npos);
        CHECK(HostfxrFileName(DotnetEnv{}) == "libhostfxr.so");
        DotnetEnv w; w.windows = true; CHECK(HostfxrFileName(w) == "hostfxr.dll");
    }
    {   // discovery in a fake install
        fs::path root = fs::temp_directory_path() / "okay_csharp_fake_dotnet";
        fs::remove_all(root);
        Touch(root / "host/fxr/6.0.1/libhostfxr.so");
        Touch(root / "host/fxr/8.0.2/libhostfxr.so");
        Touch(root / "host/fxr/8.0.10/libhostfxr.so");
        fs::create_directories(root / "shared/Microsoft.NETCore.App/8.0.10");
        DotnetEnv e; e.dotnetRoot = root.string();
        std::string ver;
        std::string p = FindHostfxr(e, &ver);
        CHECK(ver == "8.0.10");
        CHECK(p == root.string() + "/host/fxr/8.0.10/libhostfxr.so");
        CHECK(ListInstalledRuntimes(root.string()).size() == 1);
        fs::remove_all(root);
    }
    {   // compiler helpers
        CHECK(ContentHash("a") != ContentHash("b"));
        CHECK(ContentHash("a") == ContentHash("a"));
        CHECK(ContentHash("a").size() == 32);
        CHECK(DetectScriptClass("using OkayScripting;\npublic class Mover : Behaviour { }") == "Mover");
        CHECK(DetectScriptClass("public sealed class A:Behaviour{}") == "A");
        CHECK(DetectScriptClass("class B : IFoo, Behaviour { }") == "B");
        CHECK(DetectScriptClass("class C : OkayScripting.Behaviour { }") == "C");
        CHECK(DetectScriptClass("// class X : Behaviour\nclass Y { }").empty());
        CHECK(DetectScriptClass("string s = \"class Z : Behaviour\"; class W : Other { }").empty());
        CHECK(DetectScriptClass("/* class Q : Behaviour */ class R : Behaviour {}") == "R");
        CHECK(DetectScriptClass("").empty());
        auto d = ParseBuildOutput("/tmp/x/Script.cs(12,5): error CS1002: ; expected [/tmp/x/build.csproj]\r\n"
                                  "Script.cs(3,1): warning CS0168: The variable 'a' is declared but never used [p.csproj]\n"
                                  "Script.cs(12,5): error CS1002: ; expected [/tmp/x/build.csproj]\n"
                                  "Build FAILED.\n");
        CHECK(d.size() == 2);
        if (d.size() == 2) {
            CHECK(d[0].line == 12 && d[0].column == 5 && d[0].error && d[0].code == "CS1002" && d[0].message == "; expected");
            CHECK(!d[1].error && d[1].code == "CS0168");
            CHECK(DiagText(d[0]) == "line 12: ; expected (CS1002)");
        }
        CHECK(ParseBuildOutput("nothing here").empty());
        std::string proj = MakeScriptCsproj("abc", "Script.cs", "C:/x/OkayScripting.dll");
        CHECK(proj.find("<AssemblyName>abc</AssemblyName>") != std::string::npos);
        CHECK(proj.find("<HintPath>C:/x/OkayScripting.dll</HintPath>") != std::string::npos);
        CHECK(MakeApiCsproj("csharp/OkayScripting").find("AllowUnsafeBlocks") != std::string::npos);
        CHECK(BuildCommand("C:/dn/dotnet.exe", "a.csproj", "out", "log.txt", true).find("build \"a.csproj\"") != std::string::npos);
        // SDK lookup in a fake install
        fs::path root = fs::temp_directory_path() / "okay_csharp_fake_sdk";
        fs::remove_all(root);
        fs::create_directories(root / "sdk/8.0.100"); Touch(root / "dotnet");
        DotnetEnv e; e.dotnetRoot = root.string();
        CHECK(FindDotnetCli(e) == root.string() + "/dotnet");
        fs::remove_all(root);
        // compile cache: a cached dll is returned without any SDK; a remembered failure is not rebuilt
        fs::path cache = fs::temp_directory_path() / "okay_csharp_fake_cache";
        fs::remove_all(cache);
        std::string src = "class A : Behaviour { }";
        CHECK(CachedAssembly(src, cache.string()).empty());
        Touch(cache / ContentHash(src) / (ContentHash(src) + ".dll"));
        CompileResult r = CompileScript(src, cache.string(), "", DotnetEnv{});
        CHECK(r.ok && r.cached && !r.assemblyPath.empty());
        std::string bad = "class B : Behaviour { oops }";
        fs::create_directories(cache / ContentHash(bad));
        std::ofstream(cache / ContentHash(bad) / "errors.txt") << "Script.cs(1,26): error CS1519: Invalid token 'oops' [x]\n";
        r = CompileScript(bad, cache.string(), "", DotnetEnv{});
        CHECK(!r.ok && r.cached && r.diags.size() == 1 && r.diags[0].line == 1);
        // not enabled / no SDK: plain words
        r = CompileScript("class C : Behaviour { }", cache.string(), "", DotnetEnv{});
        CHECK(!r.ok && (r.message.find("not enabled in this build") != std::string::npos || r.message.find("SDK") != std::string::npos ||
                        r.message.find("OkayScripting.dll") != std::string::npos));
        fs::remove_all(cache);
    }
    {   // the VM with a fake managed side
        std::vector<std::string> errors, warns;
        okay::Log::sink = [&](okay::Log::Level l, const std::string& m) {
            if (l >= okay::Log::Level::Error) errors.push_back(m); else if (l == okay::Log::Level::Warning) warns.push_back(m);
        };
        // event names
        CHECK(CSharpScriptVM::ManagedMethodName("on_fixed_update") == "FixedUpdate");
        CHECK(CSharpScriptVM::ManagedMethodName("on_late_update") == "LateUpdate");
        CHECK(CSharpScriptVM::ManagedMethodName("on_trigger_enter") == "OnTriggerEnter");
        CHECK(CSharpScriptVM::ManagedMethodName("on_enable") == "OnEnable");
        CHECK(CSharpScriptVM::ManagedMethodName("on_destroy") == "OnDestroy");
        CHECK(CSharpScriptVM::ManagedMethodName("update") == "Update");
        CHECK(CSharpScriptVM::ManagedMethodName("OnCollisionEnter") == "OnCollisionEnter");
        CHECK(CSharpScriptVM::ManagedMethodName("collision_enter") == "OnCollisionEnter");

        FakeBackend fb;
        SetManagedBackendForTests(&fb);
        okay::Scene scene("S");
        okay::GameObject* go = scene.CreateGameObject("Hero");
        okay::ScriptHost host; host.gameObject = go; host.transform = go->transform;
        CSharpScriptVM vm;
        std::string e;
        CHECK(!vm.Load("class NoBase { }", &e));
        CHECK(e.find("Behaviour") != std::string::npos);
        CHECK(!vm.Load("public class S : Behaviour { SYNTAXERR }", &e));
        CHECK(e == "line 3: ; expected (CS1002)");
        CHECK(!vm.Load("public class Broken : Behaviour { }", &e));
        CHECK(e.find("constructor threw") != std::string::npos);
        vm.Bind(&host);
        CHECK(vm.Load("public class Mover : Behaviour { }", &e));
        CHECK(fb.calls.size() == 1 && fb.calls[0] == "owner=" + std::to_string(go->InstanceId()));   // owner given at load, VM bound first
        fb.calls.clear();
        vm.CallStart();
        CHECK(fb.calls.size() == 3 && fb.calls[0].rfind("Awake", 0) == 0 && fb.calls[1].rfind("OnEnable", 0) == 0 && fb.calls[2].rfind("Start", 0) == 0);
        fb.calls.clear();
        vm.CallUpdate(0.5f);
        CHECK(fb.calls.size() == 1 && fb.calls[0] == "Update(0,)");
        fb.calls.clear();
        vm.CallEvent("on_fixed_update");
        vm.CallEvent("on_late_update");            // not overridden: not called at all
        CHECK(fb.calls.size() == 1 && fb.calls[0].rfind("FixedUpdate", 0) == 0);
        fb.calls.clear();
        vm.CallEventWith("on_trigger_enter", okay::vs::VsValue(std::string("Coin")));
        CHECK(fb.calls.size() == 1 && fb.calls[0] == "OnTriggerEnter(0,Coin)");
        CHECK(vm.HasEvent("on_trigger_enter") && !vm.HasEvent("on_collision_enter"));
        // fields
        auto names = vm.GlobalNames();
        CHECK(names.size() == 4);
        CHECK(vm.GetGlobal("speed").IsNumber() && vm.GetGlobal("speed").AsFloat() == 5.0f);
        CHECK(vm.GetGlobal("name").IsString() && vm.GetGlobal("name").AsString() == "Bob");
        CHECK(vm.GetGlobal("go").IsBool() && vm.GetGlobal("go").AsBool());
        CHECK(vm.GetGlobal("dir").IsVec3() && vm.GetGlobal("dir").AsVec3().y == 2.0f);
        vm.SetGlobal("speed", okay::vs::VsValue(9.5f));
        CHECK(vm.GetGlobal("speed").AsFloat() == 9.5f);
        vm.SetGlobal("other", okay::vs::VsValue(std::string("x")));   // an engine name, not a field: ignored, no crash
        // hot reload keeps fields; a broken new version leaves the old one running
        int before = fb.created;
        CHECK(vm.HotReload("public class Mover : Behaviour { /*v2*/ }", &e));
        CHECK(fb.created == before + 1);
        CHECK(vm.GetGlobal("speed").AsFloat() == 9.5f);
        CHECK(!vm.HotReload("public class Mover : Behaviour { SYNTAXERR }", &e));
        CHECK(vm.GetGlobal("speed").AsFloat() == 9.5f);
        fb.calls.clear(); vm.CallUpdate(0.1f);
        CHECK(fb.calls.size() == 1);
        // a script exception becomes one plain error line, never a crash, and the script keeps running
        fb.failOn = "Update";
        CSharpScriptVM vm2; vm2.Bind(&host);
        CHECK(vm2.Load("public class Crashy : Behaviour { }", &e));
        errors.clear();
        for (int i = 0; i < 5; ++i) vm2.CallUpdate(0.016f);
        CHECK(errors.size() == 1);                         // the same error is logged once, not every frame
        if (!errors.empty()) CHECK(errors[0].find("Crashy.Update: line 7: NullReferenceException") != std::string::npos);
        std::string perr;
        vm2.CallEventArgs("update", {}, &perr);
        CHECK(perr.find("line 7") != std::string::npos);
        // the watchdog warns about a slow script (and cannot stop it)
        fb.failOn.clear(); fb.spinMs = 6;
        CSharpScriptVM vm3; vm3.Bind(&host);
        CHECK(vm3.Load("public class Slow : Behaviour { }", &e));
        vm3.SetBudgetMs(2.0);
        warns.clear();
        vm3.CallUpdate(0.016f);
        CHECK(vm3.OverBudgetCount() == 1);
        CHECK(warns.size() == 1 && warns[0].find("cannot be stopped") != std::string::npos);
        vm3.SetBudgetMs(1000.0);
        vm3.CallUpdate(0.016f);
        CHECK(vm3.OverBudgetCount() == 1);
        // validation
        CHECK(!vm.ValidateAll("class X { }").empty());
        CHECK(vm.ValidateAll("class X : Behaviour { }").empty());
        SetManagedBackendForTests(nullptr);
        okay::Log::sink = nullptr;
    }
    {   // starter script, the shipped example, and the editor compile helper
        std::string starter = okay::StarterScriptSource("csharp");
        CHECK(DetectScriptClass(starter) == "NewScript");
        CHECK(okay::StarterScriptSource("cs") == starter);
        CHECK(starter.find("Update()") != std::string::npos);
        std::ifstream cs(std::string(OKAY_SOURCE_DIR) + "/examples/csharp/Pickup.cs");
        CHECK(cs.good());
        std::stringstream css; css << cs.rdbuf();
        CHECK(DetectScriptClass(css.str()) == "Pickup");
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/examples/csharp/csharp_pickup.okayscene");
        CHECK(in.good());
        std::stringstream ss; ss << in.rdbuf();
        okay::Scene scene("Example");
        std::string err;
        CHECK(okay::SceneSerializer::Deserialize(scene, ss.str(), &err));
        okay::GameObject* pickup = scene.Find("Pickup");
        auto* sc = pickup ? pickup->GetComponent<okay::ScriptComponent>() : nullptr;
        CHECK(sc != nullptr);
        if (sc) {
            CHECK(sc->Language() == "csharp");
            CHECK(DetectScriptClass(sc->Source()) == "Pickup");
            CHECK(sc->Source() == css.str());          // the scene embeds exactly Pickup.cs
        }
        auto vm = okay::CreateScriptVM("csharp");
        CHECK(vm != nullptr);
        std::vector<std::string> lines;
        if (!DotnetRuntime::CompiledIn()) {
            CHECK(!CompileForEditor(css.str(), "Assets/Pickup.cs", &lines));
            CHECK(lines.size() == 1 && lines[0].find("not enabled in this build") != std::string::npos);
            std::string e;
            CHECK(!vm->Load(css.str(), &e));
            CHECK(e.find("C# is not enabled in this build") != std::string::npos);
            auto d = vm->ValidateAll(css.str());
            CHECK(d.size() == 1 && d[0].severity == 0);
            auto langs = okay::AvailableScriptLanguages();
            CHECK(std::find(langs.begin(), langs.end(), "csharp") == langs.end());
        }
    }
    {   // export: nothing for a project without C#; runtime + scripts for one with
        fs::path tmp = fs::temp_directory_path() / "okay_csharp_export_test";
        fs::remove_all(tmp);
        fs::path dn = tmp / "dotnet", game = tmp / "game", cache = tmp / "cache";
        Touch(dn / "host/fxr/8.0.10/libhostfxr.so");
        Touch(dn / "shared/Microsoft.NETCore.App/8.0.10/System.Private.CoreLib.dll");
        Touch(dn / "shared/Microsoft.NETCore.App/8.0.10/libcoreclr.so");
        Touch(dn / "LICENSE.txt"); Touch(dn / "ThirdPartyNotices.txt");
        Touch(tmp / "OkayScripting.dll");
        DotnetEnv env; env.dotnetRoot = dn.string();
        auto none = ExportCSharp({}, game.string(), env, cache.string(), (tmp / "OkayScripting.dll").string());
        CHECK(!none.needed && none.files.empty() && !fs::exists(game));
        std::string src = "class P : Behaviour { }";
        auto missing = ExportCSharp({src}, game.string(), env, cache.string(), (tmp / "OkayScripting.dll").string());
        CHECK(missing.needed && !missing.ok);
        bool saidSave = false; for (auto& m : missing.problems) if (m.find("save it") != std::string::npos) saidSave = true;
        CHECK(saidSave);
        Touch(cache / ContentHash(src) / (ContentHash(src) + ".dll"));
        auto good = ExportCSharp({src}, game.string(), env, cache.string(), (tmp / "OkayScripting.dll").string());
        CHECK(good.needed && good.ok && good.problems.empty());
        CHECK(fs::exists(game / "OkayScripting.dll"));
        CHECK(fs::exists(game / "Scripts" / (ContentHash(src) + ".dll")));
        CHECK(fs::exists(game / "runtime/host/fxr/8.0.10/libhostfxr.so"));
        CHECK(fs::exists(game / "runtime/shared/Microsoft.NETCore.App/8.0.10/libcoreclr.so"));
        CHECK(fs::exists(game / "runtime/LICENSE.txt") && fs::exists(game / "runtime/ThirdPartyNotices.txt"));
        CHECK(good.files.size() == 7);
        // a game folder with that runtime is found by the host search (gameDir/runtime wins)
        DotnetEnv ge; ge.gameDir = game.string();
        std::string ver2; CHECK(!FindHostfxr(ge, &ver2).empty() && ver2 == "8.0.10");
        // no runtime installed: plain-words problem, not a crash
        DotnetEnv empty; empty.dotnetRoot = (tmp / "nowhere").string(); empty.home = "/nonexistent-okay";
        fs::remove_all(game / "runtime");
        auto norun = ExportCSharp({src}, (tmp / "game2").string(), empty, cache.string(), (tmp / "OkayScripting.dll").string());
        CHECK(norun.needed);
        if (FindHostfxr(empty).empty()) { CHECK(!norun.ok); bool said = false; for (auto& m : norun.problems) if (m.find("Install .NET 8") != std::string::npos) said = true; CHECK(said); }
        fs::remove_all(tmp);
    }
    {   // not compiled in: stub says so
        if (!DotnetRuntime::CompiledIn()) {
            std::string err;
            CHECK(DotnetRuntime::Get(CurrentDotnetEnv(), &err) == nullptr);
            CHECK(err.find("not enabled in this build") != std::string::npos);
        }
    }
    TEST_MAIN_RESULT();
}
