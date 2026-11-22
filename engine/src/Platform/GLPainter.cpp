// The OpenGL okay::platform::Painter (own code, off by default). See Painter.hpp.
#include "okay/Platform/Painter.hpp"

namespace okay::platform {

std::unique_ptr<Painter> CreateGLPainter(Window& w, Backend& b, const PainterDesc& d, std::string* err) {
    (void)w; (void)b; (void)d;
    if (err) *err = "the OpenGL painter is not written yet";
    return nullptr;
}

} // namespace okay::platform
