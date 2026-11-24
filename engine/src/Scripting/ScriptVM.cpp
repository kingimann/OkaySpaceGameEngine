#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scripting/CSharpHost.hpp"
#include "okay/Scripting/CSharpScriptVM.hpp"
#include "okay/Lua/LuaScriptVM.hpp"
#include <cctype>

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    std::vector<std::string> v{
        "okayscript",
        "lua",   // our own Lua 5.4 interpreter (okay::lua), not the removed third-party one
    };
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
    if (language == LuaScriptVM::kLanguage) return std::make_unique<LuaScriptVM>();
    return nullptr;
}

std::string RemovedScriptLanguageMessage(const std::string& language) {
    // Lua (our own interpreter) and C# (the hosted .NET runtime) are languages again; no removed name is left.
    (void)language;
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
