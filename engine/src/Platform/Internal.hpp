#pragma once
// Private to okay_platform: the backend factories (defined only when compiled in).
#include "okay/Platform/Platform.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay::platform {
std::unique_ptr<Backend> CreateSdl2Backend();   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2)
std::unique_ptr<Backend> CreateOwnBackend();    // OwnLinux.cpp / OwnWin32.cpp (OKAY_PLATFORM_HAVE_OWN)
std::unique_ptr<Window> WrapSdl2WindowImpl(void* sdlWindow);   // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2): non-owning view
bool TranslateSdl2EventImpl(const void* sdlEvent, Event& out);  // SdlBackend.cpp (OKAY_PLATFORM_HAVE_SDL2)
/// Platform.cpp: a mode list as Backend::DisplayModes returns it (no empty sizes, largest first, no repeats).
std::vector<VideoMode> SortedVideoModes(std::vector<VideoMode> modes);
/// Platform.cpp: the index in `displays` of the display holding the centre of (x, y, w, h), else the one it overlaps most; -1 = none.
int DisplayIndexForRect(const std::vector<DisplayInfo>& displays, int x, int y, int w, int h);

// ---- touch (Platform.cpp), shared by the own X11 and Win32 backends ------------------------------------------------------
/// What happened to one touch contact, as the OS reports it.
enum class TouchPhase { Begin, Move, End, Cancel };
/// One touch report from the OS, already in the window's pixels (x, y from its top-left corner; winW x winH its client size).
struct TouchSample {
    long long id = 0;            // the OS's contact id: the same for one finger from Begin to End
    TouchPhase phase = TouchPhase::Begin;
    double x = 0.0, y = 0.0;
    int winW = 0, winH = 0;
    float pressure = 1.0f;       // 0..1; 1 when the device does not measure it
    uint32_t windowId = 0;
    uint32_t timestampMs = 0;
};
/// A window position as a 0..1 fraction of its extent (clamped; 0 when the extent is not positive), what Event::fx / fy hold.
float NormalizedTouch(double pos, int extent);
/// Turns touch samples into FingerDown / FingerMotion / FingerUp events (fingerId = the OS id, fx / fy normalised, pressure),
/// the shape the sdl2 backend gives. With `mouseFromTouch` the first finger down also drives the mouse (MouseMove and the left
/// button, fromTouch = true, a second tap within 400 ms and 24 pixels counts as a double click), as SDL does by default; the
/// X11 backend wants that because the X server stops making pointer events for a window that takes touch events. Win32 does
/// not: Windows makes the mouse messages itself. Moves of an unknown contact and repeated Begins are dropped or made Moves.
class TouchTracker {
public:
    explicit TouchTracker(bool mouseFromTouch) : m_mouse(mouseFromTouch) {}
    void Feed(const TouchSample& s, std::vector<Event>& out);
    int ActiveContacts() const { return (int)m_down.size(); }
    /// Forget the contacts of a window that is gone (no events).
    void ForgetWindow(uint32_t windowId);
private:
    struct Contact { long long id; uint32_t windowId; };
    std::vector<Contact> m_down;
    bool m_mouse;
    bool m_mouseHeld = false;             // a finger is acting as the left button
    long long m_mouseFinger = 0;
    int m_mx = 0, m_my = 0;               // the last mouse position the touch gave
    bool m_haveMouse = false;
    uint32_t m_lastTapMs = 0; int m_lastTapX = 0, m_lastTapY = 0, m_clicks = 0;
};

#if defined(_WIN32)
// OwnWin32Audio.cpp: waveOut / waveIn through winmm.dll loaded at run time. Null result: `err` says why.
std::unique_ptr<AudioDevice> OpenWinmmAudio(const AudioSpec& want, bool capture, const char* name, AudioSpec* got, std::string& err);
int WinmmDeviceCount(bool capture);
std::string WinmmDeviceName(int index, bool capture);
#endif
} // namespace okay::platform
