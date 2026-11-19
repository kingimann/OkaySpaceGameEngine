#!/usr/bin/env python3
"""Inventory of what still calls SDL2, by directory, file and kind of API, against okay::platform.

Reads the source files of a git ref (so the result does not depend on what is checked out), removes comments and
string literals, finds every SDL word, and sorts each one into a *capability* (for example "window geometry" or
"mutexes"). Each capability says which okay::platform function already covers it, or that there is a GAP.
Writes docs/sdl_inventory.md:

  1. headline numbers per program (launcher, editor, player),
  2. the kinds of API (families) against the directories,
  3. every capability with its call counts and its coverage,
  4. the GAP ranking (what blocks how many call sites, in which program),
  5. one row per file, and the build and CI files that name SDL2.

  python3 scripts/sdl_inventory.py --ref origin/main --out docs/sdl_inventory.md     write the page
  python3 scripts/sdl_inventory.py --ref <sha> --check docs/sdl_inventory.md         exit 1 if the page differs
  python3 scripts/sdl_inventory.py --ref HEAD                                        print to stdout

What is counted (and what is not) is written at the top of the page. This script is not run by CI: a page that is
checked in CI would break whenever anyone adds or removes one SDL call. Re-run it when you move call sites.
Written from the repository's own sources and the public SDL2 API names only (CLAUDE.md rule 19a).
"""
import argparse
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# ---------------------------------------------------------------------------------------------------------------
# What to look for
# ---------------------------------------------------------------------------------------------------------------
GREP_PATTERN = (r'\bSDL_[A-Za-z0-9_]+|\bSDLK_|\bImGui_ImplSDL|\bKMOD_[A-Z]+|\bAUDIO_[SUF](8|16|32)'
                r'|#[ \t]*include[ \t]*[<"][^>"]*[Ss][Dd][Ll]|\b(Uint|Sint)(8|16|32|64)\b')
CODE_EXT = (".cpp", ".hpp", ".h", ".cc", ".inl", ".c", ".mm")
SKIP_TOP = ("docs",)                       # prose, not code
# Names that start with SDL_ but are ours, not SDL's.
NOT_SDL = {"SDL_DelayCompat"}              # engine/src/Net/PhotonTransport.cpp: a local helper function

TOKEN = re.compile(r'\b(SDL_[A-Za-z0-9_]+|SDLK_[A-Za-z0-9_]+|ImGui_ImplSDL[A-Za-z0-9_]+|KMOD_[A-Z0-9_]+'
                   r'|AUDIO_[SUF](?:8|16|32)(?:LSB|MSB|SYS)?|(?:Uint|Sint)(?:8|16|32|64))\b(\s*\()?')
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.M)

# The SDL2 implementation of the seams: it is SDL on purpose and goes away together with SDL2.
SEAM_BACKEND = (
    "engine/src/Platform/SdlBackend.cpp",
    "engine/src/Platform/SdlPainter.cpp",
    "engine/include/okay/UI/UIPainterSDL.hpp",
)

# ---------------------------------------------------------------------------------------------------------------
# Families (the kinds of API) and capabilities (one row each: which SDL names, what covers them)
# ---------------------------------------------------------------------------------------------------------------
FAMILIES = [
    ("window", "video / window"),
    ("events", "events (loop, quit, window events, drops)"),
    ("keyboard", "keyboard, text input, IME"),
    ("mouse", "mouse, cursor, touch"),
    ("gamepad", "gamepad / joystick"),
    ("audio", "audio device"),
    ("timers", "timers"),
    ("clipboard", "clipboard"),
    ("gl", "GL context"),
    ("renderer", "SDL_Renderer 2D drawing, textures, surfaces"),
    ("file", "file / IO (WAV, BMP)"),
    ("msgbox", "message boxes"),
    ("runtime", "platform info and runtime (init, paths, locale, hints, version, log)"),
    ("threads", "threads, mutexes, atomics (rule 21a)"),
    ("imgui", "Dear ImGui SDL backends"),
    ("misc", "misc (integer typedefs, math, memory, bool, includes)"),
]
FAMILY_LABEL = dict(FAMILIES)

