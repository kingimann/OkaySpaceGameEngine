#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include <cctype>

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    std::vector<std::string> langs;
    langs.push_back("okayscript");
    langs.push_back(NextScriptVM::kCSharpLanguage);   // C#: our own typed language (engine/src/Script)
    return langs;
}

std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language) {
    if (language == "okayscript" || language.empty())
        return std::make_unique<OkayScriptVM>();
    if (language == NextScriptVM::kLanguage) return std::make_unique<NextScriptVM>();
    if (NextScriptVM::IsCSharpId(language)) return std::make_unique<NextScriptVM>(NextScriptVM::kCSharpLanguage);
    return nullptr;
}

std::string RemovedScriptLanguageMessage(const std::string& language) {
    if (language == "lua")
        return "script language 'lua' is no longer supported (the Lua backend was removed); "
               "rewrite the script in OkayScript";
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
