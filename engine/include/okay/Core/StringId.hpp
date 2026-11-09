#pragma once
// Hashed string ids: compare and hash names as one 64-bit integer, with an optional
// table that remembers the text for logs and detects hash collisions.
// See docs/core_runtime.md.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace okay {

/// A name turned into a 64-bit FNV-1a hash. Cheap to copy, compare and use as a map
/// key, and the hash of a string literal is computed at compile time:
///
///     constexpr StringId kJump = "player.jump"_sid;
///     switch (id.Hash()) { case "a"_sid.Hash(): ... }
///
/// The default id is "none" (value 0), and so is the empty string. The text is not
/// kept, so `Name()` only works for ids that went through Intern() (or Make() in a
/// debug build). Different strings can in theory share a hash; Intern() notices that
/// and reports it (see SetCollisionCheck).
class StringId {
public:
    using Value = std::uint64_t;

    constexpr StringId() = default;

    /// The hash of `s`. Constexpr, allocation free, never touches the table.
    static constexpr StringId FromString(std::string_view s) { return StringId(HashOf(s)); }
    /// An id from a number saved earlier.
    static constexpr StringId FromValue(Value v) { return StringId(v); }
    /// The hash itself, as a plain number.
    static constexpr Value HashOf(std::string_view s) {
        if (s.empty()) return 0;
        Value h = 14695981039346656037ull;   // FNV-1a, 64 bit
        for (std::size_t i = 0; i < s.size(); ++i) {
            h ^= static_cast<unsigned char>(s[i]);
            h *= 1099511628211ull;
        }
        return h == 0 ? 1 : h;   // 0 is kept for "none"
    }

    /// Hash `s` and remember its text in the global table, so Name() can give it back.
    /// If a different string with the same hash was interned before, that is a
    /// collision: the first name stays, the count goes up and the handler (if any) runs.
    static StringId Intern(std::string_view s);
    /// Intern with a hash you chose instead of the built-in one: for tests of the collision
    /// check, and for tools that hash with their own function. 0 is "none" and is returned as is.
    static StringId InternWithHash(Value hash, std::string_view s);
    /// Intern() in debug builds (names show up in logs), FromString() in release builds.
    static StringId Make(std::string_view s) {
#ifdef NDEBUG
        return FromString(s);
#else
        return Intern(s);
#endif
    }

    constexpr Value Hash() const { return m_value; }
    constexpr Value value() const { return m_value; }
    constexpr bool Empty() const { return m_value == 0; }
    constexpr explicit operator bool() const { return m_value != 0; }

    /// The interned text; "" for none; "#<hex>" for an id that was never interned.
    std::string Name() const;
    /// True if the table knows this id's text.
    bool IsInterned() const;

    friend constexpr bool operator==(StringId a, StringId b) { return a.m_value == b.m_value; }
    friend constexpr bool operator!=(StringId a, StringId b) { return a.m_value != b.m_value; }
    friend constexpr bool operator<(StringId a, StringId b) { return a.m_value < b.m_value; }

    // ---- the debug-string table ----

    /// Called when two different strings hash alike: (existing text, new text, hash).
    /// A plain function pointer, so it can be set from anywhere; it must not call Intern.
    using CollisionHandler = void (*)(std::string_view existing, std::string_view incoming, Value hash);
    static void SetCollisionHandler(CollisionHandler handler);
    /// Whether Intern() compares texts to find collisions. On by default in debug builds, off in
    /// release builds (where Intern then keeps the first text for a hash without checking).
    static void SetCollisionCheck(bool on);
    static bool CollisionCheck();
    static std::size_t CollisionCount();
    static std::size_t InternedCount();
    /// Forget every interned text and the collision count (for tests).
    static void ClearTable();

private:
    constexpr explicit StringId(Value v) : m_value(v) {}
    Value m_value = 0;
};

inline namespace literals {
/// `"name"_sid`: the id of a string literal, computed at compile time.
constexpr StringId operator""_sid(const char* s, std::size_t n) { return StringId::FromString(std::string_view(s, n)); }
} // namespace literals

} // namespace okay

namespace std {
template <>
struct hash<okay::StringId> {
    size_t operator()(okay::StringId id) const noexcept { return static_cast<size_t>(id.Hash()); }
};
} // namespace std
