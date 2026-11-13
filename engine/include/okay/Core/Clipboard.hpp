#pragma once
// Text the game can copy and paste. The engine keeps a memory clipboard so tests
// and a headless run never touch the operating system. The player and editor Play
// mode install an SDL clipboard (shared/SdlClipboard.hpp) on top of this. That
// path calls SDL directly. It does not go through okay::platform.
#include <cstddef>
#include <string>

namespace okay {

/// Characters kept on the clipboard. Longer text is cut on a character boundary.
inline constexpr std::size_t kMaxClipboardChars = 8192;
/// Bytes read from a pasted string. Anything past this is ignored, so a huge
/// paste cannot force a large allocation. A character that would be split is dropped.
inline constexpr std::size_t kMaxClipboardBytes = 64 * 1024;

/// Text safe to store or insert: invalid UTF-8 is dropped, control characters are
/// dropped (tab and newline stay, a carriage return is dropped so a Windows line
/// ending becomes a newline), and the result is capped at kMaxClipboardChars.
/// Call this on the game thread.
std::string SanitizeClipboardText(const std::string& text);

/// Where clipboard text lives. GetText / SetText are the raw store. Games and
/// scripts use GetClipboardText / SetClipboardText, which sanitize first.
class Clipboard {
public:
    virtual ~Clipboard() = default;
    virtual std::string GetText() const = 0;
    virtual bool SetText(const std::string& text) = 0;
};

/// In-memory clipboard. This is the default, and the stand-in tests install.
class MemoryClipboard final : public Clipboard {
public:
    std::string GetText() const override { return m_text; }
    bool SetText(const std::string& text) override { m_text = text; return true; }
private:
    std::string m_text;
};

/// The clipboard games and scripts read. Never null.
Clipboard& GameClipboard();
/// Install `clipboard` (the player passes its SDL clipboard). Null restores the
/// memory clipboard. The pointed-to object must outlive the install.
void InstallGameClipboard(Clipboard* clipboard);

/// Sanitized text currently on the clipboard. Empty when there is nothing.
std::string GetClipboardText();
/// Store sanitized text. False when the installed clipboard refuses the write.
bool SetClipboardText(const std::string& text);

} // namespace okay
