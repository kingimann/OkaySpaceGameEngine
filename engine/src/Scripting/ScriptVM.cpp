#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Lua/LuaScriptVM.hpp"
#include <cctype>

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    return {
        "okayscript",
        "lua",   // our own Lua 5.4 interpreter (okay::lua), not the removed third-party one
    };
}

std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language) {
    if (language == "okayscript" || language.empty())
        return std::make_unique<OkayScriptVM>();
    if (language == NextScriptVM::kLanguage) return std::make_unique<NextScriptVM>();
    if (language == LuaScriptVM::kLanguage) return std::make_unique<LuaScriptVM>();
    return nullptr;
}

std::string RemovedScriptLanguageMessage(const std::string& language) {
    if (language == "csharp" || language == "cs")
        return "script language '" + language + "' is no longer supported (the Mono C# backend was removed); "
               "rewrite the script in OkayScript, which has C#-style syntax";
    return {};
}

const char* ScriptLanguageForExtension(const std::string& ext) {
    std::string e;
    for (char c : ext) e += (char)std::tolower((unsigned char)c);
    if (!e.empty() && e[0] != '.') e = "." + e;
    if (e == ".lua") return LuaScriptVM::kLanguage;
    if (e == ".cs") return "cs";
    return "okayscript";
}

} // namespace okay
