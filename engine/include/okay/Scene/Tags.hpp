#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// The type of GameObject::tag: a std::string that counts its assignments, so a
/// Scene can keep a tag -> objects index and rebuild it only when some tag changed.
/// It behaves exactly like a std::string everywhere else.
class TagString : public std::string {
public:
    TagString() = default;
    TagString(const std::string& s) : std::string(s) {}
    TagString(const char* s) : std::string(s ? s : "") {}
    TagString(const TagString&) = default;
    TagString& operator=(const TagString& o) { std::string::operator=(o); Touch(); return *this; }
    TagString& operator=(const std::string& s) { std::string::operator=(s); Touch(); return *this; }
    TagString& operator=(std::string&& s) { std::string::operator=(std::move(s)); Touch(); return *this; }
    TagString& operator=(const char* s) { std::string::operator=(s ? s : ""); Touch(); return *this; }
    /// Bumped by every assignment to any tag in the process.
    static std::uint64_t Version() { return Counter().load(std::memory_order_relaxed); }
private:
    static std::atomic<std::uint64_t>& Counter() { static std::atomic<std::uint64_t> c{1}; return c; }
    static void Touch() { Counter().fetch_add(1, std::memory_order_relaxed); }
};

/// The project's named tag list. Tags on objects stay free strings (scripts and
/// components assign runtime tags such as "Debris"); the list is what editors offer
/// and what Accepts() checks when the project asks for strict tags.
struct Tags {
    static constexpr std::size_t kMaxLength = 64;

    /// A usable tag name: 1..kMaxLength characters, no control characters, and no
    /// leading or trailing space (so "Player" and "Player " can't be two tags).
    static bool IsValidName(const std::string& t) {
        if (t.empty() || t.size() > kMaxLength) return false;
        if (t.front() == ' ' || t.back() == ' ') return false;
        for (unsigned char c : t) if (c < 0x20 || c == 0x7F) return false;
        return true;
    }
    /// The built-in tags every project starts with.
    static const std::vector<std::string>& Builtin() {
        static const std::vector<std::string> b = {"Player", "MainCamera", "Enemy", "Respawn", "Finish", "GameController", "UI"};
        return b;
    }
    /// The custom tags the project added (the built-ins are not stored here).
    static std::vector<std::string>& Custom() { static std::vector<std::string> v; return v; }
    /// Strict mode: GameObject::SetTag refuses a tag that is not in the list.
    static bool& Strict() { static bool s = false; return s; }

    static bool Contains(const std::string& t) {
        const auto& b = Builtin(); const auto& c = Custom();
        return std::find(b.begin(), b.end(), t) != b.end() || std::find(c.begin(), c.end(), t) != c.end();
    }
    /// Add a custom tag. False when the name is invalid; true when added or already listed.
    static bool Add(const std::string& t) {
        if (!IsValidName(t)) return false;
        if (!Contains(t)) Custom().push_back(t);
        return true;
    }
    /// Remove a custom tag from the list (objects keep it). Built-ins can't be removed.
    static bool Remove(const std::string& t) {
        auto& c = Custom();
        auto it = std::find(c.begin(), c.end(), t);
        if (it == c.end()) return false;
        c.erase(it);
        return true;
    }
    /// Every tag in the list: built-ins first, then custom ones in the order added.
    static std::vector<std::string> All() {
        std::vector<std::string> out = Builtin();
        out.insert(out.end(), Custom().begin(), Custom().end());
        return out;
    }
    /// Would SetTag take this? The empty string (untagged) always passes.
    static bool Accepts(const std::string& t) {
        if (t.empty()) return true;
        return IsValidName(t) && (!Strict() || Contains(t));
    }
};

} // namespace okay
