// C# scripting through the hosted .NET runtime (rule 24c). Always (no .NET needed): runtime search order, version
// choice, runtimeconfig.json text, plain-words errors, hostfxr discovery in a fake install, the compiler helpers,
// the VM with a fake managed side, the API generated from the binding table, export. With OKAY_WITH_CSHARP=ON and a
// .NET SDK installed it also compiles real C# with the SDK, starts the runtime with hostfxr and runs scripts on
// objects (the "real .NET" block; it says when it is skipped).
#include "test_framework.hpp"
#include "okay/Scripting/CSharpHost.hpp"
#include "okay/Scripting/CSharpCompiler.hpp"
#include "okay/Scripting/CSharpScriptVM.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/CSharpBridge.hpp"
#include "okay/Scripting/ScriptBindings.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include <cstdlib>
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
    bool Invoke(const std::string& m, double num, const std::string& str, std::uint64_t obj, std::string* err) override {
        calls->push_back(m + "(" + std::to_string((int)num) + "," + str + (obj ? ",#" + std::to_string(obj) : std::string()) + ")");
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
        CHECK(MakeApiCsproj("managed/OkayScripting").find("AllowUnsafeBlocks") != std::string::npos);
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
        fb.calls.clear();   // a collision message carries the other object's id, so the handler gets that exact object
        vm.CallEventWith("on_trigger_enter", okay::vs::VsValue::Object(go->InstanceId()));
        CHECK(fb.calls.size() == 1 && fb.calls[0] == "OnTriggerEnter(0,Hero,#" + std::to_string(go->InstanceId()) + ")");
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
    {   // a machine with no .NET SDK: a plain-words message, never a crash
        DotnetEnv none; none.systemPlaces = false; none.dotnetRoot = (fs::temp_directory_path() / "okay_no_dotnet_here").string();
        CHECK(FindDotnetCli(none).empty());
        CHECK(FindHostfxr(none).empty());
        fs::path cache = fs::temp_directory_path() / "okay_csharp_nosdk_cache";
        fs::remove_all(cache);
        CompileResult r = CompileScript("public class NoSdk : Behaviour { }", cache.string(), "", none);
        CHECK(!r.ok && r.diags.empty());
        if (DotnetRuntime::CompiledIn()) CHECK(r.message == SdkNotFoundMessage());
        else CHECK(r.message == NotEnabledMessage());
        if (DotnetRuntime::CompiledIn()) CHECK(r.message.find("dotnet.microsoft.com") != std::string::npos);   // where to get the SDK
        else CHECK(r.message.find("OKAY_WITH_CSHARP") != std::string::npos);                                 // how to turn C# on
        std::string err;
        CHECK(EnsureApiAssembly(ApiSourceDir(), cache.string(), none, &err).empty());
        CHECK(err.find(DotnetRuntime::CompiledIn() ? "SDK" : "SDK") != std::string::npos);
        std::string why;
        CHECK(!DotnetRuntime::Available(none, &why) && !why.empty());
        fs::remove_all(cache);
    }
    {   // the C# API is generated from the binding table (the one OkayScript uses), not written by hand
        std::string g = GenerateApiSource();
        ApiGenStats st = LastApiGenStats();
        std::printf("generated C# API: %d classes, %d methods, %d properties, %d left out\n", st.classes, st.methods, st.properties, st.skipped);
        CHECK(st.classes > 100);
        CHECK(st.methods > 500);
        CHECK(st.properties > 300);
        CHECK(g.find("public partial class Light : Component") != std::string::npos);
        CHECK(g.find("public float intensity { get => Get<float>(\"intensity\"); set => Set(\"intensity\", value); }") != std::string::npos);
        CHECK(g.find("public partial class HealthStat : Component") != std::string::npos);
        CHECK(g.find("public void Damage(float a0) => CallVoid(\"Damage\", a0);") != std::string::npos);
        CHECK(g.find("public void Damage(float a0, Vector3 a1) => CallVoid(\"Damage\", a0, a1);") != std::string::npos);
        CHECK(g.find("m[\"Light\"] = () => new Light();") != std::string::npos);
        CHECK(g.find("class Transform :") == std::string::npos);   // hand-written types are not generated again
        CHECK(GenerateApiSource() == g);                          // the same table gives the same text (a stable cache key)
        {   // the generator's file, network, platform and clipboard rules (#1234 review B1) hold in C#: the C# API is
            // made from the same binding table, so a denied class has no methods and no denied method appears
            namespace sb = okay::scriptbind;
            auto body = [&](const std::string& cls) {
                const std::size_t a = g.find("public partial class " + cls + " : Component");
                if (a == std::string::npos) return std::string();
                const std::size_t b = g.find("public partial class ", a + 1);
                return g.substr(a, b == std::string::npos ? std::string::npos : b - a);
            };
            int leaks = 0;
            for (const std::string& cls : sb::GeneratedDenyRules().classes) {
                const std::string t = body(cls);
                if (t.find("Call(") != std::string::npos || t.find("Call<") != std::string::npos ||
                    t.find("CallVoid(") != std::string::npos) { std::printf("  C# %s has methods\n", cls.c_str()); ++leaks; }
            }
            for (const std::string& cm : sb::GeneratedDenyRules().methods) {
                const std::size_t dot = cm.find('.');
                const std::string t = body(cm.substr(0, dot));
                if (t.find("(\"" + cm.substr(dot + 1) + "\"") != std::string::npos) { std::printf("  C# has %s\n", cm.c_str()); ++leaks; }
            }
            for (const char* m : {"ExportHeightmap", "ImportHeightmap", "PasteFromClipboard", "CopyToClipboard", "OpenStream",
                                  "SaveClipsToFile", "LoadClipsFromFile", "LoadFile"})
                if (g.find(std::string("(\"") + m + "\"") != std::string::npos) { std::printf("  C# has %s\n", m); ++leaks; }
            CHECK(leaks == 0);
        }
        CHECK(!ApiSourceDir().empty());
        // a scene with no C# scripts has no C# sources, so its game ships no runtime
        okay::Scene plain("Plain");
        plain.CreateGameObject("A")->AddComponent<okay::ScriptComponent>("okayscript")->SetSource("void Update() {}");
        CHECK(CollectCSharpSources(plain).empty());
        okay::Scene withCs("Cs");
        withCs.CreateGameObject("B")->AddComponent<okay::ScriptComponent>("csharp")->SetSource("public class B : Behaviour { }");
        CHECK(CollectCSharpSources(withCs).size() == 1);
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
    {   // real .NET: compile with the SDK's compiler, start the runtime through hostfxr, run scripts on objects
        DotnetEnv env = CurrentDotnetEnv();
        const bool canRun = DotnetRuntime::CompiledIn() && !FindDotnetCli(env).empty() && !FindHostfxr(env).empty();
        if (!canRun) {
            std::printf("real .NET: SKIPPED (%s)\n", !DotnetRuntime::CompiledIn() ? "built without OKAY_WITH_CSHARP"
                                                                                 : "no .NET SDK / runtime on this machine");
        } else {
            std::printf("real .NET: running (SDK %s)\n", FindDotnetCli(env).c_str());
            fs::path cache = fs::temp_directory_path() / "okay_csharp_real_cache";
            fs::remove_all(cache);
#ifdef _WIN32
            _putenv_s("OKAY_CSHARP_CACHE", cache.string().c_str());
#else
            setenv("OKAY_CSHARP_CACHE", cache.string().c_str(), 1);
#endif
            std::vector<std::string> errors, infos;
            okay::Log::sink = [&](okay::Log::Level l, const std::string& m) {
                if (l >= okay::Log::Level::Error) errors.push_back(m); else infos.push_back(m);
            };
            auto hasLine = [](const std::vector<std::string>& v, const std::string& needle) {
                for (auto& x : v) if (x.find(needle) != std::string::npos) return true;
                return false;
            };
            // 1. a script on an object changes the scene: moves it, sets a Light property, calls an engine method
            okay::Scene scene("Real");
            okay::GameObject* go = scene.CreateGameObject("Mover");
            go->transform->localPosition = {0.0f, 0.0f, 0.0f};
            auto* light = go->AddComponent<okay::Light>();
            auto* hp = go->AddComponent<okay::HealthStat>();
            hp->maxHealth = 100.0f; hp->health = 100.0f;
            okay::GameObject* coin = scene.CreateGameObject("Coin");
            auto* sc = go->AddComponent<okay::ScriptComponent>("csharp");
            const std::string mover =
                "using OkayScripting;\n"
                "public class Mover : Behaviour\n"
                "{\n"
                "    public float speed = 2f;\n"
                "    public string lastHit = \"\";\n"
                "    int frames;\n"
                "    void Start() { Debug.Log(\"started on \" + gameObject.name); }\n"
                "    void Update()\n"
                "    {\n"
                "        frames++;\n"
                "        transform.position = transform.position + new Vector3(speed * Time.deltaTime, 0f, 0f);\n"
                "        Light l = GetComponent<Light>();\n"
                "        if (l != null) l.intensity = 3.5f;\n"
                "        HealthStat h = GetComponent<HealthStat>();\n"
                "        if (frames == 1 && h != null) h.Damage(10f);\n"
                "    }\n"
                "    void OnTriggerEnter(GameObject other) { lastHit = other.name; }\n"
                "}\n";
            std::string err;
            auto t0 = std::chrono::steady_clock::now();
            CHECK(sc->LoadSource(mover, &err));
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("  first compile + runtime start: %.1f s%s%s\n", secs, err.empty() ? "" : " error: ", err.c_str());
            scene.Start();
            CHECK(hasLine(infos, "started on Mover"));
            for (int i = 0; i < 4; ++i) scene.Update(0.25f);
            okay::Vec3 p = go->transform->Position();
            std::printf("  after 1 s: x = %.3f, light = %.2f, health = %.1f\n", p.x, light->intensity, hp->health);
            CHECK(p.x > 1.9f && p.x < 2.1f);
            CHECK(light->intensity == 3.5f);
            CHECK(hp->health < 100.0f);
            // fields: the Inspector reads and writes them; a trigger message gets the exact other object
            CHECK(sc->VM() && sc->VM()->GetGlobal("speed").AsFloat() == 2.0f);
            sc->VM()->SetGlobal("speed", okay::vs::VsValue(4.0f));
            CHECK(sc->VM()->GetGlobal("speed").AsFloat() == 4.0f);
            sc->VM()->CallEventWith("on_trigger_enter", okay::vs::VsValue::Object(coin->InstanceId()));
            CHECK(sc->VM()->GetGlobal("lastHit").IsString() && sc->VM()->GetGlobal("lastHit").AsString() == "Coin");
            // the same text again is cached (no second SDK build)
            t0 = std::chrono::steady_clock::now();
            okay::GameObject* go2 = scene.CreateGameObject("Mover2");
            CHECK(go2->AddComponent<okay::ScriptComponent>("csharp")->LoadSource(mover, &err));
            secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("  cached load: %.2f s\n", secs);
            CHECK(secs < 2.0);
            // 2. a compile error names the file and the line
            errors.clear();
            auto* bad = scene.CreateGameObject("Bad")->AddComponent<okay::ScriptComponent>("csharp");
            bad->SetPath("Assets/Scripts/Broken.cs");
            CHECK(!bad->LoadSource("using OkayScripting;\npublic class Broken : Behaviour\n{\n    void Update() { int x = ; }\n}\n", &err));
            std::printf("  compile error: %s\n", err.c_str());
            CHECK(err.find("Assets/Scripts/Broken.cs: line 4:") != std::string::npos);
            CHECK(err.find("(CS") != std::string::npos);
            CHECK(hasLine(errors, "Assets/Scripts/Broken.cs: line 4:"));   // the Console shows it too
            // 3. an exception in the script names the script, the method and the line, and the game goes on
            errors.clear();
            auto* crash = scene.CreateGameObject("Crash")->AddComponent<okay::ScriptComponent>("csharp");
            CHECK(crash->LoadSource("using OkayScripting;\npublic class Crashy : Behaviour\n{\n    void Update()\n    {\n"
                                    "        GameObject g = Find(\"nobody\");\n        if (g == null) throw new System.InvalidOperationException(\"nobody here\");\n    }\n}\n", &err));
            scene.Update(0.016f);
            scene.Update(0.016f);
            for (auto& e : errors) std::printf("  runtime error: %s\n", e.c_str());
            CHECK(hasLine(errors, "Crashy.Update: line 7: InvalidOperationException: nobody here"));
            int crashLines = 0; for (auto& e : errors) if (e.find("Crashy.Update") != std::string::npos) ++crashLines;
            CHECK(crashLines == 1);   // logged once, not every frame
            // a binding-layer error is a plain C# exception with the engine's own words
            errors.clear();
            okay::GameObject* wgo = scene.CreateGameObject("Wrong");
            wgo->AddComponent<okay::Light>();
            auto* wrong = wgo->AddComponent<okay::ScriptComponent>("csharp");
            CHECK(wrong->LoadSource("using OkayScripting;\npublic class Wrong : Behaviour\n{\n    void Update()\n    {\n"
                                    "        GetComponent<Light>().Set(\"intensity\", \"bright\");\n    }\n}\n", &err));
            scene.Update(0.016f);
            for (auto& e : errors) if (e.find("Wrong.") != std::string::npos) std::printf("  binding error: %s\n", e.c_str());
            CHECK(hasLine(errors, "Wrong.Update: line 6: InvalidOperationException"));
            CHECK(hasLine(errors, "intensity"));
            // 4. hot reload: new code, the field value kept
            fs::path file = cache / "HotMover.cs";
            fs::create_directories(cache);
            std::ofstream(file) << mover;
            okay::GameObject* hot = scene.CreateGameObject("Hot");
            auto* hsc = hot->AddComponent<okay::ScriptComponent>("csharp");
            CHECK(hsc->LoadFile(file.string(), &err));
            hsc->VM()->SetGlobal("speed", okay::vs::VsValue(7.0f));
            std::string v2 = mover;
            const std::string xMove = "new Vector3(speed * Time.deltaTime, 0f, 0f)";
            v2.replace(v2.find(xMove), xMove.size(), "new Vector3(0f, speed * Time.deltaTime, 0f)");
            std::ofstream(file) << v2;
            CHECK(hsc->VM()->SupportsHotReload());
            CHECK(hsc->Reload(&err));
            CHECK(hsc->VM()->GetGlobal("speed").AsFloat() == 7.0f);
            hot->transform->localPosition = {0, 0, 0};
            scene.Update(0.5f);
            std::printf("  after hot reload: (%.2f, %.2f)\n", hot->transform->Position().x, hot->transform->Position().y);
            CHECK(hot->transform->Position().y > 3.4f && hot->transform->Position().x == 0.0f);
            std::ofstream(file) << "public class Mover : Behaviour { oops }";
            CHECK(!hsc->Reload(&err));                                  // a broken file: the old code keeps running
            scene.Update(0.5f);
            CHECK(hot->transform->Position().y > 6.9f);
            // 5. export: the compiled scripts, OkayScripting.dll and the runtime with its licence files
            fs::path game = fs::temp_directory_path() / "okay_csharp_real_export";
            fs::remove_all(game);
            std::string apiErr;
            std::string api = EnsureApiAssembly(ApiSourceDir(), DefaultCacheDir(), env, &apiErr);
            CHECK(!api.empty());
            ExportReport rep = ExportCSharp(CollectCSharpSources(scene), game.string(), env, DefaultCacheDir(), api);
            for (auto& pr : rep.problems) std::printf("  export problem: %s\n", pr.c_str());
            CHECK(rep.needed);
            CHECK(rep.problems.size() == 1 && rep.problems[0].find("'Broken'") != std::string::npos);   // the one with errors is named
            CHECK(fs::exists(game / "OkayScripting.dll"));
            CHECK(fs::exists(game / "Scripts" / (ContentHash(mover) + ".dll")));
            CHECK(fs::exists(game / "runtime" / "LICENSE.txt"));
            CHECK(fs::exists(game / "runtime" / "ThirdPartyNotices.txt"));
            std::string fv; CHECK(!FindHostfxr([&] { DotnetEnv g; g.gameDir = game.string(); g.systemPlaces = false; return g; }(), &fv).empty());
            std::printf("  export: %zu files\n", rep.files.size());
            fs::remove_all(game);
            okay::Log::sink = nullptr;
            fs::remove_all(cache);
        }
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
