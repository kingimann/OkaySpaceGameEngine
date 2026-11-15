#pragma once
// What the field menu's Paste Value does with the text on the system clipboard, as plain data, so the rules can be tested without a
// window and without the engine. The clipboard is whatever the person last copied in any program, so it is treated as untrusted:
//   - it is read ONCE, when the menu opens (not every frame), and at most kMaxClipboardChars of it are looked at;
//   - text that is empty, too long or on several lines for a one-line field is refused before anything parses it;
//   - a file path must stay inside the project: no "..", no network path, no absolute path outside the project folder;
//   - a refusal says why in plain words, and a paste that is allowed shows what it would paste.
// What a value of each kind has to look like (a number, 3 numbers, a colour ...) is the property registry's business: see
// oked::CheckPaste in InspectorModel.hpp, which runs these checks first.
//
// No ImGui, no engine, no <mutex> / <thread> (they cannot be built for the Windows programs).
// Written for the Inspector menus review of 2026-11-15 (finding 6) from its description of what could go wrong; the limits are ours.
#include <cstddef>
#include <cstdio>
#include <string>

namespace okay::inspectorctx {

/// The most clipboard text a field menu looks at (64 KiB). A number, a vector, a colour or a path is never this long; a text field
/// takes a few pages at most. More than this is refused, not cut: half a text would be pasted without anyone noticing.
constexpr std::size_t kMaxClipboardChars = 64 * 1024;
/// The longest file path Paste Value takes.
constexpr std::size_t kMaxPastedPathChars = 1024;

/// The clipboard as it was when the menu opened.
struct ClipboardText {
    std::string text;        ///< the first kMaxClipboardChars characters (all of it when it is shorter)
    bool tooLong = false;    ///< the clipboard held more than that; `text` is only the start
    bool present = false;    ///< the clipboard held text at all (not null, not empty)
};

/// Take at most kMaxClipboardChars of a clipboard string, without walking the rest of it (a clipboard can hold a whole file).
inline ClipboardText ReadClipboard(const char* raw) {
    ClipboardText c;
    if (!raw || !raw[0]) return c;
    c.present = true;
    std::size_t n = 0;
    while (n <= kMaxClipboardChars && raw[n]) ++n;   // stops one past the limit: enough to know it is too long
    c.tooLong = n > kMaxClipboardChars;
    c.text.assign(raw, c.tooLong ? kMaxClipboardChars : n);
    return c;
}

/// "64 KB", "2.3 MB" ... for a message. `bytes` is rounded; below 1 KB it says "N bytes".
inline std::string HumanSize(std::size_t bytes) {
    char b[32];
    if (bytes < 1024) std::snprintf(b, sizeof(b), "%zu bytes", bytes);
    else if (bytes < 1024 * 1024) std::snprintf(b, sizeof(b), "%.0f KB", (double)bytes / 1024.0);
    else std::snprintf(b, sizeof(b), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    return b;
}

/// What the first look at the clipboard text found.
enum class PasteGate { Ok, Nothing, TooLong, SeveralLines, ControlChars };

struct GateResult {
    PasteGate gate = PasteGate::Ok;
    std::string text;      ///< Ok: the text to parse, trimmed of spaces and line ends at both ends
    std::string reason;    ///< not Ok: why, in plain words (the tooltip of the greyed Paste Value)
};

namespace detail {
inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
}   // namespace detail

/// The first look at the clipboard text for a field: is there something, is it a sensible size, is it one line (when the field is
/// one line) without control characters. `multiLineField`: the field is a multi-line text box (it takes line ends and tabs).
inline GateResult GateClipboard(const ClipboardText& clip, bool multiLineField) {
    GateResult r;
    if (!clip.present) {
        r.gate = PasteGate::Nothing;
        r.reason = "The clipboard holds no text. Use Copy Value on a field of the same kind first.";
        return r;
    }
    if (clip.tooLong) {
        r.gate = PasteGate::TooLong;
        r.reason = "The clipboard holds more than " + HumanSize(kMaxClipboardChars) + " of text, too much for a field. Copy a shorter text.";
        return r;
    }
    std::size_t b = 0, e = clip.text.size();
    while (b < e && detail::IsSpace(clip.text[b])) ++b;
    while (e > b && detail::IsSpace(clip.text[e - 1])) --e;
    if (b == e) {
        r.gate = PasteGate::Nothing;
        r.reason = "The clipboard holds only blank text. Use Copy Value on a field of the same kind first.";
        return r;
    }
    r.text = clip.text.substr(b, e - b);
    if (!multiLineField) {
        for (char c : r.text) {
            if (c == '\n' || c == '\r') {
                r.gate = PasteGate::SeveralLines;
                r.reason = "The clipboard holds several lines. This field takes one line.";
                r.text.clear();
                return r;
            }
        }
        for (unsigned char c : r.text) {
            if (c < 0x20 || c == 0x7F) {
                r.gate = PasteGate::ControlChars;
                r.reason = "The clipboard holds control characters (a tab or an invisible mark). This field cannot take them.";
                r.text.clear();
                return r;
            }
        }
    }
    return r;
}

/// The text as one short line for a tooltip: line ends and tabs show as spaces, and it is cut with "..." after `maxChars` characters
/// (never in the middle of a multi-byte UTF-8 character).
inline std::string PreviewText(const std::string& text, std::size_t maxChars = 48) {
    std::string out;
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char c = (unsigned char)text[i];
        const bool lead = (c & 0xC0) != 0x80;   // not a continuation byte: a new character starts
        if (lead) {
            if (chars == maxChars) { out += "..."; return out; }
            ++chars;
        }
        out += (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    return out;
}

// ---- File paths ---------------------------------------------------------------------------------------------------------

enum class PathCheck { Ok, TooLong, ControlChars, Network, ParentFolder, OutsideProject, NoProject };

struct PathVerdict {
    PathCheck check = PathCheck::Ok;
    std::string reason;    ///< not Ok: why, in plain words
    bool Ok() const { return check == PathCheck::Ok; }
};

namespace detail {
inline std::string Slashes(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}
inline char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
}   // namespace detail

/// May this text be pasted into a file-path field? A path has to stay inside the project:
///   - a network path (\\server\share or //server/share) is refused: the editor would try to open a file on another computer;
///   - a path with a ".." part is refused: it can climb out of the project folder;
///   - an absolute path (/x, C:\x) is refused unless it lies inside `projectDir` (that is the form the Project panel itself stores
///     when a file is picked or dropped, so Copy Value on one file field can be pasted into another); with `allowAbsoluteInProject`
///     false every absolute path is refused and only project-relative paths ("Assets/hero.png") pass;
///   - nothing is accepted when it holds control characters or is longer than kMaxPastedPathChars.
inline PathVerdict CheckAssetPath(const std::string& path, const std::string& projectDir, bool allowAbsoluteInProject = true) {
    PathVerdict v;
    if (path.size() > kMaxPastedPathChars) {
        v.check = PathCheck::TooLong;
        v.reason = "That path is longer than " + std::to_string(kMaxPastedPathChars) + " characters.";
        return v;
    }
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7F) {
            v.check = PathCheck::ControlChars;
            v.reason = "A path cannot hold control characters.";
            return v;
        }
    }
    const std::string p = detail::Slashes(path);
    if (p.size() >= 2 && p[0] == '/' && p[1] == '/') {
        v.check = PathCheck::Network;
        v.reason = "A network path (\\\\server\\share) cannot be pasted: the editor would reach out to another computer. Use a file of the project.";
        return v;
    }
    for (std::size_t i = 0; i < p.size();) {   // any ".." between slashes (or at either end)
        std::size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        if (j - i == 2 && p[i] == '.' && p[i + 1] == '.') {
            v.check = PathCheck::ParentFolder;
            v.reason = "A path with \"..\" can leave the project. Paste a path inside the project folder.";
            return v;
        }
        i = j + 1;
    }
    const bool drive = p.size() >= 2 && p[1] == ':' && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'));
    const bool absolute = (!p.empty() && p[0] == '/') || drive;
    if (!absolute) return v;   // project-relative: fine
    if (!allowAbsoluteInProject) {
        v.check = PathCheck::OutsideProject;
        v.reason = "Only paths relative to the project folder can be pasted (for example Assets/hero.png).";
        return v;
    }
    if (projectDir.empty()) {
        v.check = PathCheck::NoProject;
        v.reason = "Open a project first: a pasted path has to be inside the project folder.";
        return v;
    }
    std::string dir = detail::Slashes(projectDir);
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    const bool windowsForm = drive;   // drive letters are not case sensitive; neither are the folder names after them
    bool inside = p.size() > dir.size() && p[dir.size()] == '/';
    for (std::size_t i = 0; inside && i < dir.size(); ++i)
        if (windowsForm ? detail::LowerAscii(p[i]) != detail::LowerAscii(dir[i]) : p[i] != dir[i]) inside = false;
    if (!inside) {
        v.check = PathCheck::OutsideProject;
        v.reason = "That path is outside the project folder. Paste a path inside the project.";
        return v;
    }
    return v;
}

}   // namespace okay::inspectorctx
