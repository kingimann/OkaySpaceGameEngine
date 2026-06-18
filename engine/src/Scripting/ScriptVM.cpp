#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"

#if defined(OKAY_WITH_LUA)
#  include "okay/Scripting/LuaScriptVM.hpp"
#endif
#if defined(OKAY_WITH_CSHARP)
#  include "okay/Scripting/CSharpScriptVM.hpp"
#endif

namespace okay {

std::vector<std::string> AvailableScriptLanguages() {
    std::vector<std::string> langs{"okayscript"};
#if defined(OKAY_WITH_LUA)
    langs.push_back("lua");
#endif
#if defined(OKAY_WITH_CSHARP)
    langs.push_back("csharp");
#endif
    return langs;
}

std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language) {
    if (language == "okayscript" || language.empty())
        return std::make_unique<OkayScriptVM>();
#if defined(OKAY_WITH_LUA)
    if (language == "lua") return std::make_unique<LuaScriptVM>();
#endif
#if defined(OKAY_WITH_CSHARP)
    if (language == "csharp" || language == "cs")
        return std::make_unique<CSharpScriptVM>();
#endif
    return nullptr;
}

} // namespace okay
