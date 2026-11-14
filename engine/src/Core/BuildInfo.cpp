#include "okay/Core/BuildInfo.hpp"
#include <cstdlib>

#ifndef OKAY_ENGINE_VERSION
#  define OKAY_ENGINE_VERSION "dev"
#endif
#ifndef OKAY_BUILD_COMMIT
#  define OKAY_BUILD_COMMIT "unknown"
#endif

#define OKAY_STR2(x) #x
#define OKAY_STR(x) OKAY_STR2(x)

namespace okay {

const char* BuildInfo::Version() { return OKAY_ENGINE_VERSION; }
const char* BuildInfo::Commit()  { return OKAY_BUILD_COMMIT; }

const char* BuildInfo::Compiler() {
#if defined(__clang__)
    return "Clang " OKAY_STR(__clang_major__) "." OKAY_STR(__clang_minor__);
#elif defined(__GNUC__)
#  if defined(__MINGW32__)
    return "MinGW GCC " OKAY_STR(__GNUC__) "." OKAY_STR(__GNUC_MINOR__);
#  else
    return "GCC " OKAY_STR(__GNUC__) "." OKAY_STR(__GNUC_MINOR__);
#  endif
#elif defined(_MSC_VER)
    return "MSVC " OKAY_STR(_MSC_VER);
#else
    return "unknown compiler";
#endif
}

const char* BuildInfo::Config() {
#if defined(NDEBUG)
    return "Release";
#else
    return "Debug";
#endif
}

const char* BuildInfo::Platform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

const char* BuildInfo::Architecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}

std::string BuildInfo::Summary() {
    return std::string("OkaySpace ") + Version() + " (" + Config() + ", " + Compiler() + ", " +
           Platform() + " " + Architecture() + ")";
}

bool BuildInfo::ParseVersion(const std::string& text, int& major, int& minor, int& patch) {
    int part[3] = {0, 0, 0}; int n = 0;
    std::size_t i = 0;
    if (text.empty()) return false;
    while (i < text.size() && n < 3) {
        if (text[i] < '0' || text[i] > '9') return false;
        long v = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            v = v * 10 + (text[i] - '0');
            if (v > 1000000000L) return false;
            ++i;
        }
        part[n++] = (int)v;
        if (i < text.size()) { if (text[i] != '.') return false; ++i; if (i >= text.size()) return false; }
    }
    if (i < text.size()) return false;   // more than three parts
    major = part[0]; minor = part[1]; patch = part[2];
    return true;
}

int BuildInfo::CompareVersions(const std::string& a, const std::string& b) {
    int a0 = 0, a1 = 0, a2 = 0, b0 = 0, b1 = 0, b2 = 0;
    const bool okA = ParseVersion(a, a0, a1, a2), okB = ParseVersion(b, b0, b1, b2);
    if (!okA || !okB) return okA == okB ? 0 : (okA ? 1 : -1);   // a non-numeric ("dev") sorts lowest
    if (a0 != b0) return a0 < b0 ? -1 : 1;
    if (a1 != b1) return a1 < b1 ? -1 : 1;
    if (a2 != b2) return a2 < b2 ? -1 : 1;
    return 0;
}

bool BuildInfo::IsCompatible(const std::string& theirs) {
    int a0, a1, a2, b0, b1, b2;
    if (!ParseVersion(theirs, a0, a1, a2) || !ParseVersion(Version(), b0, b1, b2)) return false;
    return a0 == b0 && a1 == b1;
}

} // namespace okay
