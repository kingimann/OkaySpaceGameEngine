// Own Linux backend, game controllers: the kernel's evdev interface
// (/dev/input/event*), no library. A controller is any event node with a
// BTN_GAMEPAD button and an ABS_X axis. Buttons and axes follow the layout the
// Linux gamepad API documents (what xpad and the usual HID drivers report).
#include "OwnLinux.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <mutex>
#include <set>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace okay::platform::own {
namespace {

bool TestBit(const unsigned long* bits, int bit) {
    return (bits[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1ul;
}

std::vector<std::string> ScanEventNodes() {
    std::vector<std::string> out;
    DIR* d = opendir("/dev/input");
    if (!d) return out;
    while (dirent* e = readdir(d))
        if (std::strncmp(e->d_name, "event", 5) == 0) out.push_back(std::string("/dev/input/") + e->d_name);
    closedir(d);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;   // event2 before event10
    });
    return out;
}

bool LooksLikeGamepad(const std::string& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    unsigned long ev[(EV_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
    unsigned long key[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
    unsigned long abs[(ABS_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
    bool ok = false;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) >= 0 && TestBit(ev, EV_KEY) && TestBit(ev, EV_ABS) &&
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) >= 0 && ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) >= 0)
        ok = TestBit(key, BTN_GAMEPAD) && TestBit(abs, ABS_X);
    close(fd);
    return ok;
}

constexpr int kBtnSlots = 512;   // BTN_MISC (0x100) .. 0x2ff: pad buttons, triggers and the d-pad buttons
class EvdevPad;
std::mutex g_mu;
std::vector<EvdevPad*> g_open;   // pads currently open, for hotplug bookkeeping
int g_nextInstance = 1;

class EvdevPad final : public Gamepad {
public:
    EvdevPad(int fd, const EvdevCaps& caps, std::string name, std::string path)
        : m_fd(fd), m_caps(caps), m_name(std::move(name)), m_path(std::move(path)) {
        std::lock_guard<std::mutex> lk(g_mu);
        m_id = g_nextInstance++;
        g_open.push_back(this);
    }
    ~EvdevPad() override {
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_open.erase(std::remove(g_open.begin(), g_open.end(), this), g_open.end());
        }
        if (m_ffId >= 0) ioctl(m_fd, EVIOCRMFF, m_ffId);
        if (m_fd >= 0) close(m_fd);
    }
    int InstanceId() const override { return m_id; }
    const char* Name() const override { return m_name.c_str(); }
    int16_t Axis(GamepadAxis a) const override {
        const_cast<EvdevPad*>(this)->Pump();
        auto norm = [&](int code, bool trigger) -> int16_t {
            const EvdevAbsRange& r = m_caps.abs[code];
            if (!r.present || r.max == r.min) return 0;
            const double t = (double)(m_abs[code] - r.min) / (double)(r.max - r.min);   // 0..1
            return trigger ? (int16_t)(t * 32767.0 + 0.5) : (int16_t)(t * 65535.0 - 32768.0 + (t > 0.5 ? 0.5 : -0.5));
        };
        switch (a) {
        case GamepadAxis::LeftX: return norm(ABS_X, false);
        case GamepadAxis::LeftY: return norm(ABS_Y, false);
        case GamepadAxis::RightX: return norm(ABS_RX, false);
        case GamepadAxis::RightY: return norm(ABS_RY, false);
        case GamepadAxis::TriggerLeft:
            if (m_caps.abs[ABS_Z].present) return norm(ABS_Z, true);
            return m_btn[BTN_TL2 - BTN_MISC] ? 32767 : 0;
        case GamepadAxis::TriggerRight:
            if (m_caps.abs[ABS_RZ].present) return norm(ABS_RZ, true);
            return m_btn[BTN_TR2 - BTN_MISC] ? 32767 : 0;
        }
        return 0;
    }
    bool Button(GamepadButton b) const override {
        const_cast<EvdevPad*>(this)->Pump();
        auto k = [&](int code) { return m_btn[code - BTN_MISC]; };
        switch (b) {
        case GamepadButton::A: return k(BTN_SOUTH);
        case GamepadButton::B: return k(BTN_EAST);
        case GamepadButton::X: return k(BTN_NORTH);
        case GamepadButton::Y: return k(BTN_WEST);
        case GamepadButton::Back: return k(BTN_SELECT);
        case GamepadButton::Guide: return k(BTN_MODE);
        case GamepadButton::Start: return k(BTN_START);
        case GamepadButton::LeftStick: return k(BTN_THUMBL);
        case GamepadButton::RightStick: return k(BTN_THUMBR);
        case GamepadButton::LeftShoulder: return k(BTN_TL);
        case GamepadButton::RightShoulder: return k(BTN_TR);
        case GamepadButton::DpadUp: return k(BTN_DPAD_UP) || m_abs[ABS_HAT0Y] < 0;
        case GamepadButton::DpadDown: return k(BTN_DPAD_DOWN) || m_abs[ABS_HAT0Y] > 0;
        case GamepadButton::DpadLeft: return k(BTN_DPAD_LEFT) || m_abs[ABS_HAT0X] < 0;
        case GamepadButton::DpadRight: return k(BTN_DPAD_RIGHT) || m_abs[ABS_HAT0X] > 0;
        }
        return false;
    }
    bool Rumble(uint16_t lo, uint16_t hi, uint32_t ms) override {
        if (!m_caps.canRumble || m_fd < 0) return false;
        struct ff_effect fx;
        std::memset(&fx, 0, sizeof(fx));
        fx.type = FF_RUMBLE;
        fx.id = m_ffId;   // -1 first time: the kernel picks one; later it updates in place
        fx.u.rumble.strong_magnitude = lo;
        fx.u.rumble.weak_magnitude = hi;
        fx.replay.length = (uint16_t)std::min<uint32_t>(ms, 0xFFFF);
        if (ioctl(m_fd, EVIOCSFF, &fx) < 0) return false;
        m_ffId = fx.id;
        struct input_event play;
        std::memset(&play, 0, sizeof(play));
        play.type = EV_FF; play.code = (uint16_t)m_ffId; play.value = 1;
        return write(m_fd, &play, sizeof(play)) == (ssize_t)sizeof(play);
    }

    bool TakeGone() { if (!m_gone || m_reported) return false; m_reported = true; return true; }
    const std::string& Path() const { return m_path; }

    /// Drain the fd without blocking and fold the events into the state.
    void Pump() {
        if (m_fd < 0 || m_gone) return;
        struct input_event ev[32];
        for (;;) {
            const ssize_t n = read(m_fd, ev, sizeof(ev));
            if (n < 0) { if (errno == ENODEV || errno == EIO) m_gone = true; return; }   // EAGAIN: drained
            if (n == 0) { m_gone = true; return; }                                         // EOF (a closed pipe)
            for (size_t i = 0; i < (size_t)n / sizeof(ev[0]); ++i) {
                if (ev[i].type == EV_KEY && ev[i].code >= BTN_MISC && ev[i].code < BTN_MISC + kBtnSlots) m_btn[ev[i].code - BTN_MISC] = ev[i].value != 0;
                else if (ev[i].type == EV_ABS && ev[i].code < 64) m_abs[ev[i].code] = ev[i].value;
            }
        }
    }

    void InitState(int fdForQuery) {   // pick up the state at open time
        for (int c = 0; c < 64; ++c) if (m_caps.abs[c].present) {
            struct input_absinfo ai;
            if (fdForQuery >= 0 && ioctl(fdForQuery, EVIOCGABS(c), &ai) >= 0) m_abs[c] = ai.value;
            else m_abs[c] = (m_caps.abs[c].min + m_caps.abs[c].max) / 2;
        }
        if (fdForQuery >= 0) {
            unsigned long keys[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
            if (ioctl(fdForQuery, EVIOCGKEY(sizeof(keys)), keys) >= 0)
                for (int c = BTN_MISC; c < BTN_MISC + kBtnSlots; ++c) m_btn[c - BTN_MISC] = TestBit(keys, c);
        } else {
            // no device to ask: a pipe test starts with every stick centred, which InitState set above
        }
    }

private:
    int m_fd;
    EvdevCaps m_caps;
    std::string m_name, m_path;
    int m_id = 0;
    int m_ffId = -1;
    bool m_gone = false, m_reported = false;
    int m_abs[64] = {};
    bool m_btn[kBtnSlots] = {};
};

EvdevCaps ProbeCaps(int fd) {
    EvdevCaps c;
    unsigned long key[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
    unsigned long ff[(FF_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
    for (int a = 0; a < 64; ++a) {
        struct input_absinfo ai;
        if (ioctl(fd, EVIOCGABS(a), &ai) >= 0 && ai.maximum != ai.minimum) { c.abs[a].present = true; c.abs[a].min = ai.minimum; c.abs[a].max = ai.maximum; }
    }
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) >= 0) c.hasTriggerButtons = TestBit(key, BTN_TL2) && TestBit(key, BTN_TR2);
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff)), ff) >= 0) c.canRumble = TestBit(ff, FF_RUMBLE);
    return c;
}

