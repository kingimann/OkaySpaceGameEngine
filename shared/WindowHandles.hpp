#pragma once
// A native window handle (the SDL_Window* of an sdl2 window, the X11 id or HWND of an own-backend window) to its okay::platform::Window.
// The launcher keeps its windows as raw handles in many places; this lets the helpers that act on them work on either backend.
#include "okay/Platform/Platform.hpp"

#include <memory>
#include <unordered_map>

namespace okay::pb {

inline std::unordered_map<void*, okay::platform::Window*>& WindowTable() {
    static auto* t = new std::unordered_map<void*, okay::platform::Window*>();   // (leaked on purpose: windows may outlive static destruction)
    return *t;
}
inline void RegisterWindow(okay::platform::Window& w) { WindowTable()[w.NativeHandle()] = &w; }
inline void UnregisterWindow(okay::platform::Window& w) { WindowTable().erase(w.NativeHandle()); }

/// The Window for `handle`: the registered one, else (an SDL_Window* the app made itself) a non-owning wrapper made for the call.
class WindowRef {
public:
    explicit WindowRef(void* handle) {
        auto it = WindowTable().find(handle);
        if (it != WindowTable().end()) m_w = it->second;
        else { m_own = okay::platform::WrapSdl2Window(handle); m_w = m_own.get(); }
    }
    explicit operator bool() const { return m_w != nullptr; }
    okay::platform::Window* operator->() const { return m_w; }
private:
    okay::platform::Window* m_w = nullptr;
    std::unique_ptr<okay::platform::Window> m_own;
};
inline WindowRef Win(void* handle) { return WindowRef(handle); }

} // namespace okay::pb
