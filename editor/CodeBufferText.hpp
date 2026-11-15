#pragma once
// The script editor's text buffers, one per open script, kept apart from the GUI so a test can run them.
//
// Each buffer is a NUL-terminated char array: the code widget, Play, Save and the problem checker all read it as a C
// string. Two things used to cut a script silently, and Save then wrote the shorter text back to disk:
//   - the buffer was a fixed 65,536 bytes, so a bigger script lost its end;
//   - a NUL byte inside the file ends a C string, so everything after it was gone.
// Now the buffer grows to fit the text, and a text with a NUL byte is marked: LosesBytes() is true for that script, the
// editor shows it read-only and every save of it is refused (SaveRefusal() gives the reason for the Console).
// The mark stays until the buffer is erased (the tab is closed): the code widget writes its text back into the buffer,
// and that text is already cut at the NUL, so it must not clear the mark.
//
// Written from the behaviour above; no outside code.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace okay::editor {

class CodeBufferStore {
public:
    /// Smallest buffer, as before (the widget and older code expect at least this much).
    static constexpr std::size_t kMinBytes = 65536;

    /// Copy `text` into `b`: the whole text, then zeros. Grows `b` when the text does not fit.
    static void Fill(std::vector<char>& b, const std::string& text) {
        const std::size_t need = std::max(kMinBytes, text.size() + 1);
        if (b.size() < need) b.assign(need, 0);
        else std::fill(b.begin(), b.end(), 0);
        if (!text.empty()) std::memcpy(b.data(), text.data(), text.size());
    }

    /// True when `text` holds a NUL byte, so a C-string buffer cannot keep all of it.
    static bool HasNul(const std::string& text) { return text.find('\0') != std::string::npos; }

    /// The live text, or nullptr when this script has no buffer yet.
    const char* Peek(const void* key) const {
        auto it = bufs_.find(key);
        return it == bufs_.end() ? nullptr : it->second.data();
    }

    /// The buffer of `key`, made from `initial` the first time.
    std::vector<char>& Get(const void* key, const std::string& initial) {
        auto it = bufs_.find(key);
        if (it == bufs_.end()) {
            std::vector<char> b;
            Fill(b, initial);
            Mark(key, initial);
            it = bufs_.emplace(key, std::move(b)).first;
        }
        return it->second;
    }

    /// Replace the text of `key` (creates the buffer when missing).
    void Set(const void* key, const std::string& text) {
        Fill(bufs_[key], text);
        Mark(key, text);
    }

    /// True when a text put into this buffer since it was made had a NUL byte: the buffer holds less than that text, so
    /// saving the buffer would cut the file. The editor shows such a script read-only and refuses to save it.
    bool LosesBytes(const void* key) const { return lossy_.count(key) != 0; }

    /// "" when the buffer of `key` may be written to `path`; otherwise the Console message that says why not.
    std::string SaveRefusal(const void* key, const std::string& path) const {
        if (!LosesBytes(key)) return {};
        return "Not saved: " + (path.empty() ? std::string("this script") : path) +
               " contains a NUL byte, which the script editor cannot keep, so saving would cut the file there. "
               "It is open read-only; edit it in another editor.";
    }

    /// Forget the buffer of `key` (the script was closed or deleted).
    void Erase(const void* key) { bufs_.erase(key); lossy_.erase(key); }

private:
    void Mark(const void* key, const std::string& text) {
        if (HasNul(text)) lossy_.insert(key);   // sticky: see the note at the top
    }

    std::unordered_map<const void*, std::vector<char>> bufs_;
    std::unordered_set<const void*> lossy_;
};

} // namespace okay::editor
