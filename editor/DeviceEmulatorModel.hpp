#pragma once
// The Game view's device emulator (feature map 12.9.4), the part with no UI library: the device table, the screen and
// safe-area geometry per orientation, placing the screen in the view (Fit / 100% / zoom), the mouse-as-finger touch
// emulation (a second finger for pinch, rotate and two-finger pan), the on-screen keyboard, the motion sensor, the
// rotate animation, the slow-device and network conditions, the multi-device layout, screenshot names and the
// per-project settings file. Drawn by editor/DeviceEmulator.cpp (Dear ImGui); tested by tests/test_device_emulator.cpp.
//
// Written for this engine from public device spec sheets (screen pixels, pixel density) and the platforms' public
// layout documentation (iOS safe-area insets in points, Android status / navigation bar heights in dp); every number in
// the table is our own reading of those, rounded. No simulator or SDK code, frames or pictures: the frame is drawn by our
// own vector code from these numbers.
#include <cstdint>
#include <string>
#include <vector>

namespace okay::devemu {

struct Pt { float x = 0.0f, y = 0.0f; };
struct Box {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    bool Contains(Pt p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
};
/// Lengths in from each edge of a screen.
struct Insets { float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f; };

enum class Platform : unsigned char { Ios, Android };
enum class Form : unsigned char { Phone, Foldable, Tablet };
/// What the front camera takes out of the screen. HomeButton: an older phone with a forehead and a chin (no cut-out).
enum class Cutout : unsigned char { None, Notch, Island, PunchHole, HomeButton };
/// How the system is driven: a gesture bar, Android's three buttons (back, home, recents), or a round home button.
enum class Nav : unsigned char { Gesture, ThreeButton, HomeButton };
/// The device's turn. LandscapeLeft: turned a quarter counter-clockwise, its top edge (the camera) on the left.
enum class Orientation : unsigned char { Portrait, LandscapeLeft, PortraitUpsideDown, LandscapeRight };

/// One device. Pixel sizes are the portrait screen. Lengths in `pt` are the platform's own units (iOS points, Android
/// dp): pixels = pt * scale.
struct DeviceSpec {
    std::string id, name;
    Platform platform = Platform::Android;
    Form form = Form::Phone;
    int width = 1080, height = 2400;   // portrait pixels
    float ppi = 400.0f;                // pixels per inch of the real screen
    float scale = 2.625f;              // pixels per point / dp
    float cornerPt = 0.0f;             // screen corner radius
    Cutout cutout = Cutout::None;
    float cutoutWPt = 0.0f, cutoutHPt = 0.0f;   // the camera shape (a punch hole: its diameter)
    float topPt = 24.0f;               // portrait: status bar and camera, from the top
    float bottomPt = 0.0f;             // portrait: home indicator / navigation bar, from the bottom
    float landSidePt = 0.0f;           // landscape: the camera side
    float landBottomPt = 0.0f;         // landscape: home indicator / navigation bar
    float landTopPt = 0.0f;            // landscape: status bar (0: hidden, as on an iPhone)
    bool bothSides = false;            // landscape: the camera side's inset on both sides (iOS keeps the layout symmetric)
    Nav nav = Nav::Gesture;
    bool upsideDown = false;           // the system turns to portrait upside down (tablets)
};

/// The built-in devices. The first 24 are in the order of the old list (okay_settings.txt "gamedevice2" 1..24).
const std::vector<DeviceSpec>& Devices();
/// The device with this id: the table's, or `custom` for "custom". Null for "" (emulator off) or an unknown id.
const DeviceSpec* FindDevice(const std::string& id, const DeviceSpec* custom = nullptr);
/// The id for an old saved index ("gamedevice2": 0 off, 1..24 the old list). "" for 0 or a number out of range.
std::string LegacyDeviceId(int index);
/// The old list's index of a device (0 when it is not one of the old 24).
int LegacyDeviceIndex(const std::string& id);
/// The even older short list ("gamedevice": 0..10), mapped through it to an id.
std::string LegacyShortListId(int value);
/// A custom device to start editing from (a 1080 x 2400 punch-hole phone).
DeviceSpec DefaultCustomDevice();
/// Keep a custom device sane: pixel sizes 64..8192, density 50..1000, scale 0.5..5, insets 0..half the screen.
void ClampCustomDevice(DeviceSpec& d);

const char* PlatformName(Platform p);
const char* FormName(Form f);
const char* CutoutName(Cutout c);
const char* OrientationName(Orientation o);   // "Portrait", "Landscape left", ...
inline bool IsLandscape(Orientation o) { return o == Orientation::LandscapeLeft || o == Orientation::LandscapeRight; }
/// Degrees the device is turned counter-clockwise: 0, 90, 180, 270.
int OrientationDegrees(Orientation o);
Orientation TurnLeft(Orientation o);    // a quarter counter-clockwise
Orientation TurnRight(Orientation o);   // a quarter clockwise

// ---- the screen in device pixels --------------------------------------------------------------------------------
void ScreenPixels(const DeviceSpec& d, Orientation o, int& w, int& h);
/// The parts a game's UI should keep clear of, in device pixels, for the screen as it is turned.
Insets SafeInsetsPx(const DeviceSpec& d, Orientation o);
/// The status bar's height at the top of the screen (0 when the system hides it in this orientation).
float StatusBarPx(const DeviceSpec& d, Orientation o);
/// The home indicator's / navigation bar's height at the bottom.
float HomeBarPx(const DeviceSpec& d, Orientation o);
float CornerPx(const DeviceSpec& d);
/// The camera cut-out on the screen as it is turned: an axis-aligned box (in device pixels) and its corner radius.
struct CutoutShape { Cutout kind = Cutout::None; Box box; float radius = 0.0f; };
CutoutShape CutoutPx(const DeviceSpec& d, Orientation o);

// ---- placing the screen in the view -------------------------------------------------------------------------------
/// Fit: as large as fits (with room for the frame). Actual: one device pixel per display pixel. Percent: that zoom of Actual.
enum class Zoom : unsigned char { Fit, Actual, Percent };
struct Placement {
    Box screen;           // the device screen in view units (Dear ImGui's logical pixels)
    float scale = 1.0f;   // view units per device pixel
};
/// `framebufferScale`: display pixels per view unit (2 on a high-DPI display), so Actual is truly 1:1 on the display.
/// `pan` moves a zoomed screen that is larger than the view (view units). `frameRoom` is kept free around a Fit screen.
Placement PlaceScreen(Box view, int screenW, int screenH, Zoom zoom, float percent, float framebufferScale, Pt pan, float frameRoom);
/// The room Fit keeps around the screen for the frame: 6% of the view's short side plus 10 units.
float FrameRoom(Box view);
/// A point in the view to the game's own pixels (Input::MousePosition, touches, UI): measured from the screen's corner.
Pt ViewToGame(const Placement& p, Pt view);
Pt GameToView(const Placement& p, Pt game);
/// A point in the view to device pixels and back.
Pt ViewToDevice(const Placement& p, Pt view);
Pt DeviceToView(const Placement& p, Pt device);
/// Screen pixels per inch as the game's pixels go: the device's density times the on-screen scale, so a length of dpi
/// game pixels is one inch of the real phone.
float GameDpi(const DeviceSpec& d, const Placement& p);
/// The size the game's 3D picture is rendered at: the device's own pixels (capped at `cap` per side, keeping the shape),
/// or the on-screen size when `deviceResolution` is off.
void RenderSize(int screenW, int screenH, const Placement& p, float framebufferScale, bool deviceResolution, int cap, int& w, int& h);

// ---- what the scripts' Screen API reports (okay::UICanvas, okay::UISafeInsets) --------------------------------------
struct ScreenReport {
    float width = 0.0f, height = 0.0f;   // game pixels
    float dpi = 96.0f;
    int orientation = -1;                // UICanvas::Orientation (0..3)
    Insets safe;                         // fractions of the width / height, the keyboard included
    float keyboardHeight = 0.0f;         // game pixels
    bool darkTheme = false;
};
/// `keyboardGamePx`: the on-screen keyboard's height in game pixels (0 = hidden); it is outside the safe area.
ScreenReport MakeScreenReport(const DeviceSpec& d, Orientation o, const Placement& p, float keyboardGamePx, bool darkTheme);
/// Write a report into the engine's Screen state, or put the desktop values back (96 dpi, no orientation, no insets).
void ApplyScreenReport(const ScreenReport& r);
void ResetScreenReport();

// ---- touch: the mouse is a finger -----------------------------------------------------------------------------------
struct TouchEvent { long long id = 0; int state = 0; Pt pos; };   // state: 0 down, 1 moved, 2 up (Input::FeedTouch)
/// The pointer this frame, in game pixels. `twoFinger`: the modifier for a second finger (pinch / rotate around the
/// screen centre); `pan` with it: the two fingers move together (two-finger pan).
struct PointerFrame { Pt pos; bool down = false; bool twoFinger = false; bool pan = false; };
class TouchEmulator {
public:
    static constexpr long long kFirstId = 0x5EED0001LL;   // ids no real finger device uses
    /// Turn this frame's pointer into touch events. A press outside `screen` (game pixels) or inside `exclude` (the
    /// on-screen keyboard) starts no finger; a finger that slides off the screen stays at its edge.
    void Update(const PointerFrame& in, Box screen, Box exclude, std::vector<TouchEvent>& out);
    /// Lift every finger (the emulator turned off, Play stopped).
    void Release(std::vector<TouchEvent>& out);
    int Fingers() const { return m_down ? (m_two ? 2 : 1) : 0; }
    Pt Finger(int i) const { return i == 0 ? m_p0 : m_p1; }
    /// Where the second finger is or would be (shown while the modifier is held, before pressing).
    static Pt Mirror(Pt p, Pt centre) { return Pt{2.0f * centre.x - p.x, 2.0f * centre.y - p.y}; }
private:
    bool m_down = false, m_two = false;
    Pt m_p0, m_p1, m_panOffset;
    long long m_id0 = kFirstId, m_id1 = kFirstId + 1, m_next = kFirstId;
};

// ---- the on-screen keyboard ------------------------------------------------------------------------------------------
enum class KeyKind : unsigned char { Char, Shift, Backspace, Space, Enter, Symbols, Hide };
struct KeyCap { Box box; KeyKind kind = KeyKind::Char; char ch = 0; std::string label; };
struct KeyboardLayout { Box area; std::vector<KeyCap> keys; };
/// Its height on a screen of w x h game pixels: about 40% of a portrait phone, 55% of a landscape one, 33% of a tablet;
/// `bottomInset` (the home bar) is added below the keys.
float KeyboardHeight(float w, float h, bool tablet, float bottomInset);
/// The keys (letters, or digits and symbols with `symbols`; capitals with `shift`), at the bottom of the screen.
KeyboardLayout LayoutKeyboard(float w, float h, bool tablet, float bottomInset, bool shift, bool symbols);
const KeyCap* KeyAt(const KeyboardLayout& k, Pt p);

// ---- motion sensor ---------------------------------------------------------------------------------------------------
struct Vec3f { float x = 0.0f, y = 0.0f, z = 0.0f; };
/// Gravity in the device's axes (Input::FeedAcceleration) for a device tilted `pitchDeg` back from upright (90 = flat
/// face up) and rolled `rollDeg` about the screen's normal (+ = its right side down).
Vec3f TiltToGravity(float pitchDeg, float rollDeg);
/// Auto-rotate: the orientation gravity asks for, or `current` when it is not clearly one (flat, or between two).
Orientation OrientationForGravity(Vec3f g, Orientation current, bool allowUpsideDown);

// ---- the rotate animation ----------------------------------------------------------------------------------------------
/// The screen turning from one orientation to the next over `seconds`, eased. Angle() is how far the drawing is still
/// turned away from the new orientation (degrees, counter-clockwise; 0 when done).
class RotateAnimation {
public:
    void Start(Orientation from, Orientation to, double now, double seconds = 0.35);
    float Angle(double now) const;
    bool Running(double now) const { return m_running && now < m_t0 + m_len; }
private:
    float m_delta = 0.0f;
    double m_t0 = 0.0, m_len = 0.35;
    bool m_running = false;
};

// ---- conditions ---------------------------------------------------------------------------------------------------------
enum class Network : unsigned char { Online, Slow, Offline };
struct NetValues { bool enabled = false; float latencyMs = 0.0f, jitterMs = 0.0f, loss = 0.0f; };
/// What the network simulator (okay::net::Simulator) gets for each condition. Online: nothing changed.
NetValues NetworkValues(Network n);
const char* NetworkName(Network n);
/// The frame cap the editor sleeps to: the device's cap while it plays on an emulated device, when it is lower.
int EffectiveFpsCap(int editorCap, int deviceCap, bool playing, bool emulating);

// ---- several devices at once ----------------------------------------------------------------------------------------------
/// `n` cells (1..4) side by side in `view` (2 x 2 for four in a view taller than wide), `gap` apart.
std::vector<Box> SplitView(Box view, int n, float gap);

// ---- screenshots -------------------------------------------------------------------------------------------------------------
/// "iPhone_16_Pro_portrait_20261127_154502.png": letters and digits kept, the rest become one underscore.
std::string ScreenshotName(const std::string& device, Orientation o, int y, int mo, int d, int h, int mi, int s);
/// The w x h part at (x, y) of an RGBA picture sw x sh (clipped to it; empty when nothing is left).
std::vector<std::uint8_t> CropRgba(const std::vector<std::uint8_t>& src, int sw, int sh, int x, int y, int w, int h, int& outW, int& outH);

// ---- the status bar ------------------------------------------------------------------------------------------------------------
/// "9:41" (iOS) or "09:41" (Android, 24-hour).
std::string StatusClock(int hour, int minute, Platform p);

// ---- settings ---------------------------------------------------------------------------------------------------------------------
struct Settings {
    std::string device;                 // "" = off
    Orientation orientation = Orientation::Portrait;
    bool rotationLock = true;           // off: the tilt control turns the screen (auto-rotate)
    Zoom zoom = Zoom::Fit;
    float zoomPercent = 100.0f;
    bool safeArea = true, statusBar = true, touchInput = true, touchPoints = true, keyboard = true;
    bool darkTheme = false, deviceResolution = true;
    int fpsCap = 0;                      // 0 = the editor's own
    Network network = Network::Online;
    float tiltPitch = 0.0f, tiltRoll = 0.0f;
    int battery = 80;
    std::vector<std::string> extra;      // more devices shown beside it (up to 3)
    DeviceSpec custom = DefaultCustomDevice();
};
constexpr int kMaxExtraDevices = 3;
/// The file's text (key=value lines) and back. Unknown keys are skipped; a bad number keeps the old value.
std::string WriteSettings(const Settings& s);
void ParseSettings(const std::string& text, Settings& s);
/// The old editor-wide keys in okay_settings.txt (frozen names): gamedevice, gamedevice2, gamedeviceland, gamesafearea.
/// True when `key` was one of them.
bool ApplyLegacySetting(Settings& s, const std::string& key, int value);
/// The project file's name inside ProjectSettings/ and its place in an older project.
inline const char* kSettingsFile = "device_emulator.cfg";
inline const char* kLegacySettingsFile = ".okay/device_emulator.cfg";

} // namespace okay::devemu
