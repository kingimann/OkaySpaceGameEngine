// okay::platform backend for the browser (the Emscripten / WebAssembly build of the player). Our own code: it replaced
// the sdl2 backend over Emscripten's SDL2 port on 2026-11-27, so no program uses SDL2 any more.
//
// One window: the page's canvas (#canvas, platform/web/shell.html). Its parts:
//   events     keyboard, mouse, wheel, touch, focus through Emscripten's html5.h callbacks, queued for PollEvent
//   canvas     each poll sizes the drawing buffer to the canvas's CSS size times devicePixelRatio (WebInput.hpp)
//   GL         a WebGL 2 context (emscripten_webgl_create_context); the OpenGL painter (GLPainter.cpp) draws on it
//   gamepads   the Gamepad API through emscripten_sample_gamepad_data / emscripten_get_gamepad_status, read once a frame
//   audio      Web Audio: an AudioContext and a ScriptProcessorNode that pulls from our queue (EM_JS glue below)
//   clipboard  our own copy of the text, filled by the page's paste events and written with navigator.clipboard
//
// Written from Emscripten's documented C API (emscripten.h, html5.h, html5_webgl.h: only called, nothing copied) and the
// W3C / WHATWG specifications of the DOM events, CSSOM View, Fullscreen, Pointer Lock, Gamepad, Web Audio and the
// Clipboard API. The decisions that need no browser are in WebInput.hpp and tested on Linux
// (tests/test_platform_web_pure.cpp). Not run here: the browser test in CI's web job (tests/scripts/test_web_player.js)
// is what runs it.
#ifdef __EMSCRIPTEN__
#include "Internal.hpp"
#include "AudioPump.hpp"
#include "WebInput.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace okay::platform {
namespace {

const char* const kCanvas = "#canvas";   // the canvas of platform/web/shell.html (Module.canvas)

// ---- Page glue (our own JavaScript, inside okay-player.js) ------------------------------------------------------
// Strings cross as UTF-8 bytes in the WebAssembly memory (HEAPU8) and the page's TextEncoder / TextDecoder.
// A string coming back is two calls: the first returns its byte length and keeps the bytes, the second copies them.

EM_JS(void, okay_web_canvas_rect, (double* out), {
    var c = Module["canvas"] || document.querySelector("#canvas");
    var r = c ? c.getBoundingClientRect() : { left: 0, top: 0, width: 0, height: 0 };
    var i = out >> 3;
    HEAPF64[i] = r.left; HEAPF64[i + 1] = r.top; HEAPF64[i + 2] = r.width; HEAPF64[i + 3] = r.height;
});

EM_JS(void, okay_web_set_title, (const char* s, int n), {
    document.title = new TextDecoder().decode(HEAPU8.slice(s, s + n));
});

EM_JS(void, okay_web_set_cursor, (const char* s, int n), {
    var c = Module["canvas"] || document.querySelector("#canvas");
    if (c) c.style.cursor = new TextDecoder().decode(HEAPU8.slice(s, s + n));
});

EM_JS(int, okay_web_fullscreen, (int on), {
    var c = Module["canvas"] || document.querySelector("#canvas");
    var d = document;
    var cur = d.fullscreenElement || d.webkitFullscreenElement;
    if (!on) {
        if (cur) (d.exitFullscreen || d.webkitExitFullscreen).call(d);
        return 1;
    }
    if (cur || !c) return cur ? 1 : 0;
    var el = c.parentElement && c.parentElement !== d.body ? c.parentElement : c;
    var req = el.requestFullscreen || el.webkitRequestFullscreen;
    if (!req) return 0;
    try { var p = req.call(el); if (p && p.catch) p.catch(function () {}); } catch (e) { return 0; }
    return 1;
});

EM_JS(int, okay_web_is_fullscreen, (void), {
    return (document.fullscreenElement || document.webkitFullscreenElement) ? 1 : 0;
});

EM_JS(int, okay_web_touch_points, (void), {
    return (navigator.maxTouchPoints | 0) > 0 || ("ontouchstart" in window) ? 1 : 0;
});

EM_JS(int, okay_web_has_focus, (void), {
    return document.hasFocus() ? 1 : 0;
});

EM_JS(void, okay_web_screen, (int* out), {
    var s = window.screen || {};
    var i = out >> 2;
    HEAP32[i] = s.width | 0; HEAP32[i + 1] = s.height | 0;
    HEAP32[i + 2] = (s.availWidth || s.width) | 0; HEAP32[i + 3] = (s.availHeight || s.height) | 0;
});

EM_JS(int, okay_web_str_len_languages, (void), {
    var l = navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || ""];
    Module["okayStr"] = new TextEncoder().encode(l.join(","));
    return Module["okayStr"].length;
});

EM_JS(int, okay_web_str_len_clipboard, (void), {
    Module["okayStr"] = new TextEncoder().encode(Module["okayClip"] || "");
    return Module["okayStr"].length;
});

EM_JS(int, okay_web_str_take, (char* dst, int max), {
    var b = Module["okayStr"] || new Uint8Array(0);
    var n = Math.min(b.length, max);
    HEAPU8.set(b.subarray(0, n), dst);
    Module["okayStr"] = null;
    return n;
});

EM_JS(void, okay_web_clip_install, (void), {
    if (Module["okayClipOn"]) return;
    Module["okayClipOn"] = true;
    Module["okayClip"] = Module["okayClip"] || "";
    window.addEventListener("paste", function (e) {
        var t = e.clipboardData ? e.clipboardData.getData("text/plain") : "";
        if (t) Module["okayClip"] = t;
    });
    window.addEventListener("copy", function (e) {
        if (!Module["okayClip"] || !e.clipboardData) return;
        e.clipboardData.setData("text/plain", Module["okayClip"]);
        e.preventDefault();
    });
});

