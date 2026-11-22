#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Lua/LuaScriptVM.hpp"
#include <cctype>

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    std::vector<std::string> langs;
    langs.push_back("okayscript");
    langs.push_back(NextScriptVM::kCSharpLanguage);   // C#: our own typed language (engine/src/Script)
    langs.push_back(LuaScriptVM::kLanguage);          // our own Lua 5.4 interpreter (okay::lua)
    return langs;
}

std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language) {
    if (language == "okayscript" || language.empty())
        return std::make_unique<OkayScriptVM>();
    if (language == NextScriptVM::kLanguage) return std::make_unique<NextScriptVM>();
    if (NextScriptVM::IsCSharpId(language)) return std::make_unique<NextScriptVM>(NextScriptVM::kCSharpLanguage);
    if (language == LuaScriptVM::kLanguage) return std::make_unique<LuaScriptVM>();
    return nullptr;
}

std::string RemovedScriptLanguageMessage(const std::string& language) {
    (void)language;   // nothing is removed today: lua and csharp/cs both run on our own code
    return {};
}

const char* ScriptLanguageForExtension(const std::string& ext) {
    std::string e;
    for (char c : ext) e += (char)std::tolower((unsigned char)c);
    if (!e.empty() && e[0] != '.') e = "." + e;
    if (e == ".cs") return NextScriptVM::kCSharpLanguage;
    return "okayscript";
}

} // namespace okay
