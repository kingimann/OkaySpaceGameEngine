#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Lua/LuaScriptVM.hpp"
#include "okay/CSharp/CSharpScriptVM.hpp"
#include "okay/CSharp/DotnetHost.hpp"

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    std::vector<std::string> v{"okayscript", "lua"};
    if (csharp::DotnetRuntime::CompiledIn()) v.push_back(CSharpScriptVM::kLanguage);   // only when this build hosts .NET
    return v;
}

std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language) {
    if (language == "okayscript" || language.empty())
        return std::make_unique<OkayScriptVM>();
    if (language == LuaScriptVM::kLanguage) return std::make_unique<LuaScriptVM>();
    // "csharp" always gets a VM: without OKAY_WITH_CSHARP its Load() says "C# is not enabled in this build".
    if (language == CSharpScriptVM::kLanguage || language == "cs") return std::make_unique<CSharpScriptVM>();
    if (language == NextScriptVM::kLanguage) return std::make_unique<NextScriptVM>();
    return nullptr;
}

std::string RemovedScriptLanguageMessage(const std::string&) {
    // The old Mono C# backend is gone, but "csharp" is a language again (the hosted .NET runtime,
    // engine/src/CSharp), so no language name is "removed" any more.
    return {};
}

} // namespace okay
