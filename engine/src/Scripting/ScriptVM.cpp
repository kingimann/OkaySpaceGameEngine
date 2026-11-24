#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scripting/CSharpHost.hpp"
#include "okay/Scripting/CSharpScriptVM.hpp"

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    std::vector<std::string> v{"okayscript"};
    // C# through the hosted .NET runtime (rule 24c): listed only when this build has the host compiled in.
    if (csharp::DotnetRuntime::CompiledIn()) v.push_back(CSharpScriptVM::kLanguage);
    return v;
}

std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language) {
    if (language == "okayscript" || language.empty())
        return std::make_unique<OkayScriptVM>();
    if (language == NextScriptVM::kLanguage) return std::make_unique<NextScriptVM>();
    // "csharp" / "cs" always get a VM: without OKAY_WITH_CSHARP its Load() says plainly that C# is not
    // enabled in this build, so an old or shared scene still loads and tells the person why.
    if (language == CSharpScriptVM::kLanguage || language == "cs") return std::make_unique<CSharpScriptVM>();
    return nullptr;
}

std::string RemovedScriptLanguageMessage(const std::string& language) {
    if (language == "lua")
        return "script language 'lua' is no longer supported (the Lua backend was removed); "
               "rewrite the script in OkayScript";
    return {};
}

} // namespace okay