EM_JS(void, okay_web_clip_set, (const char* s, int n), {
    var t = new TextDecoder().decode(HEAPU8.slice(s, s + n));
    Module["okayClip"] = t;
    try {
        if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(t).catch(function () {});
    } catch (e) {}
});

EM_JS(int, okay_web_pad_rumble, (int index, double weak, double strong, int ms), {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    var p = pads ? pads[index] : null;
    var a = p ? p.vibrationActuator : null;
    if (!a || !a.playEffect) return 0;
    try {
        var r = a.playEffect("dual-rumble", { startDelay: 0, duration: ms, weakMagnitude: weak, strongMagnitude: strong });
        if (r && r.catch) r.catch(function () {});
    } catch (e) { return 0; }
    return 1;
});

// Web Audio: one AudioContext per device; a ScriptProcessorNode asks okay_web_audio_pull for `frames` interleaved float
// frames and spreads them over its output channels. Returns the context's sample rate, 0 when there is no Web Audio.
EM_JS(int, okay_web_audio_open, (int id, int freq, int channels, int frames, int anyRate), {
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return 0;
    var ctx = null;
    if (!anyRate) { try { ctx = new AC({ sampleRate: freq }); } catch (e) { ctx = null; } }
    if (!ctx) { try { ctx = new AC(); } catch (e) { return 0; } }
    if (!anyRate && ctx.sampleRate !== freq) { try { ctx.close(); } catch (e) {} return 0; }
    var node = ctx.createScriptProcessor(frames, 0, channels);
    node.onaudioprocess = function (ev) {
        var out = ev.outputBuffer, n = out.length, ch = out.numberOfChannels;
        var pull = Module["_okay_web_audio_pull"];
        var p = pull ? pull(id, n) : 0;
        for (var c = 0; c < ch; ++c) {
            var d = out.getChannelData(c);
            if (!p) { d.fill(0); continue; }
            var base = (p >> 2) + c;
            for (var i = 0; i < n; ++i) d[i] = HEAPF32[base + i * ch];
        }
    };
    node.connect(ctx.destination);
    Module["okayAudio"] = Module["okayAudio"] || {};
    Module["okayAudio"][id] = { ctx: ctx, node: node };
    return ctx.sampleRate | 0;
});

EM_JS(void, okay_web_audio_close, (int id), {
    var a = Module["okayAudio"] ? Module["okayAudio"][id] : null;
    if (!a) return;
    try { a.node.onaudioprocess = null; a.node.disconnect(); } catch (e) {}
    try { a.ctx.close(); } catch (e) {}
    delete Module["okayAudio"][id];
});

std::string g_err;
std::string TakeString(int len) {
    std::string s;
    if (len <= 0) { okay_web_str_take(nullptr, 0); return s; }
    s.resize((size_t)len);
    const int got = okay_web_str_take(&s[0], len);
    s.resize(got > 0 ? (size_t)got : 0);
    return s;
}

// ---- Audio ------------------------------------------------------------------------------------------------------
class WebAudioDevice;
std::vector<WebAudioDevice*>& AudioDevices() { static std::vector<WebAudioDevice*> v; return v; }

class WebAudioDevice final : public AudioDevice {
public:
    WebAudioDevice(int id, const AudioSpec& s) : m_id(id), m_spec(s) {}
    ~WebAudioDevice() override {
        okay_web_audio_close(m_id);
        auto& v = AudioDevices();
        for (auto& p : v) if (p == this) p = nullptr;
    }
    int IdNum() const { return m_id; }
    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool p) override { m_paused = p; }
    bool Queue(const void* d, uint32_t n) override {
        if (!d || !n) return n == 0;
        // At most two seconds may wait (a page in the background stops pulling, the game keeps queueing).
        const uint32_t frame = (uint32_t)(BytesPerSample(m_spec.format) * m_spec.channels);
        const uint32_t cap = (uint32_t)m_spec.freq * 2u * frame;
        if (m_q.Size() + n > cap) return false;   // dup-allow: pump::ByteRing::Size (AudioPump.hpp), not the retired Size of an okaycc test program
        m_q.Push(d, n);
        return true;
    }
    uint32_t QueuedBytes() const override { return m_q.Size(); }   // dup-allow: pump::ByteRing::Size (AudioPump.hpp), as OwnWin32Audio.cpp
    void ClearQueued() override { m_q.Clear(); }
    uint32_t Dequeue(void*, uint32_t) override { return 0; }   // playback only

    /// The ScriptProcessorNode's request: `frames` interleaved float frames, silence while paused or short of data.
    float* Pull(int frames) {
        if (frames <= 0 || frames > 16384) return nullptr;
        const int ch = m_spec.channels;
        const size_t samples = (size_t)frames * (size_t)ch;
        m_out.assign(samples, 0.0f);
        if (m_paused) return m_out.data();
        const uint32_t bytes = (uint32_t)(samples * (size_t)BytesPerSample(m_spec.format));
        m_raw.resize(bytes);
        pump::FillPlayback(m_spec, m_q, m_raw.data(), bytes);
        if (m_spec.format == SampleFormat::F32) std::memcpy(m_out.data(), m_raw.data(), bytes);
        else {
            const int16_t* s = reinterpret_cast<const int16_t*>(m_raw.data());
            for (size_t i = 0; i < samples; ++i) m_out[i] = (float)s[i] / 32768.0f;
        }
        return m_out.data();
    }