# status: HAVE    okay::platform has a function; call sites in apps are still SDL (a mechanical move)
#         PARTIAL some of it is covered, the rest is a GAP
#         GAP     nothing in okay::platform yet
#         OTHER   not a platform-layer job (needs another replacement: threads, ImGui backends, <cstdint>, ...)
# Platform.hpp = engine/include/okay/Platform/Platform.hpp, Painter.hpp = engine/include/okay/Platform/Painter.hpp.
P = "Platform.hpp"
PP = "Painter.hpp"
CAPS = [
    # ---- window ----
    ("win.create", "window", "HAVE", "create and destroy a window, creation flags, centred position",
     f"{P}: Backend::MakeWindow(WindowDesc), kCentered (owning unique_ptr<Window>). The apps hold raw SDL_Window*, so the Window methods "
     "below are called on their windows through WrapSdl2Window (see win.adopt) until a window is made by MakeWindow",
     r"SDL_CreateWindow|SDL_DestroyWindow|SDL_Window|SDL_WINDOW_(RESIZABLE|ALLOW_HIGHDPI|SHOWN|BORDERLESS|MAXIMIZED|FULLSCREEN|FULLSCREEN_DESKTOP|OPENGL|HIDDEN)|SDL_WINDOWPOS_[A-Z]+"),
    ("win.adopt", "window", "HAVE", "(no SDL word) use the Window methods on a window the app made with SDL_CreateWindow",
     f"{P}: okay::platform::WrapSdl2Window(void*): a non-owning Window over the SDL_Window* an app made itself (null without sdl2). Used by the launcher (helpers in namespace lw) and the editor; goes away in step 6 with SDL2",
     r"(?!x)x"),
    ("win.geometry", "window", "HAVE", "window size, position, minimum size, title, id",
     f"{P}: Window::GetSize / GetPosition / SetSize / SetPosition / SetMinimumSize / SetTitle / Id",
     r"SDL_GetWindowSize|SDL_GetWindowPosition|SDL_SetWindowSize|SDL_SetWindowPosition|SDL_SetWindowMinimumSize|SDL_SetWindowTitle|SDL_GetWindowID"),
    ("win.visibility", "window", "HAVE", "show, hide, raise, minimize, restore",
     f"{P}: Window::Show / Hide / Raise / Minimize / Restore",
     r"SDL_ShowWindow|SDL_HideWindow|SDL_RaiseWindow|SDL_MinimizeWindow|SDL_RestoreWindow"),
    ("win.fullscreen", "window", "HAVE", "desktop fullscreen",
     f"{P}: Window::SetFullscreen / IsFullscreen, WindowDesc::fullscreen",
     r"SDL_SetWindowFullscreen"),
    ("win.icon", "window", "HAVE", "window icon",
     f"{P}: Window::SetIcon(rgba, w, h)", r"SDL_SetWindowIcon"),
    ("win.dpi", "window", "HAVE", "per-monitor DPI awareness (Windows hint)",
     f"{P}: Backend::SetPerMonitorDpiAware()", r"SDL_HINT_WINDOWS_DPI_AWARENESS"),
    ("win.refresh", "window", "HAVE", "refresh rate of the window's display",
     f"{P}: Window::RefreshRateHz()", r"SDL_GetWindowDisplayMode"),
    ("win.state.have", "window", "HAVE", "is the window focused / fullscreen (SDL_GetWindowFlags)",
     f"{P}: Window::IsFocused(), IsFullscreen()", r"SDL_WINDOW_INPUT_FOCUS"),
    ("win.state", "window", "GAP", "is the window minimised, hidden, or under the mouse (SDL_GetWindowFlags)",
     "GAP: no Window::IsMinimized / IsVisible / HasMouseFocus", r"SDL_GetWindowFlags|SDL_WINDOW_(MINIMIZED|MOUSE_FOCUS)"),
    ("win.focus", "window", "GAP", "which window has keyboard or mouse focus; give a window input focus; window from an id",
     "GAP: no Backend::KeyboardFocusWindowId / MouseFocusWindowId / WindowFromId, no Window::SetInputFocus (Window::Raise gives X11 focus only)",
     r"SDL_SetWindowInputFocus|SDL_GetKeyboardFocus|SDL_GetMouseFocus|SDL_GetWindowFromID"),
    ("win.flash", "window", "HAVE", "flash the taskbar button (attention request)",
     f"{P}: Window::Flash(FlashMode::Stop / Briefly / UntilFocused). sdl2: SDL_FlashWindow; own Windows: FlashWindowEx (plan tested in test_platform_win32_pure); own Linux: nothing yet (urgency hint is on claude/platform-gaps2)", r"SDL_FlashWindow|SDL_FLASH_[A-Z_]+"),
    ("win.border", "window", "GAP", "switch the window border on or off after creation",
     "GAP: only WindowDesc::borderless at creation", r"SDL_SetWindowBordered"),
    ("win.displays", "window", "GAP", "monitor list, bounds, usable area, DPI, display modes",
     "GAP: none (Window::RefreshRateHz only). A version exists on branch claude/platform-gaps2 (DisplayInfo, Backend::DisplayCount), not merged",
     r"SDL_GetNumVideoDisplays|SDL_GetDisplayUsableBounds|SDL_GetDisplayBounds|SDL_GetDisplayName|SDL_GetDisplayDPI|SDL_GetWindowDisplayIndex|SDL_GetNumDisplayModes|SDL_GetDisplayMode|SDL_GetCurrentDisplayMode|SDL_DisplayMode"),
    ("win.excl", "window", "GAP", "exclusive fullscreen video mode (change the monitor's resolution)",
     "GAP: only desktop fullscreen", r"SDL_SetWindowDisplayMode|SDL_GetClosestDisplayMode"),
    ("win.syswm", "window", "HAVE", "native window handle and a Windows message hook (launcher tray icon)",
     f"{P}: Window::NativeHandle() gives the HWND; Backend::SetNativeMessageHook (every window message of our windows, Windows only; sdl2 and own Win32, never run on Windows)", r"SDL_GetWindowWMInfo|SDL_SetWindowsMessageHook|SDL_SysWMinfo|SDL_syswm|SDL_SYSWM_[A-Z]+|#include<SDL_syswm\.h>"),
    # ---- events ----
    ("ev.loop", "events", "HAVE", "the event loop (get the next event)",
     f"{P}: Backend::PollEvent(Event&), Event, EventType", r"SDL_Event|SDL_PollEvent"),
    ("ev.pump", "events", "HAVE", "pump events without taking them",
     f"{P}: Backend::PumpEvents() (the apps use it to keep the window alive in a wait loop)", r"SDL_PumpEvents"),
    ("ev.push", "events", "HAVE", "put an event into the queue",
     f"{P}: Backend::PushEvent(Event) (the editor injects a drop; tests inject input)", r"SDL_PushEvent"),
    ("ev.filter", "events", "GAP", "turn an event type on or off",
     "GAP: no Backend::EventState (the own backends always deliver)", r"SDL_EventState"),
    ("ev.window", "events", "HAVE", "quit, window close, resize, focus, exposed events",
     f"{P}: EventType::Quit / WindowClose / WindowResized / WindowFocusGained / WindowFocusLost / WindowExposed",
     r"SDL_QUIT|SDL_WINDOWEVENT[A-Z_]*"),
    ("ev.drop", "events", "HAVE", "file drag and drop",
     f"{P}: EventType::DropFile (sdl2; own Win32 WM_DROPFILES; own X11 XDND v5 text/uri-list, tested with a fake drag source under Xvfb). Not in the seam: DropBegin / DropComplete (nothing needs them)",
     r"SDL_DROPFILE|SDL_DROPBEGIN|SDL_DROPCOMPLETE"),
    # ---- keyboard ----
    ("kb.state", "keyboard", "HAVE", "held keys and modifiers",
     f"{P}: Backend::IsKeyDown(Key), ModState(), Mod bits", r"SDL_GetKeyboardState|SDL_NUM_SCANCODES|SDL_GetModState|KMOD_[A-Z]+"),
    ("kb.keys", "keyboard", "HAVE", "key names and codes",
     f"{P}: enum Key (USB HID numbering = SDL scancodes), Backend::KeyForChar(char)",
     r"SDL_Scancode|SDL_scancode|SDL_SCANCODE_[A-Z0-9_]+|SDLK_[A-Za-z0-9_]+|SDL_Keycode|SDL_GetScancodeFromKey"),
    ("kb.events", "keyboard", "HAVE", "key down and up events",
     f"{P}: EventType::KeyDown / KeyUp", r"SDL_KEYDOWN|SDL_KEYUP"),
    ("kb.text", "keyboard", "HAVE", "typed text",
     f"{P}: Backend::StartTextInput / StopTextInput, EventType::TextInput", r"SDL_StartTextInput|SDL_StopTextInput|SDL_TEXTINPUT"),
    ("kb.ime", "keyboard", "GAP", "IME composition text (the text still being composed)",
     "GAP: no TextEditing event (a version exists on branch claude/platform-gaps2, not merged)", r"SDL_TEXTEDITING"),
    # ---- mouse ----
    ("m.state", "mouse", "HAVE", "mouse position and buttons",
     f"{P}: Backend::MouseState(x, y), MouseMask, enum MouseButton", r"SDL_GetMouseState|SDL_BUTTON|SDL_BUTTON_[A-Z0-9]+"),
    ("m.events", "mouse", "HAVE", "mouse motion, button and wheel events",
     f"{P}: EventType::MouseMove / MouseButtonDown / MouseButtonUp / MouseWheel, Event::wheelFlipped",
     r"SDL_MOUSEMOTION|SDL_MOUSEBUTTONDOWN|SDL_MOUSEBUTTONUP|SDL_MOUSEWHEEL[A-Z_]*"),
    ("m.relative", "mouse", "HAVE", "relative mouse mode (mouse look)",
     f"{P}: Backend::SetRelativeMouse, RelativeMouseState", r"SDL_SetRelativeMouseMode|SDL_GetRelativeMouseState"),
    ("m.relget", "mouse", "GAP", "ask whether relative mouse mode is on",
     "GAP: no Backend::IsRelativeMouse getter", r"SDL_GetRelativeMouseMode"),
    ("m.cursor", "mouse", "HAVE", "show or hide the cursor, warp it",
     f"{P}: Backend::ShowCursor / IsCursorShown / WarpMouse", r"SDL_ShowCursor|SDL_WarpMouseInWindow|SDL_ENABLE|SDL_DISABLE|SDL_QUERY"),
    ("m.global", "mouse", "HAVE", "mouse position on the whole desktop",
     f"{P}: Backend::GlobalMouseState(x, y, buttons) (sdl2, own X11 and Win32)", r"SDL_GetGlobalMouseState"),
    ("m.touch", "mouse", "PARTIAL", "touch events",
     f"{P}: EventType::FingerDown / FingerMotion / FingerUp (sdl2 only; own never fires), Backend::TouchDeviceCount() (sdl2 only; own says 0)",
     r"SDL_FINGER[A-Z]+|SDL_GetNumTouchDevices"),
    # ---- gamepad ----
    ("pad", "gamepad", "HAVE", "game controllers",
     f"{P}: Backend::GamepadDeviceCount / IsGamepad / OpenGamepad, class Gamepad, EventType::GamepadAdded / GamepadRemoved",
     r"SDL_GameController[A-Za-z]*|SDL_IsGameController|SDL_NumJoysticks|SDL_JoystickInstanceID|SDL_CONTROLLERDEVICE[A-Z]+"),
    # ---- audio ----
    ("audio", "audio", "HAVE", "audio output and capture device",
     f"{P}: Backend::OpenAudio / AudioDeviceCount / AudioDeviceName, class AudioDevice, AudioSpec, SampleFormat",
     r"SDL_OpenAudioDevice|SDL_CloseAudioDevice|SDL_PauseAudioDevice|SDL_QueueAudio|SDL_DequeueAudio|SDL_GetQueuedAudioSize|SDL_ClearQueuedAudio|SDL_GetNumAudioDevices|SDL_GetAudioDeviceName|SDL_AudioSpec|SDL_AudioDeviceID|SDL_AudioCallback|SDL_AUDIO_ALLOW_[A-Z_]+"),
    # ---- timers ----
    ("timers", "timers", "HAVE", "ticks, delay, performance counter",
     f"{P}: okay::platform::TicksMs32 / TicksMs / DelayMs / PerfCounter / PerfFrequency",
     r"SDL_GetTicks|SDL_GetTicks64|SDL_Delay|SDL_GetPerformanceCounter|SDL_GetPerformanceFrequency"),
    # ---- clipboard ----
    ("clip", "clipboard", "HAVE", "clipboard text",
     f"{P}: Backend::GetClipboardText / SetClipboardText", r"SDL_GetClipboardText|SDL_SetClipboardText"),
    # ---- GL ----
    ("gl.ctx", "gl", "HAVE", "create a GL context, make current, swap, vsync, proc address",
     f"{P}: Window::CreateGLContext / DestroyGLContext / MakeCurrent / SwapBuffers / SetSwapInterval / GetDrawableSize, Backend::GLProcAddress / ReleaseCurrent, GLAttributes",
     r"SDL_GL_(MakeCurrent|SetAttribute|CreateContext|DeleteContext|GetProcAddress|SwapWindow|SetSwapInterval|GetDrawableSize|CONTEXT_[A-Z_]+|DEPTH_SIZE)|SDL_GLContext"),
    ("gl.cur", "gl", "HAVE", "which window and context are current (save and restore around other GL work)",
     f"{P}: Backend::CurrentGL() returns an opaque GLCurrent, Backend::RestoreGL(GLCurrent) puts it back (sdl2 only; own backends return an empty state)", r"SDL_GL_GetCurrentContext|SDL_GL_GetCurrentWindow"),
    # ---- renderer ----
    ("r.create", "renderer", "HAVE", "make or wrap an SDL_Renderer",
     f"{PP}: CreateSdl2Painter(Window&), WrapSdl2Renderer(void*), CreateSdl2SurfacePainter(w, h); own: OkayUI::soft::CreateWindowPainter",
     r"SDL_CreateRenderer|SDL_DestroyRenderer|SDL_Renderer|SDL_RENDERER_(ACCELERATED|PRESENTVSYNC|SOFTWARE)|SDL_CreateSoftwareRenderer"),
    ("r.draw", "renderer", "HAVE", "fill, outline, lines, copy, geometry, clip, scale, present, vsync",
     f"{PP}: Painter::Clear / SetDrawColor / FillRect(F) / DrawRect / DrawLine(F) / DrawPoint / Copy(F/Ex/ExF) / Geometry / SetClipRect / SetScale / Present / SetVSync / OutputSize / ReadPixels / SetBlendMode",
     r"SDL_Render(Clear|Present|Copy|CopyF|CopyEx|CopyExF|FillRect|FillRectF|DrawRect|DrawLine|DrawLineF|DrawPoint|Geometry|SetClipRect|GetClipRect|IsClipEnabled|SetScale|SetVSync|ReadPixels)|SDL_SetRenderDrawColor|SDL_SetRenderDrawBlendMode|SDL_GetRenderDrawBlendMode|SDL_GetRendererOutputSize|SDL_RendererFlip|SDL_FLIP_[A-Z]+"),
    ("r.tex", "renderer", "HAVE", "textures: create, update, destroy, blend, scale, colour and alpha mod, query",
     f"{PP}: Painter::CreateTexture(desc, rgba) / UpdateTexture / DestroyTexture / SetTextureBlendMode / SetTextureScaleMode / QueryTexture; ui2d::Painter colour and alpha mod. RGBA8 only",
     r"SDL_Texture|SDL_CreateTexture|SDL_UpdateTexture|SDL_DestroyTexture|SDL_QueryTexture|SDL_SetTexture[A-Za-z]+|SDL_TEXTUREACCESS_(STATIC|STREAMING)|SDL_ScaleMode[A-Za-z]*|SDL_BLENDMODE_[A-Z]+|SDL_BlendMode|SDL_PIXELFORMAT_[A-Z0-9]+"),
    ("r.types", "renderer", "HAVE", "plain data types (rect, point, colour, vertex)",
     "ui2d::Rect / FRect / Point / FPoint / RGBA / Vertex (okay/UI/UIPainter.hpp) have the same layout (static_assert in UIPainterSDL.hpp): a rename",
     r"SDL_Rect|SDL_FRect|SDL_Point|SDL_FPoint|SDL_Color|SDL_Vertex|SDL_GetRGBA"),
    ("r.surface", "renderer", "HAVE", "CPU surfaces used as a pixel buffer (create from RGBA, free, texture from surface, lock)",
     f"{PP}: CreateTexture(desc, rgba) replaces surface-to-texture (avatars, thumbnails); ReadPixels replaces readback; CreateSdl2SurfacePainter / OkayUI::soft::CreateBitmapPainter draw on the CPU (tests lock surfaces to read pixels)",
     r"SDL_Surface|SDL_CreateRGBSurface[A-Za-z]*|SDL_FreeSurface|SDL_LockSurface|SDL_UnlockSurface|SDL_CreateTextureFromSurface"),
    ("r.target", "renderer", "GAP", "render to a texture (render targets)",
     "GAP: Painter has no render targets (the software painter has none either)", r"SDL_SetRenderTarget|SDL_GetRenderTarget|SDL_TEXTUREACCESS_TARGET"),
    ("r.rare", "renderer", "GAP", "raw geometry, flush, read the draw colour, the Direct3D 11 device behind the renderer",
     "GAP: no Painter equivalents", r"SDL_RenderGeometryRaw|SDL_RenderFlush|SDL_GetRenderDrawColor|SDL_RenderGetD3D11Device"),
    ("r.reset", "renderer", "GAP", "device-lost events from the renderer",
     "GAP: no event for it", r"SDL_RENDER_DEVICE_RESET|SDL_RENDER_TARGETS_RESET"),
    ("r.hint", "renderer", "GAP", "choose the renderer driver (hint)",
     "GAP: PainterDesc has no driver choice", r"SDL_HINT_RENDER_DRIVER"),
    # ---- file ----
    ("f.wav", "file", "OTHER", "WAV loading and the audio format words around it",
     "okay::wav (engine/include/okay/Audio/WavDecoder.hpp) behind shared/WavPlayback.hpp, off by default (OKAY_OWN_WAV). Not an okay::platform job",
     r"SDL_LoadWAV|SDL_FreeWAV|SDL_AudioFormat|AUDIO_[SUF](8|16|32)(LSB|MSB|SYS)?|SDL_AUDIO_(BITSIZE|ISBIGENDIAN|ISFLOAT|ISSIGNED)|SDL_Swap16"),
    ("f.bmp", "file", "GAP", "save a surface as a BMP file",
     "GAP: none (the engine has its own PNG writer; the one caller is the editor's --vs-shot tool)", r"SDL_SaveBMP"),
    # ---- message box ----
    ("msg", "msgbox", "PARTIAL", "message box",
     f"{P}: Backend::ShowErrorBox (error icon; sdl2 and Win32 real, own Linux logs only). GAP: warning and information icons",
     r"SDL_ShowSimpleMessageBox|SDL_MESSAGEBOX_[A-Z]+"),
    # ---- runtime ----
    ("x.init", "runtime", "HAVE", "start and stop SDL, init flags, SDL_MAIN_HANDLED",
     f"{P}: Backend::Init(InitFlags) / Shutdown (SetMainReady inside)",
     r"SDL_Init|SDL_InitSubSystem|SDL_QuitSubSystem|SDL_Quit|SDL_WasInit|SDL_INIT_[A-Z]+|SDL_MAIN_HANDLED|SDL_SetMainReady"),
    ("x.paths", "runtime", "HAVE", "base path, per-user folder, preferred locales",
     f"{P}: Backend::BasePath / PrefPath / PreferredLocales", r"SDL_GetBasePath|SDL_GetPrefPath|SDL_GetPreferredLocales|SDL_Locale"),
    ("x.log", "runtime", "HAVE", "log line, last error",
     f"{P}: Backend::Log / LastError, okay::platform::Log", r"SDL_Log|SDL_GetError"),
    ("x.hint", "runtime", "OTHER", "SDL hints other than DPI awareness and the renderer driver",
     "none needed: hints configure SDL itself and go away with it", r"SDL_SetHint|SDL_HINT_[A-Z_0-9]+"),
    ("x.version", "runtime", "OTHER", "SDL version checks",
     "none needed: they go away with SDL", r"SDL_VERSION|SDL_VERSION_ATLEAST|SDL_MAJOR_VERSION|SDL_MINOR_VERSION|SDL_PATCHLEVEL|SDL_COMPILEDVERSION"),
    # ---- threads ----
    ("t.thread", "threads", "OTHER", "threads (create, detach, wait)",
     "Not a platform-layer job and NOT std::thread: rule 21a (MinGW win32 thread model cannot compile <thread>). Needs our own thread wrapper on pthread / Win32 CreateThread",
     r"SDL_Thread|SDL_CreateThread|SDL_DetachThread|SDL_WaitThread"),
    ("t.mutex", "threads", "OTHER", "mutexes",
     "Not a platform-layer job and NOT std::mutex (rule 21a). SDL mutexes are recursive, so the replacement must be recursive or every lock site audited. debugutil::SpinLock exists but spins",
     r"SDL_mutex|SDL_CreateMutex|SDL_LockMutex|SDL_UnlockMutex|SDL_DestroyMutex"),
    ("t.atomic", "threads", "OTHER", "atomics",
     "std::atomic (<atomic> does not need the thread model); mechanical", r"SDL_atomic_t|SDL_Atomic[A-Za-z]*"),
    # ---- imgui ----
    ("i.platform", "imgui", "OTHER", "Dear ImGui SDL platform backend (events, cursor, clipboard, window)",
     "Needs our own ImGui platform backend on okay::platform (docs/sdl_removal_plan.md step 4), or the OkayUI port",
     r"ImGui_ImplSDL2_[A-Za-z0-9_]+|#include<[^>]*imgui_impl_sdl2\.h>"),
    ("i.render", "imgui", "OTHER", "Dear ImGui SDL_Renderer backend (draws ImGui through SDL_Renderer)",
     "Needs our own ImGui renderer backend on okay::platform::Painter or GL (docs/sdl_removal_plan.md step 3), or the OkayUI port",
     r"ImGui_ImplSDLRenderer2_[A-Za-z0-9_]+|#include<[^>]*imgui_impl_sdlrenderer2\.h>"),
    # ---- misc ----
    ("z.typedef", "misc", "OTHER", "SDL integer typedefs (Uint8, Sint16, ...)",
     "<cstdint> (mechanical rename, no run-time dependency)", r"Uint(8|16|32|64)|Sint(8|16|32|64)"),
    ("z.bool", "misc", "OTHER", "SDL_bool and SDL_TRUE / SDL_FALSE",
     "bool (mechanical)", r"SDL_bool|SDL_TRUE|SDL_FALSE"),
    ("z.libc", "misc", "OTHER", "SDL's memory and string helpers",
     "libc: free, strdup, memset (mechanical)", r"SDL_free|SDL_strdup|SDL_strlcat|SDL_zero|SDL_memset"),
    ("z.math", "misc", "OTHER", "SDL's math functions",
     "<cmath>: std::sin, cos, sqrt, floor (mechanical; SDL calls the C library's)", r"SDL_(sinf|cosf|sqrtf|floorf)"),
    ("z.include", "misc", "OTHER", "#include of an SDL header",
     "goes away with the last SDL word in the file", r"#include<[^>]*>"),
]
CAP_BY_ID = {c[0]: c for c in CAPS}
CAP_RX = [(c[0], re.compile(r"(?:" + c[5] + r")$")) for c in CAPS]
UNKNOWN = ("zz.unclassified", "misc")

