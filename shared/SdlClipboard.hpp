#pragma once
// Operating-system clipboard for the player and for editor Play mode.
// Calls SDL directly. The default SDL path stays the default: this does not
// go through okay::platform, and nothing here turns that seam on.
#include "okay/Core/Clipboard.hpp"
#include <SDL.h>

namespace okay {

class SdlClipboard final : public Clipboard {
public:
    std::string GetText() const override {
        char* raw = SDL_GetClipboardText();
        if (!raw) return std::string();
        std::string text(raw);
        SDL_free(raw);
        return text;
    }
    bool SetText(const std::string& text) override {
        return SDL_SetClipboardText(text.c_str()) == 0;
    }
};

} // namespace okay
