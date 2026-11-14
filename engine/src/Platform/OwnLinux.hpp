#pragma once
// Private to the own Linux backend: the three parts (X11/GLX, ALSA, evdev) and
// the few test hooks. Nothing here is public API.
#include "okay/Platform/Platform.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay::platform::own {

// ---- X11 / GLX, loaded with dlopen ---------------------------------------------
// One connection to the X server. Windows made through it share its event queue.
class X11System {
public:
    virtual ~X11System() = default;
    /// Connect (honours $DISPLAY). Null with `err` set when libX11 is missing,
    /// there is no display, or this build had no X11 headers.
    static std::unique_ptr<X11System> Open(std::string& err);

    virtual std::unique_ptr<Window> MakeWindow(const WindowDesc& d, std::string& err) = 0;
    virtual bool PollEvent(Event& out) = 0;
    virtual void* GLProcAddress(const char* name) = 0;
    virtual void ReleaseCurrent() = 0;
    virtual void StartTextInput() = 0;
    virtual void StopTextInput() = 0;
    virtual bool IsKeyDown(Key k) = 0;
    virtual uint32_t MouseState(int* x, int* y) = 0;
    virtual bool SetRelativeMouse(bool on) = 0;
    virtual void ShowCursor(bool show) = 0;
    virtual void WarpMouse(Window* w, int x, int y) = 0;
    virtual uint32_t RelativeMouseState(int* dx, int* dy) = 0;
    virtual Key KeyForChar(char c) = 0;
    virtual bool IsCursorShown() = 0;
    /// System cursor shape from the core cursor font (no Xcursor). False when the server has no cursor font.
    virtual bool SetCursor(CursorShape shape) = 0;
    /// Monitors (RandR 1.5 when libXrandr is there, else the one X screen). Primary first. The list is cached for
    /// half a second.
    virtual int DisplayCount() = 0;
    virtual bool GetDisplayInfo(int index, DisplayInfo& out) = 0;
    /// A modal box with one OK button: an X window drawn with a core font, or a dialog program (zenity, kdialog,
    /// xmessage) when no core font can be loaded. False when neither worked.
    virtual bool MessageBox(const std::string& title, const std::string& text, Window* parent) = 0;
    virtual bool SetClipboardText(const std::string& t) = 0;
    virtual std::string GetClipboardText() = 0;
    virtual bool ClipboardPut(const std::string& mime, const std::vector<uint8_t>& data) = 0;
    virtual bool ClipboardFetch(const std::string& mime, std::vector<uint8_t>& out) = 0;
    virtual std::vector<std::string> ClipboardFormats() = 0;
    /// Test hook: the window's current pixels as RGBA8 (XGetImage), top row first.
    virtual bool GrabWindow(Window& w, std::vector<uint8_t>& rgba, int& width, int& height) = 0;
};

// ---- ALSA, loaded with dlopen ----------------------------------------------------
/// `device` null = $OKAY_ALSA_DEVICE or "default". Null result: `err` says why.
std::unique_ptr<AudioDevice> OpenAlsa(const AudioSpec& want, bool capture, const char* device,
                                      AudioSpec* got, std::string& err);
int AlsaDeviceCount(bool capture);
std::string AlsaDeviceName(int index, bool capture);

/// Test hook: RGBA8 pixels of a window made by the own backend `b` (false for other backends).
bool GrabWindowPixels(Backend& b, Window& w, std::vector<uint8_t>& rgba, int& width, int& height);

// ---- evdev game controllers ----------------------------------------------------------
int EvdevGamepadCount();                 // /dev/input/event* nodes that look like gamepads
std::string EvdevGamepadPath(int index);
std::unique_ptr<Gamepad> OpenEvdevGamepad(int index);

/// What a controller reports, so the mapping can be tested with no device.
struct EvdevAbsRange { int min = 0, max = 0; bool present = false; };
struct EvdevCaps {
    EvdevAbsRange abs[64];               // indexed by ABS_* code
    bool hasTriggerButtons = false;      // BTN_TL2 / BTN_TR2 stand in for the triggers
    bool canRumble = false;
    std::vector<uint16_t> keys;          // key codes >= BTN_MISC the device has, ascending (for the raw button numbering)
    uint16_t bus = 0, vendor = 0, product = 0, version = 0;   // EVIOCGID; picks the entry in the mapping table
};
/// Add mapping lines (SDL-style database text, see PadMapping.hpp) to the table the evdev pads use; later
/// lines win. Returns how many lines were taken. Pads opened afterwards see them. The table starts with our
/// own built-in entries (PadMappingData.hpp) plus the file named by $OKAY_PAD_MAPPINGS, if set.
size_t EvdevAddMappings(const std::string& text);
/// True when `g` (a pad from this backend) is read through a mapping table entry, false for the fixed layout.
bool EvdevPadUsesMapping(const Gamepad* g);
/// Test hook: a gamepad that reads struct input_event records from `fd` (a pipe).
/// Takes ownership of the fd.
std::unique_ptr<Gamepad> OpenEvdevFromFd(int fd, const EvdevCaps& caps, const char* name);
/// Poll hotplug: appends GamepadAdded / GamepadRemoved for devices that appeared
/// or went away since the last call (at most once a second), for open pads too.
void EvdevPollHotplug(std::vector<Event>& out);

} // namespace okay::platform::own