# The HAVE window capabilities whose call sites run on a raw SDL_Window* and so wait for win.adopt.
ADOPT_CAPS = ("win.geometry", "win.visibility", "win.state.have", "win.state", "win.refresh", "win.icon", "win.fullscreen", "win.flash")
WIN_FLAG_GAP = {"SDL_WINDOW_MINIMIZED", "SDL_WINDOW_HIDDEN", "SDL_WINDOW_MOUSE_FOCUS", "SDL_WINDOW_MAXIMIZED",
                "SDL_WINDOW_SHOWN"}
WIN_FLAG_HAVE = {"SDL_WINDOW_INPUT_FOCUS", "SDL_WINDOW_FULLSCREEN", "SDL_WINDOW_FULLSCREEN_DESKTOP"}


def classify(sym):
    for cid, rx in CAP_RX:
        if rx.match(sym):
            return cid
    return "zz.unclassified"


def family_of(cid):
    return CAP_BY_ID[cid][1] if cid in CAP_BY_ID else "misc"


# ---------------------------------------------------------------------------------------------------------------
# Reading the repository
# ---------------------------------------------------------------------------------------------------------------
def git(*args, check=True):
    r = subprocess.run(["git", "-C", ROOT, *args], capture_output=True)
    if check and r.returncode != 0:
        sys.exit("git %s failed: %s" % (" ".join(args), r.stderr.decode("utf-8", "replace")))
    return r.stdout


