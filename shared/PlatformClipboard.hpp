#pragma once
// Operating-system clipboard for the player and for editor Play mode, through okay::platform
// (the SDL2 backend by default, so the behaviour is the same as the direct SDL calls it replaced).
#include "okay/Core/Clipboard.hpp"
#include "okay/Platform/Platform.hpp"

namespace okay {

class PlatformClipboard final : public Clipboard {
public:
    std::string GetText() const override {
        okay::platform::Backend* b = okay::platform::Default();
        return b ? b->GetClipboardText() : std::string();
    }
    bool SetText(const std::string& text) override {
        okay::platform::Backend* b = okay::platform::Default();
        return b && b->SetClipboardText(text);
    }
};

} // namespace okay
