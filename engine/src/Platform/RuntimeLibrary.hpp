#pragma once
// A shared library the game developer supplies (the Store Kit's store libraries: steam_api64.dll,
// EOSSDK-Win64-Shipping.dll), opened at run time. Engine-internal; our own code over the operating system's
// LoadLibrary / dlopen.
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef DeleteFile   // windows.h's macro would rename IEpicService::DeleteFile
#else
#include <dlfcn.h>
#endif

namespace okay::detail {

class RuntimeLibrary {
public:
    bool Open(const std::string& path) {
#if defined(_WIN32)
        m_h = (void*)LoadLibraryA(path.c_str());
#else
        m_h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        return m_h != nullptr;
    }
    void* Find(const char* name) const {
        if (!m_h) return nullptr;
#if defined(_WIN32)
        return (void*)GetProcAddress((HMODULE)m_h, name);
#else
        return dlsym(m_h, name);
#endif
    }
    template <class F> F Get(const char* name) const { return reinterpret_cast<F>(Find(name)); }
    // Kept loaded for the life of the process: store libraries run their own threads and must not be unloaded.
    ~RuntimeLibrary() = default;

private:
    void* m_h = nullptr;
};

}  // namespace okay::detail
