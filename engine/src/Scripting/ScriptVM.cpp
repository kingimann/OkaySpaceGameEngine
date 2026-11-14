#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    return {"okayscript"};
}

std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language) {
    if (language == "okayscript" || language.empty())
        return std::make_unique<OkayScriptVM>();
    if (language == NextScriptVM::kLanguage) return std::make_unique<NextScriptVM>();
    return nullptr;
}

std::string RemovedScriptLanguageMessage(const std::string& language) {
    if (language == "lua")
        return "script language 'lua' is no longer supported (the Lua backend was removed); "
               "rewrite the script in OkayScript";
    if (language == "csharp" || language == "cs")
        return "script language '" + language + "' is no longer supported (the Mono C# backend was removed); "
               "rewrite the script in OkayScript, which has C#-style syntax";
    return {};
}

} // namespace okay
