#pragma once
#include <iostream>
#include <sstream>
#include <string>

namespace okay {

/// Minimal logging facility, similar in spirit to Unity's `Debug.Log`.
class Log {
public:
    enum class Level { Trace, Info, Warning, Error };

    static Level minLevel;

    template <typename... Args>
    static void Trace(Args&&... args)   { Emit(Level::Trace,   "TRACE", std::forward<Args>(args)...); }
    template <typename... Args>
    static void Info(Args&&... args)    { Emit(Level::Info,    "INFO ", std::forward<Args>(args)...); }
    template <typename... Args>
    static void Warning(Args&&... args) { Emit(Level::Warning, "WARN ", std::forward<Args>(args)...); }
    template <typename... Args>
    static void Error(Args&&... args)   { Emit(Level::Error,   "ERROR", std::forward<Args>(args)...); }

private:
    template <typename... Args>
    static void Emit(Level level, const char* tag, Args&&... args) {
        if (level < minLevel) return;
        std::ostringstream oss;
        (oss << ... << args);
        std::ostream& out = (level == Level::Error || level == Level::Warning)
                                ? std::cerr : std::cout;
        out << "[okay][" << tag << "] " << oss.str() << '\n';
    }
};

inline Log::Level Log::minLevel = Log::Level::Trace;

} // namespace okay

#define OKAY_INFO(...)  ::okay::Log::Info(__VA_ARGS__)
#define OKAY_WARN(...)  ::okay::Log::Warning(__VA_ARGS__)
#define OKAY_ERROR(...) ::okay::Log::Error(__VA_ARGS__)
#define OKAY_TRACE(...) ::okay::Log::Trace(__VA_ARGS__)