private:
    int m_id;
    AudioSpec m_spec;
    bool m_paused = true;   // devices start paused
    pump::ByteRing m_q;
    std::vector<uint8_t> m_raw;
    std::vector<float> m_out;
};

// ---- Gamepads ---------------------------------------------------------------------------------------------------
constexpr int kMaxPads = 8;
struct PadState {
    bool connected = false, standard = false;
    int numAxes = 0, numButtons = 0;
    double axis[64] = {}, analog[64] = {};
    bool digital[64] = {};
    std::string name;
    uint16_t vendor = 0, product = 0;
};
PadState g_pads[kMaxPads];

class WebGamepad final : public Gamepad {
public:
    explicit WebGamepad(int index) : m_i(index) {}
    int InstanceId() const override { return m_i; }
    const char* Name() const override { return P().name.c_str(); }
    int16_t Axis(GamepadAxis a) const override {
        const PadState& p = P();
        if (!p.connected || !p.standard) return 0;
        if (a == GamepadAxis::TriggerLeft) return Analog(p, web::kStandardLeftTrigger);
        if (a == GamepadAxis::TriggerRight) return Analog(p, web::kStandardRightTrigger);
        const int i = (int)a;   // LeftX, LeftY, RightX, RightY = axes 0..3
        return i < p.numAxes ? web::StickValue(p.axis[i]) : (int16_t)0;
    }
    bool Button(GamepadButton b) const override {
        const PadState& p = P();
        const int i = web::StandardButtonIndex(b);
        return p.connected && p.standard && i >= 0 && i < p.numButtons && p.digital[i];
    }
    bool Rumble(uint16_t lo, uint16_t hi, uint32_t ms) override {
        // lo = the low-frequency (strong) motor, hi = the high-frequency (weak) one
        return okay_web_pad_rumble(m_i, hi / 65535.0, lo / 65535.0, (int)(ms > 60000 ? 60000 : ms)) != 0;
    }
    uint16_t VendorId() const override { return P().vendor; }
    uint16_t ProductId() const override { return P().product; }
    bool HasBuiltinMapping() const override { return P().standard; }
    int RawAxisCount() const override { return P().numAxes; }
    int RawButtonCount() const override { return P().numButtons; }
    int16_t RawAxis(int i) const override { const PadState& p = P(); return i >= 0 && i < p.numAxes ? web::StickValue(p.axis[i]) : (int16_t)0; }
    bool RawButton(int i) const override { const PadState& p = P(); return i >= 0 && i < p.numButtons && p.digital[i]; }

private:
    const PadState& P() const { return g_pads[m_i]; }
    static int16_t Analog(const PadState& p, int i) { return i < p.numButtons ? web::TriggerValue(p.analog[i]) : (int16_t)0; }
    int m_i;
};

// ---- The window (the canvas) --------------------------------------------------------------------------------------
class WebBackend;

class WebWindow final : public Window {
public:
    WebWindow(WebBackend& b, const WindowDesc& d) : m_b(b), m_highDpi(d.highDpi), m_title(d.title) {}
    ~WebWindow() override;
    uint32_t Id() const override { return 1; }
    void* NativeHandle() const override { return (void*)kCanvas; }
    void GetSize(int& w, int& h) const override { w = m_cssW; h = m_cssH; }
    void GetPosition(int& x, int& y) const override { x = y = 0; }
    void SetTitle(const std::string& t) override { m_title = t; okay_web_set_title(t.data(), (int)t.size()); }
    void SetSize(int w, int h) override {
        // The page sizes the canvas (shell.html). Only a canvas with no size of its own in the page's style takes ours.
        if (!m_explicitCss || w < 1 || h < 1) return;
        emscripten_set_element_css_size(kCanvas, (double)w, (double)h);
        Sync(nullptr);
    }
    void SetPosition(int, int) override {}
    void SetMinimumSize(int, int) override {}
    void Show() override {}
    void Hide() override {}
    void Raise() override {}
    void Minimize() override {}
    void Restore() override {}
    void SetFullscreen(bool on) override { okay_web_fullscreen(on ? 1 : 0); }
    bool IsFocused() const override;
    bool HasMouseFocus() const override;
    bool IsFullscreen() const override { return okay_web_is_fullscreen() != 0; }
    int Dpi() const override {
        const double r = emscripten_get_device_pixel_ratio();
        return r > 0.25 && r < 8.0 ? (int)(96.0 * r + 0.5) : 96;
    }

    bool CreateGLContext(const GLAttributes& a) override {
        DestroyGLContext();
        EmscriptenWebGLContextAttributes at;
        emscripten_webgl_init_context_attributes(&at);
        at.alpha = EM_FALSE;            // the game covers the canvas; the page behind it must not show through
        at.depth = a.depthBits > 0 ? EM_TRUE : EM_FALSE;
        at.stencil = EM_FALSE;
        at.antialias = EM_FALSE;        // the painter's pixels follow the software painter's rules
        at.premultipliedAlpha = EM_TRUE;
        at.preserveDrawingBuffer = EM_FALSE;
        at.majorVersion = 2;            // WebGL 2 = OpenGL ES 3.0, what the painter's GLSL ES 3.00 shaders need
        at.minorVersion = 0;
        at.enableExtensionsByDefault = EM_TRUE;
        m_ctx = emscripten_webgl_create_context(kCanvas, &at);
        if ((intptr_t)m_ctx <= 0) { m_ctx = 0; g_err = "this browser gave the page no WebGL 2 context"; return false; }
        if (emscripten_webgl_make_context_current(m_ctx) != EMSCRIPTEN_RESULT_SUCCESS) { DestroyGLContext(); g_err = "the WebGL 2 context could not be made current"; return false; }
        return true;
    }
    void DestroyGLContext() override {
        if (!m_ctx) return;
        if (emscripten_webgl_get_current_context() == m_ctx) emscripten_webgl_make_context_current(0);
        emscripten_webgl_destroy_context(m_ctx);
        m_ctx = 0;
    }
    bool MakeCurrent() override { return m_ctx && emscripten_webgl_make_context_current(m_ctx) == EMSCRIPTEN_RESULT_SUCCESS; }
    // The browser shows the frame when the frame callback returns (requestAnimationFrame), at the display's rate.
    void SwapBuffers() override {}
    bool SetSwapInterval(int) override { return true; }
    void GetDrawableSize(int& w, int& h) const override { w = m_pxW; h = m_pxH; }

