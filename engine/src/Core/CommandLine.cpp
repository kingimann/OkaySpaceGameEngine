#include "okay/Core/CommandLine.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace okay {

CommandLine& CommandLine::Flag(const std::string& name, const std::string& help) {
    Option o; o.name = name; o.kind = Kind::Flag; o.help = help;
    m_options.push_back(o); return *this;
}
CommandLine& CommandLine::Value(const std::string& name, const std::string& valueName,
                                const std::string& help, const std::string& defaultValue) {
    Option o; o.name = name; o.kind = Kind::Value; o.valueName = valueName; o.help = help; o.defaultValue = defaultValue;
    m_options.push_back(o); return *this;
}
CommandLine& CommandLine::OptionalValue(const std::string& name, const std::string& valueName,
                                        const std::string& help) {
    Option o; o.name = name; o.kind = Kind::OptionalValue; o.valueName = valueName; o.help = help;
    m_options.push_back(o); return *this;
}
CommandLine& CommandLine::Short(char letter) {
    if (!m_options.empty()) m_options.back().shortName = letter;
    return *this;
}

const CommandLine::Option* CommandLine::FindLong(const std::string& n) const {
    for (const Option& o : m_options) if (o.name == n) return &o;
    return nullptr;
}
const CommandLine::Option* CommandLine::FindShort(char c) const {
    for (const Option& o : m_options) if (o.shortName == c) return &o;
    return nullptr;
}

bool CommandLine::Parse(int argc, const char* const* argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i] ? argv[i] : "");
    return Parse(args);
}

bool CommandLine::Parse(const std::vector<std::string>& args) {
    m_seen.clear(); m_positionals.clear(); m_error.clear();
    bool onlyPositionals = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (onlyPositionals || a.size() < 2 || a[0] != '-') { m_positionals.push_back(a); continue; }
        if (a == "--") { onlyPositionals = true; continue; }

        // Takes the value for `opt` from "=value", or from the next argument.
        auto take = [&](const Option& opt, bool hasInline, const std::string& inlineVal, std::string& out) -> bool {
            if (hasInline) { out = inlineVal; return true; }
            if (opt.kind == Kind::Value) {
                if (i + 1 >= args.size()) return Fail("--" + opt.name + " needs a value" +
                                                      (opt.valueName.empty() ? "" : " (" + opt.valueName + ")"));
                out = args[++i]; return true;
            }
            // OptionalValue
            if (i + 1 < args.size() && !(args[i + 1].size() > 1 && args[i + 1][0] == '-')) out = args[++i];
            else out.clear();
            return true;
        };

        if (a[1] == '-') {
            std::string name = a.substr(2), val; bool hasInline = false;
            const std::size_t eq = name.find('=');
            if (eq != std::string::npos) { val = name.substr(eq + 1); name = name.substr(0, eq); hasInline = true; }
            const Option* opt = FindLong(name);
            if (!opt) {
                if (!m_allowUnknown) return Fail("unknown option --" + name);
                m_seen.push_back({name, hasInline ? val : "true"});
                continue;
            }
            if (opt->kind == Kind::Flag) {
                if (hasInline && val.empty()) return Fail("--" + name + " does not take a value");
                m_seen.push_back({name, hasInline ? val : "true"});
                continue;
            }
            if (!take(*opt, hasInline, val, val)) return false;
            m_seen.push_back({name, val});
        } else {
            // -x, -x value, or grouped flags -abc
            for (std::size_t k = 1; k < a.size(); ++k) {
                const Option* opt = FindShort(a[k]);
                if (!opt) return Fail(std::string("unknown option -") + a[k]);
                if (opt->kind == Kind::Flag) { m_seen.push_back({opt->name, "true"}); continue; }
                std::string val;
                if (k + 1 < a.size()) { val = a.substr(k + 1); if (!val.empty() && val[0] == '=') val.erase(0, 1); }
                else if (!take(*opt, false, "", val)) return false;
                m_seen.push_back({opt->name, val});
                break;
            }
        }
    }
    return true;
}

