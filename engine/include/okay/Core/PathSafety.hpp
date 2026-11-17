#pragma once
// One place that decides whether a file name that came from a script, a visual-script graph, an action list, a
// plugin or a network peer may be opened. Docs: docs/security.md "Script file paths". Written from the Windows file
// naming rules (Microsoft's "Naming Files, Paths, and Namespaces" page) and POSIX path rules; no other project's code.
//
// A name passes only if it is a RELATIVE path that stays inside the folder it is resolved against:
//   * not empty, at most kMaxPathBytes long, no NUL and no control character (a NUL would end the name early in
//     the C library and make "ok\0../x" look like "ok");
//   * not absolute: no leading '/' or '\' (this also covers UNC "\\server\share", "\\?\C:\x" and "\\.\COM1");
//   * no ".." part (any separator in front of it or behind it);
//   * Windows and Portable modes also refuse: a ':' anywhere (drive letter "C:x", NTFS alternate data stream
//     "file.txt:hidden", "NUL:"), the characters < > " | ? *, a part that ends in '.' or ' ' (Windows silently
//     strips them, so "x." is "x" and "..." is ".."), and the reserved device names CON PRN AUX NUL COM1-9 LPT1-9
//     CONIN$ CONOUT$, with or without an extension ("nul.txt" is still the NUL device), in any letter case.
//
// Modes:
//   Portable (the default, and what every script-facing API uses): all of the above. A game made on Linux must still
//            be safe when it runs on Windows, and a save written on Windows must still be readable on Linux.
//   Windows: the Windows rules only ('/' and '\' both separate parts).
//   Posix:   only the rules every system shares (empty, too long, NUL / control, leading '/', ".."). '\' is an ordinary
//            character. Used by tests, and by code that really does run only on POSIX.
//
// StaysInside() adds the check a string rule cannot make: an existing symbolic link (or Windows junction) inside the
// folder that points outside it. Header only; <filesystem> but no threads, so every program may include it.
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace okay::pathsafety {

inline constexpr std::size_t kMaxPathBytes = 240;   ///< below Windows' 260 so a folder can be put in front

enum class Mode { Portable, Windows, Posix };

enum class Why {
    Ok, Empty, TooLong, Nul, ControlChar, Absolute, ParentRef, DriveOrStream, BadChar, TrailingDotOrSpace, DeviceName,
};

inline const char* Describe(Why w) {
    switch (w) {
        case Why::Ok: return "ok";
        case Why::Empty: return "empty name";
        case Why::TooLong: return "name too long";
        case Why::Nul: return "contains a NUL byte";
        case Why::ControlChar: return "contains a control character";
        case Why::Absolute: return "absolute path (use a name or a relative path)";
        case Why::ParentRef: return "contains '..'";
        case Why::DriveOrStream: return "contains ':' (a drive letter or a hidden stream)";
        case Why::BadChar: return "contains a character Windows cannot store";
        case Why::TrailingDotOrSpace: return "a part ends in a dot or a space";
        case Why::DeviceName: return "a reserved device name (CON, NUL, COM1 ...)";
    }
    return "refused";
}

namespace detail {
inline char Low(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
inline bool EqNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (Low(a[i]) != Low(b[i])) return false;
    return true;
}
// The part up to the first '.' is the device name Windows matches ("nul.txt" is NUL); trailing spaces before the dot count too.
inline bool IsDeviceName(std::string_view part) {
    std::string_view stem = part.substr(0, part.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.remove_suffix(1);
    static constexpr std::string_view kDev[] = {"con", "prn", "aux", "nul", "conin$", "conout$"};
    for (auto d : kDev) if (EqNoCase(stem, d)) return true;
    if (stem.size() == 4 && (EqNoCase(stem.substr(0, 3), "com") || EqNoCase(stem.substr(0, 3), "lpt")) && stem[3] >= '1' && stem[3] <= '9')
        return true;
    return false;
}
}  // namespace detail

/// Why `name` is refused (Why::Ok when it passes).
inline Why Check(std::string_view name, Mode mode = Mode::Portable) {
    if (name.empty()) return Why::Empty;
    if (name.size() > kMaxPathBytes) return Why::TooLong;
    for (unsigned char c : name) {
        if (c == 0) return Why::Nul;
        if (c < 0x20 || c == 0x7f) return Why::ControlChar;
    }
    const bool win = mode != Mode::Posix;
    const std::string_view seps = win ? std::string_view("/\\") : std::string_view("/");
    if (seps.find(name[0]) != std::string_view::npos) return Why::Absolute;
    if (win) {
        for (char c : name) {
            if (c == ':') return Why::DriveOrStream;
            if (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return Why::BadChar;
        }
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find_first_of(seps, start);
        if (end == std::string_view::npos) end = name.size();
        const std::string_view part = name.substr(start, end - start);
        if (part == "..") return Why::ParentRef;
        if (win && !part.empty() && part != ".") {
            if (part.back() == '.' || part.back() == ' ') return Why::TrailingDotOrSpace;
            if (detail::IsDeviceName(part)) return Why::DeviceName;
        }
        start = end + 1;
    }
    return Why::Ok;
}

inline bool IsSafeRelative(std::string_view name, Mode mode = Mode::Portable) { return Check(name, mode) == Why::Ok; }

/// root / name, with every existing link along the way resolved, when the result is still inside `root` (and is not
/// `root` itself); false otherwise (`why` says which rule). The name rules above run first. Nothing is created.
inline bool StaysInside(const std::filesystem::path& root, std::string_view name, std::filesystem::path* out = nullptr,
                        std::string* why = nullptr, Mode mode = Mode::Portable) {
    namespace fs = std::filesystem;
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (const Why w = Check(name, mode); w != Why::Ok) return no(Describe(w));
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(root.empty() ? fs::current_path(ec) : root, ec);
    if (ec || base.empty()) return no("the folder cannot be resolved");
    const fs::path full = fs::weakly_canonical(base / fs::u8path(std::string(name)), ec);
    if (ec) return no("the path cannot be resolved");
    auto b = base.begin(), f = full.begin();
    for (; b != base.end(); ++b, ++f) {
        if (b->empty() && std::next(b) == base.end()) break;   // a trailing separator on the root
        if (f == full.end() || *f != *b) return no("resolves outside the folder (a link points out of it)");
    }
    if (f == full.end()) return no("resolves to the folder itself");
    if (out) *out = full;
    return true;
}

}  // namespace okay::pathsafety