def split_code(src):
    """Return (no_comments, code): comments removed; and also string and character literals blanked.
    Newlines are kept so line numbers stay right."""
    nc, cd = [], []
    i, n = 0, len(src)

    def both(s):
        nc.append(s)
        cd.append(s)
    while i < n:
        c = src[i]
        nx = src[i + 1] if i + 1 < n else ""
        if c == "/" and nx == "/":
            while i < n and src[i] != "\n":
                i += 1
        elif c == "/" and nx == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            both("\n" * src.count("\n", i, j))
            i = j
        elif c == "R" and nx == '"' and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == "_")):
            m = re.match(r'R"([^(\s]{0,16})\(', src[i:])
            if not m:
                both(c)
                i += 1
                continue
            end = ")" + m.group(1) + '"'
            j = src.find(end, i + m.end())
            j = n if j < 0 else j + len(end)
            nc.append(src[i:j])
            cd.append('""' + "\n" * src.count("\n", i, j))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == "\\":
                    j += 1
                if j < n and src[j] == "\n":
                    break
                j += 1
            j = min(j + 1, n)
            nc.append(src[i:j])
            cd.append(c + c)
            i = j
        else:
            both(c)
            i += 1
    return "".join(nc), "".join(cd)


class FileInfo:
    def __init__(self, path):
        self.path = path
        self.counts = collections.Counter()      # (capability, 'call'|'ref') -> n
        self.syms = collections.defaultdict(collections.Counter)   # capability -> symbol -> n
        self.includes = []                        # include strings (all, for reach)
        self.reach = set()                        # programs that compile or include this file

    @property
    def calls(self):
        return sum(v for (c, k), v in self.counts.items() if k == "call")

    @property
    def refs(self):
        return sum(v for (c, k), v in self.counts.items())

    def fam(self, fam):
        calls = refs = 0
        for (cid, k), v in self.counts.items():
            if family_of(cid) == fam:
                refs += v
                if k == "call":
                    calls += v
        return calls, refs


