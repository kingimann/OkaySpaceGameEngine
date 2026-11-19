// C# host, stage 1 (pure logic, runs without .NET): runtime search order, version choice,
// runtimeconfig.json text, plain-words errors, hostfxr discovery in a fake install folder.
#include "test_framework.hpp"
#include "okay/CSharp/DotnetHost.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace okay::csharp;
namespace fs = std::filesystem;

static void Touch(const fs::path& p) { fs::create_directories(p.parent_path()); std::ofstream(p) << "x"; }

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
    {   // not compiled in: stub says so
        if (!DotnetRuntime::CompiledIn()) {
            std::string err;
            CHECK(DotnetRuntime::Get(CurrentDotnetEnv(), &err) == nullptr);
            CHECK(err.find("not enabled in this build") != std::string::npos);
        }
    }
    TEST_MAIN_RESULT();
}