    /// Read the canvas's CSS box and give its drawing buffer the matching device pixels. Appends WindowResized to `out`
    /// when the CSS size changed. Called by every poll and once when the window is made.
    void Sync(std::deque<Event>* out, uint32_t nowMs = 0) {
        double r[4] = {0, 0, 0, 0};
        okay_web_canvas_rect(r);
        m_left = r[0]; m_top = r[1]; m_cssWf = r[2]; m_cssHf = r[3];
        int pw = 1, ph = 1;
        web::CanvasPixels(r[2], r[3], emscripten_get_device_pixel_ratio(), m_highDpi, pw, ph);
        if (pw != m_pxW || ph != m_pxH) {
            emscripten_set_canvas_element_size(kCanvas, pw, ph);
            m_pxW = pw; m_pxH = ph;
        }
        const int cw = (int)(r[2] + 0.5), ch = (int)(r[3] + 0.5);
        if (cw != m_cssW || ch != m_cssH) {
            m_cssW = cw; m_cssH = ch;
            if (out) {
                Event e; e.type = EventType::WindowResized; e.windowId = 1; e.width = cw; e.height = ch;
                e.timestampMs = nowMs;
                out->push_back(e);
            }
        }
    }
    /// A canvas whose CSS size follows its width / height attributes (no size in the page's style) would grow without end
    /// when the attributes become device pixels; such a canvas gets the window's size as its CSS size.
    void FixUnsizedCanvas(int w, int h) {
        double a[4], b[4];
        okay_web_canvas_rect(a);
        int ow = 0, oh = 0;
        emscripten_get_canvas_element_size(kCanvas, &ow, &oh);
        emscripten_set_canvas_element_size(kCanvas, ow + 17, oh + 13);
        okay_web_canvas_rect(b);
        emscripten_set_canvas_element_size(kCanvas, ow, oh);
        if (a[2] != b[2] || a[3] != b[3]) {
            m_explicitCss = true;
            emscripten_set_element_css_size(kCanvas, (double)(w > 0 ? w : 640), (double)(h > 0 ? h : 480));
        }
    }
    /// A position in the page (clientX / clientY) relative to the canvas, in CSS pixels.
    void ToCanvas(double cx, double cy, int& x, int& y) const { x = (int)std::floor(cx - m_left); y = (int)std::floor(cy - m_top); }
    bool Inside(int x, int y) const { return x >= 0 && y >= 0 && x < m_cssW && y < m_cssH; }
    double CssW() const { return m_cssWf; }
    double CssH() const { return m_cssHf; }

private:
    WebBackend& m_b;
    bool m_highDpi;
    bool m_explicitCss = false;
    std::string m_title;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_ctx = 0;
    int m_cssW = 0, m_cssH = 0, m_pxW = 0, m_pxH = 0;
    double m_left = 0, m_top = 0, m_cssWf = 0, m_cssHf = 0;
};

// ---- The backend ----------------------------------------------------------------------------------------------------
class WebBackend final : public Backend {
public:
    BackendKind Kind() const override { return BackendKind::Own; }

    bool Init(const InitFlags& f) override {
        if (f.video && !m_callbacks) {
            m_focused = okay_web_has_focus() != 0;
            const char* W = EMSCRIPTEN_EVENT_TARGET_WINDOW;
            // Keys on the window (the page or the iframe the game is in), pointer input on the canvas, the button release
            // and motion on the window too so a drag that leaves the canvas still ends.
            emscripten_set_keydown_callback(W, this, EM_TRUE, &WebBackend::OnKey);
            emscripten_set_keyup_callback(W, this, EM_TRUE, &WebBackend::OnKey);
            emscripten_set_keypress_callback(W, this, EM_TRUE, &WebBackend::OnKeyPress);
            if (emscripten_set_mousedown_callback(kCanvas, this, EM_TRUE, &WebBackend::OnMouse) != EMSCRIPTEN_RESULT_SUCCESS) {
                g_err = "the page has no canvas with the id \"canvas\"";
                emscripten_html5_remove_all_event_listeners();
                return false;
            }
            emscripten_set_mouseup_callback(W, this, EM_TRUE, &WebBackend::OnMouse);
            emscripten_set_mousemove_callback(W, this, EM_TRUE, &WebBackend::OnMouse);
            emscripten_set_mouseenter_callback(kCanvas, this, EM_TRUE, &WebBackend::OnMouse);
            emscripten_set_mouseleave_callback(kCanvas, this, EM_TRUE, &WebBackend::OnMouse);
            emscripten_set_wheel_callback(kCanvas, this, EM_TRUE, &WebBackend::OnWheel);
            emscripten_set_touchstart_callback(kCanvas, this, EM_TRUE, &WebBackend::OnTouch);
            emscripten_set_touchmove_callback(kCanvas, this, EM_TRUE, &WebBackend::OnTouch);
            emscripten_set_touchend_callback(kCanvas, this, EM_TRUE, &WebBackend::OnTouch);
            emscripten_set_touchcancel_callback(kCanvas, this, EM_TRUE, &WebBackend::OnTouch);
            emscripten_set_focus_callback(W, this, EM_TRUE, &WebBackend::OnFocus);
            emscripten_set_blur_callback(W, this, EM_TRUE, &WebBackend::OnFocus);
            okay_web_clip_install();
            m_callbacks = true;
        }
        return true;
    }
    void Shutdown() override {
        if (m_callbacks) emscripten_html5_remove_all_event_listeners();
        m_callbacks = false;
        m_queue.clear();
    }
    std::string LastError() const override { return g_err; }