NO_SDL_MODE = False


def resolve_no_sdl(src):
    """The source as a -DOKAY_NO_SDL build sees it: #ifndef OKAY_NO_SDL / #if !defined(OKAY_NO_SDL) keep only the #else part,
    #ifdef OKAY_NO_SDL / #if defined(OKAY_NO_SDL) keep only the first part. Other #if lines are left alone (their nesting is tracked)."""
    out, stack = [], []   # stack of [kind, keeping]: kind "no" = a OKAY_NO_SDL conditional, "x" = any other
    def keeping():
        return all(k for _, k in stack)
    for line in src.split("\n"):
        t = line.strip()
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b\s*(.*)", t)
        if m:
            d, rest = m.group(1), m.group(2)
            if d in ("if", "ifdef", "ifndef"):
                neg = d == "ifndef" or bool(re.match(r"!\s*defined", rest))
                if "OKAY_NO_SDL" in rest and not re.search(r"&&|\|\|", rest):
                    stack.append(["no", not neg])
                else:
                    stack.append(["x", True])
                out.append("")
                continue
            if d == "else" and stack:
                if stack[-1][0] == "no":
                    stack[-1][1] = not stack[-1][1]
                out.append("")
                continue
            if d == "elif" and stack:
                out.append("")
                continue
            if d == "endif" and stack:
                stack.pop()
                out.append("")
                continue
        out.append(line if keeping() else "")
    return "\n".join(out)


def scan_file(ref, path):
    src = git("show", "%s:%s" % (ref, path)).decode("utf-8", "replace")
    if NO_SDL_MODE:
        src = resolve_no_sdl(src)
    nocomment, code = split_code(src)
    code = re.sub(r"^[ \t]*#[ \t]*include[^\n]*", "", code, flags=re.M)
    fi = FileInfo(path)
    for m in INCLUDE.finditer(nocomment):
        inc = m.group(1)
        base = inc.rsplit("/", 1)[-1]
        if re.match(r"SDL\w*\.h$", base) or re.match(r"imgui_impl_sdl\w*\.h$", base):
            sym = "#include<%s>" % inc
            cid = classify(sym)
            fi.counts[(cid, "ref")] += 1
            fi.syms[cid][sym] += 1
    for m in TOKEN.finditer(code):
        sym = m.group(1)
        if sym in NOT_SDL:
            continue
        is_call = bool(m.group(2))
        cid = classify(sym)
        if sym == "SDL_GetWindowFlags" and is_call:
            cid = flags_capability(code, m.end())
        elif sym == "SDL_SetHint" and is_call:
            cid = hint_capability(code, m.end())
        kind = "call" if is_call else "ref"
        fi.counts[(cid, kind)] += 1
        fi.syms[cid][sym] += 1
    return fi


def flags_capability(code, pos):
    """SDL_GetWindowFlags(...): decide by the SDL_WINDOW_* words in this statement and the next one."""
    end = pos
    for _ in range(2):
        j = code.find(";", end)
        if j < 0:
            break
        end = j + 1
    words = set(re.findall(r"SDL_WINDOW_[A-Z_]+", code[pos:end]))
    if words & WIN_FLAG_GAP:
        return "win.state"
    if words & WIN_FLAG_HAVE:
        return "win.state.have"
    return "win.state"


def hint_capability(code, pos):
    j = code.find(";", pos)
    stmt = code[pos:j if j > 0 else pos + 200]
    if "SDL_HINT_WINDOWS_DPI_AWARENESS" in stmt:
        return "win.dpi"
    if "SDL_HINT_RENDER_DRIVER" in stmt:
        return "r.hint"
    return "x.hint"


def list_sdl_files(ref):
    out = git("grep", "-l", "-I", "-E", GREP_PATTERN, ref, "--", ".", *[":!%s" % d for d in SKIP_TOP], ":!*.md",
              ":!*.txt", ":!*.cmake", ":!*.yml", ":!*.sh", ":!*.py", ":!*.json", check=False).decode()
    files = []
    for line in out.split("\n"):
        if not line:
            continue
        p = line.split(":", 1)[1] if ":" in line else line
        if p.endswith(CODE_EXT):
            files.append(p)
    return sorted(set(files))


def group_of(path):
    top = path.split("/")[0]
    if path in SEAM_BACKEND:
        return "seam"
    if "/tests/" in path or top == "tests":
        return "tests"
    if top in ("launcher", "editor", "player", "shared", "engine", "okayui"):
        return top
    return "other:" + top


def build_reach(ref, infos):
    """Which programs compile or include each SDL-bearing file. A program is the .cpp files of its directory that its
    CMakeLists names, plus engine/src and okayui/src (libraries all three link), followed through #include."""
    all_inc = git("grep", "-n", "-I", "-E", r"^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]", ref, "--",
                  "launcher", "editor", "player", "shared", "engine", "okayui", check=False).decode()
    inc_of = collections.defaultdict(list)
    paths = set()
    rx = re.compile(r'^[^:]+:([^:]+):\d+:[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]')
    for line in all_inc.split("\n"):
        m = rx.match(line)
        if m and m.group(1).endswith(CODE_EXT):
            inc_of[m.group(1)].append(m.group(2))
    tree = git("ls-tree", "-r", "--name-only", ref).decode().split("\n")
    for p in tree:
        if p.endswith(CODE_EXT):
            paths.add(p)
    by_base = collections.defaultdict(list)
    for p in paths:
        by_base[os.path.basename(p)].append(p)

    def resolve(includer, inc):
        cands = [p for p in by_base.get(os.path.basename(inc), []) if p == inc or p.endswith("/" + inc)]
        if not cands:
            return []
        d = os.path.dirname(includer)
        same = [p for p in cands if os.path.dirname(p) == d]
        if same:
            return same
        pref = [p for p in cands if p.startswith(("shared/", "engine/include/", "okayui/include/", "launcher/", "editor/", "player/"))]
        return pref or cands

    def cmake_text(d):
        return git("show", "%s:%s/CMakeLists.txt" % (ref, d), check=False).decode("utf-8", "replace")
    roots = {"launcher": [], "editor": [], "player": []}
    for prog, d in (("launcher", "launcher"), ("editor", "editor"), ("player", "player")):
        txt = cmake_text(d)
        for p in sorted(paths):
            if p.startswith(d + "/") and p.endswith((".cpp", ".cc", ".c")) and "/" not in p[len(d) + 1:]:
                if os.path.basename(p) in txt:
                    roots[prog].append(p)
        # sources a program names from other directories (shared/Foo.cpp, engine/src/Bar.cpp)
        for m in re.finditer(r"\$\{CMAKE_SOURCE_DIR\}/((?:shared|engine/src|launcher|okayui)/[A-Za-z0-9_./-]+\.(?:cpp|cc|c))", txt):
            if m.group(1) in paths:
                roots[prog].append(m.group(1))
        for p in paths:
            if p.startswith(("engine/src/", "okayui/src/")) and p.endswith((".cpp", ".cc", ".c")):
                roots[prog].append(p)
    reach = {}
    for prog, rs in roots.items():
        seen = set()
        stack = list(rs)
        while stack:
            p = stack.pop()
            if p in seen:
                continue
            seen.add(p)
            for inc in inc_of.get(p, []):
                stack.extend(resolve(p, inc))
        reach[prog] = seen
    for p, fi in infos.items():
        fi.reach = {prog for prog, s in reach.items() if p in s}


