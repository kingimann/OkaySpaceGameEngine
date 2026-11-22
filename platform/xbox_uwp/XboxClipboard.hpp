#pragma once
// The Windows clipboard seam for the UWP app (pure, no Windows headers; tested on Linux by tests/test_xbox_uwp_pieces.cpp over a fake host).
// The engine already has okay::Clipboard (engine/include/okay/Core/Clipboard.hpp: a memory clipboard, and SetClipboardText /
// GetClipboardText on top of it). On UWP the real clipboard (Windows.ApplicationModel.DataTransfer.Clipboard) reads text ASYNCHRONOUSLY
// (DataPackageView.GetTextAsync), which a game thread must not wait for. So this adapter:
//   * SetText: updates the in-app copy at once, then writes to the system through the host. A host failure (no clipboard on this device,
//     access denied) is not an error for the game: the text still pastes inside the app, and InAppOnly() says so.
//   * GetText: returns the newest text we know. Each call also collects a finished system read (replacing the copy when the system text is
//     different) and starts the next read, so text copied in another app arrives a frame or two later, never blocking.
// The host (the C++/WinRT glue) is the only part that needs Windows: Write(text), StartRead(), TakeRead(text). Written from Microsoft's
// documentation of Clipboard.SetContent, Clipboard.GetContent and DataPackageView.GetTextAsync. The glue is UNVERIFIED (never built).
#include "okay/Core/Clipboard.hpp"
#include <string>

namespace okay::xbox {

class ClipboardHost {
public:
    virtual ~ClipboardHost() = default;
    virtual bool Write(const std::string& utf8) = 0;       // Clipboard.SetContent (synchronous); false when refused
    virtual bool StartRead() = 0;                           // begin GetTextAsync; false when no read can start (nothing is in flight)
    virtual bool TakeRead(std::string& utf8) = 0;           // true once, when a started read finished with text
};

class UwpClipboard final : public okay::Clipboard {
public:
    explicit UwpClipboard(ClipboardHost* host = nullptr) : host_(host) {}
    void SetHost(ClipboardHost* host) { host_ = host; }

    std::string GetText() const override {
        if (host_) {
            std::string t;
            if (host_->TakeRead(t)) { if (!stale_) text_ = t; stale_ = false; inFlight_ = false; }   // a read started before our own SetText is old: dropped
            if (!inFlight_) inFlight_ = host_->StartRead();
        }
        return text_;
    }
    bool SetText(const std::string& text) override {
        text_ = text; if (inFlight_) stale_ = true;
        inAppOnly_ = !(host_ && host_->Write(text));
        return true;
    }
    /// True when the last SetText could not reach the system clipboard (the text only pastes inside the game).
    bool InAppOnly() const { return inAppOnly_; }

private:
    ClipboardHost* host_;
    mutable std::string text_;
    mutable bool inFlight_ = false, stale_ = false;
    bool inAppOnly_ = false;
};

} // namespace okay::xbox
