#pragma once
#include "okay/Scripting/ScriptVM.hpp"

namespace okay {

/// Lua scripting backend (real Lua, embedded via the C API). Only compiled when
/// the engine is built with -DOKAY_WITH_LUA=ON and a Lua development package is
/// present. Scripts define start() and update(dt) and call the same host API as
/// the built-in language: move(), set_pos(), rotate(), pos_x(), time(), key(),
/// print(), get()/set(), and so on.
class LuaScriptVM : public IScriptVM {
public:
    LuaScriptVM();
    ~LuaScriptVM() override;

    const char* Language() const override { return "lua"; }
    bool Load(const std::string& source, std::string* error = nullptr) override;
    void Bind(ScriptHost* host) override;
    void CallStart() override;
    void CallUpdate(float deltaTime) override;
    vs::VsValue GetGlobal(const std::string& name) const override;

private:
    friend struct LuaHostAccess;
    void CallVoid(const char* fn, bool pushDelta, float delta);

    struct State;            // hides the lua_State from this header
    State* m_state = nullptr;
    ScriptHost* m_host = nullptr;
    bool m_loaded = false;
};

} // namespace okay