# ---------------------------------------------------------------------------------------------------------------
# Writing the page
# ---------------------------------------------------------------------------------------------------------------
GROUPS = ["launcher", "editor", "player", "shared", "engine", "okayui", "tests", "seam"]
GROUP_LABEL = {
    "launcher": "launcher/", "editor": "editor/", "player": "player/", "shared": "shared/",
    "engine": "engine/ (not the SDL backend)", "okayui": "okayui/ (not tests)", "tests": "tests", "seam": "sdl2 backend",
}


def cell(c, r):
    return "-" if r == 0 else ("%d / %d" % (c, r))


def table(headers, rows, align=None):
    out = ["| " + " | ".join(headers) + " |"]
    out.append("|" + "|".join(("---:" if (align and align[i] == "r") else "---") for i in range(len(headers))) + "|")
    for r in rows:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    return "\n".join(out)


def md_escape(s):
    return s.replace("|", "\\|")


def render(ref, sha, infos, build_files, comment_only, commit_date):
    groups = collections.defaultdict(list)
    for p, fi in infos.items():
        groups[group_of(p)].append(fi)

    def gsum(g, fam=None, cap=None):
        c = r = 0
        for fi in groups.get(g, []):
            for (cid, k), v in fi.counts.items():
                if fam and family_of(cid) != fam:
                    continue
                if cap and cid != cap:
                    continue
                r += v
                if k == "call":
                    c += v
        return c, r

    def prog_sum(prog, fam=None, cap=None, own_only=False, exclude_threads=False):
        c = r = 0
        for p, fi in infos.items():
            g = group_of(p)
            if g in ("tests", "seam") or g.startswith("other:"):
                continue
            if own_only:
                if g != prog:
                    continue
            elif g in ("launcher", "editor", "player"):
                if g != prog:
                    continue
            elif prog not in fi.reach:
                continue
            for (cid, k), v in fi.counts.items():
                if fam and family_of(cid) != fam:
                    continue
                if cap and cid != cap:
                    continue
                if exclude_threads and family_of(cid) in ("threads",):
                    continue
                r += v
                if k == "call":
                    c += v
        return c, r

    L = []
    w = L.append
    nfiles = len(infos)
    total_c = sum(fi.calls for fi in infos.values())
    total_r = sum(fi.refs for fi in infos.values())
    w("# SDL2 inventory: what still calls SDL, where, and what covers it")
    w("")
    w("Generated by `python3 scripts/sdl_inventory.py --ref %s --out docs/sdl_inventory.md` from commit `%s` (%s). Do not edit by hand:" % (ref, sha[:8], commit_date))
    w("run the script again after moving call sites (a plain-language list of what to do next is in `docs/sdl_removal_plan.md`).")
    w("")
    w("This page makes the remaining SDL2 dependency visible. `docs/platform_layer.md` is the design and the audit history of the seam;")
    w("this page is the current count, file by file, taken straight from the source.")
    w("")
    w("## How it was counted")
    w("")
    w("* Source: every `.cpp .hpp .h .cc .inl` file of the commit above that has an SDL word in code (%d files; %d more mention SDL only in comments,"
      % (nfiles, len(comment_only)))
    w("  see section 7). Docs, scripts and build files are not counted as code; build and CI files that name SDL2 are listed in section 6.")
    w("* Comments and string literals are removed first, so a word in a comment is not a use.")
    w("* **call** = an SDL name followed by `(`: a function call or a function-like macro. **ref** = every SDL word, calls included, so it also")
    w("  counts types (`SDL_Rect`), constants (`SDL_WINDOW_RESIZABLE`), event names (`SDL_KEYDOWN`), the integer typedefs (`Uint8`) and `#include`")
    w("  lines. A cell shows `calls / refs`. Calls are the numbers to read as \"call sites\"; refs show how much code mentions SDL at all.")
    w("* Words counted: `SDL_*`, `SDLK_*`, `KMOD_*`, the `AUDIO_*` format words, `Uint8..Sint64`, `ImGui_ImplSDL2_*`, `ImGui_ImplSDLRenderer2_*`.")
    w("  `SDL_DelayCompat` (a local helper in `engine/src/Net/PhotonTransport.cpp`) is not SDL and is skipped.")
    w("* All `#if` branches count (Windows-only and Emscripten-only code too). A macro that wraps an SDL call counts once, where it is written.")
    w("* **sdl2 backend** = `SdlBackend.cpp`, `SdlPainter.cpp` and `UIPainterSDL.hpp`: SDL on purpose, deleted together with SDL2. They are shown")
    w("  apart and are not part of \"what the apps still call\".")
    w("* \"Used by\" is by `#include` and CMake: a program's `.cpp` files (the ones its `CMakeLists.txt` names) plus `engine/src` and `okayui/src`,")
    w("  followed through `#include`. It over-approximates a little (one header name can resolve to more than one file).")
    w("* Not counted: Dear ImGui's own SDL backend source (it is fetched at configure time, not in this repository), SDL's own headers, the")
    w("  committed binaries `dist/SDL2.dll` and `dist/*.exe`, and anything reached only through a macro.")
    w("")

    # ---------------- headline
    w("## 1. Headline numbers")
    w("")
    w("What each program still takes from SDL2 (`calls / refs`; sdl2 backend and tests not included):")
    w("")
    rows = []
    for prog in ("launcher", "editor", "player"):
        oc, orf = prog_sum(prog, own_only=True)
        tc, tr = prog_sum(prog)
        thc, thr = prog_sum(prog, fam="threads", own_only=False)
        rc, rr = prog_sum(prog, fam="renderer")
        ic, ir = prog_sum(prog, fam="imgui")
        pc = tc - thc - rc - ic
        pr = tr - thr - rr - ir
        rows.append(["**%s**" % prog, cell(oc, orf), cell(tc, tr), cell(thc, thr), cell(rc, rr), cell(ic, ir), cell(pc, pr)])
    w(table(["program", "its own directory", "everything it reaches", "of that: threads / mutexes / atomics", "of that: SDL_Renderer 2D",
             "of that: Dear ImGui SDL backends", "of that: the rest (window, input, audio, GL, clipboard, ...)"], rows))
    w("")
    w("\"Everything it reaches\" adds the `shared/`, `engine/` and `okayui/` code the program compiles or includes. The three programs share")
    w("those files, so the rows do not add up to the grand total.")
    w("")
    gc, gr = 0, 0
    rows = []
    for g in GROUPS:
        c, r = gsum(g)
        nf = len(groups.get(g, []))
        gc += c
        gr += r
        rows.append([GROUP_LABEL[g], nf, cell(c, r)])
    for g in sorted(k for k in groups if k.startswith("other:")):
        c, r = gsum(g)
        gc += c
        gr += r
        rows.append([g[6:] + "/", len(groups[g]), cell(c, r)])
    rows.append(["**all**", nfiles, "**%d / %d**" % (gc, gr)])
    w(table(["where", "files with SDL words", "calls / refs"], rows, "lrr"))
    w("")
    app_c = sum(gsum(g)[0] for g in ("launcher", "editor", "player", "shared", "engine", "okayui"))
    app_r = sum(gsum(g)[1] for g in ("launcher", "editor", "player", "shared", "engine", "okayui"))
    w("Without tests and the sdl2 backend, **%d call sites and %d SDL words** are left in the programs and their shared code." % (app_c, app_r))
    # observations computed from the data
    lc, lr = prog_sum("launcher", own_only=True)
    lth, _ = prog_sum("launcher", fam="threads", own_only=True)
    lrn, _ = prog_sum("launcher", fam="renderer", own_only=True)
    li, _ = prog_sum("launcher", fam="imgui", own_only=True)
    pc_, pr_ = prog_sum("player", own_only=True)
    ec, er = prog_sum("editor", own_only=True)
    ern, _ = prog_sum("editor", fam="renderer", own_only=True)
    w("")
    w("Reading the numbers:")
    w("")
    w("* **Launcher: %d of its %d calls (%d%%) are threads, mutexes and atomics**, not platform work, and %d are `SDL_Renderer` drawing. Only %d are"
      % (lth, lc, round(100.0 * lth / lc) if lc else 0, lrn, lc - lth - lrn - li))
    w("  what `okay::platform` is about (window, input, audio, clipboard) or small helpers (WAV, libc, version checks).")
    w("* **Editor: %d calls**, %d of them `SDL_Renderer` drawing. The editor's window, GL context and events are the platform part." % (ec, ern))
    pty, _ = prog_sum("player", cap="z.typedef", own_only=True)
    ptyr = prog_sum("player", cap="z.typedef", own_only=True)[1]
    prty = prog_sum("player", cap="r.types", own_only=True)[1]
    w("* **Player: %d calls and %d SDL words** are left in `player/main.cpp`: %d are the integer typedefs (`Uint8`, `Uint32`) and %d the plain data types"
      " `SDL_Rect`, `SDL_Color` and `SDL_Vertex` (the player moved onto the seam in stages 2 and 3)." % (pc_, pr_, ptyr, prty))
    w("")

    # ---------------- families
    w("## 2. Kinds of API (families) against the directories")
    w("")
    w("`calls / refs`. The first three columns are the three programs' own directories.")
    w("")
    hdr = ["family"] + [GROUP_LABEL[g] for g in GROUPS]
    rows = []
    for fam, label in FAMILIES:
        row = [label]
        tot = 0
        for g in GROUPS:
            c, r = gsum(g, fam=fam)
            tot += r
            row.append(cell(c, r))
        if tot:
            rows.append(row)
    w(table(hdr, rows))
    w("")

    # ---------------- capabilities
    w("## 3. Every capability, with its coverage")
    w("")
    w("One row per thing SDL is used for. **HAVE** = `okay::platform` has a function for it and the call sites in the apps are still plain SDL")
    w("(moving them is mechanical, but see the window caveat below). **PARTIAL** = part is covered. **GAP** = nothing in `okay::platform` yet.")
    w("**OTHER** = not a platform-layer job; it needs another replacement (named in the row). Counts are `calls / refs`; the last column of numbers")
    w("is `tests` (not counted in the headline).")
    w("")
    w("`Platform.hpp` = `engine/include/okay/Platform/Platform.hpp`, `Painter.hpp` = `engine/include/okay/Platform/Painter.hpp`.")
    w("")
    hdr = ["capability", "family", "status", "launcher", "editor", "player", "shared + engine + okayui", "tests", "covered by (or what is missing)"]
    rows = []
    for cid, fam, status, desc, cover, rx in CAPS:
        cells = []
        total = 0
        for g in ("launcher", "editor", "player"):
            c, r = gsum(g, cap=cid)
            total += r
            cells.append(cell(c, r))
        c = r = 0
        for g in ("shared", "engine", "okayui"):
            gc2, gr2 = gsum(g, cap=cid)
            c += gc2
            r += gr2
        total += r
        cells.append(cell(c, r))
        tc, tr = gsum("tests", cap=cid)
        cells.append(cell(tc, tr))
        sc, sr = gsum("seam", cap=cid)
        if total == 0 and tr == 0 and sr == 0:
            continue
        rows.append(["%s: %s" % (cid, desc), FAMILY_LABEL[fam].split(" (")[0], "**%s**" % status] + cells + [md_escape(cover)])
    w(table(hdr, rows))
    w("")
    unc = collections.Counter()
    for fi in infos.values():
        for sym, n in fi.syms.get("zz.unclassified", {}).items():
            unc[sym] += n
    if unc:
        w("**Not classified (add them to `CAPS` in the script):** " + ", ".join("`%s` x%d" % kv for kv in sorted(unc.items())))
    else:
        w("Every SDL word found is classified (the script lists any it cannot place here).")
    w("")
    w("**The window caveat.** The launcher and the editor create their windows with `SDL_CreateWindow` and keep a raw `SDL_Window*`. Every `Window::`")
    w("method in `okay::platform` needs an `okay::platform::Window` object. `okay::platform::WrapSdl2Window(void*)` gives a non-owning one over the app's")
    w("own SDL window, so call sites move one by one (the launcher through its `lw::` helpers, the editor through `winSeam`) while SDL still creates the window.")
    w("Window calls that still show SDL words in the table are the ones with no `Window` method yet (see the GAP rows).")
    w("")

    # ---------------- ranking
    def cap_stats(cid):
        """(calls, refs, who, per-program (calls, refs)) for one capability over the programs' code and shared code."""
        if cid == "win.adopt":
            ids = ADOPT_CAPS
        else:
            ids = (cid,)
        per = {prog: (sum(prog_sum(prog, cap=i)[0] for i in ids), sum(prog_sum(prog, cap=i)[1] for i in ids))
               for prog in ("launcher", "editor", "player")}
        calls = refs = 0
        for p, fi in infos.items():
            g = group_of(p)
            if g in ("tests", "seam") or g.startswith("other:"):
                continue
            for (c2, k), v in fi.counts.items():
                if c2 in ids:
                    refs += v
                    if k == "call":
                        calls += v
        who = "".join(x[0].upper() for x in ("launcher", "editor", "player") if per[x][1] > 0)
        return calls, refs, who, per

    def lep(per):
        return "%d / %d / %d" % (per["launcher"][0], per["editor"][0], per["player"][0])

    by_status = collections.OrderedDict((k, [0, 0]) for k in ("HAVE", "PARTIAL", "GAP", "OTHER"))
    by_status_cap = collections.defaultdict(list)
    for cid, fam, status, desc, cover, rx in CAPS:
        if cid == "win.adopt":
            continue
        calls, refs, who, per = cap_stats(cid)
        by_status[status][0] += calls
        by_status[status][1] += refs
        by_status_cap[status].append((calls, cid))
    w("## 4. Ranking")
    w("")
    w("Counted over the launcher, editor, player and the shared code they use (not tests, not the sdl2 backend), `calls / refs`:")
    w("")
    rows = []
    meaning = {"HAVE": "okay::platform has it; the apps still call SDL (a mechanical move)",
               "PARTIAL": "part is covered, part is a GAP",
               "GAP": "no function in okay::platform yet",
               "OTHER": "not a platform-layer job (threads, ImGui backends, typedefs, WAV, ...)"}
    for st, (c, r) in by_status.items():
        rows.append(["**%s**" % st, "%d / %d" % (c, r), meaning[st]])
    w(table(["status", "call sites / refs", "meaning"], rows, "lrl"))
    w("")
    thr_c = sum(prog_sum("launcher", fam="threads")[0:1])
    w("So the missing platform functions are a **small** part: the GAP rows are about %d call sites, against about %d that `okay::platform` already covers and"
      % (by_status["GAP"][0] + by_status["PARTIAL"][0], by_status["HAVE"][0]))
    w("that only need moving, and %d that are not platform work at all (%d of those are the launcher's threads, mutexes and atomics)."
      % (by_status["OTHER"][0], thr_c))
    w("")
    w("**Unmerged work that already covers some of these GAPs.** The branch `origin/claude/platform-gaps2` (tip `33da9e1d`, 2026-11-14, no pull request;")
    w("it contains `origin/claude/platform-gaps`) holds about 6,500 added lines of seam work (code, tests and docs) that is **not on main**: X11 drag-and-drop receive (Xdnd), RandR")
    w("monitors and `Xft.dpi`, cursor shapes, the urgency hint (taskbar flash), a message box, input-method preedit, Win32 clipboard formats, an OLE drop")
    w("target, `FlashWindowEx`, IME composition events, `DisplayInfo`/`DisplayCount`, `RequestAttention`, `SetCursor`, a pad mapping table, and")
    w("`docs/own_platform_status.md`. A dry-run merge (`git merge-tree`) into this commit conflicts only in the generated")
    w("`docs/feature_map/_index_az.md`; it was **not built or run here**. Rows below say \"a version exists on branch ...\" where it applies; this page")
    w("does not count that work as done.")
    w("")
    w("### 4a. GAPs in `okay::platform` itself, by call sites they block")
    w("")
    w("L / E / P = the programs that still have the call (shared and engine files count for every program that includes them). \"Call sites\" is the")
    w("ranking number; \"refs\" breaks ties.")
    w("")
    gap_rows = []
    for cid, fam, status, desc, cover, rx in CAPS:
        if status not in ("GAP", "PARTIAL"):
            continue
        calls, refs, who, per = cap_stats(cid)
        if refs == 0 and cid != "win.adopt":
            continue
        gap_rows.append((calls, refs, cid, status, desc, who, per, cover))
    gap_rows.sort(key=lambda t: (-t[0], -t[1], t[2]))
    rows = []
    for i, (calls, refs, cid, status, desc, who, per, cover) in enumerate(gap_rows, 1):
        rows.append([i, "%s: %s" % (cid, desc), status, calls, refs, who, lep(per), md_escape(cover)])
    w(table(["#", "GAP", "status", "call sites", "refs", "blocks", "calls L / E / P", "what exists / what is missing"], rows,
            "lllrrlll"))
    w("")
    w("### 4b. HAVE in `okay::platform`, but the apps still call SDL (mechanical moves)")
    w("")
    w("These need no new platform code, only moving call sites (the window rows can be moved now through `WrapSdl2Window`).")
    w("")
    have_rows = []
    for cid, fam, status, desc, cover, rx in CAPS:
        if status != "HAVE" or cid == "win.adopt":
            continue
        calls, refs, who, per = cap_stats(cid)
        if refs == 0:
            continue
        have_rows.append((calls, refs, cid, desc, who, per))
    have_rows.sort(key=lambda t: (-t[0], -t[1], t[2]))
    rows = [[i, "%s: %s" % (cid, desc), calls, refs, who, lep(per)] for i, (calls, refs, cid, desc, who, per) in enumerate(have_rows, 1)]
    w(table(["#", "capability", "call sites", "refs", "in", "calls L / E / P"], rows, "llrrll"))
    w("")
    w("### 4c. Not platform-layer work (another replacement is needed)")
    w("")
    other_rows = []
    for cid, fam, status, desc, cover, rx in CAPS:
        if status != "OTHER":
            continue
        calls, refs, who, per = cap_stats(cid)
        if refs == 0:
            continue
        other_rows.append((calls, refs, cid, desc, who, per, cover))
    other_rows.sort(key=lambda t: (-t[0], -t[1], t[2]))
    rows = [[i, "%s: %s" % (cid, desc), calls, refs, who, lep(per), md_escape(cover)]
            for i, (calls, refs, cid, desc, who, per, cover) in enumerate(other_rows, 1)]
    w(table(["#", "what", "call sites", "refs", "in", "calls L / E / P", "replacement"], rows, "llrrlll"))
    w("")

    # ---------------- per file
    w("## 5. Every file")
    w("")
    w("Sorted by directory, then by calls. `Used by`: L launcher, E editor, P player (from `#include` and CMake). The family column lists the")
    w("families with their calls / refs, largest first (`threads`, `renderer`, `window`, ... as in section 2).")
    w("")
    short = {f: f for f, _ in FAMILIES}
    order = ["launcher", "editor", "player", "shared", "engine", "okayui", "seam", "tests"] + sorted(k for k in groups if k.startswith("other:"))
    for g in order:
        fis = groups.get(g, [])
        if not fis:
            continue
        gc3, gr3 = gsum(g)
        w("### %s (%d files, %d calls / %d refs)" % (GROUP_LABEL.get(g, g[6:] + "/"), len(fis), gc3, gr3))
        w("")
        rows = []
        for fi in sorted(fis, key=lambda f: (-f.calls, -f.refs, f.path)):
            fams = []
            for fam, _ in FAMILIES:
                c, r = fi.fam(fam)
                if r:
                    fams.append((r, c, fam))
            fams.sort(key=lambda t: (-t[0], t[2]))
            used = "".join(x[0].upper() for x in ("launcher", "editor", "player") if x in fi.reach) or "-"
            rows.append(["`%s`" % fi.path, used, fi.calls, fi.refs, ", ".join("%s %d/%d" % (short[f], c, r) for r, c, f in fams)])
        w(table(["file", "used by", "calls", "refs", "families (calls/refs)"], rows, "llrrl"))
        w("")

    # ---------------- build files
    w("## 6. Build, CI and packaging files that name SDL2")
    w("")
    w("Not counted above. Removing SDL2 also means editing these (the last column is the number of lines that mention SDL).")
    w("")
    rows = [["`%s`" % p, n] for p, n in build_files]
    w(table(["file", "lines naming SDL"], rows, "lr"))
    w("")
    w("Committed binaries that carry SDL2: `dist/SDL2.dll`, and the three `dist/*.exe` programs link it.")
    w("")
    w("## 7. Files that mention SDL only in comments")
    w("")
    w("They have no SDL word in code (so they are not in the counts), but their comments name SDL and should be reworded when SDL goes: " +
      ", ".join("`%s`" % p for p in comment_only) + ".")
    w("")
    return "\n".join(L) + "\n"


