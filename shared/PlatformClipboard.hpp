#pragma once
// Operating-system clipboard for editor Play mode (and any host) through okay::platform, with no SDL: the editor's no-SDL mode
// (OKAY_EDITOR_PLATFORM=own + OKAY_EDITOR_RENDERER=own + OKAY_PLATFORM_BACKEND=own, docs/sdl_removal_plan.md stage E3) installs
// it where the default path installs shared/SdlClipboard.hpp. Written from the two interfaces (okay::Clipboard in
// engine/include/okay/Core/Clipboard.hpp, Backend::GetClipboardText / SetClipboardText in okay/Platform/Platform.hpp).
#include "okay/Core/Clipboard.hpp"
#include "okay/Platform/Platform.hpp"

namespace okay {

class PlatformClipboard final : public Clipboard {
public:
    explicit PlatformClipboard(okay::platform::Backend* b = nullptr) : m_b(b) {}
    void SetBackend(okay::platform::Backend* b) { m_b = b; }
    std::string GetText() const override { return m_b ? m_b->GetClipboardText() : std::string(); }
    bool SetText(const std::string& text) override { return m_b && m_b->SetClipboardText(text); }

private:
    okay::platform::Backend* m_b;
};

} // namespace okay
