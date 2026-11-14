#pragma once
// The OkayUI render backends (see OkayUIRender.hpp for the contract). Each one is a render::Backend in its own file
// under okayui/render/. All of them are OWN CODE, OFF BY DEFAULT and wired into nothing.
//
//   Soft      OkayUIRender_Soft.cpp      CPU, into an RGBA bitmap. The REFERENCE: every other backend is compared with it.
//   Painter   OkayUIRender_Painter.cpp   through okay::platform::Painter (sdl2 painter or the own software painter).
//   OpenGL    OkayUIRender_GL.cpp        OpenGL 3.3 core or OpenGL ES 3.0, entry points through a get-proc function
//                                        (no link-time GL dependency, like okay::GLRenderer::LoadGL).
//   D3D11     OkayUIRender_D3D11.cpp     Direct3D 11 (Windows only; the file is empty elsewhere).
#include "okay/UI/OkayUIRender.hpp"

#include <memory>
#include <string>

namespace okay { namespace platform { class Painter; } }

namespace OkayUI {
namespace soft { struct Bitmap; }

namespace render {

// ---- software (reference) ---------------------------------------------------------------------------------
/// Draws into `target`, which the caller sizes to the framebuffer (BeginFrame fails when it does not match) and
/// clears. The bitmap must outlive the backend. Top-left fill rule, source-over blending, per-span scissor,
/// bilinear or nearest textures. With DPI 1 and nearest atlas sampling it is byte-identical to soft::Render.
std::unique_ptr<Backend> CreateSoftBackend(soft::Bitmap& target);

// ---- okay::platform::Painter ------------------------------------------------------------------------------
/// Draws through `painter` (sdl2 or software): spans become SetClipRect + Geometry calls on painter textures. The
/// painter's scale must be 1 (the DPI scale is applied to the vertices here). Does not clear and does not present.
/// The painter must outlive the backend. ReadPixels is the painter's.
std::unique_ptr<Backend> CreatePainterBackend(okay::platform::Painter& painter);

// ---- OpenGL ------------------------------------------------------------------------------------------------
enum class GLFlavor { Core33, ES30 };
using GLGetProc = void* (*)(const char*);
/// A GL context of the right kind must be current, now and for every later call on the backend. `getProc` resolves
/// entry points (SDL_GL_GetProcAddress, eglGetProcAddress, wglGetProcAddress ...). Returns null and sets `err` when
/// an entry point is missing or the shader does not build. Draws into the framebuffer bound at BeginFrame, over a
/// viewport of the frame size, and restores the GL state it changed.
std::unique_ptr<Backend> CreateGLBackend(GLGetProc getProc, GLFlavor flavor, std::string* err = nullptr);
/// The shader source the OpenGL backend builds (for tests and tools): stage 0 = vertex, 1 = fragment.
const char* GLShaderSource(GLFlavor flavor, int stage);

// ---- Direct3D 11 (Windows) --------------------------------------------------------------------------------
/// `device` is an ID3D11Device*, `context` its immediate ID3D11DeviceContext*; a render target is bound when
/// BeginFrame is called. Draws with a scissor rectangle per span and restores the pipeline state it changed.
/// Null (and `err`) when a shader does not compile. Always null off Windows.
std::unique_ptr<Backend> CreateD3D11Backend(void* device, void* context, std::string* err = nullptr);
/// The HLSL source (one file, entry points VSMain and PSMain) the Direct3D 11 backend compiles, on every platform, so
/// tests can look at it.
const char* D3D11ShaderSource();

} // namespace render
} // namespace OkayUI