def build_file_counts(ref):
    out = git("grep", "-c", "-i", "-E", r"sdl", ref, "--", "CMakeLists.txt", "*CMakeLists.txt", "*.cmake", ".github", "scripts", "deploy",
              "steam", "tools", "relay", "sandbox", "examples", check=False).decode()
    res = []
    for line in out.split("\n"):
        if not line:
            continue
        m = re.match(r"^[^:]+:(.+):(\d+)$", line)
        if not m:
            continue
        path, n = m.group(1), int(m.group(2))
        if path.endswith("sdl_inventory.py"):
            continue
        res.append((path, n))
    return sorted(set(res), key=lambda t: (-t[1], t[0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", default="HEAD", help="git ref to read (a commit sha is best: it keeps the page reproducible)")
    ap.add_argument("--out", help="write the page here")
    ap.add_argument("--no-sdl", action="store_true", help="count the source as a -DOKAY_NO_SDL build sees it (OKAY_NO_SDL branches resolved)")
    ap.add_argument("--check", help="compare the result with this file; exit 1 when it differs")
    args = ap.parse_args()
    global NO_SDL_MODE
    NO_SDL_MODE = args.no_sdl
    sha = git("rev-parse", "--verify", args.ref + "^{commit}").decode().strip()
    files = list_sdl_files(sha)
    scanned = {p: scan_file(sha, p) for p in files}
    infos = {p: fi for p, fi in scanned.items() if fi.refs > 0}
    comment_only = sorted(p for p, fi in scanned.items() if fi.refs == 0)
    build_reach(sha, infos)
    page = render(args.ref, sha, infos, build_file_counts(sha), comment_only, git("show", "-s", "--format=%cs", sha).decode().strip())
    if args.check:
        with open(args.check, encoding="utf-8") as f:
            have = f.read()
        if have != page:
            sys.stderr.write("%s is out of date: run python3 scripts/sdl_inventory.py --ref %s --out %s\n" % (args.check, args.ref, args.check))
            return 1
        print("%s is up to date" % args.check)
        return 0
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(page)
        print("wrote %s (%d files scanned)" % (args.out, len(files)))
    else:
        sys.stdout.write(page)
    return 0


if __name__ == "__main__":
    sys.exit(main())
