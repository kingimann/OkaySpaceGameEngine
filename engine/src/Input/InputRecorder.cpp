#include "okay/Input/InputRecorder.hpp"
#include "okay/Input/Input.hpp"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace okay {

namespace {

struct PadFrame { bool connected = false; Vec2 axis, right; float lt = 0, rt = 0; unsigned mask = 0; };
struct Frame {
    float dt = 0;
    std::vector<unsigned char> keys;       // held key codes
    Vec2 mouse; unsigned mouseMask = 0; float wheel = 0;
    PadFrame pads[Input::kMaxGamepads];
};

std::vector<Frame> g_frames;
bool g_recording = false, g_playing = false, g_finished = false;
std::size_t g_at = 0;
std::uint64_t g_seed = 0;
std::string g_note;

} // namespace

// The recorder reads and writes Input's own state; Input names it a friend.
struct InputRecorderAccess {
    static void Capture(Frame& f) {
        // (a letter held only because its arrow is is not recorded: Apply adds it again, as FeedKeys does)
        for (int i = 0; i < 256; ++i)
            if (Input::s_current[(std::size_t)i] && !Input::s_aliasOnly[(std::size_t)i]) f.keys.push_back((unsigned char)i);
        f.mouse = Input::s_mousePos; f.mouseMask = Input::s_mouseCurrent; f.wheel = Input::s_mouseWheel;
        for (int p = 0; p < Input::kMaxGamepads; ++p) {
            const Input::PadState& s = Input::s_pads[p];
            f.pads[p] = {s.connected, s.axis, s.right, s.trigger[0], s.trigger[1], s.current};
        }
    }
    static void Apply(const Frame& f, const Frame* before) {
        Input::s_previous.fill(false);
        if (before) for (unsigned char k : before->keys) Input::s_previous[k] = true;
        {   // the arrows' W A S D letters, as Apply adds them below
            const unsigned char arrowLetter[4][2] = {{(unsigned char)Input::KeyUp, 'w'}, {(unsigned char)Input::KeyLeft, 'a'},
                                                     {(unsigned char)Input::KeyDown, 's'}, {(unsigned char)Input::KeyRight, 'd'}};
            for (const auto& al : arrowLetter) if (Input::s_previous[al[0]]) Input::s_previous[al[1]] = true;
        }
        Input::s_current.fill(false);
        for (unsigned char k : f.keys) Input::s_current[k] = true;
        Input::ApplyArrowAliases();
        Input::s_mousePrevPos = before ? before->mouse : f.mouse;
        Input::s_mousePos = f.mouse; Input::s_mouseFed = true;
        Input::s_mousePrevious = before ? before->mouseMask : 0u;
        Input::s_mouseCurrent = f.mouseMask;
        Input::s_mouseWheel = f.wheel;
        for (int p = 0; p < Input::kMaxGamepads; ++p) {
            Input::PadState& s = Input::s_pads[p];
            const PadFrame& pf = f.pads[p];
            s.connected = pf.connected; s.axis = pf.axis; s.right = pf.right;
            s.trigger[0] = pf.lt; s.trigger[1] = pf.rt;
            s.previous = before ? before->pads[p].mask : 0u;
            s.current = pf.mask;
        }
    }
};

void InputRecorder::Clear() { g_frames.clear(); g_at = 0; g_finished = false; }
void InputRecorder::StartRecording() { g_playing = false; g_recording = true; Clear(); }
void InputRecorder::StopRecording() { g_recording = false; }
bool InputRecorder::Recording() { return g_recording; }
std::size_t InputRecorder::FrameCount() { return g_frames.size(); }
std::size_t InputRecorder::PlaybackFrame() { return g_at; }
bool InputRecorder::Playing() { return g_playing; }
bool InputRecorder::Finished() { return g_finished; }
void InputRecorder::StopPlayback() { g_playing = false; g_finished = false; }
void InputRecorder::SetSeed(std::uint64_t s) { g_seed = s; }
std::uint64_t InputRecorder::Seed() { return g_seed; }
void InputRecorder::SetNote(const std::string& n) { g_note = n; }
std::string InputRecorder::Note() { return g_note; }

