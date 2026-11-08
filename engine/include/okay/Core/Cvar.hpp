#pragma once
#include <functional>
#include <string>
#include <vector>

namespace okay {

/// A console variable: a named int / float / bool / string setting with a default,
/// an optional range, a description and change callbacks. Cvars live in the registry
/// (`Cvars::Int("r.shadows", 1, "...")`), are set from the console
/// (`Console::Execute("r.shadows 0")`), the command line (`--cvar r.shadows=0`) or code,
/// and the ones marked `persist` are saved to / loaded from a small text file.
///
/// A Cvar reference stays valid until Cvars::Clear(). Not thread safe: main thread only.
class Cvar {
public:
    enum class Type { Int, Float, Bool, String };
    using Callback = std::function<void(const Cvar&)>;

    const std::string& Name() const { return m_name; }
    const std::string& Description() const { return m_desc; }
    Type  GetType() const { return m_type; }
    const char* TypeName() const;
    bool  Persist() const { return m_persist; }
    bool  HasRange() const { return m_hasRange; }
    double Min() const { return m_min; }
    double Max() const { return m_max; }

    int         AsInt() const;
    float       AsFloat() const;
    bool        AsBool() const;
    std::string AsString() const;           ///< current value as text ("1", "0.5", "true", "text")
    std::string DefaultString() const;
    bool        IsDefault() const { return AsString() == DefaultString(); }

    /// Set from text, parsed per type (bool accepts 1/0/true/false/on/off/yes/no).
    /// A number outside the range is clamped. False, with `error` filled and the value
    /// unchanged, when the text can't be parsed.
    bool Set(const std::string& text, std::string* error = nullptr);
    void SetInt(int v);
    void SetFloat(float v);
    void SetBool(bool v);
    void SetString(const std::string& v);
    void Reset();                            ///< back to the default

    /// Called after the value changes (not when it is set to what it already was).
    /// Returns an id for RemoveOnChange.
    int  OnChange(Callback cb);
    void RemoveOnChange(int id);

private:
    friend class Cvars;
    Cvar() = default;
    void Assign(double number, const std::string& text);   // store, notify if changed

    std::string m_name, m_desc;
    Type   m_type = Type::Int;
    double m_value = 0.0, m_default = 0.0;
    std::string m_text, m_defaultText;       // String type
    double m_min = 0.0, m_max = 0.0;
    bool   m_hasRange = false, m_persist = false;
    struct Cb { int id; Callback fn; };
    std::vector<Cb> m_callbacks;
    int    m_nextCb = 1;
};

/// The registry of all Cvars. Registering a name twice returns the first one (so
/// registration may be repeated safely).
class Cvars {
public:
    static Cvar& Int(const std::string& name, int def, const std::string& desc = "",
                     int lo = 0, int hi = 0, bool persist = false);     // lo == hi == 0: no range
    static Cvar& Float(const std::string& name, float def, const std::string& desc = "",
                       float lo = 0.0f, float hi = 0.0f, bool persist = false);
    static Cvar& Bool(const std::string& name, bool def, const std::string& desc = "", bool persist = false);
    static Cvar& String(const std::string& name, const std::string& def, const std::string& desc = "",
                        bool persist = false);

    static Cvar* Find(const std::string& name);
    /// All cvars, sorted by name; with `prefix`, only the names starting with it.
    static std::vector<Cvar*> All(const std::string& prefix = "");

    /// Set by name. False (error filled) for an unknown name or unparseable value.
    static bool Set(const std::string& name, const std::string& value, std::string* error = nullptr);
    /// "name=value" (as in `--cvar name=value`).
    static bool SetFromAssignment(const std::string& assignment, std::string* error = nullptr);
    static void ResetAll();

    /// Write the persistent cvars that differ from their default to `path` as "name value"
    /// lines (atomically: the old file survives a failed write). Names loaded earlier but
    /// not registered yet are written back too, so another build's settings aren't lost.
    static bool Save(const std::string& path);
    /// Read a file written by Save. Known names are set; unknown ones are held and applied
    /// when that cvar is registered. False if the file can't be read. Bad lines are skipped.
    static bool Load(const std::string& path);

    /// Drop every cvar (invalidates references) and held values. For tests and shutdown.
    static void Clear();
};

} // namespace okay
