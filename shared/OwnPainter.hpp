#pragma once
// Which painter draws a window of the own platform backend (OKAY_PLATFORM_BACKEND=own, off by default).
//
//   OKAY_OWN_PAINTER unset, "opengl" or "gl"   the OpenGL painter (okay::platform::CreateGLPainter: GLX on Linux, WGL on Windows),
//                                              and the software painter when GL cannot start on that window (the reason is logged).
//   OKAY_OWN_PAINTER "software" or "soft"      the software painter (OkayUI::soft::CreateWindowPainter: CPU, XPutImage / StretchDIBits).
//
// The app registers MakeOwnPainter with okay::pb::SetOwnPainterFactory and asks for GL-capable windows with OwnWindowsWantGL, so
// okay::pb::CreateRenderer on an own-backend window gets the painter chosen here. Each choice is printed once per window on stderr.
#include "PainterBridge.hpp"
#if __has_include("OkayUI_SoftPainter.hpp")   // the software painter is there when the app links okay_ui_soft
#include "OkayUI_SoftPainter.hpp"
#define OKAY_OWN_PAINTER_HAVE_SOFT 1
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace okay::pb {

enum class OwnPainterPick { OpenGL, Software };

/// The value of OKAY_OWN_PAINTER as a choice: "software" / "soft" (any letter case) = Software; anything else, or null, = OpenGL.
inline OwnPainterPick ParseOwnPainter(const char* v) {
    if (!v) return OwnPainterPick::OpenGL;
    std::string s(v);
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s == "software" || s == "soft" ? OwnPainterPick::Software : OwnPainterPick::OpenGL;
}
inline OwnPainterPick OwnPainterChoice() { return ParseOwnPainter(std::getenv("OKAY_OWN_PAINTER")); }

/// True when windows made on `b` should carry a GL pixel format (WindowDesc::opengl): `b` is the own backend and OpenGL will be tried.
inline bool OwnWindowsWantGL(const okay::platform::Backend* b) {
    return b && b->Kind() == okay::platform::BackendKind::Own && OwnPainterChoice() == OwnPainterPick::OpenGL;
}

/// The painter for an own-backend window: OpenGL first unless OKAY_OWN_PAINTER says software, the software painter as the fallback.
inline std::unique_ptr<okay::platform::Painter> MakeOwnPainter(okay::platform::Window& w, const okay::platform::PainterDesc& d) {
    if (OwnPainterChoice() == OwnPainterPick::OpenGL) {
        if (okay::platform::Backend* b = okay::platform::Get(okay::platform::BackendKind::Own)) {
            std::string err;
            if (std::unique_ptr<okay::platform::Painter> p = okay::platform::CreateGLPainter(w, *b, d, &err)) {
                std::fprintf(stderr, "own painter: opengl (OKAY_OWN_PAINTER=software for the CPU painter)\n");
                return p;
            }
            std::fprintf(stderr, "own painter: OpenGL could not start (%s); using the software painter\n", err.c_str());
        }
    } else std::fprintf(stderr, "own painter: software (OKAY_OWN_PAINTER=software)\n");
#ifdef OKAY_OWN_PAINTER_HAVE_SOFT
    return OkayUI::soft::CreateWindowPainter(w, d);
#else
    std::fprintf(stderr, "own painter: this build has no software painter (okay_ui_soft not linked)\n");
    return nullptr;
#endif
}

} // namespace okay::pb
