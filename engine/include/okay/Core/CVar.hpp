#pragma once
// Console variables: typed settings registered by name, readable from anywhere,
// settable from a console line, the command line, code or the Prefs file.
// See docs/core_runtime.md.
//
//     static CVar<int>   s_maxFps("render.max_fps", 0, "Frame cap (0 = off)", CVarFlags::Persist, 0, 1000);
//     static CVar<bool>  s_vsync ("render.vsync", true, "Wait for the display", CVarFlags::Persist);
//     ...
//     int cap = s_maxFps;                    // Get(): a plain atomic read for bool / int / float
//     CVarRegistry::Global().Execute("render.max_fps 120");   // "render.max_fps = 120"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace okay {

enum class CVarType { Bool, Int, Float, String };

enum class CVarFlags : std::uint32_t {
    None = 0,
    /// The console, command line and Execute cannot change it; code can (CVar::Set).
    ReadOnly = 1,
    /// The user can change it only while the built-in `cheats` variable is true.
    Cheat = 2,
    /// Saved to and loaded from Prefs under "cvar.<name>" (see StoreToPrefs / LoadFromPrefs).
    Persist = 4,
    /// A new value is kept aside and only applies after a restart: Get() keeps returning
    /// the value the program started with, and the Persist file gets the new one.
    RestartRequired = 8,
};
constexpr CVarFlags operator|(CVarFlags a, CVarFlags b) {
    return static_cast<CVarFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr CVarFlags operator&(CVarFlags a, CVarFlags b) {
    return static_cast<CVarFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr bool HasFlag(CVarFlags set, CVarFlags flag) { return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0; }

/// What a Set did.
enum class CVarResult {
    Ok,               // changed
    Unchanged,        // the value was already that
    Clamped,          // accepted, but the value had to be pulled into [min, max]
    RestartPending,   // stored, but only applies after a restart
    ReadOnly,         // refused: read-only
    CheatsDisabled,   // refused: a cheat variable and `cheats` is off
    ParseError,       // refused: the text is not a value of this type
    Unknown,          // no such variable
};
constexpr bool Accepted(CVarResult r) {
    return r == CVarResult::Ok || r == CVarResult::Unchanged || r == CVarResult::Clamped || r == CVarResult::RestartPending;
}

/// A value of any of the four types (only the field matching `type` is meaningful).
struct CVarValue {
    CVarType type = CVarType::Int;
    bool b = false;
    int i = 0;
    float f = 0.0f;
    std::string s;

    static CVarValue Bool(bool v) { CVarValue c; c.type = CVarType::Bool; c.b = v; return c; }
    static CVarValue Int(int v) { CVarValue c; c.type = CVarType::Int; c.i = v; return c; }
    static CVarValue Float(float v) { CVarValue c; c.type = CVarType::Float; c.f = v; return c; }
    static CVarValue String(std::string v) { CVarValue c; c.type = CVarType::String; c.s = std::move(v); return c; }
};

/// A snapshot of one variable, for lists and UIs.
struct CVarInfo {
    std::string name, description;
    CVarType type = CVarType::Int;
    CVarFlags flags = CVarFlags::None;
    std::string value;          // the value in effect, as text
    std::string defaultValue;
    bool hasPending = false;    // RestartRequired: a new value waits for the next start
    std::string pendingValue;
    bool hasRange = false;
    double min = 0.0, max = 0.0;
    bool modified = false;      // value differs from the default
};

namespace cvar_detail { struct Entry; }
class CVarRegistry;

/// The set of variables. There is one process-wide registry (Global()); tests and tools
/// can make their own. Registering, setting and listing take a lock, so they are safe from any
/// thread; reading a bool / int / float variable does not (it is an atomic load).
class CVarRegistry {
public:
    using Listener = std::function<void(const CVarValue& oldValue, const CVarValue& newValue)>;
    /// The Prefs key of a Persist variable is this plus its name.
    static constexpr const char* kPrefsPrefix = "cvar.";

    CVarRegistry();
    ~CVarRegistry();
    CVarRegistry(const CVarRegistry&) = delete;
    CVarRegistry& operator=(const CVarRegistry&) = delete;

    static CVarRegistry& Global();

    // ---- registration (the CVar<T> constructors call this) ----

    /// Add a variable, or return the existing one when the name is taken by a variable of
    /// the same type (the first registration's default, flags and range stay). nullptr when
    /// the name is not valid ([A-Za-z_][A-Za-z0-9_.]*, not "list", "reset", "toggle" or "help")
    /// or the name is taken by another type; see RegistrationErrors().
    cvar_detail::Entry* Register(const std::string& name, const CVarValue& def, const std::string& description,
                                 CVarFlags flags, bool hasRange, double minV, double maxV);
    static bool IsValidName(const std::string& name);

    // ---- typed access (used by CVar<T>) ----
    static bool ReadBool(const cvar_detail::Entry* e);
    static int ReadInt(const cvar_detail::Entry* e);
    static float ReadFloat(const cvar_detail::Entry* e);
    std::string ReadString(const cvar_detail::Entry* e) const;
    CVarValue DefaultOf(const cvar_detail::Entry* e) const;
    /// Set from code: ignores ReadOnly and Cheat, still clamps and still defers a RestartRequired value.
    CVarResult StoreValue(cvar_detail::Entry* e, const CVarValue& v);
    CVarResult ResetEntry(cvar_detail::Entry* e);
    /// Called, outside any lock, after the value in effect changed. Returns an id for RemoveListener.
    int AddListener(cvar_detail::Entry* e, Listener fn);
    void RemoveListener(cvar_detail::Entry* e, int id);
    bool NeedsRestart(const cvar_detail::Entry* e) const;
    const std::string& NameOf(const cvar_detail::Entry* e) const;

    // ---- by name ----

    bool Exists(const std::string& name) const;
    std::size_t Count() const;
    /// The value in effect, as text. False if there is no such variable.
    bool GetString(const std::string& name, std::string& out) const;
    bool Info(const std::string& name, CVarInfo& out) const;
    /// Set from text, the way the console does: refuses ReadOnly, refuses Cheat unless `cheats` is on,
    /// parses by type ("true", "on", "1" for bools), clamps to the range. On a refusal `error` (if given)
    /// says why. Names are matched exactly, or ignoring case when that finds exactly one.
    CVarResult SetFromString(const std::string& name, const std::string& text, std::string* error = nullptr);
    /// Back to the default (the user path: refuses ReadOnly and cheat variables like SetFromString).
    CVarResult Reset(const std::string& name, std::string* error = nullptr);
    /// Every variable back to its default, no questions asked (code path). Also clears pending
    /// restart values and command-line marks.
    void ResetAll();

    // ---- enumeration and autocomplete ----

    /// All variables whose name starts with `prefix`, sorted by name.
    std::vector<CVarInfo> List(const std::string& prefix = std::string()) const;
    std::vector<std::string> Names(const std::string& prefix = std::string()) const;
    /// Names to offer for a half-typed name (the same as Names).
    std::vector<std::string> Complete(const std::string& prefix) const { return Names(prefix); }
    /// The longest text all matches of `prefix` start with: what Tab should insert.
    std::string CompleteCommon(const std::string& prefix) const;
    /// Variables with a RestartRequired value waiting.
    std::vector<std::string> RestartPending() const;

    // ---- command line and console ----

    /// Apply `+name=value` overrides from a command line (also `+name value`, and a bare `+name`
    /// for a bool means true). They count as the user's: ReadOnly and cheat rules apply, a
    /// RestartRequired variable takes the value right away (this is startup configuration), and a
    /// value set this way is not written by StoreToPrefs (it is for this run only). A name that is
    /// not registered yet is kept and applied when it registers (see UnresolvedOverrides). Returns how
    /// many were applied; problems are added to `errors`.
    int ApplyArgs(int argc, const char* const* argv, std::vector<std::string>* errors = nullptr);
    std::vector<std::string> UnresolvedOverrides() const;
    void ClearUnresolvedOverrides();

    /// Run one console line and return the text to show. `name` prints the variable, `name value`
    /// (or `name=value`) sets it, `toggle name`, `reset name`, `list [prefix]` and `help` do what
    /// they say. Errors start with "error: ". Never throws.
    std::string Execute(const std::string& line);

    // ---- Prefs ----

    /// Write every Persist variable into Prefs as "cvar.<name>" (not the ones set from the command
    /// line). Returns how many. Prefs::Save does the file.
    int StoreToPrefs() const;
    /// Apply the "cvar.<name>" values found in Prefs to the Persist variables, as the value in effect
    /// (also for RestartRequired ones: this is the start). Returns how many were applied.
    int LoadFromPrefs();
    bool SavePersistent(const std::string& prefsPath) const;   // StoreToPrefs + Prefs::Save
    bool LoadPersistent(const std::string& prefsPath);         // Prefs::Load + LoadFromPrefs

    // ---- cheats and diagnostics ----

    /// The built-in bool variable `cheats`.
    bool CheatsEnabled() const;
    void SetCheatsEnabled(bool on);
    /// Why a Register call returned nullptr (and bad overrides found at registration).
    std::vector<std::string> RegistrationErrors() const;

    // ---- parsing and formatting ----

    static bool ParseBool(std::string_view text, bool& out);
    /// Whole-string decimal integer within int range.
    static bool ParseInt(std::string_view text, int& out);
    /// A finite decimal float ("1.5", "-2", ".5", "1e3"); locale independent.
    static bool ParseFloat(std::string_view text, float& out);
    /// The shortest text that reads back as the same float.
    static std::string FormatFloat(float v);
    static std::string FormatValue(const CVarValue& v);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// A typed handle to a variable. Make them `static` in the file that owns the setting.
template <class T>
class CVar {
    static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> || std::is_same_v<T, std::string>,
                  "CVar<T>: T must be bool, int, float or std::string");
public:
    using Change = std::function<void(const T& newValue, const T& oldValue)>;

    /// Register in the global registry. If the name is invalid or taken by another type the
    /// handle is "detached": Valid() is false, Get() gives `def`, and sets are refused.
    CVar(const std::string& name, T def, const std::string& description, CVarFlags flags = CVarFlags::None)
        : CVar(CVarRegistry::Global(), name, std::move(def), description, flags) {}
    CVar(CVarRegistry& reg, const std::string& name, T def, const std::string& description, CVarFlags flags = CVarFlags::None)
        : m_reg(&reg), m_fallback(def) {
        m_entry = reg.Register(name, ToValue(def), description, flags, false, 0.0, 0.0);
    }
    /// With a range (int and float only): values are clamped into [minV, maxV].
    template <class U = T, class = std::enable_if_t<std::is_same_v<U, int> || std::is_same_v<U, float>>>
    CVar(const std::string& name, T def, const std::string& description, CVarFlags flags, T minV, T maxV)
        : CVar(CVarRegistry::Global(), name, def, description, flags, minV, maxV) {}
    template <class U = T, class = std::enable_if_t<std::is_same_v<U, int> || std::is_same_v<U, float>>>
    CVar(CVarRegistry& reg, const std::string& name, T def, const std::string& description, CVarFlags flags, T minV, T maxV)
        : m_reg(&reg), m_fallback(def) {
        m_entry = reg.Register(name, ToValue(def), description, flags, true, static_cast<double>(minV), static_cast<double>(maxV));
    }

    bool Valid() const { return m_entry != nullptr; }
    const std::string& Name() const { return m_entry ? m_reg->NameOf(m_entry) : EmptyName(); }

    /// The value in effect (for a RestartRequired variable: the one the program started with).
    T Get() const {
        if (!m_entry) return m_fallback;
        if constexpr (std::is_same_v<T, bool>) return CVarRegistry::ReadBool(m_entry);
        else if constexpr (std::is_same_v<T, int>) return CVarRegistry::ReadInt(m_entry);
        else if constexpr (std::is_same_v<T, float>) return CVarRegistry::ReadFloat(m_entry);
        else return m_reg->ReadString(m_entry);
    }
    operator T() const { return Get(); }

    /// Set from code: ignores ReadOnly and Cheat, clamps to the range, and defers a RestartRequired value.
    CVarResult TrySet(const T& v) { return m_entry ? m_reg->StoreValue(m_entry, ToValue(v)) : CVarResult::Unknown; }
    bool Set(const T& v) { return Accepted(TrySet(v)); }
    CVar& operator=(const T& v) { Set(v); return *this; }
    /// Back to the default.
    void Reset() { if (m_entry) m_reg->ResetEntry(m_entry); }
    T Default() const { return m_entry ? FromValue(m_reg->DefaultOf(m_entry)) : m_fallback; }
    bool NeedsRestart() const { return m_entry && m_reg->NeedsRestart(m_entry); }

    /// Call `fn(newValue, oldValue)` whenever the value in effect changes (on the thread that changed
    /// it, with no lock held). Returns an id for RemoveOnChange, or 0 for a detached handle.
    int OnChange(Change fn) {
        if (!m_entry) return 0;
        return m_reg->AddListener(m_entry, [fn](const CVarValue& o, const CVarValue& n) { fn(FromValue(n), FromValue(o)); });
    }
    void RemoveOnChange(int id) { if (m_entry) m_reg->RemoveListener(m_entry, id); }

private:
    static const std::string& EmptyName() { static const std::string e; return e; }
    static CVarValue ToValue(const T& v) {
        if constexpr (std::is_same_v<T, bool>) return CVarValue::Bool(v);
        else if constexpr (std::is_same_v<T, int>) return CVarValue::Int(v);
        else if constexpr (std::is_same_v<T, float>) return CVarValue::Float(v);
        else return CVarValue::String(v);
    }
    static T FromValue(const CVarValue& v) {
        if constexpr (std::is_same_v<T, bool>) return v.b;
        else if constexpr (std::is_same_v<T, int>) return v.i;
        else if constexpr (std::is_same_v<T, float>) return v.f;
        else return v.s;
    }

    CVarRegistry* m_reg;
    cvar_detail::Entry* m_entry = nullptr;
    T m_fallback;
};

using CVarBool = CVar<bool>;
using CVarInt = CVar<int>;
using CVarFloat = CVar<float>;
using CVarString = CVar<std::string>;

} // namespace okay
