#pragma once
// Operating-system clipboard for the player and for editor Play mode, through
// okay::platform. On the sdl2 backend (the default) this makes the same SDL clipboard
// calls the old SdlClipboard made; on the own backend it uses the own clipboard.
#include "okay/Core/Clipboard.hpp"
#include "okay/Platform/Platform.hpp"

namespace okay {

class PlatformClipboard final : public Clipboard {
public:
    PlatformClipboard() = default;
    /// `b` null = okay::platform::Default() at call time.
    explicit PlatformClipboard(platform::Backend* b) : m_backend(b) {}
    std::string GetText() const override {
        platform::Backend* b = Pick();
        return b ? b->GetClipboardText() : std::string();
    }
    bool SetText(const std::string& text) override {
        platform::Backend* b = Pick();
        return b && b->SetClipboardText(text);
    }
private:
    platform::Backend* Pick() const { return m_backend ? m_backend : platform::Default(); }
    platform::Backend* m_backend = nullptr;
};

} // namespace okay