    uint64_t TicksMs() override { return (uint64_t)(emscripten_get_now() - m_t0); }
    uint64_t PerfCounter() override { return (uint64_t)(emscripten_get_now() * 1000.0); }   // microseconds
    uint64_t PerfFrequency() override { return 1000000ull; }
    // The page's only thread must not wait: the browser calls the frame again when it is due (emscripten_set_main_loop).
    void DelayMs(uint32_t) override {}

    std::unique_ptr<Window> MakeWindow(const WindowDesc& d) override {
        if (m_window) { g_err = "the page has one canvas, and it already belongs to a window"; return nullptr; }
        std::unique_ptr<WebWindow> w(new WebWindow(*this, d));
        w->FixUnsizedCanvas(d.width, d.height);
        if (!d.title.empty()) w->SetTitle(d.title);
        w->Sync(nullptr);
        m_window = w.get();
        return w;
    }
    void WindowGone(WebWindow* w) { if (m_window == w) m_window = nullptr; }
    bool Focused() const { return m_focused; }
    bool MouseIn() const { return m_mouseIn; }

    bool PollEvent(Event& out) override {
        if (m_queue.empty() && !m_pumped) { Pump(); m_pumped = true; }
        if (m_queue.empty()) { m_pumped = false; return false; }
        out = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }
    void PumpEvents() override { Pump(); }
    bool HasPendingEvents() override { return !m_queue.empty(); }
    bool PushEvent(const Event& e) override {
        if (e.type == EventType::NoEvent || e.type == EventType::Other) return false;
        if (e.type == EventType::KeyDown || e.type == EventType::KeyUp) {
            const int k = (int)e.key;
            if (k > 0 && k < kKeyCount) m_keys[k] = e.type == EventType::KeyDown;
        }
        m_queue.push_back(e);
        return true;
    }
    void* GLProcAddress(const char* name) override { return emscripten_webgl_get_proc_address(name); }
    void ReleaseCurrent() override { emscripten_webgl_make_context_current(0); }
    GLCurrent CurrentGL() override {
        GLCurrent c;
        const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE h = emscripten_webgl_get_current_context();
        c.context = (void*)(uintptr_t)h;
        c.window = h ? (void*)kCanvas : nullptr;
        return c;
    }
    bool RestoreGL(const GLCurrent& s) override {
        return emscripten_webgl_make_context_current((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)(uintptr_t)s.context) == EMSCRIPTEN_RESULT_SUCCESS;
    }
    int TouchDeviceCount() override { return m_sawTouch || okay_web_touch_points() ? 1 : 0; }

    uint32_t KeyboardFocusWindowId() override { return m_window && m_focused ? 1 : 0; }
    uint32_t MouseFocusWindowId() override { return m_window && m_mouseIn ? 1 : 0; }
    void* NativeHandleFromId(uint32_t id) override { return id == 1 && m_window ? (void*)kCanvas : nullptr; }

    void StartTextInput() override { m_text = true; }
    void StopTextInput() override { m_text = false; }
    bool IsKeyDown(Key k) override { const int i = (int)k; return i > 0 && i < kKeyCount && m_keys[i]; }
    uint32_t MouseState(int* x, int* y) override { if (x) *x = m_mx; if (y) *y = m_my; return m_buttons; }
    bool GlobalMouseState(int* x, int* y, uint32_t* buttons) override {
        if (x) *x = m_gx;
        if (y) *y = m_gy;
        if (buttons) *buttons = m_buttons;
        return true;
    }
    bool SetRelativeMouse(bool on) override {
        if (on) {
            const EMSCRIPTEN_RESULT r = emscripten_request_pointerlock(kCanvas, EM_TRUE);   // waits for the next click or key when needed
            return r == EMSCRIPTEN_RESULT_SUCCESS || r == EMSCRIPTEN_RESULT_DEFERRED;
        }
        emscripten_exit_pointerlock();
        return true;
    }
    bool IsRelativeMouse() override {
        EmscriptenPointerlockChangeEvent s;
        return emscripten_get_pointerlock_status(&s) == EMSCRIPTEN_RESULT_SUCCESS && s.isActive;
    }
    void ShowCursor(bool show) override { m_cursorShown = show; ApplyCursor(); }
    bool IsCursorShown() override { return m_cursorShown; }
    bool SetCursorShape(CursorShape shape) override { m_shape = shape; ApplyCursor(); return true; }
    void WarpMouse(Window*, int, int) override {}   // a page cannot move the pointer
    uint32_t RelativeMouseState(int* dx, int* dy) override {
        if (dx) *dx = m_relX;
        if (dy) *dy = m_relY;
        m_relX = m_relY = 0;
        return m_buttons;
    }
    bool SupportsRelativeMouseState() override { return true; }
    Key KeyForChar(char c) override {
        const Key k = m_layout.ForChar(c);
        return k != Key::Unknown ? k : Backend::KeyForChar(c);
    }

