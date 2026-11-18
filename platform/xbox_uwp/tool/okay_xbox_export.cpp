// okay-xbox-export: writes a minimal TEST Xbox export folder from the command line (no editor needed). See XboxExportTool.hpp.
// Links only the engine's image files (placeholder tile PNGs): scripts/build-xbox-uwp.ps1 -MinimalTest compiles it with cl.exe.
#include "XboxExportTool.hpp"
#include <cstdio>

int ToolMain(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    okay::xbox::ExportToolOptions o; std::string err;
    if (!okay::xbox::ParseExportArgs(a, o, err)) { std::fprintf(stderr, "okay-xbox-export: %s\n%s", err.c_str(), okay::xbox::ExportToolUsage()); return 2; }
    if (o.help) { std::fputs(okay::xbox::ExportToolUsage(), stdout); return 0; }
    std::string msg; const bool ok = okay::xbox::WriteTestExport(o, msg);
    std::printf("%s%s\n", ok ? "okay-xbox-export: " : "okay-xbox-export FAILED: ", msg.c_str());
    return ok ? 0 : 1;
}

#include "XboxArgs.hpp"   // main / wmain (UTF-8 arguments on Windows)
