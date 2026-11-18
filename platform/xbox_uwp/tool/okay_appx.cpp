// okay-appx: our own APPX packer and checker (EXPERIMENTAL, UNVERIFIED on Windows). No MakeAppx.
//   okay-appx pack <folder> <out.appx>    writes an UNSIGNED package from every file in <folder>
//   okay-appx verify <file.appx>          reads it back and checks the container, content types and every block hash
// Signing is not done here (docs/xbox_uwp.md, "Signing"): use SignTool on the result.
#include "XboxAppx.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    using namespace okay::xbox::appx;
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "pack" && argc == 4) {
        PackResult r = PackFolder(argv[2]);
        if (!r.ok) { std::fprintf(stderr, "okay-appx: %s\n", r.error.c_str()); return 1; }
        std::ofstream o(std::filesystem::u8path(argv[3]), std::ios::binary | std::ios::trunc);
        o.write(r.bytes.data(), static_cast<std::streamsize>(r.bytes.size()));
        if (!o) { std::fprintf(stderr, "okay-appx: cannot write %s\n", argv[3]); return 1; }
        o.close();
        VerifyResult v = Verify(r.bytes);   // never hand out a package that fails our own check
        if (!v.ok) { for (auto& p : v.problems) std::fprintf(stderr, "okay-appx: self-check: %s\n", p.c_str()); return 1; }
        std::printf("okay-appx: wrote UNSIGNED package %s (%d files, %llu blocks, %zu bytes). It must be signed before Windows will install it.\n",
                    argv[3], r.files, static_cast<unsigned long long>(r.blocks), r.bytes.size());
        return 0;
    }
    if (cmd == "verify" && argc == 3) {
        std::ifstream f(std::filesystem::u8path(argv[2]), std::ios::binary);
        if (!f) { std::fprintf(stderr, "okay-appx: cannot read %s\n", argv[2]); return 1; }
        std::stringstream ss; ss << f.rdbuf();
        VerifyResult v = Verify(ss.str());
        for (auto& p : v.problems) std::printf("PROBLEM: %s\n", p.c_str());
        std::printf("okay-appx: %s (%d files, %llu blocks checked, %s)\n", v.ok ? "OK" : "FAILED", v.files,
                    static_cast<unsigned long long>(v.blocks), v.signedPackage ? "signature part present, not checked" : "unsigned");
        return v.ok ? 0 : 1;
    }
    std::fprintf(stderr, "usage: okay-appx pack <folder> <out.appx> | okay-appx verify <file.appx>\n");
    return 2;
}