std::vector<std::string> GamepadNodes() {
    std::vector<std::string> out;
    for (const std::string& p : ScanEventNodes()) if (LooksLikeGamepad(p)) out.push_back(p);
    return out;
}

} // namespace

int EvdevGamepadCount() { return (int)GamepadNodes().size(); }
std::string EvdevGamepadPath(int index) {
    const auto v = GamepadNodes();
    return index >= 0 && index < (int)v.size() ? v[(size_t)index] : std::string();
}

std::unique_ptr<Gamepad> OpenEvdevGamepad(int index) {
    const std::string path = EvdevGamepadPath(index);
    if (path.empty()) return nullptr;
    const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    const int fdr = fd >= 0 ? fd : open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);   // no rumble without write access
    if (fdr < 0) return nullptr;
    char name[128] = {};
    if (ioctl(fdr, EVIOCGNAME(sizeof(name) - 1), name) < 0) std::snprintf(name, sizeof(name), "Gamepad");
    EvdevCaps caps = ProbeCaps(fdr);
    if (fd < 0) caps.canRumble = false;
    auto* p = new EvdevPad(fdr, caps, name, path);
    p->InitState(fdr);
    return std::unique_ptr<Gamepad>(p);
}

std::unique_ptr<Gamepad> OpenEvdevFromFd(int fd, const EvdevCaps& caps, const char* name) {
    auto* p = new EvdevPad(fd, caps, name ? name : "test pad", "");
    p->InitState(-1);
    return std::unique_ptr<Gamepad>(p);
}

void EvdevPollHotplug(std::vector<Event>& out) {
    static std::set<std::string> known;
    static bool first = true;
    static timespec last = {0, 0};
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const long long dtMs = (long long)(now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000;

    // A pad whose device vanished (ENODEV on read) is reported at once.
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (EvdevPad* p : g_open) {
            p->Pump();
            if (p->TakeGone()) { Event e; e.type = EventType::GamepadRemoved; e.deviceId = p->InstanceId(); out.push_back(e); }
        }
    }
    if (!first && dtMs < 1000) return;
    first = false;
    last = now;
    const auto nodes = GamepadNodes();
    std::set<std::string> cur(nodes.begin(), nodes.end());
    for (size_t i = 0; i < nodes.size(); ++i)
        if (!known.count(nodes[i])) { Event e; e.type = EventType::GamepadAdded; e.deviceId = (int)i; out.push_back(e); }
    known = std::move(cur);
}

} // namespace okay::platform::own
