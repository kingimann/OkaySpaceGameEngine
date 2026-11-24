// okay: the OkaySpace engine's command line (okay.exe on Windows). Everything is in Cli.cpp; this only reads the
// arguments (as UTF-8 on Windows) and prints. Our own code.
#include "Cli.hpp"

#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char** argv) {
    std::vector<std::string> args;
#if defined(_WIN32)
    (void)argc; (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    int n = 0;
    if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 1; i < n; ++i) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 0 ? (std::size_t)len - 1 : 0, '\0');
            if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
            args.push_back(s);
        }
        LocalFree(w);
    }
#else
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
#endif
    okaycli::Io io;
    return okaycli::Main(args, io);
}