bool CommandLine::Has(const std::string& name) const {
    for (const Seen& s : m_seen) if (s.name == name) return true;
    return false;
}

std::string CommandLine::Get(const std::string& name, const std::string& def) const {
    for (auto it = m_seen.rbegin(); it != m_seen.rend(); ++it)
        if (it->name == name) return it->value;
    if (const Option* o = FindLong(name); o && !o->defaultValue.empty()) return o->defaultValue;
    return def;
}

std::vector<std::string> CommandLine::GetAll(const std::string& name) const {
    std::vector<std::string> out;
    for (const Seen& s : m_seen) if (s.name == name) out.push_back(s.value);
    return out;
}

namespace {
bool ParseIntStrict(const std::string& s, long& v) {
    if (s.empty()) return false;
    char* end = nullptr; errno = 0;
    v = std::strtol(s.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0';
}
bool ParseFloatStrict(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr; errno = 0;
    v = std::strtod(s.c_str(), &end);
    return errno == 0 && end && *end == '\0' && v == v;
}
}

int CommandLine::GetInt(const std::string& name, int def) const {
    long v = 0;
    return ParseIntStrict(Get(name), v) && v >= -2147483647L - 1 && v <= 2147483647L ? (int)v : def;
}
float CommandLine::GetFloat(const std::string& name, float def) const {
    double v = 0;
    return ParseFloatStrict(Get(name), v) ? (float)v : def;
}
bool CommandLine::GetBool(const std::string& name, bool def) const {
    if (!Has(name)) return def;
    std::string v = Get(name);
    for (char& c : v) c = (char)std::tolower((unsigned char)c);
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    return true;
}

bool CommandLine::TryGetInt(const std::string& name, int lo, int hi, int& out) {
    if (!Has(name)) return true;
    long v = 0;
    if (!ParseIntStrict(Get(name), v) || v < lo || v > hi)
        return Fail("--" + name + " needs a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
    out = (int)v; return true;
}
bool CommandLine::TryGetFloat(const std::string& name, float lo, float hi, float& out) {
    if (!Has(name)) return true;
    double v = 0;
    if (!ParseFloatStrict(Get(name), v) || v < lo || v > hi)
        return Fail("--" + name + " needs a number from " + std::to_string(lo) + " to " + std::to_string(hi));
    out = (float)v; return true;
}

std::string CommandLine::Usage(const std::string& program, const std::string& summary) const {
    std::string out = "Usage: " + program + " [options]";
    out += "\n";
    if (!summary.empty()) out += summary + "\n";
    // Left column: "  -w, --name VALUE"
    std::vector<std::string> left;
    std::size_t width = 0;
    for (const Option& o : m_options) {
        std::string l = "  ";
        if (o.shortName) { l += '-'; l += o.shortName; l += ", "; } else l += "    ";
        l += "--" + o.name;
        if (o.kind == Kind::Value) l += " " + (o.valueName.empty() ? std::string("VALUE") : o.valueName);
        else if (o.kind == Kind::OptionalValue) l += " [" + (o.valueName.empty() ? std::string("VALUE") : o.valueName) + "]";
        width = std::max(width, l.size());
        left.push_back(l);
    }
    if (!m_options.empty()) out += "\nOptions:\n";
    for (std::size_t i = 0; i < m_options.size(); ++i) {
        const Option& o = m_options[i];
        out += left[i];
        if (!o.help.empty() || !o.defaultValue.empty()) {
            out += std::string(width - left[i].size() + 2, ' ') + o.help;
            if (!o.defaultValue.empty()) out += (o.help.empty() ? "" : " ") + std::string("(default ") + o.defaultValue + ")";
        }
        out += "\n";
    }
    return out;
}

} // namespace okay