    int DisplayCount() override { return 1; }
    bool DisplayAt(int i, DisplayInfo& o) override {
        if (i != 0) return false;
        int s[4] = {0, 0, 0, 0};
        okay_web_screen(s);
        const double r = emscripten_get_device_pixel_ratio();
        o = DisplayInfo();
        o.name = "Browser screen";
        o.w = s[0]; o.h = s[1]; o.uw = s[2]; o.uh = s[3];
        o.dpi = r > 0.25 && r < 8.0 ? (float)(96.0 * r) : 96.0f;
        o.primary = true;
        return true;
    }

    std::string BasePath() override { return "/"; }   // the game's files are unpacked at the root of the page's file system
    std::vector<std::string> PreferredLocales() override {
        std::vector<std::string> out;
        const std::string all = TakeString(okay_web_str_len_languages());
        size_t pos = 0;
        while (pos <= all.size()) {
            const size_t c = all.find(',', pos);
            const std::string one = all.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
            if (!one.empty()) out.push_back(one);
            if (c == std::string::npos) break;
            pos = c + 1;
        }
        return out;
    }
    void Log(const std::string& line) override {   // stdout = the page's console.log
        std::printf("INFO: %s\n", line.c_str());
        std::fflush(stdout);
    }

    bool SetClipboardText(const std::string& t) override { okay_web_clip_install(); okay_web_clip_set(t.data(), (int)t.size()); return true; }
    std::string GetClipboardText() override { return TakeString(okay_web_str_len_clipboard()); }

    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& want, bool capture, const char*, AudioSpec* got) override {
        if (capture) { g_err = "recording sound is not supported in the browser build"; return nullptr; }
        if (want.freq <= 0 || want.channels <= 0) { g_err = "bad audio format"; return nullptr; }
        AudioSpec s = want;
        if (s.channels > 2) {
            if (!want.allowChange) { g_err = "the browser build plays at most two channels"; return nullptr; }
            s.channels = 2;
        }
        s.frames = web::ProcessorFrames(want.frames);
        const int id = m_nextAudio++;
        const int rate = okay_web_audio_open(id, want.freq, s.channels, s.frames, want.allowChange ? 1 : 0);
        if (rate <= 0) { g_err = "this browser has no Web Audio at that sample rate"; return nullptr; }
        s.freq = rate;
        std::unique_ptr<WebAudioDevice> d(new WebAudioDevice(id, s));
        AudioDevices().push_back(d.get());
        if (got) *got = s;
        return d;
    }
    int AudioDeviceCount(bool capture) override { return capture ? 0 : 1; }
    std::string AudioDeviceName(int i, bool capture) override { return !capture && i == 0 ? "Browser audio" : ""; }

    int GamepadDeviceCount() override {
        SamplePads();
        int n = 0;
        for (const PadState& p : g_pads) n += p.connected ? 1 : 0;
        return n;
    }
    bool IsGamepad(int i) override { return i >= 0 && BrowserIndex(i) >= 0; }
    std::unique_ptr<Gamepad> OpenGamepad(int i) override {
        const int b = BrowserIndex(i);
        if (b < 0) { g_err = "no such game controller"; return nullptr; }
        return std::unique_ptr<Gamepad>(new WebGamepad(b));
    }