void InputRecorder::BeginFrame(float) {
    if (!g_playing) return;
    if (g_at >= g_frames.size()) { g_finished = true; g_playing = false; return; }
    InputRecorderAccess::Apply(g_frames[g_at], g_at > 0 ? &g_frames[g_at - 1] : nullptr);
    ++g_at;
    if (g_at >= g_frames.size()) g_finished = true;
}

void InputRecorder::EndFrame(float dt) {
    if (!g_recording) return;
    Frame f; f.dt = dt;
    InputRecorderAccess::Capture(f);
    g_frames.push_back(std::move(f));
}

std::string InputRecorder::Text() {
    std::ostringstream o;
    o << "okayinputrec 1\nseed " << g_seed << "\nnote " << g_note << "\nframes " << g_frames.size() << "\n";
    char buf[160];
    for (const Frame& f : g_frames) {
        std::snprintf(buf, sizeof buf, "f %g m %g %g %u %g k", f.dt, f.mouse.x, f.mouse.y, f.mouseMask, f.wheel);
        o << buf;
        for (unsigned char k : f.keys) o << ' ' << (int)k;
        for (int p = 0; p < Input::kMaxGamepads; ++p) {
            const PadFrame& pf = f.pads[p];
            if (!pf.connected && pf.mask == 0) continue;
            std::snprintf(buf, sizeof buf, " | p%d %d %g %g %g %g %g %g %u", p, pf.connected ? 1 : 0, pf.axis.x, pf.axis.y,
                          pf.right.x, pf.right.y, pf.lt, pf.rt, pf.mask);
            o << buf;
        }
        o << "\n";
    }
    return o.str();
}

bool InputRecorder::StartPlayback(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("okayinputrec ", 0) != 0 || std::atoi(line.c_str() + 13) != 1) return false;
    std::vector<Frame> frames;
    std::uint64_t seed = 0; std::string note;
    while (std::getline(in, line)) {
        if (line.rfind("seed ", 0) == 0) { seed = std::strtoull(line.c_str() + 5, nullptr, 10); continue; }
        if (line.rfind("note ", 0) == 0) { note.assign(line, 5, std::string::npos); continue; }
        if (line.rfind("f ", 0) != 0) continue;
        Frame f;
        const std::size_t bar = line.find('|');
        std::istringstream head(line.substr(0, bar));
        std::string tag, mtag, ktag;
        head >> tag >> f.dt >> mtag >> f.mouse.x >> f.mouse.y >> f.mouseMask >> f.wheel >> ktag;
        if (mtag != "m" || ktag != "k") return false;
        int k;
        while (head >> k) if (k >= 0 && k < 256) f.keys.push_back((unsigned char)k);
        std::size_t at = bar;
        while (at != std::string::npos) {
            const std::size_t next = line.find('|', at + 1);
            std::istringstream ps(line.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1));
            std::string pt; int conn = 0;
            PadFrame pf;
            if (ps >> pt >> conn >> pf.axis.x >> pf.axis.y >> pf.right.x >> pf.right.y >> pf.lt >> pf.rt >> pf.mask && pt.size() == 2 && pt[0] == 'p') {
                const int idx = pt[1] - '0';
                pf.connected = conn != 0;
                if (idx >= 0 && idx < Input::kMaxGamepads) f.pads[idx] = pf;
            }
            at = next;
        }
        frames.push_back(std::move(f));
    }
    g_frames = std::move(frames);
    g_seed = seed; g_note = note;
    g_recording = false; g_playing = !g_frames.empty(); g_finished = false; g_at = 0;
    return !g_frames.empty();
}

} // namespace okay
