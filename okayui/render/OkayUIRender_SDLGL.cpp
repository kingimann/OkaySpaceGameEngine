// OkayUI's own OpenGL renderer inside an SDL_Renderer (see OkayUIRenderSDL.hpp). STUB: not wired yet (test first).
#include "okay/UI/OkayUIRenderSDL.hpp"

#include <SDL.h>

namespace OkayUI {
namespace render {

struct SdlGLHost::Impl { HostFrame last; };

bool OwnRenderSwitchValue(const char*) { return false; }
bool OwnRenderSwitchOn() { return false; }
bool PickGLFlavor(const char*, const char*, GLFlavor*, const char** why) { if (why) *why = "not wired yet"; return false; }

SdlGLHost::SdlGLHost() : m(new Impl) {}
SdlGLHost::~SdlGLHost() = default;
const HostFrame& SdlGLHost::EndFrame(SDL_Renderer* r) { return EndFrameSdl(r); }
const HostFrame& SdlGLHost::EndFrameSdl(SDL_Renderer* r) {
    OkayUI::EndFrame(r);
    m->last = HostFrame(); m->last.path = HostPath::Sdl; m->last.reason = "not wired yet";
    return m->last;
}
const HostFrame& SdlGLHost::Last() const { return m->last; }
void SdlGLHost::Forget(SDL_Renderer*) {}
int SdlGLHost::Count() const { return 0; }

} // namespace render
} // namespace OkayUI