private:
    void Pump() {
        if (m_window) m_window->Sync(&m_queue, Now());
        SamplePads();
    }
    uint32_t Now() { return (uint32_t)TicksMs(); }
    uint32_t Mods(EM_BOOL ctrl, EM_BOOL shift, EM_BOOL alt, EM_BOOL meta) const {
        uint32_t m = 0;
        if (shift) m |= ModShift;
        if (ctrl) m |= ModCtrl;
        if (alt) m |= ModAlt;
        if (meta) m |= ModGui;
        return m;
    }
    void ApplyCursor() {
        const char* c = m_cursorShown ? web::CssCursor(m_shape) : "none";
        okay_web_set_cursor(c, (int)std::strlen(c));
    }
    // The `i`-th connected pad's place in navigator.getGamepads(), -1 when there is none.
    int BrowserIndex(int i) const {
        for (int b = 0, n = 0; b < kMaxPads; ++b)
            if (g_pads[b].connected) { if (n == i) return b; ++n; }
        return -1;
    }
    // Read the Gamepad API once; pads that came or went become GamepadAdded / GamepadRemoved events (also when the read was
    // asked for by GamepadDeviceCount: PadSet::Attach ignores a pad it already holds).
    void SamplePads() {
        if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) return;
        int n = emscripten_get_num_gamepads();
        if (n > kMaxPads) n = kMaxPads;
        bool now[kMaxPads] = {};
        for (int b = 0; b < kMaxPads; ++b) {
            PadState& p = g_pads[b];
            EmscriptenGamepadEvent g;
            if (b < n && emscripten_get_gamepad_status(b, &g) == EMSCRIPTEN_RESULT_SUCCESS && g.connected) {
                now[b] = true;
                p.numAxes = g.numAxes < 0 ? 0 : g.numAxes > 64 ? 64 : g.numAxes;
                p.numButtons = g.numButtons < 0 ? 0 : g.numButtons > 64 ? 64 : g.numButtons;
                for (int k = 0; k < p.numAxes; ++k) p.axis[k] = g.axis[k];
                for (int k = 0; k < p.numButtons; ++k) { p.analog[k] = g.analogButton[k]; p.digital[k] = g.digitalButton[k] != 0; }
                if (!p.connected) {
                    char id[sizeof(g.id) + 1], map[sizeof(g.mapping) + 1];
                    std::memcpy(id, g.id, sizeof(g.id)); id[sizeof(g.id)] = 0;
                    std::memcpy(map, g.mapping, sizeof(g.mapping)); map[sizeof(g.mapping)] = 0;
                    p.name = id;
                    p.standard = std::strcmp(map, "standard") == 0;
                    web::VendorProductFromId(id, p.vendor, p.product);
                }
            }
        }
        for (int b = 0; b < kMaxPads; ++b) {
            PadState& p = g_pads[b];
            if (now[b] == p.connected) continue;
            p.connected = now[b];
            Event e;
            e.timestampMs = Now();
            if (now[b]) {
                int pos = 0;
                for (int k = 0; k < b; ++k) pos += g_pads[k].connected ? 1 : 0;
                e.type = EventType::GamepadAdded; e.deviceId = pos;
            } else {
                p = PadState();
                e.type = EventType::GamepadRemoved; e.deviceId = b;
            }
            m_queue.push_back(e);
        }
    }

    // ---- html5.h callbacks: return EM_TRUE to keep the browser from acting on the event as well --------------------
    static EM_BOOL OnKey(int type, const EmscriptenKeyboardEvent* k, void* user) {
        WebBackend& b = *static_cast<WebBackend*>(user);
        const Key key = web::KeyFromDomCode(k->code);
        const bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;
        const bool text = web::KeyTypesText(k->key);
        if (down) b.m_layout.Learn(key, k->key);
        if (key != Key::Unknown) {
            const int i = (int)key;
            if (i < kKeyCount) b.m_keys[i] = down;
            Event e;
            e.type = down ? EventType::KeyDown : EventType::KeyUp;
            e.timestampMs = b.Now(); e.windowId = 1; e.key = key;
            e.mods = b.Mods(k->ctrlKey, k->shiftKey, k->altKey, k->metaKey);
            e.repeat = down && k->repeat;
            b.m_queue.push_back(e);
        }
        if (!down) return EM_TRUE;
        return web::PreventKeyDown(key, text, k->ctrlKey != 0, k->metaKey != 0) ? EM_TRUE : EM_FALSE;
    }
    static EM_BOOL OnKeyPress(int, const EmscriptenKeyboardEvent* k, void* user) {
        WebBackend& b = *static_cast<WebBackend*>(user);
        if (b.m_text && web::KeyTypesText(k->key) && !k->ctrlKey && !k->metaKey) {
            Event e;
            e.type = EventType::TextInput; e.timestampMs = b.Now(); e.windowId = 1; e.text = k->key;
            b.m_queue.push_back(e);
        }
        return EM_TRUE;   // the text is ours; this also keeps Space from scrolling the page
    }
    static EM_BOOL OnMouse(int type, const EmscriptenMouseEvent* m, void* user) {
        WebBackend& b = *static_cast<WebBackend*>(user);
        WebWindow* w = b.m_window;
        if (type == EMSCRIPTEN_EVENT_MOUSEENTER) { b.m_mouseIn = true; return EM_FALSE; }
        if (type == EMSCRIPTEN_EVENT_MOUSELEAVE) { b.m_mouseIn = false; return EM_FALSE; }
        int x = 0, y = 0;
        if (w) w->ToCanvas((double)m->clientX, (double)m->clientY, x, y);
        b.m_gx = (int)m->screenX; b.m_gy = (int)m->screenY;
        const bool locked = b.IsRelativeMouse();
        if (type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
            const bool inside = w && w->Inside(x, y);
            if (!inside && !b.m_buttons && !locked) return EM_FALSE;   // the pointer moving over the rest of the page
            const int dx = (int)m->movementX, dy = (int)m->movementY;
            b.m_relX += dx; b.m_relY += dy;
            if (!locked) { b.m_mx = x; b.m_my = y; }
            Event e;
            e.type = EventType::MouseMove; e.timestampMs = b.Now(); e.windowId = 1;
            e.x = b.m_mx; e.y = b.m_my; e.dx = dx; e.dy = dy;
            b.m_queue.push_back(e);
            return EM_FALSE;
        }
        const int btn = web::MouseButtonFromDom((int)m->button);
        if (!btn) return EM_FALSE;
        const bool down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
        if (!down && !(b.m_buttons & MouseMask(btn))) return EM_FALSE;   // a release of a press that did not start on the canvas
        if (!locked) { b.m_mx = x; b.m_my = y; }
        Event e;
        e.type = down ? EventType::MouseButtonDown : EventType::MouseButtonUp;
        e.timestampMs = b.Now(); e.windowId = 1; e.button = btn; e.x = b.m_mx; e.y = b.m_my;
        if (down) {
            b.m_buttons |= MouseMask(btn);
            // two presses of one button within half a second and 4 pixels: a double click
            const bool again = btn == b.m_lastBtn && e.timestampMs - b.m_lastDownMs <= 500 &&
                               std::abs(e.x - b.m_lastX) <= 4 && std::abs(e.y - b.m_lastY) <= 4;
            b.m_clicks = again ? b.m_clicks + 1 : 1;
            b.m_lastBtn = btn; b.m_lastDownMs = e.timestampMs; b.m_lastX = e.x; b.m_lastY = e.y;
        } else {
            b.m_buttons &= ~MouseMask(btn);
        }
        e.clicks = b.m_clicks;
        b.m_queue.push_back(e);
        return down ? EM_TRUE : EM_FALSE;   // a press on the canvas selects no page text
    }
    static EM_BOOL OnWheel(int, const EmscriptenWheelEvent* wv, void* user) {
        WebBackend& b = *static_cast<WebBackend*>(user);
        Event e;
        e.type = EventType::MouseWheel; e.timestampMs = b.Now(); e.windowId = 1;
        e.wheelX = web::WheelNotches(wv->deltaX, (int)wv->deltaMode);
        e.wheelY = -web::WheelNotches(wv->deltaY, (int)wv->deltaMode);   // the page's +y is down; ours is up
        if (e.wheelX != 0.0f || e.wheelY != 0.0f) b.m_queue.push_back(e);
        return EM_TRUE;   // the page around the game does not scroll
    }
    static EM_BOOL OnTouch(int type, const EmscriptenTouchEvent* t, void* user) {
        WebBackend& b = *static_cast<WebBackend*>(user);
        WebWindow* w = b.m_window;
        b.m_sawTouch = true;
        const double cw = w ? w->CssW() : 0, ch = w ? w->CssH() : 0;
        const int n = t->numTouches < 0 ? 0 : t->numTouches > 32 ? 32 : t->numTouches;
        for (int i = 0; i < n; ++i) {
            const EmscriptenTouchPoint& p = t->touches[i];
            if (!p.isChanged) continue;
            Event e;
            e.timestampMs = b.Now(); e.windowId = 1;
            e.type = type == EMSCRIPTEN_EVENT_TOUCHSTART ? EventType::FingerDown
                   : type == EMSCRIPTEN_EVENT_TOUCHMOVE ? EventType::FingerMotion : EventType::FingerUp;
            e.fingerId = (long long)p.identifier;
            e.fx = web::Fraction((double)p.targetX, cw);
            e.fy = web::Fraction((double)p.targetY, ch);
            b.m_queue.push_back(e);
            // The first finger also works the mouse (left button), so menus and buttons answer to touch.
            const bool primary = (b.m_touchMouse < 0 && e.type == EventType::FingerDown) || b.m_touchMouse == (long long)p.identifier;
            if (!primary) continue;
            Event m;
            m.timestampMs = e.timestampMs; m.windowId = 1; m.x = (int)p.targetX; m.y = (int)p.targetY;
            m.dx = m.x - b.m_mx; m.dy = m.y - b.m_my;
            b.m_mx = m.x; b.m_my = m.y;
            if (e.type == EventType::FingerDown) {
                b.m_touchMouse = (long long)p.identifier;
                Event mv = m; mv.type = EventType::MouseMove; b.m_queue.push_back(mv);
                m.type = EventType::MouseButtonDown; m.button = MouseLeft; m.clicks = 1;
                b.m_buttons |= MouseMask(MouseLeft);
            } else if (e.type == EventType::FingerMotion) {
                m.type = EventType::MouseMove;
            } else {
                b.m_touchMouse = -1;
                m.type = EventType::MouseButtonUp; m.button = MouseLeft; m.clicks = 1;
                b.m_buttons &= ~MouseMask(MouseLeft);
            }
            b.m_queue.push_back(m);
        }
        return EM_TRUE;   // no mouse events made up by the browser after these, and no page pan or zoom
    }
    static EM_BOOL OnFocus(int type, const EmscriptenFocusEvent*, void* user) {
        WebBackend& b = *static_cast<WebBackend*>(user);
        const bool gained = type == EMSCRIPTEN_EVENT_FOCUS;
        if (gained == b.m_focused) return EM_FALSE;
        b.m_focused = gained;
        if (!gained) {   // keys held while the page lost the focus never send their release: let them go now
            for (int k = 1; k < kKeyCount; ++k) {
                if (!b.m_keys[k]) continue;
                b.m_keys[k] = false;
                Event up; up.type = EventType::KeyUp; up.timestampMs = b.Now(); up.windowId = 1; up.key = (Key)k;
                b.m_queue.push_back(up);
            }
            b.m_buttons = 0;
        }
        Event e;
        e.type = gained ? EventType::WindowFocusGained : EventType::WindowFocusLost;
        e.timestampMs = b.Now(); e.windowId = 1;
        b.m_queue.push_back(e);
        return EM_FALSE;
    }

    double m_t0 = emscripten_get_now();
    bool m_callbacks = false, m_pumped = false, m_text = false, m_focused = true, m_mouseIn = false;
    bool m_cursorShown = true, m_sawTouch = false;
    CursorShape m_shape = CursorShape::Arrow;
    WebWindow* m_window = nullptr;
    std::deque<Event> m_queue;
    bool m_keys[kKeyCount] = {};
    web::LayoutMap m_layout;
    int m_mx = 0, m_my = 0, m_gx = 0, m_gy = 0, m_relX = 0, m_relY = 0;
    uint32_t m_buttons = 0;
    int m_lastBtn = 0, m_clicks = 0, m_lastX = 0, m_lastY = 0;
    uint32_t m_lastDownMs = 0;
    long long m_touchMouse = -1;
    int m_nextAudio = 1;
};

WebWindow::~WebWindow() {
    if (BackendsShutDown()) return;   // the end of the process: nothing to give back
    DestroyGLContext();
    m_b.WindowGone(this);
}
bool WebWindow::IsFocused() const { return m_b.Focused(); }
bool WebWindow::HasMouseFocus() const { return m_b.MouseIn(); }

} // namespace

std::unique_ptr<Backend> CreateOwnBackend() { return std::unique_ptr<Backend>(new WebBackend()); }

} // namespace okay::platform

/// Called by the page's ScriptProcessorNode (okay_web_audio_open): `frames` interleaved float frames of device `id`.
extern "C" EMSCRIPTEN_KEEPALIVE float* okay_web_audio_pull(int id, int frames) {
    for (okay::platform::WebAudioDevice* d : okay::platform::AudioDevices())
        if (d && d->IdNum() == id) return d->Pull(frames);
    return nullptr;
}

#endif // __EMSCRIPTEN__
