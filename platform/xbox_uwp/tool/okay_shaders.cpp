// okay-shaders: build-time helper for the Xbox UWP app's offline shaders. Plain C++17, no engine link, no Direct3D.
// Written from Microsoft's fxc.exe documentation (the command line is in scripts/xbox-compile-shaders.ps1) and our own
// ShaderBundle.hpp. UNVERIFIED on Windows (the tests run it on Linux with fake .cso files).
//   okay-shaders dump <dir>            writes okay3d.hlsl (the engine's HLSL text) and entries.txt ("<entry> <profile>" per line)
//   okay-shaders manifest <dir>        reads <dir>\okay3d_<entry>.cso, writes <dir>\shaders.manifest (needs okay3d.hlsl in <dir>)
//   okay-shaders verify <dir>          loads <dir> exactly like the app would; exit 0 = the bundle is usable
#include "okay/Render/D3D11Hlsl.hpp"
#include "okay/Render/ShaderBundle.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace okay::shaderbundle;

static bool ReadAll(const std::string& p, std::vector<unsigned char>& out) {
    std::ifstream in(std::filesystem::u8path(p), std::ios::binary); if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); return true;
}
static bool WriteAll(const std::string& p, const std::string& s) { std::ofstream o(std::filesystem::u8path(p), std::ios::binary); if (!o) return false; o << s; return (bool)o; }

int ToolMain(int argc, char** argv) {
    if (argc != 3) { std::fputs("usage: okay-shaders dump|manifest|verify <dir>\n", stderr); return 2; }
    const std::string cmd = argv[1], dir = argv[2];
    const std::string sep = "/";
    if (cmd == "dump") {
        std::string list;
        for (const EntryReq& q : Entries()) list += std::string(q.entry) + " " + q.profile + (q.required ? " required\n" : " optional\n");
        if (!WriteAll(dir + sep + "okay3d.hlsl", okay::D3D11HlslSource()) || !WriteAll(dir + sep + "entries.txt", list)) { std::fputs("cannot write into the folder\n", stderr); return 1; }
        std::printf("wrote okay3d.hlsl (%zu bytes) and entries.txt (%zu entries)\n", std::string(okay::D3D11HlslSource()).size(), Entries().size());
        return 0;
    }
    auto rd = [&](const std::string& n, std::vector<unsigned char>& b) { return ReadAll(dir + sep + n, b); };
    if (cmd == "manifest") {
        // The text must be the one this program was built from: the manifest records its hash.
        Manifest m; std::vector<std::string> warn; std::string err;
        if (!BuildManifest(rd, okay::D3D11HlslSource(), m, warn, &err)) { std::fprintf(stderr, "okay-shaders: %s\n", err.c_str()); return 1; }
        for (const std::string& w : warn) std::printf("warning: %s\n", w.c_str());
        if (!WriteAll(dir + sep + "shaders.manifest", WriteManifest(m))) { std::fputs("cannot write shaders.manifest\n", stderr); return 1; }
        std::printf("wrote shaders.manifest with %zu shaders\n", m.records.size());
        return 0;
    }
    if (cmd == "verify") {
        Bundle b; std::string err;
        if (!b.Load(rd, okay::D3D11HlslSource(), &err)) { std::fprintf(stderr, "okay-shaders verify FAILED: %s\n", err.c_str()); return 1; }
        for (const std::string& w : b.Warnings()) std::printf("warning: %s\n", w.c_str());
        std::printf("okay-shaders verify OK: %zu shaders\n", b.Count());
        return 0;
    }
    std::fputs("unknown command\n", stderr); return 2;
}

#include "XboxArgs.hpp"   // main / wmain (UTF-8 arguments on Windows)
