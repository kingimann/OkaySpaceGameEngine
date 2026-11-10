#pragma once
#include <string>
#include <vector>

namespace okay {

/// A small shared command-line parser. Declare the options, Parse(argc, argv), then
/// read them back with typed getters; Usage() builds the --help text from the same
/// declarations, so the two never drift.
///
///     CommandLine cl;
///     cl.Flag("windowed", "run in a window").Short('w');
///     cl.Value("width", "N", "window width in pixels", "1280");
///     cl.Value("cvar", "NAME=VALUE", "set a console variable (repeatable)");
///     if (!cl.Parse(argc, argv)) { std::fputs(cl.Error().c_str(), stderr); return 1; }
///     int w = cl.GetInt("width", 1280);
///     const std::string scene = cl.Positional(0);
///
/// Accepted forms: `--flag`, `--key=value`, `--key value`, `-k`, `-k value`,
/// `-abc` (grouped short flags), bare positionals, and `--` (everything after it is
/// a positional, even if it starts with a dash). A lone `-` is a positional.
/// A Value option always takes the next argument as its value, even a negative
/// number. An OptionalValue option takes the next argument only when it does not
/// start with a dash (`--find-match [MODE]`).
class CommandLine {
public:
    enum class Kind { Flag, Value, OptionalValue };

    CommandLine& Flag(const std::string& name, const std::string& help = "");
    CommandLine& Value(const std::string& name, const std::string& valueName,
                       const std::string& help = "", const std::string& defaultValue = "");
    CommandLine& OptionalValue(const std::string& name, const std::string& valueName,
                               const std::string& help = "");
    /// Give the most recently declared option a one-letter alias (`-w`).
    CommandLine& Short(char letter);
    /// Unknown `--options` are kept instead of being an error: `--k=v` records k=v,
    /// a bare `--k` records a flag. (An unknown option never consumes the next argument.)
    CommandLine& AllowUnknown(bool allow = true) { m_allowUnknown = allow; return *this; }

    /// Parse argv (argv[0] is the program name and is skipped). Replaces any earlier
    /// result. False on a bad command line; Error() says why.
    bool Parse(int argc, const char* const* argv);
    bool Parse(const std::vector<std::string>& args);   ///< same, without the program name
    const std::string& Error() const { return m_error; }

    /// Present on the command line (or, for GetXxx, given a default by the declaration).
    bool Has(const std::string& name) const;
    /// The last value given; `def` if the option is absent (declared default is used
    /// before `def` when one was declared).
    std::string Get(const std::string& name, const std::string& def = "") const;
    /// Every value given for a repeatable option, in order.
    std::vector<std::string> GetAll(const std::string& name) const;
    int         GetInt(const std::string& name, int def = 0) const;
    float       GetFloat(const std::string& name, float def = 0.0f) const;
    /// A flag is true when present. A value like `--vsync=false` / 0 / off / no is false. When the
    /// option is absent, the default it was declared with (Value(..., "true")) is read the same way,
    /// else `def`.
    bool        GetBool(const std::string& name, bool def = false) const;
    /// Int/float with a range check: false (and Error() set) when given but malformed or out of range.
    bool        TryGetInt(const std::string& name, int lo, int hi, int& out);
    bool        TryGetFloat(const std::string& name, float lo, float hi, float& out);

    const std::vector<std::string>& Positionals() const { return m_positionals; }
    std::string Positional(std::size_t i, const std::string& def = "") const {
        return i < m_positionals.size() ? m_positionals[i] : def;
    }

    /// "Usage: program [options] ..." plus an aligned option table.
    std::string Usage(const std::string& program, const std::string& summary = "") const;

private:
    struct Option {
        std::string name; char shortName = 0; Kind kind = Kind::Flag;
        std::string valueName, help, defaultValue;
    };
    struct Seen { std::string name, value; };
    const Option* FindLong(const std::string& n) const;
    const Option* FindShort(char c) const;
    bool Fail(const std::string& why) { m_error = why; return false; }

    std::vector<Option> m_options;
    std::vector<Seen> m_seen;
    std::vector<std::string> m_positionals;
    std::string m_error;
    bool m_allowUnknown = false;
};

} // namespace okay
