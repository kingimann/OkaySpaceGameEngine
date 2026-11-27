#pragma once
// Private: what makes OkayUI hold several contexts (CreateContext / SetCurrentContext, OkayUI_Context.cpp).
//
// OkayUI keeps its state in file-scope variables. Each file lists the variables that belong to ONE context (windows, ids, focus,
// input history, popups, style stacks, ...) once, at its end, with OKAYUI_CONTEXT_STATE. The context manager keeps one stored copy of
// every listed variable per context; switching contexts exchanges the live variables with the stored copies (std::swap, so strings,
// vectors and maps move safely), so the code that uses the variables does not change. A new context starts from a copy of each
// variable's value as it was when the program started (taken when the list is registered, at the end of the file's static start-up).
//
// Not listed, so shared by every context on purpose: the frame's geometry buffers (one frame is built and drawn at a time), the font
// atlas and font caches, the clipboard text, scratch buffers and constant tables. okayui/tests/test_okayui_contexts.cpp checks with
// `nm` that every data symbol of the library is either listed here or named in its shared list, so a new variable cannot be
// forgotten. Written by us for OkaySpace from Dear ImGui's documented behaviour of multiple contexts (each context has its own
// windows, input and ids); no code from any other project.
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

namespace OkayUI {
struct Context;
namespace ctxstate {

/// One variable of a context: where it lives and how to make, copy, exchange and free a stored copy of it.
struct VarOps {
    void* global = nullptr;
    const char* name = "";
    void* (*fromGlobal)(const void* global) = nullptr;   // a new stored copy holding the variable's current value
    void* (*fromStored)(const void* stored) = nullptr;   // a new stored copy of another stored copy
    void (*exchange)(void* global, void* stored) = nullptr;
    void (*destroy)(void* stored) = nullptr;
};

template <class T> struct Stored { T v{}; };
template <class T> void CopyValue(T& dst, const T& src) {
    if constexpr (std::is_array<T>::value) { for (std::size_t i = 0; i < std::extent<T>::value; ++i) CopyValue(dst[i], src[i]); }
    else dst = src;
}
template <class T> void SwapValue(T& a, T& b) {
    if constexpr (std::is_array<T>::value) { for (std::size_t i = 0; i < std::extent<T>::value; ++i) SwapValue(a[i], b[i]); }
    else { using std::swap; swap(a, b); }
}
template <class T> VarOps Var(T& g, const char* name) {
    VarOps o;
    o.global = (void*)std::addressof(g);
    o.name = name;
    o.fromGlobal = [](const void* gl) -> void* { auto* s = new Stored<T>(); CopyValue(s->v, *(const T*)gl); return s; };
    o.fromStored = [](const void* st) -> void* { auto* s = new Stored<T>(); CopyValue(s->v, ((const Stored<T>*)st)->v); return s; };
    o.exchange = [](void* gl, void* st) { SwapValue(*(T*)gl, ((Stored<T>*)st)->v); };
    o.destroy = [](void* st) { delete (Stored<T>*)st; };
    return o;
}

/// Adds the variables to the list (and takes their start-up values). Only from static start-up, before any context is made.
void Register(std::initializer_list<VarOps> vars);
struct Registrar { Registrar(std::initializer_list<VarOps> vars) { Register(vars); } };

/// The names of every listed variable (for the completeness test).
int RegisteredCount();
/// The handle of the context whose values are in the live variables now (RefreshContext fills it, so GImGui is that handle).
Context* LiveHandle();
const char* RegisteredName(int i);

} // namespace ctxstate
} // namespace OkayUI

#define OKAYUI_CTX_VAR(x) ::OkayUI::ctxstate::Var(x, #x)
#define OKAYUI_CTX_CAT2(a, b) a##b
#define OKAYUI_CTX_CAT(a, b) OKAYUI_CTX_CAT2(a, b)
/// At the end of a file, inside namespace OkayUI: OKAYUI_CONTEXT_STATE(OKAYUI_CTX_VAR(g_a), OKAYUI_CTX_VAR(g_b), ...)
#define OKAYUI_CONTEXT_STATE(...) \
    namespace { const ::OkayUI::ctxstate::Registrar OKAYUI_CTX_CAT(kContextState_, __LINE__){__VA_ARGS__}; }
