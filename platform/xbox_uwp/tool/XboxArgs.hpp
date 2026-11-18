#pragma once
// Command-line entry for the small Xbox tools. With cl.exe (MSVC) `main` receives the arguments in the ANSI code page, which breaks a non-ASCII
// user folder; here `wmain` converts them to UTF-8 (the tools read every path with std::filesystem::u8path). Elsewhere a plain `main`.
// Usage in a tool: `int ToolMain(int argc, char** argv)` instead of main, then `#include "XboxArgs.hpp"` AFTER it is declared... (declared below).
#include <string>
#include <vector>
int ToolMain(int argc, char** argv);
#if defined(_WIN32) && defined(_MSC_VER)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> store; store.reserve((std::size_t)argc);
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s((std::size_t)(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], n, nullptr, nullptr);
        store.push_back(s);
    }
    std::vector<char*> ptrs; for (std::string& s : store) ptrs.push_back(&s[0]); ptrs.push_back(nullptr);
    return ToolMain(argc, ptrs.data());
}
#else
int main(int argc, char** argv) { return ToolMain(argc, argv); }
#endif
