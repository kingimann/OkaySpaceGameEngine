#pragma once
// Pure display maths and Present() result handling for the Xbox UWP backend. No Windows headers: tested on Linux.
// Written from Microsoft's documentation: CoreWindow.Bounds is in DIPs (device-independent pixels, 1 DIP = 1/96 inch),
// a DIP size becomes pixels as dips * dpi / 96 (DisplayInformation.LogicalDpi), and the DXGI result codes
// DXGI_ERROR_DEVICE_REMOVED 0x887A0005, DXGI_ERROR_DEVICE_RESET 0x887A0007, DXGI_STATUS_OCCLUDED 0x087A0001.
// UNVERIFIED on Windows or an Xbox. The numbers (1080p cap) are our conservative default, not a measured limit.
#include <cmath>
#include <cstdint>

namespace okay::xbox {

struct SizePx { int w = 0, h = 0; bool operator==(const SizePx& o) const { return w == o.w && h == o.h; } };

/// DIPs to pixels, rounded to the nearest pixel, never below 1 for a positive size. dpi <= 0 is treated as 96.
inline int DipsToPixels(float dips, float dpi) {
    if (!(dips > 0.0f)) return 0;
    if (!(dpi > 0.0f)) dpi = 96.0f;
    const double px = std::floor(double(dips) * double(dpi) / 96.0 + 0.5);
    if (px < 1.0) return 1;
    if (px > 16384.0) return 16384;
    return int(px);
}

/// The back-buffer size for a window of `win` pixels: the window size, scaled down (aspect kept) so neither side passes the cap.
/// Swap-chain scaling (DXGI_SCALING_STRETCH) stretches it to the window. Cap 1920x1080 is our default for a UWP app on a console
/// (UNVERIFIED: Microsoft's pages list what a UWP app may use on Xbox One S/X). Even sizes (video-friendly), at least 16x16.
inline SizePx ChooseBackBuffer(SizePx win, SizePx cap = {1920, 1080}) {
    if (win.w < 1 || win.h < 1) return {16, 16};
    double s = 1.0;
    if (win.w > cap.w) s = double(cap.w) / double(win.w);
    if (win.h * s > cap.h) s = double(cap.h) / double(win.h);
    int w = int(std::floor(win.w * s + 0.5)), h = int(std::floor(win.h * s + 0.5));
    w &= ~1; h &= ~1;
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    return {w, h};
}

enum class PresentOutcome { Ok, Occluded, DeviceLost, Failed };
/// What to do after IDXGISwapChain::Present returned `hr`. Occluded is not an error (skip drawing, keep going). DeviceLost = the
/// device was removed or reset: release everything and rebuild it. Anything else that failed is Failed.
inline PresentOutcome ClassifyPresent(std::int32_t hr) {
    const std::uint32_t u = std::uint32_t(hr);
    if (u == 0x887A0005u || u == 0x887A0007u) return PresentOutcome::DeviceLost;   // DEVICE_REMOVED, DEVICE_RESET
    if (u == 0x087A0001u) return PresentOutcome::Occluded;                         // DXGI_STATUS_OCCLUDED (a success code)
    return hr >= 0 ? PresentOutcome::Ok : PresentOutcome::Failed;
}

/// Tracks what the render loop should do about the swap chain: one place for "size changed", "device lost", "occluded".
class PresentTracker {
public:
    void WindowSized(SizePx winPx) { want_ = ChooseBackBuffer(winPx, cap_); }
    void SetCap(SizePx cap) { cap_ = cap; }
    SizePx Wanted() const { return want_; }
    /// True when the swap chain's buffers must be resized to Wanted() before the next frame.
    bool NeedsResize() const { return have_.w != want_.w || have_.h != want_.h; }
    void Resized(SizePx now) { have_ = now; }
    void Report(std::int32_t hr) {
        switch (ClassifyPresent(hr)) {
            case PresentOutcome::DeviceLost: lost_ = true; break;
            case PresentOutcome::Occluded: occluded_ = true; break;
            case PresentOutcome::Ok: occluded_ = false; break;
            case PresentOutcome::Failed: ++failures_; break;
        }
    }
    bool DeviceLost() const { return lost_; }
    bool Occluded() const { return occluded_; }
    int Failures() const { return failures_; }
    /// After the device was rebuilt.
    void DeviceRebuilt() { lost_ = false; have_ = {}; failures_ = 0; }
    /// Draw this frame? Not while the device is lost or the window is fully covered.
    bool ShouldDraw() const { return !lost_ && !occluded_; }
private:
    SizePx cap_{1920, 1080}, want_{}, have_{};
    bool lost_ = false, occluded_ = false; int failures_ = 0;
};

} // namespace okay::xbox
