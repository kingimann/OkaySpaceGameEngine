#include "okay/Scripting/LuaScriptVM.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string>

namespace okay {

struct LuaScriptVM::State { lua_State* L = nullptr; };

namespace {
// Fetch the owning VM (stashed as an upvalue) so host functions reach the host.
LuaScriptVM* Self(lua_State* L) {
    return static_cast<LuaScriptVM*>(lua_touserdata(L, lua_upvalueindex(1)));
}
} // namespace

// We need access to the host from the C closures; expose a tiny accessor.
struct LuaHostAccess {
    static ScriptHost* Host(LuaScriptVM* vm);
};

LuaScriptVM::LuaScriptVM() {
    m_state = new State();
    m_state->L = luaL_newstate();
    luaL_openlibs(m_state->L);
}

LuaScriptVM::~LuaScriptVM() {
    if (m_state) {
        if (m_state->L) lua_close(m_state->L);
        delete m_state;
    }
}

void LuaScriptVM::Bind(ScriptHost* host) { m_host = host; }

namespace {
Transform* HostTransform(lua_State* L) {
    LuaScriptVM* vm = Self(L);
    ScriptHost* h = vm ? LuaHostAccess::Host(vm) : nullptr;
    return h ? h->transform : nullptr;
}

int l_print(lua_State* L) {
    int n = lua_gettop(L);
    std::string s;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) s += " ";
        if (const char* str = lua_tostring(L, i)) s += str;
    }
    Log::Info("[lua] ", s);
    return 0;
}
int l_move(lua_State* L) {
    if (Transform* t = HostTransform(L))
        t->Translate({(float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0), 0});
    return 0;
}
int l_set_pos(lua_State* L) {
    if (Transform* t = HostTransform(L))
        t->localPosition = {(float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0), 0};
    return 0;
}
int l_rotate(lua_State* L) {
    if (Transform* t = HostTransform(L)) t->Rotate({0, 0, (float)luaL_optnumber(L, 1, 0)});
    return 0;
}
int l_pos_x(lua_State* L) { Transform* t = HostTransform(L); lua_pushnumber(L, t ? t->localPosition.x : 0.0); return 1; }
int l_pos_y(lua_State* L) { Transform* t = HostTransform(L); lua_pushnumber(L, t ? t->localPosition.y : 0.0); return 1; }
int l_time(lua_State* L)  { lua_pushnumber(L, Time::ElapsedTime()); return 1; }
int l_dt(lua_State* L) {
    LuaScriptVM* vm = Self(L);
    ScriptHost* h = vm ? LuaHostAccess::Host(vm) : nullptr;
    lua_pushnumber(L, h ? h->deltaTime : 0.0);
    return 1;
}
int l_axis_x(lua_State* L) { lua_pushnumber(L, Input::AxisWASD().x); return 1; }
int l_axis_y(lua_State* L) { lua_pushnumber(L, Input::AxisWASD().y); return 1; }
int l_key(lua_State* L) {
    const char* s = luaL_optstring(L, 1, "");
    lua_pushboolean(L, s && s[0] && Input::GetKey(s[0]));
    return 1;
}
int l_get(lua_State* L) {
    LuaScriptVM* vm = Self(L);
    ScriptHost* h = vm ? LuaHostAccess::Host(vm) : nullptr;
    const char* k = luaL_optstring(L, 1, "");
    if (h) {
        auto it = h->globals.find(k);
        if (it != h->globals.end()) { lua_pushnumber(L, it->second.AsFloat()); return 1; }
    }
    lua_pushnil(L);
    return 1;
}
int l_set(lua_State* L) {
    LuaScriptVM* vm = Self(L);
    ScriptHost* h = vm ? LuaHostAccess::Host(vm) : nullptr;
    const char* k = luaL_optstring(L, 1, "");
    if (h && k[0]) h->globals[k] = (float)luaL_optnumber(L, 2, 0);
    return 0;
}

void Register(lua_State* L, LuaScriptVM* vm, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, vm);
    lua_pushcclosure(L, fn, 1);
    lua_setglobal(L, name);
}
} // namespace

ScriptHost* LuaHostAccess::Host(LuaScriptVM* vm) { return vm->m_host; }

bool LuaScriptVM::Load(const std::string& source, std::string* error) {
    lua_State* L = m_state->L;
    Register(L, this, "print",  l_print);
    Register(L, this, "move",   l_move);
    Register(L, this, "set_pos",l_set_pos);
    Register(L, this, "rotate", l_rotate);
    Register(L, this, "pos_x",  l_pos_x);
    Register(L, this, "pos_y",  l_pos_y);
    Register(L, this, "time",   l_time);
    Register(L, this, "dt",     l_dt);
    Register(L, this, "axis_x", l_axis_x);
    Register(L, this, "axis_y", l_axis_y);
    Register(L, this, "key",    l_key);
    Register(L, this, "get",    l_get);
    Register(L, this, "set",    l_set);

    if (luaL_loadstring(L, source.c_str()) != LUA_OK ||
        lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::string e = lua_tostring(L, -1) ? lua_tostring(L, -1) : "lua error";
        if (error) *error = e;
        Log::Error("Lua: ", e);
        lua_pop(L, 1);
        m_loaded = false;
        return false;
    }
    m_loaded = true;
    return true;
}

void LuaScriptVM::CallVoid(const char* fn, bool pushDelta, float delta) {
    if (!m_loaded) return;
    lua_State* L = m_state->L;
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    int nargs = 0;
    if (pushDelta) { lua_pushnumber(L, delta); nargs = 1; }
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        Log::Error("Lua ", fn, "(): ", lua_tostring(L, -1) ? lua_tostring(L, -1) : "error");
        lua_pop(L, 1);
    }
}

void LuaScriptVM::CallStart() { CallVoid("start", false, 0.0f); }
void LuaScriptVM::CallUpdate(float deltaTime) {
    if (m_host) m_host->deltaTime = deltaTime;
    CallVoid("update", true, deltaTime);
}

vs::VsValue LuaScriptVM::GetGlobal(const std::string& name) const {
    lua_State* L = m_state->L;
    lua_getglobal(L, name.c_str());
    vs::VsValue out;
    if (lua_isboolean(L, -1))      out = (bool)lua_toboolean(L, -1);
    else if (lua_isnumber(L, -1))  out = (float)lua_tonumber(L, -1);
    else if (lua_isstring(L, -1))  out = std::string(lua_tostring(L, -1));
    lua_pop(L, 1);
    return out;
}

} // namespace okay
