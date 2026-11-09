#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// The type of GameObject::tag: a std::string that counts its changes, so a Scene can keep
/// a tag -> objects index and rebuild it only when some tag changed. It behaves exactly like
/// a std::string everywhere else. Assignment and every std::string call that changes the text
/// (clear, +=, append, assign, insert, erase, replace, resize, push_back, pop_back, swap, a
/// non-const operator[] / at / front / back / begin / end / data) is counted; a change made
/// through a plain `std::string&` taken from it is not (Scene lookups still check what they find).
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

    void clear() noexcept { Touch(); std::string::clear(); }
    template <class T> TagString& operator+=(T&& v) { Touch(); std::string::operator+=(std::forward<T>(v)); return *this; }
    template <class... A> TagString& append(A&&... a) { Touch(); std::string::append(std::forward<A>(a)...); return *this; }
    template <class... A> TagString& assign(A&&... a) { Touch(); std::string::assign(std::forward<A>(a)...); return *this; }
    template <class... A> TagString& insert(A&&... a) { Touch(); std::string::insert(std::forward<A>(a)...); return *this; }
    template <class... A> TagString& replace(A&&... a) { Touch(); std::string::replace(std::forward<A>(a)...); return *this; }
    template <class... A> decltype(auto) erase(A&&... a) { Touch(); return std::string::erase(std::forward<A>(a)...); }
    template <class... A> void resize(A&&... a) { Touch(); std::string::resize(std::forward<A>(a)...); }
    void push_back(char c) { Touch(); std::string::push_back(c); }
    void pop_back() { Touch(); std::string::pop_back(); }
    void swap(std::string& o) noexcept { Touch(); std::string::swap(o); }
    using std::string::operator[]; using std::string::at; using std::string::front; using std::string::back;
    using std::string::begin; using std::string::end; using std::string::data;
    char& operator[](size_type i) { Touch(); return std::string::operator[](i); }
    char& at(size_type i) { Touch(); return std::string::at(i); }
    char& front() { Touch(); return std::string::front(); }
    char& back() { Touch(); return std::string::back(); }
    iterator begin() noexcept { Touch(); return std::string::begin(); }
    iterator end() noexcept { Touch(); return std::string::end(); }
    char* data() noexcept { Touch(); return std::string::data(); }

    /// Bumped by every change to any tag in the process.
    static std::uint64_t Version() { return Counter().load(std::memory_order_relaxed); }
private:
    static std::atomic<std::uint64_t>& Counter() { static std::atomic<std::uint64_t> c{1}; return c; }
    static void Touch() { Counter().fetch_add(1, std::memory_order_relaxed); }
};

/// The project's named tag list. Tags on objects stay free strings (scripts and
/// components assign runtime tags such as "Debris"); the list is what editors offer
/// and what Accepts() checks when the project asks for strict tags. ProjectLists saves
/// and loads the custom tags (and the strict flag) with the project settings. The list is
/// process-wide and not locked: change it from the main thread.
struct Tags {
    static constexpr std::size_t kMaxLength = 64;
    /// The most custom tags a project can list (a damaged project file cannot make the list huge).
    static constexpr int kMaxCustom = 1000;

    /// A usable tag name: 1..kMaxLength characters, no control characters, and no
    /// leading or trailing space (so "Player" and "Player " can't be two tags).
    static bool IsValidName(const std::string& t) { return WhyInvalid(t).empty(); }
    /// Why a name cannot be a tag, for an editor to show; "" when it can.
    static std::string WhyInvalid(const std::string& t) {
        if (t.empty()) return "a tag name cannot be empty";
        if (t.size() > kMaxLength) return "a tag name is at most " + std::to_string(kMaxLength) + " characters";
        if (t.front() == ' ') return "a tag name cannot start with a space";
        if (t.back() == ' ') return "a tag name cannot end with a space";
        for (unsigned char c : t) if (c < 0x20 || c == 0x7F) return "a tag name cannot contain control characters";
        return {};
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

    static bool IsBuiltin(const std::string& t) {
        const auto& b = Builtin();
        return std::find(b.begin(), b.end(), t) != b.end();
    }
    static bool Contains(const std::string& t) {
        const auto& c = Custom();
        return IsBuiltin(t) || std::find(c.begin(), c.end(), t) != c.end();
    }
    /// Add a custom tag. False when the name is invalid or the list is full (kMaxCustom); true when
    /// added or already listed (a built-in is listed already).
    static bool Add(const std::string& t) {
        if (!IsValidName(t)) return false;
        if (Contains(t)) return true;
        if (static_cast<int>(Custom().size()) >= kMaxCustom) return false;
        Custom().push_back(t);
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
    /// Rename a custom tag in the list, keeping its place. False (nothing changes) for a built-in or
    /// unlisted tag, an invalid new name, or a new name that is already listed. Objects that carry
    /// the old name are not touched: Scene::ReplaceTag does that.
    static bool Rename(const std::string& from, const std::string& to) {
        auto& c = Custom();
        auto it = std::find(c.begin(), c.end(), from);
        if (it == c.end() || !IsValidName(to)) return false;
        if (from == to) return true;
        if (Contains(to)) return false;
        *it = to;
        return true;
    }
    /// Every tag in the list: built-ins first, then custom ones in the order added.
    static std::vector<std::string> All() {
        std::vector<std::string> out = Builtin();
        out.insert(out.end(), Custom().begin(), Custom().end());
        return out;
    }
    /// Position of a tag in All(), or -1.
    static int Index(const std::string& t) {
        const std::vector<std::string> all = All();
        auto it = std::find(all.begin(), all.end(), t);
        return it == all.end() ? -1 : static_cast<int>(it - all.begin());
    }
    /// Would SetTag take this? The empty string (untagged) always passes.
    static bool Accepts(const std::string& t) {
        if (t.empty()) return true;
        return IsValidName(t) && (!Strict() || Contains(t));
    }
};

} // namespace okay
